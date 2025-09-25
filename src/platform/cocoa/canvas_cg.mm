#import <AppKit/AppKit.h>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::platform {

  class CGCanvas final : public pulseui::ui::Canvas {
    CGContextRef ctx_{};
    float scale_{1.f};

  public:
    CGCanvas(CGContextRef ctx, float scale) : ctx_(ctx), scale_(scale) {}

    void clear(pulseui::ui::Color c) override {
      CGContextSaveGState(ctx_);
      CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
      CGRect bounds = CGContextGetClipBoundingBox(ctx_);
      CGContextFillRect(ctx_, bounds);
      CGContextRestoreGState(ctx_);
    }

    void fill_rect(pulseui::ui::Rect r, pulseui::ui::Color c) override {
      CGContextSaveGState(ctx_);
      CGContextSetRGBFillColor(ctx_, c.r, c.g, c.b, c.a);
      CGRect rr = CGRectMake(r.x*scale_, r.y*scale_, r.w*scale_, r.h*scale_);
      CGContextFillRect(ctx_, rr);
      CGContextRestoreGState(ctx_);
    }

    void draw_text(pulseui::ui::Point p,
                   std::string_view text,
                   const pulseui::ui::Font& f,
                   pulseui::ui::Color c) override {
      NSString* ns = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
      NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:f.size],
        NSForegroundColorAttributeName: [NSColor colorWithCalibratedRed:c.r green:c.g blue:c.b alpha:c.a]
      };
      [ns drawAtPoint:NSMakePoint(p.x*scale_, p.y*scale_) withAttributes:attrs];
    }
  };

  std::unique_ptr<pulseui::ui::Canvas> make_cg_canvas(CGContextRef ctx, float scale) {
    return std::make_unique<CGCanvas>(ctx, scale);
  }
}
