#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>

#include "../platform/platform.hpp"

namespace pulseui {

namespace detail {
std::unique_ptr<View> make_menu_bar(MenuItems items);
bool menu_bar_key(View* bar, const InputEvent& e, bool alt_alone_released);
View* open_menu(Window& w, MenuItems items, Point at, std::function<void()> on_closed);
} // namespace detail

// ═════════════════════════════════════════════════════════════════════════════
// App
// ═════════════════════════════════════════════════════════════════════════════
namespace {
App* g_app = nullptr;
std::vector<Window*> g_windows;

struct UiExecutor final : pulse::executor {
  void post(std::function<void()> f) override { platform::post(std::move(f)); }
};

pulse::subscription make_timer(std::chrono::milliseconds d, bool repeat, std::function<void()> fn) {
  auto alive = std::make_shared<std::atomic<bool>>(true);
  auto id = std::make_shared<std::atomic<std::uint64_t>>(0);
  auto start = [alive, id, d, repeat, fn] {
    if (!alive->load()) return;
    id->store(platform::start_timer(static_cast<int>(d.count()), repeat, [alive, fn] {
      if (alive->load()) fn();
    }));
  };
  if (platform::is_main_thread()) start();
  else platform::post(start);
  return pulse::subscription([alive, id] {
    alive->store(false);
    auto stop = [id] {
      if (auto v = id->load()) platform::stop_timer(v);
    };
    if (platform::is_main_thread()) stop();
    else platform::post(stop);
  });
}
} // namespace

App::App() {
  platform::init();
  g_app = this;
  if (const char* t = std::getenv("PULSEUI_THEME")) {
    const std::string name = t;
    if (name == "light") theme() = Theme::light();
    else if (name == "dark") theme() = Theme::dark();
  }
}

App::App(int argc, char** argv) : App() {
  for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
}

App::~App() {
  if (g_app == this) g_app = nullptr;
}

int App::run() {
  for (Window* w : g_windows)
    if (!w->shown_ && !w->hide_requested_ && w->native_) w->show();
  // PULSEUI_SCREENSHOT=out.png renders the first window to a PNG and exits
  // (handy for docs and visual regression checks).
  if (const char* shot = std::getenv("PULSEUI_SCREENSHOT")) {
    std::string path = shot;
    static pulse::subscription t;
    t = after(std::chrono::milliseconds(500), [path] {
      if (!g_windows.empty()) g_windows.front()->screenshot(path);
      platform::quit(0);
    });
  }
  return platform::run();
}

void App::quit(int code) { platform::quit(code); }
void App::process_events(std::chrono::milliseconds d) { platform::process_events(static_cast<int>(d.count())); }
App* App::instance() { return g_app; }
void App::post(std::function<void()> fn) { platform::post(std::move(fn)); }
bool App::is_ui_thread() { return platform::is_main_thread(); }

pulse::executor& App::ui() {
  static UiExecutor ex;
  return ex;
}

pulse::subscription App::after(std::chrono::milliseconds d, std::function<void()> fn) {
  return make_timer(d, false, std::move(fn));
}
pulse::subscription App::every(std::chrono::milliseconds d, std::function<void()> fn) {
  return make_timer(d, true, std::move(fn));
}

void App::set_clipboard(const std::string& text) { platform::set_clipboard(text); }
std::string App::clipboard() { return platform::get_clipboard(); }

int run(std::string title, float width, float height, Node root) {
  App app;
  Window win(std::move(title), width, height);
  win.content(std::move(root));
  win.show();
  return app.run();
}

// ═════════════════════════════════════════════════════════════════════════════
// Window
// ═════════════════════════════════════════════════════════════════════════════
const std::vector<Window*>& Window::all() { return g_windows; }

namespace {
bool g_headless = false;
}
void Window::set_headless(bool on) { g_headless = on; }
bool Window::headless() { return g_headless; }

Window::Window(Bind<std::string> title, float width, float height) {
  platform::init();
  platform::WindowConfig cfg;
  cfg.title = title.value().value_or("PulseUI");
  cfg.width = width;
  cfg.height = height;
  native_ = platform::create_window(this, cfg);
  size_ = native_->size();
  g_windows.push_back(this);

  if (title.is_dynamic()) {
    std::weak_ptr<bool> alive = alive_;
    title_sub_ = title.attach([this, alive](const std::string& t) {
      platform::post([this, alive, t] {
        if (!alive.expired()) set_title(t);
      });
    });
  }
  std::weak_ptr<bool> alive = alive_;
  theme_sub_ = theme().changed.connect([this, alive](const Theme&) {
    if (!alive.expired()) request_layout();
  }).scoped();
}

Window::~Window() {
  *alive_ = false;
  tooltip_timer_.reset();
  popups_.clear();
  if (menu_bar_) menu_bar_->unmount();
  menu_bar_.reset();
  menu_subs_.clear();
  if (root_) root_->unmount();
  root_.reset();
  graveyard_.clear();
  native_.reset();
  g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), this), g_windows.end());
}

