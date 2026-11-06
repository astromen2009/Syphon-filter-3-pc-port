#pragma once
#include "pakfs.hpp"
#include <functional>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "sf/psx/r3000_runtime.hpp"
#include "ru_fuente.hpp"

class TextosEs {
public:
  static constexpr std::uint32_t kPrint = 0x800a8f18u;
  static constexpr std::uint32_t kGlyph = 0x800ab598u;
  static constexpr std::uint32_t kStub = 0x80700000u;
  static constexpr std::uint32_t kTable = 0x80700100u;
  static constexpr std::uint32_t kSlots = 0x80701000u;
  static constexpr std::uint32_t kSlotsEnd = 0x80710000u;

  static constexpr std::uint32_t kPool = 0x80710000u;
  static constexpr std::uint32_t kPoolN = 1024u;

  static constexpr std::uint32_t kMenuLabels = 0x80720000u, kMenuPanel = 0x80730000u, kMenuFoot = 0x80740000u, kMenuSub = 0x80750000u;
  static constexpr std::uint32_t kMenuSubN = 0x40u;

  static constexpr std::uint32_t kTextTab = 0x8013b878u;
  static constexpr std::uint32_t kHudTab = 0x80760000u, kHudBufs = 0x80760100u, kHudN = 24u, kHudBuf = 0x400u;
  static constexpr std::uint32_t kMenuPatch[2][14][3] = {{
{0x801478d8u, 0x26105820u, 0x3c108074u},
{0x80149708u, 0x25173e80u, 0x3c178073u},
{0x80149d00u, 0x24515760u, 0x3c118075u},
{0x8014cd14u, 0x8c427688u, 0x3c028073u},
{0x8014cd1cu, 0x8c423e80u, 0x8c420000u},
{0x8014ce18u, 0x24a53e80u, 0x3c058073u},
{0x8014e61cu, 0x26123ba0u, 0x3c128072u},
{0x8014e828u, 0x26643ba0u, 0x3c048072u},
{0x8014e83cu, 0x2405002eu, 0x24050080u},
{0x8014e840u, 0x26643e80u, 0x3c048073u},
{0x8014e848u, 0x2405019au, 0x24050400u},
{0x8014e84cu, 0x26645820u, 0x3c048074u},
{0x8014e854u, 0x24050019u, 0x24050040u},
{0x8014f0e0u, 0x24533e80u, 0x3c138073u}}, {
{0x801478b8u, 0x26105820u, 0x3c108074u},
{0x80149670u, 0x25173e80u, 0x3c178073u},
{0x80149c68u, 0x24515760u, 0x3c118075u},
{0x8014c964u, 0x8c427688u, 0x3c028073u},
{0x8014c96cu, 0x8c423e80u, 0x8c420000u},
{0x8014ca68u, 0x24a53e80u, 0x3c058073u},
{0x8014e180u, 0x26123ba0u, 0x3c128072u},
{0x8014e388u, 0x26643ba0u, 0x3c048072u},
{0x8014e39cu, 0x2405002eu, 0x24050080u},
{0x8014e3a0u, 0x26643e80u, 0x3c048073u},
{0x8014e3a8u, 0x2405019au, 0x24050400u},
{0x8014e3acu, 0x26645820u, 0x3c048074u},
{0x8014e3b4u, 0x24050019u, 0x24050040u},
{0x8014ec40u, 0x24533e80u, 0x3c138073u}}};

  static constexpr std::uint32_t kPoolPatch[][3] = {
      {0x800a868cu, 0x3c048014u, kPool >> 16}, {0x800a8690u, 0x2484ad88u, kPool & 0xffffu}, {0x800a8698u, 0x240500afu, kPoolN},
      {0x800a8e60u, 0x3c0a8014u, kPool >> 16}, {0x800a8e64u, 0x254aad88u, kPool & 0xffffu},
      {0x800a8e74u, 0x284200afu, kPoolN}, {0x800a8ed4u, 0x28c200afu, kPoolN}};

  bool active() const { return active_; }
  bool ru = false;

  static int patchExe(const std::string& pakPath, std::vector<std::byte>& exe) { return rufont::patchExe(pakPath, exe); }

