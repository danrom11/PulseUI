#import <Cocoa/Cocoa.h>

#include <pulseui/ui/window.hpp>

#include "../platform.hpp"
#include "cg_painter.hpp"

using pulseui::InputEvent;
using pulseui::Key;
namespace Mod = pulseui::Mod;

namespace {

unsigned mods_of(NSEventModifierFlags f) {
  unsigned m = 0;
  if (f & NSEventModifierFlagShift) m |= Mod::Shift;
  if (f & NSEventModifierFlagControl) m |= Mod::Ctrl;
  if (f & NSEventModifierFlagOption) m |= Mod::Alt;
  if (f & NSEventModifierFlagCommand) m |= Mod::Super;
  return m;
}

Key map_key(unsigned short code) {
  switch (code) {
    case 0x00: return Key::A; case 0x0B: return Key::B; case 0x08: return Key::C; case 0x02: return Key::D;
    case 0x0E: return Key::E; case 0x03: return Key::F; case 0x05: return Key::G; case 0x04: return Key::H;
    case 0x22: return Key::I; case 0x26: return Key::J; case 0x28: return Key::K; case 0x25: return Key::L;
    case 0x2E: return Key::M; case 0x2D: return Key::N; case 0x1F: return Key::O; case 0x23: return Key::P;
    case 0x0C: return Key::Q; case 0x0F: return Key::R; case 0x01: return Key::S; case 0x11: return Key::T;
    case 0x20: return Key::U; case 0x09: return Key::V; case 0x0D: return Key::W; case 0x07: return Key::X;
    case 0x10: return Key::Y; case 0x06: return Key::Z;
    case 0x1D: return Key::Num0; case 0x12: return Key::Num1; case 0x13: return Key::Num2; case 0x14: return Key::Num3;
    case 0x15: return Key::Num4; case 0x17: return Key::Num5; case 0x16: return Key::Num6; case 0x1A: return Key::Num7;
    case 0x1C: return Key::Num8; case 0x19: return Key::Num9;
    case 0x7A: return Key::F1; case 0x78: return Key::F2; case 0x63: return Key::F3; case 0x76: return Key::F4;
    case 0x60: return Key::F5; case 0x61: return Key::F6; case 0x62: return Key::F7; case 0x64: return Key::F8;
    case 0x65: return Key::F9; case 0x6D: return Key::F10; case 0x67: return Key::F11; case 0x6F: return Key::F12;
    case 0x35: return Key::Escape; case 0x30: return Key::Tab; case 0x33: return Key::Backspace;
    case 0x24: case 0x4C: return Key::Enter; case 0x31: return Key::Space; case 0x75: return Key::Delete;
    case 0x73: return Key::Home; case 0x77: return Key::End; case 0x74: return Key::PageUp; case 0x79: return Key::PageDown;
    case 0x7B: return Key::Left; case 0x7C: return Key::Right; case 0x7D: return Key::Down; case 0x7E: return Key::Up;
    case 0x38: case 0x3C: return Key::Shift; case 0x3B: case 0x3E: return Key::Control;
    case 0x3A: case 0x3D: return Key::Alt; case 0x37: case 0x36: return Key::Super;
    case 0x2C: return Key::Slash;
    default: return Key::Unknown;
  }
}

NSCursor* ns_cursor(pulseui::Cursor c) {
  switch (c) {
    case pulseui::Cursor::IBeam: return [NSCursor IBeamCursor];
    case pulseui::Cursor::Hand: return [NSCursor pointingHandCursor];
    case pulseui::Cursor::Crosshair: return [NSCursor crosshairCursor];
    case pulseui::Cursor::SizeWE: return [NSCursor resizeLeftRightCursor];
    case pulseui::Cursor::SizeNS: return [NSCursor resizeUpDownCursor];
    case pulseui::Cursor::SizeAll: return [NSCursor openHandCursor];
    default: return [NSCursor arrowCursor];
  }
}

} // namespace

@interface PulseView : NSView <NSWindowDelegate, NSDraggingSource>
@property(nonatomic) pulseui::Window* owner;
@property(nonatomic) pulseui::Cursor cursorKind;
@property(nonatomic, strong) NSMenu* menuBar; // shown while this window is key
@property(nonatomic, strong) NSEvent* lastMouseEvent; // starts system drags
@end

