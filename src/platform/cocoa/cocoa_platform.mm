#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_map>

#include "../platform.hpp"
#include "cg_painter.hpp"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface PulseAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation PulseAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
  return NO; // pulseui::App decides (quit_on_last_window_closed)
}
@end

namespace pulseui::platform {

namespace {
PulseAppDelegate* g_delegate = nil;
int g_exit_code = 0;
bool g_quit_requested = false;
bool g_inited = false;
// Never destroyed: timers may be cancelled by static objects during shutdown.
std::unordered_map<std::uint64_t, NSTimer*>& g_timers = *new std::unordered_map<std::uint64_t, NSTimer*>();
std::uint64_t g_next_timer = 1;

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

NSArray<NSString*>* extensions(const std::vector<FileFilter>& filters) {
  NSMutableArray<NSString*>* out = [NSMutableArray array];
  for (auto& f : filters) {
    std::string p = f.patterns;
    std::size_t start = 0;
    while (start < p.size()) {
      std::size_t end = p.find(';', start);
      std::string item = p.substr(start, end == std::string::npos ? std::string::npos : end - start);
      auto dot = item.rfind('.');
      if (dot != std::string::npos && item.substr(dot + 1) != "*") [out addObject:ns(item.substr(dot + 1))];
      if (end == std::string::npos) break;
      start = end + 1;
    }
  }
  return out;
}
} // namespace

void init() {
  if (g_inited) return;
  g_inited = true;
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_delegate = [[PulseAppDelegate alloc] init];
    [NSApp setDelegate:g_delegate];

    [NSApp setMainMenu:cocoa::default_main_menu()];
    [NSApp finishLaunching];
  }
}

int run() {
  [NSApp activateIgnoringOtherApps:YES];
  [NSApp run];
  return g_exit_code;
}

void process_events(int timeout_ms) {
  @autoreleasepool {
    NSDate* until = [NSDate dateWithTimeIntervalSinceNow:std::max(0, timeout_ms) / 1000.0];
    for (;;) {
      NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
      if (!e) break; // the deadline passed (timers and dispatched work ran meanwhile)
      [NSApp sendEvent:e];
    }
  }
}

void run_modal_loop(const std::function<bool()>& done) {
  while (!done() && !g_quit_requested) {
    @autoreleasepool {
      NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                      untilDate:[NSDate distantFuture]
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES];
      if (e) [NSApp sendEvent:e];
    }
  }
}

namespace {
// NSAlert wants the default button first; `order` maps alert buttons back to Message::buttons.
NSAlert* make_alert(const dialogs::Message& m, std::vector<int>& order, NSTextField** field) {
  NSAlert* a = [[NSAlert alloc] init];
  a.messageText = ns(m.title.empty() ? m.text : m.title);
  std::string info = m.title.empty() ? std::string() : m.text;
  if (!m.detail.empty()) info += (info.empty() ? "" : "\n\n") + m.detail;
  a.informativeText = ns(info);
  switch (m.icon) {
    case dialogs::Icon::Warning: a.alertStyle = NSAlertStyleWarning; break;
    case dialogs::Icon::Error: a.alertStyle = NSAlertStyleCritical; break;
    default: a.alertStyle = NSAlertStyleInformational; break;
  }
  const int n = static_cast<int>(m.buttons.size());
  const int def = std::clamp(m.default_button, 0, std::max(0, n - 1));
  order.clear();
  if (n) order.push_back(def);
  for (int i = 0; i < n; ++i)
    if (i != def) order.push_back(i);
  const int cancel = dialogs::cancel_index(m);
  for (std::size_t k = 0; k < order.size(); ++k) {
    NSButton* b = [a addButtonWithTitle:ns(m.buttons[static_cast<std::size_t>(order[k])])];
    if (order[k] == cancel && k > 0) b.keyEquivalent = @"\033";
    if (k == 0 && m.danger && [b respondsToSelector:@selector(setHasDestructiveAction:)]) b.hasDestructiveAction = YES;
  }
  *field = nil;
  if (m.input) {
    NSTextField* f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    f.stringValue = ns(m.input_text);
    if (!m.input_placeholder.empty()) f.placeholderString = ns(m.input_placeholder);
    a.accessoryView = f;
    a.window.initialFirstResponder = f;
    *field = f;
  }
  return a;
}

dialogs::Answer answer_of(NSModalResponse r, const std::vector<int>& order, NSTextField* field, const dialogs::Message& m) {
  dialogs::Answer out;
  const long k = static_cast<long>(r) - static_cast<long>(NSAlertFirstButtonReturn);
  out.button = k >= 0 && k < static_cast<long>(order.size()) ? order[static_cast<std::size_t>(k)] : dialogs::cancel_index(m);
  if (field) out.text = field.stringValue.UTF8String;
  return out;
}
} // namespace

