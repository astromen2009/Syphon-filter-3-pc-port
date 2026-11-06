// Syphon Filter 3 PC: lanzador de Windows (configuracion).
#pragma once
#include "pc_textos.hpp"

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>
#include <fstream>
#include <map>
#include "controls.hpp"
#include "recursos.hpp"
#include <sstream>
#include <cstring>

namespace natlaunch {

inline HBITMAP memBitmap(const std::vector<std::uint8_t>& bmp) {
  if (bmp.size() <= 54 || bmp[0] != 'B' || bmp[1] != 'M') return nullptr;
  std::uint32_t bitsOff; std::memcpy(&bitsOff, bmp.data() + 10, 4);
  const auto* bi = reinterpret_cast<const BITMAPINFO*>(bmp.data() + 14);
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP h = CreateDIBSection(dc, bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (h && bits && bitsOff < bmp.size()) {
    const std::size_t rows = std::size_t(bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight);
    const std::size_t stride = ((std::size_t(bi->bmiHeader.biWidth) * bi->bmiHeader.biBitCount + 31) / 32) * 4;
    std::memcpy(bits, bmp.data() + bitsOff, std::min(stride * rows, bmp.size() - bitsOff));
  }
  return h;
}

using IniGet = std::function<std::string(const char*, const char*)>;
using IniSet = std::function<void(const std::string&, const std::string&)>;

inline std::wstring W(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring w(std::size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
  return w;
}

struct State {
  IniGet get;
  IniSet set;
  bool en = false;
  HINSTANCE inst = nullptr;
  HWND main = nullptr, cfg = nullptr;
  HBITMAP bg = nullptr;
  int w = 720, h = 480;
  double sc = 1.0;
  int sel = 0, hover = -1;
  RECT btn[3]{};
  int result = 0;
  bool done = false;
  HFONT fontBtn = nullptr;
  std::string dataDir;
  HWND ctlWin = nullptr;
  int lang = 0;
  const wchar_t* tr(const wchar_t* es, const wchar_t* en_) const {
    if (lang == 2) {
      static const std::map<std::wstring, const wchar_t*> ru = {
      {L"Aceptar", L"ОК"},
      {L"Ampliados", L"Увеличенные"},
      {L"Anisotrópico", L"Анизотропная"},
      {L"Aplicar", L"Применить"},
      {L"Bilineal", L"Билинейная"},
      {L"Cancelar", L"Отмена"},
      {L"Clásico", L"Классика"},
      {L"Como en PS1", L"Как на PS1"},
      {L"Configuración - Syphon Filter 3", L"Настройки - Syphon Filter 3"},
      {L"Configurar teclas y mando...", L"Настроить клавиши и геймпад..."},
      {L"Controles", L"Управление"},
      {L"Dispositivo:", L"Устройство:"},
      {L"El jugador 2 juega con:", L"Игрок 2 играет на:"},
      {L"Estéreo", L"Стерео"},
      {L"Filtrado de texturas", L"Фильтрация текстур"},
      {L"Formato", L"Формат"},
      {L"General", L"Общие"},
      {L"Gráficos", L"Графика"},
      {L"HUD en los bordes de la pantalla", L"HUD по краям экрана"},
      {L"Idioma de las voces", L"Язык озвучки"},
      {L"Idioma de los textos", L"Язык текста"},
      {L"Invertir el ratón (arriba/abajo)", L"Инверсия мыши (вверх/вниз)"},
      {L"Invertir la rueda", L"Инверсия колеса"},
      {L"Jugador 1", L"Игрок 1"},
      {L"Jugador 2", L"Игрок 2"},
      {L"Jugador:", L"Игрок:"},
      {L"Límite de FPS", L"Ограничение FPS"},
      {L"Mando", L"Геймпад"},
      {L"Menú principal de alta calidad", L"Главное меню в высоком качестве"},
      {L"Moderno", L"Современное"},
      {L"Música y efectos", L"Музыка и эффекты"},
      {L"No", L"Нет"},
      {L"Original 4:3", L"Оригинал 4:3"},
      {L"Panorámico", L"Широкий"},
      {L"Pantalla completa", L"Полный экран"},
      {L"Pixelado", L"Пиксели"},
      {L"Resolución", L"Разрешение"},
      {L"Rueda al apuntar: zoom de la mira", L"Колесо при прицеливании: зум прицела"},
      {L"Rueda del ratón: cambiar de arma", L"Колесо мыши: смена оружия"},
      {L"Rueda en el menú de armas", L"Колесо в меню оружия"},
      {L"Salida", L"Вывод звука"},
      {L"Sensibilidad del ratón", L"Чувствительность мыши"},
      {L"Sin límite", L"Без ограничения"},
      {L"Sincronía vertical", L"Вертикальная синхронизация"},
      {L"Sombras", L"Тени"},
      {L"Sonido", L"Звук"},
      {L"Tamaño del HUD", L"Размер HUD"},
      {L"Teclado y ratón", L"Клавиатура и мышь"},
      {L"Teclado", L"Клавиатура"},
      {L"Teclas y mando - Syphon Filter 3", L"Клавиши и геймпад - Syphon Filter 3"},
      {L"Tipo de movimiento", L"Тип движения"},
      {L"Trilineal", L"Трилинейная"},
      {L"Valores de fábrica", L"По умолчанию"},
      {L"Vibración del mando", L"Вибрация геймпада"},
      {L"Voces y vídeos", L"Голоса и видео"},
      {L"Volumen general", L"Общая громкость"},
      {L"Vídeos", L"Видео"},
      {L"(sin asignar)", L"(не назначено)"},
      {L"Pulsa una tecla...", L"Нажмите клавишу..."},
      {L"Pulsa una tecla o botón del ratón...", L"Нажмите клавишу или кнопку мыши..."},
      {L"Pulsa un botón del mando...", L"Нажмите кнопку геймпада..."},
      {L"Esc: cancelar   -   Supr: dejar sin asignar", L"Esc: отмена   -   Del: очистить"},
      {L"Haz clic en una acción para cambiar su tecla o botón.", L"Щёлкните действие, чтобы сменить клавишу или кнопку."},
      {L"(no está controles_mando.bmp)", L"(нет controles_mando.bmp)"}};
      if (auto it = ru.find(es); it != ru.end()) return it->second;
    }
    return en ? en_ : es;
  }
};
inline State& st() { static State s; return s; }

inline void buildBackground(const std::string& bmpPath) {
  State& S = st();
  HDC scr = GetDC(nullptr);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = S.w; bi.bmiHeader.biHeight = -S.h;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  S.bg = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HDC dc = CreateCompatibleDC(scr);
  HGDIOBJ old = SelectObject(dc, S.bg);
  RECT all{0, 0, S.w, S.h};
  HBRUSH dark = CreateSolidBrush(RGB(10, 14, 24));
  FillRect(dc, &all, dark);
  DeleteObject(dark);
  HBITMAP fondo = HBITMAP(LoadImageW(nullptr, W(bmpPath).c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
  if (!fondo) fondo = memBitmap(res::get("lanzador_fondo.bmp"));
  if (HBITMAP img = fondo) {
    BITMAP b{};
    GetObject(img, sizeof b, &b);
    HDC src = CreateCompatibleDC(scr);
    HGDIOBJ o2 = SelectObject(src, img);

    const double k = std::max(double(S.w) / b.bmWidth, double(S.h) / b.bmHeight);
    const int sw = int(S.w / k), sh = int(S.h / k);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, 0, 0, S.w, S.h, src, (b.bmWidth - sw) / 2, (b.bmHeight - sh) / 2, sw, sh, SRCCOPY);
    SelectObject(src, o2);
    DeleteDC(src);
    DeleteObject(img);
  }

  auto* px = static_cast<std::uint8_t*>(bits);
  const int band = int(110 * S.sc);
  for (int y = std::max(0, S.h - band); y < S.h; ++y) {
    const double t = double(y - (S.h - band)) / band;
    const double keep = 1.0 - 0.78 * std::min(1.0, t * 1.6);
    for (int x = 0; x < S.w; ++x) {
      std::uint8_t* p = px + (std::size_t(y) * S.w + x) * 4;
      for (int c = 0; c < 3; ++c) p[c] = std::uint8_t(p[c] * keep);
    }
  }

  HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 70, 90));
  HGDIOBJ op = SelectObject(dc, pen);
  HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, 0, 0, S.w, S.h);
  SelectObject(dc, op); SelectObject(dc, ob);
  DeleteObject(pen);
  SelectObject(dc, old);
  DeleteDC(dc);
  ReleaseDC(nullptr, scr);
}

inline const wchar_t* btnText(int i) {
  const State& S = st();
  static const wchar_t* es[3] = {L"JUGAR", L"CONFIGURACIÓN", L"SALIR"};
  static const wchar_t* en[3] = {L"PLAY", L"SETTINGS", L"EXIT"};
  static const wchar_t* ru[3] = {L"ИГРАТЬ", L"НАСТРОЙКИ", L"ВЫХОД"};
  return S.lang == 2 ? ru[i] : S.en ? en[i] : es[i];
}

inline void layoutButtons(HDC dc) {
  State& S = st();
  HGDIOBJ of = SelectObject(dc, S.fontBtn);
  const int y = S.h - int(58 * S.sc), gap = int(52 * S.sc);
  SIZE sz[3]{};
  int tot = 0;
  for (int i = 0; i < 3; ++i) { GetTextExtentPoint32W(dc, btnText(i), int(wcslen(btnText(i))), &sz[i]); tot += sz[i].cx; }
  tot += gap * 2;
  int x = (S.w - tot) / 2;
  for (int i = 0; i < 3; ++i) {
    S.btn[i] = RECT{x - int(10 * S.sc), y - int(6 * S.sc), x + sz[i].cx + int(10 * S.sc), y + sz[i].cy + int(8 * S.sc)};
    x += sz[i].cx + gap;
  }
  SelectObject(dc, of);
}

inline void paintMain(HWND hwnd) {
  State& S = st();
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP buf = CreateCompatibleBitmap(hdc, S.w, S.h);
  HGDIOBJ ob = SelectObject(mem, buf);
  HDC bgdc = CreateCompatibleDC(hdc);
  HGDIOBJ og = SelectObject(bgdc, S.bg);
  BitBlt(mem, 0, 0, S.w, S.h, bgdc, 0, 0, SRCCOPY);
  SelectObject(bgdc, og);
  DeleteDC(bgdc);
  layoutButtons(mem);
  HGDIOBJ of = SelectObject(mem, S.fontBtn);
  SetBkMode(mem, TRANSPARENT);
  for (int i = 0; i < 3; ++i) {
    const bool on = i == S.sel;
    RECT r = S.btn[i];
    RECT t{r.left + int(10 * S.sc), r.top + int(6 * S.sc), r.right, r.bottom};
    RECT sh = t; OffsetRect(&sh, int(2 * S.sc), int(2 * S.sc));
    SetTextColor(mem, RGB(0, 0, 0));
    DrawTextW(mem, btnText(i), -1, &sh, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(mem, on ? RGB(255, 255, 255) : RGB(170, 176, 188));
    DrawTextW(mem, btnText(i), -1, &t, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    if (on) {
      RECT u{r.left + int(10 * S.sc), r.bottom - int(3 * S.sc), r.right - int(10 * S.sc), r.bottom - int(1 * S.sc)};
      HBRUSH g = CreateSolidBrush(RGB(120, 210, 70));
      FillRect(mem, &u, g);
      DeleteObject(g);
    }
  }
  SelectObject(mem, of);
  BitBlt(hdc, 0, 0, S.w, S.h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, ob);
  DeleteObject(buf);
  DeleteDC(mem);
  EndPaint(hwnd, &ps);
}

struct Ctl { int tab; HWND label, ctl; };
struct Cfg {
  HWND tabs = nullptr;
  std::vector<Ctl> ctls;
  HFONT font = nullptr;
  int y[4] = {0, 0, 0, 0};
  std::vector<std::pair<int, int>> modes;

  HWND hqMenu = nullptr;
  HWND onName = nullptr, onPort = nullptr, onPeers = nullptr;
  HWND mov, lang, voice, res, full, aa, filt, aspect, vids, vsync, fps, shadows, hud, hudEdges, turbo;
  HWND vol[3], mono, rumble, sens, sensVal, inv, wArm, wZoom, wInv, wMenu, volVal[3];
};
inline Cfg& cf() { static Cfg c; return c; }
enum { kIdTabs = 100, kIdOk = 1, kIdCancel = 2, kIdApply = 3, kIdControls = 10 };
inline void openControls();

inline int S2(double v) { return int(v * st().sc); }

inline HWND addRow(HWND dlg, int tab, const wchar_t* label, const wchar_t* cls, DWORD style, int ctlW, int ctlH = 0) {
  Cfg& C = cf();
  const int x0 = S2(28), lw = S2(170), y = C.y[tab];
  HWND lab = nullptr;
  if (label) {
    lab = CreateWindowExW(0, L"STATIC", label, WS_CHILD | SS_LEFT, x0, y + S2(4), lw, S2(20), dlg, nullptr, st().inst, nullptr);
    SendMessageW(lab, WM_SETFONT, WPARAM(C.font), TRUE);
  }
  const int cx = label ? x0 + lw : x0;
  HWND c = CreateWindowExW(0, cls, label ? L"" : nullptr, WS_CHILD | WS_TABSTOP | style, cx, y, ctlW, ctlH ? ctlH : S2(24), dlg, nullptr, st().inst, nullptr);
  SendMessageW(c, WM_SETFONT, WPARAM(C.font), TRUE);
  C.ctls.push_back({tab, lab, c});
  C.y[tab] += S2(32);
  return c;
}
inline HWND addCombo(HWND dlg, int tab, const wchar_t* label, const std::vector<std::wstring>& items, int sel) {
  HWND c = addRow(dlg, tab, label, WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL, S2(220), S2(300));
  for (const auto& it : items) SendMessageW(c, CB_ADDSTRING, 0, LPARAM(it.c_str()));
  SendMessageW(c, CB_SETCURSEL, WPARAM(std::max(0, sel)), 0);
  return c;
}
inline HWND addCheck(HWND dlg, int tab, const wchar_t* text, bool on) {
  HWND c = addRow(dlg, tab, nullptr, L"BUTTON", BS_AUTOCHECKBOX, S2(390));
  SetWindowTextW(c, text);
  SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
  return c;
}
inline HWND addSlider(HWND dlg, int tab, const wchar_t* label, int lo, int hi, int val, HWND* valLabel) {
  Cfg& C = cf();
  const int y = C.y[tab];
  HWND c = addRow(dlg, tab, label, TRACKBAR_CLASSW, TBS_HORZ | TBS_AUTOTICKS, S2(180), S2(28));
  SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
  SendMessageW(c, TBM_SETTICFREQ, hi - lo > 12 ? 5 : 1, 0);
  SendMessageW(c, TBM_SETPOS, TRUE, val);
  *valLabel = CreateWindowExW(0, L"STATIC", std::to_wstring(val).c_str(), WS_CHILD | SS_LEFT, S2(28 + 170 + 186), y + S2(4), S2(40), S2(20), dlg, nullptr, st().inst, nullptr);
  SendMessageW(*valLabel, WM_SETFONT, WPARAM(C.font), TRUE);
  C.ctls.push_back({tab, nullptr, *valLabel});
  return c;
}
inline void addNote(HWND dlg, int tab, const wchar_t* text) {
  Cfg& C = cf();
  HWND n = CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_LEFT, S2(28), C.y[tab] + S2(6), S2(400), S2(40), dlg, nullptr, st().inst, nullptr);
  SendMessageW(n, WM_SETFONT, WPARAM(C.font), TRUE);
  C.ctls.push_back({tab, nullptr, n});
  C.y[tab] += S2(44);
}
inline void showTab(int t) {
  for (const auto& c : cf().ctls) {
    if (c.label) ShowWindow(c.label, c.tab == t ? SW_SHOW : SW_HIDE);
    ShowWindow(c.ctl, c.tab == t ? SW_SHOW : SW_HIDE);
  }
}
inline int cbSel(HWND c) { return int(SendMessageW(c, CB_GETCURSEL, 0, 0)); }
inline bool ckOn(HWND c) { return SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED; }
inline int tbPos(HWND c) { return int(SendMessageW(c, TBM_GETPOS, 0, 0)); }
inline int toInt(const std::string& s, int d) { try { return std::stoi(s); } catch (...) { return d; } }

inline const int kAaOpts[5] = {0, -1, 2, 4, 8};
inline const int kFpsOpts[9] = {30, 60, 72, 90, 120, 144, 165, 240, 0};
inline const int kHudOpts[4] = {75, 100, 125, 150};

inline void buildConfig(HWND dlg) {
  State& S = st();
  Cfg& C = cf();
  C.ctls.clear();
  for (int& y : C.y) y = S2(58);
  NONCLIENTMETRICSW ncm{}; ncm.cbSize = sizeof ncm;
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
  C.font = CreateFontIndirectW(&ncm.lfMessageFont);
  RECT rc; GetClientRect(dlg, &rc);
  C.tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, S2(10), S2(10), rc.right - S2(20), rc.bottom - S2(64), dlg, HMENU(INT_PTR(kIdTabs)), S.inst, nullptr);
  SendMessageW(C.tabs, WM_SETFONT, WPARAM(C.font), TRUE);
  const wchar_t* tn[4] = {S.tr(L"General", L"General"), S.tr(L"Gráficos", L"Graphics"), S.tr(L"Sonido", L"Sound"), S.tr(L"Controles", L"Controls")};
  for (int i = 0; i < 4; ++i) { TCITEMW it{}; it.mask = TCIF_TEXT; it.pszText = const_cast<wchar_t*>(tn[i]); SendMessageW(C.tabs, TCM_INSERTITEMW, i, LPARAM(&it)); }
  auto g = [&](const char* k, const char* d) { return S.get(k, d); };

  const std::string mv = g("movimiento", "clasico");
  C.mov = addCombo(dlg, 0, S.tr(L"Tipo de movimiento", L"Movement type"), {S.tr(L"Clásico", L"Classic"), S.tr(L"Moderno", L"Modern")}, mv == "libre" || mv == "moderno" ? 1 : 0);
  C.lang = addCombo(dlg, 0, S.tr(L"Idioma de los textos", L"Text language"), {L"Español", L"English", L"Русский"}, g("idioma", "es") == "en" ? 1 : g("idioma", "es") == "ru" ? 2 : 0);
  {
    std::string v = g("idioma_voces", "");
    if (v != "en" && v != "es" && v != "ru") v = g("idioma", "es") == "en" ? "en" : "es";
    C.voice = addCombo(dlg, 0, S.tr(L"Idioma de las voces", L"Voice language"), {L"Español", L"English", L"Русский"}, v == "ru" ? 2 : v == "en" ? 1 : 0);
  }

  C.hqMenu = addCheck(dlg, 0, S.tr(L"Menú principal de alta calidad", L"High quality main menu"), g("menu_alta_calidad", "1") != "0");

  {
    DEVMODEW dm{}; dm.dmSize = sizeof dm;
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i)
      if (dm.dmBitsPerPel >= 24 && dm.dmPelsWidth >= 640) { std::pair<int, int> m{int(dm.dmPelsWidth), int(dm.dmPelsHeight)}; if (std::find(C.modes.begin(), C.modes.end(), m) == C.modes.end()) C.modes.push_back(m); }
    const std::pair<int, int> cur{toInt(g("resolucion_ancho", "1920"), 1920), toInt(g("resolucion_alto", "1080"), 1080)};
    if (std::find(C.modes.begin(), C.modes.end(), cur) == C.modes.end()) C.modes.push_back(cur);
    std::sort(C.modes.begin(), C.modes.end(), [](auto a, auto b) { return a.first * 100000 + a.second > b.first * 100000 + b.second; });
    std::vector<std::wstring> it; int sel = 0;
    for (std::size_t i = 0; i < C.modes.size(); ++i) { it.push_back(std::to_wstring(C.modes[i].first) + L" x " + std::to_wstring(C.modes[i].second)); if (C.modes[i] == cur) sel = int(i); }
    C.res = addCombo(dlg, 1, S.tr(L"Resolución", L"Resolution"), it, sel);
  }
  C.full = addCheck(dlg, 1, S.tr(L"Pantalla completa", L"Fullscreen"), g("pantalla_completa", "0") == "1");
  {
    int sel = 3;
    if (g("smaa", "0") == "1") sel = 1;
    else { const int m = toInt(g("msaa", "4"), 4); for (int k = 0; k < 5; ++k) if (kAaOpts[k] == m) sel = k; }
    C.aa = addCombo(dlg, 1, L"Antialiasing", {S.tr(L"No", L"Off"), L"SMAA", L"MSAA 2x", L"MSAA 4x", L"MSAA 8x"}, sel);
  }
  {
    const int lvl = g("filtrado", "1") != "1" ? 0 : g("trilineal", "1") != "1" ? 1 : g("anisotropico", "1") != "1" ? 2 : 3;
    C.filt = addCombo(dlg, 1, S.tr(L"Filtrado de texturas", L"Texture filtering"), {S.tr(L"Pixelado", L"Pixelated"), S.tr(L"Bilineal", L"Bilinear"), S.tr(L"Trilineal", L"Trilinear"), S.tr(L"Anisotrópico", L"Anisotropic")}, lvl);
  }
  C.aspect = addCombo(dlg, 1, S.tr(L"Formato", L"Aspect ratio"), {S.tr(L"Panorámico", L"Widescreen"), S.tr(L"Original 4:3", L"Original 4:3")}, g("solo_4_3", "0") == "1" ? 1 : 0);
  C.vids = addCombo(dlg, 1, S.tr(L"Vídeos", L"Videos"), {S.tr(L"Ampliados", L"Enlarged"), S.tr(L"Como en PS1", L"As on PS1")}, g("videos_ampliados", "1") == "1" ? 0 : 1);
  {
    const int f = toInt(g("fps_max", "0"), 0); int sel = 8;
    for (int k = 0; k < 9; ++k) if (kFpsOpts[k] == f) sel = k;
    std::vector<std::wstring> it; for (int k = 0; k < 8; ++k) it.push_back(std::to_wstring(kFpsOpts[k]) + L" FPS");
    it.push_back(S.tr(L"Sin límite", L"Unlimited"));
    C.fps = addCombo(dlg, 1, S.tr(L"Límite de FPS", L"FPS limit"), it, sel);
  }
  {
    const int hs = toInt(g("hud_tamano", "100"), 100); int sel = 1;
    for (int k = 0; k < 4; ++k) if (kHudOpts[k] == hs) sel = k;
    C.hud = addCombo(dlg, 1, S.tr(L"Tamaño del HUD", L"HUD size"), {L"75 %", L"100 %", L"125 %", L"150 %"}, sel);
  }
  C.vsync = addCheck(dlg, 1, S.tr(L"Sincronía vertical", L"Vertical sync"), g("vsync", "1") != "0");
  C.shadows = addCheck(dlg, 1, S.tr(L"Sombras", L"Shadows"), g("sombras", "1") != "0");
  C.hudEdges = addCheck(dlg, 1, S.tr(L"HUD en los bordes de la pantalla", L"HUD at the screen edges"), g("hud_bordes", "1") != "0");

