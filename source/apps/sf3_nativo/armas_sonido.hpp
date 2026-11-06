#pragma once
#include "pakfs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sf3wav {

inline const double kAdpcmF[5][2] = {{0.0, 0.0}, {60.0 / 64.0, 0.0}, {115.0 / 64.0, -52.0 / 64.0}, {98.0 / 64.0, -55.0 / 64.0}, {122.0 / 64.0, -60.0 / 64.0}};
inline const int kAdpcmFi[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

inline std::vector<std::int16_t> readWav44k(const std::string& path, int& rate_in, std::string* err = nullptr) {
  rate_in = 0;
  std::vector<std::uint8_t> b;
  if (!pakfs::read(path, b)) { if (err) *err = "no se puede abrir"; return {}; }
  auto r16 = [&](std::size_t o) { return o + 2 <= b.size() ? std::uint32_t(b[o] | (b[o + 1] << 8)) : 0u; };
  auto r32 = [&](std::size_t o) { return r16(o) | (r16(o + 2) << 16); };
  if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) { if (err) *err = "no es un WAV (RIFF/WAVE)"; return {}; }
  int fmt = 0, ch = 0, rate = 0, bits = 0, align = 0;
  std::vector<double> mono;
  for (std::size_t o = 12; o + 8 <= b.size();) {
    const std::uint32_t sz = r32(o + 4);
    if (!std::memcmp(b.data() + o, "fmt ", 4)) {
      fmt = int(r16(o + 8)); ch = int(r16(o + 10)); rate = int(r32(o + 12)); align = int(r16(o + 20)); bits = int(r16(o + 22));
      if (fmt == 0xfffe && sz >= 40) fmt = int(r16(o + 8 + 24));
    } else if (!std::memcmp(b.data() + o, "data", 4)) {
      if (ch < 1 || rate <= 0 || align <= 0 || !((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
        if (err) *err = "formato no soportado (usa PCM 8/16/24/32 bits o float)"; return {};
      }
      const std::size_t avail = std::min<std::size_t>(sz, b.size() - o - 8);
      const std::size_t frames = avail / std::size_t(align);
      const int bps = bits / 8;
      mono.resize(frames);
      for (std::size_t i = 0; i < frames; ++i) {
        double acc = 0;
        for (int c = 0; c < ch; ++c) {
          const std::uint8_t* p = b.data() + o + 8 + i * std::size_t(align) + std::size_t(c * bps);
          double v = 0;
          if (fmt == 3) { float fl; std::memcpy(&fl, p, 4); v = fl; }
          else if (bits == 8) v = (int(p[0]) - 128) / 128.0;
          else if (bits == 16) v = std::int16_t(p[0] | (p[1] << 8)) / 32768.0;
          else if (bits == 24) v = double(std::int32_t((std::uint32_t(p[0]) << 8) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 24)) >> 8) / 8388608.0;
          else { std::int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.0; }
          acc += v;
        }
        mono[i] = acc / ch;
      }
      break;
    }
    o += 8 + sz + (sz & 1);
  }
  if (mono.empty()) { if (err) *err = "WAV sin datos"; return {}; }
  rate_in = rate;
  std::vector<std::int16_t> out;
  if (rate == 44100) {
    out.resize(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) out[i] = std::int16_t(std::clamp(std::lround(mono[i] * 32767.0), -32768L, 32767L));
    return out;
  }

  const double ratio = 44100.0 / rate, cut = std::min(1.0, ratio);
  const int half = 16;
  const double hw = half / cut;
  const std::size_t n = std::size_t(std::ceil(double(mono.size()) * ratio));
  out.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double pos = double(i) / ratio;
    const long c0 = long(std::floor(pos - hw)), c1 = long(std::ceil(pos + hw));
    double acc = 0, ws = 0;
    for (long k = std::max(0L, c0); k <= std::min(long(mono.size()) - 1, c1); ++k) {
      const double x = double(k) - pos;
      if (std::fabs(x) > hw) continue;
      const double t = x * cut;
      const double sinc = std::fabs(t) < 1e-9 ? 1.0 : std::sin(M_PI * t) / (M_PI * t);
      const double wv = 0.42 + 0.5 * std::cos(M_PI * x / hw) + 0.08 * std::cos(2 * M_PI * x / hw);
      acc += mono[std::size_t(k)] * sinc * wv; ws += sinc * wv;
    }
    const double v = ws != 0 ? acc / ws * 1.0 : 0.0;
    out[i] = std::int16_t(std::clamp(std::lround(v * 32767.0), -32768L, 32767L));
  }
  return out;
}

