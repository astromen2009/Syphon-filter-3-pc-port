#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

class MemCard {
 public:
  static constexpr std::size_t kSize = 128 * 1024, kFrame = 128, kBlock = 8192;

  void initMemory() { path_.clear(); data_.assign(kSize, 0); format(); dirty_ = false; }

  std::vector<std::uint8_t> readFile(const std::string& name) {
    std::vector<std::uint8_t> v;
    const int first = find(name);
    if (first < 0) return v;
    v.resize(fileSize(first));
    v.resize(io(first, 0, v.data(), v.size(), false));
    return v;
  }
  bool addFile(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const int blocks = int((bytes.size() + kBlock - 1) / kBlock);
    const int first = create(name, std::max(1, blocks));
    if (first < 0) return false;
    std::vector<std::uint8_t> b(bytes);
    io(first, 0, b.data(), b.size(), true);
    return true;
  }
  void load(const std::string& path) {
    path_ = path;
    data_.assign(kSize, 0);
    bool ok = false;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
      ok = std::fread(data_.data(), 1, kSize, f) == kSize;
      std::fclose(f);
    }
    if (!ok || data_[0] != 'M' || data_[1] != 'C') format();
  }
  bool present() const { return !data_.empty(); }

  void format() {
    data_.assign(kSize, 0);
    data_[0] = 'M'; data_[1] = 'C';
    fixChecksum(0);
    for (int i = 1; i < 16; ++i) {
      std::uint8_t* e = frame(i);
      std::memset(e, 0, kFrame);
      put32(e, 0xa0);
      put16(e + 8, 0xffff);
      fixChecksum(i);
    }
    for (int i = 16; i < 36; ++i) {
      std::uint8_t* e = frame(i);
      std::memset(e, 0, kFrame);
      put32(e, 0xffffffffu);
      put16(e + 8, 0xffff);
      fixChecksum(i);
    }
    std::memcpy(frame(63), frame(0), kFrame);
    save();
  }

  bool readSector(int sector, std::uint8_t* out) const {
    if (sector < 0 || sector >= 1024) return false;
    std::memcpy(out, data_.data() + std::size_t(sector) * kFrame, kFrame);
    return true;
  }
  bool writeSector(int sector, const std::uint8_t* in) {
    if (sector < 0 || sector >= 1024) return false;
    std::memcpy(data_.data() + std::size_t(sector) * kFrame, in, kFrame);
    dirty_ = true;
    return true;
  }

  struct Entry { int first = -1; std::string name; std::uint32_t size = 0; };
  std::vector<Entry> files() const {
    std::vector<Entry> v;
    for (int b = 1; b < 16; ++b) {
      const std::uint8_t* e = frame(b);
      if ((get32(e) & 0xff) != 0x51) continue;
      Entry en;
      en.first = b;
      en.size = get32(e + 4);
      en.name = nameOf(e);
      v.push_back(en);
    }
    return v;
  }
  int find(const std::string& name) const {
    for (const auto& f : files()) if (f.name == name) return f.first;
    return -1;
  }
  int freeBlocks() const {
    int n = 0;
    for (int b = 1; b < 16; ++b) if ((get32(frame(b)) & 0xf0) == 0xa0) ++n;
    return n;
  }

  int create(const std::string& name, int blocks) {
    if (blocks < 1 || blocks > 15 || find(name) >= 0 || freeBlocks() < blocks || name.size() > 20) return -1;
    std::vector<int> chain;
    for (int b = 1; b < 16 && int(chain.size()) < blocks; ++b) if ((get32(frame(b)) & 0xf0) == 0xa0) chain.push_back(b);
    for (std::size_t i = 0; i < chain.size(); ++i) {
      std::uint8_t* e = frame(chain[i]);
      std::memset(e, 0, kFrame);
      const std::uint32_t state = i == 0 ? 0x51 : (i + 1 == chain.size() ? 0x53 : 0x52);
      put32(e, chain.size() == 1 ? 0x51 : state);
      put32(e + 4, i == 0 ? std::uint32_t(blocks) * kBlock : 0);
      put16(e + 8, i + 1 < chain.size() ? std::uint16_t(chain[i + 1] - 1) : 0xffff);
      if (i == 0) std::memcpy(e + 0x0a, name.data(), name.size());
      fixChecksum(chain[i]);
      std::memset(data_.data() + std::size_t(chain[i]) * kBlock, 0, kBlock);
    }
    dirty_ = true;
    return chain.front();
  }
  bool erase(const std::string& name) {
    int b = find(name);
    if (b < 0) return false;
    for (int guard = 0; b >= 1 && b < 16 && guard < 16; ++guard) {
      std::uint8_t* e = frame(b);
      const std::uint16_t next = get16(e + 8);
      const std::uint32_t st = get32(e) & 0xff;
      put32(e, st == 0x51 ? 0xa1 : st == 0x52 ? 0xa2 : 0xa3);
      fixChecksum(b);
      b = next == 0xffff ? -1 : int(next) + 1;
    }
    dirty_ = true;
    return true;
  }

  std::size_t io(int first, std::uint32_t pos, std::uint8_t* buf, std::size_t len, bool write) {
    std::vector<int> chain;
    for (int b = first, guard = 0; b >= 1 && b < 16 && guard < 16; ++guard) {
      chain.push_back(b);
      const std::uint16_t next = get16(frame(b) + 8);
      b = next == 0xffff ? -1 : int(next) + 1;
    }
    std::size_t done = 0;
    while (done < len) {
      const std::size_t p = pos + done;
      const std::size_t bi = p / kBlock;
      if (bi >= chain.size()) break;
      const std::size_t off = p % kBlock;
      const std::size_t n = std::min(len - done, kBlock - off);
      std::uint8_t* d = data_.data() + std::size_t(chain[bi]) * kBlock + off;
      if (write) std::memcpy(d, buf + done, n); else std::memcpy(buf + done, d, n);
      done += n;
    }
    if (write && done) dirty_ = true;
    return done;
  }
  std::uint32_t fileSize(int first) const { return first >= 1 && first < 16 ? get32(frame(first) + 4) : 0; }

  void save() {
    if (path_.empty()) { dirty_ = false; return; }
    if (path_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path(), ec);
    const std::string tmp = path_ + ".tmp";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
      const bool ok = std::fwrite(data_.data(), 1, data_.size(), f) == data_.size();
      std::fclose(f);
      if (ok) { std::filesystem::rename(tmp, path_, ec); if (ec) { std::filesystem::remove(path_, ec); std::filesystem::rename(tmp, path_, ec); } }
    }
    dirty_ = false;
  }
  void flush() { if (dirty_) save(); }

  static bool match(const std::string& pat, const std::string& name) {
    std::size_t i = 0;
    for (; i < pat.size(); ++i) {
      if (pat[i] == '*') return true;
      if (i >= name.size()) return false;
      if (pat[i] != '?' && pat[i] != name[i]) return false;
    }
    return i == name.size();
  }

 private:
  std::uint8_t* frame(int i) { return data_.data() + std::size_t(i) * kFrame; }
  const std::uint8_t* frame(int i) const { return data_.data() + std::size_t(i) * kFrame; }
  static std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
  static std::uint16_t get16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
  static void put32(std::uint8_t* p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = std::uint8_t(v >> (8 * i)); }
  static void put16(std::uint8_t* p, std::uint16_t v) { p[0] = std::uint8_t(v); p[1] = std::uint8_t(v >> 8); }
  static std::string nameOf(const std::uint8_t* e) {
    std::string n;
    for (int i = 0; i < 20 && e[0x0a + i]; ++i) n += char(e[0x0a + i]);
    return n;
  }
  void fixChecksum(int i) {
    std::uint8_t* e = frame(i);
    std::uint8_t c = 0;
    for (std::size_t k = 0; k < kFrame - 1; ++k) c ^= e[k];
    e[kFrame - 1] = c;
  }
  std::vector<std::uint8_t> data_;
  std::string path_;
  bool dirty_ = false;
};

