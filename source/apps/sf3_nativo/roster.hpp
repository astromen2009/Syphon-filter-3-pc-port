#pragma once

#include "vfs.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
inline bool g_roster_ingles = false;
inline FILE* rosterDbg() { static FILE* f = std::getenv("SF3N_ROSTERLOG") ? std::fopen("roster_log.txt", "w") : nullptr; return f; }

struct RosterAddrs {
  std::uint32_t idxP1, mpBase0, mpBase1, mode;
  std::uint32_t namePtrVar, mgNameTable, mpNamePtrVar, mpFileTable;
  std::uint32_t hookRight, hookLeft, hookStart, hookCopy, hookCopyPrev;
  std::uint32_t mpSel, mpSet, mpStrcpy, mpAfter, mpStart0;
  std::uint32_t opLeft, opStrcpy, opCopyPrev;
};
inline constexpr RosterAddrs kRosterUsa{
    0x8012d6b4u, 0x8012d6acu, 0x8012d6f0u, 0x80122374u,
    0x8015c77cu, 0x8015c728u, 0x8015c778u, 0x8015c6bcu,
    0x80156d44u, 0x80156eb0u, 0x8015a11cu, 0x8015679cu, 0x80156794u,
    0x8015b718u, 0x8015b7a0u, 0x8015b7acu, 0x8015b7b0u, 0x8015a0f8u,
    0xa032d6b4u, 0x0c03ec0au, 0x8c25c728u};

struct ExtraRoster {
  RosterAddrs A = kRosterUsa;
  void setUsa(bool) { A = kRosterUsa; }
  struct Entry {
    std::string stem, name;
    std::vector<std::uint16_t> portrait;
    std::vector<std::uint8_t> rgb;
    std::vector<std::uint16_t> from_bmp;
    std::uint32_t pal_hash = 0;
    bool female = false;
  };
  std::vector<Entry> mg;
  int v = -1;
  bool active = false;
  std::uint32_t orig_name_ptr = 0;
  std::vector<std::uint16_t> orig_portrait;
  bool portrait_ours = false;

  static constexpr int kGameCount = 12, kCarrier = 0;
  static constexpr std::uint32_t kHookRightOp = 0xa152ff78u;

  static constexpr std::uint32_t kNames = 0x80002000u, kStems = 0x80003000u;

  static constexpr int kPortX = 384, kPortY = 112, kPortW = 32, kPortH = 56;

  struct Item { int native = -1, virt = -1; std::string name; };
  std::vector<Item> mgItems, mpItems;
  int total() const { return int(mgItems.size()); }
  int gOf(int i) const { return mgItems[std::size_t(i)].native >= 0 ? mgItems[std::size_t(i)].native : kCarrier; }
  bool virtOf(int i) const { return i >= 0 && i < total() && mgItems[std::size_t(i)].native < 0; }
  int findMg(int idx) const { for (int i = 0; i < total(); ++i) if (mgItems[std::size_t(i)].native == idx) return i; return -1; }
  void syncMg(int idx) { if (v < 0 || v >= total() || gOf(v) != idx) v = findMg(idx); }

  static std::string mgNativeOf(const std::string& st) {
    if (st == "GABE") return "GABES";
    if (st == "TERESA") return "TERES";
    if (st == "GABNIN") return "GABEN";
    return st;
  }
  static std::string mpNativeOf(const std::string& st) {
    if (st == "GABES") return "GABE";
    if (st == "TERES") return "TERESA";
    if (st == "GABEN") return "GABNIN";
    return st;
  }

  static constexpr const char* kMgNative[12] = {"GABES", "LIAN", "GABED", "ELSA", "GABEJ", "LARRY", "GABEL", "TERES", "CHANCE", "LIANG", "BAGLAD", "GABEN"};
  static constexpr const char* kMpNative[27] = {"GABE", "LIAN", "GIRDEU", "BAGLAD", "CHANCE", "CHEMO", "CORPSE", "ELSA", "GABNIN", "GREG", "KGB", "LARRY", "LEPER",
                                                "MARA", "MARCOS", "MEX", "MIB", "MONK", "MP", "PHAGSM", "RAMIR", "RHOEME", "SCIEVL", "SPOOK", "SWATG", "TERESA", "VLAD"};

  std::vector<std::pair<std::string, bool>> gender_;

