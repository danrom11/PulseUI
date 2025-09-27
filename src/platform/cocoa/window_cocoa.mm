#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <functional>
#include <memory>
#include <string>

#include <pulseui/ui/window.hpp>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::ui {
  std::unique_ptr<Canvas> make_canvas_from_context(CGContextRef, float dpi_scale);
}

using pulseui::ui::Canvas;
using pulseui::ui::InputEvent;

@interface PulseView : NSView {
@public
  std::function<void(Canvas&)>*               paintCB;
  std::function<void(const InputEvent&)>*     inputCB;
  float                                       dpiScale;
}
- (void)emitMouse:(InputEvent::Type)type fromEvent:(NSEvent*)e;
@end

@implementation PulseView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)resignFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }

- (instancetype)initWithFrame:(NSRect)frameRect {
  if (self = [super initWithFrame:frameRect]) {
    paintCB = nullptr;
    inputCB = nullptr;
    self.wantsLayer = YES;
    if (!self.layer) self.layer = [CALayer layer];
    CGFloat s = NSScreen.mainScreen.backingScaleFactor; if (s <= 0) s = 1.0;
    self.layer.contentsScale = s; dpiScale = (float)s;

    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved|NSTrackingActiveAlways|NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
  }
  return self;
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  if (!paintCB) return;
  CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
  auto canvas = pulseui::ui::make_canvas_from_context(ctx, dpiScale);
  (*paintCB)(*canvas);
}

- (void)emitMouse:(InputEvent::Type)type fromEvent:(NSEvent*)e {
  if (!inputCB) return;
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  InputEvent ev{}; ev.type = type; ev.pos = {(float)p.x, (float)p.y};
  (*inputCB)(ev);
}

#pragma mark - Mouse

- (void)mouseDown:(NSEvent *)e {
  [[self window] makeFirstResponder:self];
  [self emitMouse:InputEvent::MouseDown fromEvent:e];
}
- (void)mouseUp:(NSEvent *)e      { [self emitMouse:InputEvent::MouseUp   fromEvent:e]; }
- (void)mouseMoved:(NSEvent *)e   { [self emitMouse:InputEvent::MouseMove fromEvent:e]; }
- (void)mouseDragged:(NSEvent *)e { [self emitMouse:InputEvent::MouseMove fromEvent:e]; }

#pragma mark - Keyboard

- (void)keyDown:(NSEvent *)event {
  [self interpretKeyEvents:@[event]];
}

- (void)insertText:(id)string {
  if (!inputCB) return;
  NSString *ns = [string isKindOfClass:[NSAttributedString class]] ? [(NSAttributedString*)string string] : (NSString*)string;
  if (!ns) return;

  const char *utf8 = [ns UTF8String];
  if (!utf8) return;

  InputEvent ev{};
  ev.type = InputEvent::KeyDown;
  ev.keycode = 0;
  ev.text = std::string(utf8);
  ev.pos = {0.f, 0.f};
  (*inputCB)(ev);
}

- (void)insertText:(id)string replacementRange:(NSRange)range {
  (void)range;
  [self insertText:string];
}

- (void)doCommandBySelector:(SEL)sel {
  if (!inputCB) return;

  auto sendKey = [&](int keycode){
    InputEvent ev{};
    ev.type = InputEvent::KeyDown;
    ev.keycode = keycode;
    ev.text.clear();
    (*inputCB)(ev);
  };

  if (sel == @selector(deleteBackward:)) {
    sendKey(8);                      // macOS backspace FIXME code
    return;
  }

  // Delete Forward (Fn+Backspace)
  if (sel == @selector(deleteForward:) ||
      sel == @selector(deleteWordForward:)) {
    sendKey(117);                    // macOS DeleteForward
    return;
  }

  if (sel == @selector(moveLeft:) ||
      sel == @selector(moveWordLeft:) ||
      sel == @selector(moveBackward:) ||
      sel == @selector(moveLeftAndModifySelection:) ||
      sel == @selector(moveWordLeftAndModifySelection:)) {
    sendKey(123);                    // macOS Left
    return;
  }

  if (sel == @selector(moveRight:) ||
      sel == @selector(moveWordRight:) ||
      sel == @selector(moveForward:) ||
      sel == @selector(moveRightAndModifySelection:) ||
      sel == @selector(moveWordRightAndModifySelection:)) {
    sendKey(124);                    // macOS Right
    return;
  }

  if (sel == @selector(moveToBeginningOfLine:) ||
      sel == @selector(moveToBeginningOfParagraph:) ||
      sel == @selector(moveToBeginningOfDocument:)) {
    sendKey(115);                    // macOS Home
    return;
  }

  if (sel == @selector(moveToEndOfLine:) ||
      sel == @selector(moveToEndOfParagraph:) ||
      sel == @selector(moveToEndOfDocument:)) {
    sendKey(119);                    // macOS End
    return;
  }

  if (sel == @selector(insertNewline:) ||
      sel == @selector(insertLineBreak:) ||
      sel == @selector(insertParagraphSeparator:)) {
    return;
  }

  [super doCommandBySelector:sel];
}

@end

namespace pulseui::ui {

class CocoaWindow final : public Window {
public:
  CocoaWindow(int width, int height, std::string title) {
    @autoreleasepool {
      NSRect frame = NSMakeRect(0, 0, width, height);
      window_ = [[NSWindow alloc] initWithContentRect:frame
                                            styleMask:(NSWindowStyleMaskTitled|
                                                       NSWindowStyleMaskClosable|
                                                       NSWindowStyleMaskResizable|
                                                       NSWindowStyleMaskMiniaturizable)
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
      [window_ center];
      [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];

      view_ = [[PulseView alloc] initWithFrame:frame];
      [window_ setContentView:view_];
      [window_ makeKeyAndOrderFront:nil];
      [window_ makeFirstResponder:view_];

      paint_holder_ = std::make_unique<std::function<void(Canvas&)>>();
      input_holder_ = std::make_unique<std::function<void(const InputEvent&)>>();
      view_->paintCB = paint_holder_.get();
      view_->inputCB = input_holder_.get();

      *paint_holder_ = [](Canvas&){};
      *input_holder_ = [](const InputEvent&){};
    }
  }

  ~CocoaWindow() override {
    @autoreleasepool {
      if (view_) { view_->paintCB = nullptr; view_->inputCB = nullptr; }
      if (window_) { [window_ orderOut:nil]; [window_ close]; }
      view_ = nil; window_ = nil;
    }
  }

  void set_title(std::string t) override {
    @autoreleasepool { [window_ setTitle:[NSString stringWithUTF8String:t.c_str()]]; }
  }

  void invalidate() override {
    @autoreleasepool { [view_ setNeedsDisplay:YES]; }
  }

  float dpi_scale() const override {
    @autoreleasepool {
      CGFloat s = window_.screen.backingScaleFactor; if (s <= 0) s = 1.0;
      return (float)s;
    }
  }

  void on_paint(PaintCB cb) override { *paint_holder_ = std::move(cb); [view_ setNeedsDisplay:YES]; }
  void on_input(InputCB cb) override { *input_holder_ = std::move(cb); }

private:
  NSWindow*  window_{nil};
  PulseView* view_{nil};
  std::unique_ptr<std::function<void(Canvas&)>>           paint_holder_;
  std::unique_ptr<std::function<void(const InputEvent&)>> input_holder_;
};

} // namespace pulseui::ui

namespace pulseui::platform {
  std::unique_ptr<pulseui::ui::Window>
  make_cocoa_window(int w, int h, const std::string& title) {
    return std::make_unique<pulseui::ui::CocoaWindow>(w, h, title);
  }
}