Window& Window::content(Node root) {
  if (root_) root_->unmount();
  root_ = root.release();
  if (root_) {
    // The root fills the window and gets the theme background.
    root_->mount(this);
  }
  request_layout();
  return *this;
}

void Window::show() {
  if (!native_) return;
  shown_ = true;
  hide_requested_ = false;
  if (g_headless) return;
  native_->show();
}

void Window::activate() {
  if (native_ && !g_headless) native_->activate();
}

void Window::show_modal(Window& parent) {
  if (!native_) return;
  if (parent.native_) native_->set_modal_parent(parent.native_.get());
  show();
}

void Window::run_modal(Window* parent) {
  if (parent) show_modal(*parent);
  else show();
  std::weak_ptr<bool> alive = alive_;
  platform::run_modal_loop([this, alive] { return alive.expired() || destroyed_ || !native_; });
}

void Window::set_resizable(bool r) {
  if (native_) native_->set_resizable(r);
}

void Window::fit_content(float max_width) {
  float top = 0;
  if (menu_bar_) top = menu_bar_->measure({max_width, 10000}).h;
  if (!root_) return;
  Size s = root_->measure({max_width, 10000});
  s.w = std::min(std::ceil(s.w), max_width);
  // a second pass at the final width (wrapped text gets taller when narrower)
  s.h = root_->measure({s.w, 10000}).h;
  resize(std::max(s.w, 120.f), std::ceil(s.h + top));
  size_ = native_ ? native_->size() : Size{s.w, s.h + top};
  request_layout();
}

void Window::hide() {
  hide_requested_ = true;
  if (native_) native_->hide();
}

void Window::close() {
  if (native_ && handle_close_request()) native_->destroy();
}

void Window::set_title(const std::string& t) {
  if (native_) native_->set_title(t);
}

void Window::resize(float w, float h) {
  if (native_) native_->set_size(w, h);
}

void Window::set_min_size(float w, float h) {
  if (native_) native_->set_min_size(w, h);
}

float Window::scale() const { return native_ ? native_->scale() : 1.f; }
void* Window::native_handle() const { return native_ ? native_->handle() : nullptr; }

void Window::request_paint() {
  if (native_) native_->invalidate();
}

void Window::request_layout() {
  layout_dirty_ = true;
  request_paint();
}

Window& Window::shortcut(std::string_view keys, std::function<void()> fn) {
  shortcuts_.push_back({Shortcut::parse(keys), std::move(fn)});
  return *this;
}

// ── layout & paint ───────────────────────────────────────────────────────────

void Window::update_layout() { ensure_layout(); }

void Window::ensure_layout() {
  if (!layout_dirty_) return;
  layout_dirty_ = false;
  float top = 0;
  if (menu_bar_) {
    top = std::min(size_.h, menu_bar_->measure({size_.w, size_.h}).h);
    menu_bar_->set_frame({0, 0, size_.w, top});
    menu_bar_->layout();
  }
  if (root_) {
    root_->set_frame({0, top, size_.w, std::max(0.f, size_.h - top)});
    root_->layout();
  }
  float stack_bottom = size_.h, stack_top = top; // BottomRight / TopCenter popups stack up
  for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
    Popup& p = *it;
    const PopupOptions& o = p.options;
    Size s = p.view->measure({size_.w - 2 * (o.place == PopupOptions::Place::At ? 0 : o.margin), size_.h});
    s.w = std::min(s.w, size_.w);
    s.h = std::min(s.h, size_.h);
    float x = 0, y = 0;
    switch (o.place) {
      case PopupOptions::Place::At:
        x = std::clamp(p.at.x, 0.f, std::max(0.f, size_.w - s.w));
        y = p.at.y;
        if (y + s.h > size_.h) {
          const float above = o.anchor.w > 0 ? o.anchor.y - 4 - s.h : -1;
          y = above >= 0 ? above : std::max(0.f, size_.h - s.h);
        }
        break;
      case PopupOptions::Place::Center:
        x = std::max(0.f, (size_.w - s.w) / 2);
        y = std::max(top, top + (size_.h - top - s.h) / 3);
        break;
      case PopupOptions::Place::BottomRight:
        x = std::max(0.f, size_.w - s.w - o.margin);
        y = std::max(0.f, stack_bottom - s.h - o.margin);
        stack_bottom = y + o.margin - 8;
        break;
      case PopupOptions::Place::TopCenter:
        x = std::max(0.f, (size_.w - s.w) / 2);
        y = stack_top + o.margin;
        stack_top = y + s.h - o.margin + 8;
        break;
    }
    p.view->set_frame({x, y, s.w, s.h});
    p.view->layout();
  }
}

