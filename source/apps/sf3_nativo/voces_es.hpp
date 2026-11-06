#pragma once
#include "pakfs.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

class VocesEs {
public:
  VocesEs() = default;
  static constexpr std::size_t kLines = 566;

  static constexpr std::array<std::uint32_t, 37> kUsaStarts = {8640, 12000, 14208, 16160, 17888, 19392, 20736, 21984, 23136, 24160, 25120, 26016, 26848, 27648, 28416, 29120, 29760, 30400, 31008, 31584, 32128, 32672, 33216, 33728, 34208, 34688, 35136, 35584, 36032, 36448, 36864, 37248, 37632, 38016, 38368, 38720, 39040};
  bool active() const { return active_; }
  int lines() const { return int(custom_.size()) + int(pakIdx_.size()); }

  bool openPack(const std::string& pakPath, std::uint32_t usaLba, std::uint32_t usaEnd, const std::string& listPath, FILE* log) {
    if (!pakfs::read(pakPath, pak_)) return false;
    if (pak_.size() < 16 || std::memcmp(pak_.data(), "SF3VOCES", 8)) { pak_.clear(); return false; }
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, pak_.data() + o, 4); return v; };
    const std::uint32_t n = u32(12);
    if (16 + std::size_t(n) * 20 > pak_.size()) { pak_.clear(); return false; }
    for (std::uint32_t i = 0; i < n; ++i) {
      const std::size_t o = 16 + std::size_t(i) * 20;
      std::uint16_t id; std::memcpy(&id, pak_.data() + o, 2);
      PakEntry e{u32(o + 4), u32(o + 8), u32(o + 12), u32(o + 16)};
      if (id < kLines && std::size_t(e.offset) + e.bytes <= pak_.size()) pakIdx_[id] = e;
    }
    usaLba_ = usaLba; usaEnd_ = usaEnd;
    loadOwn(listPath, log);
    active_ = true;
    if (log) std::fprintf(log, "voces castellano: paquete %s con %zu lineas\n", pakPath.c_str(), pakIdx_.size());
    return true;
  }

  void loadOwn(const std::string& listPath, FILE* log) {

    {
      std::error_code ec;
      const auto dir = std::filesystem::path(pakfs::dataPath("voces_propias"));
      (void)listPath;
      for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const auto n = e.path().filename().string();
        int id = -1;
        if ((n.size() == 10 && (n[0] == 'E' || n[0] == 'e') && (n[1] == 'N' || n[1] == 'n') && n[2] == '_' && std::sscanf(n.c_str() + 3, "%3d", &id) == 1) && id >= 0 && id < int(kLines)) {
          std::vector<std::int16_t> pcm;
          if (readWav(e.path(), pcm) && !pcm.empty()) custom_[std::uint16_t(id)] = std::move(pcm);
          else if (log) std::fprintf(log, "voces propias: no se pudo leer %s (WAV PCM de 16 bits)\n", n.c_str());
        }
      }
      if (log && !custom_.empty()) std::fprintf(log, "voces propias: %zu lineas desde %s\n", custom_.size(), dir.string().c_str());
    }
  }

  void patchSector(std::uint32_t lba, unsigned char* raw) {
    if (!active_ || lba < usaLba_ || lba >= usaEnd_) return;
    const std::uint32_t rel = lba - usaLba_;
    std::uint32_t block = 0;
    while (block < kUsaStarts.size() && rel >= kUsaStarts[block]) ++block;
    const std::uint32_t start = block == 0 ? 0u : kUsaStarts[block - 1];
    if (rel - start != 31 || raw[17] != 1 || raw[18] != 0x42) return;
    const std::uint32_t end = block < kUsaStarts.size() ? kUsaStarts[block] : (usaEnd_ - usaLba_);
    const bool last = block >= kUsaStarts.size();

    const std::uint32_t cap = last ? ((end - start) / 16u - 10u) / 2u : 0xffffu;
    for (std::uint32_t ch = 0; ch < 15; ++ch) {
      const std::uint32_t id = block * 15u + ch;
      if (id >= kLines) continue;
      std::uint32_t n = 0;
      if (auto c = custom_.find(std::uint16_t(id)); c != custom_.end()) n = std::uint32_t(voiceLen(c->second));
      else if (auto pk = pakIdx_.find(std::uint16_t(id)); pk != pakIdx_.end()) n = pk->second.voice;
      else continue;

      const std::uint32_t secs = (n + 4703u) / 4704u;
      const std::uint32_t orig = std::uint32_t(raw[24 + ch * 2] | (raw[25 + ch * 2] << 8));

      const std::uint32_t want = std::clamp<std::uint32_t>((secs + 1) / 2, 1u, cap);
      unsigned char* h = raw + 24 + ch * 2;
      h[0] = std::uint8_t(want); h[1] = std::uint8_t(want >> 8);
      if (static const bool dbg = std::getenv("SF3N_VOZLOG") != nullptr; dbg)
        std::fprintf(stderr, "VOZDUR linea %u: %u -> %u unidades\n", id, orig, want);
    }
  }

  void filter(std::uint32_t lba, std::span<sf::psx::SpuPcmFrame> pcm) {
    if (!active_ || lba < usaLba_) return;
    std::size_t pos = 0;
    if (curOwn_ != 0xffff && lba == last_ + 16u) {
      pos = ++pos_;
    } else {
      curOwn_ = 0xffff;
      if (lba >= usaEnd_) return;
      const std::uint32_t rel = lba - usaLba_;
      std::uint32_t block = 0;
      while (block < kUsaStarts.size() && rel >= kUsaStarts[block]) ++block;
      const std::uint32_t start = block == 0 ? 0u : kUsaStarts[block - 1];
      const std::uint32_t off = rel - start, ch = off % 16u, k = off / 16u;
      if (ch >= 15) return;
      const std::uint32_t id = block * 15u + ch;
      if (id >= kLines) return;
      if (custom_.count(std::uint16_t(id)) || pakIdx_.count(std::uint16_t(id))) curOwn_ = std::uint16_t(id);
      else return;
      pos = pos_ = k;
    }
    last_ = lba;
    const std::size_t from = pos * pcm.size();
    const auto& es = ownLine(curOwn_);
    if (es.empty()) return;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
      const std::int16_t s = from + i < es.size() ? es[from + i] : 0;
      pcm[i].left = s; pcm[i].right = s;
    }
  }

