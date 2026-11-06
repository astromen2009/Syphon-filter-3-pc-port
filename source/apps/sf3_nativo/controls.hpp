#pragma once

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace ctl {

enum Action : int {
  kUp, kDown, kLeft, kRight, kCross, kCircle, kSquare, kTriangle,
  kL1, kR1, kL2, kR2, kStart, kSelect, kStrafeL, kStrafeR, kCount
};

struct ActionInfo {
  const char* id;
  const char* label;
  std::uint16_t bit;
};

inline const ActionInfo kActions[kCount] = {
    {"arriba", "ARRIBA / AVANZAR", 0x0010},
    {"abajo", "ABAJO / RETROCEDER", 0x0040},
    {"izquierda", "IZQUIERDA / GIRAR", 0x0080},
    {"derecha", "DERECHA / GIRAR", 0x0020},
    {"cruz", "X (CRUZ)", 0x4000},
    {"circulo", "CÍRCULO", 0x2000},
    {"cuadrado", "CUADRADO", 0x8000},
    {"triangulo", "TRIÁNGULO", 0x1000},
    {"l1", "L1", 0x0400},
    {"r1", "R1", 0x0800},
    {"l2", "L2", 0x0100},
    {"r2", "R2", 0x0200},
    {"start", "START (PAUSA)", 0x0008},
    {"select", "SELECT", 0x0001},
    {"lateral_izq", "PASO LATERAL IZQ.", 0},
    {"lateral_der", "PASO LATERAL DER.", 0},
};

constexpr int kMouseBase = 1000;
constexpr int kKeySlots = 3, kPadSlots = 2;

constexpr int kPadTriggerL = 100, kPadTriggerR = 101;

struct Bindings {
  std::array<std::array<int, kKeySlots>, kCount> keys{};
  std::array<std::array<int, kPadSlots>, kCount> pad{};
};

inline Bindings defaults() {
  Bindings b;
  for (auto& k : b.keys) k.fill(0);
  for (auto& p : b.pad) p.fill(-1);
  auto K = [&](Action a, std::initializer_list<int> v) { int i = 0; for (int x : v) b.keys[a][std::size_t(i++)] = x; };
  auto P = [&](Action a, std::initializer_list<int> v) { int i = 0; for (int x : v) b.pad[a][std::size_t(i++)] = x; };
  K(kUp, {SDL_SCANCODE_W, SDL_SCANCODE_UP});
  K(kDown, {SDL_SCANCODE_S, SDL_SCANCODE_DOWN});
  K(kLeft, {SDL_SCANCODE_A, SDL_SCANCODE_LEFT});
  K(kRight, {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT});
  K(kCross, {SDL_SCANCODE_SPACE, SDL_SCANCODE_K});
  K(kCircle, {SDL_SCANCODE_L});
  K(kSquare, {SDL_SCANCODE_J, kMouseBase + SDL_BUTTON_LEFT});
  K(kTriangle, {SDL_SCANCODE_I});
  K(kL1, {SDL_SCANCODE_Q, SDL_SCANCODE_LSHIFT, kMouseBase + SDL_BUTTON_RIGHT});
  K(kR1, {SDL_SCANCODE_E, SDL_SCANCODE_LCTRL, kMouseBase + SDL_BUTTON_MIDDLE});
  K(kL2, {SDL_SCANCODE_1});
  K(kR2, {SDL_SCANCODE_3});
  K(kStart, {SDL_SCANCODE_RETURN});
  K(kSelect, {SDL_SCANCODE_BACKSPACE});
  K(kStrafeL, {SDL_SCANCODE_Z});
  K(kStrafeR, {SDL_SCANCODE_C});
  P(kUp, {SDL_CONTROLLER_BUTTON_DPAD_UP});
  P(kDown, {SDL_CONTROLLER_BUTTON_DPAD_DOWN});
  P(kLeft, {SDL_CONTROLLER_BUTTON_DPAD_LEFT});
  P(kRight, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT});
  P(kCross, {SDL_CONTROLLER_BUTTON_A});
  P(kCircle, {SDL_CONTROLLER_BUTTON_B});
  P(kSquare, {SDL_CONTROLLER_BUTTON_X});
  P(kTriangle, {SDL_CONTROLLER_BUTTON_Y});
  P(kL1, {SDL_CONTROLLER_BUTTON_LEFTSHOULDER});
  P(kR1, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER});
  P(kL2, {kPadTriggerL});
  P(kR2, {kPadTriggerR});
  P(kStart, {SDL_CONTROLLER_BUTTON_START});
  P(kSelect, {SDL_CONTROLLER_BUTTON_BACK});
  return b;
}

