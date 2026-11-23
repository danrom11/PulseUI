#pragma once
#include <cstddef>
#include <string_view>
#include <vector>

#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/gfx/font.hpp>
#include <pulseui/gfx/image.hpp>

namespace pulseui {

// Immediate-mode drawing API implemented by each backend
// (Direct2D/DirectWrite on Windows, CoreGraphics/AppKit on macOS).
// All coordinates are logical pixels; HiDPI scaling is handled by the backend.
class Painter {
public:
  virtual ~Painter() = default;

  // state stack: transform + clip
  virtual void save() = 0;
  virtual void restore() = 0;
  virtual void translate(float dx, float dy) = 0;
  virtual void clip(const Rect& r) = 0; // intersects with the current clip
  virtual void set_opacity(float a) = 0; // multiplies (restored by restore())

  // shapes
  virtual void fill_rect(const Rect& r, Color c, float radius = 0) = 0;
  virtual void stroke_rect(const Rect& r, Color c, float width = 1, float radius = 0) = 0;
  virtual void fill_ellipse(const Rect& r, Color c) = 0;
  virtual void stroke_ellipse(const Rect& r, Color c, float width = 1) = 0;
  virtual void line(Point a, Point b, Color c, float width = 1) = 0;
  virtual void polyline(const std::vector<Point>& pts, Color c, float width = 1, bool closed = false) = 0;
  virtual void fill_polygon(const std::vector<Point>& pts, Color c) = 0;
  virtual void fill_gradient(const Rect& r, Color top, Color bottom, float radius = 0) = 0;
  // Several closed contours filled as one shape (holes via the fill rule).
  virtual void fill_path(const std::vector<std::vector<Point>>& contours, Color c, bool even_odd = false) {
    for (auto& ct : contours) fill_polygon(ct, c); // fallback without holes
  }

  // text (single line unless `wrap`; elided with "…" if it does not fit)
  virtual void text(const Rect& r, std::string_view s, const Font& f, Color c,
                    int align = Align::LeftCenter, bool wrap = false) = 0;

  virtual void image(const Image& img, const Rect& dst, float opacity = 1.f) = 0;
};

// Text measurement (available outside of painting, e.g. for layout).
Size measure_text(std::string_view s, const Font& f, float max_width = 0);
float line_height(const Font& f);
// x offset of every caret position (one per UTF-8 code point boundary, incl. 0 and end).
std::vector<float> caret_stops(std::string_view s, const Font& f);

} // namespace pulseui
