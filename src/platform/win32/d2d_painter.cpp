#include "d2d_painter.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace pulseui::platform::win32 {

namespace {
D2D1_RECT_F rf(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
D2D1_COLOR_F cf(Color c, float opacity) { return D2D1::ColorF(c.r, c.g, c.b, c.a * opacity); }
} // namespace

D2DPainter::D2DPainter(ID2D1RenderTarget* rt, BitmapCache& cache) : rt_(rt), cache_(cache) {
  rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush_.put());
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
}

D2DPainter::~D2DPainter() {
  while (clips_ > 0) {
    rt_->PopAxisAlignedClip();
    --clips_;
  }
  rt_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void D2DPainter::save() { stack_.push_back({tx_, ty_, clips_, opacity_}); }

void D2DPainter::restore() {
  if (stack_.empty()) return;
  const State s = stack_.back();
  stack_.pop_back();
  while (clips_ > s.clips) {
    rt_->PopAxisAlignedClip();
    --clips_;
  }
  tx_ = s.tx;
  ty_ = s.ty;
  opacity_ = s.opacity;
  rt_->SetTransform(D2D1::Matrix3x2F::Translation(tx_, ty_));
}

void D2DPainter::translate(float dx, float dy) {
  tx_ += dx;
  ty_ += dy;
  rt_->SetTransform(D2D1::Matrix3x2F::Translation(tx_, ty_));
}

void D2DPainter::clip(const Rect& r) {
  rt_->PushAxisAlignedClip(rf(r), D2D1_ANTIALIAS_MODE_ALIASED);
  ++clips_;
}

void D2DPainter::set_opacity(float a) { opacity_ *= std::clamp(a, 0.f, 1.f); }

ID2D1SolidColorBrush* D2DPainter::brush(Color c) {
  brush_->SetColor(cf(c, opacity_));
  return brush_.get();
}

void D2DPainter::fill_rect(const Rect& r, Color c, float radius) {
  if (c.a <= 0) return;
  if (radius > 0)
    rt_->FillRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), brush(c));
  else
    rt_->FillRectangle(rf(r), brush(c));
}

void D2DPainter::stroke_rect(const Rect& r, Color c, float width, float radius) {
  if (c.a <= 0 || width <= 0) return;
  const Rect in = r.inset(width * 0.5f);
  if (radius > 0)
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(rf(in), radius, radius), brush(c), width);
  else
    rt_->DrawRectangle(rf(in), brush(c), width);
}

void D2DPainter::fill_ellipse(const Rect& r, Color c) {
  if (c.a <= 0) return;
  rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.x + r.w / 2, r.y + r.h / 2), r.w / 2, r.h / 2),
                   brush(c));
}

void D2DPainter::stroke_ellipse(const Rect& r, Color c, float width) {
  if (c.a <= 0) return;
  const Rect in = r.inset(width * 0.5f);
  rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(in.x + in.w / 2, in.y + in.h / 2), in.w / 2, in.h / 2),
                   brush(c), width);
}

void D2DPainter::line(Point a, Point b, Color c, float width) {
  rt_->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), brush(c), width);
}

Com<ID2D1PathGeometry> D2DPainter::path(const std::vector<Point>& pts, bool closed, bool filled) {
  Com<ID2D1PathGeometry> geo;
  if (pts.size() < 2) return geo;
  factories().d2d->CreatePathGeometry(geo.put());
  Com<ID2D1GeometrySink> sink;
  geo->Open(sink.put());
  sink->BeginFigure(D2D1::Point2F(pts[0].x, pts[0].y),
                    filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
  for (std::size_t i = 1; i < pts.size(); ++i) sink->AddLine(D2D1::Point2F(pts[i].x, pts[i].y));
  sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
  sink->Close();
  return geo;
}

void D2DPainter::polyline(const std::vector<Point>& pts, Color c, float width, bool closed) {
  auto g = path(pts, closed, false);
  if (g) rt_->DrawGeometry(g.get(), brush(c), width);
}

void D2DPainter::fill_polygon(const std::vector<Point>& pts, Color c) {
  auto g = path(pts, true, true);
  if (g) rt_->FillGeometry(g.get(), brush(c));
}

void D2DPainter::fill_gradient(const Rect& r, Color top, Color bottom, float radius) {
  D2D1_GRADIENT_STOP stops[2] = {{0.f, cf(top, opacity_)}, {1.f, cf(bottom, opacity_)}};
  Com<ID2D1GradientStopCollection> coll;
  rt_->CreateGradientStopCollection(stops, 2, coll.put());
  Com<ID2D1LinearGradientBrush> lg;
  rt_->CreateLinearGradientBrush(
      D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.y + r.h)),
      coll.get(), lg.put());
  if (!lg) return;
  if (radius > 0)
    rt_->FillRoundedRectangle(D2D1::RoundedRect(rf(r), radius, radius), lg.get());
  else
    rt_->FillRectangle(rf(r), lg.get());
}

