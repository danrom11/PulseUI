#import <AppKit/AppKit.h>
#include <memory>
#include <functional>
#include <pulseui/ui/window.hpp>
#include <pulseui/ui/input.hpp>

// -------------------- Objective-C part (global scope) --------------------
@interface PulseUIView : NSView
@property(nonatomic, copy) void (^paintBlock)(NSView* view, CGContextRef ctx, CGFloat scale);
@property(nonatomic, copy) void (^inputBlock)(NSEvent* ev, NSView* view);
@end

@implementation PulseUIView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [self.window makeFirstResponder:self];
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  if (self.paintBlock) {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    NSScreen* screen = self.window.screen ?: NSScreen.mainScreen;
    CGFloat scale = screen ? screen.backingScaleFactor : 1.0;
    self.paintBlock(self, ctx, scale);
  }
}
- (void)mouseDown:(NSEvent *)event        { if (self.inputBlock) self.inputBlock(event, self); }
- (void)mouseUp:(NSEvent *)event          { if (self.inputBlock) self.inputBlock(event, self); }
- (void)mouseMoved:(NSEvent *)event       { if (self.inputBlock) self.inputBlock(event, self); }
- (void)mouseDragged:(NSEvent *)event     { if (self.inputBlock) self.inputBlock(event, self); }
- (void)rightMouseDown:(NSEvent *)event   { if (self.inputBlock) self.inputBlock(event, self); }
- (void)rightMouseUp:(NSEvent *)event     { if (self.inputBlock) self.inputBlock(event, self); }
@end

// -------------------- C++ part --------------------
namespace pulseui::platform {
  std::unique_ptr<pulseui::ui::Canvas> make_cg_canvas(CGContextRef ctx, float scale);

  class CocoaWindow final : public pulseui::ui::Window {
    NSWindow* win_{nullptr};
    PulseUIView* view_{nullptr};
    pulseui::ui::Window::PaintCB paint_;
    pulseui::ui::Window::InputCB input_;

  public:
    CocoaWindow(int width, int height, const std::string& title) {
      NSRect rect = NSMakeRect(0, 0, width, height);
      NSUInteger style = NSWindowStyleMaskTitled
                       | NSWindowStyleMaskClosable
                       | NSWindowStyleMaskResizable
                       | NSWindowStyleMaskMiniaturizable;

      win_ = [[NSWindow alloc] initWithContentRect:rect
                                         styleMask:style
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
      [win_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
      [win_ center];
      [win_ setAcceptsMouseMovedEvents:YES];

      view_ = [[PulseUIView alloc] initWithFrame:rect];
      [win_ setContentView:view_];
      [win_ makeKeyAndOrderFront:nil];
      [win_ makeFirstResponder:view_];

      __weak PulseUIView* weakView = view_; // ARC

      view_.paintBlock = ^(NSView* v, CGContextRef ctx, CGFloat scale) {
        if (!paint_) return;
        auto canvas = make_cg_canvas(ctx, scale);
        paint_(*canvas);
      };

      view_.inputBlock = ^(NSEvent* ev, NSView* v) {
        if (!input_) return;
        pulseui::ui::InputEvent e{};
        switch (ev.type) {
          case NSEventTypeLeftMouseDown:  e.type = pulseui::ui::InputEvent::MouseDown; break;
          case NSEventTypeLeftMouseUp:    e.type = pulseui::ui::InputEvent::MouseUp;   break;
          case NSEventTypeRightMouseDown: e.type = pulseui::ui::InputEvent::MouseDown; break;
          case NSEventTypeRightMouseUp:   e.type = pulseui::ui::InputEvent::MouseUp;   break;
          case NSEventTypeMouseMoved:
          case NSEventTypeLeftMouseDragged:
          case NSEventTypeRightMouseDragged:
            e.type = pulseui::ui::InputEvent::MouseMove; break;
          default: return;
        }
        NSPoint p = [weakView convertPoint:ev.locationInWindow fromView:nil];
        e.pos = { (float)p.x, (float)p.y };
        input_(e);
      };
    }

    ~CocoaWindow() override = default; // ARC manages memory

    void set_title(std::string t) override {
      [win_ setTitle:[NSString stringWithUTF8String:t.c_str()]];
    }
    void invalidate() override { [view_ setNeedsDisplay:YES]; }
    float dpi_scale() const override {
      NSScreen* s = win_.screen ?: NSScreen.mainScreen;
      return s ? (float)s.backingScaleFactor : 1.0f;
    }
    void on_paint(PaintCB cb) override { paint_ = std::move(cb); [view_ setNeedsDisplay:YES]; }
    void on_input(InputCB cb) override { input_ = std::move(cb); }
  };

  std::unique_ptr<pulseui::ui::Window>
  make_cocoa_window(int width, int height, const std::string& title) {
    return std::make_unique<CocoaWindow>(width, height, title);
  }
} // namespace pulseui::platform