  template <class Find> void placeFiles(Find&& find) {
    for (auto& f : files_) {
      std::uint32_t lba = 0, size = 0;
      if (!find(f.name, lba, size) || size != f.data.size()) { if (log_) std::fprintf(log_, "textos castellano: %s no encaja (%u/%zu)\n", f.name.c_str(), size, f.data.size()); continue; }
      f.lba = lba;
      if (log_) std::fprintf(log_, "textos castellano: %s sustituido (lba %u)\n", f.name.c_str(), lba);
    }
  }
  bool hasFiles() const { for (const auto& f : files_) if (f.lba) return true; return false; }

  void patchSector(std::uint32_t lba, unsigned char* raw) const {
    for (const auto& f : files_) {
      if (!f.lba || lba < f.lba) continue;
      const std::size_t off = std::size_t(lba - f.lba) * 2048u;
      if (off >= f.data.size()) continue;
      const std::size_t n = std::min<std::size_t>(2048u, f.data.size() - off);
      std::memcpy(raw + 24, f.data.data() + off, n);
    }
  }
  std::uint32_t retWatch() const { return retWatch_; }
  void onReturn(sf::psx::R3000Runtime& cpu) {
    if (log_ && retLogs_ < 4000) { ++retLogs_; std::fprintf(log_, "  -> v0 %08x (%s) idx %u\n", cpu.state().gpr[2], retStr_.c_str(), 0u); }
    retWatch_ = 1;
  }
  void setLog(FILE* log) { log_ = log; }

  void addEntry(const std::string& en, const std::string& es) { dict_[en] = es; }

  bool open(const std::string& pakPath, FILE* log) {
    log_ = log;
    std::vector<char> d;
    if (!pakfs::read(pakPath, d)) return false;
    if (d.size() < 16 || std::memcmp(d.data(), "SF3TEXTO", 8)) return false;
    std::uint32_t n; std::memcpy(&n, d.data() + 12, 4);
    std::size_t o = 16;
    for (std::uint32_t i = 0; i < n && o + 4 <= d.size(); ++i) {
      std::uint16_t le, ls; std::memcpy(&le, d.data() + o, 2); std::memcpy(&ls, d.data() + o + 2, 2); o += 4;
      if (o + le + ls > d.size()) break;
      dict_[std::string(d.data() + o, le)] = std::string(d.data() + o + le, ls);
      addFormat(std::string(d.data() + o, le), std::string(d.data() + o + le, ls));
      o += le + ls;
    }
    if (o + 4 <= d.size() && std::memcmp(d.data() + o, "NOGL", 4) == 0) o += 4;
    else if (o + 768 <= d.size()) { std::memcpy(glyphs_.data(), d.data() + o, 768); haveGlyphs_ = true; o += 768; }

    if (o + 8 <= d.size() && std::memcmp(d.data() + o, "ARCH", 4) == 0) {
      std::uint32_t nf; std::memcpy(&nf, d.data() + o + 4, 4); o += 8;
      for (std::uint32_t i = 0; i < nf && o + 2 <= d.size(); ++i) {
        std::uint16_t nl; std::memcpy(&nl, d.data() + o, 2); o += 2;
        if (o + nl + 4 > d.size()) break;
        std::string name(d.data() + o, nl); o += nl;
        std::uint32_t sz; std::memcpy(&sz, d.data() + o, 4); o += 4;
        if (o + sz > d.size()) break;
        files_.push_back({name, std::vector<std::uint8_t>(d.begin() + std::ptrdiff_t(o), d.begin() + std::ptrdiff_t(o + sz)), 0});
        o += sz;
      }
    }

    if (o + 8 <= d.size() && std::memcmp(d.data() + o, "EXEP", 4) == 0) {
      std::uint32_t np; std::memcpy(&np, d.data() + o + 4, 4); o += 8;
      for (std::uint32_t i = 0; i < np && o + 8 <= d.size(); ++i) {
        std::uint32_t off, len; std::memcpy(&off, d.data() + o, 4); std::memcpy(&len, d.data() + o + 4, 4); o += 8;
        if (o + len > d.size()) break;
        o += len;
      }
    }
    active_ = true;
    if (log) std::fprintf(log, "textos castellano: %zu textos de %s\n", dict_.size(), pakPath.c_str());
    return true;
  }

  std::function<void(const std::string&)> on_text;
  std::unordered_map<std::string, std::string> dyn;

