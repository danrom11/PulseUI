#include "cairo_painter.hpp"

#include <pango/pangocairo.h>
#if PULSEUI_HAS_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

#include "../platform.hpp"

namespace pulseui::platform {
namespace cairo {

// ─────────────────────────────────────────────────────────────────────────────
// Pixel formats
// ─────────────────────────────────────────────────────────────────────────────
void rgba_to_argb32(const std::uint8_t* rgba, int w, int h, unsigned char* dst, int stride) {
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<std::uint32_t*>(dst + static_cast<std::ptrdiff_t>(y) * stride);
    const std::uint8_t* s = rgba + static_cast<std::size_t>(y) * w * 4;
    for (int x = 0; x < w; ++x, s += 4) {
      const std::uint32_t a = s[3];
      const std::uint32_t r = s[0] * a / 255, g = s[1] * a / 255, b = s[2] * a / 255;
      row[x] = (a << 24) | (r << 16) | (g << 8) | b;
    }
  }
}

void argb32_to_rgba(const unsigned char* src, int w, int h, int stride, std::uint8_t* rgba) {
  for (int y = 0; y < h; ++y) {
    const auto* row = reinterpret_cast<const std::uint32_t*>(src + static_cast<std::ptrdiff_t>(y) * stride);
    std::uint8_t* d = rgba + static_cast<std::size_t>(y) * w * 4;
    for (int x = 0; x < w; ++x, d += 4) {
      const std::uint32_t p = row[x];
      const std::uint32_t a = p >> 24;
      auto un = [a](std::uint32_t c) { return static_cast<std::uint8_t>(a ? std::min<std::uint32_t>(255, c * 255 / a) : 0); };
      d[0] = un((p >> 16) & 0xFF);
      d[1] = un((p >> 8) & 0xFF);
      d[2] = un(p & 0xFF);
      d[3] = static_cast<std::uint8_t>(a);
    }
  }
}

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Text (Pango)
// ─────────────────────────────────────────────────────────────────────────────
// One device-independent context for measuring and drawing: metrics are not
// hinted, so a layout measures the same at every scale and on every surface.
PangoContext* text_context() {
  static PangoContext* ctx = [] {
    PangoContext* c = pango_font_map_create_context(pango_cairo_font_map_get_default());
    cairo_font_options_t* o = cairo_font_options_create();
    cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_GRAY);
    pango_cairo_context_set_font_options(c, o);
    cairo_font_options_destroy(o);
    pango_context_set_round_glyph_positions(c, FALSE);
    return c;
  }();
  return ctx;
}

