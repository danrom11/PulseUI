#pragma once
#import <Cocoa/Cocoa.h>

#include <vector>

#include <pulseui/gfx/painter.hpp>

namespace pulseui::platform::cocoa {

NSFont* ns_font(const Font& f);
NSDictionary* text_attributes(const Font& f, Color c, int align, bool wrap);

// Painter over a flipped CGContext (origin top-left, y down). Text and images
// go through AppKit, so an NSGraphicsContext must be current.
class CGPainter final : public Painter {
public:
  explicit CGPainter(CGContextRef ctx);
  ~CGPainter() override;

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
  void text(const Rect& r, std::string_view s, const Font& f, Color c, int align, bool wrap) override;
  void image(const Image& img, const Rect& dst, float opacity) override;

private:
  void add_rect_path(const Rect& r, float radius);
  CGContextRef ctx_;
  std::vector<float> opacity_stack_;
  float opacity_ = 1;
  int depth_ = 0;
};

} // namespace pulseui::platform::cocoa
