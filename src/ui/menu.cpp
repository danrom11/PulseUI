// Menus: pop-up menus (context menus, submenus) and the drawn menu bar.
#include <pulseui/gfx/svg.hpp>
#include <pulseui/ui/menu.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "../platform/platform.hpp"

namespace pulseui {

// ── MenuEntry ────────────────────────────────────────────────────────────────

std::string MenuEntry::display_label() const {
  std::string out;
  out.reserve(label.size());
  for (std::size_t i = 0; i < label.size(); ++i) {
    if (label[i] == '&' && i + 1 < label.size()) {
      out += label[++i];
      continue;
    }
    out += label[i];
  }
  return out;
}

char MenuEntry::mnemonic() const {
  for (std::size_t i = 0; i + 1 < label.size(); ++i) {
    if (label[i] != '&') continue;
    if (label[i + 1] == '&') { ++i; continue; }
    return static_cast<char>(std::tolower(static_cast<unsigned char>(label[i + 1])));
  }
  return 0;
}

void MenuEntry::trigger() const {
  if (toggle_) toggle_->set(!toggle_->get());
  if (fn) fn();
}

namespace detail {

// Human readable shortcut: "Ctrl+Shift+S" (⌘⇧S on macOS).
std::string shortcut_label(const std::string& s) {
#ifdef __APPLE__
  std::string out, key;
  std::size_t start = 0;
  while (start <= s.size()) {
    std::size_t plus = s.find('+', start);
    if (plus == start && plus + 1 == s.size()) plus = std::string::npos;
    std::string tok = s.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
    std::string low = tok;
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (low == "ctrl" || low == "cmd" || low == "primary") out += "\xE2\x8C\x98";      // ⌘
    else if (low == "shift") out += "\xE2\x87\xA7";                                     // ⇧
    else if (low == "alt" || low == "option") out += "\xE2\x8C\xA5";                    // ⌥
    else if (low == "meta" || low == "super") out += "\xE2\x8C\x83";                    // ⌃
    else key = tok.size() == 1 ? std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0])))) : tok;
    if (plus == std::string::npos) break;
    start = plus + 1;
  }
  return out + key;
#else
  return s;
#endif
}

} // namespace detail

namespace {

// ═════════════════════════════════════════════════════════════════════════════
// Session: the chain of menus open at the same time (menu, submenu, ...)
// ═════════════════════════════════════════════════════════════════════════════
class MenuPopup;
class MenuBarView;

struct MenuSession {
  Window* window = nullptr;
  std::vector<MenuPopup*> levels;
  std::weak_ptr<bool> restore_alive;
  View* restore_focus = nullptr;
  MenuBarView* bar = nullptr; // set when opened from the menu bar
  std::function<void()> on_closed;
  bool closing = false;

  void close_from(std::size_t level);
  void close_all() { close_from(0); }
  void activate(const MenuEntry& e);
};

constexpr float kRowH = 28, kSepH = 9, kPad = 4;

// One row of a pop-up menu.
class MenuRow : public ViewT<MenuRow> {
public:
  MenuRow(MenuEntry e, int index) : entry_(std::move(e)), index_(index) {
    if (entry_.kind != MenuEntry::Kind::Separator) {
      bind(entry_.enabled_, [](View& v, bool b) { v.set_enabled(b); });
      bind(entry_.checked_, [](View& v, bool b) {
        static_cast<MenuRow&>(v).checked_ = b;
        v.invalidate();
      });
    }
  }
  const char* type_name() const override { return "MenuItem"; }
  const MenuEntry& entry() const { return entry_; }
  bool separator() const { return entry_.kind == MenuEntry::Kind::Separator; }
  bool selectable() const { return !separator() && is_enabled(); }
  bool active = false;

  Size measure_content(Size) override {
    if (separator()) return {0, kSepH};
    const Theme& t = th();
    float w = 28 + measure_text(entry_.display_label(), t.font, 0).w + 24;
    if (!entry_.shortcut_.empty()) w += measure_text(detail::shortcut_label(entry_.shortcut_), t.font_small, 0).w + 24;
    if (entry_.kind == MenuEntry::Kind::Submenu) w += 16;
    return {std::ceil(w), kRowH};
  }

