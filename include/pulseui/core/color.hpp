#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace pulseui {

// RGBA color, float components in [0, 1].
struct Color {
  float r = 0, g = 0, b = 0, a = 1;

  constexpr Color() = default;
  constexpr Color(float r_, float g_, float b_, float a_ = 1.f) : r(r_), g(g_), b(b_), a(a_) {}

  // Color::hex(0x3D7EFF)
  static constexpr Color hex(std::uint32_t rgb, float alpha = 1.f) {
    return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
  }
  static constexpr Color rgb8(int r, int g, int b, int a = 255) {
    return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
  }
  static constexpr Color transparent() { return {0, 0, 0, 0}; }
  static constexpr Color white() { return {1, 1, 1, 1}; }
  static constexpr Color black() { return {0, 0, 0, 1}; }

  // "#RGB", "#RRGGBB", "#RRGGBBAA"
  static std::optional<Color> parse(std::string_view s) {
    if (!s.empty() && s[0] == '#') s.remove_prefix(1);
    int v[8] = {};
    if (s.size() != 3 && s.size() != 6 && s.size() != 8) return std::nullopt;
    for (std::size_t i = 0; i < s.size(); ++i) {
      const char c = s[i];
      if (c >= '0' && c <= '9') v[i] = c - '0';
      else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
      else return std::nullopt;
    }
    if (s.size() == 3) return rgb8(v[0] * 17, v[1] * 17, v[2] * 17);
    const int a = s.size() == 8 ? v[6] * 16 + v[7] : 255;
    return rgb8(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5], a);
  }

  std::string to_hex() const {
    auto c8 = [](float f) { return static_cast<int>(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
    char buf[16];
    if (a < 1.f) std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", c8(r), c8(g), c8(b), c8(a));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c8(r), c8(g), c8(b));
    return buf;
  }

  constexpr Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
  constexpr Color lighter(float k = 0.1f) const {
    return {r + (1 - r) * k, g + (1 - g) * k, b + (1 - b) * k, a};
  }
  constexpr Color darker(float k = 0.1f) const { return {r * (1 - k), g * (1 - k), b * (1 - k), a}; }
  static constexpr Color mix(Color x, Color y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
  }
  constexpr float luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

  // HSV: hue in degrees [0, 360), saturation and value in [0, 1].
  static Color hsv(float h, float s, float v, float alpha = 1.f) {
    h = h - 360.f * static_cast<float>(static_cast<int>(h / 360.f));
    if (h < 0) h += 360.f;
    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);
    const float c = v * s, hp = h / 60.f;
    const float x = c * (1 - std::abs(hp - 2.f * static_cast<float>(static_cast<int>(hp / 2.f)) - 1));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hp < 1) { r1 = c; g1 = x; }
    else if (hp < 2) { r1 = x; g1 = c; }
    else if (hp < 3) { g1 = c; b1 = x; }
    else if (hp < 4) { g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; b1 = c; }
    else { r1 = c; b1 = x; }
    const float m = v - c;
    return {r1 + m, g1 + m, b1 + m, alpha};
  }
  void to_hsv(float& h, float& s, float& v) const {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 0) h = 0;
    else if (mx == r) h = 60.f * ((g - b) / d);
    else if (mx == g) h = 60.f * ((b - r) / d + 2);
    else h = 60.f * ((r - g) / d + 4);
    if (h < 0) h += 360.f;
  }
  constexpr bool operator==(const Color&) const = default;
};

} // namespace pulseui
