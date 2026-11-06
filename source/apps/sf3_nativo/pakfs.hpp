// Syphon Filter 3 PC: paquetes de datos (idiomas.pak, armas.pak).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

namespace pakfs {

struct Entry { std::string pak; std::uint64_t off = 0, size = 0; };

struct State {
  std::string base;
  std::vector<std::string> up;
  std::map<std::string, Entry> index;
  std::vector<std::string> mounted;
  bool ready = false;
};
inline State& st() { static State s; return s; }

inline std::string norm(std::string p) {
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}
inline std::string lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

inline bool mount(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  char hdr[16];
  if (!f.read(hdr, 16) || std::memcmp(hdr, "SF3PAK01", 8)) return false;
  std::uint32_t n; std::memcpy(&n, hdr + 8, 4);
  for (std::uint32_t i = 0; i < n; ++i) {
    std::uint16_t l = 0;
    if (!f.read(reinterpret_cast<char*>(&l), 2)) return false;
    std::string name(l, '\0');
    Entry e; e.pak = path;
    if (!f.read(name.data(), l) || !f.read(reinterpret_cast<char*>(&e.off), 8) || !f.read(reinterpret_cast<char*>(&e.size), 8)) return false;
    st().index.emplace(lower(norm(name)), e);
  }
  st().mounted.push_back(path);
  return true;
}

inline void init(const std::string& exeDir) {
  auto& s = st();
  if (s.ready) return;
  s.ready = true;
  std::string b = norm(exeDir);
  while (!b.empty() && b.back() == '/') b.pop_back();
  s.base = b.empty() ? std::string() : b + "/";
  std::string cur = b;
  for (int i = 0; i < 3; ++i) {
    const auto par = norm(std::filesystem::path(cur).parent_path().string());
    if (par.empty() || par == cur) break;
    s.up.push_back(par + "/"); cur = par;
  }
  for (const char* p : {"idiomas.pak", "armas.pak"}) {
    if (mount(s.base + p)) continue;
    for (const auto& d : s.up) if (mount(d + p)) break;
  }
}

inline std::string rel(const std::string& path) {
  std::string p = norm(path);
  const auto& s = st();
  if (!s.base.empty() && lower(p).rfind(lower(s.base), 0) == 0) return p.substr(s.base.size());
  for (const auto& d : s.up) if (lower(p).rfind(lower(d), 0) == 0) return p.substr(d.size());
  while (p.rfind("./", 0) == 0) p.erase(0, 2);
  return p;
}

inline std::string loosePath(const std::string& path) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return path;
  const auto& s = st();
  const std::string r = rel(path);
  for (const auto& d : s.up) if (std::filesystem::exists(d + r, ec)) return d + r;
  return {};
}

inline const Entry* find(const std::string& path) {
  const auto it = st().index.find(lower(rel(path)));
  return it == st().index.end() ? nullptr : &it->second;
}

inline bool exists(const std::string& path) { return !loosePath(path).empty() || find(path) != nullptr; }

inline std::string dataPath(const std::string& relName) {
  std::error_code ec;
  const auto& s = st();
  if (std::filesystem::exists(s.base + relName, ec)) return s.base + relName;
  for (const auto& d : s.up) if (std::filesystem::exists(d + relName, ec)) return d + relName;
  return s.base + relName;
}

inline bool looseDir(const std::string& path) {
  const auto p = loosePath(path);
  std::error_code ec;
  return !p.empty() && std::filesystem::is_directory(p, ec);
}