std::optional<dialogs::Answer> native_message(NativeWindow*, const dialogs::Message& m) {
  @autoreleasepool {
    std::vector<int> order;
    NSTextField* field = nil;
    NSAlert* a = make_alert(m, order, &field);
    return answer_of([a runModal], order, field, m);
  }
}

bool native_message_async(NativeWindow* parent, const dialogs::Message& m, std::function<void(dialogs::Answer)> done) {
  auto order = std::make_shared<std::vector<int>>();
  NSTextField* field = nil;
  NSAlert* a = make_alert(m, *order, &field);
  auto msg = std::make_shared<dialogs::Message>(m);
  auto cb = std::make_shared<std::function<void(dialogs::Answer)>>(std::move(done));
  NSWindow* pw = parent ? (__bridge NSWindow*)parent->handle() : nil;
  if (pw) {
    [a beginSheetModalForWindow:pw
              completionHandler:^(NSModalResponse r) {
                if (*cb) (*cb)(answer_of(r, *order, field, *msg));
              }];
  } else {
    dispatch_async(dispatch_get_main_queue(), ^{
      NSModalResponse r = [a runModal];
      if (*cb) (*cb)(answer_of(r, *order, field, *msg));
    });
  }
  return true;
}

void quit(int code) {
  g_exit_code = code;
  g_quit_requested = true;
  dispatch_async(dispatch_get_main_queue(), ^{
    [NSApp stop:nil];
    // wake the loop so -stop: takes effect immediately
    NSEvent* e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0
                                   timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
    [NSApp postEvent:e atStart:YES];
  });
}

void post(std::function<void()> fn) {
  auto f = std::make_shared<std::function<void()>>(std::move(fn));
  dispatch_async(dispatch_get_main_queue(), ^{
    if (*f) (*f)();
  });
}

bool is_main_thread() { return [NSThread isMainThread]; }