namespace {
pulseui::DragData drag_data(id<NSDraggingInfo> info) {
  pulseui::DragData d;
  NSPasteboard* pb = [info draggingPasteboard];
  NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ [NSURL class] ]
                                            options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  for (NSURL* u in urls)
    if (u.path) d.files.push_back(u.path.UTF8String);
  if (NSString* s = [pb stringForType:NSPasteboardTypeString]) d.text = s.UTF8String;
  return d;
}
} // namespace

@implementation PulseView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)e { return YES; }

- (instancetype)initWithFrame:(NSRect)r {
  if ((self = [super initWithFrame:r])) {
    NSTrackingArea* ta = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:ta];
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
  }
  return self;
}

// ── drop target ──
- (NSDragOperation)dragPhase:(pulseui::Window::DragPhase)phase info:(id<NSDraggingInfo>)info {
  if (!self.owner) return NSDragOperationNone;
  NSPoint p = [self convertPoint:[info draggingLocation] fromView:nil];
  const bool ok = self.owner->handle_drag(phase, {static_cast<float>(p.x), static_cast<float>(p.y)}, drag_data(info));
  return ok ? NSDragOperationCopy : NSDragOperationNone;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
  return [self dragPhase:pulseui::Window::DragPhase::Enter info:info];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info {
  return [self dragPhase:pulseui::Window::DragPhase::Over info:info];
}
- (void)draggingExited:(id<NSDraggingInfo>)info {
  if (self.owner) self.owner->handle_drag(pulseui::Window::DragPhase::Leave, {}, {});
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
  return [self dragPhase:pulseui::Window::DragPhase::Drop info:info] != NSDragOperationNone;
}

// ── drag source ──
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return NSDragOperationCopy;
}

- (void)drawRect:(NSRect)dirty {
  if (!self.owner) return;
  CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
  pulseui::platform::cocoa::CGPainter p(ctx);
  self.owner->handle_paint(p);
}

- (void)resetCursorRects {
  [self addCursorRect:self.bounds cursor:ns_cursor(self.cursorKind)];
}

- (void)emitMouse:(InputEvent::Type)type event:(NSEvent*)e button:(pulseui::MouseButton)b {
  if (!self.owner) return;
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  InputEvent ev;
  ev.type = type;
  ev.pos = ev.window_pos = {static_cast<float>(p.x), static_cast<float>(p.y)};
  ev.button = b;
  ev.mods = mods_of(e.modifierFlags);
  if (type == InputEvent::MouseDown) ev.clicks = static_cast<int>(e.clickCount);
  self.owner->handle_input(ev);
}

- (void)mouseDown:(NSEvent*)e {
  self.lastMouseEvent = e;
  [self emitMouse:InputEvent::MouseDown event:e button:pulseui::MouseButton::Left];
}
- (void)mouseUp:(NSEvent*)e { [self emitMouse:InputEvent::MouseUp event:e button:pulseui::MouseButton::Left]; }
- (void)mouseDragged:(NSEvent*)e {
  self.lastMouseEvent = e;
  [self emitMouse:InputEvent::MouseMove event:e button:pulseui::MouseButton::None];
}
- (void)mouseMoved:(NSEvent*)e { [self emitMouse:InputEvent::MouseMove event:e button:pulseui::MouseButton::None]; }
- (void)rightMouseDown:(NSEvent*)e { [self emitMouse:InputEvent::MouseDown event:e button:pulseui::MouseButton::Right]; }
- (void)rightMouseUp:(NSEvent*)e { [self emitMouse:InputEvent::MouseUp event:e button:pulseui::MouseButton::Right]; }
- (void)rightMouseDragged:(NSEvent*)e { [self emitMouse:InputEvent::MouseMove event:e button:pulseui::MouseButton::None]; }
- (void)otherMouseDown:(NSEvent*)e { [self emitMouse:InputEvent::MouseDown event:e button:pulseui::MouseButton::Middle]; }
- (void)otherMouseUp:(NSEvent*)e { [self emitMouse:InputEvent::MouseUp event:e button:pulseui::MouseButton::Middle]; }

- (void)mouseExited:(NSEvent*)e {
  if (!self.owner) return;
  InputEvent ev;
  ev.type = InputEvent::MouseLeave;
  self.owner->handle_input(ev);
}

