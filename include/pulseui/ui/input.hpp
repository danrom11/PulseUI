#pragma once
#include <cstdint>
#include <string>

namespace pulseui::ui {

  enum class MouseButton { Left, Right, Middle };

  struct Point { float x{}, y{}; };
  struct Rect  { float x{}, y{}, w{}, h{}; };
  struct Color { float r{}, g{}, b{}, a{1.f}; };

  struct InputEvent {
    enum Type { MouseDown, MouseUp, MouseMove, KeyDown, KeyUp, Scroll } type{};
    Point       pos{};
    int         keycode{};
    float       scrollY{};
    std::string text;
  };

} // namespace pulseui::ui