void Window::handle_paint(Painter& p) {
  ensure_layout();
  const Theme& t = th();
  p.fill_rect({0, 0, size_.w, size_.h}, t.window);
  if (root_) root_->paint_tree(p);
  if (menu_bar_) menu_bar_->paint_tree(p);
  if (drop_target_) {
    const Point tl = drop_target_->to_window({0, 0});
    const Rect r{tl.x, tl.y, drop_target_->frame().w, drop_target_->frame().h};
    const float rad = std::max(drop_target_->style().radius, t.radius_small);
    p.fill_rect(r, t.accent.with_alpha(0.12f), rad);
    p.stroke_rect(r.inset(1), t.accent, 2, rad);
  }
  for (auto& pop : popups_) {
    const Rect f = pop.view->frame();
    const float op = pop.view->style().opacity;
    if (pop.options.backdrop) p.fill_rect({0, 0, size_.w, size_.h}, Color::black().with_alpha(0.35f * op));
    if (pop.options.shadow && pop.view->is_visible())
      for (int i = 3; i >= 1; --i)
        p.fill_rect(f.outset(static_cast<float>(i) * 2).translated(0, 3), t.shadow.with_alpha(t.shadow.a * 0.25f * op),
                    t.radius + i * 2);
    pop.view->paint_tree(p);
  }
  if (tooltip_view_ && !tooltip_text_.empty()) {
    const Font f = t.font_small;
    Size ts = measure_text(tooltip_text_, f, 320);
    Rect r{tooltip_pos_.x + 12, tooltip_pos_.y + 18, ts.w + 16, ts.h + 10};
    if (r.right() > size_.w) r.x = std::max(0.f, size_.w - r.w - 4);
    if (r.bottom() > size_.h) r.y = tooltip_pos_.y - r.h - 6;
    p.fill_rect(r, t.tooltip, t.radius_small);
    p.stroke_rect(r, t.border, 1, t.radius_small);
    p.text(r.inset(Insets(8, 5)), tooltip_text_, f, Color::hex(0xF0F2F5), Align::LeftCenter, true);
  }
}

void Window::handle_resize(Size s) {
  if (s == size_) return;
  size_ = s;
  request_layout();
  resized.emit(s);
}

// ── input ────────────────────────────────────────────────────────────────────

View* Window::hit_in(View* v, Point local) {
  if (!v->is_visible() || !v->hit_test(local)) return nullptr;
  if (v->hit_test_children(local)) {
    const auto& ch = v->children();
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) {
      View* c = it->get();
      if (View* h = hit_in(c, local - c->frame().pos())) return h;
    }
  }
  return v;
}

View* Window::hit(Point p) {
  for (auto it = popups_.rbegin(); it != popups_.rend(); ++it) {
    if (it->view->is_visible() && it->view->frame().contains(p)) return hit_in(it->view.get(), p - it->view->frame().pos());
    if (it->options.backdrop) return it->view.get(); // a modal sheet blocks what is behind it
  }
  if (menu_bar_ && menu_bar_->frame().contains(p)) return hit_in(menu_bar_.get(), p - menu_bar_->frame().pos());
  return root_ ? hit_in(root_.get(), p - root_->frame().pos()) : nullptr;
}

View* Window::bubble(View* v, InputEvent& e, bool capture) {
  for (View* x = v; x; x = x->parent()) {
    if (!x->is_enabled()) continue;
    e.pos = x->from_window(e.window_pos);
    std::weak_ptr<bool> alive = x->alive_;
    if (capture) capture_ = x;
    if (x->events_) x->events_->emit(e);
    if (alive.expired()) return nullptr;
    const bool handled = x->on_event(e);
    if (alive.expired()) return nullptr;
    if (handled) return x;
    if (capture && capture_ == x) capture_ = nullptr;
  }
  return nullptr;
}

