#pragma once
#include <algorithm>
#include <cmath>

namespace pulseui {

struct Point {
  float x = 0, y = 0;
  constexpr Point() = default;
  constexpr Point(float x_, float y_) : x(x_), y(y_) {}
  constexpr Point operator+(Point o) const { return {x + o.x, y + o.y}; }
  constexpr Point operator-(Point o) const { return {x - o.x, y - o.y}; }
  constexpr Point operator*(float k) const { return {x * k, y * k}; }
  constexpr bool operator==(const Point&) const = default;
};

struct Size {
  float w = 0, h = 0;
  constexpr Size() = default;
  constexpr Size(float w_, float h_) : w(w_), h(h_) {}
  constexpr bool operator==(const Size&) const = default;
};

struct Insets {
  float left = 0, top = 0, right = 0, bottom = 0;
  constexpr Insets() = default;
  constexpr Insets(float all) : left(all), top(all), right(all), bottom(all) {}
  constexpr Insets(float h, float v) : left(h), top(v), right(h), bottom(v) {}
  constexpr Insets(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
  constexpr float horizontal() const { return left + right; }
  constexpr float vertical() const { return top + bottom; }
  constexpr bool operator==(const Insets&) const = default;
};

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  constexpr Rect() = default;
  constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
  constexpr Rect(Point p, Size s) : x(p.x), y(p.y), w(s.w), h(s.h) {}

  static constexpr Rect from_points(Point a, Point b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
  }
  constexpr float right() const { return x + w; }
  constexpr float bottom() const { return y + h; }
  constexpr Point pos() const { return {x, y}; }
  constexpr Size size() const { return {w, h}; }
  constexpr Point center() const { return {x + w * 0.5f, y + h * 0.5f}; }
  constexpr bool empty() const { return w <= 0 || h <= 0; }
  constexpr bool contains(Point p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
  constexpr bool intersects(const Rect& r) const {
    return r.x < right() && r.right() > x && r.y < bottom() && r.bottom() > y;
  }
  constexpr Rect intersected(const Rect& r) const {
    const float l = std::max(x, r.x), t = std::max(y, r.y);
    const float rr = std::min(right(), r.right()), b = std::min(bottom(), r.bottom());
    return (rr <= l || b <= t) ? Rect{l, t, 0, 0} : Rect{l, t, rr - l, b - t};
  }
  constexpr Rect united(const Rect& r) const {
    if (empty()) return r;
    if (r.empty()) return *this;
    const float l = std::min(x, r.x), t = std::min(y, r.y);
    return {l, t, std::max(right(), r.right()) - l, std::max(bottom(), r.bottom()) - t};
  }
  constexpr Rect inset(const Insets& i) const {
    return {x + i.left, y + i.top, std::max(0.f, w - i.horizontal()), std::max(0.f, h - i.vertical())};
  }
  constexpr Rect inset(float d) const { return inset(Insets{d}); }
  constexpr Rect outset(float d) const { return {x - d, y - d, w + 2 * d, h + 2 * d}; }
  constexpr Rect translated(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
  constexpr bool operator==(const Rect&) const = default;
};

// Text / content alignment flags.
namespace Align {
enum : int {
  Left = 1, HCenter = 2, Right = 4,
  Top = 8, VCenter = 16, Bottom = 32,
  Center = HCenter | VCenter,
  LeftCenter = Left | VCenter,
  RightCenter = Right | VCenter,
};
}

enum class Orientation { Horizontal, Vertical };

inline Rect align_rect(Size s, const Rect& area, int align) {
  float x = area.x, y = area.y;
  if (align & Align::HCenter) x = area.x + (area.w - s.w) * 0.5f;
  else if (align & Align::Right) x = area.right() - s.w;
  if (align & Align::VCenter) y = area.y + (area.h - s.h) * 0.5f;
  else if (align & Align::Bottom) y = area.bottom() - s.h;
  return {x, y, s.w, s.h};
}

} // namespace pulseui
