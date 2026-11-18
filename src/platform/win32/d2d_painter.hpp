#pragma once
#include <unordered_map>
#include <vector>

#include <pulseui/gfx/painter.hpp>

#include "win32_common.hpp"

namespace pulseui::platform::win32 {

// Per render-target cache of GPU bitmaps.
struct BitmapCache {
  std::unordered_map<const void*, Com<ID2D1Bitmap>> bitmaps;
  void clear() { bitmaps.clear(); }
};

class D2DPainter final : public Painter {
public:
  D2DPainter(ID2D1RenderTarget* rt, BitmapCache& cache);
  ~D2DPainter() override;

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
  struct State {
    float tx, ty;
    int clips;
    float opacity;
  };
  ID2D1SolidColorBrush* brush(Color c);
  Com<ID2D1PathGeometry> path(const std::vector<Point>& pts, bool closed, bool filled);

  ID2D1RenderTarget* rt_;
  BitmapCache& cache_;
  Com<ID2D1SolidColorBrush> brush_;
  std::vector<State> stack_;
  int clips_ = 0;
  float opacity_ = 1;
  float tx_ = 0, ty_ = 0;
};

} // namespace pulseui::platform::win32
