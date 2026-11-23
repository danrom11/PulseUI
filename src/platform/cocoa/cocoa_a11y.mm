// NSAccessibility: exposes PulseUI views to VoiceOver.
#import <Cocoa/Cocoa.h>

#include <unordered_map>

#include <pulseui/ui/window.hpp>

#include "../platform.hpp"
#include "cg_painter.hpp"

namespace pulseui {
namespace detail {
void a11y_children(const View* v, std::vector<View*>& out);
void a11y_window_children(const Window& w, std::vector<View*>& out);
View* a11y_parent(const View* v);
View* a11y_exposed(View* v);
} // namespace detail
} // namespace pulseui

using pulseui::A11yAction;
using pulseui::A11yInfo;
using pulseui::Role;

@class PulseA11yElement;

namespace {
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

// one element per exposed view, per host NSView
std::unordered_map<const void*, std::unordered_map<const pulseui::View*, PulseA11yElement*>>& registry() {
  static auto* r = new std::unordered_map<const void*, std::unordered_map<const pulseui::View*, PulseA11yElement*>>();
  return *r;
}

NSAccessibilityRole ns_role(const A11yInfo& i) {
  switch (i.role) {
    case Role::Button: return NSAccessibilityButtonRole;
    case Role::Link: return NSAccessibilityLinkRole;
    case Role::CheckBox:
    case Role::Switch: return NSAccessibilityCheckBoxRole;
    case Role::RadioButton: return NSAccessibilityRadioButtonRole;
    case Role::Edit: return i.multi_line ? NSAccessibilityTextAreaRole : NSAccessibilityTextFieldRole;
    case Role::Document: return NSAccessibilityTextAreaRole;
    case Role::Text:
    case Role::Heading:
    case Role::Alert: return NSAccessibilityStaticTextRole;
    case Role::Slider: return NSAccessibilitySliderRole;
    case Role::Spinner: return NSAccessibilityIncrementorRole;
    case Role::ProgressBar: return NSAccessibilityProgressIndicatorRole;
    case Role::ComboBox: return NSAccessibilityPopUpButtonRole;
    case Role::List: return NSAccessibilityListRole;
    case Role::Table: return NSAccessibilityTableRole;
    case Role::Tree: return NSAccessibilityOutlineRole;
    case Role::Tab: return NSAccessibilityTabGroupRole;
    case Role::Menu: return NSAccessibilityMenuRole;
    case Role::MenuBar: return NSAccessibilityMenuBarRole;
    case Role::MenuItem: return NSAccessibilityMenuItemRole;
    case Role::Image: return NSAccessibilityImageRole;
    case Role::Toolbar: return NSAccessibilityToolbarRole;
    default: return NSAccessibilityGroupRole;
  }
}
} // namespace

@interface PulseA11yElement : NSAccessibilityElement
- (instancetype)initWithView:(pulseui::View*)v window:(pulseui::Window*)w host:(NSView*)host;
- (pulseui::View*)view;
@end

namespace {
PulseA11yElement* element_for(NSView* host, pulseui::Window* w, pulseui::View* v) {
  if (!v) return nil;
  auto& map = registry()[(__bridge const void*)host];
  auto it = map.find(v);
  if (it != map.end() && [it->second view] == v) return it->second;
  PulseA11yElement* e = [[PulseA11yElement alloc] initWithView:v window:w host:host];
  map[v] = e;
  return e;
}
NSArray* elements(NSView* host, pulseui::Window* w, const std::vector<pulseui::View*>& views) {
  NSMutableArray* out = [NSMutableArray arrayWithCapacity:views.size()];
  for (pulseui::View* v : views) [out addObject:element_for(host, w, v)];
  return out;
}
} // namespace

@implementation PulseA11yElement {
  pulseui::View* view_;
  std::weak_ptr<bool> alive_;
  pulseui::Window* window_;
  std::weak_ptr<bool> window_alive_;
  __weak NSView* host_;
}

- (instancetype)initWithView:(pulseui::View*)v window:(pulseui::Window*)w host:(NSView*)host {
  if ((self = [super init])) {
    view_ = v;
    alive_ = v->alive_token();
    window_ = w;
    window_alive_ = w->alive_token();
    host_ = host;
  }
  return self;
}

- (pulseui::View*)view { return !alive_.expired() && !window_alive_.expired() ? view_ : nullptr; }
- (A11yInfo)info {
  pulseui::View* v = [self view];
  return v ? v->a11y_info() : A11yInfo{};
}