PangoFontDescription* font_desc(const Font& f) {
  struct Key {
    std::string family;
    int size, weight;
    bool italic;
    bool operator==(const Key&) const = default;
  };
  struct Hash {
    std::size_t operator()(const Key& k) const {
      return std::hash<std::string>()(k.family) ^ (static_cast<std::size_t>(k.size) * 31 + k.weight * 7 + k.italic);
    }
  };
  static std::unordered_map<Key, PangoFontDescription*, Hash> cache;
  const float size = (std::isfinite(f.size) && f.size > 0.5f) ? f.size : 13.f;
  Key key{f.family, static_cast<int>(size * 100), static_cast<int>(f.weight), f.italic};
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  PangoFontDescription* d = pango_font_description_new();
  // fontconfig resolves the generic names to the desktop's fonts; Pango falls
  // back per character (Cyrillic, CJK, emoji) to whatever font has the glyph.
  pango_font_description_set_family(d, f.family.empty()         ? "Sans"
                                       : f.family == "monospace" ? "Monospace"
                                                                 : f.family.c_str());
  pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
  pango_font_description_set_weight(d, static_cast<PangoWeight>(static_cast<int>(f.weight)));
  pango_font_description_set_style(d, f.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
  cache.emplace(std::move(key), d);
  return d;
}

PangoLayout* make_layout(std::string_view s, const Font& f) {
  PangoLayout* l = pango_layout_new(text_context());
  pango_layout_set_font_description(l, font_desc(f));
  pango_layout_set_text(l, s.data(), static_cast<int>(s.size()));
  return l;
}

struct LayoutRef {
  PangoLayout* layout = nullptr;
  float height = 0; // logical height in pixels
};

// Layouts are device independent: cache them across frames (like the
// DirectWrite backend does).
LayoutRef cached_layout(std::string_view s, const Font& f, float w, int align, bool wrap) {
  struct Entry {
    PangoLayout* layout;
    float height;
  };
  static std::unordered_map<std::string, Entry> cache;
  std::string key;
  key.reserve(s.size() + 48);
  key.append(f.family).push_back('\x1f');
  key.append(std::to_string(static_cast<int>(f.size * 64) * 16 + static_cast<int>(f.weight) / 100 + (f.italic ? 8 : 0)));
  key.push_back('\x1f');
  key.append(std::to_string(static_cast<int>(w * 4))).push_back('\x1f');
  key.append(std::to_string((align & (Align::HCenter | Align::Right)) * 2 + (wrap ? 1 : 0))).push_back('\x1f');
  key.append(s);
  if (auto it = cache.find(key); it != cache.end()) return {it->second.layout, it->second.height};
  if (cache.size() > 2048) {
    for (auto& [k, e] : cache) g_object_unref(e.layout);
    cache.clear();
  }
  PangoLayout* l = make_layout(s, f);
  if (w > 0) pango_layout_set_width(l, static_cast<int>(w * PANGO_SCALE));
  if (wrap) {
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  } else if (w > 0) {
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
  }
  pango_layout_set_alignment(l, (align & Align::HCenter) ? PANGO_ALIGN_CENTER
                                : (align & Align::Right) ? PANGO_ALIGN_RIGHT
                                                         : PANGO_ALIGN_LEFT);
  PangoRectangle logical{};
  pango_layout_get_extents(l, nullptr, &logical);
  const float h = static_cast<float>(logical.height) / PANGO_SCALE;
  cache.emplace(std::move(key), Entry{l, h});
  return {l, h};
}

// ─────────────────────────────────────────────────────────────────────────────
// Images
// ─────────────────────────────────────────────────────────────────────────────
// The cache keeps a reference to the image, so a key (its pixel buffer) can
// not be reused by another image while the surface is cached.
cairo_surface_t* image_surface(const Image& img) {
  struct Entry {
    Image keep;
    cairo_surface_t* surface;
  };
  static std::unordered_map<const void*, Entry> cache;
  if (auto it = cache.find(img.cache_key()); it != cache.end()) return it->second.surface;
  if (cache.size() > 128) {
    for (auto& [k, e] : cache) cairo_surface_destroy(e.surface);
    cache.clear();
  }
  const int w = img.width(), h = img.height();
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return nullptr;
  }
  cairo_surface_flush(s);
  rgba_to_argb32(img.pixels(), w, h, cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s));
  cairo_surface_mark_dirty(s);
  cache.emplace(img.cache_key(), Entry{img, s});
  return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// CairoPainter