  void onPrint(sf::psx::R3000Runtime& cpu) {
    const auto& st = cpu.state();
    const std::uint32_t box = st.gpr[4], str = st.gpr[5];
    std::string s;
    for (std::uint32_t i = 0; i < 1024; ++i) {
      std::uint8_t c = 0;
      if (!cpu.read8(str + i, c) || c == 0) break;
      s.push_back(char(c));
    }
    if (on_text) on_text(s);
    bool small = false;
    if (box) { std::uint32_t w = 0; if (cpu.read32(box, w)) small = (w & 0x10) != 0; }
    if (static const bool tl = std::getenv("SF3N_TEXTLOG") != nullptr; tl && log_ && (seen_.insert(s).second || std::getenv("SF3N_TEXTALL"))) {
      std::fprintf(log_, "TEXTO %08x %s ra %08x \"", str, small ? "peq" : "gra", st.gpr[31]);
      for (unsigned char c : s) { if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') std::fputc(c, log_); else std::fprintf(log_, "\\x%02x", c); }
      std::fprintf(log_, "\"\n");
    }
    if (std::getenv("SF3N_TEXTRET")) {
      retWatch_ = st.gpr[31]; retStr_ = s.substr(0, 20);
      std::uint32_t bw = 0, bh = 0, h = 0; if (box) { cpu.read32(box + 0xc, bw); }
      if (st.gpr[7]) cpu.read32(st.gpr[7], h);
      if (log_ && retLogs_ < 4000) { std::uint32_t r2 = 0; cpu.read32(st.gpr[29] + 0x34, r2); std::fprintf(log_, "  caja %08x wh %08x a2 %08x a3 %08x *a3 %08x ra2 %08x\n", box, bw, st.gpr[6], st.gpr[7], h, r2); }
    }
    if (!active_) { restoreOriginal(cpu); return; }
    ensureGlyphs(cpu);
    std::string out;
    if (auto d = dyn.find(s); d != dyn.end()) out = d->second;
    else if (auto it = dict_.find(s); it != dict_.end()) out = it->second;
    else if (!translateFormat(s, out) && !translatePieces(s, out)) {
      if (ru && rufont::needsTranslit(s)) {
        out = rufont::latinToRu(s);
        if (static const bool tl = std::getenv("SF3N_TEXTLOG") != nullptr; tl && log_ && missing_.insert(s).second) std::fprintf(log_, "TRANSLITERADO \"%s\"\n", s.c_str());
        cpu.setRegister(5, place(cpu, out));
        return;
      }
      if (static const bool tl = std::getenv("SF3N_TEXTLOG") != nullptr; tl && log_ && missing_.insert(s).second) {
        std::fprintf(log_, "SIN TRADUCIR \"");
        for (unsigned char c : s) { if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') std::fputc(c, log_); else std::fprintf(log_, "\\x%02x", c); }
        std::fprintf(log_, "\"\n");
      }
      return;
    }
    if (small) out = foldAccents(out);
    if (static const int bl = std::getenv("SF3N_LARGOLOG") ? std::atoi(std::getenv("SF3N_LARGOLOG")) : -1; bl >= 0 && log_ && int(s.size()) > bl) {
      auto esc = [&](const std::string& t) { for (unsigned char c : t) { if (c == '\n') std::fputs("\\n", log_); else if (c >= 0x20 && c < 0x7f) std::fputc(c, log_); else std::fprintf(log_, "\\x%02x", c); } };
      std::fprintf(log_, "LARGO EN \""); esc(s); std::fprintf(log_, "\"\nLARGO ES \""); esc(out); std::fprintf(log_, "\"\n");
    }
    const std::uint32_t slot = place(cpu, out);
    cpu.setRegister(5, slot);
    if (st.gpr[7] && st.gpr[31] == 0x800a9c70u) {
      std::uint32_t ra2 = 0;
      (void)cpu.read32(st.gpr[29] + 0x24, ra2);
      if (static const bool hl = std::getenv("SF3N_HUDTXTLOG") != nullptr; hl && log_) { static int nl = 0; if (nl++ < 20) { std::uint32_t t0 = 0; cpu.read32(kHudTab, t0); char sb[16] = {}; for (int q = 0; q < 12; ++q) { std::uint8_t c = 0; cpu.read8(slot + q, c); sb[q] = c >= 32 && c < 127 ? char(c) : '.'; } std::uint32_t p0 = 0, p1 = 0, p2 = 0; cpu.read32(kHudBufs, p0); cpu.read32(kHudBufs + 4, p1); cpu.read32(kHudBufs + 8, p2); std::fprintf(log_, "hudtxt \"%s\" a3 %08x ra2 %08x tab0 %08x slot %08x [%s] buf %08x %08x %08x\n", s.c_str(), st.gpr[7], ra2, t0, slot, sb, p0, p1, p2); } }
      static const bool sinBuf = std::getenv("SF3N_SIN_HUDBUF") != nullptr;
      if (!sinBuf && (ra2 == 0x8005b0c0u || ra2 == 0x8005b324u || ra2 == 0x8005b568u)) hudBuffer(cpu, st.gpr[7], ra2 == 0x8005b0c0u ? 0 : ra2 == 0x8005b324u ? 1 : 2);
    }
  }

  void perField(sf::psx::R3000Runtime& cpu) {
    if (!active_) return;
    for (const auto& t : kMenuPatch) {
      std::uint32_t w = 0;
      (void)cpu.read32(t[0][0], w);
      if (w != t[0][1]) continue;
      bool all = true;
      for (const auto& p : t) { (void)cpu.read32(p[0], w); if (w != p[1]) { all = false; break; } }
      if (!all) continue;
      initPrims(cpu, kMenuLabels, 0x80); initPrims(cpu, kMenuPanel, 0x400); initPrims(cpu, kMenuFoot, 0x40); initPrims(cpu, kMenuSub, kMenuSubN);
      for (const auto& p : t) (void)cpu.write32(p[0], p[2]);
      if (log_) std::fprintf(log_, "textos castellano: menu de pausa con zonas grandes\n");
    }
  }

  void onInitPrims(sf::psx::R3000Runtime& cpu) {
    if (active_ && cpu.state().gpr[4] == kMenuPanel) initPrims(cpu, kMenuSub, kMenuSubN);
  }

  static void initPrims(sf::psx::R3000Runtime& cpu, std::uint32_t a, std::uint32_t n) {
    for (std::uint32_t i = 0; i < n; ++i, a += 16) {
      (void)cpu.write32(a, 0); (void)cpu.write8(a + 4, 0); (void)cpu.write8(a + 5, 0); (void)cpu.write8(a + 6, 0); (void)cpu.write8(a + 0xf, 8);
    }
  }

  static constexpr std::uint32_t kUpdate = 0x800aac9cu;
  void onUpdate(sf::psx::R3000Runtime& cpu) {
    const auto& st = cpu.state();
    std::string s;
    for (std::uint32_t i = 0; i < 1024; ++i) { std::uint8_t c = 0; if (!cpu.read8(st.gpr[5] + i, c) || c == 0) break; s.push_back(char(c)); }
    if (static const bool tl = std::getenv("SF3N_TEXTLOG") != nullptr; tl && log_ && seenUpd_.insert(s).second) {
      std::fprintf(log_, "CAMBIO ra %08x \"", st.gpr[31]);
      for (unsigned char c : s) { if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') std::fputc(c, log_); else std::fprintf(log_, "\\x%02x", c); }
      std::fprintf(log_, "\"\n");
    }
    if (!active_ || s.empty()) return;
    std::string out;
    if (auto it = dict_.find(s); it != dict_.end()) out = it->second;
    else if (!translateFormat(s, out) && !translatePieces(s, out)) return;
    if (out.empty()) return;
    cpu.setRegister(5, place(cpu, out));
  }

  void clearHud(sf::psx::R3000Runtime& cpu) { for (std::uint32_t i = 0; i <= kHudN; ++i) (void)cpu.write32(kHudTab + 4u * i, 0); }

  void muteHudBufs(sf::psx::R3000Runtime& cpu) {
    if (!std::getenv("SF3N_APAGAR_HUD")) return;
    for (std::uint32_t a = kHudBufs; a < kHudBufs + kHudN * kHudBuf; a += 16) (void)cpu.write8(a + 7, 0);
  }
  void resync(sf::psx::R3000Runtime& cpu) {

    muteHudBufs(cpu);
    placed_.clear(); next_ = kSlots;
    while (next_ < kSlotsEnd) {
      std::string t;
      for (std::uint32_t i = 0; next_ + i < kSlotsEnd; ++i) { std::uint8_t c = 0; (void)cpu.read8(next_ + i, c); if (!c) break; t.push_back(char(c)); }
      if (t.empty()) break;
      placed_.emplace(t, next_);
      next_ += (std::uint32_t(t.size()) + 4u) & ~3u;
    }
  }

private:

  struct Format { std::vector<std::string> parts; std::vector<char> kinds; std::string es; };
  std::vector<Format> formats_;
  static void splitFormat(const std::string& f, std::vector<std::string>& parts, std::vector<char>& kinds) {
    std::string cur;
    for (std::size_t i = 0; i < f.size(); ++i) {
      if (f[i] == '%' && i + 1 < f.size() && (f[i + 1] == 'd' || f[i + 1] == 's')) { parts.push_back(cur); cur.clear(); kinds.push_back(f[i + 1]); ++i; }
      else if (f[i] == '%' && i + 1 < f.size() && f[i + 1] == '%') { cur.push_back('%'); ++i; }
      else cur.push_back(f[i]);
    }
    parts.push_back(cur);
  }
  void addFormat(const std::string& en, const std::string& es) {
    Format f; splitFormat(en, f.parts, f.kinds);
    if (f.kinds.empty()) return;
    std::vector<std::string> p2; std::vector<char> k2; splitFormat(es, p2, k2);
    if (k2 != f.kinds) return;
    f.es = es; formats_.push_back(std::move(f));
  }
  bool translateFormat(const std::string& s, std::string& out, int depth = 0) const {
    for (const auto& f : formats_) {
      if (s.compare(0, f.parts[0].size(), f.parts[0]) != 0) continue;
      std::size_t pos = f.parts[0].size();
      std::vector<std::string> vals; bool ok = true;
      for (std::size_t k = 0; k < f.kinds.size() && ok; ++k) {
        const std::string& next = f.parts[k + 1];
        std::size_t end;
        if (f.kinds[k] == 'd') {
          end = pos; if (end < s.size() && s[end] == '-') ++end;
          while (end < s.size() && s[end] >= '0' && s[end] <= '9') ++end;
          if (end == pos) { ok = false; break; }
        } else {
          end = next.empty() ? s.size() : s.find(next, pos);
          if (end == std::string::npos) { ok = false; break; }
        }
        vals.push_back(s.substr(pos, end - pos));
        if (s.compare(end, next.size(), next) != 0) { ok = false; break; }
        pos = end + next.size();
      }
      if (!ok || pos != s.size()) continue;

      for (std::size_t k = 0; k < vals.size() && depth == 0; ++k) {
        if (f.kinds[k] != 's') continue;
        if (auto it = dict_.find(vals[k]); it != dict_.end()) vals[k] = it->second;
        else if (std::string t; translateFormat(vals[k], t, 1)) vals[k] = t;
      }
      out.clear(); std::size_t vi = 0;
      for (std::size_t i = 0; i < f.es.size(); ++i) {
        if (f.es[i] == '%' && i + 1 < f.es.size() && (f.es[i + 1] == 'd' || f.es[i + 1] == 's')) { if (vi < vals.size()) out += vals[vi++]; ++i; }
        else if (f.es[i] == '%' && i + 1 < f.es.size() && f.es[i + 1] == '%') { out.push_back('%'); ++i; }
        else out.push_back(f.es[i]);
      }
      return true;
    }
    return false;
  }

  std::uint32_t place(sf::psx::R3000Runtime& cpu, const std::string& out) {
    std::uint32_t slot;
    if (auto at = placed_.find(out); at != placed_.end()) slot = at->second;
    else {
      const std::uint32_t need = (std::uint32_t(out.size()) + 4u) & ~3u;
      if (next_ + need > kSlotsEnd) { placed_.clear(); next_ = kSlots; }
      slot = next_; next_ += need;
      placed_[out] = slot;
    }
    std::uint8_t first = 0;
    (void)cpu.read8(slot, first);
    if (first != std::uint8_t(out[0])) {
      for (std::size_t i = 0; i < out.size(); ++i) (void)cpu.write8(slot + std::uint32_t(i), std::uint8_t(out[i]));
      (void)cpu.write8(slot + std::uint32_t(out.size()), 0);
    }
    return slot;
  }

  void hudBuffer(sf::psx::R3000Runtime& cpu, std::uint32_t a3, int kind) {
    const std::uint32_t h = ((a3 >> 5) * 0x9E3779B1u) >> 29;

    auto inUse = [&](std::uint32_t b) {
      for (std::uint32_t e = 0; e < 64; ++e) {
        const std::uint32_t ent = kTextTab + e * 0x1cu; std::uint32_t p = 0; std::uint8_t fl = 0;
        (void)cpu.read32(ent, p); (void)cpu.read8(ent + 0x14, fl);
        if ((fl & 1u) && p - b < kHudBuf) return true;
      }
      return false;
    };
    std::uint32_t k = 0;
    static const bool sinLibre = std::getenv("SF3N_HUDBUF_FIJO") != nullptr;
    if (!sinLibre) while (k < 8u && inUse(kHudBufs + (std::uint32_t(kind) * 8u + ((h + k) & 7u)) * kHudBuf)) ++k;
    if (k == 8u) k = 0;
    const std::uint32_t i = std::uint32_t(kind) * 8u + ((h + k) & 7u);
    const std::uint32_t buf = kHudBufs + i * kHudBuf;
    if (static const bool hl = std::getenv("SF3N_HUDBUFLOG") != nullptr; hl && log_) {
      static const std::uint32_t offs[3] = {0x11c, 0x17c, 0x1dc}, hoff[3] = {0x110, 0x114, 0x118};
      const std::uint32_t obj = a3 - offs[kind]; std::uint32_t hnd = 0, cnt = 0; cpu.read32(obj + hoff[kind], hnd); cpu.read32(0x801223b8u, cnt);
      std::uint32_t users = 0;
      for (std::uint32_t e = 0; e < 64; ++e) { std::uint32_t b = 0; std::uint8_t fl = 0; cpu.read32(kTextTab + e * 0x1c, b); cpu.read8(kTextTab + e * 0x1c + 0x14, fl); if (b - buf < kHudBuf && (fl & 1)) users |= 1u << (e & 31); }
      std::fprintf(log_, "hudbuf clase %d a3 %08x buf %08x asa %08x cnt %x vivos %08x\n", kind, a3, buf, hnd, cnt, users);
    }
    initPrims(cpu, buf, kHudBuf / 16u);
    cpu.setRegister(7, buf);
  }

  void restoreOriginal(sf::psx::R3000Runtime& cpu) {
    std::uint32_t w0 = 0;
    (void)cpu.read32(kGlyph, w0);
    if (w0 != (0x08000000u | ((kStub >> 2) & 0x03ffffffu))) return;
    (void)cpu.write32(kGlyph, 0x308200ffu);
    (void)cpu.write32(kGlyph + 4, 0x2443ffdfu);
    for (const auto& p : kPoolPatch) (void)cpu.write32(p[0], p[1]);
    for (const auto& t : kMenuPatch) {
      std::uint32_t w = 0;
      (void)cpu.read32(t[0][0], w);
      if (w == t[0][2]) for (const auto& p : t) (void)cpu.write32(p[0], p[1]);
    }
    if (log_) std::fprintf(log_, "textos: codigo original restaurado\n");
  }

  void ensureGlyphs(sf::psx::R3000Runtime& cpu) {
    if (!haveGlyphs_) return;
    std::uint32_t w0 = 0, t0 = 0;
    (void)cpu.read32(kGlyph, w0);
    (void)cpu.read32(kStub, t0);
    const std::uint32_t jump = 0x08000000u | ((kStub >> 2) & 0x03ffffffu);
    if (w0 == jump && t0 == kStubCode[0]) return;
    for (const auto& p : kPoolPatch) {
      std::uint32_t w = 0;
      (void)cpu.read32(p[0], w);
      if (w == p[1]) (void)cpu.write32(p[0], (w & 0xffff0000u) | p[2]);
      else if (log_) std::fprintf(log_, "textos castellano: %08x no esperado (%08x)\n", p[0], w);
    }
    for (std::size_t i = 0; i < kStubCode.size(); ++i) (void)cpu.write32(kStub + std::uint32_t(4 * i), kStubCode[i]);
    for (std::size_t i = 0; i < glyphs_.size(); ++i) (void)cpu.write8(kTable + std::uint32_t(i), glyphs_[i]);
    (void)cpu.write32(kGlyph, jump);
    (void)cpu.write32(kGlyph + 4, 0);
    if (log_) std::fprintf(log_, "textos castellano: fuente con acentos activada\n");
  }

  static constexpr std::array<std::uint32_t, 13> kStubCode = {
      0x308200ffu, 0x00021840u, 0x00621821u, 0x3c028070u, 0x24420100u, 0x00431021u,
      0x90430002u, 0x00000000u, 0x14600002u, 0x00000000u, 0x00001021u, 0x03e00008u, 0x00000000u};

  bool translatePieces(const std::string& s, std::string& out) const {
    bool any = false;
    std::size_t i = 0;
    while (i < s.size()) {
      std::size_t j = i;
      while (j < s.size() && !(s[j] == '\r' || s[j] == '\n' || (s[j] == ' ' && j + 1 < s.size() && s[j + 1] == ' '))) ++j;
      const std::string piece = s.substr(i, j - i);
      const auto colon = piece.find(": ");
      auto itn = piece.empty() ? dict_.end() : dict_.find("\n" + piece);
      if (auto it = dict_.find(piece); !piece.empty() && it != dict_.end()) { out += it->second; any = true; }
      else if (itn != dict_.end()) { const auto& t = itn->second; out += (!t.empty() && t[0] == '\n') ? t.substr(1) : t; any = true; }
      else if (colon != std::string::npos && colon > 0) {

        const std::string a = piece.substr(0, colon), b = piece.substr(colon + 2);
        const auto ia = dict_.find(a), ib = dict_.find(b);
        std::string tb;
        const bool okb = ib != dict_.end() || translateFormat(b, tb, 1);
        if (ia != dict_.end() || okb) any = true;
        out += (ia != dict_.end() ? ia->second : a) + ": " + (ib != dict_.end() ? ib->second : okb ? tb : b);
      } else {

        bool pieceOk = false;
        std::size_t e = piece.size();
        while (e > 0 && (piece[e - 1] == ':' || piece[e - 1] == ' ' || piece[e - 1] == '.')) --e;
        if (e > 0 && e < piece.size()) {
          if (auto it2 = dict_.find(piece.substr(0, e)); it2 != dict_.end()) { out += it2->second + piece.substr(e); any = true; pieceOk = true; }
          else out += piece;
        } else out += piece;
        if (static const bool tl = std::getenv("SF3N_TEXTLOG") != nullptr; !pieceOk && tl && log_ && !piece.empty() && piece.size() > 3 && const_cast<TextosEs*>(this)->missing_.insert(piece).second) {
          std::fprintf(log_, "TROZO SIN TRADUCIR \"");
          for (unsigned char c : piece) { if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') std::fputc(c, log_); else std::fprintf(log_, "\\x%02x", c); }
          std::fprintf(log_, "\"\n");
        }
      }
      std::size_t k = j;
      while (k < s.size() && (s[k] == '\r' || s[k] == '\n' || s[k] == ' ')) ++k;
      out += s.substr(j, k - j);
      i = k;
    }
    return any;
  }
  static std::string foldAccents(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
      static const char* from = "\xe1\xe9\xed\xf3\xfa\xfc\xf1\xc1\xc9\xcd\xd3\xda\xdc\xd1\xe0\xe8\xec\xf2\xf9";
      static const char* to = "aeiouunAEIOUUNaeiou";
      if (c == 0xa1 || c == 0xbf) continue;
      const char* p = std::strchr(from, char(c));
      o.push_back(p && c ? to[p - from] : char(c));
    }
    return o;
  }
  struct File { std::string name; std::vector<std::uint8_t> data; std::uint32_t lba; };
  std::vector<File> files_;
  std::uint32_t retWatch_ = 1; int retLogs_ = 0; std::string retStr_;
  bool active_ = false, haveGlyphs_ = false;
  FILE* log_ = nullptr;
  std::unordered_map<std::string, std::string> dict_;
  std::array<std::uint8_t, 768> glyphs_{};
  std::set<std::string> seen_, missing_, seenUpd_;
  std::uint32_t next_ = kSlots;
  std::unordered_map<std::string, std::uint32_t> placed_;
};
