#pragma once
#include "pakfs.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <unordered_map>
#include "sf/psx/spu.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

class VideosEs {
public:
  static constexpr std::size_t kData = 2324;
  bool open(const std::string& path, std::uint32_t movieLba, std::uint32_t movieSectors, FILE* log) {
    auto fp = pakfs::open(path);
    if (!fp) return false;
    auto& f = *fp;
    char hdr[16];
    if (!f.read(hdr, 16) || std::memcmp(hdr, "SF3VIDEO", 8)) return false;
    std::uint32_t n; std::memcpy(&n, hdr + 12, 4);
    std::vector<std::uint32_t> rel(n);
    std::vector<std::uint8_t> data(std::size_t(n) * kData);
    if (!f.read(reinterpret_cast<char*>(rel.data()), std::streamsize(n) * 4)) return false;
    if (!f.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) return false;

    std::vector<std::size_t> idx(rel_.size() + n);
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    auto relOf = [&](std::size_t i) { return i < rel_.size() ? rel_[i] : rel[i - rel_.size()]; };
    std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return relOf(a) < relOf(b); });
    std::vector<std::uint32_t> nr(idx.size()); std::vector<std::uint8_t> nd(idx.size() * kData);
    for (std::size_t k = 0; k < idx.size(); ++k) {
      const std::size_t i = idx[k]; nr[k] = relOf(i);
      std::memcpy(nd.data() + k * kData, i < rel_.size() ? data_.data() + i * kData : data.data() + (i - rel_.size()) * kData, kData);
    }
    rel_.swap(nr); data_.swap(nd);

    char tag[8];
    if (f.read(tag, 8) && std::memcmp(tag, "MOVS", 4) == 0) {
      std::uint32_t nm; std::memcpy(&nm, tag + 4, 4);
      for (std::uint32_t i = 0; i < nm; ++i) {
        std::uint8_t l = 0; char name[256]{};
        if (!f.read(reinterpret_cast<char*>(&l), 1) || !f.read(name, l)) break;
        std::uint32_t k = 0; if (!f.read(reinterpret_cast<char*>(&k), 4)) break;
        std::vector<std::uint32_t> au(k);
        if (!f.read(reinterpret_cast<char*>(au.data()), std::streamsize(k) * 4)) break;
        const std::uint16_t mi = std::uint16_t(movieNames_.size());
        movieNames_.emplace_back(name, l);
        for (std::uint32_t j = 0; j < k; ++j) movieOf_[au[j]] = {mi, j};
      }
    }
    lba_ = movieLba; sectors_ = movieSectors; active_ = true;
    if (log) std::fprintf(log, "videos castellano: %u sectores de audio de %s\n", n, path.c_str());
    return true;
  }
  bool active() const { return active_; }
  bool has(std::uint32_t lba) const {
    if (!active_ || lba < lba_ || lba - lba_ >= sectors_) return false;
    return std::binary_search(rel_.begin(), rel_.end(), lba - lba_);
  }

  void loadOwn(const std::string& dir, FILE* log) {
    for (std::size_t i = 0; i < movieNames_.size(); ++i) {
      const std::string p = dir + "/" + movieNames_[i] + ".wav";
      std::ifstream f(p, std::ios::binary);
      if (!f) continue;
      std::vector<char> d((std::istreambuf_iterator<char>(f)), {});

      std::size_t o = 12; std::uint16_t ch = 0, bits = 0; std::uint32_t rate = 0;
      while (o + 8 <= d.size()) {
        std::uint32_t sz; std::memcpy(&sz, d.data() + o + 4, 4);
        if (!std::memcmp(d.data() + o, "fmt ", 4) && o + 24 <= d.size()) { std::memcpy(&ch, d.data() + o + 10, 2); std::memcpy(&rate, d.data() + o + 12, 4); std::memcpy(&bits, d.data() + o + 22, 2); }
        if (!std::memcmp(d.data() + o, "data", 4)) {
          if (ch != 2 || bits != 16 || rate != 44100) { if (log) std::fprintf(log, "videos: %s debe ser WAV 44100 Hz, 16 bits, estereo\n", p.c_str()); break; }
          auto& pcm = own_[std::uint16_t(i)];
          pcm.resize(std::min<std::size_t>(sz, d.size() - o - 8) / 4);
          std::memcpy(pcm.data(), d.data() + o + 8, pcm.size() * 4);
          if (log) std::fprintf(log, "videos: audio propio %s (%.1f s)\n", p.c_str(), double(pcm.size()) / 44100.0);
          break;
        }
        o += 8 + sz + (sz & 1);
      }
    }
  }

  void filter(std::uint32_t lba, std::span<sf::psx::SpuPcmFrame> pcm) const {
    if (own_.empty() || lba < lba_ || lba >= lba_ + sectors_) return;
    const auto it = movieOf_.find(lba - lba_);
    if (it == movieOf_.end()) return;
    const auto o = own_.find(it->second.first);
    if (o == own_.end()) return;
    const std::size_t from = std::size_t(it->second.second) * pcm.size();
    for (std::size_t i = 0; i < pcm.size(); ++i) {
      const std::size_t j = from + i;
      if (j < o->second.size()) { pcm[i].left = o->second[j][0]; pcm[i].right = o->second[j][1]; }
      else { pcm[i].left = 0; pcm[i].right = 0; }
    }
  }
  void patchSector(std::uint32_t lba, unsigned char* raw) const {
    if (!active_ || lba < lba_ || lba - lba_ >= sectors_) return;
    if (!(raw[18] & 4)) return;
    const std::uint32_t rel = lba - lba_;
    const auto it = std::lower_bound(rel_.begin(), rel_.end(), rel);
    if (it == rel_.end() || *it != rel) return;
    std::memcpy(raw + 24, data_.data() + std::size_t(it - rel_.begin()) * kData, kData);
  }
private:
  std::vector<std::string> movieNames_;
  std::unordered_map<std::uint32_t, std::pair<std::uint16_t, std::uint32_t>> movieOf_;
  std::unordered_map<std::uint16_t, std::vector<std::array<std::int16_t, 2>>> own_;
  bool active_ = false;
  std::uint32_t lba_ = 0, sectors_ = 0;
  std::vector<std::uint32_t> rel_;
  std::vector<std::uint8_t> data_;
};
