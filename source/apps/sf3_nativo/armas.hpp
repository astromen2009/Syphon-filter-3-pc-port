#pragma once
#include "pakfs.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <thread>
#include <string>
#include <vector>

namespace nat {

inline const std::vector<std::string>& weaponTableNames() {
  static const std::vector<std::string> v = {
      "GLOCK17", "GLOKSIL", "COLT45", "FALCON", "GLOCK18", "MP5", "MP5S", "AK102", "M16", "BIZON2", "H11",
      "AU300", "MARS", "SPYDER", "G3", "BERELLI", "UAS12", "MIL15", "MAKS", "SUPERG", "SUPERGS", "DRAGSVD",
      "TASER", "GRENLAUN", "FLAMETRK", "CHNGUN", "TEARGUN", "CROSSBOW", "TASERH", "KNIFE", "GRENADE", "GASGREN", "STRYKER"};
  return v;
}

inline std::string weaponIdName(int id) {
  const auto& n = weaponTableNames();
  if (id >= 1 && id <= 32) return n[std::size_t(id - 1)];
  if (id == 37) return "STRYKER";
  return {};
}
inline int weaponGameId(const std::string& name) {
  for (int id = 1; id <= 39; ++id) if (weaponIdName(id) == name) return id;
  return 0;
}

struct WeaponConfig { bool humo = true; std::string foto, icono, sonido; int volumen_sonido = 100; };
inline std::map<std::string, WeaponConfig>& weaponConfigs() { static std::map<std::string, WeaponConfig> m; return m; }
inline std::string& weaponConfigDir() { static std::string d; return d; }
inline bool weaponSmokeDefault(const std::string& n) {
  for (const char* x : {"TASER", "TASERH", "CROSSBOW", "KNIFE", "GRENADE", "GASGREN", "FLAMETRK"}) if (n == x) return false;
  return true;
}
inline constexpr const char* kSoundHelp =
    "#   sonido=ARCHIVO    sonido del disparo: WAV (cualquier frecuencia, mono o estereo;\r\n"
    "#                     se pasa a mono), ruta desde la carpeta armas, p. ej.\r\n"
    "#                     sonidos/MIDISPARO.wav. Vacio = el del juego. Los sonidos que ya\r\n"
    "#                     usa el juego estan en sonidos/originales (se crean al arrancar).\r\n"
    "#   volumen_sonido=N  volumen de ese WAV en % (100 = tal cual, 50 = la mitad).\r\n";

inline void loadWeaponConfig(const std::string& armasDir, FILE* log) {
  if (weaponConfigDir() == armasDir) return;
  weaponConfigDir() = armasDir;
  auto& cfg = weaponConfigs(); cfg.clear();
  for (const auto& w : weaponTableNames()) cfg[w].humo = weaponSmokeDefault(w);
  const std::string path = armasDir + "/armas.txt";
  std::string text;

  std::error_code fec;

  const bool exists = pakfs::exists(path);
  const bool writable = pakfs::looseDir(armasDir);
  for (int tries = 0; tries < 10; ++tries) {
    text.clear();
    pakfs::read(path, text);
    if (!exists || !text.empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
  else if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE) {
    std::string a8; for (std::size_t i = 2; i + 1 < text.size(); i += 2) { const unsigned c = (unsigned char)text[i] | ((unsigned char)text[i + 1] << 8); a8 += c < 0x80 ? char(c) : '?'; }
    text = std::move(a8);
  }
  if (exists && text.empty()) {
    if (log) std::fprintf(log, "armas: no se pudo leer %s (bloqueado?): se usan los valores de siempre y NO se toca el archivo\n", path.c_str());
    return;
  }
  const bool isConfig = text.find("\n[1 ") != std::string::npos || text.rfind("[1 ", 0) == 0 || text.find("CONFIGURACION DE LAS ARMAS") != std::string::npos;
  if (isConfig) {

    if (const auto at = text.find("[33 STRYKER]"); at != std::string::npos) {
      text.replace(at, 12, "[37 STRYKER]");
      if (FILE* f = writable ? std::fopen(path.c_str(), "wb") : nullptr) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
    }

    if (text.find("sonido=") == std::string::npos) {
      std::string nt; std::size_t p2 = 0;
      while (p2 < text.size()) {
        std::size_t q = text.find('\n', p2); const bool last = q == std::string::npos; if (last) q = text.size() - 1;
        std::string line = text.substr(p2, q - p2 + 1); p2 = q + 1;
        nt += line;
        std::string tl = line; while (!tl.empty() && (tl.back() == '\n' || tl.back() == '\r')) tl.pop_back();
        if (tl.rfind("icono=", 0) == 0) { if (line.back() != '\n') nt += "\r\n"; nt += "sonido=\r\nvolumen_sonido=100\r\n"; }
        else if (tl.rfind("#                     o iconos/NOMBRE.bmp", 0) == 0) nt += kSoundHelp;
      }
      text = nt;
      if (FILE* f = writable ? std::fopen(path.c_str(), "wb") : nullptr) { std::fwrite(text.data(), 1, text.size(), f); std::fclose(f); }
      if (log) std::fprintf(log, "armas: armas.txt actualizado con sonido= y volumen_sonido=\n");
    }
    std::string cur; std::size_t p = 0;
    while (p < text.size()) {
      std::size_t q = text.find('\n', p); if (q == std::string::npos) q = text.size();
      std::string line = text.substr(p, q - p); p = q + 1;
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
      std::size_t s0 = line.find_first_not_of(" \t"); if (s0 == std::string::npos) continue; line = line.substr(s0);
      if (line[0] == '#') continue;
      if (line[0] == '[') {
        const auto e = line.find(']'); std::string in = line.substr(1, e == std::string::npos ? std::string::npos : e - 1);
        const auto sp = in.find(' '); cur = sp == std::string::npos ? in : in.substr(sp + 1);
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        for (auto& c : cur) c = char(std::toupper(static_cast<unsigned char>(c)));
        continue;
      }
      const auto eq = line.find('='); if (eq == std::string::npos || cur.empty()) continue;
      std::string k = line.substr(0, eq), v = line.substr(eq + 1);
      while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
      std::size_t v0 = v.find_first_not_of(" \t"); v = v0 == std::string::npos ? std::string() : v.substr(v0);
      std::string vl = v; for (auto& c : vl) c = char(std::tolower(static_cast<unsigned char>(c)));
      auto& w = cfg[cur];
      if (k == "humo") w.humo = !(vl == "no" || vl == "0" || vl == "falso");
      else if (k == "foto") w.foto = v;
      else if (k == "icono") w.icono = v;
      else if (k == "sonido") {
        w.sonido = v; for (auto& c : w.sonido) if (c == '\\') c = '/';
        while (!w.sonido.empty() && w.sonido.front() == '/') w.sonido.erase(0, 1);
      }
      else if (k == "volumen_sonido") w.volumen_sonido = std::clamp(std::atoi(v.c_str()), 0, 400);
    }
    if (log) std::fprintf(log, "armas: configuracion leida de %s (%zu armas)\n", path.c_str(), cfg.size());
    return;
  }

  if (!writable) return;
  if (exists) {
    char nm[64]; const std::time_t t = std::time(nullptr); std::strftime(nm, sizeof nm, "armas_respaldo_%Y%m%d_%H%M%S.txt", std::localtime(&t));
    std::filesystem::copy_file(path, armasDir + "/" + nm, std::filesystem::copy_options::overwrite_existing, fec);
    if (log) std::fprintf(log, "armas: %s no parece la configuracion: copia en %s antes de escribir la plantilla\n", path.c_str(), nm);
  }
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fprintf(f, "# CONFIGURACION DE LAS ARMAS DE SF3 NATIVO (este archivo lo puedes editar)\r\n");
    std::fprintf(f, "# Una seccion por arma: [id NOMBRE]. Cierra el juego antes de cambiarlo.\r\n");
    std::fprintf(f, "#   humo=si / no      humo del fogonazo al disparar\r\n");
    std::fprintf(f, "#   foto=ARCHIVO      foto del menu Armamento (TIM de 4 bits 120x82, ruta desde\r\n");
    std::fprintf(f, "#                     la carpeta armas, p. ej. menu/MIFOTO.TIM). Vacio = la de siempre.\r\n");
    std::fprintf(f, "#   icono=ARCHIVO     icono del HUD (abajo a la derecha): imagen BMP, el negro puro\r\n");
    std::fprintf(f, "#                     es transparente (p. ej. iconos/MIICONO.bmp). Vacio = el del juego,\r\n");
    std::fprintf(f, "#                     o iconos/NOMBRE.bmp si el juego no tiene icono para esa arma.\r\n");
    std::fprintf(f, "%s", kSoundHelp);
    std::fprintf(f, "# Si borras una linea se usa el valor de siempre. El informe de donde sale cada\r\n");
    std::fprintf(f, "# modelo esta en informe_armas.txt (se rehace al arrancar).\r\n");
    const auto& names = weaponTableNames();
    for (std::size_t i = 0; i < names.size(); ++i)
      std::fprintf(f, "\r\n[%d %s]\r\nhumo=%s\r\nfoto=\r\nicono=\r\nsonido=\r\nvolumen_sonido=100\r\n", weaponGameId(names[i]), names[i].c_str(), cfg[names[i]].humo ? "si" : "no");
    std::fclose(f);
    if (log) std::fprintf(log, "armas: plantilla de configuracion escrita en %s\n", path.c_str());
  }
}
inline const WeaponConfig* weaponConfigFor(std::string name) {
  if (const auto d = name.find('.'); d != std::string::npos) name = name.substr(0, d);
  auto& m = weaponConfigs(); auto it = m.find(name); return it == m.end() ? nullptr : &it->second;
}

struct WeaponBank {
  struct Model {
    std::string gmd;
    std::string source;
    std::vector<std::byte> bytes;
    std::uint32_t addr = 0;
    struct Tri { std::uint16_t tpage = 0; std::array<std::uint8_t, 6> uv{}; bool ok = false; };
    std::vector<Tri> tris;
  };
  std::vector<Model> models;
  std::map<std::string, std::size_t> by_name;
  std::map<std::uint32_t, std::size_t> by_addr;
  std::vector<std::vector<std::uint16_t>> groups;
  static constexpr int kFirstAlias = 64;
  static constexpr int kScopeAlias = 120;
  static constexpr std::uint32_t kBase = 0x80600000u;
  bool ok = false;

  static bool globalPage(std::uint16_t tp) { const unsigned p = tp & 0x1fu; return p == 0x0c || p == 0x0d || p == 0x1c || p == 0x1f; }

  static std::uint16_t aliasTpage(int page, std::uint16_t flags) {
    return std::uint16_t(((page % 63) + 1) * 1024 + (page / 63) + (flags & 0x3e0u));
  }
};

}