// ─────────────────────────────────────────────────────────────────────────────
CairoPainter::CairoPainter(cairo_t* cr) : cr_(cr) {
  cairo_save(cr_);
  // same defaults as Direct2D: flat caps, mitred joins, winding fill
  cairo_set_line_cap(cr_, CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join(cr_, CAIRO_LINE_JOIN_MITER);
  cairo_set_fill_rule(cr_, CAIRO_FILL_RULE_WINDING);
}

CairoPainter::~CairoPainter() {
  while (!opacity_stack_.empty()) restore();
  cairo_restore(cr_);
}

void CairoPainter::save() {
  cairo_save(cr_);
  opacity_stack_.push_back(opacity_);
}

void CairoPainter::restore() {
  if (opacity_stack_.empty()) return;
  cairo_restore(cr_);
  opacity_ = opacity_stack_.back();
  opacity_stack_.pop_back();
}

void CairoPainter::translate(float dx, float dy) { cairo_translate(cr_, dx, dy); }

void CairoPainter::clip(const Rect& r) {
  // axis aligned and not anti-aliased, like PushAxisAlignedClip(ALIASED)
  const cairo_antialias_t aa = cairo_get_antialias(cr_);
  cairo_set_antialias(cr_, CAIRO_ANTIALIAS_NONE);
  cairo_new_path(cr_);
  cairo_rectangle(cr_, r.x, r.y, std::max(0.f, r.w), std::max(0.f, r.h));
  cairo_clip(cr_);
  cairo_set_antialias(cr_, aa);
}

void CairoPainter::set_opacity(float a) { opacity_ *= std::clamp(a, 0.f, 1.f); }

void CairoPainter::source(Color c) { cairo_set_source_rgba(cr_, c.r, c.g, c.b, c.a * opacity_); }

void CairoPainter::rect_path(const Rect& r, float radius) {
  cairo_new_path(cr_);
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) {
    cairo_rectangle(cr_, r.x, r.y, r.w, r.h);
    return;
  }
  const double x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h, k = radius;
  cairo_new_sub_path(cr_);
  cairo_arc(cr_, x1 - k, y0 + k, k, -M_PI / 2, 0);
  cairo_arc(cr_, x1 - k, y1 - k, k, 0, M_PI / 2);
  cairo_arc(cr_, x0 + k, y1 - k, k, M_PI / 2, M_PI);
  cairo_arc(cr_, x0 + k, y0 + k, k, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr_);
}

void CairoPainter::ellipse_path(const Rect& r) {
  cairo_new_path(cr_);
  if (r.w <= 0 || r.h <= 0) return;
  cairo_save(cr_);
  cairo_translate(cr_, r.x + r.w / 2, r.y + r.h / 2);
  cairo_scale(cr_, r.w / 2, r.h / 2);
  cairo_arc(cr_, 0, 0, 1, 0, 2 * M_PI);
  cairo_restore(cr_); // the path keeps its device coordinates; the stroke width does not get scaled
}

void CairoPainter::points_path(const std::vector<Point>& pts, bool closed) {
  cairo_new_path(cr_);
  cairo_move_to(cr_, pts[0].x, pts[0].y);
  for (std::size_t i = 1; i < pts.size(); ++i) cairo_line_to(cr_, pts[i].x, pts[i].y);
  if (closed) cairo_close_path(cr_);
}

void CairoPainter::fill_rect(const Rect& r, Color c, float radius) {
  if (c.a <= 0 || r.w <= 0 || r.h <= 0) return;
  source(c);
  rect_path(r, radius);
  cairo_fill(cr_);
}

void CairoPainter::stroke_rect(const Rect& r, Color c, float width, float radius) {
  if (c.a <= 0 || width <= 0) return;
  source(c);
  cairo_set_line_width(cr_, width);
  rect_path(r.inset(width * 0.5f), radius);
  cairo_stroke(cr_);
}

void CairoPainter::fill_ellipse(const Rect& r, Color c) {
  if (c.a <= 0) return;
  source(c);
  ellipse_path(r);
  cairo_fill(cr_);
}

void CairoPainter::stroke_ellipse(const Rect& r, Color c, float width) {
  if (c.a <= 0 || width <= 0) return;
  source(c);
  cairo_set_line_width(cr_, width);
  ellipse_path(r.inset(width * 0.5f));
  cairo_stroke(cr_);
}

void CairoPainter::line(Point a, Point b, Color c, float width) {
  if (c.a <= 0 || width <= 0) return;
  source(c);
  cairo_set_line_width(cr_, width);
  cairo_new_path(cr_);
  cairo_move_to(cr_, a.x, a.y);
  cairo_line_to(cr_, b.x, b.y);
  cairo_stroke(cr_);
}