  void paint(Painter& p) override {
    const Theme& t = th();
    const Rect b = bounds();
    if (separator()) {
      p.line({b.x + 8, b.h / 2}, {b.right() - 8, b.h / 2}, t.border, 1);
      return;
    }
    const bool enabled = is_enabled();
    const bool hot = active && enabled;
    if (hot) p.fill_rect(b.inset(Insets(4, 1)), t.accent, t.radius_small);
    const Color fg = !enabled ? t.text_disabled : hot ? t.on_accent : t.text;
    const Color dim = !enabled ? t.text_disabled : hot ? t.on_accent.with_alpha(0.8f) : t.text_dim;
    if (checked_) {
      if (entry_.radio_) p.fill_ellipse({b.x + 12, b.h / 2 - 3.5f, 7, 7}, fg);
      else p.polyline({{b.x + 11, b.h / 2}, {b.x + 14.5f, b.h / 2 + 3.5f}, {b.x + 20, b.h / 2 - 4}}, fg, 1.6f);
    } else if (!entry_.icon_.empty()) {
      draw_icon(p, entry_.icon_, {b.x + 6, 0, 22, b.h}, dim, t.font);
    }
    Rect lr{b.x + 28, 0, b.w - 36, b.h};
    p.text(lr, entry_.display_label(), t.font, fg, Align::LeftCenter);
    if (entry_.kind == MenuEntry::Kind::Submenu) {
      const float x = b.right() - 16, y = b.h / 2;
      p.polyline({{x, y - 4}, {x + 4, y}, {x, y + 4}}, dim, 1.4f);
    } else if (!entry_.shortcut_.empty()) {
      p.text({b.x, 0, b.w - 14, b.h}, detail::shortcut_label(entry_.shortcut_), t.font_small, dim, Align::RightCenter);
    }
  }

  bool on_event(InputEvent& e) override;
  void hover_changed(bool h) override;
  void accessibility(A11yInfo& info) const override {
    View::accessibility(info);
    if (separator()) {
      info.role = Role::Separator;
      return;
    }
    info.role = Role::MenuItem;
    info.name = entry_.display_label();
    info.description = detail::shortcut_label(entry_.shortcut_);
    info.selected = active;
    if (entry_.is_check()) info.checked = checked_ ? 1 : 0;
    if (entry_.kind == MenuEntry::Kind::Submenu) info.expanded = 0;
  }
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  MenuEntry entry_;
  int index_;
  bool checked_ = false;
};

// A pop-up menu (one level).
class MenuPopup : public ViewT<MenuPopup> {
public:
  MenuPopup(std::shared_ptr<MenuSession> s, const MenuItems& items, std::size_t level)
      : session_(std::move(s)), level_(level) {
    focusable_ = true;
    style_.padding = Insets(0, kPad);
    int i = 0;
    for (auto& e : items) add(MenuRow(e, i++));
  }
  const char* type_name() const override { return "MenuPopup"; }
  std::size_t level() const { return level_; }
  MenuSession& session() { return *session_; }
  int active() const { return active_; }

  MenuRow* row(int i) const {
    return i >= 0 && i < static_cast<int>(children_.size()) ? static_cast<MenuRow*>(children_[static_cast<std::size_t>(i)].get())
                                                            : nullptr;
  }

  Size measure_content(Size) override {
    float w = 180, h = 0;
    for (auto& c : children_) {
      const Size s = c->measure({10000, 10000});
      w = std::max(w, s.w);
      h += s.h;
    }
    return {std::min(w, 480.f), h};
  }
  void layout() override {
    const Rect r = content_rect();
    float y = r.y;
    for (auto& c : children_) {
      const float h = static_cast<MenuRow*>(c.get())->separator() ? kSepH : kRowH;
      c->set_frame({r.x, y, r.w, h});
      y += h;
    }
  }
  void paint(Painter& p) override {
    const Theme& t = th();
    p.fill_rect(bounds(), t.surface, t.radius);
    p.stroke_rect(bounds(), t.border, 1, t.radius);
  }

  void set_active(int i, bool open_sub = false) {
    if (i == active_) {
      if (open_sub) open_submenu();
      return;
    }
    if (MenuRow* r = row(active_)) {
      r->active = false;
      r->invalidate();
    }
    active_ = i;
    if (MenuRow* r = row(i)) {
      r->active = true;
      r->invalidate();
    }
    // a different row closes the submenu of the previous one
    if (session_->levels.size() > level_ + 1) session_->close_from(level_ + 1);
    if (open_sub) open_submenu();
  }

