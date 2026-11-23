#pragma once
// Painter over a Cairo context, text through Pango. Shared by the Linux
// backends (X11 today, Wayland later): it does not depend on the window system.
#include <cairo.h>

#include <vector>

#include <pulseui/gfx/painter.hpp>

namespace pulseui::platform::cairo {

class CairoPainter final : public Painter {
public:
  explicit CairoPainter(cairo_t* cr);
  ~CairoPainter() override;

  void save() override;
  void restore() override;
  void translate(float dx, float dy) override;
  void clip(const Rect& r) override;
  void set_opacity(float a) override;

  void fill_rect(const Rect& r, Color c, float radius) override;
  void stroke_rect(const Rect& r, Color c, float width, float radius) override;
  void fill_ellipse(const Rect& r, Color c) override;
  void stroke_ellipse(const Rect& r, Color c, float width) override;
  void line(Point a, Point b, Color c, float width) override;
  void polyline(const std::vector<Point>& pts, Color c, float width, bool closed) override;
  void fill_polygon(const std::vector<Point>& pts, Color c) override;
  void fill_gradient(const Rect& r, Color top, Color bottom, float radius) override;
  void fill_path(const std::vector<std::vector<Point>>& contours, Color c, bool even_odd) override;
  void text(const Rect& r, std::string_view s, const Font& f, Color c, int align, bool wrap) override;
  void image(const Image& img, const Rect& dst, float opacity) override;

private:
  void source(Color c);
  void rect_path(const Rect& r, float radius);
  void ellipse_path(const Rect& r);
  void points_path(const std::vector<Point>& pts, bool closed);

  cairo_t* cr_;
  std::vector<float> opacity_stack_;
  float opacity_ = 1;
};

// Straight RGBA8 <-> Cairo ARGB32 (premultiplied, native endian).
void rgba_to_argb32(const std::uint8_t* rgba, int w, int h, unsigned char* dst, int stride);
void argb32_to_rgba(const unsigned char* src, int w, int h, int stride, std::uint8_t* rgba);

} // namespace pulseui::platform::cairo