  const char* vk[3] = {"volumen_general", "volumen_musica", "volumen_voces"};
  const wchar_t* vl[3] = {S.tr(L"Volumen general", L"Master volume"), S.tr(L"Música y efectos", L"Music and effects"), S.tr(L"Voces y vídeos", L"Voices and videos")};
  for (int i = 0; i < 3; ++i) C.vol[i] = addSlider(dlg, 2, vl[i], 0, 10, std::clamp(toInt(g(vk[i], "10"), 10), 0, 10), &C.volVal[i]);
  C.mono = addCombo(dlg, 2, S.tr(L"Salida", L"Output"), {S.tr(L"Estéreo", L"Stereo"), L"Mono"}, g("sonido_mono", "0") == "1" ? 1 : 0);

  C.rumble = addCheck(dlg, 3, S.tr(L"Vibración del mando", L"Controller vibration"), g("vibracion", "1") != "0");
  C.sens = addSlider(dlg, 3, S.tr(L"Sensibilidad del ratón", L"Mouse sensitivity"), 1, 40, std::clamp(toInt(g("raton_sensibilidad", "10"), 10), 1, 40), &C.sensVal);
  C.inv = addCheck(dlg, 3, S.tr(L"Invertir el ratón (arriba/abajo)", L"Invert mouse (up/down)"), g("raton_invertir_y", "0") == "1");
  C.wArm = addCheck(dlg, 3, S.tr(L"Rueda del ratón: cambiar de arma", L"Mouse wheel: switch weapon"), g("rueda_armas", "1") == "1");
  C.wZoom = addCheck(dlg, 3, S.tr(L"Rueda al apuntar: zoom de la mira", L"Wheel while aiming: scope zoom"), g("rueda_zoom", "1") == "1");
  C.wMenu = addCheck(dlg, 3, S.tr(L"Rueda en el menú de armas", L"Wheel in the weapon menu"), g("rueda_menu", "1") == "1");
  C.wInv = addCheck(dlg, 3, S.tr(L"Invertir la rueda", L"Invert the wheel"), g("rueda_invertir", "0") == "1");
  {
    Cfg& C2 = cf();
    HWND b = CreateWindowExW(0, L"BUTTON", S.tr(L"Configurar teclas y mando...", L"Configure keys and controller..."), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                             S2(28), C2.y[3] + S2(6), S2(240), S2(30), dlg, HMENU(INT_PTR(kIdControls)), S.inst, nullptr);
    SendMessageW(b, WM_SETFONT, WPARAM(C2.font), TRUE);
    C2.ctls.push_back({3, nullptr, b});
    C2.y[3] += S2(44);
  }