class CardStore {
 public:
  struct Slot { int id = 0; std::string archivo, nombre, mision; long long fecha = 0; };
  static constexpr const char* kSettings = "BASCUS-94640settings";
  static constexpr const char* kPrefix = "BASCUS-94640";

  void open(const std::string& dir) {
    dir_ = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    settings_ = readAll(dir_ + "ajustes.bin");
    slots_.clear();
    for (const auto& de : std::filesystem::directory_iterator(dir_, ec)) {
      const std::string n = de.path().filename().string();
      int id = 0;
      if (std::sscanf(n.c_str(), "partida%d.txt", &id) != 1 || n.size() < 5 || n.substr(n.size() - 4) != ".txt") continue;
      Slot s; s.id = id;
      if (FILE* f = std::fopen(de.path().string().c_str(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
          std::string l(line);
          while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
          if (l.rfind("archivo=", 0) == 0) s.archivo = l.substr(8);
          else if (l.rfind("nombre=", 0) == 0) s.nombre = l.substr(7);
          else if (l.rfind("mision=", 0) == 0) s.mision = l.substr(7);
          else if (l.rfind("fecha=", 0) == 0) s.fecha = std::atoll(l.c_str() + 6);
        }
        std::fclose(f);
      }
      if (!s.archivo.empty() && std::filesystem::exists(binPath(id), ec)) slots_.push_back(s);
    }
    std::sort(slots_.begin(), slots_.end(), [](const Slot& a, const Slot& b) { return a.fecha != b.fecha ? a.fecha > b.fecha : a.id > b.id; });
  }

  void importMcr(const std::string& mcr, const std::string& mision) {
    std::error_code ec;
    if (!std::filesystem::exists(mcr, ec)) return;
    MemCard c; c.load(mcr);
    for (const auto& f : c.files()) {
      if (f.name.rfind(kPrefix, 0) != 0) continue;
      const auto bytes = c.readFile(f.name);
      if (f.name == kSettings) setSettings(bytes);
      else store(-1, f.name, bytes, mision);
    }
    std::filesystem::rename(mcr, mcr + ".importada", ec);
  }
  const std::vector<Slot>& slots() const { return slots_; }
  const Slot* find(int id) const { for (const auto& s : slots_) if (s.id == id) return &s; return nullptr; }
  std::vector<std::uint8_t> data(int id) const { return readAll(binPath(id)); }
  void setSettings(const std::vector<std::uint8_t>& b) { if (b == settings_) return; settings_ = b; writeAll(dir_ + "ajustes.bin", b); }

  int store(int id, const std::string& archivo, const std::vector<std::uint8_t>& bytes, const std::string& mision) {
    if (id < 0) { id = 1; for (const auto& s : slots_) id = std::max(id, s.id + 1); }
    writeAll(binPath(id), bytes);
    Slot s; s.id = id; s.archivo = archivo; s.nombre = archivo.rfind(kPrefix, 0) == 0 ? archivo.substr(std::strlen(kPrefix)) : archivo;
    s.mision = mision; s.fecha = (long long)std::time(nullptr);
    if (const Slot* old = find(id); old && s.mision.empty()) s.mision = old->mision;
    if (FILE* f = std::fopen(txtPath(id).c_str(), "w")) {
      std::fprintf(f, "archivo=%s\nnombre=%s\nmision=%s\nfecha=%lld\n", s.archivo.c_str(), s.nombre.c_str(), s.mision.c_str(), s.fecha);
      std::fclose(f);
    }
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [&](const Slot& o) { return o.id == id; }), slots_.end());
    slots_.insert(slots_.begin(), s);
    return id;
  }
  bool remove(int id) {
    std::error_code ec;
    std::filesystem::remove(binPath(id), ec); std::filesystem::remove(txtPath(id), ec);
    const auto n = slots_.size();
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [&](const Slot& o) { return o.id == id; }), slots_.end());
    return slots_.size() != n;
  }

  int buildView(MemCard& card, int show = -1, const std::string& name = {}) const {
    card.initMemory();
    if (!settings_.empty()) card.addFile(kSettings, settings_);
    if (slots_.empty()) return 0;
    const Slot& rep = slots_.front();
    const int id = show >= 0 && find(show) ? show : rep.id;
    auto bytes = data(id);
    if (bytes.empty()) return 0;
    return card.addFile(name.empty() ? rep.archivo : name, bytes) ? 1 : 0;
  }

  std::vector<std::vector<int>> pages(int per) const {
    std::vector<std::vector<int>> pg;
    for (const auto& s : slots_) {
      if (s.archivo.rfind(kPrefix, 0) != 0 || s.archivo == kSettings) continue;
      bool placed = false;
      for (auto& p : pg) {
        if (int(p.size()) >= per) continue;
        bool dup = false;
        for (int id : p) if (const Slot* o = find(id)) dup = dup || o->archivo == s.archivo;
        if (!dup) { p.push_back(s.id); placed = true; break; }
      }
      if (!placed) pg.push_back({s.id});
    }
    if (pg.empty()) pg.push_back({});
    return pg;
  }

  std::vector<std::pair<std::string, int>> buildViewIds(MemCard& card, const std::vector<int>& ids) const {
    std::vector<std::pair<std::string, int>> out;
    card.initMemory();
    if (!settings_.empty()) card.addFile(kSettings, settings_);
    for (int id : ids) {
      const Slot* s = find(id);
      if (!s) continue;
      const auto bytes = data(id);
      if (bytes.empty() || !card.addFile(s->archivo, bytes)) continue;
      out.push_back({s->archivo, id});
    }
    return out;
  }

 private:
  std::string binPath(int id) const { char b[32]; std::snprintf(b, sizeof b, "partida%04d.bin", id); return dir_ + b; }
  std::string txtPath(int id) const { char b[32]; std::snprintf(b, sizeof b, "partida%04d.txt", id); return dir_ + b; }
  static std::vector<std::uint8_t> readAll(const std::string& p) {
    std::vector<std::uint8_t> v;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
      std::fseek(f, 0, SEEK_END); v.resize(std::size_t(std::max(0L, std::ftell(f)))); std::fseek(f, 0, SEEK_SET);
      if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
      std::fclose(f);
    }
    return v;
  }
  static void writeAll(const std::string& p, const std::vector<std::uint8_t>& b) {
    const std::string tmp = p + ".tmp";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
      std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
      std::error_code ec; std::filesystem::rename(tmp, p, ec);
      if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
    }
  }
  std::string dir_;
  std::vector<std::uint8_t> settings_;
  std::vector<Slot> slots_;
};
