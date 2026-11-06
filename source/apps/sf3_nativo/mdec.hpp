#pragma once

#include "sf/psx/machine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

class SoftMdec final : public sf::psx::GpuMmio {
 public:
  class InPort final : public sf::psx::DmaPort {
   public:
    explicit InPort(SoftMdec& m) : m_(m) {}
    bool dmaRequest() const noexcept override { return true; }
    bool readDmaWord(std::uint32_t&) noexcept override { return false; }
    bool writeDmaWord(std::uint32_t v) noexcept override { m_.writeData(v); return true; }
   private:
    SoftMdec& m_;
  };
  class OutPort final : public sf::psx::DmaPort {
   public:
    explicit OutPort(SoftMdec& m) : m_(m) {}
    bool dmaRequest() const noexcept override { return !m_.out_.empty(); }
    bool readDmaWord(std::uint32_t& v) noexcept override {
      if (m_.out_.empty()) { v = 0; return true; }
      v = m_.out_.front(); m_.out_.pop_front(); return true;
    }
    bool writeDmaWord(std::uint32_t) noexcept override { return false; }
   private:
    SoftMdec& m_;
  };

  InPort in{*this};
  OutPort out{*this};
  std::uint64_t frames = 0, blocks = 0;

  std::uint32_t readGpu(std::uint32_t offset) noexcept override {
    if (offset == 0) {
      if (out_.empty()) return 0;
      auto v = out_.front(); out_.pop_front(); return v;
    }
    std::uint32_t s = 0;
    if (out_.empty()) s |= 1u << 31;
    if (remaining_ > 0 || !out_.empty()) s |= 1u << 29;
    if (enable_in_ && remaining_ > 0) s |= 1u << 28;
    if (enable_out_ && !out_.empty()) s |= 1u << 27;
    s |= (depth_ & 3u) << 25;
    if (signed_) s |= 1u << 24;
    if (bit15_) s |= 1u << 23;
    s |= remaining_ ? ((remaining_ - 1) & 0xffffu) : 0xffffu;
    return s;
  }

  void writeGpu(std::uint32_t offset, std::uint32_t value) noexcept override {
    if (offset == 0) { writeData(value); return; }
    if (value & (1u << 31)) {
      remaining_ = 0; cmd_ = 0; params_.clear(); out_.clear();
    }
    enable_in_ = (value >> 30) & 1u;
    enable_out_ = (value >> 29) & 1u;
  }

 private:
  void writeData(std::uint32_t w) {
    if (remaining_ == 0) {
      cmd_ = w >> 29;
      depth_ = (w >> 27) & 3u;
      signed_ = (w >> 26) & 1u;
      bit15_ = (w >> 25) & 1u;
      params_.clear();
      switch (cmd_) {
        case 1: remaining_ = w & 0xffffu; break;
        case 2: color_quant_ = w & 1u; remaining_ = color_quant_ ? 32 : 16; break;
        case 3: remaining_ = 32; break;
        default: remaining_ = 0; break;
      }
      if (remaining_ == 0 && cmd_ == 1) finish();
      return;
    }
    params_.push_back(w);
    if (--remaining_ == 0) finish();
  }

  void finish() {
    if (cmd_ == 2) {
      for (int i = 0; i < 64; ++i) qt_y_[i] = std::uint8_t(params_[i / 4] >> ((i & 3) * 8));
      if (color_quant_)
        for (int i = 0; i < 64; ++i) qt_c_[i] = std::uint8_t(params_[16 + i / 4] >> ((i & 3) * 8));
    } else if (cmd_ == 3) {
      for (int i = 0; i < 64; ++i) scale_[i] = std::int16_t(params_[i / 2] >> ((i & 1) * 16));
    } else if (cmd_ == 1) {
      decodeStream();
      ++frames;
    }
    params_.clear();
  }

  static int signed10(int v) { return (v & 0x200) ? v - 0x400 : v; }

  bool decodeBlock(std::array<int, 64>& blk, const std::uint8_t* qt) {
    blk.fill(0);
    std::uint16_t n;
    do {
      if (pos_ >= half_.size()) return false;
      n = half_[pos_++];
    } while (n == 0xfe00);
    int k = 0;
    const int q = (n >> 10) & 0x3f;
    int val = signed10(n & 0x3ff) * qt[0];
    for (;;) {
      if (q == 0) val = signed10(n & 0x3ff) * 2;
      val = std::clamp(val, -0x400, 0x3ff);
      if (q > 0) blk[zagzig()[k]] = val; else blk[k] = val;
      if (pos_ >= half_.size()) break;
      n = half_[pos_++];
      k += ((n >> 10) & 0x3f) + 1;
      if (k > 63) break;
      val = (signed10(n & 0x3ff) * qt[k] * q + 4) / 8;
    }
    idct(blk);
    return true;
  }

  void idct(std::array<int, 64>& blk) const {
    std::array<std::int64_t, 64> tmp{};
    for (int x = 0; x < 8; ++x)
      for (int y = 0; y < 8; ++y) {
        std::int64_t sum = 0;
        for (int z = 0; z < 8; ++z) sum += std::int64_t(blk[y + z * 8]) * scale_[x + z * 8];
        tmp[x + y * 8] = (sum + 0x8000) >> 16;
      }
    for (int x = 0; x < 8; ++x)
      for (int y = 0; y < 8; ++y) {
        std::int64_t sum = 0;
        for (int z = 0; z < 8; ++z) sum += tmp[y + z * 8] * scale_[x + z * 8];
        blk[x + y * 8] = int((sum + 0x8000) >> 16);
      }
  }