namespace {
// Text layouts are device independent: cache them across frames.
IDWriteTextLayout* cached_layout(std::string_view s, const Font& f, float w, float h, int align, bool wrap) {
  static std::unordered_map<std::string, Com<IDWriteTextLayout>> cache;
  std::string key;
  key.reserve(s.size() + 48);
  key.append(f.family).push_back('\x1f');
  key.append(std::to_string(static_cast<int>(f.size * 64) * 16 + static_cast<int>(f.weight) / 100 + (f.italic ? 8 : 0)));
  key.push_back('\x1f');
  key.append(std::to_string(static_cast<int>(w * 4))).push_back('x');
  key.append(std::to_string(static_cast<int>(h * 4))).push_back('\x1f');
  key.append(std::to_string(align * 2 + (wrap ? 1 : 0))).push_back('\x1f');
  key.append(s);
  if (auto it = cache.find(key); it != cache.end()) return it->second.get();
  if (cache.size() > 2048) cache.clear();

  IDWriteTextFormat* fmt = text_format(f);
  if (!fmt) return nullptr;
  const std::wstring ws = widen(s);
  Com<IDWriteTextLayout> layout;
  factories().dwrite->CreateTextLayout(ws.c_str(), static_cast<UINT32>(ws.size()), fmt, std::max(0.f, w),
                                       std::max(0.f, h), layout.put());
  if (!layout) return nullptr;
  layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  layout->SetTextAlignment((align & Align::HCenter)  ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : (align & Align::Right) ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                    : DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetParagraphAlignment((align & Align::VCenter)   ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                : (align & Align::Bottom) ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                          : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  if (!wrap) {
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    Com<IDWriteInlineObject> ellipsis;
    factories().dwrite->CreateEllipsisTrimmingSign(fmt, ellipsis.put());
    layout->SetTrimming(&trim, ellipsis.get());
  }
  auto* raw = layout.get();
  cache.emplace(std::move(key), std::move(layout));
  return raw;
}
} // namespace

void D2DPainter::text(const Rect& r, std::string_view s, const Font& f, Color c, int align, bool wrap) {
  if (s.empty() || c.a <= 0) return;
  if (IDWriteTextLayout* layout = cached_layout(s, f, r.w, r.h, align, wrap))
    rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout, brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void D2DPainter::image(const Image& img, const Rect& dst, float opacity) {
  if (!img.valid()) return;
  auto& slot = cache_.bitmaps[img.cache_key()];
  if (!slot) {
    // straight RGBA -> premultiplied BGRA
    const int w = img.width(), h = img.height();
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t* src = img.pixels();
    for (std::size_t i = 0; i < bgra.size(); i += 4) {
      const unsigned a = src[i + 3];
      bgra[i + 0] = static_cast<std::uint8_t>(src[i + 2] * a / 255);
      bgra[i + 1] = static_cast<std::uint8_t>(src[i + 1] * a / 255);
      bgra[i + 2] = static_cast<std::uint8_t>(src[i + 0] * a / 255);
      bgra[i + 3] = static_cast<std::uint8_t>(a);
    }
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    rt_->CreateBitmap(D2D1::SizeU(w, h), bgra.data(), w * 4, props, slot.put());
  }
  if (slot)
    rt_->DrawBitmap(slot.get(), rf(dst), opacity * opacity_,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

} // namespace pulseui::platform::win32