  void move(int dir) {
    const int n = static_cast<int>(children_.size());
    if (!n) return;
    int i = active_ < 0 ? (dir > 0 ? -1 : n) : active_;
    for (int step = 0; step < n; ++step) {
      i = ((i + dir) % n + n) % n;
      if (row(i)->selectable()) {
        set_active(i);
        return;
      }
    }
  }

  void open_submenu();
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override {
    View::accessibility(info);
    info.role = Role::Menu;
  }

private:
  std::shared_ptr<MenuSession> session_;
  std::size_t level_;
  int active_ = -1;
};

MenuPopup* open_level(const std::shared_ptr<MenuSession>& s, const MenuItems& items, Point at, float avoid_left = -1);
void menu_bar_switch(MenuBarView* bar, int dir);

// ── MenuSession ──────────────────────────────────────────────────────────────

void MenuSession::close_from(std::size_t level) {
  if (!window) return;
  while (levels.size() > level) {
    MenuPopup* p = levels.back();
    window->close_popup(p); // its on_dismiss pops it from `levels`
    if (!levels.empty() && levels.back() == p) levels.pop_back();
  }
}

void MenuSession::activate(const MenuEntry& e) {
  if (e.kind == MenuEntry::Kind::Separator) return;
  MenuEntry copy = e; // the rows die with the popups
  close_all();
  copy.trigger();
}

// ── MenuRow ──────────────────────────────────────────────────────────────────

void MenuRow::hover_changed(bool h) {
  if (!h || separator()) return;
  auto* popup = static_cast<MenuPopup*>(parent());
  popup->set_active(selectable() ? index_ : -1, entry_.kind == MenuEntry::Kind::Submenu && selectable());
}

bool MenuRow::on_event(InputEvent& e) {
  if (separator()) return true;
  if (e.type == InputEvent::MouseDown) return e.button == MouseButton::Left;
  if (e.type == InputEvent::MouseUp && e.button == MouseButton::Left && bounds().contains(e.pos)) {
    auto* popup = static_cast<MenuPopup*>(parent());
    if (entry_.kind == MenuEntry::Kind::Submenu) popup->set_active(index_, true);
    else popup->session().activate(entry_);
    return true;
  }
  return false;
}

bool MenuRow::accessibility_action(A11yAction a, const std::string& arg) {
  if (separator() || !is_enabled()) return false;
  if (a == A11yAction::Press || a == A11yAction::Toggle || a == A11yAction::Expand) {
    auto* popup = static_cast<MenuPopup*>(parent());
    if (entry_.kind == MenuEntry::Kind::Submenu) popup->set_active(index_, true);
    else popup->session().activate(entry_);
    return true;
  }
  return View::accessibility_action(a, arg);
}

// ── MenuPopup ────────────────────────────────────────────────────────────────

void MenuPopup::open_submenu() {
  MenuRow* r = row(active_);
  if (!r || r->entry().kind != MenuEntry::Kind::Submenu || !r->selectable()) return;
  if (session_->levels.size() > level_ + 1) return; // already open
  const Point at = r->to_window({r->frame().w - 2, -kPad});
  open_level(session_, r->entry().children(), at, to_window({0, 0}).x);
}

bool MenuPopup::on_event(InputEvent& e) {
  if (e.type == InputEvent::Text) {
    // mnemonic or first letter
    if (e.text.size() != 1) return true;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(e.text[0])));
    for (int pass = 0; pass < 2; ++pass)
      for (int i = 0; i < static_cast<int>(children_.size()); ++i) {
        MenuRow* r = row(i);
        if (!r->selectable()) continue;
        const std::string l = r->entry().display_label();
        const char first = l.empty() ? 0 : static_cast<char>(std::tolower(static_cast<unsigned char>(l[0])));
        if ((pass == 0 && r->entry().mnemonic() == c) || (pass == 1 && first == c)) {
          set_active(i);
          if (r->entry().kind == MenuEntry::Kind::Submenu) open_submenu();
          else session_->activate(r->entry());
          return true;
        }
      }
    return true;
  }
  if (e.type != InputEvent::KeyDown) return e.is_mouse();
  switch (e.key) {
    case Key::Up: move(-1); return true;
    case Key::Down: move(1); return true;
    case Key::Home: set_active(-1); move(1); return true;
    case Key::End: set_active(-1); move(-1); return true;
    case Key::Enter:
    case Key::Space:
      if (MenuRow* r = row(active_)) {
        if (r->entry().kind == MenuEntry::Kind::Submenu) {
          open_submenu();
          if (session_->levels.size() > level_ + 1) session_->levels.back()->move(1);
        } else if (r->selectable()) {
          session_->activate(r->entry());
        }
      }
      return true;
    case Key::Right:
      if (MenuRow* r = row(active_); r && r->entry().kind == MenuEntry::Kind::Submenu) {
        open_submenu();
        if (session_->levels.size() > level_ + 1) session_->levels.back()->move(1);
        return true;
      }
      if (session_->bar) menu_bar_switch(session_->bar, 1);
      return true;
    case Key::Left:
      if (level_ > 0) {
        MenuPopup* parent_menu = session_->levels[level_ - 1];
        session_->close_from(level_);
        session_->window->set_focus(parent_menu);
        return true;
      }
      if (session_->bar) menu_bar_switch(session_->bar, -1);
      return true;
    case Key::Escape:
      if (level_ > 0) {
        MenuPopup* parent_menu = session_->levels[level_ - 1];
        session_->close_from(level_);
        session_->window->set_focus(parent_menu);
      } else {
        session_->close_all();
      }
      return true;
    case Key::Tab:
      return true;
    default:
      return true; // a menu swallows the keyboard while it is open
  }
}

