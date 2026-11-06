// Syphon Filter 3 PC: recursos comprimidos (data/recursos.bin).
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace res {

inline constexpr unsigned char kBlob[] = {
#include "recursos_blob.inc"
};

inline const std::vector<std::uint8_t>& get(const std::string& name) {
  static std::map<std::string, std::vector<std::uint8_t>> cache;
  static std::mutex m;
  static const std::vector<std::uint8_t> none;
  std::lock_guard<std::mutex> g(m);
  if (auto it = cache.find(name); it != cache.end()) return it->second;
  const unsigned char* p = kBlob;
  const unsigned char* end = kBlob + sizeof kBlob;
  if (sizeof kBlob < 12 || std::memcmp(p, "SF3RES01", 8)) return none;
  std::uint32_t n; std::memcpy(&n, p + 8, 4); p += 12;
  for (std::uint32_t i = 0; i < n && p < end; ++i) {
    const std::size_t nl = *p++;
    const std::string nm(reinterpret_cast<const char*>(p), nl); p += nl;
    std::uint32_t size, comp; std::memcpy(&size, p, 4); std::memcpy(&comp, p + 4, 4); p += 8;
    if (nm == name) {
      std::vector<std::uint8_t> out; out.reserve(size);
      const unsigned char* c = p;
      while (out.size() < size) {
        const unsigned flags = *c++;
        for (int bit = 0; bit < 8 && out.size() < size; ++bit) {
          if (flags & (1u << bit)) out.push_back(*c++);
          else {
            const unsigned v = unsigned(c[0]) | (unsigned(c[1]) << 8); c += 2;
            const std::size_t off = (v >> 4) + 1, len = (v & 15) + 3;
            for (std::size_t k = 0; k < len; ++k) out.push_back(out[out.size() - off]);
          }
        }
      }
      return cache.emplace(name, std::move(out)).first->second;
    }
    p += comp;
  }
  return none;
}
inline const std::uint16_t* u16(const char* name) { return reinterpret_cast<const std::uint16_t*>(get(name).data()); }
inline const std::int16_t* i16(const char* name) { return reinterpret_cast<const std::int16_t*>(get(name).data()); }

}

inline constexpr int kOpcW = 108, kOpcH = 66;
inline const std::uint16_t* const kOpcPix = res::u16("kOpcPix");
inline const std::uint16_t* const kOpcPixEn = res::u16("kOpcPixEn");
inline const std::uint16_t* const kOpcPixEn2 = res::u16("kOpcPixEn2");
inline const std::uint16_t* const kConPix = res::u16("kConPix");
inline const std::uint16_t* const kConPixEn = res::u16("kConPixEn");
inline const std::uint16_t* const kConPixEn2 = res::u16("kConPixEn2");
inline const std::uint16_t* const kConPixRu = res::u16("kConPixRu");
inline const std::uint16_t* const kSalPix = res::u16("kSalPix");
inline const std::uint16_t* const kSalPixEn = res::u16("kSalPixEn");
inline const std::uint16_t* const kSalPixRu = res::u16("kSalPixRu");
inline const std::int16_t* const kSndMover = res::i16("kSndMover");
inline const std::int16_t* const kSndAceptar = res::i16("kSndAceptar");
inline const int kSndMoverLen = int(res::get("kSndMover").size() / 2);
inline const int kSndAceptarLen = int(res::get("kSndAceptar").size() / 2);
