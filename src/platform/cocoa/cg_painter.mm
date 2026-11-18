#include "cg_painter.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace pulseui::platform::cocoa {

namespace {

CGRect cgr(const Rect& r) { return CGRectMake(r.x, r.y, r.w, r.h); }

CGColorSpaceRef srgb() {
  static CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  return cs;
}

// GPU-less cache of CGImages created from pulseui::Image pixel buffers.
std::unordered_map<const void*, CGImageRef>& image_cache() {
  static std::unordered_map<const void*, CGImageRef> cache;
  return cache;
}

CGImageRef make_cgimage(const Image& img) {
  auto& cache = image_cache();
  auto it = cache.find(img.cache_key());
  if (it != cache.end()) return it->second;
  if (cache.size() > 128) {
    for (auto& [k, v] : cache) CGImageRelease(v);
    cache.clear();
  }
  const int w = img.width(), h = img.height();
  CFDataRef data = CFDataCreate(nullptr, img.pixels(), static_cast<CFIndex>(w) * h * 4);
  CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
  CGImageRef cg = CGImageCreate(w, h, 8, 32, w * 4, srgb(),
                                static_cast<CGBitmapInfo>(kCGBitmapByteOrderDefault) | static_cast<CGBitmapInfo>(kCGImageAlphaLast), prov, nullptr, true,
                                kCGRenderingIntentDefault);
  CGDataProviderRelease(prov);
  CFRelease(data);
  cache.emplace(img.cache_key(), cg);
  return cg;
}

} // namespace

namespace {

NSFont* make_font(const Font& f) {
  // AppKit returns nil for invalid sizes; never pass one
  const CGFloat size = (std::isfinite(f.size) && f.size > 0.5f) ? f.size : 13.0;
  NSFontWeight w = NSFontWeightRegular;
  switch (f.weight) {
    case FontWeight::Light: w = NSFontWeightLight; break;
    case FontWeight::Medium: w = NSFontWeightMedium; break;
    case FontWeight::SemiBold: w = NSFontWeightSemibold; break;
    case FontWeight::Bold: w = NSFontWeightBold; break;
    default: break;
  }
  NSFont* font = nil;
  if (f.family.empty()) {
    font = [NSFont systemFontOfSize:size weight:w];
  } else if (f.family == "monospace") {
    // several fallbacks: the result is put into NSDictionary literals, a nil there throws
    if ([NSFont respondsToSelector:@selector(monospacedSystemFontOfSize:weight:)])
      font = [NSFont monospacedSystemFontOfSize:size weight:w];
    if (!font) font = [NSFont userFixedPitchFontOfSize:size];
    if (!font) font = [NSFont fontWithName:@"Menlo" size:size];
  } else {
    NSString* name = [NSString stringWithUTF8String:f.family.c_str()];
    if (name) font = [NSFont fontWithName:name size:size];
  }
  if (!font) font = [NSFont systemFontOfSize:size weight:w];
  if (!font) font = [NSFont systemFontOfSize:size];
  if (f.italic && font) {
    NSFont* it = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSItalicFontMask];
    if (it) font = it;
  }
  return font;
}

} // namespace

NSFont* ns_font(const Font& f) {
  // fonts are requested for every text draw and measurement: cache them
  static NSMutableDictionary<NSString*, NSFont*>* cache = [NSMutableDictionary dictionary];
  NSString* key = [NSString stringWithFormat:@"%s|%.2f|%d|%d", f.family.c_str(), static_cast<double>(f.size),
                                             static_cast<int>(f.weight), f.italic ? 1 : 0];
  NSFont* font = cache[key];
  if (!font) {
    font = make_font(f);
    if (font) cache[key] = font;
  }
  return font;
}

NSDictionary* text_attributes(const Font& f, Color c, int align, bool wrap) {
  NSMutableParagraphStyle* ps = [[NSMutableParagraphStyle alloc] init];
  ps.alignment = (align & Align::HCenter) ? NSTextAlignmentCenter
                 : (align & Align::Right) ? NSTextAlignmentRight
                                          : NSTextAlignmentLeft;
  ps.lineBreakMode = wrap ? NSLineBreakByWordWrapping : NSLineBreakByTruncatingTail;
  // built with setters: a missing value is skipped instead of throwing
  NSMutableDictionary* attrs = [NSMutableDictionary dictionaryWithCapacity:3];
  if (NSFont* font = ns_font(f)) attrs[NSFontAttributeName] = font;
  if (NSColor* color = [NSColor colorWithSRGBRed:c.r green:c.g blue:c.b alpha:c.a]) attrs[NSForegroundColorAttributeName] = color;
  attrs[NSParagraphStyleAttributeName] = ps;
  return attrs;
}

CGPainter::CGPainter(CGContextRef ctx) : ctx_(ctx) {
  CGContextSaveGState(ctx_);
  CGContextSetLineJoin(ctx_, kCGLineJoinRound);
  CGContextSetLineCap(ctx_, kCGLineCapRound);
}

CGPainter::~CGPainter() {
  while (depth_ > 0) restore();
  CGContextRestoreGState(ctx_);
}

void CGPainter::save() {
  CGContextSaveGState(ctx_);
  opacity_stack_.push_back(opacity_);
  ++depth_;
}

void CGPainter::restore() {
  if (depth_ == 0) return;
  CGContextRestoreGState(ctx_);
  opacity_ = opacity_stack_.back();
  opacity_stack_.pop_back();
  --depth_;
}

void CGPainter::translate(float dx, float dy) { CGContextTranslateCTM(ctx_, dx, dy); }
void CGPainter::clip(const Rect& r) { CGContextClipToRect(ctx_, cgr(r)); }