private:

  const std::vector<std::int16_t>& ownLine(std::uint16_t id) {
    if (auto c = custom_.find(id); c != custom_.end()) return c->second;
    std::lock_guard lk(m_);
    if (auto c = pakCache_.find(id); c != pakCache_.end()) return c->second;
    auto& out = pakCache_[id];
    if (auto it = pakIdx_.find(id); it != pakIdx_.end()) {
      int ch = 0, rate = 0; short* pcm = nullptr;
      const int n = stb_vorbis_decode_memory(reinterpret_cast<const unsigned char*>(pak_.data() + it->second.offset), int(it->second.bytes), &ch, &rate, &pcm);
      if (n > 0 && pcm && ch >= 1) {
        out.resize(std::size_t(n));
        for (int i = 0; i < n; ++i) out[std::size_t(i)] = pcm[std::size_t(i) * std::size_t(ch)];
      }
      std::free(pcm);
    }
    return out;
  }
  static bool readWav(const std::filesystem::path& p, std::vector<std::int16_t>& out) {
    std::ifstream f(p, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
    auto u16 = [&](std::size_t o) { return unsigned(d[o] | d[o + 1] << 8); };
    auto u32 = [&](std::size_t o) { return std::uint32_t(u16(o) | u16(o + 2) << 16); };
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    unsigned ch = 0, rate = 0, bits = 0, fmt = 0; std::size_t data = 0, len = 0;
    for (std::size_t o = 12; o + 8 <= d.size();) {
      const std::uint32_t sz = u32(o + 4);
      if (!std::memcmp(d.data() + o, "fmt ", 4) && o + 24 <= d.size()) { fmt = u16(o + 8); ch = u16(o + 10); rate = u32(o + 12); bits = u16(o + 22); }
      if (!std::memcmp(d.data() + o, "data", 4)) { data = o + 8; len = std::min<std::size_t>(sz, d.size() - data); break; }
      o += 8 + sz + (sz & 1);
    }
    if ((fmt != 1 && fmt != 0xfffe) || bits != 16 || !ch || !rate || !data) return false;
    const std::size_t frames = len / (2 * ch);
    std::vector<double> m(frames);
    for (std::size_t i = 0; i < frames; ++i) {
      int acc = 0;
      for (unsigned c = 0; c < ch; ++c) acc += std::int16_t(u16(data + (i * ch + c) * 2));
      m[i] = double(acc) / ch;
    }
    const std::size_t n = std::size_t(double(frames) * 44100.0 / rate);
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      const double t = double(i) * rate / 44100.0; const std::size_t a = std::size_t(t);
      const double fr = t - a, v = a + 1 < frames ? m[a] * (1 - fr) + m[a + 1] * fr : (a < frames ? m[a] : 0);
      out[i] = std::int16_t(std::clamp(v, -32768.0, 32767.0));
    }
    return true;
  }

  static std::size_t voiceLen(const std::vector<std::int16_t>& v) {
    std::size_t end = v.size();
    while (end > 0 && std::abs(int(v[end - 1])) < 64) --end;
    return end;
  }
  bool active_ = false;
  std::uint32_t usaLba_ = 0, usaEnd_ = 0;
  std::mutex m_;
  std::map<std::uint16_t, std::vector<std::int16_t>> custom_;
  std::uint16_t curOwn_ = 0xffff;
  struct PakEntry { std::uint32_t offset, bytes, samples, voice; };
  std::vector<char> pak_;
  std::map<std::uint16_t, PakEntry> pakIdx_;
  std::map<std::uint16_t, std::vector<std::int16_t>> pakCache_;
  std::size_t pos_ = 0;
  std::uint32_t last_ = 0;
};