void CairoPainter::polyline(const std::vector<Point>& pts, Color c, float width, bool closed) {
  if (pts.size() < 2 || c.a <= 0 || width <= 0) return;
  source(c);
  cairo_set_line_width(cr_, width);
  points_path(pts, closed);
  cairo_stroke(cr_);
}

void CairoPainter::fill_polygon(const std::vector<Point>& pts, Color c) {
  if (pts.size() < 3 || c.a <= 0) return;
  source(c);
  points_path(pts, true);
  cairo_fill(cr_);
}

void CairoPainter::fill_path(const std::vector<std::vector<Point>>& contours, Color c, bool even_odd) {
  if (c.a <= 0) return;
  cairo_new_path(cr_);
  for (auto& pts : contours) {
    if (pts.size() < 3) continue;
    cairo_move_to(cr_, pts[0].x, pts[0].y);
    for (std::size_t i = 1; i < pts.size(); ++i) cairo_line_to(cr_, pts[i].x, pts[i].y);
    cairo_close_path(cr_);
  }
  source(c);
  cairo_set_fill_rule(cr_, even_odd ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING);
  cairo_fill(cr_);
  cairo_set_fill_rule(cr_, CAIRO_FILL_RULE_WINDING);
}

void CairoPainter::fill_gradient(const Rect& r, Color top, Color bottom, float radius) {
  if (r.w <= 0 || r.h <= 0) return;
  cairo_pattern_t* g = cairo_pattern_create_linear(r.x, r.y, r.x, r.y + r.h);
  cairo_pattern_add_color_stop_rgba(g, 0, top.r, top.g, top.b, top.a * opacity_);
  cairo_pattern_add_color_stop_rgba(g, 1, bottom.r, bottom.g, bottom.b, bottom.a * opacity_);
  cairo_set_source(cr_, g);
  rect_path(r, radius);
  cairo_fill(cr_);
  cairo_pattern_destroy(g);
}

void CairoPainter::text(const Rect& r, std::string_view s, const Font& f, Color c, int align, bool wrap) {
  if (s.empty() || c.a <= 0) return;
  const LayoutRef l = cached_layout(s, f, r.w, align, wrap);
  if (!l.layout) return;
  float y = r.y;
  if (align & Align::VCenter) y = r.y + (r.h - l.height) / 2;
  else if (align & Align::Bottom) y = r.y + r.h - l.height;
  cairo_save(cr_);
  // the layout box clips, like D2D1_DRAW_TEXT_OPTIONS_CLIP
  cairo_new_path(cr_);
  cairo_rectangle(cr_, r.x, r.y, std::max(0.f, r.w), std::max(0.f, r.h));
  cairo_clip(cr_);
  source(c);
  cairo_move_to(cr_, r.x, y);
  pango_cairo_show_layout(cr_, l.layout);
  cairo_restore(cr_);
}

void CairoPainter::image(const Image& img, const Rect& dst, float opacity) {
  if (!img.valid() || dst.w <= 0 || dst.h <= 0) return;
  cairo_surface_t* s = image_surface(img);
  if (!s) return;
  cairo_save(cr_);
  cairo_translate(cr_, dst.x, dst.y);
  cairo_scale(cr_, dst.w / img.width(), dst.h / img.height());
  cairo_set_source_surface(cr_, s, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr_), CAIRO_FILTER_GOOD);
  cairo_pattern_set_extend(cairo_get_source(cr_), CAIRO_EXTEND_PAD);
  cairo_new_path(cr_);
  cairo_rectangle(cr_, 0, 0, img.width(), img.height());
  cairo_clip(cr_);
  cairo_paint_with_alpha(cr_, std::clamp(opacity, 0.f, 1.f) * opacity_);
  cairo_restore(cr_);
}

} // namespace cairo