- (void)scrollWheel:(NSEvent*)e {
  if (!self.owner) return;
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  InputEvent ev;
  ev.type = InputEvent::Wheel;
  ev.pos = ev.window_pos = {static_cast<float>(p.x), static_cast<float>(p.y)};
  const CGFloat k = e.hasPreciseScrollingDeltas ? 1.0 / 40.0 : 1.0;
  ev.wheel_dx = static_cast<float>(e.scrollingDeltaX * k);
  ev.wheel_dy = static_cast<float>(e.scrollingDeltaY * k);
  ev.mods = mods_of(e.modifierFlags);
  self.owner->handle_input(ev);
}

- (void)keyDown:(NSEvent*)e {
  if (!self.owner) return;
  InputEvent ev;
  ev.type = InputEvent::KeyDown;
  ev.key = map_key(e.keyCode);
  ev.mods = mods_of(e.modifierFlags);
  ev.repeat = e.isARepeat;
  self.owner->handle_input(ev);
  if (!(e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)))
    [self interpretKeyEvents:@[ e ]]; // -> insertText:
}

- (void)keyUp:(NSEvent*)e {
  if (!self.owner) return;
  InputEvent ev;
  ev.type = InputEvent::KeyUp;
  ev.key = map_key(e.keyCode);
  ev.mods = mods_of(e.modifierFlags);
  self.owner->handle_input(ev);
}

- (void)insertText:(id)string {
  if (!self.owner) return;
  NSString* s = [string isKindOfClass:[NSAttributedString class]] ? [string string] : string;
  if (s.length == 0) return;
  unichar c = [s characterAtIndex:0];
  if (c < 0x20 || c == 0x7F || (c >= 0xF700 && c <= 0xF8FF)) return; // control / function keys
  InputEvent ev;
  ev.type = InputEvent::Text;
  ev.text = s.UTF8String;
  self.owner->handle_input(ev);
}

- (void)doCommandBySelector:(SEL)sel {
  // handled through KeyDown events; swallow to avoid the system beep
}

// accessibility: the view is a container of PulseUI elements
- (BOOL)isAccessibilityElement { return NO; }
- (NSAccessibilityRole)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSArray*)accessibilityChildren { return pulseui::platform::cocoa::a11y_window_children(self, self.owner); }
- (id)accessibilityHitTest:(NSPoint)p { return pulseui::platform::cocoa::a11y_hit_test(self, self.owner, p); }
- (id)accessibilityFocusedUIElement { return pulseui::platform::cocoa::a11y_focused(self, self.owner); }

// NSWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)w { return !self.owner || self.owner->handle_close_request(); }
- (void)windowWillClose:(NSNotification*)n {
  pulseui::Window* o = self.owner;
  self.owner = nullptr;
  pulseui::platform::cocoa::a11y_forget(self);
  if (o) o->handle_destroyed();
}
- (void)windowDidResize:(NSNotification*)n {
  if (self.owner) self.owner->handle_resize({static_cast<float>(self.bounds.size.width), static_cast<float>(self.bounds.size.height)});
}
- (void)windowDidChangeBackingProperties:(NSNotification*)n { [self setNeedsDisplay:YES]; }
- (void)windowDidBecomeKey:(NSNotification*)n {
  [NSApp setMainMenu:self.menuBar ? self.menuBar : pulseui::platform::cocoa::default_main_menu()];
}

@end

namespace pulseui::platform {

namespace {

class CocoaWindow final : public NativeWindow {
public:
  CocoaWindow(Window* owner, const WindowConfig& cfg) {
    @autoreleasepool {
      window_ = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, cfg.width, cfg.height)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
      window_.releasedWhenClosed = NO;
      window_.title = [NSString stringWithUTF8String:cfg.title.c_str()];
      view_ = [[PulseView alloc] initWithFrame:NSMakeRect(0, 0, cfg.width, cfg.height)];
      view_.owner = owner;
      view_.wantsLayer = YES;
      window_.contentView = view_;
      window_.delegate = view_;
      [window_ makeFirstResponder:view_];
      [window_ center];
    }
  }

  ~CocoaWindow() override {
    if (window_) {
      view_.owner = nullptr;
      window_.delegate = nil;
      [window_ close];
    }
  }