  const int bw = S2(96), bh = S2(28), by = rc.bottom - S2(44);
  const wchar_t* bt[3] = {S.tr(L"Aceptar", L"OK"), S.tr(L"Cancelar", L"Cancel"), S.tr(L"Aplicar", L"Apply")};
  for (int i = 0; i < 3; ++i) {
    HWND b = CreateWindowExW(0, L"BUTTON", bt[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                             rc.right - S2(12) - (3 - i) * (bw + S2(8)) + S2(8), by, bw, bh, dlg, HMENU(INT_PTR(i + 1)), S.inst, nullptr);
    SendMessageW(b, WM_SETFONT, WPARAM(C.font), TRUE);
  }
  showTab(0);
}

inline void applyConfig() {
  State& S = st();
  Cfg& C = cf();
  auto s = [&](const char* k, const std::string& v) { S.set(k, v); };
  const bool mod = cbSel(C.mov) == 1;
  s("movimiento", mod ? "libre" : "clasico"); s("camara", mod ? "firme" : "original");
  const int tl = cbSel(C.lang);
  const bool en = tl == 1;
  s("idioma", tl == 2 ? "ru" : en ? "en" : "es");
  { const int v = cbSel(C.voice); s("idioma_voces", v == 2 ? "ru" : v == 1 ? "en" : "es"); }
  if (C.hqMenu) s("menu_alta_calidad", ckOn(C.hqMenu) ? "1" : "0");
  if (const int r = cbSel(C.res); r >= 0 && r < int(C.modes.size())) {
    const auto m = C.modes[std::size_t(r)];
    s("resolucion_ancho", std::to_string(m.first)); s("resolucion_alto", std::to_string(m.second));
    s("ventana_ancho", std::to_string(m.first)); s("ventana_alto", std::to_string(m.second));
  }
  s("pantalla_completa", ckOn(C.full) ? "1" : "0");
  const int aa = kAaOpts[std::clamp(cbSel(C.aa), 0, 4)];
  s("smaa", aa == -1 ? "1" : "0"); s("msaa", std::to_string(std::max(0, aa)));
  const int lvl = cbSel(C.filt);
  s("filtrado", lvl >= 1 ? "1" : "0"); s("trilineal", lvl >= 2 ? "1" : "0"); s("anisotropico", lvl >= 3 ? "1" : "0");
  s("solo_4_3", cbSel(C.aspect) == 1 ? "1" : "0");
  s("videos_ampliados", cbSel(C.vids) == 0 ? "1" : "0");
  s("fps_max", std::to_string(kFpsOpts[std::clamp(cbSel(C.fps), 0, 8)]));
  s("hud_tamano", std::to_string(kHudOpts[std::clamp(cbSel(C.hud), 0, 3)]));
  s("vsync", ckOn(C.vsync) ? "1" : "0");
  s("sombras", ckOn(C.shadows) ? "1" : "0");
  s("hud_bordes", ckOn(C.hudEdges) ? "1" : "0");
  const char* vk[3] = {"volumen_general", "volumen_musica", "volumen_voces"};
  for (int i = 0; i < 3; ++i) s(vk[i], std::to_string(tbPos(C.vol[i])));
  s("sonido_mono", cbSel(C.mono) == 1 ? "1" : "0");
  s("vibracion", ckOn(C.rumble) ? "1" : "0");
  s("raton_sensibilidad", std::to_string(tbPos(C.sens)));
  s("raton_invertir_y", ckOn(C.inv) ? "1" : "0");
  s("rueda_armas", ckOn(C.wArm) ? "1" : "0");
  s("rueda_zoom", ckOn(C.wZoom) ? "1" : "0");
  s("rueda_menu", ckOn(C.wMenu) ? "1" : "0");
  s("rueda_invertir", ckOn(C.wInv) ? "1" : "0");
  if (en != S.en || tl != S.lang) { S.en = en; S.lang = tl; InvalidateRect(S.main, nullptr, FALSE); }
}

inline LRESULT CALLBACK cfgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_CREATE: buildConfig(h); return 0;
    case WM_NOTIFY:
      if (reinterpret_cast<NMHDR*>(lp)->idFrom == kIdTabs && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE)
        showTab(int(SendMessageW(cf().tabs, TCM_GETCURSEL, 0, 0)));
      return 0;
    case WM_HSCROLL: {
      Cfg& C = cf();
      HWND src = HWND(lp);
      for (int i = 0; i < 3; ++i) if (src == C.vol[i]) SetWindowTextW(C.volVal[i], std::to_wstring(tbPos(src)).c_str());
      if (src == C.sens) SetWindowTextW(C.sensVal, std::to_wstring(tbPos(src)).c_str());
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      SetBkMode(HDC(wp), TRANSPARENT);
      return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case kIdOk: applyConfig(); DestroyWindow(h); return 0;
        case kIdCancel: DestroyWindow(h); return 0;
        case kIdApply: applyConfig(); return 0;
        case kIdControls: openControls(); return 0;
      }
      break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
      if (st().ctlWin) DestroyWindow(st().ctlWin);
      Cfg& C = cf();
      EnableWindow(st().main, TRUE);
      SetForegroundWindow(st().main);
      st().cfg = nullptr;
      if (C.font) { DeleteObject(C.font); C.font = nullptr; }
      C.modes.clear();
      return 0;
    }
  }
  return DefWindowProcW(h, m, wp, lp);
}