- (BOOL)isAccessibilityElement { return [self view] != nullptr; }
- (NSAccessibilityRole)accessibilityRole { return ns_role([self info]); }
- (NSAccessibilitySubrole)accessibilitySubrole {
  return [self info].role == Role::Switch ? NSAccessibilitySwitchSubrole : nil;
}
- (NSString*)accessibilityRoleDescription { return NSAccessibilityRoleDescription([self accessibilityRole], [self accessibilitySubrole]); }
- (NSString*)accessibilityLabel {
  const A11yInfo i = [self info];
  return i.name.empty() ? nil : ns(i.name);
}
- (NSString*)accessibilityHelp {
  const A11yInfo i = [self info];
  return i.description.empty() ? nil : ns(i.description);
}
- (id)accessibilityValue {
  const A11yInfo i = [self info];
  if (i.checked >= 0) return @(i.checked);
  if (i.has_range && (i.role == Role::Slider || i.role == Role::ProgressBar)) return @(i.now);
  return i.value.empty() ? nil : ns(i.value);
}
- (void)setAccessibilityValue:(id)value {
  pulseui::View* v = [self view];
  if (!v) return;
  NSString* s = [value isKindOfClass:[NSString class]] ? value : [value description];
  v->accessibility_action(A11yAction::SetValue, s.UTF8String);
}
- (id)accessibilityMinValue {
  const A11yInfo i = [self info];
  return i.has_range ? @(i.min) : nil;
}
- (id)accessibilityMaxValue {
  const A11yInfo i = [self info];
  return i.has_range ? @(i.max) : nil;
}
- (BOOL)isAccessibilityEnabled { return [self info].enabled; }
- (BOOL)isAccessibilityFocused { return [self info].focused; }
- (void)setAccessibilityFocused:(BOOL)f {
  if (f)
    if (pulseui::View* v = [self view]) v->accessibility_action(A11yAction::Focus);
}
- (BOOL)isAccessibilityExpanded { return [self info].expanded == 1; }
- (BOOL)isAccessibilitySelected { return [self info].selected; }

- (NSRect)accessibilityFrame {
  pulseui::View* v = [self view];
  NSView* host = host_;
  if (!v || !host || !host.window) return NSZeroRect;
  const pulseui::Point tl = v->to_window({0, 0});
  NSRect local = NSMakeRect(tl.x, tl.y, v->frame().w, v->frame().h); // the host view is flipped
  NSRect inWindow = [host convertRect:local toView:nil];
  return [host.window convertRectToScreen:inWindow];
}

- (id)accessibilityParent {
  pulseui::View* v = [self view];
  NSView* host = host_;
  if (!v) return host;
  pulseui::View* p = pulseui::detail::a11y_parent(v);
  return p ? (id)element_for(host, window_, p) : (id)host;
}

- (NSArray*)accessibilityChildren {
  pulseui::View* v = [self view];
  if (!v) return @[];
  std::vector<pulseui::View*> kids;
  pulseui::detail::a11y_children(v, kids);
  return elements(host_, window_, kids);
}

- (BOOL)accessibilityPerformPress {
  pulseui::View* v = [self view];
  if (!v) return NO;
  const A11yInfo i = v->a11y_info();
  return v->accessibility_action(i.checked >= 0 ? A11yAction::Toggle : A11yAction::Press);
}
- (BOOL)accessibilityPerformIncrement {
  pulseui::View* v = [self view];
  return v && v->accessibility_action(A11yAction::Increment);
}
- (BOOL)accessibilityPerformDecrement {
  pulseui::View* v = [self view];
  return v && v->accessibility_action(A11yAction::Decrement);
}
- (BOOL)accessibilityPerformShowMenu {
  pulseui::View* v = [self view];
  return v && v->accessibility_action(A11yAction::Expand);
}
@end

namespace pulseui::platform::cocoa {

NSArray* a11y_window_children(NSView* host, Window* w) {
  if (!w) return @[];
  std::vector<View*> kids;
  pulseui::detail::a11y_window_children(*w, kids);
  return elements(host, w, kids);
}

id a11y_hit_test(NSView* host, Window* w, NSPoint screen) {
  if (!w || !host.window) return host;
  NSRect r = [host.window convertRectFromScreen:NSMakeRect(screen.x, screen.y, 0, 0)];
  NSPoint p = [host convertPoint:r.origin fromView:nil];
  View* v = pulseui::detail::a11y_exposed(w->view_at({static_cast<float>(p.x), static_cast<float>(p.y)}));
  return v ? (id)element_for(host, w, v) : (id)host;
}

id a11y_focused(NSView* host, Window* w) {
  if (!w) return host;
  View* v = pulseui::detail::a11y_exposed(w->focus_view());
  return v ? (id)element_for(host, w, v) : (id)host;
}

void a11y_post_focus(NSView* host, Window* w, View* v) {
  if (!w) return;
  View* e = pulseui::detail::a11y_exposed(v);
  id target = e ? (id)element_for(host, w, e) : (id)host;
  NSAccessibilityPostNotification(target, NSAccessibilityFocusedUIElementChangedNotification);
}

void a11y_forget(NSView* host) { registry().erase((__bridge const void*)host); }

} // namespace pulseui::platform::cocoa
