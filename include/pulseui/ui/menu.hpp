#pragma once
// Menus as plain values: a window menu bar, context menus and pop-up menus are
// all described with the same three building blocks.
//
//   win.menu({
//     Menu("File", {
//       Action("New", [&] { doc.reset(); }).shortcut("Ctrl+N"),
//       Action("Save", [&] { save(); }).shortcut("Ctrl+S").enabled(dirty), // Bind<bool>
//       Separator(),
//       Menu("Recent", [&] { return recent_entries(); }),                 // built when opened
//       Action("Quit", [] { App::quit(); }),
//     }),
//     Menu("View", { Action("Word wrap").checked(wrap) }),               // Model<bool>& toggles
//   });
//
//   Text("Right-click me").context_menu({ Action("Copy", copy), Action("Paste", paste) });
//
// On macOS the window menu becomes the native application menu bar (with the
// standard application menu added in front). On Windows it is drawn at the top
// of the window in the current theme; Alt or F10 moves the keyboard into it.
// Shortcuts of menu actions work without opening the menu.
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <pulseui/ui/bind.hpp>

namespace pulseui {

struct MenuEntry {
  enum class Kind { Action, Separator, Submenu };
  Kind kind = Kind::Action;
  std::string label;        // "&File": the letter after & is the mnemonic (Alt+F)
  std::string shortcut_;    // "Ctrl+S" (shown on the right, and active while the window has the menu)
  std::string icon_;        // a glyph drawn left of the label
  std::function<void()> fn;
  Bind<bool> enabled_;      // empty = enabled
  Bind<bool> checked_;      // empty = not a check item
  Model<bool>* toggle_ = nullptr;
  bool radio_ = false;      // checked mark drawn as a dot
  std::vector<MenuEntry> items;                        // submenu entries
  std::function<std::vector<MenuEntry>()> provider;    // or built when the submenu opens

  MenuEntry& shortcut(std::string s) & { shortcut_ = std::move(s); return *this; }
  MenuEntry&& shortcut(std::string s) && { shortcut_ = std::move(s); return std::move(*this); }
  MenuEntry& icon(std::string g) & { icon_ = std::move(g); return *this; }
  MenuEntry&& icon(std::string g) && { icon_ = std::move(g); return std::move(*this); }
  MenuEntry& enabled(Bind<bool> b) & { enabled_ = std::move(b); return *this; }
  MenuEntry&& enabled(Bind<bool> b) && { enabled_ = std::move(b); return std::move(*this); }
  // A check item following a value/stream (fn decides what a click does)...
  MenuEntry& checked(Bind<bool> b) & { checked_ = std::move(b); return *this; }
  MenuEntry&& checked(Bind<bool> b) && { checked_ = std::move(b); return std::move(*this); }
  // ...or two-way bound: a click flips the model, then runs fn (if any).
  MenuEntry& checked(Model<bool>& m) & { toggle_ = &m; checked_ = Bind<bool>(m); return *this; }
  MenuEntry&& checked(Model<bool>& m) && { toggle_ = &m; checked_ = Bind<bool>(m); return std::move(*this); }
  // Draw the check mark as a radio dot (for groups of exclusive options).
  MenuEntry& radio(bool r = true) & { radio_ = r; return *this; }
  MenuEntry&& radio(bool r = true) && { radio_ = r; return std::move(*this); }

  bool is_check() const { return !checked_.empty(); }
  // label without the mnemonic marker ("&File" -> "File", "&&" -> "&")
  std::string display_label() const;
  // the mnemonic letter (lower case), or 0
  char mnemonic() const;
  // runs the action (flips a bound check model first)
  void trigger() const;
  // entries of a submenu (calls the provider if there is one)
  std::vector<MenuEntry> children() const { return provider ? provider() : items; }
};

inline MenuEntry Action(std::string label, std::function<void()> fn = {}) {
  MenuEntry e;
  e.label = std::move(label);
  e.fn = std::move(fn);
  return e;
}

inline MenuEntry Separator() {
  MenuEntry e;
  e.kind = MenuEntry::Kind::Separator;
  return e;
}

inline MenuEntry Menu(std::string label, std::vector<MenuEntry> items) {
  MenuEntry e;
  e.kind = MenuEntry::Kind::Submenu;
  e.label = std::move(label);
  e.items = std::move(items);
  return e;
}

inline MenuEntry Menu(std::string label, std::function<std::vector<MenuEntry>()> provider) {
  MenuEntry e;
  e.kind = MenuEntry::Kind::Submenu;
  e.label = std::move(label);
  e.provider = std::move(provider);
  return e;
}

using MenuItems = std::vector<MenuEntry>;
using MenuProvider = std::function<MenuItems()>;

} // namespace pulseui
