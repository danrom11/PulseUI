#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <memory>
#include <string>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace pulseui::platform {

static inline void set_fill(CGContextRef ctx, const pulseui::ui::Color& c) {
  CGContextSetRGBFillColor(ctx, c.r, c.g, c.b, c.a);
}

class CanvasCG final : public pulseui::ui::Canvas {
public:
  CanvasCG(CGContextRef ctx, float dpi_scale)
  : ctx_(ctx), dpi_(dpi_scale) {}

  void clear(pulseui::ui::Color c) override {
    CGRect r = CGContextGetClipBoundingBox(ctx_);
    set_fill(ctx_, c);
    CGContextFillRect(ctx_, r);
  }

  void fill_rect(pulseui::ui::Rect r, pulseui::ui::Color c) override {
    set_fill(ctx_, c);
    CGContextFillRect(ctx_, CGRectMake(r.x, r.y, r.w, r.h));
  }

  void draw_text(pulseui::ui::Point p,
                 std::string_view text,
                 const pulseui::ui::Font& f,
                 pulseui::ui::Color c) override
  {
    @autoreleasepool {
      NSString* ns = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
      if (!ns) return;

      NSColor* col = [NSColor colorWithCalibratedRed:c.r green:c.g blue:c.b alpha:c.a];
      NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:f.size],
        NSForegroundColorAttributeName: col
      };

      [ns drawAtPoint:NSMakePoint(p.x, p.y) withAttributes:attrs];
    }
  }

  float text_width(std::string_view text, const pulseui::ui::Font& f) const override {
    @autoreleasepool {
      NSString* ns = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
      if (!ns) return 0.f;
      NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:f.size],
      };
      NSSize sz = [ns sizeWithAttributes:attrs];
      return (float)sz.width;
    }
  }

  ui::Rect bounds() const override {
    CGRect r = CGContextGetClipBoundingBox(ctx_);
    return ui::Rect{ (float)r.origin.x, (float)r.origin.y,
                              (float)r.size.width, (float)r.size.height };
  }

private:
  CGContextRef ctx_{};
  float dpi_{1.f};
};

} // namespace pulseui::platform

namespace pulseui::ui {

std::unique_ptr<Canvas> make_canvas_from_context(CGContextRef ctx, float dpi_scale) {
  return std::make_unique<platform::CanvasCG>(ctx, dpi_scale);
}

} // namespace pulseui::ui