inline void openConfig() {
  State& S = st();
  if (S.cfg) { SetForegroundWindow(S.cfg); return; }
  RECT mr; GetWindowRect(S.main, &mr);
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  RECT r{0, 0, S2(470), S2(560)};
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  const int ww = r.right - r.left, wh = r.bottom - r.top;
  const int x = (mr.left + mr.right - ww) / 2, y = std::max(0L, (mr.top + mr.bottom - wh) / 2);
  EnableWindow(S.main, FALSE);
  S.cfg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SF3Configuracion", S.tr(L"Configuración - Syphon Filter 3", L"Settings - Syphon Filter 3"),
                          style, x, y, ww, wh, S.main, nullptr, S.inst, nullptr);
  ShowWindow(S.cfg, SW_SHOW);
}

struct Ctrl {
  ctl::Bindings b1, b2;
  int player = 0;
  int p2src = 0;
  HWND playerCombo = nullptr, p2Label = nullptr, p2Combo = nullptr;
  SDL_GameController* pad2 = nullptr;
  ctl::Bindings& b() { return player ? b2 : b1; }
  const ctl::Bindings& b() const { return player ? b2 : b1; }
  int device = 0;
  bool modern = false;
  HWND devCombo = nullptr, movCombo = nullptr, status = nullptr;
  HWND act[ctl::kCount]{};
  int capture = -1;
  unsigned captureFrames = 0;
  bool prevDown[256]{};
  int prevPad = -1;
  HBITMAP img = nullptr;
  int iw = 0, ih = 0;
  RECT imgRect{};
  std::map<std::string, POINT> anchor;
  SDL_GameController* pad = nullptr;
  HFONT font = nullptr;
};
inline Ctrl& cc() { static Ctrl c; return c; }
enum { kIdPlayer = 205, kIdP2Src = 206, kIdDev = 200, kIdMov = 201, kIdDefaults = 202, kIdCtlOk = 203, kIdCtlCancel = 204, kIdAct0 = 300 };