std::uint64_t start_timer(int ms, bool repeat, std::function<void()> fn) {
  const std::uint64_t id = g_next_timer++;
  auto f = std::make_shared<std::function<void()>>(std::move(fn));
  NSTimer* t = [NSTimer timerWithTimeInterval:std::max(ms, 1) / 1000.0
                                      repeats:repeat
                                        block:^(NSTimer*) {
                                          if (!repeat) g_timers.erase(id);
                                          if (*f) (*f)();
                                        }];
  [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
  g_timers[id] = t;
  return id;
}

void stop_timer(std::uint64_t id) {
  auto it = g_timers.find(id);
  if (it == g_timers.end()) return;
  [it->second invalidate];
  g_timers.erase(it);
}

Size measure_text(std::string_view s, const Font& f, float max_width) {
  @autoreleasepool {
    std::string str(s.empty() ? std::string_view("Ag") : s);
    NSString* text = ns(str);
    NSDictionary* attrs = cocoa::text_attributes(f, Color::black(), Align::Left, max_width > 0);
    NSRect r = [text boundingRectWithSize:NSMakeSize(max_width > 0 ? max_width : CGFLOAT_MAX, CGFLOAT_MAX)
                                  options:NSStringDrawingUsesLineFragmentOrigin
                               attributes:attrs];
    float w = static_cast<float>(std::ceil(r.size.width));
    if (max_width <= 0) w = static_cast<float>(std::ceil([text sizeWithAttributes:attrs].width));
    return {s.empty() ? 0.f : w, static_cast<float>(std::ceil(r.size.height))};
  }
}

float line_height(const Font& f) { return platform::measure_text("Ag", f, 0).h; }

void set_clipboard(const std::string& s) {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  [pb setString:ns(s) forType:NSPasteboardTypeString];
}

std::string get_clipboard() {
  NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  return s ? std::string([s UTF8String]) : std::string();
}

std::optional<std::string> open_file_dialog(NativeWindow*, const std::string& title,
                                            const std::vector<FileFilter>& filters) {
  @autoreleasepool {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.title = ns(title);
    p.canChooseFiles = YES;
    p.canChooseDirectories = NO;
    p.allowsMultipleSelection = NO;
    NSArray* ext = extensions(filters);
    if (ext.count) p.allowedFileTypes = ext;
    if ([p runModal] != NSModalResponseOK || !p.URL) return std::nullopt;
    return std::string(p.URL.path.UTF8String);
  }
}

std::optional<std::string> save_file_dialog(NativeWindow*, const std::string& title,
                                            const std::vector<FileFilter>& filters,
                                            const std::string& default_name) {
  @autoreleasepool {
    NSSavePanel* p = [NSSavePanel savePanel];
    p.title = ns(title);
    if (!default_name.empty()) p.nameFieldStringValue = ns(default_name);
    NSArray* ext = extensions(filters);
    if (ext.count) p.allowedFileTypes = ext;
    if ([p runModal] != NSModalResponseOK || !p.URL) return std::nullopt;
    return std::string(p.URL.path.UTF8String);
  }
}

namespace {
void unpremultiply(std::vector<std::uint8_t>& px) {
  for (std::size_t i = 0; i < px.size(); i += 4) {
    const unsigned a = px[i + 3];
    if (a == 0 || a == 255) continue;
    for (int k = 0; k < 3; ++k) px[i + k] = static_cast<std::uint8_t>(std::min(255u, px[i + k] * 255u / a));
  }
}
} // namespace

bool decode_image(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba) {
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:ns(path)];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!src) return false;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return false;
    w = static_cast<int>(CGImageGetWidth(img));
    h = static_cast<int>(CGImageGetHeight(img));
    rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(img);
    unpremultiply(rgba);
    return true;
  }
}

bool encode_png(const std::string& path, int w, int h, const std::uint8_t* rgba) {
  @autoreleasepool {
    std::vector<std::uint8_t> pm(rgba, rgba + static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < pm.size(); i += 4)
      for (int k = 0; k < 3; ++k) pm[i + k] = static_cast<std::uint8_t>(pm[i + k] * pm[i + 3] / 255);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(pm.data(), w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedLast);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:ns(path)];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dst) {
      CGImageDestinationAddImage(dst, img, nullptr);
      ok = CGImageDestinationFinalize(dst);
      CFRelease(dst);
    }
    CGImageRelease(img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    return ok;
  }
}

bool render_offscreen(int w, int h, float scale, const std::function<void(Painter&)>& paint,
                      std::vector<std::uint8_t>& rgba) {
  @autoreleasepool {
    const int pw = static_cast<int>(w * scale + 0.5f), ph = static_cast<int>(h * scale + 0.5f);
    rgba.assign(static_cast<std::size_t>(pw) * ph * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), pw, ph, 8, pw * 4, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!ctx) return false;
    CGContextTranslateCTM(ctx, 0, ph);
    CGContextScaleCTM(ctx, scale, -scale);
    NSGraphicsContext* prev = [NSGraphicsContext currentContext];
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithCGContext:ctx flipped:YES]];
    {
      cocoa::CGPainter p(ctx);
      paint(p);
    }
    [NSGraphicsContext setCurrentContext:prev];
    CGContextRelease(ctx);
    unpremultiply(rgba);
    return true;
  }
}

} // namespace pulseui::platform