// ─────────────────────────────────────────────────────────────────────────────
// Platform functions that only need Cairo/Pango
// ─────────────────────────────────────────────────────────────────────────────
Size measure_text(std::string_view s, const Font& f, float max_width) {
  PangoLayout* l = cairo::make_layout(s.empty() ? std::string_view("Ag") : s, f);
  if (max_width > 0) {
    pango_layout_set_width(l, static_cast<int>(max_width * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
  }
  PangoRectangle logical{};
  pango_layout_get_extents(l, nullptr, &logical);
  g_object_unref(l);
  return {s.empty() ? 0.f : static_cast<float>(logical.width) / PANGO_SCALE,
          static_cast<float>(logical.height) / PANGO_SCALE};
}

float line_height(const Font& f) {
  // asked for on every layout pass by text views: cache per font
  static std::unordered_map<std::string, float> cache;
  std::string key = f.family + '\x1f' + std::to_string(static_cast<int>(f.size * 64)) + '\x1f' +
                    std::to_string(static_cast<int>(f.weight)) + (f.italic ? "i" : "");
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  const float h = platform::measure_text("Ag", f, 0).h;
  cache.emplace(std::move(key), h);
  return h;
}

bool decode_image(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba) {
#if PULSEUI_HAS_GDK_PIXBUF
  GError* err = nullptr;
  if (GdkPixbuf* pb = gdk_pixbuf_new_from_file(path.c_str(), &err)) {
    GdkPixbuf* src = gdk_pixbuf_apply_embedded_orientation(pb);
    g_object_unref(pb);
    w = gdk_pixbuf_get_width(src);
    h = gdk_pixbuf_get_height(src);
    const int n = gdk_pixbuf_get_n_channels(src), stride = gdk_pixbuf_get_rowstride(src);
    const bool alpha = gdk_pixbuf_get_has_alpha(src);
    const guchar* px = gdk_pixbuf_read_pixels(src);
    rgba.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
      const guchar* s = px + static_cast<std::ptrdiff_t>(y) * stride;
      std::uint8_t* d = rgba.data() + static_cast<std::size_t>(y) * w * 4;
      for (int x = 0; x < w; ++x, s += n, d += 4) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = alpha ? s[3] : 255;
      }
    }
    g_object_unref(src);
    return w > 0 && h > 0;
  }
  if (err) g_error_free(err);
#endif
  // PNG only, through Cairo
  cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return false;
  }
  cairo_surface_t* argb = s;
  if (cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32) {
    argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_image_surface_get_width(s),
                                      cairo_image_surface_get_height(s));
    cairo_t* cr = cairo_create(argb);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
  }
  cairo_surface_flush(argb);
  w = cairo_image_surface_get_width(argb);
  h = cairo_image_surface_get_height(argb);
  rgba.resize(static_cast<std::size_t>(w) * h * 4);
  cairo::argb32_to_rgba(cairo_image_surface_get_data(argb), w, h, cairo_image_surface_get_stride(argb), rgba.data());
  cairo_surface_destroy(argb);
  return w > 0 && h > 0;
}

bool encode_png(const std::string& path, int w, int h, const std::uint8_t* rgba) {
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return false;
  }
  cairo_surface_flush(s);
  cairo::rgba_to_argb32(rgba, w, h, cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s));
  cairo_surface_mark_dirty(s);
  const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
  cairo_surface_destroy(s);
  return ok;
}

bool render_offscreen(int w, int h, float scale, const std::function<void(Painter&)>& paint,
                      std::vector<std::uint8_t>& rgba) {
  const int pw = static_cast<int>(w * scale + 0.5f), ph = static_cast<int>(h * scale + 0.5f);
  if (pw <= 0 || ph <= 0) return false;
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return false;
  }
  cairo_t* cr = cairo_create(s);
  cairo_scale(cr, scale, scale);
  {
    cairo::CairoPainter p(cr);
    paint(p);
  }
  cairo_destroy(cr);
  cairo_surface_flush(s);
  rgba.resize(static_cast<std::size_t>(pw) * ph * 4);
  cairo::argb32_to_rgba(cairo_image_surface_get_data(s), pw, ph, cairo_image_surface_get_stride(s), rgba.data());
  cairo_surface_destroy(s);
  return true;
}

} // namespace pulseui::platform