inline const wchar_t* actionName(int a, bool modern, bool en) {

  static const wchar_t* es_c[ctl::kCount] = {L"Avanzar", L"Retroceder", L"Girar a la izquierda", L"Girar a la derecha", L"Saltar / trepar", L"Agacharse", L"Disparar", L"Acción / usar",
      L"Apuntar a mano", L"Fijar blanco", L"Paso lateral izq.", L"Paso lateral der.", L"Pausa / mapa", L"Cambiar de arma (mantener: menú)", L"Paso lateral izq.", L"Paso lateral der."};
  static const wchar_t* es_m[ctl::kCount] = {L"Mover hacia delante", L"Mover hacia atrás", L"Mover a la izquierda", L"Mover a la derecha", L"Saltar / trepar", L"Agacharse", L"Disparar", L"Acción / usar",
      L"Apuntar a mano", L"Fijar blanco", L"Paso lateral izq.", L"Paso lateral der.", L"Pausa / mapa", L"Cambiar de arma (mantener: menú)", L"(sin uso en moderno)", L"(sin uso en moderno)"};
  static const wchar_t* en_c[ctl::kCount] = {L"Walk forward", L"Walk back", L"Turn left", L"Turn right", L"Jump / climb", L"Crouch", L"Fire", L"Action / use",
      L"Manual aim", L"Target lock", L"Sidestep left", L"Sidestep right", L"Pause / map", L"Switch weapon (hold: menu)", L"Sidestep left", L"Sidestep right"};
  static const wchar_t* en_m[ctl::kCount] = {L"Move forward", L"Move back", L"Move left", L"Move right", L"Jump / climb", L"Crouch", L"Fire", L"Action / use",
      L"Manual aim", L"Target lock", L"Sidestep left", L"Sidestep right", L"Pause / map", L"Switch weapon (hold: menu)", L"(unused in modern)", L"(unused in modern)"};
  static const wchar_t* ru_c[ctl::kCount] = {L"Идти вперёд", L"Идти назад", L"Поворот влево", L"Поворот вправо", L"Прыжок / лазать", L"Присесть", L"Огонь", L"Действие",
      L"Ручное прицеливание", L"Захват цели", L"Шаг влево", L"Шаг вправо", L"Пауза / карта", L"Смена оружия (держать: меню)", L"Шаг влево", L"Шаг вправо"};
  static const wchar_t* ru_m[ctl::kCount] = {L"Вперёд", L"Назад", L"Влево", L"Вправо", L"Прыжок / лазать", L"Присесть", L"Огонь", L"Действие",
      L"Ручное прицеливание", L"Захват цели", L"Шаг влево", L"Шаг вправо", L"Пауза / карта", L"Смена оружия (держать: меню)", L"(не используется)", L"(не используется)"};
  if (st().lang == 2) return (modern ? ru_m : ru_c)[a];
  return (en ? (modern ? en_m : en_c) : (modern ? es_m : es_c))[a];
}
inline const wchar_t* buttonName(int a, bool en) {
  static const wchar_t* es[ctl::kCount] = {L"ARRIBA", L"ABAJO", L"IZQUIERDA", L"DERECHA", L"X", L"CÍRCULO", L"CUADRADO", L"TRIÁNGULO", L"L1", L"R1", L"L2", L"R2", L"START", L"SELECT", L"LATERAL IZQ.", L"LATERAL DER."};
  static const wchar_t* en_[ctl::kCount] = {L"UP", L"DOWN", L"LEFT", L"RIGHT", L"X", L"CIRCLE", L"SQUARE", L"TRIANGLE", L"L1", L"R1", L"L2", L"R2", L"START", L"SELECT", L"STRAFE L", L"STRAFE R"};
  static const wchar_t* ru[ctl::kCount] = {L"ВВЕРХ", L"ВНИЗ", L"ВЛЕВО", L"ВПРАВО", L"X", L"КРУГ", L"КВАДРАТ", L"ТРЕУГОЛЬНИК", L"L1", L"R1", L"L2", L"R2", L"START", L"SELECT", L"ШАГ ВЛЕВО", L"ШАГ ВПРАВО"};
  if (st().lang == 2) return ru[a];
  return (en ? en_ : es)[a];
}

inline const SDL_Scancode* scanTable() {
  static const SDL_Scancode base[0x59] = {
      SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
      SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
      SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I,
      SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_RETURN, SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_S,
      SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON,
      SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
      SDL_SCANCODE_B, SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_KP_MULTIPLY,
      SDL_SCANCODE_LALT, SDL_SCANCODE_SPACE, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5,
      SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_KP_7,
      SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_1,
      SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_F11,
      SDL_SCANCODE_F12};
  return base;
}
inline int vkToSdlScan(int sc) { return sc >= 0 && sc < 0x59 ? int(scanTable()[sc]) : 0; }
inline int vkToSdl(int vk) {
  switch (vk) {
    case VK_LEFT: return SDL_SCANCODE_LEFT; case VK_RIGHT: return SDL_SCANCODE_RIGHT; case VK_UP: return SDL_SCANCODE_UP; case VK_DOWN: return SDL_SCANCODE_DOWN;
    case VK_HOME: return SDL_SCANCODE_HOME; case VK_END: return SDL_SCANCODE_END; case VK_PRIOR: return SDL_SCANCODE_PAGEUP; case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case VK_INSERT: return SDL_SCANCODE_INSERT; case VK_DELETE: return SDL_SCANCODE_DELETE; case VK_RCONTROL: return SDL_SCANCODE_RCTRL; case VK_RMENU: return SDL_SCANCODE_RALT;
    case VK_LCONTROL: return SDL_SCANCODE_LCTRL; case VK_LMENU: return SDL_SCANCODE_LALT; case VK_LSHIFT: return SDL_SCANCODE_LSHIFT; case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
    case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE; case VK_LWIN: return SDL_SCANCODE_LGUI; case VK_RWIN: return SDL_SCANCODE_RGUI; case VK_APPS: return SDL_SCANCODE_APPLICATION;
    case VK_SHIFT: case VK_CONTROL: case VK_MENU: return 0;
  }
  const UINT sc = MapVirtualKeyW(UINT(vk), 0  );
  return vkToSdlScan(int(sc));
}

inline int scanToSdl(LPARAM lp, int vk) {
  const int sc = int((lp >> 16) & 0xff);
  if ((lp >> 24) & 1) {
    switch (sc) {
      case 0x1C: return SDL_SCANCODE_KP_ENTER; case 0x1D: return SDL_SCANCODE_RCTRL; case 0x35: return SDL_SCANCODE_KP_DIVIDE; case 0x38: return SDL_SCANCODE_RALT;
      case 0x47: return SDL_SCANCODE_HOME; case 0x48: return SDL_SCANCODE_UP; case 0x49: return SDL_SCANCODE_PAGEUP; case 0x4B: return SDL_SCANCODE_LEFT;
      case 0x4D: return SDL_SCANCODE_RIGHT; case 0x4F: return SDL_SCANCODE_END; case 0x50: return SDL_SCANCODE_DOWN; case 0x51: return SDL_SCANCODE_PAGEDOWN;
      case 0x52: return SDL_SCANCODE_INSERT; case 0x53: return SDL_SCANCODE_DELETE; case 0x5B: return SDL_SCANCODE_LGUI; case 0x5C: return SDL_SCANCODE_RGUI;
      case 0x5D: return SDL_SCANCODE_APPLICATION;
    }
    return vkToSdl(vk);
  }
  if (sc == 0x2A) return SDL_SCANCODE_LSHIFT;
  if (sc == 0x36) return SDL_SCANCODE_RSHIFT;
  if (sc > 0 && sc < 0x59) { const int c = vkToSdlScan(sc); if (c) return c; }
  return vkToSdl(vk);
}
inline void endCapture(HWND h);