void Window::update_hover(View* v) {
  if (v == hover_) return;
  std::vector<View*> now;
  for (View* x = v; x; x = x->parent()) now.push_back(x);
  for (View* x = hover_; x; x = x->parent())
    if (std::find(now.begin(), now.end(), x) == now.end()) x->set_hovered(false);
  for (View* x : now) x->set_hovered(true);
  hover_ = v;

  hide_tooltip();
  for (View* x = v; x; x = x->parent()) {
    if (!x->tooltip().empty()) {
      show_tooltip(x);
      break;
    }
  }
}

void Window::update_cursor(View* v) {
  if (!native_) return;
  Cursor c = Cursor::Arrow;
  for (View* x = capture_ ? capture_ : v; x; x = x->parent()) {
    if (!x->is_enabled()) continue;
    if (x->cursor() != Cursor::Arrow) {
      c = x->cursor();
      break;
    }
  }
  native_->set_cursor(c);
}

void Window::show_tooltip(View* v) {
  std::weak_ptr<bool> alive = alive_;
  tooltip_timer_ = App::after(std::chrono::milliseconds(550), [this, alive, v] {
    if (alive.expired() || hover_ == nullptr) return;
    for (View* x = hover_; x; x = x->parent())
      if (x == v) {
        tooltip_view_ = v;
        tooltip_text_ = v->tooltip();
        request_paint();
        return;
      }
  });
}

void Window::hide_tooltip() {
  tooltip_timer_.reset();
  if (tooltip_view_) {
    tooltip_view_ = nullptr;
    request_paint();
  }
}