  bool isFemaleStem(const std::string& stem) const {
    for (const auto& [n, f] : gender_) if (f && (stem == n || stem.rfind(n, 0) == 0)) return true;
    return false;
  }
  void load(const std::filesystem::path& dir, FILE* log) {
    std::ifstream f(dir / "personajes.txt");
    std::string line;
    auto inList = [](const std::string& st, const char* const* l, int n) { for (int i = 0; i < n; ++i) if (st == l[i]) return true; return false; };
    while (std::getline(f, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::vector<std::string> fld;
      for (std::size_t p = 0;;) { const auto q = line.find(';', p); fld.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
      if (fld.size() < 3) continue;
      const std::string mpStem = fld[1];
      std::string sex = fld.size() > 3 ? fld[3] : "H";
      while (!sex.empty() && (sex.back() == ' ' || sex.back() == '\t')) sex.pop_back();
      while (!sex.empty() && (sex[0] == ' ' || sex[0] == '\t')) sex.erase(0, 1);
      const bool female = !sex.empty() && (sex[0] == 'M' || sex[0] == 'm' || sex[0] == 'F' || sex[0] == 'f');

      std::string shown = fld[2];
      if (fld.size() > 4 && g_roster_ingles) {
        std::string en = fld[4];
        while (!en.empty() && (en.back() == ' ' || en.back() == '\t')) en.pop_back();
        while (!en.empty() && (en[0] == ' ' || en[0] == '\t')) en.erase(0, 1);
        if (!en.empty()) shown = en;
      }
      std::string clean;
      for (unsigned char c : shown) if (c >= 32 && c < 127) clean += char(c);
      clean = clean.substr(0, 30);
      const auto rgb = readBmpRgb(dir / "retratos" / (mpStem + ".bmp"));
      auto idxIn = [](const std::string& st, const char* const* l, int n) { for (int i = 0; i < n; ++i) if (st == l[i]) return i; return -1; };

      if (const int ni = idxIn(mgNativeOf(mpStem), kMgNative, 12); ni >= 0) {
        bool dup = false; for (const auto& it : mgItems) dup = dup || it.native == ni;
        if (!dup && mgItems.size() < 120) mgItems.push_back({ni, -1, clean});
      } else {
        Entry e;
        e.stem = "MG" + mpStem; e.name = clean; e.female = female; e.rgb = rgb;
        if (mgItems.size() < 120 && e.stem.size() <= 8 && virtualDisc().find("MINIGAME/" + e.stem + ".HOG")) {
          if (const auto* t = virtualDisc().find("RETRATOS/" + e.stem + ".TIM"); t && t->data.size() >= 20) e.portrait = readPortrait("RETRATOS/" + e.stem + ".TIM");
          gender_.push_back({e.stem, female});
          mgItems.push_back({-1, int(mg.size()), clean});
          mg.push_back(e);
        }
      }

      if (const int ni = idxIn(mpNativeOf(mpStem), kMpNative, 27); ni >= 0) {
        bool dup = false; for (const auto& it : mpItems) dup = dup || it.native == ni;
        if (!dup && mpItems.size() < 120) mpItems.push_back({ni, -1, clean});
      } else if (mpItems.size() < 120 && mpStem.size() <= 6 && virtualDisc().find("MPLAYER/" + mpStem + "1.HOG")) {
        Entry m;
        m.stem = mpStem; m.name = clean; m.female = female; m.rgb = rgb;
        m.portrait = readPortrait("RETRATOS/" + mpStem + ".TIM");
        gender_.push_back({mpStem, female});
        mpItems.push_back({-1, int(mp.size()), clean});
        mp.push_back(std::move(m));
      }
    }

    auto addNatives = [](std::vector<Item>& items, int n) {
      bool any = false; for (const auto& it : items) any = any || it.native >= 0;
      if (any) return;
      std::vector<Item> v; for (int i = 0; i < n; ++i) v.push_back({i, -1, std::string()});
      items.insert(items.begin(), v.begin(), v.end());
    };
    addNatives(mgItems, 12); addNatives(mpItems, 27);
    if (log && (!mg.empty() || !mp.empty())) std::fprintf(log, "personajes: %zu anadidos al minijuego y %zu al multijugador\n", mg.size(), mp.size());
  }

  static constexpr std::uint32_t kGenderPc = 0x801639e8u;
  template <class Cpu> bool onPcGender(Cpu& cpu, std::uint32_t pc) {
    if (pc != kGenderPc || gender_.empty()) return false;
    if (r32(cpu, pc) != 0x26310004u || r32(cpu, pc - 4) != 0xae020008u || r32(cpu, pc - 8) != 0x34421000u) return false;
    const auto st = cpu.state().gpr[16];
    if (st != A.mpBase0 && st != A.mpBase1) return false;
    std::string nm;
    for (std::uint32_t i = 0; i < 8; ++i) { const auto c = r8(cpu, st + i); if (!c) break; nm.push_back(char(c)); }
    for (const auto& [n, fem] : gender_) {
      if (n != nm) continue;
      auto w = r32(cpu, st + 8);
      w = fem ? (w | 0x1000u) : (w & ~0x1000u);
      (void)cpu.write32(st + 8, w);
      if (std::getenv("SF3N_ROSTERLOG")) std::fprintf(stderr, "ROSTER sexo %s %s\n", nm.c_str(), fem ? "mujer" : "hombre");
      return true;
    }
    return false;
  }

  static std::vector<std::uint8_t> readBmpRgb(const std::filesystem::path& p) {
    std::vector<std::uint8_t> out;
    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](std::size_t o) { return o + 1 < b.size() ? unsigned(b[o] | (b[o + 1] << 8)) : 0u; };
    auto u32 = [&](std::size_t o) { return o + 3 < b.size() ? std::uint32_t(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (std::uint32_t(b[o + 3]) << 24)) : 0u; };
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return out;
    const std::uint32_t off = u32(10);
    const int w = int(std::int32_t(u32(18))), hraw = int(std::int32_t(u32(22)));
    const unsigned bpp = u16(28), comp = u32(30);
    if (w <= 0 || hraw == 0 || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) return out;
    const int h = hraw < 0 ? -hraw : hraw;
    const std::size_t stride = (std::size_t(w) * (bpp / 8) + 3) & ~std::size_t(3);
    if (off + stride * std::size_t(h) > b.size()) return out;
    const int W = kPortW * 2, H = kPortH;
    out.assign(std::size_t(W * H * 3), 0);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {

        const int x0 = x * w / W, x1 = std::max(x0 + 1, (x + 1) * w / W), y0 = y * h / H, y1 = std::max(y0 + 1, (y + 1) * h / H);
        unsigned r = 0, g = 0, bl = 0, n = 0;
        for (int sy = y0; sy < y1; ++sy)
          for (int sx = x0; sx < x1; ++sx) {
            const int row = hraw > 0 ? h - 1 - sy : sy;
            const std::size_t o = off + std::size_t(row) * stride + std::size_t(sx) * (bpp / 8);
            bl += b[o]; g += b[o + 1]; r += b[o + 2]; ++n;
          }
        const std::size_t d = std::size_t((y * W + x) * 3);
        out[d] = std::uint8_t(r / n); out[d + 1] = std::uint8_t(g / n); out[d + 2] = std::uint8_t(bl / n);
      }
    return out;
  }

  static std::vector<std::uint16_t> quantizePortrait(const std::vector<std::uint8_t>& rgb, const std::vector<std::uint16_t>& vram) {
    std::vector<std::uint16_t> out;
    if (rgb.size() != std::size_t(kPortW * 2 * kPortH * 3) || vram.size() < 1024 * 512) return out;
    int pal[256][3]; bool ok[256];
    for (int i = 0; i < 256; ++i) {
      const auto c = vram[std::size_t(484) * 1024 + 768 + std::size_t(i)];
      ok[i] = c != 0;
      pal[i][0] = (c & 31) << 3; pal[i][1] = ((c >> 5) & 31) << 3; pal[i][2] = ((c >> 10) & 31) << 3;
    }
    out.assign(std::size_t(kPortW * kPortH), 0);
    for (int p = 0; p < kPortW * 2 * kPortH; ++p) {
      int best = 0, bd = 1 << 30;
      for (int i = 0; i < 256; ++i) {
        if (!ok[i]) continue;
        const int dr = pal[i][0] - rgb[std::size_t(p * 3)], dg = pal[i][1] - rgb[std::size_t(p * 3 + 1)], db = pal[i][2] - rgb[std::size_t(p * 3 + 2)];
        const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < bd) { bd = d; best = i; }
      }
      auto& hw = out[std::size_t(p / 2)];
      hw = std::uint16_t((p & 1) ? ((hw & 0x00ff) | (best << 8)) : ((hw & 0xff00) | best));
    }
    return out;
  }