inline Bindings defaultsP2() {
  Bindings b;
  for (auto& k : b.keys) k.fill(0);
  for (auto& p : b.pad) p.fill(-1);
  auto K = [&](Action a, std::initializer_list<int> v) { int i = 0; for (int x : v) b.keys[a][std::size_t(i++)] = x; };
  K(kUp, {SDL_SCANCODE_UP});
  K(kDown, {SDL_SCANCODE_DOWN});
  K(kLeft, {SDL_SCANCODE_LEFT});
  K(kRight, {SDL_SCANCODE_RIGHT});
  K(kCross, {SDL_SCANCODE_KP_2, SDL_SCANCODE_RCTRL});
  K(kCircle, {SDL_SCANCODE_KP_6, SDL_SCANCODE_RSHIFT});
  K(kSquare, {SDL_SCANCODE_KP_4, SDL_SCANCODE_SLASH});
  K(kTriangle, {SDL_SCANCODE_KP_8, SDL_SCANCODE_APOSTROPHE});
  K(kL1, {SDL_SCANCODE_KP_7, SDL_SCANCODE_PERIOD});
  K(kR1, {SDL_SCANCODE_KP_9, SDL_SCANCODE_SEMICOLON});
  K(kL2, {SDL_SCANCODE_KP_1});
  K(kR2, {SDL_SCANCODE_KP_3});
  K(kStart, {SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RALT});
  K(kSelect, {SDL_SCANCODE_KP_PLUS});
  K(kStrafeL, {SDL_SCANCODE_KP_0});
  K(kStrafeR, {SDL_SCANCODE_KP_PERIOD});
  b.pad = defaults().pad;
  return b;
}

inline bool reservedKey(int sc) {
  return sc == SDL_SCANCODE_ESCAPE || (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

inline std::string keyIniName(int code) {
  if (code >= kMouseBase) {
    switch (code - kMouseBase) {
      case SDL_BUTTON_LEFT: return "Raton izquierdo";
      case SDL_BUTTON_RIGHT: return "Raton derecho";
      case SDL_BUTTON_MIDDLE: return "Raton medio";
      case SDL_BUTTON_X1: return "Raton 4";
      case SDL_BUTTON_X2: return "Raton 5";
      default: return "";
    }
  }
  if (code <= 0) return "";
  return SDL_GetScancodeName(SDL_Scancode(code));
}
inline int keyFromIniName(const std::string& n) {
  if (n == "Raton izquierdo") return kMouseBase + SDL_BUTTON_LEFT;
  if (n == "Raton derecho") return kMouseBase + SDL_BUTTON_RIGHT;
  if (n == "Raton medio") return kMouseBase + SDL_BUTTON_MIDDLE;
  if (n == "Raton 4") return kMouseBase + SDL_BUTTON_X1;
  if (n == "Raton 5") return kMouseBase + SDL_BUTTON_X2;
  const SDL_Scancode s = SDL_GetScancodeFromName(n.c_str());
  return s == SDL_SCANCODE_UNKNOWN ? 0 : int(s);
}

inline std::string keyLabel(int code) {
  if (code >= kMouseBase) {
    switch (code - kMouseBase) {
      case SDL_BUTTON_LEFT: return "RATÓN IZQ.";
      case SDL_BUTTON_RIGHT: return "RATÓN DER.";
      case SDL_BUTTON_MIDDLE: return "RATÓN CENTRO";
      case SDL_BUTTON_X1: return "RATÓN 4";
      case SDL_BUTTON_X2: return "RATÓN 5";
      default: return "?";
    }
  }
  switch (code) {
    case SDL_SCANCODE_SPACE: return "ESPACIO";
    case SDL_SCANCODE_RETURN: return "ENTER";
    case SDL_SCANCODE_KP_ENTER: return "ENTER (NUM)";
    case SDL_SCANCODE_BACKSPACE: return "RETROCESO";
    case SDL_SCANCODE_TAB: return "TAB";
    case SDL_SCANCODE_UP: return "FLECHA ARRIBA";
    case SDL_SCANCODE_DOWN: return "FLECHA ABAJO";
    case SDL_SCANCODE_LEFT: return "FLECHA IZQ.";
    case SDL_SCANCODE_RIGHT: return "FLECHA DER.";
    case SDL_SCANCODE_LSHIFT: return "MAYÚS IZQ.";
    case SDL_SCANCODE_RSHIFT: return "MAYÚS DER.";
    case SDL_SCANCODE_LCTRL: return "CTRL IZQ.";
    case SDL_SCANCODE_RCTRL: return "CTRL DER.";
    case SDL_SCANCODE_LALT: return "ALT";
    case SDL_SCANCODE_RALT: return "ALT GR";
    case SDL_SCANCODE_CAPSLOCK: return "BLOQ MAYÚS";
    case SDL_SCANCODE_DELETE: return "SUPR";
    case SDL_SCANCODE_INSERT: return "INSERT";
    case SDL_SCANCODE_HOME: return "INICIO";
    case SDL_SCANCODE_END: return "FIN";
    case SDL_SCANCODE_PAGEUP: return "RE PÁG";
    case SDL_SCANCODE_PAGEDOWN: return "AV PÁG";
    case SDL_SCANCODE_SEMICOLON: return "Ñ";
    default: break;
  }
  std::string n = SDL_GetScancodeName(SDL_Scancode(code));
  for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
  if (n.rfind("KEYPAD ", 0) == 0) n = n.substr(7) + " (NUM)";
  return n.empty() ? "?" : n;
}

inline std::string padIniName(int code) {
  if (code == kPadTriggerL) return "lefttrigger";
  if (code == kPadTriggerR) return "righttrigger";
  if (code < 0) return "";
  const char* n = SDL_GameControllerGetStringForButton(SDL_GameControllerButton(code));
  return n ? n : "";
}
inline int padFromIniName(const std::string& n) {
  if (n == "lefttrigger") return kPadTriggerL;
  if (n == "righttrigger") return kPadTriggerR;
  const auto b = SDL_GameControllerGetButtonFromString(n.c_str());
  return b == SDL_CONTROLLER_BUTTON_INVALID ? -1 : int(b);
}

enum class PadFamily { Xbox, PlayStation, Nintendo };
inline PadFamily padFamily(SDL_GameController* p) {
  if (!p) return PadFamily::Xbox;
#if SDL_VERSION_ATLEAST(2, 0, 12)
  switch (SDL_GameControllerGetType(p)) {
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5: return PadFamily::PlayStation;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return PadFamily::Nintendo;
    default: break;
  }
#endif
  return PadFamily::Xbox;
}
inline std::string padLabel(int code, PadFamily f) {
  const bool ps = f == PadFamily::PlayStation, nin = f == PadFamily::Nintendo;
  switch (code) {
    case SDL_CONTROLLER_BUTTON_A: return ps ? "CRUZ" : nin ? "B" : "A";
    case SDL_CONTROLLER_BUTTON_B: return ps ? "CÍRCULO" : nin ? "A" : "B";
    case SDL_CONTROLLER_BUTTON_X: return ps ? "CUADRADO" : nin ? "Y" : "X";
    case SDL_CONTROLLER_BUTTON_Y: return ps ? "TRIÁNGULO" : nin ? "X" : "Y";
    case SDL_CONTROLLER_BUTTON_BACK: return ps ? "SHARE" : nin ? "-" : "VIEW";
    case SDL_CONTROLLER_BUTTON_GUIDE: return ps ? "PS" : nin ? "HOME" : "GUÍA";
    case SDL_CONTROLLER_BUTTON_START: return ps ? "OPTIONS" : nin ? "+" : "MENU";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return ps ? "L3" : "STICK IZQ.";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return ps ? "R3" : "STICK DER.";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ps ? "L1" : nin ? "L" : "LB";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ps ? "R1" : nin ? "R" : "RB";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "CRUCETA ARRIBA";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "CRUCETA ABAJO";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "CRUCETA IZQ.";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "CRUCETA DER.";
    case kPadTriggerL: return ps ? "L2" : nin ? "ZL" : "LT";
    case kPadTriggerR: return ps ? "R2" : nin ? "ZR" : "RT";
    default: break;
  }
  if (code >= 0) { std::string n = padIniName(code); for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c))); return n; }
  return "";
}

