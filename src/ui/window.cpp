#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>

#include "../platform/platform.hpp"

namespace pulseui {

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
  native_->show();
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
  if (root_) {
    root_->set_frame({0, 0, size_.w, size_.h});
    root_->layout();
  }
  for (auto& p : popups_) {
    Size s = p.view->measure({size_.w, size_.h});
    s.w = std::min(s.w, size_.w);
    s.h = std::min(s.h, size_.h);
    float x = std::clamp(p.at.x, 0.f, std::max(0.f, size_.w - s.w));
    float y = p.at.y;
    if (y + s.h > size_.h) y = std::max(0.f, size_.h - s.h);
    p.view->set_frame({x, y, s.w, s.h});
    p.view->layout();
  }
}

void Window::handle_paint(Painter& p) {
  ensure_layout();
  const Theme& t = th();
  p.fill_rect({0, 0, size_.w, size_.h}, t.window);
  if (root_) root_->paint_tree(p);
  for (auto& pop : popups_) {
    const Rect f = pop.view->frame();
    for (int i = 3; i >= 1; --i)
      p.fill_rect(f.outset(static_cast<float>(i) * 2).translated(0, 3), t.shadow.with_alpha(t.shadow.a * 0.25f), t.radius + i * 2);
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
  for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
    if (it->view->frame().contains(p)) return hit_in(it->view.get(), p - it->view->frame().pos());
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
      hide_tooltip();
      View* v = hit(e.window_pos);
      if (!popups_.empty()) {
        bool inside = false;
        for (View* x = v; x; x = x->parent())
          for (auto& p : popups_)
            if (p.view.get() == x) inside = true;
        if (!inside) {
          while (!popups_.empty()) close_popup(popups_.back().view.get());
          request_layout();
          break;
        }
      }
      View* focus_target = nullptr;
      for (View* x = v; x; x = x->parent())
        if (x->focusable() && x->is_enabled()) {
          focus_target = x;
          break;
        }
      set_focus(focus_target);
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
      break;
    }
    case InputEvent::Wheel:
      hide_tooltip();
      if (View* v = hit(e.window_pos)) bubble(v, e, false);
      break;
    case InputEvent::KeyDown: {
      if (focus_ && bubble(focus_, e, false)) break;
      if (e.key == Key::Escape && !popups_.empty()) {
        close_popup(popups_.back().view.get());
        break;
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
      if (e.key == Key::Tab) focus_next((e.mods & Mod::Shift) != 0);
      break;
    }
    case InputEvent::KeyUp:
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
}

// ── popups ───────────────────────────────────────────────────────────────────

View* Window::open_popup(Node popup, Point at, std::function<void()> on_dismiss) {
  auto v = popup.release();
  if (!v) return nullptr;
  View* raw = v.get();
  popups_.push_back({std::move(v), std::move(on_dismiss), at});
  raw->mount(this);
  request_layout();
  return raw;
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
    bool any = false;
    for (Window* w : g_windows)
      if (w != this && w->native_ && !w->destroyed_) any = true;
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