MenuPopup* open_level(const std::shared_ptr<MenuSession>& s, const MenuItems& items, Point at, float avoid_left) {
  Window* w = s->window;
  if (!w || items.empty()) return nullptr;
  const std::size_t level = s->levels.size();
  MenuPopup popup(s, items, level);
  const Size win = w->size();
  const Size sz = popup.measure(win);
  if (at.x + sz.w > win.w && avoid_left >= 0) at.x = std::max(0.f, avoid_left - sz.w + 2); // flip a submenu to the left
  if (at.y + sz.h > win.h) at.y = level == 0 && at.y - sz.h >= 0 ? at.y - sz.h : std::max(0.f, win.h - sz.h);
  std::weak_ptr<MenuSession> weak = s;
  auto* raw = static_cast<MenuPopup*>(w->open_popup(std::move(popup), at, [weak, level] {
    auto ses = weak.lock();
    if (!ses) return;
    // the deeper menus go with it
    while (ses->levels.size() > level + 1) {
      MenuPopup* deeper = ses->levels.back();
      ses->levels.pop_back();
      if (ses->window) ses->window->close_popup(deeper);
    }
    if (ses->levels.size() > level) ses->levels.resize(level);
    if (level == 0 && !ses->closing) {
      ses->closing = true;
      ses->close_all();
      if (ses->window && !ses->restore_alive.expired()) ses->window->set_focus(ses->restore_focus);
      else if (ses->window && ses->window->focus_view() && dynamic_cast<MenuPopup*>(ses->window->focus_view()))
        ses->window->set_focus(nullptr);
      if (ses->on_closed) ses->on_closed();
    }
  }));
  if (!raw) return nullptr;
  s->levels.push_back(raw);
  w->set_focus(raw);
  return raw;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Menu bar (drawn; used where the platform has no native one for the window)
// ═════════════════════════════════════════════════════════════════════════════
namespace {

class MenuBarView : public ViewT<MenuBarView> {
public:
  explicit MenuBarView(MenuItems items) : items_(std::move(items)) {
    focusable_ = true;
    hover_repaint_ = true;
  }
  const char* type_name() const override { return "MenuBar"; }

  Size measure_content(Size avail) override { return {avail.w, 28}; }

  std::vector<Rect> title_rects() const {
    std::vector<Rect> out;
    float x = 4;
    for (auto& e : items_) {
      const float w = std::ceil(measure_text(e.display_label(), th().font, 0).w) + 20;
      out.push_back({x, 2, w, frame_.h - 4});
      x += w;
    }
    return out;
  }
  int title_at(Point p) const {
    const auto rs = title_rects();
    for (std::size_t i = 0; i < rs.size(); ++i)
      if (rs[i].contains(p)) return static_cast<int>(i);
    return -1;
  }

  void paint(Painter& p) override {
    const Theme& t = th();
    p.fill_rect(bounds(), t.surface);
    p.line({0, frame_.h - 0.5f}, {frame_.w, frame_.h - 0.5f}, t.border, 1);
    const auto rs = title_rects();
    for (std::size_t i = 0; i < rs.size(); ++i) {
      const int ii = static_cast<int>(i);
      const bool open = ii == open_;
      const bool hot = ii == hot_ && (keyboard_ || hovered_);
      if (open) p.fill_rect(rs[i], t.selection, t.radius_small);
      else if (hot) p.fill_rect(rs[i], t.control_hover, t.radius_small);
      const std::string label = items_[i].display_label();
      p.text(rs[i], label, t.font, t.text, Align::Center);
      if (keyboard_ || show_mnemonics_) {
        // underline the mnemonic letter
        const char m = items_[i].mnemonic();
        if (m) {
          const std::size_t pos = [&] {
            for (std::size_t k = 0; k < label.size(); ++k)
              if (std::tolower(static_cast<unsigned char>(label[k])) == m) return k;
            return std::string::npos;
          }();
          if (pos != std::string::npos) {
            const float tw = measure_text(label, t.font, 0).w;
            const float x0 = rs[i].x + (rs[i].w - tw) / 2 + measure_text(label.substr(0, pos), t.font, 0).w;
            const float cw = measure_text(label.substr(pos, 1), t.font, 0).w;
            const float y = rs[i].y + rs[i].h / 2 + line_height(t.font) / 2 - 2;
            p.line({x0, y}, {x0 + cw, y}, t.text, 1);
          }
        }
      }
    }
  }

  void open(int i) {
    if (i < 0 || i >= static_cast<int>(items_.size()) || !window()) return;
    Window* w = window();
    if (session_) {
      auto old = session_;
      old->on_closed = nullptr;
      old->close_all();
    }
    session_ = std::make_shared<MenuSession>();
    session_->window = w;
    session_->bar = this;
    session_->restore_alive = restore_alive_;
    session_->restore_focus = restore_focus_;
    std::weak_ptr<bool> alive = alive_;
    session_->on_closed = [this, alive] {
      if (alive.expired()) return;
      open_ = -1;
      keyboard_ = false;
      session_.reset();
      invalidate();
    };
    open_ = hot_ = i;
    const Rect r = title_rects()[static_cast<std::size_t>(i)];
    auto s = session_;
    MenuPopup* popup = open_level(s, items_[static_cast<std::size_t>(i)].children(), to_window({r.x, frame_.h}));
    if (popup && keyboard_) popup->move(1);
    if (!popup) {
      open_ = -1;
      session_.reset();
    }
    invalidate();
  }

  void switch_to(int dir) {
    const int n = static_cast<int>(items_.size());
    if (!n) return;
    const bool kb = true;
    keyboard_ = kb;
    open((std::max(open_, hot_) + dir + n) % n);
  }

  // Alt / F10: keyboard mode (titles highlighted, arrows move, Enter opens)
  void enter_keyboard_mode() {
    if (!window() || items_.empty()) return;
    View* f = window()->focus_view();
    if (f != this) {
      restore_focus_ = f;
      restore_alive_ = f ? f->alive_token() : std::weak_ptr<bool>();
    }
    keyboard_ = true;
    hot_ = 0;
    window()->set_focus(this);
    invalidate();
  }
  void leave_keyboard_mode() {
    keyboard_ = false;
    hot_ = -1;
    if (window() && window()->focus_view() == this)
      window()->set_focus(restore_alive_.expired() ? nullptr : restore_focus_);
    invalidate();
  }
  bool keyboard_mode() const { return keyboard_; }

  bool open_mnemonic(char c) {
    for (std::size_t i = 0; i < items_.size(); ++i)
      if (items_[i].mnemonic() == c) {
        View* f = window() ? window()->focus_view() : nullptr;
        if (f != this) {
          restore_focus_ = f;
          restore_alive_ = f ? f->alive_token() : std::weak_ptr<bool>();
        }
        keyboard_ = true;
        open(static_cast<int>(i));
        return true;
      }
    return false;
  }

  void on_focus(bool f) override {
    if (!f && keyboard_ && open_ < 0) {
      keyboard_ = false;
      hot_ = -1;
      invalidate();
    }
  }

  bool on_event(InputEvent& e) override {
    switch (e.type) {
      case InputEvent::MouseMove: {
        const int i = title_at(e.pos);
        if (i != hot_) {
          hot_ = i;
          invalidate();
        }
        if (open_ >= 0 && i >= 0 && i != open_) open(i); // slide between open menus
        return true;
      }
      case InputEvent::MouseDown: {
        const int i = title_at(e.pos);
        if (i < 0) return true;
        if (open_ == i && session_) {
          session_->close_all();
        } else {
          View* f = window() ? window()->focus_view() : nullptr;
          if (f != this && !dynamic_cast<MenuPopup*>(f)) {
            restore_focus_ = f;
            restore_alive_ = f ? f->alive_token() : std::weak_ptr<bool>();
          }
          keyboard_ = false;
          open(i);
        }
        return true;
      }
      case InputEvent::KeyDown:
        if (!keyboard_) return false;
        switch (e.key) {
          case Key::Left: hot_ = (hot_ - 1 + static_cast<int>(items_.size())) % static_cast<int>(items_.size()); invalidate(); return true;
          case Key::Right: hot_ = (hot_ + 1) % static_cast<int>(items_.size()); invalidate(); return true;
          case Key::Down:
          case Key::Enter:
          case Key::Space: open(hot_); return true;
          case Key::Escape:
          case Key::Alt:
          case Key::F10: leave_keyboard_mode(); return true;
          default: return true;
        }
      case InputEvent::Text:
        if (keyboard_ && e.text.size() == 1)
          open_mnemonic(static_cast<char>(std::tolower(static_cast<unsigned char>(e.text[0]))));
        return keyboard_;
      default:
        return false;
    }
  }

  void hover_changed(bool h) override {
    if (!h && !keyboard_) {
      hot_ = -1;
      invalidate();
    }
  }

  void set_items(MenuItems items) {
    items_ = std::move(items);
    invalidate();
  }
  void accessibility(A11yInfo& info) const override {
    View::accessibility(info);
    info.role = Role::MenuBar;
    std::string names;
    for (auto& e : items_) names += (names.empty() ? "" : ", ") + e.display_label();
    info.name = names;
    info.value = hot_ >= 0 && hot_ < static_cast<int>(items_.size()) ? items_[static_cast<std::size_t>(hot_)].display_label() : "";
    info.expanded = open_ >= 0 ? 1 : 0;
  }
  bool accessibility_action(A11yAction a, const std::string& arg) override {
    if (a == A11yAction::SetValue) { // open a menu by its title
      for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].display_label() == arg) {
          keyboard_ = true;
          open(static_cast<int>(i));
          return true;
        }
      return false;
    }
    if (a == A11yAction::Press || a == A11yAction::Expand) {
      keyboard_ = true;
      open(std::max(hot_, 0));
      return true;
    }
    if (a == A11yAction::Collapse && session_) {
      session_->close_all();
      return true;
    }
    return View::accessibility_action(a, arg);
  }
  const MenuItems& items() const { return items_; }
  bool show_mnemonics_ = false;

private:
  MenuItems items_;
  int open_ = -1, hot_ = -1;
  bool keyboard_ = false;
  std::shared_ptr<MenuSession> session_;
  View* restore_focus_ = nullptr;
  std::weak_ptr<bool> restore_alive_;
};

