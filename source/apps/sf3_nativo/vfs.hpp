#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <vector>

struct VirtualDiscFiles {
  struct File { std::vector<std::byte> data; std::uint32_t lba = 0; };
  std::map<std::string, File> files;
  std::uint32_t base = 0, end = 0;

  static std::string key(std::string p) {
    if (!p.empty() && (p[0] == '\\' || p[0] == '/')) p.erase(0, 1);
    if (auto s = p.find(';'); s != std::string::npos) p.resize(s);
    for (auto& c : p) c = c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c)));
    return p;
  }

  int load(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    int n = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      auto ext = it->path().extension().string();
      for (auto& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
      if (ext == ".TXT" || ext == ".PNG" || ext == ".BMP" || ext == ".MD" || ext == ".JSON" || ext == ".INI") continue;
      const auto rel = fs::relative(it->path(), dir, ec).generic_string();
      if (ec) continue;
      std::ifstream f(it->path(), std::ios::binary);
      std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (ext == ".PAK") { n += loadPak(raw); continue; }
      File file;
      file.data.resize(raw.size());
      std::memcpy(file.data.data(), raw.data(), raw.size());
      files[key(rel)] = std::move(file);
      ++n;
    }
    return n;
  }

  void add(const std::string& path, std::vector<std::byte> data) { files[key(path)].data = std::move(data); }

  int loadPak(const std::vector<char>& b) {
    auto u32 = [&](std::size_t o) { return std::uint32_t(std::uint8_t(b[o])) | std::uint32_t(std::uint8_t(b[o + 1])) << 8 | std::uint32_t(std::uint8_t(b[o + 2])) << 16 | std::uint32_t(std::uint8_t(b[o + 3])) << 24; };
    if (b.size() < 20) return 0;
    const std::uint32_t cnt = u32(4), no = u32(12), dof = u32(16);
    if (cnt == 0 || cnt > 100000 || 20 + std::size_t(cnt) * 4 > b.size() || no > dof || dof > b.size()) return 0;
    std::vector<std::string> names;
    for (std::size_t o = no; o < dof && names.size() < cnt;) {
      std::string n;
      while (o < dof && b[o]) n += b[o++];
      ++o;
      names.push_back(n);
    }
    int n = 0;
    for (std::uint32_t i = 0; i < cnt && i < names.size(); ++i) {
      const std::size_t a = dof + u32(20 + i * 4), e = i + 1 < cnt ? dof + u32(20 + (i + 1) * 4) : b.size();
      if (a > e || e > b.size()) continue;
      const auto k = key(names[i]);
      if (files.count(k)) continue;
      File f;
      f.data.resize(e - a);
      std::memcpy(f.data.data(), b.data() + a, e - a);
      files[k] = std::move(f);
      ++n;
    }
    return n;
  }

  void layout(std::uint32_t first) {
    base = first + 16;
    std::uint32_t lba = base;
    for (auto& [k, f] : files) {
      f.lba = lba;
      lba += std::uint32_t((f.data.size() + 2047) / 2048) + 1;
    }
    end = lba;
  }

  const File* find(const std::string& path) const {
    auto it = files.find(key(path));
    return it == files.end() ? nullptr : &it->second;
  }

  bool readData(std::uint32_t lba, std::span<std::byte, 2048> d) const {
    if (lba < base || lba >= end) return false;
    std::fill(d.begin(), d.end(), std::byte{0});
    for (const auto& [k, f] : files) {
      const auto n = std::uint32_t((f.data.size() + 2047) / 2048);
      if (lba >= f.lba && lba < f.lba + n) {
        const std::size_t off = std::size_t(lba - f.lba) * 2048;
        std::memcpy(d.data(), f.data.data() + off, std::min<std::size_t>(2048, f.data.size() - off));
        break;
      }
    }
    return true;
  }

  bool readRaw(std::uint32_t lba, std::span<std::byte, 2352> d) const {
    if (lba < base || lba >= end) return false;
    std::fill(d.begin(), d.end(), std::byte{0});
    for (int i = 1; i <= 10; ++i) d[std::size_t(i)] = std::byte{0xff};
    const std::uint32_t abs = lba + 150;
    auto bcd = [](std::uint32_t v) { return std::byte(((v / 10) << 4) | (v % 10)); };
    d[12] = bcd(abs / 4500); d[13] = bcd((abs / 75) % 60); d[14] = bcd(abs % 75); d[15] = std::byte{2};
    d[18] = d[22] = std::byte{0x08};
    return readData(lba, std::span<std::byte, 2048>(d.data() + 24, 2048));
  }
};

inline VirtualDiscFiles& virtualDisc() {
  static VirtualDiscFiles v;
  return v;
}