  static const std::vector<std::uint16_t>& portraitOf(Entry& e, const std::vector<std::uint16_t>& vram) {
    if (!e.rgb.empty() && vram.size() >= 1024 * 512) {
      std::uint32_t h = 2166136261u;
      for (int i = 0; i < 256; ++i) h = (h ^ vram[std::size_t(484) * 1024 + 768 + std::size_t(i)]) * 16777619u;
      if (h != e.pal_hash || e.from_bmp.empty()) { e.from_bmp = quantizePortrait(e.rgb, vram); e.pal_hash = h; }
      if (!e.from_bmp.empty()) return e.from_bmp;
    }
    return e.portrait;
  }
  static std::vector<std::uint16_t> readPortrait(const std::string& path) {
    std::vector<std::uint16_t> out;
    const auto* t = virtualDisc().find(path);
    if (!t || t->data.size() < 20) return out;
    const auto& d = t->data;
    auto b8 = [&](std::size_t o) { return std::uint32_t(std::to_integer<std::uint8_t>(d[o])); };
    const std::size_t img = 8 + (b8(8) | b8(9) << 8 | b8(10) << 16 | b8(11) << 24) + 12;
    if (img + kPortW * kPortH * 2 > d.size()) return out;
    out.resize(std::size_t(kPortW * kPortH));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint16_t(b8(img + i * 2) | (b8(img + i * 2 + 1) << 8));
    return out;
  }