inline std::vector<std::uint8_t> encodeAdpcm(const std::vector<std::int16_t>& pcm) {
  std::vector<std::uint8_t> out(16, 0);
  int h1 = 0, h2 = 0;
  const std::size_t nb = (pcm.size() + 27) / 28;
  for (std::size_t bi = 0; bi < nb; ++bi) {
    int s[28];
    for (int i = 0; i < 28; ++i) { const std::size_t k = bi * 28 + std::size_t(i); s[i] = k < pcm.size() ? pcm[k] : 0; }
    int bestF = 0, bestSh = 0; double bestE = 1e300; std::uint8_t bestN[28]{}; int bH1 = 0, bH2 = 0;
    for (int f = 0; f < 5; ++f)
      for (int sh = 0; sh <= 12; ++sh) {
        int p1 = h1, p2 = h2; double e = 0; std::uint8_t nib[28];
        for (int i = 0; i < 28; ++i) {
          const int pred = (p1 * kAdpcmFi[f][0] + p2 * kAdpcmFi[f][1] + 32) >> 6;
          const int want = s[i] - pred;

          long q = std::lround(double(want) * double(1 << sh) / 4096.0);
          q = std::clamp(q, -8L, 7L);
          const int dec = std::clamp(int((int(q) * 4096) >> sh) + pred, -32768, 32767);
          const double d = double(dec - s[i]); e += d * d;
          nib[i] = std::uint8_t(q & 15);
          p2 = p1; p1 = dec;
          if (e >= bestE) break;
        }
        if (e < bestE) { bestE = e; bestF = f; bestSh = sh; std::memcpy(bestN, nib, 28); bH1 = p1; bH2 = p2; }
      }
    h1 = bH1; h2 = bH2;
    std::uint8_t blk[16]{};
    blk[0] = std::uint8_t((bestF << 4) | bestSh);
    blk[1] = bi == 0 ? 4 : 0;
    for (int i = 0; i < 14; ++i) blk[2 + i] = std::uint8_t(bestN[2 * i] | (bestN[2 * i + 1] << 4));
    out.insert(out.end(), blk, blk + 16);
  }
  std::uint8_t last[16]{}; last[0] = 0x0c; last[1] = 1;
  if (nb == 0) last[1] = 1 | 4;
  out.insert(out.end(), last, last + 16);
  return out;
}

inline std::vector<std::int16_t> decodeAdpcm(const std::uint8_t* d, std::size_t n) {
  std::vector<std::int16_t> out;
  int h1 = 0, h2 = 0;
  for (std::size_t o = 0; o + 16 <= n; o += 16) {
    const int sh = d[o] & 15, f = std::min(4, (d[o] >> 4) & 7);
    for (int i = 0; i < 28; ++i) {
      const int nib = (d[o + 2 + i / 2] >> ((i & 1) * 4)) & 15;
      const int sn = int(std::int16_t(std::uint16_t(nib << 12))) >> (sh > 12 ? 9 : sh);
      const int v = std::clamp(sn + ((h1 * kAdpcmFi[f][0] + h2 * kAdpcmFi[f][1] + 32) >> 6), -32768, 32767);
      out.push_back(std::int16_t(v)); h2 = h1; h1 = v;
    }
    if (d[o + 1] & 1) break;
  }
  return out;
}

inline bool writeWav(const std::string& path, const std::vector<std::int16_t>& pcm, int rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  auto w32 = [&](std::uint32_t v) { std::uint8_t b[4] = {std::uint8_t(v), std::uint8_t(v >> 8), std::uint8_t(v >> 16), std::uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); };
  auto w16 = [&](std::uint16_t v) { std::uint8_t b[2] = {std::uint8_t(v), std::uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); };
  const std::uint32_t data = std::uint32_t(pcm.size() * 2);
  std::fwrite("RIFF", 1, 4, f); w32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
  w32(16); w16(1); w16(1); w32(std::uint32_t(rate)); w32(std::uint32_t(rate * 2)); w16(2); w16(16);
  std::fwrite("data", 1, 4, f); w32(data);
  std::fwrite(pcm.data(), 2, pcm.size(), f);
  std::fclose(f);
  return true;
}

}
