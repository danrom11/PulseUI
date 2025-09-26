#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#include <memory>
#include <string_view>

#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace {

class CanvasCG final : public pulseui::ui::Canvas {
public:
  CanvasCG(CGContextRef ctx, float dpi) : ctx_(ctx), dpi_(dpi) {}

  void clear(pulseui::ui::Color c) override {
    CGContextSaveGState(ctx_);
    CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
    CGRect r = CGContextGetClipBoundingBox(ctx_);
    CGContextFillRect(ctx_, r);
    CGContextRestoreGState(ctx_);
  }

  void fill_rect(pulseui::ui::Rect r, pulseui::ui::Color c) override {
    CGContextSaveGState(ctx_);
    CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
    CGContextFillRect(ctx_, CGRectMake(r.x, r.y, r.w, r.h));
    CGContextRestoreGState(ctx_);
  }

  void draw_text(pulseui::ui::Point p,
                 std::string_view text,
                 const pulseui::ui::Font& f,
                 pulseui::ui::Color c) override
  {
    @autoreleasepool {
      NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:f.size],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:c.r green:c.g blue:c.b alpha:c.a]
      };
      NSString* ns = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
      [ns drawAtPoint:NSMakePoint(p.x, p.y) withAttributes:attrs];
    }
  }

private:
  CGContextRef ctx_;
  float dpi_{1.f};
};

} // namespace

namespace pulseui::ui {
  std::unique_ptr<Canvas> make_canvas_from_context(CGContextRef ctx, float dpi_scale) {
    return std::make_unique<CanvasCG>(ctx, dpi_scale);
  }
}
