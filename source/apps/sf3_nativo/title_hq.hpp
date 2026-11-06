#pragma once
#include "pakfs.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

struct TitleHq {
  bool active() const { return plm_ != nullptr; }

  bool open(const std::filesystem::path& base, std::uint32_t strLba, std::uint32_t strEnd, int delay, std::FILE* log) {
    {
      std::ifstream f(std::filesystem::path(pakfs::dataPath("videos_propios")) / "TITLE_alta.mpg", std::ios::binary);
      if (f) data_.assign(std::istreambuf_iterator<char>(f), {});
    }
    const bool single = !data_.empty();
    for (int k = 1; k <= 9 && !single; ++k) {
      const auto p = std::filesystem::path(pakfs::dataPath("videos_propios")) / ("TITLE_alta_" + std::to_string(k) + ".mpg");
      std::ifstream f(p, std::ios::binary);
      if (!f) break;
      data_.insert(data_.end(), std::istreambuf_iterator<char>(f), {});
    }
    if (data_.empty()) return false;
    plm_ = plm_create_with_memory(data_.data(), data_.size(), 0);
    if (!plm_) { data_.clear(); return false; }
    plm_set_audio_enabled(plm_, 0);
    w_ = plm_get_width(plm_); h_ = plm_get_height(plm_);
    fps_ = plm_get_framerate(plm_);
    if (w_ <= 0 || h_ <= 0 || fps_ <= 0) { plm_destroy(plm_); plm_ = nullptr; data_.clear(); return false; }
    lba_ = strLba; end_ = strEnd; delay_ = delay;
    rgba_.assign(std::size_t(w_) * h_ * 4, 255);
    if (log) std::fprintf(log, "menu de alta calidad: %dx%d, %.2f cuadros/s, %.1f MB\n", w_, h_, fps_, data_.size() / 1048576.0);
    return true;
  }

  void onSector(std::uint32_t lba, const unsigned char* raw) {
    if (!plm_ || lba < lba_ || lba >= end_) return;
    if (raw[24] != 0x60 || raw[25] != 0x01) return;
    std::uint32_t n = std::uint32_t(raw[32]) | (std::uint32_t(raw[33]) << 8) | (std::uint32_t(raw[34]) << 16) | (std::uint32_t(raw[35]) << 24);
    str_frame_.store(int(n));
    read_tick_.fetch_add(1);
  }

  int psFrame() const { const int n = str_frame_.load() - delay_; return n < 1 ? 1 : n; }
  std::uint32_t readTick() const { return read_tick_.load(); }

  bool frameFor(int n, double nowSec) {
    if (!plm_) return false;
    if (n != last_n_) { last_n_ = n; last_change_ = nowSec; }
    const double t = (n - 1) / 15.0 + std::min(std::max(nowSec - last_change_, 0.0), 1.0 / 15.0 - 1e-4);
    const int want = int(t * fps_ + 1e-6);
    if (want == cur_) return false;
    if (want < cur_ || want > cur_ + 90) {
      plm_seek(plm_, t, 0);
      cur_ = int(plm_get_time(plm_) * fps_ + 0.5) - 1;
    }
    plm_frame_t* fr = nullptr;
    while (cur_ < want) {
      fr = plm_decode_video(plm_);
      if (!fr) { plm_rewind(plm_); cur_ = -1; fr = plm_decode_video(plm_); if (!fr) return false; cur_ = 0; break; }
      ++cur_;
    }
    if (!fr) return false;
    plm_frame_to_rgba(fr, rgba_.data(), w_ * 4);
    return true;
  }
  int w() const { return w_; }
  int h() const { return h_; }
  const std::vector<unsigned char>& rgba() const { return rgba_; }
  ~TitleHq() { if (plm_) plm_destroy(plm_); }

 private:
  std::vector<std::uint8_t> data_;
  std::vector<unsigned char> rgba_;
  plm_t* plm_ = nullptr;
  int w_ = 0, h_ = 0, cur_ = -1, delay_ = 3, last_n_ = -1;
  double last_change_ = 0;
  double fps_ = 0;
  std::uint32_t lba_ = 0, end_ = 0;
  std::atomic<int> str_frame_{0};
  std::atomic<std::uint32_t> read_tick_{0};
};
inline TitleHq g_title_hq;