void Window::handle_input(InputEvent e) {
  if (e.is_mouse() && e.type != InputEvent::MouseLeave && e.window_pos == Point{} && e.pos != Point{})
    e.window_pos = e.pos;
  input_.on_next(e);
  ensure_layout();

  switch (e.type) {
    case InputEvent::MouseMove: {
      tooltip_pos_ = e.window_pos;
      if (drag_candidate_) {
        const Point d = e.window_pos - drag_from_;
        if (d.x * d.x + d.y * d.y > 25) { // moved 5 px with the button down: start dragging
          View* src = drag_candidate_;
          drag_candidate_ = nullptr;
          DragData data = src->extras_if()->drag_source();
          if (!data.empty()) {
            std::weak_ptr<bool> alive = alive_;
            start_drag(data);
            if (alive.expired()) return;
            // the system drag swallowed the button release: finish the press here
            InputEvent up = e;
            up.type = InputEvent::MouseUp;
            up.button = MouseButton::Left;
            handle_input(up);
            return;
          }
        }
      }
      if (capture_) {
        bubble(capture_, e, false);
      } else {
        View* v = hit(e.window_pos);
        update_hover(v);
        if (v) {
          InputEvent m = e;
          bubble(v, m, false);
        }
      }
      update_cursor(hover_);
      break;
    }
    case InputEvent::MouseLeave:
      if (!capture_) update_hover(nullptr);
      hide_tooltip();
      break;
    case InputEvent::MouseDown: {
      alt_alone_ = false;
      hide_tooltip();
      View* v = hit(e.window_pos);
      drag_candidate_ = nullptr;
      if (e.button == MouseButton::Left)
        for (View* x = v; x; x = x->parent())
          if (x->extras_if() && x->extras_if()->drag_source && x->is_enabled()) {
            drag_candidate_ = x;
            drag_from_ = e.window_pos;
            break;
          }
      if (!popups_.empty()) {
        bool inside = false;
        for (View* x = v; x; x = x->parent())
          for (auto& p : popups_)
            if (p.view.get() == x) inside = true;
        if (!inside) {
          // close the dismissible popups (menus, drop-downs); toasts and sheets stay
          std::vector<View*> doomed;
          for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
            if (it->options.dismiss_on_outside) doomed.push_back(it->view.get());
          for (View* d : doomed) close_popup(d);
          request_layout();
          if (!doomed.empty()) break;
        }
      }
      View* focus_target = nullptr;
      bool in_popup = false;
      for (View* x = v; x; x = x->parent()) {
        if (!focus_target && x->focusable() && x->is_enabled()) focus_target = x;
        for (auto& p : popups_)
          if (p.view.get() == x) in_popup = true;
      }
      // a click on a popup without focusable views (suggestions, toasts) keeps the focus
      if (focus_target || !in_popup) set_focus(focus_target);
      if (v) {
        View* h = bubble(v, e, true);
        if (h) {
          capture_ = h;
          h->set_pressed(true);
          if (native_) native_->capture_mouse(true);
        }
      }
      break;
    }
    case InputEvent::MouseUp: {
      drag_candidate_ = nullptr;
      if (capture_) {
        View* c = capture_;
        std::weak_ptr<bool> alive = c->alive_;
        bubble(c, e, false);
        if (!alive.expired()) c->set_pressed(false);
        capture_ = nullptr;
        if (native_) native_->capture_mouse(false);
        View* v = hit(e.window_pos);
        update_hover(v);
        update_cursor(v);
      } else if (View* v = hit(e.window_pos)) {
        bubble(v, e, false);
      }
      if (e.button == MouseButton::Right) open_context_menu(hit(e.window_pos), e.window_pos);
      break;
    }
    case InputEvent::Wheel:
      hide_tooltip();
      if (View* v = hit(e.window_pos)) bubble(v, e, false);
      break;
    case InputEvent::KeyDown: {
      alt_alone_ = e.key == Key::Alt && (!e.repeat || alt_alone_);
      if (menu_bar_ && detail::menu_bar_key(menu_bar_.get(), e, false)) break;
      if ((e.key == Key::Menu || (e.key == Key::F10 && e.mods == Mod::Shift)) && focus_) {
        const Point at = focus_->to_window({8, std::min(focus_->frame().h, 24.f)});
        if (open_context_menu(focus_, at)) break;
      }
      if (focus_ && bubble(focus_, e, false)) break;
      if (e.key == Key::Escape && !popups_.empty()) {
        View* top_popup = nullptr;
        for (auto it = popups_.rbegin(); it != popups_.rend() && !top_popup; ++it)
          if (it->options.dismiss_on_outside || it->options.backdrop) top_popup = it->view.get();
        if (top_popup) {
          close_popup(top_popup);
          break;
        }
      }
      bool done = false;
      for (auto& sc : shortcuts_) {
        if (sc.sc.matches(e)) {
          auto fn = sc.fn;
          fn();
          done = true;
          break;
        }
      }
      if (done) break;
      for (auto& sc : menu_shortcuts_) {
        if (sc.sc.matches(e)) {
          auto fn = sc.fn;
          fn();
          done = true;
          break;
        }
      }
      if (done) break;
      if (e.key == Key::Tab) focus_next((e.mods & Mod::Shift) != 0);
      break;
    }
    case InputEvent::KeyUp:
      if (e.key == Key::Alt && alt_alone_ && menu_bar_) {
        alt_alone_ = false;
        detail::menu_bar_key(menu_bar_.get(), e, true);
        break;
      }
      if (focus_) bubble(focus_, e, false);
      break;
    case InputEvent::Text:
      if (focus_) bubble(focus_, e, false);
      break;
    default:
      break;
  }
}

void Window::send(InputEvent e) {
  if (e.window_pos == Point{}) e.window_pos = e.pos;
  handle_input(std::move(e));
}

// ── focus ────────────────────────────────────────────────────────────────────

void Window::set_focus(View* v) {
  if (v == focus_) return;
  View* old = focus_;
  focus_ = v;
  if (old) old->on_focus(false);
  if (v) v->on_focus(true);
  if (native_ && focus_ == v) native_->a11y_focus_changed(v);
  request_paint();
}

void Window::collect_focusable(View* v, std::vector<View*>& out) {
  if (!v->is_visible() || !v->is_enabled()) return;
  if (v->focusable()) out.push_back(v);
  for (auto& c : v->children()) collect_focusable(c.get(), out);
}

void Window::focus_next(bool backwards) {
  std::vector<View*> list;
  if (root_) collect_focusable(root_.get(), list);
  if (list.empty()) return;
  auto it = std::find(list.begin(), list.end(), focus_);
  std::size_t i = 0;
  if (it != list.end()) {
    const std::size_t cur = static_cast<std::size_t>(it - list.begin());
    i = backwards ? (cur + list.size() - 1) % list.size() : (cur + 1) % list.size();
  } else if (backwards) {
    i = list.size() - 1;
  }
  set_focus(list[i]);
}