  static constexpr int kHiX = 512;
  static std::uint32_t hiHash(const std::vector<std::uint16_t>& vram, int sx, int sy) {
    std::uint32_t h = 2166136261u;
    for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) h = (h ^ vram[std::size_t((sy + y) & 511) * 1024 + std::size_t((sx + x) & 1023)]) * 16777619u;
    return h;
  }

  struct HiSlot { std::vector<std::uint8_t> cur, from; const std::vector<std::uint8_t>* tgt = nullptr; int step = 0;
                  std::vector<std::uint16_t> backup, written; bool lost = false; const std::vector<std::uint8_t>* lostTgt = nullptr; };
  static void hiRegion(const std::vector<std::uint16_t>& vram, int slot, std::vector<std::uint16_t>& out) {
    out.resize(std::size_t((kPortH + 1) * kPortW * 2));
    for (int y = 0; y <= kPortH; ++y) for (int x = 0; x < kPortW * 2; ++x) out[std::size_t(y * kPortW * 2 + x)] = vram[std::size_t(slot * 64 + y) * 1024 + std::size_t(kHiX + x)];
  }
  HiSlot hi_[3];
  void writeHi(std::vector<std::uint16_t>& vram, int slot, const std::vector<std::uint8_t>& target, int sx, int sy) {
    HiSlot& H = hi_[slot];
    const std::size_t n = std::size_t(kPortW * 2 * kPortH * 3);
    if (target.size() != n) { clearHi(vram, slot); return; }
    if (H.lost && &target == H.lostTgt) return;
    H.lost = false;
    {
      std::vector<std::uint16_t> now; hiRegion(vram, slot, now);
      if (H.written.empty()) H.backup = now;
      else if (now != H.written) { H.lost = true; H.lostTgt = &target; H.written.clear(); H.backup.clear(); H.cur.clear(); H.tgt = nullptr; return; }
    }
    static const int kFade = std::getenv("SF3N_SIN_FUNDIDO_RETRATO") ? 0 : 16;
    if (H.tgt != &target) {
      if (H.cur.size() == n && H.tgt && kFade > 0) { H.from = H.cur; H.step = 1; } else H.step = 0;
      H.tgt = &target;
    }
    if (H.step > 0 && H.step <= kFade) {
      H.cur.resize(n);
      const int a = H.step * 256 / kFade;
      for (std::size_t i = 0; i < n; ++i) H.cur[i] = std::uint8_t((H.from[i] * (256 - a) + target[i] * a) >> 8);
      ++H.step;
    } else { H.cur = target; H.step = 0; }
    const int y0 = slot * 64;
    for (int y = 0; y < kPortH; ++y)
      for (int x = 0; x < kPortW * 2; ++x) {
        const std::size_t o = std::size_t((y * kPortW * 2 + x) * 3);
        vram[std::size_t(y0 + y) * 1024 + std::size_t(kHiX + x)] = std::uint16_t(0x8000 | (H.cur[o] >> 3) | ((H.cur[o + 1] >> 3) << 5) | ((H.cur[o + 2] >> 3) << 10));
      }
    const std::uint32_t h = hiHash(vram, sx, sy);
    vram[std::size_t(y0 + kPortH) * 1024 + kHiX] = std::uint16_t(h & 0xffff);
    vram[std::size_t(y0 + kPortH) * 1024 + kHiX + 1] = std::uint16_t(h >> 16);
    vram[std::size_t(y0 + kPortH) * 1024 + kHiX + 2] = 0x5F3A;
    hiRegion(vram, slot, H.written);
  }
  void clearHi(std::vector<std::uint16_t>& vram, int slot) {
    HiSlot& H = hi_[slot];
    H.tgt = nullptr; H.step = 0; H.cur.clear(); H.lost = false;
    if (!H.written.empty()) {
      std::vector<std::uint16_t> now; hiRegion(vram, slot, now);
      if (now == H.written && H.backup.size() == now.size())
        for (int y = 0; y <= kPortH; ++y) for (int x = 0; x < kPortW * 2; ++x) vram[std::size_t(slot * 64 + y) * 1024 + std::size_t(kHiX + x)] = H.backup[std::size_t(y * kPortW * 2 + x)];
      H.written.clear(); H.backup.clear();
      return;
    }
    auto& m = vram[std::size_t(slot * 64 + kPortH) * 1024 + kHiX + 2];
    if (m == 0x5F3A) m = 0;
  }

  std::vector<Entry> mp;
  int vp[2] = {-1, -1}, old_[2] = {0, 0}, dir_[2] = {0, 0};
  std::uint32_t mp_orig_name[2] = {0, 0};
  std::vector<std::uint16_t> mp_orig_portrait[2];
  bool mp_portrait_ours[2] = {false, false};
  const std::vector<std::uint16_t>* mp_fade_target_[2] = {nullptr, nullptr};
  int mp_fade_step_[2] = {0, 0};
  std::vector<std::uint8_t> mp_fade_from_[2], mp_fade_to_[2];
  static constexpr int kMpCount = 27;
  static constexpr int kMpCarrier[2] = {6, 5};
  std::uint32_t mpBase(int p) const { return p == 0 ? A.mpBase0 : A.mpBase1; }
  static constexpr int kMpPortX[2] = {384, 416}, kMpPortY[2] = {56, 56};
  static constexpr std::uint32_t kMpNames = 0x80004000u, kMpStems = 0x80004800u;
  int mpTotal() const { return int(mpItems.size()); }
  int mpGOf(int p, int i) const { return mpItems[std::size_t(i)].native >= 0 ? mpItems[std::size_t(i)].native : kMpCarrier[p]; }
  bool mpVirt(int p) const { return vp[p] >= 0 && vp[p] < mpTotal() && mpItems[std::size_t(vp[p])].native < 0; }
  const Entry& mpEntry(int p) const { return mp[std::size_t(mpItems[std::size_t(vp[p])].virt)]; }
  void syncMp(int p, int idx) {
    if (vp[p] >= 0 && vp[p] < mpTotal() && mpGOf(p, vp[p]) == idx) return;
    vp[p] = -1;
    for (int i = 0; i < mpTotal(); ++i) if (mpItems[std::size_t(i)].native == idx) { vp[p] = i; break; }
  }
  std::uint32_t mp_orig_names_[kMpCount] = {};
  std::uint32_t mpNameAddr(int p) const { return kMpNames + std::uint32_t(p) * 64; }
  std::uint32_t mpStemAddr(int p) const { return kMpStems + std::uint32_t(p) * 16; }
  template <class Cpu> bool mpResident(Cpu& cpu) const {
    return r32(cpu, A.mpSel) == 0x3c0b8016u && r32(cpu, A.mpSet) == 0x3c018016u && r8(cpu, A.mode) == 1;
  }
  template <class Cpu> void mpApplyName(Cpu& cpu, int) {
    const auto tbl = r32(cpu, A.mpNamePtrVar);
    if ((tbl & 0xffe00000u) != 0x80000000u) return;
    auto mine = [&](std::uint32_t a) { return a >= kMpNames && a < kMpNames + 0x200; };
    int want[kMpCount]; for (int& w : want) w = -1;
    for (int q = 0; q < 2; ++q)
      if (vp[q] >= 0 && vp[q] < mpTotal() && !mpItems[std::size_t(vp[q])].name.empty()) want[mpGOf(q, vp[q])] = q;
    for (int i = 0; i < kMpCount; ++i) {
      const auto slot = tbl + 4 * std::uint32_t(i);
      const auto cur = r32(cpu, slot);
      if (!mine(cur)) mp_orig_names_[i] = cur;
      if (want[i] >= 0) {
        const int q = want[i];
        wstr(cpu, mpNameAddr(q), mpItems[std::size_t(vp[q])].name, 64);
        if (cur != mpNameAddr(q)) (void)cpu.write32(slot, mpNameAddr(q));
      } else if (mine(cur) && mp_orig_names_[i]) (void)cpu.write32(slot, mp_orig_names_[i]);
    }
    for (int q = 0; q < 2; ++q) if (mpVirt(q)) wstr(cpu, mpStemAddr(q), mpEntry(q).stem, 16);
  }
  template <class Cpu> bool onPcMp(Cpu& cpu, std::uint32_t pc) {
    if (mpItems.empty()) return false;
    const auto a0 = cpu.state().gpr[4];
    const int p = a0 == mpBase(0) ? 0 : a0 == mpBase(1) ? 1 : -1;
    if (pc == A.mpSel && p >= 0 && mpResident(cpu)) {
      old_[p] = r8(cpu, a0 + 8);
      dir_[p] = int(std::int32_t(cpu.state().gpr[6]));
      syncMp(p, old_[p]);
      return true;
    }
    if (pc == A.mpSet && p >= 0 && mpResident(cpu) && dir_[p] != 0) {
      const int d = dir_[p] > 0 ? 1 : -1, n = mpTotal();
      const int nv = vp[p] < 0 ? (d > 0 ? 0 : n - 1) : (vp[p] + d + n) % n;
      const int g = mpGOf(p, nv);
      vp[p] = nv;
      if (rosterDbg()) std::fprintf(rosterDbg(), "ROSTER mp j%d v %d/%d g %d %s %s\n", p + 1, nv, n, g, mpItems[std::size_t(nv)].native < 0 ? "anadido" : "original", mpItems[std::size_t(nv)].name.c_str());
      dir_[p] = 0;
      (void)cpu.write8(a0 + 8, std::uint8_t(g));
      cpu.setRegister(7, std::uint32_t(g));
      cpu.setRegister(2, std::uint32_t(g) * 4);
      mpApplyName(cpu, p);
      return true;
    }
    if (pc == A.mpAfter && p >= 0 && mpVirt(p) && r32(cpu, A.mpStrcpy) == A.opStrcpy && mpResident(cpu)) {
      cpu.setRegister(5, mpStemAddr(p));
      return true;
    }

    if (pc == A.mpStart0 && r32(cpu, pc) == 0x2604ffb4u && r8(cpu, A.mode) == 1 && mpVirt(0) &&
        r8(cpu, mpBase(0) + 8) == kMpCarrier[0] && cpu.state().gpr[5] == r32(cpu, A.mpFileTable + 4 * kMpCarrier[0])) {
      cpu.setRegister(5, mpStemAddr(0));
      return true;
    }
    if (pc == A.hookStart && a0 == mpBase(1) && r32(cpu, pc) == A.opStrcpy && r8(cpu, A.mode) == 1 && mpVirt(1) &&
        r8(cpu, mpBase(1) + 8) == kMpCarrier[1] && cpu.state().gpr[5] == r32(cpu, A.mpFileTable + 4 * kMpCarrier[1])) {
      cpu.setRegister(5, mpStemAddr(1));
      return true;
    }
    return false;
  }
  template <class Cpu> void perFieldMp(Cpu& cpu, std::vector<std::uint16_t>& vram) {
    if (mpItems.empty() || !mpResident(cpu)) return;
    for (int p = 0; p < 2; ++p) {
      const int idx = r8(cpu, mpBase(p) + 8);
      if (idx >= kMpCount) continue;
      syncMp(p, idx);
      mpApplyName(cpu, p);
      auto at = [&](int x, int y) -> std::uint16_t& { return vram[std::size_t((kMpPortY[p] + y) & 511) * 1024 + std::size_t((kMpPortX[p] + x) & 1023)]; };
      if (mpVirt(p) && !portraitOf(mp[std::size_t(mpItems[std::size_t(vp[p])].virt)], vram).empty()) {
        const auto& pt = portraitOf(mp[std::size_t(mpItems[std::size_t(vp[p])].virt)], vram);
        bool same = mp_portrait_ours[p];
        for (int y = 0; y < kPortH && same; ++y) for (int x = 0; x < kPortW && same; ++x) same = at(x, y) == pt[std::size_t(y * kPortW + x)];

        static const int kFade = std::getenv("SF3N_SIN_FUNDIDO_RETRATO") ? 0 : 16;
        if (!same && mp_portrait_ours[p] && kFade > 0 && mp_fade_target_[p] != &pt) {
          mp_fade_from_[p] = portraitRgb(vram, [&](int x, int y) { return at(x, y); });
          mp_fade_to_[p] = portraitRgb(vram, [&](int x, int y) { return pt[std::size_t(y * kPortW + x)]; });
          mp_fade_target_[p] = &pt; mp_fade_step_[p] = 1;
        }
        if (mp_fade_step_[p] > 0 && mp_fade_target_[p] == &pt && mp_fade_step_[p] <= kFade) {
          std::vector<std::uint8_t> mix(mp_fade_to_[p].size());
          const int a = mp_fade_step_[p] * 256 / kFade;
          for (std::size_t i = 0; i < mix.size(); ++i) mix[i] = std::uint8_t((mp_fade_from_[p][i] * (256 - a) + mp_fade_to_[p][i] * a) >> 8);
          const auto q = quantizePortrait(mix, vram);
          if (!q.empty()) for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = q[std::size_t(y * kPortW + x)];
          if (++mp_fade_step_[p] > kFade) { mp_fade_step_[p] = 0; for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = pt[std::size_t(y * kPortW + x)]; }
        } else if (!same) {
          if (!mp_portrait_ours[p] || mp_orig_portrait[p].empty()) {
            mp_orig_portrait[p].resize(std::size_t(kPortW * kPortH));
            for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) mp_orig_portrait[p][std::size_t(y * kPortW + x)] = at(x, y);
          }
          for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = pt[std::size_t(y * kPortW + x)];
          mp_fade_target_[p] = &pt; mp_fade_step_[p] = 0;
        }
        mp_portrait_ours[p] = true;
        writeHi(vram, 1 + p, mp[std::size_t(mpItems[std::size_t(vp[p])].virt)].rgb, kMpPortX[p], kMpPortY[p]);
      } else if (mp_portrait_ours[p] && !mp_orig_portrait[p].empty()) {
        for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = mp_orig_portrait[p][std::size_t(y * kPortW + x)];
        mp_portrait_ours[p] = false; mp_fade_step_[p] = 0; mp_fade_target_[p] = nullptr;
        clearHi(vram, 1 + p);
      }
    }
  }

  template <class Cpu> static std::uint32_t r32(Cpu& cpu, std::uint32_t a) { std::uint32_t v = 0; (void)cpu.read32(a, v); return v; }
  template <class Cpu> static std::uint8_t r8(Cpu& cpu, std::uint32_t a) { std::uint8_t v = 0; (void)cpu.read8(a, v); return v; }
  template <class Cpu> static void wstr(Cpu& cpu, std::uint32_t a, const std::string& s, std::size_t max) {
    for (std::size_t i = 0; i < max; ++i) (void)cpu.write8(a + std::uint32_t(i), i < s.size() ? std::uint8_t(s[i]) : 0);
  }
  std::uint32_t nameAddr(int k) const { return kNames + std::uint32_t(k) * 32; }
  std::uint32_t stemAddr(int k) const { return kStems + std::uint32_t(k) * 16; }

  template <class Cpu> bool resident(Cpu& cpu) const {
    return r32(cpu, A.hookRight) == kHookRightOp && r32(cpu, A.hookLeft) == A.opLeft && r8(cpu, A.mode) == 0;
  }

  template <class Cpu> bool onPc(Cpu& cpu, std::uint32_t pc) {
    if (onPcGender(cpu, pc)) return true;
    if (onPcMp(cpu, pc)) return true;
    if (mgItems.empty()) return false;
    if (pc == A.hookRight || pc == A.hookLeft) {
      if (!resident(cpu)) return false;
      const bool right = pc == A.hookRight;
      syncMg(r8(cpu, A.idxP1));
      const int n = total();
      v = v < 0 ? (right ? 0 : n - 1) : right ? (v + 1) % n : (v - 1 + n) % n;
      const int g = gOf(v);
      cpu.setRegister(18, std::uint32_t(g));
      if (rosterDbg()) std::fprintf(rosterDbg(), "ROSTER mg v %d/%d g %d %s %s\n", v, n, g, virtOf(v) ? "anadido" : "original", mgItems[std::size_t(v)].name.c_str());
      if (virtOf(v)) wstr(cpu, stemAddr(v), mg[std::size_t(mgItems[std::size_t(v)].virt)].stem, 16);
      applyName(cpu);
      return true;
    }
    if ((pc == A.hookStart || pc == A.hookCopy) && std::getenv("SF3N_ROSTERLOG")) std::fprintf(stderr, "ROSTER pc %08x v %d idx %d a1 %08x tabla %08x op %08x\n", pc, v, r8(cpu, A.idxP1), cpu.state().gpr[5], r32(cpu, A.mgNameTable + 4 * kCarrier), r32(cpu, pc));
    if (pc == A.hookStart && virtOf(v) && r32(cpu, A.hookStart) == A.opStrcpy && r8(cpu, A.idxP1) == kCarrier && cpu.state().gpr[4] == A.mpBase0) {
      if (cpu.state().gpr[5] == r32(cpu, A.mgNameTable + 4 * kCarrier)) {
        wstr(cpu, stemAddr(v), mg[std::size_t(mgItems[std::size_t(v)].virt)].stem, 16);
        cpu.setRegister(5, stemAddr(v));
        return true;
      }
    }
    if (pc == A.hookCopy && virtOf(v) && r32(cpu, A.hookCopyPrev) == A.opCopyPrev && r8(cpu, A.idxP1) == kCarrier) {
      if (cpu.state().gpr[5] == r32(cpu, A.mgNameTable + 4 * kCarrier)) {
        wstr(cpu, stemAddr(v), mg[std::size_t(mgItems[std::size_t(v)].virt)].stem, 16);
        cpu.setRegister(5, stemAddr(v));
        return true;
      }
    }
    return false;
  }

  std::uint32_t mg_orig_names_[kGameCount] = {};
  template <class Cpu> void applyName(Cpu& cpu) {
    const auto tbl = r32(cpu, A.namePtrVar);
    if ((tbl & 0xffe00000u) != 0x80000000u) return;
    for (int i = 0; i < kGameCount; ++i) {
      const auto cur = r32(cpu, tbl + 4 * std::uint32_t(i));
      if (cur < kNames || cur >= kStems) mg_orig_names_[i] = cur;
      else if (mg_orig_names_[i] && !(v >= 0 && gOf(v) == i)) (void)cpu.write32(tbl + 4 * std::uint32_t(i), mg_orig_names_[i]);
    }
    if (v < 0 || v >= total()) return;
    const auto& it = mgItems[std::size_t(v)];
    if (it.name.empty()) return;
    wstr(cpu, nameAddr(v), it.name, 32);
    const auto slot = tbl + 4 * std::uint32_t(gOf(v));
    if (r32(cpu, slot) != nameAddr(v)) (void)cpu.write32(slot, nameAddr(v));
  }

  template <class Cpu> void perField(Cpu& cpu, std::vector<std::uint16_t>& vram) {
    perFieldMp(cpu, vram);
    if (mgItems.empty()) return;
    if (static int dbg = 0; rosterDbg() && ++dbg % 50 == 0)
      std::fprintf(rosterDbg(), "ROSTER campo: mg residente %d idx %d v %d | mp residente %d j1 %d j2 %d vp %d %d | r %08x l %08x modo %d\n", int(resident(cpu)), r8(cpu, A.idxP1), v,
                   int(mpResident(cpu)), r8(cpu, mpBase(0) + 8), r8(cpu, mpBase(1) + 8), vp[0], vp[1], r32(cpu, A.hookRight), r32(cpu, A.hookLeft), r8(cpu, A.mode));
    if (!resident(cpu)) return;
    const int idx = r8(cpu, A.idxP1);
    if (idx >= kGameCount) return;
    syncMg(idx);
    applyName(cpu);

    auto at = [&](int x, int y) -> std::uint16_t& { return vram[std::size_t((kPortY + y) & 511) * 1024 + std::size_t((kPortX + x) & 1023)]; };
    if (virtOf(v) && !portraitOf(mg[std::size_t(mgItems[std::size_t(v)].virt)], vram).empty()) {
      const auto& p = portraitOf(mg[std::size_t(mgItems[std::size_t(v)].virt)], vram);
      bool same = portrait_ours;
      for (int y = 0; y < kPortH && same; ++y) for (int x = 0; x < kPortW && same; ++x) same = at(x, y) == p[std::size_t(y * kPortW + x)];

      static const int kFade = std::getenv("SF3N_SIN_FUNDIDO_RETRATO") ? 0 : 16;
      if (!same && portrait_ours && kFade > 0 && fade_target_ != &p) {
        fade_from_ = portraitRgb(vram, [&](int x, int y) { return at(x, y); });
        fade_to_ = portraitRgb(vram, [&](int x, int y) { return p[std::size_t(y * kPortW + x)]; });
        fade_target_ = &p; fade_step_ = 1;
      }
      if (fade_step_ > 0 && fade_target_ == &p && fade_step_ <= kFade) {
        std::vector<std::uint8_t> mix(fade_to_.size());
        const int a = fade_step_ * 256 / kFade;
        for (std::size_t i = 0; i < mix.size(); ++i) mix[i] = std::uint8_t((fade_from_[i] * (256 - a) + fade_to_[i] * a) >> 8);
        const auto q = quantizePortrait(mix, vram);
        if (!q.empty()) for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = q[std::size_t(y * kPortW + x)];
        if (++fade_step_ > kFade) { fade_step_ = 0; for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = p[std::size_t(y * kPortW + x)]; }
      } else if (!same) {
        if (!portrait_ours || orig_portrait.empty()) {

          orig_portrait.resize(std::size_t(kPortW * kPortH));
          for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) orig_portrait[std::size_t(y * kPortW + x)] = at(x, y);
        }
        for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = p[std::size_t(y * kPortW + x)];
        fade_target_ = &p; fade_step_ = 0;
      }
      portrait_ours = true;
      writeHi(vram, 0, mg[std::size_t(mgItems[std::size_t(v)].virt)].rgb, kPortX, kPortY);
    } else if (portrait_ours && !orig_portrait.empty()) {
      for (int y = 0; y < kPortH; ++y) for (int x = 0; x < kPortW; ++x) at(x, y) = orig_portrait[std::size_t(y * kPortW + x)];
      portrait_ours = false; fade_step_ = 0; fade_target_ = nullptr;
      clearHi(vram, 0);
    }
  }

  const std::vector<std::uint16_t>* fade_target_ = nullptr;
  int fade_step_ = 0;
  std::vector<std::uint8_t> fade_from_, fade_to_;
  template <class F> static std::vector<std::uint8_t> portraitRgb(const std::vector<std::uint16_t>& vram, F px) {
    std::vector<std::uint8_t> out(std::size_t(kPortW * 2 * kPortH * 3));
    for (int y = 0; y < kPortH; ++y)
      for (int x = 0; x < kPortW * 2; ++x) {
        const std::uint16_t hw = px(x / 2, y);
        const int idx = (x & 1) ? (hw >> 8) : (hw & 0xff);
        const auto c = vram[std::size_t(484) * 1024 + 768 + std::size_t(idx)];
        const std::size_t d = std::size_t((y * kPortW * 2 + x) * 3);
        out[d] = std::uint8_t((c & 31) << 3); out[d + 1] = std::uint8_t(((c >> 5) & 31) << 3); out[d + 2] = std::uint8_t(((c >> 10) & 31) << 3);
      }
    return out;
  }
};

inline ExtraRoster& extraRoster() {
  static ExtraRoster r;
  return r;
}