void menu_bar_switch(MenuBarView* bar, int dir) { bar->switch_to(dir); }

} // namespace

namespace detail {

std::unique_ptr<View> make_menu_bar(MenuItems items) { return std::make_unique<MenuBarView>(std::move(items)); }

// Keyboard handling for the menu bar that has to happen at window level
// (before shortcuts and focus): Alt alone, F10, Alt+mnemonic.
bool menu_bar_key(View* bar_view, const InputEvent& e, bool alt_alone_released) {
  auto* bar = static_cast<MenuBarView*>(bar_view);
  if (!bar) return false;
  if (alt_alone_released) {
    if (bar->keyboard_mode()) bar->leave_keyboard_mode();
    else bar->enter_keyboard_mode();
    return true;
  }
  if (e.type != InputEvent::KeyDown) return false;
  if (e.key == Key::F10 && (e.mods & ~Mod::Shift) == 0 && !(e.mods & Mod::Shift)) {
    if (bar->keyboard_mode()) bar->leave_keyboard_mode();
    else bar->enter_keyboard_mode();
    return true;
  }
  if (e.mods == Mod::Alt && e.key >= Key::A && e.key <= Key::Z) {
    const char c = static_cast<char>('a' + (static_cast<int>(e.key) - static_cast<int>(Key::A)));
    return bar->open_mnemonic(c);
  }
  return false;
}

View* open_menu(Window& w, MenuItems items, Point at, std::function<void()> on_closed) {
  auto s = std::make_shared<MenuSession>();
  s->window = &w;
  if (View* f = w.focus_view()) {
    s->restore_focus = f;
    s->restore_alive = f->alive_token();
  }
  s->on_closed = std::move(on_closed);
  return open_level(s, items, at);
}

} // namespace detail
} // namespace pulseui
