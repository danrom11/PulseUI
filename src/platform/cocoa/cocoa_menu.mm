// Native macOS menus: the application menu bar built from Window::menu().
#import <Cocoa/Cocoa.h>

#include <memory>
#include <string>
#include <vector>

#include "../platform.hpp"
#include "cg_painter.hpp"

using pulseui::MenuEntry;
using pulseui::MenuItems;

// Target of one menu item: keeps a copy of the entry and runs it.
@interface PulseMenuAction : NSObject
- (instancetype)initWithEntry:(const MenuEntry&)entry;
- (void)fire:(id)sender;
@end

@implementation PulseMenuAction {
  MenuEntry entry_;
}
- (instancetype)initWithEntry:(const MenuEntry&)entry {
  if ((self = [super init])) entry_ = entry;
  return self;
}
- (void)fire:(id)sender {
  MenuEntry e = entry_;
  e.trigger();
}
@end

// Rebuilds a submenu from its provider each time it opens ("Recent files").
@interface PulseMenuFiller : NSObject <NSMenuDelegate>
- (instancetype)initWithProvider:(std::function<MenuItems()>)provider;
@end

namespace pulseui::platform::cocoa {
void fill_menu(NSMenu* menu, const MenuItems& items, std::vector<pulse::subscription>* subs);
}

@implementation PulseMenuFiller {
  std::function<MenuItems()> provider_;
}
- (instancetype)initWithProvider:(std::function<MenuItems()>)provider {
  if ((self = [super init])) provider_ = std::move(provider);
  return self;
}
- (void)menuNeedsUpdate:(NSMenu*)menu {
  [menu removeAllItems];
  if (provider_) pulseui::platform::cocoa::fill_menu(menu, provider_(), nullptr);
}
@end

namespace pulseui::platform::cocoa {

namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

// "Ctrl+Shift+S" -> key equivalent "s" + Command|Shift ("Ctrl" means Command, as in Window::shortcut)
void apply_shortcut(NSMenuItem* item, const std::string& text) {
  const Shortcut sc = Shortcut::parse(text);
  NSString* key = nil;
  const int k = static_cast<int>(sc.key);
  if (k >= static_cast<int>(Key::A) && k <= static_cast<int>(Key::Z)) {
    const unichar c = static_cast<unichar>('a' + (k - static_cast<int>(Key::A)));
    key = [NSString stringWithCharacters:&c length:1];
  } else if (k >= static_cast<int>(Key::Num0) && k <= static_cast<int>(Key::Num9)) {
    const unichar c = static_cast<unichar>('0' + (k - static_cast<int>(Key::Num0)));
    key = [NSString stringWithCharacters:&c length:1];
  } else if (k >= static_cast<int>(Key::F1) && k <= static_cast<int>(Key::F12)) {
    const unichar c = static_cast<unichar>(NSF1FunctionKey + (k - static_cast<int>(Key::F1)));
    key = [NSString stringWithCharacters:&c length:1];
  } else {
    unichar c = 0;
    switch (sc.key) {
      case Key::Enter: c = '\r'; break;
      case Key::Escape: c = 0x1B; break;
      case Key::Tab: c = '\t'; break;
      case Key::Space: c = ' '; break;
      case Key::Backspace: c = 0x08; break;
      case Key::Delete: c = NSDeleteFunctionKey; break;
      case Key::Left: c = NSLeftArrowFunctionKey; break;
      case Key::Right: c = NSRightArrowFunctionKey; break;
      case Key::Up: c = NSUpArrowFunctionKey; break;
      case Key::Down: c = NSDownArrowFunctionKey; break;
      case Key::Home: c = NSHomeFunctionKey; break;
      case Key::End: c = NSEndFunctionKey; break;
      case Key::PageUp: c = NSPageUpFunctionKey; break;
      case Key::PageDown: c = NSPageDownFunctionKey; break;
      case Key::Slash: c = '/'; break;
      default: break;
    }
    if (c) key = [NSString stringWithCharacters:&c length:1];
  }
  if (!key) return;
  NSEventModifierFlags m = 0;
  if (sc.mods & Mod::Super) m |= NSEventModifierFlagCommand;  // Mod::Primary on macOS
  if (sc.mods & Mod::Ctrl) m |= NSEventModifierFlagControl;
  if (sc.mods & Mod::Shift) m |= NSEventModifierFlagShift;
  if (sc.mods & Mod::Alt) m |= NSEventModifierFlagOption;
  item.keyEquivalent = key;
  item.keyEquivalentModifierMask = m;
}

void follow(NSMenuItem* item, const Bind<bool>& b, bool checked, std::vector<pulse::subscription>* subs) {
  if (b.empty()) return;
  __weak NSMenuItem* weak = item;
  auto apply = [weak, checked](bool v) {
    NSMenuItem* it = weak;
    if (!it) return;
    if (checked) it.state = v ? NSControlStateValueOn : NSControlStateValueOff;
    else it.enabled = v;
  };
  if (!b.is_dynamic()) {
    apply(*b.value());
    return;
  }
  auto s = b.attach([apply](const bool& v) {
    if ([NSThread isMainThread]) apply(v);
    else dispatch_async(dispatch_get_main_queue(), ^{ apply(v); });
  });
  if (subs) subs->push_back(std::move(s));
  // without a subscription list (a menu filled on open) the current value is enough:
  // `s` is cancelled here, after the synchronous first emission.
}

} // namespace

