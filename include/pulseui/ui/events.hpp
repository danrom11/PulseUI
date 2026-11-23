#pragma once
#include <string>
#include <string_view>
#include <vector>

#include <pulseui/core/geometry.hpp>

namespace pulseui {

enum class Key {
  Unknown,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  Escape, Tab, Backspace, Enter, Space, Insert, Delete, Home, End, PageUp, PageDown,
  Left, Right, Up, Down, Shift, Control, Alt, Super,
  Slash, // the "/ ?" key (US layout position)
  Menu,  // the context-menu key
};

namespace Mod {
enum : unsigned { None = 0, Shift = 1, Ctrl = 2, Alt = 4, Super = 8 /* Cmd / Win */ };
#ifdef __APPLE__
inline constexpr unsigned Primary = Super; // Cmd on macOS
#else
inline constexpr unsigned Primary = Ctrl;
#endif
} // namespace Mod

enum class MouseButton { None, Left, Right, Middle };

enum class Cursor { Arrow, IBeam, Hand, Crosshair, SizeWE, SizeNS, SizeNWSE, SizeNESW, SizeAll };

// One input event. Kept as a single struct so it can flow through a single
// observable stream (window.input()) and be filtered with Pulse operators.
struct InputEvent {
  enum Type { MouseDown, MouseUp, MouseMove, MouseLeave, Wheel, KeyDown, KeyUp, Text } type = MouseMove;
  Point pos{};        // local to the receiving view (window coordinates for window.input())
  Point window_pos{}; // always in window coordinates
  MouseButton button = MouseButton::None;
  int clicks = 1;     // 2 for double click
  float wheel_dx = 0, wheel_dy = 0; // in "lines"
  Key key = Key::Unknown;
  unsigned mods = 0;
  bool repeat = false;
  std::string text;   // UTF-8 for Text events

  bool is_mouse() const { return type <= Wheel; }
  bool is_key() const { return type == KeyDown || type == KeyUp; }
  bool primary() const { return (mods & Mod::Primary) != 0; }
};

// Data carried by drag and drop (between views, windows and other applications).
struct DragData {
  std::vector<std::string> files; // absolute paths
  std::string text;               // UTF-8
  bool empty() const { return files.empty() && text.empty(); }
};

// "Ctrl+S", "Ctrl+Shift+Z", "F5". "Ctrl" means Cmd on macOS (like Qt).
struct Shortcut {
  Key key = Key::Unknown;
  unsigned mods = 0;
  static Shortcut parse(std::string_view s);
  bool matches(const InputEvent& e) const {
    return e.type == InputEvent::KeyDown && e.key == key &&
           (e.mods & (Mod::Shift | Mod::Ctrl | Mod::Alt | Mod::Super)) == mods;
  }
};

} // namespace pulseui
