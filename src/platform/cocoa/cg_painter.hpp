#pragma once
#import <Cocoa/Cocoa.h>

#include <vector>

#include <pulse/core/subscription.hpp>
#include <pulseui/gfx/painter.hpp>
#include <pulseui/ui/menu.hpp>

namespace pulseui {
class Window;
class View;
} // namespace pulseui

namespace pulseui::platform::cocoa {

// cocoa_a11y.mm
NSArray* a11y_window_children(NSView* host, Window* w);
id a11y_hit_test(NSView* host, Window* w, NSPoint screen);
id a11y_focused(NSView* host, Window* w);
void a11y_post_focus(NSView* host, Window* w, View* v);
void a11y_forget(NSView* host);

// cocoa_menu.mm
NSMenu* default_main_menu();
NSMenu* build_main_menu(const MenuItems& items, std::vector<pulse::subscription>& subs);

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
  void fill_path(const std::vector<std::vector<Point>>& contours, Color c, bool even_odd) override;
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