void Window::view_detached(View* v) {
  if (hover_ == v) hover_ = nullptr;
  if (capture_ == v) {
    capture_ = nullptr;
    if (native_) native_->capture_mouse(false);
  }
  if (focus_ == v) focus_ = nullptr;
  if (tooltip_view_ == v) tooltip_view_ = nullptr;
  if (drop_target_ == v) drop_target_ = nullptr;
  if (drag_candidate_ == v) drag_candidate_ = nullptr;
}

// ── popups ───────────────────────────────────────────────────────────────────

View* Window::open_popup(Node popup, Point at, std::function<void()> on_dismiss) {
  return open_popup(std::move(popup), PopupOptions{}, std::move(on_dismiss), at);
}

View* Window::open_popup(Node popup, PopupOptions options, std::function<void()> on_dismiss, Point at) {
  auto v = popup.release();
  if (!v) return nullptr;
  View* raw = v.get();
  popups_.push_back({std::move(v), std::move(on_dismiss), at, options});
  raw->mount(this);
  request_layout();
  return raw;
}

View* Window::open_dropdown(Node popup, View& anchor, std::function<void()> on_dismiss) {
  PopupOptions o;
  const Point tl = anchor.to_window({0, 0});
  o.anchor = {tl.x, tl.y, anchor.frame().w, anchor.frame().h};
  if (popup && popup->style().min_width < o.anchor.w) popup->style().min_width = o.anchor.w;
  return open_popup(std::move(popup), o, std::move(on_dismiss), {tl.x, tl.y + anchor.frame().h + 4});
}

View* Window::present(Node sheet, std::function<void()> on_dismiss) {
  PopupOptions o;
  o.place = PopupOptions::Place::Center;
  o.backdrop = true;
  o.dismiss_on_outside = false;
  View* v = open_popup(std::move(sheet), o, std::move(on_dismiss));
  if (v) {
    // focus the first focusable view of the sheet
    std::vector<View*> list;
    collect_focusable(v, list);
    set_focus(list.empty() ? nullptr : list.front());
  }
  return v;
}

std::vector<View*> Window::popups() const {
  std::vector<View*> out;
  for (auto& p : popups_) out.push_back(p.view.get());
  return out;
}

void Window::close_popup(View* popup) {
  auto it = std::find_if(popups_.begin(), popups_.end(),
                         [popup](const Popup& p) { return p.view.get() == popup; });
  if (it == popups_.end()) return;
  Popup p = std::move(*it);
  popups_.erase(it);
  p.view->unmount();
  graveyard_.push_back(std::move(p.view)); // may be inside its own event handler
  std::weak_ptr<bool> alive = alive_;
  platform::post([this, alive] {
    if (!alive.expired()) graveyard_.clear();
  });
  if (p.on_dismiss) p.on_dismiss();
  request_layout();
}

// ── drag and drop ────────────────────────────────────────────────────────────

bool Window::handle_drag(DragPhase phase, Point pos, const DragData& data) {
  ensure_layout();
  View* target = nullptr;
  if (phase != DragPhase::Leave)
    for (View* x = hit(pos); x; x = x->parent())
      if (x->is_enabled() && x->accepts_drop(data)) {
        target = x;
        break;
      }
  const bool window_takes = !data.files.empty() && static_cast<bool>(on_drop_files);
  if (phase == DragPhase::Drop || phase == DragPhase::Leave) {
    if (drop_target_) request_paint();
    drop_target_ = nullptr;
  } else if (target != drop_target_) {
    drop_target_ = target;
    request_paint();
  }
  if (phase != DragPhase::Drop) return target != nullptr || (phase != DragPhase::Leave && window_takes);
  if (target && target->deliver_drop(data)) return true;
  if (window_takes) {
    auto fn = on_drop_files;
    fn(data.files);
    return true;
  }
  return false;
}

bool Window::start_drag(const DragData& data) {
  last_drag_ = data;
  if (headless() || !native_) return false;
  return platform::start_drag(native_.get(), data);
}

// ── menus ────────────────────────────────────────────────────────────────────

namespace {
void collect_menu_shortcuts(const MenuItems& items, std::vector<std::pair<Shortcut, MenuEntry>>& out) {
  for (auto& e : items) {
    if (e.kind == MenuEntry::Kind::Submenu) collect_menu_shortcuts(e.items, out);
    else if (e.kind == MenuEntry::Kind::Action && !e.shortcut_.empty()) out.push_back({Shortcut::parse(e.shortcut_), e});
  }
}
} // namespace

