// Syphon Filter 3 PC: ventana, renderizador nativo, entrada, menus y HUD.
#pragma once
#ifndef SF3_VERSION
#define SF3_VERSION "0.9.0"
#endif
#define SF3_TITULO "Syphon Filter 3 - PC " SF3_VERSION

#ifndef SF3_INI
#define SF3_INI "sf3_nativo.ini"
#define SF3_PARTIDAS "partidas"
#define SF3_REGISTRO "sf3_nativo_registro.txt"
#endif

#include <PsyX/PsyX_globals.h>
#include <PsyX/PsyX_public.h>
#include <PsyX/PsyX_render.h>
#include <psx/libetc.h>
#include <psx/libgpu.h>
#include <psx/libgte.h>

#include <SDL.h>

#include "sf/assets/emd_scene.hpp"
#include "sf/assets/fog_archive.hpp"
#include "sf/assets/hog_archive.hpp"
#include "sf/assets/hmd_model.hpp"
#include "sf/assets/gmd_model.hpp"
#include <variant>
#include "sf/disc/iso9660.hpp"
#include "sf/psx/r3000_runtime.hpp"
#include "soft_gpu.hpp"
#include "vfs.hpp"
#include "recursos.hpp"
#include "title_hq.hpp"
#include "pc_textos.hpp"
#include "ru_fuente.hpp"

#include <atomic>
#include <deque>
namespace sf::psx { extern std::atomic<int> g_vol_spu, g_vol_cd; }
inline const std::string* g_idioma_actual = nullptr;
inline std::string* g_idioma_mut = nullptr;

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "pc_menu.hpp"
#include "armas.hpp"
#include "armas_sonido.hpp"
#include "controls.hpp"
#include <set>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

extern SDL_Window* g_window;
extern "C" void (*g_psyx_before_swap)(void);
extern "C" volatile int g_psyx_quit_requested;

extern unsigned long long g_noDepthTris, g_depthTris, g_flatTris;
namespace nat {

using Clock = std::chrono::steady_clock;

inline double nowSeconds() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

struct Camera {
  std::array<double, 9> rot{};
  std::array<double, 3> eye{};
  double fx{}, fy{};
  double sz_scale{1.0};
  bool valid{};
};

inline std::int16_t lo16(std::uint32_t v) { return static_cast<std::int16_t>(v & 0xffffu); }
inline std::int16_t hi16(std::uint32_t v) { return static_cast<std::int16_t>(v >> 16); }

inline Camera decomposeGteCamera(const std::array<std::uint32_t, 32>& c) {
  Camera cam;
  const double r[9] = {double(lo16(c[0])), double(hi16(c[0])), double(lo16(c[1])),
                       double(hi16(c[1])), double(lo16(c[2])), double(hi16(c[2])),
                       double(lo16(c[3])), double(hi16(c[3])), double(lo16(c[4]))};
  const double t[3] = {double(std::int32_t(c[5])), double(std::int32_t(c[6])), double(std::int32_t(c[7]))};
  const double h = double(c[26] & 0xffffu);
  double s[3];
  double rows[3][3];
  for (int i = 0; i < 3; ++i) {
    const double n = std::sqrt(r[i * 3] * r[i * 3] + r[i * 3 + 1] * r[i * 3 + 1] + r[i * 3 + 2] * r[i * 3 + 2]);
    if (n < 1.0 || h < 1.0) return cam;
    s[i] = n / 4096.0;
    for (int j = 0; j < 3; ++j) rows[i][j] = r[i * 3 + j] / n;
  }

  auto dot = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  double* f = rows[2];
  double* d = rows[1];
  const double fd = dot(f, d);
  for (int j = 0; j < 3; ++j) d[j] -= fd * f[j];
  const double dn = std::sqrt(dot(d, d));
  for (int j = 0; j < 3; ++j) d[j] /= dn;

  double rr[3] = {d[1] * f[2] - d[2] * f[1], d[2] * f[0] - d[0] * f[2], d[0] * f[1] - d[1] * f[0]};
  if (dot(rr, rows[0]) < 0) for (double& x : rr) x = -x;
  for (int j = 0; j < 3; ++j) rows[0][j] = rr[j];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) cam.rot[i * 3 + j] = rows[i][j];

  const double k[3] = {-t[0] / s[0], -t[1] / s[1], -t[2] / s[2]};
  for (int j = 0; j < 3; ++j) cam.eye[j] = rows[0][j] * k[0] + rows[1][j] * k[1] + rows[2][j] * k[2];
  cam.fx = h * s[0] / s[2];
  cam.fy = h * s[1] / s[2];
  cam.sz_scale = s[2];
  cam.valid = true;
  return cam;
}

struct Quat { double w, x, y, z; };

inline Quat quatFromRows(const std::array<double, 9>& m) {

  Quat q{};
  const double tr = m[0] + m[4] + m[8];
  if (tr > 0) {
    const double s = std::sqrt(tr + 1.0) * 2;
    q = {0.25 * s, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s};
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
    q = {(m[7] - m[5]) / s, 0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
  } else if (m[4] > m[8]) {
    const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
    q = {(m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s};
  } else {
    const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
    q = {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s};
  }
  return q;
}

inline std::array<double, 9> rowsFromQuat(Quat q) {
  const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  q = {q.w / n, q.x / n, q.y / n, q.z / n};
  const double w = q.w, x = q.x, y = q.y, z = q.z;
  return {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
          2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
          2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)};
}

inline Camera interpolate(const Camera& a, const Camera& b, double t) {
  if (!a.valid || t >= 1.0) return b;
  if (t <= 0.0) return a;

  const double dx = b.eye[0] - a.eye[0], dy = b.eye[1] - a.eye[1], dz = b.eye[2] - a.eye[2];
  if (dx * dx + dy * dy + dz * dz > 1500.0 * 1500.0) return b;
  Camera c = b;
  for (int j = 0; j < 3; ++j) c.eye[j] = a.eye[j] + (b.eye[j] - a.eye[j]) * t;
  Quat qa = quatFromRows(a.rot), qb = quatFromRows(b.rot);
  double d = qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z;
  if (d < 0) { qb = {-qb.w, -qb.x, -qb.y, -qb.z}; d = -d; }
  if (d < 0.8) return b;
  Quat q;
  if (d > 0.9995) {
    q = {qa.w + (qb.w - qa.w) * t, qa.x + (qb.x - qa.x) * t, qa.y + (qb.y - qa.y) * t, qa.z + (qb.z - qa.z) * t};
  } else {
    const double th = std::acos(d), s = std::sin(th);
    const double wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    q = {qa.w * wa + qb.w * wb, qa.x * wa + qb.x * wb, qa.y * wa + qb.y * wb, qa.z * wa + qb.z * wb};
  }
  c.rot = rowsFromQuat(q);
  c.fx = a.fx + (b.fx - a.fx) * t;
  c.fy = a.fy + (b.fy - a.fy) * t;
  return c;
}

struct RoomSignature {
  std::vector<std::pair<std::uint32_t, std::array<std::byte, 32>>> chunks;
};

struct Model {
  std::string name;
  std::variant<std::monostate, sf::assets::HmdModel, sf::assets::GmdModel, sf::assets::EmdScene> geometry;
  RoomSignature signature;
  std::vector<std::uint32_t> part_vertex_offsets;
  int full_variant = -1;

  std::vector<std::uint16_t> own_tex;
  int own_alias = -1;
  int bank = -1;
};

struct Mission {
  std::string name;
  std::vector<sf::assets::EmdScene> rooms;
  std::vector<std::string> room_names;
  std::vector<RoomSignature> signatures;
  std::vector<std::vector<std::byte>> room_bytes;
  std::vector<std::vector<std::uint16_t>> sig_ties;
  std::vector<std::vector<std::uint32_t>> section_offsets;
  std::vector<std::uint16_t> vram;
  std::vector<std::uint16_t> vram1;
  std::vector<Model> models;
};

inline RoomSignature makeSignature(std::span<const std::byte> bytes, std::size_t start, std::size_t step) {
  RoomSignature sig;
  for (std::size_t off = start; off + 32 <= bytes.size() && sig.chunks.size() < 16; off += step) {
    std::array<std::byte, 32> chunk{};
    std::memcpy(chunk.data(), bytes.data() + off, 32);
    sig.chunks.push_back({std::uint32_t(off), chunk});
  }
  return sig;
}

inline std::uint32_t le32(std::span<const std::byte> b, std::size_t o) {
  return std::to_integer<std::uint32_t>(b[o]) | std::to_integer<std::uint32_t>(b[o + 1]) << 8 |
         std::to_integer<std::uint32_t>(b[o + 2]) << 16 | std::to_integer<std::uint32_t>(b[o + 3]) << 24;
}

inline std::vector<std::byte> readGameFile(sf::disc::Iso9660Image& image, const std::string& path) {
  if (const auto* vf = virtualDisc().find(path)) return vf->data;
  auto d = image.readFile(path);
  return std::vector<std::byte>(d.begin(), d.end());
}

inline std::unique_ptr<Mission> loadMission(sf::disc::Iso9660Image& image, const std::string& fogPath, FILE* log) {
  using namespace sf::assets;
  auto m = std::make_unique<Mission>();
  m->name = fogPath;
  auto fog = FogArchive::parse(image.readFile(fogPath));
  auto vec = [](std::span<const std::byte> s) { return std::vector<std::byte>(s.begin(), s.end()); };
  auto loadBank = [&](const char* hogName, std::vector<std::uint16_t>& vram) {
  vram.assign(1024 * 512, 0);
  std::span<const std::byte> hogBytes;
  try { hogBytes = fog.file(hogName); } catch (const std::exception&) { return; }
  if (hogBytes.size() < 32) return;
  auto vh = HogArchive::parse(vec(hogBytes));
  for (const auto& e : vh.entries()) {
    const auto d = vh.file(e.name);
    auto rd = [&](std::size_t o) { return std::uint16_t(std::to_integer<std::uint16_t>(d[o]) | (std::to_integer<std::uint16_t>(d[o + 1]) << 8)); };
    if (e.name.rfind("TP", 0) == 0 && d.size() == 32768) {
      const int p = std::stoi(e.name.substr(2, 2));
      const int x0 = (p & 15) * 64, y0 = (p >> 4) * 256;
      for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 64; ++x) vram[std::size_t((y0 + y) * 1024 + x0 + x)] = rd(std::size_t((y * 64 + x) * 2));
    } else if (e.name == "ZCLUT.BIN") {
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 256; ++x) {
          const std::size_t o = std::size_t((y * 256 + x) * 2);
          if (o + 1 >= d.size()) break;
          vram[std::size_t((480 + y) * 1024 + 768 + x)] = rd(o);
        }
    }
  }
  };
  loadBank("VRAM.HOG", m->vram);
  loadBank("VRAM1.HOG", m->vram1);
  auto wh = HogArchive::parse(vec(fog.file("WLDEMD.HOG")));
  for (const auto& e : wh.entries()) {
    const auto bytes = wh.file(e.name);
    m->rooms.push_back(EmdScene::parse(bytes));
    m->room_names.push_back(e.name);
    {
      std::vector<std::uint32_t> offs;
      for (std::size_t i = 0; i < 31; ++i) {
        const auto v = le32(bytes, 4 + 4 * i);
        if (v == 0xffffffffu) break;
        offs.push_back(v);
      }
      m->section_offsets.push_back(std::move(offs));
    }
    RoomSignature sig;
    for (std::size_t off = 0xa0; off + 32 <= bytes.size() && sig.chunks.size() < 16; off += 256) {
      std::array<std::byte, 32> chunk{};
      std::memcpy(chunk.data(), bytes.data() + off, 32);
      sig.chunks.push_back({std::uint32_t(off), chunk});
    }
    m->signatures.push_back(std::move(sig));
    m->room_bytes.emplace_back(reinterpret_cast<const std::byte*>(bytes.data()), reinterpret_cast<const std::byte*>(bytes.data()) + bytes.size());
  }
  m->sig_ties.assign(m->signatures.size(), {});
  for (std::size_t a = 0; a < m->signatures.size(); ++a)
    for (std::size_t b = 0; b < m->signatures.size(); ++b)
      if (a != b && m->signatures[a].chunks.size() == m->signatures[b].chunks.size() && !m->signatures[a].chunks.empty() &&
          std::equal(m->signatures[a].chunks.begin(), m->signatures[a].chunks.end(), m->signatures[b].chunks.begin(), [](const auto& x, const auto& y) { return x.first == y.first && x.second == y.second; }))
        m->sig_ties[a].push_back(std::uint16_t(b));

  auto addModel = [&](const std::string& name, std::span<const std::byte> bytes) {
    if (bytes.size() < 0x40) return;
    Model md;
    md.name = name;
    try {
      const auto ext = name.size() > 4 ? name.substr(name.size() - 4) : std::string{};
      if (ext == ".HMD") {
        md.geometry = HmdModel::parse(bytes);
        const auto triangleWords = le32(bytes, 8);
        const auto parts = le32(bytes, 4);
        std::size_t cursor = 0x24 + std::size_t(triangleWords) * 4;
        for (std::uint32_t i = 0; i < parts && cursor + 0x44 <= bytes.size(); ++i) {
          md.part_vertex_offsets.push_back(std::uint32_t(cursor + 0x44));
          cursor += le32(bytes, cursor);
        }
        md.signature = makeSignature(bytes, 0x24, 128);
      } else if (ext == ".GMD") {
        md.geometry = GmdModel::parse(bytes);
        md.signature = makeSignature(bytes, 0x10, 64);
      } else if (ext == ".EMD") {
        md.geometry = EmdScene::parse(bytes);
        md.signature = makeSignature(bytes, 0xa0, 128);
      } else {
        return;
      }
    } catch (const std::exception& ex) {
      if (log) std::fprintf(log, "nativo: modelo %s ignorado: %s\n", name.c_str(), ex.what());
      return;
    }

    if (md.signature.chunks.size() < 2 && bytes.size() >= 32) {
      std::array<std::byte, 32> chunk{};
      std::memcpy(chunk.data(), bytes.data(), 32);
      md.signature.chunks.push_back({0u, chunk});
    }
    m->models.push_back(std::move(md));
  };
  try {
    const auto dlf = fog.file("DLF.RFF");
    const auto a = le32(dlf, 0), b = le32(dlf, 4);
    if (a < b && b <= dlf.size()) {
      auto hog = HogArchive::parse(vec(dlf.subspan(a, b - a)));
      for (const auto& e : hog.entries()) addModel(e.name, hog.file(e.name));
    }
  } catch (const std::exception& ex) { if (log) std::fprintf(log, "nativo: DLF: %s\n", ex.what()); }
  try {
    auto npc = HogArchive::parse(vec(fog.file("NPC.HOG")));
    for (const auto& e : npc.entries()) {
      try {
        auto inner = HogArchive::parse(vec(npc.file(e.name)));
        for (const auto& ie : inner.entries()) addModel(ie.name, inner.file(ie.name));
      } catch (const std::exception&) {}
    }
  } catch (const std::exception& ex) { if (log) std::fprintf(log, "nativo: NPC: %s\n", ex.what()); }

  int mgCount = 0;
  try {

    static const char* kMg[] = {"MGGABE", "MGBAGLAD", "MGCHANCE", "MGELSA", "MGGABED", "MGGABEJ", "MGGABEL", "MGGABEN", "MGGABES", "MGLARRY", "MGLIAN", "MGLIANG", "MGTERES"};
    std::vector<std::pair<std::string, std::string>> charHogs;
    for (const char* mg : kMg) charHogs.push_back({mg, std::string("MINIGAME/") + (std::string(mg) == "MGGABE" ? "GABE" : mg) + ".HOG"});
    const bool arena = fogPath.find("FOG2") != std::string::npos || fogPath.find("ARENA") != std::string::npos;
    if (arena) {
      try {
        for (const auto& e : image.list("MPLAYER")) {
          if (e.is_directory || e.name.size() < 5) continue;
          auto n = e.name.substr(0, e.name.find(';'));
          if (n.size() > 4 && n.substr(n.size() - 4) == ".HOG") charHogs.push_back({"MP" + n.substr(0, n.size() - 4), "MPLAYER/" + n});
        }
      } catch (const std::exception&) {}
    }
    for (const auto& [k, f] : virtualDisc().files) {
      const bool mgv = k.rfind("MINIGAME/", 0) == 0, mpv = k.rfind("MPLAYER/", 0) == 0;
      if ((!mgv && !(mpv && arena)) || k.size() < 5 || k.substr(k.size() - 4) != ".HOG") continue;
      const auto base = k.substr(k.find('/') + 1, k.size() - k.find('/') - 5);
      const std::string nm = (mgv ? "" : "MP") + base;
      if (std::none_of(charHogs.begin(), charHogs.end(), [&](const auto& c) { return c.second == k; })) charHogs.push_back({nm, k});
    }
    for (const auto& [stem, hogPath] : charHogs) {
      const char* mg = stem.c_str();
      try {
        auto hog = HogArchive::parse(readGameFile(image, hogPath));
        std::vector<std::uint16_t> tex;
        for (const auto& e : hog.entries()) {
          if (e.name.size() < 4 || e.name.substr(e.name.size() - 4) != ".TIM") continue;
          const auto t = hog.file(e.name);
          if (t.size() < 20 || le32(t, 0) != 0x10) continue;
          const std::uint32_t flags = le32(t, 4);
          const unsigned bpp = flags & 3;
          std::size_t o = 8;
          std::vector<std::uint16_t> clut;
          auto h16 = [&](std::size_t at) { return std::uint16_t(std::to_integer<unsigned>(t[at]) | (std::to_integer<unsigned>(t[at + 1]) << 8)); };
          if (flags & 8) {
            const std::uint32_t cl = le32(t, o);
            const unsigned cw = h16(o + 8), ch = h16(o + 10);
            for (unsigned i = 0; i < cw * ch && o + 12 + i * 2 + 1 < t.size(); ++i) clut.push_back(h16(o + 12 + i * 2));
            o += cl;
          }
          if (o + 12 > t.size() || bpp > 1 || clut.empty()) continue;
          const unsigned iw = h16(o + 8), ih = h16(o + 10);
          const std::size_t px = o + 12;
          const unsigned W = bpp == 0 ? iw * 4 : iw * 2;
          if (W > 128 || ih > 256) continue;
          tex.assign(128 * 256, 0);
          for (unsigned y = 0; y < ih; ++y)
            for (unsigned x = 0; x < W; ++x) {
              const std::size_t bi = px + std::size_t(y) * iw * 2 + (bpp == 0 ? x / 2 : x);
              if (bi >= t.size()) continue;
              unsigned idx = std::to_integer<unsigned>(t[bi]);
              if (bpp == 0) idx = (idx >> (4 * (x & 1))) & 15;
              tex[std::size_t(y) * 128 + x] = idx < clut.size() ? clut[idx] : 0;
            }
        }
        for (const auto& e : hog.entries()) {
          if (e.name.size() < 4 || e.name.substr(e.name.size() - 4) != ".HMD") continue;
          const bool low = e.name.find("X.HMD") != std::string::npos;
          const auto before = m->models.size();
          addModel(stem + (low ? "X.HMD" : ".HMD"), hog.file(e.name));
          if (m->models.size() > before) m->models.back().own_tex = tex;
          ++mgCount;
        }
      } catch (const std::exception& ex) { if (log) std::fprintf(log, "nativo: minijuego %s: %s\n", mg, ex.what()); }
    }
  } catch (const std::exception&) {}
  if (log) std::fprintf(log, "nativo: %d modelos de minijuegos\n", mgCount);

  for (std::size_t i = 0; i < m->models.size(); ++i) {
    auto* hx = std::get_if<HmdModel>(&m->models[i].geometry);
    if (!hx || !hx->flatLit()) continue;
    const auto& n = m->models[i].name;
    for (std::size_t j = 0; j < m->models.size(); ++j) {
      auto* hf = std::get_if<HmdModel>(&m->models[j].geometry);
      if (!hf || hf->flatLit() || hf->parts().size() != hx->parts().size()) continue;
      const auto& f = m->models[j].name;
      const auto stem = f.substr(0, f.size() - 4);
      if (n.rfind(stem.substr(0, std::min<std::size_t>(stem.size(), 7)), 0) == 0) { m->models[i].full_variant = int(j); break; }
    }
  }
  if (log) std::fprintf(log, "nativo: mision %s cargada: %zu salas, %zu modelos\n", fogPath.c_str(), m->rooms.size(), m->models.size());
  return m;
}

inline std::map<std::string, std::string>& weaponMenuTims() { static std::map<std::string, std::string> m; return m; }
struct WeaponMenuPic { std::uint16_t x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0, cw = 0, ch = 0; std::vector<std::uint16_t> clut, pix; };
inline std::map<std::string, WeaponMenuPic>& weaponMenuPics() { static std::map<std::string, WeaponMenuPic> m; return m; }
inline int collectWeaponMenuTims(sf::disc::Iso9660Image& image, FILE* log, const std::string& extraDir = {}) {
  using namespace sf::assets;
  std::vector<std::string> want;
  for (const auto& w : weaponTableNames()) want.push_back(w + ".TIM");
  for (const char* z : {"C4", "TRANSPON", "SPYCAM", "MODEM", "BINOCULA"}) want.push_back(std::string(z) + ".TIM");
  std::vector<std::string> fogs;

  std::uint64_t key = 1469598103934665603ull;
  auto mix = [&](std::uint64_t v) { key = (key ^ v) * 1099511628211ull; };
  for (const char* dir : {"FOG", "FOG2"})
    try { for (const auto& e : image.list(dir)) { if (e.is_directory) continue; auto n = e.name.substr(0, e.name.find(';')); if (n.size() > 4 && n.substr(n.size() - 4) == ".FOG") { fogs.push_back(std::string(dir) + "/" + n); for (char c : fogs.back()) mix(std::uint8_t(c)); mix(e.extent_lba); mix(e.size); } } } catch (const std::exception&) {}
  for (const auto& w : want) for (char c : w) mix(std::uint8_t(c));
  std::map<std::string, std::vector<std::byte>> got; std::map<std::string, std::string> from; std::map<std::string, int> diff;
  const std::string cacheDir = extraDir.size() > 5 && extraDir.substr(extraDir.size() - 5) == "/menu" ? extraDir.substr(0, extraDir.size() - 5) : extraDir;

  const std::string cachePath = cacheDir.empty() ? std::string() : pakfs::looseDir(cacheDir) ? cacheDir + "/cache_fotos_menu.bin" : [] { std::error_code e; std::filesystem::create_directories(pakfs::st().base + "partidas", e); return pakfs::st().base + "partidas/cache_fotos_menu.bin"; }();
  bool fromCache = false;
  static const bool noCache = std::getenv("SF3N_SIN_CACHE_FOTOS") != nullptr;
  if (!cachePath.empty() && !noCache) {
    if (FILE* f = std::fopen(cachePath.c_str(), "rb")) {
      auto rdU = [&](std::uint64_t& v, int nb) { v = 0; for (int i = 0; i < nb; ++i) { const int c = std::fgetc(f); if (c == EOF) return false; v |= std::uint64_t(c) << (8 * i); } return true; };
      auto rdS = [&](std::string& t) { std::uint64_t n; if (!rdU(n, 4) || n > 4096) return false; t.resize(std::size_t(n)); return n == 0 || std::fread(t.data(), 1, t.size(), f) == t.size(); };
      char mg[8] = {};
      std::uint64_t k = 0, cnt = 0;
      bool ok = std::fread(mg, 1, 8, f) == 8 && std::memcmp(mg, "SF3FOTO1", 8) == 0 && rdU(k, 8) && k == key && rdU(cnt, 4) && cnt < 1000;
      for (std::uint64_t i = 0; ok && i < cnt; ++i) {
        std::string n, fr; std::uint64_t df = 0, len = 0;
        ok = rdS(n) && rdS(fr) && rdU(df, 4) && rdU(len, 4) && len < 1000000;
        if (!ok) break;
        std::vector<std::byte> d(static_cast<std::size_t>(len));
        ok = len == 0 || std::fread(d.data(), 1, d.size(), f) == d.size();
        if (!ok) break;
        got[n] = std::move(d); from[n] = fr; if (df) diff[n] = int(df);
      }
      std::fclose(f);
      if (ok) fromCache = true; else { got.clear(); from.clear(); diff.clear(); }
    }
  }
  if (fromCache) fogs.clear();
  for (const auto& fp : fogs) {
    try {
      auto fog = FogArchive::parse(image.readFile(fp));
      std::span<const std::byte> mh;
      try { mh = fog.file("MENU.HOG"); } catch (const std::exception&) { continue; }
      if (mh.size() < 32) continue;
      auto hog = HogArchive::parse(std::vector<std::byte>(mh.begin(), mh.end()));
      for (const auto& e : hog.entries()) {
        if (std::find(want.begin(), want.end(), e.name) == want.end()) continue;
        const auto d = hog.file(e.name);
        if (!got.count(e.name)) { got[e.name].assign(d.begin(), d.end()); from[e.name] = fp; }
        else if (got[e.name].size() != d.size() || !std::equal(d.begin(), d.end(), got[e.name].begin())) ++diff[e.name];
      }
    } catch (const std::exception&) {}
  }
  if (!fromCache && !cachePath.empty() && !noCache && !got.empty()) {
    if (FILE* f = std::fopen(cachePath.c_str(), "wb")) {
      auto wrU = [&](std::uint64_t v, int nb) { for (int i = 0; i < nb; ++i) std::fputc(int((v >> (8 * i)) & 255), f); };
      auto wrS = [&](const std::string& t) { wrU(t.size(), 4); std::fwrite(t.data(), 1, t.size(), f); };
      std::fwrite("SF3FOTO1", 1, 8, f); wrU(key, 8); wrU(got.size(), 4);
      for (const auto& [n, d] : got) { wrS(n); wrS(from[n]); wrU(std::uint64_t(diff.count(n) ? diff[n] : 0), 4); wrU(d.size(), 4); std::fwrite(d.data(), 1, d.size(), f); }
      std::fclose(f);
    }
  }
  if (log) std::fprintf(log, "armas: fotos del menu %s\n", fromCache ? "de la cache (sin leer los FOG)" : "leidas de los FOG del disco");

  if (!extraDir.empty()) {
    const std::string armasDir = extraDir.size() > 5 && extraDir.substr(extraDir.size() - 5) == "/menu" ? extraDir.substr(0, extraDir.size() - 5) : extraDir;
    loadWeaponConfig(armasDir, log);
    for (const auto& w : want) {
      const auto* wc = weaponConfigFor(w);
      if (!wc || wc->foto.empty()) continue;
      std::vector<std::byte> d;
      pakfs::read(armasDir + "/" + wc->foto, d, 200000);
      if (!d.empty()) { got[w] = std::move(d); from[w] = "armas/" + wc->foto; diff.erase(w); }
      else if (log) std::fprintf(log, "armas: no se pudo leer la foto %s de %s\n", wc->foto.c_str(), w.c_str());
    }
  }
  if (!extraDir.empty())
    for (const auto& w : want) {
      if (got.count(w)) continue;
      std::vector<std::byte> d;
      pakfs::read(extraDir + "/" + w, d, 200000);
      if (!d.empty()) { got[w] = std::move(d); from[w] = "armas/menu"; }
    }

  for (auto& [n, d] : got) {
    auto rd32 = [&](std::size_t o) { return o + 4 <= d.size() ? std::uint32_t(std::to_integer<std::uint32_t>(d[o]) | (std::to_integer<std::uint32_t>(d[o + 1]) << 8) | (std::to_integer<std::uint32_t>(d[o + 2]) << 16) | (std::to_integer<std::uint32_t>(d[o + 3]) << 24)) : 0u; };
    auto rd16 = [&](std::size_t o) { return o + 2 <= d.size() ? std::uint16_t(std::to_integer<std::uint16_t>(d[o]) | (std::to_integer<std::uint16_t>(d[o + 1]) << 8)) : std::uint16_t(0); };
    if (rd32(0) != 0x10 || !(rd32(4) & 8)) continue;
    const std::size_t c0 = 8, cl = rd32(c0), i0 = c0 + cl, il = rd32(i0);
    if (i0 + il > d.size() || il < 12 || cl < 12) continue;
    WeaponMenuPic pic;
    pic.cx = rd16(c0 + 4); pic.cy = rd16(c0 + 6); pic.cw = rd16(c0 + 8); pic.ch = rd16(c0 + 10);
    pic.w = rd16(i0 + 8); pic.h = rd16(i0 + 10);
    for (std::size_t k = c0 + 12; k + 1 < c0 + cl; k += 2) pic.clut.push_back(rd16(k));
    for (std::size_t k = i0 + 12; k + 1 < i0 + il; k += 2) pic.pix.push_back(rd16(k));
    d[i0 + 4] = std::byte{0x80}; d[i0 + 5] = std::byte{0x03}; d[i0 + 6] = std::byte{0}; d[i0 + 7] = std::byte{0};
    pic.x = 896; pic.y = 0;
    weaponMenuPics()[n] = std::move(pic);
    virtualDisc().add("ARMAS/" + n, std::move(d)); weaponMenuTims()[n] = "ARMAS/" + n;
  }

  if (!weaponMenuTims().count("SUPERG.TIM") && weaponMenuTims().count("SUPERGS.TIM")) weaponMenuTims()["SUPERG.TIM"] = weaponMenuTims()["SUPERGS.TIM"];
  if (!weaponMenuPics().count("SUPERG.TIM") && weaponMenuPics().count("SUPERGS.TIM")) weaponMenuPics()["SUPERG.TIM"] = weaponMenuPics()["SUPERGS.TIM"];
  if (log) {
    std::fprintf(log, "armas: %zu fotos del menu de armamento;", weaponMenuTims().size());
    for (const auto& w : want) if (!weaponMenuTims().count(w)) std::fprintf(log, " falta %s", w.c_str());
    for (const auto& [n, c] : diff) std::fprintf(log, " %s distinta en %d misiones", n.c_str(), c);
    std::fprintf(log, "\n");
  }
  return int(got.size());
}

struct WeaponSound {
  int program = -1;
  std::array<std::uint8_t, 36> prog{};
  std::vector<std::array<std::uint8_t, 24>> tones;
  std::string source;
  bool wav = false;
};
struct WeaponSoundBank { std::map<int, WeaponSound> by_weapon; std::vector<std::byte> ext; std::vector<std::pair<std::uint32_t, std::uint32_t>> redirect; bool ok = false; };
inline WeaponSoundBank buildWeaponSounds(sf::disc::Iso9660Image& image, FILE* log, const std::string& armasDir = {}) {
  using namespace sf::assets;
  WeaponSoundBank out;

  static const std::vector<std::tuple<int, int, const char*>> ref = {
      {1, 52, "AFGHAN2"}, {2, 3, "AFGHAN2"}, {3, 4, "AFGHAN1"}, {4, 56, "JUNGLE2"}, {5, 0, "SENATE"}, {6, 67, "LONDON1"},
      {7, 2, "JUNGLE"}, {8, 2, "AFGHAN1"}, {9, 0, "AFGHAN1"}, {10, 0, "AFGHAN2"}, {11, 57, "SENATE"}, {12, 1, "AFGHAN2"},
      {13, 2, "SENATE"}, {14, 57, "AFRICA1"}, {15, 57, "AFGHAN2"}, {16, 6, "AFGHAN2"}, {17, 60, "AFGHAN2"}, {18, 54, "LONDON2"},
      {19, 55, "MCAVES"}, {20, 13, "AFGHAN1"}, {21, 14, "AFRICA1"}, {22, 13, "AFGHAN1"}, {23, 17, "JUNGLE"}, {27, 0, "JUNGLE2"},
      {28, 8, "AFRICA1"}, {29, 17, "JUNGLE"}, {31, 15, "AFGHAN1"}, {32, 15, "AFRICA1"}};
  auto le32 = [](std::span<const std::byte> b, std::size_t o) -> std::uint32_t { return o + 4 <= b.size() ? std::uint32_t(std::to_integer<std::uint32_t>(b[o]) | (std::to_integer<std::uint32_t>(b[o + 1]) << 8) | (std::to_integer<std::uint32_t>(b[o + 2]) << 16) | (std::to_integer<std::uint32_t>(b[o + 3]) << 24)) : 0u; };
  struct Lvl { std::vector<std::byte> vb, vh; };
  std::map<std::string, Lvl> lv;
  std::map<std::vector<std::byte>, std::uint32_t> sampleAt;
  std::uint32_t nextSent = 0x10;
  for (const auto& [w, prog, name] : ref) {
    try {
      if (!lv.count(name)) {
        auto fog = FogArchive::parse(image.readFile(std::string("FOG/") + name + ".FOG"));
        const auto slf = fog.file("SLF.RFF");
        std::vector<std::byte> sv(slf.begin(), slf.end());
        auto vbh = HogArchive::parse(sv);
        Lvl L; const auto pv = vbh.file("PL.VB"); L.vb.assign(pv.begin(), pv.end());
        const char key[] = "AM.VH";
        auto it = std::search(sv.begin(), sv.end(), reinterpret_cast<const std::byte*>(key), reinterpret_cast<const std::byte*>(key) + 5);
        if (it == sv.end()) throw std::runtime_error("sin AM.VH");
        const std::size_t k = std::size_t(it - sv.begin());
        std::size_t st = 0;
        for (std::size_t s0 = (k >= 200 ? (k - 200) & ~std::size_t(3) : 0); s0 < k; s0 += 4)
          if (le32(sv, s0 + 8) == 20 && s0 + le32(sv, s0 + 12) == k && le32(sv, s0 + 4) > 0 && le32(sv, s0 + 4) < 20) { st = s0; break; }
        if (!st) throw std::runtime_error("sin HOG de VH");
        auto vhh = HogArchive::parse(std::vector<std::byte>(sv.begin() + std::ptrdiff_t(st), sv.end()));
        const auto ph = vhh.file("PL.VH"); L.vh.assign(ph.begin(), ph.end());
        lv[name] = std::move(L);
      }
      const auto& L = lv[name];
      const std::span<const std::byte> vh(L.vh);
      const std::size_t np = le32(vh, 0x18) & 0xffff, ng = (le32(vh, 0x18) >> 16) & 0xffff;
      if (prog >= int(np) || vh.size() < 0x40 + 36 * np + 8 * ng) continue;
      WeaponSound ws; ws.program = prog; ws.source = name;
      for (int i = 0; i < 36; ++i) ws.prog[std::size_t(i)] = std::to_integer<std::uint8_t>(vh[0x40 + 36 * std::size_t(prog) + std::size_t(i)]);
      const int g = ws.prog[13];
      if (le32(vh, 0x40 + 36 * std::size_t(prog)) == 0 || g >= int(ng)) continue;
      const std::size_t goff = 0x40 + 36 * np + 8 * std::size_t(g);
      const int cnt = std::to_integer<int>(vh[goff]);
      const std::size_t toff = le32(vh, goff + 4);
      for (int t = 0; t < cnt; ++t) {
        std::array<std::uint8_t, 24> tr{};
        for (int i = 0; i < 24; ++i) tr[std::size_t(i)] = std::to_integer<std::uint8_t>(vh[toff + 24 * std::size_t(t) + std::size_t(i)]);
        const std::uint32_t so = tr[20] | (tr[21] << 8) | (std::uint32_t(tr[22]) << 16) | (std::uint32_t(tr[23]) << 24);
        std::vector<std::byte> smp;
        for (std::size_t o = so; o + 16 <= L.vb.size(); o += 16) { smp.insert(smp.end(), L.vb.begin() + std::ptrdiff_t(o), L.vb.begin() + std::ptrdiff_t(o + 16)); if (std::to_integer<int>(L.vb[o + 1]) & 1) break; }
        if (smp.empty()) continue;
        std::uint32_t sent;
        if (auto f = sampleAt.find(smp); f != sampleAt.end()) sent = f->second;
        else {
          if (nextSent >= 0x1000) throw std::runtime_error("sin centinelas");
          sent = nextSent; nextSent += 0x10;
          out.redirect.push_back({sent, std::uint32_t(0x80000u + out.ext.size())});
          out.ext.insert(out.ext.end(), smp.begin(), smp.end());
          sampleAt[smp] = sent;
        }
        tr[20] = std::uint8_t(sent); tr[21] = std::uint8_t(sent >> 8); tr[22] = 0; tr[23] = 0;
        ws.tones.push_back(tr);
      }
      if (!ws.tones.empty()) out.by_weapon[w] = std::move(ws);
    } catch (const std::exception& ex) { if (log) std::fprintf(log, "armas: sonido del arma %d (%s): %s\n", w, name, ex.what()); }
  }

  auto mainTone = [](const WeaponSound& ws) -> const std::array<std::uint8_t, 24>* {
    const int note = ws.prog[14];
    for (const auto& t : ws.tones) if (note >= t[6] && note <= t[7]) return &t;
    return ws.tones.empty() ? nullptr : &ws.tones.front();
  };
  auto sentinelBytes = [&](std::uint32_t sent) -> std::vector<std::byte> {
    for (const auto& [s0, e] : out.redirect) if (s0 == sent) {
      const std::size_t a = e - 0x80000u; std::size_t b = a;
      while (b + 16 <= out.ext.size()) { b += 16; if (std::to_integer<int>(out.ext[b - 15]) & 1) break; }
      return std::vector<std::byte>(out.ext.begin() + std::ptrdiff_t(a), out.ext.begin() + std::ptrdiff_t(b));
    }
    return {};
  };
  if (!armasDir.empty() && pakfs::looseDir(armasDir)) {

    std::error_code ec;
    const std::string od = armasDir + "/sonidos/originales";
    std::filesystem::create_directories(od, ec);
    for (const auto& [w, ws] : out.by_weapon) {
      const auto* t = mainTone(ws); if (!t) continue;
      char nm[96]; std::snprintf(nm, sizeof nm, "%s/%02d_%s.wav", od.c_str(), w, weaponIdName(w).c_str());
      if (std::filesystem::exists(nm, ec)) continue;
      const auto smp = sentinelBytes(std::uint32_t((*t)[20] | ((*t)[21] << 8)));
      if (smp.empty()) continue;
      const auto pcm = sf3wav::decodeAdpcm(reinterpret_cast<const std::uint8_t*>(smp.data()), smp.size());
      const double semis = double(ws.prog[14]) - (double((*t)[2]) + double((*t)[3]) / 128.0);
      const int rate = int(std::lround(44100.0 * std::pow(2.0, semis / 12.0)));
      sf3wav::writeWav(nm, pcm, std::clamp(rate, 1000, 192000));
    }

    for (int w = 1; w <= 39; ++w) {
      const std::string nmw = weaponIdName(w);
      const auto* wc = nmw.empty() ? nullptr : weaponConfigFor(nmw);
      if (!wc || wc->sonido.empty()) continue;
      int rin = 0; std::string err;
      auto pcm = sf3wav::readWav44k(armasDir + "/" + wc->sonido, rin, &err);
      if (pcm.empty()) { if (log) std::fprintf(log, "armas: sonido %s de %s: %s\n", wc->sonido.c_str(), nmw.c_str(), err.c_str()); continue; }
      if (wc->volumen_sonido != 100) for (auto& v : pcm) v = std::int16_t(std::clamp(int(v) * wc->volumen_sonido / 100, -32768, 32767));
      const WeaponSound* base = out.by_weapon.count(w) ? &out.by_weapon[w] : out.by_weapon.count(8) ? &out.by_weapon[8] : out.by_weapon.empty() ? nullptr : &out.by_weapon.begin()->second;
      const auto* tpl = base ? mainTone(*base) : nullptr;
      if (!tpl || nextSent >= 0x1000) { if (log) std::fprintf(log, "armas: sonido %s de %s: sin tono de base\n", wc->sonido.c_str(), nmw.c_str()); continue; }
      const auto adp = sf3wav::encodeAdpcm(pcm);
      const std::uint32_t sent = nextSent; nextSent += 0x10;
      out.redirect.push_back({sent, std::uint32_t(0x80000u + out.ext.size())});
      for (auto c : adp) out.ext.push_back(std::byte(c));
      WeaponSound ws;
      ws.program = out.by_weapon.count(w) ? out.by_weapon[w].program : -1;
      ws.prog = base->prog;
      auto tr = *tpl;
      if (!std::getenv("SF3N_PRUEBA_CENTRO")) { tr[2] = ws.prog[14]; tr[3] = 0; }
      tr[20] = std::uint8_t(sent); tr[21] = std::uint8_t(sent >> 8); tr[22] = 0; tr[23] = 0;
      ws.tones = {tr};
      ws.wav = true; ws.source = "armas/" + wc->sonido;
      out.by_weapon[w] = std::move(ws);
      if (log) std::fprintf(log, "armas: sonido de %s (%d) = %s (%d Hz, %.2f s, %zu bytes ADPCM, volumen %d %%)\n", nmw.c_str(), w, wc->sonido.c_str(), rin, double(pcm.size()) / 44100.0, adp.size(), wc->volumen_sonido);
    }
  }
  out.ok = !out.by_weapon.empty();
  if (log) std::fprintf(log, "armas: sonidos de disparo de %zu armas, %zu muestras, %zu bytes de memoria de sonido propia\n", out.by_weapon.size(), out.redirect.size(), out.ext.size());
  return out;
}

struct ScopePic {
  std::mutex m;
  std::vector<std::uint16_t> pix;
  int x = 0, y = 0, w = 0, h = 0;
  std::uint64_t seq = 0;
  std::atomic<std::uint64_t> flip{0};
  std::chrono::steady_clock::time_point when{};
  std::vector<std::vector<std::uint32_t>> pk;
  std::vector<std::uint8_t> actor;
};
inline ScopePic& scopePic() { static ScopePic p; return p; }

inline WeaponBank buildWeaponBank(sf::disc::Iso9660Image& image, FILE* log, const std::string& armasDir = {}) {
  using namespace sf::assets;
  WeaponBank bank;
  auto vec = [](std::span<const std::byte> s) { return std::vector<std::byte>(s.begin(), s.end()); };

  std::vector<std::string> fogs;
  for (int i = 11; i <= 20; ++i) { char b[32]; std::snprintf(b, sizeof b, "FOG2/ARENA%02d.FOG", i); fogs.push_back(b); }
  try { for (const auto& e : image.list("FOG")) { if (e.is_directory) continue; auto n = e.name.substr(0, e.name.find(';')); if (n.size() > 4 && n.substr(n.size() - 4) == ".FOG") fogs.push_back("FOG/" + n); } } catch (const std::exception&) {}
  for (int i = 1; i <= 10; ++i) { char b[32]; std::snprintf(b, sizeof b, "FOG2/ARENA%02d.FOG", i); fogs.push_back(b); }

  auto foreign = [](const std::string& src) { return src.rfind("ARMAS:", 0) == 0; };
  constexpr std::uint16_t kOwnTp = 0x100;
  std::vector<std::string> want;
  for (const auto& w : weaponTableNames()) { want.push_back(w + ".GMD"); want.push_back("X" + w.substr(1) + ".GMD"); }

  std::map<std::string, std::array<std::vector<std::uint16_t>, 2>> vrams;
  auto pageOf = [](std::uint16_t t) { std::uint16_t tp = t; if ((tp & 0xf) < 6) tp = std::uint16_t(tp + 6); return tp; };
  struct Key { std::string fog; std::uint16_t tp, clut; bool operator<(const Key& o) const { return fog != o.fog ? fog < o.fog : tp != o.tp ? tp < o.tp : clut < o.clut; } };
  struct Rect { int u0 = 255, v0 = 255, u1 = 0, v1 = 0; int bank = 0; int ax = 0, ay = 0, grp = 0; };
  std::map<Key, Rect> rects;
  std::vector<GmdModel> parsed;
  std::vector<std::string> texsrc;
  for (std::size_t fi = 0; fi < fogs.size(); ++fi) {
    const std::string fogPath = fogs[fi];
    bool need = false;
    for (const auto& w : want) if (!bank.by_name.count(w)) { need = true; break; }
    if (!need) break;
    try {
      std::vector<std::byte> fogBytes;
      fogBytes = image.readFile(fogPath);
      auto fog = FogArchive::parse(std::move(fogBytes));
      const auto dlf = fog.file("DLF.RFF");
      const auto a = le32(dlf, 0), b = le32(dlf, 4);
      if (!(a < b && b <= dlf.size())) continue;
      auto hog = HogArchive::parse(vec(dlf.subspan(a, b - a)));
      for (const auto& w : want) {
        if (bank.by_name.count(w)) continue;
        std::span<const std::byte> gb; bool found = false;
        for (const auto& e : hog.entries()) if (e.name == w) { gb = hog.file(e.name); found = true; break; }
        if (!found || gb.empty()) continue;
        if (!vrams.count(fogPath)) {
          auto& vv = vrams[fogPath];
          const char* names[2] = {"VRAM.HOG", "VRAM1.HOG"};
          for (int k = 0; k < 2; ++k) {
            std::span<const std::byte> hb;
            try { hb = fog.file(names[k]); } catch (const std::exception&) { continue; }
            if (hb.size() < 32) continue;
            vv[std::size_t(k)].assign(1024 * 512, 0);
            auto vh = HogArchive::parse(vec(hb));
            for (const auto& e : vh.entries()) {
              const auto d = vh.file(e.name);
              auto rd = [&](std::size_t o) { return std::uint16_t(std::to_integer<std::uint16_t>(d[o]) | (std::to_integer<std::uint16_t>(d[o + 1]) << 8)); };
              if (e.name.rfind("TP", 0) == 0 && d.size() == 32768) {
                const int p = std::stoi(e.name.substr(2, 2));
                const int x0 = (p & 15) * 64, y0 = (p >> 4) * 256;
                for (int y = 0; y < 256; ++y) for (int x = 0; x < 64; ++x) vv[std::size_t(k)][std::size_t((y0 + y) * 1024 + x0 + x)] = rd(std::size_t((y * 64 + x) * 2));
              } else if (e.name == "ZCLUT.BIN") {
                for (int y = 0; y < 32; ++y) for (int x = 0; x < 256; ++x) { const std::size_t o = std::size_t((y * 256 + x) * 2); if (o + 1 >= d.size()) break; vv[std::size_t(k)][std::size_t((480 + y) * 1024 + 768 + x)] = rd(o); }
              }
            }
          }
        }
        WeaponBank::Model md;
        md.gmd = w; md.source = fogPath; md.bytes = vec(gb);
        parsed.push_back(GmdModel::parse(md.bytes));
        texsrc.push_back(fogPath);
        for (const auto& t : parsed.back().triangles()) {
          if (WeaponBank::globalPage(t.texture_page)) continue;
          auto& r = rects[{fogPath, std::uint16_t(pageOf(t.texture_page) & 0x19f), t.clut}];
          for (const auto& uv : t.uv) { r.u0 = std::min<int>(r.u0, uv.u); r.u1 = std::max<int>(r.u1, uv.u); r.v0 = std::min<int>(r.v0, uv.v); r.v1 = std::max<int>(r.v1, uv.v); }
        }
        bank.by_name[w] = bank.models.size();
        bank.models.push_back(std::move(md));
      }
    } catch (const std::exception& ex) { if (log) std::fprintf(log, "armas: %s: %s\n", fogPath.c_str(), ex.what()); }
  }

  if (!armasDir.empty()) {
    for (const auto& w : want) {
      if (bank.by_name.count(w) || w[0] == 'X') continue;
      const std::string base = w.substr(0, w.size() - 4);
      auto readAll = [](const std::string& path) { std::vector<std::byte> b; pakfs::read(path, b); return b; };
      auto gb = readAll(armasDir + "/" + base + ".GMD");
      if (gb.empty()) continue;
      auto tb = readAll(armasDir + "/" + base + ".TIM");
      try {

        if (tb.size() < 20 || le32(tb, 0) != 0x10 || (le32(tb, 4) & 7u) != 2u) throw std::runtime_error("falta " + base + ".TIM de 16 bits");
        const unsigned tw = unsigned(std::to_integer<unsigned>(tb[16]) | (std::to_integer<unsigned>(tb[17]) << 8)), th = unsigned(std::to_integer<unsigned>(tb[18]) | (std::to_integer<unsigned>(tb[19]) << 8));
        if (!tw || !th || tw > 256 || th > 256 || tb.size() < 20 + std::size_t(tw) * th * 2) throw std::runtime_error(base + ".TIM con tamano no valido");
        const std::string src = "ARMAS:" + base;
        auto& vv = vrams[src];
        vv[0].assign(1024 * 512, 0);
        for (unsigned y = 0; y < th; ++y) for (unsigned x = 0; x < tw; ++x) { const std::size_t o = 20 + (std::size_t(y) * tw + x) * 2; vv[0][y * 1024 + x] = std::uint16_t(std::to_integer<unsigned>(tb[o]) | (std::to_integer<unsigned>(tb[o + 1]) << 8)); }
        WeaponBank::Model md; md.gmd = w; md.source = src; md.bytes = gb;
        auto gm = GmdModel::parse(md.bytes);
        auto& r = rects[{src, kOwnTp, 0}];
        for (const auto& t : gm.triangles()) for (const auto& uv : t.uv) {
          if (uv.u >= tw || uv.v >= th) throw std::runtime_error(base + ".GMD con uv fuera del TIM");
          r.u0 = std::min<int>(r.u0, uv.u); r.u1 = std::max<int>(r.u1, uv.u); r.v0 = std::min<int>(r.v0, uv.v); r.v1 = std::max<int>(r.v1, uv.v);
        }
        parsed.push_back(std::move(gm));
        texsrc.push_back(src);
        bank.by_name[w] = bank.models.size();
        bank.models.push_back(std::move(md));
        if (log) std::fprintf(log, "armas: %s desde la carpeta armas (textura %ux%u)\n", w.c_str(), tw, th);
      } catch (const std::exception& ex) { if (log) std::fprintf(log, "armas: carpeta armas: %s\n", ex.what()); }
    }
  }

  auto makeUnsilenced = [&](const std::vector<std::byte>& in) -> std::vector<std::byte> {
    auto r16 = [&](std::size_t o) { return std::uint16_t(std::to_integer<std::uint16_t>(in[o]) | (std::to_integer<std::uint16_t>(in[o + 1]) << 8)); };
    auto r32 = [&](const std::vector<std::byte>& b, std::size_t o) { return std::uint32_t(std::to_integer<std::uint32_t>(b[o]) | (std::to_integer<std::uint32_t>(b[o + 1]) << 8) | (std::to_integer<std::uint32_t>(b[o + 2]) << 16) | (std::to_integer<std::uint32_t>(b[o + 3]) << 24)); };
    if (in.size() < 0x18) return {};
    const std::size_t tc = r16(4), vo = r16(6), no = r16(8);
    if (vo != 0x18 + tc * 16 || no < vo || no > in.size()) return {};
    const std::size_t vc = (no - vo) / 4;
    auto sx = [](std::uint32_t v, int sh, int bits) { int x = int((v >> sh) & ((1u << bits) - 1)); if (x & (1 << (bits - 1))) x -= 1 << bits; return x; };
    struct V { int x, y, z; };
    std::vector<V> vs(vc);
    for (std::size_t i = 0; i < vc; ++i) { const auto p = r32(in, vo + i * 4); vs[i] = {sx(p, 0, 10), sx(p, 10, 10), sx(p, 20, 12)}; }
    std::vector<std::size_t> keep; int removed = 0, caps = 0; std::set<std::size_t> capV, flashV;
    for (std::size_t t = 0; t < tc; ++t) {
      const auto w2 = r32(in, 0x18 + t * 16 + 8);
      const std::size_t a = w2 & 0xff, b = (w2 >> 8) & 0xff, c = (w2 >> 16) & 0xff;
      const int fl = int((w2 >> 24) & 0x7f);
      if (a >= vc || b >= vc || c >= vc) return {};
      if (fl == 0) { flashV.insert(a); flashV.insert(b); flashV.insert(c); if (!std::getenv("SF3N_PRUEBA_SIN_FOGONAZO")) keep.push_back(t); continue; }
      const bool all92 = vs[a].y >= 92 && vs[b].y >= 92 && vs[c].y >= 92;
      const bool cap = vs[a].y == 92 && vs[b].y == 92 && vs[c].y == 92;
      if (cap) { ++caps; capV.insert(a); capV.insert(b); capV.insert(c); keep.push_back(t); }
      else if (all92) ++removed;
      else keep.push_back(t);
    }
    if (removed != 10 || caps != 2 || capV.size() != 4) return {};
    for (auto i : capV) { vs[i].x = vs[i].x >= 1 ? 1 : -1; vs[i].z = vs[i].z >= -11 ? -10 : -12; }
    for (auto i : flashV) vs[i].y -= 20;
    std::vector<std::byte> out(in.begin(), in.begin() + 0x18);
    for (auto t : keep) out.insert(out.end(), in.begin() + std::ptrdiff_t(0x18 + t * 16), in.begin() + std::ptrdiff_t(0x18 + t * 16 + 16));
    const std::size_t nvo = out.size();
    for (const auto& v : vs) { const std::uint32_t p = (std::uint32_t(v.x) & 0x3ffu) | ((std::uint32_t(v.y) & 0x3ffu) << 10) | ((std::uint32_t(v.z) & 0xfffu) << 20); for (int k = 0; k < 4; ++k) out.push_back(std::byte((p >> (8 * k)) & 0xff)); }
    const std::size_t nno = out.size();
    out.insert(out.end(), in.begin() + std::ptrdiff_t(no), in.end());
    auto w16 = [&](std::size_t o, std::size_t v) { out[o] = std::byte(v & 0xff); out[o + 1] = std::byte((v >> 8) & 0xff); };
    w16(4, keep.size()); w16(6, nvo); w16(8, nno);
    try { (void)GmdModel::parse(out); } catch (const std::exception&) { return {}; }
    return out;
  };
  if (!bank.by_name.count("SUPERG.GMD") && bank.by_name.count("SUPERGS.GMD")) {
    const auto& src = bank.models[bank.by_name["SUPERGS.GMD"]];
    WeaponBank::Model md; md.gmd = "SUPERG.GMD"; md.source = src.source + " (SUPERGS sin silenciador)";
    md.bytes = std::getenv("SF3N_SUPERG_TAL_CUAL") ? std::vector<std::byte>{} : makeUnsilenced(src.bytes);
    if (md.bytes.empty()) { md.bytes = src.bytes; md.source = src.source + " (SUPERGS)"; }
    parsed.push_back(GmdModel::parse(md.bytes));
    texsrc.push_back(texsrc[bank.by_name["SUPERGS.GMD"]]);
    bank.by_name["SUPERG.GMD"] = bank.models.size();
    bank.models.push_back(std::move(md));
  }
  if (std::getenv("SF3N_VOLCAR_ARMAS")) for (const auto& m : bank.models) if (FILE* f = std::fopen(("volcado_" + m.gmd).c_str(), "wb")) { std::fwrite(m.bytes.data(), 1, m.bytes.size(), f); std::fclose(f); }
  if (!bank.by_name.count("XUPERG.GMD") && bank.by_name.count("XUPERGS.GMD") && !std::getenv("SF3N_PRUEBA_SIN_XUPERG")) {
    const auto& src = bank.models[bank.by_name["XUPERGS.GMD"]];
    WeaponBank::Model md; md.gmd = "XUPERG.GMD"; md.source = src.source + " (XUPERGS)"; md.bytes = src.bytes;
    parsed.push_back(GmdModel::parse(md.bytes));
    texsrc.push_back(texsrc[bank.by_name["XUPERGS.GMD"]]);
    bank.by_name["XUPERG.GMD"] = bank.models.size();
    bank.models.push_back(std::move(md));
  }

  for (const auto& w : weaponTableNames()) {
    const std::string hand = w + ".GMD", floor = "X" + w.substr(1) + ".GMD";
    if (bank.by_name.count(floor) || !bank.by_name.count(hand)) continue;
    const std::size_t hi = bank.by_name[hand];
    WeaponBank::Model md; md.gmd = floor; md.source = bank.models[hi].source + " (" + w + ")"; md.bytes = bank.models[hi].bytes;
    parsed.push_back(GmdModel::parse(md.bytes));
    texsrc.push_back(texsrc[hi]);
    bank.by_name[floor] = bank.models.size();
    bank.models.push_back(std::move(md));
  }

  auto texel = [&](const std::vector<std::uint16_t>& vr, std::uint16_t tp, std::uint16_t clut, int u, int v) -> std::uint16_t {
    if (vr.empty()) return 0;
    const int depth = (tp >> 7) & 3, px = (tp & 15) * 64, py = ((tp >> 4) & 1) * 256;
    const int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 0x1ff;
    const int y = (py + v) & 511;
    if (depth == 0) { const auto wv = vr[std::size_t(y * 1024 + ((px + u / 4) & 1023))]; return vr[std::size_t(cy * 1024 + ((cx + ((wv >> ((u & 3) * 4)) & 15)) & 1023))]; }
    if (depth == 1) { const auto wv = vr[std::size_t(y * 1024 + ((px + u / 2) & 1023))]; return vr[std::size_t(cy * 1024 + ((cx + ((u & 1) ? (wv >> 8) : (wv & 0xff))) & 1023))]; }
    return vr[std::size_t(y * 1024 + ((px + u) & 1023))];
  };

  std::vector<std::pair<const Key*, Rect*>> order;
  for (auto& [k, r] : rects) order.push_back({&k, &r});
  std::sort(order.begin(), order.end(), [](const auto& x, const auto& y) { return (x.second->v1 - x.second->v0) > (y.second->v1 - y.second->v0); });
  int gx = 0, gy = 0, shelf = 0;
  auto newGroup = [&] { bank.groups.emplace_back(256 * 256, 0); gx = 0; gy = 0; shelf = 0; };
  for (auto& [kp, rp] : order) {
    const auto& k = *kp; auto& r = *rp;
    const auto& vv = vrams[k.fog];
    for (int bk = 0; bk < 2; ++bk) {
      bool any = false;
      for (int v = r.v0; v <= r.v1 && !any; ++v) for (int u = r.u0; u <= r.u1 && !any; ++u) any = (texel(vv[std::size_t(bk)], k.tp, k.clut, u, v) & 0x7fff) != 0;
      if (any) { r.bank = bk; break; }
    }
    const int w = r.u1 - r.u0 + 1, h = r.v1 - r.v0 + 1;
    const int bw = std::min(256, w + 2), bh = std::min(256, h + 2);
    if (bank.groups.empty()) newGroup();
    if (gx + bw > 256) { gx = 0; gy += shelf; shelf = 0; }
    if (gy + bh > 256) newGroup();
    r.grp = int(bank.groups.size()) - 1; r.ax = gx + (bw > w ? 1 : 0); r.ay = gy + (bh > h ? 1 : 0);
    auto& G = bank.groups.back();
    for (int y = -1; y <= h; ++y) for (int x = -1; x <= w; ++x) {
      const int dx = r.ax + x, dy = r.ay + y;
      if (dx < 0 || dy < 0 || dx > 255 || dy > 255) continue;
      const int su = std::clamp(r.u0 + x, r.u0, r.u1), sv = std::clamp(r.v0 + y, r.v0, r.v1);
      G[std::size_t(dy * 256 + dx)] = texel(vv[std::size_t(r.bank)], k.tp, k.clut, su, sv);
    }
    gx += bw; shelf = std::max(shelf, bh);
  }

  std::uint32_t off = 0;
  for (std::size_t i = 0; i < bank.models.size(); ++i) {
    auto& md = bank.models[i];
    for (const auto& t : parsed[i].triangles()) {
      WeaponBank::Model::Tri tr;
      const auto it = foreign(texsrc[i]) ? rects.find({texsrc[i], kOwnTp, 0}) : WeaponBank::globalPage(t.texture_page) ? rects.end() : rects.find({texsrc[i], std::uint16_t(pageOf(t.texture_page) & 0x19f), t.clut});
      if (it != rects.end()) {
        const auto& r = it->second;
        tr.tpage = WeaponBank::aliasTpage(WeaponBank::kFirstAlias + 4 * r.grp, std::uint16_t((t.texture_page & 0x60) | (2u << 7)));
        for (int c = 0; c < 3; ++c) { tr.uv[std::size_t(c * 2)] = std::uint8_t(std::clamp(t.uv[std::size_t(c)].u - r.u0 + r.ax, 0, 255)); tr.uv[std::size_t(c * 2 + 1)] = std::uint8_t(std::clamp(t.uv[std::size_t(c)].v - r.v0 + r.ay, 0, 255)); }
        tr.ok = true;
      }
      md.tris.push_back(tr);
    }
    if (off + md.bytes.size() > 0x100000u) { if (log) std::fprintf(log, "armas: sin sitio para %s\n", md.gmd.c_str()); continue; }
    md.addr = WeaponBank::kBase + off;
    off = (off + std::uint32_t(md.bytes.size()) + 3u) & ~3u;
    bank.by_addr[md.addr] = i;
  }
  if (log) {
    std::fprintf(log, "armas: banco con %zu modelos, %zu texturas, %zu grupos de textura (max %d), %u bytes de RAM; faltan:", bank.models.size(), rects.size(), bank.groups.size(), (126 - WeaponBank::kFirstAlias) / 4, off);
    for (const auto& w : want) if (!bank.by_name.count(w)) std::fprintf(log, " %s", w.c_str());
    std::fprintf(log, "\n");
    for (const auto& md : bank.models) std::fprintf(log, "armas: %s de %s (%zu bytes, %zu triangulos) en %08x\n", md.gmd.c_str(), md.source.c_str(), md.bytes.size(), md.tris.size(), md.addr);
  }

  bank.ok = !bank.models.empty() && WeaponBank::kFirstAlias + 4 * int(bank.groups.size()) <= WeaponBank::kScopeAlias;
  if (const char* dd = std::getenv("SF3N_BANCODUMP")) {
    if (FILE* f = std::fopen((std::string(dd) + "/banco_atlas.bin").c_str(), "wb")) { for (const auto& g : bank.groups) std::fwrite(g.data(), 2, g.size(), f); std::fclose(f); }
    if (FILE* f = std::fopen((std::string(dd) + "/banco_tris.txt").c_str(), "w")) {
      for (std::size_t i = 0; i < bank.models.size(); ++i) {
        const auto& md = bank.models[i];
        for (std::size_t t = 0; t < md.tris.size() && t < parsed[i].triangles().size(); ++t) {
          const auto& ot = parsed[i].triangles()[t]; const auto& bt = md.tris[t];
          std::fprintf(f, "%s %s %zu %04x %04x %d %d %d %d %d %d %04x %d %d %d %d %d %d %d\n", md.gmd.c_str(), md.source.c_str(), t, ot.texture_page, ot.clut, ot.uv[0].u, ot.uv[0].v, ot.uv[1].u, ot.uv[1].v, ot.uv[2].u, ot.uv[2].v,
                       bt.tpage, bt.uv[0], bt.uv[1], bt.uv[2], bt.uv[3], bt.uv[4], bt.uv[5], int(bt.ok));
        }
      }
      std::fclose(f);
    }
  }
  return bank;
}

struct Affine {
  std::array<double, 9> a{};
  std::array<double, 3> b{};
  std::array<double, 3> apply(double x, double y, double z) const {
    return {a[0] * x + a[1] * y + a[2] * z + b[0], a[3] * x + a[4] * y + a[5] * z + b[1], a[6] * x + a[7] * y + a[8] * z + b[2]};
  }
};

inline std::array<double, 4> quatFromRot(const double R[9]) {
  std::array<double, 4> q{};
  const double tr = R[0] + R[4] + R[8];
  if (tr > 0) { const double s = std::sqrt(tr + 1.0) * 2; q = {0.25 * s, (R[7] - R[5]) / s, (R[2] - R[6]) / s, (R[3] - R[1]) / s}; }
  else if (R[0] > R[4] && R[0] > R[8]) { const double s = std::sqrt(1.0 + R[0] - R[4] - R[8]) * 2; q = {(R[7] - R[5]) / s, 0.25 * s, (R[1] + R[3]) / s, (R[2] + R[6]) / s}; }
  else if (R[4] > R[8]) { const double s = std::sqrt(1.0 + R[4] - R[0] - R[8]) * 2; q = {(R[2] - R[6]) / s, (R[1] + R[3]) / s, 0.25 * s, (R[5] + R[7]) / s}; }
  else { const double s = std::sqrt(1.0 + R[8] - R[0] - R[4]) * 2; q = {(R[3] - R[1]) / s, (R[2] + R[6]) / s, (R[5] + R[7]) / s, 0.25 * s}; }
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (auto& v : q) v /= n > 0 ? n : 1;
  return q;
}
inline Affine lerpLinear(const Affine& p, const Affine& q, double t);
inline Affine lerp(const Affine& p, const Affine& q, double t) {
  static const bool lineal = std::getenv("SF3N_LERP_LINEAL") != nullptr;
  if (lineal) return lerpLinear(p, q, t);

  double sp[3], sq[3], Rp[9], Rq[9];
  for (int c = 0; c < 3; ++c) {
    sp[c] = std::sqrt(p.a[std::size_t(c)] * p.a[std::size_t(c)] + p.a[std::size_t(3 + c)] * p.a[std::size_t(3 + c)] + p.a[std::size_t(6 + c)] * p.a[std::size_t(6 + c)]);
    sq[c] = std::sqrt(q.a[std::size_t(c)] * q.a[std::size_t(c)] + q.a[std::size_t(3 + c)] * q.a[std::size_t(3 + c)] + q.a[std::size_t(6 + c)] * q.a[std::size_t(6 + c)]);
    if (sp[c] < 1e-9 || sq[c] < 1e-9) return lerpLinear(p, q, t);
    for (int r = 0; r < 3; ++r) { Rp[r * 3 + c] = p.a[std::size_t(r * 3 + c)] / sp[c]; Rq[r * 3 + c] = q.a[std::size_t(r * 3 + c)] / sq[c]; }
  }
  auto det = [](const double* A) { return A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]); };
  if (det(Rp) <= 0 || det(Rq) <= 0) return lerpLinear(p, q, t);
  auto a = quatFromRot(Rp), b = quatFromRot(Rq);
  double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0) { for (auto& v : b) v = -v; dot = -dot; }
  std::array<double, 4> m{};
  if (dot > 0.9995) { for (int k = 0; k < 4; ++k) m[std::size_t(k)] = a[std::size_t(k)] + (b[std::size_t(k)] - a[std::size_t(k)]) * t; }
  else {
    const double th = std::acos(std::clamp(dot, -1.0, 1.0)), st = std::sin(th);
    const double wa = std::sin((1 - t) * th) / st, wb = std::sin(t * th) / st;
    for (int k = 0; k < 4; ++k) m[std::size_t(k)] = a[std::size_t(k)] * wa + b[std::size_t(k)] * wb;
  }
  const double n = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3]);
  for (auto& v : m) v /= n;
  const double w = m[0], x = m[1], y = m[2], z = m[3];
  const double R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                       2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                       2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
  Affine r;
  for (int c = 0; c < 3; ++c) {
    const double sc = sp[c] + (sq[c] - sp[c]) * t;
    for (int rr = 0; rr < 3; ++rr) r.a[std::size_t(rr * 3 + c)] = R[rr * 3 + c] * sc;
  }
  for (int i = 0; i < 3; ++i) r.b[std::size_t(i)] = p.b[std::size_t(i)] + (q.b[std::size_t(i)] - p.b[std::size_t(i)]) * t;
  return r;
}
inline Affine lerpLinear(const Affine& p, const Affine& q, double t) {
  Affine r;
  for (int i = 0; i < 9; ++i) r.a[std::size_t(i)] = p.a[std::size_t(i)] + (q.a[std::size_t(i)] - p.a[std::size_t(i)]) * t;
  for (int i = 0; i < 3; ++i) r.b[std::size_t(i)] = p.b[std::size_t(i)] + (q.b[std::size_t(i)] - p.b[std::size_t(i)]) * t;
  return r;
}

using Ctl = std::array<std::uint32_t, 8>;

struct Light {
  std::array<double, 9> llm{};
  std::array<double, 3> bk{};
  std::array<double, 9> lcm{};
  bool valid{};
};
inline Light lightFromGte(const std::array<std::uint32_t, 32>& c) {
  Light l;
  l.llm = {double(lo16(c[8])), double(hi16(c[8])), double(lo16(c[9])), double(hi16(c[9])), double(lo16(c[10])), double(hi16(c[10])), double(lo16(c[11])), double(hi16(c[11])), double(lo16(c[12]))};
  l.bk = {double(std::int32_t(c[13])), double(std::int32_t(c[14])), double(std::int32_t(c[15]))};
  l.lcm = {double(lo16(c[16])), double(hi16(c[16])), double(lo16(c[17])), double(hi16(c[17])), double(lo16(c[18])), double(hi16(c[18])), double(lo16(c[19])), double(hi16(c[19])), double(lo16(c[20]))};
  l.valid = true;
  return l;
}

inline std::array<double, 3> shade(const Light& l, double nx, double ny, double nz, double rgbc = 128.0) {
  if (!l.valid) return {128, 128, 128};
  double ir[3];
  for (int i = 0; i < 3; ++i) ir[i] = std::max(0.0, (l.llm[i * 3] * nx + l.llm[i * 3 + 1] * ny + l.llm[i * 3 + 2] * nz) / 4096.0);
  std::array<double, 3> out{};
  for (int i = 0; i < 3; ++i) {
    const double c = l.bk[i] + (l.lcm[i * 3] * ir[0] + l.lcm[i * 3 + 1] * ir[1] + l.lcm[i * 3 + 2] * ir[2]) / 4096.0;
    out[i] = std::clamp(rgbc * c / 4096.0, 0.0, 255.0);
  }
  return out;
}

struct RawPart { std::uint32_t offset{}; Ctl ctl{}; Light light; };
struct RawActor { std::uint32_t a0{}; std::vector<RawPart> parts; std::vector<std::pair<std::uint32_t, std::uint32_t>> vsxy;   std::vector<std::uint32_t> tri_words;   };
struct RawObject { std::uint32_t a0{}; Ctl ctl{}; Light light; std::uint32_t a1{}; };

struct RawEffectPoint { Ctl ctl{}; std::array<std::int16_t, 3> v{}; std::uint32_t sxy{}; std::uint16_t sz{}; double ofx{}, ofy{}; bool actor{}; bool light{}; std::uint32_t ra{}; std::uint32_t h{}; };
struct EffectPrim {
  std::uint8_t kind{};
  std::uint8_t cmd{};
  std::uint8_t n{};
  std::array<std::array<double, 3>, 4> w{};
  std::array<std::uint32_t, 4> col{};
  std::array<std::uint16_t, 4> uv{};
  std::uint16_t clut{}, tpage{};
  double size_w{}, size_h{};
  double size_px_w{}, size_px_h{};
  std::array<std::array<double, 2>, 4> off{};
  double ghost_y = 1.0;
  double ps1x = 0, ps1y = 0;
  double ghost = 1.0;
};

struct ActorPose {
  std::uint32_t a0{};
  std::size_t model{};
  std::vector<std::optional<Affine>> parts;
  std::vector<Light> lights;
  int prev = -1;
  std::vector<std::uint32_t> vertex_sxy;
  std::uint64_t id = 0;
};
struct ObjectPose {
  std::uint32_t a0{};
  std::size_t model{};
  Affine xf;
  int occurrence{};
  Light light;
  std::uint32_t draw_flags{};
  int prev = -1;
};

struct Snapshot {
  Camera camera;
  std::vector<std::uint16_t> rooms;

  std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::uint32_t>> room_colors;

  std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::array<std::int16_t, 3>>> room_positions;
  std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::uint16_t>> room_ram_colors;
  std::vector<ActorPose> actors;
  std::vector<ObjectPose> objects;

  double fog_near = 0, fog_far = 0;
  double time{};
  double aim_yaw = 0, aim_pitch = 0;
  bool aim_valid = false;
  std::array<double, 3> cam_point{};
  bool cam_point_valid = false;
  double aim_cy = 0, aim_cp = 0;
  std::uint64_t field{};
  std::uint64_t sequence{};
  std::uint64_t tex_hash{};
  std::shared_ptr<const Mission> mission;
};

inline std::optional<Affine> worldFromGuest(const Ctl& cam, const Ctl& part) {
  auto mat = [](const Ctl& c, double m[9]) {
    m[0] = lo16(c[0]); m[1] = hi16(c[0]); m[2] = lo16(c[1]); m[3] = hi16(c[1]); m[4] = lo16(c[2]);
    m[5] = hi16(c[2]); m[6] = lo16(c[3]); m[7] = hi16(c[3]); m[8] = lo16(c[4]);
    for (int i = 0; i < 9; ++i) m[i] /= 4096.0;
  };
  double C[9], P[9];
  mat(cam, C); mat(part, P);
  const double det = C[0] * (C[4] * C[8] - C[5] * C[7]) - C[1] * (C[3] * C[8] - C[5] * C[6]) + C[2] * (C[3] * C[7] - C[4] * C[6]);
  if (std::fabs(det) < 1e-9) return std::nullopt;
  double I[9] = {(C[4] * C[8] - C[5] * C[7]) / det, (C[2] * C[7] - C[1] * C[8]) / det, (C[1] * C[5] - C[2] * C[4]) / det,
                 (C[5] * C[6] - C[3] * C[8]) / det, (C[0] * C[8] - C[2] * C[6]) / det, (C[2] * C[3] - C[0] * C[5]) / det,
                 (C[3] * C[7] - C[4] * C[6]) / det, (C[1] * C[6] - C[0] * C[7]) / det, (C[0] * C[4] - C[1] * C[3]) / det};
  const double dt[3] = {double(std::int32_t(part[5])) - double(std::int32_t(cam[5])), double(std::int32_t(part[6])) - double(std::int32_t(cam[6])),
                        double(std::int32_t(part[7])) - double(std::int32_t(cam[7]))};
  Affine r;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double acc = 0;
      for (int k = 0; k < 3; ++k) acc += I[i * 3 + k] * P[k * 3 + j];
      r.a[std::size_t(i * 3 + j)] = acc;
    }
    r.b[std::size_t(i)] = I[i * 3] * dt[0] + I[i * 3 + 1] * dt[1] + I[i * 3 + 2] * dt[2];
  }
  return r;
}

class BootWindow {
 public:
  static constexpr int canvas_w = 384;
  static constexpr int canvas_h = 240;
  static constexpr int ot_size = 4096;
  static inline const double near_base_ = std::getenv("SF3N_NEAR") ? std::atof(std::getenv("SF3N_NEAR")) : 24.0;
  double near_z = near_base_;

  std::atomic<bool> quit{false};
  std::atomic<bool> fast{false};
  FILE* log = stderr;
  sf::disc::Iso9660Image* image = nullptr;

  bool open() {
    g_cfg_msaaSamples = envInt("SF3N_MSAA", 4);
    g_cfg_bilinearFiltering = envInt("SF3N_BILINEAR", 1);

    g_cfg_trilinearFiltering = g_cfg_bilinearFiltering && envInt("SF3N_TRILINEAL", 1);
    g_cfg_anisotropicFiltering = g_cfg_trilinearFiltering && envInt("SF3N_ANISO", 1);

    g_cfg_smaa = envInt("SF3N_SMAA", 0) ? 1 : 0;
    if (g_cfg_smaa) g_cfg_msaaSamples = 0;
    g_cfg_volumetricEffects = 0;
    g_cfg_aspectMode = envInt("SF3N_43", 0) ? PSYX_ASPECT_ORIGINAL_4_3 : PSYX_ASPECT_ADAPTIVE;
    g_cfg_renderWidth = envInt("SF3N_W", 1920);
    g_cfg_renderHeight = envInt("SF3N_H", 1080);
    g_cfg_swapInterval = envInt("SF3N_VSYNC", 1);
    g_cfg_framebufferFeedback = 0;
    g_cfg_vblankThread = 0;
    g_cfg_pgxpTextureCorrection = 1;
    g_cfg_pgxpZBuffer = 1;
    static char title[] = SF3_TITULO;
    PsyX_Initialise(title, envInt("SF3N_WINW", 1280), envInt("SF3N_WINH", 720), 0);
    PsyX_EnableSwapInterval(g_cfg_swapInterval);
    PsyX_SetSwapInterval(1);
    setVideoMode(canvas_w, canvas_h, true);
    InitGeom();
    SetGeomOffset(canvas_w / 2, canvas_h / 2);
    SetGeomScreen(256);
    ot_.resize(ot_size);
    ot_semi_.resize(ot_size);
    ot_shadow_.resize(ot_size);
    ot_room_.resize(ot_size);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
      if (SDL_IsGameController(i)) { if (!pad_) pad_ = SDL_GameControllerOpen(i); else if (!pad2_) { pad2_ = SDL_GameControllerOpen(i); break; } }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
      SDL_AudioSpec want{}, have{};
      want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
      audio_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
      if (audio_) SDL_PauseAudioDevice(audio_, 0);
    }
    mouse_enabled_ = envInt("SF3N_MOUSE", 1) != 0;
    mouse_sens_ = std::max(1, envInt("SF3N_MOUSE_SENS", 10)) / 10.0;
    mouse_invert_ = envInt("SF3N_MOUSE_INV", 0) != 0;
    mouse_direct_turn_ = envInt("SF3N_MOUSE_TURN", 1) != 0;
    loadBindings();
    SDL_AddEventWatch([](void* self, SDL_Event* e) -> int {
      if (e->type == SDL_MOUSEWHEEL) static_cast<BootWindow*>(self)->mouse_wheel_ += e->wheel.y;
      return 0;
    }, this);
    rumble_enabled_ = envInt("SF3N_VIBRACION", 1) != 0;
    initTitleOptions();
    if (envInt("SF3N_FULL", 0)) { full_ = true; SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP); }

    if (!std::getenv("SF3N_W")) {
      const auto modes = monitorModes();
      bool ok = false;
      for (const auto& m : modes) ok = ok || (m.first == g_cfg_renderWidth && m.second == g_cfg_renderHeight);
      if (!ok) { std::fprintf(log, "nativo: resolucion %dx%d no es del monitor: %dx%d\n", g_cfg_renderWidth, g_cfg_renderHeight, modes[0].first, modes[0].second); applyResolution(modes[0].first, modes[0].second, true); }
      else applyResolution(g_cfg_renderWidth, g_cfg_renderHeight, false);
    }
    sched_ = field_start_ = Clock::now();
    return true;
  }

  std::uint16_t buttons() const {
    if (menu_open_) return 0;
    std::uint16_t b = rawButtons();
    if (name_entry_.load() && nameKeyHeld()) b = 0;
    if (game_list_.load()) b = std::uint16_t(b & ~0x00a0u);
    if (card_hold_) { if (b == 0 && pad_seq_.empty()) card_hold_ = false; b = 0; }
    if (!pad_seq_.empty()) b |= pad_seq_.front().first;
    if (start_block_) b = std::uint16_t(b & ~0x0008u);
    if (title_top_.load() || title_sub_.load()) {

      b = pad_seq_.empty() ? 0 : pad_seq_.front().first;
    }
    if (inject_start_ > 0) b |= 0x0008;
    if (inject_back_ > 0) b |= 0x1000;
    if (weapon_phase_ == 1 || wheel_tap_phase_ == 1) b |= 0x0001;

    if (const int wp = wheel_menu_phase_.load(); wp >= 1 && wp <= 3) { b |= 0x0001; if (wp == 2) b |= wheel_step_bit_.load(); }
    if (menu_step_on_.load() > 0) b |= menu_step_bit_.load();
    if (zoom_fields_.load() > 0) b |= zoom_bit_.load();

    if ((b & 0x8000) && flameLevel() > 0 && !ram_.empty() && weapon_cur_addr_ && ramByte(weapon_cur_addr_) == 25 && ((field_count_ >> 2) & 1)) b = std::uint16_t(b & ~0x8000u);
    return b;
  }

  SDL_GameController* p1Pad() const {
    if (two_player_ && p2_source_ == 0 && !pad2_) return nullptr;
    return pad_;
  }
  SDL_GameController* p2Pad() const {
    if (p2_source_ != 0) return nullptr;
    return pad2_ ? pad2_ : (two_player_ ? pad_ : nullptr);
  }
  bool p1Key(int code, const Uint8* k, Uint32 mb) const {
    if (!code) return false;
    if (two_player_ && p2_source_ == 1 && code < ctl::kMouseBase)
      for (const auto& row : binds2_.keys) for (int c : row) if (c == code) return false;
    return ctl::keyDown(code, k, mb, mouse_captured_);
  }
 public:

  void setTwoPlayer(bool on) {
    if (on && !two_player_.load()) { mv_saved_modern_ = movement_modern_; mv_saved_free_ = movement_free_; movement_modern_ = movement_free_ = false; mv_saved_ = true; }
    else if (!on && two_player_.load() && mv_saved_) { movement_modern_ = mv_saved_modern_; movement_free_ = mv_saved_free_; mv_saved_ = false; }
    two_player_ = on;
  }
  bool mv_saved_ = false, mv_saved_modern_ = false, mv_saved_free_ = false;
  bool p2Connected() const { return p2_source_ == 1 || pad2_ != nullptr || pad_ != nullptr; }
  std::uint16_t buttons2() const {
    if (menu_open_) return 0;
    std::uint16_t b = 0;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    SDL_GameController* pd = p2Pad();
    for (int a = 0; a < ctl::kCount; ++a) {
      const auto bit = ctl::kActions[a].bit;
      if (!bit) continue;
      bool on = false;
      if (p2_source_ == 1) for (int c : binds2_.keys[std::size_t(a)]) on = on || (c && c < ctl::kMouseBase && ctl::keyDown(c, k, 0, false));
      if (pd) for (int c : binds2_.pad[std::size_t(a)]) on = on || ctl::padDown(c, pd);
      if (on) b |= bit;
    }
    if (pd) {
      const int lx = SDL_GameControllerGetAxis(pd, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(pd, SDL_CONTROLLER_AXIS_LEFTY);
      if (lx < -16000) b |= 0x0080;
      if (lx > 16000) b |= 0x0020;
      if (ly < -16000) b |= 0x0010;
      if (ly > 16000) b |= 0x0040;
    }
    return b | scripted_buttons2_;
  }
  std::array<std::uint8_t, 4> sticks2() const {
    std::array<double, 4> v{0, 0, 0, 0};
    if (SDL_GameController* pd = p2Pad()) {
      auto ax = [&](SDL_GameControllerAxis a) { const double x = SDL_GameControllerGetAxis(pd, a) / 32767.0; return std::fabs(x) < 0.12 ? 0.0 : x; };
      v[2] = ax(SDL_CONTROLLER_AXIS_LEFTX); v[3] = ax(SDL_CONTROLLER_AXIS_LEFTY);
      v[0] = ax(SDL_CONTROLLER_AXIS_RIGHTX); v[1] = ax(SDL_CONTROLLER_AXIS_RIGHTY);
    }
    if (p2_source_ == 1) {
      const Uint8* k = SDL_GetKeyboardState(nullptr);
      auto held = [&](int a) { for (int c : binds2_.keys[std::size_t(a)]) if (c && c < ctl::kMouseBase && k[c]) return true; return false; };

    }
    std::array<std::uint8_t, 4> out{};
    for (int i = 0; i < 4; ++i) out[std::size_t(i)] = std::uint8_t(std::clamp(128.0 + std::clamp(v[std::size_t(i)], -1.0, 1.0) * 127.0, 0.0, 255.0));
    return out;
  }
  void setScriptedButtons2(std::uint16_t b) { scripted_buttons2_ = b; }
 private:
  std::atomic<bool> two_player_{false};
  int p2_source_ = 0;
  ctl::Bindings binds2_ = ctl::defaultsP2();
  std::uint16_t scripted_buttons2_ = 0;
  SDL_GameController* pad2_ = nullptr;
 public:
  std::uint16_t rawButtons() const {
    std::uint16_t b = rawButtonsBase();

    if (freeNow(b)) {
      double mx = 0, my = 0;
      if (b & 0x0080u) mx -= 1; if (b & 0x0020u) mx += 1;
      if (b & 0x0010u) my += 1; if (b & 0x0040u) my -= 1;
      if (SDL_GameController* p1 = p1Pad()) {
        const double ax = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0, ay = -SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0;
        if (std::hypot(ax, ay) > 0.35) { mx = ax; my = ay; }
      }
      b = std::uint16_t(b & ~0x00f0u);
      if (mx != 0 || my != 0) {
        free_move_ang_ = int(std::lround(std::atan2(mx, my) * 4096.0 / 6.283185307179586));

        static const bool giroAndando = std::getenv("SF3N_LIBRE_GIRO_EN_SITIO") == nullptr;

        static const bool sinAtras = std::getenv("SF3N_LIBRE_ATRAS") == nullptr;
        bool back = false;
        if (!sinAtras && player_ptr_addr_) {
          const auto pl = ramWord(player_ptr_addr_);
          if ((pl >> 24) == 0x80u) {
            const double hd = double(std::int32_t(ramWord(pl + 0x220)));
            const bool dispOk = disp_view_t_.load() > 0 && nowSeconds() - disp_view_t_.load() < 0.3;
            double rel = (dispOk ? disp_view_yaw_.load() : cam_free_yaw_) + free_move_ang_.load() - hd;
            while (rel > 2048) rel -= 4096; while (rel < -2048) rel += 4096;
            back = std::fabs(rel) > (free_back_.load() ? 1200.0 : 1536.0);
          }
        }
        free_back_ = back;

        if (back) b |= 0x0040u;
        else if (giroAndando || freeDirect() || free_turn_left_.load() < kFreeWalkTurn) b |= 0x0010u;
        {

          const bool dispOk2 = disp_view_t_.load() > 0 && nowSeconds() - disp_view_t_.load() < 0.3;
          const int ang = free_move_ang_.load();
          static const bool sinFijar = std::getenv("SF3N_LIBRE_SIN_FIJAR") != nullptr;
          const bool relock = sinFijar || !free_lock_ok_ || std::abs(((ang - free_lock_ang_) % 4096 + 6144) % 4096 - 2048) > 160;
          if (relock) { free_lock_base_ = dispOk2 ? disp_view_yaw_.load() : cam_free_yaw_; free_lock_cam_ = cam_free_yaw_; free_lock_ang_ = ang; free_lock_ok_ = true; }
          const double base = free_lock_base_ + (cam_free_yaw_ - free_lock_cam_);
          free_target_yaw_ = back ? kNoMove : int(std::lround(base + ang));
        }
      } else { free_move_ang_ = kNoMove; free_back_ = false; free_target_yaw_ = kNoMove; free_lock_ok_ = false; }
      return b;
    }
    free_move_ang_ = kNoMove; free_target_yaw_ = kNoMove;

    if (modernNow(b)) {
      const std::uint16_t lr = b & 0x00a0u;
      b = std::uint16_t(b & ~0x00a0u);

      if (modern_lr_block_.load() && !no_lr_block_) return b;
      if (lr & 0x0080u) b |= 0x0100u;
      if (lr & 0x0020u) b |= 0x0200u;
    }
    return b;
  }
  bool modernNow(std::uint16_t b) const { return movement_modern_ && in_play_.load() && !(b & 0x0c00) && !playerHanging(); }
  bool freeNow(std::uint16_t b) const { return movement_free_ && in_play_.load() && !(b & 0x0c00) && !playerHanging(); }

  bool playerDead() const {
    static const bool off = std::getenv("SF3N_MUERTE_CAMARA_LIBRE") != nullptr;
    if (off || !player_ptr_addr_) return false;
    const auto pl = ramWord(player_ptr_addr_);
    if ((pl >> 24) != 0x80u) return false;
    const auto a = ramWord(pl + 0x1d8);
    return a == 16u || a == 17u;
  }
  bool playerHanging() const {
    static const bool off = std::getenv("SF3N_COLGADO_SIN_CLASICO") != nullptr;
    if (off || !player_ptr_addr_) return false;
    const auto pl = ramWord(player_ptr_addr_);
    return (pl >> 24) == 0x80u && ramWord(pl + 0x1d8) == 6u;
  }
  static constexpr int kNoMove = 100000;
  static constexpr double kFreePitchMax = 341.0;
  mutable std::atomic<int> free_move_ang_{kNoMove};
  std::atomic<int> free_turn_left_{0};
  mutable std::atomic<bool> free_back_{false};
  mutable std::atomic<int> free_target_yaw_{kNoMove};
  mutable double free_lock_base_ = 0, free_lock_cam_ = 0; mutable int free_lock_ang_ = 0; mutable bool free_lock_ok_ = false;
  std::atomic<int> free_written_yaw_{kNoMove};
  std::atomic<int> free_flip_yaw_{kNoMove};
  std::atomic<bool> free_hold_{false}; std::atomic<int> free_hold_ang_{0};
  std::atomic<std::uint64_t> free_written_field_{0};
  static bool freeDirect() { static const bool v = std::getenv("SF3N_LIBRE_DIRECTO") != nullptr; return v; }
 public:

  template <class R32, class W32, class W16>
  void freeBodyTick(R32 r32, W32 w32, W16 w16) {
    const int flip = free_flip_yaw_.exchange(kNoMove);

    if (free_hold_.load()) {
      const int a = free_move_ang_.load();
      if (a == kNoMove || std::abs(((a - free_hold_ang_.load()) % 4096 + 6144) % 4096 - 2048) > 160) free_hold_ = false;
    }
    int t = freeDirect() ? free_target_yaw_.load() : flip;
    if (!freeDirect() && t == kNoMove && free_hold_.load()) t = free_target_yaw_.load();
    if (t == kNoMove || !movement_free_ || !in_play_.load() || field_count_ < game_drive_until_ || playerDead() || playerHanging() || !player_ptr_addr_) return;
    const std::uint32_t pl = r32(player_ptr_addr_);
    if ((pl >> 24) != 0x80u || pl < 0x80000400u) return;
    auto wrap = [](int d) { d %= 4096; if (d > 2048) d -= 4096; if (d < -2048) d += 4096; return d; };
    const int h = int(std::int32_t(r32(pl + 0x218)));
    const int c1 = int(std::int32_t(r32(pl - 0x21c))), c2 = int(std::int32_t(r32(pl - 0x10c))), c3 = int(std::int32_t(r32(pl - 0xcc)));
    const bool copies = std::abs(wrap(c1 - c2)) <= 4 && std::abs(wrap(c1 - c3)) <= 4 && std::abs(c1) < 0x100000;
    const bool nearH = std::abs(wrap(c1 - h)) <= 1300;
    static int nbad = 0;
    if (!copies || !nearH) { if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl && nbad++ < 20) std::fprintf(log, "LIBRE cuerpo: no se reconoce (%d %d %d, rumbo %d)\n", c1, c2, c3, h); return; }

    const int tc = c1 + wrap(t - c1), th = h + wrap(t - h);
    for (std::uint32_t o : {0x21cu, 0x10cu, 0xccu}) w32(pl - o, std::uint32_t(std::int32_t(tc)));
    for (std::uint32_t o : {0x218u, 0x220u, 0x228u}) w32(pl + o, std::uint32_t(std::int32_t(th)));
    const double a = double(t) * 6.283185307179586 / 4096.0;
    const int sn = int(std::lround(std::sin(a) * 4096.0)), cs = int(std::lround(std::cos(a) * 4096.0));
    w32(pl + 0x234, std::uint32_t(std::int32_t(sn))); w32(pl + 0x23c, std::uint32_t(std::int32_t(cs)));
    w16(pl + 0x2d4, std::uint16_t(std::int16_t(sn))); w16(pl + 0x2d8, std::uint16_t(std::int16_t(cs)));
    free_written_yaw_ = th; free_written_field_ = field_count_;
    if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "LIBRE cuerpo campo %llu: %d -> %d (rumbo %d)\n", (unsigned long long)field_count_, c1, tc, h);
  }
 private:

  std::atomic<double> disp_view_yaw_{0.0};
  std::atomic<double> disp_view_t_{-1.0};
  int free_turn_sign_ = 0;
  static constexpr int kFreeWalkTurn = 640;

  double cam_free_yaw_ = 0; bool cam_free_valid_ = false; bool free_was_aim_ = false;
  std::atomic<double> view_yaw_{0.0}; std::atomic<std::uint64_t> view_yaw_field_{0};
 public:
  void setViewYaw(double y) { view_yaw_ = y; view_yaw_field_ = field_count_; }
  int aim_turn_ = 0;
  int takeAimTurn() { const int t = aim_turn_; aim_turn_ = 0; return t; }
  std::atomic<double> cam_stick_{0.0};
 public:
  bool freeMovement() const { return movement_free_; }

  void camFreeSync(std::int32_t yaw, bool following) {
    if (!movement_free_) return;

    if (following && cam_free_valid_ && field_count_ >= game_drive_until_ && !free_was_aim_ && field_count_ <= last_walk_take_ + 30) return;
    if (!cam_free_valid_ && yaw == 0) return;
    if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl && (!cam_free_valid_ || std::fabs(cam_free_yaw_ - yaw) > 50)) std::fprintf(log, "LIBRE: sync campo %llu %d (antes %.0f valida %d sigue %d)\n", (unsigned long long)field_count_, yaw, cam_free_yaw_, int(cam_free_valid_), int(following));
    cam_free_yaw_ = yaw; cam_free_valid_ = true;
    if (!following) game_drive_until_ = std::max(game_drive_until_, field_count_ + 10);
  }

  void openCardPicker(bool save, std::vector<std::string> items) {
    { std::lock_guard lk(card_mtx_); card_items_ = std::move(items); card_save_ = save; }
    card_result_ = -99;
    card_req_ = true;
  }
  int cardPickResult() const { return card_result_.load(); }

  bool padSeqIdle() const { return pad_seq_.empty(); }

  void injectPadSeqSoft(const std::vector<std::pair<std::uint16_t, int>>& seq) { pad_seq_.assign(seq.begin(), seq.end()); }
  void injectPadSeq(const std::vector<std::pair<std::uint16_t, int>>& seq) {
    pad_seq_.assign(seq.begin(), seq.end());
    card_hold_ = true;
  }

  int takeCardDelete() { return card_delete_req_.exchange(-1); }
  void setCardItems(std::vector<std::string> items) {
    std::lock_guard lk(card_mtx_); card_items_ = std::move(items);
    if (menu_page_ == 40) menu_sel_ = std::min(menu_sel_, int(card_items_.size()));
  }

  void setNameEntry(bool on) { name_entry_ = on; }

  void setGameList(bool on) { game_list_ = on; }
  int takeListPage() { return list_page_req_.exchange(0); }
  bool takeListDelete() { return list_del_req_.exchange(false); }
  std::atomic<bool> game_list_{false};
  std::atomic<int> list_page_req_{0};
  std::atomic<bool> list_del_req_{false};
  std::uint16_t game_list_prev_ = 0;
  void addTyped(const std::string& t) { std::lock_guard lk(card_mtx_); typed_ += t; }
  std::string takeTyped() { std::lock_guard lk(card_mtx_); std::string t; t.swap(typed_); return t; }
  static bool nameKeyHeld() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    if (!k) return false;
    for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_0; ++sc) if (k[sc]) return true;
    return k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_DELETE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
  }
  bool cardPickerOpen() const { return card_req_.load() || (menu_open_.load() && menu_page_ == 40); }

  std::atomic<int> debug_mission_req_{-1}, debug_cur_mission_{-1};

  std::atomic<bool> cheat_god_{false}, cheat_weapons_{false}, cheat_invis_{false}, cheat_unlock_req_{false};
  std::atomic<bool> debug_missions_ok_{false};
  std::uint32_t debug_names_addr_ = 0x8010f594u;
  bool debugMissionsAvailable() const { return debug_missions_ok_.load(); }
  int takeDebugMission() { return debug_mission_req_.exchange(-1); }
  void setDebugMissions(bool ok, int cur) { debug_missions_ok_ = ok; debug_cur_mission_ = cur; }
  std::vector<std::string> debugMissionNames() const {
    std::vector<std::string> out;
    for (int i = 0; i < 39; ++i) {
      const std::uint32_t p = ramWord(debug_names_addr_ + 4u * std::uint32_t(i));
      if ((p >> 24) != 0x80u) break;
      std::string s;
      for (int c = 0; c < 12; ++c) { const auto ch = ramByte(p + std::uint32_t(c)); if (!ch || ch < 32 || ch > 126) break; s += char(ch); }
      if (s.empty()) break;
      out.push_back(s);
    }
    return out;
  }
  std::uint64_t game_drive_until_ = 0;

  static inline const int kFreeWalkGap = std::getenv("SF3N_LIBRE_MARGEN") ? std::atoi(std::getenv("SF3N_LIBRE_MARGEN")) : 30;
  bool playerSteering() const { return field_count_ <= last_walk_take_ + 8; }
  std::int32_t free_prev_hd_ = 0; int free_prev_dyaw_ = 0; bool free_prev_ok_ = false;

  double cam_free_pitch_ = 0;
  bool cam_following_ = true;
  void setCamFollowing(bool f) { cam_following_ = f; }

  double free_view_elev_ = 0; std::uint64_t free_view_elev_field_ = 0; int free_aim_pitch_extra_ = 0;
  void setFreeViewElev(double e) { free_view_elev_ = e; free_view_elev_field_ = field_count_; }

  static bool freePitchOn() { static const bool on = std::getenv("SF3N_LIBRE_INCLINAR") != nullptr; return on; }
  bool camFreePitch(double& p) const {
    if (!freePitchOn()) return false;
    std::int32_t y = 0;
    if (!cam_following_ || !camFreeYaw(y)) return false;
    p = cam_free_pitch_; return true;
  }
  bool camFreeYaw(std::int32_t& yaw) const { static const bool off = std::getenv("SF3N_LIBRE_SIN_CAMARA") != nullptr; if (off || !movement_free_ || playerDead() || !cam_free_valid_ || free_was_aim_ || field_count_ < game_drive_until_ || field_count_ > last_walk_take_ + kFreeWalkGap) return false; yaw = std::int32_t(std::lround(cam_free_yaw_)); return true; }
  std::uint16_t rawButtonsBase() const {
    std::uint16_t b = 0;
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    const Uint32 mb = mouse_captured_ ? SDL_GetMouseState(nullptr, nullptr) : 0;
    SDL_GameController* p1 = p1Pad();
    for (int a = 0; a < ctl::kCount; ++a) {
      const auto bit = ctl::kActions[a].bit;
      if (!bit) continue;
      bool on = false;
      for (int c : binds_.keys[std::size_t(a)]) on = on || p1Key(c, k, mb);
      for (int c : binds_.pad[std::size_t(a)]) on = on || ctl::padDown(c, p1);
      if (on) b |= bit;
    }
    if (p1) {

      const int lx = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_LEFTY);
      if (lx < -16000) b |= 0x0080;
      if (lx > 16000) b |= 0x0020;
      if (ly < -16000) b |= 0x0010;
      if (ly > 16000) b |= 0x0040;
    }
    if (static const char* mk = std::getenv("SF3N_MENUKEYS"); mk && menu_open_) {

      static double t0 = -1; if (t0 < 0) t0 = nowSeconds();
      const double t = nowSeconds() - t0;
      std::string str(mk); std::size_t p = 0;
      while (p < str.size()) {
        auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
        const auto it = str.substr(p, q - p); const auto c = it.find(':');
        const double at = std::atof(it.substr(0, c).c_str()); const std::string bn = it.substr(c + 1);
        if (t >= at && t < at + 0.25) b |= bn == "up" ? 0x10 : bn == "down" ? 0x40 : bn == "left" ? 0x80 : bn == "right" ? 0x20 : bn == "cross" ? 0x4000 : bn == "triangle" ? 0x1000 : bn == "l1" ? 0x400 : bn == "r1" ? 0x800 : 0;
        p = q + 1;
      }
      if (static double shotAt = 0; t - shotAt > 0.5) { shotAt = t; char nm[64]; std::snprintf(nm, sizeof nm, "mk_%03d.ppm", int(t * 10)); const_cast<BootWindow*>(this)->screenshot(nm); }
    }
    return scripted_buttons_ ? std::uint16_t(scripted_buttons_ | b) : b;
  }

  std::array<std::uint8_t, 4> sticks() {
    std::array<double, 4> v{0, 0, 0, 0};
    if (SDL_GameController* p1 = p1Pad()) {
      auto ax = [&](SDL_GameControllerAxis a) { const double x = SDL_GameControllerGetAxis(p1, a) / 32767.0; return std::fabs(x) < 0.12 ? 0.0 : x; };
      v[2] = ax(SDL_CONTROLLER_AXIS_LEFTX); v[3] = ax(SDL_CONTROLLER_AXIS_LEFTY);
      v[0] = ax(SDL_CONTROLLER_AXIS_RIGHTX); v[1] = ax(SDL_CONTROLLER_AXIS_RIGHTY);
    }
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    {
      const Uint32 mb = mouse_captured_ ? SDL_GetMouseState(nullptr, nullptr) : 0;
      auto held = [&](int a) {
        bool on = false;
        for (int c : binds_.keys[std::size_t(a)]) on = on || p1Key(c, k, mb);
        for (int c : binds_.pad[std::size_t(a)]) on = on || ctl::padDown(c, p1Pad());
        return on;
      };

      static const bool lateral = std::getenv("SF3N_PASO_LATERAL") != nullptr;
      if (lateral && held(ctl::kStrafeL)) v[0] -= 1.0;
      if (lateral && held(ctl::kStrafeR)) v[0] += 1.0;

      if (const std::uint16_t base = rawButtonsBase(); freeNow(base)) {
        v[2] = 0; v[3] = 0;
        if (SDL_GameController* p1 = p1Pad()) {
          const double rx = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0;
          if (std::fabs(rx) > 0.15) cam_stick_ = cam_stick_.load() + rx * 70.0;
          const double ry = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0;
          if (freePitchOn() && std::fabs(ry) > 0.15) cam_free_pitch_ = std::clamp(cam_free_pitch_ + (mouse_invert_ ? -1.0 : 1.0) * ry * 25.0, -kFreePitchMax, kFreePitchMax);
          v[0] = 0; v[1] = 0;
        }
      } else if (modernNow(base)) {
        if (SDL_GameController* p1 = p1Pad()) {
          const double rx = SDL_GameControllerGetAxis(p1, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0;
          v[2] = std::fabs(rx) < 0.12 ? 0.0 : rx;
          v[0] = lateral ? (held(ctl::kStrafeR) ? 1.0 : 0.0) - (held(ctl::kStrafeL) ? 1.0 : 0.0) : 0.0;
        }
      }
    }
    if (mouse_captured_) {
      const bool aiming = (rawButtons() & 0x0c00) != 0;
      const bool direct = mouse_direct_turn_ && nowSeconds() - last_aim_take_ < 0.3;

      std::unique_lock lkm(look_mtx_);
      int dx, dy;

      static const double lat = envInt("SF3N_RATON_MARGEN_MS", 25) / 1000.0;
      if ((camera_firm_ || movement_free_) && !aiming && !aim_mode_ && lat > 0.0) {
        const double cutoff = std::chrono::duration<double>(sched_.time_since_epoch()).count() - lat;
        dx = 0; dy = 0;
        std::lock_guard lq(mouse_q_mtx_);
        while (!mouse_q_.empty() && mouse_q_.front().t <= cutoff) { dx += mouse_q_.front().dx; dy += mouse_q_.front().dy; mouse_q_.pop_front(); }
        mouse_acc_x_ -= dx; mouse_acc_y_ -= dy;
      } else {
        { std::lock_guard lq(mouse_q_mtx_); mouse_q_.clear(); }
        dx = mouse_acc_x_.exchange(0); dy = mouse_acc_y_.exchange(0);
      }
      if (!direct) lkm.unlock();
      if (direct) {

        yaw_accum_ += dx * mouseGain();
        if (aiming || aim_mode_) pitch_accum_ += (mouse_invert_ ? -1.0 : 1.0) * dy * mouseGain();
        else if (movement_free_) {
          static const double k = envInt("SF3N_INCLINAR", 60) / 100.0;
          if (freePitchOn()) cam_free_pitch_ = std::clamp(cam_free_pitch_ + (mouse_invert_ ? -1.0 : 1.0) * dy * mouseGain() * k, -kFreePitchMax, kFreePitchMax);
        }
        mouse_dx_ = 0;
        mouse_dy_ = 0;
      } else {

        mouse_dx_ = mouse_dx_ * 0.15 + dx;
        v[2] += mouse_dx_ * mouse_sens_ / 18.0;
      }
      if (!direct) {
        mouse_dy_ = mouse_dy_ * 0.15 + dy;
        if (aiming) v[3] += (mouse_invert_ ? -1.0 : 1.0) * mouse_dy_ * mouse_sens_ / 18.0;
      }
    }
    std::array<std::uint8_t, 4> out{};
    for (int i = 0; i < 4; ++i) out[std::size_t(i)] = std::uint8_t(std::clamp(128.0 + std::clamp(v[std::size_t(i)], -1.0, 1.0) * 127.0, 0.0, 255.0));
    return out;
  }

  struct MouseEv { double t; int dx, dy; };
  std::mutex mouse_q_mtx_;
  std::deque<MouseEv> mouse_q_;
  void mouseAdd(int dx, int dy) {
    if (!dx && !dy) return;
    mouse_acc_x_ += dx; mouse_acc_y_ += dy;
    std::lock_guard lq(mouse_q_mtx_);
    mouse_q_.push_back({nowSeconds(), dx, dy});
    while (mouse_q_.size() > 2000) mouse_q_.pop_front();
  }
  double mouseGain() const {
    double g = mouse_sens_ * mouse_turn_scale_;
    static const bool off = std::getenv("SF3N_ZOOM_SENS") && std::atoi(std::getenv("SF3N_ZOOM_SENS")) == 0;
    const double fy = zoom_fy_.load();
    if (!off && zoom_sens_ && fy > 450.0) g *= std::clamp(715.0 / fy, 0.05, 1.0);
    return g;
  }
  std::atomic<double> zoom_fy_{0.0};
  bool zoom_sens_ = true;
  void setScriptedButtons(std::uint16_t b) { scripted_buttons_ = b; }

  void setRumble(std::uint8_t small, std::uint8_t large) { rumble_small_ = small; rumble_large_ = large; }

  void setFieldRate(double hz) { HudMap::center_y = 120.0; field_period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz)); std::fprintf(log, "nativo: %.2f campos por segundo\n", hz); }

  void addMouseTest(double dx, double dy) {
    yaw_accum_ += dx * mouse_sens_ * mouse_turn_scale_;
    if (aim_mode_) pitch_accum_ += dy * mouse_sens_ * mouse_turn_scale_;
  }

  void takeAim(bool aimPath, int& dyaw, int& dpitch) {
    std::lock_guard lk(look_mtx_);
    last_aim_take_ = nowSeconds();
    aim_mode_ = aimPath;

    static const double tau = envInt("SF3N_SUAVE_RATON", 0) / 1000.0;
    const double dtg = std::clamp(double(field_count_ - last_take_field_) * std::chrono::duration<double>(field_period_).count(), 0.005, 0.25);
    last_take_field_ = field_count_;
    if (!aimPath && camera_firm_ && tau > 0.0) {
      const double want = yaw_accum_ * (1.0 - std::exp(-dtg / tau));
      dyaw = int(want);
    } else dyaw = int(yaw_accum_);
    yaw_accum_ -= dyaw;

    if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "GIRO campo %llu apuntar %d angulo %d juego_hasta %llu camara %.0f libre %d juego %d pl %08x valida %d\n", (unsigned long long)field_count_, int(aimPath), free_move_ang_.load(), (unsigned long long)game_drive_until_, cam_free_yaw_, int(movement_free_), int(in_play_.load()), player_ptr_addr_ ? ramWord(player_ptr_addr_) : 0u, int(cam_free_valid_));

    if (!aimPath && movement_free_ && field_count_ > last_walk_take_ + 30 && last_walk_take_ != 0 && player_ptr_addr_) {
      const auto pl0 = ramWord(player_ptr_addr_);
      if ((pl0 >> 24) == 0x80u) { cam_free_yaw_ = double(std::int32_t(ramWord(pl0 + 0x220))); cam_free_valid_ = true;
        if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "LIBRE: vuelve el control, camara a la espalda (%.0f)\n", cam_free_yaw_); }
    }
    if (!aimPath) last_walk_take_ = field_count_;
    if (movement_free_ && in_play_.load()) {
      std::int32_t hd = 0;
      const auto pl = player_ptr_addr_ ? ramWord(player_ptr_addr_) : 0u;
      const bool havePl = (pl >> 24) == 0x80u;
      if (havePl) hd = std::int32_t(ramWord(pl + 0x220));
      auto wrap = [](double d) { while (d > 2048) d -= 4096; while (d < -2048) d += 4096; return d; };
      if (!cam_free_valid_ && havePl) { cam_free_yaw_ = view_yaw_field_ != 0 && view_yaw_field_ + 15 >= field_count_ ? view_yaw_.load() : double(hd); cam_free_valid_ = true; }
      if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl && field_count_ < 120) std::fprintf(log, "LIBRE: init? campo %llu hd %d vista %.0f (%llu)\n", (unsigned long long)field_count_, hd, view_yaw_.load(), (unsigned long long)view_yaw_field_.load());
      const bool lockHeld = (rawButtonsBase() & 0x0800u) != 0;
      if (aimPath || lockHeld) {

        if (aimPath && !free_was_aim_ && havePl) {

          const bool viewOk = view_yaw_field_ != 0 && view_yaw_field_ + 15 >= field_count_;
          const double aimYaw = viewOk ? view_yaw_.load() : cam_free_yaw_;

          aim_turn_ = int(std::lround(wrap(aimYaw - hd)));
          if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "LIBRE: empieza a apuntar camara %.0f vista %.0f (%d) Gabe %d -> giro %d\n", cam_free_yaw_, view_yaw_.load(), int(viewOk), hd, aim_turn_);

          static const int signo = envInt("SF3N_MIRA_SIGNO", 1);
          if (std::fabs(cam_free_pitch_) > 8.0 && free_view_elev_field_ + 10 >= field_count_) {
            const double ap = double(std::int32_t(ramWord(pl + 0x224)));
            free_aim_pitch_extra_ = int(std::lround(signo * free_view_elev_ - ap));
            if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "LIBRE: apuntar con inclinacion %.0f (cabeceo %.0f -> %+d)\n", free_view_elev_, ap, free_aim_pitch_extra_);
          }
        }
        free_was_aim_ = true;
        cam_free_yaw_ = hd + aim_turn_ + dyaw;
      } else {
        free_was_aim_ = false;

        if (havePl && free_prev_ok_) {
          const double extra = wrap(double(hd) - double(free_prev_hd_) - double(free_prev_dyaw_));
          const bool ours = free_written_field_.load() + 6 >= field_count_ && std::fabs(wrap(double(hd) - double(free_written_yaw_.load()))) < 24.0;
          if (std::fabs(extra) > 12.0 && !ours) {
            game_drive_until_ = std::max(game_drive_until_, field_count_ + 25);

            if (!std::getenv("SF3N_LIBRE_CAMARA_QUIETA")) cam_free_yaw_ += extra;
            if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) std::fprintf(log, "LIBRE: el juego gira a Gabe (%.0f) campo %llu\n", extra, (unsigned long long)field_count_);
          }
        }
        cam_free_yaw_ += dyaw + cam_stick_.exchange(0.0);
        dyaw = 0;
        const int ang = (field_count_ < game_drive_until_ || free_back_.load() || freeDirect()) ? kNoMove : free_move_ang_.load();
        if (ang != kNoMove && havePl) {
          static const bool vieja = std::getenv("SF3N_LIBRE_RUMBO_VIEJO") != nullptr;
          static const bool vista = std::getenv("SF3N_LIBRE_RUMBO_VISTA") != nullptr;
          const bool dispOk = vista && !vieja && disp_view_t_.load() > 0 && nowSeconds() - disp_view_t_.load() < 0.3;
          const double base = dispOk ? disp_view_yaw_.load() : cam_free_yaw_;
          const double target = base + ang;
          if (static const bool fl3 = std::getenv("SF3N_FREELOG") != nullptr; fl3) std::fprintf(log, "LIBRE base campo %llu vista %.0f libre %.0f usa %s\n", (unsigned long long)field_count_, disp_view_yaw_.load(), cam_free_yaw_, dispOk ? "vista" : "libre");
          double diff = wrap(target - hd);

          static const bool signo = std::getenv("SF3N_LIBRE_SIGNO") != nullptr;
          if (signo && std::fabs(diff) > 1800 && free_turn_sign_ != 0 && (diff > 0) != (free_turn_sign_ > 0)) diff = diff > 0 ? diff - 4096 : diff + 4096;
          free_turn_sign_ = std::fabs(diff) > 64 ? (diff > 0 ? 1 : -1) : 0;
          const double frames = std::max(1.0, dtg * 60.0);
          double rate = 150.0 * frames; if (std::fabs(diff) > 1400) rate *= 1.6;
          dyaw = int(std::lround(std::clamp(diff, -rate, rate)));

          static const bool sinMedia = std::getenv("SF3N_LIBRE_MEDIA_VUELTA") == nullptr;

          static const bool porGiro = std::getenv("SF3N_LIBRE_MEDIA_VUELTA_GIRO") != nullptr;
          if (!sinMedia && std::fabs(diff) > 1536) { dyaw = porGiro ? int(std::lround(diff)) : 0; free_flip_yaw_ = int(std::lround(double(hd) + diff)); free_hold_ang_ = ang; free_hold_ = true; }
          if (!sinMedia && !porGiro && free_hold_.load()) dyaw = 0;
          free_turn_left_ = int(std::fabs(diff - dyaw));
          if (static const bool fl2 = std::getenv("SF3N_FREELOG") != nullptr; fl2) std::fprintf(log, "LIBRE giro campo %llu hd %d objetivo %.0f falta %.0f dyaw %d\n", (unsigned long long)field_count_, int(hd), target, diff, dyaw);
        } else { free_turn_left_ = 0; free_turn_sign_ = 0; }
        while (cam_free_yaw_ > 1e6) cam_free_yaw_ -= 4096; while (cam_free_yaw_ < -1e6) cam_free_yaw_ += 4096;
      }
      free_prev_hd_ = hd; free_prev_dyaw_ = dyaw; free_prev_ok_ = havePl;
    } else { cam_free_valid_ = false; free_was_aim_ = false; free_prev_ok_ = false; }
    if (aimPath) { dpitch = int(pitch_accum_); pitch_accum_ -= dpitch; dpitch += free_aim_pitch_extra_; free_aim_pitch_extra_ = 0; }
    else { dpitch = 0; pitch_accum_ = 0; }
    taken_yaw_ += dyaw; taken_pitch_ += dpitch;
    if (aimPath && (dyaw || dpitch)) {
      look_hist_.push_back({field_count_, dyaw, dpitch});
      while (look_hist_.size() > 64) look_hist_.pop_front();
    }
  }

  std::uint64_t last_take_field_ = 0, last_walk_take_ = 0;
  struct LookRec { std::uint64_t field; int yaw, pitch; };
  std::mutex look_mtx_;
  std::deque<LookRec> look_hist_;
  void lookLead(std::uint64_t prvField, std::uint64_t curField, double alpha, double& yaw, double& pitch) {
    yaw = 0; pitch = 0;
    std::lock_guard lk(look_mtx_);
    for (const auto& r : look_hist_) {
      double w = r.field > curField ? 1.0 : r.field > prvField ? 1.0 - alpha : 0.0;
      yaw += w * r.yaw; pitch += w * r.pitch;
    }

    yaw += yaw_accum_; pitch += pitch_accum_;
    yaw += mouse_acc_x_.load() * mouseGain();
    pitch += (mouse_invert_ ? -1.0 : 1.0) * mouse_acc_y_.load() * mouseGain();
  }

  void onMissionFile(const std::string& name) {

    std::string p = name;
    if (!p.empty() && p[0] == '\\') p = p.substr(1);
    if (auto s = p.find(';'); s != std::string::npos) p = p.substr(0, s);
    for (auto& c : p) if (c == '\\') c = '/';
    if (p.rfind("FOG/", 0) != 0 && p.rfind("FOG2/", 0) != 0) return;
    if (mission_ && mission_->name == p) return;

    if (!in_state_load_ && p.rfind("FOG/", 0) == 0) { std::lock_guard lk(req_mtx_); pending_mission_ = p; }
    try {
      { auto nm = std::shared_ptr<Mission>(loadMission(*image, p, log)); mission_ = nm; addBankModels(); }
      vram_uploaded_ = false;
      room_cache_.clear();
      model_cache_.clear();
      light_reg_.clear(); light_hist_.clear();
    } catch (const std::exception& e) {
      std::fprintf(log, "nativo: no se pudo cargar %s: %s\n", p.c_str(), e.what());
      mission_.reset();
    }
  }

  std::atomic<double> voice_semitones{0.0};

  void setTurboHints(int audioPeak, std::uint64_t mdecBlocks) {
    quiet_fields_ = audioPeak < 300 ? quiet_fields_ + 1 : 0;
    if (mdecBlocks != last_mdec_blocks_) { last_mdec_blocks_ = mdecBlocks; last_mdec_field_ = field_count_; }
  }
  static double iniDouble(const char* key, double d) {
    if (auto v = ini().find(key); v != ini().end() && !v->second.empty()) return std::atof(v->second.c_str());
    return d;
  }

  static std::string iniGet(const char* key, const char* d);
  static std::string dataDir() {
    static const std::string dir = [] {
      std::string d;
      if (const char* e = std::getenv("SF3N_DATOS")) d = e;
      else { const char* base = SDL_GetBasePath(); d = base ? base : ""; }
      while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
      std::error_code ec;
      if (!d.empty()) std::filesystem::create_directories(d, ec);
      return d.empty() ? std::string() : d + "/";
    }();
    return dir;
  }
  static std::string exeDir() {
    const char* base = SDL_GetBasePath();
    return base ? base : "";
  }
  static std::string iniPath() {
    static const std::string path = [] {
      const std::string p = dataDir() + SF3_INI;

      std::error_code ec;
      if (!std::filesystem::exists(p, ec) && std::filesystem::exists(exeDir() + SF3_INI, ec))
        std::filesystem::copy_file(exeDir() + SF3_INI, p, ec);
      return p;
    }();
    return path;
  }
  static void iniSave(const std::string& key, const std::string& value) {
    const std::string path = iniPath();
    std::vector<std::string> lines;
    bool done = false;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
      char line[512];
      while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const auto eq = l.find('=');
        if (eq != std::string::npos && l[0] != '#' && l[0] != ';') {
          std::string k = l.substr(0, eq);
          while (!k.empty() && k.back() == ' ') k.pop_back();
          if (k == key) { l = key + "=" + value; done = true; }
        }
        lines.push_back(l);
      }
      std::fclose(f);
    }
    if (!done) lines.push_back(key + "=" + value);
    if (FILE* f = std::fopen(path.c_str(), "w")) { for (const auto& l : lines) std::fprintf(f, "%s\n", l.c_str()); std::fclose(f); }
    ini()[key] = value;

    if (key == "idioma") {
      const char* bp = SDL_GetBasePath();
      const std::string exeIni = std::string(bp ? bp : "") + SF3_INI;
      if (exeIni != path) {
        std::vector<std::string> el; bool has = false;
        if (FILE* f = std::fopen(exeIni.c_str(), "r")) {
          char line[512];
          while (std::fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if ((l.rfind("idioma", 0) == 0 || l.rfind("lenguaje", 0) == 0) && l.find('=') != std::string::npos) { l = l.substr(0, l.find('=') + 1) + value; has = true; }
            el.push_back(l);
          }
          std::fclose(f);
        }
        if (has) if (FILE* f = std::fopen(exeIni.c_str(), "w")) { for (const auto& l : el) std::fprintf(f, "%s\n", l.c_str()); std::fclose(f); }
      }
    }
  }
  int takeSaveRequest() { return save_request_.exchange(0); }
  bool takeMarkRequest() { return mark_request_.exchange(false); }
  int takeLoadRequest() { return load_request_.exchange(0); }
  void notify(const std::string& text) { notify(text.c_str()); }
  void notify(const char* text) {
    std::fprintf(log, "nativo: %s\n", text);
    std::lock_guard lk(mtx_);
    pub_title_ = std::string(SF3_TITULO " - ") + pctext::translateLang(text, uiLang());
  }
  void afterStateLoad(const std::string& fogName) {
    in_state_load_ = true;
    cam_free_valid_ = false; free_was_aim_ = false; free_prev_ok_ = false; game_drive_until_ = 0; last_walk_take_ = 0; view_yaw_field_ = 0;
    if (!fogName.empty()) onMissionFile(fogName);
    in_state_load_ = false;
    { std::lock_guard lk(req_mtx_); pending_mission_.clear(); }
    room_cache_.clear();
    model_cache_.clear();
    building_ = false;
    emu_cur_.reset();
    eff_raw_.clear();
    eff_maps_.clear();
    {
      std::lock_guard lk(mtx_);
      pub_hist_.clear();
      pub_effects_.clear(); pub_glow_hist_.clear();
      pub_overlay_.clear();
    }
    if (audio_) SDL_ClearQueuedAudio(audio_);
  }

  void onFlip() { if (building_) finalize(); }

  void beginPhase(int phase) {
    if (!building_) {
      building_ = true;
      pending_ = Snapshot{};
      pending_actors_.clear();
      pending_objects_.clear();
      have_camera_ = false;
    }
    phase_ = phase;
    hooks_this_field_ = true;
  }

  void onWorldRoom(const sf::psx::R3000State& s, std::span<const std::byte> ram) {
    if (!mission_) return;
    const auto camera = decomposeGteCamera(s.gte.control);
    if (!camera.valid) return;
    world_h_ = s.gte.control[26] & 0xffffu;
    const std::uint32_t a0 = s.gpr[4];
    const auto room = identifyRoom(a0, ram);
    if (static const bool rl = std::getenv("SF3N_ROOMCALLLOG") != nullptr; rl)
      std::fprintf(log, "SALA campo %llu a0 %08x %s (%d) ra %08x t %d %d %d\n", (unsigned long long)field_count_, a0, room ? mission_->room_names[*room].c_str() : "?", room ? int(*room) : -1, s.gpr[31], std::int32_t(s.gte.control[5]), std::int32_t(s.gte.control[6]), std::int32_t(s.gte.control[7]));
    if (static const bool fl = std::getenv("SF3N_FOGLOG") != nullptr; fl && (field_count_ % 100) == 0)
      std::fprintf(log, "NIEBLA campo %llu FC %d %d %d DQA %d DQB %d H %u\n", (unsigned long long)field_count_, std::int32_t(s.gte.control[21]), std::int32_t(s.gte.control[22]), std::int32_t(s.gte.control[23]), std::int32_t(std::int16_t(s.gte.control[27])), std::int32_t(s.gte.control[28]), s.gte.control[26] & 0xffff);
    if (!room) {

      const auto mi = identifyModel(a0);
      if (static const bool rl = std::getenv("SF3N_ROOMCALLLOG") != nullptr; rl)
        std::fprintf(log, "  suelto a0 %08x -> %s\n", a0, mi ? mission_->models[*mi].name.c_str() : "(no reconocido)");
      if (mi && std::holds_alternative<sf::assets::EmdScene>(mission_->models[*mi].geometry)) {
          if (!building_) beginPhase(1);
          Ctl c{};
          for (int i = 0; i < 8; ++i) c[std::size_t(i)] = s.gte.control[std::size_t(i)];
          pending_objects_.push_back(RawObject{a0, c, lightFromGte(s.gte.control), 0});
        }
      return;
    }
    if (building_ && std::find(pending_.rooms.begin(), pending_.rooms.end(), *room) != pending_.rooms.end()) return;
    beginPhase(1);
    in_actor_ = false;
    if (!have_camera_) {
      if (static int ol = std::getenv("SF3N_OFLOG") ? 20 : 0; ol > 0 && field_count_ % 50 == 0) { --ol; std::fprintf(log, "OF campo %llu ofx %.2f ofy %.2f h %u\n", (unsigned long long)field_count_, std::int32_t(s.gte.control[24]) / 65536.0, std::int32_t(s.gte.control[25]) / 65536.0, s.gte.control[26] & 0xffff); }
      pending_.camera = camera;
      std::memcpy(pending_ctl_.data(), s.gte.control.data(), 8 * 4);
      have_camera_ = true;
    }
    if (static const bool rd = std::getenv("SF3N_ROOMCOLDIAG") != nullptr; rd && (field_count_ % 10) == 0) {
      const auto& secs = mission_->rooms[*room].sections();
      const auto& offs = mission_->section_offsets[*room];
      const std::uint32_t base = a0 & 0x1fffffu;
      int diff = 0, total = 0; std::string sample;
      for (std::size_t si = 0; si < secs.size() && si < offs.size(); ++si) {
        const std::uint32_t so = base + offs[si];
        if (so + 0x28 > ram.size()) continue;
        const std::uint32_t rel = le32(ram, so + 0x24);
        for (std::size_t vi = 0; vi < secs[si].vertices.size(); ++vi) {
          const std::size_t o = so + rel + vi * 8 + 6;
          if (o + 2 > ram.size()) break;
          const std::uint16_t c = std::uint16_t(std::to_integer<unsigned>(ram[o]) | (std::to_integer<unsigned>(ram[o + 1]) << 8));
          ++total;
          if (c != secs[si].vertices[vi].color && vi == 0) std::fprintf(log, "ROOMCOLSEC %s sec %zu n %zu v0 %04x->%04x\n", mission_->room_names[*room].c_str(), si, secs[si].vertices.size(), secs[si].vertices[vi].color, c);
          if (c != secs[si].vertices[vi].color) { if (diff < 4) { char b2[64]; std::snprintf(b2, sizeof b2, " %04x->%04x", secs[si].vertices[vi].color, c); sample += b2; } ++diff; }
        }
      }
      std::fprintf(log, "ROOMCOL %s campo %llu dif %d/%d%s\n", mission_->room_names[*room].c_str(), (unsigned long long)field_count_, diff, total, sample.c_str());
    }
    if (static const bool pd = std::getenv("SF3N_ROOMPOSDIAG") != nullptr; pd && (field_count_ % 25) == 0) {
      const auto& secs = mission_->rooms[*room].sections();
      const auto& offs = mission_->section_offsets[*room];
      const std::uint32_t base = a0 & 0x1fffffu;
      for (std::size_t si = 0; si < secs.size() && si < offs.size(); ++si) {
        const std::uint32_t so = base + offs[si];
        if (so + 0x28 > ram.size()) continue;
        const std::uint32_t rel = le32(ram, so + 0x24);
        int diff = 0; std::string sample;
        for (std::size_t vi = 0; vi < secs[si].vertices.size(); ++vi) {
          const std::size_t o = so + rel + vi * 8;
          if (o + 8 > ram.size()) break;
          auto rd = [&](std::size_t k) { return std::int16_t(std::to_integer<unsigned>(ram[o + k]) | (std::to_integer<unsigned>(ram[o + k + 1]) << 8)); };
          const auto& v = secs[si].vertices[vi];
          if (rd(0) != v.x || rd(2) != v.y || rd(4) != v.z) { if (diff < 3) { char b2[96]; std::snprintf(b2, sizeof b2, " v%zu (%d,%d,%d)->(%d,%d,%d)", vi, v.x, v.y, v.z, rd(0), rd(2), rd(4)); sample += b2; } ++diff; }
        }
        if (diff) std::fprintf(log, "ROOMPOS %s sec %zu campo %llu dif %d/%zu%s\n", mission_->room_names[*room].c_str(), si, (unsigned long long)field_count_, diff, secs[si].vertices.size(), sample.c_str());
      }
    }
    if (static const bool liveRoom = std::getenv("SF3N_SALA_VIVA") != nullptr; liveRoom) {

      const auto& secs = mission_->rooms[*room].sections();
      const auto& offs = mission_->section_offsets[*room];
      const std::uint32_t base = a0 & 0x1fffffu;
      for (std::size_t si = 0; si < secs.size() && si < offs.size(); ++si) {
        const std::uint32_t so = base + offs[si];
        if (so + 0x28 > ram.size()) continue;
        const std::uint32_t rel = le32(ram, so + 0x24);
        const auto& vs = secs[si].vertices;
        if (so + rel + vs.size() * 8 > ram.size()) continue;
        auto rd = [&](std::size_t o) { return std::int16_t(std::to_integer<unsigned>(ram[o]) | (std::to_integer<unsigned>(ram[o + 1]) << 8)); };
        std::size_t vi = 0;
        for (; vi < vs.size(); ++vi) {
          const std::size_t o = so + rel + vi * 8;
          if (rd(o) != vs[vi].x || rd(o + 2) != vs[vi].y || rd(o + 4) != vs[vi].z) break;
        }
        if (vi == vs.size()) continue;
        std::vector<std::array<std::int16_t, 3>> pos(vs.size());
        for (std::size_t k = 0; k < vs.size(); ++k) {
          const std::size_t o = so + rel + k * 8;
          pos[k] = {rd(o), rd(o + 2), rd(o + 4)};
        }
        pending_.room_positions[{std::uint16_t(*room), std::uint16_t(si)}] = std::move(pos);
      }
    }
    if (static const bool ramCol = envInt("SF3N_COLOR_RAM", 1) != 0; ramCol) {

      const auto& secs = mission_->rooms[*room].sections();
      const auto& offs = mission_->section_offsets[*room];
      const std::uint32_t base = a0 & 0x1fffffu;
      for (std::size_t si = 0; si < secs.size() && si < offs.size(); ++si) {
        const std::uint32_t so = base + offs[si];
        if (so + 0x28 > ram.size()) continue;
        const std::uint32_t rel = le32(ram, so + 0x24);
        const auto& vs = secs[si].vertices;
        if (so + rel + vs.size() * 8 > ram.size()) continue;
        bool diff = false, bad = false;
        std::vector<std::uint16_t> cs(vs.size());
        auto rd16 = [&](std::size_t o) { return std::uint16_t(std::to_integer<unsigned>(ram[o]) | (std::to_integer<unsigned>(ram[o + 1]) << 8)); };
        for (std::size_t vi = 0; vi < vs.size() && !bad; ++vi) {
          const std::size_t o = so + rel + vi * 8;

          if (std::int16_t(rd16(o)) != vs[vi].x || std::int16_t(rd16(o + 2)) != vs[vi].y || std::int16_t(rd16(o + 4)) != vs[vi].z) { bad = true; break; }
          cs[vi] = rd16(o + 6);
          if (cs[vi] != vs[vi].color) {
            diff = true;
            const int cr = cs[vi] & 31, cg = (cs[vi] >> 5) & 31, cb = (cs[vi] >> 10) & 31;
            if (std::abs(cr - cg) > 2 || std::abs(cg - cb) > 2 || std::abs(cr - cb) > 2 || (cs[vi] & 0x8000)) bad = true;
          }
        }
        if (static const bool rl = std::getenv("SF3N_COLORRAMLOG") != nullptr; rl && diff) std::fprintf(log, "COLORRAM %s sec %zu %s\n", mission_->room_names[*room].c_str(), si, bad ? "descartada" : "usada");
        if (diff && !bad) pending_.room_ram_colors[{std::uint16_t(*room), std::uint16_t(si)}] = std::move(cs);
      }
    }
    pending_.rooms.push_back(*room);
    cur_room_ = int(*room);
    cur_room_a0_ = a0;
  }

  std::unordered_set<std::uint32_t> room_tex_keys_;
  const void* room_tex_mis_ = nullptr;
  bool isRoomTexture(std::uint16_t tpage, std::uint16_t clut) {
    if (!mission_) return false;
    if (room_tex_mis_ != mission_.get()) {
      room_tex_keys_.clear(); room_tex_mis_ = mission_.get();
      for (const auto& room : mission_->rooms)
        for (const auto& sec : room.sections())
          for (const auto& poly : sec.polygons) room_tex_keys_.insert((std::uint32_t(poly.texture_page & 0x1f) << 16) | poly.clut);
    }
    return room_tex_keys_.count((std::uint32_t(tpage & 0x1f) << 16) | clut) != 0;
  }

  std::unordered_set<std::uint64_t> room_vtx_;
  const void* room_vtx_mis_ = nullptr;
  static std::uint64_t vkey(long x, long y, long z) { return (std::uint64_t(x & 0xffff) << 32) | (std::uint64_t(y & 0xffff) << 16) | std::uint64_t(z & 0xffff); }
  double nearestRoomVertex(const std::array<double, 3>& w) {
    double best = 1e9;
    if (!mission_) return best;
    for (const auto& room : mission_->rooms)
      for (const auto& sec : room.sections())
        for (const auto& v : sec.vertices) best = std::min(best, std::sqrt((v.x - w[0]) * (v.x - w[0]) + (v.y - w[1]) * (v.y - w[1]) + (v.z - w[2]) * (v.z - w[2])));
    return best;
  }
  bool isRoomVertex(const std::array<double, 3>& w) {
    if (!mission_) return false;
    if (room_vtx_mis_ != mission_.get()) {
      room_vtx_.clear(); room_vtx_mis_ = mission_.get();
      for (const auto& room : mission_->rooms)
        for (const auto& sec : room.sections())
          for (const auto& v : sec.vertices) room_vtx_.insert(vkey(v.x, v.y, v.z));
    }
    const long x = std::lround(w[0]), y = std::lround(w[1]), z = std::lround(w[2]);
    for (long dx = -3; dx <= 3; ++dx) for (long dy = -3; dy <= 3; ++dy) for (long dz = -3; dz <= 3; ++dz)
      if (room_vtx_.count(vkey(x + dx, y + dy, z + dz))) return true;
    return false;
  }

  void onRoomSection(const sf::psx::R3000State& s, std::span<const std::byte> scratch, std::span<const std::byte> ram = {}) {
    if (!building_ || phase_ != 1 || cur_room_ < 0) return;
    const std::uint32_t secPtr = s.gpr[4];
    const std::uint32_t off = secPtr - cur_room_a0_;
    const auto& offs = mission_->section_offsets[std::size_t(cur_room_)];
    auto it = std::find(offs.begin(), offs.end(), off);

    if (static const bool tabla = std::getenv("SF3N_SECCION_DISCO") == nullptr; tabla && !ram.empty()) {
      it = offs.end();
      for (std::size_t i = 0; i < offs.size(); ++i) {
        const std::uint32_t a = (cur_room_a0_ + 4u + 4u * std::uint32_t(i)) & 0x1fffffu;
        if (a + 4 > ram.size()) break;
        std::uint32_t v; std::memcpy(&v, ram.data() + a, 4);
        if (v == secPtr) { it = offs.begin() + std::ptrdiff_t(i); break; }
      }
    }
    if (static const bool sl = std::getenv("SF3N_SECLOG") != nullptr; sl && it == offs.end()) { static int once = 0; if (once++ < 3) { std::fprintf(log, "SECOFFS sala %d:", cur_room_); for (auto o : offs) std::fprintf(log, " %x", o); std::fprintf(log, "\n"); } std::fprintf(log, "SECLOG campo %llu sala %d sin seccion off %x\n", (unsigned long long)field_count_, cur_room_, off); }
    if (it == offs.end()) return;
    const auto si = std::size_t(it - offs.begin());
    const auto& sec = mission_->rooms[std::size_t(cur_room_)].sections()[si];
    const std::size_t n = sec.vertices.size();
    if (static const bool sl = std::getenv("SF3N_SECLOG") != nullptr; sl) std::fprintf(log, "SECLOG campo %llu sala %d sec %zu n %zu scratch %zu\n", (unsigned long long)field_count_, cur_room_, si, n, scratch.size());
    if (n * 8 > scratch.size()) return;
    std::vector<std::uint32_t> cols(n);
    for (std::size_t i = 0; i < n; ++i) std::memcpy(&cols[i], scratch.data() + i * 8 + 4, 4);
    if (static const long fd = std::getenv("SF3N_FOGDIAG") ? std::atol(std::getenv("SF3N_FOGDIAG")) : -1; fd >= 0 && long(field_count_) >= fd && long(field_count_) < fd + 3) {
      const auto& c = s.gte.control;
      auto lo = [](std::uint32_t v) { return double(std::int16_t(v & 0xffff)); };
      auto hi = [](std::uint32_t v) { return double(std::int16_t(v >> 16)); };
      const double r31 = lo(c[3]), r32 = hi(c[3]), r33 = lo(c[4]);
      std::fprintf(log, "FOGDIAG sala %d sec %zu FC %d %d %d DQA %d DQB %d H %u\n", cur_room_, si, std::int32_t(c[21]), std::int32_t(c[22]), std::int32_t(c[23]), std::int32_t(std::int16_t(c[27])), std::int32_t(c[28]), c[26] & 0xffff);
      for (std::size_t i = 0; i < n && i < 400; ++i) {
        const auto& v = sec.vertices[i];
        const double z = (r31 * v.x + r32 * v.y + r33 * v.z) / 4096.0 + double(std::int32_t(c[7]));
        std::uint32_t w0; std::memcpy(&w0, scratch.data() + i * 8, 4);
        std::fprintf(log, "  v%zu est %06x vivo %06x z %.0f w0 %08x\n", i, unsigned(((v.color & 31) << 3) | (((v.color >> 5) & 31) << 11) | (((v.color >> 10) & 31) << 19)), cols[i], z, w0);
      }
    }
    if (static const bool ca = std::getenv("SF3N_ROOMCOLAVG") != nullptr; ca) {
      static std::uint64_t fc = 0; static double sr = 0, sg = 0; static std::size_t cnt = 0;
      if (fc != field_count_) { if (cnt) std::fprintf(log, "ROOMAVG campo %llu %.1f %.1f (%zu)\n", (unsigned long long)fc, sr / double(cnt), sg / double(cnt), cnt); fc = field_count_; sr = sg = 0; cnt = 0; }
      for (auto c : cols) { sr += double(c & 0xff); sg += double((c >> 8) & 0xff); ++cnt; }
    }
    if (static const char* bl = std::getenv("SF3N_BLUELOG"); bl && long(field_count_) >= std::atol(bl) && long(field_count_) < std::atol(bl) + 4) {
      int blue = 0;
      for (auto c : cols) { const int r = int(c & 0xff), g = int((c >> 8) & 0xff), b = int((c >> 16) & 0xff); if (b > r + 30 && b > g + 30) ++blue; }
      if (blue) std::fprintf(log, "AZUL campo %llu sala %d sec %zu %d/%zu\n", (unsigned long long)field_count_, cur_room_, si, blue, cols.size());
    }
    {

      std::uint32_t fw = 0;
      if (scratch.size() >= 0x3ac) std::memcpy(&fw, scratch.data() + 0x3a8, 4);
      int start = 0x7ff, shift = 0;
      if (fw) { start = int(fw & 0xffff); shift = int(fw >> 16) & 31; }
      if (start < 0x1000) { pending_.fog_near = double(start) / 0.75; pending_.fog_far = double(start + (4096 >> shift)) / 0.75; }
      else pending_.fog_near = pending_.fog_far = 0;
    }
    if (static const bool cs = std::getenv("SF3N_COLSPIKE") != nullptr; cs) {
      static std::map<std::pair<int, int>, std::vector<std::uint32_t>> prev;
      auto& pv = prev[{cur_room_, int(si)}];
      if (pv.size() == cols.size()) {
        int worst = 0; std::size_t wi = 0;
        for (std::size_t i = 0; i < cols.size(); ++i)
          for (int k = 0; k < 3; ++k) { const int d = std::abs(int((cols[i] >> (8 * k)) & 0xff) - int((pv[i] >> (8 * k)) & 0xff)); if (d > worst) { worst = d; wi = i; } }
        if (worst > 60) std::fprintf(log, "COLSPIKE campo %llu sala %d sec %zu v%zu %06x -> %06x (n %zu)\n", (unsigned long long)field_count_, cur_room_, si, wi, pv[wi] & 0xffffff, cols[wi] & 0xffffff, cols.size());
      }
      pv = cols;
    }
    pending_.room_colors[{std::uint16_t(cur_room_), std::uint16_t(si)}] = std::move(cols);
  }

  void onActorTris(std::span<const std::byte> ram) {
    if (!mission_ || !building_ || !in_actor_ || pending_actors_.empty()) return;
    auto& ra = pending_actors_.back();
    const auto mi = identifyModel(ra.a0);
    if (!mi) return;
    const auto* hmd = std::get_if<sf::assets::HmdModel>(&mission_->models[*mi].geometry);
    if (!hmd) return;
    const std::size_t n = hmd->triangles().size();
    const std::size_t base = (ra.a0 & 0x1fffffu) + 0x24;
    if (base + n * 16 > ram.size()) return;
    ra.tri_words.resize(n * 3);
    int diff = 0;
    for (std::size_t i = 0; i < n; ++i) {
      for (int k = 0; k < 3; ++k) ra.tri_words[i * 3 + std::size_t(k)] = le32(ram, base + i * 16 + std::size_t(k) * 4);
      if (static const bool d = std::getenv("SF3N_TRIDIAG") != nullptr; d) {
        const auto& t = hmd->triangles()[i];
        const std::uint32_t w0 = std::uint32_t(t.uv[0].u | (t.uv[0].v << 8)) | (std::uint32_t(t.clut) << 16);
        const std::uint32_t w1 = std::uint32_t(t.uv[1].u | (t.uv[1].v << 8)) | (std::uint32_t(t.texture_page) << 16);
        if (ra.tri_words[i * 3] != w0 || ra.tri_words[i * 3 + 1] != w1 || (ra.tri_words[i * 3 + 2] & 0xffff) != std::uint32_t(t.uv[2].u | (t.uv[2].v << 8))) ++diff;
      }
    }
    if (static const bool d = std::getenv("SF3N_TRIDIAG") != nullptr; d && field_count_ % 10 == 0)
      std::fprintf(log, "TRIS %s@%08x campo %llu dif %d/%zu w %08x %08x %08x file clut %04x tp %04x\n", mission_->models[*mi].name.c_str(), ra.a0, (unsigned long long)field_count_, diff, n, ra.tri_words[0], ra.tri_words[1], ra.tri_words[2], hmd->triangles()[0].clut, hmd->triangles()[0].texture_page);
  }

  void onActorBegin(const sf::psx::R3000State& s) {
    if (!mission_) return;
    beginPhase(0);
    pending_actors_.push_back(RawActor{s.gpr[4], {}});
    last_part_ctl_ = {};
    in_actor_ = true;
  }

  void onActorPart(const sf::psx::R3000State& s) {
    if (!building_ || pending_actors_.empty() || !in_actor_) return;
    Ctl c{};
    for (int i = 0; i < 8; ++i) c[std::size_t(i)] = s.gte.control[std::size_t(i)];
    if (c == last_part_ctl_) return;
    last_part_ctl_ = c;
    auto& actor = pending_actors_.back();
    actor.parts.push_back(RawPart{s.gpr[30] - actor.a0, c, lightFromGte(s.gte.control)});
  }

  void onObject(const sf::psx::R3000State& s) {
    if (!mission_) return;
    beginPhase(2);
    in_actor_ = false;
    Ctl c{};
    for (int i = 0; i < 8; ++i) c[std::size_t(i)] = s.gte.control[std::size_t(i)];
    if (static const char* orr = std::getenv("SF3N_OBJRA"); orr && mission_) {
      if (const auto mi = identifyModel(s.gpr[4]); mi && mission_->models[*mi].name.rfind(orr, 0) == 0) {
        static int nlog = 0;
        if (nlog++ < 40) std::fprintf(log, "OBJRA %s a0 %08x a1 %08x ra %08x s0 %08x s1 %08x s2 %08x s3 %08x s4 %08x s5 %08x s6 %08x s7 %08x fp %08x sp %08x\n", mission_->models[*mi].name.c_str(), s.gpr[4], s.gpr[5], s.gpr[31], s.gpr[16], s.gpr[17], s.gpr[18], s.gpr[19], s.gpr[20], s.gpr[21], s.gpr[22], s.gpr[23], s.gpr[30], s.gpr[29]);
      }
    }
    if (static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr; bl && (s.gpr[4] & 0x1fffffffu) - 0x00600000u < 0x00100000u) {
      static int nb = 0;
      if (field_count_ >= 2600 && nb++ < 300) { const auto mi = identifyModel(s.gpr[4]); std::fprintf(log, "BANCO obj campo %llu a0 %08x %s building %d objetos %zu\n", (unsigned long long)field_count_, s.gpr[4], mi ? mission_->models[*mi].name.c_str() : "?", int(building_), pending_objects_.size()); }
    }
    pending_objects_.push_back(RawObject{s.gpr[4], c, lightFromGte(s.gte.control), s.gpr[5]});
  }
  void setRam(std::span<const std::byte> ram) { ram_ = ram; }
  const std::vector<std::uint16_t>* soft_vram_ = nullptr;
  std::atomic<std::uint32_t> cam_struct_{0};
  void setCamStruct(std::uint32_t c) { cam_struct_ = c; }
  std::uint32_t player_ptr_addr_ = 0;

  std::vector<std::pair<std::uint32_t, std::uint32_t>> dbg_proj_, dbg_proj_frame_;
  std::map<std::uint32_t, long> dbg_luz_pc_; long dbg_luz_n_ = 0, dbg_luz_none_ = 0;
  void dbgProj(std::uint32_t pc, std::uint32_t sxy) { if (dbg_proj_.size() < 200000) dbg_proj_.emplace_back(pc, sxy); }
  std::uint32_t fx_ra_ = 0;

  WeaponBank weapon_bank_;
  std::map<std::uint32_t, std::size_t> bank_model_index_;
  WeaponSoundBank weapon_sounds_;

  bool missionHasOwnModel(const std::string& name) const {
    if (!mission_) return false;
    for (const auto& m : mission_->models) if (m.bank < 0 && m.name == name) return true;
    return false;
  }
  void initWeaponBank() {
    if (!image || std::getenv("SF3N_SIN_BANCO_ARMAS")) return;
    const auto t0 = std::chrono::steady_clock::now();
    loadWeaponConfig(pakfs::dataPath("armas"), log);
    weapon_bank_ = buildWeaponBank(*image, log, pakfs::dataPath("armas"));
    if (!std::getenv("SF3N_SIN_SONIDOS_ARMAS")) weapon_sounds_ = buildWeaponSounds(*image, log, std::getenv("SF3N_SIN_WAV_ARMAS") ? std::string() : pakfs::dataPath("armas"));
    writeWeaponCatalog();
    if (log) std::fprintf(log, "armas: banco %s en %.0f ms\n", weapon_bank_.ok ? "listo" : "NO valido", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }

  void writeWeaponCatalog() const {
    std::error_code ec;
    const std::string dir = pakfs::dataPath("armas");
    if (!pakfs::looseDir(dir)) return;
    FILE* f = std::fopen((dir + "/informe_armas.txt").c_str(), "w");
    if (!f) return;
    std::fprintf(f, "BANCO DE ARMAS DE SF3 NATIVO (se rehace desde el disco al arrancar)\n");
    std::fprintf(f, "Las 33 armas de la tabla del ejecutable. id = numero del juego (0x8012D880).\n");
    std::fprintf(f, "Modelos: copia exacta del disco, en la RAM de armas del port (0x80600000).\n");
    std::fprintf(f, "Texturas: hoja de armas comun (paginas 12/13/28/31), el juego la sube desde el\n");
    std::fprintf(f, "ejecutable en todos los niveles. Postura, municion y danos: tabla del ejecutable.\n\n");
    std::fprintf(f, "id  arma       mano (origen, bytes, triangulos, RAM)                    suelo\n");
    const auto& names = weaponTableNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
      auto desc = [&](const std::string& gmd) {
        const auto it = weapon_bank_.by_name.find(gmd);
        if (it == weapon_bank_.by_name.end()) return gmd + " NO ESTA EN EL DISCO";
        const auto& m = weapon_bank_.models[it->second];
        char b[160]; std::snprintf(b, sizeof b, "%s (%s, %zu, %zu, %08x)", gmd.c_str(), m.source.c_str(), m.bytes.size(), m.tris.size(), m.addr);
        return std::string(b);
      };
      std::fprintf(f, "%2d  %-9s  %-56s %s\n", weaponGameId(names[i]), names[i].c_str(), desc(names[i] + ".GMD").c_str(), desc("X" + names[i].substr(1) + ".GMD").c_str());
    }
    std::fclose(f);
  }

  std::vector<std::string> weaponBankOrder() const {
    std::vector<std::pair<std::uint32_t, std::string>> v;
    for (const auto& m : weapon_bank_.models) if (m.addr) v.push_back({m.addr, m.gmd});
    std::sort(v.begin(), v.end());
    std::vector<std::string> o; for (auto& [a, n] : v) o.push_back(n); return o;
  }

  std::vector<std::string> weaponBankOrderV192() const {
    auto o = weaponBankOrder();
    static const char* nuevas[] = {"CHNGUN.GMD", "TASERH.GMD", "STRYKER.GMD"};
    std::vector<std::string> tail;
    for (const char* n : nuevas) if (auto it = std::find(o.begin(), o.end(), n); it != o.end()) { tail.push_back(*it); o.erase(it); }
    if (auto xl = std::find(o.begin(), o.end(), "XLAMETRK.GMD"); xl != o.end()) {
      o.erase(xl);
      auto xu = std::find(o.begin(), o.end(), "XUPERG.GMD");
      o.insert(xu == o.end() ? o.end() : xu + 1, "XLAMETRK.GMD");
    }
    for (auto& n : tail) o.push_back(n);
    return o;
  }
  std::map<std::string, std::uint32_t> weaponBankLayout(const std::vector<std::string>& order) const {
    std::map<std::string, std::uint32_t> d; std::uint32_t off = 0;
    std::vector<std::string> all = order;
    for (const auto& n : weaponBankOrder()) if (std::find(all.begin(), all.end(), n) == all.end()) all.push_back(n);
    for (const auto& n : all) {
      auto it = weapon_bank_.by_name.find(n); if (it == weapon_bank_.by_name.end()) continue;
      const auto sz = weapon_bank_.models[it->second].bytes.size();
      if (off + sz > 0x70000u) continue;
      d[n] = WeaponBank::kBase + off; off = (off + std::uint32_t(sz) + 3u) & ~3u;
    }
    return d;
  }

  bool relayoutWeaponBank(const std::vector<std::string>& order) {
    const auto d = weaponBankLayout(order);
    bool changed = false;
    for (auto& m : weapon_bank_.models) if (auto it = d.find(m.gmd); it != d.end() && it->second != m.addr) { m.addr = it->second; changed = true; }
    if (!changed) return false;
    weapon_bank_.by_addr.clear();
    for (std::size_t i = 0; i < weapon_bank_.models.size(); ++i) if (weapon_bank_.models[i].addr) weapon_bank_.by_addr[weapon_bank_.models[i].addr] = i;
    bank_model_index_.clear();
    if (mission_) for (std::size_t i = 0; i < mission_->models.size(); ++i)
      if (const int bk = mission_->models[i].bank; bk >= 0 && std::size_t(bk) < weapon_bank_.models.size()) bank_model_index_[weapon_bank_.models[std::size_t(bk)].addr] = i;
    model_cache_.clear();
    return true;
  }
  void addBankModels() {
    bank_model_index_.clear();
    if (!weapon_bank_.ok || !mission_) return;
    for (std::size_t i = 0; i < weapon_bank_.models.size(); ++i) {
      const auto& bm = weapon_bank_.models[i];
      try {
        Model md; md.name = bm.gmd; md.geometry = sf::assets::GmdModel::parse(bm.bytes); md.bank = int(i);
        bank_model_index_[bm.addr] = mission_->models.size();
        mission_->models.push_back(std::move(md));
      } catch (const std::exception&) {}
    }
  }
  std::uint32_t world_h_ = 0;

  void onEffectProject(const std::array<std::uint32_t, 32>& ctl, const std::array<std::uint32_t, 32>& in,
                       const std::array<std::uint32_t, 32>& out, bool rtpt, bool actor = false, std::uint32_t fp = 0) {
    if (!mission_) return;
    if (actor && building_ && in_actor_ && !pending_actors_.empty()) {
      auto& ra = pending_actors_.back();
      const std::uint32_t off = fp - ra.a0;
      if (off < 0x20000 && ra.vsxy.size() < 4000)
        for (int k = 0; k < 3; ++k) ra.vsxy.push_back({off + 8u * std::uint32_t(k), out[std::size_t(12 + k)] & 0x07ff07ffu});
    }
    if (eff_raw_.size() > 12000) return;
    RawEffectPoint p;
    p.actor = actor;

    { static const std::uint32_t gra = std::getenv("SF3N_LUZ_RA") ? std::uint32_t(std::stoul(std::getenv("SF3N_LUZ_RA"), nullptr, 16)) : 0x800c1b9cu; p.light = fx_ra_ == gra; p.ra = fx_ra_; p.h = ctl[26] & 0xffffu; }
    for (int i = 0; i < 8; ++i) p.ctl[std::size_t(i)] = ctl[std::size_t(i)];
    p.ofx = double(std::int32_t(ctl[24])) / 65536.0;
    p.ofy = double(std::int32_t(ctl[25])) / 65536.0;
    const int first = rtpt ? 0 : 2;
    for (int k = first; k < 3; ++k) {
      const int vi = rtpt ? k : 0;
      p.v = {std::int16_t(in[std::size_t(vi * 2)] & 0xffff), std::int16_t(in[std::size_t(vi * 2)] >> 16), std::int16_t(in[std::size_t(vi * 2 + 1)] & 0xffff)};
      p.sxy = out[std::size_t(12 + k)];
      p.sz = std::uint16_t(out[std::size_t(17 + k)]);
      eff_raw_.push_back(p);
    }
  }

  std::uint64_t light_vote_field_ = ~0ull;
  std::vector<std::uint32_t> light_voted_px_;
  struct LightPick { std::array<double, 3> w; double z, px, py; };
  template <class EM> std::optional<LightPick> lightFor(const EM& mp, double cx, double cy, double tol2) {
    if (!mp.cam.valid || !mp.of_ok) return std::nullopt;
    const auto& cam = mp.cam;
    auto camz = [&](const std::array<double, 3>& w, double& px, double& py) {
      double c[3];
      for (int i = 0; i < 3; ++i) c[i] = cam.rot[std::size_t(i * 3)] * (w[0] - cam.eye[0]) + cam.rot[std::size_t(i * 3 + 1)] * (w[1] - cam.eye[1]) + cam.rot[std::size_t(i * 3 + 2)] * (w[2] - cam.eye[2]);
      if (c[2] <= 1.0) return -1.0;
      px = mp.ofx + cam.fx * c[0] / c[2]; py = mp.ofy + cam.fy * c[1] / c[2];
      return c[2];
    };
    static const double R = envInt("SF3N_LUZ_RADIO", 24);

    const int ix = int(std::lround(cx)), iy = int(std::lround(cy));
    const std::uint32_t key = (std::uint32_t(ix) & 0x7ffu) | ((std::uint32_t(iy) & 0x7ffu) << 16);
    if (light_vote_field_ != field_count_) { light_vote_field_ = field_count_; light_voted_px_.clear(); }
    if (std::find(light_voted_px_.begin(), light_voted_px_.end(), key) == light_voted_px_.end()) {
      light_voted_px_.push_back(key);
      std::vector<std::size_t> hit;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const std::uint32_t k = (std::uint32_t(ix + dx) & 0x7ffu) | ((std::uint32_t(iy + dy) & 0x7ffu) << 16);
          auto [b, e] = mp.all.equal_range(k);
          for (auto it = b; it != e; ++it) {
            const auto& w = it->second;
            std::size_t best = ~std::size_t(0); double bd = R * R;
            for (std::size_t r = 0; r < light_reg_.size(); ++r) {
              const auto& p = light_reg_[r].p;
              const double d = (p[0] - w[0]) * (p[0] - w[0]) + (p[1] - w[1]) * (p[1] - w[1]) + (p[2] - w[2]) * (p[2] - w[2]);
              if (d < bd) { bd = d; best = r; }
            }
            if (best == ~std::size_t(0)) { light_reg_.push_back({w, 0.0, field_count_}); best = light_reg_.size() - 1; }
            if (std::find(hit.begin(), hit.end(), best) != hit.end()) continue;
            hit.push_back(best);
            auto& L = light_reg_[best];
            L.votes += 1.0; L.last = field_count_;
            const double a = 1.0 / std::min(L.votes, 50.0);
            for (int j = 0; j < 3; ++j) L.p[std::size_t(j)] += (w[std::size_t(j)] - L.p[std::size_t(j)]) * a;
          }
        }

      if (light_reg_.size() > 4000)
        light_reg_.erase(std::remove_if(light_reg_.begin(), light_reg_.end(), [&](const LightReg& L) { return L.votes < 4 && L.last + 300 < field_count_; }), light_reg_.end());
    }

    const LightReg* pick = nullptr; LightPick r{};
    static const double tol = 2.5;
    for (const auto& L : light_reg_) {
      double px, py; const double z = camz(L.p, px, py);
      if (z <= 0 || std::fabs(px - cx) > tol || std::fabs(py - cy) > tol) continue;
      if (!pick || L.votes > pick->votes) { pick = &L; r = {L.p, z, px, py}; }
    }
    if (!pick && tol2 > tol) {
      double bd = tol2 * tol2;
      for (const auto& L : light_reg_) {
        if (L.votes < 3) continue;
        double px, py; const double z = camz(L.p, px, py);
        if (z <= 0) continue;
        const double d = (px - cx) * (px - cx) + (py - cy) * (py - cy);
        if (d < bd) { bd = d; pick = &L; r = {L.p, z, px, py}; }
      }
    }
    if (!pick) return std::nullopt;
    return r;
  }

  std::vector<std::uint8_t> classifyActorPolys(const std::vector<std::vector<std::uint32_t>>& seg) {
    std::vector<std::uint8_t> out(seg.size(), 0);
    for (std::size_t i = 0; i < seg.size(); ++i) {
      const auto& pk = seg[i];
      if (pk.empty()) continue;
      const auto c = pk[0] >> 24;
      if (c < 0x20 || c >= 0x40) continue;
      const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
      const int n = quad ? 4 : 3;
      std::size_t idx = 1; bool all = true;
      for (int k = 0; k < n && all; ++k) {
        if (k > 0 && gour) ++idx;
        if (idx >= pk.size()) { all = false; break; }
        const std::uint32_t xy = pk[idx++] & 0x07ff07ffu;
        if (tex) ++idx;
        bool a = false;
        for (const auto& em : eff_maps_) if (em.actor_pts.count(xy)) { a = true; break; }
        all = a;
      }
      out[i] = all ? 1 : 0;
    }
    return out;
  }
  void onWorldList(const std::vector<std::vector<std::uint32_t>>& seg) {
    if (static const char* wd2 = std::getenv("SF3N_WLDUMP2"); wd2) {
      static long a = -1, b = -1; if (a < 0) std::sscanf(wd2, "%ld:%ld", &a, &b);
      const long gf = long(game_field_.load());
      if (gf >= a && gf <= b) { char nm[64]; std::snprintf(nm, sizeof nm, "wl2_%ld_%zu.txt", gf, seg.size()); if (FILE* f = std::fopen(nm, "w")) { for (const auto& pk : seg) { for (auto w : pk) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); } std::fclose(f); } }
    }
    { static std::vector<std::pair<std::uint32_t, std::uint32_t>> prev; auto cur = std::move(dbg_proj_); dbg_proj_.clear(); dbg_proj_frame_ = prev; dbg_proj_frame_.insert(dbg_proj_frame_.end(), cur.begin(), cur.end()); prev = std::move(cur); }
    if (eff_maps_.empty()) eff_maps_.emplace_back();
    std::vector<EffectPrim> out;
    std::uint16_t drawTpage = 0;
    auto find = [&](std::uint32_t xy, std::size_t mi, bool fuzzy = false) -> const std::array<double, 3>* {
      const auto& m = eff_maps_[mi].pts;
      if (auto it = m.find(xy & 0x07ff07ffu); it != m.end()) return &it->second.first;
      if (!fuzzy) return nullptr;

      if (static const bool ex = std::getenv("SF3N_FX_EXACTO") != nullptr; ex) return nullptr;
      const int x = int(std::int16_t(std::uint16_t(xy << 5)) >> 5), y = int(std::int16_t(std::uint16_t((xy >> 16) << 5)) >> 5);
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          if (!dx && !dy) continue;
          const std::uint32_t k = (std::uint32_t(x + dx) & 0x7ffu) | ((std::uint32_t(y + dy) & 0x7ffu) << 16);
          if (auto it = m.find(k); it != m.end()) return &it->second.first;
        }
      return nullptr;
    };
    static const bool fxlog = std::getenv("SF3N_FXLOG") != nullptr;
    std::map<int, int> partial, allcmd;

    auto triTex = std::make_shared<TriTexMap>();

    std::vector<VCopy> copies;
    for (const auto& pk : seg) {
      if (!pk.empty() && (pk[0] >> 24) == 0x80 && pk.size() >= 4) {
        VCopy cp{int(pk[1] & 0x3ff), int((pk[1] >> 16) & 0x1ff), int(pk[2] & 0x3ff), int((pk[2] >> 16) & 0x1ff), int(pk[3] & 0xffff), int(pk[3] >> 16)};
        if (cp.w >= 4 && cp.h >= 4) {

          std::erase_if(copies, [&](const VCopy& o) { return o.dx == cp.dx && o.dy == cp.dy; });
          copies.push_back(cp);
        }
        continue;
      }
      if (pk.size() < 9) continue;
      const std::uint32_t c = pk[0] >> 24;
      const bool gt3 = (c & 0xfc) == 0x34, gt4 = (c & 0xfc) == 0x3c;
      if (!gt3 && !(gt4 && pk.size() >= 12)) continue;
      const int nv = gt3 ? 3 : 4;
      std::array<std::uint32_t, 4> v{};
      std::array<std::uint16_t, 4> t{};
      std::array<std::uint32_t, 4> cc{};
      for (int k = 0; k < nv; ++k) { v[std::size_t(k)] = pk[std::size_t(1 + 3 * k)] & 0x07ff07ffu; t[std::size_t(k)] = std::uint16_t(pk[std::size_t(2 + 3 * k)] & 0xffff); cc[std::size_t(k)] = pk[std::size_t(3 * k)] & 0xffffff; }
      auto add = [&](int i0, int i1, int i2) {
        TriTex tt;
        tt.clut = std::uint16_t(pk[2] >> 16);
        tt.tpage = std::uint16_t((pk[5] >> 16) & 0x1ff);
        tt.semi = (c & 2) != 0;
        tt.uv[0] = {v[std::size_t(i0)], t[std::size_t(i0)]};
        tt.uv[1] = {v[std::size_t(i1)], t[std::size_t(i1)]};
        tt.uv[2] = {v[std::size_t(i2)], t[std::size_t(i2)]};
        tt.col = {cc[std::size_t(i0)], cc[std::size_t(i1)], cc[std::size_t(i2)]};
        tt.orig_tpage = tt.tpage;
        for (auto ci = copies.rbegin(); ci != copies.rend(); ++ci) {
          std::array<std::uint16_t, 3> uv3{tt.uv[0].second, tt.uv[1].second, tt.uv[2].second};
          std::uint16_t tp = tt.tpage;
          if (moveTex(tp, uv3, *ci)) {
            tt.moved = true; tt.copy = *ci; tt.tpage = tp;
            for (int k = 0; k < 3; ++k) tt.uv[std::size_t(k)].second = uv3[std::size_t(k)];
            break;
          }
        }
        (*triTex)[triKey(v[std::size_t(i0)], v[std::size_t(i1)], v[std::size_t(i2)])] = tt;
      };
      add(0, 1, 2);
      if (gt4) { add(1, 2, 3); add(0, 1, 3); add(0, 2, 3); }
    }
    static int diagSkip = std::getenv("SF3N_WOUNDDIAG") ? std::atoi(std::getenv("SF3N_WOUNDDIAG")) : 0;
    if (wound_diag_ && !eff_maps_.empty() && diag_logs_ < 1500 && diagSkip-- <= 0) {

      std::uint16_t tp = 0;
      int normal = 0, extra = 0;
      for (const auto& pk : seg) {
        if (pk.empty()) continue;
        const std::uint32_t c = pk[0] >> 24;
        if (c == 0xe1) { tp = std::uint16_t(pk[0] & 0x1ff); continue; }
        if (c < 0x20 || c >= 0x40) continue;
        const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
        const int n = quad ? 4 : 3;
        std::size_t i = 1; std::array<std::uint32_t, 4> xy{}, uv{}, col{};
        col[0] = pk[0] & 0xffffff;
        bool ok = true;
        for (int k = 0; k < n; ++k) {
          if (k > 0 && gour) { if (i >= pk.size()) { ok = false; break; } col[std::size_t(k)] = pk[i++] & 0xffffff; }
          if (i >= pk.size()) { ok = false; break; }
          xy[std::size_t(k)] = pk[i++] & 0x07ff07ffu;
          if (tex) { if (i >= pk.size()) { ok = false; break; } uv[std::size_t(k)] = pk[i++]; }
        }
        if (!ok) continue;
        bool onscreen = true;
        for (int k = 0; k < n; ++k) { const int x = std::int16_t(xy[std::size_t(k)] & 0xffff), y = std::int16_t(xy[std::size_t(k)] >> 16); if (x < 8 || x > 400 || y < 8 || y > 250) onscreen = false; }
        if (!onscreen) continue;
        int act = 0, eff = 0;
        for (int k = 0; k < n; ++k) {
          bool a = false, e = false;
          for (const auto& em : eff_maps_) { a = a || em.actor_pts.count(xy[std::size_t(k)]); e = e || em.pts.count(xy[std::size_t(k)]); }
          act += a; eff += e;
        }
        if (act == 0) continue;
        if (n == 3 && act == 3 && diag_tris_.count(triKey(xy[0], xy[1], xy[2]))) { ++normal; continue; }
        ++extra;
        if (diag_logs_ < 1500) {
          ++diag_logs_;
          std::fprintf(log, "extra cmd %02x act %d eff %d/%d col %06x", c, act, eff, n, col[0]);
          for (int k = 0; k < n; ++k) std::fprintf(log, " (%d,%d uv%04x)", int(std::int16_t(xy[std::size_t(k)] & 0xffff)), int(std::int16_t(xy[std::size_t(k)] >> 16)), unsigned(uv[std::size_t(k)] & 0xffff));
          if (tex) std::fprintf(log, " clut %04x tp %04x", unsigned(uv[0] >> 16), unsigned(uv[1] >> 16)); else std::fprintf(log, " e1 %04x", tp);
          std::fprintf(log, "\n");
        }
      }
      std::fprintf(log, "diag: normales %d extra %d\n", normal, extra);
      diag_tris_.clear();
    }
    if (static const bool cl = std::getenv("SF3N_COLLOG") != nullptr; cl) {
      double sum = 0; int n = 0; int semi = 0; std::map<int,int> h;
      for (const auto& pk : seg) {
        if (pk.empty()) continue;
        const std::uint32_t c = pk[0] >> 24; h[int(c)]++;
        if (c >= 0x20 && c < 0x40) { sum += double(pk[0] & 0xff) + double((pk[0] >> 8) & 0xff) + double((pk[0] >> 16) & 0xff); ++n; if (c & 2) ++semi; }
      }
      std::fprintf(log, "COL campo %llu media %.1f n %d semi %d |", (unsigned long long)field_count_, n ? sum / n / 3 : 0.0, n, semi);
      for (auto [c, k] : h) if (!(c >= 0x20 && c < 0x40)) std::fprintf(log, " %02x:%d", c, k);
      std::fprintf(log, "\n");
    }
    if (static const long long wd = std::getenv("SF3N_WLDUMP") ? std::atoll(std::getenv("SF3N_WLDUMP")) : -1; wd >= 0 && (long long)field_count_ >= wd && (long long)field_count_ < wd + 40) {
      char nm[64]; std::snprintf(nm, sizeof nm, "wl_%llu.txt", (unsigned long long)field_count_);
      if (FILE* f = std::fopen(nm, "w")) {
        for (const auto& pk : seg) { for (auto w : pk) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); }
        std::fclose(f);
      }
    }
    std::vector<std::vector<std::uint32_t>> overlay;
    std::uint32_t overlayTpage = 0xffffffffu;
    int areaX = 0, areaY = 0, offX = 0, offY = 0;

    auto shownOnPs1 = [&](const std::array<std::uint32_t, 4>& xyv, const std::array<std::uint32_t, 4>& colv, int nv) {
      if (!soft_vram_ || soft_vram_->size() < 1024 * 512 || std::getenv("SF3N_MARCAS_SIEMPRE")) return true;
      double cx = 0, cy = 0; int r = 0, g = 0, b = 0;
      for (int k = 0; k < nv; ++k) {
        cx += double(std::int16_t(std::uint16_t(xyv[std::size_t(k)] << 5)) >> 5) / nv;
        cy += double(std::int16_t(std::uint16_t((xyv[std::size_t(k)] >> 16) << 5)) >> 5) / nv;
        r += int(colv[std::size_t(k)] & 0xff); g += int((colv[std::size_t(k)] >> 8) & 0xff); b += int((colv[std::size_t(k)] >> 16) & 0xff);
      }
      r /= nv; g /= nv; b /= nv;
      int hits = 0, tests = 0;
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const int px = offX + int(std::lround(cx)) + dx, py = offY + int(std::lround(cy)) + dy;
        if (px < 0 || px >= 1024 || py < 0 || py >= 512) continue;
        const auto v = (*soft_vram_)[std::size_t(py) * 1024 + std::size_t(px)];
        ++tests;
        const int pr = (v & 31) << 3, pg = ((v >> 5) & 31) << 3, pb = ((v >> 10) & 31) << 3;
        if (std::abs(pr - std::min(r, 255)) <= 40 && std::abs(pg - std::min(g, 255)) <= 40 && std::abs(pb - std::min(b, 255)) <= 40) ++hits;
      }
      return tests == 0 || hits > 0;
    };

    struct GlareCand { std::size_t idx; double cx, cy; std::array<std::uint32_t, 4> corners; };
    std::vector<GlareCand> glares;
    int snow_idx = 0;
    for (std::size_t segIdx = 0; segIdx < seg.size(); ++segIdx) {
      const auto& pk = seg[segIdx];
      if (pk.empty()) continue;
      const std::uint32_t cmd = pk[0] >> 24;
      if (fxlog) allcmd[int(cmd)]++;
      if (cmd == 0xe1) { drawTpage = std::uint16_t(pk[0] & 0x1ff); continue; }
      if (cmd == 0xe3) { areaX = int(pk[0] & 0x3ff); areaY = int((pk[0] >> 10) & 0x1ff); }
      if (cmd == 0xe5) { offX = int(std::int32_t(pk[0] << 21) >> 21); offY = int(std::int32_t((pk[0] >> 11) << 21) >> 21); }
      if (cmd == 0xe3 || cmd == 0xe5) { overlay.push_back(pk); continue; }
      if (cmd >= 0x20 && cmd < 0x40) {
        const bool quad = cmd & 8, tex = cmd & 4, gour = cmd & 0x10;
        const int n = quad ? 4 : 3;
        std::array<std::uint32_t, 4> col{}, xy{}, uvw{};
        std::size_t i = 0;
        col[0] = pk[i++] & 0xffffff;
        bool okLen = true;
        for (int k = 0; k < n; ++k) {
          if (k > 0) col[std::size_t(k)] = gour ? (i < pk.size() ? pk[i++] & 0xffffff : 0) : col[0];
          if (i >= pk.size()) { okLen = false; break; }
          xy[std::size_t(k)] = pk[i++];
          if (tex) uvw[std::size_t(k)] = i < pk.size() ? pk[i++] : 0;
        }
        if (!okLen) continue;

        if (!tex && (cmd & 2)) {
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (int k = 0; k < n; ++k) {
            const double x = double(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5), y = double(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5);
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
          }

          if (x1 - x0 >= 320 && y1 - y0 >= 220 && n == 4) {
            if (overlayTpage != drawTpage) { overlay.push_back({0xe1000000u | drawTpage}); overlayTpage = drawTpage; }
            overlay.push_back(pk);
            if (static const bool fl = std::getenv("SF3N_FLASHLOG") != nullptr; fl) std::fprintf(log, "destello cmd %02x col %06x %gx%g tp %03x campo %llu\n", cmd, col[0], x1 - x0, y1 - y0, drawTpage, (unsigned long long)field_count_);
            continue;
          }
        }
        if (fxlog) {
          int hits = 0;
          for (int k = 0; k < n; ++k) hits += find(xy[std::size_t(k)], eff_maps_.size() - 1) != nullptr;
          if (hits > 0 && hits < n) partial[int(cmd)]++;
        }

        if (!tex && night_vision_.load() && (cmd & 0xfc) == 0x20) {
          const std::uint32_t c0 = col[0] & 0xffffffu;
          static const bool keepNv = std::getenv("SF3N_NOCTURNA_JUEGO") != nullptr;
          if (!keepNv && (c0 == 0x621a05u || c0 == 0x956807u || c0 == 0xaba500u || c0 == 0x00ff48u)) continue;
        }
        bool polyMatched = false;
        bool flat2d = false;
        if (!tex && !(cmd & 2)) {

          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (int k = 0; k < n; ++k) {
            const double x = double(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5), y = double(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5);
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
          }
          flat2d = x1 - x0 <= 48 && y1 - y0 <= 48 && std::fabs(x0) < 400 && std::fabs(y0) < 300;
        }

        const bool fuzzy = (cmd & 2) && ((((tex ? (uvw[1] >> 16) : drawTpage) >> 5) & 3) == 2);

        bool actorPoly = false;
        if (static const bool keep = std::getenv("SF3N_FX_ACTOR") != nullptr; !keep) {
          actorPoly = true;
          for (int k = 0; k < n && actorPoly; ++k) {
            bool a = false;
            for (const auto& em : eff_maps_) if (em.actor_pts.count(xy[std::size_t(k)] & 0x07ff07ffu)) { a = true; break; }
            actorPoly = a;
          }
          if (actorPoly) ++fx_actor_skip_;
        }
        if (!flat2d && !actorPoly)
        for (std::size_t mi = eff_maps_.size(); mi-- > 0;) {
          std::array<const std::array<double, 3>*, 4> w{};
          bool all = true;
          int effHits = 0;
          for (int k = 0; k < n && all; ++k) {
            w[std::size_t(k)] = find(xy[std::size_t(k)], mi, fuzzy);
            if (w[std::size_t(k)]) { ++effHits; continue; }

            all = false;
          }
          if (!all || effHits == 0) continue;
          if (effHits < n) {

            double maxEdge = 0;
            for (int a2 = 0; a2 < n; ++a2)
              for (int b2 = a2 + 1; b2 < n; ++b2) {
                const auto& pa = *w[std::size_t(a2)];
                const auto& pb = *w[std::size_t(b2)];
                maxEdge = std::max(maxEdge, std::sqrt((pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2])));
              }
            if (maxEdge > 90.0) continue;
          }
          if (fxlog && wlog_ < 60) {
            ++wlog_;
            std::fprintf(log, "herida cmd %02x efect %d/%d col %06x %06x %06x clut %04x tp %04x uv %04x %04x %04x\n", cmd, effHits, n, col[0], col[1], col[2],
                         tex ? unsigned(uvw[0] >> 16) : 0u, tex ? unsigned(uvw[1] >> 16) : 0u, unsigned(uvw[0] & 0xffff), unsigned(uvw[1] & 0xffff), unsigned(uvw[2] & 0xffff));
          }
          EffectPrim e;
          e.kind = quad ? 1 : 0; e.cmd = std::uint8_t(cmd); e.n = std::uint8_t(n);
          for (int k = 0; k < n; ++k) {
            e.w[std::size_t(k)] = *w[std::size_t(k)];
            e.col[std::size_t(k)] = col[std::size_t(k)];
            e.uv[std::size_t(k)] = std::uint16_t(uvw[std::size_t(k)] & 0xffff);
          }
          e.clut = tex ? std::uint16_t(uvw[0] >> 16) : 0;
          e.tpage = tex ? std::uint16_t((uvw[1] >> 16) & 0x1ffu) : drawTpage;
          if (static const char* fs = std::getenv("SF3N_FXSTAT"); fs && (cmd & 2) && ((e.tpage >> 5) & 3) == 2) {
            static long a = -1, b = -1; if (a < 0) std::sscanf(fs, "%ld:%ld", &a, &b);
            if (long(field_count_) >= a && long(field_count_) <= b) {
              std::fprintf(log, "FXPK campo %llu cmd %02x n %d col %06x uv", (unsigned long long)field_count_, cmd, n, col[0]);
              for (int k = 0; k < n; ++k) std::fprintf(log, " %04x", unsigned(uvw[std::size_t(k)] & 0xffff));
              std::fprintf(log, " xy");
              for (int k = 0; k < n; ++k) std::fprintf(log, " (%d,%d)", int(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5), int(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5));
              std::fprintf(log, " map %zu/%zu w", mi, eff_maps_.size());
              for (int k = 0; k < n; ++k) std::fprintf(log, " (%.0f,%.0f,%.0f)", (*w[std::size_t(k)])[0], (*w[std::size_t(k)])[1], (*w[std::size_t(k)])[2]);
              std::fprintf(log, "\n");
            }
          }

          if (static const bool keepRoomFx = std::getenv("SF3N_FX_SALA") != nullptr; !keepRoomFx && tex && isRoomTexture(e.tpage, e.clut)) {
            bool allRoom = true;
            for (int k = 0; k < n && allRoom; ++k) allRoom = isRoomVertex(e.w[std::size_t(k)]);
            if (static const bool rl = std::getenv("SF3N_ROOMFXLOG") != nullptr; rl) {
              std::fprintf(log, "ROOMFX campo %llu cmd %02x tp %04x clut %04x sala %d:", (unsigned long long)field_count_, cmd, e.tpage, e.clut, int(allRoom));
              for (int k = 0; k < n; ++k) std::fprintf(log, " (%.1f,%.1f,%.1f) d%.1f", e.w[std::size_t(k)][0], e.w[std::size_t(k)][1], e.w[std::size_t(k)][2], nearestRoomVertex(e.w[std::size_t(k)]));
              std::fprintf(log, "\n");
            }

            double extent = 0;
            for (int k = 1; k < n; ++k) for (int j = 0; j < 3; ++j) extent = std::max(extent, std::fabs(e.w[std::size_t(k)][std::size_t(j)] - e.w[0][std::size_t(j)]));
            if (allRoom || extent > 40.0) { polyMatched = true; break; }
          }
          out.push_back(e);
          polyMatched = true;
          break;
        }

        if (static const bool sinPart = std::getenv("SF3N_SIN_PARTICULAS") != nullptr; !polyMatched && !tex && !sinPart) {
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          std::array<double, 4> px{}, py{};
          for (int k = 0; k < n; ++k) { px[std::size_t(k)] = double(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5); py[std::size_t(k)] = double(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5); x0 = std::min(x0, px[std::size_t(k)]); x1 = std::max(x1, px[std::size_t(k)]); y0 = std::min(y0, py[std::size_t(k)]); y1 = std::max(y1, py[std::size_t(k)]); }
          const int r = int(col[0] & 0xff), g = int((col[0] >> 8) & 0xff), b = int((col[0] >> 16) & 0xff);
          const bool snowLike = (cmd & 2) && !quad && !gour && std::abs(r - g) <= 12 && std::abs(g - b) <= 12 && r >= 16 && x1 - x0 <= 8 && y1 - y0 <= 8 && snow_mission_;
          static const double maxPx = envInt("SF3N_PARTICULA_MAX", 24);
          if (!snowLike && x1 - x0 <= maxPx && y1 - y0 <= maxPx && !eff_maps_.empty()) {
            const auto& mp = eff_maps_.back();
            int ak = -1; const std::pair<std::array<double, 3>, double>* ap = nullptr;
            for (int k = 0; k < n && !ap; ++k) if (auto it = mp.pts.find(xy[std::size_t(k)] & 0x07ff07ffu); it != mp.pts.end()) { ak = k; ap = &it->second; }
            double ax = 0, ay = 0;
            if (ap) { ax = px[std::size_t(ak)]; ay = py[std::size_t(ak)]; }
            else {

              double cx = 0, cy = 0; for (int k = 0; k < n; ++k) { cx += px[std::size_t(k)]; cy += py[std::size_t(k)]; } cx /= n; cy /= n;
              double bd = 6.0 * 6.0;
              for (int dy = -6; dy <= 6; ++dy) for (int dx = -6; dx <= 6; ++dx) {
                const int qx = int(std::lround(cx)) + dx, qy = int(std::lround(cy)) + dy;
                const double d = double(dx * dx + dy * dy); if (d > bd) continue;
                if (auto it = mp.pts.find((std::uint32_t(qx) & 0x7ffu) | ((std::uint32_t(qy) & 0x7ffu) << 16)); it != mp.pts.end()) { bd = d; ap = &it->second; ax = qx; ay = qy; }
              }
            }
            if (static const bool pl = std::getenv("SF3N_PARTLOG") != nullptr; pl && !ap) std::fprintf(log, "PARTICULA SIN ANCLA campo %llu cmd %02x col %06x\n", (unsigned long long)field_count_, cmd, col[0]);
            if (ap && mp.cam.valid) {
              const auto& cam = mp.cam; double z = 0;
              for (int i = 0; i < 3; ++i) z += cam.rot[std::size_t(6 + i)] * (ap->first[std::size_t(i)] - cam.eye[std::size_t(i)]);
              if (z > 1.0) {
                EffectPrim e;
                e.kind = 5; e.cmd = std::uint8_t(cmd); e.n = std::uint8_t(n);
                e.w[0] = ap->first;
                for (int k = 0; k < n; ++k) { e.off[std::size_t(k)] = {px[std::size_t(k)] - ax, py[std::size_t(k)] - ay}; e.col[std::size_t(k)] = col[std::size_t(k)]; }
                e.size_w = z / cam.fx; e.size_h = z / cam.fy;
                e.tpage = drawTpage;
                out.push_back(e);
                if (static const bool pl = std::getenv("SF3N_PARTLOG") != nullptr; pl) std::fprintf(log, "PARTICULA campo %llu cmd %02x col %06x n %d z %.0f ancla %d\n", (unsigned long long)field_count_, cmd, col[0], n, z, ak);
                continue;
              }
            }
          }
        }
        if (static const bool fp = std::getenv("SF3N_LUZPC") != nullptr && std::getenv("SF3N_FXPC") != nullptr; fp && !polyMatched) {

          static std::map<std::uint32_t, std::unordered_set<std::uint32_t>> byXY; static std::uint64_t built = ~0ull;
          if (built != field_count_) { built = field_count_; byXY.clear(); for (const auto& [pc, sxy] : dbg_proj_frame_) byXY[sxy].insert(pc); }
          std::map<std::uint32_t, int> cnt;
          for (int k = 0; k < n; ++k) if (auto it = byXY.find(xy[std::size_t(k)] & 0x07ff07ffu); it != byXY.end()) for (auto pc : it->second) ++cnt[pc];
          std::uint32_t bpc = 0; int bn = 0; for (auto& [pc, c] : cnt) if (c > bn) { bn = c; bpc = pc; }
          std::fprintf(log, "FXPC campo %llu cmd %02x col %06x n %d tex %d vertices_en_rutina %d/%d pc %08x xy0 %d,%d\n", (unsigned long long)field_count_, cmd, col[0], n, int(tex), bn, n, bpc, int(std::int16_t(std::uint16_t(xy[0] << 5)) >> 5), int(std::int16_t(std::uint16_t((xy[0] >> 16) << 5)) >> 5));
        }

        if (!polyMatched && flat2d && shownOnPs1(xy, col, n)) {
          if (overlayTpage != drawTpage) { overlay.push_back({0xe1000000u | drawTpage}); overlayTpage = drawTpage; }
          overlay.push_back(pk);
        }

        if (!polyMatched && !tex && (cmd & 2) && !quad && !gour) {
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (int k = 0; k < n; ++k) {
            const double x = double(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5), y = double(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5);
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
          }
          const int r = int(col[0] & 0xff), g = int((col[0] >> 8) & 0xff), b = int((col[0] >> 16) & 0xff);
          const bool gray = std::abs(r - g) <= 12 && std::abs(g - b) <= 12 && r >= 16;
          static const int kSnow = std::clamp(envInt("SF3N_NIEVE", 3), 0, 6);
          if (gray && x1 - x0 <= 8 && y1 - y0 <= 8 && kSnow > 0) {
            ++snow_idx;

            if (kSnow >= 2) continue;
            if (overlayTpage != drawTpage) { overlay.push_back({0xe1000000u | drawTpage}); overlayTpage = drawTpage; }
            overlay.push_back(pk);
            for (int c2 = 1; c2 < kSnow; ++c2) {
              std::uint32_t h = std::uint32_t(snow_idx) * 2654435761u + std::uint32_t(c2) * 40503u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
              const int dx = int(h % 401u) - 200, dy = int((h >> 9) % 241u) - 120;
              auto cp = pk;
              for (int k = 0; k < n; ++k) {
                const std::size_t wi = std::size_t(1 + k);
                const int x = int(std::int16_t(std::uint16_t(cp[wi] << 5)) >> 5) + dx, y = int(std::int16_t(std::uint16_t((cp[wi] >> 16) << 5)) >> 5) + dy;
                cp[wi] = (std::uint32_t(x) & 0x7ffu) | ((std::uint32_t(y) & 0x7ffu) << 16);
              }
              overlay.push_back(cp);
            }
            continue;
          }
        }

        else if (!polyMatched && tex && (cmd & 2) && (!(cmd & 0x10) || std::getenv("SF3N_BRILLOS_DEGRADADO")) && ((((uvw[1] >> 16) & 0x1ff) >> 5) & 3) == 1 && !std::getenv("SF3N_SIN_DESTELLOS")) {

          std::array<double, 4> px{}, py{};
          for (int k = 0; k < n; ++k) { px[std::size_t(k)] = double(std::int16_t(std::uint16_t(xy[std::size_t(k)] << 5)) >> 5); py[std::size_t(k)] = double(std::int16_t(std::uint16_t((xy[std::size_t(k)] >> 16) << 5)) >> 5); }
          bool rect = n == 4;
          if (rect) {
            const double x0 = *std::min_element(px.begin(), px.end()), x1 = *std::max_element(px.begin(), px.end());
            const double y0 = *std::min_element(py.begin(), py.end()), y1 = *std::max_element(py.begin(), py.end());
            for (int k = 0; k < 4 && rect; ++k) rect = (std::fabs(px[std::size_t(k)] - x0) <= 1 || std::fabs(px[std::size_t(k)] - x1) <= 1) && (std::fabs(py[std::size_t(k)] - y0) <= 1 || std::fabs(py[std::size_t(k)] - y1) <= 1);
          }
          if (rect) {

            double cx = (px[0] + px[1] + px[2] + px[3]) / 4, cy = (py[0] + py[1] + py[2] + py[3]) / 4;
            for (int k = 0; k < 4; ++k) if (find(xy[std::size_t(k)], eff_maps_.size() - 1)) { cx = px[std::size_t(k)]; cy = py[std::size_t(k)]; break; }
            glares.push_back({segIdx, cx, cy, {xy[0] & 0x07ff07ffu, xy[1] & 0x07ff07ffu, xy[2] & 0x07ff07ffu, xy[3] & 0x07ff07ffu}});
          }
        }
        continue;
      }
      if (cmd >= 0x40 && cmd < 0x60 && !(cmd & 0x08)) {
        const bool gour = cmd & 0x10;
        if (pk.size() < (gour ? 4u : 3u)) continue;
        const std::uint32_t xa = pk[1], xb = gour ? pk[3] : pk[2];
        bool matched = false;
        for (std::size_t mi = eff_maps_.size(); mi-- > 0;) {
          const auto* a = find(xa, mi);
          const auto* b = a ? find(xb, mi) : nullptr;
          if (!a || !b) continue;
          matched = true;
          EffectPrim e;
          e.kind = 2; e.cmd = std::uint8_t(cmd); e.n = 2;
          e.w[0] = *a; e.w[1] = *b;
          e.col[0] = pk[0] & 0xffffff; e.col[1] = gour ? pk[2] & 0xffffff : e.col[0];
          e.tpage = drawTpage;
          out.push_back(e);
          break;
        }

        if (!matched) {

          if (overlayTpage != drawTpage) { overlay.push_back({0xe1000000u | drawTpage}); overlayTpage = drawTpage; }
          overlay.push_back(pk);
        }
        continue;
      }
      if (cmd >= 0x60 && cmd < 0x80 && !(cmd & 4) && (cmd & 2) && ((cmd >> 3) & 3) == 0 && pk.size() >= 3 && (pk[2] & 0x3ff) >= 320 && ((pk[2] >> 16) & 0x1ff) >= 220) {

        if (overlayTpage != drawTpage) { overlay.push_back({0xe1000000u | drawTpage}); overlayTpage = drawTpage; }
        overlay.push_back(pk);
        if (static const bool fl = std::getenv("SF3N_FLASHLOG") != nullptr; fl) std::fprintf(log, "destello rect col %06x campo %llu\n", pk[0] & 0xffffff, (unsigned long long)field_count_);
        continue;
      }
      if (cmd >= 0x60 && cmd < 0x80) {
        const bool tex = cmd & 4;
        std::size_t i = 1;
        const std::uint32_t xy = i < pk.size() ? pk[i++] : 0;
        const std::uint32_t uvw = tex && i < pk.size() ? pk[i++] : 0;
        double w = 1, h = 1;
        switch ((cmd >> 3) & 3) {
          case 0: { const auto wh = i < pk.size() ? pk[i] : 0; w = double(wh & 0x3ff); h = double((wh >> 16) & 0x1ff); break; }
          case 1: w = h = 1; break;
          case 2: w = h = 8; break;
          default: w = h = 16; break;
        }
        const double cx = double(std::int16_t(std::uint16_t(xy << 5)) >> 5) + w / 2, cy = double(std::int16_t(std::uint16_t((xy >> 16) << 5)) >> 5) + h / 2;
        const auto& mp = eff_maps_.back();
        double best = 2.6 * 2.6;
        const std::pair<std::array<double, 3>, double>* bp = nullptr;
        for (const auto& [k, v] : mp.pts) {
          const double dx = double(std::int16_t(k & 0xffff)) - cx, dy = double(std::int16_t(k >> 16)) - cy;
          const double d = dx * dx + dy * dy;
          if (d < best) { best = d; bp = &v; }
        }
        if (!bp || bp->second <= 0) continue;
        EffectPrim e;
        e.kind = 3; e.cmd = std::uint8_t(cmd); e.n = 1;
        e.w[0] = bp->first;
        e.col[0] = pk[0] & 0xffffff;
        e.uv[0] = std::uint16_t(uvw & 0xffff);
        e.clut = std::uint16_t(uvw >> 16);
        e.tpage = drawTpage;

        e.size_w = w * bp->second / mp.fx;
        e.size_h = h * bp->second / mp.fy;
        e.size_px_w = w; e.size_px_h = h;
        out.push_back(e);
        continue;
      }
    }
    static const std::string fxdT = std::getenv("SF3N_FXDUMP_T") ? std::getenv("SF3N_FXDUMP_T") : "";
    bool fxdNow = false;
    if (!fxdT.empty()) { std::size_t p0 = 0; while (p0 < fxdT.size()) { auto q = fxdT.find(',', p0); if (q == std::string::npos) q = fxdT.size(); if (game_field_.load() == std::uint64_t(std::atof(fxdT.substr(p0, q - p0).c_str()) * 50 + 0.5)) fxdNow = true; p0 = q + 1; } }
    if (static int dumpN = std::getenv("SF3N_FXDUMP") ? std::atoi(std::getenv("SF3N_FXDUMP")) : -1; fxdNow || (dumpN >= 0 && dumpN-- == 0)) {
      std::fprintf(log, "FXDUMP puntos:");
      for (const auto& [k, v] : eff_maps_.back().pts) std::fprintf(log, " (%d,%d z%.0f)", int(std::int16_t(k & 0xffff)), int(std::int16_t(k >> 16)), v.second);
      std::fprintf(log, "\nFXDUMP 2e/20/36/34-semi:");
      for (const auto& pk : seg) {
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        const std::uint32_t col = pk[0] & 0xffffff;
        const bool reddish = (col & 0xff) > 2 * ((col >> 8) & 0xff) + 8 && (col & 0xff) > 2 * ((col >> 16) & 0xff) + 8;
        if (c != 0x2e && c != 0x20 && c != 0x2c && c != 0x36 && c != 0x22 && c != 0x2a && !reddish && !(c >= 0x60 && c < 0x80)) continue;
        std::fprintf(log, " [%02x col%06x", c, pk[0] & 0xffffff);
        const bool tex = c & 4; const int n = (c & 8) ? 4 : 3;
        for (int k = 0; k < n; ++k) { const auto w = pk[std::size_t(1 + k * (tex ? 2 : 1))]; std::fprintf(log, " %d,%d", int(std::int16_t(w & 0xffff)), int(std::int16_t(w >> 16))); }
        if (tex) { std::fprintf(log, " clut%04x tp%04x uv", pk[2] >> 16, pk[4] >> 16); for (int k = 0; k < n; ++k) { const auto w = pk[std::size_t(2 + k * 2)]; std::fprintf(log, " %u,%u", w & 0xff, (w >> 8) & 0xff); } }
        std::fprintf(log, "]");
      }
      std::fprintf(log, "\n");
    }
    if (std::getenv("SF3N_FXLOG") && !out.empty()) {
      std::map<int, int> hist;
      for (const auto& e : out) hist[e.cmd]++;
      std::fprintf(log, "efectos: %zu (puntos %zu, exactos %llu, desproyectados %llu):", out.size(), eff_maps_.back().pts.size(), (unsigned long long)eff_exact_, (unsigned long long)eff_unprojected_);
      for (auto [c, n] : hist) std::fprintf(log, " %02x:%d", c, n);
      std::fprintf(log, " | superpuestas");
      for (const auto& o : overlay) std::fprintf(log, " %08x", o[0]);
      std::fprintf(log, " | parciales");
      for (auto [c, n] : partial) std::fprintf(log, " %02x:%d", c, n);
      std::fprintf(log, " | lista");
      for (auto [c, n] : allcmd) std::fprintf(log, " %02x:%d", c, n);
      std::fprintf(log, "\n");
    }
    std::lock_guard lk(mtx_);
    pub_tritex_.push_back(triTex);
    if (pub_tritex_.size() > 8) pub_tritex_.erase(pub_tritex_.begin());
    if (static const char* fs = std::getenv("SF3N_FXSTAT"); fs) {
      static long a = -1, b = -1; if (a < 0) std::sscanf(fs, "%ld:%ld", &a, &b);
      if (long(field_count_) >= a && long(field_count_) <= b)
        for (const auto& e : out) {
          double ext = 0;
          for (int i = 1; i < e.n; ++i) for (int j = 0; j < 3; ++j) ext = std::max(ext, std::fabs(e.w[std::size_t(i)][std::size_t(j)] - e.w[0][std::size_t(j)]));
          std::fprintf(log, "FXSTAT campo %llu k%d cmd %02x col %06x %06x tp %04x abr %d clut %04x uv %04x w0 %.0f %.0f %.0f ext %.0f sz %.0fx%.0f\n", (unsigned long long)field_count_, e.kind, e.cmd, e.col[0], e.col[1], e.tpage, (e.tpage >> 5) & 3, e.clut, e.uv[0], e.w[0][0], e.w[0][1], e.w[0][2], ext, e.size_w, e.size_h);
        }
    }
    pub_effects_.swap(out);
    if (static const bool ol = std::getenv("SF3N_MIRALOG") != nullptr; ol && !overlay.empty() && field_count_ % 10 == 0) {
      std::fprintf(log, "MIRA campo %llu:", (unsigned long long)field_count_);
      for (const auto& o : overlay) { std::fprintf(log, " ["); for (auto w : o) std::fprintf(log, "%08x ", w); std::fprintf(log, "]"); }
      std::fprintf(log, "\n");
    }

    if (!glares.empty()) {
      auto inside = [](double x, double y, const double* xs, const double* ys, int n) {
        auto tri = [&](int a, int b, int c) {
          const double d1 = (x - xs[b]) * (ys[a] - ys[b]) - (xs[a] - xs[b]) * (y - ys[b]);
          const double d2 = (x - xs[c]) * (ys[b] - ys[c]) - (xs[b] - xs[c]) * (y - ys[c]);
          const double d3 = (x - xs[a]) * (ys[c] - ys[a]) - (xs[c] - xs[a]) * (y - ys[a]);
          const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
          return !(neg && pos);
        };
        return tri(0, 1, 2) || (n == 4 && tri(1, 3, 2));
      };

      {
        std::map<std::uint32_t, int> cnt;
        for (const auto& g : glares) for (auto c : g.corners) ++cnt[c];
        for (auto& g : glares) {
          std::uint32_t best = 0; int bc = 1;
          for (auto c : g.corners) if (cnt[c] > bc) { bc = cnt[c]; best = c; }
          if (bc >= 2) { g.cx = double(std::int16_t(std::uint16_t(best << 5)) >> 5); g.cy = double(std::int16_t(std::uint16_t((best >> 16) << 5)) >> 5); }
        }
      }
      int shown = 0, hidden = 0;
      for (const auto& g : glares) {
        bool covered = false;
        for (std::size_t j = g.idx + 1; j < seg.size() && !covered; ++j) {
          const auto& q = seg[j];
          if (q.empty()) continue;
          const std::uint32_t c = q[0] >> 24;
          if (c < 0x20 || c >= 0x40 || (c & 2)) continue;
          const bool qq = c & 8, tx = c & 4, gr = c & 0x10;
          const int nv = qq ? 4 : 3;
          double xs[4], ys[4]; std::size_t i = 1; bool ok = true;
          for (int k = 0; k < nv; ++k) {
            if (k > 0 && gr) ++i;
            if (i >= q.size()) { ok = false; break; }
            xs[k] = double(std::int16_t(std::uint16_t(q[i] << 5)) >> 5); ys[k] = double(std::int16_t(std::uint16_t((q[i] >> 16) << 5)) >> 5); ++i;
            if (tx) ++i;
          }
          if (ok && inside(g.cx, g.cy, xs, ys, nv)) covered = true;
        }
        if (covered) { ++hidden; continue; }
        ++shown;
        if (static const bool gl2 = std::getenv("SF3N_GLARELOG") != nullptr; gl2) {
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (auto c : g.corners) { const double x = double(std::int16_t(std::uint16_t(c << 5)) >> 5), y = double(std::int16_t(std::uint16_t((c >> 16) << 5)) >> 5); x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
          const auto& q = seg[g.idx];
          std::fprintf(log, "  brillo %.0fx%.0f en %.0f,%.0f cmd %02x color %06x uv0 %08x uv1 %08x\n", x1 - x0, y1 - y0, x0, y0, q[0] >> 24, q[0] & 0xffffff, q.size() > 2 ? q[2] : 0, q.size() > 5 ? q[5] : 0); if (static int once = 0; !once++) std::fprintf(log, "  (alias del mundo %d)\n", int(world_alias_));
        }

        if (static const bool en2d = std::getenv("SF3N_BRILLOS_2D") != nullptr; !en2d) {
          const auto& q = seg[g.idx];
          const std::uint32_t c = q[0] >> 24;
          const bool gr = c & 0x10;
          const auto& mp = eff_maps_.back();
          const std::pair<std::array<double, 3>, double>* bp = nullptr;
          std::pair<std::array<double, 3>, double> regPick;
          double ocx = g.cx, ocy = g.cy;
          double glowGhost = 1.0, glowGhostY = 1.0;
          {
            double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
            for (auto cc : g.corners) { const double x = double(std::int16_t(std::uint16_t(cc << 5)) >> 5), y = double(std::int16_t(std::uint16_t((cc >> 16) << 5)) >> 5); x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
            static const double tf = envInt("SF3N_LUZ_TOL", 150) / 100.0;

            const std::array<double, 3>* exact = nullptr;
            double ghost = 1.0, ghostY = 1.0;
            const LightHistPt* dbgL = nullptr; bool dbgNeg = false; int dbgAge = -1;
            {
              const int ix = int(std::lround(g.cx)), iy = int(std::lround(g.cy));
              auto near1 = [&](std::uint32_t k) { const int kx = int(std::int16_t(std::uint16_t(k << 5)) >> 5), ky = int(std::int16_t(std::uint16_t((k >> 16) << 5)) >> 5); return std::abs(kx - ix) <= 1 && std::abs(ky - iy) <= 1; };
              for (std::size_t mi = light_hist_.size(); mi-- > 0 && !exact;)
                for (const auto& L : light_hist_[mi]) {
                  if (near1(L.key)) { exact = &L.w; ghost = L.s; ghostY = L.scy; dbgL = &L; dbgNeg = false; dbgAge = int(light_hist_.size() - 1 - mi); break; }
                  if (near1(L.keyNeg)) { exact = &L.w; ghost = -L.s; ghostY = -L.scy; dbgL = &L; dbgNeg = true; dbgAge = int(light_hist_.size() - 1 - mi); break; }
                }
              if (static const bool lm = std::getenv("SF3N_LUZMED") != nullptr; lm) {
                const auto& q = seg[g.idx];
                if (dbgL) std::fprintf(log, "MED campo %llu brillo %d,%d col %06x uv %08x luz %s edad %d escala %.3f H %u/%u real %.1f,%.1f proy %.1f,%.1f mundo %.0f %.0f %.0f\n", (unsigned long long)field_count_, ix, iy, q[0] & 0xffffff, q.size() > 2 ? q[2] : 0, dbgNeg ? "NEG" : "POS", dbgAge, dbgL->s, dbgL->h, dbgL->wh, dbgL->sx, dbgL->sy, dbgL->px, dbgL->py, dbgL->w[0], dbgL->w[1], dbgL->w[2]);
                else std::fprintf(log, "MED campo %llu brillo %d,%d col %06x uv %08x SIN LUZ\n", (unsigned long long)field_count_, ix, iy, q[0] & 0xffffff, q.size() > 2 ? q[2] : 0);
              }
              if (static const bool ld = std::getenv("SF3N_LUZDIF") != nullptr; ld && !exact) {
                double bd = 1e9; int bx = 0, by = 0;
                if (!light_hist_.empty()) for (const auto& L : light_hist_.back()) { const auto k = L.key; const int kx = int(std::int16_t(std::uint16_t(k << 5)) >> 5), ky = int(std::int16_t(std::uint16_t((k >> 16) << 5)) >> 5); const double d = double((kx - ix) * (kx - ix) + (ky - iy) * (ky - iy)); if (d < bd) { bd = d; bx = ix - kx; by = iy - ky; } }
                const auto& q = seg[g.idx];
                { std::map<std::uint32_t, int> ras; for (const auto& v : dbg_ra_hist_) for (const auto& [k, ra] : v) { const int kx = int(std::int16_t(std::uint16_t(k << 5)) >> 5), ky = int(std::int16_t(std::uint16_t((k >> 16) << 5)) >> 5); if (std::abs(kx - ix) <= 1 && std::abs(ky - iy) <= 1) ++ras[ra]; }
                  std::fprintf(log, "LUZRA"); for (auto& [ra, n] : ras) std::fprintf(log, " %08x=%d", ra, n); std::fprintf(log, "\n"); }
                std::fprintf(log, "LUZDIF campo %llu centro %d,%d luces %zu dif %d,%d col %06x uv %08x %08x\n", (unsigned long long)field_count_, ix, iy, light_hist_.size(), bx, by, q[0] & 0xffffff, q.size() > 2 ? q[2] : 0, q.size() > 4 ? q[4] : 0);
              }
              if (static const bool ll = std::getenv("SF3N_LUZLOG") != nullptr; ll) { static long a = 0, b = 0; (exact ? a : b)++; if ((a + b) % 1000 == 0) std::fprintf(log, "luz exacta %ld, sin ella %ld\n", a, b); }
            }
            if (exact && mp.cam.valid) {
              const auto& cam = mp.cam; double z = 0;
              for (int i = 0; i < 3; ++i) z += cam.rot[std::size_t(6 + i)] * ((*exact)[std::size_t(i)] - cam.eye[std::size_t(i)]);
              if (z > 1.0) { regPick = {*exact, z}; bp = &regPick; glowGhost = ghost; glowGhostY = ghostY; }
            }
            if (!bp)

            if (const auto lp = lightFor(mp, g.cx, g.cy, std::max(8.0, tf * std::max(x1 - x0, y1 - y0))); lp && mp.cam.valid && mp.of_ok) {
              const auto& cam = mp.cam; const double pz = lp->z;
              const double cc[3] = {(g.cx - mp.ofx) * pz / cam.fx, (g.cy - mp.ofy) * pz / cam.fy, pz};
              std::array<double, 3> w{};
              for (int j = 0; j < 3; ++j) w[std::size_t(j)] = cam.eye[std::size_t(j)] + cam.rot[std::size_t(j)] * cc[0] + cam.rot[std::size_t(3 + j)] * cc[1] + cam.rot[std::size_t(6 + j)] * cc[2];
              regPick = {w, pz}; bp = &regPick;
            } else if (mp.cam.valid && mp.of_ok) {

              const auto& cam = mp.cam; double pz = 0, bd = 6.0 * 6.0;
              for (const auto& [k, v] : mp.pts) {
                const double dx = double(std::int16_t(k & 0xffff)) - g.cx, dy = double(std::int16_t(k >> 16)) - g.cy;
                if (dx * dx + dy * dy >= bd) continue;
                const auto& w = v.first; double z = 0;
                for (int i = 0; i < 3; ++i) z += cam.rot[std::size_t(6 + i)] * (w[std::size_t(i)] - cam.eye[std::size_t(i)]);
                if (z > 1.0) { bd = dx * dx + dy * dy; pz = z; }
              }
              if (pz <= 0) pz = envInt("SF3N_LUZ_Z", 2000);
              const double cc[3] = {(g.cx - mp.ofx) * pz / cam.fx, (g.cy - mp.ofy) * pz / cam.fy, pz};
              std::array<double, 3> w{};
              for (int j = 0; j < 3; ++j) w[std::size_t(j)] = cam.eye[std::size_t(j)] + cam.rot[std::size_t(j)] * cc[0] + cam.rot[std::size_t(3 + j)] * cc[1] + cam.rot[std::size_t(6 + j)] * cc[2];
              regPick = {w, pz}; bp = &regPick;
            }
          }
          if (static const bool ld = std::getenv("SF3N_LUZDUMP") != nullptr; ld && field_count_ % 100 == 0) {
            double bd = 1e9; std::array<double,3> bw{};
            for (const auto& [k, w] : mp.all) { const double dx = double(std::int16_t(k & 0xffff)) - g.cx, dy = double(std::int16_t(k >> 16)) - g.cy; if (dx*dx+dy*dy < bd) { bd = dx*dx+dy*dy; bw = w; } }
            std::fprintf(log, "LUZ campo %llu centro %.0f,%.0f %s punto mas cerca %.1f px (%.0f %.0f %.0f) puntos %zu\n", (unsigned long long)field_count_, g.cx, g.cy, bp ? "ANCLADA" : "2D", std::sqrt(bd), bw[0], bw[1], bw[2], mp.all.size());
          }
          if (static const bool lp = std::getenv("SF3N_LUZPC") != nullptr; lp) {
            const std::uint32_t key = (std::uint32_t(std::lround(g.cx)) & 0x7ffu) | ((std::uint32_t(std::lround(g.cy)) & 0x7ffu) << 16);
            std::set<std::uint32_t> pcs; for (const auto& [pc, sxy] : dbg_proj_frame_) if (sxy == key) pcs.insert(pc);
            if (pcs.empty()) { double bd = 1e9; std::uint32_t bpc = 0; for (const auto& [pc, sxy] : dbg_proj_frame_) { const double dx = double(std::int16_t(sxy & 0xffff)) - g.cx, dy = double(std::int16_t(sxy >> 16)) - g.cy; if (dx*dx+dy*dy < bd) { bd = dx*dx+dy*dy; bpc = pc; } }
              static std::map<int, long> hist; ++hist[std::min(20, int(std::sqrt(bd)))]; static std::map<std::uint32_t, long> np; if (bd < 64) ++np[bpc];
              if (dbg_luz_n_ % 3000 == 0) { std::fprintf(log, "LUZDIST"); for (auto& [d, n] : hist) std::fprintf(log, " %d:%ld", d, n); std::fprintf(log, " | cerca:"); for (auto& [pc, n] : np) if (n > 20) std::fprintf(log, " %08x=%ld", pc, n); std::fprintf(log, "\n"); } }
            ++dbg_luz_n_; if (pcs.empty()) ++dbg_luz_none_; for (auto pc : pcs) ++dbg_luz_pc_[pc];
            if (dbg_luz_n_ % 3000 == 0) { std::fprintf(log, "LUZPC %ld brillos, %ld sin proyeccion, proyecciones/cuadro %zu:", dbg_luz_n_, dbg_luz_none_, dbg_proj_frame_.size()); for (auto& [pc, n] : dbg_luz_pc_) if (n * 50 > dbg_luz_n_) std::fprintf(log, " %08x=%ld", pc, n); std::fprintf(log, "\n"); }
          }
          if (static const bool ll = std::getenv("SF3N_LUZLOG") != nullptr; ll) { static long ok = 0, no = 0; (bp ? ok : no)++; if ((ok + no) % 2000 == 0) std::fprintf(log, "luces: %ld ancladas, %ld en 2D, registro %zu\n", ok, no, light_reg_.size()); }
          if (bp && bp->second > 0 && mp.fx > 0 && mp.fy > 0) {
            EffectPrim e;
            e.kind = 4; e.cmd = std::uint8_t(c); e.n = 4;
            e.w[0] = bp->first;
            std::size_t i = 1; bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
              std::uint32_t col = q[0];
              if (gr && k > 0) { if (i >= q.size()) { ok = false; break; } col = q[i++]; }
              if (i + 1 >= q.size()) { ok = false; break; }
              const std::uint32_t xy = q[i++], uv = q[i++];
              e.col[std::size_t(k)] = col & 0xffffff;
              e.off[std::size_t(k)] = {double(std::int16_t(std::uint16_t(xy << 5)) >> 5) - ocx, double(std::int16_t(std::uint16_t((xy >> 16) << 5)) >> 5) - ocy};
              e.uv[std::size_t(k)] = std::uint16_t(uv & 0xffff);
              if (k == 0) e.clut = std::uint16_t(uv >> 16);
              if (k == 1) e.tpage = std::uint16_t((uv >> 16) & 0x1ff);
            }
            if (ok) {
              e.size_w = bp->second / mp.fx; e.size_h = bp->second / mp.fy;
              e.ghost = glowGhost; e.ghost_y = glowGhostY; e.ps1x = g.cx; e.ps1y = g.cy;
              glows_now_.push_back(e);
              continue;
            }
          }
        }
        overlay.push_back(seg[g.idx]);
      }
      if (static const bool gl = std::getenv("SF3N_GLARELOG") != nullptr; gl) std::fprintf(log, "brillos campo %llu: %d se ven, %d tapados\n", (unsigned long long)field_count_, shown, hidden);
    }
    pub_overlay_.swap(overlay);
    pub_glow_hist_.push_back({eff_maps_.back().cam, std::move(glows_now_)});
    glows_now_.clear();
    if (pub_glow_hist_.size() > 16) pub_glow_hist_.erase(pub_glow_hist_.begin());
    pub_effects_time_ = nowSeconds();
    pub_snow_count_ = snow_idx;
  }

  const std::vector<std::vector<std::uint32_t>>* hud_source = nullptr;
  const std::vector<std::vector<std::uint32_t>>* frame_source = nullptr;
  const std::uint64_t* frame_seq = nullptr;
  const bool* hud_valid = nullptr;

  void queueAudio(const std::int16_t* lr, std::size_t frames) {
    if (static const char* wv = std::getenv("SF3N_WAV"); wv) {
      static FILE* wf = nullptr; static const double desde = std::getenv("SF3N_WAV_DESDE") ? std::atof(std::getenv("SF3N_WAV_DESDE")) : 0.0;
      if (double(game_field_.load()) / 50.0 >= desde) {
        if (!wf) { wf = std::fopen(wv, "wb"); if (log) std::fprintf(log, "WAV desde campo %llu\n", (unsigned long long)game_field_.load()); }
        if (wf) { std::fwrite(lr, 4, frames, wf); std::fflush(wf); }
      }
    }
    if (!audio_ || frames == 0) return;
    last_game_audio_ = nowSeconds();
    static std::vector<std::int16_t> mixed;

    if (fast || SDL_GetQueuedAudioSize(audio_) / 4 > 44100 / 4) return;
    {
      std::lock_guard lk(sfx_mtx_);
      if (!sfx_mix_.empty()) {
        mixed.assign(lr, lr + frames * 2);
        const std::size_t n = std::min(sfx_mix_.size(), mixed.size());
        for (std::size_t i = 0; i < n; ++i) mixed[i] = std::int16_t(std::clamp(int(mixed[i]) + int(sfx_mix_[i]), -32768, 32767));
        sfx_mix_.erase(sfx_mix_.begin(), sfx_mix_.begin() + std::ptrdiff_t(n));
        lr = mixed.data();
      }
    }
    const auto queued = SDL_GetQueuedAudioSize(audio_) / 4;
    if (fast || queued > 44100 / 4) return;
    if (queued == 0) {

      static const std::size_t delayFrames = std::size_t(std::clamp(envInt("SF3N_AUDIO_DELAY_MS", envInt("SF3N_DELAY_MS", 60) + 40), 0, 500)) * 44100 / 1000;
      static const std::vector<std::int16_t> silence((2048 + delayFrames) * 2, 0);
      SDL_QueueAudio(audio_, silence.data(), Uint32(silence.size() * 2));
    }
    if (vol_master_ != 10 || mono_ || exit_fade_vol_.load() < 256) {

      static std::vector<std::int16_t> buf;
      buf.assign(lr, lr + frames * 2);
      for (std::size_t i = 0; i < frames; ++i) {
        const int fv = exit_fade_vol_.load(); int l = buf[2 * i] * vol_master_ / 10 * fv / 256, r = buf[2 * i + 1] * vol_master_ / 10 * fv / 256;
        if (mono_) l = r = (l + r) / 2;
        buf[2 * i] = std::int16_t(l); buf[2 * i + 1] = std::int16_t(r);
      }
      SDL_QueueAudio(audio_, buf.data(), Uint32(frames * 4));
      return;
    }
    SDL_QueueAudio(audio_, lr, Uint32(frames * 4));
  }

  void field(const SoftGpu& g) {
    ++field_count_;
    const bool tab = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] != 0;

    const bool worldNow = field_count_ - last_snapshot_field_ <= 3;
    const bool quiet = quiet_fields_ >= 12;
    const bool mdecIdle = field_count_ - last_mdec_field_ > 25;
    const bool turbo = turbo_enabled_ && !worldNow && quiet && mdecIdle && buttons() == 0 && field_count_ > 50;
    if (turbo != turbo_on_) { turbo_on_ = turbo; if (std::getenv("SF3N_TURBOLOG")) std::fprintf(log, "turbo %s campo %llu\n", turbo ? "si" : "no", (unsigned long long)field_count_); }
    static const bool fastEnv = std::getenv("SF3N_SIN_ESPERA") != nullptr;
    fast = tab || fastEnv;
    publishField(g);
    const auto now = Clock::now();
    const auto t_emu = now - field_start_;
    emu_busy_ += t_emu;
    if (fast) {
      sched_ = now;
    } else if (turbo) {
      sched_ += field_period_ / 4;
      if (now > sched_ + std::chrono::milliseconds(50)) sched_ = now;
      else if (sched_ > now) std::this_thread::sleep_until(sched_);
    } else {
      sched_ += field_period_;

      if (const double late = std::chrono::duration<double, std::milli>(now - sched_).count(); late > 250.0 && field_count_ > 120)
        std::fprintf(log, "tiron: campo %llu con %.0f ms de retraso (campo %.0f ms)\n", (unsigned long long)field_count_, late, std::chrono::duration<double, std::milli>(t_emu).count());
      if (now > sched_ + std::chrono::milliseconds(600)) { sched_ = now; ++emu_resyncs_; }
      else if (sched_ > now) std::this_thread::sleep_until(sched_);
    }
    if (inject_start_ > 0) --inject_start_;
    if (inject_back_ > 0) --inject_back_;
    if (!pad_seq_.empty() && --pad_seq_.front().second <= 0) pad_seq_.pop_front();
    if (title_pulse_n_.load() > 0) --title_pulse_n_;

    if (worldNow && !menu_open_ && (rawButtons() & ~0x0009u)) {
      std::string fog;
      { std::lock_guard lk(req_mtx_); fog.swap(pending_mission_); }
      if (!fog.empty()) { requestSave(missionPath(fog)); addProgress(fog); }
    }
    wheelTick();
    if (static const char* dm = std::getenv("SF3N_DEBUGMISION"); dm) {
      double t = 0; int n = -1;
      if (std::sscanf(dm, "%lf:%d", &t, &n) == 2 && field_count_ == std::uint64_t(t * 50 + 0.5)) { debug_mission_req_ = n; std::fprintf(log, "depuracion (prueba): pedir mision %d\n", n); }
    }

    if (const int tgt = weapon_target_; tgt >= 0 && !ram_.empty()) {
      if (ramByte(weapon_cur_addr_) == tgt && weapon_phase_ == 0) weapon_target_ = -1;
      else if (++weapon_deadline_ > 400) { weapon_target_ = -1; weapon_phase_ = 0; }
      else { const int t = weapon_deadline_ % 6; weapon_phase_ = t < 3 ? 1 : 0; }
      if (weapon_target_ < 0) weapon_phase_ = 0;
    }
    if (menu_open_) {

      while (menu_open_ && !quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
      sched_ = Clock::now();
    }

    if (static const char* pt = std::getenv("SF3N_PRUEBA_TIRON"); pt) {
      unsigned long long fc = 0; int ms = 0;
      if (std::sscanf(pt, "%llu:%d", &fc, &ms) == 2 && field_count_ == fc) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    field_start_ = Clock::now();
    if (field_count_ % 500 == 0) {
      std::fprintf(log, "ritmo: emulacion %.1f ms/campo (de %.1f), %llu resincronizaciones, dibujo %.0f cuadros/s\n",
                   std::chrono::duration<double, std::milli>(emu_busy_).count() / 500.0, std::chrono::duration<double, std::milli>(field_period_).count(), (unsigned long long)emu_resyncs_,
                   double(frames_drawn_.exchange(0)) / 10.0);
      emu_busy_ = {};
      emu_resyncs_ = 0;
    }
  }

  void runRenderLoop(const std::atomic<bool>& emu_done) {

    auto next = Clock::now();
    while (!quit && !emu_done) {
      renderFrame();
      ++frames_drawn_;
      const int cap = fps_cap_.load();
      if (cap <= 0) { next = Clock::now(); continue; }
      const auto period = std::chrono::nanoseconds(1000000000LL / cap);
      next += period;
      auto now = Clock::now();
      if (now > next + period) next = now;
      if (next - now > std::chrono::microseconds(1500)) std::this_thread::sleep_for(next - now - std::chrono::microseconds(1500));
      while (Clock::now() < next) std::this_thread::yield();
    }
  }

  bool screenshot(const char* path) {
    std::lock_guard lk(mtx_);
    shot_path_ = path;
    g_psyx_before_swap = &BootWindow::beforeSwap;
    s_self = this;
    return true;
  }
  bool shotPending() { std::lock_guard lk(mtx_); return !shot_path_.empty(); }
  static inline BootWindow* s_self = nullptr;
  static void beforeSwap() {
    if (!s_self) return;
    std::string path;
    {
      std::lock_guard lk(s_self->mtx_);
      path.swap(s_self->shot_path_);
    }
    if (!path.empty()) {
      if (std::getenv("SF3N_LUZMED")) std::fprintf(s_self->log, "FOTO %s\n", path.c_str());
      s_self->takeShot(path.c_str());
      if (std::getenv("SF3N_PRIMDUMP")) {
        auto* S = s_self; std::fprintf(S->log, "PRIMDUMP %s\n", path.c_str());
        static const bool allz = std::getenv("SF3N_PRIMDUMP")[0] == '2';
        auto inz = [](int x, int y) { return allz || (x > 170 && x < 220 && y > 160 && y < 240); };
        for (const auto& t : S->tris_) if (!(t.code & 2) && (inz(int(float(t.x0)), int(float(t.y0))) || inz(int(float(t.x1)), int(float(t.y1))) || inz(int(float(t.x2)), int(float(t.y2)))) && true)
          std::fprintf(S->log, "  GT3 claro tp %04x clut %04x rgb %02x%02x%02x %02x%02x%02x %02x%02x%02x (%d,%d) (%d,%d) (%d,%d) idx %zu\n", unsigned(t.tpage), unsigned(t.clut), t.r0, t.g0, t.b0, t.r1, t.g1, t.b1, t.r2, t.g2, t.b2, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)), std::size_t(&t - S->tris_.data()));
        for (const auto& t : S->quads_) if (!(t.code & 2) && (inz(int(float(t.x0)), int(float(t.y0))) || inz(int(float(t.x3)), int(float(t.y3)))) && true)
          std::fprintf(S->log, "  GT4 claro tp %04x clut %04x rgb %02x%02x%02x (%d,%d) (%d,%d) (%d,%d) (%d,%d)\n", unsigned(t.tpage), unsigned(t.clut), t.r0, t.g0, t.b0, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)), int(float(t.x3)), int(float(t.y3)));
        for (const auto& t : S->tris_) if (t.code & 2) std::fprintf(S->log, "  GT3 semi tp %04x rgb %02x%02x%02x (%d,%d) (%d,%d) (%d,%d)\n", unsigned(t.tpage), t.r0, t.g0, t.b0, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)));
        for (const auto& t : S->quads_) if (t.code & 2) std::fprintf(S->log, "  GT4 semi tp %04x rgb %02x%02x%02x (%d,%d) (%d,%d) (%d,%d) (%d,%d)\n", unsigned(t.tpage), t.r0, t.g0, t.b0, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)), int(float(t.x3)), int(float(t.y3)));
        for (const auto& t : S->fx_g3_) if (t.code & 2) std::fprintf(S->log, "  G3 semi rgb %02x%02x%02x (%d,%d) (%d,%d) (%d,%d)\n", t.r0, t.g0, t.b0, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)));
        for (const auto& t : S->shadow_prims_) if (t.code & 2) std::fprintf(S->log, "  F3 semi rgb %02x%02x%02x (%d,%d) (%d,%d) (%d,%d)\n", t.r0, t.g0, t.b0, int(float(t.x0)), int(float(t.y0)), int(float(t.x1)), int(float(t.y1)), int(float(t.x2)), int(float(t.y2)));
      }
      if (std::getenv("SF3N_SHOTSTAT")) std::fprintf(s_self->log, "FOTO %s cuadro %llu tris %zu quads %zu pgxp %u salas %zu seq %llu\n", path.c_str(), (unsigned long long)s_self->render_frame_, s_self->tris_.size(), s_self->quads_.size(), s_self->last_pgxp_, s_self->rcur_ ? s_self->rcur_->rooms.size() : 0, (unsigned long long)(s_self->rcur_ ? s_self->rcur_->sequence : 0));
    }
  }
  std::string shot_path_;
  unsigned last_pgxp_ = 0;
  bool takeShot(const char* path) {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(g_window, &w, &h);
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> px(std::size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(px.data() + std::size_t(y) * w * 3, 1, std::size_t(w) * 3, f);
    std::fclose(f);
    return true;
  }

  inline static const bool kOldStall = std::getenv("SF3N_TIRON_PS1") != nullptr;
  bool nativeActive() const {

    if (two_player_ && rcur_ && rcur_->mission && rcur_->mission->name.find("FOG2") != std::string::npos) return false;

    const bool stalled = !rhist_.empty() && field_no_ < rhist_.back()->field + 6 && !kOldStall;
    if (!(native_mode_ && rcur_ && rcur_->mission && rcur_->camera.valid && (menu_frozen_native_ || game_frozen_ || nowSeconds() - rcur_->time < 0.5 || stalled))) return false;
    if (menu_frozen_native_) return true;
    if (game_frozen_) return true;

    if (!rhist_.empty() && field_no_ >= rhist_.back()->field + 6 && render_time_ >= rhist_.back()->time) return false;
    if (disp_black_ && !menu_frozen_native_) return false;

    if (!rhist_.empty() && field_no_ >= rhist_.back()->field + 4 && rhist_.back()->tex_hash && tex_hash_ != rhist_.back()->tex_hash) return false;

    if (rhist_.size() < 2 && last_path_ != 0) return false;
    return true;
  }
  std::uint64_t snapshots() const { return snapshot_count_; }

 private:

  static std::map<std::string, std::string>& ini() {
    static std::map<std::string, std::string> values = [] {
      std::map<std::string, std::string> v;
      std::string path = iniPath();
      if (FILE* f = std::fopen(path.c_str(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
          std::string l(line);
          const auto eq = l.find('=');
          if (eq == std::string::npos || l[0] == '#' || l[0] == ';') continue;
          auto trim = [](std::string t) { while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back(); while (!t.empty() && t[0] == ' ') t.erase(0, 1); return t; };
          v[trim(l.substr(0, eq))] = trim(l.substr(eq + 1));
        }
        std::fclose(f);
      }
      return v;
    }();
    return values;
  }
  static std::string iniString(const char* key, const char* d) {
    if (auto v = ini().find(key); v != ini().end()) return v->second;
    return d;
  }

  static std::vector<std::string> splitBar(const std::string& v) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= v.size()) {
      auto q = v.find('|', p);
      if (q == std::string::npos) q = v.size();
      std::string t = v.substr(p, q - p);
      while (!t.empty() && t.front() == ' ') t.erase(t.begin());
      while (!t.empty() && t.back() == ' ') t.pop_back();
      if (!t.empty()) out.push_back(t);
      p = q + 1;
    }
    return out;
  }
  void loadBindings() {
    binds2_ = ctl::defaultsP2();
    if (auto it = ini().find("jugador2"); it != ini().end()) p2_source_ = it->second == "teclado" ? 1 : 0;
    for (int a = 0; a < ctl::kCount; ++a)
      if (auto it = ini().find(std::string("j2_teclas_") + ctl::kActions[a].id); it != ini().end()) {
        auto& row = binds2_.keys[std::size_t(a)];
        row.fill(0);
        std::size_t j = 0;
        for (const auto& n : splitBar(it->second)) if (int c = ctl::keyFromIniName(n); c && c < ctl::kMouseBase && j < row.size() && !ctl::reservedKey(c)) row[j++] = c;
      }
    for (int a = 0; a < ctl::kCount; ++a)
      if (auto it = ini().find(std::string("j2_mando_") + ctl::kActions[a].id); it != ini().end()) {
        auto& row = binds2_.pad[std::size_t(a)];
        row.fill(-1);
        std::size_t j = 0;
        for (const auto& n : splitBar(it->second)) if (int c = ctl::padFromIniName(n); c >= 0 && j < row.size()) row[j++] = c;
      }
    binds_ = ctl::defaults();
    bool anyKeys = false;
    for (int a = 0; a < ctl::kCount; ++a) {
      const std::string id = ctl::kActions[a].id;
      if (auto it = ini().find("teclas_" + id); it != ini().end()) {
        anyKeys = true;
        auto& row = binds_.keys[std::size_t(a)];
        row.fill(0);
        std::size_t j = 0;
        for (const auto& n : splitBar(it->second)) if (int c = ctl::keyFromIniName(n); c && j < row.size() && !(c < ctl::kMouseBase && ctl::reservedKey(c))) row[j++] = c;
      }
      if (auto it = ini().find("mando_" + id); it != ini().end()) {
        auto& row = binds_.pad[std::size_t(a)];
        row.fill(-1);
        std::size_t j = 0;
        for (const auto& n : splitBar(it->second)) if (int c = ctl::padFromIniName(n); c >= 0 && j < row.size()) row[j++] = c;
      }
    }

    if (!anyKeys) {
      const std::pair<const char*, int> old[] = {{"raton_izquierdo", SDL_BUTTON_LEFT}, {"raton_derecho", SDL_BUTTON_RIGHT}, {"raton_medio", SDL_BUTTON_MIDDLE}};
      for (const auto& [key, btn] : old) {
        auto it = ini().find(key);
        if (it == ini().end()) continue;
        const auto bits = buttonBits(it->second);
        for (int a = 0; a < ctl::kCount; ++a)
          if (ctl::kActions[a].bit == bits && bits) { ctl::assign(binds_.keys, a, ctl::kMouseBase + btn, 0); break; }
      }
    }
  }
  void saveBindings(int action) const {
    std::string k, pd;
    for (int c : binds_.keys[std::size_t(action)]) if (c) { if (!k.empty()) k += "|"; k += ctl::keyIniName(c); }
    for (int c : binds_.pad[std::size_t(action)]) if (c >= 0) { if (!pd.empty()) pd += "|"; pd += ctl::padIniName(c); }
    iniSave(std::string("teclas_") + ctl::kActions[action].id, k);
    iniSave(std::string("mando_") + ctl::kActions[action].id, pd);
  }
  void saveAllBindings() const {
    for (int a = 0; a < ctl::kCount; ++a) saveBindings(a);
    for (int a = 0; a < ctl::kCount; ++a) {
      std::string k;
      for (int c : binds2_.keys[std::size_t(a)]) if (c) { if (!k.empty()) k += "|"; k += ctl::keyIniName(c); }
      iniSave(std::string("j2_teclas_") + ctl::kActions[a].id, k);
      std::string pd;
      for (int c : binds2_.pad[std::size_t(a)]) if (c >= 0) { if (!pd.empty()) pd += "|"; pd += ctl::padIniName(c); }
      iniSave(std::string("j2_mando_") + ctl::kActions[a].id, pd);
    }
  }
  static std::uint16_t buttonBits(std::string n) {
    for (auto& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    static const std::map<std::string, std::uint16_t> m = {
        {"x", 0x4000}, {"equis", 0x4000}, {"cuadrado", 0x8000}, {"circulo", 0x2000}, {"triangulo", 0x1000},
        {"l1", 0x0400}, {"r1", 0x0800}, {"l2", 0x0100}, {"r2", 0x0200}, {"start", 0x0008}, {"select", 0x0001}, {"ninguno", 0}};
    if (auto it = m.find(n); it != m.end()) return it->second;
    return 0;
  }
  static int envInt(const char* n, int d) {
    if (const char* e = std::getenv(n)) return std::atoi(e);
    static const std::map<std::string, std::string> names = {
        {"SF3N_W", "resolucion_ancho"}, {"SF3N_H", "resolucion_alto"}, {"SF3N_MSAA", "msaa"},
        {"SF3N_BILINEAR", "filtrado"}, {"SF3N_43", "solo_4_3"}, {"SF3N_VSYNC", "vsync"},
        {"SF3N_WINW", "ventana_ancho"}, {"SF3N_WINH", "ventana_alto"}, {"SF3N_FULL", "pantalla_completa"},
        {"SF3N_MOUSE", "raton"}, {"SF3N_MOUSE_SENS", "raton_sensibilidad"}, {"SF3N_MOUSE_INV", "raton_invertir_y"}, {"SF3N_DELAY_MS", "retraso_ms"}, {"SF3N_AUDIO_DELAY_MS", "retraso_sonido_ms"}, {"SF3N_MOUSE_TURN", "raton_giro_directo"}, {"SF3N_FPS", "fps_max"}, {"SF3N_SOMBRAS", "sombras"}, {"SF3N_TURBO", "turbo_cargas"}, {"SF3N_CD_RAPIDO", "cd_rapido"}, {"SF3N_VIBRACION", "vibracion"}, {"SF3N_VIDEOS_AMPLIADOS", "videos_ampliados"}, {"SF3N_TRILINEAL", "trilineal"}, {"SF3N_ANISO", "anisotropico"}, {"SF3N_SMAA", "smaa"}, {"SF3N_HUD_TAMANO", "hud_tamano"}, {"SF3N_HUD_BORDES", "hud_bordes"}, {"SF3N_BOTONES_REDONDOS", "botones_redondos"}, {"SF3N_NIEVE", "nieve"}, {"SF3N_VAHO", "vaho"}};
    if (auto it = names.find(n); it != names.end())
      if (auto v = ini().find(it->second); v != ini().end()) return std::atoi(v->second.c_str());
    return d;
  }

  static void setVideoMode(int w, int h, bool reset) {
    SetVideoMode(MODE_NTSC);
    if (reset) ResetGraph(0);
    DISPENV disp{};
    DRAWENV draw{};
    SetDefDispEnv(&disp, 0, 0, w, h);
    SetDefDrawEnv(&draw, 0, 0, w, h);
    draw.isbg = 1;
    draw.dtd = 0;
    setRGB0(&draw, 0, 0, 0);
    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);
  }

  std::optional<std::uint16_t> identifyRoom(std::uint32_t a0, std::span<const std::byte> ram) {
    const std::uint32_t base = a0 & 0x1fffffu;
    auto score = [&](std::size_t r) {
      const auto& sig = mission_->signatures[r];
      int hit = 0;
      for (const auto& [off, chunk] : sig.chunks) {
        if (base + off + 32 > ram.size()) continue;
        if (std::memcmp(ram.data() + base + off, chunk.data(), 32) == 0) ++hit;
      }
      return sig.chunks.empty() ? 0.0 : double(hit) / double(sig.chunks.size());
    };

    auto fullScore = [&](std::size_t r) {
      const auto& b = mission_->room_bytes[r];
      int hit = 0, n = 0;
      for (std::size_t off = 0; off + 32 <= b.size(); off += 96) {
        if (base + off + 32 > ram.size()) break;
        ++n; if (std::memcmp(ram.data() + base + off, b.data() + off, 32) == 0) ++hit;
      }
      return n ? double(hit) / n : 0.0;
    };
    auto untie = [&](std::size_t r) {
      if (r >= mission_->sig_ties.size() || mission_->sig_ties[r].empty()) return r;
      std::size_t best = r; double bs = fullScore(r);
      for (auto t : mission_->sig_ties[r]) { const double sc = fullScore(t); if (sc > bs) { bs = sc; best = t; } }
      if (static const bool rl = std::getenv("SF3N_SALALOG") != nullptr; rl && best != r) std::fprintf(log, "SALA %08x: firma igual %zu -> %zu (%.2f)\n", a0, r, best, bs);
      return best;
    };
    double cachedScore = -1.0;
    if (auto it = room_cache_.find(a0); it != room_cache_.end()) {
      cachedScore = score(it->second);
      if (cachedScore >= 0.95) { const auto u = untie(it->second); it->second = u; return std::uint16_t(u); }
      room_cache_.erase(it);
    }
    double best = 0.0;
    std::size_t bestRoom = 0;
    for (std::size_t r = 0; r < mission_->rooms.size(); ++r) {
      const double sc = score(r);
      if (sc > best) { best = sc; bestRoom = r; }
    }
    if (best < 0.35) return std::nullopt;
    if (static const bool rl = std::getenv("SF3N_SALALOG") != nullptr; rl && cachedScore >= 0) std::fprintf(log, "SALA %08x: cache %.2f -> sala %zu (%.2f)\n", a0, cachedScore, bestRoom, best);
    bestRoom = untie(bestRoom);
    room_cache_[a0] = bestRoom;
    return std::uint16_t(bestRoom);
  }

  std::optional<std::size_t> identifyModel(std::uint32_t a0) {
    if ((a0 & 0x1fffffffu) - 0x00600000u < 0x00100000u) {
      if (auto it = bank_model_index_.find(a0 | 0x80000000u); it != bank_model_index_.end()) return it->second;
      return std::nullopt;
    }
    if (auto it = model_cache_.find(a0); it != model_cache_.end()) {

      if (it->second < 0 && (field_count_ & 15) != 0) return std::nullopt;
      if (it->second < 0) { model_cache_.erase(it); return identifyModel(a0); }
      if (scoreSig(mission_->models[std::size_t(it->second)].signature, a0) >= 0.6) return std::size_t(it->second);
      model_cache_.erase(it);
    }
    double best = 0.0;
    int bestModel = -1;
    for (std::size_t i = 0; i < mission_->models.size(); ++i) {
      const double sc = scoreSig(mission_->models[i].signature, a0);
      if (sc > best) { best = sc; bestModel = int(i); }
    }
    if (best < 0.6) bestModel = -1;
    model_cache_[a0] = bestModel;
    if (bestModel < 0) return std::nullopt;
    return std::size_t(bestModel);
  }
  double scoreSig(const RoomSignature& sig, std::uint32_t a0) const {
    const std::uint32_t base = a0 & 0x1fffffu;
    int hit = 0;
    for (const auto& [off, chunk] : sig.chunks) {
      if (base + off + 32 > ram_.size()) continue;
      if (std::memcmp(ram_.data() + base + off, chunk.data(), 32) == 0) ++hit;
    }
    return sig.chunks.empty() ? 0.0 : double(hit) / double(sig.chunks.size());
  }

  void finalize() {
    building_ = false;
    if (!have_camera_) { eff_raw_.clear(); return; }

    if (player_ptr_addr_ && !ram_.empty()) {
      auto r32 = [&](std::uint32_t a) { const auto o = a & 0x1fffffu; std::uint32_t v = 0; if (o + 4 <= ram_.size()) std::memcpy(&v, ram_.data() + o, 4); return v; };
      const auto pl = r32(player_ptr_addr_);
      if ((pl >> 24) == 0x80u) { pending_.aim_yaw = double(std::int32_t(r32(pl + 0x220))); pending_.aim_pitch = double(std::int32_t(r32(pl + 0x224))); pending_.aim_valid = true; }
      { std::lock_guard lk(look_mtx_); pending_.aim_cy = double(taken_yaw_); pending_.aim_cp = double(taken_pitch_); }
      if (const std::uint32_t C = cam_struct_.load(); C) {
        pending_.cam_point = {double(std::int32_t(r32(C + 0x3d8))), -double(std::int32_t(r32(C + 0x3dc))), double(std::int32_t(r32(C + 0x3e0)))};
        pending_.cam_point_valid = true;
      }
    }
    {
      EffectMap em;
      em.fx = pending_.camera.fx; em.fy = pending_.camera.fy;
      em.cam = pending_.camera;

      const auto& cam = pending_.camera;

      auto ctlDet = [](const Ctl& c) {
        auto e = [&](int i) { const std::uint32_t w = c[std::size_t(i / 2)]; return double(std::int16_t((i & 1) ? (w >> 16) : (w & 0xffff))) / 4096.0; };
        const double m[9] = {e(0), e(1), e(2), e(3), e(4), e(5), e(6), e(7), e(8)};
        return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
      };
      std::map<std::array<std::uint32_t, 3>, double> farDepth;
      {
        struct G { double sx = 0, sy = 0, sz = 0; int n = 0; };
        std::map<std::array<std::uint32_t, 3>, G> groups;
        std::vector<char> bb(eff_raw_.size(), 0);
        for (std::size_t i = 0; i < eff_raw_.size(); ++i) {
          const auto& p = eff_raw_[i];
          if (p.actor || p.sz == 0 || std::fabs(ctlDet(p.ctl)) > 1e-3 || std::int32_t(p.ctl[7]) <= 0 || std::int32_t(p.ctl[7]) > 200) continue;
          bb[i] = 1;
          auto& g = groups[{p.ctl[5], p.ctl[6], p.ctl[7]}];
          g.sx += double(std::int16_t(p.sxy & 0xffff)); g.sy += double(std::int16_t(p.sxy >> 16)); g.sz += p.sz; ++g.n;
        }
        for (auto& [k, g] : groups) {
          const double cx = g.sx / g.n, cy = g.sy / g.n, gz = g.sz / g.n;
          double best = 10.0 * 10.0, bz = 0;
          for (std::size_t i = 0; i < eff_raw_.size(); ++i) {
            const auto& q = eff_raw_[i];
            if (bb[i] || q.actor || q.sz == 0) continue;
            const double dx = double(std::int16_t(q.sxy & 0xffff)) - cx, dy = double(std::int16_t(q.sxy >> 16)) - cy;
            if (dx * dx + dy * dy < best && q.sz > 3.0 * gz) { best = dx * dx + dy * dy; bz = q.sz; }
          }
          if (bz > 0) farDepth[k] = bz;
        }
      }
      for (const auto& p : eff_raw_) {
        if (p.sz == 0) continue;
        if (!farDepth.empty() && !p.actor)
          if (auto it = farDepth.find({p.ctl[5], p.ctl[6], p.ctl[7]}); it != farDepth.end() && std::fabs(ctlDet(p.ctl)) <= 1e-3) {
            const double sx = double(std::int16_t(p.sxy & 0xffff)), sy = double(std::int16_t(p.sxy >> 16));
            const double pz = it->second / cam.sz_scale;
            const double c[3] = {(sx - p.ofx) * pz / cam.fx, (sy - p.ofy) * pz / cam.fy, pz};
            std::array<double, 3> w{};
            for (int j = 0; j < 3; ++j) w[std::size_t(j)] = cam.eye[std::size_t(j)] + cam.rot[std::size_t(j)] * c[0] + cam.rot[std::size_t(3 + j)] * c[1] + cam.rot[std::size_t(6 + j)] * c[2];
            em.pts[p.sxy & 0x07ff07ffu] = {w, it->second};
            em.all.emplace(p.sxy & 0x07ff07ffu, w);
            continue;
          }
        if (p.actor) {
          if (const auto xf = worldFromGuest(pending_ctl_, p.ctl)) em.actor_pts[p.sxy & 0x07ff07ffu] = xf->apply(p.v[0], p.v[1], p.v[2]);
          continue;
        }
        if (p.light) {

          {

            const double R[9] = {double(lo16(p.ctl[0])), double(hi16(p.ctl[0])), double(lo16(p.ctl[1])), double(hi16(p.ctl[1])), double(lo16(p.ctl[2])), double(hi16(p.ctl[2])), double(lo16(p.ctl[3])), double(hi16(p.ctl[3])), double(lo16(p.ctl[4]))};
            const double T[3] = {double(std::int32_t(p.ctl[5])), double(std::int32_t(p.ctl[6])), double(std::int32_t(p.ctl[7]))};
            double q[3];
            for (int i = 0; i < 3; ++i) q[i] = T[i] + (R[i * 3] * p.v[0] + R[i * 3 + 1] * p.v[1] + R[i * 3 + 2] * p.v[2]) / 4096.0;

            const double n0 = std::sqrt(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]) / 4096.0, n1 = std::sqrt(R[3] * R[3] + R[4] * R[4] + R[5] * R[5]) / 4096.0, n2 = std::sqrt(R[6] * R[6] + R[7] * R[7] + R[8] * R[8]) / 4096.0;
            if (q[2] > 1.0 && n0 > 0 && n1 > 0 && n2 > 0) {
              const double qc[3] = {q[0] / n0, q[1] / n1, q[2] / n2};
              std::array<double, 3> w{};
              for (int j = 0; j < 3; ++j) w[std::size_t(j)] = cam.eye[std::size_t(j)] + cam.rot[std::size_t(j)] * qc[0] + cam.rot[std::size_t(3 + j)] * qc[1] + cam.rot[std::size_t(6 + j)] * qc[2];
              if (static const bool lm = std::getenv("SF3N_LUZMED") != nullptr; lm) {
                const double psx = double(p.h) * q[0] / q[2] + p.ofx, psy = double(p.h) * q[1] / q[2] + p.ofy;
                std::fprintf(log, "GTE luz: pixel del juego %d,%d  formula %.1f,%.1f  (H %u)\n", int(std::int16_t(p.sxy & 0xffff)), int(std::int16_t(p.sxy >> 16)), psx, psy, p.h);
              }
            double c[3];
            for (int i = 0; i < 3; ++i) c[i] = cam.rot[std::size_t(i * 3)] * (w[0] - cam.eye[0]) + cam.rot[std::size_t(i * 3 + 1)] * (w[1] - cam.eye[1]) + cam.rot[std::size_t(i * 3 + 2)] * (w[2] - cam.eye[2]);
            if (c[2] > 1.0) {
              const double px = cam.fx * c[0] / c[2], py = cam.fy * c[1] / c[2];
              const double sx = double(std::int16_t(p.sxy & 0xffff)) - p.ofx, sy = double(std::int16_t(p.sxy >> 16)) - p.ofy;

              auto rn = [](const Ctl& m, int r) { const double a[9] = {double(lo16(m[0])), double(hi16(m[0])), double(lo16(m[1])), double(hi16(m[1])), double(lo16(m[2])), double(hi16(m[2])), double(lo16(m[3])), double(hi16(m[3])), double(lo16(m[4]))}; return std::sqrt(a[r * 3] * a[r * 3] + a[r * 3 + 1] * a[r * 3 + 1] + a[r * 3 + 2] * a[r * 3 + 2]); };
              double scx = 1.0, scy = 1.0;
              {
                const double l0 = rn(p.ctl, 0), l1 = rn(p.ctl, 1), l2 = rn(p.ctl, 2), w0 = rn(pending_ctl_, 0), w1 = rn(pending_ctl_, 1), w2 = rn(pending_ctl_, 2);
                if (world_h_ && p.h && l2 > 0 && w0 > 0 && w1 > 0 && w2 > 0) {
                  scx = (double(p.h) * l0 / l2) / (double(world_h_) * w0 / w2);
                  scy = (double(p.h) * l1 / l2) / (double(world_h_) * w1 / w2);
                }
              }
              const double sc = scx;
              const int rx = int(std::int16_t(p.sxy & 0xffff)), ry = int(std::int16_t(p.sxy >> 16));
              const std::uint32_t kneg = (std::uint32_t(-rx) & 0x7ffu) | ((std::uint32_t(-ry) & 0x7ffu) << 16);
              em.lights.push_back({p.sxy & 0x07ff07ffu, kneg, w, sc, p.h, world_h_, px, py, sx, sy, scy});
            }
            }
          }
        }
        const double sx = double(std::int16_t(p.sxy & 0xffff)), sy = double(std::int16_t(p.sxy >> 16));
        std::array<double, 3> w{};
        bool ok = false;
        if (const auto xf = worldFromGuest(pending_ctl_, p.ctl)) {
          w = xf->apply(p.v[0], p.v[1], p.v[2]);
          if (static const char* fs = std::getenv("SF3N_FXSCALE"); fs) {
            static long a = -1, b = -1; if (a < 0) std::sscanf(fs, "%ld:%ld", &a, &b);
            if (long(field_count_) >= a && long(field_count_) <= b) {
              const auto& A = xf->a;
              const double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
              std::fprintf(log, "FXSCALE campo %llu det %.4f v %d %d %d w %.0f %.0f %.0f sz %u sxy %d,%d T %d %d %d camT %d %d %d\n", (unsigned long long)field_count_, det, p.v[0], p.v[1], p.v[2], w[0], w[1], w[2], p.sz, int(std::int16_t(p.sxy & 0xffff)), int(std::int16_t(p.sxy >> 16)), std::int32_t(p.ctl[5]), std::int32_t(p.ctl[6]), std::int32_t(p.ctl[7]), std::int32_t(pending_ctl_[5]), std::int32_t(pending_ctl_[6]), std::int32_t(pending_ctl_[7]));
            }
          }
          double c[3];
          for (int i = 0; i < 3; ++i) c[i] = cam.rot[i * 3] * (w[0] - cam.eye[0]) + cam.rot[i * 3 + 1] * (w[1] - cam.eye[1]) + cam.rot[i * 3 + 2] * (w[2] - cam.eye[2]);
          if (c[2] > 1.0) {
            const double px = p.ofx + cam.fx * c[0] / c[2], py = p.ofy + cam.fy * c[1] / c[2];
            ok = std::fabs(px - sx) <= 1.6 && std::fabs(py - sy) <= 1.6;
          }
        }
        if (!ok) {
          const double pz = double(p.sz) / cam.sz_scale;
          const double c[3] = {(sx - p.ofx) * pz / cam.fx, (sy - p.ofy) * pz / cam.fy, pz};
          for (int j = 0; j < 3; ++j) w[std::size_t(j)] = cam.eye[std::size_t(j)] + cam.rot[std::size_t(j)] * c[0] + cam.rot[std::size_t(3 + j)] * c[1] + cam.rot[std::size_t(6 + j)] * c[2];
          ++eff_unprojected_;
        } else {
          ++eff_exact_;
        }
        em.pts[p.sxy & 0x07ff07ffu] = {w, double(p.sz)};
        em.all.emplace(p.sxy & 0x07ff07ffu, w);
        if (!em.of_ok) { em.ofx = p.ofx; em.ofy = p.ofy; em.of_ok = true; }
      }
      if (static const bool ld = std::getenv("SF3N_LUZDIF") != nullptr; ld) { std::vector<std::pair<std::uint32_t, std::uint32_t>> v; for (const auto& p : eff_raw_) v.push_back({p.sxy & 0x07ff07ffu, p.ra}); dbg_ra_hist_.push_back(std::move(v)); if (dbg_ra_hist_.size() > 24) dbg_ra_hist_.erase(dbg_ra_hist_.begin()); }
      eff_raw_.clear();
      if (!em.lights.empty()) { std::vector<LightHistPt> v; for (const auto& L : em.lights) v.push_back({L.key, L.keyNeg, L.w, L.s, L.h, L.wh, L.px, L.py, L.sx, L.sy, L.scy}); light_hist_.push_back(std::move(v)); if (light_hist_.size() > 24) light_hist_.erase(light_hist_.begin()); }
      eff_maps_.push_back(std::move(em));
      if (eff_maps_.size() > 3) eff_maps_.erase(eff_maps_.begin());
    }

    for (const auto& ra : pending_actors_) {
      const auto mi = identifyModel(ra.a0);
      if (!mi) continue;
      ActorPose pose;
      pose.a0 = ra.a0;
      pose.model = *mi;
      const auto* model = &mission_->models[*mi];
      pose.parts.assign(model->part_vertex_offsets.size(), std::nullopt);
      pose.lights.assign(model->part_vertex_offsets.size(), Light{});
      for (const auto& rp : ra.parts) {
        const auto& offs = model->part_vertex_offsets;
        const auto it = std::find(offs.begin(), offs.end(), rp.offset);
        if (it == offs.end()) continue;
        pose.parts[std::size_t(it - offs.begin())] = worldFromGuest(pending_ctl_, rp.ctl);
        pose.lights[std::size_t(it - offs.begin())] = rp.light;
      }
      if (static const bool partlog = std::getenv("SF3N_PARTLOG") != nullptr; partlog) {
        static int pl = 0;
        if (pl++ < 400 && model->full_variant >= 0) {
          std::fprintf(log, "piezas %s: modelo", model->name.c_str());
          for (auto o : model->part_vertex_offsets) std::fprintf(log, " %x", o);
          std::fprintf(log, " | capturadas");
          for (const auto& rp : ra.parts) std::fprintf(log, " %x", rp.offset);
          std::fprintf(log, "\n");
        }
      }
      if (model->full_variant < 0 || std::getenv("SF3N_NOFULL")) {
        if (const auto* hmd = std::get_if<sf::assets::HmdModel>(&model->geometry)) {

          pose.vertex_sxy.assign(hmd->vertices().size(), 0u);
          const auto& hp = hmd->parts();
          for (const auto& [off, sxy] : ra.vsxy) {
            for (std::size_t pi = 0; pi < hp.size() && pi < model->part_vertex_offsets.size(); ++pi) {
              const std::uint32_t base = model->part_vertex_offsets[pi];
              if (off < base) continue;
              const std::uint32_t j = (off - base) / 8;
              if ((off - base) % 8 != 0 || j >= hp[pi].vertex_count) continue;
              const std::size_t vi = hp[pi].first_vertex + j;
              if (vi < pose.vertex_sxy.size()) pose.vertex_sxy[vi] = sxy;
              break;
            }
          }
        }
      }
      if (static const bool ll = std::getenv("SF3N_LODLOG") != nullptr; ll) {
        const Affine* r0 = !pose.parts.empty() && pose.parts[0] ? &*pose.parts[0] : nullptr;
        std::fprintf(log, "LOD %llu a0 %08x %s raiz %.0f %.0f %.0f\n", (unsigned long long)field_count_, ra.a0, model->name.c_str(), r0 ? r0->b[0] : 0.0, r0 ? r0->b[1] : 0.0, r0 ? r0->b[2] : 0.0);
      }
      if (model->full_variant >= 0 && !std::getenv("SF3N_NOFULL")) pose.model = std::size_t(model->full_variant);
      if (wound_diag_ && !pose.vertex_sxy.empty())
        if (const auto* hmd = std::get_if<sf::assets::HmdModel>(&mission_->models[*mi].geometry))
          for (const auto& tri : hmd->triangles()) {
            const auto a = pose.vertex_sxy[tri.vertex_indices[0]], b = pose.vertex_sxy[tri.vertex_indices[1]], c = pose.vertex_sxy[tri.vertex_indices[2]];
            if (a && b && c) diag_tris_.insert(triKey(a, b, c));
          }
      pending_.actors.push_back(std::move(pose));
    }
    std::map<std::uint32_t, int> seen;

    if (static const bool al = std::getenv("SF3N_ARMALOG") != nullptr; al && !ram_.empty()) {
      static int lastId = -1; static long cnt = 0;
      const int id = int(std::to_integer<unsigned>(ram_[0x12d880]));
      if (id != lastId || (++cnt % 100) == 0) {
        lastId = id;
        std::fprintf(log, "ARMA campo %llu id %d objetos:", (unsigned long long)game_field_.load(), id);
        for (const auto& ro : pending_objects_) { if (const auto mi = identifyModel(ro.a0)) std::fprintf(log, " %s", mission_->models[*mi].name.c_str()); else if ((ro.a0 & 0x1fffffffu) - 0x00600000u < 0x00100000u) std::fprintf(log, " ?%08x", ro.a0); }
        for (const auto& ra : pending_actors_) if ((ra.a0 & 0x1fffffffu) - 0x00600000u < 0x00100000u) { const auto mi = identifyModel(ra.a0); std::fprintf(log, " ACTOR %08x %s partes %zu", ra.a0, mi ? mission_->models[*mi].name.c_str() : "?", ra.parts.size()); }
        std::fprintf(log, "\n");
      }
    }
    for (const auto& ro : pending_objects_) {
      const auto mi = identifyModel(ro.a0);
      if (!mi) continue;
      const auto xf = worldFromGuest(pending_ctl_, ro.ctl);
      if (static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr; bl && field_count_ >= 2600 && (ro.a0 & 0x1fffffffu) - 0x00600000u < 0x00100000u) {
        static int nb = 0;
        if (nb++ < 60) std::fprintf(log, "BANCO cuadro campo %llu %s xf %d objetos %zu\n", (unsigned long long)field_count_, mission_->models[*mi].name.c_str(), int(bool(xf)), pending_objects_.size());
      }
      if (!xf) continue;
      if (static const bool ol = std::getenv("SF3N_OBJPROJ") != nullptr; ol && field_count_ >= 54 && field_count_ <= 56 && mission_->models[*mi].name.rfind("SCONCE", 0) == 0)
        if (const auto* g = std::get_if<sf::assets::GmdModel>(&mission_->models[*mi].geometry)) {
          const auto& cam = pending_.camera;
          std::fprintf(log, "OBJPROJ %s campo %llu a=[%.2f %.2f %.2f/%.2f %.2f %.2f/%.2f %.2f %.2f] ctl0 %08x %08x %08x %08x %08x:", mission_->models[*mi].name.c_str(), (unsigned long long)field_count_, xf->a[0], xf->a[1], xf->a[2], xf->a[3], xf->a[4], xf->a[5], xf->a[6], xf->a[7], xf->a[8], ro.ctl[0], ro.ctl[1], ro.ctl[2], ro.ctl[3], ro.ctl[4]);
          for (const auto& t : g->triangles()) {
            std::fprintf(log, " tp%x:", t.texture_page);
            for (int k = 0; k < 3; ++k) {
              const auto& v = g->vertices()[t.vertex_indices[std::size_t(k)]];
              const auto w = xf->apply(v.x, v.y, v.z);
              double c[3];
              for (int i = 0; i < 3; ++i) c[i] = cam.rot[std::size_t(i) * 3] * (w[0] - cam.eye[0]) + cam.rot[std::size_t(i) * 3 + 1] * (w[1] - cam.eye[1]) + cam.rot[std::size_t(i) * 3 + 2] * (w[2] - cam.eye[2]);
              std::fprintf(log, " (%.0f,%.0f)", c[2] > 1 ? cam.fx * c[0] / c[2] : 0.0, c[2] > 1 ? cam.fy * c[1] / c[2] : 0.0);
            }
          }
          std::fprintf(log, "\n");
        }
      if (static const long long olf = std::getenv("SF3N_OBJLOG") ? std::atoll(std::getenv("SF3N_OBJLOG")) : -1; olf >= 0 && (long long)game_field_ >= olf && (long long)game_field_ < olf + 3)
        std::fprintf(log, "OBJLOG campo %llu %s a0 %08x a1 %08x a=[%.3f %.3f %.3f/%.3f %.3f %.3f/%.3f %.3f %.3f] b=[%.0f %.0f %.0f] ctl %08x %08x %08x %08x %08x %08x %08x %08x\n", (unsigned long long)field_count_, mission_->models[*mi].name.c_str(), ro.a0, ro.a1, xf->a[0], xf->a[1], xf->a[2], xf->a[3], xf->a[4], xf->a[5], xf->a[6], xf->a[7], xf->a[8], xf->b[0], xf->b[1], xf->b[2], ro.ctl[0], ro.ctl[1], ro.ctl[2], ro.ctl[3], ro.ctl[4], ro.ctl[5], ro.ctl[6], ro.ctl[7]);
      if (static const char* vd = std::getenv("SF3N_VERTDUMP"); vd && mission_->models[*mi].name.rfind(vd, 0) == 0) {
        static bool once = false;
        if (!once) { once = true;
          if (const auto* g = std::get_if<sf::assets::GmdModel>(&mission_->models[*mi].geometry)) {
            std::fprintf(log, "VERTDUMP %s %zu vertices:", mission_->models[*mi].name.c_str(), g->vertices().size());
            for (const auto& v : g->vertices()) std::fprintf(log, " (%d %d %d)", int(v.x), int(v.y), int(v.z));
            std::fprintf(log, "\n  camara del cuadro filas: %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f\n", pending_.camera.rot[0], pending_.camera.rot[1], pending_.camera.rot[2], pending_.camera.rot[3], pending_.camera.rot[4], pending_.camera.rot[5], pending_.camera.rot[6], pending_.camera.rot[7], pending_.camera.rot[8]);
          } } }
      if (static const long long olf = std::getenv("SF3N_OBJLOG") ? std::atoll(std::getenv("SF3N_OBJLOG")) : -1; olf >= 0 && (long long)game_field_ >= olf && (long long)game_field_ < olf + 3)
        if (const auto* g = std::get_if<sf::assets::GmdModel>(&mission_->models[*mi].geometry)) {
          const auto& cam = pending_.camera; std::fprintf(log, "  proy %s:", mission_->models[*mi].name.c_str());
          for (std::size_t vi = 0; vi < g->vertices().size() && vi < 8; ++vi) { const auto& v = g->vertices()[vi]; const auto w = xf->apply(v.x, v.y, v.z); double c[3];
            for (int i = 0; i < 3; ++i) c[i] = cam.rot[std::size_t(i) * 3] * (w[0] - cam.eye[0]) + cam.rot[std::size_t(i) * 3 + 1] * (w[1] - cam.eye[1]) + cam.rot[std::size_t(i) * 3 + 2] * (w[2] - cam.eye[2]);
            std::fprintf(log, " (%.0f,%.0f z%.0f)", c[2] > 1 ? cam.fx * c[0] / c[2] : 0.0, c[2] > 1 ? cam.fy * c[1] / c[2] : 0.0, c[2]); }
          std::fprintf(log, "\n"); }
      pending_.objects.push_back(ObjectPose{ro.a0, *mi, *xf, seen[ro.a0]++, ro.light, ro.a1});
    }

    pending_.time = std::chrono::duration<double>((fast ? Clock::now() : sched_ + field_period_).time_since_epoch()).count();
    pending_.sequence = ++snapshot_count_;
    pending_.tex_hash = emu_tex_hash_;
    pending_.field = field_count_;
    last_snapshot_field_ = field_count_;
    if (static const bool fl = std::getenv("SF3N_FOGFIT") != nullptr; fl && field_count_ % 25 == 0) std::fprintf(log, "NIEBLA campo %llu cerca %.0f lejos %.0f\n", (unsigned long long)field_count_, pending_.fog_near, pending_.fog_far);
    pending_.mission = mission_;
    if (emu_cur_ && emu_cur_->mission == mission_) matchPrevious(pending_, *emu_cur_);
    if (static const bool pj = std::getenv("SF3N_PARTJUMP") != nullptr; pj) {
      for (const auto& ac : pending_.actors) {
        std::fprintf(log, "PJ %llu %llu %s", (unsigned long long)field_count_, (unsigned long long)ac.id, mission_->models[ac.model].name.c_str());
        for (const auto& pt : ac.parts) { if (pt) std::fprintf(log, " %.0f,%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f", pt->b[0], pt->b[1], pt->b[2], pt->a[0], pt->a[3], pt->a[6], pt->a[1], pt->a[4], pt->a[7]); else std::fprintf(log, " -"); }
        std::fprintf(log, "\n");
      }
    }
    if (static const bool jl = std::getenv("SF3N_JUMPLOG") != nullptr; jl && emu_cur_) {
      for (const auto& a : pending_.actors) {
        const Affine* r = rootOf(a);
        if (a.prev < 0) { std::fprintf(log, "SALTO campo %llu %s@%08x sin anterior (raiz %.0f %.0f %.0f) actores %zu->%zu\n", (unsigned long long)field_count_, mission_->models[a.model].name.c_str(), a.a0, r ? r->b[0] : 0, r ? r->b[1] : 0, r ? r->b[2] : 0, emu_cur_->actors.size(), pending_.actors.size()); continue; }
        const Affine* q = rootOf(emu_cur_->actors[std::size_t(a.prev)]);
        if (r && q && dist2(*r, *q) > 80.0 * 80.0) std::fprintf(log, "SALTO campo %llu %s id %llu %.0f\n", (unsigned long long)field_count_, mission_->models[a.model].name.c_str(), (unsigned long long)a.id, std::sqrt(dist2(*r, *q)));
      }
      for (std::size_t j = 0; j < emu_cur_->actors.size(); ++j) {
        bool used = false; for (const auto& a : pending_.actors) used = used || a.prev == int(j);
        if (!used) std::fprintf(log, "SALTO campo %llu desaparece %s@%08x id %llu\n", (unsigned long long)field_count_, mission_->models[emu_cur_->actors[j].model].name.c_str(), emu_cur_->actors[j].a0, (unsigned long long)emu_cur_->actors[j].id);
      }
    }
    auto snap = std::make_shared<const Snapshot>(std::move(pending_));
    pending_ = Snapshot{};
    {
      std::lock_guard lk(mtx_);
      pub_hist_.push_back(snap);
      if (pub_hist_.size() > 8) pub_hist_.erase(pub_hist_.begin());
    }
    emu_cur_ = snap;
    const Snapshot& c = *snap;
    if (static const bool yawlog = std::getenv("SF3N_YAWLOG") != nullptr; yawlog) {
      for (const auto& ac : c.actors)
        if (mission_->models[ac.model].name.rfind("GABE", 0) == 0)
          if (const Affine* r = rootOf(ac)) std::fprintf(log, "YAW campo %llu a %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n", (unsigned long long)field_count_, r->a[0], r->a[1], r->a[2], r->a[3], r->a[4], r->a[5], r->a[6], r->a[7], r->a[8]);
    }
    if (snapshot_count_ == 1 || snapshot_count_ % (std::getenv("SF3N_SNAPLOG") ? 10 : 500) == 0) {
      std::fprintf(log, "nativo: cuadro %llu campo %llu salas", (unsigned long long)snapshot_count_, (unsigned long long)field_count_);
      for (auto r : c.rooms) std::fprintf(log, " %s", mission_->room_names[r].c_str());
      std::fprintf(log, " ojo %.0f %.0f %.0f f %.1f %.1f |", c.camera.eye[0], c.camera.eye[1], c.camera.eye[2], c.camera.fx, c.camera.fy);
      for (const auto& a : c.actors) std::fprintf(log, " %s@%08x", mission_->models[a.model].name.c_str(), a.a0);
      std::fprintf(log, " | %zu objetos (de %zu)", c.objects.size(), pending_objects_.size());
      if (std::getenv("SF3N_SNAPLOG")) {
        for (const auto& o : c.objects) std::fprintf(log, " %s@%08x/%x(%.0f,%.0f,%.0f)", mission_->models[o.model].name.c_str(), o.a0, o.draw_flags, o.xf.b[0], o.xf.b[1], o.xf.b[2]);
        for (const auto& a : c.actors) if (const Affine* r = rootOf(a)) std::fprintf(log, " actor(%.0f,%.0f,%.0f)", r->b[0], r->b[1], r->b[2]);
        std::fprintf(log, " | crudos");
        for (const auto& o : pending_objects_) std::fprintf(log, " %08x", o.a0);
      }
      std::fprintf(log, "\n");
    }
  }

  static const Affine* rootOf(const ActorPose& a) {
    for (const auto& p : a.parts) if (p) return &*p;
    return nullptr;
  }
  static double dist2(const Affine& x, const Affine& y) {
    const double dx = x.b[0] - y.b[0], dy = x.b[1] - y.b[1], dz = x.b[2] - y.b[2];
    return dx * dx + dy * dy + dz * dz;
  }
  static void matchPrevious(Snapshot& cur, const Snapshot& prev) {
    constexpr double maxJump = 700.0 * 700.0;

    struct Pair { double d; std::size_t i, j; };
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < cur.actors.size(); ++i) {
      const Affine* ra = rootOf(cur.actors[i]);
      if (!ra) continue;
      for (std::size_t j = 0; j < prev.actors.size(); ++j) {
        const auto& p = prev.actors[j];
        if (p.model != cur.actors[i].model) continue;
        const Affine* rp = rootOf(p);
        if (!rp) continue;
        double d = dist2(*ra, *rp);
        if (p.a0 != cur.actors[i].a0) d *= 1.5;
        if (d < maxJump) pairs.push_back({d, i, j});
      }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.d < y.d; });
    std::vector<char> usedC(cur.actors.size(), 0), used(prev.actors.size(), 0);
    for (const auto& pr : pairs) {
      if (usedC[pr.i] || used[pr.j]) continue;
      usedC[pr.i] = used[pr.j] = 1;
      cur.actors[pr.i].prev = int(pr.j);
      cur.actors[pr.i].id = prev.actors[pr.j].id;
    }
    static std::uint64_t nextId = 0;
    for (auto& a : cur.actors) if (a.id == 0) a.id = ++nextId;

    {
      static const double objJump = [] { const double v = envInt("SF3N_OBJ_SALTO", 150); return v * v; }();
      std::vector<std::tuple<double, std::size_t, std::size_t>> op;
      for (std::size_t i = 0; i < cur.objects.size(); ++i)
        for (std::size_t j = 0; j < prev.objects.size(); ++j) {
          if (prev.objects[j].a0 != cur.objects[i].a0 || prev.objects[j].model != cur.objects[i].model) continue;
          const double d = dist2(cur.objects[i].xf, prev.objects[j].xf);
          if (d < objJump) op.push_back({d, i, j});
        }
      std::sort(op.begin(), op.end());
      std::vector<char> usedC(cur.objects.size(), 0), usedO(prev.objects.size(), 0);
      for (const auto& [d, i, j] : op) {
        if (usedC[i] || usedO[j]) continue;
        usedC[i] = usedO[j] = 1; cur.objects[i].prev = int(j);
      }
    }
  }

 public:
  void setDiscId(const std::string& id) {
    disc_id_ = id;
  }
  const std::string& discId() const { return disc_id_; }
  std::string saveDir() const {
    const std::string d = dataDir() + SF3_PARTIDAS "/" + disc_id_ + "/";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
  }
  std::string quickPath() const { return saveDir() + "rapido.sf3s"; }
  std::string continuePath() const { return saveDir() + "continuar.sf3s"; }

  std::string autoPath() const { return saveDir() + "autoguardado.sf3s"; }
  std::string lang() const { return lang_start_; }
  std::string voices_lang() const {
    std::string v = iniString("idioma_voces", "");
    if (v != "en" && v != "es" && v != "ru") v = lang_ == "en" ? "en" : "es";
    return v;
  }

  std::string continueTarget() const {
    std::string best; std::time_t bt = 0;
    auto consider = [&](const std::string& p) { const auto m = readMeta(p); if (m.exists && m.when >= bt) { bt = m.when; best = p; } };
    consider(autoPath()); consider(continuePath());
    for (const auto& [fog, t] : readProgress()) consider(missionPath(fog));
    return best;
  }
  std::string slotPath(int n) const { return saveDir() + "ranura" + std::to_string(n) + ".sf3s"; }
  std::string missionPath(const std::string& fog) const { return saveDir() + "mision_" + fogKey(fog) + ".sf3s"; }
  static std::string fogKey(std::string fog) {
    if (auto sl = fog.find_last_of("/\\"); sl != std::string::npos) fog = fog.substr(sl + 1);
    if (auto dot = fog.find('.'); dot != std::string::npos) fog = fog.substr(0, dot);
    return fog;
  }
  static std::string missionName(const std::string& fog) {
    static const std::map<std::string, std::string> names = {
        {"TOKYO", "TOKIO"}, {"SENATE", "SENADO 1"}, {"SENATE2", "SENADO 2"},
        {"AFGHAN1", "AFGANISTÁN 1"}, {"AFGHAN2", "AFGANISTÁN 2"}, {"AFGHAN3", "AFGANISTÁN 3"},
        {"AFRICA1", "ÁFRICA 1"}, {"AFRICA2", "ÁFRICA 2"},
        {"JUNGLE", "JUNGLA 1"}, {"JUNGLE2", "JUNGLA 2"}, {"JUNGLE3", "JUNGLA 3"},
        {"LONDON1", "LONDRES 1"}, {"LONDON2", "LONDRES 2"}, {"LONDON3", "LONDRES 3"},
        {"MCAVES", "CUEVAS"}, {"RIDGE", "LA CRESTA"}, {"SNOWCAMP", "CAMPAMENTO NEVADO"},
        {"TRIAGE1", "HOSPITAL DE CAMPAÑA 1"}, {"TRIAGE2", "HOSPITAL DE CAMPAÑA 2"}};
    const std::string k = fogKey(fog);
    if (auto it = names.find(k); it != names.end()) return it->second;
    return k;
  }
  static std::string dateText(std::time_t t) {
    if (!t) return "";
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d/%02d/%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return b;
  }
  struct SaveMeta { bool exists = false; std::string fog; std::time_t when = 0; };
  static SaveMeta readMeta(const std::string& statePath) {
    SaveMeta m;
    std::error_code ec;
    m.exists = std::filesystem::exists(statePath, ec);
    if (!m.exists) return m;
    if (FILE* f = std::fopen((statePath + ".txt").c_str(), "r")) {
      char line[256];
      while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("mision=", 0) == 0) m.fog = l.substr(7);
        if (l.rfind("fecha=", 0) == 0) m.when = std::time_t(std::atoll(l.c_str() + 6));
      }
      std::fclose(f);
    }
    if (!m.when) {

      struct stat st{};
      if (::stat(statePath.c_str(), &st) == 0) m.when = st.st_mtime;
    }
    return m;
  }
  static std::string metaText(const SaveMeta& m) {
    if (!m.exists) return "VACÍA";
    std::string t = m.fog.empty() ? std::string("PARTIDA") : missionName(m.fog);
    if (m.when) t += "  -  " + dateText(m.when);
    return t;
  }

  void onStateSaved(const std::string& path, const std::string& fog) {
    if (FILE* f = std::fopen((path + ".txt").c_str(), "w")) {
      std::fprintf(f, "mision=%s\nfecha=%lld\n", fogKey(fog).c_str(), (long long)std::time(nullptr));
      std::fclose(f);
    }
  }
  void addProgress(const std::string& fog) {
    const std::string k = fogKey(fog);
    for (const auto& [f, t] : readProgress()) if (f == k) return;
    if (FILE* f = std::fopen((saveDir() + "progreso.txt").c_str(), "a")) { std::fprintf(f, "%s\t%lld\n", k.c_str(), (long long)std::time(nullptr)); std::fclose(f); }
  }
  std::vector<std::pair<std::string, std::time_t>> readProgress() const {
    std::vector<std::pair<std::string, std::time_t>> v;
    if (FILE* f = std::fopen((saveDir() + "progreso.txt").c_str(), "r")) {
      char line[256];
      while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const auto tab = l.find('\t');
        if (tab == std::string::npos) continue;
        v.push_back({l.substr(0, tab), std::time_t(std::atoll(l.c_str() + tab + 1))});
      }
      std::fclose(f);
    }
    return v;
  }
  void requestSave(const std::string& path) { std::lock_guard lk(req_mtx_); req_save_ = path; }
  void requestLoad(const std::string& path) { std::lock_guard lk(req_mtx_); req_load_ = path; }
  std::string takeSavePath() { std::lock_guard lk(req_mtx_); std::string p; p.swap(req_save_); return p; }
  std::string takeLoadPath() { std::lock_guard lk(req_mtx_); std::string p; p.swap(req_load_); return p; }

  bool recentWorld() const { return mission_ && field_count_ > 0 && field_count_ - last_snapshot_field_ < 25; }
  bool inMission() const { return mission_ && field_count_ > 0 && field_count_ - last_snapshot_field_ < 50 * 60; }
  static std::string iniGet(const char* key) { return iniString(key, ""); }

  static int xrayCfg(const char* key, const char* env, int def) {
    if (const char* e = std::getenv(env)) return std::atoi(e);
    const std::string v = iniString(key, "");
    if (v.empty()) { iniSave(key, std::to_string(def)); return def; }
    return std::atoi(v.c_str());
  }
  static bool xrayOwnView() {
    if (std::getenv("SF3N_MIRA_PS1")) return false;
    const std::string v = iniString("rayosx_vista", "");
    if (v.empty()) { iniSave("rayosx_vista", "propia"); return true; }
    return v != "ps1";
  }

  static bool xrayWorldView() {
    if (const char* e = std::getenv("SF3N_RAYOSX_VISTA")) return std::string(e) == "propia";
    return iniString("rayosx_vista", "propia") == "propia";
  }

  std::string actorModelName(std::uint32_t a0) const {
    if (!mission_ || !emu_cur_) return {};
    for (const auto& a : emu_cur_->actors)
      if (a.a0 == a0 && a.model >= 0 && std::size_t(a.model) < mission_->models.size()) return mission_->models[std::size_t(a.model)].name;
    return {};
  }

  std::string boot_load;

 private:
  std::string disc_id_ = "SCUS_946.40";
  std::mutex req_mtx_;
  std::string req_save_, req_load_, pending_mission_;
  bool in_state_load_ = false;
  struct LoadEntry { std::string text; std::string path; };
  std::vector<LoadEntry> load_entries_;
  void buildLoadEntries(bool missions) {
    load_entries_.clear();
    if (missions) {
      for (const auto& [fog, t] : readProgress()) {
        const std::string path = missionPath(fog);
        const auto m = readMeta(path);
        if (!m.exists) continue;
        load_entries_.push_back({missionName(fog) + "  -  " + dateText(m.when ? m.when : t), path});
      }
      return;
    }
    if (const auto m = readMeta(continuePath()); m.exists) load_entries_.push_back({"CONTINUAR: " + metaText(m), continuePath()});
    if (const auto m = readMeta(quickPath()); m.exists) load_entries_.push_back({"RÁPIDA (F5): " + metaText(m), quickPath()});
    for (int n = 1; n <= 6; ++n)
      if (const auto m = readMeta(slotPath(n)); m.exists) load_entries_.push_back({"RANURA " + std::to_string(n) + ": " + metaText(m), slotPath(n)});
  }

 public:

  static bool launcherEnabled() { return iniString("lanzador", "0") == "1"; }
  bool runLauncher(const std::vector<std::string>& cues, std::string& cue) {
    launcher_cues_ = cues;
    launcher_cue_ = 0;
    const std::string want = iniString("disco", "");
    for (std::size_t i = 0; i < cues.size(); ++i) if (cues[i] == cue || (!want.empty() && cues[i] == want)) launcher_cue_ = int(i);
    if (!cues.empty()) { setDiscId(discIdOf(cues[std::size_t(launcher_cue_)])); loadGameFontFromCue(cues[std::size_t(launcher_cue_)]); }
    launcher_ = true;
    launcher_result_ = 0;
    menu_open_ = true;
    menu_page_ = 10;
    menu_sel_ = 0;

    const int keepW = g_cfg_renderWidth, keepH = g_cfg_renderHeight, keepAspect = g_cfg_aspectMode;
    if (g_window && (!std::getenv("SF3N_TEST") || std::getenv("SF3N_LAUNCHERSHOT"))) {
      SDL_SetWindowFullscreen(g_window, 0);
      SDL_RestoreWindow(g_window);
      SDL_SetWindowResizable(g_window, SDL_FALSE);
      SDL_SetWindowSize(g_window, kLauncherW, kLauncherH);
      const int d = std::max(0, SDL_GetWindowDisplayIndex(g_window));
      SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
    }
    launcher_res_set_ = launcher_aspect_set_ = true;
    launcher_res_w_ = keepW; launcher_res_h_ = keepH; launcher_aspect_ = keepAspect;
    g_cfg_renderWidth = kLauncherW * 2; g_cfg_renderHeight = kLauncherH * 2;
    g_cfg_aspectMode = PSYX_ASPECT_ADAPTIVE;
    loadLauncherImage();
    while (launcher_result_ == 0) {
      if (g_psyx_quit_requested) { launcher_result_ = -1; break; }
      const auto t0 = Clock::now();
      if (PsyX_BeginScene() == 0) { PsyX_EndScene(); if (PsyX_BeginScene() == 0) continue; }
      handleKeys();
      if (quit) { launcher_result_ = -1; DrawSync(0); PsyX_EndScene(); break; }
      drawLauncherBackground();
      if (menu_page_ == 10) drawLauncherButtons(); else drawPcMenu();
      if (static const char* shot = std::getenv("SF3N_LAUNCHERSHOT"); shot) {
        static int fr = 0;
        ++fr;
        if (fr == 10) screenshot((std::string(shot) + "_botones.ppm").c_str());
        if (fr == 12) menuActivate(870, 0);
        if (fr == 20) screenshot((std::string(shot) + "_general.ppm").c_str());
        if (fr == 22) goPage(11);
        if (fr == 30) screenshot((std::string(shot) + "_graficos.ppm").c_str());
        if (fr == 32) { closeTitleOptions(); menu_sel_ = 2; }
        if (fr == 40) screenshot((std::string(shot) + "_salir.ppm").c_str());
        if (fr == 42) launcher_result_ = 1;
      }
      DrawSync(0);
      PsyX_EndScene();
      const auto dt = Clock::now() - t0;
      if (dt < std::chrono::milliseconds(8)) std::this_thread::sleep_for(std::chrono::milliseconds(8) - dt);
    }
    launcher_ = false;
    title_opts_ = false;
    menu_open_ = false;
    menu_page_ = 0;
    menu_sel_ = 0;

    g_cfg_aspectMode = launcher_aspect_set_ ? launcher_aspect_ : keepAspect;
    g_cfg_renderWidth = launcher_res_set_ ? launcher_res_w_ : keepW;
    g_cfg_renderHeight = launcher_res_set_ ? launcher_res_h_ : keepH;
    vram_uploaded_ = false; opc_uploaded_ = false;
    if (g_window && !std::getenv("SF3N_TEST") && launcher_result_ > 0) {
      SDL_SetWindowResizable(g_window, SDL_TRUE);
      if (full_) SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
      else applyResolution(g_cfg_renderWidth, g_cfg_renderHeight, false);
    }
    if (!launcher_cues_.empty()) cue = launcher_cues_[std::size_t(launcher_cue_)];
    if (launcher_result_ < 0) quit = true;
    return launcher_result_ > 0;
  }
  static std::string discIdOf(const std::string& cue) {
    try {
      auto img = sf::disc::Iso9660Image::open(cue);
      const auto cnf = img.readFile("SYSTEM.CNF");
      std::string t(reinterpret_cast<const char*>(cnf.data()), cnf.size());
      if (auto b = t.find("BOOT"); b != std::string::npos) {
        auto s0 = t.find('\\', b), e0 = t.find(';', b);
        if (s0 != std::string::npos && e0 != std::string::npos && e0 > s0) return t.substr(s0 + 1, e0 - s0 - 1);
      }
    } catch (const std::exception&) {}
    return "SCUS_946.40";
  }

 private:
  bool launcher_ = false;
  int launcher_result_ = 0;
  std::vector<std::string> launcher_cues_;
  int launcher_cue_ = 0;
  static std::string cueLabel(const std::string& path) {
    std::string n = path;
    if (auto sl = n.find_last_of("/\\"); sl != std::string::npos) n = n.substr(sl + 1);
    if (auto dot = n.rfind('.'); dot != std::string::npos) n = n.substr(0, dot);
    for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    return n;
  }
  static constexpr int kLauncherW = 720, kLauncherH = 480;
  static constexpr int kLauncherAlias = 64;
  bool launcher_img_ok_ = false;
  bool launcher_res_set_ = false, launcher_aspect_set_ = false;
  int launcher_res_w_ = 0, launcher_res_h_ = 0, launcher_aspect_ = 0;

  void loadLauncherImage() {
    launcher_img_ok_ = false;
    const auto& fondo = res::get("lanzador_fondo.bmp");
    SDL_Surface* raw = fondo.empty() ? nullptr : SDL_LoadBMP_RW(SDL_RWFromConstMem(fondo.data(), int(fondo.size())), 1);
    if (!raw) { std::fprintf(log, "lanzador: sin lanzador_fondo.bmp (%s)\n", SDL_GetError()); return; }
    SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(raw);
    if (!img) return;
    const int W = kLauncherW, H = kLauncherH;
    const double sc = std::max(double(W) / img->w, double(H) / img->h);
    const double ox = (img->w * sc - W) * 0.5, oy = (img->h * sc - H) * 0.5;
    std::vector<std::uint16_t> pix(std::size_t(W) * H);
    const auto* src = static_cast<const std::uint8_t*>(img->pixels);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const double fx = std::clamp((x + 0.5 + ox) / sc - 0.5, 0.0, img->w - 1.0), fy = std::clamp((y + 0.5 + oy) / sc - 0.5, 0.0, img->h - 1.0);
        const int x0 = int(fx), y0 = int(fy), x1 = std::min(x0 + 1, img->w - 1), y1 = std::min(y0 + 1, img->h - 1);
        const double ax = fx - x0, ay = fy - y0;
        int c[3];
        for (int k = 0; k < 3; ++k) {
          auto at = [&](int xx, int yy) { return double(src[yy * img->pitch + xx * 3 + k]); };
          c[k] = int(std::lround((at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay));
        }
        pix[std::size_t(y) * W + x] = std::uint16_t(0x8000 | (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10));
      }
    SDL_FreeSurface(img);
    std::vector<std::uint16_t> page(64 * 256);
    for (int ty = 0; ty < 2; ++ty)
      for (int tx = 0; tx < 12; ++tx) {
        for (int y = 0; y < 256; ++y)
          for (int x = 0; x < 64; ++x) {
            const int sx = std::clamp(tx * 62 + x - 1, 0, W - 1), sy = std::clamp(ty * 254 + y - 1, 0, H - 1);
            page[std::size_t(y) * 64 + x] = pix[std::size_t(sy) * W + sx];
          }
        GR_UploadVRAMAliasPage(kLauncherAlias + ty * 12 + tx, page.data());
      }
    launcher_img_ok_ = true;
  }

  static float launcherL() { return 192.0f - 192.0f * (float(kLauncherW) / kLauncherH) / (4.0f / 3.0f); }
  static float launcherR() { return 384.0f - launcherL(); }
  void drawLauncherBackground() {
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    const float L = launcherL(), Rr = launcherR();
    if (!launcher_img_ok_) {
      menuRect(L, 0, Rr, 240, 4, 8, 28, false);
      const std::string t = "SYPHON FILTER 3";
      menuText(t, 192.0f - textWidth(t, 1.6f) * 0.5f, 90.0f, 1.6f, 170, 195, 255);
      return;
    }
    const float kx = (Rr - L) / kLauncherW, ky = 240.0f / kLauncherH;
    for (int ty = 0; ty < 2; ++ty)
      for (int tx = 0; tx < 12; ++tx) {
        const int px0 = tx * 62, py0 = ty * 254;
        const int pw = std::min(62, kLauncherW - px0), ph = std::min(254, kLauncherH - py0);
        if (pw <= 0 || ph <= 0) continue;
        POLY_FT4 p{}; setPolyFT4(&p);
        setRGB0(&p, 128, 128, 128);
        const float x0 = L + px0 * kx, x1 = L + (px0 + pw) * kx, y0 = py0 * ky, y1 = (py0 + ph) * ky;
        setXY4(&p, x0, y0, x1, y0, x0, y1, x1, y1);
        setUV4(&p, 1, 1, 1 + pw, 1, 1, 1 + ph, 1 + pw, 1 + ph);
        p.tpage = WeaponBank::aliasTpage(kLauncherAlias + ty * 12 + tx, std::uint16_t(2u << 7));
        p.clut = 0;
        DrawPrim(&p);
      }
  }

  float launcherBtnX(int i) const { return 192.0f + float(i - 1) * 118.0f; }
  static constexpr float kBtnW = 54.0f, kBtnT = 212.0f, kBtnB = 228.0f;
  void drawLauncherButtons() {
    const float L = launcherL(), Rr = launcherR();
    DR_TPAGE t{}; SetDrawTPage(&t, 1, 0, GetTPage(0, 0, 0, 0)); DrawPrim(&t);
    menuRect(L, 204.0f, Rr, 240.0f, 0, 0, 0, true);
    menuRect(L, 204.0f, Rr, 240.0f, 0, 0, 0, true);
    menuRect(L, 204.0f, Rr, 204.6f, 60, 80, 180, false);
    const auto items = menuItems();
    for (int i = 0; i < int(items.size()) && i < 3; ++i) {
      const float cx = launcherBtnX(i);
      const bool on = i == menu_sel_;
      menuRect(cx - kBtnW, kBtnT, cx + kBtnW, kBtnB, on ? 90 : 40, on ? 120 : 50, on ? 255 : 140, false);
      menuRect(cx - kBtnW + 0.7f, kBtnT + 0.7f, cx + kBtnW - 0.7f, kBtnB - 0.7f, on ? 30 : 6, on ? 40 : 8, on ? 120 : 40, false);
      const float ts = 0.75f;
      menuText(items[std::size_t(i)].text, cx - textWidth(items[std::size_t(i)].text, ts) * 0.5f, kBtnT + 4.5f, ts, on ? 255 : 170, on ? 255 : 180, on ? 255 : 215);
    }
  }

  int launcherHit(int mx, int my) const {
    int ww = kLauncherW, wh = kLauncherH;
    if (g_window) SDL_GetWindowSize(g_window, &ww, &wh);
    const float L = launcherL(), Rr = launcherR();
    const float x = L + (Rr - L) * float(mx) / float(std::max(1, ww)), y = 240.0f * float(my) / float(std::max(1, wh));
    for (int i = 0; i < 3; ++i) if (std::fabs(x - launcherBtnX(i)) <= kBtnW && y >= kBtnT && y <= kBtnB) return i;
    return -1;
  }
  static int tabOf(int page) {
    switch (page) {
      case 11: return 1;
      case 12: case 3: case 4: case 5: return 2;
      default: return 0;
    }
  }
  static constexpr int kTabPages[3] = {10, 11, 12};

  static std::vector<std::pair<int, int>> monitorModes() {
    std::vector<std::pair<int, int>> v;
    const int d = g_window ? std::max(0, SDL_GetWindowDisplayIndex(g_window)) : 0;
    SDL_DisplayMode dm{};
    if (SDL_GetDesktopDisplayMode(d, &dm) == 0) v.push_back({dm.w, dm.h});
    for (int i = 0, n = SDL_GetNumDisplayModes(d); i < n; ++i)
      if (SDL_GetDisplayMode(d, i, &dm) == 0 && dm.w >= 640 && dm.h >= 480 && (v.empty() || (dm.w <= v[0].first && dm.h <= v[0].second)))
        v.push_back({dm.w, dm.h});
    if (v.empty()) v.push_back({1280, 720});
    std::sort(v.begin() + 1, v.end(), [](const auto& a, const auto& b) { return a.first * a.second > b.first * b.second; });
    v.erase(std::unique(v.begin(), v.end()), v.end());
    for (std::size_t i = 1; i < v.size(); ++i) if (v[i] == v[0]) { v.erase(v.begin() + long(i)); break; }
    return v;
  }

  void applyResolution(int w, int h, bool save) {
    if (launcher_) {
      launcher_res_set_ = true; launcher_res_w_ = w; launcher_res_h_ = h;
      if (save) { iniSave("resolucion_ancho", std::to_string(w)); iniSave("resolucion_alto", std::to_string(h)); iniSave("ventana_ancho", std::to_string(w)); iniSave("ventana_alto", std::to_string(h)); }
      return;
    }
    g_cfg_renderWidth = w; g_cfg_renderHeight = h;
    if (g_window && !full_) {
      SDL_Rect ub{};
      const int d = std::max(0, SDL_GetWindowDisplayIndex(g_window));
      if (SDL_GetDisplayUsableBounds(d, &ub) == 0 && (w > ub.w || h > ub.h - 40)) SDL_MaximizeWindow(g_window);
      else { SDL_RestoreWindow(g_window); SDL_SetWindowSize(g_window, w, h); SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d)); }
    }
    if (save) {
      iniSave("resolucion_ancho", std::to_string(w)); iniSave("resolucion_alto", std::to_string(h));
      iniSave("ventana_ancho", std::to_string(w)); iniSave("ventana_alto", std::to_string(h));
    }
  }
  static int maxMsaa() {
    using Fn = void (*)(unsigned, int*);
    static int v = [] {
      int m = 8;
      if (auto f = reinterpret_cast<Fn>(SDL_GL_GetProcAddress("glGetIntegerv"))) { m = 0; f(0x8D57u, &m); }
      return m;
    }();
    return v;
  }

 public:
  void setTitleTop(bool on) { title_top_ = on; }

  void setNightVision(bool on) { night_vision_ = on; }
  std::atomic<bool> night_vision_{false};
  bool nv_flat_ = false;
  bool nv_tint_ = true;
  double feet_pull_ = 0, feet_low_ = 0;
  std::array<double, 3> cur_view_eye_{};
  std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::array<float, 3>>> light_ratio_;
  const void* light_ratio_mission_ = nullptr;
  std::map<std::pair<std::uint16_t, std::uint16_t>, std::uint64_t> light_ratio_sig_;
  std::map<std::uint16_t, std::array<double, 3>> room_luz_;
  const void* room_luz_mission_ = nullptr;
  int nv_age_ = 0;
  int takeTitleGo() { return title_go_.exchange(-1); }
  void setTitleSub(bool on) { title_sub_ = on; }
  int takeSubGo() { return sub_go_.exchange(-1); }
  bool takeSubBack() { return sub_back_.exchange(false); }
  bool takeTitleRestore() { return title_restore_.exchange(false); }
  std::string titlePath() { return saveDir() + "titulo.sf3s"; }
 private:
  void initTitleOptions() {
    camera_firm_ = (std::getenv("SF3N_CAMARA") ? std::string(std::getenv("SF3N_CAMARA")) : iniString("camara", "original")) == "firme";
    {
      const std::string mv = std::getenv("SF3N_MOVIMIENTO") ? std::string(std::getenv("SF3N_MOVIMIENTO")) : iniString("movimiento", "clasico");

      movement_modern_ = false; movement_free_ = mv == "libre" || mv == "moderno";
      if (!std::getenv("SF3N_CAMARA")) camera_firm_ = movement_free_;
    }
    { const std::string li = g_idioma_actual ? *g_idioma_actual : iniString("idioma", "es"); lang_ = lang_start_ = li == "en" ? "en" : li == "ru" ? "ru" : "es"; }
    vol_master_ = std::clamp(envInt("SF3N_VOLUMEN", std::atoi(iniString("volumen_general", "10").c_str())), 0, 10);
    vol_spu_ = std::clamp(std::atoi(iniString("volumen_musica", "10").c_str()), 0, 10);
    vol_cd_ = std::clamp(std::atoi(iniString("volumen_voces", "10").c_str()), 0, 10);
    mono_ = iniString("sonido_mono", "0") == "1";
    wheel_weapons_ = iniString("rueda_armas", "1") == "1";
    wheel_menu_hand_ = iniString("rueda_menu", "1") == "1";
    wheel_zoom_ = iniString("rueda_zoom", "1") == "1";
    wheel_invert_ = iniString("rueda_invertir", "0") == "1";
    wheel_in_btn_ = wheelBtnFromIni(iniString("rueda_acercar", "triangulo"), 7);
    wheel_out_btn_ = wheelBtnFromIni(iniString("rueda_alejar", "circulo"), 5);
    applyVolumes();
  }
  void applyVolumes() {
    sf::psx::g_vol_spu = vol_spu_ * 256 / 10;
    sf::psx::g_vol_cd = vol_cd_ * 256 / 10;
  }
  void openTitleOptions() {
    prev_menu_rb_ = 0xffff;
    menuSound(true);
    menu_frozen_native_ = false;
    title_opts_ = true;
    menu_open_ = true;
    goPage(30);
  }

  void openExitConfirm() {
    prev_menu_rb_ = 0xffff;
    menuSound(true);
    menu_frozen_native_ = false;
    title_opts_ = true;
    menu_open_ = true;
    goPage(39, 1);
  }
  void closeExitConfirm() { title_opts_ = false; closeMenu(); title_sel_ = 1; prev_title_raw_ = 0xffff; }
 public:
  double exit_fade_t_ = 0;
  std::atomic<int> exit_fade_vol_{256};
  static constexpr double kExitFade = 0.8;
 private:
  void closeTitleOptions() {
    if (launcher_) { title_opts_ = false; lang_start_ = lang_; goPage(10, 1); return; }
    if (lang_ != lang_start_ && menu_page_ != 36) { confirm_back_page_ = menu_page_; goPage(36, 0); return; }
    title_opts_ = false;
    closeMenu();
    title_sel_ = 1;
    prev_title_raw_ = 0xffff;
    title_restore_ = true;
  }
  static int titleTabOf(int page) { return page == 31 || page == 11 ? 1 : page == 32 ? 2 : ((page >= 33 && page <= 35) || page == 37) ? 3 : 0; }
  static constexpr int kTitleTabPages[4] = {30, 31, 32, 33};

  struct PolyTwin { std::uint16_t r, s; };
  const Mission* twins_mission_ = nullptr;
  std::vector<std::vector<std::vector<std::vector<PolyTwin>>>> twins_;

  std::vector<std::vector<std::vector<std::array<float, 3>>>> decal_n_;

  std::vector<std::vector<std::vector<float>>> buried_;

  const Mission* smooth_mission_ = nullptr;
  std::vector<std::vector<std::vector<std::array<std::array<float, 3>, 4>>>> smooth_;
  void buildSmooth(double R) {
    smooth_mission_ = &mis();
    const auto& rooms = mis().rooms;
    smooth_.assign(rooms.size(), {});
    struct C { float p[3], n[3], c[3]; std::uint16_t r, s; std::uint32_t pi; std::uint8_t k; };
    std::vector<C> all;
    for (std::size_t r = 0; r < rooms.size(); ++r) {
      const auto& secs = rooms[r].sections();
      smooth_[r].resize(secs.size());
      for (std::size_t si = 0; si < secs.size(); ++si) {
        const auto& sec = secs[si];
        smooth_[r][si].resize(sec.polygons.size());
        for (std::size_t pi = 0; pi < sec.polygons.size(); ++pi) {
          const auto& poly = sec.polygons[pi];
          const unsigned cnt = poly.quad ? 4u : 3u;
          bool ok = true;
          for (unsigned k = 0; k < cnt; ++k) ok &= poly.vertex_indices[k] < sec.vertices.size();
          auto& out = smooth_[r][si][pi];
          for (unsigned k = 0; k < cnt && ok; ++k) { const auto v = sec.vertices[poly.vertex_indices[k]].color; for (int ch = 0; ch < 3; ++ch) out[k][std::size_t(ch)] = float(((v >> (5 * ch)) & 31) << 3); }
          if (!ok || !poly.renderable || poly.degenerate) continue;
          const auto& v0 = sec.vertices[poly.vertex_indices[0]]; const auto& v1 = sec.vertices[poly.vertex_indices[1]]; const auto& v2 = sec.vertices[poly.vertex_indices[2]];
          const double a[3] = {double(v1.x - v0.x), double(v1.y - v0.y), double(v1.z - v0.z)}, b[3] = {double(v2.x - v0.x), double(v2.y - v0.y), double(v2.z - v0.z)};
          double n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
          const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
          if (len < 1e-6) continue;
          for (double& x : n) x /= len;
          for (unsigned k = 0; k < cnt; ++k) {
            const auto& v = sec.vertices[poly.vertex_indices[k]];
            C c{{float(v.x), float(v.y), float(v.z)}, {float(n[0]), float(n[1]), float(n[2])}, {out[k][0], out[k][1], out[k][2]}, std::uint16_t(r), std::uint16_t(si), std::uint32_t(pi), std::uint8_t(k)};
            all.push_back(c);
          }
        }
      }
    }
    const double cellSz = R;
    auto cell = [&](float x) { return std::int64_t(std::floor(double(x) / cellSz)); };
    auto key = [](std::int64_t x, std::int64_t y, std::int64_t z) { return (x & 0x1fffff) | ((y & 0x1fffff) << 21) | ((z & 0x1fffff) << 42); };
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid;
    for (std::uint32_t i = 0; i < all.size(); ++i) grid[key(cell(all[i].p[0]), cell(all[i].p[1]), cell(all[i].p[2]))].push_back(i);
    for (const auto& c : all) {
      double sw = 0, acc[3] = {0, 0, 0};
      const auto cx = cell(c.p[0]), cy = cell(c.p[1]), cz = cell(c.p[2]);
      for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
        if (it == grid.end()) continue;
        for (auto j : it->second) {
          const auto& o = all[j];
          if (c.n[0] * o.n[0] + c.n[1] * o.n[1] + c.n[2] * o.n[2] < 0.85f) continue;

          const double pd = std::fabs(double(c.n[0]) * (o.p[0] - c.p[0]) + double(c.n[1]) * (o.p[1] - c.p[1]) + double(c.n[2]) * (o.p[2] - c.p[2]));
          if (pd > 24.0) continue;
          const double d = std::sqrt(double(o.p[0] - c.p[0]) * (o.p[0] - c.p[0]) + double(o.p[1] - c.p[1]) * (o.p[1] - c.p[1]) + double(o.p[2] - c.p[2]) * (o.p[2] - c.p[2]));
          if (d >= R) continue;
          const double t = 1.0 - d / R, w = t * t;
          sw += w; for (int ch = 0; ch < 3; ++ch) acc[ch] += w * o.c[ch];
        }
      }
      if (sw > 0) { auto& out = smooth_[c.r][c.s][c.pi][c.k]; for (int ch = 0; ch < 3; ++ch) out[std::size_t(ch)] = float(acc[ch] / sw); }
    }
    if (log) std::fprintf(log, "luz suave: %zu esquinas, radio %.0f\n", all.size(), R);
  }
  void buildTwins() {
    twins_mission_ = &mis();
    const auto& rooms = mis().rooms;
    twins_.assign(rooms.size(), {});
    decal_n_.assign(rooms.size(), {});
    buried_.assign(rooms.size(), {});
    struct P { std::uint16_t r, s; std::uint32_t p; double n[3], d, c[3], lo[3], hi[3], area; double v[4][3]; unsigned cnt; bool dbl; };
    std::vector<P> all;
    for (std::size_t r = 0; r < rooms.size(); ++r) {
      const auto& secs = rooms[r].sections();
      twins_[r].resize(secs.size());
      decal_n_[r].resize(secs.size());
      buried_[r].resize(secs.size());
      for (std::size_t si = 0; si < secs.size(); ++si) {
        const auto& sec = secs[si];
        twins_[r][si].resize(sec.polygons.size());
        decal_n_[r][si].assign(sec.polygons.size(), {0.f, 0.f, 0.f});
        buried_[r][si].assign(sec.polygons.size(), 0.f);
        for (std::size_t pi = 0; pi < sec.polygons.size(); ++pi) {
          const auto& poly = sec.polygons[pi];
          if (!poly.renderable || poly.degenerate) continue;
          const unsigned cnt = poly.quad ? 4u : 3u;
          P q{std::uint16_t(r), std::uint16_t(si), std::uint32_t(pi), {}, 0, {}, {1e9, 1e9, 1e9}, {-1e9, -1e9, -1e9}, 0};
          double v[4][3];
          bool ok = true;
          for (unsigned k = 0; k < cnt; ++k) {
            const auto vi = poly.vertex_indices[k];
            if (vi >= sec.vertices.size()) { ok = false; break; }
            v[k][0] = sec.vertices[vi].x; v[k][1] = sec.vertices[vi].y; v[k][2] = sec.vertices[vi].z;
            for (int j = 0; j < 3; ++j) { q.c[j] += v[k][j] / cnt; q.lo[j] = std::min(q.lo[j], v[k][j]); q.hi[j] = std::max(q.hi[j], v[k][j]); }
          }
          if (!ok) continue;
          q.cnt = cnt; q.dbl = poly.double_sided;
          for (unsigned k = 0; k < cnt; ++k) for (int j = 0; j < 3; ++j) q.v[k][j] = v[k][j];
          const double a[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]}, b[3] = {v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]};
          q.n[0] = a[1] * b[2] - a[2] * b[1]; q.n[1] = a[2] * b[0] - a[0] * b[2]; q.n[2] = a[0] * b[1] - a[1] * b[0];
          const double len = std::sqrt(q.n[0] * q.n[0] + q.n[1] * q.n[1] + q.n[2] * q.n[2]);
          if (len < 1e-6) continue;
          q.area = len * (cnt == 4 ? 1.0 : 0.5);
          for (double& x : q.n) x /= len;
          q.d = q.n[0] * v[0][0] + q.n[1] * v[0][1] + q.n[2] * v[0][2];
          all.push_back(q);
        }
      }
    }
    auto cell = [](double x) { return std::int64_t(std::floor(x / 256.0)); };
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid;
    auto key = [](std::int64_t x, std::int64_t y, std::int64_t z) { return (x & 0x1fffff) | ((y & 0x1fffff) << 21) | ((z & 0x1fffff) << 42); };
    for (std::uint32_t i = 0; i < all.size(); ++i) grid[key(cell(all[i].c[0]), cell(all[i].c[1]), cell(all[i].c[2]))].push_back(i);
    std::size_t marks = 0;
    for (const auto& p : all) {
      auto& out = twins_[p.r][p.s][p.p];
      for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
        auto it = grid.find(key(cell(p.c[0]) + dx, cell(p.c[1]) + dy, cell(p.c[2]) + dz));
        if (it == grid.end()) continue;
        for (auto j : it->second) {
          const auto& q = all[j];
          if (&q == &p) continue;
          if (std::fabs(p.n[0] * q.n[0] + p.n[1] * q.n[1] + p.n[2] * q.n[2]) < 0.995) continue;
          if (std::fabs(q.n[0] * p.c[0] + q.n[1] * p.c[1] + q.n[2] * p.c[2] - q.d) > 3.0) continue;
          bool inside = true;
          for (int k = 0; k < 3 && inside; ++k) inside = p.c[k] >= q.lo[k] - 2.0 && p.c[k] <= q.hi[k] + 2.0;
          if (!inside) continue;

          auto insideQ = [&](const double* pt) {
            static const int ord4[4] = {0, 1, 3, 2}, ord3[3] = {0, 1, 2};
            const int* ord = q.cnt == 4 ? ord4 : ord3;
            int sgn = 0;
            for (unsigned e = 0; e < q.cnt; ++e) {
              const double* a0 = q.v[ord[e]]; const double* a1 = q.v[ord[(e + 1) % q.cnt]];
              const double ed[3] = {a1[0] - a0[0], a1[1] - a0[1], a1[2] - a0[2]}, w[3] = {pt[0] - a0[0], pt[1] - a0[1], pt[2] - a0[2]};
              const double cr = (ed[1] * w[2] - ed[2] * w[1]) * q.n[0] + (ed[2] * w[0] - ed[0] * w[2]) * q.n[1] + (ed[0] * w[1] - ed[1] * w[0]) * q.n[2];
              const double el = std::sqrt(ed[0] * ed[0] + ed[1] * ed[1] + ed[2] * ed[2]);
              const double dist = el > 0 ? cr / el : 0;
              if (dist > 1.5) { if (sgn < 0) return false; sgn = 1; }
              else if (dist < -1.5) { if (sgn > 0) return false; sgn = -1; }
            }
            return true;
          };
          bool inBox = true;
          for (unsigned k = 0; k < p.cnt && inBox; ++k) inBox = insideQ(p.v[k]);
          if (inBox && p.area < q.area * 0.7) { auto& dn = decal_n_[p.r][p.s][p.p]; dn = {float(p.n[0]), float(p.n[1]), float(p.n[2])}; }
          if (q.r == p.r) continue;
          if (std::none_of(out.begin(), out.end(), [&](const PolyTwin& t) { return t.r == q.r && t.s == q.s; })) { out.push_back({q.r, q.s}); ++marks; }
        }
      }
    }
    if (static const char* pf = std::getenv("SF3N_POLYFIND"); pf && log) {
      const unsigned want = unsigned(std::strtoul(pf, nullptr, 16));
      for (std::size_t r = 0; r < rooms.size(); ++r) for (std::size_t si = 0; si < rooms[r].sections().size(); ++si) {
        const auto& sec = rooms[r].sections()[si];
        for (std::size_t pi = 0; pi < sec.polygons.size(); ++pi) { const auto& pg = sec.polygons[pi];
          if (pf[0] == 's') { int rr = -1, ss = -1; std::sscanf(pf + 1, "%d:%d", &rr, &ss); if (int(r) != rr || int(si) != ss) continue; }
          else if (pf[0] == 'n') { double cx = 0, cy = 0, cz = 0; const unsigned nv = pg.quad ? 4u : 3u; bool bad = false;
            for (unsigned k = 0; k < nv; ++k) { const auto vi = pg.vertex_indices[k]; if (vi >= sec.vertices.size()) { bad = true; break; } cx += sec.vertices[vi].x / double(nv); cy += sec.vertices[vi].y / double(nv); cz += sec.vertices[vi].z / double(nv); }
            if (bad || std::hypot(cx - 485, cz - 970) > 300 || cy < 225 || cy > 260) continue; }
          else if (pf[0] == 'p') { double px = 0, py = 0, pz = 0; std::sscanf(pf + 1, "%lf,%lf,%lf", &px, &py, &pz); const unsigned nv = pg.quad ? 4u : 3u; double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9}; bool bad = false;
            for (unsigned k = 0; k < nv; ++k) { const auto vi = pg.vertex_indices[k]; if (vi >= sec.vertices.size()) { bad = true; break; } const double c3[3] = {double(sec.vertices[vi].x), double(sec.vertices[vi].y), double(sec.vertices[vi].z)}; for (int j = 0; j < 3; ++j) { lo[j] = std::min(lo[j], c3[j]); hi[j] = std::max(hi[j], c3[j]); } }
            const double pp[3] = {px, py, pz}; for (int j = 0; j < 3 && !bad; ++j) bad = pp[j] < lo[j] - 20 || pp[j] > hi[j] + 20;
            if (bad) continue; }
          else if (pg.clut != want) continue;
          std::fprintf(log, "POLYFIND clut %04x sala %zu sec %zu p %zu rend %d deg %d dup %d dbl %d tp %03x uv %02x,%02x %02x,%02x calco %d gemelos %zu v", unsigned(pg.clut), r, si, pi, int(pg.renderable), int(pg.degenerate), int(pg.duplicate), int(pg.double_sided), unsigned(pg.texture_page), pg.uv[0].u, pg.uv[0].v, pg.uv[3].u, pg.uv[3].v, int(decal_n_[r][si][pi][1] != 0.f || decal_n_[r][si][pi][0] != 0.f || decal_n_[r][si][pi][2] != 0.f), twins_[r][si][pi].size());
          for (unsigned k = 0; k < (pg.quad ? 4u : 3u); ++k) { const auto vi = pg.vertex_indices[k]; if (vi < sec.vertices.size()) std::fprintf(log, " %d,%d,%d c%02x", int(sec.vertices[vi].x), int(sec.vertices[vi].y), int(sec.vertices[vi].z), unsigned(sec.vertices[vi].color & 31) << 3); }
          std::fprintf(log, "\n"); }
      }
    }
    std::size_t buried = 0;
    if (!std::getenv("SF3N_SIN_ENTERRADOS"))
      for (const auto& p : all) {
        if (!p.dbl || std::fabs(p.n[1]) < 0.95) continue;
        double gap = 0;
        for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) for (int dz = -1; dz <= 1; ++dz) {
          auto it = grid.find(key(cell(p.c[0]) + dx, cell(p.c[1]) + dy, cell(p.c[2]) + dz));
          if (it == grid.end()) continue;
          for (auto j : it->second) {
            const auto& q = all[j];
            if (&q == &p || q.dbl || std::fabs(q.n[1]) < 0.2 || q.area <= p.area) continue;

            auto inXZ = [&](const double* pt) {
              static const int ord4[4] = {0, 1, 3, 2}, ord3[3] = {0, 1, 2};
              const int* ord = q.cnt == 4 ? ord4 : ord3; int sg = 0;
              for (unsigned e = 0; e < q.cnt; ++e) {
                const double* a0 = q.v[ord[e]]; const double* a1 = q.v[ord[(e + 1) % q.cnt]];
                const double cr = (a1[0] - a0[0]) * (pt[2] - a0[2]) - (a1[2] - a0[2]) * (pt[0] - a0[0]);
                if (cr > 1e-6) { if (sg < 0) return false; sg = 1; } else if (cr < -1e-6) { if (sg > 0) return false; sg = -1; }
              }
              return true;
            };
            for (unsigned k = 0; k <= p.cnt; ++k) {
              const double* pt = k < p.cnt ? p.v[k] : p.c;
              if (!inXZ(pt)) continue;
              const double yq = (q.d - q.n[0] * pt[0] - q.n[2] * pt[2]) / q.n[1];
              const double g = pt[1] - yq;
              if (g > 0.5 && g < 24.0) gap = std::max(gap, g);
            }
          }
        }
        if (gap > 0) { buried_[p.r][p.s][p.p] = float(gap); ++buried; }
      }
    if (log) std::fprintf(log, "nativo: %zu poligonos bajo una pendiente (se acercan al ojo)\n", buried);
    std::size_t decals = 0;
    for (const auto& a : decal_n_) for (const auto& b : a) for (const auto& n : b) if (n[0] != 0.f || n[1] != 0.f || n[2] != 0.f) ++decals;
    if (log) std::fprintf(log, "nativo: %zu poligonos repetidos en salas vecinas, %zu calcomanias\n", marks, decals);
  }

  static constexpr int kOpcAlias = 60;
  inline static const bool kOldOpc = std::getenv("SF3N_TITULO_OPCIONES") != nullptr;

  std::vector<int> titleItems() const {
    std::vector<int> v{0};
    if (title_cont_ok_) v.push_back(2);
    if (titleCjVisible()) v.push_back(3);
    v.push_back(1);
    return v;
  }
  bool titleCjVisible() const { return title_cj_seen_ >= 0 && field_count_ < title_cj_seen_ + 30; }

  double titleCenter(int item) const {
    const auto items = titleItems();
    if (items.size() == 2) return item == 0 ? -99.0 : 103.5;
    static const double wEs[4] = {73, 46, 53, 75};
    static const double wRu[4] = {77, 45, 94, 72};
    const bool ru = g_idioma_actual && *g_idioma_actual == "ru";
    const double* w = ru ? wRu : wEs;
    const double span = items.size() >= 4 ? (ru ? 314.0 : 296.0) : 260.0;
    double tot = 0; for (int it : items) tot += w[it];
    const double gap = (span - tot) / double(items.size() - 1);
    double x = -2.0 - span * 0.5;
    for (int it : items) { if (it == item) return x + w[it] * 0.5; x += w[it] + gap; }
    return 0.0;
  }

  struct TitleWord { unsigned tpage, v, w; };
  static constexpr TitleWord kTitleWords[5] = {{0x0af, 70, 102}, {0x0ae, 136, 108}, {0x0af, 138, 102}, {0x0ae, 0, 102}, {0x0af, 0, 108}};
  std::int64_t title_word_seen_[5] = {-1, -1, -1, -1, -1};
  unsigned title_word_cmd_ = 0x64;
  void noteTitleWord(unsigned tpage, unsigned v, unsigned w, unsigned cmd) {
    for (int i = 0; i < 5; ++i)
      if (kTitleWords[i].tpage == (tpage & 0x1ff) && kTitleWords[i].v == v && kTitleWords[i].w == w) { title_word_seen_[i] = std::int64_t(field_count_); title_word_cmd_ = cmd & ~1u; }
    if (v == 136 && w == 108) title_cj_seen_ = std::int64_t(field_count_);
  }
  bool titleWordOk(int i) const { return title_word_seen_[i] >= 0 && field_count_ < std::uint64_t(title_word_seen_[i] + 30); }

  float tlev_[8] = {70, 70, 70, 70, 70, 70, 70, 70};
  double tlev_t_ = 0;
  void stepTitleLevels(bool snap) {
    const double now = nowSeconds();
    const double dt = std::clamp(tlev_t_ > 0 ? now - tlev_t_ : 0.0, 0.0, 0.1);
    tlev_t_ = now;
    const bool sub = title_sub_.load();
    for (int i = 0; i < 8; ++i) {
      const bool on = sub ? (i >= 5 && i - 5 == sub_sel_) : (i < 4 && i == title_sel_);
      const float t = on ? 200.0f : 70.0f;
      if (snap) { tlev_[i] = t; continue; }
      const float step = float(900.0 * dt);
      tlev_[i] = tlev_[i] < t ? std::min(t, tlev_[i] + step) : std::max(t, tlev_[i] - step);
    }
  }
  template <class Map> void drawGameWord(const Map& map, int word, double left, float level) {
    const auto& tw = kTitleWords[word];
    hud_tpage_ = tw.tpage;
    DR_TPAGE t{}; SetDrawTPage(&t, 1, 0, int(tw.tpage)); DrawPrim(&t);
    const unsigned c = unsigned(std::clamp(level, 0.0f, 255.0f));
    const int x = int(std::lround(left));
    std::vector<std::uint32_t> q{(title_word_cmd_ << 24) | (c * 0x010101u), (50u << 16) | (std::uint32_t(x) & 0xffffu), (0x7fb0u << 16) | (tw.v << 8), (66u << 16) | tw.w};
    hudRect(q, map);
  }
  template <class Map> void drawTitleRow(const Map& map) {
    const bool sub = title_sub_.load();
    stepTitleLevels(title_row_snap_);
    title_row_snap_ = false;
    if (sub) {

      static const double xs[3] = {-150.0, -46.0, 50.0};
      for (int i = 0; i < 3; ++i) if (titleWordOk(2 + i)) drawGameWord(map, 2 + i, xs[i], tlev_[5 + i]);
      return;
    }
    if (titleWordOk(0)) drawGameWord(map, 0, titleCenter(0) - 51.0, tlev_[0]);
    if (titleCjVisible()) drawGameWord(map, 1, titleCenter(3) - 54.5, tlev_[3]);
    if (title_cont_ok_) drawTitleImage(map, 2, titleCenter(2) - 53.0, tlev_[2]);
    drawTitleImage(map, 1, titleCenter(1) - 53.5, tlev_[1]);
  }

  template <class Map> void drawTitleImage(const Map& map, int which, double left, float level) {
    if (!opc_uploaded_) {
      const bool en = g_idioma_actual && *g_idioma_actual == "en";
      const bool ru = g_idioma_actual && *g_idioma_actual == "ru";
      std::vector<std::uint16_t> page(64 * 256, 0);
      for (int half = 0; half < 2; ++half) {
        std::fill(page.begin(), page.end(), 0);
        for (int y = 0; y < kOpcH; ++y)
          for (int x = 0; x < kOpcW / 2; ++x) {
            const std::size_t src = std::size_t(y * kOpcW + half * (kOpcW / 2) + x);
            page[std::size_t(y * 64 + x)] = kOldOpc ? (en ? kOpcPixEn2 : kOpcPix)[src] : (ru ? kSalPixRu : en ? kSalPixEn : kSalPix)[src];
            page[std::size_t((y + 80) * 64 + x)] = (ru ? kConPixRu : en ? kConPixEn2 : kConPix)[src];
          }
        GR_UploadVRAMAliasPage(kOpcAlias + half, page.data());
      }
      opc_uploaded_ = true;
      vram_uploaded_ = false;
    }
    const int v0 = which == 2 ? 80 : 0;
    for (int half = 0; half < 2; ++half) {
      POLY_FT4 p{}; setPolyFT4(&p);
      const u_char c = u_char(std::clamp(level, 0.0f, 255.0f));
      setRGB0(&p, c, c, c);
      const float x0 = map.x(left + half * (kOpcW / 2)), x1 = map.x(left + (half + 1) * (kOpcW / 2)), y0 = map.y(50.0), y1 = y0 + float(kOpcH);
      setXY4(&p, x0, y0, x1, y0, x0, y1, x1, y1);
      setUV4(&p, 0, v0, kOpcW / 2, v0, 0, v0 + kOpcH, kOpcW / 2, v0 + kOpcH);
      p.tpage = u_short((kOpcAlias + half + 1) * 1024 + (2 << 7) + (1 << 5));
      p.clut = 0;
      setSemiTrans(&p, 1);
      DrawPrim(&p);
    }
  }
 public:

  struct MenuItem { std::string text; int id; };
  std::vector<MenuItem> menuItems() const {
    auto yn = [](bool b) { return std::string(b ? "SÍ" : "NO"); };
    char sens[16]; std::snprintf(sens, sizeof sens, "%d", int(std::lround(mouse_sens_ * 10)));
    char vols[3][8];
    std::snprintf(vols[0], 8, "%d", vol_master_); std::snprintf(vols[1], 8, "%d", vol_spu_); std::snprintf(vols[2], 8, "%d", vol_cd_);
    auto bindList = [&](const ctl::Bindings& bd, bool keys, int base, ctl::PadFamily fam) {
      std::vector<MenuItem> v;
      for (int a = 0; a < ctl::kCount; ++a) {
        if (a == ctl::kStrafeL || a == ctl::kStrafeR) continue;
        std::string list;
        if (keys) { for (int c : bd.keys[std::size_t(a)]) if (c) { if (!list.empty()) list += ", "; list += ctl::keyLabel(c); } }
        else { for (int c : bd.pad[std::size_t(a)]) if (c >= 0) { if (!list.empty()) list += ", "; list += ctl::padLabel(c, fam); } }
        v.push_back({std::string(ctl::kActions[a].label) + ":  " + (list.empty() ? "---" : list), base + a});
      }
      return v;
    };
    switch (menu_page_ == 31 ? 11 : menu_page_) {
      case 46:
        return {{"DESBLOQUEAR TODO (MISIONES, MINIJUEGOS, TRUCOS)", 880}, {"INVULNERABLE: " + yn(cheat_god_.load()), 881},
                {"TODAS LAS ARMAS: " + yn(cheat_weapons_.load()), 882}, {"INVISIBLE (LOS ENEMIGOS NO TE ATACAN): " + yn(cheat_invis_.load()), 883}, {"VOLVER", 876}};
      case 39:
        return {{"SÍ", 860}, {"NO", 861}};
      case 36:
        return {{"SÍ, REINICIAR EL JUEGO", 840}, {"NO (DEJAR EL IDIOMA COMO ESTABA)", 841}};
      case 30:
        return {{std::string("TIPO DE MOVIMIENTO: < ") + ((two_player_.load() && mv_saved_ ? mv_saved_free_ : movement_free_) ? "MODERNO" : "CLÁSICO") + " >", 801},
                {std::string("TEXTOS: < ") + (lang_ == "en" ? "INGLÉS" : lang_ == "ru" ? "RUSO" : "ESPAÑOL") + " >", 802},
                {std::string("VOCES: < ") + (voices_lang() == "ru" ? "RUSO" : voices_lang() == "en" ? "INGLÉS" : "ESPAÑOL") + " >", 804},
                {"VOLVER", 800}};
      case 32:
        return {{std::string("VOLUMEN GENERAL: < ") + vols[0] + " >", 810},
                {std::string("MÚSICA Y EFECTOS: < ") + vols[1] + " >", 811},
                {std::string("VOCES Y VIDEOS: < ") + vols[2] + " >", 812},
                {std::string("SALIDA: < ") + (mono_ ? "MONO" : "ESTÉREO") + " >", 813},
                {"VOLVER", 800}};
      case 33:
        return {{"CONTROL P1", 820}, {"CONTROL P2", 821},
                {"VIBRACIÓN DEL MANDO: " + yn(rumble_enabled_), 22},
                {std::string("SENSIBILIDAD DEL RATÓN: < ") + sens + " >", 10},
                {"INVERTIR RATÓN (ARRIBA/ABAJO): " + yn(mouse_invert_), 11},
                {"RUEDA DEL RATÓN", 822},
                {"VOLVER", 800}};
      case 37:
        return {{"RUEDA: CAMBIAR DE ARMA: " + yn(wheel_weapons_), 850},
                {"RUEDA AL APUNTAR: ZOOM DE LA MIRA: " + yn(wheel_zoom_), 851},
                {std::string("ACERCAR LA MIRA CON: < ") + kWheelBtns[wheel_in_btn_].first + " >", 852},
                {std::string("ALEJAR LA MIRA CON: < ") + kWheelBtns[wheel_out_btn_].first + " >", 853},
                {"INVERTIR LA RUEDA: " + yn(wheel_invert_), 854},
                {"RUEDA EN EL MENU DE ARMAS: " + yn(wheel_menu_hand_), 856},
                {"VOLVER", 855}};
      case 34: {
        std::string dev = p1_dev_ ? std::string("MANDO") : std::string("TECLADO Y RATÓN");
        if (p1_dev_ && pad_) { if (const char* n = SDL_GameControllerName(pad_)) { dev += " ("; dev += n; dev += ")"; } }
        else if (p1_dev_) dev += " (NO CONECTADO)";
        std::vector<MenuItem> v{{"DISPOSITIVO: < " + dev + " >", 830}};
        for (auto& it : bindList(binds_, p1_dev_ == 0, p1_dev_ == 0 ? 200 : 300, ctl::padFamily(pad_))) v.push_back(it);
        v.push_back({"RESTAURAR LOS VALORES DE FÁBRICA", p1_dev_ == 0 ? 290 : 390});
        v.push_back({"VOLVER", 831});
        return v;
      }
      case 35: {
        SDL_GameController* p2 = pad2_ ? pad2_ : pad_;
        std::string dev = p2_source_ ? std::string("TECLADO") : std::string("MANDO");
        if (!p2_source_ && p2) { if (const char* n = SDL_GameControllerName(p2)) { dev += " ("; dev += n; dev += ")"; } }
        else if (!p2_source_) dev += " (NO CONECTADO)";
        std::vector<MenuItem> v{{"DISPOSITIVO: < " + dev + " >", 24}};
        for (auto& it : bindList(binds2_, p2_source_ == 1, p2_source_ == 1 ? 400 : 500, ctl::padFamily(p2))) v.push_back(it);
        v.push_back({"RESTAURAR LOS VALORES DE FÁBRICA", p2_source_ == 1 ? 490 : 590});
        v.push_back({"VOLVER", 831});
        return v;
      }
      case 0:
        return {{"CONTINUAR", 0}, {"ARMAS", 6}, {"MENÚ DEL JUEGO (MAPA, OBJETIVOS)", 1}, {"GUARDAR PARTIDA", 2},
                {"CARGAR PARTIDA", 3}, {"OPCIONES", 4}, {"SALIR DEL JUEGO", 5}};
      case 2: {
        std::vector<MenuItem> v;
        for (const auto& w : menu_weapons_) {
          char line[96];
          if (w.reserve) std::snprintf(line, sizeof line, "%s%s   %d / %d", w.current ? "> " : "", w.name.c_str(), w.clip, w.reserve);
          else if (w.clip) std::snprintf(line, sizeof line, "%s%s   X%d", w.current ? "> " : "", w.name.c_str(), w.clip);
          else std::snprintf(line, sizeof line, "%s%s", w.current ? "> " : "", w.name.c_str());
          v.push_back({line, 100 + w.index});
        }
        if (v.empty()) v.push_back({"(SIN ARMAS)", 17});
        v.push_back({"VOLVER", 17});
        return v;
      }
      case 5: {
        std::vector<MenuItem> v;
        for (int a = 0; a < ctl::kCount; ++a) {
          std::string list;
          for (int c : binds2_.keys[std::size_t(a)]) if (c) { if (!list.empty()) list += ", "; list += ctl::keyLabel(c); }
          v.push_back({std::string(ctl::kActions[a].label) + ":  " + (list.empty() ? "---" : list), 400 + a});
        }
        v.push_back({"RESTAURAR LOS VALORES DE FÁBRICA", 490});
        v.push_back({"VOLVER", 291});
        return v;
      }
      case 3: case 4: {
        std::vector<MenuItem> v;
        const auto fam = ctl::padFamily(pad_);
        for (int a = 0; a < ctl::kCount; ++a) {
          std::string line = std::string(ctl::kActions[a].label) + ":  ";
          std::string list;
          if (menu_page_ == 3) {
            for (int c : binds_.keys[std::size_t(a)]) if (c) { if (!list.empty()) list += ", "; list += ctl::keyLabel(c); }
          } else {
            for (int c : binds_.pad[std::size_t(a)]) if (c >= 0) { if (!list.empty()) list += ", "; list += ctl::padLabel(c, fam); }
          }
          v.push_back({line + (list.empty() ? "---" : list), (menu_page_ == 3 ? 200 : 300) + a});
        }
        v.push_back({"RESTAURAR LOS VALORES DE FÁBRICA", menu_page_ == 3 ? 290 : 390});
        v.push_back({"VOLVER", 291});
        return v;
      }
      case 10:
        return {{"JUGAR", 52}, {"CONFIGURACIÓN", 870}, {"SALIR", 55}};
      case 11: {
        char res[48];
        std::snprintf(res, sizeof res, "%dX%d", launcher_ ? launcher_res_w_ : g_cfg_renderWidth, launcher_ ? launcher_res_h_ : g_cfg_renderHeight);
        char msaa[16];
        if (g_cfg_smaa) std::snprintf(msaa, sizeof msaa, "SMAA");
        else if (g_cfg_msaaSamples > 1) std::snprintf(msaa, sizeof msaa, "MSAA %dX", g_cfg_msaaSamples);
        else std::snprintf(msaa, sizeof msaa, "NO");
        const char* filt = !g_cfg_bilinearFiltering ? "PIXELADO" : !g_cfg_trilinearFiltering ? "BILINEAL" : !g_cfg_anisotropicFiltering ? "TRILINEAL" : "ANISOTRÓPICO";
        std::vector<MenuItem> v = {
            {std::string("RESOLUCIÓN: < ") + res + " >", 40},
            {"PANTALLA COMPLETA: " + yn(full_), 15},
            {std::string("ANTIALIASING: < ") + msaa + " >", 42},
            {std::string("FILTRADO: < ") + filt + " >", 43},
            {std::string("FORMATO: < ") + ((launcher_ ? launcher_aspect_ : g_cfg_aspectMode) == PSYX_ASPECT_ORIGINAL_4_3 ? "ORIGINAL 4:3" : "PANORÁMICO") + " >", 44},
            {std::string("VIDEOS: < ") + (videos_zoom_ ? "AMPLIADOS" : "COMO EN PS1") + " >", 46},
            {"SINCRONÍA VERTICAL: " + yn(g_cfg_swapInterval != 0), 45},
            {std::string("LÍMITE DE FPS: < ") + (fps_cap_.load() > 0 ? std::to_string(fps_cap_.load()) + " FPS" : std::string("SIN LÍMITE")) + " >", 49},
            {"SOMBRAS: " + yn(shadows_mode_ != 0), 13},
            {"TAMAÑO DEL HUD: < " + std::to_string(hud_scale_) + "% >", 47},
            {"HUD EN LOS BORDES: " + yn(hud_edges_), 48},
            {"CARGAS RÁPIDAS (TURBO): " + yn(turbo_enabled_.load()), 14}};
        v.push_back({"VOLVER", title_opts_ ? 800 : 292});
        return v;
      }
      case 12:
        return {{"TECLADO Y RATÓN", 20}, {std::string("MANDO") + (pad_ ? "" : " (NO CONECTADO)"), 21},
                {"VIBRACIÓN DEL MANDO: " + yn(rumble_enabled_), 22},
                {std::string("SENSIBILIDAD DEL RATÓN: < ") + sens + " >", 10}, {"INVERTIR RATÓN (ARRIBA/ABAJO): " + yn(mouse_invert_), 11},
                {std::string("JUGADOR 2: < ") + (p2_source_ ? "TECLADO" : "MANDO") + " >", 24}, {"TECLADO DEL JUGADOR 2", 25}};
      case 13: case 14: {
        std::vector<MenuItem> v;
        for (std::size_t i = 0; i < load_entries_.size(); ++i) v.push_back({load_entries_[i].text, 600 + int(i)});
        if (menu_page_ == 14 && !readProgress().empty()) v.push_back({"INICIO DE UNA MISIÓN...", 53});
        if (load_entries_.empty() && menu_page_ == 14) v.insert(v.begin(), {"(NO HAY PARTIDAS GUARDADAS)", 293});
        v.push_back({"VOLVER", 293});
        return v;
      }
      case 38: {
        std::vector<MenuItem> v;
        v.push_back({"DESBLOQUEAR TODO (MISIONES, MINIJUEGOS, TRUCOS)", 880});
        v.push_back({"INVULNERABLE: " + yn(cheat_god_.load()), 881});
        v.push_back({"TODAS LAS ARMAS: " + yn(cheat_weapons_.load()), 882});
        v.push_back({"INVISIBLE (LOS ENEMIGOS NO TE ATACAN): " + yn(cheat_invis_.load()), 883});
        const auto names = debugMissionNames();
        for (std::size_t i = 0; i < names.size() && i < 19; ++i) {
          char line[96];
          std::snprintf(line, sizeof line, "%s%02zu  %s", int(i) == debug_cur_mission_.load() ? "> " : "", i, names[i].c_str());
          v.push_back({line, 900 + int(i)});
        }
        v.push_back({"VOLVER", 899});
        return v;
      }
      case 40: {
        std::vector<MenuItem> v;
        std::lock_guard lk(const_cast<std::mutex&>(card_mtx_));
        for (std::size_t i = 0; i < card_items_.size(); ++i) v.push_back({card_items_[i], 950 + int(i)});
        v.push_back({"CANCELAR", 949});
        return v;
      }
      case 41: {
        std::lock_guard lk(const_cast<std::mutex&>(card_mtx_));
        const std::string what = card_del_idx_ >= 0 && card_del_idx_ < int(card_items_.size()) ? card_items_[std::size_t(card_del_idx_)] : std::string();
        return {{what, 964}, {"SÍ, BORRARLA", 962}, {"NO", 963}};
      }
      case 15: {
        std::vector<MenuItem> v;
        for (int n = 1; n <= 6; ++n) v.push_back({"RANURA " + std::to_string(n) + ": " + metaText(readMeta(slotPath(n))), 700 + n});
        v.push_back({"VOLVER", 293});
        return v;
      }
      default: break;
    }

    return {{"CONTROLES: TECLADO Y RATÓN", 20}, {std::string("CONTROLES: MANDO") + (pad_ ? "" : " (NO CONECTADO)"), 21},
            {"VIBRACIÓN DEL MANDO: " + yn(rumble_enabled_), 22},
            {std::string("SENSIBILIDAD DEL RATÓN: < ") + sens + " >", 10},
            {"INVERTIR RATÓN (ARRIBA/ABAJO): " + yn(mouse_invert_), 11},
            {std::string("TIPO DE MOVIMIENTO: < ") + ((two_player_.load() && mv_saved_ ? mv_saved_free_ : movement_free_) ? "MODERNO" : "CLÁSICO") + " >", 801},
            {std::string("JUGADOR 2: < ") + (p2_source_ ? "TECLADO" : "MANDO") + " >", 24},
            {"TECLADO DEL JUGADOR 2", 25},
            {"RUEDA DEL RATÓN", 822},
            {"GRÁFICOS", 23}, {"TRUCOS", 875},
            {"GUARDAR PARTIDA", 2}, {"CARGAR PARTIDA", 3}, {"VOLVER AL JUEGO", 16}, {"SALIR DEL JUEGO", 5}};
  }
  static bool adjustable(int id) { return (id >= 10 && id <= 12) || id == 24 || id == 801 || id == 802 || id == 803 || id == 804 || (id >= 810 && id <= 813) || id == 830 || id == 852 || id == 853 || id == 40 || id == 42 || id == 43 || id == 44 || id == 46 || id == 47 || id == 49 || id == 50; }
  void menuSoundPub(bool accept) { menuSound(accept); }

  void menuSound(bool accept) {
    if (!audio_ || std::getenv("SF3N_SIN_SONIDO_MENU")) return;
    const std::int16_t* src = accept ? kSndAceptar : kSndMover;
    const int n = accept ? kSndAceptarLen : kSndMoverLen;
    std::vector<std::int16_t> st(std::size_t(n) * 2);
    for (int i = 0; i < n; ++i) {
      int v = src[i] * vol_master_ / 10 * vol_spu_ / 10;
      st[std::size_t(2 * i)] = st[std::size_t(2 * i + 1)] = std::int16_t(v);
    }

    if (nowSeconds() - last_game_audio_.load() < 0.15) {
      std::lock_guard lk(sfx_mtx_);
      if (sfx_mix_.size() < st.size()) sfx_mix_.resize(st.size(), 0);
      for (std::size_t i = 0; i < st.size(); ++i) sfx_mix_[i] = std::int16_t(std::clamp(int(sfx_mix_[i]) + int(st[i]), -32768, 32767));
      return;
    }
    if (SDL_GetQueuedAudioSize(audio_) > 44100 * 4 / 5) SDL_ClearQueuedAudio(audio_);
    SDL_QueueAudio(audio_, st.data(), Uint32(st.size() * 2));
  }

  bool playWav(const std::string& path, int gainPct = 100) {
    if (!audio_) return false;
    static std::map<std::string, std::vector<std::int16_t>> cache;
    auto it = cache.find(path);
    if (it == cache.end()) {
      std::vector<std::int16_t> st;
      if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::vector<std::uint8_t> b;
        std::uint8_t tmp[65536];
        for (std::size_t n; (n = std::fread(tmp, 1, sizeof tmp, f)) > 0;) b.insert(b.end(), tmp, tmp + n);
        std::fclose(f);
        auto r16 = [&](std::size_t o) { return o + 2 <= b.size() ? int(b[o] | (b[o + 1] << 8)) : 0; };
        auto r32 = [&](std::size_t o) { return o + 4 <= b.size() ? std::uint32_t(r16(o)) | (std::uint32_t(r16(o + 2)) << 16) : 0u; };
        int ch = 0, rate = 0, bits = 0;
        if (b.size() > 12 && !std::memcmp(b.data(), "RIFF", 4) && !std::memcmp(b.data() + 8, "WAVE", 4)) {
          for (std::size_t o = 12; o + 8 <= b.size();) {
            const std::uint32_t sz = r32(o + 4);
            if (!std::memcmp(b.data() + o, "fmt ", 4)) { ch = r16(o + 10); rate = int(r32(o + 12)); bits = r16(o + 22); }
            else if (!std::memcmp(b.data() + o, "data", 4) && ch >= 1 && ch <= 2 && bits == 16 && rate > 0) {
              const std::size_t frames = std::min<std::size_t>(sz, b.size() - o - 8) / std::size_t(2 * ch);
              const std::int16_t* s = reinterpret_cast<const std::int16_t*>(b.data() + o + 8);
              const std::size_t outN = std::size_t(double(frames) * 44100.0 / rate);
              st.resize(outN * 2);
              for (std::size_t i = 0; i < outN; ++i) {
                const double p = double(i) * rate / 44100.0;
                const std::size_t a = std::min(frames - 1, std::size_t(p)), c = std::min(frames - 1, a + 1);
                const double t = p - double(a);
                for (int k = 0; k < 2; ++k) {
                  const int kk = ch == 2 ? k : 0;
                  st[2 * i + std::size_t(k)] = std::int16_t(s[a * std::size_t(ch) + std::size_t(kk)] * (1 - t) + s[c * std::size_t(ch) + std::size_t(kk)] * t);
                }
              }
              break;
            }
            o += 8 + sz + (sz & 1);
          }
        }
      }
      it = cache.emplace(path, std::move(st)).first;
    }
    if (it->second.empty()) return false;
    std::vector<std::int16_t> st(it->second.size());
    for (std::size_t i = 0; i < st.size(); ++i) st[i] = std::int16_t(std::clamp(it->second[i] * gainPct / 100 * vol_spu_ / 10, -32768, 32767));
    std::lock_guard lk(sfx_mtx_);
    if (sfx_mix_.size() < st.size()) sfx_mix_.resize(st.size(), 0);
    for (std::size_t i = 0; i < st.size(); ++i) sfx_mix_[i] = std::int16_t(std::clamp(int(sfx_mix_[i]) + int(st[i]), -32768, 32767));
    return true;
  }
  std::mutex sfx_mtx_;
  std::vector<std::int16_t> sfx_mix_;
  std::atomic<double> last_game_audio_{-1.0};
  void openMenu() {
    prev_menu_rb_ = 0xffff;
    menu_frozen_native_ = nativeActive();
    menu_page_ = 0; menu_sel_ = 0;
    menu_open_ = true;
  }
  void closeMenu() { menu_open_ = false; menu_frozen_native_ = false; }
  void goPage(int page, int sel = 0) { menu_page_ = page; menu_sel_ = sel; menu_scroll_ = 0; }

  void menuBack(bool startRaw) {
    if (title_opts_) {
      if (menu_page_ == 39) { closeExitConfirm(); return; }
      if (menu_page_ == 36) goPage(confirm_back_page_);
      else if (menu_page_ == 37) goPage(33, 5);
      else if (menu_page_ == 34 || menu_page_ == 35) goPage(33, menu_page_ - 34);
      else if (menu_page_ == 3 || menu_page_ == 4 || menu_page_ == 5) goPage(33);
      else closeTitleOptions();
      return;
    }
    switch (menu_page_) {
      case 2: goPage(0, 1); return;
      case 3: case 4: goPage(launcher_ ? 12 : 1, menu_page_ == 3 ? 0 : 1); return;
      case 5: goPage(launcher_ ? 12 : 1, launcher_ ? 6 : 6); return;
      case 11: if (!launcher_) goPage(1, 7); return;
      case 13: if (load_back_ == 14) { buildLoadEntries(false); goPage(14); } else goPage(launcher_ ? 10 : 1, launcher_ ? 2 : 10); return;
      case 14: goPage(launcher_ ? 10 : 1, launcher_ ? 3 : 10); return;
      case 15: goPage(1, 9); return;
      case 37: goPage(1, 7); return;
      case 38: closeMenu(); start_block_ = startRaw; return;
      case 46: goPage(1, 10); return;
      case 40: card_result_ = -1; closeMenu(); return;
      case 41: goPage(40, std::max(0, card_del_idx_)); return;
      case 10: case 12: return;
      default: closeMenu(); start_block_ = startRaw; return;
    }
  }
  int load_back_ = 0;
  void menuActivate(int id, int dir) {
    char v[32];
    if (id == 962) { card_delete_req_ = card_del_idx_; goPage(40, std::max(0, card_del_idx_ - 1)); return; }
    if (id == 963) { goPage(40, std::max(0, card_del_idx_)); return; }
    if (id == 964) return;
    if (id == 949) { card_result_ = -1; closeMenu(); return; }
    if (id >= 950 && id < 950 + 1000) { card_result_ = id - 950; closeMenu(); return; }
    if (id == 899) { closeMenu(); return; }
    if (id == 875) { goPage(46); return; }
    if (id == 876) { goPage(1, 10); return; }
    if (id >= 880 && id <= 883) {
      if (id == 880) { cheat_unlock_req_ = true; notify("todo desbloqueado"); }
      if (id == 881) cheat_god_ = !cheat_god_.load();
      if (id == 882) cheat_weapons_ = !cheat_weapons_.load();
      if (id == 883) cheat_invis_ = !cheat_invis_.load();
      return;
    }
    if (id >= 900 && id < 900 + 19) {
      debug_mission_req_ = id - 900;
      closeMenu();
      char t[96]; std::snprintf(t, sizeof t, "depuracion: cargando la mision %02d...", id - 900);
      notify(t);
      return;
    }
    switch (id) {
      case 0: closeMenu(); break;
      case 1: closeMenu(); inject_start_ = 4; break;
      case 2: goPage(15); break;
      case 3: buildLoadEntries(false); load_back_ = 0; goPage(14); break;
      case 4: goPage(1); break;
      case 5: quit = true; break;
      case 10: {
        int sv = int(std::lround(mouse_sens_ * 10)) + (dir == 0 ? 1 : dir);
        if (sv > 40) sv = 1; if (sv < 1) sv = 1;
        mouse_sens_ = sv / 10.0; std::snprintf(v, sizeof v, "%d", sv); iniSave("raton_sensibilidad", v); break;
      }
      case 11: mouse_invert_ = !mouse_invert_; iniSave("raton_invertir_y", mouse_invert_ ? "1" : "0"); break;
      case 13: shadows_mode_ = shadows_mode_ ? 0 : 1; iniSave("sombras", shadows_mode_ ? "1" : "0"); break;
      case 14: turbo_enabled_ = !turbo_enabled_; iniSave("turbo_cargas", turbo_enabled_ ? "1" : "0"); break;
      case 15: full_ = !full_; if (!launcher_) SDL_SetWindowFullscreen(g_window, full_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); iniSave("pantalla_completa", full_ ? "1" : "0"); break;
      case 16: closeMenu(); break;
      case 6: readWeapons(); goPage(2);
        for (std::size_t i = 0; i < menu_weapons_.size(); ++i) if (menu_weapons_[i].current) menu_sel_ = int(i);
        break;
      case 17: goPage(0, 1); break;
      case 20: goPage(3); break;
      case 24: p2_source_ = p2_source_ ? 0 : 1; iniSave("jugador2", p2_source_ ? "teclado" : "mando"); break;
      case 25: goPage(5); break;
      case 21: goPage(4); break;
      case 22: rumble_enabled_ = !rumble_enabled_; iniSave("vibracion", rumble_enabled_ ? "1" : "0");
        if (rumble_enabled_ && pad_) SDL_GameControllerRumble(pad_, 0x8000, 0x8000, 250);
        break;
      case 23: goPage(11); break;
      case 40: {
        const auto modes = monitorModes();
        int ri = -1;
        const int cw = launcher_ ? launcher_res_w_ : g_cfg_renderWidth, ch = launcher_ ? launcher_res_h_ : g_cfg_renderHeight;
        for (std::size_t i = 0; i < modes.size(); ++i) if (modes[i].first == cw && modes[i].second == ch) ri = int(i);
        const int n = int(modes.size());
        ri = ri < 0 ? 0 : (ri + (dir < 0 ? 1 : n - 1)) % n;
        applyResolution(modes[std::size_t(ri)].first, modes[std::size_t(ri)].second, true);
        break;
      }
      case 42: {

        static const int opts[] = {0, -1, 2, 4, 8};
        int i = 0;
        for (int k = 0; k < 5; ++k) if ((g_cfg_smaa && opts[k] == -1) || (!g_cfg_smaa && opts[k] == g_cfg_msaaSamples)) i = k;
        for (int tries = 0; tries < 5; ++tries) {
          i = (i + (dir < 0 ? 4 : 1)) % 5;
          if (opts[i] <= maxMsaa()) break;
        }
        g_cfg_smaa = opts[i] == -1 ? 1 : 0;
        g_cfg_msaaSamples = std::max(0, opts[i]);
        iniSave("msaa", std::to_string(g_cfg_msaaSamples));
        iniSave("smaa", g_cfg_smaa ? "1" : "0");
        break;
      }
      case 43: {
        int lvl = !g_cfg_bilinearFiltering ? 0 : !g_cfg_trilinearFiltering ? 1 : !g_cfg_anisotropicFiltering ? 2 : 3;
        lvl = (lvl + (dir < 0 ? 3 : 1)) % 4;
        g_cfg_bilinearFiltering = lvl >= 1; g_cfg_trilinearFiltering = lvl >= 2; g_cfg_anisotropicFiltering = lvl >= 3;
        iniSave("filtrado", g_cfg_bilinearFiltering ? "1" : "0");
        iniSave("trilineal", g_cfg_trilinearFiltering ? "1" : "0");
        iniSave("anisotropico", g_cfg_anisotropicFiltering ? "1" : "0");
        break;
      }
      case 44: {
        const bool orig = (launcher_ ? launcher_aspect_ : g_cfg_aspectMode) != PSYX_ASPECT_ORIGINAL_4_3;
        (launcher_ ? launcher_aspect_ : g_cfg_aspectMode) = orig ? PSYX_ASPECT_ORIGINAL_4_3 : PSYX_ASPECT_ADAPTIVE;
        iniSave("solo_4_3", orig ? "1" : "0");
        break;
      }
      case 47: {
        static const int opts[] = {75, 100, 125, 150};
        int i = 1;
        for (int k = 0; k < 4; ++k) if (opts[k] == hud_scale_) i = k;
        i = std::clamp(i + (dir < 0 ? -1 : 1), 0, 3);
        hud_scale_ = opts[i]; iniSave("hud_tamano", std::to_string(hud_scale_));
        break;
      }
      case 48: hud_edges_ = !hud_edges_; iniSave("hud_bordes", hud_edges_ ? "1" : "0"); break;
      case 46: videos_zoom_ = !videos_zoom_; iniSave("videos_ampliados", videos_zoom_ ? "1" : "0"); break;
      case 49: {
        static const int opts[] = {30, 60, 72, 90, 120, 144, 165, 240, 0};
        constexpr int n = 9;
        int i = n - 1;
        for (int k = 0; k < n; ++k) if (opts[k] == fps_cap_.load()) i = k;
        i = (i + (dir < 0 ? n - 1 : 1)) % n;
        fps_cap_ = opts[i]; iniSave("fps_max", std::to_string(opts[i]));
        break;
      }
      case 45: g_cfg_swapInterval = g_cfg_swapInterval ? 0 : 1; iniSave("vsync", g_cfg_swapInterval ? "1" : "0"); break;
      case 50:
        if (!launcher_cues_.empty()) {
          const int n = int(launcher_cues_.size());
          launcher_cue_ = (launcher_cue_ + (dir < 0 ? n - 1 : 1)) % n;
          setDiscId(discIdOf(launcher_cues_[std::size_t(launcher_cue_)]));
          loadGameFontFromCue(launcher_cues_[std::size_t(launcher_cue_)]);
          iniSave("disco", launcher_cues_[std::size_t(launcher_cue_)]);
        }
        break;
      case 51: boot_load = continuePath(); launcher_result_ = 1; break;
      case 52: boot_load.clear(); launcher_result_ = 1; break;
      case 53: load_back_ = menu_page_; buildLoadEntries(true); goPage(13); break;
      case 54: buildLoadEntries(false); load_back_ = 0; goPage(14); break;
      case 55: launcher_result_ = -1; break;
      case 490: binds2_ = ctl::defaultsP2(); saveAllBindings(); notify("controles restaurados"); break;
      case 290: case 390: {
        const auto d = ctl::defaults();
        if (id == 290) binds_.keys = d.keys; else binds_.pad = d.pad;
        saveAllBindings();
        notify("controles restaurados");
        break;
      }
      case 291: case 292: case 293: menuBack(false); break;
      case 800: closeTitleOptions(); break;
      case 860: closeExitConfirm(); exit_fade_t_ = nowSeconds(); break;
      case 861: closeExitConfirm(); break;
      case 870: menuSound(true); title_opts_ = true; goPage(30); break;
      case 840: restart_lang_ = lang_; quit = true; break;
      case 841: lang_ = lang_start_; iniSave("idioma", lang_); title_opts_ = false; closeMenu(); title_sel_ = 1; prev_title_raw_ = 0xffff; title_restore_ = true; break;
      case 801: {
        const bool tp = two_player_.load() && mv_saved_;
        (void)dir;
        const bool fr = !(tp ? mv_saved_free_ : movement_free_);
        if (tp) { mv_saved_modern_ = false; mv_saved_free_ = fr; }
        else { movement_modern_ = false; movement_free_ = fr; }
        camera_firm_ = fr;
        iniSave("movimiento", fr ? "libre" : "clasico"); iniSave("camara", fr ? "firme" : "original");
        break;
      }
      case 802: lang_ = lang_ == "es" ? "en" : lang_ == "en" ? "ru" : "es"; iniSave("idioma", lang_); if (launcher_ && g_idioma_mut) { *g_idioma_mut = lang_; if (!launcher_cues_.empty()) loadGameFontFromCue(launcher_cues_[std::size_t(launcher_cue_)]); } break;
      case 804: {
        const std::string v = voices_lang();
        const char* order[3] = {"es", "en", "ru"};
        int i = v == "en" ? 1 : v == "ru" ? 2 : 0;
        i = (i + (dir < 0 ? 2 : 1)) % 3;
        iniSave("idioma_voces", order[i]);
        break;
      }
      case 803: camera_firm_ = !camera_firm_; iniSave("camara", camera_firm_ ? "firme" : "original"); break;
      case 810: case 811: case 812: {
        int& vv = id == 810 ? vol_master_ : id == 811 ? vol_spu_ : vol_cd_;
        vv = dir == 0 ? (vv + 1) % 11 : std::clamp(vv + dir, 0, 10);
        iniSave(id == 810 ? "volumen_general" : id == 811 ? "volumen_musica" : "volumen_voces", std::to_string(vv));
        applyVolumes();
        break;
      }
      case 813: mono_ = !mono_; iniSave("sonido_mono", mono_ ? "1" : "0"); break;
      case 820: goPage(34); break;
      case 822: goPage(37); break;
      case 850: wheel_weapons_ = !wheel_weapons_; iniSave("rueda_armas", wheel_weapons_ ? "1" : "0"); break;
      case 856: wheel_menu_hand_ = !wheel_menu_hand_; iniSave("rueda_menu", wheel_menu_hand_ ? "1" : "0"); break;
      case 851: wheel_zoom_ = !wheel_zoom_; iniSave("rueda_zoom", wheel_zoom_ ? "1" : "0"); break;
      case 852: wheel_in_btn_ = (wheel_in_btn_ + (dir < 0 ? 7 : 1)) % 8; iniSave("rueda_acercar", wheelBtnIni(wheel_in_btn_)); break;
      case 853: wheel_out_btn_ = (wheel_out_btn_ + (dir < 0 ? 7 : 1)) % 8; iniSave("rueda_alejar", wheelBtnIni(wheel_out_btn_)); break;
      case 854: wheel_invert_ = !wheel_invert_; iniSave("rueda_invertir", wheel_invert_ ? "1" : "0"); break;
      case 855: if (title_opts_) goPage(33, 5); else goPage(1, 7); break;
      case 821: goPage(35); break;
      case 830: p1_dev_ = p1_dev_ ? 0 : 1; break;
      case 831: goPage(33, menu_page_ == 35 ? 1 : 0); break;
      case 590: binds2_.pad = ctl::defaults().pad; saveAllBindings(); notify("controles restaurados"); break;
      default:
        if (id >= 200 && id < 200 + ctl::kCount) { startCapture(1, id - 200); break; }
        if (id >= 300 && id < 300 + ctl::kCount) { startCapture(2, id - 300); break; }
        if (id >= 400 && id < 400 + ctl::kCount) { startCapture(3, id - 400); break; }
        if (id >= 500 && id < 500 + ctl::kCount) { startCapture(4, id - 500); break; }
        if (id >= 600 && id < 600 + int(load_entries_.size())) {
          const std::string path = load_entries_[std::size_t(id - 600)].path;
          if (launcher_) { boot_load = path; launcher_result_ = 1; }
          else { requestLoad(path); closeMenu(); }
          break;
        }
        if (id >= 701 && id <= 706) { requestSave(slotPath(id - 700)); closeMenu(); break; }
        if (id >= 100 && id < 200) { weapon_target_ = id - 100; weapon_deadline_ = 0; closeMenu(); }
        break;
    }
  }

  static constexpr float kMenuScale = 1.0f, kMenuLine = 13.0f;
  float menuTop(std::size_t n) const { return (launcher_ ? 138.0f : 120.0f) - float(n) * kMenuLine * 0.5f + 8.0f; }
  int menuVisible() const { return launcher_ ? 10 : 13; }

  int menuFirst(int n) {
    const int vis = menuVisible();
    if (n <= vis) { menu_scroll_ = 0; return 0; }
    if (menu_sel_ < menu_scroll_) menu_scroll_ = menu_sel_;
    if (menu_sel_ >= menu_scroll_ + vis) menu_scroll_ = menu_sel_ - vis + 1;
    menu_scroll_ = std::clamp(menu_scroll_, 0, n - vis);
    return menu_scroll_;
  }
  void startCapture(int kind, int action) {
    capture_kind_ = kind;
    capture_action_ = action;
    capture_armed_ = false;
    capture_deadline_ = Clock::now() + std::chrono::seconds(8);
  }

  void captureInput(const Uint8* k) {
    const Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
    bool anyKey = false;
    for (int i = 1; i < SDL_NUM_SCANCODES; ++i) if (k[i]) { anyKey = true; break; }
    const int padNow = ctl::padPressed(capture_kind_ == 4 ? (pad2_ ? pad2_ : pad_) : pad_);
    const bool anyMouse = (mb & (SDL_BUTTON(1) | SDL_BUTTON(2) | SDL_BUTTON(3) | SDL_BUTTON(4) | SDL_BUTTON(5))) != 0;
    if (!capture_armed_) {
      if (!anyKey && !anyMouse && padNow < 0) capture_armed_ = true;
      return;
    }
    auto done = [&] {
      capture_kind_ = 0;
      for (int i = 1; i < SDL_NUM_SCANCODES; ++i) if (k[i]) prev_keys_[i] = true;
      prev_mouse_ = mb;
      prev_pad_menu_ = ~0u;
      prev_menu_rb_ = 0xffff;
    };
    if (k[SDL_SCANCODE_ESCAPE] || Clock::now() > capture_deadline_) { done(); return; }
    if (k[SDL_SCANCODE_DELETE]) {
      if (capture_kind_ == 1) binds_.keys[std::size_t(capture_action_)].fill(0);
      else if (capture_kind_ == 3) binds2_.keys[std::size_t(capture_action_)].fill(0);
      else if (capture_kind_ == 4) binds2_.pad[std::size_t(capture_action_)].fill(-1);
      else binds_.pad[std::size_t(capture_action_)].fill(-1);
      saveAllBindings();
      done();
      return;
    }
    if (capture_kind_ == 1) {
      int code = 0;
      for (int i = 1; i < SDL_NUM_SCANCODES && !code; ++i) if (k[i] && !ctl::reservedKey(i)) code = i;
      for (int b = 1; b <= 5 && !code; ++b) if (mb & SDL_BUTTON(b)) code = ctl::kMouseBase + b;
      if (!code) return;
      ctl::assign(binds_.keys, capture_action_, code, 0);
    } else if (capture_kind_ == 3) {
      int code = 0;
      for (int i = 1; i < SDL_NUM_SCANCODES && !code; ++i) if (k[i] && !ctl::reservedKey(i)) code = i;
      if (!code) return;
      ctl::assign(binds2_.keys, capture_action_, code, 0);
    } else {
      if (padNow < 0) return;
      ctl::assign(capture_kind_ == 4 ? binds2_.pad : binds_.pad, capture_action_, padNow, -1);
    }
    saveAllBindings();
    done();
  }
  template <class Edge>
  void menuInput(const Uint8* k, Edge& edge) {
    if (card_req_.exchange(false)) { openMenu(); goPage(40); prev_menu_rb_ = 0xffff; }
    if (!menu_open_ && name_entry_.load()) {
      std::string t;
      for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_Z; ++sc) if (edge(SDL_Scancode(sc))) t += char('A' + (sc - SDL_SCANCODE_A));
      for (int sc = SDL_SCANCODE_1; sc <= SDL_SCANCODE_0; ++sc) if (edge(SDL_Scancode(sc))) t += sc == SDL_SCANCODE_0 ? '0' : char('1' + (sc - SDL_SCANCODE_1));
      if (edge(SDL_SCANCODE_BACKSPACE) || edge(SDL_SCANCODE_DELETE)) t += '\b';
      if (edge(SDL_SCANCODE_RETURN) || edge(SDL_SCANCODE_KP_ENTER)) t += '\n';
      if (!t.empty()) { std::lock_guard lk(card_mtx_); typed_ += t; }
      return;
    }
    if (!menu_open_ && game_list_.load()) {
      const std::uint16_t rb = rawButtons();
      const std::uint16_t e = std::uint16_t(rb & ~game_list_prev_);
      game_list_prev_ = rb;
      if ((e & 0x0080) || edge(SDL_SCANCODE_PAGEUP)) { list_page_req_ = -1; }
      if ((e & 0x0020) || edge(SDL_SCANCODE_PAGEDOWN)) { list_page_req_ = 1; }
      if (edge(SDL_SCANCODE_DELETE)) list_del_req_ = true;
    } else game_list_prev_ = 0xffff;
    if (menu_open_ && menu_page_ == 40 && edge(SDL_SCANCODE_DELETE)) {
      auto its = menuItems();
      if (menu_sel_ >= 0 && menu_sel_ < int(its.size())) {
        const int id = its[std::size_t(menu_sel_)].id;
        if (id >= 950 && !(card_save_ && id == 950)) { card_del_idx_ = id - 950; menuSound(false); goPage(41, 2); return; }
      }
    }
    const bool startRaw = (rawButtons() & 0x0008) != 0;
    if (!startRaw) start_block_ = false;
    const bool escE = edge(SDL_SCANCODE_ESCAPE);
    std::uint32_t pad = 0;
    if (pad_) {
      auto b = [&](SDL_GameControllerButton c, std::uint32_t bit) { if (SDL_GameControllerGetButton(pad_, c)) pad |= bit; };
      b(SDL_CONTROLLER_BUTTON_START, 1); b(SDL_CONTROLLER_BUTTON_DPAD_UP, 2); b(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 4);
      b(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 8); b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 16); b(SDL_CONTROLLER_BUTTON_A, 32);
      b(SDL_CONTROLLER_BUTTON_B, 64); b(SDL_CONTROLLER_BUTTON_GUIDE, 128);
      b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 256); b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 512);
      const int ly = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY), lx = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX);
      if (ly < -20000) pad |= 2; if (ly > 20000) pad |= 4; if (lx < -20000) pad |= 8; if (lx > 20000) pad |= 16;
    }
    const std::uint32_t padE = pad & ~prev_pad_menu_;
    prev_pad_menu_ = pad;
    const bool altHeld = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
    const bool enterE = (edge(SDL_SCANCODE_RETURN) | edge(SDL_SCANCODE_KP_ENTER)) && !altHeld, spaceE = edge(SDL_SCANCODE_SPACE);
    const bool upE = edge(SDL_SCANCODE_UP) | edge(SDL_SCANCODE_W), downE = edge(SDL_SCANCODE_DOWN) | edge(SDL_SCANCODE_S);
    const bool leftE = edge(SDL_SCANCODE_LEFT) | edge(SDL_SCANCODE_A), rightE = edge(SDL_SCANCODE_RIGHT) | edge(SDL_SCANCODE_D);
    const bool backE = edge(SDL_SCANCODE_BACKSPACE);
    if (!menu_open_ && title_sub_.load()) {

      if (!sub_prev_) { sub_sel_ = std::clamp(int(ramWord(0x80157424u) & 0xff), 0, 2); title_row_snap_ = true; prev_sub_raw_ = 0xffff; }
      sub_prev_ = true;
      const std::uint16_t rb = rawButtons();
      const std::uint16_t e = std::uint16_t(rb & ~prev_sub_raw_);
      prev_sub_raw_ = rb;
      std::vector<int> its;
      for (int i = 0; i < 3; ++i) if (titleWordOk(2 + i)) its.push_back(i);
      if (its.empty()) its = {0, 1, 2};
      int idx = int(std::find(its.begin(), its.end(), sub_sel_) - its.begin());
      if (idx >= int(its.size())) { idx = 0; sub_sel_ = its[0]; }
      if ((e & 0x0080) && idx > 0) { sub_sel_ = its[std::size_t(idx - 1)]; menuSound(false); }
      if ((e & 0x0020) && idx + 1 < int(its.size())) { sub_sel_ = its[std::size_t(idx + 1)]; menuSound(false); }
      if (((e & 0x4008) || enterE) && sub_go_.load() < 0 && pad_seq_.empty()) { sub_go_ = sub_sel_; return; }
      if (((e & 0x1000) || backE) && pad_seq_.empty()) { sub_back_ = true; menuSound(false); return; }
    } else sub_prev_ = false;
    if (!menu_open_ && title_top_.load()) {

      if (!title_prev_top_) {
        title_cont_path_ = continueTarget();
        title_cont_ok_ = !title_cont_path_.empty();
        title_row_snap_ = true;
      }
      title_prev_top_ = true;
      const std::uint16_t rb = rawButtons();
      const std::uint16_t e = std::uint16_t(rb & ~prev_title_raw_);
      prev_title_raw_ = rb;
      const auto items = titleItems();
      int idx = int(std::find(items.begin(), items.end(), title_sel_) - items.begin());
      if (idx >= int(items.size())) { idx = 0; title_sel_ = 0; }

      if ((e & 0x0080) && idx > 0) { title_sel_ = items[std::size_t(idx - 1)]; menuSound(false); }
      if ((e & 0x0020) && idx + 1 < int(items.size())) { title_sel_ = items[std::size_t(idx + 1)]; menuSound(false); }
      if (std::getenv("SF3N_TITLELOG") && (e & 0x00a0)) std::fprintf(log, "titulo: seleccion %d campo %llu\n", title_sel_, (unsigned long long)field_count_);
      if ((title_sel_ == 0 || title_sel_ == 3) && ((e & 0x4008) || enterE) && title_go_.load() < 0 && pad_seq_.empty()) {
        title_go_ = title_sel_ == 0 ? 0 : 1;
        return;
      }
      if (title_sel_ == 1 && ((e & 0x4008) || enterE)) { if (kOldOpc) openTitleOptions(); else openExitConfirm(); return; }
      if (title_sel_ == 2 && ((e & 0x4008) || enterE) && title_cont_ok_) {
        menuSound(true);
        requestLoad(title_cont_path_);
        title_sel_ = 0;
        return;
      }
    } else if (!menu_open_) { title_prev_top_ = false; if (title_sel_ && std::getenv("SF3N_TITLELOG")) std::fprintf(log, "titulo: seleccion a 0 (fuera del titulo) campo %llu\n", (unsigned long long)field_count_); title_sel_ = 0; prev_title_raw_ = 0xffff; }
    if (exit_fade_t_ > 0) return;
    if (!menu_open_) {

      if (escE) { if (nativeActive()) inject_start_ = 4; else inject_back_ = 4; }
      if (edge(SDL_SCANCODE_F12) || (padE & 128)) { openMenu(); menu_page_ = 1; menu_sel_ = 0; }
      return;
    }
    auto items = menuItems();
    const int n = int(items.size());

    const std::uint16_t rbNow = rawButtonsBase();
    const std::uint16_t rbE = std::uint16_t(rbNow & ~prev_menu_rb_);
    prev_menu_rb_ = rbNow;
    const bool gUp = rbE & 0x0010, gDown = rbE & 0x0040, gLeft = rbE & 0x0080, gRight = rbE & 0x0020;
    const bool gAccept = rbE & 0x4000, gBack = (rbE & 0x1000) || (rbE & 0x2000);
    const bool gTabL = rbE & 0x0400, gTabR = rbE & 0x0800;
    const int selBefore = menu_sel_;
    if (gUp) menu_sel_ = (menu_sel_ + n - 1) % n;
    if (gDown) menu_sel_ = (menu_sel_ + 1) % n;
    const int first = menuFirst(n);
    const int shown = std::min(n, menuVisible());

    (void)first; (void)shown;
    (void)mouse_wheel_.exchange(0);
    { int mx = 0, my = 0; prev_mouse_ = SDL_GetMouseState(&mx, &my); last_mouse_x_ = mx; last_mouse_y_ = my; }
    int hover = -1;
    bool clickE = false;
    const bool rclickE = false;
    if (launcher_ && menu_page_ == 10) {
      int mx = 0, my = 0;
      const Uint32 mb = SDL_GetMouseState(&mx, &my);
      hover = launcherHit(mx, my);
      static Uint32 prevMb = 0;
      clickE = (mb & SDL_BUTTON_LMASK) && !(prevMb & SDL_BUTTON_LMASK);
      prevMb = mb;
      static int prevHover = -1;
      if (hover >= 0 && hover != prevHover) menu_sel_ = hover;
      prevHover = hover;
      if (clickE && hover >= 0) menu_sel_ = hover;
    }
    menu_sel_ = std::clamp(menu_sel_, 0, n - 1);
    const int id = items[std::size_t(menu_sel_)].id;
    const int pageBefore = menu_page_;
    if (enterE || gAccept || (clickE && hover >= 0)) { start_block_ = startRaw; menuSound(true); menuActivate(id, 0); }
    else if (gLeft && adjustable(id)) { menuSound(false); menuActivate(id, -1); }
    else if (gRight && adjustable(id)) { menuSound(false); menuActivate(id, +1); }
    else if (title_opts_ && menu_page_ != 36 && menu_page_ != 39 && (gTabL || gTabR || edge(SDL_SCANCODE_TAB))) {

      const int t = titleTabOf(menu_page_);
      menuSound(false);
      goPage(kTitleTabPages[gTabL ? (t + 3) % 4 : (t + 1) % 4]);
    }
    else if (launcher_ && menu_page_ == 10 && (leftE || rightE || gLeft || gRight)) {

      menu_sel_ = (leftE || gLeft) ? (menu_sel_ + 2) % 3 : (menu_sel_ + 1) % 3;
    }
    else if (escE || backE || gBack || (padE & 128) || rclickE || ((padE & 1) && menu_page_ == 0)) {
      menuSound(false);
      menuBack(startRaw);
    }
    if (menu_open_ && menu_sel_ != selBefore && menu_page_ == pageBefore) menuSound(false);
    (void)upE; (void)downE; (void)leftE; (void)rightE; (void)spaceE;
  }
  void menuRect(float x0, float y0, float x1, float y1, u_char r, u_char g, u_char b, bool semi) {
    POLY_F4 q{};
    setPolyF4(&q);
    setRGB0(&q, r, g, b);
    if (semi) setSemiTrans(&q, 1);
    setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
    DrawPrim(&q);
  }

  static bool englishUi() { return g_idioma_actual && *g_idioma_actual == "en"; }
  static int uiLang() { return !g_idioma_actual ? 0 : *g_idioma_actual == "en" ? 1 : *g_idioma_actual == "ru" ? 2 : 0; }
  float textWidth(const std::string& s0, float px) const {
    const std::string s = pctext::translateLang(s0, uiLang());
    if (gameFontReady()) return gameTextWidth(s, px * 1.6f, px >= 1.5f);
    return float(pcmenu::plain(s).size()) * 6.0f * px;
  }
  void menuText(const std::string& str0, float x, float y, float px, u_char r, u_char g, u_char b) {
    const std::string str = pctext::translateLang(str0, uiLang());
    if (gameFontReady()) { gameText(str, x, y - px * 1.6f * 0.5f, px * 1.6f, r, g, b, px >= 1.5f); return; }
    const std::string s = pcmenu::plain(str);
    float cx = x;
    for (char c : s) {
      if (const auto* gl = pcmenu::glyph(c)) {
        for (int row = 0; row < 7; ++row) {
          const auto bits = gl->rows[std::size_t(row)];
          int col = 0;
          while (col < 5) {
            if (!(bits & (0x10 >> col))) { ++col; continue; }
            int run = col;
            while (run < 5 && (bits & (0x10 >> run))) ++run;
            menuRect(cx + float(col) * px, y + float(row) * px, cx + float(run) * px, y + float(row + 1) * px, r, g, b, false);
            col = run;
          }
        }
      }
      cx += 6.0f * px;
    }
  }
  void drawPcMenu() {
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);

    DR_TPAGE t{};
    SetDrawTPage(&t, 1, 0, GetTPage(0, 0, 0, 0));
    DrawPrim(&t);
    menuRect(-2000, -100, 2400, 400, 0, 0, 0, true);
    const auto all = menuItems();
    const int first = menuFirst(int(all.size()));
    const std::vector<MenuItem> items(all.begin() + first, all.begin() + std::min<int>(int(all.size()), first + menuVisible()));
    const int selLocal = menu_sel_ - first;
    const float top = menuTop(items.size());
    const float px = 0.62f;
    float wmax = 0;
    for (const auto& it : all) wmax = std::max(wmax, textWidth(it.text, px));
    if (title_opts_) wmax = std::max(wmax, 230.0f);
    const float panelL = 192.0f - wmax * 0.5f - 18.0f, panelR = 192.0f + wmax * 0.5f + 18.0f;
    menuRect(panelL, top - 26.0f, panelR, top + float(items.size()) * kMenuLine + 6.0f, 0, 0, 0, true);
    DR_TPAGE t2{};
    SetDrawTPage(&t2, 1, 0, GetTPage(0, 0, 0, 0));
    DrawPrim(&t2);

    const float pT = top - 26.0f, pB = top + float(items.size()) * kMenuLine + 6.0f;
    menuRect(panelL, pT, panelR, pB, 6, 8, 40, false);
    for (float gx = panelL + 6.0f; gx < panelR; gx += 12.0f) menuRect(gx, pT, gx + 0.35f, pB, 30, 38, 110, false);
    for (float gy = pT + 6.0f; gy < pB; gy += 12.0f) menuRect(panelL, gy, panelR, gy + 0.35f, 30, 38, 110, false);
    for (int k = 0; k < 2; ++k) {
      const float o = k == 0 ? 0.0f : 2.2f;
      const u_char br = k == 0 ? 90 : 60, bg = k == 0 ? 120 : 80, bb = k == 0 ? 255 : 200;
      menuRect(panelL - o, pT - o, panelR + o, pT - o + 0.8f, br, bg, bb, false);
      menuRect(panelL - o, pB + o - 0.8f, panelR + o, pB + o, br, bg, bb, false);
      menuRect(panelL - o, pT - o, panelL - o + 0.8f, pB + o, br, bg, bb, false);
      menuRect(panelR + o - 0.8f, pT - o, panelR + o, pB + o, br, bg, bb, false);
    }
    std::string title;
    switch (menu_page_) {
      case 0: title = "PAUSA"; break;
      case 1: title = "OPCIONES DEL PC"; break;
      case 2: title = "ARMAS"; break;
      case 3: title = "TECLADO Y RATÓN"; break;
      case 4: title = "MANDO"; break;
      case 5: title = "TECLADO DEL JUGADOR 2"; break;
      case 11: title = launcher_ ? "" : "GRÁFICOS"; break;
      case 13: title = "ELEGIR MISIÓN"; break;
      case 38: title = "DEPURACIÓN: TRUCOS E IR A UNA MISIÓN"; break;
      case 40: title = card_save_ ? "GUARDAR PARTIDA" : "CARGAR PARTIDA"; break;
      case 41: title = "¿BORRAR ESTA PARTIDA?"; break;
      case 45: title = "EN LÍNEA"; break;
      case 46: title = "TRUCOS"; break;
      case 14: title = "CARGAR PARTIDA"; break;
      case 15: title = "GUARDAR PARTIDA"; break;
      case 37: title = "RUEDA DEL RATÓN"; break;
      default: break;
    }
    if (title_opts_) {
      switch (menu_page_) {
        case 34: title = "CONTROL P1"; break;
        case 35: title = "CONTROL P2"; break;
        case 36: title = "AVISO"; break;
        case 37: title = "RUEDA DEL RATÓN"; break;
        case 39: title = "¿SALIR DEL JUEGO?"; break;
        default: title = "OPCIONES"; break;
      }

      if (menu_page_ != 36 && menu_page_ != 39) {
      const char* tabs[4] = {"GENERAL", "GRÁFICOS", "SONIDO", "CONTROLES"};
      const int active = titleTabOf(menu_page_);
      for (int i = 0; i < 4; ++i) {
        const float cx = 192.0f + (float(i) - 1.5f) * 74.0f, ty = pT - 16.0f;
        const bool on = i == active;
        menuRect(cx - 34.0f, ty, cx + 34.0f, ty + 11.0f, on ? 60 : 20, on ? 80 : 26, on ? 200 : 90, false);
        menuRect(cx - 33.4f, ty + 0.6f, cx + 33.4f, ty + 10.4f, on ? 30 : 6, on ? 40 : 8, on ? 120 : 40, false);
        menuText(tabs[i], cx - textWidth(tabs[i], 0.62f) * 0.5f, ty + 2.8f, 0.62f, on ? 255 : 150, on ? 255 : 165, on ? 255 : 210);
      }
      }
    }
    menuText(title, 192.0f - textWidth(title, 0.9f) * 0.5f, top - 20.0f, 0.9f, 140, 170, 255);
    for (std::size_t i = 0; i < items.size(); ++i) {
      const float y = top + float(i) * kMenuLine;
      const bool sel = int(i) == selLocal;
      if (sel) {
        menuRect(panelL + 6.0f, y - 3.0f, panelR - 6.0f, y + 7.0f * px + 3.0f, 60, 80, 200, false);
        menuRect(panelL + 6.6f, y - 2.4f, panelR - 6.6f, y + 7.0f * px + 2.4f, 30, 40, 120, false);
      } else {
        menuRect(panelL + 6.0f, y - 3.0f, panelR - 6.0f, y - 2.6f, 60, 80, 180, false);
        menuRect(panelL + 6.0f, y + 7.0f * px + 2.6f, panelR - 6.0f, y + 7.0f * px + 3.0f, 60, 80, 180, false);
      }
      const float w = textWidth(items[i].text, px);
      menuText(items[i].text, 192.0f - w * 0.5f, y, px, sel ? 255 : 190, sel ? 255 : 200, sel ? 255 : 220);
    }

    if (first > 0) menuText("+ + +", 192.0f - textWidth("+ + +", 0.5f) * 0.5f, top - 9.0f, 0.5f, 120, 140, 220);
    if (first + int(items.size()) < int(all.size())) menuText("+ + +", 192.0f - textWidth("+ + +", 0.5f) * 0.5f, top + float(items.size()) * kMenuLine - 3.5f, 0.5f, 120, 140, 220);
    std::string hint;
    if (last_input_pad_ && pad_) {
      const auto fam = ctl::padFamily(pad_);
      hint = ctl::padLabel(SDL_CONTROLLER_BUTTON_A, fam) + ": ELEGIR  -  " + ctl::padLabel(SDL_CONTROLLER_BUTTON_B, fam) + ": VOLVER";
    } else {
      hint = "ENTER: ELEGIR  -  ESC: VOLVER";
    }
    if (menu_page_ == 1) hint += "  -  < >: CAMBIAR";
    if (menu_page_ == 40) hint += "  -  SUPR: BORRAR";
    if (menu_page_ == 3 || menu_page_ == 4 || menu_page_ == 5 || menu_page_ == 34 || menu_page_ == 35) hint += "  -  ELIGE UNA ACCIÓN PARA CAMBIARLA";
    if (title_opts_ && menu_page_ != 34 && menu_page_ != 35 && menu_page_ != 36 && menu_page_ != 39) hint += "  -  L1/R1 O TAB: PESTAÑA";
    if (title_opts_ && menu_page_ == 30 && lang_ != lang_start_ && !launcher_) {
      const std::string a = "AVISO: PARA CAMBIAR EL IDIOMA HAY QUE REINICIAR EL JUEGO (SE PREGUNTA AL VOLVER)";
      menuText(a, 192.0f - textWidth(a, 0.5f) * 0.5f, 214.0f, 0.5f, 255, 210, 120);
    }
    if (title_opts_ && menu_page_ == 36) {
      const std::string a = "EL JUEGO SE REINICIARÁ DEBIDO A LOS CAMBIOS", b2 = "REALIZADOS EN EL IDIOMA. ¿REINICIAR AHORA?";
      const float bw = std::max(textWidth(a, 0.62f), textWidth(b2, 0.62f)) * 0.5f + 12.0f;
      menuRect(192.0f - bw, top - 62.0f, 192.0f + bw, top - 34.0f, 6, 8, 40, false);
      menuRect(192.0f - bw, top - 62.0f, 192.0f + bw, top - 61.2f, 255, 200, 90, false);
      menuRect(192.0f - bw, top - 34.8f, 192.0f + bw, top - 34.0f, 255, 200, 90, false);
      menuText(a, 192.0f - textWidth(a, 0.62f) * 0.5f, top - 56.0f, 0.62f, 255, 220, 140);
      menuText(b2, 192.0f - textWidth(b2, 0.62f) * 0.5f, top - 46.0f, 0.62f, 255, 220, 140);
    }
    menuText(hint, 192.0f - textWidth(hint, 0.45f) * 0.5f, 226.0f, 0.45f, 150, 160, 190);
    if (capture_kind_) {

      const std::string l1 = capture_kind_ == 1 ? "PULSA UNA TECLA O UN BOTÓN DEL RATÓN PARA" : capture_kind_ == 3 ? "PULSA UNA TECLA PARA" : "PULSA UN BOTÓN DEL MANDO PARA";
      const std::string l2 = ctl::kActions[capture_action_].label;
      const std::string l3 = "ESC: CANCELAR  -  SUPR: DEJAR SIN ASIGNAR";
      const float w = std::max({textWidth(l1, 0.6f), textWidth(l2, 0.9f), textWidth(l3, 0.45f)}) * 0.5f + 16.0f;
      menuRect(192.0f - w, 92.0f, 192.0f + w, 150.0f, 6, 8, 40, false);
      menuRect(192.0f - w, 92.0f, 192.0f + w, 92.8f, 90, 120, 255, false);
      menuRect(192.0f - w, 149.2f, 192.0f + w, 150.0f, 90, 120, 255, false);
      menuRect(192.0f - w, 92.0f, 192.0f - w + 0.8f, 150.0f, 90, 120, 255, false);
      menuRect(192.0f + w - 0.8f, 92.0f, 192.0f + w, 150.0f, 90, 120, 255, false);
      menuText(l1, 192.0f - textWidth(l1, 0.6f) * 0.5f, 101.0f, 0.6f, 200, 210, 240);
      menuText(l2, 192.0f - textWidth(l2, 0.9f) * 0.5f, 115.0f, 0.9f, 255, 255, 255);
      menuText(l3, 192.0f - textWidth(l3, 0.45f) * 0.5f, 137.0f, 0.45f, 150, 160, 190);
    }
  }

  void updatePad(const Uint8* k) {
    const auto now = Clock::now();
    if (now >= pad_scan_at_) {
      pad_scan_at_ = now + std::chrono::milliseconds(700);
      if (pad_ && !SDL_GameControllerGetAttached(pad_)) { SDL_GameControllerClose(pad_); pad_ = nullptr; notify("mando desconectado"); }
      if (pad2_ && !SDL_GameControllerGetAttached(pad2_)) { SDL_GameControllerClose(pad2_); pad2_ = nullptr; notify("mando del jugador 2 desconectado"); }
      if (!pad_ && pad2_) { pad_ = pad2_; pad2_ = nullptr; }
      if (!pad_ || !pad2_) {
        SDL_GameControllerUpdate();
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
          if (!SDL_IsGameController(i)) continue;
          const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
          auto same = [&](SDL_GameController* c) { return c && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == id; };
          if (same(pad_) || same(pad2_)) continue;
          SDL_GameController* c = SDL_GameControllerOpen(i);
          if (!c) continue;
          const char* n = SDL_GameControllerName(c);
          if (!pad_) { pad_ = c; notify(std::string("mando conectado: ") + (n ? n : "")); }
          else { pad2_ = c; notify(std::string("mando del jugador 2: ") + (n ? n : "")); break; }
        }
      }
    }
    if (ctl::padPressed(pad_) >= 0) last_input_pad_ = true;
    else {
      for (int i = 1; i < SDL_NUM_SCANCODES; ++i) if (k[i]) { last_input_pad_ = false; break; }
    }

    const std::uint8_t sm = rumble_enabled_ && !menu_open_ ? rumble_small_.load() : 0, lg = rumble_enabled_ && !menu_open_ ? rumble_large_.load() : 0;
    if (pad_ && (sm != rumble_sent_small_ || lg != rumble_sent_large_ || ((sm || lg) && now - rumble_sent_at_ > std::chrono::milliseconds(80)))) {
      SDL_GameControllerRumble(pad_, Uint16(lg * 257), Uint16(sm ? 0xc000 : 0), (sm || lg) ? 200 : 0);
      rumble_sent_small_ = sm; rumble_sent_large_ = lg; rumble_sent_at_ = now;
    }
  }
  void handleKeys() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    auto edge = [&](SDL_Scancode s) { const bool down = k[s] != 0; const bool e = down && !prev_keys_[s]; prev_keys_[s] = down; return e; };

    static const bool fakeRt = std::getenv("SF3N_FAKEMOUSE_RT") != nullptr;
    const bool want = mouse_enabled_ && !two_player_.load() && !menu_open_ && nativeActive() && (fakeRt || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS));
    if (want != mouse_captured_) {
      SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE);
      if (want) SDL_GetRelativeMouseState(nullptr, nullptr);
      mouse_captured_ = want;
      mouse_acc_x_ = 0; mouse_acc_y_ = 0;
    }
    if (mouse_captured_) {
      int dx = 0, dy = 0;
      SDL_GetRelativeMouseState(&dx, &dy);
      mouseAdd(dx, dy);
    }
    if (static const char* fm = std::getenv("SF3N_FAKEMOUSE_RT"); fm) {
      static double last = nowSeconds(), fx = 0, fy = 0;
      const double now = nowSeconds(), dt = now - last; last = now;
      double t0 = 0, t1 = 0, vx = 0, vy = 0;
      const double t = double(cur().field) / 50.0;
      if (std::sscanf(fm, "%lf:%lf:%lf:%lf", &t0, &t1, &vx, &vy) == 4 && t >= t0 && t < t1) {
        mouse_captured_ = true;
        fx += vx * dt; fy += vy * dt;
        const int ix = int(fx), iy = int(fy); fx -= ix; fy -= iy;
        mouseAdd(ix, iy);
        if (std::getenv("SF3N_FAKEDBG")) std::fprintf(log, "FAKE t %.2f cap %d want %d en %d menu %d act %d acc %d yaw_acc %.1f\n", t, int(mouse_captured_), int(want), int(mouse_enabled_), int(menu_open_), int(nativeActive()), mouse_acc_x_.load(), yaw_accum_);
      }
    }

    if ((k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT]) && edge(SDL_SCANCODE_RETURN)) {
      full_ = !full_;
      SDL_SetWindowFullscreen(g_window, full_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    bool f11 = false;
    if (static const char* fm = std::getenv("SF3N_F11_AT"); fm && !menu_open_) {
      static bool done = false; if (!done && double(cur().field) / 50.0 >= std::atof(fm)) { done = true; f11 = true; }
    }
    if (f11 && !menu_open_ && !launcher_) {
      if (!debugMissionsAvailable()) notify("menu de depuracion: solo con el disco USA");
      else { openMenu(); goPage(38, std::max(0, debug_cur_mission_.load())); }
    }
    if (g_psyx_quit_requested) quit = true;
    updatePad(k);
    if (capture_kind_) { captureInput(k); return; }
    menuInput(k, edge);
  }

  void pullShared() {
    std::string title;
    {
      std::lock_guard lk(mtx_);
      rhist_ = pub_hist_;
      hud_hist_ = pub_hud_hist_;
      field_time_ = pub_field_time_;
      field_no_ = pub_field_no_;

      for (std::size_t j = rhist_.size(); j-- > 1;)
        if (rhist_[j]->time - rhist_[j - 1]->time > 0.3) { rhist_.erase(rhist_.begin(), rhist_.begin() + std::ptrdiff_t(j)); break; }
      frame_field_ = pub_frame_field_;

      game_frozen_ = last_path_ == 0 && !rhist_.empty() && field_no_ >= rhist_.back()->field + 3 && frame_field_ <= rhist_.back()->field + 2 && pub_frame_seq_ == frame_seq_seen_ && !pub_disp_black_;
      if (pub_vram_seq_ != vram_seq_seen_ && !game_frozen_) {
        vram_seq_seen_ = pub_vram_seq_;
        vram_.swap(pub_vram_);
        tex_hash_ = pub_tex_hash_;
        disp_black_ = pub_disp_black_;
        disp_ = pub_disp_;
        hud_.swap(pub_hud_);
        hud_ok_ = pub_hud_ok_;
        if (pub_frame_seq_ != frame_seq_seen_) { frame_seq_seen_ = pub_frame_seq_; frame_.swap(pub_frame_); frame_vram_.swap(pub_frame_vram_); frame_vram_dirty_ = true; }
        guest_dirty_ = true;
        guest_vram_dirty_ = true;
      }
      title.swap(pub_title_);
      effects_ = pub_effects_;
      glow_hist_ = pub_glow_hist_;
      tritex_ = pub_tritex_;
      overlay_ = pub_overlay_;
      effects_time_ = pub_effects_time_;
      snow_count_ = pub_snow_count_;
    }

    {
      static const double delay = envInt("SF3N_DELAY_MS", 60) / 1000.0;
      const double T = nowSeconds() - delay;
      render_time_ = T;
      rprev_.reset(); rcur_.reset(); alpha_ = 1.0;
      int i = int(rhist_.size()) - 1;
      while (i >= 0 && rhist_[std::size_t(i)]->time > T) --i;
      if (rhist_.empty()) {
      } else if (i < 0) {
        rcur_ = rhist_.front();
      } else if (std::size_t(i) + 1 < rhist_.size()) {
        rprev_ = rhist_[std::size_t(i)];
        rcur_ = rhist_[std::size_t(i) + 1];
        const double span = rcur_->time - rprev_->time;
        alpha_ = span > 1e-6 ? std::clamp((T - rprev_->time) / span, 0.0, 1.0) : 1.0;
      } else {
        rcur_ = rhist_[std::size_t(i)];
        if (i > 0) rprev_ = rhist_[std::size_t(i) - 1];
        alpha_ = 1.0;
      }
    }
    if (!title.empty()) {
      SDL_SetWindowTitle(g_window, title.c_str());
      notify_until_ = Clock::now() + std::chrono::seconds(3);
    }
  }

  void publishField(const SoftGpu& g) {
    std::lock_guard lk(mtx_);
    pub_vram_.resize(g.vram.size());
    std::memcpy(pub_vram_.data(), g.vram.data(), g.vram.size() * sizeof(g.vram[0]));
    pub_disp_ = GuestDisp{g.disp_x, g.disp_y, g.disp_w, g.disp_h, g.rgb24};
    {

      bool black = !g.rgb24 && g.disp_w > 0 && g.disp_h > 0;
      for (int y = 0; y < g.disp_h && black; y += 3)
        for (int x = 0; x < g.disp_w && black; x += 3)
          if (g.vram[std::size_t(((g.disp_y + y) & 511) * 1024 + ((g.disp_x + x) & 1023))] & 0x7fff) black = false;
      pub_disp_black_ = black;
    }
    pub_hud_ok_ = hud_source && hud_valid && *hud_valid;
    pub_field_no_ = field_count_;
    {

      std::uint64_t h = 1469598103934665603ull;
      for (int y = 0; y < 512; y += 3) {
        const bool fbRow = y < 240 || (y >= 256 && y < 496);
        for (int x = fbRow ? 384 : 0; x < 1024; x += 5) h = (h ^ g.vram[std::size_t(y) * 1024 + std::size_t(x)]) * 1099511628211ull;
      }
      pub_tex_hash_ = h;
      if (static const bool hl = std::getenv("SF3N_HASHLOG") != nullptr; hl) std::fprintf(log, "HASH campo %llu %016llx snap %llu\n", (unsigned long long)field_count_, (unsigned long long)h, (unsigned long long)last_snapshot_field_);
      emu_tex_hash_ = h;
    }
    pub_field_time_ = std::chrono::duration<double>((fast ? Clock::now() : sched_ + field_period_).time_since_epoch()).count();
    if (pub_hud_ok_) {
      pub_hud_ = *hud_source;

      auto sub = radarSub(pub_hud_);
      const int wid = (weapon_cur_addr_ && !ram_.empty()) ? int(ramByte(weapon_cur_addr_)) : -1;
      if (!pub_hud_hist_.empty() && pub_hud_hist_.back().pk == pub_hud_ && sameSub(pub_hud_hist_.back().sub, sub) && pub_hud_hist_.back().wid == wid) pub_hud_hist_.back().t_last = pub_field_time_;
      else {
        pub_hud_hist_.push_back(HudState{pub_field_time_, pub_field_time_, pub_hud_, std::move(sub), wid});
        if (pub_hud_hist_.size() > 6) pub_hud_hist_.erase(pub_hud_hist_.begin());
      }
    } else pub_hud_hist_.clear();
    if (static const char* vd = std::getenv("SF3N_VRAMDUMP"); vd) {
      static long a = -1, b = -1, every = 1; if (a < 0) std::sscanf(vd, "%ld:%ld:%ld", &a, &b, &every);
      if (long(field_count_) >= a && long(field_count_) <= b && (long(field_count_) - a) % std::max(1L, every) == 0) {
        char nm[64]; std::snprintf(nm, sizeof nm, "vram_%llu.bin", (unsigned long long)field_count_);
        if (FILE* f = std::fopen(nm, "wb")) { std::fwrite(pub_vram_.data(), 2, pub_vram_.size(), f); std::fclose(f); }
      }
    }
    if (static const long long hd = std::getenv("SF3N_HUDDUMP") ? std::atoll(std::getenv("SF3N_HUDDUMP")) : -1; hd >= 0 && pub_hud_ok_ && (long long)field_count_ >= hd && ((long long)field_count_ < hd + 6 || (field_count_ - hd) % 50 == 0)) {
      char nm[64]; std::snprintf(nm, sizeof nm, "hud_%llu.txt", (unsigned long long)field_count_);
      if (FILE* f = std::fopen(nm, "w")) { for (const auto& pk : pub_hud_) { for (auto w : pk) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); } std::fclose(f); }
    }
    if (frame_source && frame_seq && *frame_seq != pub_frame_seq_) {
      pub_frame_seq_ = *frame_seq; pub_frame_ = *frame_source;
      pub_frame_field_ = field_count_;

      if (frame_vram_source && frame_vram_source->size() == g.vram.size()) pub_frame_vram_ = *frame_vram_source;
      else pub_frame_vram_.assign(g.vram.begin(), g.vram.end());
    }
    ++pub_vram_seq_;
  }

  std::atomic<double> wide_factor_{1.0};
 public:
  double wideFactor() const { return wide_factor_.load(); }
 private:
  void renderFrame() {
    const auto t0 = Clock::now();
    {
      int ww = 0, wh = 0;
      SDL_GetWindowSize(g_window, &ww, &wh);
      const double f = (g_cfg_aspectMode == PSYX_ASPECT_ORIGINAL_4_3 || wh <= 0) ? 1.0 : std::max(1.0, (double(ww) / double(wh)) / (4.0 / 3.0));
      wide_factor_.store(f);
    }
    if (!menu_open_) pullShared();
    in_play_ = !menu_open_ && nativeActive();
    if (notify_until_.time_since_epoch().count() && t0 > notify_until_) {
      notify_until_ = {};
      SDL_SetWindowTitle(g_window, SF3_TITULO);
    }
    if (nativeActive() && !std::getenv("SF3N_SIN_BORRADO_FONDO")) {
      DRAWENV bg{}; SetDefDrawEnv(&bg, 0, 0, canvas_w, canvas_h); bg.isbg = 1; setRGB0(&bg, 0, 0, 0); bg.dtd = 0; bg.dfe = 1; PutDrawEnv(&bg);
    }
    if (PsyX_BeginScene() == 0) { PsyX_EndScene(); if (PsyX_BeginScene() == 0) return; }
    handleKeys();
    if (nativeActive()) {
      static const bool perf = std::getenv("SF3N_PERFLOG") != nullptr;
      const auto pa = Clock::now();
      drawNativeWorld();
      const auto pb = Clock::now();

      if (g_cfg_smaa) { DrawSync(0); GR_ApplySceneSMAA(); }
      xray_drawn_ = hud_ok_ && drawXrayScope();
      const auto pc = Clock::now();
      if (hud_ok_ && !xray_drawn_) drawScopePic();
      drawHud();
      if (perf) {
        static double sw = 0, sx = 0, sh = 0; static int n = 0;
        const auto pd = Clock::now();
        sw += std::chrono::duration<double, std::milli>(pb - pa).count(); sx += std::chrono::duration<double, std::milli>(pc - pb).count(); sh += std::chrono::duration<double, std::milli>(pd - pc).count();
        if (++n == 30) { std::fprintf(log, "TIEMPOS mundo %.1f ms, mira %.1f ms, hud %.1f ms (rayosx %d)\n", sw / n, sx / n, sh / n, int(xray_drawn_)); sw = sx = sh = 0; n = 0; }
      }
      if (pip_) drawGuestImage(true);
      last_path_ = 0;
    } else if (!drawGuestNative()) {
      drawGuestImage(false);
      last_path_ = disp_.rgb24 ? 3 : 2;
    } else last_path_ = 1;
    if (static const bool pl = std::getenv("SF3N_PATHLOG") != nullptr; pl) std::fprintf(log, "RUTA campo %llu ruta %d ultimo %llu negro %d disp %d,%d %dx%d cuadro %llu paquetes %zu tex %08x/%08x\n", (unsigned long long)field_no_, last_path_, (unsigned long long)(rhist_.empty() ? 0 : rhist_.back()->field), int(disp_black_), disp_.x, disp_.y, disp_.w, disp_.h, (unsigned long long)frame_seq_seen_, frame_.size(), unsigned(tex_hash_ ^ (game_frozen_ ? 0x80000000u : 0)), unsigned(rhist_.empty() ? 0 : rhist_.back()->tex_hash));
    if (static const double mt = std::getenv("SF3N_MENUTEST") ? std::atof(std::getenv("SF3N_MENUTEST")) : -1; mt >= 0) {
      static const double t0m = nowSeconds();
      static int stage = 0, frames = 0;
      if (stage == 0 && nowSeconds() - t0m > mt) { openMenu(); stage = 1; frames = 0; }
      else if (stage == 1 && ++frames == 15) { screenshot("menu_pausa.ppm"); }
      else if (stage == 1 && frames == 20) {
        if (std::getenv("SF3N_MENUWEAPON")) { menuActivate(6, 0); } else if (std::getenv("SF3N_MENUPAGE")) { menu_page_ = std::atoi(std::getenv("SF3N_MENUPAGE")); menu_sel_ = 14; } else { menu_page_ = 1; menu_sel_ = 2; }
      }
      else if (stage == 1 && frames == 38 && std::getenv("SF3N_MENUWEAPON")) {
        for (const auto& w : menu_weapons_) std::fprintf(log, "MENUARMA %d %s %d/%d%s\n", w.index, w.name.c_str(), w.clip, w.reserve, w.current ? " (actual)" : "");
        menuActivate(100 + std::atoi(std::getenv("SF3N_MENUWEAPON")), 0); stage = 2;
      }
      else if (stage == 1 && frames == 35) { screenshot("menu_opciones.ppm"); if (std::getenv("SF3N_MENUPAGE")) startCapture(1, 6); }
      else if (stage == 1 && frames == 39 && capture_kind_) { screenshot("menu_captura.ppm"); }
      else if (stage == 1 && frames == 40) { closeMenu(); stage = 2; }
    }
    if (static const bool ot = std::getenv("SF3N_OPCTEST") != nullptr; ot && title_opts_) {

      static double t0o = 0; static int st = -1;
      if (st < 0) { t0o = nowSeconds(); st = 0; }
      const int pages[] = {30, 31, 32, 33, 34, 35};
      const int want = int((nowSeconds() - t0o) / 1.0);
      static int shot = -1;
      if (std::string(std::getenv("SF3N_OPCTEST")) == "idioma") {
        if (want == 0 && shot < 0) { shot = 0; menuActivate(802, 1); }
        else if (want == 1 && shot == 0) { shot = 1; screenshot("opc_30b.ppm"); closeTitleOptions(); }
        else if (want == 2 && shot == 1) { shot = 2; screenshot("opc_36.ppm"); }
        else if (want == 3 && shot == 2) { shot = 3; menuActivate(840, 0); }
      } else
      if (want < 6) {
        if (menu_page_ != pages[want]) goPage(pages[want]);
        if (shot != want && nowSeconds() - t0o > want + 0.6) { shot = want; char nm[64]; std::snprintf(nm, sizeof nm, "opc_%d.ppm", pages[want]); screenshot(nm); }
      }
      else { closeTitleOptions(); st = -1; }
    }
    if (static const char* ps = std::getenv("SF3N_PRUEBA_SALIR"); ps && title_top_) {
      static double t0 = -1; static int st = 0;
      if (t0 < 0) t0 = nowSeconds();
      const double t = nowSeconds() - t0;
      if (st == 0 && t > 2.0) { st = 1; screenshot("salir_titulo.ppm"); title_sel_ = 1; openExitConfirm(); }
      else if (st == 1 && t > 3.0) { st = 2; screenshot("salir_confirmar.ppm"); menuActivate(860, 0); }
      else if (st == 2 && t > 3.4) { st = 3; screenshot("salir_fundido.ppm"); }
    }
    if (menu_open_) drawPcMenu();
    if (exit_fade_t_ > 0) {
      const double f = std::clamp((nowSeconds() - exit_fade_t_) / kExitFade, 0.0, 1.0);
      exit_fade_vol_ = int(256 * (1.0 - f));
      GR_SetDepthState(0, 0); GR_EnableDepth(0);
      DR_TPAGE ft{}; SetDrawTPage(&ft, 1, 0, GetTPage(0, 2, 0, 0)); DrawPrim(&ft);
      const u_char c = u_char(std::lround(255.0 * f));
      menuRect(-2000, -100, 2400, 400, c, c, c, true);
      if (nowSeconds() - exit_fade_t_ > kExitFade + 0.15) quit = true;
    }
    DrawSync(0);
    PsyX_EndScene();
  }

  const Mission* uploaded_mission_ = nullptr;
  void uploadMissionVram() {
    if (uploaded_mission_ != &mis()) { uploaded_mission_ = &mis(); vram_uploaded_ = false; }
    if (!vram_uploaded_) {
      opc_uploaded_ = false;
      std::vector<std::uint16_t> page(64 * 256);
      auto upload = [&](const std::vector<std::uint16_t>& vram, unsigned bank) {
        if (vram.empty()) return;
        for (unsigned p = 0; p < 32; ++p) {
          const unsigned alias = bank * 32 + p;
          if (alias >= 63) break;
          const int x0 = int(p & 15) * 64, y0 = int(p >> 4) * 256;
          for (int y = 0; y < 256; ++y) std::memcpy(page.data() + y * 64, vram.data() + std::size_t((y0 + y) * 1024 + x0), 64 * 2);
          GR_UploadVRAMAliasPage(int(alias), page.data());
        }
      };
      (void)upload;
      world_alias_ = envInt("SF3N_ALIAS_MUNDO", 1) != 0 && mis().name.rfind("FOG2/", 0) == 0 && mis().vram.size() == 1024 * 512 && mis().vram1.empty();
      if (world_alias_) {
        for (int pg = 0; pg < 32; ++pg) {
          const int x0 = (pg & 15) * 64, y0 = (pg >> 4) * 256;
          for (int y = 0; y < 256; ++y) std::memcpy(page.data() + y * 64, mis().vram.data() + std::size_t((y0 + y) * 1024 + x0), 64 * 2);
          GR_UploadVRAMAliasPage(pg, page.data());
        }
      }

      for (std::size_t g = 0; g < weapon_bank_.groups.size() && weapon_bank_.ok; ++g)
        for (int k = 0; k < 4; ++k) {
          for (int y = 0; y < 256; ++y) std::memcpy(page.data() + y * 64, weapon_bank_.groups[g].data() + std::size_t(y) * 256 + std::size_t(k) * 64, 64 * 2);
          GR_UploadVRAMAliasPage(WeaponBank::kFirstAlias + int(g) * 4 + k, page.data());
        }

      int nextAlias = 32;
      for (auto& md : const_cast<Mission&>(mis()).models) {
        if (md.own_tex.size() != 128 * 256 || nextAlias + 1 >= 62) continue;
        if ((nextAlias & 15) == 15) ++nextAlias;
        for (int half = 0; half < 2; ++half) {
          for (int y = 0; y < 256; ++y) std::memcpy(page.data() + y * 64, md.own_tex.data() + std::size_t(y) * 128 + half * 64, 64 * 2);
          GR_UploadVRAMAliasPage(nextAlias + half, page.data());
        }
        md.own_alias = nextAlias;
        nextAlias += 2;
      }
      vram_uploaded_ = true;
    }
    if (guest_vram_dirty_ && vram_.size() >= 1024 * 512) {
      guest_vram_dirty_ = false;
      uploadVram(vram_);
      if (world_alias_) {

        for (int y = 480; y < 512; ++y) {
          const auto* g = vram_.data() + std::size_t(y) * 1024 + 768;
          const auto* m = mis().vram.data() + std::size_t(y) * 1024 + 768;
          int diff = 0, nz = 0;
          for (int x = 0; x < 256; ++x) { diff += g[x] != m[x]; nz += m[x] != 0; }
          if (static const bool cl = std::getenv("SF3N_ZCLUTLOG") != nullptr; cl && diff > 0 && nz > 0) std::fprintf(log, "ZCLUT campo %llu fila %d distintas %d no-cero %d%s\n", (unsigned long long)field_count_, y, diff, nz, diff > 128 ? " (repuesta)" : "");
          if (diff > 128 && nz > 0) { RECT16 r{768, short(y), 256, 1}; LoadImage(&r, reinterpret_cast<u_long*>(const_cast<std::uint16_t*>(m))); invalidateShadow(y, y + 1); }
        }
      }
      DrawSync(0);
    }
  }

  std::vector<std::uint16_t> vram_shadow_;
  void uploadVram(const std::vector<std::uint16_t>& src) {
    if (src.size() < 1024 * 512) return;
    if (vram_shadow_.size() != 1024 * 512) { vram_shadow_.assign(1024 * 512, 0); invalidateShadow(0, 512); }
    for (int ty = 0; ty < 512; ty += 64) {
      for (int tx = 0; tx < 1024; tx += 64) {
        bool diff = false;
        for (int y = ty; y < ty + 64 && !diff; ++y)
          diff = std::memcmp(src.data() + std::size_t(y) * 1024 + tx, vram_shadow_.data() + std::size_t(y) * 1024 + tx, 128) != 0 || shadow_bad_[std::size_t(y)];
        if (!diff) continue;

        int tx2 = tx + 64;
        while (tx2 < 1024) {
          bool d2 = false;
          for (int y = ty; y < ty + 64 && !d2; ++y)
            d2 = std::memcmp(src.data() + std::size_t(y) * 1024 + tx2, vram_shadow_.data() + std::size_t(y) * 1024 + tx2, 128) != 0 || shadow_bad_[std::size_t(y)];
          if (!d2) break;
          tx2 += 64;
        }
        for (int y = ty; y < ty + 64; ++y) std::memcpy(vram_shadow_.data() + std::size_t(y) * 1024 + tx, src.data() + std::size_t(y) * 1024 + tx, std::size_t(tx2 - tx) * 2);

        upload_tmp_.resize(std::size_t(tx2 - tx) * 64);
        for (int y = 0; y < 64; ++y) std::memcpy(upload_tmp_.data() + std::size_t(y) * std::size_t(tx2 - tx), src.data() + std::size_t(ty + y) * 1024 + tx, std::size_t(tx2 - tx) * 2);
        RECT16 r{short(tx), short(ty), short(tx2 - tx), 64};
        LoadImage(&r, reinterpret_cast<u_long*>(upload_tmp_.data()));
        tx = tx2 - 64;
      }
      for (int y = ty; y < ty + 64; ++y) shadow_bad_[std::size_t(y)] = false;
    }
  }
  std::vector<std::uint16_t> upload_tmp_;
  std::array<bool, 512> shadow_bad_{};
  void invalidateShadow(int y0, int y1) { for (int y = std::max(0, y0); y < std::min(512, y1); ++y) shadow_bad_[std::size_t(y)] = true; }

  struct ViewSetup {
    MATRIX m{};
    std::array<double, 9> exact_rot{};
    std::array<double, 3> exact_t{};
    std::array<double, 3> forward{};
    std::array<double, 3> eye{};
    std::array<double, 3> right{}, down{};
    int h{};
  };

  ViewSetup makeView(const Camera& cam) {
    ViewSetup v;

    const double fy = cam.fy;
    const double fx_canvas = cam.fx * (canvas_w * 0.5) / 184.0;
    v.h = std::max(16, int(std::lround(fy)));
    const double sx = fx_canvas / v.h, sy = fy / v.h;
    const double scale[3] = {sx, sy, 1.0};
    for (int i = 0; i < 3; ++i) {
      double tt = 0;
      for (int j = 0; j < 3; ++j) {
        const double e = cam.rot[i * 3 + j] * scale[i];
        v.exact_rot[i * 3 + j] = e;
        v.m.m[i][j] = static_cast<short>(std::lround(std::clamp(e * 4096.0, -32768.0, 32767.0)));
        tt += e * cam.eye[j];
      }
      v.exact_t[i] = -tt;
      v.m.t[i] = static_cast<long>(std::lround(-tt));
    }
    for (int j = 0; j < 3; ++j) { v.forward[j] = cam.rot[6 + j]; v.eye[j] = cam.eye[j]; v.right[j] = cam.rot[j]; v.down[j] = cam.rot[3 + j]; }
    PGXP_MatrixRegister(&v.m, v.exact_rot.data());
    PGXP_MatrixRegisterTranslation(&v.m, v.exact_t.data());
    return v;
  }

  static float sxOf(long p) { return from_half_float(static_cast<short>(static_cast<std::uint32_t>(p))); }
  static float syOf(long p) { return from_half_float(static_cast<short>(static_cast<std::uint32_t>(p) >> 16)); }
  static void setProjected(VERTTYPE* dst, long packed) {
    const auto bits = static_cast<std::uint32_t>(packed);
    std::memcpy(dst, &bits, sizeof(bits));
  }

  static bool frontFacingIdx(unsigned count, const long* xy, unsigned i, unsigned j, unsigned k) {
    double P[4][2];
    for (unsigned n = 0; n < count; ++n) { P[n][0] = sxOf(xy[n]); P[n][1] = syOf(xy[n]); }
    const auto end = PGXP_GetIndex(0);
    if (end != 0xffffu && end >= count) {
      const auto first = static_cast<ushort>(end - count);
      PGXPVData pv[4]{};
      bool ok = true;
      for (unsigned n = 0; n < count && ok; ++n) ok = PGXP_GetCacheDataExact(&pv[n], ushort(first + n));
      if (ok) for (unsigned n = 0; n < count; ++n) { P[n][0] = pv[n].sx; P[n][1] = pv[n].sy; }
    }
    return (P[j][0] - P[i][0]) * (P[k][1] - P[i][1]) - (P[j][1] - P[i][1]) * (P[k][0] - P[i][0]) > 1e-8;
  }
  static bool frontFacing(unsigned count, long a, long b, long c) {
    double ax = sxOf(a), ay = syOf(a), bx = sxOf(b), by = syOf(b), cx = sxOf(c), cy = syOf(c);
    const auto end = PGXP_GetIndex(0);
    if (end != 0xffffu && end >= count) {
      PGXPVData p0{}, p1{}, p2{};
      const auto first = static_cast<ushort>(end - count);
      if (PGXP_GetCacheDataExact(&p0, first) && PGXP_GetCacheDataExact(&p1, ushort(first + 1)) && PGXP_GetCacheDataExact(&p2, ushort(first + 2))) {
        ax = p0.sx; ay = p0.sy; bx = p1.sx; by = p1.sy; cx = p2.sx; cy = p2.sy;
      }
    }
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax) > 1e-8;
  }

  struct CV { double x, y, z, u, v, r, g, b, depth; };

  std::optional<double> floorAt(double x, double z, double feet) {
    std::optional<double> best;
    for (const auto r : cur().rooms) {
      for (const auto& sec : mis().rooms[r].sections()) {
        const auto& bb = sec.bounds;
        if (x < bb.minimum_x - 1 || x > bb.maximum_x + 1 || z < bb.minimum_z - 1 || z > bb.maximum_z + 1) continue;
        for (const auto& poly : sec.polygons) {
          const unsigned cnt = poly.quad ? 4u : 3u;
          (void)cnt;
          for (unsigned t = 0; t < (poly.quad ? 2u : 1u); ++t) {
            const auto& A = sec.vertices[poly.vertex_indices[t == 0 ? 0 : 1]];
            const auto& B = sec.vertices[poly.vertex_indices[t == 0 ? 1 : 3]];
            const auto& C = sec.vertices[poly.vertex_indices[t == 0 ? 2 : 2]];
            const double ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nl < 1e-6 || std::fabs(ny) < 0.75 * nl) continue;

            const double d = (B.x - A.x) * (C.z - A.z) - (B.z - A.z) * (C.x - A.x);
            if (std::fabs(d) < 1e-9) continue;
            const double l1 = ((B.x - x) * (C.z - z) - (B.z - z) * (C.x - x)) / d;
            const double l2 = ((C.x - x) * (A.z - z) - (C.z - z) * (A.x - x)) / d;
            const double l3 = 1.0 - l1 - l2;
            if (l1 < -0.001 || l2 < -0.001 || l3 < -0.001) continue;
            const double y = l1 * A.y + l2 * B.y + l3 * C.y;
            if (y < feet - 60.0) continue;
            if (!best || y < *best) best = y;
          }
        }
      }
    }
    return best;
  }

  void actorShadow(const ActorPose& actor, const sf::assets::HmdModel& hmd, const Affine& root, const ViewSetup& view) {
    const auto& verts = hmd.vertices();
    double feet = -1e18;
    std::size_t n = 0;
    for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi]) { feet = std::max(feet, world_verts_[vi][1]); ++n; }
    if (n == 0) return;
    const auto fl = floorAt(root.b[0], root.b[2], feet);
    const double ground = fl ? *fl : feet;
    if (ground - feet > 800.0) return;

    std::array<double, 3> target{0.0, 1.0, 0.0};
    if (!actor.lights.empty() && actor.lights[0].valid) {
      const Light& L = actor.lights[0];
      int bi = -1; double bc = 0;
      for (int i = 0; i < 3; ++i) {
        const double c = std::fabs(L.lcm[0 * 3 + std::size_t(i)]) + std::fabs(L.lcm[1 * 3 + std::size_t(i)]) + std::fabs(L.lcm[2 * 3 + std::size_t(i)]);
        const double len = std::sqrt(L.llm[std::size_t(i) * 3] * L.llm[std::size_t(i) * 3] + L.llm[std::size_t(i) * 3 + 1] * L.llm[std::size_t(i) * 3 + 1] + L.llm[std::size_t(i) * 3 + 2] * L.llm[std::size_t(i) * 3 + 2]);
        if (len > 100 && c > bc) { bc = c; bi = i; }
      }
      if (bi >= 0) {
        const double lx = L.llm[std::size_t(bi) * 3], ly = L.llm[std::size_t(bi) * 3 + 1], lz = L.llm[std::size_t(bi) * 3 + 2];

        double wx = root.a[0] * lx + root.a[1] * ly + root.a[2] * lz;
        double wy = root.a[3] * lx + root.a[4] * ly + root.a[5] * lz;
        double wz = root.a[6] * lx + root.a[7] * ly + root.a[8] * lz;
        const double wl = std::sqrt(wx * wx + wy * wy + wz * wz);
        if (wl > 1e-6) {
          std::array<double, 3> d{-wx / wl, -wy / wl, -wz / wl};
          const bool grazing = d[1] < 0.35;
          if (d[1] < 0.35) d[1] = 0.35;

          const double maxH = (grazing && !roof_top_.empty() && underRoof(root.b[0], root.b[1], root.b[2], 0.0, true)) ? 0.25 : 0.9;
          const double h = std::sqrt(d[0] * d[0] + d[2] * d[2]);
          if (h > maxH * d[1]) { d[0] *= maxH * d[1] / h; d[2] *= maxH * d[1] / h; }
          target = d;
        }
      }
    }
    auto& st = shadow_state_[actor.id];
    if (!st.init) { st.dir = target; st.init = true; }
    else for (int k = 0; k < 3; ++k) st.dir[std::size_t(k)] += (target[std::size_t(k)] - st.dir[std::size_t(k)]) * 0.08;
    st.last_frame = render_frame_;
    const auto d = st.dir;
    if (static const bool sl = std::getenv("SF3N_SHADOWLOG") != nullptr; sl && render_frame_ % 4 == 0)
      std::fprintf(log, "sombra %s id %llu pies %.0f suelo %.0f (%s) dir %.2f %.2f %.2f raiz %.0f %.0f %.0f\n", mis().models[actor.model].name.c_str(), (unsigned long long)actor.id, feet, ground, fl ? "sala" : "pies", d[0], d[1], d[2], root.b[0], root.b[1], root.b[2]);
    if (d[1] < 0.2) return;
    const double plane = ground - 1.0;
    {
      double top = 1e18;
      for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi]) top = std::min(top, world_verts_[vi][1]);
      frame_casters_.push_back(ShadowCaster{root.b[0], root.b[2], top, feet, plane, d});
    }
    auto proj = [&](const std::array<double, 3>& p) {
      const double t = std::max(0.0, (plane - p[1]) / d[1]);
      return std::array<double, 3>{p[0] + d[0] * t, plane, p[2] + d[2] * t};
    };
    auto depthOf = [&](const std::array<double, 3>& w) {
      return view.forward[0] * (w[0] - view.eye[0]) + view.forward[1] * (w[1] - view.eye[1]) + view.forward[2] * (w[2] - view.eye[2]);
    };
    shadow_world_.resize(verts.size());
    for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi]) shadow_world_[vi] = proj(world_verts_[vi]);

    {
      double reach = 0;
      for (std::size_t vi = 0; vi < verts.size(); ++vi)
        if (part_ok_[vi]) reach = std::max(reach, std::hypot(shadow_world_[vi][0] - root.b[0], shadow_world_[vi][2] - root.b[2]));
      floor_tris_.clear();
      for (const auto r : cur().rooms)
        for (const auto& sec : mis().rooms[r].sections()) {
          const auto& bb = sec.bounds;
          if (root.b[0] + reach < bb.minimum_x || root.b[0] - reach > bb.maximum_x || root.b[2] + reach < bb.minimum_z || root.b[2] - reach > bb.maximum_z) continue;
          if (plane < bb.minimum_y - 160 || plane > bb.maximum_y + 160) continue;
          for (const auto& poly : sec.polygons)
            for (unsigned t = 0; t < (poly.quad ? 2u : 1u); ++t) {
              const auto& A = sec.vertices[poly.vertex_indices[t == 0 ? 0 : 1]];
              const auto& B = sec.vertices[poly.vertex_indices[t == 0 ? 1 : 3]];
              const auto& C = sec.vertices[poly.vertex_indices[t == 0 ? 2 : 2]];
              const double ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z, vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
              const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
              const double nl = std::sqrt(nx * nx + ny * ny + nz * nz);
              if (nl < 1e-6 || std::fabs(ny) < 0.75 * nl) continue;
              const double ymid = (A.y + B.y + C.y) / 3.0;
              if (std::fabs(ymid - plane) > 160) continue;
              floor_tris_.push_back({double(A.x), double(A.z), double(B.x), double(B.z), double(C.x), double(C.z), double(A.y), double(B.y), double(C.y)});
            }
        }
    }

    auto floorY = [&](const std::array<double, 3>& w) { return floorYIn(floor_tris_, w, plane); };
    shadow_ok_.assign(verts.size(), 1);
    if (!floor_tris_.empty())
      for (std::size_t vi = 0; vi < verts.size(); ++vi) {
        if (!part_ok_[vi]) continue;
        auto& sw = shadow_world_[vi];
        const auto fy = floorY(sw);
        if (!fy) { shadow_ok_[vi] = 0; continue; }

        const auto& wv = world_verts_[vi];
        const double t = std::max(0.0, (*fy - 1.0 - wv[1]) / d[1]);
        std::array<double, 3> q{wv[0] + d[0] * t, *fy - 1.0, wv[2] + d[2] * t};
        if (const auto fy2 = floorY(q)) q[1] = *fy2 - 1.0;
        sw = q;
      }
    if (static const bool sl2 = std::getenv("SF3N_SHADOWLOG") != nullptr; sl2 && render_frame_ % 4 == 0) {
      double best = 0; std::size_t bv = 0;
      for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi] && shadow_ok_[vi]) { const double r = std::hypot(shadow_world_[vi][0] - root.b[0], shadow_world_[vi][2] - root.b[2]); if (r > best) { best = r; bv = vi; } }
      std::fprintf(log, "  sombra lejos: v%zu a %.0f (mundo %.0f %.0f %.0f -> %.0f %.0f %.0f)\n", bv, best, world_verts_[bv][0], world_verts_[bv][1], world_verts_[bv][2], shadow_world_[bv][0], shadow_world_[bv][1], shadow_world_[bv][2]);
    }
    static const bool dbgc = std::getenv("SF3N_SHADOWDEBUG") != nullptr;

    const u_char shade = dbgc ? 200 : u_char(std::clamp(18.0 + 58.0 * std::clamp((d[1] - 0.2) / 0.8, 0.0, 1.0) * 0.45, 20.0, 44.0));

    frame_casters_.back().shade = shade;
    frame_casters_.back().floor = floor_tris_;
    for (const auto& tri : hmd.triangles()) {
      const auto i0 = tri.vertex_indices[0], i1 = tri.vertex_indices[1], i2 = tri.vertex_indices[2];
      if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size() || !part_ok_[i0] || !part_ok_[i1] || !part_ok_[i2]) continue;
      if (!shadow_ok_[i0] || !shadow_ok_[i1] || !shadow_ok_[i2]) continue;

      {
        const double y0 = shadow_world_[i0][1], y1 = shadow_world_[i1][1], y2 = shadow_world_[i2][1];
        if (std::max({y0, y1, y2}) - std::min({y0, y1, y2}) > 40.0) continue;
      }
      std::array<CV, 3> cv;
      const std::uint16_t idx[3] = {i0, i1, i2};
      for (int c = 0; c < 3; ++c) { const auto& w = shadow_world_[idx[c]]; cv[std::size_t(c)] = CV{w[0], w[1], w[2], 0, 0, double(shade), double(shade), double(shade), depthOf(w)}; }
      emitShadowTriangle(cv);
    }
  }

  static std::optional<double> floorYIn(const std::vector<std::array<double, 9>>& tris, const std::array<double, 3>& w, double plane) {
    std::optional<double> best;
    for (const auto& f : tris) {
      const double d = (f[2] - f[0]) * (f[5] - f[1]) - (f[3] - f[1]) * (f[4] - f[0]);
      if (std::fabs(d) < 1e-9) continue;
      const double l1 = ((f[2] - w[0]) * (f[5] - w[2]) - (f[3] - w[2]) * (f[4] - w[0])) / d;
      const double l2 = ((f[4] - w[0]) * (f[1] - w[2]) - (f[5] - w[2]) * (f[0] - w[0])) / d;
      const double l3 = 1.0 - l1 - l2;
      if (l1 < -0.02 || l2 < -0.02 || l3 < -0.02) continue;
      const double y = l1 * f[6] + l2 * f[7] + l3 * f[8];
      if (!best || std::fabs(y - plane) < std::fabs(*best - plane)) best = y;
    }
    return best;
  }

  void emitShadowTriangle(const std::array<CV, 3>& in) {
    std::array<CV, 4> out{};
    int n = 0;
    for (int i = 0; i < 3; ++i) {
      const CV& a = in[std::size_t(i)];
      const CV& b = in[std::size_t((i + 1) % 3)];
      const bool ain = a.depth >= near_z, bin = b.depth >= near_z;
      if (ain) out[std::size_t(n++)] = a;
      if (ain != bin) {
        const double t = (near_z - a.depth) / (b.depth - a.depth);
        CV c = a;
        c.x = a.x + (b.x - a.x) * t; c.y = a.y + (b.y - a.y) * t; c.z = a.z + (b.z - a.z) * t;
        c.depth = near_z;
        out[std::size_t(n++)] = c;
      }
    }
    for (int k = 1; k + 1 < n; ++k) {
      if (shadow_prims_.size() >= shadow_prims_.capacity()) return;
      const CV* tri[3] = {&out[0], &out[std::size_t(k)], &out[std::size_t(k + 1)]};
      SVECTOR sv[3];
      for (int i = 0; i < 3; ++i) {
        sv[i] = SVECTOR{short(std::lround(tri[i]->x)), short(std::lround(tri[i]->y)), short(std::lround(tri[i]->z)), 0};
        const double ex[3] = {tri[i]->x, tri[i]->y, tri[i]->z};
        PGXP_VectorRegister(&sv[i], ex);
      }
      long xy[3]{}, dq{}, fl{};
      const int depth = RotTransPers3(&sv[0], &sv[1], &sv[2], &xy[0], &xy[1], &xy[2], &dq, &fl);
      auto& p = shadow_prims_.emplace_back();
      setPolyF3(&p);
      setSemiTrans(&p, 1);
      setRGB0(&p, u_char(tri[0]->r), u_char(tri[0]->g), u_char(tri[0]->b));
      setProjected(&p.x0, xy[0]);
      setProjected(&p.x1, xy[1]);
      setProjected(&p.x2, xy[2]);
      addPrim(&ot_shadow_[std::clamp(depth, 1, ot_size - 1)], &p);
    }
  }
  std::vector<std::array<double, 3>> shadow_world_;
  std::vector<std::array<double, 9>> floor_tris_;
  std::vector<char> shadow_ok_;
  struct ShadowCaster { double x, z, top, feet, plane; std::array<double, 3> dir; std::vector<std::array<double, 3>> joints; double shade = 34; std::vector<std::array<double, 9>> floor; };
  std::vector<ShadowCaster> frame_casters_;
  std::vector<std::array<double, 7>> frame_heads_;
  std::vector<double> frame_head_up_;

  void objectShadow(const sf::assets::GmdModel& gmd, const Affine& xf, std::uint32_t drawFlags, const ViewSetup& view) {
    const auto c = xf.apply(0, 0, 0);

    double radius = 0;
    for (const auto& v : gmd.vertices()) radius = std::max(radius, std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
    if (radius > 300.0) return;
    const ShadowCaster* best = nullptr;
    double bd = 15.0 * 15.0;
    for (const auto& sc : frame_casters_) {
      if (c[1] < sc.top - 60.0 || c[1] > sc.feet + 20.0) continue;
      for (const auto& j : sc.joints) {
        const double dx = c[0] - j[0], dy = c[1] - j[1], dz = c[2] - j[2];
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bd) { bd = d2; best = &sc; }
      }
    }
    if (static const bool sl = std::getenv("SF3N_SHADOWLOG") != nullptr; sl && render_frame_ % 4 == 0)
      std::fprintf(log, "sombra objeto %s radio %.0f a %.0f de la mano%s\n", "", radius, best ? std::sqrt(bd) : -1.0, best ? "" : " (sin sombra)");
    if (!best) return;
    const auto d = best->dir;
    auto depthOf = [&](const std::array<double, 3>& w) {
      return view.forward[0] * (w[0] - view.eye[0]) + view.forward[1] * (w[1] - view.eye[1]) + view.forward[2] * (w[2] - view.eye[2]);
    };
    for (const auto& tri : gmd.triangles()) {
      if (tri.degenerate) continue;
      if (tri.flags == 0) continue;
      if (tri.flags == 1 && (drawFlags & 0x80)) continue;
      if (tri.semi_transparent) continue;
      std::array<CV, 3> cv;
      bool ok = true;
      for (int k = 0; k < 3 && ok; ++k) {
        const auto vi = tri.vertex_indices[std::size_t(k)];
        if (vi >= gmd.vertices().size()) { ok = false; break; }
        const auto& v = gmd.vertices()[vi];
        const auto w = xf.apply(v.x, v.y, v.z);
        const double t = std::max(0.0, (best->plane - w[1]) / d[1]);
        std::array<double, 3> p{w[0] + d[0] * t, best->plane, w[2] + d[2] * t};
        if (!best->floor.empty()) {
          const auto fy = floorYIn(best->floor, p, best->plane);
          if (!fy) { ok = false; break; }
          const double t2 = std::max(0.0, (*fy - 1.0 - w[1]) / d[1]);
          p = {w[0] + d[0] * t2, *fy - 1.0, w[2] + d[2] * t2};
        }
        cv[std::size_t(k)] = CV{p[0], p[1], p[2], 0, 0, best->shade, best->shade, best->shade, depthOf(p)};
      }
      if (ok && std::max({cv[0].y, cv[1].y, cv[2].y}) - std::min({cv[0].y, cv[1].y, cv[2].y}) > 40.0) ok = false;
      if (ok) emitShadowTriangle(cv);
    }
  }

  void noteOverflow(const char* what) {
    static int n = 0;
    if (n < 20) { ++n; std::fprintf(log, "nativo: sin sitio para %s (cuadro %llu)\n", what, (unsigned long long)render_frame_); }
  }
  std::array<double, 3> glow_eye_{};
  bool glow_eye_ok_ = false;
  bool no_glow_pull_ = false;
  void emitClippedTriangle(const std::array<CV, 3>& in0, std::uint16_t tpage, std::uint16_t clut, bool semi = false, bool cull = true, bool textured = true) {

    std::array<CV, 3> in = in0;

    auto smallGlow = [&] {
      static const double mx = envInt("SF3N_BRILLO_MAX", 160);
      for (int i = 0; i < 3; ++i) { const auto& a = in0[std::size_t(i)]; const auto& b = in0[std::size_t((i + 1) % 3)];
        if (std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) > mx) return false; }
      return true; };
    if (semi && textured && glow_eye_ok_ && !no_glow_pull_ && ((tpage >> 5) & 3) == 1 && !std::getenv("SF3N_SIN_ACERCAR_BRILLOS") && smallGlow()) {
      for (auto& v : in) {
        const double dx = glow_eye_[0] - v.x, dy = glow_eye_[1] - v.y, dz = glow_eye_[2] - v.z;
        const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-6) continue;
        const double b = std::min(0.5, std::max(40.0 / len, 0.12));
        v.x += dx * b; v.y += dy * b; v.z += dz * b; v.depth *= (1.0 - b);
      }
    }

    std::array<CV, 4> out{};
    int n = 0;
    for (int i = 0; i < 3; ++i) {
      const CV& a = in[i];
      const CV& b = in[(i + 1) % 3];
      const bool ain = a.depth >= near_z, bin = b.depth >= near_z;
      if (ain) out[n++] = a;
      if (ain != bin) {
        const double t = (near_z - a.depth) / (b.depth - a.depth);
        CV c;
        c.x = a.x + (b.x - a.x) * t; c.y = a.y + (b.y - a.y) * t; c.z = a.z + (b.z - a.z) * t;
        c.u = a.u + (b.u - a.u) * t; c.v = a.v + (b.v - a.v) * t;
        c.r = a.r + (b.r - a.r) * t; c.g = a.g + (b.g - a.g) * t; c.b = a.b + (b.b - a.b) * t;
        c.depth = near_z;
        out[n++] = c;
      }
    }

    static const bool sinLimite16 = std::getenv("SF3N_SIN_LIMITE16") != nullptr;
    if (!sinLimite16)
      for (int i = 0; i < n; ++i)
        if (std::fabs(out[i].x) > 32767.0 || std::fabs(out[i].y) > 32767.0 || std::fabs(out[i].z) > 32767.0) return;
    for (int k = 1; k + 1 < n; ++k) {
      const CV* tri[3] = {&out[0], &out[k], &out[k + 1]};
      SVECTOR sv[3];
      for (int i = 0; i < 3; ++i) {
        sv[i] = SVECTOR{short(std::lround(tri[i]->x)), short(std::lround(tri[i]->y)), short(std::lround(tri[i]->z)), 0};
        const double ex[3] = {tri[i]->x, tri[i]->y, tri[i]->z};
        PGXP_VectorRegister(&sv[i], ex);
      }
      long xy[3]{}, dq{}, fl{};
      const int depth = RotTransPers3(&sv[0], &sv[1], &sv[2], &xy[0], &xy[1], &xy[2], &dq, &fl);
      if (cull && !frontFacing(3, xy[0], xy[1], xy[2])) continue;
      auto uc = [](double u) { return static_cast<u_char>(std::clamp(std::lround(u), 0L, 255L)); };
      const double wg = (!room_pass_ && !xray_actors_) ? world_gain_ : 1.0;
      auto cc = [wg](double c) { return static_cast<u_char>(std::clamp(std::lround(c * wg), 0L, 255L)); };
      const bool nvS = nv_flat_ && !xray_actors_ && nv_tint_;
      auto cx = [&](double c) { return nvS ? u_char(0) : cc(c); };
      auto* slot = &(semi ? ot_semi_ : room_pass_ ? ot_room_ : ot_)[std::clamp(depth, 1, ot_size - 1)];
      if (!textured) {
        if (fx_g3_.size() >= fx_g3_.capacity()) continue;
        auto& p = fx_g3_.emplace_back();
        setPolyG3(&p);
        setRGB0(&p, cx(tri[0]->r), cc(tri[0]->g), cx(tri[0]->b));
        setRGB1(&p, cx(tri[1]->r), cc(tri[1]->g), cx(tri[1]->b));
        setRGB2(&p, cx(tri[2]->r), cc(tri[2]->g), cx(tri[2]->b));
        if (semi) setSemiTrans(&p, 1);
        setProjected(&p.x0, xy[0]);
        setProjected(&p.x1, xy[1]);
        setProjected(&p.x2, xy[2]);
        addPrim(slot, &p);
        if (semi && fx_tpages_.size() < fx_tpages_.capacity()) {

          auto& t = fx_tpages_.emplace_back();
          SetDrawTPage(&t, 1, 0, tpage);
          addPrim(slot, &t);
        }
        continue;
      }

      if (tris_.size() >= tris_.capacity()) { noteOverflow("triangulos recortados"); continue; }
      auto& p = tris_.emplace_back();
      setPolyGT3(&p);
      setRGB0(&p, cx(tri[0]->r), cc(tri[0]->g), cx(tri[0]->b));
      setRGB1(&p, cx(tri[1]->r), cc(tri[1]->g), cx(tri[1]->b));
      setRGB2(&p, cx(tri[2]->r), cc(tri[2]->g), cx(tri[2]->b));
      setUV3(&p, uc(tri[0]->u), uc(tri[0]->v), uc(tri[1]->u), uc(tri[1]->v), uc(tri[2]->u), uc(tri[2]->v));
      p.clut = clut;
      p.tpage = tpage;
      if (semi) setSemiTrans(&p, 1);
      setProjected(&p.x0, xy[0]);
      setProjected(&p.x1, xy[1]);
      setProjected(&p.x2, xy[2]);
      addPrim(slot, &p);
    }
  }

  static constexpr int kSnowMaxFlakes = 3500, kSnowTris = kSnowMaxFlakes * 6;
  double snow_level_ = 0, snow_clock_ = 0, snow_last_ = -1;
  bool snow_last_first_ = true;

  const Mission* roof_mission_ = nullptr;
  std::unordered_map<std::int64_t, float> roof_top_;
  bool cold_mission_ = false, snow_mission_ = false;
  std::string roof_name_;
  double snow_since_ = -1.0;
  static std::int64_t roofKey(double x, double z) { return (std::int64_t(std::floor(x / 64.0)) & 0xffffffff) | (std::int64_t(std::floor(z / 64.0)) << 32); }
  void buildRoofs() {
    roof_mission_ = &mis(); roof_name_ = mis().name; roof_top_.clear();

    snow_level_ = 0.0; snow_since_ = -1.0; snow_last_first_ = true; snow_count_ = 0; pub_snow_count_ = 0;
    const std::string lista = std::getenv("SF3N_FRIO_MISIONES") ? std::getenv("SF3N_FRIO_MISIONES") : "RIDGE,ARENA06";
    std::string nm = mis().name; if (auto sl = nm.rfind('/'); sl != std::string::npos) nm = nm.substr(sl + 1); if (auto dt = nm.find('.'); dt != std::string::npos) nm = nm.substr(0, dt);
    cold_mission_ = false;
    {
      const std::string sl = std::getenv("SF3N_NIEVE_MISIONES") ? std::getenv("SF3N_NIEVE_MISIONES") : "SNOWCAMP,RIDGE,ARENA06";
      snow_mission_ = false;
      for (std::size_t a = 0; a <= sl.size();) { auto b = sl.find(',', a); if (b == std::string::npos) b = sl.size(); if (sl.substr(a, b - a) == nm) snow_mission_ = true; a = b + 1; }
    }
    for (std::size_t a = 0; a <= lista.size();) { auto b = lista.find(',', a); if (b == std::string::npos) b = lista.size(); if (lista.substr(a, b - a) == nm) cold_mission_ = true; a = b + 1; }
    for (const auto& room : mis().rooms) for (const auto& sec : room.sections()) for (const auto& pg : sec.polygons) {
      if (!pg.renderable || pg.degenerate) continue;
      const unsigned n = pg.quad ? 4u : 3u; double v[4][3]; bool ok = true;
      for (unsigned k = 0; k < n; ++k) { const auto vi = pg.vertex_indices[k]; if (vi >= sec.vertices.size()) { ok = false; break; } v[k][0] = sec.vertices[vi].x; v[k][1] = sec.vertices[vi].y; v[k][2] = sec.vertices[vi].z; }
      if (!ok) continue;
      const double a[3] = {v[1][0] - v[0][0], v[1][1] - v[0][1], v[1][2] - v[0][2]}, b[3] = {v[2][0] - v[0][0], v[2][1] - v[0][1], v[2][2] - v[0][2]};
      double nx = a[1] * b[2] - a[2] * b[1], ny = a[2] * b[0] - a[0] * b[2], nz = a[0] * b[1] - a[1] * b[0];
      const double ln = std::sqrt(nx * nx + ny * ny + nz * nz); if (ln < 1e-6) continue; nx /= ln; ny /= ln; nz /= ln;
      if (std::fabs(ny) < 0.3) continue;
      double lo[2] = {1e18, 1e18}, hi[2] = {-1e18, -1e18}, ylo = 1e18, yhi = -1e18;
      for (unsigned k = 0; k < n; ++k) { lo[0] = std::min(lo[0], v[k][0]); hi[0] = std::max(hi[0], v[k][0]); lo[1] = std::min(lo[1], v[k][2]); hi[1] = std::max(hi[1], v[k][2]); ylo = std::min(ylo, v[k][1]); yhi = std::max(yhi, v[k][1]); }
      const double d = nx * v[0][0] + ny * v[0][1] + nz * v[0][2];
      auto inXZ = [&](double px, double pz) {
        static const int o4[4] = {0, 1, 3, 2}, o3[3] = {0, 1, 2}; const int* o = n == 4 ? o4 : o3; int sg = 0;
        for (unsigned e = 0; e < n; ++e) { const double* p0 = v[o[e]]; const double* p1 = v[o[(e + 1) % n]];
          const double cr = (p1[0] - p0[0]) * (pz - p0[2]) - (p1[2] - p0[2]) * (px - p0[0]);
          if (cr > 1e-6) { if (sg < 0) return false; sg = 1; } else if (cr < -1e-6) { if (sg > 0) return false; sg = -1; } }
        return true; };
      auto put = [&](double px, double pz) {
        const double y = std::clamp((d - nx * px - nz * pz) / ny, ylo, yhi);
        auto [it, nw] = roof_top_.try_emplace(roofKey(px, pz), float(y)); if (!nw) it->second = std::min(it->second, float(y)); };
      if ((hi[0] - lo[0]) * (hi[1] - lo[1]) > 64.0 * 64.0 * 40000.0) continue;
      for (double x = std::floor(lo[0] / 64.0) * 64.0 + 32.0; x < hi[0]; x += 64.0)
        for (double z = std::floor(lo[1] / 64.0) * 64.0 + 32.0; z < hi[1]; z += 64.0) if (inXZ(x, z)) put(x, z);
      for (unsigned k = 0; k < n; ++k) put(v[k][0], v[k][2]);
    }
    std::fprintf(log, "nativo: frio %d (%s), rejilla de techos %zu celdas\n", int(cold_mission_), nm.c_str(), roof_top_.size());
  }

  bool underRoof(double x, double y, double z, double m = 30.0, bool nearWalls = false) const {
    auto it = roof_top_.find(roofKey(x, z));
    if (it != roof_top_.end() && double(it->second) < y - m) return true;
    if (!nearWalls) return false;

    for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) {
      if (!dx && !dz) continue;
      auto jt = roof_top_.find(roofKey(x + dx * 64.0, z + dz * 64.0));
      if (jt != roof_top_.end() && double(jt->second) < y - m) return true;
    }
    return false;
  }
  void drawSnow(const ViewSetup& view) {
    static const int lvl = std::clamp(envInt("SF3N_NIEVE", 3), 0, 6);
    if (lvl < 2) return;
    const double now = nowSeconds();
    const double dt = snow_last_ < 0 ? 0.0 : std::clamp(now - snow_last_, 0.0, 0.1);
    snow_last_ = now;
    if (roof_mission_ != &mis() || roof_name_ != mis().name) buildRoofs();

    bool gameSnow = now - effects_time_ < 0.5 && snow_count_ > 0;
    if (gameSnow && !snow_mission_) {
      if (snow_count_ < 20) snow_since_ = -1.0;
      else if (snow_since_ < 0) snow_since_ = now;
      gameSnow = snow_since_ >= 0 && now - snow_since_ > 3.0;
    } else if (!gameSnow) snow_since_ = -1.0;
    const bool falling = gameSnow || (cold_mission_ && !game_frozen_ && !menu_frozen_native_ && now - effects_time_ < 0.5);
    const double target = falling ? (snow_count_ > 0 ? std::min(1.0, 0.35 + snow_count_ / 70.0) : 1.0) : 0.0;
    if (snow_last_first_) { snow_level_ = target; snow_last_first_ = false; }
    snow_level_ += (target - snow_level_) * std::min(1.0, dt * 1.2);
    if (!game_frozen_ && !menu_frozen_native_) snow_clock_ += dt;
    if (snow_level_ < 0.01) return;
    const int count = std::min(kSnowMaxFlakes, int(std::lround(700.0 * (lvl - 1) * snow_level_)));
    constexpr double B = 1400.0, H = 1200.0;
    const double T = snow_clock_;
    auto rnd = [](std::uint32_t i, std::uint32_t k) {
      std::uint32_t h = i * 0x9E3779B1u ^ (k + 0x7f4a7c15u) * 0x85EBCA77u;
      h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
      return double(h & 0xffffff) / double(0x1000000);
    };
    auto wrap = [](double v, double c, double L) { double d = std::fmod(v - c + L * 0.5, L); if (d < 0) d += L; return c - L * 0.5 + d; };
    auto smooth = [](double a, double b, double x) { const double t = std::clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); };
    int snow_drawn = 0;
    const double windX = 18.0 * std::sin(T * 0.07), windZ = 12.0 * std::cos(T * 0.05);
    for (int i = 0; i < count; ++i) {
      const double speed = 55.0 + 55.0 * rnd(std::uint32_t(i), 1);
      const double amp = 10.0 + 22.0 * rnd(std::uint32_t(i), 2), fr = 0.35 + 0.7 * rnd(std::uint32_t(i), 3), ph = 6.2832 * rnd(std::uint32_t(i), 4);
      const double px = rnd(std::uint32_t(i), 5) * B + windX * T + amp * std::sin(fr * T + ph);
      const double py = rnd(std::uint32_t(i), 6) * H + speed * T;
      const double pz = rnd(std::uint32_t(i), 7) * B + windZ * T + amp * std::cos(0.8 * fr * T + ph);
      const std::array<double, 3> p{wrap(px, view.eye[0], B), wrap(py, view.eye[1], H), wrap(pz, view.eye[2], B)};
      const double dx = p[0] - view.eye[0], dy = p[1] - view.eye[1], dz = p[2] - view.eye[2];
      const double depth = view.forward[0] * dx + view.forward[1] * dy + view.forward[2] * dz;
      if (depth < 25.0) continue;
      if (!roof_top_.empty() && underRoof(p[0], p[1], p[2], 0.0, true)) continue;
      const double edge = std::max({std::fabs(dx) / (B * 0.5), std::fabs(dy) / (H * 0.5), std::fabs(dz) / (B * 0.5)});
      double fade = smooth(25.0, 90.0, depth) * (1.0 - smooth(0.7, 0.98, edge)) * (1.0 - 0.6 * smooth(300.0, 800.0, depth));
      if (fade < 0.02) continue;
      const double size = 2.6 + 3.0 * rnd(std::uint32_t(i), 8);
      const double c0 = std::min(255.0, (210.0 + 45.0 * rnd(std::uint32_t(i), 9)) * fade);
      CV center{p[0], p[1], p[2], 0, 0, c0, c0, c0 * 1.04, depth};
      std::array<CV, 6> rim{};
      for (int k = 0; k < 6; ++k) {
        const double a = k * (6.2832 / 6.0), ca = std::cos(a) * size, sa = std::sin(a) * size;
        const double q[3] = {p[0] + view.right[0] * ca + view.down[0] * sa, p[1] + view.right[1] * ca + view.down[1] * sa, p[2] + view.right[2] * ca + view.down[2] * sa};
        rim[std::size_t(k)] = CV{q[0], q[1], q[2], 0, 0, 0, 0, 0, depth};
      }
      for (int k = 0; k < 6; ++k) emitClippedTriangle({center, rim[std::size_t(k)], rim[std::size_t((k + 1) % 6)]}, 0x20, 0, true, false, false);
      ++snow_drawn;
    }
    if (static const bool nl = std::getenv("SF3N_NIEVELOG") != nullptr; nl)
      std::fprintf(log, "NIEVE nivel %.2f juego %d copos %d dibujados %d ojo %.0f,%.0f,%.0f fy %d\n", snow_level_, snow_count_, count, snow_drawn, view.eye[0], view.eye[1], view.eye[2], view.h);
  }

  struct BreathEmitter { std::array<double, 3> head; double last_seen = -1e9, last_spawn = -1e9; };
  struct BreathPuff { std::array<double, 3> o, dir; double t0, life, speed, s0, s1, bright, seed; };
  std::vector<BreathEmitter> breath_em_;
  std::vector<BreathPuff> breath_;
  double fx_clock_ = 0, fx_clock_last_ = -1;
  void tickFxClock() {
    const double now = nowSeconds();
    const double dt = fx_clock_last_ < 0 ? 0.0 : std::clamp(now - fx_clock_last_, 0.0, 0.1);
    fx_clock_last_ = now;
    if (!game_frozen_ && !menu_frozen_native_) fx_clock_ += dt;
  }
  void noteBreath(const std::array<double, 3>& c, const std::array<double, 7>& h) {
    const double T = fx_clock_;
    BreathEmitter* em = nullptr;
    for (auto& b : breath_em_) if (std::hypot(b.head[0] - h[0], b.head[1] - h[1], b.head[2] - h[2]) < 60.0) { em = &b; break; }
    if (!em) { breath_em_.push_back({{h[0], h[1], h[2]}}); em = &breath_em_.back(); }
    em->head = {h[0], h[1], h[2]};
    const bool start = T - em->last_seen > 0.18 && T - em->last_spawn > 0.5;
    em->last_seen = T;
    if (!start || breath_.size() > 400) return;
    em->last_spawn = T;
    spawnBreath(c, h);
  }
  void spawnBreath(const std::array<double, 3>& c, const std::array<double, 7>& h) {
    const double T = fx_clock_;
    if (!roof_top_.empty() && underRoof(h[0], h[1], h[2], 0.0)) return;
    if (static const bool vl = std::getenv("SF3N_VAHOLOG") != nullptr; vl) std::fprintf(log, "VAHO bocanada en %.0f,%.0f,%.0f t %.2f\n", h[0], h[1], h[2], T);
    if (static const bool vl = std::getenv("SF3N_VAHOLOG") != nullptr; vl) std::fprintf(log, "VAHO nace en %.0f,%.0f,%.0f t %.2f\n", c[0], c[1], c[2], T);
    (void)c;
    std::array<double, 3> d{h[4], h[5] * 0.4 + 0.12, h[6]};
    const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-3) return;
    for (auto& v : d) v /= l;

    const std::array<double, 3> mouth{h[0] + h[4] * h[3] * 0.85, h[1] + h[3] * 0.3 + h[5] * h[3] * 0.85, h[2] + h[6] * h[3] * 0.85};
    static std::uint32_t seq = 1;
    for (int k = 0; k < 4; ++k) {
      const double r1 = double((++seq * 2654435761u) >> 8 & 0xffff) / 65535.0, r2 = double((++seq * 2246822519u) >> 8 & 0xffff) / 65535.0;
      std::array<double, 3> dd{d[0] + (r1 - 0.5) * 0.35, d[1] + (r2 - 0.5) * 0.25, d[2] + (r2 - 0.5) * 0.35};
      breath_.push_back({mouth, dd, T + k * 0.07, 1.3 + 0.5 * r1, 34.0 + 18.0 * r2, 3.0 + 1.5 * r1, 16.0 + 10.0 * r2, 0.55 + 0.25 * r2, r1 * 6.28});
    }
  }

  void ownBreath() {
    if (roof_mission_ != &mis() || roof_name_ != mis().name) buildRoofs();
    if (!cold_mission_ || game_frozen_ || menu_frozen_native_) return;
    const double T = fx_clock_;
    for (std::size_t hi = 0; hi < frame_heads_.size(); ++hi) {
      const auto& h = frame_heads_[hi];

      if (static const bool vl = std::getenv("SF3N_VAHOLOG") != nullptr; vl && hi < frame_head_up_.size() && (render_frame_ % 50) == 0) std::fprintf(log, "VAHO cabeza %.0f,%.0f,%.0f altura %.0f cara.y %.2f\n", h[0], h[1], h[2], frame_head_up_[hi], h[5]);
      if (hi < frame_head_up_.size() && frame_head_up_[hi] < 85.0) continue;
      if (std::fabs(h[5]) > 0.7) continue;
      BreathEmitter* em = nullptr;
      for (auto& b : breath_em_) if (std::hypot(b.head[0] - h[0], b.head[1] - h[1], b.head[2] - h[2]) < 60.0) { em = &b; break; }
      if (!em) { breath_em_.push_back({{h[0], h[1], h[2]}}); em = &breath_em_.back(); em->last_spawn = T - double((breath_em_.size() * 977u) % 1000u) / 400.0; }
      em->head = {h[0], h[1], h[2]}; em->last_seen = T;
      const double period = 2.2 + 1.8 * double(((std::uint32_t(std::lround(em->last_spawn * 1000.0)) + std::uint32_t(breath_em_.size())) * 2654435761u >> 16) % 1000u) / 1000.0;
      if (T - em->last_spawn < period || breath_.size() > 400) continue;
      em->last_spawn = T;
      spawnBreath({h[0], h[1], h[2]}, h);
    }
  }
  void drawBreath(const ViewSetup& view) {
    static const int vaho = envInt("SF3N_VAHO", 1);
    if (vaho >= 1) ownBreath();
    if (vaho < 1 || breath_.empty()) return;
    const double T = fx_clock_;
    std::erase_if(breath_, [&](const BreathPuff& p) { return T - p.t0 > p.life; });
    std::erase_if(breath_em_, [&](const BreathEmitter& b) { return T - b.last_seen > 10.0; });
    for (const auto& p : breath_) {
      const double age = T - p.t0;
      if (age < 0) continue;
      const double u = age / p.life;
      const double travel = p.speed * (1.0 - std::exp(-age * 2.2)) / 2.2;
      const std::array<double, 3> c{p.o[0] + p.dir[0] * travel + 3.0 * std::sin(age * 2.0 + p.seed),
                                   p.o[1] + p.dir[1] * travel - 9.0 * age,
                                   p.o[2] + p.dir[2] * travel + 3.0 * std::cos(age * 1.7 + p.seed)};
      const double size = p.s0 + (p.s1 - p.s0) * (1.0 - std::exp(-age * 2.5));
      const double a = std::min(1.0, age / 0.08) * std::pow(1.0 - u, 1.6);
      const double c0 = 100.0 * p.bright * a;
      if (c0 < 2.0) continue;
      const double depth = view.forward[0] * (c[0] - view.eye[0]) + view.forward[1] * (c[1] - view.eye[1]) + view.forward[2] * (c[2] - view.eye[2]);
      if (depth < 20.0) continue;
      CV center{c[0], c[1], c[2], 0, 0, c0, c0, c0 * 1.03, depth};
      constexpr int kSeg = 10;
      std::array<CV, kSeg> rim{};
      for (int k = 0; k < kSeg; ++k) {
        const double ang = k * (6.2832 / kSeg), ca = std::cos(ang) * size, sa = std::sin(ang) * size;
        const double q[3] = {c[0] + view.right[0] * ca + view.down[0] * sa, c[1] + view.right[1] * ca + view.down[1] * sa, c[2] + view.right[2] * ca + view.down[2] * sa};
        rim[std::size_t(k)] = CV{q[0], q[1], q[2], 0, 0, 0, 0, 0, depth};
      }
      for (int k = 0; k < kSeg; ++k) emitClippedTriangle({center, rim[std::size_t(k)], rim[std::size_t((k + 1) % kSeg)]}, 0x20, 0, true, false, false);
    }
  }

  struct SmokePuff { std::array<double, 3> o, dir; double t0, life, speed, s0, s1, bright, seed; };
  std::vector<SmokePuff> smoke_;
  std::vector<std::array<double, 4>> smoke_src_;
  static int smokeLevel() { static const int v = std::clamp(xrayCfg("humo_disparo", "SF3N_HUMO_DISPARO", 1), 0, 3); return v; }
  void noteMuzzle(const std::array<double, 3>& c, const Affine& xf) {
    if (smokeLevel() == 0) return;
    const double T = fx_clock_;

    std::erase_if(smoke_src_, [&](const std::array<double, 4>& f) { return T - f[3] > 0.6 || T < f[3]; });
    for (auto& f : smoke_src_) if (std::hypot(f[0] - c[0], f[1] - c[1], f[2] - c[2]) < 40.0) { if (T - f[3] < 0.09) { f[3] = T; return; } }
    smoke_src_.push_back({c[0], c[1], c[2], T});
    if (smoke_.size() > 300) return;

    double d[3] = {c[0] - xf.b[0], c[1] - xf.b[1], c[2] - xf.b[2]};
    double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-6) { d[0] = 0; d[1] = 0; d[2] = 0; l = 1; }
    const int nPuffs = 1 + smokeLevel();
    for (int k = 0; k < nPuffs; ++k) {
      smoke_rng_ = smoke_rng_ * 1664525u + 1013904223u; const double r1 = double((smoke_rng_ >> 8) & 0xffff) / 65535.0;
      smoke_rng_ = smoke_rng_ * 1664525u + 1013904223u; const double r2 = double((smoke_rng_ >> 8) & 0xffff) / 65535.0;
      const std::array<double, 3> dir{d[0] / l, d[1] / l, d[2] / l};
      smoke_.push_back({c, dir, T + k * 0.05, 1.1 + 0.9 * r1, 20.0 + 25.0 * r2, 2.5 + 1.5 * r1, 14.0 + 10.0 * r2 + 4.0 * smokeLevel(), 0.22 + 0.06 * smokeLevel() + 0.08 * r2, r1 * 6.28});
    }
  }
  std::uint32_t smoke_rng_ = 0x2468ace1u;

  struct FlameShot { std::array<double, 3> p, v; double t0, last, life, size; bool dead = false; };
  struct FlameBurst { std::array<double, 3> c, v; double t0, life, s0, s1, seed; int frame; };
  struct FlameBurn { std::uint64_t id; double until; };
  std::vector<FlameShot> flame_shots_;
  std::vector<FlameBurst> flame_bursts_;
  std::vector<FlameBurn> flame_burns_;
  double flame_emit_t_ = -1, flame_last_flash_ = -10, flame_pilot_t_ = -10;
  std::array<double, 3> flame_tip_{}, flame_axis_{0, -1, 0}, flame_fwd_{0, 0, 1};
  static int flameLevel() { static const int v = std::clamp(xrayCfg("lanzallamas_llamas", "SF3N_LANZALLAMAS_LLAMAS", 1), 0, 2); return v; }
  double flameRand() { smoke_rng_ = smoke_rng_ * 1664525u + 1013904223u; return double((smoke_rng_ >> 8) & 0xffff) / 65535.0; }
  static constexpr std::uint16_t kFireTpage = 0x00bc, kFireClut = 0x7ff0;

  void noteFlame(const std::array<double, 3>& c, const Affine& xf, bool flash) {
    const double T = fx_clock_;
    if (T < flame_last_flash_) flame_last_flash_ = -10;
    if (flash) flame_last_flash_ = T;
    const auto p0 = xf.apply(0, 0, -13), p1 = xf.apply(0, 100, -13);
    double d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const double l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-6) return;
    for (auto& x : d) x /= l;
    flame_axis_ = {d[0], d[1], d[2]};
    flame_tip_ = {c[0] + d[0] * 6, c[1] + d[1] * 6, c[2] + d[2] * 6};
    flame_pilot_t_ = T;
    if (static const bool fl = std::getenv("SF3N_LLAMALOG") != nullptr; fl) std::fprintf(log, "LLAMA t %.3f c %.0f %.0f %.0f eje %.2f %.2f %.2f disparos %zu estallidos %zu\n", T, c[0], c[1], c[2], d[0], d[1], d[2], flame_shots_.size(), flame_bursts_.size());

    if (T - flame_last_flash_ > 0.22) { flame_emit_t_ = -1; return; }
    const double rate = flameLevel() >= 2 ? 30.0 : 22.0;
    if (flame_emit_t_ < 0 || T - flame_emit_t_ > 0.2 || T < flame_emit_t_) flame_emit_t_ = T - 1.0 / rate;
    int n = int((T - flame_emit_t_) * rate);
    if (n <= 0) return;
    flame_emit_t_ += n / rate;
    n = std::min(n, 6);
    if (flame_shots_.size() > 120) return;
    for (int k = 0; k < n; ++k) {
      FlameShot s;
      s.p = flame_tip_;

      const double up = 450.0 + 150.0 * flameRand(), j = 100.0;
      for (int i = 0; i < 3; ++i) s.v[std::size_t(i)] = flame_axis_[std::size_t(i)] * up + flame_fwd_[std::size_t(i)] * 1500.0 + (flameRand() - 0.5) * j;
      s.t0 = T - flameRand() / rate; s.last = s.t0;
      s.life = 0.75 + 0.15 * flameRand();
      s.size = 46.0 + 14.0 * flameRand() + 10.0 * flameLevel();
      flame_shots_.push_back(s);
    }
  }

  std::optional<double> flameRayHit(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    std::optional<double> best;
    const double mnx = std::min(a[0], b[0]) - 2, mxx = std::max(a[0], b[0]) + 2, mny = std::min(a[1], b[1]) - 2, mxy = std::max(a[1], b[1]) + 2, mnz = std::min(a[2], b[2]) - 2, mxz = std::max(a[2], b[2]) + 2;
    const double dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    for (const auto r : cur().rooms) {
      if (r >= mis().rooms.size()) continue;
      for (const auto& sec : mis().rooms[r].sections()) {
        const auto& bb = sec.bounds;
        if (mxx < bb.minimum_x || mnx > bb.maximum_x || mxy < bb.minimum_y || mny > bb.maximum_y || mxz < bb.minimum_z || mnz > bb.maximum_z) continue;
        for (const auto& poly : sec.polygons) {
          if (!poly.renderable || poly.degenerate) continue;
          for (unsigned t = 0; t < (poly.quad ? 2u : 1u); ++t) {
            const auto& A = sec.vertices[poly.vertex_indices[t == 0 ? 0 : 1]];
            const auto& B = sec.vertices[poly.vertex_indices[t == 0 ? 1 : 3]];
            const auto& C = sec.vertices[poly.vertex_indices[2]];
            const double e1x = B.x - A.x, e1y = B.y - A.y, e1z = B.z - A.z, e2x = C.x - A.x, e2y = C.y - A.y, e2z = C.z - A.z;
            const double px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
            const double det = e1x * px + e1y * py + e1z * pz;
            if (std::fabs(det) < 1e-9) continue;
            const double inv = 1.0 / det;
            const double tx = a[0] - A.x, ty = a[1] - A.y, tz = a[2] - A.z;
            const double u = (tx * px + ty * py + tz * pz) * inv;
            if (u < 0 || u > 1) continue;
            const double qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
            const double v = (dx * qx + dy * qy + dz * qz) * inv;
            if (v < 0 || u + v > 1) continue;
            const double tt = (e2x * qx + e2y * qy + e2z * qz) * inv;
            if (tt < 0 || tt > 1) continue;
            if (!best || tt < *best) best = tt;
          }
        }
      }
    }
    return best;
  }
  void flameBurst(const std::array<double, 3>& c, double scale) {
    if (flame_bursts_.size() > 220) return;
    const double T = fx_clock_;
    const int n = 4 + int(3 * flameRand()) + flameLevel();
    for (int k = 0; k < n; ++k) {
      FlameBurst b;
      b.c = {c[0] + (flameRand() - 0.5) * 70 * scale, c[1] - flameRand() * 40 * scale, c[2] + (flameRand() - 0.5) * 70 * scale};
      b.v = {(flameRand() - 0.5) * 260 * scale, -(120 + 220 * flameRand()) * scale, (flameRand() - 0.5) * 260 * scale};
      b.t0 = T + k * 0.03; b.life = 0.45 + 0.35 * flameRand();
      b.s0 = (40 + 30 * flameRand()) * scale; b.s1 = (110 + 80 * flameRand()) * scale;
      b.seed = flameRand() * 6.2832; b.frame = flameRand() < 0.5 ? 0 : 1;
      flame_bursts_.push_back(b);
    }
  }

  void fireQuad(const ViewSetup& view, const std::array<double, 3>& c, double w, double h, double bright, int frame, double rot) {
    const double depth = view.forward[0] * (c[0] - view.eye[0]) + view.forward[1] * (c[1] - view.eye[1]) + view.forward[2] * (c[2] - view.eye[2]);
    if (depth < 20.0 || bright < 0.02) return;
    const double cr = std::cos(rot), sr = std::sin(rot);
    const double rgt[3] = {view.right[0] * cr + view.down[0] * sr, view.right[1] * cr + view.down[1] * sr, view.right[2] * cr + view.down[2] * sr};
    const double dwn[3] = {view.down[0] * cr - view.right[0] * sr, view.down[1] * cr - view.right[1] * sr, view.down[2] * cr - view.right[2] * sr};
    std::array<std::array<double, 3>, 4> q;
    for (int k = 0; k < 4; ++k) {
      const double sx = (k & 1) ? w / 2 : -w / 2, sy = (k & 2) ? h / 2 : -h / 2;
      for (int a = 0; a < 3; ++a) q[std::size_t(k)][std::size_t(a)] = c[std::size_t(a)] + rgt[a] * sx + dwn[a] * sy;
    }
    const double u0 = frame ? 32 : 0, u1 = u0 + 31, v0 = 0, v1 = 31;
    const double col = std::min(255.0, 128.0 * bright);
    auto mk = [&](int k, double u, double v) { return CV{q[std::size_t(k)][0], q[std::size_t(k)][1], q[std::size_t(k)][2], u, v, col, col, col, depth}; };
    emitClippedTriangle({mk(0, u0, v0), mk(1, u1, v0), mk(2, u0, v1)}, kFireTpage, kFireClut, true, false, true);
    emitClippedTriangle({mk(1, u1, v0), mk(3, u1, v1), mk(2, u0, v1)}, kFireTpage, kFireClut, true, false, true);
  }
  void drawFlames(const ViewSetup& view) {
    if (flameLevel() <= 0) return;
    const double T = fx_clock_;
    flame_fwd_ = {view.forward[0], view.forward[1] * 0.35, view.forward[2]};
    { const double l = std::hypot(flame_fwd_[0], flame_fwd_[1], flame_fwd_[2]); if (l > 1e-6) for (auto& x : flame_fwd_) x /= l; }

    if (T - flame_pilot_t_ < 0.15 && T >= flame_pilot_t_) {
      const int fr = int(T * 14.0) & 1;
      const double fl = 0.85 + 0.15 * std::sin(T * 31.0) + 0.1 * std::sin(T * 17.0);
      std::array<double, 3> c1{flame_tip_[0] + flame_axis_[0] * 18, flame_tip_[1] + flame_axis_[1] * 18, flame_tip_[2] + flame_axis_[2] * 18};
      std::array<double, 3> c2{flame_tip_[0] + flame_axis_[0] * 40, flame_tip_[1] + flame_axis_[1] * 40, flame_tip_[2] + flame_axis_[2] * 40};
      fireQuad(view, c1, 30 * fl, 46 * fl, 1.3, fr, 0.15 * std::sin(T * 9.0));
      fireQuad(view, c2, 20 * fl, 34 * fl, 0.9, fr ^ 1, 0.25 * std::sin(T * 7.0));
    }

    for (auto& s : flame_shots_) {
      if (s.dead) continue;
      const double age = T - s.t0;
      if (age < 0) continue;
      double dt = std::clamp(T - s.last, 0.0, 0.05);
      while (dt > 1e-6) {
        const double h = std::min(dt, 1.0 / 120.0); dt -= h;

        const double k = 1.0 - std::exp(-h * 14.0);
        for (int i = 0; i < 3; ++i) s.v[std::size_t(i)] += (flame_fwd_[std::size_t(i)] * 4200.0 - s.v[std::size_t(i)]) * k;
        s.v[1] += 300.0 * h;
        const std::array<double, 3> np{s.p[0] + s.v[0] * h, s.p[1] + s.v[1] * h, s.p[2] + s.v[2] * h};
        bool hit = false; std::array<double, 3> hp = np;
        if (age > 0.03) {
          if (const auto f = flameRayHit(s.p, np)) { hit = true; for (int i = 0; i < 3; ++i) hp[std::size_t(i)] = s.p[std::size_t(i)] + (np[std::size_t(i)] - s.p[std::size_t(i)]) * *f; }

          if (!hit && age > 0.12)
            for (const auto& a : cur().actors) {
              bool cerca = false;
              for (const auto& pt : a.parts) if (pt && std::hypot(pt->b[0] - np[0], pt->b[1] - np[1], pt->b[2] - np[2]) < 90.0) { cerca = true; break; }
              if (!cerca) continue;
              bool holder = false;
              for (const auto& pt : a.parts) if (pt && std::hypot(pt->b[0] - flame_tip_[0], pt->b[1] - flame_tip_[1], pt->b[2] - flame_tip_[2]) < 260.0) { holder = true; break; }
              if (holder) continue;
              hit = true;
              bool known = false;
              for (auto& b : flame_burns_) if (b.id == a.id) { b.until = T + 4.0; known = true; }
              if (!known && flame_burns_.size() < 32) flame_burns_.push_back({a.id, T + 4.0});
              break;
            }
        }
        if (hit) { if (static const bool fl = std::getenv("SF3N_LLAMALOG") != nullptr; fl) std::fprintf(log, "LLAMA choque edad %.3f en %.0f %.0f %.0f desde %.0f %.0f %.0f punta %.0f %.0f %.0f\n", age, hp[0], hp[1], hp[2], s.p[0], s.p[1], s.p[2], flame_tip_[0], flame_tip_[1], flame_tip_[2]); flameBurst(hp, 1.0); s.dead = true; s.p = hp; break; }
        s.p = np;
      }
      s.last = T;
      if (!s.dead && age > s.life) { flameBurst(s.p, 0.6); s.dead = true; }
    }
    std::erase_if(flame_shots_, [&](const FlameShot& s) { return s.dead || T < s.t0 - 1.0; });
    for (const auto& s : flame_shots_) {
      const double age = T - s.t0;
      if (age < 0) continue;
      const double u = age / s.life;
      const double sz = s.size * (1.0 + 1.4 * u);
      fireQuad(view, s.p, sz, sz, 1.6 * (1.0 - 0.5 * u), int(age * 20.0 + s.size) & 1, age * 3.0 + s.size);
    }

    std::erase_if(flame_bursts_, [&](const FlameBurst& b) { return T - b.t0 > b.life || T < b.t0 - 1.0; });
    for (const auto& b : flame_bursts_) {
      const double age = T - b.t0;
      if (age < 0) continue;
      const double u = age / b.life;
      const std::array<double, 3> c{b.c[0] + b.v[0] * age, b.c[1] + b.v[1] * age - 150.0 * age * age, b.c[2] + b.v[2] * age};
      const double sz = b.s0 + (b.s1 - b.s0) * std::sqrt(u);
      fireQuad(view, c, sz, sz, 1.7 * std::min(1.0, age / 0.05) * (1.0 - u), b.frame ^ (int(age * 12.0) & 1), b.seed + age * 1.5);
    }

    std::erase_if(flame_burns_, [&](const FlameBurn& b) { return T > b.until || T < b.until - 10.0; });
    for (const auto& br : flame_burns_)
      for (const auto& a : cur().actors) {
        if (a.id != br.id) continue;
        const double fade = std::min(1.0, (br.until - T) / 1.0);
        int k = 0;
        for (const auto& pt : a.parts) {
          if (!pt || (k++ % 2)) continue;
          const double ph = T * 9.0 + k * 1.7;
          const std::array<double, 3> c{pt->b[0], pt->b[1] - 30.0 - 10.0 * std::sin(ph), pt->b[2]};
          fireQuad(view, c, 70 + 15 * std::sin(ph * 1.3), 95 + 20 * std::sin(ph), 1.2 * fade, (int(T * 12.0) + k) & 1, 0.2 * std::sin(ph));
        }
      }
  }

  void drawSmoke(const ViewSetup& view) {
    if (smoke_.empty()) return;
    const double T = fx_clock_;
    std::erase_if(smoke_, [&](const SmokePuff& p) { return T - p.t0 > p.life; });
    for (const auto& p : smoke_) {
      const double age = T - p.t0;
      if (age < 0) continue;
      const double u = age / p.life;
      const double travel = p.speed * (1.0 - std::exp(-age * 3.0)) / 3.0;
      const std::array<double, 3> c{p.o[0] + p.dir[0] * travel + 4.0 * std::sin(age * 1.6 + p.seed) * age,
                                   p.o[1] + p.dir[1] * travel - 14.0 * age,
                                   p.o[2] + p.dir[2] * travel + 4.0 * std::cos(age * 1.3 + p.seed) * age};
      const double size = p.s0 + (p.s1 - p.s0) * (1.0 - std::exp(-age * 2.0));
      const double a = std::min(1.0, age / 0.06) * std::pow(1.0 - u, 1.8);
      const double c0 = 100.0 * p.bright * a;
      if (c0 < 1.5) continue;
      const double depth = view.forward[0] * (c[0] - view.eye[0]) + view.forward[1] * (c[1] - view.eye[1]) + view.forward[2] * (c[2] - view.eye[2]);
      if (depth < 20.0) continue;
      CV center{c[0], c[1], c[2], 0, 0, c0 * 0.95, c0 * 0.95, c0, depth};
      constexpr int kSeg = 10;
      std::array<CV, kSeg> rim{};
      for (int k = 0; k < kSeg; ++k) {
        const double ang = k * (6.2832 / kSeg), ca = std::cos(ang) * size, sa = std::sin(ang) * size;
        const double q[3] = {c[0] + view.right[0] * ca + view.down[0] * sa, c[1] + view.right[1] * ca + view.down[1] * sa, c[2] + view.right[2] * ca + view.down[2] * sa};
        rim[std::size_t(k)] = CV{q[0], q[1], q[2], 0, 0, 0, 0, 0, depth};
      }
      for (int k = 0; k < kSeg; ++k) emitClippedTriangle({center, rim[std::size_t(k)], rim[std::size_t((k + 1) % kSeg)]}, 0x20, 0, true, false, false);
    }
  }

  void drawEffects(const ViewSetup& view) {
    if (static const bool hf = std::getenv("SF3N_HIDEFX") != nullptr; hf) return;
    auto depthOf = [&](const std::array<double, 3>& w) {
      return view.forward[0] * (w[0] - view.eye[0]) + view.forward[1] * (w[1] - view.eye[1]) + view.forward[2] * (w[2] - view.eye[2]);
    };
    auto rgb = [](std::uint32_t c, int ch) { return double((c >> (8 * ch)) & 0xff); };
    static const int skipAbr = std::getenv("SF3N_FXSKIP_ABR") ? std::atoi(std::getenv("SF3N_FXSKIP_ABR")) : -1;

    const std::vector<EffectPrim>* glows = nullptr;
    {
      const auto& tgt = (rprev_ && alpha_ < 0.5) ? rprev_ : rcur_;
      static const bool viejo = std::getenv("SF3N_BRILLOS_NUEVOS") != nullptr;
      if (tgt && !viejo)
        for (auto it = glow_hist_.rbegin(); it != glow_hist_.rend(); ++it)
          if (it->cam.eye == tgt->camera.eye && it->cam.rot == tgt->camera.rot) { glows = &it->prims; break; }
      if (static const bool ll = std::getenv("SF3N_LUZLOG") != nullptr; ll) { static long ok = 0, no = 0; (glows ? ok : no)++; if ((ok + no) % 50 == 0) std::fprintf(log, "brillos sincronizados %ld, sin cuadro %ld\n", ok, no); }
      if (!glows && !glow_hist_.empty()) glows = &glow_hist_.back().prims;
    }
    if (static const bool lm = std::getenv("SF3N_LUZMED") != nullptr; lm) {
      int k4 = 0, k01 = 0; for (const auto& e : effects_) { if (e.kind == 4) ++k4; if (e.kind <= 1 && (e.cmd & 2) && ((e.tpage >> 5) & 3) == 1) ++k01; }
      std::fprintf(log, "CUADRO efectos %zu (brillos-luz suman %d, k4 %d) brillos del cuadro %zu sinc %d alpha %.2f\n", effects_.size(), k01, k4, glows ? glows->size() : 0, int(glows != nullptr), alpha_);
    }
    std::vector<EffectPrim> fxAll;
    fxAll.reserve(effects_.size() + (glows ? glows->size() : 0));
    fxAll.insert(fxAll.end(), effects_.begin(), effects_.end());
    if (glows) fxAll.insert(fxAll.end(), glows->begin(), glows->end());
    for (const auto& e : fxAll) {
      if (skipAbr >= 0 && (e.cmd & 2) && ((e.tpage >> 5) & 3) == skipAbr) continue;
      if (static const bool fpl = std::getenv("SF3N_FXPOSLOG") != nullptr; fpl) {
        const int r = int(e.col[0] & 0xff), g = int((e.col[0] >> 8) & 0xff), b = int((e.col[0] >> 16) & 0xff);
        if (r > 120 && g > 30 && g < 170 && b < 80) {
          const auto& w = e.w[0]; double c[3];
          for (int i = 0; i < 3; ++i) c[i] = view.exact_rot[std::size_t(i) * 3] * (w[0] - view.eye[0]) + view.exact_rot[std::size_t(i) * 3 + 1] * (w[1] - view.eye[1]) + view.exact_rot[std::size_t(i) * 3 + 2] * (w[2] - view.eye[2]);
          std::fprintf(log, "FXPOS cuadro %llu kind %d n %d cmd %02x col %06x w %.0f %.0f %.0f prof %.0f\n", (unsigned long long)render_frame_, int(e.kind), int(e.n), unsigned(e.cmd), unsigned(e.col[0]), w[0], w[1], w[2], depthOf(w));
        }
      }

      static const bool keepBlobs = std::getenv("SF3N_BLOBS") && std::atoi(std::getenv("SF3N_BLOBS")) != 0;
      static const bool logBlobs = std::getenv("SF3N_BLOBLOG") != nullptr;
      if (!keepBlobs && shadows_mode_ && (e.cmd & 2) && ((e.tpage >> 5) & 3) == 2 && e.kind == 1 && (e.cmd & 4) && e.clut == 0x78f0 && (e.tpage & 0x1ff) == 0x15f &&
          (e.col[0] & 0xff) == ((e.col[0] >> 8) & 0xff) && (e.col[0] & 0xff) == ((e.col[0] >> 16) & 0xff) && !frame_casters_.empty()) {
        double cx = 0, cy = 0, cz = 0; const int nv = e.kind == 1 ? 4 : 3;
        for (int k = 0; k < nv; ++k) { cx += e.w[std::size_t(k)][0]; cy += e.w[std::size_t(k)][1]; cz += e.w[std::size_t(k)][2]; }
        cx /= nv; cy /= nv; cz /= nv;
        bool blob = false;
        for (const auto& sc : frame_casters_)
          if (std::hypot(cx - sc.x, cz - sc.z) < 140.0 && std::fabs(cy - sc.plane) < 80.0) { blob = true; break; }
        double md = 1e18, mdy = 0;
        if (logBlobs) for (const auto& sc : frame_casters_) { const double dd = std::hypot(cx - sc.x, cz - sc.z); if (dd < md) { md = dd; mdy = cy - sc.plane; } }
        if (logBlobs) std::fprintf(stderr, "BLOBD %d %.0f %.0f\n", int(blob), md, mdy);
        if (logBlobs) std::fprintf(stderr, "BLOB %d cmd=%02x tp=%04x clut=%04x col=%06x kind=%d n=%d c=(%.0f,%.0f,%.0f) uv=%04x %04x %04x %04x\n", int(blob), unsigned(e.cmd), unsigned(e.tpage), unsigned(e.clut), unsigned(e.col[0] & 0xffffff), int(e.kind), int(e.n), cx, cy, cz, unsigned(e.uv[0]), unsigned(e.uv[1]), unsigned(e.uv[2]), unsigned(e.uv[3]));
        if (blob) continue;
      }
      const bool semi = e.cmd & 2;
      const bool raw = e.cmd & 1;
      if (static const bool vl = std::getenv("SF3N_VAHOLOG") != nullptr; vl && e.kind <= 1) {
        const int nv = e.kind == 1 ? 4 : 3; double c[3] = {0, 0, 0}, ext = 0;
        for (int k = 0; k < nv; ++k) for (int a = 0; a < 3; ++a) c[a] += e.w[std::size_t(k)][std::size_t(a)] / nv;
        for (int k = 0; k < nv; ++k) ext = std::max(ext, std::hypot(e.w[std::size_t(k)][0] - c[0], e.w[std::size_t(k)][1] - c[1], e.w[std::size_t(k)][2] - c[2]));
        double bd = 1e9; std::array<double, 7> bh{};
        for (const auto& h : frame_heads_) { const double d = std::hypot(c[0] - h[0], c[1] - h[1], c[2] - h[2]); if (d < bd) { bd = d; bh = h; } }
        std::fprintf(log, "VAHO cuadro %llu cmd %02x tp %03x clut %04x col %06x c %.0f,%.0f,%.0f ext %.0f cabeza %.0f (r %.0f) rel %.0f,%.0f,%.0f\n", (unsigned long long)render_frame_, e.cmd, e.tpage, e.clut, e.col[0], c[0], c[1], c[2], ext, bd, bh[3], c[0] - bh[0], c[1] - bh[1], c[2] - bh[2]);
      }

      bool breath = false;

      if (roof_mission_ != &mis() || roof_name_ != mis().name) buildRoofs();
      if ((cold_mission_ || snow_mission_) && e.kind <= 1 && semi && (e.cmd & 4) && ((e.tpage >> 5) & 3) == 1 && !frame_heads_.empty()) {
        const int nv = e.kind == 1 ? 4 : 3; double c[3] = {0, 0, 0}, ext = 0;
        for (int k = 0; k < nv; ++k) for (int a = 0; a < 3; ++a) c[a] += e.w[std::size_t(k)][std::size_t(a)] / nv;
        for (int k = 0; k < nv; ++k) ext = std::max(ext, std::hypot(e.w[std::size_t(k)][0] - c[0], e.w[std::size_t(k)][1] - c[1], e.w[std::size_t(k)][2] - c[2]));
        for (const auto& h : frame_heads_)
          if (ext < 40.0 && std::hypot(c[0] - h[0], c[1] - h[1], c[2] - h[2]) < h[3] * 2.5 + 45.0) {
            breath = true;
            if (static const bool vl = std::getenv("SF3N_VAHOLOG") != nullptr; vl) std::fprintf(log, "VAHO efecto cmd %02x tp %04x col %06x ext %.0f dist %.0f cabeza %.0f,%.0f,%.0f mision %s\n", unsigned(e.cmd), unsigned(e.tpage), unsigned(e.col[0] & 0xffffff), ext, std::hypot(c[0] - h[0], c[1] - h[1], c[2] - h[2]), h[0], h[1], h[2], mis().name.c_str());
            static const int vaho = envInt("SF3N_VAHO", 1);
            if (vaho >= 1) { noteBreath({c[0], c[1], c[2]}, h); }
            break;
          }
        if (breath && envInt("SF3N_VAHO", 1) >= 1) continue;
      }
      if (e.kind <= 1) {
        const bool tex = e.cmd & 4;
        no_glow_pull_ = breath;
        auto mk = [&](int k) {

          auto w = e.w[std::size_t(k)];
          {
            const double dx = view.eye[0] - w[0], dy = view.eye[1] - w[1], dz = view.eye[2] - w[2];
            const double len = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (len > 1e-6 && !breath) { const double b = std::min(4.0, len * 0.012) / len; w[0] += dx * b; w[1] += dy * b; w[2] += dz * b; }
          }
          const auto c = e.col[std::size_t(k)];
          const double r = tex && raw ? 128 : rgb(c, 0), g = tex && raw ? 128 : rgb(c, 1), b = tex && raw ? 128 : rgb(c, 2);
          return CV{w[0], w[1], w[2], double(e.uv[std::size_t(k)] & 0xff), double(e.uv[std::size_t(k)] >> 8), r, g, b, depthOf(w)};
        };
        emitClippedTriangle({mk(0), mk(1), mk(2)}, e.tpage, e.clut, semi, false, tex);
        if (e.kind == 1) emitClippedTriangle({mk(1), mk(3), mk(2)}, e.tpage, e.clut, semi, false, tex);
        no_glow_pull_ = false;
      } else if (e.kind == 2) {

        const double da = depthOf(e.w[0]), db = depthOf(e.w[1]);
        if (da < near_z || db < near_z) continue;

        if (static const bool l2d = std::getenv("SF3N_LINEAS_2D") != nullptr; !l2d) {
          const auto& A = e.w[0]; const auto& B = e.w[1];
          const double d[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};

          double p3[3] = {d[1] * view.forward[2] - d[2] * view.forward[1], d[2] * view.forward[0] - d[0] * view.forward[2], d[0] * view.forward[1] - d[1] * view.forward[0]};
          double pl = std::sqrt(p3[0] * p3[0] + p3[1] * p3[1] + p3[2] * p3[2]);
          if (pl < 1e-9) { p3[0] = view.right[0]; p3[1] = view.right[1]; p3[2] = view.right[2]; pl = 1.0; }
          for (double& v : p3) v /= pl;
          const double f = std::max(16.0, double(view.h));
          const double ha = 0.5 * da / f, hb = 0.5 * db / f;
          auto mkL = [&](const std::array<double, 3>& P, double h, double sgn, std::uint32_t c) {
            std::array<double, 3> q{P[0] + p3[0] * h * sgn, P[1] + p3[1] * h * sgn, P[2] + p3[2] * h * sgn};
            return CV{q[0], q[1], q[2], 0.0, 0.0, rgb(c, 0), rgb(c, 1), rgb(c, 2), depthOf(q)};
          };
          const CV a0 = mkL(A, ha, -1, e.col[0]), a1 = mkL(A, ha, 1, e.col[0]), b0 = mkL(B, hb, -1, e.col[1]), b1 = mkL(B, hb, 1, e.col[1]);
          emitClippedTriangle({a0, b0, a1}, e.tpage, 0, semi, false, false);
          emitClippedTriangle({a1, b0, b1}, e.tpage, 0, semi, false, false);
          continue;
        }
        if (fx_lines_.size() >= fx_lines_.capacity()) continue;
        SVECTOR sv[2];
        long xy[2]{}, dq{}, fl{};
        for (int i = 0; i < 2; ++i) {
          sv[i] = SVECTOR{short(std::lround(e.w[std::size_t(i)][0])), short(std::lround(e.w[std::size_t(i)][1])), short(std::lround(e.w[std::size_t(i)][2])), 0};
          PGXP_VectorRegister(&sv[i], e.w[std::size_t(i)].data());
        }
        int ixy[2]{}; long idq{}, ifl{};
        const int depth = RotTransPers(&sv[0], &ixy[0], &idq, &ifl);
        RotTransPers(&sv[1], &ixy[1], &idq, &ifl);
        xy[0] = ixy[0]; xy[1] = ixy[1]; (void)dq; (void)fl;
        auto& l = fx_lines_.emplace_back();
        setLineG2(&l);
        setRGB0(&l, u_char(rgb(e.col[0], 0)), u_char(rgb(e.col[0], 1)), u_char(rgb(e.col[0], 2)));
        setRGB1(&l, u_char(rgb(e.col[1], 0)), u_char(rgb(e.col[1], 1)), u_char(rgb(e.col[1], 2)));
        if (semi) setSemiTrans(&l, 1);
        setProjected(&l.x0, xy[0]);
        setProjected(&l.x1, xy[1]);
        auto* slot = &(semi ? ot_semi_ : room_pass_ ? ot_room_ : ot_)[std::clamp(depth, 1, ot_size - 1)];
        addPrim(slot, &l);
        if (semi && fx_tpages_.size() < fx_tpages_.capacity()) {
          auto& t = fx_tpages_.emplace_back();
          SetDrawTPage(&t, 1, 0, e.tpage);
          addPrim(slot, &t);
        }
      } else if (e.kind == 4) {

        auto c = e.w[0];
        double gsc = 1.0;
        if (e.ghost != 1.0 || e.ghost_y != 1.0) {

          const double d[3] = {c[0] - view.eye[0], c[1] - view.eye[1], c[2] - view.eye[2]};
          const double x = view.right[0] * d[0] + view.right[1] * d[1] + view.right[2] * d[2];
          const double y = view.down[0] * d[0] + view.down[1] * d[1] + view.down[2] * d[2];
          const double z = view.forward[0] * d[0] + view.forward[1] * d[1] + view.forward[2] * d[2];
          if (z <= 1.0) continue;
          for (int a = 0; a < 3; ++a) c[std::size_t(a)] = view.eye[std::size_t(a)] + view.right[std::size_t(a)] * x * e.ghost + view.down[std::size_t(a)] * y * e.ghost_y + view.forward[std::size_t(a)] * z;
        }
        if (static const bool lm = std::getenv("SF3N_LUZMED") != nullptr; lm && e.size_w > 0) {
          const double d[3] = {c[0] - view.eye[0], c[1] - view.eye[1], c[2] - view.eye[2]};
          const double x = view.right[0] * d[0] + view.right[1] * d[1] + view.right[2] * d[2], y = view.down[0] * d[0] + view.down[1] * d[1] + view.down[2] * d[2], z = view.forward[0] * d[0] + view.forward[1] * d[1] + view.forward[2] * d[2];
          const double dw[3] = {e.w[0][0] - view.eye[0], e.w[0][1] - view.eye[1], e.w[0][2] - view.eye[2]};
          const double zw = view.forward[0] * dw[0] + view.forward[1] * dw[1] + view.forward[2] * dw[2];
          const double fxg = zw / e.size_w, fyg = zw / e.size_h;
          std::fprintf(log, "DIB brillo nuestro %.1f,%.1f PS1 %.0f,%.0f error %.1f escala %.3f,%.3f z %.0f alpha %.2f\n", fxg * x / z, fyg * y / z, e.ps1x, e.ps1y, std::hypot(fxg * x / z - e.ps1x, fyg * y / z - e.ps1y), e.ghost, e.ghost_y, z, alpha_);
        }
        {
          const double dx = c[0] - view.eye[0], dy = c[1] - view.eye[1], dz = c[2] - view.eye[2];
          const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
          static const double nl = std::max(near_z * 1.5, double(envInt("SF3N_BRILLO_DIST", 48)));
          if (len > nl) { gsc = nl / len; c = {view.eye[0] + dx * gsc, view.eye[1] + dy * gsc, view.eye[2] + dz * gsc}; }
        }
        const int nv = e.n == 4 ? 4 : 3;
        std::array<std::array<double, 3>, 4> q{};
        for (int k = 0; k < nv; ++k)
          for (int a = 0; a < 3; ++a) q[std::size_t(k)][std::size_t(a)] = c[std::size_t(a)] + (view.right[std::size_t(a)] * e.off[std::size_t(k)][0] * e.size_w + view.down[std::size_t(a)] * e.off[std::size_t(k)][1] * e.size_h) * gsc;
        auto mk = [&](int k) {
          const auto col = e.col[std::size_t(k)];
          const double r = raw ? 128 : rgb(col, 0), g = raw ? 128 : rgb(col, 1), b = raw ? 128 : rgb(col, 2);
          return CV{q[std::size_t(k)][0], q[std::size_t(k)][1], q[std::size_t(k)][2], double(e.uv[std::size_t(k)] & 0xff), double(e.uv[std::size_t(k)] >> 8), r, g, b, depthOf(q[std::size_t(k)])};
        };
        no_glow_pull_ = true;
        emitClippedTriangle({mk(0), mk(1), mk(2)}, e.tpage, e.clut, semi, false, true);
        if (nv == 4) emitClippedTriangle({mk(1), mk(3), mk(2)}, e.tpage, e.clut, semi, false, true);
        no_glow_pull_ = false;
      } else if (e.kind == 5) {

        const int nv = e.n == 4 ? 4 : 3;
        std::array<std::array<double, 3>, 4> q{};
        for (int k = 0; k < nv; ++k)
          for (int a = 0; a < 3; ++a) q[std::size_t(k)][std::size_t(a)] = e.w[0][std::size_t(a)] + view.right[std::size_t(a)] * e.off[std::size_t(k)][0] * e.size_w + view.down[std::size_t(a)] * e.off[std::size_t(k)][1] * e.size_h;
        auto mk = [&](int k) { const auto col = e.col[std::size_t(k)]; return CV{q[std::size_t(k)][0], q[std::size_t(k)][1], q[std::size_t(k)][2], 0.0, 0.0, rgb(col, 0), rgb(col, 1), rgb(col, 2), depthOf(q[std::size_t(k)])}; };
        emitClippedTriangle({mk(0), mk(1), mk(2)}, e.tpage, 0, semi, false, false);
        if (nv == 4) emitClippedTriangle({mk(1), mk(3), mk(2)}, e.tpage, 0, semi, false, false);
      } else if (e.kind == 3) {

        const auto& c = e.w[0];
        const double hw = e.size_w / 2, hh = e.size_h / 2;
        std::array<std::array<double, 3>, 4> q;
        for (int k = 0; k < 4; ++k) {
          const double sx = (k & 1) ? hw : -hw, sy = (k & 2) ? hh : -hh;
          for (int a = 0; a < 3; ++a) q[std::size_t(k)][std::size_t(a)] = c[std::size_t(a)] + view.right[std::size_t(a)] * sx + view.down[std::size_t(a)] * sy;
        }
        const bool tex = e.cmd & 4;
        const int u0 = e.uv[0] & 0xff, v0 = e.uv[0] >> 8;
        auto mk = [&](int k, double u, double v) {
          const double r = tex && raw ? 128 : rgb(e.col[0], 0), g = tex && raw ? 128 : rgb(e.col[0], 1), b = tex && raw ? 128 : rgb(e.col[0], 2);
          return CV{q[std::size_t(k)][0], q[std::size_t(k)][1], q[std::size_t(k)][2], u, v, r, g, b, depthOf(q[std::size_t(k)])};
        };
        const double u1 = std::min(255.0, double(u0) + e.size_px_w), v1 = std::min(255.0, double(v0) + e.size_px_h);
        emitClippedTriangle({mk(0, u0, v0), mk(1, u1, v0), mk(2, u0, v1)}, e.tpage, e.clut, semi, false, tex);
        emitClippedTriangle({mk(1, u1, v0), mk(3, u1, v1), mk(2, u0, v1)}, e.tpage, e.clut, semi, false, tex);
      }
    }
  }

  bool world_alias_ = false;
  std::uint16_t aliasTpage(std::uint16_t tpage, unsigned bank) const {
    if (!world_alias_ || bank != 0) return tpage;
    const unsigned p = (tpage & 0xfu) + ((tpage >> 4) & 1u) * 16u;
    return std::uint16_t((p + 1) * 1024 + (tpage & 0x1e0u));
  }

  static bool noFogTest() { static const bool v = std::getenv("SF3N_PRUEBA_SIN_NIEBLA") != nullptr; return v; }
  double fogFactor(double depth) const {
    if (noFogTest() || !(cur().fog_far > cur().fog_near && cur().fog_near > 0)) return 1.0;
    return std::clamp((cur().fog_far + fog_shift_ - depth) / (cur().fog_far - cur().fog_near), 0.0, 1.0);
  }

  struct SkinLink { std::uint16_t other = 0; std::array<double, 3> local{}; double w = 0; };
  struct SkinData { bool built = false; std::vector<std::vector<SkinLink>> links; double size = 0; };
  std::map<std::size_t, SkinData> skin_cache_;
  const void* skin_mission_ = nullptr;
  void skinJoints(const sf::assets::HmdModel& hmd, std::size_t modelIdx, const std::vector<std::optional<Affine>>& xf) {
    if (skin_mission_ != static_cast<const void*>(&mis())) { skin_mission_ = &mis(); skin_cache_.clear(); }
    auto& sd = skin_cache_[modelIdx];
    const auto& verts = hmd.vertices();
    const auto& vparts = hmd.vertexParts();
    const auto& parts = hmd.parts();
    if (!sd.built) {
      sd.built = true;
      sd.links.assign(verts.size(), {});

      std::vector<Affine> bind(parts.size());
      std::vector<char> done(parts.size(), 0);
      std::function<void(std::size_t)> solve = [&](std::size_t i) {
        if (done[i]) return;
        done[i] = 2;
        const auto& lt = parts[i].local_transform;
        Affine inv;
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) inv.a[std::size_t(r * 3 + c)] = double(lt.rotation[std::size_t(c * 3 + r)]) / 4096.0;
        for (int r = 0; r < 3; ++r) inv.b[std::size_t(r)] = -(inv.a[std::size_t(r * 3)] * lt.translation[0] + inv.a[std::size_t(r * 3 + 1)] * lt.translation[1] + inv.a[std::size_t(r * 3 + 2)] * lt.translation[2]);
        const int par = parts[i].parent;
        if (par >= 0 && std::size_t(par) < parts.size() && done[std::size_t(par)] != 2) {
          solve(std::size_t(par));
          const Affine& P = bind[std::size_t(par)];
          Affine r;
          for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) { double v = 0; for (int k = 0; k < 3; ++k) v += P.a[std::size_t(a * 3 + k)] * inv.a[std::size_t(k * 3 + b)]; r.a[std::size_t(a * 3 + b)] = v; }
          r.b = P.apply(inv.b[0], inv.b[1], inv.b[2]);
          bind[i] = r;
        } else bind[i] = inv;
        done[i] = 1;
      };
      for (std::size_t i = 0; i < parts.size(); ++i) solve(i);
      auto invert = [](const Affine& m) {
        Affine r;
        const auto& A = m.a;
        const double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
        if (std::fabs(det) < 1e-9) return r;
        r.a = {(A[4] * A[8] - A[5] * A[7]) / det, (A[2] * A[7] - A[1] * A[8]) / det, (A[1] * A[5] - A[2] * A[4]) / det,
               (A[5] * A[6] - A[3] * A[8]) / det, (A[0] * A[8] - A[2] * A[6]) / det, (A[2] * A[3] - A[0] * A[5]) / det,
               (A[3] * A[7] - A[4] * A[6]) / det, (A[1] * A[6] - A[0] * A[7]) / det, (A[0] * A[4] - A[1] * A[3]) / det};
        for (int i = 0; i < 3; ++i) r.b[std::size_t(i)] = -(r.a[std::size_t(i * 3)] * m.b[0] + r.a[std::size_t(i * 3 + 1)] * m.b[1] + r.a[std::size_t(i * 3 + 2)] * m.b[2]);
        return r;
      };
      std::vector<Affine> bindInv(parts.size());
      for (std::size_t i = 0; i < parts.size(); ++i) bindInv[i] = invert(bind[i]);

      double mn[3] = {1e18, 1e18, 1e18}, mx[3] = {-1e18, -1e18, -1e18};
      for (std::size_t vi = 0; vi < verts.size(); ++vi) {
        if (vparts[vi] >= parts.size()) continue;
        const auto p = bind[vparts[vi]].apply(verts[vi].x, verts[vi].y, verts[vi].z);
        for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], p[std::size_t(k)]); mx[k] = std::max(mx[k], p[std::size_t(k)]); }
      }
      sd.size = std::max({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]});

      std::vector<std::map<std::uint16_t, int>> nb(verts.size());
      for (const auto& tri : hmd.triangles())
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) {
          const auto va = tri.vertex_indices[std::size_t(a)], vb = tri.vertex_indices[std::size_t(b)];
          if (va >= verts.size() || vb >= verts.size() || vparts[va] == vparts[vb]) continue;
          nb[va][std::uint16_t(vparts[vb])]++;
        }
      static const double wv = std::getenv("SF3N_PIEL") ? std::atof(std::getenv("SF3N_PIEL")) : 0.3;
      for (std::size_t vi = 0; vi < verts.size(); ++vi) {
        if (nb[vi].empty() || vparts[vi] >= parts.size()) continue;
        int best = 0; std::uint16_t op = 0;
        for (auto [pp, n] : nb[vi]) if (n > best) { best = n; op = pp; }
        const auto p = bind[vparts[vi]].apply(verts[vi].x, verts[vi].y, verts[vi].z);
        SkinLink l; l.other = op; l.local = bindInv[op].apply(p[0], p[1], p[2]); l.w = wv;
        sd.links[vi].push_back(l);
      }
    }
    if (sd.size <= 0) return;
    const double limit = 0.12 * sd.size;
    std::size_t used = 0, refused = 0;
    for (std::size_t vi = 0; vi < verts.size() && vi < sd.links.size(); ++vi) {
      if (sd.links[vi].empty() || !part_ok_[vi]) continue;
      const auto& l = sd.links[vi][0];
      if (l.other >= xf.size() || !xf[l.other]) continue;
      const auto q = xf[l.other]->apply(l.local[0], l.local[1], l.local[2]);
      auto& w = world_verts_[vi];
      const double d = std::sqrt((q[0] - w[0]) * (q[0] - w[0]) + (q[1] - w[1]) * (q[1] - w[1]) + (q[2] - w[2]) * (q[2] - w[2]));
      if (d > limit) { ++refused; continue; }
      for (int k = 0; k < 3; ++k) w[std::size_t(k)] += (q[std::size_t(k)] - w[std::size_t(k)]) * l.w;
      ++used;
    }
    skin_used_ += used; skin_refused_ += refused;
  }
  std::uint64_t skin_used_ = 0, skin_refused_ = 0;
  std::uint64_t tex_ambig_ = 0;
  std::map<std::size_t, std::map<std::uint32_t, std::pair<int, int>>> uv_off_memo_;
  void drawModels(double alpha, const ViewSetup& view) {
    if (static const void* lastMis = nullptr; lastMis != static_cast<const void*>(&mis())) { lastMis = &mis(); uv_off_memo_.clear(); }
    if (static const bool nd = std::getenv("SF3N_NODEPTHLOG") != nullptr; nd && render_frame_ % 10 == 0) { std::fprintf(log, "SINPROF %llu conprof %llu planos %llu\n", ::g_noDepthTris, ::g_depthTris, ::g_flatTris); }
    if (static const bool sk = std::getenv("SF3N_SKINLOG") != nullptr; sk && render_frame_ % 30 == 0) std::fprintf(log, "PIEL usados %llu rechazados %llu\n", (unsigned long long)skin_used_, (unsigned long long)skin_refused_);
    if (static const bool tl = std::getenv("SF3N_TEXAMBIG") != nullptr; tl && render_frame_ % 30 == 0) std::fprintf(log, "TEXAMBIG %llu\n", (unsigned long long)tex_ambig_);
    frame_casters_.clear();
    frame_heads_.clear(); frame_head_up_.clear();
    if (!xray_actors_ && ++render_frame_ % 600 == 0)
      for (auto it = tex_memo_.begin(); it != tex_memo_.end();) it = render_frame_ - it->second.last_frame > 3000 ? tex_memo_.erase(it) : std::next(it);
    auto depthOf = [&](const std::array<double, 3>& w) {
      return view.forward[0] * (w[0] - view.eye[0]) + view.forward[1] * (w[1] - view.eye[1]) + view.forward[2] * (w[2] - view.eye[2]);
    };

    if (xray_world_ || xray_actors_) {
      xray_hands_.clear();
      for (const auto& actor : cur().actors)
        if (const auto* h = std::get_if<sf::assets::HmdModel>(&mis().models[actor.model].geometry))
          for (std::size_t pi = 0; pi < h->parts().size() && pi < actor.parts.size(); ++pi)
            if (actor.parts[pi] && h->parts()[pi].name.find("Han") != std::string::npos) xray_hands_.push_back({actor.parts[pi]->b[0], actor.parts[pi]->b[1], actor.parts[pi]->b[2]});
    }

    for (const auto& actor : cur().actors) {
      if (xray_world_ || nv_age_ < 0) break;
      if (static const bool ha = std::getenv("SF3N_HIDEACT") != nullptr; ha) continue;
      if (static const char* so = std::getenv("SF3N_SOLO"); so && mis().models[actor.model].name.find(so) == std::string::npos) continue;
      if (static const bool ln = std::getenv("SF3N_ACTNAMES") != nullptr; ln && render_frame_ % 20 == 0) std::fprintf(log, "ACT %s id %llu\n", mis().models[actor.model].name.c_str(), (unsigned long long)actor.id);
      const ActorPose* before = actor.prev >= 0 && std::size_t(actor.prev) < prv().actors.size() ? &prv().actors[std::size_t(actor.prev)] : nullptr;
      if (static const bool jl = std::getenv("SF3N_EMPAREJALOG") != nullptr; jl && before) {
        const Affine* r1 = rootOf(actor); const Affine* r0 = rootOf(*before);
        if (r1 && r0) {
          const double d = std::sqrt(dist2(*r0, *r1));
          double maxPart = 0;
          for (std::size_t pi = 0; pi < actor.parts.size() && pi < before->parts.size(); ++pi)
            if (actor.parts[pi] && before->parts[pi]) maxPart = std::max(maxPart, std::sqrt(dist2(*actor.parts[pi], *before->parts[pi])));
          if (d > 60 || maxPart > 120) std::fprintf(log, "EMPAREJA campo %llu %s id %llu raiz %.0f maxpieza %.0f actores %zu/%zu\n", (unsigned long long)cur().field, mis().models[actor.model].name.c_str(), (unsigned long long)actor.id, d, maxPart, prv().actors.size(), cur().actors.size());
        }
      }
      const auto* hmd = std::get_if<sf::assets::HmdModel>(&mis().models[actor.model].geometry);
      if (!hmd) continue;
      const auto& parts = hmd->parts();
      std::vector<std::optional<Affine>> xf(parts.size());
      for (std::size_t pi = 0; pi < parts.size() && pi < actor.parts.size(); ++pi) {
        if (!actor.parts[pi]) continue;
        const Affine* prev = before && pi < before->parts.size() && before->parts[pi] ? &*before->parts[pi] : nullptr;
        xf[pi] = prev ? lerp(*prev, *actor.parts[pi], alpha) : *actor.parts[pi];
        if (static const bool rl = std::getenv("SF3N_ROTLOG") != nullptr; rl && prev) {
          auto det = [](const Affine& m) { const auto& A = m.a; return A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]); };
          const double d0 = det(*prev), d1 = det(*actor.parts[pi]), dm = det(lerp(*prev, *actor.parts[pi], 0.5));
          const double ref = std::cbrt(std::fabs(d0 * d1)) ;
          if (ref > 0 && dm / ref < 0.6) std::fprintf(log, "ROT campo %llu %s pieza %zu det medio %.2f\n", (unsigned long long)cur().field, mis().models[actor.model].name.c_str(), pi, dm / ref);
        }
      }
      const auto& verts = hmd->vertices();
      const auto& vparts = hmd->vertexParts();
      world_verts_.resize(verts.size());
      vert_colors_.resize(verts.size());
      part_ok_.assign(verts.size(), 0);
      const auto& normals = hmd->normals();
      for (std::size_t vi = 0; vi < verts.size(); ++vi) {
        const auto pi = vparts[vi];
        if (pi >= xf.size() || !xf[pi]) continue;
        world_verts_[vi] = xf[pi]->apply(verts[vi].x, verts[vi].y, verts[vi].z);
        part_ok_[vi] = 1;
        const Light& L = pi < actor.lights.size() ? actor.lights[pi] : Light{};
        vert_colors_[vi] = vi < normals.size() ? shade(L, normals[vi].x, normals[vi].y, normals[vi].z, 255.0) : std::array<double, 3>{128, 128, 128};
      }

      feet_pull_ = 0;
      if (static const int maxSink = std::clamp(xrayCfg("pies_hundidos", "SF3N_PIES_SUELO", 12), 0, 200); maxSink > 0) {
        double low = -1e18; std::size_t lv = 0;
        for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi] && world_verts_[vi][1] > low) { low = world_verts_[vi][1]; lv = vi; }
        if (low > -1e17) {

          double lift = 0;
          for (std::size_t vi = 0; vi < verts.size(); ++vi) {
            if (!part_ok_[vi] || world_verts_[vi][1] < low - 8.0) continue;
            const auto fl = floorAt(world_verts_[vi][0], world_verts_[vi][2], world_verts_[vi][1]);
            if (!fl) continue;
            const double sink = world_verts_[vi][1] - *fl;
            if (sink > lift && sink <= double(maxSink)) lift = sink;
          }
          if (static const bool pl = std::getenv("SF3N_PIESLOG") != nullptr; pl && (render_frame_ % 25) == 0) std::fprintf(log, "PIES %s bajo %.0f v%zu sube %.1f\n", mis().models[actor.model].name.c_str(), low, lv, lift);

          feet_pull_ = lift; feet_low_ = low;
        }
      }
      {
        double hx = 0, hy = 0, hz = 0, mnY = 1e18, mxY = -1e18; int hn = 0;
        for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi] && vparts[vi] == 0) { const auto& w = world_verts_[vi]; hx += w[0]; hy += w[1]; hz += w[2]; mnY = std::min(mnY, w[1]); mxY = std::max(mxY, w[1]); ++hn; }
        if (hn >= 4 && !xf.empty() && xf[0]) {

          const auto& A = xf[0]->a; double fx = -A[2], fy = -A[5], fz = -A[8];
          const double fl = std::sqrt(fx * fx + fy * fy + fz * fz); if (fl > 1e-9) { fx /= fl; fy /= fl; fz /= fl; }
          frame_heads_.push_back({hx / hn, hy / hn, hz / hn, std::max(12.0, (mxY - mnY) * 0.6), fx, fy, fz});
          double feetY = -1e18; for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi]) feetY = std::max(feetY, world_verts_[vi][1]);
          frame_head_up_.push_back(feetY - hy / hn);
        }
      }
      if (static const bool skin = std::getenv("SF3N_PIEL_SUAVE") != nullptr; skin) skinJoints(*hmd, actor.model, xf);
      if (static const bool ms = std::getenv("SF3N_MODELSTAT") != nullptr; ms) {
        static std::set<std::size_t> done;
        if (!done.count(actor.model) && std::all_of(part_ok_.begin(), part_ok_.end(), [](unsigned char c) { return c != 0; })) {
          done.insert(actor.model);
          std::size_t cross = 0;
          for (const auto& tri : hmd->triangles()) {
            const auto a = vparts[tri.vertex_indices[0]], b = vparts[tri.vertex_indices[1]], c = vparts[tri.vertex_indices[2]];
            if (a != b || b != c) ++cross;
          }
          std::size_t close = 0, closeParts = 0;
          std::map<std::pair<int, int>, int> pp;
          for (std::size_t i = 0; i < verts.size(); ++i)
            for (std::size_t j = i + 1; j < verts.size(); ++j) {
              if (vparts[i] == vparts[j]) continue;
              const auto& A = world_verts_[i]; const auto& B = world_verts_[j];
              const double d = std::sqrt((A[0] - B[0]) * (A[0] - B[0]) + (A[1] - B[1]) * (A[1] - B[1]) + (A[2] - B[2]) * (A[2] - B[2]));
              if (d < 4.0) { ++close; pp[{int(vparts[i]), int(vparts[j])}]++; }
            }
          std::fprintf(log, "MODELSTAT %s verts %zu tris %zu piezas %zu tris_entre_piezas %zu pares_cercanos(<4) %zu:", mis().models[actor.model].name.c_str(), verts.size(), hmd->triangles().size(), parts.size(), cross, close);
          for (auto [k, n] : pp) std::fprintf(log, " %d-%d:%d", k.first, k.second, n);
          std::fprintf(log, "\n");
          for (std::size_t pi = 0; pi < parts.size(); ++pi) std::fprintf(log, "  pieza %zu verts %u\n", pi, parts[pi].vertex_count);
        }
      }
      if (static const bool info = std::getenv("SF3N_MODELINFO") != nullptr; info && render_frame_ % 15 == 0) {
        double sr = 0, sn = 0; int k = 0;
        for (std::size_t vi = 0; vi < verts.size(); ++vi) if (part_ok_[vi]) { sr += vert_colors_[vi][0]; ++k; if (vi < normals.size()) sn += std::sqrt(double(normals[vi].x) * normals[vi].x + double(normals[vi].y) * normals[vi].y + double(normals[vi].z) * normals[vi].z); }
        std::fprintf(log, "actor %s media R %.0f |n| %.0f\n", mis().models[actor.model].name.c_str(), k ? sr / k : 0.0, k ? sn / k : 0.0);
      }
      if (shadows_mode_ && !xray_actors_ && !xf.empty() && xf[0]) {
        const std::size_t before = frame_casters_.size();
        actorShadow(actor, *hmd, *xf[0], view);

        if (frame_casters_.size() > before) {
          const auto& parts = hmd->parts();
          for (std::size_t pi = 0; pi < xf.size() && pi < parts.size(); ++pi)
            if (xf[pi] && parts[pi].name.find("Han") != std::string::npos) frame_casters_.back().joints.push_back({xf[pi]->b[0], xf[pi]->b[1], xf[pi]->b[2]});
        }
      }
      const bool haveSxy = actor.vertex_sxy.size() == verts.size();

      auto findReal = [&](const sf::assets::HmdTriangle& tri) -> const TriTex* {
        if (!haveSxy || tritex_.empty()) return nullptr;
        const auto s0 = actor.vertex_sxy[tri.vertex_indices[0]], s1 = actor.vertex_sxy[tri.vertex_indices[1]], s2 = actor.vertex_sxy[tri.vertex_indices[2]];
        if (!s0 || !s1 || !s2) return nullptr;

        static const bool noFix = std::getenv("SF3N_SIN_ARREGLO_TEX") != nullptr;
        if (!noFix && (s0 == s1 || s1 == s2 || s0 == s2)) { ++tex_ambig_; return nullptr; }
        const auto key = triKey(s0, s1, s2);
        for (std::size_t m = tritex_.size(); m-- > 0;)
          if (auto it = tritex_[m]->find(key); it != tritex_[m]->end()) {
            const auto& u = it->second.uv;
            if (!noFix && (u[0].first == u[1].first || u[1].first == u[2].first || u[0].first == u[2].first)) { ++tex_ambig_; return nullptr; }
            return &it->second;
          }
        return nullptr;
      };
      std::map<std::uint32_t, std::map<std::uint32_t, int>> votes;
      std::map<std::uint32_t, std::map<std::uint32_t, int>> uvVotes;
      if (haveSxy && !tritex_.empty())
        for (const auto& tri : hmd->triangles())
          if (const auto* r = findReal(tri)) {
            votes[(std::uint32_t(tri.texture_page & 0x1ff) << 16) | tri.clut][(std::uint32_t(r->orig_tpage) << 16) | r->clut]++;
            if (r->moved) continue;
            int du = 999, dv = 999; bool ok = true;
            for (int c = 0; c < 3 && ok; ++c) {
              const auto sv = actor.vertex_sxy[tri.vertex_indices[std::size_t(c)]];
              bool f = false;
              for (const auto& [ps, puv] : r->uv) if (ps == sv) {
                f = true;
                const int a = int(puv & 0xff) - int(tri.uv[std::size_t(c)].u), b = int(puv >> 8) - int(tri.uv[std::size_t(c)].v);
                if (du == 999) { du = a; dv = b; } else ok = std::abs(a - du) <= 1 && std::abs(b - dv) <= 1;
                break;
              }
              ok = ok && f;
            }
            if (ok && du != 999) uvVotes[(std::uint32_t(tri.texture_page & 0x1ff) << 16) | tri.clut][(std::uint32_t(std::uint8_t(du)) << 8) | std::uint8_t(dv)]++;
          }
      std::map<std::uint32_t, std::pair<int, int>> uvOff;
      for (const auto& [pg, cand] : uvVotes) {
        int best = 0; std::uint32_t k = 0;
        for (const auto& [kk, n] : cand) if (n > best) { best = n; k = kk; }
        uvOff[pg] = {int(std::int8_t(k >> 8)), int(std::int8_t(k & 0xff))};
      }

      {
        auto& mem = uv_off_memo_[actor.model];
        for (const auto& [pg, o] : uvOff) mem[pg] = o;
        uvOff = mem;
      }
      std::map<std::uint32_t, std::uint32_t> remap;
      for (const auto& [from, cand] : votes) {
        int best = 0; std::uint32_t to = from;
        for (const auto& [k, n] : cand) if (n > best) { best = n; to = k; }
        remap[from] = to;

        remap.emplace(0x80000000u | (from >> 16), to >> 16);
      }
      remap_.swap(remap);

      const VCopy* actorCopy = nullptr;
      if (haveSxy && !tritex_.empty())
        for (const auto& tri : hmd->triangles())
          if (const auto* r = findReal(tri); r && r->moved) { actorCopy = &r->copy; break; }

      bool actorSemi = false;
      if (haveSxy && !tritex_.empty()) {
        int nSemi = 0, nAll = 0;
        for (const auto& tri : hmd->triangles()) if (const auto* r = findReal(tri)) { ++nAll; nSemi += r->semi; }
        actorSemi = nAll > 0 && nSemi * 2 > nAll;
        if (static const bool sl = std::getenv("SF3N_SEMILOG") != nullptr; sl && nSemi) std::fprintf(log, "semi %s %d/%d\n", mission_->models[actor.model].name.c_str(), nSemi, nAll);
      }

      auto& memo = tex_memo_[actor.id];
      memo.last_frame = render_frame_;
      if (actorCopy) { memo.has_copy = true; memo.copy = *actorCopy; }
      if (static const bool fl = std::getenv("SF3N_FACELOG") != nullptr; fl) {
        static std::set<std::string> seenFace;
        char k[200];
        std::snprintf(k, sizeof k, "CARA %s %s src %d,%d %dx%d -> %d,%d", mission_->name.c_str(), mission_->models[actor.model].name.c_str(),
                      actorCopy ? actorCopy->sx : -1, actorCopy ? actorCopy->sy : -1, actorCopy ? actorCopy->w : 0, actorCopy ? actorCopy->h : 0, actorCopy ? actorCopy->dx : -1, actorCopy ? actorCopy->dy : -1);
        if (seenFace.insert(k).second) std::fprintf(log, "%s\n", k);
      }
      if (memo.tris.size() != hmd->triangles().size()) memo.tris.assign(hmd->triangles().size(), TexMemo{});
      if (memo.wound.size() != verts.size()) memo.wound.assign(verts.size(), 0u);
      std::size_t ti = 0;
      for (const auto& tri : hmd->triangles()) {
        auto& tm = memo.tris[ti++];
        std::array<CV, 3> cv;
        bool ok = true;
        std::uint16_t triTpage = tri.texture_page, triClut = tri.clut;
        const TriTex* real = findReal(tri);

        const auto offIt = uvOff.find((std::uint32_t(tri.texture_page & 0x1ff) << 16) | tri.clut);
        const int odu = offIt != uvOff.end() ? offIt->second.first : 0, odv = offIt != uvOff.end() ? offIt->second.second : 0;
        static const bool texLista = std::getenv("SF3N_TEX_LISTA") != nullptr;
        if (real && !real->moved && !texLista) {
          bool same = true;
          for (int c = 0; c < 3 && same; ++c) {
            const auto sv = actor.vertex_sxy[tri.vertex_indices[std::size_t(c)]];
            bool found = false;
            for (const auto& [ps, puv] : real->uv) if (ps == sv) {
              found = true;
              same = std::abs(int(puv & 0xff) - int(tri.uv[std::size_t(c)].u) - odu) <= 1 && std::abs(int(puv >> 8) - int(tri.uv[std::size_t(c)].v) - odv) <= 1;
              break;
            }
            same = same && found;
          }
          if (!same) { real = nullptr; ++tex_ambig_; }
        }
        if (real) {

          for (int c = 0; c < 3; ++c) {
            const auto vi = tri.vertex_indices[std::size_t(c)];
            const auto sv = actor.vertex_sxy[vi];
            for (int k = 0; k < 3; ++k) {
              if (real->uv[std::size_t(k)].first != sv) continue;
              const std::uint32_t pc = real->col[std::size_t(k)];
              const unsigned r = pc & 0xff, g = (pc >> 8) & 0xff, b = (pc >> 16) & 0xff;
              if (r >= 0x30 && g * 4 <= r && b * 4 <= r) memo.wound[vi] = pc | 0x1000000u;
              else if (g * 2 > r) memo.wound[vi] = 0;
              break;
            }
          }
          tm.valid = true; tm.tpage = real->tpage; tm.clut = real->clut;
          for (int c = 0; c < 3; ++c) {
            tm.uv[std::size_t(c)] = std::uint16_t(tri.uv[std::size_t(c)].u | (tri.uv[std::size_t(c)].v << 8));
            const auto sv = haveSxy ? actor.vertex_sxy[tri.vertex_indices[std::size_t(c)]] : 0u;
            for (const auto& [ps, puv] : real->uv) if (ps == sv) { tm.uv[std::size_t(c)] = puv; break; }
          }
        }
        const bool useMemo = !real && tm.valid;
        if (useMemo) { triTpage = std::uint16_t((tri.texture_page & ~0x1ff) | tm.tpage); triClut = tm.clut; }
        else if (!real && !remap_.empty()) {
          const std::uint32_t from = (std::uint32_t(tri.texture_page & 0x1ff) << 16) | tri.clut;
          if (auto it = remap_.find(from); it != remap_.end()) { triTpage = std::uint16_t((tri.texture_page & ~0x1ff) | (it->second >> 16)); triClut = std::uint16_t(it->second); }
          else if (auto it2 = remap_.find(0x80000000u | (tri.texture_page & 0x1ffu)); it2 != remap_.end()) triTpage = std::uint16_t((tri.texture_page & ~0x1ff) | it2->second);
        }
        {
          {
            if (real) {
              static const bool tl = std::getenv("SF3N_TRITEXLOG") != nullptr;
              if (tl) { ++tt_hit_; if (real->clut != tri.clut || (real->tpage & 0x1ff) != (tri.texture_page & 0x1ff)) { ++tt_diff_; if (tt_diff_ < 40) std::fprintf(stderr, "tritex dif clut %04x->%04x tp %04x->%04x\n", tri.clut, real->clut, tri.texture_page, real->tpage); } }
              triTpage = real->tpage; triClut = real->clut;
            } else if (tl0_) ++tt_miss_;
          }
        }
        const int ownAlias = mis().models[actor.model].own_alias;
        if (ownAlias >= 0) { triTpage = std::uint16_t((ownAlias + 1) * 1024 + (2 << 7) + (tri.texture_page & 0x60)); triClut = 0; }
        for (int c = 0; c < 3 && ok; ++c) {
          const auto vi = tri.vertex_indices[std::size_t(c)];
          if (vi >= verts.size() || !part_ok_[vi]) { ok = false; break; }
          const auto& w = world_verts_[vi];
          std::array<double, 3> col = vert_colors_[vi];
          static const bool noWound = std::getenv("SF3N_NOWOUND") != nullptr;
          if (static const bool cl = std::getenv("SF3N_COLLOG") != nullptr; cl && real && c == 0 && field_no_ + 3 >= std::uint64_t(std::atoll(std::getenv("SF3N_COLLOG")))) {
            const auto sv = actor.vertex_sxy[vi];
            for (int k = 0; k < 3; ++k) if (real->uv[std::size_t(k)].first == sv) { const auto pc = real->col[std::size_t(k)]; std::fprintf(log, "COLCMP %s pc %d %d %d pc_calc %.0f %.0f %.0f\n", mis().models[actor.model].name.c_str(), int(pc & 0xff), int((pc >> 8) & 0xff), int((pc >> 16) & 0xff), col[0], col[1], col[2]); break; }
          }
          if (const auto wc = noWound ? 0u : memo.wound[vi]) col = {double(wc & 0xff), double((wc >> 8) & 0xff), double((wc >> 16) & 0xff)};
          else if (!xray_actors_) { const double f = fogFactor(depthOf(w)); col = {col[0] * f, col[1] * f, col[2] * f}; }
          if (xray_actors_) col = xrayTint(col);
          double u = double(tri.uv[std::size_t(c)].u), v = double(tri.uv[std::size_t(c)].v);
          if (!texLista && ownAlias < 0) { u += odu; v += odv; }
          if (useMemo && ownAlias < 0) { u = double(tm.uv[std::size_t(c)] & 0xff); v = double(tm.uv[std::size_t(c)] >> 8); }
          if (real && ownAlias < 0) {

            const auto sv = actor.vertex_sxy[vi];
            for (const auto& [ps, puv] : real->uv) if (ps == sv) {
              if (tl0_ && (std::abs(double(puv & 0xff) - u) > 2 || std::abs(double(puv >> 8) - v) > 2)) { ++tt_uvdiff_; if (tt_uvdiff_ < 60) std::fprintf(stderr, "uvdif %s tri %zu v%d (%d,%d)->(%d,%d) tp %04x->%04x\n", mission_->models[actor.model].name.c_str(), ti - 1, c, int(u), int(v), int(puv & 0xff), int(puv >> 8), tri.texture_page, real->tpage); }
              u = double(puv & 0xff); v = double(puv >> 8); break;
            }
          }
          if (feet_pull_ > 0.5 && w[1] > feet_low_ - feet_pull_ - 16.0 && !xray_actors_) {
            const auto& E = view.eye;
            const double ex = E[0] - w[0], ey = E[1] - w[1], ez = E[2] - w[2], l = std::sqrt(ex * ex + ey * ey + ez * ez);
            if (l > 1.0) {
              const double pull = std::min({feet_pull_ / std::max(std::fabs(ey) / l, 0.2) + 2.0, 70.0, l * 0.3});
              const std::array<double, 3> wp{w[0] + ex / l * pull, w[1] + ey / l * pull, w[2] + ez / l * pull};
              cv[std::size_t(c)] = CV{wp[0], wp[1], wp[2], u, v, col[0], col[1], col[2], depthOf(wp)};
              continue;
            }
          }
          cv[std::size_t(c)] = CV{w[0], w[1], w[2], u, v, col[0], col[1], col[2], depthOf(w)};
        }

        if (ok && !actorSemi && std::min({cv[0].depth, cv[1].depth, cv[2].depth}) < actor_near_cull_) continue;

        if (ok && !real && !useMemo && ownAlias < 0 && memo.has_copy) {
          std::array<std::uint16_t, 3> uv3{};
          for (int c = 0; c < 3; ++c) uv3[std::size_t(c)] = std::uint16_t(int(cv[std::size_t(c)].u) | (int(cv[std::size_t(c)].v) << 8));
          std::uint16_t tp = triTpage;
          if (moveTex(tp, uv3, memo.copy)) {
            triTpage = tp;
            for (int c = 0; c < 3; ++c) { cv[std::size_t(c)].u = double(uv3[std::size_t(c)] & 0xff); cv[std::size_t(c)].v = double(uv3[std::size_t(c)] >> 8); }
          }
        }
        if (static const long long pd = std::getenv("SF3N_PAGEDBG") ? std::atoll(std::getenv("SF3N_PAGEDBG")) : -1; pd >= 0 && ok && (triTpage & 0xf) == unsigned(pd & 0xf) && !((triTpage >> 4) & 1) && render_frame_ % 4 == 0)
          std::fprintf(log, "PAGINA campo %llu %s id %llu tri %zu %s tp %04x clut %04x modelo tp %04x clut %04x uv %d,%d %d,%d %d,%d\n", (unsigned long long)field_no_, mis().models[actor.model].name.c_str(), (unsigned long long)actor.id, ti - 1, real ? "real" : useMemo ? "memo" : "remap", triTpage, triClut, tri.texture_page, tri.clut, int(cv[0].u), int(cv[0].v), int(cv[1].u), int(cv[1].v), int(cv[2].u), int(cv[2].v));

        static const bool cullActors = std::getenv("SF3N_DOS_CARAS") == nullptr;
        if (ok && nv_flat_) {

          static const double nvc[5][3] = {{72, 255, 0}, {0, 220, 150}, {0, 165, 171}, {7, 104, 149}, {5, 26, 98}};
          const int lv = std::clamp(nv_age_, 0, 4);
          auto cs = cv;
          for (auto& v : cs) { v.r = nvc[lv][0]; v.g = nvc[lv][1]; v.b = nvc[lv][2]; }
          emitClippedTriangle(cs, triTpage, triClut, false, cullActors, false);
          continue;
        }
        if (ok && xray_actors_) {

          static const double base = std::clamp(xrayCfg("rayosx_base", "SF3N_RAYOSX_BASE", 35), 0, 100) / 100.0;
          if (base > 0) {
            auto cb = cv;
            for (auto& v : cb) { v.r *= base; v.g *= base; v.b *= base; }
            emitClippedTriangle(cb, triTpage, triClut, false, cullActors, false);
            emitClippedTriangle(cv, std::uint16_t((triTpage & ~0x60u) | 0x20u), triClut, true, cullActors, true);
          } else emitClippedTriangle(cv, triTpage, triClut, false, cullActors, true);
        } else
        if (ok) emitClippedTriangle(cv, triTpage, triClut, actorSemi, cullActors || actorSemi);
      }
    }

    for (const auto& obj : cur().objects) {
      if (nv_flat_ && xray_actors_) break;
      if (xray_world_ || xray_actors_) {

        bool held = false;
        if (std::holds_alternative<sf::assets::GmdModel>(mis().models[obj.model].geometry))
          for (const auto& h : xray_hands_)
            if (std::hypot(obj.xf.b[0] - h[0], obj.xf.b[1] - h[1], obj.xf.b[2] - h[2]) < 60.0) { held = true; break; }
        if (xray_actors_ != held) continue;
      }
      if (static const bool ho = std::getenv("SF3N_HIDEOBJ") != nullptr; ho) continue;
      if (const char* hn = std::getenv("SF3N_HIDEOBJNOMBRE"); hn && mis().models[obj.model].name.rfind(hn, 0) == 0) continue;
      Affine xf = obj.xf;
      if (obj.prev >= 0 && std::size_t(obj.prev) < prv().objects.size()) xf = lerp(prv().objects[std::size_t(obj.prev)].xf, obj.xf, alpha);
      if (const char* odl = std::getenv("SF3N_OBJDRAWLOG"); odl && mis().models[obj.model].name.rfind(odl, 0) == 0) {
        const Affine* pv = obj.prev >= 0 && std::size_t(obj.prev) < prv().objects.size() ? &prv().objects[std::size_t(obj.prev)].xf : nullptr;
        std::fprintf(log, "OBJDRAW cuadro %llu alpha %.2f xf=[%.2f %.2f %.2f/%.2f %.2f %.2f/%.2f %.2f %.2f|%.0f %.0f %.0f] cur=[%.2f %.2f %.2f|%.0f %.0f %.0f] prev=%s[%.2f %.2f %.2f|%.0f %.0f %.0f]\n", (unsigned long long)render_frame_, alpha,
          xf.a[0], xf.a[1], xf.a[2], xf.a[3], xf.a[4], xf.a[5], xf.a[6], xf.a[7], xf.a[8], xf.b[0], xf.b[1], xf.b[2], obj.xf.a[0], obj.xf.a[1], obj.xf.a[2], obj.xf.b[0], obj.xf.b[1], obj.xf.b[2],
          pv ? "" : "NO", pv ? pv->a[0] : 0.0, pv ? pv->a[1] : 0.0, pv ? pv->a[2] : 0.0, pv ? pv->b[0] : 0.0, pv ? pv->b[1] : 0.0, pv ? pv->b[2] : 0.0);
      }
      const auto& geo = mis().models[obj.model].geometry;
      if (const auto* gmd = std::get_if<sf::assets::GmdModel>(&geo)) {
        const auto& verts = gmd->vertices();
        if (shadows_mode_ && !frame_casters_.empty()) objectShadow(*gmd, xf, obj.draw_flags, view);
        if (static const bool info = std::getenv("SF3N_MODELINFO") != nullptr; info) {
          static std::set<std::size_t> seen;
          if (seen.insert(obj.model).second) {
            std::map<int, int> fl; int deg = 0; std::set<int> pages;
            for (const auto& t : gmd->triangles()) { fl[t.flags]++; deg += t.degenerate; pages.insert(t.texture_page); }
            std::fprintf(log, "modelo %s: %zu tri, %d degenerados, flags", mis().models[obj.model].name.c_str(), gmd->triangles().size(), deg);
            for (auto [f, n] : fl) std::fprintf(log, " %d:%d", f, n);
            std::fprintf(log, " paginas"); for (int pg : pages) std::fprintf(log, " %d", pg);
            for (const auto& t : gmd->triangles()) { std::fprintf(log, " [clut %04x tp %04x fl %d", t.clut, t.texture_page, t.flags); for (int k = 0; k < 3; ++k) { const auto ni = t.normal_indices[std::size_t(k)]; if (ni < gmd->normals().size()) { const auto& nn = gmd->normals()[ni]; const auto c = shade(obj.light, nn.x * 64.0, nn.y * 64.0, nn.z * 64.0); std::fprintf(log, " n(%d,%d,%d)=%.0f", nn.x, nn.y, nn.z, c[0]); } } std::fprintf(log, "]"); }
            std::fprintf(log, " luz bk %.0f %.0f %.0f llm %.0f %.0f %.0f / %.0f %.0f %.0f / %.0f %.0f %.0f", obj.light.bk[0], obj.light.bk[1], obj.light.bk[2], obj.light.llm[0], obj.light.llm[1], obj.light.llm[2], obj.light.llm[3], obj.light.llm[4], obj.light.llm[5], obj.light.llm[6], obj.light.llm[7], obj.light.llm[8]);
            { double sl = 0; for (const auto& nn : gmd->normals()) sl += std::sqrt(double(nn.x) * nn.x + double(nn.y) * nn.y + double(nn.z) * nn.z); std::fprintf(log, " |n| %.1f", gmd->normals().empty() ? 0.0 : sl / double(gmd->normals().size())); }
            if (!gmd->triangles().empty()) { const auto& t0 = gmd->triangles()[0]; const auto ni = t0.normal_indices[0]; if (ni < gmd->normals().size()) { const auto& nn = gmd->normals()[ni]; const auto c = shade(obj.light, nn.x * 64.0, nn.y * 64.0, nn.z * 64.0); std::fprintf(log, " col0 %.0f %.0f %.0f", c[0], c[1], c[2]); } }
            std::fprintf(log, " a=[%.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f]\n", obj.xf.a[0], obj.xf.a[1], obj.xf.a[2], obj.xf.a[3], obj.xf.a[4], obj.xf.a[5], obj.xf.a[6], obj.xf.a[7], obj.xf.a[8]);
            double mn[3] = {1e9, 1e9, 1e9}, mx[3] = {-1e9, -1e9, -1e9};
            for (const auto& v : verts) { const auto w = obj.xf.apply(v.x, v.y, v.z); for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], w[std::size_t(k)]); mx[k] = std::max(mx[k], w[std::size_t(k)]); } }
            std::fprintf(log, "   caja %.0f..%.0f %.0f..%.0f %.0f..%.0f (%zu vertices)\n", mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], verts.size());
            if (const char* td = std::getenv("SF3N_TRIDUMP"); td && mis().models[obj.model].name.rfind(td, 0) == 0)
              for (const auto& t : gmd->triangles()) { std::fprintf(log, "   tri fl %d tp %04x clut %04x semi %d:", t.flags, t.texture_page, t.clut, int(t.semi_transparent)); for (int k = 0; k < 3; ++k) { const auto vi = t.vertex_indices[std::size_t(k)]; if (vi < verts.size()) std::fprintf(log, " v%u(%d,%d,%d) uv(%d,%d)", unsigned(vi), verts[vi].x, verts[vi].y, verts[vi].z, t.uv[std::size_t(k)].u, t.uv[std::size_t(k)].v); } std::fprintf(log, "\n"); }
          }
        }

        double bbScale = 1.0;
        if (static const bool sinPanel = std::getenv("SF3N_PANEL_SIN_ACERCAR") != nullptr; !sinPanel && cur().camera.valid && !gmd->triangles().empty()) {
          bool allSemi = true; for (const auto& t : gmd->triangles()) if (!t.semi_transparent) { allSemi = false; break; }
          if (allSemi) {
            const auto& cr = cur().camera.rot;
            double err = 1e9, rn[3] = {0, 0, 0};
            for (int i = 0; i < 3; ++i) rn[i] = std::sqrt(xf.a[std::size_t(i * 3)] * xf.a[std::size_t(i * 3)] + xf.a[std::size_t(i * 3 + 1)] * xf.a[std::size_t(i * 3 + 1)] + xf.a[std::size_t(i * 3 + 2)] * xf.a[std::size_t(i * 3 + 2)]);

            for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) {
              const double sg[3] = {double(sx), 1.0, double(sz)}; double e2 = 0;
              for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) e2 += std::fabs(xf.a[std::size_t(i * 3 + j)] / std::max(1e-9, rn[i]) - cr[std::size_t(j * 3 + i)] * sg[j]);
              err = std::min(err, e2);
            }
            if (err < 0.3) {
              double rad = 0; for (const auto& v : verts) rad = std::max(rad, std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
              rad *= std::max({rn[0], rn[1], rn[2]});
              const auto o = xf.apply(0, 0, 0);
              const double len = std::sqrt((o[0] - view.eye[0]) * (o[0] - view.eye[0]) + (o[1] - view.eye[1]) * (o[1] - view.eye[1]) + (o[2] - view.eye[2]) * (o[2] - view.eye[2]));
              if (len > 1e-6) bbScale = (len - std::min(rad, len * 0.5)) / len;
            }
          }
        }
        if (static const char* pl = std::getenv("SF3N_PANELLOG"); pl && mis().models[obj.model].name.rfind(pl, 0) == 0) {
          bool allSemi = true; for (const auto& t : gmd->triangles()) if (!t.semi_transparent) allSemi = false;
          const auto& cr = cur().camera.rot; double err = 0;
          for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) err += std::fabs(xf.a[std::size_t(i * 3 + j)] - cr[std::size_t(j * 3 + i)] * (j == 1 ? 1.0 : -1.0));
          std::fprintf(log, "PANEL %s semi %d err %.3f escala %.3f\n", mis().models[obj.model].name.c_str(), int(allSemi), err, bbScale);
        }
        if (bbScale != 1.0) no_glow_pull_ = true;
        for (const auto& tri : gmd->triangles()) {
          if (tri.degenerate) continue;

          if (tri.flags == 0 && !(obj.draw_flags & 0x10)) continue;
          if (tri.flags == 1 && (obj.draw_flags & 0x80)) continue;
          std::array<CV, 3> cv;
          bool ok = true;
          for (int c = 0; c < 3; ++c) {
            const auto vi = tri.vertex_indices[std::size_t(c)];
            if (vi >= verts.size()) { ok = false; break; }
            auto w = xf.apply(verts[vi].x, verts[vi].y, verts[vi].z);
            const double fogDepth = depthOf(w);
            if (bbScale != 1.0) for (int a = 0; a < 3; ++a) w[std::size_t(a)] = view.eye[std::size_t(a)] + (w[std::size_t(a)] - view.eye[std::size_t(a)]) * bbScale;

            std::array<double, 3> col{tri.flags == 0 ? 255.0 : 128.0, tri.flags == 0 ? 255.0 : 128.0, tri.flags == 0 ? 255.0 : 128.0};
            const auto ni = tri.normal_indices[std::size_t(c)];
            if (tri.flags != 0 && ni < gmd->normals().size()) {
              const auto& n = gmd->normals()[ni];
              col = shade(obj.light, n.x * 64.0, n.y * 64.0, n.z * 64.0, 255.0);
            }
            if (tri.flags != 0 && !xray_actors_) { const double f = fogFactor(fogDepth); col = {col[0] * f, col[1] * f, col[2] * f}; }
            if (xray_actors_ && tri.flags != 0) col = xrayTint(col);
            if (const char* gl = std::getenv("SF3N_OBJCOLLOG"); gl && mis().models[obj.model].name.rfind(gl, 0) == 0 && c == 0) std::fprintf(log, "OBJCOL %s col %.0f %.0f %.0f niebla %.2f prof %.0f xray %d\n", mis().models[obj.model].name.c_str(), col[0], col[1], col[2], fogFactor(fogDepth), fogDepth, int(xray_));
            cv[std::size_t(c)] = CV{w[0], w[1], w[2], double(tri.uv[std::size_t(c)].u), double(tri.uv[std::size_t(c)].v), col[0], col[1], col[2], depthOf(w)};
          }

          std::uint16_t tp = tri.texture_page;
          if ((tp & 0xf) < 6) tp = std::uint16_t(tp + 6);
          std::uint16_t triClutObj = tri.clut;

          if (const int bk = mis().models[obj.model].bank; bk >= 0 && std::size_t(bk) < weapon_bank_.models.size()) {
            const auto& bt = weapon_bank_.models[std::size_t(bk)].tris;
            const auto ti = std::size_t(&tri - &gmd->triangles()[0]);
            if (ti < bt.size() && bt[ti].ok) {
              tp = bt[ti].tpage; triClutObj = 0;
              for (int c = 0; c < 3; ++c) { cv[std::size_t(c)].u = bt[ti].uv[std::size_t(c * 2)]; cv[std::size_t(c)].v = bt[ti].uv[std::size_t(c * 2 + 1)]; }
            }
          }
          if (tri.flags == 0) tp = std::uint16_t((tp & ~0x60u) | (1u << 5));
          static const bool noCullObj = std::getenv("SF3N_NOCULLOBJ") != nullptr;
          static const bool objDebug = std::getenv("SF3N_OBJDEBUG") != nullptr;
          if (static const int dbgSec = envInt("SF3N_SECDBG", 0); dbgSec == 2) continue;
          static const int objMode = std::getenv("SF3N_OBJMODE") ? std::atoi(std::getenv("SF3N_OBJMODE")) : 0;
          if (static const bool scd = std::getenv("SF3N_SCONCEDBG") != nullptr; scd && ok && mis().models[obj.model].name.rfind("SCONCE", 0) == 0) {
            static const double cols[6][3] = {{255, 0, 0}, {255, 128, 0}, {0, 255, 0}, {0, 128, 255}, {255, 0, 255}, {255, 255, 0}};
            const auto ti = std::size_t(&tri - &gmd->triangles()[0]);
            for (auto& v : cv) { v.r = cols[ti % 6][0]; v.g = cols[ti % 6][1]; v.b = cols[ti % 6][2]; }
            emitClippedTriangle(cv, tp, tri.clut, false, true, false);
            continue;
          }
          if (objMode == 1 && ok && mis().models[obj.model].name.rfind("SUPERGS", 0) == 0) { emitClippedTriangle(cv, tp, tri.clut, false, false, true); continue; }
          if (objDebug && ok) {
            static int dl = 0;
            if (dl < 6 && mis().models[obj.model].name.rfind("SUPERGS", 0) == 0) {
              ++dl;
              const auto n0 = tris_.size(), g0 = fx_g3_.size();
              for (auto& v : cv) { v.r = 255; v.g = 0; v.b = 255; }
              emitClippedTriangle(cv, tp, tri.clut, false, false, false);
              std::fprintf(log, "SUPERGS tri w0 %.0f %.0f %.0f d %.0f/%.0f/%.0f -> emitidos %zu", cv[0].x, cv[0].y, cv[0].z, cv[0].depth, cv[1].depth, cv[2].depth, fx_g3_.size() - g0);
              if (fx_g3_.size() > g0) std::fprintf(log, " xy %.1f,%.1f", double(fx_g3_.back().x0), double(fx_g3_.back().y0));
              std::fprintf(log, " cap %zu\n", fx_g3_.capacity());
              (void)n0;
              continue;
            }
            for (auto& v : cv) { v.r = 255; v.g = 0; v.b = 255; } emitClippedTriangle(cv, tp, tri.clut, false, false, false); continue; }
          if (ok) {
            bool two = noCullObj || tri.semi_transparent || triClutObj != tri.clut;
            if (!two && tri.flags != 0) {
              int u0 = 255, v0 = 255, u1 = 0, v1 = 0;
              for (const auto& uv : tri.uv) { u0 = std::min<int>(u0, uv.u); u1 = std::max<int>(u1, uv.u); v0 = std::min<int>(v0, uv.v); v1 = std::max<int>(v1, uv.v); }
              two = cutoutTexture(tp, tri.clut, u0, v0, u1, v1);
            }
            emitClippedTriangle(cv, tp, triClutObj, tri.semi_transparent || tri.flags == 0, !two);
          }
        }
        no_glow_pull_ = false;
      } else if (const auto* emd = std::get_if<sf::assets::EmdScene>(&geo)) {

        double bgScale = 1.0;
        {
          const auto o = xf.apply(0, 0, 0);
          const double dx = o[0] - view.eye[0], dy = o[1] - view.eye[1], dz = o[2] - view.eye[2];
          if (std::sqrt(dx * dx + dy * dy + dz * dz) < 80.0) {
            double maxR = 1.0;
            for (const auto& sec : emd->sections())
              for (const auto& v : sec.vertices) {
                const auto w = xf.apply(v.x, v.y, v.z);
                maxR = std::max(maxR, std::sqrt((w[0] - view.eye[0]) * (w[0] - view.eye[0]) + (w[1] - view.eye[1]) * (w[1] - view.eye[1]) + (w[2] - view.eye[2]) * (w[2] - view.eye[2])));
              }
            const double ea = std::max({std::fabs(view.eye[0]), std::fabs(view.eye[1]), std::fabs(view.eye[2])});
            bgScale = std::clamp((30000.0 - ea) / maxR, 1.0, 40.0);
          }
        }
        for (const auto& sec : emd->sections())
          for (const auto& poly : sec.polygons) {
            if (!poly.renderable || poly.degenerate || poly.duplicate) continue;
            auto mk = [&](unsigned c) {
              const auto& v = sec.vertices[poly.vertex_indices[c]];
              auto w = xf.apply(v.x, v.y, v.z);

              static const bool sinNiebla = std::getenv("SF3N_EMD_SIN_NIEBLA") != nullptr;
              const double f = (bgScale != 1.0 || sinNiebla) ? 1.0 : fogFactor(depthOf(w));
              if (bgScale != 1.0) for (int k = 0; k < 3; ++k) w[std::size_t(k)] = view.eye[std::size_t(k)] + (w[std::size_t(k)] - view.eye[std::size_t(k)]) * bgScale;
              return CV{w[0], w[1], w[2], double(poly.uv[c].u), double(poly.uv[c].v), double((v.color & 31) << 3) * f, double(((v.color >> 5) & 31) << 3) * f, double(((v.color >> 10) & 31) << 3) * f, depthOf(w)};
            };

            const bool semiPoly = !(poly.texture_page & 0x80);
            const auto tp = aliasTpage(std::uint16_t(poly.texture_page | 0x80), std::min<unsigned>(emd->textureBank(), 1u));
            const bool two = twoSidedRoom(poly, semiPoly);
            if (static const char* ed = std::getenv("SF3N_EMDDBG"); ed && mis().models[obj.model].name.rfind(ed, 0) == 0) {
              auto a = mk(0), b2 = mk(1), c2 = mk(2);
              for (auto* v : {&a, &b2, &c2}) { v->r = 255; v->g = 0; v->b = 255; }
              emitClippedTriangle({a, b2, c2}, tp, poly.clut, false, false, false);
              continue;
            }
            emitClippedTriangle({mk(0), mk(1), mk(2)}, tp, poly.clut, semiPoly, !two);
            if (poly.quad) emitClippedTriangle({mk(1), mk(3), mk(2)}, tp, poly.clut, semiPoly, !two);
          }
      }
    }
  }

  bool cutoutTexture(std::uint16_t tpage, std::uint16_t clut, int u0, int v0, int u1, int v1) {
    if (vram_.size() < 1024 * 512) return false;
    if (cutout_mission_ != &mis() || render_frame_ >= cutout_reset_) { cutout_mission_ = &mis(); cutout_cache_.clear(); cutout_reset_ = render_frame_ + 900; }
    const std::uint64_t key = (std::uint64_t(tpage & 0x1ff) << 48) | (std::uint64_t(clut) << 32) | (std::uint64_t(u0) << 24) | (std::uint64_t(v0) << 16) | (std::uint64_t(u1) << 8) | std::uint64_t(v1);
    if (auto it = cutout_cache_.find(key); it != cutout_cache_.end()) return it->second;
    const int px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256, depth = (tpage >> 7) & 3;
    const int cx = (clut & 0x3f) * 16, cy = (clut >> 6) & 0x1ff;
    int total = 0, holes = 0;
    const int su = std::max(1, (u1 - u0) / 12), sv = std::max(1, (v1 - v0) / 12);
    for (int v = v0; v <= v1; v += sv)
      for (int u = u0; u <= u1; u += su) {
        const int y = (py + v) & 511;
        std::uint16_t c;
        if (depth == 1) { const auto w = vram_[std::size_t(y) * 1024 + std::size_t((px + u / 2) & 1023)]; c = vram_[std::size_t(cy) * 1024 + std::size_t(cx + ((u & 1) ? (w >> 8) : (w & 0xff)))]; }
        else if (depth == 0) { const auto w = vram_[std::size_t(y) * 1024 + std::size_t((px + u / 4) & 1023)]; c = vram_[std::size_t(cy) * 1024 + std::size_t(cx + ((w >> ((u & 3) * 4)) & 15))]; }
        else c = vram_[std::size_t(y) * 1024 + std::size_t((px + u) & 1023)];
        ++total; holes += c == 0;
      }
    const bool r = total > 0 && holes * 10 >= total;
    cutout_cache_[key] = r;
    return r;
  }
  template <class Poly> bool twoSidedRoom(const Poly& poly, bool semiPoly) {
    if (poly.double_sided || semiPoly) return true;
    int u0 = 255, v0 = 255, u1 = 0, v1 = 0;
    for (unsigned c = 0; c < (poly.quad ? 4u : 3u); ++c) { u0 = std::min<int>(u0, poly.uv[c].u); u1 = std::max<int>(u1, poly.uv[c].u); v0 = std::min<int>(v0, poly.uv[c].v); v1 = std::max<int>(v1, poly.uv[c].v); }
    return cutoutTexture(std::uint16_t(poly.texture_page | 0x80), poly.clut, u0, v0, u1, v1);
  }
  const void* cutout_mission_ = nullptr;
  std::uint64_t cutout_reset_ = 0;
  std::unordered_map<std::uint64_t, bool> cutout_cache_;

  static void lookRotate(std::array<double, 9>& rot, double ly, double lp) {
    const double ay = -ly * (2.0 * 3.14159265358979 / 4096.0), ap = lp * (2.0 * 3.14159265358979 / 4096.0);
    if (ay == 0.0 && ap == 0.0) return;
    auto rotY = [&](double* v) { const double c = std::cos(ay), s = std::sin(ay), x = v[0], z = v[2]; v[0] = x * c - z * s; v[2] = x * s + z * c; };
    double R[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) R[i][j] = rot[std::size_t(i * 3 + j)];
    for (auto& row : R) rotY(row);
    const double c = std::cos(ap), s = std::sin(ap);
    for (int j = 0; j < 3; ++j) { const double d = R[1][j], f = R[2][j]; R[1][j] = d * c - f * s; R[2][j] = d * s + f * c; }
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) rot[std::size_t(i * 3 + j)] = R[i][j];
  }
  Camera disp_cam_{};
  bool disp_aim_ = false;
  struct ReticleFix { bool on = false; std::array<double, 9> rg{}, rr{}; double fx = 1, fy = 1; } reticle_;

  template <class Map> std::uint32_t reticlePoint(std::uint32_t xy, const Map& map) const {
    const double gx = sx11(xy), gy = sy11(xy);
    const double rx = (gx + map.ox - map.ax - 192.0) / reticle_.fx, ryy = (gy + map.oy - map.ay - Map::center_y) / reticle_.fy;
    double w[3];
    for (int j = 0; j < 3; ++j) w[j] = reticle_.rg[std::size_t(j)] * rx + reticle_.rg[std::size_t(3 + j)] * ryy + reticle_.rg[std::size_t(6 + j)];
    double c[3];
    for (int i = 0; i < 3; ++i) c[i] = reticle_.rr[std::size_t(i * 3)] * w[0] + reticle_.rr[std::size_t(i * 3 + 1)] * w[1] + reticle_.rr[std::size_t(i * 3 + 2)] * w[2];
    if (c[2] < 1e-3) return xy;
    const int nx = int(std::lround(c[0] / c[2] * reticle_.fx + 192.0 - map.ox + map.ax)), ny = int(std::lround(c[1] / c[2] * reticle_.fy + Map::center_y - map.oy + map.ay));
    return (std::uint32_t(ny & 0x7ff) << 16) | std::uint32_t(nx & 0x7ff);
  }

  void drawNativeWorld() {
    reticle_.on = false;
    uploadMissionVram();
    const double now = nowSeconds();
    const double alpha = prv().camera.valid ? alpha_ : 1.0;
    Camera cam = interpolate(prv().camera, cur().camera, alpha);
    zoom_fy_ = (aim_mode_ && nowSeconds() - last_aim_take_ < 0.3 && cur().camera.valid) ? cur().camera.fy : 0.0;
    if (!(aim_mode_ && nowSeconds() - last_aim_take_ < 0.3)) aim_locked_ = false;
    if (aim_mode_ && nowSeconds() - last_aim_take_ < 0.3 && std::getenv("SF3N_SIN_ADELANTO") == nullptr) {
      double ly = 0, lp = 0;

      const bool direct = cur().aim_valid && prv().aim_valid && std::getenv("SF3N_AIM_CAMARA") == nullptr;
      if (direct) {
        auto wrap = [](double d) { while (d > 2048) d -= 4096; while (d < -2048) d += 4096; return d; };
        const double aimY = prv().aim_yaw + wrap(cur().aim_yaw - prv().aim_yaw) * alpha, aimP = prv().aim_pitch + (cur().aim_pitch - prv().aim_pitch) * alpha;
        const double camY = std::atan2(cam.rot[6], cam.rot[8]) * 4096.0 / (2 * 3.14159265358979);
        const double camP = std::atan2(cam.rot[7], std::hypot(cam.rot[6], cam.rot[8])) * 4096.0 / (2 * 3.14159265358979);

        ly = wrap(aimY - camY) * aim_sy_;
        lp = (aimP - camP) * aim_sp_;

        if (!aim_locked_ && std::fabs(ly) < 300 && std::fabs(lp) < 300) aim_locked_ = true;
        if (!aim_locked_) { ly = 0; lp = 0; }
        else if (std::getenv("SF3N_MIRA_JUEGO") == nullptr) {

          const auto& gc = cur().camera;
          const double gY = std::atan2(gc.rot[6], gc.rot[8]) * 4096.0 / (2 * 3.14159265358979);
          const double gP = std::atan2(gc.rot[7], std::hypot(gc.rot[6], gc.rot[8])) * 4096.0 / (2 * 3.14159265358979);
          const double ry = wrap(cur().aim_yaw - gY) * aim_sy_, rp = (cur().aim_pitch - gP) * aim_sp_;
          if (std::fabs(ry) < 300 && std::fabs(rp) < 300 && gc.fx > 1 && gc.fy > 1) {
            reticle_.rg = gc.rot; reticle_.rr = gc.rot; lookRotate(reticle_.rr, ry, rp);
            reticle_.fx = gc.fx; reticle_.fy = gc.fy; reticle_.on = true;
          }
        }

        {
          const bool scope = cur().camera.fy > 450.0 && aim_locked_;
          bool idle;
          { std::lock_guard lk(look_mtx_); idle = mouse_acc_x_.load() == 0 && mouse_acc_y_.load() == 0 && std::fabs(yaw_accum_) < 1 && std::fabs(pitch_accum_) < 1 && double(taken_yaw_) == cur().aim_cy && double(taken_pitch_) == cur().aim_cp; }
          const double now = nowSeconds(), dt = std::clamp(now - aim_blend_t_, 0.0, 0.1);
          aim_blend_t_ = now;
          if (!scope) { scope_ry_ = 0; scope_rp_ = 0; }
          else if (idle && std::getenv("SF3N_RESTO") != nullptr) {
            const double a = 1.0 - std::exp(-dt / 0.25);
            scope_ry_ += (-ly - scope_ry_) * a; scope_rp_ += (-lp - scope_rp_) * a;
          }
          ly += scope_ry_; lp += scope_rp_;
        }

        {
          std::lock_guard lk(look_mtx_);
          const double cy = prv().aim_cy + (cur().aim_cy - prv().aim_cy) * alpha, cp = prv().aim_cp + (cur().aim_cp - prv().aim_cp) * alpha;

          const double py = yaw_accum_ + mouse_acc_x_.load() * mouseGain();
          const double pp = pitch_accum_ + (mouse_invert_ ? -1.0 : 1.0) * mouse_acc_y_.load() * mouseGain();
          ly += double(taken_yaw_) - cy + std::trunc(py);
          lp += double(taken_pitch_) - cp + std::trunc(pp);
        }
      } else
      lookLead(prv().camera.valid ? prv().field : cur().field, cur().field, alpha, ly, lp);
      static const double sy = std::getenv("SF3N_LEAD_SY") ? std::atof(std::getenv("SF3N_LEAD_SY")) : 1.0;
      static const double sp = std::getenv("SF3N_LEAD_SP") ? std::atof(std::getenv("SF3N_LEAD_SP")) : 1.0;
      ly = std::clamp(ly, -2048.0, 2048.0); lp = std::clamp(lp, -1024.0, 1024.0);
      lookRotate(cam.rot, sy * ly, sp * lp);
    }

    static const bool sinOrbita = std::getenv("SF3N_ORBITA") == nullptr;
    if (!sinOrbita && camera_firm_ && !(aim_mode_ && nowSeconds() - last_aim_take_ < 0.3) && in_play_.load() && cur().aim_valid && prv().aim_valid &&
        cur().cam_point_valid && prv().cam_point_valid && cam.valid) {
      auto wrap = [](double d) { while (d > 2048) d -= 4096; while (d < -2048) d += 4096; return d; };
      const double aimY = prv().aim_yaw + wrap(cur().aim_yaw - prv().aim_yaw) * alpha;
      const double camY = std::atan2(cam.rot[6], cam.rot[8]) * 4096.0 / (2 * 3.14159265358979);
      const double base = wrap(aimY - camY) * aim_sy_;
      double ly = base;
      {
        std::lock_guard lk(look_mtx_);
        const double cy = prv().aim_cy + (cur().aim_cy - prv().aim_cy) * alpha;
        const double py = yaw_accum_ + mouse_acc_x_.load() * mouseGain();
        ly += double(taken_yaw_) - cy + std::trunc(py);
      }
      const double w = std::clamp((400.0 - std::fabs(base)) / 150.0, 0.0, 1.0);

      static const double maxLead = envInt("SF3N_ORBITA_MAX", 64);
      ly = std::clamp(ly * w, -maxLead, maxLead);
      if (ly != 0.0) {
        std::array<double, 3> P{};
        for (int k = 0; k < 3; ++k) P[std::size_t(k)] = prv().cam_point[std::size_t(k)] + (cur().cam_point[std::size_t(k)] - prv().cam_point[std::size_t(k)]) * alpha;
        const double ay = -ly * (2.0 * 3.14159265358979 / 4096.0), c = std::cos(ay), sn = std::sin(ay);
        const double dx = cam.eye[0] - P[0], dz = cam.eye[2] - P[2];
        cam.eye[0] = P[0] + dx * c - dz * sn; cam.eye[2] = P[2] + dx * sn + dz * c;
        lookRotate(cam.rot, ly, 0.0);
      }
      if (static const bool ol = std::getenv("SF3N_ORBITALOG") != nullptr; ol) std::fprintf(log, "ORBITA %.4f base %.1f giro %.1f peso %.2f\n", nowSeconds(), base, ly, w);
    }
    if (static const bool at = std::getenv("SF3N_AIMTRACE") != nullptr; at) {
      double ly = 0, lp = 0; lookLead(prv().camera.valid ? prv().field : cur().field, cur().field, alpha, ly, lp);
      const double yawRaw = std::atan2(cur().camera.rot[6], cur().camera.rot[8]) * 4096.0 / (2 * 3.14159265358979);
      const double yawShown = std::atan2(cam.rot[6], cam.rot[8]) * 4096.0 / (2 * 3.14159265358979);
      const double pRaw = std::atan2(cur().camera.rot[7], std::hypot(cur().camera.rot[6], cur().camera.rot[8])) * 4096.0 / (2 * 3.14159265358979);
      const double pShown = std::atan2(cam.rot[7], std::hypot(cam.rot[6], cam.rot[8])) * 4096.0 / (2 * 3.14159265358979);
      std::fprintf(log, "AIM %.4f campo %llu prv %llu a %.2f yawJuego %.1f mostrado %.1f adelanto %.1f modo %d apuntado %.0f %.0f pitch %.1f %.1f fy %.0f\n", nowSeconds(), (unsigned long long)cur().field, (unsigned long long)prv().field, alpha, yawRaw, yawShown, ly, int(aim_mode_), cur().aim_yaw, cur().aim_pitch, pRaw, pShown, cur().camera.fy);
    }
    if (static const bool trace = std::getenv("SF3N_TRACE") != nullptr; trace) {
      const Affine* g = !cur().actors.empty() ? rootOf(cur().actors[0]) : nullptr;
      std::fprintf(log, "traza %.4f a=%.2f seq=%llu ojo %.1f %.1f %.1f gabe %.1f %.1f\n", nowSeconds(), alpha, (unsigned long long)cur().sequence,
                   cam.eye[0], cam.eye[1], cam.eye[2], g ? g->b[0] : 0.0, g ? g->b[2] : 0.0);
    }
    disp_cam_ = cam; disp_aim_ = aim_mode_ && nowSeconds() - last_aim_take_ < 0.3;
    if (static const bool fl4 = std::getenv("SF3N_FREELOG") != nullptr; fl4 && !cur().actors.empty()) if (const Affine* g = rootOf(cur().actors[0])) {
      const double k = 4096.0 / (2 * 3.14159265358979);
      const auto pl = player_ptr_addr_ ? ramWord(player_ptr_addr_) : 0u;
      std::fprintf(log, "RUMBO cuadro %llu pos %.0f %.0f z %.0f x %.0f -z %.0f mem220 %d vista %.0f\n", (unsigned long long)render_frame_, g->b[0], g->b[2], std::atan2(g->a[2], g->a[8]) * k, std::atan2(g->a[0], g->a[6]) * k, std::atan2(-g->a[2], -g->a[8]) * k, (pl >> 24) == 0x80u ? int(std::int32_t(ramWord(pl + 0x220))) : -1, std::atan2(cam.rot[6], cam.rot[8]) * k);
    }
    if (cam.valid && !disp_aim_) { disp_view_yaw_ = std::atan2(cam.rot[6], cam.rot[8]) * 4096.0 / (2 * 3.14159265358979); disp_view_t_ = nowSeconds(); }
    const auto view = makeView(cam);
    static const bool nvOn = xrayCfg("nocturna_siluetas", "SF3N_NOCTURNA_SILUETAS", 1) != 0;
    const bool nv = nvOn && night_vision_.load();

    static const bool estela = xrayCfg("nocturna_estela", "SF3N_NOCTURNA_ESTELA", 1) != 0;
    static const bool verde = xrayCfg("nocturna_verde", "SF3N_NOCTURNA_VERDE", 1) != 0;
    nv_tint_ = verde;
    nv_flat_ = nv;
    nv_age_ = nv ? -1 : 0;
    emitWorld(view, alpha);
    nv_age_ = 0;

    if (rcur_) {
      const void* key = &*rcur_;
      if (!nv_views_.empty() && nv_views_.back().first == key) nv_views_.back().second = view;
      else { nv_views_.push_back({key, view}); if (nv_views_.size() > 12) nv_views_.erase(nv_views_.begin()); }
    }
    if (nv && rcur_) {
      std::size_t ci = 0; while (ci < rhist_.size() && rhist_[ci] != rcur_) ++ci;
      const auto keepCur = rcur_, keepPrv = rprev_; const auto rf = render_frame_; const bool ox = xray_;
      for (int age : {2, 3, 4, 0}) {
        if (age > 0 && (!estela || ci >= rhist_.size() || ci < std::size_t(age))) continue;
        DrawSync(0);
        if (age > 0) { rcur_ = rhist_[ci - std::size_t(age)]; rprev_.reset(); } else { rcur_ = keepCur; rprev_ = keepPrv; }
        xray_ = true; xray_actors_ = true; nv_age_ = age;
        ViewSetup v2 = view;
        static const bool pantalla = xrayCfg("nocturna_estela_pantalla", "SF3N_NOCTURNA_ESTELA_PANTALLA", 1) != 0;
        if (age > 0 && pantalla) { const void* k = &*rcur_; for (const auto& [kk, vv] : nv_views_) if (kk == k) v2 = vv; }
        emitWorld(v2, age > 0 ? 1.0 : alpha);
        DrawSync(0);
      }
      nv_age_ = 0; xray_actors_ = false; xray_ = ox; rcur_ = keepCur; rprev_ = keepPrv; render_frame_ = rf;
    }
    nv_flat_ = false;
    xray_view_ = view; xray_alpha_ = alpha; xray_view_ok_ = true;
  }

  bool xray_ = false, xray_view_ok_ = false, xray_drawn_ = false;
  std::vector<std::pair<const void*, ViewSetup>> nv_views_;
  bool xray_actors_ = false;
  bool xray_world_ = false;
  double world_gain_ = 1.0;
  std::vector<std::array<double, 3>> xray_hands_;

  static std::array<double, 3> xrayTint(std::array<double, 3> col) {
    static const double gv = std::clamp(xrayCfg("rayosx_verde", "SF3N_RAYOSX_VERDE", 170), 50, 400) / 100.0;
    static const double keep = std::clamp(xrayCfg("rayosx_color", "SF3N_RAYOSX_COLOR", 0), 0, 100) / 100.0;
    const double lum = std::max(0.299 * col[0] + 0.587 * col[1] + 0.114 * col[2], 70.0);
    const double gr[3] = {lum * gv * 0.30, lum * gv, lum * gv * 0.40};
    const double gain = std::max(1.0, gv * 0.8);
    for (int k = 0; k < 3; ++k) col[std::size_t(k)] = std::min(255.0, gr[k] * (1.0 - keep) + std::max(col[std::size_t(k)], 70.0) * gain * keep);
    return col;
  }
  double fog_shift_ = 0;
  ViewSetup xray_view_{};
  double xray_alpha_ = 1.0;
  std::vector<std::uint16_t> xray_noise_;
  std::uint32_t xray_rng_ = 0x12345678u;

  double xrayCut(const ViewSetup& view) {
    const double* E = view.eye.data(); const double* F = view.forward.data();
    std::vector<double> hits;
    for (const auto r : cur().rooms) {
      const auto& scene = mis().rooms[r];
      for (std::size_t si = 0; si < scene.sections().size(); ++si) {
        const auto& sec = scene.sections()[si];
        const std::vector<std::array<std::int16_t, 3>>* livePos = nullptr;
        if (auto it = cur().room_positions.find({r, std::uint16_t(si)}); it != cur().room_positions.end() && it->second.size() == sec.vertices.size()) livePos = &it->second;
        auto P = [&](std::size_t i, int k) -> double {
          if (livePos) return (*livePos)[i][std::size_t(k)];
          return k == 0 ? sec.vertices[i].x : k == 1 ? sec.vertices[i].y : sec.vertices[i].z;
        };
        for (const auto& poly : sec.polygons) {
          if (!poly.renderable || poly.degenerate) continue;
          auto tri = [&](unsigned a, unsigned b, unsigned c) {
            const std::size_t ia = poly.vertex_indices[a], ib = poly.vertex_indices[b], ic = poly.vertex_indices[c];
            const double e1[3] = {P(ib, 0) - P(ia, 0), P(ib, 1) - P(ia, 1), P(ib, 2) - P(ia, 2)};
            const double e2[3] = {P(ic, 0) - P(ia, 0), P(ic, 1) - P(ia, 1), P(ic, 2) - P(ia, 2)};
            const double pv[3] = {F[1] * e2[2] - F[2] * e2[1], F[2] * e2[0] - F[0] * e2[2], F[0] * e2[1] - F[1] * e2[0]};
            const double det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
            if (std::fabs(det) < 1e-9) return;
            const double tv[3] = {E[0] - P(ia, 0), E[1] - P(ia, 1), E[2] - P(ia, 2)};
            const double u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) / det;
            if (u < 0 || u > 1) return;
            const double qv[3] = {tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]};
            const double v = (F[0] * qv[0] + F[1] * qv[1] + F[2] * qv[2]) / det;
            if (v < 0 || u + v > 1) return;
            const double t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) / det;
            if (t > near_base_) hits.push_back(t);
          };
          tri(0, 1, 2);
          if (poly.quad) tri(1, 3, 2);
        }
      }
    }
    if (hits.empty()) return near_base_;
    std::sort(hits.begin(), hits.end());
    if (static const bool xl = std::getenv("SF3N_RAYOSXLOG") != nullptr; xl) { std::fprintf(log, "RAYOSX salas %zu (", cur().rooms.size()); for (auto r : cur().rooms) std::fprintf(log, " %u", unsigned(r)); std::fprintf(log, ") objetos"); for (const auto& o : cur().objects) { const double d = F[0] * (o.xf.b[0] - E[0]) + F[1] * (o.xf.b[1] - E[1]) + F[2] * (o.xf.b[2] - E[2]); const double rx = view.right[0] * (o.xf.b[0] - E[0]) + view.right[1] * (o.xf.b[1] - E[1]) + view.right[2] * (o.xf.b[2] - E[2]); std::fprintf(log, " %s(prof %.0f lado %.0f fl %x)", mis().models[o.model].name.c_str(), d, rx, unsigned(o.draw_flags)); } std::fprintf(log, " | actores %zu impactos", cur().actors.size()); for (std::size_t i = 0; i < hits.size() && i < 12; ++i) std::fprintf(log, " %.0f", hits[i]); std::fprintf(log, "\n"); }
    const double d1 = hits.front();
    static const double maxD = envInt("SF3N_RAYOSX_MAX", 6000), thick = envInt("SF3N_RAYOSX_GROSOR", 200);
    if (d1 > maxD) return near_base_;
    double cut = d1 + 8.0;
    for (const double t : hits) if (t > d1 + 8.0) { if (t < d1 + thick) cut = t + 4.0; break; }
    return cut;
  }

  bool xray_cull_ = false;
  bool xray_noise_up_ = false;
  const void* xray_noise_mis_ = nullptr;
  static constexpr int kNoiseAlias = 124;
  RECT16 xray_cull_rc_{};
  bool sectionOutsideRect(const sf::assets::EmdBounds& bb, const ViewSetup& v, const RECT16& rc) const {
    double mnx = 1e18, mxx = -1e18, mny = 1e18, mxy = -1e18;
    for (int k = 0; k < 8; ++k) {
      const double p[3] = {double((k & 1) ? bb.maximum_x : bb.minimum_x), double((k & 2) ? bb.maximum_y : bb.minimum_y), double((k & 4) ? bb.maximum_z : bb.minimum_z)};
      const double x = v.exact_rot[0] * p[0] + v.exact_rot[1] * p[1] + v.exact_rot[2] * p[2] + v.exact_t[0];
      const double y = v.exact_rot[3] * p[0] + v.exact_rot[4] * p[1] + v.exact_rot[5] * p[2] + v.exact_t[1];
      const double z = v.exact_rot[6] * p[0] + v.exact_rot[7] * p[1] + v.exact_rot[8] * p[2] + v.exact_t[2];
      if (z < 2.0) return false;
      const double sx = canvas_w * 0.5 + v.h * x / z, sy = canvas_h * 0.5 + v.h * y / z;
      mnx = std::min(mnx, sx); mxx = std::max(mxx, sx); mny = std::min(mny, sy); mxy = std::max(mxy, sy);
    }
    constexpr double m = 6.0;
    return mxx < rc.x - m || mnx > rc.x + rc.w + m || mxy < rc.y - m || mny > rc.y + rc.h + m;
  }
  bool drawXrayScope() {
    static const bool own = xrayOwnView();
    if (!xray_view_ok_ || !own) return false;
    auto& sp = scopePic();
    int x = 0, y = 0, w = 0, h = 0;
    {
      std::lock_guard<std::mutex> lk(sp.m);
      if (sp.seq == 0 || std::chrono::steady_clock::now() - sp.when > std::chrono::milliseconds(600) || sp.w <= 0 || sp.h <= 0) return false;
      x = sp.x; y = sp.y; w = sp.w; h = sp.h;
    }
    HudMap map;
    const float x0 = map.x(double(x)), x1 = map.x(double(x + w)), y0 = map.y(double(y)), y1 = map.y(double(y + h));

    const double wf = std::max(1.0, wideFactor());
    const double cx0 = 192.0 + (double(x0) - 192.0) / wf, cx1 = 192.0 + (double(x1) - 192.0) / wf;
    RECT16 rc{short(std::floor(cx0)), short(std::floor(y0)), short(std::ceil(cx1) - std::floor(cx0)), short(std::ceil(y1) - std::floor(y0))};
    if (rc.w <= 0 || rc.h <= 0) return false;
    std::vector<std::vector<std::uint32_t>> pks;
    std::vector<std::uint8_t> act;
    { std::lock_guard<std::mutex> lk(sp.m); pks = sp.pk; act = sp.actor; }
    if (pks.empty()) return false;
    DrawSync(0);

    GR_SetupClipMode(&rc, 1);

    static const bool mix = xrayCfg("rayosx_mezcla", "SF3N_RAYOSX_MEZCLA", 0) != 0;
    if (!mix) GR_Clear(rc.x, rc.y, rc.w, rc.h, 4, 5, 7);

    static const bool natActorsCfg = xrayCfg("rayosx_modelos", "SF3N_RAYOSX_MODELOS", 1) != 0;

    const bool natActors = natActorsCfg && !cur().actors.empty();
    DRAWENV draw{};
    SetDefDrawEnv(&draw, 0, 0, canvas_w, canvas_h);
    draw.clip = rc; draw.isbg = 0; draw.dtd = 0; draw.dfe = 1;
    PutDrawEnv(&draw);

    auto drawNatActors = [&]() {
    if (natActors && xray_view_ok_) {

      DrawSync(0);
      GR_ClearDepthOnly();
      const bool ox = xray_; const auto rf = render_frame_;
      xray_ = true; xray_actors_ = true;
      emitWorld(xray_view_, xray_alpha_);
      if (static const bool xl = std::getenv("SF3N_RAYOSXLOG") != nullptr; xl) std::fprintf(log, "RAYOSX modelos: actores %zu tris %zu\n", cur().actors.size(), tris_.size());
      DrawSync(0);
      xray_actors_ = false; xray_ = ox; render_frame_ = rf;
      PutDrawEnv(&draw);
      GR_SetupClipMode(&rc, 1);
    }
    };
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    if (static const bool xl = std::getenv("SF3N_RAYOSXLOG") != nullptr; xl) {
      int np = 0; for (const auto& pk : pks) if (!pk.empty() && (pk[0] >> 24) >= 0x20 && (pk[0] >> 24) < 0x40) ++np;
      std::fprintf(log, "RAYOSX dibujo: %zu paquetes, %d poligonos, caja %.1f,%.1f-%.1f,%.1f rc %d,%d %dx%d", pks.size(), np, double(x0), double(y0), double(x1), double(y1), rc.x, rc.y, rc.w, rc.h);
      { std::map<int,int> cc; for (const auto& pk : pks) if (!pk.empty()) cc[int(pk[0] >> 24)]++; std::fprintf(log, " frame %llu cmds", (unsigned long long)render_frame_); for (auto [k, v] : cc) std::fprintf(log, " %02x:%d", k, v); }
      for (const auto& pk : pks) if (!pk.empty() && (pk[0] >> 24) >= 0x20 && (pk[0] >> 24) < 0x30 && !((pk[0] >> 24) & 4)) { std::fprintf(log, " plano:"); for (auto w : pk) std::fprintf(log, " %08x", w); }
      for (const auto& pk : pks) if (!pk.empty() && (pk[0] >> 24) >= 0x20 && (pk[0] >> 24) < 0x40) { std::fprintf(log, " primero:"); for (auto w : pk) std::fprintf(log, " %08x", w); break; }
      std::fprintf(log, "\n");
    }

    static const bool natWorld = xrayWorldView();
    if (natWorld) {
      DrawSync(0);
      GR_Clear(rc.x, rc.y, rc.w, rc.h, 4, 5, 7);
      GR_ClearDepthOnly();
      const bool ox = xray_; const auto rf = render_frame_;
      static const double corte = std::clamp(xrayCfg("rayosx_corte", "SF3N_RAYOSX_CORTE", 500), 30, 5000);
      near_z = corte;
      xray_ = true; xray_world_ = true;
      world_gain_ = std::clamp(xrayCfg("rayosx_brillo", "SF3N_RAYOSX_BRILLO", 35), 5, 200) / 100.0;
      static const bool sinRecorte = std::getenv("SF3N_RAYOSX_SIN_RECORTE") != nullptr;
      xray_cull_ = !sinRecorte; xray_cull_rc_ = rc;
      emitWorld(xray_view_, xray_alpha_);
      xray_cull_ = false;
      world_gain_ = 1.0;
      DrawSync(0);
      near_z = near_base_; xray_ = ox; xray_world_ = false; render_frame_ = rf;
      PutDrawEnv(&draw);
      GR_SetupClipMode(&rc, 1);
    } else
    {

      static const int adjX = xrayCfg("rayosx_ajuste_x", "SF3N_RAYOSX_X", 0), adjY = xrayCfg("rayosx_ajuste_y", "SF3N_RAYOSX_Y", 0);
      HudMap pm;
      pm.ox = 192.0 + adjX; pm.oy = 120.0 + adjY;

      static const bool gameOrder = std::getenv("SF3N_RAYOSX_ORDEN_JUEGO") != nullptr;
      auto actorPk = [&](std::size_t pi) {
        const auto& pk = pks[pi];
        const auto c = pk[0] >> 24;
        if (c < 0x20 || c >= 0x40) return false;
        const std::uint32_t c0 = pk[0] & 0xffffffu;
        const bool gameGreen = (c & 4) && ((c0 >> 8) & 0xff) >= 0xc0 && (c0 & 0xff) < 0x60 && (c0 >> 16) < 0x60;
        return gameGreen || (pi < act.size() && act[pi]);
      };
      for (int pass = 0; pass < (gameOrder ? 1 : 2); ++pass)
      for (std::size_t pi = 0; pi < pks.size(); ++pi) {
        const auto& pk = pks[pi];
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        if (!gameOrder && c >= 0x20 && c < 0x40 && actorPk(pi) != (pass == 1)) continue;
        if (natActors && pass == 1) continue;
        if (c == 0xe1) { DR_TPAGE t{}; SetDrawTPage(&t, 1, 0, int(pk[0] & 0x1ff)); DrawPrim(&t); continue; }
        if (c >= 0x20 && c < 0x40) {

          static const bool opaque = std::getenv("SF3N_RAYOSX_OPACO") != nullptr;
          const bool keepSemi = !opaque;
          auto q = pk;
          if ((c & 4) && (c & 2) && !keepSemi) q[0] &= ~0x02000000u;

          static const int gain = std::clamp(xrayCfg("rayosx_luz", "SF3N_RAYOSX_LUZ", 100), 80, 400);
          if (gain != 100) {
            const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
            const int n = quad ? 4 : 3;
            auto sc = [&](std::uint32_t& w) {
              std::uint32_t o = w & 0xff000000u;
              for (int k = 0; k < 3; ++k) { const int v = int((w >> (8 * k)) & 0xff) * gain / 100; o |= std::uint32_t(std::min(v, 255)) << (8 * k); }
              w = o;
            };
            sc(q[0]);
            if (gour) { std::size_t idx = 1 + (tex ? 2 : 1); for (int k = 1; k < n && idx < q.size(); ++k) { sc(q[idx]); idx += tex ? 3 : 2; } }
          }

          static const int hl = std::clamp(xrayCfg("rayosx_resaltar", "SF3N_RAYOSX_RESALTAR", 35), 0, 100);

          const bool isActor = actorPk(pi);
          if (isActor && hl == 0) q[0] &= ~0x02000000u;
          if (hl > 0 && isActor) {
            const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
            const int n = quad ? 4 : 3;
            auto gr = [&](std::uint32_t& w) {
              int r = int(w & 0xff), g = int((w >> 8) & 0xff), b = int((w >> 16) & 0xff);
              r = r * (100 - hl / 2) / 100; b = b * (100 - hl / 2) / 100;
              g = std::min(255, g + (std::max(g, 0x40) * hl) / 100 + hl / 2);
              w = (w & 0xff000000u) | std::uint32_t(r) | (std::uint32_t(g) << 8) | (std::uint32_t(b) << 16);
            };
            gr(q[0]);
            if (gour) { std::size_t idx = 1 + (tex ? 2 : 1); for (int k = 1; k < n && idx < q.size(); ++k) { gr(q[idx]); idx += tex ? 3 : 2; } }
            q[0] &= ~0x02000000u;
          }
          if (static const bool st = std::getenv("SF3N_RAYOSX_SINTEX") != nullptr; st && (c & 4)) {
            const bool quad = c & 8, gour = c & 0x10; const int n = quad ? 4 : 3;
            std::vector<std::uint32_t> f; f.push_back((q[0] & 0xe8ffffffu & ~0x04000000u) | 0x00ff00u);
            std::size_t idx = 1;
            for (int k = 0; k < n; ++k) { if (k > 0 && gour) f.push_back(q[idx++] | 0x00ff00u); f.push_back(q[idx++]); idx++; }
            hudPolygon(f, pm);
          } else if (static const int sil = std::clamp(xrayCfg("rayosx_silueta", "SF3N_RAYOSX_SILUETA", 60), 0, 255); isActor && sil > 0 && (c & 4)) {

            const bool quad = c & 8, gour = c & 0x10; const int n = quad ? 4 : 3;
            std::vector<std::uint32_t> f;
            f.push_back((quad ? 0x28000000u : 0x20000000u) | std::uint32_t(sil / 4) | (std::uint32_t(sil) << 8) | (std::uint32_t(sil / 3) << 16));
            std::size_t idx = 1;
            for (int k = 0; k < n; ++k) { if (k > 0 && gour) ++idx; f.push_back(q[idx]); idx += 2; }
            hudPolygon(f, pm);
            auto t = q;
            t[0] |= 0x02000000u;
            const std::size_t uv1 = gour ? 5 : 4;
            if (uv1 < t.size()) t[uv1] = (t[uv1] & ~(3u << 21)) | (1u << 21);
            hudPolygon(t, pm);
          } else
          hudPolygon(q, pm);
        }
      }
    }
    GR_SetBlendMode(BM_NONE);
    drawNatActors();
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);

    {
      DR_TPAGE tp{}; SetDrawTPage(&tp, 1, 0, GetTPage(0, 2, 0, 0)); DrawPrim(&tp);
      static const int dk = xrayCfg("rayosx_oscuro", "SF3N_RAYOSX_OSCURO", 26);
      POLY_F4 q{}; setPolyF4(&q); setSemiTrans(&q, 1); setRGB0(&q, u_char(dk), u_char(dk), u_char(dk * 3 / 4));
      setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1); DrawPrim(&q);
    }

    {
      static const int nz = xrayCfg("rayosx_ruido", "SF3N_RAYOSX_RUIDO", 30);
      if (nz > 0) {
        DR_TPAGE tp{}; SetDrawTPage(&tp, 1, 0, GetTPage(0, 1, 0, 0)); DrawPrim(&tp);
        const int band = int((render_frame_ * 2) % unsigned(h + 40)) - 20;
        const float sx = (x1 - x0) / float(w), sy = (y1 - y0) / float(h);

        static const bool ruidoViejo = std::getenv("SF3N_RAYOSX_RUIDO_VIEJO") != nullptr;
        if (!ruidoViejo) {
          if (!xray_noise_up_ || xray_noise_mis_ != &mis()) {
            xray_noise_up_ = true; xray_noise_mis_ = &mis();
            std::vector<std::uint16_t> pg(64 * 256);
            std::uint32_t rng = 0x9e3779b9u;
            for (int k = 0; k < 2; ++k) {
              for (auto& t : pg) {
                rng = rng * 1664525u + 1013904223u;
                int v = int((rng >> 24) & 255);
                v = v * v / 255 * nz / 255;
                const int c5 = std::clamp(v >> 2, 0, 31);
                t = v < 2 ? 0 : std::uint16_t(0x8000u | (c5 << 10) | (c5 << 5) | c5);
              }
              GR_UploadVRAMAliasPage(kNoiseAlias + k, pg.data());
            }
          }
          xray_rng_ = xray_rng_ * 1664525u + 1013904223u;
          const int tw = std::min(w, 128), th = std::min(h, 256);
          const int u0 = int((xray_rng_ >> 8) % unsigned(128 - tw + 1)), v0 = int((xray_rng_ >> 16) % unsigned(256 - th + 1));
          POLY_FT4 q{}; setPolyFT4(&q); setSemiTrans(&q, 1); setRGB0(&q, 128, 128, 128);
          setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
          setUV4(&q, u_char(u0), u_char(v0), u_char(u0 + tw - 1), u_char(v0), u_char(u0), u_char(v0 + th - 1), u_char(u0 + tw - 1), u_char(v0 + th - 1));
          q.tpage = WeaponBank::aliasTpage(kNoiseAlias, std::uint16_t((2u << 7) | (1u << 5)));
          q.clut = 0;
          DrawPrim(&q);
          if (band < h && band + 6 > 0) {
            const int b0 = std::max(0, band), b1 = std::min(h, band + 6);
            const int bv = nz / 4;
            if (bv >= 2) {
              POLY_F4 f{}; setPolyF4(&f); setSemiTrans(&f, 1); setRGB0(&f, u_char(bv), u_char(bv), u_char(bv));
              setXY4(&f, x0, y0 + b0 * sy, x1, y0 + b0 * sy, x0, y0 + b1 * sy, x1, y0 + b1 * sy);
              DrawPrim(&f);
            }
          }
        } else
        for (int gy = 0; gy < h; ++gy) {
          const bool inBand = gy >= band && gy < band + 6;
          for (int gx = 0; gx < w; ++gx) {
            xray_rng_ = xray_rng_ * 1664525u + 1013904223u;
            int v = int((xray_rng_ >> 24) & 255);
            v = v * v / 255 * nz / 255;
            if (inBand) v += nz / 4;
            if (v < 2) continue;
            POLY_F4 q{}; setPolyF4(&q); setSemiTrans(&q, 1); setRGB0(&q, u_char(v), u_char(v), u_char(v));
            const float ax = x0 + float(gx) * sx, ay = y0 + float(gy) * sy;
            setXY4(&q, ax, ay, ax + sx, ay, ax, ay + sy, ax + sx, ay + sy);
            DrawPrim(&q);
          }
        }
      }
    }
    DrawSync(0);

    SetDefDrawEnv(&draw, 0, 0, canvas_w, canvas_h); draw.isbg = 1; setRGB0(&draw, 0, 0, 0); draw.dtd = 0; draw.dfe = 1;
    PutDrawEnv(&draw);
    GR_SetScissorState(0);
    return true;
  }
  void emitWorld(const ViewSetup& view, double alpha) {
    if (static const char* cl = std::getenv("SF3N_CARASLOG"); cl && !xray_) {
      static bool done = false;
      double b6[6]{};
      if (!done && std::sscanf(cl, "%lf,%lf,%lf,%lf,%lf,%lf", &b6[0], &b6[1], &b6[2], &b6[3], &b6[4], &b6[5]) == 6 && !cur().rooms.empty()) {
        done = true;
        for (const auto r : cur().rooms) {
          const auto& sc = mis().rooms[r].sections();
          for (std::size_t si = 0; si < sc.size(); ++si)
            for (std::size_t pi = 0; pi < sc[si].polygons.size(); ++pi) {
              const auto& po = sc[si].polygons[pi];
              const unsigned n = po.quad ? 4 : 3; bool in = true;
              for (unsigned k = 0; k < n && in; ++k) { const auto& v = sc[si].vertices[po.vertex_indices[k]]; in = v.x >= b6[0] && v.x <= b6[1] && v.y >= b6[2] && v.y <= b6[3] && v.z >= b6[4] && v.z <= b6[5]; }
              if (!in) continue;
              std::fprintf(log, "CARA sala %u sec %zu pol %zu tp %04x clut %04x rend %d dbl %d dup %d:", unsigned(r), si, pi, po.texture_page, po.clut, int(po.renderable), int(po.double_sided), int(po.duplicate));
              for (unsigned k = 0; k < n; ++k) { const auto& v = sc[si].vertices[po.vertex_indices[k]]; std::fprintf(log, " (%d,%d,%d uv%d,%d c%04x)", v.x, v.y, v.z, po.uv[k].u, po.uv[k].v, v.color); }
              std::fprintf(log, "\n");
            }
        }
      }
    }
    glow_eye_ = {view.eye[0], view.eye[1], view.eye[2]}; glow_eye_ok_ = true;
    if (!xray_) setVideoMode(canvas_w, canvas_h, false);
    SetGeomOffset(canvas_w / 2, canvas_h / 2);
    SetGeomScreen(view.h);
    SetRotMatrix(const_cast<MATRIX*>(&view.m));
    SetTransMatrix(const_cast<MATRIX*>(&view.m));
    ClearOTagR(reinterpret_cast<u_long*>(ot_.data()), ot_size);
    ClearOTagR(reinterpret_cast<u_long*>(ot_semi_.data()), ot_size);
    ClearOTagR(reinterpret_cast<u_long*>(ot_shadow_.data()), ot_size);
    ClearOTagR(reinterpret_cast<u_long*>(ot_room_.data()), ot_size);
    room_pass_ = true;
    shadow_prims_.clear();
    std::size_t polys = 0;
    for (const auto r : cur().rooms) polys += mis().rooms[r].polygonCount();
    for (const auto& a : cur().actors)
      if (const auto* h = std::get_if<sf::assets::HmdModel>(&mis().models[a.model].geometry)) polys += h->triangles().size();
    for (const auto& o : cur().objects) {
      const auto& g = mis().models[o.model].geometry;
      if (const auto* gm = std::get_if<sf::assets::GmdModel>(&g)) polys += gm->triangles().size();
      else if (const auto* em = std::get_if<sf::assets::EmdScene>(&g)) polys += em->polygonCount() * 2;
    }
    tris_.clear(); quads_.clear();
    tris_.reserve(polys * 4 + effects_.size() * 8 + 64);
    quads_.reserve(polys + 64);
    fx_tpages_.clear(); fx_tpages_.reserve(effects_.size() * 4 + 8 + kSnowTris + 4000);
    fx_lines_.clear(); fx_lines_.reserve(effects_.size() + 8);
    fx_g3_.clear(); fx_g3_.reserve(effects_.size() * 4 + 8 + kSnowTris + 4000 + (std::getenv("SF3N_OBJDEBUG") ? polys * 4 : 0));
    {
      std::size_t at = 0;
      for (const auto& a : cur().actors)
        if (const auto* h = std::get_if<sf::assets::HmdModel>(&mis().models[a.model].geometry)) at += h->triangles().size();
      for (const auto& o : cur().objects)
        if (const auto* g = std::get_if<sf::assets::GmdModel>(&mis().models[o.model].geometry)) at += g->triangles().size();
      if (shadow_prims_.capacity() < at * 2 + 64) shadow_prims_.reserve(at * 2 + 64);
    }
    if (twins_mission_ != &mis()) buildTwins();

    if (!xray_) {
    flash_gain_ = 1.0;

    flash_pts_.clear();
    if (static const int fz = envInt("SF3N_FOGONAZO_LUZ", 150); fz > 0)
      for (const auto& o : cur().objects) {
        const bool isFlame = flameLevel() > 0 && mis().models[o.model].name == "FLAMETRK.GMD";
        if (!(o.draw_flags & 0x10) && !isFlame) continue;
        const auto* g = std::get_if<sf::assets::GmdModel>(&mis().models[o.model].geometry);
        if (!g) continue;
        double c[3] = {0, 0, 0}; int n = 0;
        for (const auto& t : g->triangles()) if (t.flags == 0 && !t.degenerate)
          for (int k = 0; k < 3; ++k) { const auto vi = t.vertex_indices[std::size_t(k)]; if (vi < g->vertices().size()) { const auto w = o.xf.apply(g->vertices()[vi].x, g->vertices()[vi].y, g->vertices()[vi].z); c[0] += w[0]; c[1] += w[1]; c[2] += w[2]; ++n; } }
        if (n && (o.draw_flags & 0x10)) flash_seen_.push_back({c[0] / n, c[1] / n, c[2] / n, nowSeconds()});
        if (n && isFlame) noteFlame({c[0] / n, c[1] / n, c[2] / n}, o.xf, (o.draw_flags & 0x10) != 0);
        else if (n) {
          const auto* wc = weaponConfigFor(mis().models[o.model].name);
          if (static const bool hl = std::getenv("SF3N_HUMOLOG") != nullptr; hl) std::fprintf(log, "HUMO %s %s\n", mis().models[o.model].name.c_str(), (!wc || wc->humo) ? "si" : "no");
          if (!wc || wc->humo) noteMuzzle({c[0] / n, c[1] / n, c[2] / n}, o.xf);
        }
      }
    {
      const double now = nowSeconds();
      std::erase_if(flash_seen_, [&](const std::array<double, 4>& f) { return now - f[3] > 0.14 || now < f[3]; });
      for (const auto& f : flash_seen_) flash_pts_.push_back({f[0], f[1], f[2], 1.0 - (now - f[3]) / 0.14});
      {
        flash_hist_.erase(std::remove_if(flash_hist_.begin(), flash_hist_.end(), [&](double t) { return now - t > 0.5; }), flash_hist_.end());
        for (const auto& f : flash_seen_) if (flash_hist_.empty() || std::fabs(flash_hist_.back() - f[3]) > 0.03) flash_hist_.push_back(f[3]);
        const double target = 1.0;
        flash_burst_ += (target - flash_burst_) * 0.3;
      }
      if (flash_pts_.size() > 12) flash_pts_.erase(flash_pts_.begin(), flash_pts_.end() - 12);
    }

    flash_amp_ = -1.0;

    {
      double sx = 0, sy = 0, sz = 0, sw = 0; int nv = 0;
      if (static const bool on = envInt("SF3N_TASER_LUZ", 1) != 0; on && live_cache_mission_ == &mis())
        for (const auto& [key, cols] : cur().room_colors) {
          auto ci = live_cache_.find(key);
          if (ci == live_cache_.end() || ci->second.size() != cols.size() || key.first >= mis().rooms.size()) continue;
          const auto& secs = mis().rooms[key.first].sections();
          if (key.second >= secs.size() || secs[key.second].vertices.size() != cols.size()) continue;
          const auto& sv = secs[key.second].vertices;
          for (std::size_t i = 0; i < cols.size(); ++i) {
            const double db = double((cols[i] >> 16) & 0xff) - double((ci->second[i] >> 16) & 0xff);
            const double dr = double(cols[i] & 0xff) - double(ci->second[i] & 0xff);
            if (db > 24 && db > 2.5 * std::max(dr, 0.0)) { sx += sv[i].x * db; sy += sv[i].y * db; sz += sv[i].z * db; sw += db; ++nv; }
          }
        }
      const double now = nowSeconds(), dt = std::clamp(now - taser_t_, 0.0, 0.1); taser_t_ = now;
      if (nv >= 12) {
        taser_c_ = {sx / sw, sy / sw, sz / sw};
        const double amt = std::min(140.0, sw / nv);
        taser_amt_ = std::max(taser_amt_ * 0.8, amt); taser_seen_ = now;
      } else if (now - taser_seen_ > 0.12) taser_amt_ = std::max(0.0, taser_amt_ - dt * 600.0);
      if (static const bool tl = std::getenv("SF3N_TASERLOG") != nullptr; tl && (nv || taser_amt_ > 0)) std::fprintf(log, "TASER cuadro %llu vertices %d fuerza %.0f centro %.0f,%.0f,%.0f\n", (unsigned long long)render_frame_, nv, taser_amt_, taser_c_[0], taser_c_[1], taser_c_[2]);
    }
    if (!flash_pts_.empty() && live_cache_mission_ == &mis()) {
      double sum = 0, wsum = 0;
      for (const auto& [key, cols] : cur().room_colors) {
        auto ci = live_cache_.find(key);
        if (ci == live_cache_.end() || ci->second.size() != cols.size() || key.first >= mis().rooms.size()) continue;
        const auto& secs = mis().rooms[key.first].sections();
        if (key.second >= secs.size() || secs[key.second].vertices.size() != cols.size()) continue;
        const auto& sv = secs[key.second].vertices;
        for (std::size_t i = 0; i < cols.size(); ++i) {
          const double b = double((ci->second[i] & 0xff) + ((ci->second[i] >> 8) & 0xff) + ((ci->second[i] >> 16) & 0xff));
          if (b < 30) continue;
          const double a = double((cols[i] & 0xff) + ((cols[i] >> 8) & 0xff) + ((cols[i] >> 16) & 0xff));
          double tt = 0;
          for (const auto& fp : flash_pts_) { const double d = std::hypot(sv[i].x - fp[0], sv[i].y - fp[1], sv[i].z - fp[2]); if (d < kFlashR) tt = std::max(tt, (1.0 - d / kFlashR) * (1.0 - d / kFlashR)); }
          if (tt < 0.05) continue;
          sum += (a / b - 1.0) / tt * tt; wsum += tt;
        }
      }
      if (wsum > 0.5) flash_amp_ = std::clamp(sum / wsum, 0.0, 4.0);
      if (static const bool fl = std::getenv("SF3N_FOGONAZOLOG") != nullptr; fl) std::fprintf(log, "FOGONAZO fuerza medida %.2f (peso %.1f)\n", flash_amp_, wsum);
    }
    if (static const bool fl = std::getenv("SF3N_FOGONAZOLOG") != nullptr; fl) { int nf = 0; for (const auto& o : cur().objects) nf += (o.draw_flags & 0x10) ? 1 : 0; std::fprintf(log, "FOGONAZO cuadro %llu objetos %zu con bit10 %d puntos %zu efectos %zu\n", (unsigned long long)render_frame_, cur().objects.size(), nf, flash_pts_.size(), effects_.size()); }
    if (static const bool on = envInt("SF3N_FOGONAZO_GLOBAL", 0) != 0; on) {
      double sl = 0, ss = 0;
      for (const auto& [key, cols] : cur().room_colors) {
        if (key.first >= mis().rooms.size()) continue;
        const auto& secs = mis().rooms[key.first].sections();
        if (key.second >= secs.size()) continue;
        const auto& sec = secs[key.second];
        if (cols.size() != sec.vertices.size()) continue;
        for (std::size_t i = 0; i < cols.size(); i += 3) {
          const auto v = sec.vertices[i].color; const auto c = cols[i];
          ss += double(((v & 31) + ((v >> 5) & 31) + ((v >> 10) & 31)) << 3);
          sl += double((c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff));
        }
      }
      if (ss > 1000) {
        const double r = sl / ss;
        if (flash_base_ <= 0 || cur().mission.get() != flash_mission_) { flash_base_ = r; flash_mission_ = cur().mission.get(); }

        if (r < flash_base_) flash_base_ += (r - flash_base_) * 0.3; else flash_base_ += (r - flash_base_) * 0.01;
        flash_gain_ = std::clamp(r / std::max(flash_base_, 1e-3), 1.0, 3.0);
        if (flash_gain_ < 1.04) flash_gain_ = 1.0;
        if (static const bool fl = std::getenv("SF3N_FOGONAZOLOG") != nullptr; fl) std::fprintf(log, "FOGONAZO cuadro %llu r %.3f base %.3f ganancia %.2f\n", (unsigned long long)render_frame_, r, flash_base_, flash_gain_);
      }
    }
    }

    if (static const bool ll = std::getenv("SF3N_LUZSALALOG") != nullptr; ll && (render_frame_ % 10) == 0) std::fprintf(log, "LUZSALA cuadro %llu fg %.3f nv %d xa %d colores %zu\n", (unsigned long long)render_frame_, flash_gain_, int(night_vision_.load()), int(xray_actors_), cur().room_colors.size());
    if (static const bool lzOn = envInt("SF3N_LUZ_SALA", 0) != 0; lzOn && !xray_actors_ && !night_vision_.load()) {
      if (room_luz_mission_ != &mis()) { room_luz_.clear(); room_luz_mission_ = &mis(); }
      std::map<std::uint16_t, std::array<double, 6>> acc;
      const double fn = cur().fog_near > 0 ? cur().fog_near : 1e9;
      for (const auto& [key, cols] : cur().room_colors) {
        if (key.first >= mis().rooms.size()) continue;
        const auto& secs = mis().rooms[key.first].sections();
        if (key.second >= secs.size()) continue;
        const auto& sec = secs[key.second];
        if (cols.size() != sec.vertices.size()) continue;
        auto& a = acc[key.first];
        for (std::size_t i = 0; i < cols.size(); ++i) {
          const auto& v = sec.vertices[i];
          const double d = view.forward[0] * (v.x - view.eye[0]) + view.forward[1] * (v.y - view.eye[1]) + view.forward[2] * (v.z - view.eye[2]);
          if (d <= 0 || d >= fn) continue;
          for (int ch = 0; ch < 3; ++ch) { a[ch] += double((cols[i] >> (8 * ch)) & 0xff); a[3 + ch] += double(((v.color >> (5 * ch)) & 31) << 3); }
        }
      }
      for (const auto& [rr, a] : acc) {
        if (static const bool ll = std::getenv("SF3N_LUZSALALOG") != nullptr; ll && (render_frame_ % 10) == 0) std::fprintf(log, "LUZSALA suma sala %u vivo %.0f disco %.0f niebla %d\n", unsigned(rr), a[0] + a[1] + a[2], a[3] + a[4] + a[5], int(cur().fog_near));
        if (a[3] + a[4] + a[5] < 3000) continue;
        auto& g = room_luz_[rr];
        for (int ch = 0; ch < 3; ++ch) g[ch] = std::clamp(a[ch] / std::max(a[3 + ch], 1.0), 0.0, 4.0);
        if (static const bool ll = std::getenv("SF3N_LUZSALALOG") != nullptr; ll && (render_frame_ % 10) == 0) std::fprintf(log, "LUZSALA sala %u %.3f %.3f %.3f\n", unsigned(rr), g[0], g[1], g[2]);
      }
    }

    std::unordered_map<std::uint64_t, std::uint32_t> livePosCol;
    static const bool bordes = envInt("SF3N_BORDES_VIVOS", 1) != 0;
    if (bordes && !xray_actors_) {
      for (const auto& [key, cols] : cur().room_colors) {
        if (key.first >= mis().rooms.size()) continue;
        const auto& secs = mis().rooms[key.first].sections();
        if (key.second >= secs.size() || secs[key.second].vertices.size() != cols.size()) continue;
        const auto& vs = secs[key.second].vertices;
        for (std::size_t i = 0; i < vs.size(); ++i) livePosCol.emplace(vkey(vs[i].x, vs[i].y, vs[i].z), cols[i]);
      }
    }
    static const double smoothR = std::clamp(double(xrayCfg("luz_mezcla", "SF3N_LUZ_SUAVE", 0)), 0.0, 2000.0);
    if (smoothR > 0 && smooth_mission_ != &mis()) buildSmooth(smoothR);
    if (!xray_actors_) for (const auto r : cur().rooms) {
      if (static const bool rl = std::getenv("SF3N_ROOMLIST") != nullptr; rl && log && (render_frame_ % 50) == 0) { std::fprintf(log, "ROOMLIST %u secs %zu live %d\n", unsigned(r), mis().rooms[r].sections().size(), int(std::count_if(cur().room_colors.begin(), cur().room_colors.end(), [&](const auto& kv) { return kv.first.first == r; }))); }
      const auto& scene = mis().rooms[r];
      const unsigned bank = std::min<unsigned>(scene.textureBank(), 1u);
      for (std::size_t si = 0; si < scene.sections().size(); ++si) {
        const auto& sec = scene.sections()[si];

        if (xray_cull_ && !cur().room_positions.count({r, std::uint16_t(si)}) && sectionOutsideRect(sec.bounds, view, xray_cull_rc_)) continue;
        const std::vector<std::uint32_t>* live = nullptr;
        if (auto it = cur().room_colors.find({r, std::uint16_t(si)}); it != cur().room_colors.end() && it->second.size() == sec.vertices.size()) live = &it->second;

        const std::vector<std::uint32_t>* lastLive = nullptr;
        const std::vector<std::uint32_t>* liveBefore = nullptr;
        if (static const bool keep = envInt("SF3N_COLOR_RECORDADO", 1) != 0; keep) {
          if (live_cache_mission_ != &mis()) { live_cache_.clear(); live_cache_mission_ = &mis(); }
          const auto key = std::make_pair(std::uint16_t(r), std::uint16_t(si));
          if (live && !flash_pts_.empty()) if (static const bool fl = std::getenv("SF3N_FOGONAZOLOG") != nullptr; fl) {
            if (auto ci = live_cache_.find(key); ci != live_cache_.end() && ci->second.size() == live->size()) {
              double a = 0, b = 0, dmin = 1e9;
              for (std::size_t i = 0; i < live->size(); ++i) { a += ((*live)[i] & 0xff) + (((*live)[i] >> 8) & 0xff); b += (ci->second[i] & 0xff) + ((ci->second[i] >> 8) & 0xff);
                for (const auto& fp : flash_pts_) dmin = std::min(dmin, std::hypot(sec.vertices[i].x - fp[0], sec.vertices[i].y - fp[1], sec.vertices[i].z - fp[2])); }
              if (dmin < 900) std::fprintf(log, "FOGONAZO viva sala %u sec %zu dist %.0f ahora/antes %.2f\n", unsigned(r), si, dmin, b > 0 ? a / b : 0.0);
            } }
          if (live) { if (flash_pts_.empty() && taser_amt_ <= 0.5) live_cache_[key] = *live; else if (auto ci = live_cache_.find(key); ci != live_cache_.end() && ci->second.size() == live->size()) liveBefore = &ci->second; }

          else if (static const bool viejo = envInt("SF3N_COLOR_RECORDADO_VIEJO", 0) != 0; viejo || xray_world_ || xray_)
            if (auto ci = live_cache_.find(key); ci != live_cache_.end() && ci->second.size() == sec.vertices.size()) lastLive = &ci->second;
        }
        const std::vector<std::vector<PolyTwin>>* twinsSec = (!live && r < twins_.size() && si < twins_[r].size()) ? &twins_[r][si] : nullptr;
        const std::vector<std::uint16_t>* ramCols = nullptr;
        if (auto it = cur().room_ram_colors.find({r, std::uint16_t(si)}); it != cur().room_ram_colors.end() && it->second.size() == sec.vertices.size()) ramCols = &it->second;
        auto baseCol = [&](std::size_t i) { return ramCols ? (*ramCols)[i] : sec.vertices[i].color; };
        std::vector<std::array<float, 4>> fixCol;
        if (!live && !lastLive && !livePosCol.empty()) {
          double ra[3] = {0, 0, 0}, rb[3] = {0, 0, 0};
          std::vector<std::int32_t> hit(sec.vertices.size(), -1);
          for (std::size_t i = 0; i < sec.vertices.size(); ++i) {
            const auto& v = sec.vertices[i];
            if (auto it = livePosCol.find(vkey(v.x, v.y, v.z)); it != livePosCol.end()) {
              hit[i] = std::int32_t(it->second & 0xffffff);
              for (int ch = 0; ch < 3; ++ch) { ra[ch] += double((it->second >> (8 * ch)) & 0xff); rb[ch] += double(((baseCol(i) >> (5 * ch)) & 31) << 3); }
            }
          }
          if (rb[0] + rb[1] + rb[2] > 0) {
            const double kRatio = std::clamp((ra[0] + ra[1] + ra[2]) / (rb[0] + rb[1] + rb[2]), 0.75, 1.25);
            fixCol.resize(sec.vertices.size());
            for (std::size_t i = 0; i < sec.vertices.size(); ++i) {
              auto& fc = fixCol[i];
              if (hit[i] >= 0) { for (int ch = 0; ch < 3; ++ch) fc[std::size_t(ch)] = float((hit[i] >> (8 * ch)) & 0xff); fc[3] = 1.f; }
              else { for (int ch = 0; ch < 3; ++ch) fc[std::size_t(ch)] = float(double(((baseCol(i) >> (5 * ch)) & 31) << 3) * kRatio); fc[3] = 0.f; }
            }
          }
        }
        const std::vector<std::array<std::int16_t, 3>>* livePos = nullptr;
        if (auto it = cur().room_positions.find({r, std::uint16_t(si)}); it != cur().room_positions.end() && it->second.size() == sec.vertices.size()) livePos = &it->second;
        auto VX = [&](std::size_t i) { return livePos ? (*livePos)[i][0] : sec.vertices[i].x; };
        auto VY = [&](std::size_t i) { return livePos ? (*livePos)[i][1] : sec.vertices[i].y; };
        auto VZ = [&](std::size_t i) { return livePos ? (*livePos)[i][2] : sec.vertices[i].z; };

        auto b = sec.bounds;
        if (livePos && !livePos->empty()) {
          b.minimum_x = b.maximum_x = (*livePos)[0][0]; b.minimum_y = b.maximum_y = (*livePos)[0][1]; b.minimum_z = b.maximum_z = (*livePos)[0][2];
          for (const auto& p : *livePos) {
            b.minimum_x = std::min<decltype(b.minimum_x)>(b.minimum_x, p[0]); b.maximum_x = std::max<decltype(b.maximum_x)>(b.maximum_x, p[0]);
            b.minimum_y = std::min<decltype(b.minimum_y)>(b.minimum_y, p[1]); b.maximum_y = std::max<decltype(b.maximum_y)>(b.maximum_y, p[1]);
            b.minimum_z = std::min<decltype(b.minimum_z)>(b.minimum_z, p[2]); b.maximum_z = std::max<decltype(b.maximum_z)>(b.maximum_z, p[2]);
          }
        }
        bool anyFront = false;
        for (int c = 0; c < 8 && !anyFront; ++c) {
          const double px = (c & 1) ? b.maximum_x : b.minimum_x, py = (c & 2) ? b.maximum_y : b.minimum_y, pz = (c & 4) ? b.maximum_z : b.minimum_z;
          const double d = view.forward[0] * (px - view.eye[0]) + view.forward[1] * (py - view.eye[1]) + view.forward[2] * (pz - view.eye[2]);
          anyFront = d > near_z;
        }
        if (!anyFront) continue;
        double roomBias = 0.0;
        if (static const int ob = envInt("SF3N_ORDEN_SALAS", 0); ob != 0) {

          const auto& rl = cur().rooms;
          const auto it = std::find(rl.begin(), rl.end(), r);
          const double rank = it == rl.end() ? double(rl.size()) : double(it - rl.begin());
          roomBias = (ob > 0 ? double(rl.size()) - rank : rank) * 0.002;
        }
        if (static const int dbgSec = envInt("SF3N_SECDBG", 0); dbgSec == 1 && !live) continue;

        if (static const char* hr = std::getenv("SF3N_HIDEROOM"); hr) {

          bool skip = false;
          for (const char* q = hr; q && *q; ) {
            int hrr = -2, hrs = -1; std::sscanf(q, "%d:%d", &hrr, &hrs);
            if ((hrr == -1 || hrr == int(r)) && (hrs < 0 || hrs == int(si))) { skip = true; break; }
            q = std::strchr(q, ','); if (q) ++q;
          }
          if (skip) continue;
        }
        depth_.resize(sec.vertices.size());
        for (std::size_t i = 0; i < sec.vertices.size(); ++i) {
          depth_[i] = view.forward[0] * (VX(i) - view.eye[0]) + view.forward[1] * (VY(i) - view.eye[1]) + view.forward[2] * (VZ(i) - view.eye[2]);
        }

        static const bool luzReal = xrayCfg("luz_juego", "SF3N_LUZ_JUEGO", 1) != 0;
        const std::vector<std::array<float, 3>>* ratioV = nullptr;
        if (luzReal && !xray_actors_ && !xray_ && !xray_world_) {
          if (light_ratio_mission_ != &mis()) { light_ratio_.clear(); light_ratio_sig_.clear(); light_ratio_mission_ = &mis(); }
          auto& rv = light_ratio_[{std::uint16_t(r), std::uint16_t(si)}];

          std::uint64_t sig = 1469598103934665603ull;
          if (ramCols) for (auto c : *ramCols) sig = (sig ^ c) * 1099511628211ull;
          auto& rsig = light_ratio_sig_[{std::uint16_t(r), std::uint16_t(si)}];
          if (!live && rsig != sig) rv.clear();
          if (live && flash_pts_.empty() && taser_amt_ <= 0.5) {
            rsig = sig;
            if (rv.size() != sec.vertices.size()) rv.assign(sec.vertices.size(), {-1.f, -1.f, -1.f});
            const bool fogOn = cur().fog_far > cur().fog_near && cur().fog_near > 0;
            for (std::size_t i = 0; i < sec.vertices.size(); ++i) {
              double f = 1.0;
              if (fogOn) f = std::clamp((cur().fog_far + fog_shift_ - depth_[i]) / (cur().fog_far - cur().fog_near), 0.0, 1.0);
              if (f < 0.35) continue;
              for (int ch = 0; ch < 3; ++ch) {
                const double dv = double(((sec.vertices[i].color >> (5 * ch)) & 31) << 3);
                const double lv = double(((*live)[i] >> (8 * ch)) & 0xff);
                rv[i][std::size_t(ch)] = float(dv >= 16.0 ? std::clamp(lv / (dv * f), 0.0, 3.0) : -(std::clamp(lv / f, 0.0, 255.0) + 2.0));
              }
            }
          }
          if (rv.size() == sec.vertices.size()) ratioV = &rv;
        }
        for (const auto& poly : sec.polygons) {
          if (!poly.renderable || poly.degenerate || poly.duplicate) continue;

          if (twinsSec) {
            const auto pi = std::size_t(&poly - sec.polygons.data());
            bool other = false;
            if (pi < twinsSec->size())
              for (const auto& tw : (*twinsSec)[pi]) if (cur().room_colors.count({tw.r, tw.s})) { other = true; break; }
            if (other && std::getenv("SF3N_GEMELOS") == nullptr) continue;
          }
          const unsigned count = poly.quad ? 4u : 3u;
          double doff[3] = {0, 0, 0};
          if (r < decal_n_.size() && si < decal_n_[r].size()) {
            const auto& dn = decal_n_[r][si][std::size_t(&poly - sec.polygons.data())];
            if ((dn[0] != 0.f || dn[1] != 0.f || dn[2] != 0.f) && std::getenv("SF3N_SIN_CALCO") == nullptr) {
              const auto v0 = poly.vertex_indices[0];
              const double toEye = dn[0] * (view.eye[0] - VX(v0)) + dn[1] * (view.eye[1] - VY(v0)) + dn[2] * (view.eye[2] - VZ(v0));
              const double k = toEye >= 0 ? 2.0 : -2.0;
              doff[0] = dn[0] * k; doff[1] = dn[1] * k; doff[2] = dn[2] * k;
            }
          }
          const float buriedGap = (r < buried_.size() && si < buried_[r].size()) ? buried_[r][si][std::size_t(&poly - sec.polygons.data())] : 0.f;

          auto pullV = [&](double& x, double& y, double& z) {
            if (roomBias != 0.0) {
              const double ex = view.eye[0] - x, ey = view.eye[1] - y, ez = view.eye[2] - z, l = std::sqrt(ex * ex + ey * ey + ez * ez);
              if (l > 1.0) { const double pl = std::min(l * roomBias, l * 0.5); x += ex / l * pl; y += ey / l * pl; z += ez / l * pl; }
            }
            if (buriedGap <= 0.f) return;
            const double ex = view.eye[0] - x, ey = view.eye[1] - y, ez = view.eye[2] - z, l = std::sqrt(ex * ex + ey * ey + ez * ez);
            if (l < 1.0) return;
            const double pull = std::min({double(buriedGap) / std::max(std::fabs(ey) / l, 0.2) + 3.0, 120.0, l * 0.5});
            x += ex / l * pull; y += ey / l * pull; z += ez / l * pull;
          };
          if (static const int pd = std::getenv("SF3N_POLYDBG") ? int(std::strtol(std::getenv("SF3N_POLYDBG"), nullptr, 16)) : -1; pd >= 0 && poly.texture_page == pd && (render_frame_ % 60) == 0) {
            std::fprintf(log, "POLYDBG cuadro %llu sala %u sec %zu poli %zu vivo %d clut %04x uv", (unsigned long long)render_frame_, unsigned(r), si, std::size_t(&poly - sec.polygons.data()), int(live != nullptr), poly.clut);
            for (unsigned c = 0; c < count; ++c) std::fprintf(log, " (%u,%u)", poly.uv[c].u, poly.uv[c].v);
            std::fprintf(log, " v");
            for (unsigned c = 0; c < count; ++c) { const auto vi = poly.vertex_indices[c]; std::fprintf(log, " (%d,%d,%d)", int(VX(vi)), int(VY(vi)), int(VZ(vi))); }
            std::fprintf(log, "\n");
          }
          unsigned behind = 0;
          for (unsigned c = 0; c < count; ++c) if (depth_[poly.vertex_indices[c]] < near_z) ++behind;
          if (behind == count) continue;
          if (static const double nc = std::getenv("SF3N_NEARCULL") ? std::atof(std::getenv("SF3N_NEARCULL")) : 0.0; nc > 0) {
            bool nearV = false; for (unsigned c = 0; c < count; ++c) nearV |= depth_[poly.vertex_indices[c]] < nc;
            if (nearV) continue;
          }
          const std::array<std::array<float, 3>, 4>* sm = nullptr;
          if (smoothR > 0 && r < smooth_.size() && si < smooth_[r].size()) { const auto pi = std::size_t(&poly - sec.polygons.data()); if (pi < smooth_[r][si].size()) sm = &smooth_[r][si][pi]; }
          auto colRaw = [&](unsigned c, int ch) -> double {

            if (static const bool rt = std::getenv("SF3N_ROOMTINT") != nullptr; rt) return ch == 0 ? double((r * 4) & 0xff) : ch == 1 ? double((si * 4) & 0xff) : 64.0;

            auto taserAdd = [&] {
              if (taser_amt_ <= 0.5) return 0.0;
              static const double R = 1100.0, tint[3] = {0.04, 0.10, 1.0};
              const auto vi = poly.vertex_indices[c];
              const double d = std::hypot(double(VX(vi)) - taser_c_[0], double(VY(vi)) - taser_c_[1], double(VZ(vi)) - taser_c_[2]);
              if (d >= R) return 0.0;
              const double t = 1.0 - d / R;
              const double flick = 0.85 + 0.15 * std::sin(nowSeconds() * 47.0);
              static const double ta = envInt("SF3N_TASER_FUERZA", 120) / 100.0;
              return taser_amt_ * ta * flick * tint[ch] * t * t;
            };
            auto flashGain = [&] {
              double g = 1.0;
              if (flash_pts_.empty()) return g;
              static const double amp = envInt("SF3N_FOGONAZO_LUZ", 220) / 100.0, R = kFlashR;
              static const double tint[3] = {1.0, 0.9, 0.7};
              const auto vi = poly.vertex_indices[c];

              double best = 0;
              for (const auto& fp : flash_pts_) {
                const double d = std::hypot(double(VX(vi)) - fp[0], double(VY(vi)) - fp[1], double(VZ(vi)) - fp[2]);
                if (d < R) { const double t = 1.0 - d / R; best = std::max(best, fp[3] * t * t); }
              }
              return g + amp * flash_burst_ * tint[ch] * best;
            };

            if (live && !sm) {
              if (!noFogTest() && cur().fog_far > 0 && depth_[poly.vertex_indices[c]] >= cur().fog_far + fog_shift_) return 0.0;
              const std::vector<std::uint32_t>* base = live;
              if ((!flash_pts_.empty() || taser_amt_ > 0.5) && liveBefore) base = liveBefore;
              if (sm) {
                const double dv = double(((sec.vertices[poly.vertex_indices[c]].color >> (5 * ch)) & 31) << 3);
                const double lv = double(((*base)[poly.vertex_indices[c]] >> (8 * ch)) & 0xff);
                const double S = double((*sm)[c][std::size_t(ch)]);
                const double val = dv >= 24.0 ? S * std::clamp(lv / dv, 0.0, 3.0) : lv + (S - dv) * std::clamp(lv / std::max(dv, 8.0), 0.0, 1.0);
                return std::min(255.0, std::max(0.0, val) * flashGain() + taserAdd());
              }
              {
                static const bool nv = std::getenv("SF3N_NIEBLA_VIVA") != nullptr;
                double fv = 1.0;
                if (nv && cur().fog_far > cur().fog_near && cur().fog_near > 0) fv = std::clamp((cur().fog_far + fog_shift_ - depth_[poly.vertex_indices[c]]) / (cur().fog_far - cur().fog_near), 0.0, 1.0);
                return std::min(255.0, double(((*base)[poly.vertex_indices[c]] >> (8 * ch)) & 0xff) * fv * flashGain() + taserAdd());
              }
            }
            if (lastLive) return cur().fog_far > 0 && depth_[poly.vertex_indices[c]] >= cur().fog_far + fog_shift_ ? 0.0 : std::min(255.0, double(((*lastLive)[poly.vertex_indices[c]] >> (8 * ch)) & 0xff) * flashGain() + taserAdd());
            const auto v = baseCol(poly.vertex_indices[c]);

            double f = 1.0;
            static const bool nvTest = std::getenv("SF3N_PRUEBA_NOVIVA_SIN_NIEBLA") != nullptr;
            if (!noFogTest() && !nvTest && cur().fog_far > cur().fog_near && cur().fog_near > 0)
              f = std::clamp((cur().fog_far + fog_shift_ - depth_[poly.vertex_indices[c]]) / (cur().fog_far - cur().fog_near), 0.0, 1.0);
            const double g = flash_gain_ * flashGain();
            if (sm) {
              double S = double((*sm)[c][std::size_t(ch)]);
              const float rr = ratioV ? (*ratioV)[poly.vertex_indices[c]][std::size_t(ch)] : -1.f;
              if (rr >= 0.f) S *= double(rr);
              else if (rr < -1.5f) S = double(-rr - 2.0f);
              else if (ramCols) {
                const double dv = double(((sec.vertices[poly.vertex_indices[c]].color >> (5 * ch)) & 31) << 3);
                const double rv = double((((*ramCols)[poly.vertex_indices[c]]) >> (5 * ch) & 31) << 3);
                S = dv >= 24.0 ? S * std::clamp(rv / dv, 0.0, 3.0) : std::max(0.0, S + (rv - dv));
              }
              return std::min(255.0, S * f * g + taserAdd() * f);
            }
            if (static const bool rec = std::getenv("SF3N_LUZ_RECORDADA") != nullptr; rec && ratioV) {
              const float rr = (*ratioV)[poly.vertex_indices[c]][std::size_t(ch)];
              const double dv = double(((sec.vertices[poly.vertex_indices[c]].color >> (5 * ch)) & 31) << 3);
              if (rr >= 0.f) return std::min(255.0, dv * double(rr) * f * g + taserAdd() * f);
              if (rr < -1.5f) return std::min(255.0, double(-rr - 2.0f) * f * g + taserAdd() * f);
            }
            if (!fixCol.empty()) {
              const auto& fc = fixCol[poly.vertex_indices[c]];

              static const bool sinCorte = std::getenv("SF3N_BORDES_SIN_CORTE") != nullptr;
              const bool beyond = !sinCorte && !noFogTest() && cur().fog_far > 0 && depth_[poly.vertex_indices[c]] >= cur().fog_far + fog_shift_;
              const double ff = fc[3] > 0.5f ? (beyond ? 0.0 : 1.0) : f;
              return std::min(255.0, double(fc[std::size_t(ch)]) * ff * g + taserAdd() * ff);
            }
            double lz = 1.0;
            if (static const bool on = envInt("SF3N_LUZ_SALA", 0) != 0; on) {
              if (auto it = room_luz_.find(std::uint16_t(r)); it != room_luz_.end()) lz = it->second[std::size_t(ch)];
              else if (!room_luz_.empty()) { double sum = 0; for (const auto& kv : room_luz_) sum += kv.second[std::size_t(ch)]; lz = sum / double(room_luz_.size()); }
            }
            return std::min(255.0, double(((v >> (5 * ch)) & 31) << 3) * lz * f * g + taserAdd() * f);
          };
          auto col = [&](unsigned c, int ch) { return (nv_flat_ && nv_tint_ && ch != 1) ? 0.0 : colRaw(c, ch) * world_gain_; };

          const bool semiPoly = !(poly.texture_page & 0x80);
          if (static const bool sd = std::getenv("SF3N_SEMIDBG") != nullptr; sd && (behind > 0 || semiPoly)) {
            double dmin = 1e9, dmax = -1e9;
            for (unsigned c = 0; c < count; ++c) { dmin = std::min(dmin, depth_[poly.vertex_indices[c]]); dmax = std::max(dmax, depth_[poly.vertex_indices[c]]); }
            if (dmin < 300) std::fprintf(log, "SEMIDBG cuadro %llu campo %llu sala %u sec %zu poli %zu semi %d detras %u prof %.0f..%.0f tp %04x vivo %d\n", (unsigned long long)render_frame_, (unsigned long long)cur().field, unsigned(r), si, std::size_t(&poly - sec.polygons.data()), int(semiPoly), behind, dmin, dmax, unsigned(poly.texture_page), int(live != nullptr));
          }
          if (behind > 0 || semiPoly) {
            auto mk = [&](unsigned c) {
              const std::size_t vi = poly.vertex_indices[c];
              double x = double(VX(vi)) + doff[0], y = double(VY(vi)) + doff[1], z = double(VZ(vi)) + doff[2]; pullV(x, y, z);
              return CV{x, y, z, double(poly.uv[c].u), double(poly.uv[c].v), col(c, 0), col(c, 1), col(c, 2), depth_[poly.vertex_indices[c]]};
            };
            const auto tp = aliasTpage(std::uint16_t(poly.texture_page | 0x80), bank);
            const bool two = twoSidedRoom(poly, semiPoly);
            emitClippedTriangle({mk(0), mk(1), mk(2)}, tp, poly.clut, semiPoly, !two);
            if (poly.quad) emitClippedTriangle({mk(1), mk(3), mk(2)}, tp, poly.clut, semiPoly, !two);
            continue;
          }
          SVECTOR sv[4];
          for (unsigned c = 0; c < count; ++c) {
            const std::size_t vi = poly.vertex_indices[c];
            double x = double(VX(vi)) + doff[0], y = double(VY(vi)) + doff[1], z = double(VZ(vi)) + doff[2]; pullV(x, y, z);
            sv[c] = SVECTOR{short(std::lround(x)), short(std::lround(y)), short(std::lround(z)), 0};
          }
          long xy[4]{}, dq{}, fl{};
          if (poly.quad) {
            const int depth = RotTransPers4(&sv[0], &sv[1], &sv[2], &sv[3], &xy[0], &xy[1], &xy[2], &xy[3], &dq, &fl);
            int half = 3;
            if (!twoSidedRoom(poly, false)) {
              half = (frontFacingIdx(4, xy, 0, 1, 2) ? 1 : 0) | (frontFacingIdx(4, xy, 1, 3, 2) ? 2 : 0);
              if (half == 0) continue;
            }
            if (half != 3) {

              if (tris_.size() >= tris_.capacity()) { noteOverflow("triangulos"); continue; }
              const unsigned a = half == 1 ? 0u : 1u, b = half == 1 ? 1u : 3u, c = 2u;
              auto& p = tris_.emplace_back();
              setPolyGT3(&p);
              setRGB0(&p, u_char(col(a, 0)), u_char(col(a, 1)), u_char(col(a, 2)));
              setRGB1(&p, u_char(col(b, 0)), u_char(col(b, 1)), u_char(col(b, 2)));
              setRGB2(&p, u_char(col(c, 0)), u_char(col(c, 1)), u_char(col(c, 2)));
              setUV3(&p, poly.uv[a].u, poly.uv[a].v, poly.uv[b].u, poly.uv[b].v, poly.uv[c].u, poly.uv[c].v);
              p.clut = poly.clut;
              p.tpage = aliasTpage(poly.texture_page, bank);
              setProjected(&p.x0, xy[a]); setProjected(&p.x1, xy[b]); setProjected(&p.x2, xy[c]);
              addPrim(&ot_room_[std::clamp(depth, 1, ot_size - 1)], &p);
              continue;
            }
            if (quads_.size() >= quads_.capacity()) { noteOverflow("cuadros"); continue; }
            auto& p = quads_.emplace_back();
            setPolyGT4(&p);
            setRGB0(&p, u_char(col(0, 0)), u_char(col(0, 1)), u_char(col(0, 2)));
            setRGB1(&p, u_char(col(1, 0)), u_char(col(1, 1)), u_char(col(1, 2)));
            setRGB2(&p, u_char(col(2, 0)), u_char(col(2, 1)), u_char(col(2, 2)));
            setRGB3(&p, u_char(col(3, 0)), u_char(col(3, 1)), u_char(col(3, 2)));
            setUV4(&p, poly.uv[0].u, poly.uv[0].v, poly.uv[1].u, poly.uv[1].v, poly.uv[2].u, poly.uv[2].v, poly.uv[3].u, poly.uv[3].v);
            p.clut = poly.clut;
            p.tpage = aliasTpage(poly.texture_page, bank);
            setProjected(&p.x0, xy[0]); setProjected(&p.x1, xy[1]);
            setProjected(&p.x2, xy[2]); setProjected(&p.x3, xy[3]);
            addPrim(&ot_room_[std::clamp(depth, 1, ot_size - 1)], &p);
          } else {
            const int depth = RotTransPers3(&sv[0], &sv[1], &sv[2], &xy[0], &xy[1], &xy[2], &dq, &fl);
            if (!twoSidedRoom(poly, false) && !frontFacing(3, xy[0], xy[1], xy[2])) continue;
            if (tris_.size() >= tris_.capacity()) { noteOverflow("triangulos"); continue; }
            auto& p = tris_.emplace_back();
            setPolyGT3(&p);
            setRGB0(&p, u_char(col(0, 0)), u_char(col(0, 1)), u_char(col(0, 2)));
            setRGB1(&p, u_char(col(1, 0)), u_char(col(1, 1)), u_char(col(1, 2)));
            setRGB2(&p, u_char(col(2, 0)), u_char(col(2, 1)), u_char(col(2, 2)));
            setUV3(&p, poly.uv[0].u, poly.uv[0].v, poly.uv[1].u, poly.uv[1].v, poly.uv[2].u, poly.uv[2].v);
            p.clut = poly.clut;
            p.tpage = aliasTpage(poly.texture_page, bank);
            setProjected(&p.x0, xy[0]); setProjected(&p.x1, xy[1]); setProjected(&p.x2, xy[2]);
            addPrim(&ot_room_[std::clamp(depth, 1, ot_size - 1)], &p);
          }
        }
      }
    }
    room_pass_ = false;
    drawModels(alpha, view);
    if (!xray_) {
      tickFxClock();
      if (nowSeconds() - effects_time_ < 0.25) drawEffects(view);
      drawSnow(view);
      drawBreath(view);
      drawSmoke(view);
      drawFlames(view);
    }
    last_pgxp_ = PGXP_GetIndex(0);
    if (static const bool pl = std::getenv("SF3N_PGXPLOG") != nullptr; pl) {
      static unsigned mx = 0; const unsigned ix = PGXP_GetIndex(0);
      if (ix > mx || ix == 0xffffu) { mx = ix; std::fprintf(log, "PGXP indice %u (cuadro %llu)\n", ix, (unsigned long long)render_frame_); }
    }
    GR_SetBlendMode(BM_NONE);
    GR_EnableDepth(1);
    GR_SetDepthState(1, 1);

    {
      static const float pf = float(envInt("SF3N_SALA_ATRAS", 0)) / 100.0f;
      GR_SetPolygonOffset(1.0F * pf, 32.0F * pf);
      DrawOTag(reinterpret_cast<u_long*>(&ot_room_.back()));
      GR_SetPolygonOffset(0.0F, 0.0F);
    }
    DrawOTag(reinterpret_cast<u_long*>(&ot_.back()));
    if (!shadow_prims_.empty()) {

      DR_TPAGE sp{};
      static const bool dbg = std::getenv("SF3N_SHADOWDEBUG") != nullptr;
      SetDrawTPage(&sp, 1, 0, GetTPage(0, dbg ? 1 : 2, 0, 0));
      DrawPrim(&sp);
      GR_SetBlendMode(BM_SUBTRACT);
      GR_SetDepthState(1, 0);
      GR_SetPolygonOffset(0.0F, -1.0F);
      GR_BeginShadowMask();
      DrawOTag(reinterpret_cast<u_long*>(&ot_shadow_.back()));
      GR_EndShadowMask();
      GR_SetPolygonOffset(0.0F, 0.0F);
      GR_SetBlendMode(BM_NONE);
      GR_SetDepthState(1, 1);
    }

    GR_SetDepthState(1, 0);
    DrawOTag(reinterpret_cast<u_long*>(&ot_semi_.back()));
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
  }

  struct HudMap {
    double ox = 0, oy = 0;
    double ax = 0, ay = 0;
    double dw = 368;

    double gsc = 1, gax = 0, gay = 0, gsx = 0;
    float bx(double gx) const {
      if (dw == 368 || dw <= 0) return float(192.0 + (gx + ox - ax - 192.0) * (384.0 / 368.0));
      return float(192.0 + (gx + ox - ax - dw * 0.5) * (384.0 / dw));
    }
    float x(double gx) const { return float(gax + (bx(gx) - gax) * gsc + gsx); }

    static inline double center_y = 120.0;
    float y(double gy) const { return float(gay + (gy + oy - ay - center_y + 120.0 - gay) * gsc); }
    float w(double gw) const { return float(gw * (384.0 / (dw > 0 ? dw : 368.0)) * gsc); }
    float h(double gh) const { return float(gh * gsc); }

    double cx(double gx) const { return gx + ox - ax - ((dw == 368 || dw <= 0) ? 192.0 : dw * 0.5); }
    double cy(double gy) const { return gy + oy - ay - center_y; }

    float xe(double gx, bool extend) const {
      if (extend) {
        const double px = gx + ox - ax;
        if (px <= 10.0) return -2000.f;
        if (px >= (dw > 0 ? dw : 368.0) + 6.0) return 2400.f;
      }
      return x(gx);
    }
  };

  static void e3Area(HudMap& m, std::uint32_t w) {
    const double x = double(w & 0x3ff), y = double((w >> 10) & 0x3ff);
    if (x != 0) { m.ax = 0; m.ay = 0; } else { m.ax = x; m.ay = y; }
  }
  static bool blackish(std::uint32_t c) { return (c & 0xff) < 0x18 && ((c >> 8) & 0xff) < 0x18 && ((c >> 16) & 0xff) < 0x18; }
  static double sx11(std::uint32_t xy) { return double(std::int32_t(xy << 21) >> 21); }
  static double sy11(std::uint32_t xy) { return double(std::int32_t((xy >> 16) << 21) >> 21); }

 public:
  void loadGameFont(const std::vector<std::byte>& exe, bool usa) {
    const std::size_t d = usa ? 0x198 : 0;
    const std::size_t offA = 1478008 + d, offB = 1480600 + d, offT = 1483192 + d, offC = 1446780 + d;
    if (exe.size() < offT + 42 * 123) return;
    auto b = [&](std::size_t o) { return std::uint8_t(exe[o]); };
    std::array<std::uint16_t, 256> clut{};
    for (std::size_t i = 0; i < 256; ++i) clut[i] = std::uint16_t(b(offC + 2 * i) | (b(offC + 2 * i + 1) << 8));
    if (clut[0xff] != 0 || clut[0x2b] == 0) { std::fprintf(log, "fuente del juego: no encontrada\n"); return; }
    font_page_.assign(64 * 256, 0);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 32; ++x) {
        font_page_[std::size_t(y * 64 + x)] = clut[b(offA + std::size_t(y * 32 + x))];
        font_page_[std::size_t(y * 64 + 32 + x)] = clut[b(offB + std::size_t(y * 32 + x))];
      }
    for (int y = 0; y < 123; ++y)
      for (int x = 0; x < 42; ++x) font_page_[std::size_t((64 + y) * 64 + x)] = clut[b(offT + std::size_t(y * 42 + x))];

    for (auto& c : font_page_) if (c) c = std::uint16_t((c & 0x7fff) ? (c & 0x7fff) : 0x0421);
    font_dirty_ = true;
    std::fprintf(log, "fuente del juego: cargada\n");
  }
  void loadGameFontFromCue(const std::string& cue) {
    try {
      auto img = sf::disc::Iso9660Image::open(cue);
      const std::string id = discIdOf(cue);
      auto exe = img.readFile(id);
      if (g_idioma_actual && *g_idioma_actual == "ru") (void)rufont::patchExe(dataDir() + "textos_ru.pak", exe);
      loadGameFont(exe, id.rfind("SLUS", 0) == 0 || id.rfind("SCUS", 0) == 0);
    } catch (const std::exception&) {}
  }
  bool gameFontReady() const { return !font_page_.empty(); }

 private:
  std::vector<std::uint16_t> font_page_;
  bool font_dirty_ = false;
  static constexpr int kFontAlias = 62;
  struct GameGlyph { int u, v; };

  static bool gameGlyph(std::uint32_t cp, bool capital, GameGlyph& g) {
    static const std::map<char, GameGlyph> caps = {
        {'A', {56, 16}}, {'B', {32, 24}}, {'C', {40, 24}}, {'D', {48, 24}}, {'E', {56, 24}}, {'F', {32, 32}}, {'G', {40, 32}},
        {'H', {48, 32}}, {'I', {56, 32}}, {'J', {32, 40}}, {'K', {40, 40}}, {'L', {48, 40}}, {'M', {56, 40}}, {'N', {32, 48}},
        {'O', {40, 48}}, {'P', {48, 48}}, {'Q', {56, 48}}, {'R', {32, 56}}, {'S', {40, 56}}, {'T', {48, 56}}, {'U', {56, 56}},
        {'V', {64, 0}}, {'W', {72, 0}}, {'X', {88, 0}}, {'Y', {96, 0}}, {'Z', {64, 8}}};
    static const std::map<char, GameGlyph> small = {
        {'a', {8, 32}}, {'b', {16, 32}}, {'c', {24, 32}}, {'d', {0, 40}}, {'e', {8, 40}}, {'f', {16, 40}}, {'g', {24, 40}},
        {'h', {0, 48}}, {'i', {8, 48}}, {'j', {16, 48}}, {'k', {24, 48}}, {'l', {0, 56}}, {'m', {8, 56}}, {'n', {16, 56}},
        {'o', {24, 56}}, {'p', {32, 0}}, {'q', {40, 0}}, {'r', {48, 0}}, {'s', {56, 0}}, {'t', {32, 8}}, {'u', {40, 8}},
        {'v', {48, 8}}, {'w', {56, 8}}, {'x', {32, 16}}, {'y', {40, 16}}, {'z', {48, 16}},
        {'0', {0, 0}}, {'1', {8, 0}}, {'2', {16, 0}}, {'3', {24, 0}}, {'4', {0, 8}}, {'5', {8, 8}}, {'6', {16, 8}}, {'7', {24, 8}},
        {'8', {0, 16}}, {'9', {8, 16}}, {':', {16, 16}}, {'.', {28, 16}}, {'?', {0, 24}}, {'!', {8, 24}},
        {'-', {4, 32}}, {',', {0, 32}}, {'(', {88, 16}}, {')', {97, 16}}};
    static const std::map<std::uint32_t, GameGlyph> accents = {
        {0xe4, {64, 88}}, {0xe1, {72, 88}}, {0xe0, {80, 88}}, {0xe2, {88, 88}}, {0xe9, {96, 88}}, {0xe8, {64, 97}}, {0xea, {72, 97}},
        {0xe7, {80, 97}}, {0xec, {88, 97}}, {0xed, {96, 97}}, {0xee, {64, 106}}, {0xf1, {72, 106}}, {0xf2, {80, 106}}, {0xf3, {88, 106}},
        {0xf4, {96, 106}}, {0xf6, {64, 115}}, {0xf9, {72, 115}}, {0xfa, {80, 115}}, {0xfb, {88, 115}}, {0xfc, {40, 8}}};

    if (cp >= 0xc0 && cp <= 0xdd && cp != 0xd7) cp += 0x20;
    if (cp >= 0xe0) {
      static const std::map<std::uint32_t, char> base = {{0xe0, 'a'}, {0xe1, 'a'}, {0xe2, 'a'}, {0xe4, 'a'}, {0xe7, 'c'}, {0xe8, 'e'}, {0xe9, 'e'}, {0xea, 'e'},
          {0xec, 'i'}, {0xed, 'i'}, {0xee, 'i'}, {0xf1, 'n'}, {0xf2, 'o'}, {0xf3, 'o'}, {0xf4, 'o'}, {0xf6, 'o'}, {0xf9, 'u'}, {0xfa, 'u'}, {0xfb, 'u'}, {0xfc, 'u'}};
      if (capital) { if (auto it = base.find(cp); it != base.end()) cp = std::uint32_t(it->second); }
      else if (auto it = accents.find(cp); it != accents.end()) { g = it->second; return true; }
      else if (auto it2 = base.find(cp); it2 != base.end()) cp = std::uint32_t(it2->second);
    }
    if (cp >= 128) return false;
    char c = char(cp);
    if (capital && c >= 'a' && c <= 'z') c = char(c - 32);
    if (!capital && c >= 'A' && c <= 'Z') c = char(c + 32);
    if (c >= 'A' && c <= 'Z') { if (auto it = caps.find(c); it != caps.end()) { g = it->second; return true; } return false; }
    if (auto it = small.find(c); it != small.end()) { g = it->second; return true; }
    return false;
  }
  static void fontXY(const GameGlyph& g, int& x, int& y) { if (g.u < 64) { x = g.u; y = g.v; } else { x = g.u - 64; y = 64 + g.v; } }
  int gameGlyphWidth(const GameGlyph& g) const {

    if ((g.u == 16 && g.v == 16) || (g.u == 28 && g.v == 16) || (g.u == 0 && g.v == 32) || (g.u == 8 && g.v == 24)) return 1;
    if (g.u == 4 && g.v == 32) return 3;
    int x0 = 0, y0 = 0;
    fontXY(g, x0, y0);
    const int maxw = (g.u == 72 && g.v == 0) ? 15 : (g.u >= 64 && g.v >= 88) ? 6 : 8;
    int w = 0;
    for (int x = 0; x < maxw && x0 + x < 64; ++x) {
      bool ink = false;
      for (int y = 0; y < 8; ++y)
        if (font_page_[std::size_t((y0 + y) * 64 + x0 + x)]) { w = x + 1; ink = true; break; }
      if (!ink && w > 0 && g.u >= 64 && g.v < 88) break;
    }
    return std::max(1, w);
  }

  struct StyledGlyph { GameGlyph g; int w; bool space; };
  std::vector<StyledGlyph> gameLayout(const std::string& s0, bool allCaps = false) const {

    const bool ruText = uiLang() == 2 || rufont::hasCyrillic(s0);
    const std::string s = ruText ? rufont::utf8ToRu(s0) : s0;
    std::vector<StyledGlyph> out;
    bool capNext = true;
    for (std::size_t i = 0; i < s.size();) {
      std::uint32_t cp = static_cast<unsigned char>(s[i]);
      if (cp >= 0xc0 && cp < 0xe0 && i + 1 < s.size()) { cp = ((cp & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f); i += 2; }
      else if (cp >= 0xe0 && i + 2 < s.size()) { cp = 0x2022; i += 3; }
      else ++i;
      if (cp == ' ') { out.push_back({{0, 0}, 4, true}); continue; }
      GameGlyph g{};
      if (cp == '<' || cp == '>') { out.push_back({{cp == '<' ? -1 : -2, 0}, 4, false}); continue; }
      const bool cap = ruText ? (cp >= 'A' && cp <= 'Z') : (allCaps || (capNext && ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z' || cp >= 0xc0)));
      if (!gameGlyph(cp, cap, g)) { out.push_back({{0, 0}, 4, true}); continue; }
      out.push_back({g, gameGlyphWidth(g), false});
      if (cp == ':' || cp == '.' || cp == '-') capNext = true;
      else if (cp != '(' && cp != ')') capNext = false;
    }
    return out;
  }
  float gameTextWidth(const std::string& s, float scale, bool allCaps = false) const {
    int w = 0;
    bool prevGlyph = false;
    for (const auto& g : gameLayout(s, allCaps)) {
      if (g.space) { w += g.w; prevGlyph = false; continue; }
      if (prevGlyph) w += 2;
      w += g.w;
      prevGlyph = true;
    }
    return float(w) * scale;
  }
  void gameText(const std::string& s, float x, float y, float scale, u_char r, u_char g, u_char b, bool allCaps = false) {
    if (font_dirty_) { GR_UploadVRAMAliasPage(kFontAlias, font_page_.data()); font_dirty_ = false; }
    float cx = x;
    bool prevGlyph = false;
    for (const auto& gl : gameLayout(s, allCaps)) {
      if (gl.space) { cx += float(gl.w) * scale; prevGlyph = false; continue; }
      if (prevGlyph) cx += 2.0f * scale;
      if (gl.g.u < 0) {

        POLY_F3 t{};
        setPolyF3(&t);
        setRGB0(&t, r, g, b);
        const float w = 4.0f * scale, ym = y + 4.0f * scale, hh = 3.0f * scale;
        if (gl.g.u == -1) setXY3(&t, cx, ym, cx + w, ym - hh, cx + w, ym + hh);
        else setXY3(&t, cx + w, ym, cx, ym - hh, cx, ym + hh);
        DrawPrim(&t);
        cx += w;
        prevGlyph = true;
        continue;
      }
      int fx = 0, fy = 0;
      fontXY(gl.g, fx, fy);
      POLY_FT4 p{};
      setPolyFT4(&p);
      setRGB0(&p, r, g, b);
      const float x1 = cx + float(gl.w) * scale, y1 = y + 8.0f * scale;
      setXY4(&p, cx, y, x1, y, cx, y1, x1, y1);
      setUV4(&p, fx, fy, fx + gl.w, fy, fx, fy + 8, fx + gl.w, fy + 8);
      p.tpage = u_short((kFontAlias + 1) * 1024 + (2 << 7));
      p.clut = 0;
      DrawPrim(&p);
      cx = x1;
      prevGlyph = true;
    }
  }

  using Packets = std::vector<std::vector<std::uint32_t>>;
  struct FontCell { int u, v; };
  static const FontCell* fontCell(char c) {
    static const std::map<char, FontCell> m = {
        {'0', {0, 0}}, {'1', {8, 0}}, {'2', {16, 0}}, {'3', {24, 0}}, {'4', {0, 8}}, {'5', {8, 8}}, {'6', {16, 8}}, {'7', {24, 8}},
        {'8', {0, 16}}, {'9', {8, 16}},
        {'A', {56, 16}}, {'B', {32, 24}}, {'C', {40, 24}}, {'D', {48, 24}}, {'E', {56, 24}}, {'F', {32, 32}}, {'G', {40, 32}},
        {'H', {48, 32}}, {'I', {56, 32}}, {'J', {32, 40}}, {'K', {40, 40}}, {'L', {48, 40}}, {'M', {56, 40}}, {'N', {32, 48}},
        {'O', {40, 48}}, {'P', {48, 48}}, {'Q', {56, 48}}, {'R', {32, 56}}, {'S', {40, 56}}, {'T', {48, 56}}, {'U', {56, 56}},
        {'V', {64, 0}}, {'W', {72, 0}}, {'X', {88, 0}}, {'Y', {96, 0}}, {'Z', {64, 8}}, {'-', {4, 32}},

        {'a', {8, 32}}, {'b', {16, 32}}, {'c', {24, 32}}, {'d', {0, 40}}, {'e', {8, 40}}, {'f', {16, 40}}, {'g', {24, 40}},
        {'h', {0, 48}}, {'i', {8, 48}}, {'j', {16, 48}}, {'k', {24, 48}}, {'l', {0, 56}}, {'m', {8, 56}}, {'n', {16, 56}},
        {'o', {24, 56}}, {'p', {32, 0}}, {'q', {40, 0}}, {'r', {48, 0}}, {'s', {56, 0}}, {'t', {32, 8}}, {'u', {40, 8}},
        {'v', {48, 8}}, {'w', {56, 8}}, {'x', {32, 16}}, {'y', {40, 16}}, {'z', {48, 16}}};
    auto it = m.find(c);
    return it == m.end() ? nullptr : &it->second;
  }

  int texel8(unsigned tpage, int u, int v) const {
    const int bx = int(tpage & 0xf) * 64, by = int((tpage >> 4) & 1) * 256;
    const std::size_t idx = std::size_t(by + v) * 1024 + std::size_t(bx + u / 2);
    if (idx >= vram_.size()) return 0;
    const auto w = vram_[idx];
    return (u & 1) ? (w >> 8) : (w & 0xff);
  }
  std::uint16_t clutColor(std::uint16_t clut, int index) const {
    const std::size_t idx = std::size_t((clut >> 6) & 0x1ff) * 1024 + std::size_t((clut & 0x3f) * 16 + index);
    return idx < vram_.size() ? vram_[idx] : 0;
  }

  int glyphWidth(unsigned tpage, std::uint16_t clut, const FontCell& fc) const {
    int w = 0;
    const int maxw = fc.u >= 64 ? 14 : 8;
    for (int x = 0; x < maxw; ++x) {
      bool ink = false;
      for (int y = 0; y < 8; ++y)
        if (clutColor(clut, texel8(tpage, fc.u + x, fc.v + y))) { w = x + 1; ink = true; break; }
      if (!ink && w > 0 && fc.u >= 64) break;
    }
    return std::max(1, w);
  }
  static std::string promptKeyName(int code) {
    if (code >= ctl::kMouseBase) {
      switch (code - ctl::kMouseBase) {
        case SDL_BUTTON_LEFT: return "CLIC IZQ";
        case SDL_BUTTON_RIGHT: return "CLIC DER";
        case SDL_BUTTON_MIDDLE: return "RUEDA";
        default: return "RATON";
      }
    }
    switch (code) {
      case SDL_SCANCODE_SPACE: return "ESPACIO";
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return "ENTER";
      case SDL_SCANCODE_BACKSPACE: return "RETROCESO";
      case SDL_SCANCODE_ESCAPE: return "ESC";
      case SDL_SCANCODE_TAB: return "TAB";
      case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return "MAYUS";
      case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return "CTRL";
      case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return "ALT";
      case SDL_SCANCODE_UP: return "ARRIBA";
      case SDL_SCANCODE_DOWN: return "ABAJO";
      case SDL_SCANCODE_LEFT: return "IZQUIERDA";
      case SDL_SCANCODE_RIGHT: return "DERECHA";
      default: break;
    }
    std::string n = SDL_GetScancodeName(SDL_Scancode(code));
    std::string o;
    for (char c : n) { const char u = char(std::toupper(static_cast<unsigned char>(c))); if (fontCell(u) || u == ' ') o += u; }
    return o.empty() ? "?" : o;
  }

  std::string promptLabel(int action) const {
    std::string l = promptLabelEs(action);
    if (uiLang() != 2) return l;

    static const std::map<std::string, std::string> kRu = {{"ESPACIO", "ПРОБЕЛ"}, {"ENTER", "ВВОД"}, {"ARRIBA", "ВВЕРХ"}, {"ABAJO", "ВНИЗ"},
        {"IZQUIERDA", "ВЛЕВО"}, {"DERECHA", "ВПРАВО"}, {"FLECHAS", "СТРЕЛКИ"}, {"CRUCETA", "КРЕСТОВИНА"}, {"CLIC IZQ", "ЛКМ"}, {"CLIC DER", "ПКМ"},
        {"RUEDA", "КОЛЕСО"}, {"RATON", "МЫШЬ"}, {"MAYUS", "shift"}, {"RETROCESO", "backspace"}};
    if (auto it = kRu.find(l); it != kRu.end()) l = it->second;
    std::string o;
    for (std::size_t i = 0; i < l.size();) {
      const unsigned char c = static_cast<unsigned char>(l[i]);
      if ((c == 0xd0 || c == 0xd1) && i + 1 < l.size()) { const std::uint32_t cp = ((c & 31u) << 6) | (static_cast<unsigned char>(l[i + 1]) & 0x3fu); if (const char* r = rufont::cyr(cp)) o += r; i += 2; continue; }
      o += char(std::tolower(c)); ++i;
    }
    return o;
  }
  std::string promptLabelEs(int action) const {
    if (last_input_pad_ && pad_) {
      if (action == -1) return "CRUCETA";
      for (int c : binds_.pad[std::size_t(action)]) if (c >= 0) {
        std::string l = ctl::padLabel(c, ctl::padFamily(pad_));
        std::string o;
        for (char ch : l) { const char u = char(std::toupper(static_cast<unsigned char>(ch))); if (fontCell(u) || u == ' ') o += u; }
        if (!o.empty()) return o;
      }
      return "?";
    }
    if (action == -1) return "FLECHAS";
    if (action == ctl::kTriangle) return "ESC";
    for (int c : binds_.keys[std::size_t(action)]) if (c) return promptKeyName(c);
    return "?";
  }

  bool promptRound(int action, char& letter, int& r, int& g, int& b) const {
    static const bool fakeXbox = std::getenv("SF3N_PRUEBA_MANDO_XBOX") != nullptr;
    if (!((last_input_pad_ && pad_) || fakeXbox) || action < 0) return false;
    static const bool off = envInt("SF3N_BOTONES_REDONDOS", 1) == 0;
    if (off) return false;
    const auto fam = fakeXbox ? ctl::PadFamily::Xbox : ctl::padFamily(pad_);
    if (fam == ctl::PadFamily::PlayStation) return false;
    const bool nin = fam == ctl::PadFamily::Nintendo;
    for (int c : binds_.pad[std::size_t(action)]) {
      if (c < 0) continue;
      if (c != SDL_CONTROLLER_BUTTON_A && c != SDL_CONTROLLER_BUTTON_B && c != SDL_CONTROLLER_BUTTON_X && c != SDL_CONTROLLER_BUTTON_Y) return false;
      const std::string l = ctl::padLabel(c, fam);
      if (l.size() != 1) return false;
      letter = l[0];
      if (nin) { r = 90; g = 90; b = 100; return true; }
      switch (c) {
        case SDL_CONTROLLER_BUTTON_A: r = 60; g = 170; b = 40; break;
        case SDL_CONTROLLER_BUTTON_B: r = 210; g = 45; b = 45; break;
        case SDL_CONTROLLER_BUTTON_X: r = 30; g = 110; b = 220; break;
        default: r = 225; g = 175; b = 20; break;
      }
      return true;
    }
    return false;
  }

  static int iconAction(int u, int v, int w) {
    auto closeTo = [](int a, int b) { return std::abs(a - b) <= 1; };
    if (closeTo(v, 175) && closeTo(u, 94) && w >= 8) return ctl::kCross;
    if (closeTo(v, 175) && closeTo(u, 82) && w >= 8) return ctl::kCircle;
    if (closeTo(v, 184) && closeTo(u, 82) && w >= 8) return ctl::kSquare;
    if (closeTo(v, 184) && closeTo(u, 94) && w >= 8) return ctl::kTriangle;
    if (closeTo(u, 82) && w >= 18) {
      if (closeTo(v, 129)) return ctl::kSelect;
      if (closeTo(v, 138)) return ctl::kR1;
      if (closeTo(v, 147)) return ctl::kL1;
      if (closeTo(v, 156)) return ctl::kR2;
      if (closeTo(v, 165)) return ctl::kL2;
    }
    if (v >= 174 && v <= 177 && u >= 47 && u < 80) return -1;
    return -2;
  }
  Packets pcPrompts(const Packets& in) const {
    if (last_input_pad_ && pad_ && ctl::padFamily(pad_) == ctl::PadFamily::PlayStation) return in;
    static const bool off = envInt("SF3N_TECLAS_EN_PANTALLA", 1) == 0;
    if (off) return in;
    struct Spr { std::size_t idx; double x, y; int u, v, w, h; unsigned tpage; std::uint16_t clut; int oy; int ox; int action; };
    std::vector<Spr> sprs;
    unsigned tpage = 0;
    int ox = 0, oy = 0;
    std::map<std::pair<unsigned, std::uint16_t>, int> fontVotes;
    bool anyIcon = false;
    for (std::size_t i = 0; i < in.size(); ++i) {
      const auto& pk = in[i];
      if (pk.empty()) continue;
      const auto cmd = pk[0] >> 24;
      if (cmd == 0xe1) { tpage = pk[0] & 0x1ff; continue; }
      if (cmd == 0xe5) { ox = std::int32_t(pk[0] << 21) >> 21; oy = std::int32_t((pk[0] >> 11) << 21) >> 21; continue; }
      if ((cmd & 0xfc) == 0x64 && pk.size() >= 4) {
        Spr s{i, sx11(pk[1]), sy11(pk[1]), int(pk[2] & 0xff), int((pk[2] >> 8) & 0xff), int(pk[3] & 0x3ff), int((pk[3] >> 16) & 0x1ff), tpage, std::uint16_t(pk[2] >> 16), oy, ox, -2};
        if (s.h == 8 && ((tpage >> 7) & 3) == 1) {
          s.action = iconAction(s.u, s.v, s.w);
          if (s.action != -2) anyIcon = true;
          else ++fontVotes[{tpage, s.clut}];
        }
        sprs.push_back(s);
      }
    }
    if (static const bool pl = std::getenv("SF3N_PROMPTLOG") != nullptr; pl) {
      static int n = 0;
      if (n++ % 200 == 0) {
        std::fprintf(log, "prompts: %zu sprites, icono=%d:", sprs.size(), int(anyIcon));
        for (const auto& s2 : sprs) if (s2.h == 8 && s2.u >= 80) std::fprintf(log, " (%d,%d w%d tp%03x a%d)", s2.u, s2.v, s2.w, s2.tpage, s2.action);
        std::fprintf(log, "\n");
      }
    }
    if (!anyIcon) return in;
    std::map<std::size_t, double> shift;
    std::map<std::size_t, Packets> replace;
    for (const auto& ic : sprs) {
      if (ic.action == -2 || ic.h != 8) continue;
      if (fontVotes[{ic.tpage, ic.clut}] < 2) continue;
      char rl = 0; int rr = 0, rg = 0, rb = 0;
      const bool round = promptRound(ic.action, rl, rr, rg, rb);
      const std::string label = round ? std::string(1, rl) : promptLabel(ic.action);

      std::vector<std::pair<const FontCell*, int>> glyphs;
      int tw = 0;
      for (char c : label) {
        if (c == ' ') { tw += 4; glyphs.push_back({nullptr, 2}); continue; }
        const FontCell* fc = fontCell(c);
        if (!fc) continue;
        const int gw = glyphWidth(ic.tpage, ic.clut, *fc);
        if (!glyphs.empty() && glyphs.back().first) tw += 2;
        glyphs.push_back({fc, gw});
        tw += gw;
      }
      const int capW = round ? 12 : tw + 6;
      const double delta = double(capW - ic.w);

      for (const auto& o : sprs) {
        if (o.idx == ic.idx || o.ox != ic.ox || std::fabs((o.y + o.oy) - (ic.y + ic.oy)) > 1.0) continue;
        shift[o.idx] += (o.x > ic.x ? delta : 0.0) - delta * 0.5;
      }
      shift[ic.idx] += -delta * 0.5;

      const auto& orig = in[ic.idx];
      std::uint32_t col = orig[0] & 0xffffff;
      std::uint16_t tc = 0;
      if (const FontCell* fi = fontCell('I')) for (int y = 0; y < 8 && !tc; ++y) tc = clutColor(ic.clut, texel8(ic.tpage, fi->u, fi->v + y));
      int r = 200, g = 200, b = 220;
      if (tc) {
        const bool rawTex = (orig[0] >> 24) & 1;
        const int mr = rawTex ? 128 : int(col & 0xff), mg = rawTex ? 128 : int((col >> 8) & 0xff), mb = rawTex ? 128 : int((col >> 16) & 0xff);
        r = std::min(255, ((tc & 31) << 3) * mr / 128); g = std::min(255, (((tc >> 5) & 31) << 3) * mg / 128); b = std::min(255, (((tc >> 10) & 31) << 3) * mb / 128);
      }
      const std::uint32_t flat = 0x60000000u | std::uint32_t(r) | (std::uint32_t(g) << 8) | (std::uint32_t(b) << 16);
      Packets rep;
      auto xy = [](int x, int y) { return (std::uint32_t(y & 0x7ff) << 16) | std::uint32_t(x & 0x7ff); };
      const int x0 = int(std::lround(ic.x)), y0 = int(std::lround(ic.y)) - 2;
      if (round) {

        const std::uint32_t fc = 0xfd000000u | std::uint32_t(rr) | (std::uint32_t(rg) << 8) | (std::uint32_t(rb) << 16);
        const bool darkLetter = rl == 'Y' && rr > 200 && rg > 150;
        rep.push_back({fc, xy(x0 + 6, y0 + 6), (6u * 16u + 8u) | (std::uint32_t(std::uint8_t(rl)) << 16) | (darkLetter ? 1u << 24 : 0u)});
        replace[ic.idx] = std::move(rep);
        continue;
        if (static const bool pl = std::getenv("SF3N_PROMPTLOG") != nullptr; pl) std::fprintf(log, "boton redondo %c glifos %zu ancho %d\n", rl, glyphs.size(), glyphs.empty() ? -1 : glyphs.front().second);
        if (!glyphs.empty() && glyphs.front().first) {
          const auto& [gfc, gw] = glyphs.front();

          const std::uint32_t white = (orig[0] & 0xfe000000u) | (rl == 'Y' && rr > 200 && rg > 150 ? 0x202020u : 0x808080u);
          rep.push_back({white, xy(x0 + 6 - gw / 2, int(std::lround(ic.y))), std::uint32_t(gfc->u) | (std::uint32_t(gfc->v) << 8) | (std::uint32_t(ic.clut) << 16), (8u << 16) | std::uint32_t(gw)});
        }
        replace[ic.idx] = std::move(rep);
        continue;
      }
      rep.push_back({flat, xy(x0, y0), (1u << 16) | std::uint32_t(capW)});
      rep.push_back({flat, xy(x0, y0 + 11), (1u << 16) | std::uint32_t(capW)});
      rep.push_back({flat, xy(x0, y0), (12u << 16) | 1u});
      rep.push_back({flat, xy(x0 + capW - 1, y0), (12u << 16) | 1u});
      int gx = x0 + 3;
      bool first = true;
      for (const auto& [fc, gw] : glyphs) {
        if (!fc) { gx += 4; first = true; continue; }
        if (!first) gx += 2;
        first = false;
        rep.push_back({(orig[0] & 0xff000000u) | col, xy(gx, int(std::lround(ic.y))), std::uint32_t(fc->u) | (std::uint32_t(fc->v) << 8) | (std::uint32_t(ic.clut) << 16), (8u << 16) | std::uint32_t(gw)});
        gx += gw;
      }
      replace[ic.idx] = std::move(rep);
    }
    if (replace.empty()) return in;
    Packets res;
    res.reserve(in.size() + replace.size() * 10);
    for (std::size_t i = 0; i < in.size(); ++i) {
      const double dx = shift.count(i) ? shift[i] : 0.0;
      auto mv = [&](std::vector<std::uint32_t> pk) {
        if (dx != 0.0 && pk.size() >= 2) {
          const int x = int(sx11(pk[1])) + int(std::lround(dx));
          pk[1] = (pk[1] & 0xffff0000u) | std::uint32_t(x & 0x7ff);
        }
        return pk;
      };
      if (auto it = replace.find(i); it != replace.end()) { for (const auto& p : it->second) res.push_back(mv(p)); continue; }
      res.push_back(mv(in[i]));
    }
    return res;
  }
  void drawUploadedBackground() {
    const auto& vr = frame_vram_.size() >= 1024 * 512 ? frame_vram_ : vram_;
    if (vr.size() < 1024 * 512) return;
    const int w = std::clamp(disp_.w, 16, 640), h = std::clamp(disp_.h, 16, 256);

    bool hq = false;
    if (g_title_hq.active()) {
      const std::uint32_t tk = g_title_hq.readTick();
      if (tk != hq_tick_) { hq_tick_ = tk; hq_tick_field_ = field_count_; }
      hq = hq_tick_ != 0 && field_count_ - hq_tick_field_ < 30 && field_count_ - last_mdec_field_ < 25;
    }
    if (hq) {
      const bool changed = g_title_hq.frameFor(g_title_hq.psFrame(), nowSeconds());
      const int W = g_title_hq.w(), H = g_title_hq.h();
      if (changed || !bg_hq_ || bg_w_ != W || bg_h_ != H) {
        bg_px_ = g_title_hq.rgba();
        ++hq_seq_; bg_seq_ = ~0ull - hq_seq_;
        if (bg_tex_ == 0 || bg_w_ != W || bg_h_ != H) { if (bg_tex_) GR_DestroyTexture(bg_tex_); bg_tex_ = GR_CreateRGBATexture(W, H, bg_px_.data()); }
        else GR_UpdateRGBATexture(bg_tex_, W, H, bg_px_.data());
        bg_w_ = W; bg_h_ = H; bg_hq_ = true;
      }
      HudMap m; m.dw = disp_.w;
      const float x0 = m.x(0), x1 = m.x(w), y0 = m.y(0), y1 = m.y(h);
      DR_TPAGE page{};
      SetDrawTPage(&page, 1, 0, GetTPage(2, 0, 0, 0));
      DrawPrim(&page);
      drawTitleSideFill(x0, x1, y0, y1, W, H);
      DR_PSYX_TEX tex{};
      SetPsyXTexture(&tex, bg_tex_, 255, 255);
      DrawPrim(&tex);
      POLY_FT4 q{};
      setPolyFT4(&q);
      setRGB0(&q, 128, 128, 128);
      setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
      setUV4(&q, 0, 0, 255, 0, 0, 255, 255, 255);
      DrawPrim(&q);
      DR_PSYX_TEX restore{};
      SetPsyXTexture(&restore, 0u, 0, 0);
      DrawPrim(&restore);
      return;
    }
    if (bg_hq_) { bg_hq_ = false; bg_seq_ = ~0ull - 1000000; }
    if (bg_seq_ != frame_seq_seen_ || bg_w_ != w || bg_h_ != h) {
      bg_seq_ = frame_seq_seen_;
      bg_px_.resize(std::size_t(w) * h * 4);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const auto c = vr[std::size_t(((disp_.y + y) & 511) * 1024 + ((disp_.x + x) & 1023))];
          unsigned char* px = &bg_px_[(std::size_t(y) * w + x) * 4];
          const int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
          px[0] = (unsigned char)((r << 3) | (r >> 2)); px[1] = (unsigned char)((g << 3) | (g >> 2)); px[2] = (unsigned char)((b << 3) | (b >> 2)); px[3] = 255;
        }

      static const bool noFilter = envInt("SF3N_VIDEO_FILTRO", 1) == 0;
      if (!noFilter && g_cfg_bilinearFiltering && field_count_ - last_mdec_field_ < 25) filterVideo(bg_px_, w, h);
      if (bg_tex_ == 0 || bg_w_ != w || bg_h_ != h) {
        if (bg_tex_) GR_DestroyTexture(bg_tex_);
        bg_tex_ = GR_CreateRGBATexture(w, h, bg_px_.data());
      } else {
        GR_UpdateRGBATexture(bg_tex_, w, h, bg_px_.data());
      }
      bg_w_ = w; bg_h_ = h;
    }
    HudMap m; m.dw = disp_.w;
    const float x0 = m.x(0), x1 = m.x(w), y0 = m.y(0), y1 = m.y(h);
    DR_TPAGE page{};
    SetDrawTPage(&page, 1, 0, GetTPage(2, 0, 0, 0));
    DrawPrim(&page);
    drawTitleSideFill(x0, x1, y0, y1, w, h);
    const int qw = (w + 3) / 4, qh = (h + 3) / 4;
    DR_PSYX_TEX tex{};
    SetPsyXTexture(&tex, bg_tex_, qw, qh);
    DrawPrim(&tex);
    POLY_FT4 q{};
    setPolyFT4(&q);
    setRGB0(&q, 128, 128, 128);
    setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
    setUV4(&q, 0, 0, u_char(qw), 0, 0, u_char(qh), u_char(qw), u_char(qh));
    DrawPrim(&q);
    DR_PSYX_TEX restore{};
    SetPsyXTexture(&restore, 0u, 0, 0);
    DrawPrim(&restore);
  }

  TextureID blur_tex_{};
  int blur_w_ = 0, blur_h_ = 0;
  std::uint64_t blur_seq_ = ~0ull;
  std::vector<unsigned char> blur_px_;
  void drawTitleSideFill(float x0, float x1, float y0, float y1, int w, int h) {
    static const bool on = iniString("titulo_bordes", "1") != "0";
    if (!on || g_cfg_aspectMode != PSYX_ASPECT_ADAPTIVE || !g_window) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    const float visHalf = 192.0f * (float(ww) / float(wh)) / (4.0f / 3.0f);
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    if (visHalf * 2.0f <= (x1 - x0) + 2.0f) return;
    static const int level = std::clamp(std::atoi(iniString("titulo_bordes_difuminado", "3").c_str()), 1, 6);
    const int div = 1 << level;
    const int bw = std::max(4, w / div), bh = std::max(4, h / div);
    if (blur_seq_ != bg_seq_ || blur_w_ != bw || blur_h_ != bh) {
      blur_seq_ = bg_seq_;
      std::vector<float> a(std::size_t(bw) * bh * 3, 0.0f);
      for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
          const int sx0 = x * w / bw, sx1 = std::max(sx0 + 1, (x + 1) * w / bw), sy0 = y * h / bh, sy1 = std::max(sy0 + 1, (y + 1) * h / bh);
          float r = 0, g = 0, b = 0; int n = 0;
          for (int yy = sy0; yy < sy1; ++yy) for (int xx = sx0; xx < sx1; ++xx) { const unsigned char* p = &bg_px_[(std::size_t(yy) * w + xx) * 4]; r += p[0]; g += p[1]; b += p[2]; ++n; }
          float* d = &a[(std::size_t(y) * bw + x) * 3]; d[0] = r / n; d[1] = g / n; d[2] = b / n;
        }
      for (int pass = 0; pass < (level <= 3 ? 1 + level : 2 * level); ++pass) {
        std::vector<float> t(a.size());
        for (int y = 0; y < bh; ++y)
          for (int x = 0; x < bw; ++x)
            for (int c = 0; c < 3; ++c) {
              float sum = 0; int n = 0;
              for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int xx = std::clamp(x + dx, 0, bw - 1), yy = std::clamp(y + dy, 0, bh - 1);
                sum += a[(std::size_t(yy) * bw + xx) * 3 + std::size_t(c)]; ++n;
              }
              t[(std::size_t(y) * bw + x) * 3 + std::size_t(c)] = sum / n;
            }
        a.swap(t);
      }
      blur_px_.resize(std::size_t(bw) * bh * 4);
      for (std::size_t i = 0; i < std::size_t(bw) * bh; ++i) {
        for (int c = 0; c < 3; ++c) blur_px_[i * 4 + std::size_t(c)] = (unsigned char)std::clamp(int(a[i * 3 + std::size_t(c)] + 0.5f), 0, 255);
        blur_px_[i * 4 + 3] = 255;
      }
      if (blur_tex_ == 0 || blur_w_ != bw || blur_h_ != bh) {
        if (blur_tex_) GR_DestroyTexture(blur_tex_);
        blur_tex_ = GR_CreateRGBATexture(bw, bh, blur_px_.data());
      } else {
        GR_UpdateRGBATexture(blur_tex_, bw, bh, blur_px_.data());
      }
      blur_w_ = bw; blur_h_ = bh;
    }

    const float sc = std::max(visHalf * 2.0f / (x1 - x0), 240.0f / (y1 - y0)) * 1.06f;
    const float hw = (x1 - x0) * 0.5f * sc, hh = (y1 - y0) * 0.5f * sc;
    static const int bright = std::clamp(std::atoi(iniString("titulo_bordes_brillo", "50").c_str()), 0, 100);
    const u_char lv = u_char(std::lround(128.0 * bright / 100.0));
    const int qw = (bw + 3) / 4, qh = (bh + 3) / 4;
    DR_PSYX_TEX tex{};
    SetPsyXTexture(&tex, blur_tex_, qw, qh);
    DrawPrim(&tex);
    POLY_FT4 q{};
    setPolyFT4(&q);
    setRGB0(&q, lv, lv, lv);
    setXY4(&q, cx - hw, cy - hh, cx + hw, cy - hh, cx - hw, cy + hh, cx + hw, cy + hh);
    setUV4(&q, 0, 0, u_char(qw), 0, 0, u_char(qh), u_char(qw), u_char(qh));
    DrawPrim(&q);
  }
  TextureID bg_tex_{};
  int bg_w_ = 0, bg_h_ = 0;
  bool bg_hq_ = false;
  std::uint32_t hq_tick_ = 0; std::uint64_t hq_tick_field_ = 0, hq_seq_ = 0;
  std::uint64_t bg_seq_ = ~0ull;
  std::vector<unsigned char> bg_px_;

  std::uint64_t portraits_patched_seq_ = ~0ull;
  void patchPortraits() {
    static const bool off = std::getenv("SF3N_RETRATOS_8BITS") != nullptr;
    if (off || portraits_patched_seq_ == frame_seq_seen_ || frame_vram_.size() < 1024 * 512) return;
    portraits_patched_seq_ = frame_seq_seen_;
    const auto& vr = frame_vram_;
    auto markOk = [&](int slot, int sx, int sy) {
      const std::size_t m = std::size_t(slot * 64 + 56) * 1024 + 512;
      if (vr[m + 2] != 0x5F3A) return false;
      std::uint32_t h = 2166136261u;
      for (int y = 0; y < 56; ++y) for (int x = 0; x < 32; ++x) h = (h ^ vr[std::size_t((sy + y) & 511) * 1024 + std::size_t((sx + x) & 1023)]) * 16777619u;
      return vr[m] == (h & 0xffff) && vr[m + 1] == (h >> 16);
    };
    std::uint32_t e1 = 0;
    for (std::size_t i = 0; i < frame_.size(); ++i) {
      auto& pk = frame_[i];
      if (pk.empty()) continue;
      const std::uint32_t c = pk[0] >> 24;
      if (c == 0xe1) { e1 = pk[0] & 0xffffff; continue; }
      if (c < 0x64 || c > 0x67 || pk.size() < 4) continue;
      if ((pk[2] >> 16) != 0x7930u || (pk[3] & 0xffff) != 64 || (pk[3] >> 16) != 56) continue;
      int slot = -1, sx = 0, sy = 0;
      const int u = int(pk[2] & 0xff), v = int((pk[2] >> 8) & 0xff), tx = int(e1 & 15) * 64;
      if (tx == 384 && v == 112 && u == 0) { slot = 0; sx = 384; sy = 112; }
      else { const int scrx = int(std::int16_t(pk[1] & 0xffff)); slot = scrx < 0 ? 1 : 2; sx = slot == 1 ? 384 : 416; sy = 56; }
      if (!markOk(slot, sx, sy)) continue;
      if (static const bool lg = std::getenv("SF3N_ROSTERLOG") != nullptr; lg) std::fprintf(log, "RETRATO 15 bits casilla %d cuadro %llu\n", slot, (unsigned long long)frame_seq_seen_);
      const std::uint32_t hiE1 = 0xe1000000u | (e1 & ~0x19fu & 0xffffffu) | 8u | (2u << 7);
      pk[2] = (pk[2] & 0xffff0000u) | (std::uint32_t(slot * 64) << 8);
      frame_.insert(frame_.begin() + std::ptrdiff_t(i), std::vector<std::uint32_t>{hiE1});
      frame_.insert(frame_.begin() + std::ptrdiff_t(i) + 2, std::vector<std::uint32_t>{0xe1000000u | e1});
      i += 2;
    }
  }
  bool drawGuestNative() {
    static const bool off = envInt("SF3N_MENUS_NATIVOS", 1) == 0;
    if (static const char* d2 = std::getenv("SF3N_FRAMEDUMP2"); d2) {
      static int lo = std::atoi(d2), hi = lo + 50;
      static std::uint64_t lastSeq = 0;
      if (int(field_no_) >= lo && int(field_no_) <= hi && pub_frame_seq_ != lastSeq) {
        lastSeq = pub_frame_seq_;
        char nm[64]; std::snprintf(nm, sizeof nm, "fd_%llu.txt", (unsigned long long)field_no_);
        if (FILE* f = std::fopen(nm, "w")) {
          std::fprintf(f, "disp %d %d %d %d rgb24 %d\n", disp_.x, disp_.y, disp_.w, disp_.h, int(disp_.rgb24));
          for (const auto& pk : frame_) { for (auto w : pk) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); }
          std::fclose(f);
        }
      }
    }
    if (off || disp_.rgb24 || frame_.empty()) return false;
    patchPortraits();
    {

      int poly3d = 0;
      for (const auto& pk : frame_) { const auto c = pk[0] >> 24; if ((c & 0xf4) == 0x34) ++poly3d; }
      if (poly3d > 200) return false;
    }
    bool cleared = false;
    for (const auto& pk : frame_) {
      if (pk.size() >= 3 && (pk[0] >> 24) == 0x02) {
        const int x = int(pk[1] & 0x3f0), y = int((pk[1] >> 16) & 0x1ff), w = int(((pk[2] & 0x3ff) + 15) & ~15), h = int((pk[2] >> 16) & 0x1ff);
        if (x <= disp_.x && y <= disp_.y && x + w >= disp_.x + std::min(disp_.w, 368) - 2 && y + h >= disp_.y + std::min(disp_.h, 240) - 16) cleared = true;
      }
    }

    bool bgUpload = false;
    {
      bool seenPrim = false;
      long covered = 0;
      for (const auto& pk : frame_) {
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        if (c >= 0x20 && c < 0x80) { seenPrim = true; continue; }
        if (c == 0xa0 && pk.size() >= 3) {
          const int x = int(pk[1] & 0x3ff), y = int((pk[1] >> 16) & 0x1ff), w = int(pk[2] & 0xffff), h = int(pk[2] >> 16);
          const int ix0 = std::max(x, disp_.x), iy0 = std::max(y, disp_.y), ix1 = std::min(x + w, disp_.x + disp_.w), iy1 = std::min(y + h, disp_.y + disp_.h);
          if (ix1 <= ix0 || iy1 <= iy0) continue;
          if (seenPrim) { covered = -1; break; }
          covered += long(ix1 - ix0) * (iy1 - iy0);
        }
      }
      static const bool off2 = envInt("SF3N_FONDO_TITULO", 1) == 0;
      bgUpload = !off2 && covered >= long(disp_.w) * disp_.h * 9 / 10;
      if (covered < 0) return false;
    }
    if (bgUpload) cleared = true;
    if (!cleared) return false;
    if (static const char* dump = std::getenv("SF3N_FRAMEDUMP"); dump) {

      static int n = 0;
      static const int every = envInt("SF3N_FRAMEDUMP_EVERY", 100);
      if (n++ % every == 0) {
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s_%04d.txt", dump, n / every);
        if (FILE* f = std::fopen(nm, "w")) {
          std::fprintf(f, "disp %d %d %d %d\n", disp_.x, disp_.y, disp_.w, disp_.h);
          for (const auto& pk : frame_) { for (auto w : pk) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); }
          std::fclose(f);
        }
        std::snprintf(nm, sizeof nm, "%s_%04d.vram", dump, n / every);
        if (FILE* f = std::fopen(nm, "wb")) { std::fwrite(vram_.data(), 2, vram_.size(), f); std::fclose(f); }
      }
    }

    for (const auto& pk : frame_) {
      const auto c = pk[0] >> 24;
      if (c >= 0x80 && c < 0xa0 && pk.size() >= 4) {

        const int x = int(pk[2] & 0x3ff), y = int((pk[2] >> 16) & 0x1ff), w = int(pk[3] & 0xffff), h = int(pk[3] >> 16);
        if (x < disp_.x + 384 && x + w > disp_.x && y < disp_.y + 256 && y + h > disp_.y && w * h > 4) return false;
      } else if (c >= 0x80 && c < 0xa0) return false;
      if (c == 0xa0 && pk.size() >= 3 && !bgUpload) {
        const int x = int(pk[1] & 0x3ff), y = int((pk[1] >> 16) & 0x1ff), w = int(pk[2] & 0xffff), h = int(pk[2] >> 16);
        if (x < disp_.x + 384 && x + w > disp_.x && y < disp_.y + 256 && y + h > disp_.y) return false;
      }
    }

    if ((frame_vram_dirty_ || guest_vram_dirty_) && frame_vram_.size() >= 1024 * 512) {
      frame_vram_dirty_ = guest_vram_dirty_ = false;
      uploadVram(frame_vram_);
      DrawSync(0);
    } else if (guest_vram_dirty_ && vram_.size() >= 1024 * 512) {
      guest_vram_dirty_ = false;
      uploadVram(vram_);
      DrawSync(0);
    }

    setVideoMode(canvas_w, canvas_h, false);
    SetGeomOffset(canvas_w / 2, canvas_h / 2);
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    {
      POLY_F4 q{}; setPolyF4(&q); setRGB0(&q, 0, 0, 0);
      setXY4(&q, -2000.f, -100.f, 2400.f, -100.f, -2000.f, 400.f, 2400.f, 400.f); DrawPrim(&q);
    }
    if (bgUpload) drawUploadedBackground();
    HudMap map;
    map.ax = disp_.x; map.ay = disp_.y; map.dw = disp_.w;
    const Packets prompts = pcPrompts(frame_);
    bool titleRowDone = false;
    for (const auto& pk : prompts) {
      if (pk.empty()) continue;
      const auto w0 = pk[0];
      const auto cmd = w0 >> 24;
      if (cmd == 0x02 && pk.size() >= 3) {
        const double x = double(pk[1] & 0x3f0), y = double((pk[1] >> 16) & 0x1ff), w = double(((pk[2] & 0x3ff) + 15) & ~15), h = double((pk[2] >> 16) & 0x1ff);

        if (y + h <= disp_.y || y >= disp_.y + 256 || x + w <= disp_.x || x >= disp_.x + 384) continue;
        HudMap fm; fm.ax = disp_.x; fm.ay = disp_.y; fm.dw = disp_.w;
        const float x0 = fm.xe(x, true), x1 = fm.xe(x + w, true), y0 = fm.y(y), y1 = fm.y(y + h);
        POLY_F4 q{}; setPolyF4(&q); setRGB0(&q, u_char(w0 & 0xff), u_char((w0 >> 8) & 0xff), u_char((w0 >> 16) & 0xff));
        setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1); DrawPrim(&q);
        continue;
      }
      if (cmd == 0xe1) {
        hud_tpage_ = w0 & 0x1ff;
        DR_TPAGE t{};
        SetDrawTPage(&t, 1, 0, int(w0 & 0x1ff));
        DrawPrim(&t);
        continue;
      }
      if (cmd == 0xe3) { map.ax = double(w0 & 0x3ff); map.ay = double((w0 >> 10) & 0x3ff); continue; }
      if (cmd == 0xe5) { map.ox = double(std::int32_t(w0 << 21) >> 21); map.oy = double(std::int32_t((w0 >> 11) << 21) >> 21); continue; }

      if (std::fabs(map.ay - disp_.y) > 8 || std::fabs(map.ax - disp_.x) > 8) continue;
      if (cmd == 0xfd) { hudCircle(pk, map); continue; }
      if (cmd >= 0x20 && cmd < 0x40) { hudPolygon(pk, map); continue; }
      if (cmd >= 0x40 && cmd < 0x60 && !(cmd & 0x08)) { hudLine(pk, map); continue; }
      if (cmd >= 0x60 && cmd < 0x80) {
        if (static const bool tpl = std::getenv("SF3N_TITLEPK") != nullptr; tpl && pk.size() >= 4 && ((pk[3] >> 16) & 0x1ff) == 66) {
          static int n = 0; if (n++ < 60) std::fprintf(log, "TITLEPK campo %llu x %.0f y %.0f w %u col %06x uvclut %08x tpage %03x\n", (unsigned long long)field_count_, sx11(pk[1]), sy11(pk[1]), pk[3] & 0x3ff, pk[0] & 0xffffff, pk[2], hud_tpage_);
        }

        if ((title_top_.load() || title_sub_.load()) && (cmd & 4) && pk.size() >= 4 && sy11(pk[1]) == 50.0 && ((pk[3] >> 16) & 0x1ff) == 66) {
          noteTitleWord(hud_tpage_, (pk[2] >> 8) & 0xffu, pk[3] & 0x3ffu, w0 >> 24);
          if (!titleRowDone) { titleRowDone = true; const auto tp = hud_tpage_; drawTitleRow(map); hud_tpage_ = tp; DR_TPAGE t{}; SetDrawTPage(&t, 1, 0, int(tp)); DrawPrim(&t); }
          continue;
        }
        hudRect(pk, map); continue;
      }
    }
    return true;
  }

  struct NativeReticle {
    bool init = false; double x = 0, y = 0, vx = 0, vy = 0, hw = 0, hh = 0, vhw = 0, vhh = 0, rh = 0, rv = 0, vrh = 0, vrv = 0;
    double reveal = 1, last = 0, seen = -1e9; bool aim = false; std::uint32_t col = 0x40ff40;
  } nret_;
  static void retSpring(double& v, double& vel, double target, double w, double dt) {
    dt = std::clamp(dt, 0.0, 1.0 / 15.0);
    if (dt <= 0) return;
    const double d = v - target, decay = std::exp(-w * dt), imp = (vel + w * d) * dt;
    v = target + (d + imp) * decay; vel = (vel - w * imp) * decay;
  }
  Packets nativeReticle(const Packets& in) {
    HudMap map;
    struct L { double x0, y0, x1, y1; std::uint32_t col; std::size_t idx; };
    std::vector<L> mainL; std::vector<std::size_t> trail;
    double cx = 0, cy = 0; bool dot = false;
    for (std::size_t k = 0; k < in.size(); ++k) {
      const auto& pk = in[k];
      if (pk.empty()) continue;
      const auto cmd = pk[0] >> 24;
      if (cmd == 0xe3) { e3Area(map, pk[0]); continue; }
      if (cmd == 0xe5) { map.ox = double(std::int32_t(pk[0] << 21) >> 21); map.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
      if (!(cmd >= 0x40 && cmd < 0x60) || (cmd & 0x18) || pk.size() < 3) continue;
      const std::uint32_t c = pk[0] & 0xffffff, r = c & 0xff, g = (c >> 8) & 0xff, b = (c >> 16) & 0xff;
      if (!(g > r + 0x40 && g > b + 0x40)) continue;
      const double x0 = sx11(pk[1]) + map.ox - map.ax - 192.0, y0 = sy11(pk[1]) + map.oy - map.ay - HudMap::center_y;
      const double x1 = sx11(pk[2]) + map.ox - map.ax - 192.0, y1 = sy11(pk[2]) + map.oy - map.ay - HudMap::center_y;
      if (std::fabs(x0) > 400 || std::fabs(y0) > 300) { trail.push_back(k); continue; }
      if (g >= 0xd0) {
        mainL.push_back({x0, y0, x1, y1, c, k});
        if (x0 == x1 && y0 == y1) { cx = x0; cy = y0; dot = true; }
      } else trail.push_back(k);
    }
    const double now = nowSeconds(), dt = now - nret_.last;
    nret_.last = now;
    (void)dot;

    double ymin = 1e9, ymax = -1e9;
    for (const auto& l : mainL) if (l.y0 == l.y1 && l.x0 != l.x1) { ymin = std::min(ymin, l.y0); ymax = std::max(ymax, l.y0); }
    if (mainL.size() < 6 || ymax - ymin < 2) {
      if (static const bool dl = std::getenv("SF3N_MIRADBG") != nullptr; dl) std::fprintf(log, "MIRAFALLA main %zu\n", mainL.size());
      nret_.init = false; return in;
    }
    double xmin = 1e9, xmax = -1e9;
    for (const auto& l : mainL) if (l.y0 == l.y1 && (l.y0 == ymin || l.y0 == ymax)) { xmin = std::min({xmin, l.x0, l.x1}); xmax = std::max({xmax, l.x0, l.x1}); }
    cx = (xmin + xmax) * 0.5; cy = (ymin + ymax) * 0.5;
    const double hw = (xmax - xmin) * 0.5, hh = (ymax - ymin) * 0.5;
    double rh = 0, rv = 0;
    for (const auto& l : mainL) {
      if (l.y0 == l.y1 && std::fabs(l.y0 - cy) < 1.01 && l.x0 != l.x1) rh = std::max(rh, std::max(std::fabs(l.x0 - cx), std::fabs(l.x1 - cx)) - hw);
      if (l.x0 == l.x1 && std::fabs(l.x0 - cx) < 1.51 && l.y0 != l.y1) rv = std::max(rv, std::max(std::fabs(l.y0 - cy), std::fabs(l.y1 - cy)) - hh);
    }
    if (hw < 1 || hh < 1) { nret_.init = false; return in; }

    double tx = 0, ty = 0;
    const bool aim = disp_aim_;
    if (!aim) {

      const auto& gc = cur().camera;
      tx = cx; ty = cy;
      if (gc.valid && gc.fx > 1 && gc.fy > 1 && disp_cam_.fx > 1) {
        const double rx = cx / gc.fx, ry = cy / gc.fy;
        double w[3], c3[3];
        for (int j = 0; j < 3; ++j) w[j] = gc.rot[std::size_t(j)] * rx + gc.rot[std::size_t(3 + j)] * ry + gc.rot[std::size_t(6 + j)];
        for (int i = 0; i < 3; ++i) c3[i] = disp_cam_.rot[std::size_t(i * 3)] * w[0] + disp_cam_.rot[std::size_t(i * 3 + 1)] * w[1] + disp_cam_.rot[std::size_t(i * 3 + 2)] * w[2];
        if (c3[2] > 1e-3) { tx = c3[0] / c3[2] * disp_cam_.fx; ty = c3[1] / c3[2] * disp_cam_.fy; }
      }
    }
    const bool fresh = now - nret_.seen > 0.25;
    nret_.seen = now;
    if (fresh) nret_.reveal = 0;
    nret_.reveal = std::min(1.0, nret_.reveal + std::max(0.0, dt) / 0.14);
    if (!nret_.init || nret_.aim != aim || fresh || std::hypot(nret_.x - tx, nret_.y - ty) > 70) {
      nret_ = NativeReticle{true, tx, ty, 0, 0, hw, hh, 0, 0, rh, rv, 0, 0, nret_.reveal, now, now, aim, 0x40ff40};
    } else {
      const double px = nret_.x, py = nret_.y;
      retSpring(nret_.x, nret_.vx, tx, aim ? 60.0 : 10.0, dt);
      retSpring(nret_.y, nret_.vy, ty, aim ? 60.0 : 10.0, dt);
      const double step = std::hypot(nret_.x - px, nret_.y - py), mx = 240.0 * 368.0 / 320.0 * std::clamp(dt, 0.0, 1.0 / 15.0);
      if (!aim && step > mx && mx > 0) { const double a = mx / step; nret_.x = px + (nret_.x - px) * a; nret_.y = py + (nret_.y - py) * a; nret_.vx *= a; nret_.vy *= a; }
      retSpring(nret_.hw, nret_.vhw, hw, 18.0, dt); retSpring(nret_.hh, nret_.vhh, hh, 18.0, dt);
      retSpring(nret_.rh, nret_.vrh, rh, 18.0, dt); retSpring(nret_.rv, nret_.vrv, rv, 18.0, dt);
    }

    std::vector<bool> drop(in.size(), false);
    for (const auto& l : mainL) drop[l.idx] = true;
    for (auto k : trail) drop[k] = true;
    Packets out;

    const int ddx = int(std::lround(nret_.x - cx)), ddy = int(std::lround(nret_.y - cy));
    auto moved = [&](std::uint32_t xy) { return (std::uint32_t((int(sy11(xy)) + ddy) & 0x7ff) << 16) | std::uint32_t((int(sx11(xy)) + ddx) & 0x7ff); };
    for (std::size_t k = 0; k < in.size(); ++k) {
      if (drop[k]) continue;
      auto q = in[k];
      const auto cmd = q.empty() ? 0u : q[0] >> 24;
      if (cmd >= 0x40 && cmd < 0x60 && (ddx || ddy)) {
        if (cmd & 0x08) { for (std::size_t i = 1; i < q.size(); i += (cmd & 0x10) ? 2 : 1) { if ((q[i] & 0xf000f000u) == 0x50005000u) break; q[i] = moved(q[i]); } }
        else { const bool gour = cmd & 0x10; if (q.size() >= (gour ? 4u : 3u)) { q[1] = moved(q[1]); q[gour ? 3 : 2] = moved(q[gour ? 3 : 2]); } }
      }
      out.push_back(std::move(q));
    }
    reticle_shift_x_ = nret_.x - cx; reticle_shift_y_ = nret_.y - cy;
    reticle_draw_ = true;
    if (static const bool dl = std::getenv("SF3N_MIRADBG") != nullptr; dl) {
      std::size_t hudGreen = 0;
      for (const auto& pk : hud_) if (!pk.empty() && (pk[0] >> 24) >= 0x40 && (pk[0] >> 24) < 0x60) ++hudGreen;
      std::fprintf(log, "MIRADBG in %zu main %zu estela %zu quedan %zu lineasHUD %zu centro %.0f %.0f caja %.0f %.0f rayos %.0f %.0f aim %d\n", in.size(), mainL.size(), trail.size(), out.size(), hudGreen, cx, cy, hw, hh, rh, rv, int(aim));
    }
    return out;
  }
  bool reticle_draw_ = false;
  double reticle_shift_x_ = 0, reticle_shift_y_ = 0;

  void drawNativeReticle() {
    if (!reticle_draw_) return;
    reticle_draw_ = false;
    HudMap m; m.ox = 192.0; m.oy = HudMap::center_y;
    const double x = nret_.x, y = nret_.y, hw = std::round(nret_.hw), hh = std::round(nret_.hh);
    const double e = nret_.reveal, slide = (1.0 - e * e) * 12.0;
    const double rh = std::max(0.0, std::round(nret_.rh)), rv = std::max(0.0, std::round(nret_.rv));
    const std::uint32_t c = nret_.col;
    auto L = [&](double ax, double ay, double bx, double by) { hudLineF(m.x(x + ax), m.y(y + ay), m.x(x + bx), m.y(y + by), c, c, false); };
    L(-hw, -hh, hw, -hh); L(-hw, hh, hw, hh); L(-hw, -hh, -hw, hh); L(hw, -hh, hw, hh);
    if (rh > 0) { L(-hw - slide - rh, 0, -hw - slide - 1, 0); L(hw + slide + 1, 0, hw + slide + rh, 0); }
    if (rv > 0) { L(0, -hh - slide - rv, 0, -hh - slide - 1); L(0, hh + slide + 1, 0, hh + slide + rv); }
    L(-1, 0, 1, 0);
  }

  std::uint64_t scope_seq_up_ = 0;
  std::vector<std::uint16_t> scope_group_;
  void drawScopePic() {
    auto& sp = scopePic();
    int x = 0, y = 0, w = 0, h = 0;
    {
      std::lock_guard<std::mutex> lk(sp.m);
      if (sp.seq == 0 || std::chrono::steady_clock::now() - sp.when > std::chrono::milliseconds(600) || sp.w <= 0 || sp.w > 256 || sp.h <= 0 || sp.h > 256) return;
      x = sp.x; y = sp.y; w = sp.w; h = sp.h;
      if (sp.seq != scope_seq_up_) {
        scope_seq_up_ = sp.seq;
        scope_group_.assign(256 * 256, 0);
        for (int yy = 0; yy < h; ++yy) std::memcpy(scope_group_.data() + std::size_t(yy) * 256, sp.pix.data() + std::size_t(yy) * std::size_t(w), std::size_t(w) * 2);
        std::vector<std::uint16_t> page(64 * 256);
        for (int k = 0; k < 4; ++k) {
          for (int yy = 0; yy < 256; ++yy) std::memcpy(page.data() + yy * 64, scope_group_.data() + std::size_t(yy) * 256 + std::size_t(k) * 64, 64 * 2);
          GR_UploadVRAMAliasPage(WeaponBank::kScopeAlias + k, page.data());
        }
      }
    }
    HudMap map;
    const float x0 = map.x(double(x)), x1 = map.x(double(x + w)), y0 = map.y(double(y)), y1 = map.y(double(y + h));
    POLY_FT4 p{};
    setPolyFT4(&p);
    setRGB0(&p, 128, 128, 128);
    setXY4(&p, x0, y0, x1, y0, x0, y1, x1, y1);
    setUV4(&p, 0, 0, u_char(w), 0, 0, u_char(h), u_char(w), u_char(h));
    p.tpage = WeaponBank::aliasTpage(WeaponBank::kScopeAlias, 2u << 7);
    p.clut = 0;
    DrawPrim(&p);
  }
  void drawHud() {
    if (!hud_ok_) return;
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    HudMap map;
    if (std::getenv("SF3N_HUDTEST")) {
      POLY_F4 q{}; setPolyF4(&q); setRGB0(&q, 255, 0, 0);
      setXY4(&q, 20.f, 20.f, 120.f, 20.f, 20.f, 60.f, 120.f, 60.f); DrawPrim(&q);
      TILE t{}; setTile(&t); setRGB0(&t, 0, 255, 0); t.x0 = 140.f; t.y0 = 20.f; t.w = 60.f; t.h = 40.f; DrawPrim(&t);
      LINE_F2 l{}; setLineF2(&l); setRGB0(&l, 255, 255, 0); l.x0 = 20.f; l.y0 = 80.f; l.x1 = 200.f; l.y1 = 100.f; DrawPrim(&l);
    }
    if (++hud_logs_ % 3000 == 1) {
      std::fprintf(log, "hud: %zu paquetes:", hud_.size());
      for (const auto& pk : hud_) std::fprintf(log, " %02x", pk.empty() ? 0 : pk[0] >> 24);
      std::fprintf(log, "\n");
    }
    const bool fresh = nowSeconds() - effects_time_ < 0.25;

    Packets hudPrompts;
    std::vector<HudFrac> hudFrac;
    {
      static const bool noInterp = std::getenv("SF3N_HUD_SIN_INTERP") != nullptr;
      const auto& H = hud_hist_;
      const double T = render_time_;
      int hi = -1;
      for (int k = 0; k < int(H.size()); ++k) if (H[std::size_t(k)].t_first <= T) hi = k;

      auto addSub = [&](const HudState& st, double wgt) {
        if (st.sub.empty() || st.sub.size() > hudPrompts.size()) return;
        if (hudFrac.size() < hudPrompts.size()) hudFrac.resize(hudPrompts.size(), HudFrac{});
        for (std::size_t i = 0; i < st.sub.size(); ++i) for (int k = 0; k < 8; ++k) hudFrac[i].d[std::size_t(k)] += float(st.sub[i].d[std::size_t(k)] * wgt);
      };
      hud_disp_wid_ = -1;
      if (noInterp || H.empty()) hudPrompts = pcPrompts(hud_);
      else if (hi < 0) { hudPrompts = pcPrompts(H.front().pk); hud_disp_wid_ = H.front().wid; if (hudPrompts.size() == H.front().pk.size()) addSub(H.front(), 1.0); }
      else if (std::size_t(hi) + 1 >= H.size() || T <= H[std::size_t(hi)].t_last || H[std::size_t(hi) + 1].t_first - H[std::size_t(hi)].t_last > 0.25) {
        hudPrompts = pcPrompts(H[std::size_t(hi)].pk); hud_disp_wid_ = H[std::size_t(hi)].wid;
        if (hudPrompts.size() == H[std::size_t(hi)].pk.size()) addSub(H[std::size_t(hi)], 1.0);
      } else {
        const auto& a = H[std::size_t(hi)]; const auto& b = H[std::size_t(hi) + 1];
        const double t = std::clamp((T - a.t_last) / std::max(1e-6, b.t_first - a.t_last), 0.0, 1.0);
        hud_disp_wid_ = t < 0.5 ? a.wid : b.wid;
        const auto pa = pcPrompts(a.pk), pb = pcPrompts(b.pk);
        hudPrompts = hudLerp(pa, pb, t, hudFrac);
        if (pa.size() == a.pk.size() && pb.size() == b.pk.size()) {
          if (pa.size() == pb.size()) {

            for (std::size_t i = 0; i < pb.size() && i < hudFrac.size(); ++i) {
              bool lerped = false; for (int k = 0; k < 8; ++k) lerped = lerped || hudFrac[i].d[std::size_t(k)] != 0.f;
              const bool samePk = pa[i] == pb[i];
              const double wa = (lerped || samePk) ? 1.0 - t : 0.0, wb = (lerped || samePk) ? t : 1.0;
              for (int k = 0; k < 8; ++k) {
                const float sa = i < a.sub.size() ? a.sub[i].d[std::size_t(k)] : 0.f, sb = i < b.sub.size() ? b.sub[i].d[std::size_t(k)] : 0.f;
                hudFrac[i].d[std::size_t(k)] += float(sa * wa + sb * wb);
              }
            }
          } else addSub(b, 1.0);
        }
      }
    }
    if (static const bool rd = std::getenv("SF3N_RADARDRAW") != nullptr; rd)
      for (std::size_t i = 0; i < hudPrompts.size(); ++i) {
        const auto& p = hudPrompts[i];
        if (p.size() == 6 && (p[0] >> 24) == 0x30 && (p[0] & 0xffffff) == 0xf37a5e) {
          const float fx = i < hudFrac.size() ? hudFrac[i].d[0] : 0.f;
          std::fprintf(log, "RADARDRAW t %.4f punta x %.3f (entero %d + %.3f) sub %d |", render_time_, double(std::int16_t(p[1] & 0xffff)) + fx, int(std::int16_t(p[1] & 0xffff)), double(fx), int(hudFrac.size()));
          for (int k = 0; k < 3; ++k) { const auto w = p[std::size_t(2 * k + 1)]; const float a = i < hudFrac.size() ? hudFrac[i].d[std::size_t(2 * k)] : 0.f, b = i < hudFrac.size() ? hudFrac[i].d[std::size_t(2 * k + 1)] : 0.f; std::fprintf(log, " %d%+.2f,%d%+.2f", int(std::int16_t(w & 0xffff)), double(a), int(std::int16_t(w >> 16)), double(b)); }
          std::fprintf(log, "\n");
        }
      }

    std::vector<std::uint8_t> hudGroup(hudPrompts.size(), 0);

    std::vector<std::uint8_t> inBand(hudPrompts.size(), 0);
    {
      HudMap gm;
      bool band = false;
      double bandY0 = 0, bandY1 = 0;
      for (const auto& pk : hudPrompts) {
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        if (c == 0xe3) { e3Area(gm, pk[0]); continue; }
        if (c == 0xe5) { gm.ox = double(std::int32_t(pk[0] << 21) >> 21); gm.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
        if (c >= 0x40 && c < 0x50 && !(c & 8) && pk.size() >= 3) {
          const double a = gm.cx(sx11(pk[1])), b = gm.cx(sx11(pk[2])), y = gm.cy(sy11(pk[1]));
          if (std::min(a, b) <= -190 && std::max(a, b) >= 190 && y > -110 && y < -50) {
            if (!band) { bandY0 = bandY1 = y; band = true; }
            bandY0 = std::min(bandY0, y); bandY1 = std::max(bandY1, y);
          }
        }
      }
      static const bool keepBand = std::getenv("SF3N_FRANJA_JUEGO") != nullptr;

      double bandSeam = -192, bandPeriod = 384;
      std::vector<std::pair<double, double>> bandIcons;
      struct KeepIcon { std::size_t i; std::uint32_t e1, e3, e5; };
      std::vector<KeepIcon> keepIcons;
      std::uint32_t lastE1 = 0xe1000200u, lastE3 = 0xe3000000u, lastE5 = 0xe50000c0u;
      if (band && !keepBand) {
        std::vector<std::pair<double, double>> icons;
        std::vector<std::pair<double, std::uint64_t>> pieces;
        HudMap g2;
        for (const auto& pk : hudPrompts) {
          if (pk.empty()) continue;
          const auto c = pk[0] >> 24;
          if (c == 0xe3) { e3Area(g2, pk[0]); continue; }
          if (c == 0xe5) { g2.ox = double(std::int32_t(pk[0] << 21) >> 21); g2.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
          if (c >= 0x64 && c < 0x68 && pk.size() >= 4) {
            const double y = g2.cy(sy11(pk[1]));
            if (y < bandY0 - 4 || y > bandY1 + 4) continue;
            const double xa = g2.cx(sx11(pk[1]));
            icons.push_back({xa, xa + double(pk[3] & 0x3ff)});
            pieces.push_back({xa, (std::uint64_t(pk[2]) << 32) | pk[3]});
          }
        }

        std::sort(icons.begin(), icons.end());
        std::vector<std::pair<double, double>> merged;
        for (const auto& iv : icons) {
          if (!merged.empty() && iv.first <= merged.back().second + 2) merged.back().second = std::max(merged.back().second, iv.second);
          else merged.push_back(iv);
        }
        icons.swap(merged);

        {
          static const bool viejo = std::getenv("SF3N_CARRUSEL_VIEJO") != nullptr;
          double mn = 1e9, mx = -1e9;
          for (const auto& pc : pieces) { mn = std::min(mn, pc.first); mx = std::max(mx, pc.first); }
          std::vector<double> cands;
          for (std::size_t a = 0; a < pieces.size(); ++a)
            for (std::size_t b = 0; b < pieces.size(); ++b)
              if (pieces[a].second == pieces[b].second) { const double d = pieces[b].first - pieces[a].first; if (d > 40) cands.push_back(d); }
          std::sort(cands.begin(), cands.end());
          bandPeriod = 1e9;
          for (const double d : cands) {
            if (viejo) { bandPeriod = d; break; }
            bool ok = true; int matched = 0;
            for (const auto& pc : pieces) {
              if (pc.first + d > mx + 0.5) continue;
              bool f = false;
              for (const auto& q : pieces) if (q.second == pc.second && std::fabs(q.first - (pc.first + d)) <= 1.0) { f = true; break; }
              if (!f) { ok = false; break; }
              ++matched;
            }
            if (ok && matched >= 1) { bandPeriod = d; break; }
          }
          if (bandPeriod > 1e8) bandPeriod = 0;
        }
        bandIcons = icons;
        for (int d = 0; d <= 60 && bandPeriod > 0; d += 2) {
          bool found = false;
          for (const double cand : {-192.0 - d, -192.0 + d}) {
            bool ok = true;
            for (const auto& [a, b] : icons) if ((cand > a && cand < b) || (cand + bandPeriod > a && cand + bandPeriod < b)) { ok = false; break; }
            if (ok) { bandSeam = cand; found = true; break; }
          }
          if (found) break;
        }
      }
      if (band && !keepBand) {
        gm = HudMap{};
        auto setX = [&](std::uint32_t& w, double target) {
          const double raw = sx11(w) + (target - gm.cx(sx11(w)));
          w = (w & 0xffff0000u) | (std::uint32_t(std::int32_t(std::lround(raw))) & 0x7ffu);
        };
        for (std::size_t i = 0; i < hudPrompts.size(); ++i) {
          auto& pk = hudPrompts[i];
          if (pk.empty()) continue;
          const auto c = pk[0] >> 24;
          if (c == 0xe3) { lastE3 = pk[0]; e3Area(gm, pk[0]); continue; }
          if (c == 0xe5) { lastE5 = pk[0]; gm.ox = double(std::int32_t(pk[0] << 21) >> 21); gm.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
          if (c == 0xe1) { lastE1 = pk[0]; continue; }
          if (c < 0x20 || c >= 0x80) continue;
          std::array<int, 4> slot{}, cs{}; int wh = -1;
          const int n = hudSlots(pk, slot, wh, cs);
          if (n == 0) continue;
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (int k = 0; k < n; ++k) {
            const auto w = pk[std::size_t(slot[std::size_t(k)])];
            x0 = std::min(x0, gm.cx(sx11(w))); x1 = std::max(x1, gm.cx(sx11(w))); y0 = std::min(y0, gm.cy(sy11(w))); y1 = std::max(y1, gm.cy(sy11(w)));
          }
          if (y0 < bandY0 - 4 || y1 > bandY1 + 4) continue;
          const bool span = x0 <= -190 && x1 >= 190, icon = c >= 0x60 && (c & 4) && wh >= 0 && ((pk[std::size_t(wh)] >> 16) & 0x1ff) >= 10;
          const bool tall = c < 0x40 && y1 - y0 >= 18;
          if (!span && !icon && !tall) continue;
          inBand[i] = 1;
          if (x0 <= -190 && x1 >= 190 && !(c >= 0x60)) {
            for (int k = 0; k < n; ++k) {
              auto& w = pk[std::size_t(slot[std::size_t(k)])];
              const double x = gm.cx(sx11(w));
              if (x <= -190) setX(w, -500); else if (x >= 190) setX(w, 500);
            }
            continue;
          }
          if (c >= 0x60 && (c & 4) && wh >= 0 && std::size_t(wh) < pk.size()) {

            const double xa = gm.cx(sx11(pk[1]));
            double start = xa;
            for (const auto& [a, b] : bandIcons) if (xa >= a - 0.5 && xa < b) { start = a; break; }
            if (bandPeriod <= 0) continue;
            if (start < bandSeam - 0.5 || start >= bandSeam + bandPeriod - 0.5) { pk.clear(); continue; }
            keepIcons.push_back({i, lastE1, lastE3, lastE5});
          }
        }
        const double vis = 192.0 * wideFactor() * (368.0 / 384.0) + 40.0;
        for (const auto& [i, e1, e3, e5] : keepIcons) {
          for (int k = -4; k <= 4; ++k) {
            if (k == 0) continue;
            auto q = hudPrompts[i];
            if (q.size() < 4) continue;
            HudMap g3; g3.ox = double(std::int32_t(e5 << 21) >> 21); g3.ax = double(e3 & 0x3ff);
            const double xa = g3.cx(sx11(q[1])) + k * bandPeriod, w = double(q[3] & 0x3ff);
            if (xa + w < -vis || xa > vis) continue;
            const double raw = sx11(q[1]) + k * bandPeriod;
            if (raw < -1023 || raw > 1023) continue;
            q[1] = (q[1] & 0xffff0000u) | (std::uint32_t(std::int32_t(std::lround(raw))) & 0x7ffu);
            const HudFrac f = i < hudFrac.size() ? hudFrac[i] : HudFrac{};
            hudPrompts.push_back({e3}); hudPrompts.push_back({e5}); hudPrompts.push_back({e1});
            hudPrompts.push_back(std::move(q));
            if (!hudFrac.empty()) { for (int z = 0; z < 3; ++z) hudFrac.push_back(HudFrac{}); hudFrac.push_back(f); }
          }
        }
        hudGroup.resize(hudPrompts.size(), 0);
        inBand.resize(hudPrompts.size(), 1);
      }
    }
    if (hud_scale_ != 100 || hud_edges_) {
      HudMap gm;
      double iconX0 = 1e9, iconX1 = -1e9, iconY = 1e9;
      for (std::size_t i = 0; i < hudPrompts.size(); ++i) {
        const auto& pk = hudPrompts[i];
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        if (c == 0xe3) { e3Area(gm, pk[0]); continue; }
        if (c == 0xe5) { gm.ox = double(std::int32_t(pk[0] << 21) >> 21); gm.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
        if (c >= 0x60 && c < 0x80 && (c & 4) && ((c >> 3) & 3) == 0 && pk.size() >= 4) {
          const double w = double(pk[3] & 0x3ff), h = double((pk[3] >> 16) & 0x1ff);
          const double x = gm.cx(sx11(pk[1])), y = gm.cy(sy11(pk[1]));
          if (w >= 16 && h >= 12 && x > 20 && y > 40) { iconX0 = std::min(iconX0, x); iconX1 = std::max(iconX1, x + w); iconY = std::min(iconY, y); }
        }
      }
      gm = HudMap{};
      struct HudLetter { double y, x0, x1; std::size_t i; };
      std::vector<HudLetter> rows;
      for (std::size_t i = 0; i < hudPrompts.size(); ++i) {
        const auto& pk = hudPrompts[i];
        if (pk.empty()) continue;
        const auto c = pk[0] >> 24;
        if (c == 0xe3) { e3Area(gm, pk[0]); continue; }
        if (c == 0xe5) { gm.ox = double(std::int32_t(pk[0] << 21) >> 21); gm.oy = double(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
        if (c < 0x20 || c >= 0x80 || pk.size() < 2) continue;
        std::array<int, 4> slot{}, cs{}; int wh = -1;
        const int n = hudSlots(pk, slot, wh, cs);
        if (n == 0) continue;
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (int k = 0; k < n; ++k) {
          const auto w = pk[std::size_t(slot[std::size_t(k)])];
          x0 = std::min(x0, gm.cx(sx11(w))); x1 = std::max(x1, gm.cx(sx11(w))); y0 = std::min(y0, gm.cy(sy11(w))); y1 = std::max(y1, gm.cy(sy11(w)));
        }
        if (wh >= 0) { x1 += double(pk[std::size_t(wh)] & 0x3ff); y1 += double((pk[std::size_t(wh)] >> 16) & 0x1ff); }
        if (x0 < -1000 || y0 < -1000) continue;
        if (x1 - x0 > 80 || y1 - y0 > 80) continue;
        if (inBand[i]) continue;

        if (static const bool esq = std::getenv("SF3N_ESQUINAS_GRUPO") == nullptr; esq && c >= 0x20 && c <= 0x23) {
          const auto col = pk[0] & 0xffffffu;
          if ((col & 0xff) == ((col >> 8) & 0xff) && (col & 0xff) == ((col >> 16) & 0xff)) continue;
        }
        if (x0 >= -186 && x1 <= -95 && y0 >= -112 && y1 <= -50) hudGroup[i] = 1;

        else if (x0 >= -192 && x1 <= -98 && y0 >= 50 && y1 <= 116) hudGroup[i] = 2;
        else if (iconX1 > iconX0 && x0 >= iconX0 - 40 && x1 <= iconX1 + 16 && y0 >= iconY - 12 && y1 <= iconY + 40) hudGroup[i] = 3;
        if (y1 - y0 <= 10 && x1 - x0 <= 10) rows.push_back({y0, x0, x1, i});
      }

      static const bool filaEntera = std::getenv("SF3N_HUD_FILA") != nullptr;
      std::sort(rows.begin(), rows.end(), [](const HudLetter& a, const HudLetter& b) { return a.x0 < b.x0; });
      std::vector<int> run(rows.size(), -1);
      int nruns = 0;
      for (std::size_t a = 0; a < rows.size(); ++a) {
        if (run[a] >= 0) continue;
        run[a] = nruns;
        double right = rows[a].x1;
        for (std::size_t b = a + 1; b < rows.size(); ++b) {
          if (run[b] >= 0 || std::fabs(rows[b].y - rows[a].y) > 1.0) continue;
          if (!filaEntera && rows[b].x0 > right + 12.0) break;
          run[b] = nruns; right = std::max(right, rows[b].x1);
        }
        ++nruns;
      }
      std::vector<char> runOut(std::size_t(nruns), 0);
      for (std::size_t a = 0; a < rows.size(); ++a) if (!hudGroup[rows[a].i]) runOut[std::size_t(run[a])] = 1;
      for (std::size_t a = 0; a < rows.size(); ++a) if (runOut[std::size_t(run[a])]) hudGroup[rows[a].i] = 0;
    }

    if (inMission() && !ram_.empty() && weapon_cur_addr_) {
      if (icon_mis_ != &mis()) { icon_mis_ = &mis(); loadWeaponIcons(); }
      const int id = shownWeaponId();
      if (const std::string nmId = weaponIdName(id); !nmId.empty())
        if (auto it = icon_slots_.find(nmId); it != icon_slots_.end() && it->second.explicitCfg)
          for (std::size_t i = 0; i < hudPrompts.size(); ++i) {
            auto& pk = hudPrompts[i];
            if (pk.size() < 4) continue;
            const auto c = pk[0] >> 24;
            if ((c & ~3u) != 0x64 || (pk[2] >> 16) != 0x78f0 || (pk[3] & 0xffff) < 24) continue;
            const int x = int(std::int16_t(pk[1] & 0xffff)), y = int(std::int16_t(pk[1] >> 16));
            if (x < 60 || y < 60) continue;
            pk[0] = 0;
          }
    }
    weaponBlockFill(hudPrompts, hudGroup, inBand);

    const Packets overlayNow = fresh && std::getenv("SF3N_MIRA_JUEGO") == nullptr ? nativeReticle(overlay_) : overlay_;
    const double wf = wideFactor();
    const double edge = hud_edges_ ? (wf - 1.0) * 192.0 : 0.0, sc = hud_scale_ / 100.0;
    for (int pass = fresh ? 0 : 1; pass < 2; ++pass)
    for (std::size_t pi = 0; pi < (pass == 0 ? overlayNow : hudPrompts).size(); ++pi) {
      const auto& pk = (pass == 0 ? overlayNow : hudPrompts)[pi];
      if (pk.empty()) continue;
      hud_frac_ = pass == 1 && pi < hudFrac.size() ? &hudFrac[pi] : nullptr;
      const int grp = pass == 1 && pi < hudGroup.size() ? hudGroup[pi] : 0;
      map.gsc = grp ? sc : 1.0;
      map.gax = grp == 3 ? 384.0 : 0.0;
      map.gay = grp >= 2 ? 240.0 : 0.0;
      map.gsx = grp == 0 ? 0.0 : grp == 3 ? edge : -edge;
      const auto w0 = pk[0];
      const auto cmd = w0 >> 24;
      if (cmd == 0xe1) {
        hud_tpage_ = w0 & 0x1ff;
        DR_TPAGE t{};
        SetDrawTPage(&t, 1, 0, int(w0 & 0x1ff));
        DrawPrim(&t);
        continue;
      }
      if (cmd == 0xe3) {
        e3Area(map, w0);
        sub_area_ = (w0 & 0x3ff) != 0;
        continue;
      }
      if (cmd == 0xe5) {
        map.ox = double(std::int32_t(w0 << 21) >> 21);
        map.oy = double(std::int32_t((w0 >> 11) << 21) >> 21);
        continue;
      }
      if (cmd == 0xfd) { hudCircle(pk, map); continue; }
      if (cmd >= 0x20 && cmd < 0x40) { hudPolygon(pk, map); continue; }
      if (cmd >= 0x40 && cmd < 0x60 && !(cmd & 0x08)) {
        if (pass == 0 && reticle_.on && !reticle_draw_) {
          auto q = pk;
          const bool gour = cmd & 0x10;
          if (q.size() >= (gour ? 4u : 3u)) { q[1] = reticlePoint(q[1], map); auto& b = q[gour ? 3 : 2]; b = reticlePoint(b, map); }
          hudLine(q, map);
        } else hudLine(pk, map);
        continue;
      }
      if (cmd >= 0x60 && cmd < 0x80) {

        if (xray_drawn_ && cmd < 0x64 && pk.size() >= 3 && (pk[0] & 0xffffff) == 0 && (pk[2] & 0xffff) == 160 && (pk[2] >> 16) == 80) continue;
        if (static const bool keepNum = xrayCfg("rayosx_numero", "SF3N_RAYOSX_NUMERO", 0) != 0; sub_area_ && cmd >= 0x64 && !keepNum && xray_drawn_) continue;
        if (sub_area_ && cmd < 0x64) {
          static const int adjX = xrayCfg("rayosx_ajuste_x", "SF3N_RAYOSX_X", 0), adjY = xrayCfg("rayosx_ajuste_y", "SF3N_RAYOSX_Y", 0);
          HudMap m2 = map; m2.ox += 192.0 + adjX; m2.oy += 120.0 + adjY;
          hudRect(pk, m2); continue;
        }
        hudRect(pk, map); continue;
      }
    }
    hud_frac_ = nullptr;

    {
      HudMap wm = map; wm.gsc = sc; wm.gax = 384.0; wm.gay = 240.0; wm.gsx = edge;

      if (!std::getenv("SF3N_ICONO_E5_VIEJO")) { wm.ox = 192.0; wm.oy = 120.0; wm.ax = wm.ay = 0.0; wm.dw = 368.0; }
      drawWeaponIcon(hudPrompts, wm);
      HudMap cm = map; cm.gsc = 1.0; cm.gax = 0.0; cm.gay = 0.0; cm.gsx = 0.0;
      drawCarouselIcons(hudPrompts, cm);
    }
    GR_SetBlendMode(BM_NONE);
    drawNativeReticle();
  }

  int wblock_last_vis_ = -1, wblock_hold_id_ = -1;
  bool portIconWeapon(int id) const { const auto n = weaponIdName(id); return !n.empty() && icon_slots_.count(n) != 0; }
  int hud_disp_wid_ = -1;
  int shownWeaponId() const {
    static const bool ram = std::getenv("SF3N_ICONO_RAM") != nullptr;
    if (!ram && hud_disp_wid_ >= 0) return hud_disp_wid_;
    return ramByte(weapon_cur_addr_);
  }
  struct WeaponBlockMem { std::map<int, Packets> by_id; };
  std::map<std::string, WeaponBlockMem> wblock_mem_;
  int wblock_id_ = -1;
  double wblock_since_ = 0;
  static bool weaponBlockSprite(const std::vector<std::uint32_t>& pk, bool& isIcon) {
    isIcon = false;
    if (pk.size() < 3) return false;
    const auto c = pk[0] >> 24;
    if (c < 0x64 || c >= 0x80 || !(c & 4)) return false;
    const int x = int(std::int16_t(pk[1] & 0xffff)), y = int(std::int16_t(pk[1] >> 16));
    if (x < 60 || y < 60) return false;

    if ((c & ~3u) == 0x64 && (pk[2] >> 16) == 0x78f0 && pk.size() >= 4 && !((pk[3] & 0xffff) <= 8 && (pk[3] >> 16) <= 8)) { isIcon = true; return true; }

    if (x >= 90 && x <= 175 && y >= 85 && y <= 105 && ((c & 0x18) == 0x08 || (pk.size() >= 4 && (pk[3] & 0xffff) <= 8 && (pk[3] >> 16) <= 8))) return true;
    return false;
  }
  void weaponBlockFill(Packets& hud, std::vector<std::uint8_t>& group, const std::vector<std::uint8_t>& inBand) {
    static const bool on = [&] { const char* e = std::getenv("SF3N_HUD_ARMA_HUECOS"); if (e) return std::atoi(e) != 0; return xrayCfg("hud_arma_huecos", "SF3N_HUD_ARMA_HUECOS_INI", 1) != 0; }();
    if (!on || !inMission() || ram_.empty() || !weapon_cur_addr_ || !mission_) return;
    const int id = shownWeaponId();
    const double now = nowSeconds();
    wblock_hold_id_ = -1;
    if (icon_mis_ != &mis()) { icon_mis_ = &mis(); loadWeaponIcons(); }
    if (id != wblock_id_) { wblock_id_ = id; wblock_since_ = now; }

    Packets blk; bool icon = false, digits = false, band = false; int others = 0;
    std::uint32_t e1 = 0xe1000200u, e3 = 0xe3000000u, e5 = 0xe5000000u;
    for (std::size_t i = 0; i < hud.size(); ++i) {
      const auto& pk = hud[i];
      if (pk.empty() || pk[0] == 0) continue;
      const auto c = pk[0] >> 24;
      if (c == 0xe1) { e1 = pk[0]; continue; }
      if (c == 0xe3) { e3 = pk[0]; continue; }
      if (c == 0xe5) { e5 = pk[0]; continue; }
      if (c >= 0xe0) continue;
      if (i < inBand.size() && inBand[i]) { band = true; continue; }
      bool isIcon = false;
      if (weaponBlockSprite(pk, isIcon)) { blk.push_back({e1}); blk.push_back({e3}); blk.push_back({e5}); blk.push_back(pk); (isIcon ? icon : digits) = true; }
      else ++others;
    }
    if (static const bool lg0 = std::getenv("SF3N_HUDARMALOG") != nullptr; lg0) { static double lt = 0; if (now - lt > 0.5) { lt = now; std::fprintf(log, "HUDARMA arma %d icono %d cifras %d otros %d carrusel %d (%.2f s)\n", id, int(icon), int(digits), others, int(band), now - wblock_since_); } }
    if (static const bool lg1 = std::getenv("SF3N_HUDARMALOG2") != nullptr; lg1) {
      std::fprintf(log, "HUDARMA2 cuadro %llu campo %llu arma %d (ram %d) mantiene %d ultimo %d t %.3f:", (unsigned long long)render_frame_, (unsigned long long)field_count_, id, int(ramByte(weapon_cur_addr_)), wblock_hold_id_, wblock_last_vis_, now - wblock_since_);
      for (const auto& pk : hud) { if (pk.size() < 3) continue; const auto c = pk[0] >> 24; if (c < 0x60 || c >= 0x80) continue; const int x = int(std::int16_t(pk[1] & 0xffff)), y = int(std::int16_t(pk[1] >> 16)); if (x < 60 || y < 60) continue;
        std::fprintf(log, " [%02x %d,%d clut %04x %ux%u]", c, x, y, unsigned(pk.size() >= 3 ? pk[2] >> 16 : 0), unsigned(pk.size() >= 4 ? pk[3] & 0xffff : 0), unsigned(pk.size() >= 4 ? pk[3] >> 16 : 0)); }
      std::fprintf(log, "\n");
    }
    if (band) return;
    auto& mem = wblock_mem_[mission_->name].by_id;
    static const bool lg = std::getenv("SF3N_HUDARMALOG") != nullptr;

    if (icon && now - wblock_since_ >= 0.15) {
      if (lg && !mem.count(id)) std::fprintf(log, "HUDARMA aprende arma %d (%zu paquetes)\n", id, blk.size() / 4);
      mem[id] = std::move(blk);
    }
    if (icon || portIconWeapon(id)) { wblock_last_vis_ = id; return; }
    if (others < 4 || now - wblock_since_ > 1.5) return;
    auto it = mem.find(id);
    if (it == mem.end()) {

      if (wblock_last_vis_ < 0 || wblock_last_vis_ == id) return;
      if (portIconWeapon(wblock_last_vis_)) { wblock_hold_id_ = wblock_last_vis_; if (lg) std::fprintf(log, "HUDARMA hueco arma %d: se mantiene el icono propio del %d\n", id, wblock_last_vis_); return; }
      it = mem.find(wblock_last_vis_);
      if (it == mem.end()) return;
      if (lg) std::fprintf(log, "HUDARMA hueco arma %d: se mantiene el icono del %d\n", id, wblock_last_vis_);
    } else wblock_last_vis_ = id;
    std::size_t added = 0;
    for (std::size_t k = 0; k + 3 < it->second.size(); k += 4) {
      bool isIcon = false; weaponBlockSprite(it->second[k + 3], isIcon);
      if (!isIcon && digits) continue;
      for (int j = 0; j < 4; ++j) { hud.push_back(it->second[k + std::size_t(j)]); group.push_back(3); }
      ++added;
    }
    if (lg && added) std::fprintf(log, "HUDARMA hueco arma %d: %zu paquetes guardados (%.2f s tras el cambio)\n", id, added, now - wblock_since_);
  }

  static constexpr int kIconAlias = 116;
  struct IconSlot { int u = 0, v = 0, w = 0, h = 0; bool explicitCfg = false; };
  std::map<std::string, IconSlot> icon_slots_;
  const void* icon_mis_ = nullptr;
  void loadWeaponIcons() {
    icon_slots_.clear();
    std::vector<std::uint16_t> atlas(256 * 256, 0);
    int slot = 0;
    const std::string dir = dataDir() + "armas/";
    for (const auto& name : weaponTableNames()) {
      const auto* wc = weaponConfigFor(name);
      std::string file = wc && !wc->icono.empty() ? dir + wc->icono : dir + "iconos/" + name + ".bmp";
      SDL_Surface* raw = SDL_LoadBMP(file.c_str());
      if (!raw) { if (wc && !wc->icono.empty() && log) std::fprintf(log, "armas: no se pudo leer el icono %s (%s)\n", file.c_str(), SDL_GetError()); continue; }
      SDL_Surface* sfc = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
      SDL_FreeSurface(raw);
      if (!sfc) continue;
      if (slot >= 16) { SDL_FreeSurface(sfc); break; }
      const int sw = sfc->w, sh = sfc->h;

      const double k = std::min({1.0, 128.0 / sw, 32.0 / sh});
      const int w = std::max(1, int(sw * k)), h = std::max(1, int(sh * k));
      const int u0 = (slot % 2) * 128, v0 = (slot / 2) * 32;
      SDL_LockSurface(sfc);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const int sx = std::min(sw - 1, int(x / k)), sy = std::min(sh - 1, int(y / k));
          const auto* px = static_cast<const std::uint8_t*>(sfc->pixels) + sy * sfc->pitch + sx * 4;
          const int r = px[0], g = px[1], b = px[2], a = px[3];
          std::uint16_t c = 0;
          if (a >= 128 && (r | g | b) != 0) { c = std::uint16_t((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)); if (c == 0) c = 1; }
          atlas[std::size_t((v0 + y) * 256 + u0 + x)] = c;
        }
      SDL_UnlockSurface(sfc);
      SDL_FreeSurface(sfc);
      icon_slots_[name] = IconSlot{u0, v0, w, h, wc && !wc->icono.empty()};
      ++slot;
    }
    std::vector<std::uint16_t> page(64 * 256);
    for (int kq = 0; kq < 4; ++kq) {
      for (int y = 0; y < 256; ++y) std::memcpy(page.data() + y * 64, atlas.data() + std::size_t(y) * 256 + std::size_t(kq) * 64, 64 * 2);
      GR_UploadVRAMAliasPage(kIconAlias + kq, page.data());
    }
    if (log) std::fprintf(log, "armas: %zu iconos propios en armas/iconos\n", icon_slots_.size());
  }

  void drawCarouselIcons(const Packets& hud, const HudMap& cm) {
    if (!inMission() || ram_.empty() || !weapon_cur_addr_ || std::getenv("SF3N_SIN_ICONOS")) return;
    if (icon_slots_.empty()) return;
    struct Grp { double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; };
    std::vector<Grp> groups;
    int tp = 0;
    for (const auto& pk : hud) {
      if (pk.empty()) continue;
      const auto c = pk[0] >> 24;
      if (c == 0xe1) { tp = int(pk[0] & 0x1ff); continue; }
      if ((c & ~3u) != 0x64 || pk.size() < 4 || (pk[2] >> 16) != 0x78f0) continue;
      if ((tp & 0x1e) != 0x0c) continue;
      const double x = double(std::int16_t(pk[1] & 0xffff)), y = double(std::int16_t(pk[1] >> 16));
      const double w = double(pk[3] & 0xffff), h = double(pk[3] >> 16);
      if (y < -110 || y > -60) continue;

      if (!groups.empty() && std::fabs(groups.back().x1 - x) < 1.5 && std::fabs(groups.back().y0 - y) < 8) {
        auto& g = groups.back(); g.x1 = std::max(g.x1, x + w); g.y1 = std::max(g.y1, y + h); g.y0 = std::min(g.y0, y);
      } else groups.push_back({x, x + w, y, y + h});
    }

    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Grp& g) { return g.x1 - g.x0 < 16 || g.y1 - g.y0 < 6; }), groups.end());
    if (static const bool cl = std::getenv("SF3N_CARRLOG") != nullptr; cl) {
      std::fprintf(log, "CARR id %d grupos", ramByte(weapon_cur_addr_));
      for (const auto& g : groups) std::fprintf(log, " [%.0f..%.0f %.0f..%.0f]", g.x0, g.x1, g.y0, g.y1);
      std::fprintf(log, "\n");
    }
    if (groups.size() < 1) return;

    const double gc = (groups[0].x0 + groups[0].x1) * 0.5;
    const double scroll = gc - std::round(gc / 96.0) * 96.0;

    std::vector<int> owned;
    for (int id = 1; id <= 39; ++id) {
      if (weaponIdName(id).empty()) continue;
      const std::uint32_t a = 0x8012D7D8u + 2u * std::uint32_t(id / 16);
      const int m = ramByte(a) | (ramByte(a + 1) << 8);
      if (m & (1 << (id % 16))) owned.push_back(id);
    }
    const int cur = ramByte(weapon_cur_addr_);
    const auto ci = std::find(owned.begin(), owned.end(), cur);
    if (ci == owned.end()) return;
    const int centerIdx = int(ci - owned.begin());
    for (int k = -4; k <= 4; ++k) {
      const int idx = centerIdx + k;
      if (idx < 0 || idx >= int(owned.size())) continue;
      const int id = owned[std::size_t(idx)];
      const auto it = icon_slots_.find(weaponIdName(id));
      if (it == icon_slots_.end()) continue;
      const double cx = k * 96.0 + scroll, cy = -82.0;
      if (cx < -400 || cx > 400) continue;
      bool taken = false;
      for (const auto& g : groups) if (cx > g.x0 - 10 && cx < g.x1 + 10) { taken = true; break; }
      if (taken && !it->second.explicitCfg) continue;
      const auto& S = it->second;
      const double kk = std::min({1.0, 84.0 / S.w, 24.0 / S.h});
      const double w = S.w * kk, h = S.h * kk;
      POLY_FT4 p{};
      setPolyFT4(&p);
      setRGB0(&p, 128, 128, 128);
      const float x0 = cm.x(cx - w / 2), x1 = cm.x(cx + w / 2), y0 = cm.y(cy - h / 2), y1 = cm.y(cy + h / 2);
      setXY4(&p, x0, y0, x1, y0, x0, y1, x1, y1);
      setUV4(&p, u_char(S.u), u_char(S.v), u_char(S.u + S.w - 1), u_char(S.v), u_char(S.u), u_char(S.v + S.h - 1), u_char(S.u + S.w - 1), u_char(S.v + S.h - 1));
      p.tpage = WeaponBank::aliasTpage(kIconAlias, std::uint16_t(2u << 7));
      p.clut = 0;
      DrawPrim(&p);
    }
  }
  void drawWeaponIcon(Packets& hud, const HudMap& wm) {
    if (!inMission() || ram_.empty() || !weapon_cur_addr_ || std::getenv("SF3N_SIN_ICONOS")) return;
    if (icon_mis_ != &mis()) { icon_mis_ = &mis(); loadWeaponIcons(); }
    const int id = wblock_hold_id_ >= 0 ? wblock_hold_id_ : shownWeaponId();
    const std::string nmId = weaponIdName(id);
    if (nmId.empty()) return;
    const auto it = icon_slots_.find(nmId);
    if (it == icon_slots_.end()) return;

    bool gameIcon = false, gameAmmo = false;
    int ox = 0, oy = 0;
    for (const auto& pk : hud) {
      if (pk.empty()) continue;
      const auto c = pk[0] >> 24;
      if (c == 0xe5) { ox = int(std::int32_t(pk[0] << 21) >> 21); oy = int(std::int32_t((pk[0] >> 11) << 21) >> 21); continue; }
      if (c < 0x64 || c >= 0x68 || pk.size() < 3) continue;
      const int x = int(std::int16_t(pk[1] & 0xffff)), y = int(std::int16_t(pk[1] >> 16));
      if (x < 60 || y < 60) continue;
      if ((c & ~3u) == 0x64 && (pk[2] >> 16) == 0x78f0 && pk.size() >= 4 && !((pk[3] & 0xffff) <= 8 && (pk[3] >> 16) <= 8)) gameIcon = true;
      else if (x >= 90 && x <= 175 && y >= 85 && y <= 105 && ((c & 0x18) == 0x08 || (pk.size() >= 4 && (pk[3] & 0xffff) <= 8 && (pk[3] >> 16) <= 8))) gameAmmo = true;
    }
    (void)ox; (void)oy;
    if (static const bool il0 = std::getenv("SF3N_ICONOLOG") != nullptr; il0 && gameIcon && !it->second.explicitCfg) std::fprintf(log, "ICONO cuadro %llu id %d: el juego ya pinta un icono, no se dibuja\n", (unsigned long long)render_frame_, id);
    if (gameIcon && !it->second.explicitCfg) return;
    if (static const bool il = std::getenv("SF3N_ICONOLOG") != nullptr; il) std::fprintf(log, "ICONO cuadro %llu id %d icono_juego %d municion_juego %d\n", (unsigned long long)render_frame_, id, int(gameIcon), int(gameAmmo));
    const auto& S = it->second;

    const double gw = S.w, gh = std::min(32, S.h);
    const double gx1 = 170.0, gy1 = 102.0, gx0 = gx1 - gw, gy0 = gy1 - gh;
    POLY_FT4 p{};
    setPolyFT4(&p);
    setRGB0(&p, 128, 128, 128);
    setXY4(&p, wm.x(gx0), wm.y(gy0), wm.x(gx1), wm.y(gy0), wm.x(gx0), wm.y(gy1), wm.x(gx1), wm.y(gy1));
    setUV4(&p, u_char(S.u), u_char(S.v), u_char(S.u + S.w - 1), u_char(S.v), u_char(S.u), u_char(S.v + S.h - 1), u_char(S.u + S.w - 1), u_char(S.v + S.h - 1));
    p.tpage = WeaponBank::aliasTpage(kIconAlias, std::uint16_t(2u << 7));
    p.clut = 0;
    DrawPrim(&p);
    if (!gameAmmo) {
      const std::uint32_t a = 0x8012D7E0u + 4u * std::uint32_t(id);
      const int loaded = ramByte(a) | (ramByte(a + 1) << 8), reserve = ramByte(a + 2) | (ramByte(a + 3) << 8);
      const std::string txt = loaded > 0 ? std::to_string(loaded) + "/" + std::to_string(reserve) : std::to_string(reserve);
      const float scale = wm.w(1.0);
      gameText(txt, wm.x(120.0), wm.y(95.0), scale, 200, 200, 200);
    }
  }

  template <class P> static void rgb(P& p, int k, std::uint32_t c) {
    const auto r = u_char(c & 0xff), g = u_char((c >> 8) & 0xff), b = u_char((c >> 16) & 0xff);
    if constexpr (requires { p.r3; }) { if (k == 3) { p.r3 = r; p.g3 = g; p.b3 = b; return; } }
    if constexpr (requires { p.r2; }) { if (k == 2) { p.r2 = r; p.g2 = g; p.b2 = b; return; } }
    if constexpr (requires { p.r1; }) { if (k == 1) { p.r1 = r; p.g1 = g; p.b1 = b; return; } }
    if (k == 0) { p.r0 = r; p.g0 = g; p.b0 = b; }
  }

  void hudCircle(const std::vector<std::uint32_t>& pk, const HudMap& map) {
    if (pk.size() < 3) return;
    const double cx = sx11(pk[1]), cy = sy11(pk[1]), r = double(pk[2] & 0xffff) / 16.0;
    const char letter = char((pk[2] >> 16) & 0xff);
    const bool dark = (pk[2] >> 24) & 1;
    const std::uint32_t c = pk[0] & 0xffffff;
    const int segs = 40;
    for (int k = 0; k < segs; ++k) {
      const double a0 = k * 6.283185307179586 / segs, a1 = (k + 1) * 6.283185307179586 / segs;
      POLY_F3 p{}; setPolyF3(&p);
      setRGB0(&p, u_char(c & 0xff), u_char((c >> 8) & 0xff), u_char((c >> 16) & 0xff));
      p.x0 = map.xe(cx, false); p.y0 = map.y(cy);
      p.x1 = map.xe(cx + r * std::cos(a0), false); p.y1 = map.y(cy + r * std::sin(a0));
      p.x2 = map.xe(cx + r * std::cos(a1), false); p.y2 = map.y(cy + r * std::sin(a1));
      DrawPrim(&p);
    }

    using Seg = std::array<double, 4>;
    std::vector<Seg> segs2;
    switch (letter) {
      case 'A': segs2 = {{-0.5, 0.6, 0, -0.62}, {0, -0.62, 0.5, 0.6}, {-0.28, 0.17, 0.28, 0.17}}; break;
      case 'B': segs2 = {{-0.38, -0.6, -0.38, 0.6}, {-0.38, -0.6, 0.18, -0.6}, {0.18, -0.6, 0.38, -0.42}, {0.38, -0.42, 0.38, -0.18}, {0.38, -0.18, 0.18, 0}, {0.18, 0, -0.38, 0},
                         {0.18, 0, 0.44, 0.18}, {0.44, 0.18, 0.44, 0.42}, {0.44, 0.42, 0.22, 0.6}, {0.22, 0.6, -0.38, 0.6}}; break;
      case 'X': segs2 = {{-0.45, -0.6, 0.45, 0.6}, {0.45, -0.6, -0.45, 0.6}}; break;
      case 'Y': segs2 = {{-0.45, -0.6, 0, 0}, {0.45, -0.6, 0, 0}, {0, 0, 0, 0.6}}; break;
      default: break;
    }
    const double sc = r * 0.72, wdt = r * 0.13;
    const u_char lc = dark ? 30 : 245;
    for (const auto& sg : segs2) {
      const double ax = cx + sg[0] * sc, ay = cy + sg[1] * sc, bx = cx + sg[2] * sc, by = cy + sg[3] * sc;
      double dx = bx - ax, dy = by - ay; const double len = std::max(1e-6, std::hypot(dx, dy)); dx /= len; dy /= len;
      const double nx = -dy * wdt, ny = dx * wdt, ex = dx * wdt * 0.6, ey = dy * wdt * 0.6;
      POLY_F4 q{}; setPolyF4(&q); setRGB0(&q, lc, lc, lc);
      q.x0 = map.xe(ax - ex + nx, false); q.y0 = map.y(ay - ey + ny);
      q.x1 = map.xe(bx + ex + nx, false); q.y1 = map.y(by + ey + ny);
      q.x2 = map.xe(ax - ex - nx, false); q.y2 = map.y(ay - ey - ny);
      q.x3 = map.xe(bx + ex - nx, false); q.y3 = map.y(by + ey - ny);
      DrawPrim(&q);
    }
  }
  void hudPolygon(const std::vector<std::uint32_t>& pk, const HudMap& map) {
    const auto cmd = pk[0] >> 24;
    const bool quad = cmd & 8, tex = cmd & 4, gour = cmd & 0x10, semi = cmd & 2, raw = cmd & 1;
    const int n = quad ? 4 : 3;
    std::uint32_t col[4]{}, xy[4]{}, uv[4]{};
    std::size_t i = 0;
    col[0] = pk[i++] & 0xffffff;
    for (int k = 0; k < n; ++k) {
      if (k > 0) col[k] = gour ? (i < pk.size() ? pk[i++] & 0xffffff : 0) : col[0];
      xy[k] = i < pk.size() ? pk[i++] : 0;
      if (tex) uv[k] = i < pk.size() ? pk[i++] : 0;
    }

    {
      double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      for (int k = 0; k < n; ++k) { x0 = std::min(x0, sx11(xy[k])); x1 = std::max(x1, sx11(xy[k])); y0 = std::min(y0, sy11(xy[k])); y1 = std::max(y1, sy11(xy[k])); }
      if (x1 - x0 > 1023 || y1 - y0 > 511) return;
    }
    auto fill = [&](auto& p) {
      setcode(&p, u_char(cmd));
      for (int k = 0; k < n; ++k) rgb(p, k, col[k]);
      VERTTYPE* v = &p.x0;
      (void)v;
    };
    bool ext = !tex && !semi;
    for (int k = 0; k < n && ext; ++k) ext = blackish(col[k]);
    if (!tex && semi) {
      double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      for (int k = 0; k < n; ++k) { x0 = std::min(x0, sx11(xy[k])); x1 = std::max(x1, sx11(xy[k])); y0 = std::min(y0, sy11(xy[k])); y1 = std::max(y1, sy11(xy[k])); }
      if (x1 - x0 >= 300 && y1 - y0 >= 200) ext = true;
    }
    double minX = 1e9, maxX = -1e9;
    for (int k = 0; k < n; ++k) { minX = std::min(minX, sx11(xy[k])); maxX = std::max(maxX, sx11(xy[k])); }

    if (!tex && semi && !ext) {
      static const bool off = std::getenv("SF3N_MASCARA_FUNDIDO_4_3") != nullptr;
      bool grey = !off;
      for (int k = 0; k < n && grey; ++k) { const auto c = col[k]; grey = (c & 0xff) == ((c >> 8) & 0xff) && (c & 0xff) == ((c >> 16) & 0xff); }
      if (grey) {
        const double dwv = map.dw > 0 ? map.dw : 368.0;
        bool edge = false;
        for (int k = 0; k < n && !edge; ++k) { const double px = sx11(xy[k]) + map.ox - map.ax; edge = px <= 10.0 || px >= dwv + 6.0; }
        ext = edge;
      }
    }
    if (!tex && !ext && maxX - minX >= 0.8 * (map.dw > 0 ? map.dw : 368.0)) {

      bool dark = true;
      for (int k = 0; k < n && dark; ++k) dark = semi ? ((col[k] & 0xff) <= 0x40 && ((col[k] >> 8) & 0xff) <= 0x40 && ((col[k] >> 16) & 0xff) <= 0x40) : blackish(col[k]);
      ext = dark;
    }
    if (static int bl = std::getenv("SF3N_BARLOG") ? 3000 : 0; bl > 0 && !tex && maxX - minX >= 200 && long(field_count_) >= std::atol(std::getenv("SF3N_BARLOG"))) {
      --bl;
      std::fprintf(log, "BARRA poli cmd %02x ext %d col %06x campo %llu ox %.0f ax %.0f dw %.0f:", cmd, int(ext), col[0], (unsigned long long)field_count_, map.ox, map.ax, map.dw);
      for (int k = 0; k < n; ++k) std::fprintf(log, " (%.0f,%.0f)", sx11(xy[k]), sy11(xy[k]));
      std::fprintf(log, "\n");
    }
    const bool wide = ext && maxX - minX >= 0.8 * (map.dw > 0 ? map.dw : 368.0);
    auto ex = [&](int k) {
      const double gx = sx11(xy[k]) + hfx(k);
      if (wide) { if (gx <= minX + 1) return -2000.f; if (gx >= maxX - 1) return 2400.f; }
      return map.xe(gx, ext);
    };

    static const bool radialOn = std::getenv("SF3N_ASPAS_RECTAS") == nullptr;

    bool rectAxis = false;
    if (quad) {
      std::set<long> xs, ys;
      for (int k = 0; k < 4; ++k) { xs.insert(std::lround(sx11(xy[k]))); ys.insert(std::lround(sy11(xy[k]))); }
      rectAxis = xs.size() <= 2 && ys.size() <= 2;
    }
    const bool radial = radialOn && !rectAxis;
    auto vtx = [&](int k, auto& ox, auto& oy) {
      const double gx = sx11(xy[k]) + hfx(k), gy = sy11(xy[k]) + hfy(k);
      const double px = gx + map.ox - map.ax, dwv = map.dw > 0 ? map.dw : 368.0;
      if (radial && ext && !wide && (px <= 10.0 || px >= dwv + 6.0)) {
        const double cxg = gx - map.cx(gx), cyg = gy - map.cy(gy);
        const double K = 8.0;
        ox = decltype(+ox)(map.x(cxg + (gx - cxg) * K)); oy = decltype(+oy)(map.y(cyg + (gy - cyg) * K));
        return;
      }
      ox = decltype(+ox)(ex(k)); oy = decltype(+oy)(map.y(gy));
    };
    auto setxy = [&](auto& p) {
      vtx(0, p.x0, p.y0); vtx(1, p.x1, p.y1); vtx(2, p.x2, p.y2);
      if constexpr (requires { p.x3; }) vtx(3, p.x3, p.y3);
    };
    auto setuv = [&](auto& p) {
      p.u0 = u_char(uv[0]); p.v0 = u_char(uv[0] >> 8); p.clut = u_short(uv[0] >> 16);
      p.u1 = u_char(uv[1]); p.v1 = u_char(uv[1] >> 8); p.tpage = u_short(uv[1] >> 16);
      p.u2 = u_char(uv[2]); p.v2 = u_char(uv[2] >> 8);
      if constexpr (requires { p.u3; }) { p.u3 = u_char(uv[3]); p.v3 = u_char(uv[3] >> 8); }
    };
    auto finish = [&](auto& p) {
      if (semi) setSemiTrans(&p, 1);
      if (raw) setShadeTex(&p, 1);
      DrawPrim(&p);
    };
    if (!tex && !gour && !quad) { POLY_F3 p{}; setPolyF3(&p); fill(p); setxy(p); if (static const bool pl = std::getenv("SF3N_HUDTRILOG") != nullptr; pl) std::fprintf(log, "HUDTRI %02x col %06x ext %d (%.0f,%.0f)(%.0f,%.0f)(%.0f,%.0f) ox %.0f ax %.0f\n", cmd, col[0], int(ext), double(p.x0), double(p.y0), double(p.x1), double(p.y1), double(p.x2), double(p.y2), map.ox, map.ax); finish(p); }
    else if (!tex && !gour) { POLY_F4 p{}; setPolyF4(&p); fill(p); setxy(p); finish(p); }
    else if (!tex && !quad) { POLY_G3 p{}; setPolyG3(&p); fill(p); setxy(p); finish(p); }
    else if (!tex) { POLY_G4 p{}; setPolyG4(&p); fill(p); setxy(p); finish(p); }
    else if (!gour && !quad) { POLY_FT3 p{}; setPolyFT3(&p); fill(p); setxy(p); setuv(p); finish(p); }
    else if (!gour) { POLY_FT4 p{}; setPolyFT4(&p); fill(p); setxy(p); setuv(p); finish(p); }
    else if (!quad) { POLY_GT3 p{}; setPolyGT3(&p); fill(p); setxy(p); setuv(p); finish(p); }
    else { POLY_GT4 p{}; setPolyGT4(&p); fill(p); setxy(p); setuv(p); finish(p); }
  }

  void hudLine(const std::vector<std::uint32_t>& pk, const HudMap& map) {
    const auto cmd = pk[0] >> 24;
    const bool gour = cmd & 0x10, semi = cmd & 2;
    if (pk.size() < (gour ? 4u : 3u)) return;
    const std::uint32_t c0 = pk[0] & 0xffffff, c1 = gour ? pk[2] & 0xffffff : c0;
    const std::uint32_t xa = pk[1], xb = gour ? pk[3] : pk[2];
    hudLineF(map.x(sx11(xa) + hfx(0)), map.y(sy11(xa) + hfy(0)), map.x(sx11(xb) + hfx(1)), map.y(sy11(xb) + hfy(1)), c0, c1, semi);
  }
  void hudLineF(float x0, float y0, float x1, float y1, std::uint32_t c0, std::uint32_t c1, bool semi) {
    float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);

    float nx = 0.5f, ny = 0.f, ex = 0.5f, ey = 0.f;
    if (len > 1e-3f) { nx = -dy / len * 0.5f; ny = dx / len * 0.5f; ex = dx / len * 0.5f; ey = dy / len * 0.5f; }
    else { nx = 0.f; ny = 0.5f; }
    POLY_G4 p{};
    setPolyG4(&p);
    rgb(p, 0, c0); rgb(p, 1, c0); rgb(p, 2, c1); rgb(p, 3, c1);
    p.x0 = x0 + nx - ex; p.y0 = y0 + ny - ey;
    p.x1 = x0 - nx - ex; p.y1 = y0 - ny - ey;
    p.x2 = x1 + nx + ex; p.y2 = y1 + ny + ey;
    p.x3 = x1 - nx + ex; p.y3 = y1 - ny + ey;
    if (semi) setSemiTrans(&p, 1);
    DrawPrim(&p);
  }

  void hudRect(const std::vector<std::uint32_t>& pk, const HudMap& map) {
    const auto cmd = pk[0] >> 24;
    const bool tex = cmd & 4, semi = cmd & 2, raw = cmd & 1;
    std::size_t i = 1;
    const auto xy = i < pk.size() ? pk[i++] : 0;
    std::uint32_t uv = 0;
    if (tex) uv = i < pk.size() ? pk[i++] : 0;
    double w = 1, h = 1;
    switch ((cmd >> 3) & 3) {
      case 0: { const auto wh = i < pk.size() ? pk[i] : 0; w = double(wh & 0x3ff); h = double((wh >> 16) & 0x1ff); break; }
      case 1: w = h = 1; break;
      case 2: w = h = 8; break;
      default: w = h = 16; break;
    }
    if (tex) {

      POLY_FT4 p{}; setPolyFT4(&p);
      rgb(p, 0, pk[0]);
      const float x0 = map.x(sx11(xy) + hfx(0)), y0 = map.y(sy11(xy) + hfy(0)), x1 = x0 + map.w(w), y1 = y0 + map.h(h);
      p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y0; p.x2 = x0; p.y2 = y1; p.x3 = x1; p.y3 = y1;
      const int u0 = int(uv & 0xff), v0 = int((uv >> 8) & 0xff);
      const int u1 = std::min(255, u0 + int(w)), v1 = std::min(255, v0 + int(h));
      p.u0 = u_char(u0); p.v0 = u_char(v0); p.u1 = u_char(u1); p.v1 = u_char(v0);
      p.u2 = u_char(u0); p.v2 = u_char(v1); p.u3 = u_char(u1); p.v3 = u_char(v1);
      p.clut = u_short(uv >> 16);
      p.tpage = u_short(hud_tpage_);
      if (semi) setSemiTrans(&p, 1);
      if (raw) setShadeTex(&p, 1);
      DrawPrim(&p);
    } else {
      TILE p{}; setTile(&p);
      rgb(p, 0, pk[0]);
      const bool ext = blackish(pk[0]) && (!semi || w >= 0.8 * (map.dw > 0 ? map.dw : 368.0));
      if (static int bl = std::getenv("SF3N_BARLOG") ? 3000 : 0; bl > 0 && ext && long(field_count_) >= std::atol(std::getenv("SF3N_BARLOG"))) { --bl; std::fprintf(log, "BARRA rect campo %llu ox %.0f ax %.0f: (%.0f,%.0f) %gx%g\n", (unsigned long long)field_count_, map.ox, map.ax, sx11(xy), sy11(xy), w, h); }
      const double fw = w + (hud_frac_ ? hud_frac_->sw : 0.f), fh = h + (hud_frac_ ? hud_frac_->sh : 0.f);
      float xa = map.xe(sx11(xy) + hfx(0), ext), xb = map.xe(sx11(xy) + hfx(0) + fw, ext);

      if (ext && w >= 0.8 * (map.dw > 0 ? map.dw : 368.0)) { xa = -2000.f; xb = 2400.f; }
      p.x0 = xa; p.y0 = map.y(sy11(xy) + hfy(0)); p.w = xb - xa; p.h = map.h(fh);
      if (semi) setSemiTrans(&p, 1);
      DrawPrim(&p);
    }
  }
  unsigned hud_tpage_ = 0;
  std::uint64_t hud_logs_ = 0;

  struct HudFrac { std::array<float, 8> d{}; float sw = 0, sh = 0; };
  const HudFrac* hud_frac_ = nullptr;
  float hfx(int k) const { return hud_frac_ ? hud_frac_->d[std::size_t(2 * k)] : 0.f; }
  float hfy(int k) const { return hud_frac_ ? hud_frac_->d[std::size_t(2 * k + 1)] : 0.f; }

  static int hudSlots(const std::vector<std::uint32_t>& pk, std::array<int, 4>& slot, int& wh, std::array<int, 4>& colSlot) {
    slot.fill(-1); colSlot.fill(-1); wh = -1;
    if (pk.empty()) return 0;
    const auto cmd = pk[0] >> 24;
    colSlot[0] = 0;
    if (cmd >= 0x20 && cmd < 0x40) {
      const bool quad = cmd & 8, tex = cmd & 4, gour = cmd & 0x10;
      const int n = quad ? 4 : 3;
      std::size_t i = 1;
      for (int k = 0; k < n; ++k) {
        if (k > 0 && gour) colSlot[std::size_t(k)] = int(i++);
        slot[std::size_t(k)] = int(i++);
        if (tex) ++i;
      }
      return i <= pk.size() ? n : 0;
    }
    if (cmd >= 0x40 && cmd < 0x60 && !(cmd & 8)) {
      const bool gour = cmd & 0x10;
      if (pk.size() < (gour ? 4u : 3u)) return 0;
      slot[0] = 1; slot[1] = gour ? 3 : 2;
      if (gour) colSlot[1] = 2;
      return 2;
    }
    if (cmd >= 0x60 && cmd < 0x80) {
      const bool tex = cmd & 4;
      slot[0] = 1;
      if (((cmd >> 3) & 3) == 0) wh = tex ? 3 : 2;
      return std::size_t(wh) < pk.size() || wh < 0 ? 1 : 0;
    }
    return 0;
  }

  static Packets hudLerp(const Packets& A, const Packets& B, double t, std::vector<HudFrac>& frac) {
    frac.assign(B.size(), HudFrac{});
    if (A.size() != B.size()) return t < 0.5 ? A : B;
    Packets out = B;
    auto lc = [&](std::uint32_t a, std::uint32_t b) {
      std::uint32_t r = b & 0xff000000u;
      for (int sh = 0; sh < 24; sh += 8) {
        const double ca = double((a >> sh) & 0xff), cb = double((b >> sh) & 0xff);
        r |= std::uint32_t(std::clamp(std::lround(ca + (cb - ca) * t), 0L, 255L)) << sh;
      }
      return r;
    };
    for (std::size_t i = 0; i < B.size(); ++i) {
      const auto& a = A[i]; const auto& b = B[i];
      if (a.size() != b.size() || a.empty() || (a[0] >> 24) != (b[0] >> 24)) continue;
      std::array<int, 4> slot{}, cs{}; int wh = -1;
      const int n = hudSlots(b, slot, wh, cs);
      if (n == 0) continue;

      bool jump = false;
      for (int k = 0; k < n && !jump; ++k) {
        const auto wa = a[std::size_t(slot[std::size_t(k)])], wb = b[std::size_t(slot[std::size_t(k)])];
        jump = std::fabs(sx11(wa) - sx11(wb)) > 48 || std::fabs(sy11(wa) - sy11(wb)) > 48;
      }
      if (jump) continue;
      for (int k = 0; k < n; ++k) {
        const auto wa = a[std::size_t(slot[std::size_t(k)])], wb = b[std::size_t(slot[std::size_t(k)])];
        frac[i].d[std::size_t(2 * k)] = float((sx11(wa) - sx11(wb)) * (1.0 - t));
        frac[i].d[std::size_t(2 * k + 1)] = float((sy11(wa) - sy11(wb)) * (1.0 - t));
      }
      if (wh >= 0 && std::size_t(wh) < b.size()) {
        frac[i].sw = float((double(a[std::size_t(wh)] & 0x3ff) - double(b[std::size_t(wh)] & 0x3ff)) * (1.0 - t));
        frac[i].sh = float((double((a[std::size_t(wh)] >> 16) & 0x1ff) - double((b[std::size_t(wh)] >> 16) & 0x1ff)) * (1.0 - t));
      }
      for (int k = 0; k < 4; ++k) {
        const int c = cs[std::size_t(k)];
        if (c < 0 || std::size_t(c) >= b.size()) continue;
        out[i][std::size_t(c)] = lc(a[std::size_t(c)], b[std::size_t(c)]);
      }
    }
    return out;
  }

  void convertGuest() {
    if (!guest_dirty_ || vram_.size() < 1024 * 512) return;
    guest_dirty_ = false;
    ++guest_conv_seq_;
    struct { const std::vector<std::uint16_t>& vram; int disp_x, disp_y, disp_w, disp_h; bool rgb24; } g{vram_, disp_.x, disp_.y, disp_.w, disp_.h, disp_.rgb24};
    gw_ = std::clamp(g.disp_w, 16, 640);
    gh_ = std::clamp(g.disp_h, 16, 256);
    rgba_.resize(std::size_t(gw_) * gh_ * 4);
    for (int y = 0; y < gh_; ++y)
      for (int x = 0; x < gw_; ++x) {
        unsigned char* px = &rgba_[std::size_t((y * gw_ + x) * 4)];
        if (g.rgb24) {
          const int bx = g.disp_x * 2 + x * 3;
          auto byteAt = [&](int i) { const auto wv = g.vram[std::size_t(((g.disp_y + y) & 511) * 1024 + ((i / 2) & 1023))]; return (i & 1) ? (wv >> 8) : (wv & 0xff); };
          px[0] = (unsigned char)byteAt(bx); px[1] = (unsigned char)byteAt(bx + 1); px[2] = (unsigned char)byteAt(bx + 2);
        } else {
          const auto c = g.vram[std::size_t(((g.disp_y + y) & 511) * 1024 + ((g.disp_x + x) & 1023))];
          px[0] = (unsigned char)((c & 31) << 3); px[1] = (unsigned char)(((c >> 5) & 31) << 3); px[2] = (unsigned char)(((c >> 10) & 31) << 3);
        }
        px[3] = 255;
      }

    const int slices = (gw_ + 127) / 128;
    if (int(slices_.size()) < slices) slices_.resize(std::size_t(slices));
    for (int s = 0; s < slices; ++s) {
      auto& sl = slices_[std::size_t(s)];
      const int x0 = s * 128, w = std::min(128, gw_ - x0);
      sl.pixels.resize(std::size_t(w) * gh_ * 4);
      for (int y = 0; y < gh_; ++y) std::memcpy(sl.pixels.data() + std::size_t(y) * w * 4, rgba_.data() + (std::size_t(y) * gw_ + x0) * 4, std::size_t(w) * 4);
      if (sl.tex == 0 || sl.w != w || sl.h != gh_) {
        if (sl.tex) GR_DestroyTexture(sl.tex);
        sl.tex = GR_CreateRGBATexture(w, gh_, sl.pixels.data());
      } else {
        GR_UpdateRGBATexture(sl.tex, w, gh_, sl.pixels.data());
      }
      sl.w = w; sl.h = gh_; sl.x0 = x0;
    }
    slice_count_ = slices;
  }

  void updateVideoRect() {
    if (frame_seq_seen_ == vr_seq_) return;
    vr_seq_ = frame_seq_seen_;
    bool poly = false;
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -1, y1 = -1;
    const int dh = std::max(16, disp_.h);
    for (const auto& pk : frame_) {
      if (pk.empty()) continue;
      const auto c = pk[0] >> 24;
      if (c >= 0x20 && c < 0x80) { poly = true; break; }
      if (c == 0xa0 && pk.size() >= 3) {
        int x = int(pk[1] & 0x3ff) - disp_.x, y = int((pk[1] >> 16) & 0x1ff) - disp_.y;
        int w = int(pk[2] & 0xffff), h = int(pk[2] >> 16);
        if (w <= 0 || h <= 0) continue;

        y = ((y % dh) + dh) % dh;
        if (disp_.rgb24) { w = (x + w) * 2 / 3 - x * 2 / 3; x = x * 2 / 3; }
        if (x < 0 || x >= gw_) continue;
        x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, std::min(gw_, x + w)); y1 = std::max(y1, std::min(dh, y + h));
      }
    }
    if (static const bool vl = std::getenv("SF3N_VIDEOLOG") != nullptr; vl) std::fprintf(log, "VIDEO campo %llu poly %d rect %d,%d-%d,%d paquetes %zu gw %d gh %d\n", (unsigned long long)field_no_, int(poly), x0, y0, x1, y1, frame_.size(), gw_, gh_);

    const bool mdecBusy = field_count_ - last_mdec_field_ < 25;
    if (poly || !mdecBusy) { vr_valid_ = false; vr_miss_ = 0; return; }
    if (x1 > x0 + 32 && y1 > y0 + 32) {

      vr_x0_ = x0; vr_y0_ = y0; vr_x1_ = x1; vr_y1_ = y1;
      vr_valid_ = true; vr_miss_ = 0;
    } else if (++vr_miss_ > 30) vr_valid_ = false;
  }

  void drawGuestImage(bool pip) {
    convertGuest();
    if (slice_count_ == 0) return;
    if (!pip) {
      updateVideoRect();
      if (vr_valid_ && drawVideo(videos_zoom_)) return;
    }
    if (!pip) setVideoMode(gw_, gh_, false);
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    DR_TPAGE page{};
    SetDrawTPage(&page, 1, 0, GetTPage(2, 0, 0, 0));
    DrawPrim(&page);

    const double sc = pip ? 0.3 * canvas_h / gh_ : 1.0;
    const double ox = pip ? canvas_w - gw_ * sc - 4 : 0, oy = pip ? canvas_h - gh_ * sc - 4 : 0;
    for (int s = 0; s < slice_count_; ++s) {
      const auto& sl = slices_[std::size_t(s)];
      DR_PSYX_TEX tex{};
      SetPsyXTexture(&tex, sl.tex, sl.w, sl.h);
      DrawPrim(&tex);
      POLY_FT4 q{};
      setPolyFT4(&q);
      setRGB0(&q, 128, 128, 128);
      const float x0 = float(ox + sl.x0 * sc), x1 = float(ox + (sl.x0 + sl.w) * sc);
      const float y0 = float(oy), y1 = float(oy + sl.h * sc);
      setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
      setUV4(&q, 0, 0, u_char(sl.w), 0, 0, u_char(std::min(255, sl.h)), u_char(sl.w), u_char(std::min(255, sl.h)));
      DrawPrim(&q);
    }
    DR_PSYX_TEX restore{};
    SetPsyXTexture(&restore, 0u, 0, 0);
    DrawPrim(&restore);
  }

  void filterVideo(std::vector<unsigned char>& px, int w, int h) {
    const std::size_t n = std::size_t(w) * h;
    std::vector<float> Y(n), U(n), V(n), T(n);
    for (std::size_t i = 0; i < n; ++i) {
      const float r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
      Y[i] = 0.299f * r + 0.587f * g + 0.114f * b;
      U[i] = (b - Y[i]) * 0.5643f;
      V[i] = (r - Y[i]) * 0.7133f;
    }
    auto deblock = [&](float* p1, float* p0, float* q0, float* q1) {
      const float d = *q0 - *p0;
      if (std::fabs(d) >= 22.f || std::fabs(*p1 - *p0) >= 7.f || std::fabs(*q1 - *q0) >= 7.f) return;
      *p0 += d * 0.375f; *q0 -= d * 0.375f; *p1 += d * 0.125f; *q1 -= d * 0.125f;
    };
    for (int y = 0; y < h; ++y)
      for (int x = 8; x + 1 < w; x += 8) { float* r = &Y[std::size_t(y) * w]; deblock(&r[x - 2], &r[x - 1], &r[x], &r[x + 1]); }
    for (int y = 8; y + 1 < h; y += 8)
      for (int x = 0; x < w; ++x) { auto at = [&](int yy) { return &Y[std::size_t(yy) * w + x]; }; deblock(at(y - 2), at(y - 1), at(y), at(y + 1)); }
    auto blur = [&](std::vector<float>& C) {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const float* r = &C[std::size_t(y) * w];
          T[std::size_t(y) * w + x] = (r[std::max(0, x - 1)] + 2 * r[x] + r[std::min(w - 1, x + 1)]) * 0.25f;
        }
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
          C[std::size_t(y) * w + x] = (T[std::size_t(std::max(0, y - 1)) * w + x] + 2 * T[std::size_t(y) * w + x] + T[std::size_t(std::min(h - 1, y + 1)) * w + x]) * 0.25f;
    };
    blur(U); blur(V);
    for (std::size_t i = 0; i < n; ++i) {
      const float r = Y[i] + V[i] / 0.7133f, b = Y[i] + U[i] / 0.5643f;
      const float g = (Y[i] - 0.299f * r - 0.114f * b) / 0.587f;
      px[i * 4] = (unsigned char)std::clamp(int(std::lround(r)), 0, 255);
      px[i * 4 + 1] = (unsigned char)std::clamp(int(std::lround(g)), 0, 255);
      px[i * 4 + 2] = (unsigned char)std::clamp(int(std::lround(b)), 0, 255);
    }
  }

  bool drawVideo(bool zoom) {
    const int rx0 = std::clamp(vr_x0_, 0, gw_), rx1 = std::clamp(vr_x1_, 0, gw_);
    const int ry0 = std::clamp(vr_y0_, 0, gh_), ry1 = std::clamp(vr_y1_, 0, gh_);
    const int rw = rx1 - rx0, rh = ry1 - ry0;
    if (rw < 32 || rh < 32 || rgba_.size() < std::size_t(gw_) * gh_ * 4) return false;
    if (video_seq_ != guest_conv_seq_ || video_w_ != rw || video_h_ != rh) {
      video_seq_ = guest_conv_seq_;
      video_px_.resize(std::size_t(rw) * rh * 4);
      for (int y = 0; y < rh; ++y) std::memcpy(video_px_.data() + std::size_t(y) * rw * 4, rgba_.data() + (std::size_t(y + ry0) * gw_ + rx0) * 4, std::size_t(rw) * 4);
      static const bool noFilter = envInt("SF3N_VIDEO_FILTRO", 1) == 0;
      if (!noFilter && g_cfg_bilinearFiltering) filterVideo(video_px_, rw, rh);
      if (video_tex_ == 0 || video_w_ != rw || video_h_ != rh) {
        if (video_tex_) GR_DestroyTexture(video_tex_);
        video_tex_ = GR_CreateRGBATexture(rw, rh, video_px_.data());
      } else {
        GR_UpdateRGBATexture(video_tex_, rw, rh, video_px_.data());
      }
      video_w_ = rw; video_h_ = rh;
    }
    setVideoMode(gw_, gh_, false);
    GR_SetBlendMode(BM_NONE);
    GR_SetDepthState(0, 0);
    GR_EnableDepth(0);
    float x0, y0, x1, y1;
    if (zoom) {

      double A = 4.0 / 3.0;
      if (g_cfg_aspectMode == PSYX_ASPECT_ADAPTIVE && g_windowWidth > 0 && g_windowHeight > 0) A = double(g_windowWidth) / double(g_windowHeight);
      const double Av = (double(rw) / gw_) * (4.0 / 3.0) / (double(rh) / gh_);
      double Wt, Ht;
      if (Av >= A) { Wt = A; Ht = A / Av; } else { Ht = 1.0; Wt = Av; }
      const double uw = Wt * gw_ * 0.75, uh = Ht * gh_;
      x0 = float(gw_ * 0.5 - uw * 0.5); y0 = float(gh_ * 0.5 - uh * 0.5); x1 = float(x0 + uw); y1 = float(y0 + uh);
    } else {
      x0 = float(rx0); y0 = float(ry0); x1 = float(rx1); y1 = float(ry1);
    }
    {
      POLY_F4 q{}; setPolyF4(&q); setRGB0(&q, 0, 0, 0);
      setXY4(&q, -4000.f, -400.f, 4000.f, -400.f, -4000.f, 800.f, 4000.f, 800.f); DrawPrim(&q);
    }
    DR_TPAGE page{};
    SetDrawTPage(&page, 1, 0, GetTPage(2, 0, 0, 0));
    DrawPrim(&page);

    const int qw = (rw + 3) / 4, qh = (rh + 3) / 4;
    DR_PSYX_TEX tex{};
    SetPsyXTexture(&tex, video_tex_, qw, qh);
    DrawPrim(&tex);
    POLY_FT4 q{};
    setPolyFT4(&q);
    setRGB0(&q, 128, 128, 128);
    setXY4(&q, x0, y0, x1, y0, x0, y1, x1, y1);
    const float fu = float(rw) / 4.f, fv = float(rh) / 4.f;
    (void)fu; (void)fv;
    setUV4(&q, 0, 0, u_char(qw), 0, 0, u_char(qh), u_char(qw), u_char(qh));
    DrawPrim(&q);
    DR_PSYX_TEX restore{};
    SetPsyXTexture(&restore, 0u, 0, 0);
    DrawPrim(&restore);
    return true;
  }

  struct Slice { TextureID tex{}; int w{}, h{}, x0{}; std::vector<unsigned char> pixels; };
  bool videos_zoom_ = envInt("SF3N_VIDEOS_AMPLIADOS", 1) != 0;
  int hud_scale_ = std::clamp(envInt("SF3N_HUD_TAMANO", 100), 50, 200);
  bool hud_edges_ = envInt("SF3N_HUD_BORDES", 1) != 0;
  bool vr_valid_ = false;
  int vr_x0_ = 0, vr_y0_ = 0, vr_x1_ = 0, vr_y1_ = 0, vr_miss_ = 0;
  std::uint64_t vr_seq_ = ~0ull, guest_conv_seq_ = 0, video_seq_ = ~0ull;
  TextureID video_tex_{};
  int video_w_ = 0, video_h_ = 0;
  std::vector<unsigned char> video_px_;

  SDL_GameController* pad_ = nullptr;
  std::atomic<std::uint8_t> rumble_small_{0}, rumble_large_{0};
  std::uint8_t rumble_sent_small_ = 0, rumble_sent_large_ = 0;
  Clock::time_point rumble_sent_at_{};
  SDL_AudioDeviceID audio_ = 0;
  std::atomic<int> save_request_{0}, load_request_{0};
  std::atomic<bool> mark_request_{false};

  std::atomic<bool> menu_open_{false};
  std::mutex card_mtx_;
  std::vector<std::string> card_items_;
  bool card_save_ = false;
  std::atomic<bool> card_req_{false};
  std::atomic<int> card_result_{-99};
  std::atomic<int> card_delete_req_{-1};
  int card_del_idx_ = -1;
  std::atomic<bool> name_entry_{false};
  std::string typed_;
  std::deque<std::pair<std::uint16_t, int>> pad_seq_;
  mutable bool card_hold_ = false;
  bool menu_frozen_native_ = false;
  std::atomic<bool> start_block_{false};
  std::atomic<int> inject_start_{0}, inject_back_{0};
  int menu_page_ = 0, menu_sel_ = 0, menu_scroll_ = 0;

  struct WeaponInfo { int index; std::string name; int clip, reserve; bool current; };
  std::vector<WeaponInfo> menu_weapons_;
  std::uint32_t weapon_cur_addr_ = 0x8012d880u, weapon_ammo_addr_ = 0x8012d7e0u;
  std::atomic<int> weapon_target_{-1};
  int weapon_deadline_ = 0, weapon_phase_ = 0;
 public:
  void setWeaponAddrs(std::uint32_t cur, std::uint32_t ammo) { weapon_cur_addr_ = cur; weapon_ammo_addr_ = ammo; }
 private:
  std::uint32_t ramWord(std::uint32_t a) const { const auto o = a & 0x1ffffcu; std::uint32_t v = 0; if (o + 4 <= ram_.size()) std::memcpy(&v, &ram_[o], 4); return v; }
  std::uint8_t ramByte(std::uint32_t a) const { const auto o = a & 0x1fffffu; return o < ram_.size() ? std::to_integer<std::uint8_t>(ram_[o]) : 0; }
  int ramHalf(std::uint32_t a) const { return int(std::int16_t(ramByte(a) | (ramByte(a + 1) << 8))); }

  std::vector<std::string> weaponNames() const {
    std::vector<std::string> names;
    const char* key = "Falcon";
    for (std::size_t o = 0x100000; o + 16 < ram_.size(); ++o) {
      if (std::to_integer<char>(ram_[o]) != 'F' || std::to_integer<int>(ram_[o - 1]) != 0 || std::to_integer<int>(ram_[o - 2]) != 0) continue;
      bool m = true;
      for (int k = 0; k < 7 && m; ++k) m = std::to_integer<char>(ram_[o + std::size_t(k)]) == (k < 6 ? key[k] : '\0');
      if (!m) continue;
      std::size_t p = o - 2;
      for (int idx = 0; idx < 48 && p + 4 < ram_.size(); ++idx) {
        const int id = std::to_integer<int>(ram_[p]) | (std::to_integer<int>(ram_[p + 1]) << 8);
        if (id != idx) break;
        p += 2;
        std::string strs[4];
        for (auto& st : strs) { while (p < ram_.size() && std::to_integer<int>(ram_[p]) != 0) { st += std::to_integer<char>(ram_[p]); ++p; } ++p; }

        std::string n;
        for (unsigned char c : strs[0]) {
          switch (c) { case 0xe1: case 0xc1: n += 'A'; break; case 0xe9: case 0xc9: n += 'E'; break; case 0xed: case 0xcd: n += 'I'; break;
            case 0xf3: case 0xd3: n += 'O'; break; case 0xfa: case 0xda: case 0xfc: n += 'U'; break; case 0xf1: case 0xd1: n += '\x01'; break;
            default: n += char(std::toupper(c)); }
        }
        while (!n.empty() && n.back() == ' ') n.pop_back();
        names.push_back(n);
      }
      break;
    }
    return names;
  }
  void readWeapons() {
    menu_weapons_.clear();
    if (ram_.empty()) return;
    const auto names = weaponNames();
    const int cur = ramByte(weapon_cur_addr_);
    for (int i = 0; i < 42; ++i) {
      const int reserve = ramHalf(weapon_ammo_addr_ + std::uint32_t(4 * i)), clip = ramHalf(weapon_ammo_addr_ + std::uint32_t(4 * i) + 2);
      if (reserve <= 0 && clip <= 0 && i != cur) continue;
      if (i >= 36) continue;
      menu_weapons_.push_back({i, i < int(names.size()) ? names[std::size_t(i)] : ("ARMA " + std::to_string(i)), std::max(0, clip), std::max(0, reserve), i == cur});
    }
  }
  std::uint32_t prev_pad_menu_ = 0, prev_mouse_ = 0;
  std::uint16_t prev_menu_rb_ = 0xffff;
  std::atomic<int> mouse_wheel_{0};

  bool wheel_weapons_ = true, wheel_zoom_ = true, wheel_invert_ = false;
  bool wheel_menu_hand_ = true;
  std::atomic<int> menu_step_on_{0}; std::atomic<std::uint16_t> menu_step_bit_{0}; int menu_steps_ = 0, menu_step_gap_ = 0;
  int wheel_in_btn_ = 7, wheel_out_btn_ = 5;
  std::atomic<int> zoom_fields_{0};
  int wheel_taps_ = 0, wheel_tap_phase_ = 0, wheel_tap_wait_ = 0, wheel_tap_from_ = -1;

  std::atomic<int> wheel_menu_phase_{0};
  std::atomic<int> direct_target_{-1};
  std::atomic<bool> weapon_pick_hooked_{false};
  int direct_tries_ = 0;
 public:

  int wheelPick(int game, std::uint32_t curAddr) {
    weapon_pick_hooked_ = true;
    if (curAddr != weapon_cur_addr_) return -1;
    const int t = direct_target_.load();
    if (t < 0 || t == game) return -1;
    return t;
  }
  void setWeaponPickHooked() { weapon_pick_hooked_ = true; }
 private:
  std::atomic<bool> modern_lr_block_{false};
  const bool no_lr_block_ = std::getenv("SF3N_RUEDA_LATERAL") != nullptr;
  std::atomic<std::uint16_t> wheel_step_bit_{0};
  int wheel_steps_ = 0, wheel_menu_wait_ = 0, wheel_menu_from_ = -1;
  std::atomic<std::uint16_t> zoom_bit_{0};
  static constexpr std::pair<const char*, std::uint16_t> kWheelBtns[8] = {{"X (CRUZ)", 0x4000}, {"CUADRADO", 0x8000}, {"L1", 0x0400}, {"R1", 0x0800}, {"L2", 0x0100}, {"CÍRCULO", 0x2000}, {"R2", 0x0200}, {"TRIÁNGULO", 0x1000}};
  static int wheelBtnFromIni(const std::string& v, int d) {
    static const char* ids[8] = {"cruz", "cuadrado", "l1", "r1", "l2", "circulo", "r2", "triangulo"};
    for (int i = 0; i < 8; ++i) if (v == ids[i]) return i;
    return d;
  }
  static const char* wheelBtnIni(int i) { static const char* ids[8] = {"cruz", "cuadrado", "l1", "r1", "l2", "circulo", "r2", "triangulo"}; return ids[std::clamp(i, 0, 7)]; }

  void wheelTick() {
    if (static const char* fw = std::getenv("SF3N_FAKEWHEEL"); fw) {
      std::string str(fw); std::size_t p = 0;
      while (p < str.size()) { auto q = str.find(',', p); if (q == std::string::npos) q = str.size(); double t = 0; int d = 0;
        if (std::sscanf(str.substr(p, q - p).c_str(), "%lf:%d", &t, &d) == 2 && field_count_ == std::uint64_t(t * 50 + 0.5)) mouse_wheel_ += d; p = q + 1; }
    }
    if (zoom_fields_ > 0 && --zoom_fields_ == 0) zoom_bit_ = 0;

    if (wheel_tap_phase_ > 0) {
      ++wheel_tap_wait_;
      if (wheel_tap_phase_ == 1 && wheel_tap_wait_ >= 4) { wheel_tap_phase_ = 2; wheel_tap_wait_ = 0; }
      else if (wheel_tap_phase_ == 2 && direct_target_.load() >= 0 && direct_target_.load() != ramByte(weapon_cur_addr_) && ramByte(weapon_cur_addr_) == wheel_tap_from_ && wheel_tap_wait_ >= 12 && direct_tries_ < 6) {

        ++direct_tries_; wheel_tap_phase_ = 1; wheel_tap_wait_ = 0;
        if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA directo: otro toque (%d) campo %llu\n", direct_tries_, (unsigned long long)field_count_);
      }
      else if (wheel_tap_phase_ == 2 && ((ramByte(weapon_cur_addr_) != wheel_tap_from_ && wheel_tap_wait_ >= 6) || wheel_tap_wait_ >= 50)) {
        direct_tries_ = 0;
        wheel_tap_phase_ = 0;

        const int dt = direct_target_.load(), cur = ramByte(weapon_cur_addr_);
        if (dt >= 0 && dt != cur && wheel_tap_wait_ < 50) { wheel_tap_phase_ = 1; wheel_tap_wait_ = 0; wheel_tap_from_ = cur; }
        else direct_target_ = -1;
        if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA directo: fin del toque campo %llu arma %d objetivo %d\n", (unsigned long long)field_count_, cur, direct_target_.load());
      }
    } else if (wheel_taps_ > 0 && in_play_.load() && !menu_open_) {
      --wheel_taps_;
      wheel_tap_phase_ = 1; wheel_tap_wait_ = 0; wheel_tap_from_ = ramByte(weapon_cur_addr_);
    }
    if (!in_play_.load()) wheel_taps_ = 0;

    if (const int wp = wheel_menu_phase_.load(); wp > 0) {
      if (static const bool wl = std::getenv("SF3N_FAKEWHEEL") != nullptr; wl && wheel_menu_wait_ == 0) std::fprintf(log, "RUEDA campo %llu fase %d pasos %d arma %d\n", (unsigned long long)field_count_, wp, wheel_steps_, ramByte(weapon_cur_addr_));
      ++wheel_menu_wait_;

      if (wp == 3 && wheel_steps_ == 0 && wheel_menu_wait_ >= 6 && !no_lr_block_) {
        const std::uint16_t base = rawButtonsBase();
        if (modernNow(base) && (base & 0x00a0u)) { wheel_menu_phase_ = 4; wheel_menu_wait_ = 0; if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA lateral: menu cerrado campo %llu\n", (unsigned long long)field_count_); }
      }
      if (!in_play_.load() || menu_open_) { wheel_menu_phase_ = 0; wheel_steps_ = 0; }
      else if (wp == 1 && wheel_menu_wait_ >= 16) { wheel_menu_phase_ = 3; wheel_menu_wait_ = 6; }
      else if (wp == 2 && wheel_menu_wait_ >= 4) { wheel_menu_phase_ = 3; wheel_menu_wait_ = 0; }
      else if (wp == 3 && wheel_menu_wait_ >= 6) {
        if (wheel_steps_ != 0) {
          wheel_step_bit_ = wheel_steps_ > 0 ? 0x0200 : 0x0100;
          wheel_steps_ += wheel_steps_ > 0 ? -1 : 1;
          wheel_menu_phase_ = 2; wheel_menu_wait_ = 0;
        } else if (wheel_menu_wait_ >= 45) { wheel_menu_phase_ = 4; wheel_menu_wait_ = 0; }
      } else if (wp == 4 && ((ramByte(weapon_cur_addr_) != wheel_menu_from_ && wheel_menu_wait_ >= 4) || wheel_menu_wait_ >= 30)) wheel_menu_phase_ = 0;
    }
    {
      const int wp2 = wheel_menu_phase_.load();
      modern_lr_block_ = (wp2 >= 1 && wp2 <= 3) || (wp2 == 4 && wheel_menu_wait_ < 3);
    }

    {
      bool fakeSel = false;
      if (static const char* fs = std::getenv("SF3N_FAKESELECT"); fs) { double t0 = 0, t1 = 0; if (std::sscanf(fs, "%lf:%lf", &t0, &t1) == 2) fakeSel = field_count_ >= std::uint64_t(t0 * 50) && field_count_ < std::uint64_t(t1 * 50); }
      const bool selHeld = ((rawButtonsBase() & 0x0001) || fakeSel) && wheel_menu_phase_.load() == 0;
      if (!selHeld) { menu_steps_ = 0; menu_step_on_ = 0; menu_step_gap_ = 0; }
      else {
        if (menu_step_on_.load() > 0) { menu_step_on_ = menu_step_on_.load() - 1; if (menu_step_on_.load() == 0) menu_step_gap_ = 4; }
        else if (menu_step_gap_ > 0) --menu_step_gap_;
        else if (menu_steps_ != 0) {
          menu_step_bit_ = menu_steps_ > 0 ? 0x0200 : 0x0100;
          menu_steps_ += menu_steps_ > 0 ? -1 : 1;
          menu_step_on_ = 3;
        }
      }
      if (selHeld && wheel_menu_hand_ && !menu_open_ && in_play_.load()) {
        int w0 = mouse_wheel_.exchange(0);
        if (w0) { if (wheel_invert_) w0 = -w0; menu_steps_ = std::clamp(menu_steps_ - w0, -12, 12); }
        return;
      }
    }
    if (menu_open_ || !in_play_.load()) { if (std::getenv("SF3N_FAKEWHEEL") && mouse_wheel_.load()) std::fprintf(log, "rueda: fuera de partida\n"); return; }
    int w = mouse_wheel_.exchange(0);
    if (!w) return;
    if (wheel_invert_) w = -w;

    const bool aiming = (rawButtonsBase() & 0x0400) || (aim_mode_ && nowSeconds() - last_aim_take_ < 0.3 && !(rawButtons() & 0x0300));
    if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA w %d campo %llu apuntando %d (aim_mode %d) objetivo_menu %d\n", w, (unsigned long long)field_count_, int(aiming), int(aim_mode_), int(weapon_target_));
    if (aiming) {
      if (!wheel_zoom_) return;
      zoom_bit_ = kWheelBtns[w > 0 ? wheel_in_btn_ : wheel_out_btn_].second;
      zoom_fields_ = std::min(40, std::abs(w) * 6);
      return;
    }
    if (!wheel_weapons_ || weapon_target_ >= 0) return;
    readWeapons();
    const int n = int(menu_weapons_.size());

    static const bool oldTaps = std::getenv("SF3N_RUEDA_SELECT") != nullptr;

    static const bool menuWheel = std::getenv("SF3N_RUEDA_MENU") != nullptr;
    if (n >= 2 && !oldTaps && !menuWheel && weapon_pick_hooked_) {
      const int cur = ramByte(weapon_cur_addr_);
      const int base = direct_target_.load() >= 0 ? direct_target_.load() : cur;
      int pos = 0;
      for (int i = 0; i < n; ++i) if (menu_weapons_[std::size_t(i)].index == base) pos = i;
      pos = ((pos - w) % n + n) % n;
      const int tgt = menu_weapons_[std::size_t(pos)].index;
      if (tgt == cur && wheel_tap_phase_ == 0) direct_target_ = -1;
      else {
        direct_target_ = tgt;
        if (wheel_tap_phase_ == 0) { wheel_tap_phase_ = 1; wheel_tap_wait_ = 0; wheel_tap_from_ = cur; }
      }
      if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA directo: paso %d campo %llu arma %d -> objetivo %d\n", w, (unsigned long long)field_count_, cur, direct_target_.load());
      return;
    }
    if (n >= 2 && oldTaps) { wheel_taps_ = std::min(n - 1, wheel_taps_ + (w < 0 ? -w : (n - 1) * w)); return; }
    if (n >= 2) {

      wheel_steps_ = std::clamp(wheel_steps_ - w, -(n - 1), n - 1);
      if (std::getenv("SF3N_FAKEWHEEL")) std::fprintf(log, "RUEDA paso %d campo %llu -> pasos %d\n", w, (unsigned long long)field_count_, wheel_steps_);
      if (wheel_menu_phase_.load() == 0 || wheel_menu_phase_.load() == 4) {
        if (wheel_menu_phase_.load() == 0) wheel_menu_from_ = ramByte(weapon_cur_addr_);
        wheel_menu_phase_ = 1; wheel_menu_wait_ = 0;
      }
      return;
    }
    if (std::getenv("SF3N_FAKEWHEEL")) { std::fprintf(log, "rueda %d: %d armas:", w, n); for (auto& x : menu_weapons_) std::fprintf(log, " %d%s", x.index, x.current ? "*" : ""); std::fprintf(log, "\n"); }
    if (n < 2) return;
    int pos = 0;
    for (int i = 0; i < n; ++i) if (menu_weapons_[std::size_t(i)].current) pos = i;
    pos = ((pos + (w < 0 ? 1 : -1)) % n + n) % n;
    weapon_target_ = menu_weapons_[std::size_t(pos)].index;
    weapon_deadline_ = 0;
  }
  int last_mouse_x_ = -1, last_mouse_y_ = -1;
  Clock::time_point notify_until_{};
  bool mouse_enabled_ = true, mouse_invert_ = false;
  std::atomic<bool> mouse_captured_{false};
  std::atomic<int> mouse_acc_x_{0}, mouse_acc_y_{0};
  double mouse_sens_ = 1.0, mouse_dx_ = 0, mouse_dy_ = 0;
  double yaw_accum_ = 0, pitch_accum_ = 0, mouse_turn_scale_ = 1.5, last_aim_take_ = -1e9;
  bool aim_mode_ = false;
  double aim_blend_t_ = 0, scope_ry_ = 0, scope_rp_ = 0;
  bool aim_locked_ = false;
  std::int64_t taken_yaw_ = 0, taken_pitch_ = 0;
  int aim_rest_frames_ = 0; bool aim_off_ok_ = false; double aim_off_y_ = 0, aim_off_p_ = 0;
  double aim_sy_ = std::getenv("SF3N_AIM_SY") ? std::atof(std::getenv("SF3N_AIM_SY")) : 1.0, aim_sp_ = std::getenv("SF3N_AIM_SP") ? std::atof(std::getenv("SF3N_AIM_SP")) : 1.0;
  Clock::duration field_period_ = std::chrono::microseconds(20000);
  double actor_near_cull_ = envInt("SF3N_ACTOR_NEAR", 60);
  bool mouse_direct_turn_ = true;
  ctl::Bindings binds_ = ctl::defaults();
  bool rumble_enabled_ = true;

  int capture_kind_ = 0, capture_action_ = 0;

  std::atomic<bool> title_top_{false};
  std::atomic<bool> title_restore_{false};
  std::atomic<bool> in_play_{false};
  int title_sel_ = 0;
  bool title_opts_ = false;
  std::uint16_t prev_title_raw_ = 0xffff;
  bool movement_modern_ = false;
  bool movement_free_ = false;
  std::string lang_ = "es", lang_start_ = "es";
  int vol_master_ = 10, vol_spu_ = 10, vol_cd_ = 10;
  bool mono_ = false;
  int p1_dev_ = 0;
  bool opc_uploaded_ = false;
  bool title_row_snap_ = true;
  std::atomic<bool> title_sub_{false};
  int sub_sel_ = 0; bool sub_prev_ = false; std::uint16_t prev_sub_raw_ = 0xffff;
  std::atomic<int> sub_go_{-1}; std::atomic<bool> sub_back_{false};
  bool title_cont_ok_ = false;
  std::string title_cont_path_;
  std::int64_t title_cj_seen_ = -1;
  bool title_prev_top_ = false;
  std::atomic<std::uint16_t> title_pulse_bits_{0};
  std::atomic<int> title_go_{-1};
  std::atomic<int> title_pulse_n_{0};
  std::uint64_t title_pulse_wait_ = 0;
  double lvl_t_ = 0;
  int confirm_back_page_ = 30;
 public:
  std::string restart_lang_;
 private:
  bool capture_armed_ = false;
  Clock::time_point capture_deadline_{};
  bool last_input_pad_ = false;
  Clock::time_point pad_scan_at_{};
  std::uint16_t scripted_buttons_ = 0;
  std::shared_ptr<Mission> mission_;
  std::map<std::uint32_t, std::size_t> room_cache_;
  bool vram_uploaded_ = false;
  bool building_ = false, hooks_this_field_ = false;
  Snapshot pending_;
  std::vector<RawEffectPoint> eff_raw_;
  std::uint64_t fx_actor_skip_ = 0;
  std::uint64_t eff_exact_ = 0, eff_unprojected_ = 0;
  int wlog_ = 0;
  struct EffectMap {
    std::map<std::uint32_t, std::pair<std::array<double, 3>, double>> pts;
    std::unordered_map<std::uint32_t, std::array<double, 3>> actor_pts;
    double fx = 256, fy = 227;
    struct LightPt { std::uint32_t key, keyNeg; std::array<double, 3> w; double s; std::uint32_t h, wh; double px, py, sx, sy, scy; };
    std::vector<LightPt> lights;
    std::unordered_multimap<std::uint32_t, std::array<double, 3>> all;
    Camera cam; double ofx = 0, ofy = 0; bool of_ok = false;
  };
  std::vector<EffectMap> eff_maps_;

  struct LightReg { std::array<double, 3> p{}; double votes = 0; std::uint64_t last = 0; };
  std::vector<LightReg> light_reg_;

  struct LightHistPt { std::uint32_t key, keyNeg; std::array<double, 3> w; double s; std::uint32_t h, wh; double px, py, sx, sy, scy; };
  std::vector<std::vector<LightHistPt>> light_hist_;
  std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> dbg_ra_hist_;

  struct GlowFrame { Camera cam; std::vector<EffectPrim> prims; };
  std::vector<GlowFrame> pub_glow_hist_, glow_hist_;
  std::vector<EffectPrim> glows_now_;
  std::vector<EffectPrim> pub_effects_, effects_;

  struct VCopy { int sx = 0, sy = 0, dx = 0, dy = 0, w = 0, h = 0; };
  struct TriTex { bool semi = false; std::uint16_t clut{}, tpage{}; std::array<std::pair<std::uint32_t, std::uint16_t>, 3> uv{}; std::array<std::uint32_t, 3> col{};
    bool moved = false; VCopy copy{}; std::uint16_t orig_tpage{}; };

  static bool moveTex(std::uint16_t& tpage, std::array<std::uint16_t, 3>& uv, const VCopy& c) {
    const unsigned depth = (tpage >> 7) & 3;
    if (depth > 2) return false;
    const int ppw = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    const int bx = int(tpage & 0xf) * 64, by = int((tpage >> 4) & 1) * 256;
    std::array<int, 3> x{}, y{};
    for (int k = 0; k < 3; ++k) {
      x[std::size_t(k)] = bx + (uv[std::size_t(k)] & 0xff) / ppw;
      y[std::size_t(k)] = by + (uv[std::size_t(k)] >> 8);
      if (x[std::size_t(k)] < c.dx || x[std::size_t(k)] >= c.dx + c.w || y[std::size_t(k)] < c.dy || y[std::size_t(k)] >= c.dy + c.h) return false;
    }
    const int ox = c.sx - c.dx, oy = c.sy - c.dy;
    const int page = (x[0] + ox) / 64, row = (y[0] + oy) / 256;
    if (page < 0 || page > 15 || row < 0 || row > 1) return false;
    std::array<std::uint16_t, 3> nuv{};
    for (int k = 0; k < 3; ++k) {
      const int nx = x[std::size_t(k)] + ox, ny = y[std::size_t(k)] + oy;
      if (nx / 64 != page || ny / 256 != row) return false;
      const int nu = (nx - page * 64) * ppw + (uv[std::size_t(k)] & 0xff) % ppw, nv = ny - row * 256;
      if (nu > 255 || nv > 255) return false;
      nuv[std::size_t(k)] = std::uint16_t(nu | (nv << 8));
    }
    uv = nuv;
    tpage = std::uint16_t((tpage & ~0x1fu) | unsigned(page) | unsigned(row << 4));
    return true;
  }
  using TriTexMap = std::unordered_map<std::uint64_t, TriTex>;
  static std::uint64_t triKey(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (a > b) std::swap(a, b);
    if (b > c) std::swap(b, c);
    if (a > b) std::swap(a, b);
    std::uint64_t h = a; h = h * 0x9E3779B97F4A7C15ull ^ b; h = h * 0x9E3779B97F4A7C15ull ^ c;
    return h;
  }
  std::vector<std::shared_ptr<const TriTexMap>> pub_tritex_, tritex_;
  std::uint64_t tt_hit_ = 0, tt_diff_ = 0, tt_miss_ = 0, tt_uvdiff_ = 0;
  std::map<std::uint32_t, std::uint32_t> remap_;
  struct TexMemo { bool valid = false; std::uint16_t tpage{}, clut{}; std::array<std::uint16_t, 3> uv{}; };
  struct ActorMemo { std::vector<TexMemo> tris; std::vector<std::uint32_t> wound; std::uint64_t last_frame = 0; bool has_copy = false; VCopy copy{}; };
  std::unordered_map<std::uint64_t, ActorMemo> tex_memo_;
  std::uint64_t render_frame_ = 0;
  const bool tl0_ = std::getenv("SF3N_TRITEXLOG") != nullptr;
  const bool wound_diag_ = std::getenv("SF3N_WOUNDDIAG") != nullptr;
  std::unordered_set<std::uint64_t> diag_tris_;
  int diag_logs_ = 0;
 public:
  void reportTriTex() const { std::fprintf(stderr, "tritex hit %llu dif %llu miss %llu uvdif %llu\n", (unsigned long long)tt_hit_, (unsigned long long)tt_diff_, (unsigned long long)tt_miss_, (unsigned long long)tt_uvdiff_); }
 private:
  std::vector<std::vector<std::uint32_t>> pub_overlay_, overlay_;
  double pub_effects_time_ = 0, effects_time_ = 0;
  int pub_snow_count_ = 0, snow_count_ = 0;
  std::vector<DR_TPAGE> fx_tpages_;
  std::vector<LINE_G2> fx_lines_;
  std::vector<POLY_G3> fx_g3_;

  std::shared_ptr<const Snapshot> emu_cur_;
  Clock::time_point sched_ = Clock::now(), field_start_ = Clock::now();
  Clock::duration emu_busy_{};
  std::uint64_t emu_resyncs_ = 0;
  std::atomic<std::uint64_t> frames_drawn_{0};

  struct GuestDisp { int x = 0, y = 0, w = 320, h = 240; bool rgb24 = false; };
  std::mutex mtx_;
  std::vector<std::shared_ptr<const Snapshot>> pub_hist_;
  std::vector<std::uint16_t> pub_vram_;
  std::vector<std::uint16_t> pub_frame_vram_, frame_vram_;
 public:
  const std::vector<std::uint16_t>* frame_vram_source = nullptr;
 private:
  bool frame_vram_dirty_ = false;
  GuestDisp pub_disp_;
  std::vector<std::vector<std::uint32_t>> pub_hud_;
  struct HudState { double t_first = 0, t_last = 0; std::vector<std::vector<std::uint32_t>> pk; std::vector<HudFrac> sub; int wid = -1; };

  struct RadarRec { std::uint32_t w[4]; float f[8]; std::uint64_t field; };
  std::vector<RadarRec> radar_recs_;

  struct RadarArrow { std::int32_t cx = 0, cy = 0; double p[4][2]{}; int ip[2][2]{}; std::uint64_t field = 0; bool ok = false; };
  std::vector<RadarArrow> radar_arrows_;

  struct RadarCone { std::int32_t cx = 0, cy = 0; double p[3][2]{}; std::uint64_t field = 0; };
  std::vector<RadarCone> radar_cones_;
  static bool sameSub(const std::vector<HudFrac>& a, const std::vector<HudFrac>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) for (int k = 0; k < 8; ++k) if (std::fabs(a[i].d[std::size_t(k)] - b[i].d[std::size_t(k)]) > 1e-4f) return false;
    return true;
  }
  std::vector<HudFrac> radarSub(const std::vector<std::vector<std::uint32_t>>& pk) const {
    std::vector<HudFrac> sub;
    static const bool off = std::getenv("SF3N_RADAR_ENTERO") != nullptr;
    if (off) return sub;
    for (std::size_t i = 0; i < pk.size(); ++i) {
        const auto& p = pk[i];
        if (p.empty()) continue;
        const auto c = p[0] >> 24;
        const bool tri = (c & 0xfc) == 0x30 && p.size() == 6, line = (c & 0xfc) == 0x50 && p.size() == 4;
        if (!tri && !line) continue;
        const int n = tri ? 3 : 2;

        float bestF[8]{}; double bestErr = 1e9;

        const RadarArrow* pick = nullptr;
        const std::uint64_t newest = radar_arrows_.empty() ? 0 : radar_arrows_.back().field;
        for (auto it = radar_arrows_.rbegin(); it != radar_arrows_.rend(); ++it) {
          const auto& A = *it;
          if (!A.ok) continue;
          auto at = [&](int q) { return (std::uint32_t((A.cy + A.ip[q][1]) & 0xffff) << 16) | std::uint32_t((A.cx + A.ip[q][0]) & 0xffff); };
          const bool ok = tri ? p[1] == at(1) : (p[1] == at(0) && p[3] == at(1));
          if (!ok) continue;
          if (!pick || (pick->field == newest && A.field < newest)) pick = &A;
          if (A.field < newest) break;
        }
        for (const RadarArrow* Ap : {pick}) {
          if (!Ap) continue;
          const auto& A = *Ap;
          float f[8]{}; double err = 0; bool all = true;
          for (int k = 0; k < n && all; ++k) {
            const auto w = p[std::size_t(2 * k + 1)];
            const double vx = double(std::int16_t(w & 0xffff)) - A.cx, vy = double(std::int16_t(w >> 16)) - A.cy;
            int best = -1; double bd = 2.0;
            for (int q = 0; q < 4; ++q) { const double d = std::max(std::fabs(A.p[q][0] - vx), std::fabs(A.p[q][1] - vy)); if (d < bd) { bd = d; best = q; } }
            if (best < 0) all = false;
            else { f[2 * k] = float(A.p[best][0] - vx); f[2 * k + 1] = float(A.p[best][1] - vy); err = std::max(err, bd); }
          }
          if (all && err < bestErr) { bestErr = err; std::copy(f, f + 8, bestF); }
        }
        if (static const bool rl = std::getenv("SF3N_RADARLOG") != nullptr; rl && tri) {
          std::fprintf(log, "RADAR tri campo %llu err %.2f v:", (unsigned long long)field_count_, bestErr);
          for (int k = 0; k < 3; ++k) std::fprintf(log, " %d,%d", int(std::int16_t(p[std::size_t(2 * k + 1)] & 0xffff)), int(std::int16_t(p[std::size_t(2 * k + 1)] >> 16)));
          for (const auto& A : radar_arrows_) std::fprintf(log, " | f%llu cola %d,%d (%.2f,%.2f)", (unsigned long long)A.field, A.cx + A.ip[1][0], A.cy + A.ip[1][1], A.cx + A.p[1][0], A.cy + A.p[1][1]);
          std::fprintf(log, "\n");
        }
        if (bestErr > 1e8) continue;
        if (sub.empty()) sub.assign(pk.size(), HudFrac{});
        for (int k = 0; k < 8; ++k) sub[i].d[std::size_t(k)] = bestF[k];
    }

    static const bool conos = std::getenv("SF3N_RADAR_CONO") != nullptr;
    for (std::size_t i = 0; conos && i < pk.size() && !radar_cones_.empty(); ++i) {
      const auto& p = pk[i];
      if (p.size() != 8 || ((p[0] >> 24) & 0xfe) != 0x3a) continue;
      double bestErr = 2.0; const RadarCone* best = nullptr;
      for (auto it = radar_cones_.rbegin(); it != radar_cones_.rend(); ++it) {
        const auto& K = *it;
        if (p[1] != ((std::uint32_t(K.cy & 0xffff) << 16) | std::uint32_t(K.cx & 0xffff))) continue;
        double err = 0;
        for (int k = 0; k < 3; ++k) {
          const auto w = p[std::size_t(2 * k + 3)];
          err = std::max(err, std::max(std::fabs(K.p[k][0] - (std::int16_t(w & 0xffff) - K.cx)), std::fabs(K.p[k][1] - (std::int16_t(w >> 16) - K.cy))));
        }
        if (err < bestErr) { bestErr = err; best = &K; }
      }
      if (!best) continue;
      if (sub.empty()) sub.assign(pk.size(), HudFrac{});
      for (int k = 0; k < 3; ++k) {
        const auto w = p[std::size_t(2 * k + 3)];
        sub[i].d[std::size_t(2 * k + 2)] = float(best->p[k][0] - (std::int16_t(w & 0xffff) - best->cx));
        sub[i].d[std::size_t(2 * k + 3)] = float(best->p[k][1] - (std::int16_t(w >> 16) - best->cy));
      }
    }
    if (radar_recs_.empty()) return sub;
    for (std::size_t i = 0; i < pk.size(); ++i) {
      const auto& p = pk[i];
      if (p.size() != 5 || ((p[0] >> 24) & 0xfc) != 0x28) continue;
      const std::uint64_t newestB = radar_recs_.back().field;
      bool olderMatch = false;
      for (const auto& r : radar_recs_) if (r.field < newestB && p[1] == r.w[0] && p[2] == r.w[1] && p[3] == r.w[2] && p[4] == r.w[3]) olderMatch = true;
      for (auto it = radar_recs_.rbegin(); it != radar_recs_.rend(); ++it) {
        if (p[1] != it->w[0] || p[2] != it->w[1] || p[3] != it->w[2] || p[4] != it->w[3]) continue;
        if (olderMatch && it->field == newestB) continue;
        if (sub.empty()) sub.assign(pk.size(), HudFrac{});
        for (int k = 0; k < 8; ++k) sub[i].d[std::size_t(k)] = it->f[k];
        if (static const bool rl = std::getenv("SF3N_RADARLOG") != nullptr; rl) std::fprintf(log, "RADAR casa paquete %zu campo %llu (registro del campo %llu) dy %.2f\n", i, (unsigned long long)field_count_, (unsigned long long)it->field, it->f[1]);
        break;
      }
    }
    return sub;
  }
 public:

  bool cameraFirm() const { return camera_firm_; }
  bool camera_firm_ = false;
  void radarCone(std::uint32_t c, std::int32_t ix, std::int32_t iy, double ex, double ey) {
    RadarCone K; K.cx = std::int16_t(c & 0xffff); K.cy = std::int16_t(c >> 16); K.field = field_count_;

    double x = ix, y = iy;
    const double cxe = std::clamp(ex, -24.0, 24.0), cye = std::clamp(ey, -20.0, 20.0);
    if (std::fabs(cxe - ix) < 1.0 && std::fabs(cye - iy) < 1.0) { x = cxe; y = cye; }
    const double a = std::atan2(y, x), d = 0xaa * 6.283185307179586 / 4096.0;
    int side[2]{};
    for (int q = 0; q < 2; ++q) {
      const double an = q == 0 ? a + d : a - d, co = std::cos(an), si = std::sin(an);
      double px, py;
      if (std::fabs(24.0 / std::max(1e-9, std::fabs(co))) < std::fabs(20.0 / std::max(1e-9, std::fabs(si)))) { px = co > 0 ? 24.0 : -24.0; py = 31.0 * si; side[q] = co > 0 ? 0 : 1; }
      else { py = si > 0 ? 20.0 : -20.0; px = 31.0 * co; side[q] = si > 0 ? 2 : 3; }
      K.p[q][0] = px; K.p[q][1] = py;
    }
    if (side[0] == side[1]) { K.p[2][0] = (K.p[0][0] + K.p[1][0]) / 2; K.p[2][1] = (K.p[0][1] + K.p[1][1]) / 2; }
    else {
      K.p[2][0] = std::fabs(K.p[0][0]) < std::fabs(K.p[1][0]) ? K.p[1][0] : K.p[0][0];
      K.p[2][1] = std::fabs(K.p[0][1]) < std::fabs(K.p[1][1]) ? K.p[1][1] : K.p[0][1];
    }
    radar_cones_.push_back(K);
    while (!radar_cones_.empty() && radar_cones_.front().field + 10 < field_count_) radar_cones_.erase(radar_cones_.begin());
    if (radar_cones_.size() > 48) radar_cones_.erase(radar_cones_.begin());
    if (static const bool rl = std::getenv("SF3N_RADARLOG") != nullptr; rl) std::fprintf(log, "RADAR cono campo %llu int %d,%d exacto %.2f,%.2f -> %.2f,%.2f %.2f,%.2f %.2f,%.2f\n", (unsigned long long)field_count_, ix, iy, x, y, K.p[0][0], K.p[0][1], K.p[1][0], K.p[1][1], K.p[2][0], K.p[2][1]);
  }
  void radarArrow(std::uint32_t c, std::int32_t x, std::int32_t z) {
    RadarArrow A; A.cx = std::int16_t(c & 0xffff); A.cy = std::int16_t(c >> 16); A.field = field_count_;
    auto keep = [&] { radar_arrows_.push_back(A); while (radar_arrows_.size() > 8) radar_arrows_.erase(radar_arrows_.begin()); };
    if (x == 0 && z == 0) { keep(); return; }

    auto edge = [](double dx, double dw, double out[2]) {
      if (std::fabs(dw) < std::fabs(dx)) { out[0] = dx > 0 ? 24.0 : -24.0; out[1] = 20.0 * dw / std::fabs(dx); }
      else { out[0] = 24.0 * dx / std::max(1e-9, std::fabs(dw)); out[1] = dw > 0 ? 20.0 : -20.0; }
    };
    edge(double(x), -double(z), A.p[0]);
    {
      const std::int64_t w = -std::int64_t(z), X64 = x;
      int X, Y;
      if ((w < 0 ? -w : w) < (X64 < 0 ? -X64 : X64)) { X = x > 0 ? 24 : -24; Y = int((-20 * std::int64_t(z)) / (x > 0 ? X64 : -X64)); }
      else if (w > 0) { X = int((24 * X64) / w); Y = 20; }
      else { X = w == 0 ? 0 : int((24 * X64) / (-w)); Y = -20; }
      A.ip[0][0] = X; A.ip[0][1] = Y;
      A.ip[1][0] = X + int((9 * X64) / 4096); A.ip[1][1] = Y - int((7 * std::int64_t(z)) / 4096);
    }
    A.p[1][0] = A.p[0][0] + 9.0 * x / 4096.0; A.p[1][1] = A.p[0][1] - 7.0 * z / 4096.0;
    const double a = std::atan2(double(z), double(x)), d = 0x71 * 6.283185307179586 / 4096.0;
    edge(std::cos(a + d), -std::sin(a + d), A.p[2]);
    edge(std::cos(a - d), -std::sin(a - d), A.p[3]);
    A.ok = true;
    keep();
    if (static const bool rl = std::getenv("SF3N_RADARLOG") != nullptr; rl) std::fprintf(log, "RADAR flecha campo %llu x %d z %d -> %.2f,%.2f cola %.2f,%.2f lados %.2f,%.2f %.2f,%.2f\n", (unsigned long long)field_count_, x, z, A.p[0][0], A.p[0][1], A.p[1][0], A.p[1][1], A.p[2][0], A.p[2][1], A.p[3][0], A.p[3][1]);
  }
  void radarBlip(std::uint32_t c, std::int32_t ix, std::int32_t iy, double ex, double ey) {
    if (std::fabs(ex - ix) > 2.0 || std::fabs(ey - iy) > 2.0) return;
    const int cx = std::clamp(ix, -24, 24), cy = std::clamp(iy, -20, 20);
    const double fx = std::clamp(ex, -24.0, 24.0), fy = std::clamp(ey, -20.0, 20.0);
    RadarRec r{}; r.field = field_count_;
    for (int k = 0; k < 4; ++k) {
      const int vx = (k & 1) ? std::min(cx + 1, 24) : std::max(cx - 1, -24);
      const int vy = (k & 2) ? std::min(cy + 1, 20) : std::max(cy - 1, -20);
      const double dx = (k & 1) ? std::min(fx + 1.0, 24.0) : std::max(fx - 1.0, -24.0);
      const double dy = (k & 2) ? std::min(fy + 1.0, 20.0) : std::max(fy - 1.0, -20.0);
      r.w[k] = (std::uint32_t((std::int16_t(c >> 16) + vy) & 0xffff) << 16) | std::uint32_t((std::int16_t(c & 0xffff) + vx) & 0xffff);
      r.f[2 * k] = float(dx - vx); r.f[2 * k + 1] = float(dy - vy);
    }
    radar_recs_.push_back(r);
    while (!radar_recs_.empty() && radar_recs_.front().field + 10 < field_count_) radar_recs_.erase(radar_recs_.begin());
    if (radar_recs_.size() > 160) radar_recs_.erase(radar_recs_.begin());
    if (static const bool rl = std::getenv("SF3N_RADARLOG") != nullptr; rl) std::fprintf(log, "RADAR campo %llu centro %08x int %d,%d exacto %.2f,%.2f\n", (unsigned long long)field_count_, c, ix, iy, ex, ey);
  }
 private:
  std::vector<HudState> pub_hud_hist_, hud_hist_;
  bool pub_hud_ok_ = false;
  std::vector<std::vector<std::uint32_t>> pub_frame_, frame_;
  std::uint64_t pub_frame_seq_ = 0, frame_seq_seen_ = 0;
  std::uint64_t pub_frame_field_ = 0, frame_field_ = 0;
  bool game_frozen_ = false;
  std::uint64_t pub_vram_seq_ = 0;
  std::uint64_t pub_tex_hash_ = 0, emu_tex_hash_ = 0, tex_hash_ = 0;
  bool pub_disp_black_ = false, disp_black_ = false;
  std::string pub_title_;

  std::vector<std::shared_ptr<const Snapshot>> rhist_;
  std::shared_ptr<const Snapshot> rcur_, rprev_;
  double alpha_ = 1.0;
  std::vector<std::uint16_t> vram_;
  GuestDisp disp_;
  std::vector<std::vector<std::uint32_t>> hud_;
  bool hud_ok_ = false;
  bool sub_area_ = false;
  std::uint64_t vram_seq_seen_ = 0;
  static inline const Snapshot empty_snapshot_{};
  const Snapshot& cur() const { return rcur_ ? *rcur_ : empty_snapshot_; }
  const Snapshot& prv() const { return rprev_ ? *rprev_ : empty_snapshot_; }
  const Mission& mis() const { return *cur().mission; }
  Ctl pending_ctl_{};
  Ctl last_part_ctl_{};
  int phase_ = 0;
  bool in_actor_ = false;
  int cur_room_ = -1;
  std::uint32_t cur_room_a0_ = 0;
  bool have_camera_ = false;
  double pub_field_time_ = 0, field_time_ = 0, render_time_ = 0;
  std::uint64_t pub_field_no_ = 0, field_no_ = 0;
  std::uint64_t last_snapshot_field_ = 0, last_mdec_field_ = 0, last_mdec_blocks_ = 0;
  int last_path_ = 0;
  int quiet_fields_ = 0;
  bool turbo_on_ = false;
  std::atomic<bool> turbo_enabled_{envInt("SF3N_TURBO", 1) != 0};
  std::vector<RawActor> pending_actors_;
  std::vector<RawObject> pending_objects_;
  std::map<std::uint32_t, int> model_cache_;
  std::span<const std::byte> ram_;
  std::vector<std::array<double, 3>> world_verts_;
  std::vector<std::array<double, 3>> vert_colors_;
  std::vector<unsigned char> part_ok_;
  std::uint64_t snapshot_count_ = 0, field_count_ = 0;
 public:
  std::atomic<std::uint64_t> game_field_{0};
 private:
  std::vector<OT_TAG> ot_, ot_semi_, ot_shadow_, ot_room_;
  bool room_pass_ = false;
  std::atomic<int> fps_cap_{envInt("SF3N_FPS", 0)};
  std::map<std::pair<std::uint16_t, std::uint16_t>, std::vector<std::uint32_t>> live_cache_; const Mission* live_cache_mission_ = nullptr;
  static inline const double kFlashR = std::getenv("SF3N_FOGONAZO_ALCANCE") ? std::atof(std::getenv("SF3N_FOGONAZO_ALCANCE")) : 800.0;
  std::vector<double> flash_hist_; double flash_burst_ = 1.0;
  double taser_amt_ = 0, taser_t_ = 0, taser_seen_ = -1e9; std::array<double, 3> taser_c_{};
  double flash_amp_ = -1.0;
  std::vector<std::array<double, 4>> flash_pts_, flash_seen_;
  double flash_gain_ = 1.0, flash_base_ = 0.0; const Mission* flash_mission_ = nullptr;

  std::vector<POLY_F3> shadow_prims_;
  struct ShadowState { std::array<double, 3> dir{0, 1, 0}; bool init = false; std::uint64_t last_frame = 0; };
  std::unordered_map<std::uint64_t, ShadowState> shadow_state_;
  int shadows_mode_ = envInt("SF3N_SOMBRAS", 1);
  std::vector<POLY_GT3> tris_;
  std::vector<POLY_GT4> quads_;
  std::vector<double> depth_;
  bool guest_dirty_ = false;
  bool guest_vram_dirty_ = false;
  int gw_ = 0, gh_ = 0, slice_count_ = 0;
  std::vector<unsigned char> rgba_;
  std::vector<Slice> slices_;
  bool native_mode_ = true, pip_ = false, full_ = false;
  std::map<int, bool> prev_keys_;

};

inline std::string BootWindow::iniGet(const char* key, const char* d) { return iniString(key, d); }
}

using nat::BootWindow;