void CGPainter::set_opacity(float a) {
  opacity_ *= std::clamp(a, 0.f, 1.f);
  CGContextSetAlpha(ctx_, opacity_);
}

void CGPainter::add_rect_path(const Rect& r, float radius) {
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) {
    CGContextAddRect(ctx_, cgr(r));
    return;
  }
  CGPathRef p = CGPathCreateWithRoundedRect(cgr(r), radius, radius, nullptr);
  CGContextAddPath(ctx_, p);
  CGPathRelease(p);
}

void CGPainter::fill_rect(const Rect& r, Color c, float radius) {
  if (c.a <= 0 || r.w <= 0 || r.h <= 0) return;
  CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextBeginPath(ctx_);
  add_rect_path(r, radius);
  CGContextFillPath(ctx_);
}

void CGPainter::stroke_rect(const Rect& r, Color c, float width, float radius) {
  if (c.a <= 0 || width <= 0) return;
  CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextSetLineWidth(ctx_, width);
  CGContextBeginPath(ctx_);
  add_rect_path(r.inset(width / 2), std::max(0.f, radius - width / 2));
  CGContextStrokePath(ctx_);
}

void CGPainter::fill_ellipse(const Rect& r, Color c) {
  if (c.a <= 0) return;
  CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextFillEllipseInRect(ctx_, cgr(r));
}

void CGPainter::stroke_ellipse(const Rect& r, Color c, float width) {
  if (c.a <= 0) return;
  CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextSetLineWidth(ctx_, width);
  CGContextStrokeEllipseInRect(ctx_, cgr(r.inset(width / 2)));
}

void CGPainter::line(Point a, Point b, Color c, float width) {
  CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextSetLineWidth(ctx_, width);
  CGContextBeginPath(ctx_);
  CGContextMoveToPoint(ctx_, a.x, a.y);
  CGContextAddLineToPoint(ctx_, b.x, b.y);
  CGContextStrokePath(ctx_);
}

void CGPainter::polyline(const std::vector<Point>& pts, Color c, float width, bool closed) {
  if (pts.size() < 2) return;
  CGContextSetRGBStrokeColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextSetLineWidth(ctx_, width);
  CGContextBeginPath(ctx_);
  CGContextMoveToPoint(ctx_, pts[0].x, pts[0].y);
  for (std::size_t i = 1; i < pts.size(); ++i) CGContextAddLineToPoint(ctx_, pts[i].x, pts[i].y);
  if (closed) CGContextClosePath(ctx_);
  CGContextStrokePath(ctx_);
}

void CGPainter::fill_polygon(const std::vector<Point>& pts, Color c) {
  if (pts.size() < 3) return;
  CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
  CGContextBeginPath(ctx_);
  CGContextMoveToPoint(ctx_, pts[0].x, pts[0].y);
  for (std::size_t i = 1; i < pts.size(); ++i) CGContextAddLineToPoint(ctx_, pts[i].x, pts[i].y);
  CGContextClosePath(ctx_);
  CGContextFillPath(ctx_);
}

void CGPainter::fill_gradient(const Rect& r, Color top, Color bottom, float radius) {
  const CGFloat comps[8] = {top.r, top.g, top.b, top.a, bottom.r, bottom.g, bottom.b, bottom.a};
  const CGFloat locs[2] = {0, 1};
  CGGradientRef g = CGGradientCreateWithColorComponents(srgb(), comps, locs, 2);
  CGContextSaveGState(ctx_);
  CGContextBeginPath(ctx_);
  add_rect_path(r, radius);
  CGContextClip(ctx_);
  CGContextDrawLinearGradient(ctx_, g, CGPointMake(r.x, r.y), CGPointMake(r.x, r.y + r.h), 0);
  CGContextRestoreGState(ctx_);
  CGGradientRelease(g);
}

void CGPainter::text(const Rect& r, std::string_view s, const Font& f, Color c, int align, bool wrap) {
  if (s.empty() || c.a <= 0) return;
  @autoreleasepool {
    NSString* str = [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
    if (!str) return;
    NSDictionary* attrs = text_attributes(f, c, align, wrap);
    const NSStringDrawingOptions opts = NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine;
    NSRect bounds = [str boundingRectWithSize:NSMakeSize(r.w, wrap ? r.h : CGFLOAT_MAX) options:opts attributes:attrs];
    const CGFloat h = std::min<CGFloat>(std::ceil(bounds.size.height), r.h);
    CGFloat y = r.y;
    if (align & Align::VCenter) y = r.y + (r.h - h) / 2;
    else if (align & Align::Bottom) y = r.y + r.h - h;
    NSGraphicsContext* prev = [NSGraphicsContext currentContext];
    NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithCGContext:ctx_ flipped:YES];
    [NSGraphicsContext setCurrentContext:gc];
    [str drawWithRect:NSMakeRect(r.x, y, r.w, wrap ? r.h - (y - r.y) : h) options:opts attributes:attrs context:nil];
    [NSGraphicsContext setCurrentContext:prev];
  }
}

void CGPainter::image(const Image& img, const Rect& dst, float opacity) {
  if (!img.valid()) return;
  CGImageRef cg = make_cgimage(img);
  CGContextSaveGState(ctx_);
  CGContextSetAlpha(ctx_, opacity * opacity_);
  CGContextTranslateCTM(ctx_, dst.x, dst.y + dst.h);
  CGContextScaleCTM(ctx_, 1, -1);
  CGContextSetInterpolationQuality(ctx_, kCGInterpolationHigh);
  CGContextDrawImage(ctx_, CGRectMake(0, 0, dst.w, dst.h), cg);
  CGContextRestoreGState(ctx_);
}

} // namespace pulseui::platform::cocoa