Window& Window::menu(MenuItems items) {
  menu_items_ = std::move(items);
  menu_shortcuts_.clear();
  menu_subs_.clear();
  std::vector<std::pair<Shortcut, MenuEntry>> scs;
  collect_menu_shortcuts(menu_items_, scs);
  for (auto& [sc, entry] : scs) {
    auto enabled = std::make_shared<bool>(true);
    if (!entry.enabled_.empty())
      menu_subs_.push_back(entry.enabled_.attach([enabled](const bool& b) { *enabled = b; }));
    MenuEntry e = entry;
    menu_shortcuts_.push_back({sc, [enabled, e] {
                                 if (*enabled) e.trigger();
                               }});
  }
  if (menu_bar_) {
    menu_bar_->unmount();
    graveyard_.push_back(std::move(menu_bar_));
  }
  const bool native = native_ && platform::set_native_menu(native_.get(), menu_items_);
  if (!native && !menu_items_.empty()) {
    menu_bar_ = detail::make_menu_bar(menu_items_);
    menu_bar_->mount(this);
  }
  request_layout();
  return *this;
}

View* Window::popup_menu(MenuItems items, Point at, std::function<void()> on_closed) {
  return detail::open_menu(*this, std::move(items), at, std::move(on_closed));
}

bool Window::open_context_menu(View* v, Point window_pos) {
  for (View* x = v; x; x = x->parent()) {
    if (!x->has_context_menu() || !x->is_enabled()) continue;
    MenuItems items = x->extras_if()->context_menu();
    if (items.empty()) return false;
    return popup_menu(std::move(items), window_pos) != nullptr;
  }
  return false;
}

// ── lifecycle ────────────────────────────────────────────────────────────────

bool Window::handle_close_request() { return !on_close || on_close(); }

void Window::handle_destroyed() {
  destroyed_ = true;
  closed.emit();
  std::weak_ptr<bool> alive = alive_;
  platform::post([this, alive] {
    if (!alive.expired()) native_.reset();
  });
  if (g_app && g_app->quit_on_last_window_closed) {
    // only windows the user can see keep the app alive: a window hidden with
    // hide() (kept to be shown again later) must not leave an invisible process
    bool any = false;
    for (Window* w : g_windows)
      if (w != this && w->native_ && !w->destroyed_ && !w->hide_requested_) any = true;
    if (!any) platform::quit(0);
  }
}

// ── screenshots ──────────────────────────────────────────────────────────────

Image Window::grab() {
  ensure_layout();
  const float s = scale();
  std::vector<std::uint8_t> rgba;
  if (!platform::render_offscreen(static_cast<int>(size_.w), static_cast<int>(size_.h), s,
                                  [this](Painter& p) { handle_paint(p); }, rgba))
    return {};
  const int pw = static_cast<int>(static_cast<int>(size_.w) * s + 0.5f);
  const int ph = static_cast<int>(static_cast<int>(size_.h) * s + 0.5f);
  if (rgba.size() != static_cast<std::size_t>(pw) * ph * 4) return {};
  return Image::from_rgba(pw, ph, rgba.data());
}

bool Window::screenshot(const std::string& png_path) { return grab().save_png(png_path); }

} // namespace pulseui

// ═════════════════════════════════════════════════════════════════════════════
// dialogs
// ═════════════════════════════════════════════════════════════════════════════
#include <pulseui/ui/dialogs.hpp>

namespace pulseui::dialogs {

namespace {
std::vector<platform::FileFilter> convert(const std::vector<Filter>& f) {
  std::vector<platform::FileFilter> out;
  for (auto& x : f) out.push_back({x.name, x.patterns});
  return out;
}
platform::NativeWindow* native_of(Window* w) { return w ? w->native_window() : nullptr; }
} // namespace

std::optional<std::string> open_file(Window* parent, const std::string& title, const std::vector<Filter>& filters) {
  return platform::open_file_dialog(native_of(parent), title, convert(filters));
}

std::optional<std::string> save_file(Window* parent, const std::string& title, const std::vector<Filter>& filters,
                                     const std::string& default_name) {
  return platform::save_file_dialog(native_of(parent), title, convert(filters), default_name);
}

} // namespace pulseui::dialogs