  void set_title(const std::string& t) override { window_.title = [NSString stringWithUTF8String:t.c_str()]; }
  void set_size(float w, float h) override { [window_ setContentSize:NSMakeSize(w, h)]; }
  void set_min_size(float w, float h) override { window_.contentMinSize = NSMakeSize(w, h); }
  Size size() const override {
    NSSize s = view_.bounds.size;
    return {static_cast<float>(s.width), static_cast<float>(s.height)};
  }
  void show() override {
    if (sheet_parent_ && !window_.sheetParent) {
      [sheet_parent_ beginSheet:window_ completionHandler:nil]; // a modal window is a sheet on macOS
      return;
    }
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  }
  void hide() override { [window_ orderOut:nil]; }
  void destroy() override {
    if (window_.sheetParent) [window_.sheetParent endSheet:window_];
    [window_ close];
  }
  void set_resizable(bool r) override {
    if (r) window_.styleMask |= NSWindowStyleMaskResizable;
    else window_.styleMask &= ~NSWindowStyleMaskResizable;
  }
  void set_modal_parent(NativeWindow* parent) override {
    sheet_parent_ = parent ? (__bridge NSWindow*)parent->handle() : nil;
  }
  void activate() override { [window_ makeKeyAndOrderFront:nil]; }
  void a11y_focus_changed(View* v) override { cocoa::a11y_post_focus(view_, view_.owner, v); }
  void invalidate() override { [view_ setNeedsDisplay:YES]; }
  void set_cursor(Cursor c) override {
    if (view_.cursorKind == c) return;
    view_.cursorKind = c;
    [window_ invalidateCursorRectsForView:view_];
    [ns_cursor(c) set];
  }
  void capture_mouse(bool) override {} // AppKit routes drags to the pressed view
  float scale() const override { return static_cast<float>(window_.backingScaleFactor); }
  void* handle() const override { return (__bridge void*)window_; }

  void set_menu(const MenuItems& items) {
    menu_subs_.clear();
    view_.menuBar = items.empty() ? nil : cocoa::build_main_menu(items, menu_subs_);
    if (window_.isKeyWindow) [NSApp setMainMenu:view_.menuBar ? view_.menuBar : cocoa::default_main_menu()];
  }

private:
  std::vector<pulse::subscription> menu_subs_;
  __weak NSWindow* sheet_parent_ = nil;
  NSWindow* window_ = nil;
  PulseView* view_ = nil;
};

} // namespace

std::unique_ptr<NativeWindow> create_window(Window* owner, const WindowConfig& cfg) {
  return std::make_unique<CocoaWindow>(owner, cfg);
}

bool start_drag(NativeWindow* w, const DragData& data) {
  if (!w) return false;
  NSWindow* win = (__bridge NSWindow*)w->handle();
  PulseView* view = (PulseView*)win.contentView;
  NSEvent* ev = view.lastMouseEvent;
  if (!ev) return false;
  NSPoint at = [view convertPoint:ev.locationInWindow fromView:nil];
  NSMutableArray<NSDraggingItem*>* items = [NSMutableArray array];
  CGFloat offset = 0;
  for (auto& f : data.files) {
    NSString* path = [NSString stringWithUTF8String:f.c_str()];
    NSURL* url = [NSURL fileURLWithPath:path];
    NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
    NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:path];
    [item setDraggingFrame:NSMakeRect(at.x - 16 + offset, at.y - 16 + offset, 32, 32) contents:icon];
    offset += 4;
    [items addObject:item];
  }
  if (!data.text.empty()) {
    NSPasteboardItem* pi = [[NSPasteboardItem alloc] init];
    [pi setString:[NSString stringWithUTF8String:data.text.c_str()] forType:NSPasteboardTypeString];
    NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:pi];
    NSString* label = [NSString stringWithUTF8String:data.text.substr(0, 40).c_str()];
    NSImage* img = [NSImage imageWithSize:NSMakeSize(200, 20)
                                  flipped:NO
                           drawingHandler:^BOOL(NSRect r) {
                             [label drawAtPoint:NSMakePoint(2, 3) withAttributes:@{}];
                             return YES;
                           }];
    [item setDraggingFrame:NSMakeRect(at.x, at.y - 10, 200, 20) contents:img];
    [items addObject:item];
  }
  if (items.count == 0) return false;
  [view beginDraggingSessionWithItems:items event:ev source:view];
  return true; // macOS drags run asynchronously
}

bool set_native_menu(NativeWindow* w, const MenuItems& items) {
  if (!w) return false;
  static_cast<CocoaWindow*>(w)->set_menu(items);
  return true;
}

} // namespace pulseui::platform