template <class V> inline bool read(const std::string& path, V& out, std::uint64_t maxSize = ~0ull) {
  out.clear();
  if (const auto lp = loosePath(path); !lp.empty()) {
    std::ifstream f(lp, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto z = std::uint64_t(f.tellg());
    if (z > maxSize) return false;
    f.seekg(0);
    out.resize(std::size_t(z));
    return z == 0 || bool(f.read(reinterpret_cast<char*>(out.data()), std::streamsize(z)));
  }
  const Entry* e = find(path);
  if (!e || e->size > maxSize) return false;
  std::ifstream f(e->pak, std::ios::binary);
  if (!f) return false;
  f.seekg(std::streamoff(e->off));
  out.resize(std::size_t(e->size));
  return e->size == 0 || bool(f.read(reinterpret_cast<char*>(out.data()), std::streamsize(e->size)));
}

class SubBuf : public std::streambuf {
 public:
  SubBuf(const std::string& pak, std::uint64_t off, std::uint64_t size) : f_(pak, std::ios::binary), off_(off), size_(size) {}
  bool ok() const { return bool(f_); }
 protected:
  int_type underflow() override {
    if (pos_ >= size_) return traits_type::eof();
    const auto n = std::min<std::uint64_t>(sizeof buf_, size_ - pos_);
    f_.clear(); f_.seekg(std::streamoff(off_ + pos_));
    if (!f_.read(buf_, std::streamsize(n))) return traits_type::eof();
    setg(buf_, buf_, buf_ + n);
    pos_ += n;
    return traits_type::to_int_type(buf_[0]);
  }
  pos_type seekoff(off_type o, std::ios_base::seekdir dir, std::ios_base::openmode) override {
    const std::int64_t cur = std::int64_t(pos_) - std::int64_t(egptr() - gptr());
    std::int64_t t = dir == std::ios_base::beg ? o : dir == std::ios_base::cur ? cur + o : std::int64_t(size_) + o;
    if (t < 0 || std::uint64_t(t) > size_) return pos_type(off_type(-1));
    pos_ = std::uint64_t(t); setg(buf_, buf_, buf_);
    return pos_type(t);
  }
  pos_type seekpos(pos_type p, std::ios_base::openmode m) override { return seekoff(off_type(p), std::ios_base::beg, m); }
 private:
  std::ifstream f_;
  std::uint64_t off_, size_, pos_ = 0;
  char buf_[1 << 16];
};
class SubStream : public std::istream {
 public:
  SubStream(const std::string& pak, std::uint64_t off, std::uint64_t size) : std::istream(nullptr), buf_(pak, off, size) { rdbuf(&buf_); if (!buf_.ok()) setstate(std::ios::failbit); }
 private:
  SubBuf buf_;
};

inline std::unique_ptr<std::istream> open(const std::string& path) {
  if (const auto lp = loosePath(path); !lp.empty()) {
    auto f = std::make_unique<std::ifstream>(lp, std::ios::binary);
    if (!*f) return nullptr;
    return f;
  }
  if (const Entry* e = find(path)) {
    auto s = std::make_unique<SubStream>(e->pak, e->off, e->size);
    if (!*s) return nullptr;
    return s;
  }
  return nullptr;
}

inline bool writePak(const std::string& out, const std::vector<std::pair<std::string, std::string>>& files, std::string& msg) {
  std::uint64_t hdr = 16;
  for (const auto& [name, src] : files) hdr += 2 + name.size() + 16;
  std::vector<std::uint64_t> sizes;
  for (const auto& [name, src] : files) { std::error_code ec; sizes.push_back(std::filesystem::file_size(src, ec)); }
  const std::string tmp = out + ".tmp";
  std::ofstream o(tmp, std::ios::binary);
  if (!o) { msg += "no se pudo escribir " + out + "\n"; return false; }
  o.write("SF3PAK01", 8);
  const std::uint32_t n = std::uint32_t(files.size()), z = 0;
  o.write(reinterpret_cast<const char*>(&n), 4); o.write(reinterpret_cast<const char*>(&z), 4);
  std::uint64_t off = hdr;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::uint16_t l = std::uint16_t(files[i].first.size());
    o.write(reinterpret_cast<const char*>(&l), 2); o.write(files[i].first.data(), l);
    o.write(reinterpret_cast<const char*>(&off), 8); o.write(reinterpret_cast<const char*>(&sizes[i]), 8);
    off += sizes[i];
  }
  std::vector<char> buf(1 << 20);
  for (const auto& [name, src] : files) {
    std::ifstream in(src, std::ios::binary);
    while (in) { in.read(buf.data(), std::streamsize(buf.size())); o.write(buf.data(), in.gcount()); }
  }
  o.close();
  std::error_code ec;
  std::filesystem::rename(tmp, out, ec);
  if (ec) { std::filesystem::remove(out, ec); std::filesystem::rename(tmp, out, ec); }
  msg += out + ": " + std::to_string(files.size()) + " archivos, " + std::to_string(off / (1024 * 1024)) + " MB\n";
  return !ec;
}

inline std::string createPaks(const std::string& exeDir) {
  init(exeDir);
  const auto& s = st();
  std::string msg;
  std::error_code ec;

  auto pick = [&](const std::string& name) { if (std::filesystem::exists(s.base + name, ec)) return s.base; for (const auto& d : s.up) if (std::filesystem::exists(d + name, ec)) return d; return std::string(); };
  {
    std::vector<std::pair<std::string, std::string>> files;
    std::string where;
    auto add = [&](const std::string& name) { const auto d = pick(name); if (!d.empty()) { files.push_back({name, d + name}); if (where.empty()) where = d; } };
    for (const char* n : {"textos_es.pak", "textos_ru.pak", "voces_es.pak", "voces_es.txt"}) add(n);
    for (int k = 1; k <= 9; ++k) add("videos_es_" + std::to_string(k) + ".pak");
    for (int k = 1; k <= 9; ++k) add("voces_ru_" + std::to_string(k) + ".pak");
    if (files.empty()) msg += "idiomas.pak: no hay archivos de idiomas (textos_es.pak, voces_es.pak...)\n";
    else writePak(where + "idiomas.pak", files, msg);
  }
  {
    const std::string d = pick("armas");
    std::vector<std::pair<std::string, std::string>> files;
    if (!d.empty()) {
      const std::filesystem::path root = d + "armas";
      for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const std::string r = norm(std::filesystem::relative(it->path(), root, ec).string());
        const std::string lr = lower(r);
        if (lr.rfind("juegos/", 0) == 0 || lr.find("respaldo") != std::string::npos || lr == "cache_fotos_menu.bin" || lr == "informe_armas.txt" ||
            lr == "catalogo_armas.txt" || (lr.size() > 4 && lr.substr(lr.size() - 4) == ".png") ||
            lr == "banco_atlas.bin" || lr == "banco_tris.txt")
          continue;
        files.push_back({"armas/" + r, it->path().string()});
      }
    }
    if (files.empty()) msg += "armas.pak: no hay carpeta armas\n";
    else writePak(d + "armas.pak", files, msg);
  }
  return msg;
}

}