inline bool keyDown(int code, const Uint8* k, Uint32 mouseButtons, bool mouseOk) {
  if (code >= kMouseBase) return mouseOk && (mouseButtons & SDL_BUTTON(code - kMouseBase));
  return code > 0 && code < SDL_NUM_SCANCODES && k[code];
}
inline bool padDown(int code, SDL_GameController* p) {
  if (!p || code < 0) return false;
  if (code == kPadTriggerL) return SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
  if (code == kPadTriggerR) return SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
  return SDL_GameControllerGetButton(p, SDL_GameControllerButton(code)) != 0;
}

inline int padPressed(SDL_GameController* p) {
  if (!p) return -1;
  for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
    if (b != SDL_CONTROLLER_BUTTON_GUIDE && SDL_GameControllerGetButton(p, SDL_GameControllerButton(b))) return b;
  if (padDown(kPadTriggerL, p)) return kPadTriggerL;
  if (padDown(kPadTriggerR, p)) return kPadTriggerR;
  return -1;
}

template <std::size_t N>
inline void assign(std::array<std::array<int, N>, kCount>& table, int action, int code, int empty) {
  for (int a = 0; a < kCount; ++a)
    for (auto& c : table[std::size_t(a)])
      if (c == code) c = empty;
  auto& row = table[std::size_t(action)];
  std::array<int, N> out;
  out.fill(empty);
  out[0] = code;
  std::size_t j = 1;
  for (int c : row) if (c != empty && c != code && j < N) out[j++] = c;
  row = out;
}

}