  static const std::array<int, 64>& zagzig() {
    static const std::array<int, 64> t = [] {
      static const int zz[64] = {0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42,
                                 3,  8,  12, 17, 25, 30, 41, 43, 9,  11, 18, 24, 31, 40, 44, 53,
                                 10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
                                 21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};
      std::array<int, 64> r{};
      for (int i = 0; i < 64; ++i) r[zz[i]] = i;
      return r;
    }();
    return t;
  }

  void decodeStream() {
    half_.clear();
    for (auto w : params_) { half_.push_back(std::uint16_t(w)); half_.push_back(std::uint16_t(w >> 16)); }
    pos_ = 0;
    const bool color = depth_ >= 2;
    std::array<int, 64> cr{}, cb{}, y{};
    std::vector<std::uint8_t> bytes;
    while (pos_ < half_.size()) {
      if (color) {
        std::array<int, 16 * 16 * 3> rgb{};
        if (!decodeBlock(cr, qt_c_.data()) || !decodeBlock(cb, qt_c_.data())) break;
        bool ok = true;
        for (int b = 0; b < 4 && ok; ++b) {
          if (!decodeBlock(y, qt_y_.data())) { ok = false; break; }
          const int xx = (b & 1) * 8, yy = (b >> 1) * 8;
          for (int py = 0; py < 8; ++py)
            for (int px = 0; px < 8; ++px) {
              double R0, B0;
              if (smooth_chroma_) {

                const double fx = (px + xx) * 0.5 - 0.25, fy = (py + yy) * 0.5 - 0.25;
                const int ix = std::clamp(int(std::floor(fx)), 0, 7), iy = std::clamp(int(std::floor(fy)), 0, 7);
                const int jx = std::min(ix + 1, 7), jy = std::min(iy + 1, 7);
                const double ax = std::clamp(fx - ix, 0.0, 1.0), ay = std::clamp(fy - iy, 0.0, 1.0);
                auto lerp2 = [&](const std::array<int, 64>& c) {
                  const double t = c[ix + iy * 8] * (1 - ax) + c[jx + iy * 8] * ax;
                  const double b2 = c[ix + jy * 8] * (1 - ax) + c[jx + jy * 8] * ax;
                  return t * (1 - ay) + b2 * ay;
                };
                R0 = lerp2(cr); B0 = lerp2(cb);
              } else {
                const int ci = ((px + xx) >> 1) + ((py + yy) >> 1) * 8;
                R0 = cr[ci]; B0 = cb[ci];
              }
              const int G = int(std::lround(-0.3437 * B0 - 0.7143 * R0)), R = int(std::lround(1.402 * R0)), B = int(std::lround(1.772 * B0));
              const int Y = y[px + py * 8];
              auto c = [&](int v) { v = std::clamp(v, -128, 127); return signed_ ? v & 0xff : (v + 128) & 0xff; };
              const int o = ((px + xx) + (py + yy) * 16) * 3;
              rgb[o] = c(Y + R); rgb[o + 1] = c(Y + G); rgb[o + 2] = c(Y + B);
            }
        }
        if (!ok) break;
        ++blocks;
        if (depth_ == 2) {
          for (int i = 0; i < 16 * 16 * 3; ++i) bytes.push_back(std::uint8_t(rgb[i]));
        } else {
          for (int i = 0; i < 256; ++i) {
            const std::uint16_t p = std::uint16_t((rgb[i * 3] >> 3) | ((rgb[i * 3 + 1] >> 3) << 5) |
                                                  ((rgb[i * 3 + 2] >> 3) << 10) | (bit15_ ? 0x8000 : 0));
            bytes.push_back(std::uint8_t(p)); bytes.push_back(std::uint8_t(p >> 8));
          }
        }
      } else {
        if (!decodeBlock(y, qt_y_.data())) break;
        ++blocks;
        auto c = [&](int v) { v = std::clamp(v, -128, 127); return signed_ ? v & 0xff : (v + 128) & 0xff; };
        if (depth_ == 1) for (int i = 0; i < 64; ++i) bytes.push_back(std::uint8_t(c(y[i])));
        else for (int i = 0; i < 64; i += 2) bytes.push_back(std::uint8_t((c(y[i]) >> 4) | ((c(y[i + 1]) >> 4) << 4)));
      }
    }
    while (bytes.size() % 4) bytes.push_back(0);
    for (std::size_t i = 0; i < bytes.size(); i += 4)
      out_.push_back(std::uint32_t(bytes[i]) | std::uint32_t(bytes[i + 1]) << 8 | std::uint32_t(bytes[i + 2]) << 16 |
                     std::uint32_t(bytes[i + 3]) << 24);
  }

  bool smooth_chroma_ = [] { const char* e = std::getenv("SF3N_MDEC_PS1"); return !(e && std::atoi(e) != 0); }();
  std::uint32_t cmd_ = 0, depth_ = 0, remaining_ = 0;
  bool signed_ = false, bit15_ = false, color_quant_ = false, enable_in_ = false, enable_out_ = false;
  std::vector<std::uint32_t> params_;
  std::vector<std::uint16_t> half_;
  std::size_t pos_ = 0;
  std::deque<std::uint32_t> out_;
  std::array<std::uint8_t, 64> qt_y_{}, qt_c_{};
  std::array<std::int16_t, 64> scale_{};
};
