#pragma once
#include "pakfs.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace rufont {

inline const char* cyr(std::uint32_t cp) {
  if (cp == 0x401 || cp == 0x451) return "E";
  if (cp >= 0x430 && cp <= 0x44f) cp -= 0x20;
  if (cp < 0x410 || cp > 0x42f) return nullptr;
  static const char* t[32] = {"A", "6", "B", "F", "D", "E", "G", "3", "N", "N", "K", "L", "M", "H", "O", "Z",
                              "P", "C", "T", "Y", "V", "X", "Q", "4", "W", "W", "J", "S", "J", "E", "U", "R"};
  return t[cp - 0x410];
}

inline const char* latin(char c) {
  if (c >= 'A' && c <= 'Z') c = char(c + 32);
  static const char* t[26] = {"A", "6", "K", "D", "E", "V", "F", "X", "N", "DG", "K", "L", "M", "H", "O", "Z",
                              "K", "P", "C", "T", "Y", "B", "B", "KC", "N", "3"};
  return (c >= 'a' && c <= 'z') ? t[c - 'a'] : nullptr;
}

inline std::string latinToRu(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '%' && i + 1 < s.size()) { o += c; o += s[++i]; continue; }
    o += (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
  }
  return o;
}
inline bool needsTranslit(const std::string& s) {
  int run = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%') { ++i; run = 0; continue; }
    run = (s[i] >= 'a' && s[i] <= 'z') ? run + 1 : 0;
    if (run >= 2) return true;
  }
  return false;
}

inline std::string utf8ToRu(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp = c; std::size_t n = 1;
    if (c >= 0xf0 && i + 3 < s.size()) { cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3fu) << 12) | ((s[i + 2] & 0x3fu) << 6) | (s[i + 3] & 0x3fu); n = 4; }
    else if (c >= 0xe0 && i + 2 < s.size()) { cp = ((c & 15u) << 12) | ((s[i + 1] & 0x3fu) << 6) | (s[i + 2] & 0x3fu); n = 3; }
    else if (c >= 0xc0 && i + 1 < s.size()) { cp = ((c & 31u) << 6) | (s[i + 1] & 0x3fu); n = 2; }
    i += n;
    if (const char* r = cyr(cp)) { o += r; continue; }
    if (cp < 128) { o += (cp >= 'A' && cp <= 'Z') ? char(cp + 32) : char(cp); continue; }
    if (cp == 0xab || cp == 0xbb || cp == 0x201c || cp == 0x201d) { o += '"'; continue; }
    if (cp == 0x2014 || cp == 0x2013) { o += '-'; continue; }
  }
  return o;
}

inline bool hasCyrillic(const std::string& s) {
  for (std::size_t i = 0; i + 1 < s.size(); ++i)
    if (static_cast<unsigned char>(s[i]) == 0xd0 || static_cast<unsigned char>(s[i]) == 0xd1) return true;
  return false;
}

inline int patchExe(const std::string& pakPath, std::vector<std::byte>& exe) {
  std::vector<char> d;
  if (!pakfs::read(pakPath, d)) return -1;
  if (d.size() < 16 || std::memcmp(d.data(), "SF3TEXTO", 8)) return -1;
  std::uint32_t n; std::memcpy(&n, d.data() + 12, 4);
  std::size_t o = 16;
  for (std::uint32_t i = 0; i < n && o + 4 <= d.size(); ++i) { std::uint16_t le, ls; std::memcpy(&le, d.data() + o, 2); std::memcpy(&ls, d.data() + o + 2, 2); o += 4u + le + ls; }
  if (o + 4 <= d.size() && std::memcmp(d.data() + o, "NOGL", 4) == 0) o += 4; else o += 768;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "ARCH", 4) == 0) {
    std::uint32_t nf; std::memcpy(&nf, d.data() + o + 4, 4); o += 8;
    for (std::uint32_t i = 0; i < nf && o + 2 <= d.size(); ++i) { std::uint16_t nl; std::memcpy(&nl, d.data() + o, 2); o += 2u + nl; std::uint32_t sz = 0; if (o + 4 > d.size()) break; std::memcpy(&sz, d.data() + o, 4); o += 4u + sz; }
  }
  int done = 0;
  if (o + 8 <= d.size() && std::memcmp(d.data() + o, "EXEP", 4) == 0) {
    std::uint32_t np; std::memcpy(&np, d.data() + o + 4, 4); o += 8;
    for (std::uint32_t i = 0; i < np && o + 8 <= d.size(); ++i) {
      std::uint32_t off, len; std::memcpy(&off, d.data() + o, 4); std::memcpy(&len, d.data() + o + 4, 4); o += 8;
      if (o + len > d.size() || off + std::size_t(len) > exe.size()) break;
      std::memcpy(exe.data() + off, d.data() + o, len); o += len; ++done;
    }
  }
  return done;
}

}
