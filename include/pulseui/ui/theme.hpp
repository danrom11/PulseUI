#pragma once
#include <string>

#include <pulse/core/property.hpp>
#include <pulseui/core/color.hpp>
#include <pulseui/gfx/font.hpp>

namespace pulseui {

struct Theme {
  std::string name = "dark";

  Color window, surface, surface_alt, control, control_hover, control_pressed, input;
  Color border, border_focus, text, text_dim, text_disabled;
  Color accent, accent_hover, accent_pressed, on_accent;
  Color selection, danger, success, warning, shadow, tooltip, scrollbar;

  float radius = 6, radius_small = 4;
  float padding = 12, spacing = 8;
  float control_height = 30;

  Font font{13.f}, font_small{11.5f}, font_title{20.f, FontWeight::SemiBold}, font_mono;

  static Theme dark();
  static Theme light();
  bool operator==(const Theme&) const = default;
};

// The current theme is itself reactive: assigning a new one repaints every window.
//   pulseui::theme() = Theme::light();
pulse::property<Theme>& theme();

// Fast read access to the current theme (used while painting).
const Theme& th();

} // namespace pulseui