void fill_menu(NSMenu* menu, const MenuItems& items, std::vector<pulse::subscription>* subs) {
  menu.autoenablesItems = NO;
  for (const MenuEntry& e : items) {
    if (e.kind == MenuEntry::Kind::Separator) {
      [menu addItem:[NSMenuItem separatorItem]];
      continue;
    }
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:ns(e.display_label()) action:nil keyEquivalent:@""];
    if (e.kind == MenuEntry::Kind::Submenu) {
      NSMenu* sub = [[NSMenu alloc] initWithTitle:ns(e.display_label())];
      if (e.provider) {
        PulseMenuFiller* filler = [[PulseMenuFiller alloc] initWithProvider:e.provider];
        sub.delegate = filler;
        item.representedObject = filler; // menu delegates are weak: keep it alive
      } else {
        fill_menu(sub, e.items, subs);
      }
      item.submenu = sub;
    } else {
      PulseMenuAction* act = [[PulseMenuAction alloc] initWithEntry:e];
      item.target = act;
      item.action = @selector(fire:);
      item.representedObject = act; // the target is weak
      if (!e.shortcut_.empty()) apply_shortcut(item, e.shortcut_);
      follow(item, e.checked_, true, subs);
    }
    follow(item, e.enabled_, false, subs);
    [menu addItem:item];
  }
}

// The standard application menu (About, Hide, Quit) every macOS app has first.
static NSMenuItem* app_menu_item() {
  NSMenuItem* app_item = [[NSMenuItem alloc] init];
  NSMenu* app_menu = [[NSMenu alloc] init];
  NSString* name = [[NSProcessInfo processInfo] processName];
  [app_menu addItemWithTitle:[@"About " stringByAppendingString:name]
                      action:@selector(orderFrontStandardAboutPanel:)
               keyEquivalent:@""];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
  NSMenuItem* others = [app_menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
  others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [app_menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
  app_item.submenu = app_menu;
  return app_item;
}

NSMenu* default_main_menu() {
  static NSMenu* bar = nil;
  if (!bar) {
    bar = [[NSMenu alloc] init];
    [bar addItem:app_menu_item()];
  }
  return bar;
}

NSMenu* build_main_menu(const MenuItems& items, std::vector<pulse::subscription>& subs) {
  NSMenu* bar = [[NSMenu alloc] init];
  [bar addItem:app_menu_item()];
  for (const MenuEntry& e : items) {
    if (e.kind != MenuEntry::Kind::Submenu) continue; // top level holds menus only
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:ns(e.display_label()) action:nil keyEquivalent:@""];
    NSMenu* sub = [[NSMenu alloc] initWithTitle:ns(e.display_label())];
    if (e.provider) {
      PulseMenuFiller* filler = [[PulseMenuFiller alloc] initWithProvider:e.provider];
      sub.delegate = filler;
      item.representedObject = filler;
    } else {
      fill_menu(sub, e.items, &subs);
    }
    item.submenu = sub;
    [bar addItem:item];
  }
  return bar;
}

} // namespace pulseui::platform::cocoa