inline bool captureKeyMsg(HWND h, const MSG& msg) {
  Ctrl& C = cc();
  if (C.capture < 0) return false;
  if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return true;
  const int a = C.capture;
  const int vk = int(msg.wParam);
  if (vk == VK_ESCAPE) { endCapture(h); return true; }
  if (vk == VK_DELETE) { if (C.device == 0) C.b().keys[std::size_t(a)].fill(0); else C.b().pad[std::size_t(a)].fill(-1); endCapture(h); return true; }
  if (C.device != 0) return true;
  const int code = scanToSdl(msg.lParam, vk);
  if (code <= 0 || ctl::reservedKey(code)) return true;
  ctl::assign(C.b().keys, a, code, 0);
  endCapture(h);
  return true;
}
inline void loadControlsImage() {
  Ctrl& C = cc();
  if (C.img) { DeleteObject(C.img); C.img = nullptr; }
  C.img = HBITMAP(LoadImageW(nullptr, W(st().dataDir + "controles_mando.bmp").c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
  if (!C.img) C.img = memBitmap(res::get("controles_mando.bmp"));
  BITMAP b{};
  if (C.img) { GetObject(C.img, sizeof b, &b); C.iw = b.bmWidth; C.ih = std::abs(b.bmHeight); } else { C.iw = 480; C.ih = 300; }
  C.anchor.clear();
  std::ifstream ff(st().dataDir + "controles_mando.txt");
  const auto& txt = res::get("controles_mando.txt");
  std::istringstream fr(std::string(txt.begin(), txt.end()));
  std::istream& f = ff ? static_cast<std::istream&>(ff) : static_cast<std::istream&>(fr);
  std::string l;
  while (std::getline(f, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    const auto eq = l.find('='), cm = l.find(',');
    if (eq == std::string::npos || cm == std::string::npos || cm < eq) continue;
    try { C.anchor[l.substr(0, eq)] = POINT{std::stoi(l.substr(eq + 1, cm - eq - 1)), std::stoi(l.substr(cm + 1))}; } catch (...) {}
  }
}

inline void loadBindingsIni() {
  State& S = st();
  Ctrl& C = cc();
  C.b1 = ctl::defaults();
  C.b2 = ctl::defaultsP2();
  C.p2src = S.get("jugador2", "mando") == "teclado" ? 1 : 0;
  auto split = [](const std::string& v) { std::vector<std::string> o; std::size_t p = 0; while (p <= v.size()) { auto q = v.find('|', p); if (q == std::string::npos) q = v.size(); std::string t = v.substr(p, q - p);
      while (!t.empty() && t.front() == ' ') t.erase(t.begin()); while (!t.empty() && t.back() == ' ') t.pop_back(); if (!t.empty()) o.push_back(t); p = q + 1; } return o; };
  const std::string none = "\x01";
  for (int pl = 0; pl < 2; ++pl) {
    ctl::Bindings& B = pl ? C.b2 : C.b1;
    const std::string pre = pl ? "j2_" : "";
    for (int a = 0; a < ctl::kCount; ++a) {
      const std::string id = ctl::kActions[a].id;
      if (const std::string v = S.get((pre + "teclas_" + id).c_str(), none.c_str()); v != none) {
        auto& row = B.keys[std::size_t(a)]; row.fill(0); std::size_t j = 0;
        for (const auto& n : split(v)) if (int c = ctl::keyFromIniName(n); c && j < row.size() && !(c < ctl::kMouseBase && ctl::reservedKey(c)) && !(pl && c >= ctl::kMouseBase)) row[j++] = c;
      }
      if (const std::string v = S.get((pre + "mando_" + id).c_str(), none.c_str()); v != none) {
        auto& row = B.pad[std::size_t(a)]; row.fill(-1); std::size_t j = 0;
        for (const auto& n : split(v)) if (int c = ctl::padFromIniName(n); c >= 0 && j < row.size()) row[j++] = c;
      }
    }
  }
}
inline void saveBindingsIni() {
  State& S = st();
  Ctrl& C = cc();
  for (int pl = 0; pl < 2; ++pl) {
    const ctl::Bindings& B = pl ? C.b2 : C.b1;
    const std::string pre = pl ? "j2_" : "";
    for (int a = 0; a < ctl::kCount; ++a) {
      std::string k, pd;
      for (int c : B.keys[std::size_t(a)]) if (c) { if (!k.empty()) k += "|"; k += ctl::keyIniName(c); }
      for (int c : B.pad[std::size_t(a)]) if (c >= 0) { if (!pd.empty()) pd += "|"; pd += ctl::padIniName(c); }
      S.set(pre + "teclas_" + ctl::kActions[a].id, k);
      S.set(pre + "mando_" + ctl::kActions[a].id, pd);
    }
  }
  S.set("jugador2", C.p2src ? "teclado" : "mando");
}

inline std::wstring bindText(int a) {
  const Ctrl& C = cc();
  const bool en = st().en;
  std::string list;
  if (C.device == 0) { for (int c : C.b().keys[std::size_t(a)]) if (c) { if (!list.empty()) list += ", "; list += ctl::keyLabel(c); } }
  else { for (int c : C.b().pad[std::size_t(a)]) if (c >= 0) { if (!list.empty()) list += ", "; list += ctl::padLabel(c, ctl::padFamily(C.pad)); } }
  std::wstring t = std::wstring(buttonName(a, en)) + L"  —  " + actionName(a, C.modern, en) + L"\n";
  if (st().lang == 2) list = pctext::translateLang(list, 2);
  t += list.empty() ? std::wstring(st().tr(L"(sin asignar)", L"(not assigned)")) : W(list);
  if (C.capture == a) t = std::wstring(buttonName(a, en)) + L"\n" + (C.device == 0 ? (C.player ? (st().tr(L"Pulsa una tecla...", L"Press a key...")) : (st().tr(L"Pulsa una tecla o botón del ratón...", L"Press a key or mouse button...")))
                                                                                  : (st().tr(L"Pulsa un botón del mando...", L"Press a controller button...")));
  return t;
}

inline bool actionVisible(int a) { return ctl::kActions[a].bit != 0; }

inline void layoutControls(HWND h) {
  Ctrl& C = cc();
  RECT rc; GetClientRect(h, &rc);
  const int top = S2(56), colW = S2(250), rowH = S2(46), gap = S2(6);
  const int availW = rc.right - 2 * colW - S2(60), availH = rc.bottom - top - S2(70);
  const double k = std::min(double(availW) / C.iw, double(availH) / C.ih);
  const int iw = int(C.iw * k), ih = int(C.ih * k);
  C.imgRect = RECT{(rc.right - iw) / 2, top + (availH - ih) / 2, (rc.right - iw) / 2 + iw, top + (availH - ih) / 2 + ih};
  std::vector<std::pair<int, int>> L, R;
  for (int a = 0; a < ctl::kCount; ++a) {
    if (!actionVisible(a)) { ShowWindow(C.act[a], SW_HIDE); continue; }
    ShowWindow(C.act[a], SW_SHOW);
    auto it = C.anchor.find(ctl::kActions[a].id);
    const bool left = it != C.anchor.end() ? it->second.x < C.iw / 2 : (a == ctl::kStrafeL);
    const int y = it != C.anchor.end() ? it->second.y : 100000 + a;
    (left ? L : R).push_back({y, a});
  }
  std::sort(L.begin(), L.end()); std::sort(R.begin(), R.end());
  auto place = [&](const std::vector<std::pair<int, int>>& v, int x) {
    const int total = int(v.size()) * (rowH + gap) - gap;
    int y = top + std::max(0, (availH - total) / 2);
    for (const auto& [yy, a] : v) { MoveWindow(C.act[a], x, y, colW, rowH, TRUE); y += rowH + gap; }
  };
  place(L, S2(20));
  place(R, rc.right - S2(20) - colW);
  InvalidateRect(h, nullptr, TRUE);
}
inline void refreshControls(HWND h) {
  Ctrl& C = cc();
  C.modern = cbSel(cf().mov) == 1;
  if (C.p2Label) { ShowWindow(C.p2Label, C.player ? SW_SHOW : SW_HIDE); ShowWindow(C.p2Combo, C.player ? SW_SHOW : SW_HIDE); }
  for (int a = 0; a < ctl::kCount; ++a) SetWindowTextW(C.act[a], bindText(a).c_str());
  const bool en = st().en;
  SetWindowTextW(C.status, C.capture >= 0 ? (st().tr(L"Esc: cancelar   -   Supr: dejar sin asignar", L"Esc: cancel   -   Delete: clear this action"))
                                          : (st().tr(L"Haz clic en una acción para cambiar su tecla o botón.", L"Click an action to change its key or button.")));
  layoutControls(h);
}
inline void paintControls(HWND h) {
  Ctrl& C = cc();
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  RECT rc; GetClientRect(h, &rc);
  FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
  const RECT& r = C.imgRect;
  if (C.img) {
    HDC m = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(m, C.img);
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, m, 0, 0, C.iw, C.ih, SRCCOPY);
    SelectObject(m, o); DeleteDC(m);
  } else {
    RECT t = r; DrawTextW(dc, st().tr(L"(no está controles_mando.bmp)", L"(controles_mando.bmp not found)"), -1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  const double k = double(r.right - r.left) / std::max(1, C.iw);
  HPEN pen = CreatePen(PS_SOLID, std::max(1, S2(1.5)), RGB(60, 150, 40)), penSel = CreatePen(PS_SOLID, std::max(2, S2(2.5)), RGB(230, 120, 20));
  for (int a = 0; a < ctl::kCount; ++a) {
    if (!actionVisible(a)) continue;
    auto it = C.anchor.find(ctl::kActions[a].id);
    if (it == C.anchor.end()) continue;
    RECT br; GetWindowRect(C.act[a], &br); MapWindowPoints(nullptr, h, reinterpret_cast<POINT*>(&br), 2);
    const int ax = r.left + int(it->second.x * k), ay = r.top + int(it->second.y * k);
    const bool left = br.right < ax;
    HGDIOBJ op = SelectObject(dc, C.capture == a ? penSel : pen);
    MoveToEx(dc, left ? br.right : br.left, (br.top + br.bottom) / 2, nullptr);
    LineTo(dc, ax, ay);
    SelectObject(dc, op);
    HBRUSH dot = CreateSolidBrush(C.capture == a ? RGB(230, 120, 20) : RGB(60, 150, 40));
    HGDIOBJ ob = SelectObject(dc, dot); HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, ax - S2(4), ay - S2(4), ax + S2(4), ay + S2(4));
    SelectObject(dc, ob); SelectObject(dc, op2); DeleteObject(dot);
  }
  DeleteObject(pen); DeleteObject(penSel);
  EndPaint(h, &ps);
}
inline void endCapture(HWND h) { cc().capture = -1; KillTimer(h, 1); refreshControls(h); }
inline void startCapture(HWND h, int a) {
  Ctrl& C = cc();
  C.capture = a; C.captureFrames = 0;
  for (int vk = 1; vk < 256; ++vk) C.prevDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
  { SDL_GameController* use = (C.player && C.pad2) ? C.pad2 : C.pad; C.prevPad = use ? ctl::padPressed(use) : -1; }
  SetTimer(h, 1, 15, nullptr);
  SetFocus(h);
  refreshControls(h);
}
inline void captureTick(HWND h) {
  Ctrl& C = cc();
  if (C.capture < 0) return;
  const int a = C.capture;
  ++C.captureFrames;
  if (C.device == 1) {
    SDL_GameControllerUpdate();
    SDL_GameController* use = (C.player && C.pad2) ? C.pad2 : C.pad;
    const int p = use ? ctl::padPressed(use) : -1;
    if (p >= 0 && p != C.prevPad) { ctl::assign(C.b().pad, a, p, -1); endCapture(h); return; }
    C.prevPad = p;
    return;
  }
  for (int vk : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = down && !C.prevDown[vk];
    C.prevDown[vk] = down;
    if (!edge) continue;
    int code = 0;
    switch (vk) {
      case VK_LBUTTON: code = ctl::kMouseBase + SDL_BUTTON_LEFT; break;
      case VK_RBUTTON: code = ctl::kMouseBase + SDL_BUTTON_RIGHT; break;
      case VK_MBUTTON: code = ctl::kMouseBase + SDL_BUTTON_MIDDLE; break;
      case VK_XBUTTON1: code = ctl::kMouseBase + SDL_BUTTON_X1; break;
      case VK_XBUTTON2: code = ctl::kMouseBase + SDL_BUTTON_X2; break;
      default: code = vkToSdl(vk);
    }
    if (code >= ctl::kMouseBase && (C.captureFrames < 10 || C.player)) continue;
    if (code <= 0 || (code < ctl::kMouseBase && ctl::reservedKey(code))) continue;
    ctl::assign(C.b().keys, a, code, 0);
    endCapture(h);
    return;
  }
}
inline void syncMovementToGeneral() {
  Cfg& G = cf();
  if (G.mov) SendMessageW(G.mov, CB_SETCURSEL, cc().modern ? 1 : 0, 0);
}
inline LRESULT CALLBACK ctlProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  State& S = st();
  Ctrl& C = cc();
  switch (m) {
    case WM_CREATE: {
      NONCLIENTMETRICSW ncm{}; ncm.cbSize = sizeof ncm;
      SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
      C.font = CreateFontIndirectW(&ncm.lfMessageFont);
      auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int hh, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, hh, h, HMENU(INT_PTR(id)), S.inst, nullptr);
        SendMessageW(c, WM_SETFONT, WPARAM(C.font), TRUE); return c; };
      mk(L"STATIC", S.tr(L"Jugador:", L"Player:"), SS_LEFT, S2(20), S2(18), S2(60), S2(20), -1);
      C.playerCombo = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, S2(80), S2(14), S2(110), S2(200), kIdPlayer);
      SendMessageW(C.playerCombo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Jugador 1", L"Player 1")));
      SendMessageW(C.playerCombo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Jugador 2", L"Player 2")));
      SendMessageW(C.playerCombo, CB_SETCURSEL, C.player, 0);
      mk(L"STATIC", S.tr(L"Dispositivo:", L"Device:"), SS_LEFT, S2(210), S2(18), S2(80), S2(20), -1);
      C.devCombo = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, S2(290), S2(14), S2(170), S2(200), kIdDev);
      SendMessageW(C.devCombo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Teclado y ratón", L"Keyboard and mouse")));
      SendMessageW(C.devCombo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Mando", L"Controller")));
      SendMessageW(C.devCombo, CB_SETCURSEL, C.device, 0);
      C.p2Label = mk(L"STATIC", S.tr(L"El jugador 2 juega con:", L"Player 2 plays with:"), SS_LEFT, S2(480), S2(18), S2(150), S2(20), -1);
      C.p2Combo = mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, S2(630), S2(14), S2(145), S2(200), kIdP2Src);
      SendMessageW(C.p2Combo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Mando", L"Controller")));
      SendMessageW(C.p2Combo, CB_ADDSTRING, 0, LPARAM(S.tr(L"Teclado", L"Keyboard")));
      SendMessageW(C.p2Combo, CB_SETCURSEL, C.p2src, 0);
      for (int a = 0; a < ctl::kCount; ++a) C.act[a] = mk(L"BUTTON", L"", BS_PUSHBUTTON | BS_MULTILINE | BS_LEFT | WS_TABSTOP, 0, 0, 10, 10, kIdAct0 + a);
      RECT rc; GetClientRect(h, &rc);
      C.status = mk(L"STATIC", L"", SS_LEFT, S2(20), rc.bottom - S2(40), S2(420), S2(20), -1);
      mk(L"BUTTON", S.tr(L"Valores de fábrica", L"Restore defaults"), BS_PUSHBUTTON | WS_TABSTOP, rc.right - S2(20) - 3 * S2(110) - 2 * S2(8), rc.bottom - S2(46), S2(130), S2(28), kIdDefaults);
      mk(L"BUTTON", S.tr(L"Aceptar", L"OK"), BS_DEFPUSHBUTTON | WS_TABSTOP, rc.right - S2(20) - 2 * S2(96) - S2(8), rc.bottom - S2(46), S2(96), S2(28), kIdCtlOk);
      mk(L"BUTTON", S.tr(L"Cancelar", L"Cancel"), BS_PUSHBUTTON | WS_TABSTOP, rc.right - S2(20) - S2(96), rc.bottom - S2(46), S2(96), S2(28), kIdCtlCancel);
      refreshControls(h);
      return 0;
    }
    case WM_PAINT: paintControls(h); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode(HDC(wp), TRANSPARENT); return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    case WM_TIMER: captureTick(h); return 0;
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      if (id == kIdDev && HIWORD(wp) == CBN_SELCHANGE) { C.device = cbSel(C.devCombo); if (C.capture >= 0) endCapture(h); else refreshControls(h); return 0; }
      if (id == kIdPlayer && HIWORD(wp) == CBN_SELCHANGE) { if (C.capture >= 0) endCapture(h); C.player = cbSel(C.playerCombo); refreshControls(h); return 0; }
      if (id == kIdP2Src && HIWORD(wp) == CBN_SELCHANGE) { C.p2src = cbSel(C.p2Combo); return 0; }
      if (id >= kIdAct0 && id < kIdAct0 + ctl::kCount) { if (C.capture < 0) startCapture(h, id - kIdAct0); return 0; }
      if (id == kIdDefaults) {
        const ctl::Bindings d = C.player ? ctl::defaultsP2() : ctl::defaults();
        if (C.device == 0) C.b().keys = d.keys; else C.b().pad = d.pad;
        refreshControls(h); return 0;
      }
      if (id == kIdCtlOk) {
        saveBindingsIni();
        DestroyWindow(h); return 0;
      }
      if (id == kIdCtlCancel) { DestroyWindow(h); return 0; }
      break;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
      KillTimer(h, 1);
      C.capture = -1;
      if (C.font) { DeleteObject(C.font); C.font = nullptr; }
      if (C.img) { DeleteObject(C.img); C.img = nullptr; }
      if (S.cfg) { EnableWindow(S.cfg, TRUE); SetForegroundWindow(S.cfg); }
      S.ctlWin = nullptr;
      return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}
inline void openControls() {
  State& S = st();
  Ctrl& C = cc();
  if (S.ctlWin) { SetForegroundWindow(S.ctlWin); return; }
  loadBindingsIni();
  loadControlsImage();
  C.modern = cbSel(cf().mov) == 1;
  C.capture = -1;
  if (!C.pad) {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
      if (SDL_IsGameController(i)) { if (!C.pad) C.pad = SDL_GameControllerOpen(i); else if (!C.pad2) { C.pad2 = SDL_GameControllerOpen(i); break; } }
  }
  RECT pr; GetWindowRect(S.cfg, &pr);
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  RECT r{0, 0, S2(1060), S2(600)};
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  const int ww = r.right - r.left, wh = r.bottom - r.top;
  RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  const int x = std::clamp(int((pr.left + pr.right - ww) / 2), int(wa.left), int(std::max(wa.left, wa.right - ww)));
  const int y = std::clamp(int((pr.top + pr.bottom - wh) / 2), int(wa.top), int(std::max(wa.top, wa.bottom - wh)));
  EnableWindow(S.cfg, FALSE);
  S.ctlWin = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SF3Controles", S.tr(L"Teclas y mando - Syphon Filter 3", L"Keys and controller - Syphon Filter 3"),
                             style, x, y, ww, wh, S.cfg, nullptr, S.inst, nullptr);
  ShowWindow(S.ctlWin, SW_SHOW);
}

inline int hitButton(int x, int y) {
  const State& S = st();
  for (int i = 0; i < 3; ++i) if (x >= S.btn[i].left && x < S.btn[i].right && y >= S.btn[i].top && y < S.btn[i].bottom) return i;
  return -1;
}
inline void activate(int i) {
  State& S = st();
  if (i == 0) { S.result = 1; DestroyWindow(S.main); }
  else if (i == 1) openConfig();
  else { S.result = 0; DestroyWindow(S.main); }
}
inline LRESULT CALLBACK mainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  State& S = st();
  switch (m) {
    case WM_PAINT: paintMain(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
      const int b = hitButton(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (b != S.hover) { S.hover = b; if (b >= 0 && b != S.sel) { S.sel = b; InvalidateRect(h, nullptr, FALSE); } }
      SetCursor(LoadCursor(nullptr, b >= 0 ? IDC_HAND : IDC_ARROW));
      TRACKMOUSEEVENT te{sizeof te, TME_LEAVE, h, 0};
      TrackMouseEvent(&te);
      return 0;
    }
    case WM_MOUSELEAVE: S.hover = -1; return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) return TRUE; break;
    case WM_LBUTTONDOWN: {
      const int b = hitButton(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (b < 0) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
      return 0;
    }
    case WM_LBUTTONUP: {
      const int b = hitButton(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      if (b >= 0) activate(b);
      return 0;
    }
    case WM_KEYDOWN:
      if (wp == VK_LEFT || (wp == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000))) { S.sel = (S.sel + 2) % 3; InvalidateRect(h, nullptr, FALSE); }
      else if (wp == VK_RIGHT || wp == VK_TAB) { S.sel = (S.sel + 1) % 3; InvalidateRect(h, nullptr, FALSE); }
      else if (wp == VK_RETURN || wp == VK_SPACE) activate(S.sel);
      else if (wp == VK_ESCAPE) activate(2);
      return 0;
    case WM_CLOSE: S.result = 0; DestroyWindow(h); return 0;
    case WM_DESTROY: S.done = true; return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

inline bool run(const std::string& bmpPath, IniGet get, IniSet set) {
  State& S = st();
  S.dataDir = bmpPath.substr(0, bmpPath.find_last_of("/\\") + 1);
  S.get = std::move(get); S.set = std::move(set);
  S.en = S.get("idioma", "es") == "en";
  S.lang = S.en ? 1 : S.get("idioma", "es") == "ru" ? 2 : 0;
  S.inst = GetModuleHandleW(nullptr);
  S.result = 0; S.done = false; S.sel = 0;
  SetProcessDPIAware();
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  {
    HDC d = GetDC(nullptr);
    S.sc = std::max(1.0, GetDeviceCaps(d, LOGPIXELSY) / 96.0);
    ReleaseDC(nullptr, d);
  }
  S.w = int(720 * S.sc); S.h = int(480 * S.sc);
  buildBackground(bmpPath);
  S.fontBtn = CreateFontW(-int(30 * S.sc), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                          L"Bahnschrift SemiBold Condensed");
  HICON icon = LoadIconW(S.inst, MAKEINTRESOURCEW(101));
  WNDCLASSEXW wc{sizeof wc};
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = mainProc;
  wc.hInstance = S.inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = icon; wc.hIconSm = icon;
  wc.lpszClassName = L"SF3Lanzador";
  RegisterClassExW(&wc);
  WNDCLASSEXW cc_{sizeof cc_};
  cc_.lpfnWndProc = cfgProc;
  cc_.hInstance = S.inst;
  cc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cc_.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  cc_.hIcon = icon; cc_.hIconSm = icon;
  cc_.lpszClassName = L"SF3Configuracion";
  RegisterClassExW(&cc_);
  cc_.lpfnWndProc = ctlProc;
  cc_.lpszClassName = L"SF3Controles";
  RegisterClassExW(&cc_);
  RECT wa{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  const int x = (wa.left + wa.right - S.w) / 2, y = (wa.top + wa.bottom - S.h) / 2;
  S.main = CreateWindowExW(WS_EX_APPWINDOW, L"SF3Lanzador", L"Syphon Filter 3", WS_POPUP | WS_MINIMIZEBOX, x, y, S.w, S.h, nullptr, nullptr, S.inst, nullptr);
  ShowWindow(S.main, SW_SHOW);
  SetForegroundWindow(S.main);
  MSG msg;
  while (!S.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (S.ctlWin && cc().capture >= 0 && msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST && captureKeyMsg(S.ctlWin, msg)) continue;
    if (S.ctlWin && IsDialogMessageW(S.ctlWin, &msg)) continue;
    if (S.cfg && IsDialogMessageW(S.cfg, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (S.cfg) DestroyWindow(S.cfg);
  if (cc().pad) { SDL_GameControllerClose(cc().pad); cc().pad = nullptr; }
  if (cc().pad2) { SDL_GameControllerClose(cc().pad2); cc().pad2 = nullptr; }
  if (S.bg) { DeleteObject(S.bg); S.bg = nullptr; }
  if (S.fontBtn) { DeleteObject(S.fontBtn); S.fontBtn = nullptr; }
  UnregisterClassW(L"SF3Lanzador", S.inst);
  UnregisterClassW(L"SF3Configuracion", S.inst);
  UnregisterClassW(L"SF3Controles", S.inst);
  return S.result == 1;
}

}
#endif
