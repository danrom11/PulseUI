#include <pulseui/testing.hpp>

#include <pulseui/ui/code_editor.hpp>
#include <pulseui/ui/widgets.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace pulseui::testing {

ImageDiff compare(const Image& expected, const Image& actual, int tolerance) {
  ImageDiff d;
  d.same_size = expected.valid() && actual.valid() && expected.width() == actual.width() &&
                expected.height() == actual.height();
  if (!d.same_size) return d;
  d.total = static_cast<long>(expected.width()) * expected.height();
  const std::uint8_t* a = expected.pixels();
  const std::uint8_t* b = actual.pixels();
  for (long i = 0; i < d.total; ++i) {
    int delta = 0;
    for (int c = 0; c < 4; ++c) delta = std::max(delta, std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c])));
    d.max_delta = std::max(d.max_delta, delta);
    if (delta > tolerance) ++d.differing;
  }
  return d;
}

Harness::Harness(Window& window, Size size) : window_(window) {
  if (size.w <= 0 || size.h <= 0) size = window_.size();
  if (size.w <= 0 || size.h <= 0) size = {800, 600};
  window_.handle_resize(size);
  layout();
}

void Harness::layout() { window_.update_layout(); }

View& Harness::view(std::string_view id) {
  View& v = window_.get<View>(id);
  for (const View* x = &v; x; x = x->parent())
    if (!x->is_visible()) throw std::runtime_error("pulseui::testing: view '" + std::string(id) + "' is hidden");
  return v;
}

bool Harness::visible(std::string_view id) {
  View* v = window_.find(id);
  if (!v) return false;
  for (const View* x = v; x; x = x->parent())
    if (!x->is_visible()) return false;
  return true;
}

bool Harness::enabled(std::string_view id) { return window_.get<View>(id).is_enabled(); }

std::string Harness::text(std::string_view id) {
  View& v = window_.get<View>(id);
  if (auto* t = dynamic_cast<Text*>(&v)) return t->text();
  if (auto* t = dynamic_cast<TextField*>(&v)) return t->text();
  if (auto* b = dynamic_cast<Button*>(&v)) return b->label();
  if (auto* c = dynamic_cast<Checkbox*>(&v)) return c->label();
  if (auto* c = dynamic_cast<Toggle*>(&v)) return c->label();
  if (auto* e = dynamic_cast<CodeEditor*>(&v)) return e->text();
  throw std::runtime_error("pulseui::testing: view '" + std::string(id) + "' (" + v.type_name() + ") has no text");
}

Point Harness::center(View& v) {
  layout();
  return v.to_window(v.bounds().center());
}

void Harness::send_mouse(InputEvent::Type type, Point p, MouseButton button, int clicks) {
  InputEvent e;
  e.type = type;
  e.pos = e.window_pos = p;
  e.button = button;
  e.clicks = clicks;
  window_.send(e);
}

Harness& Harness::click_at(Point p, MouseButton button) {
  send_mouse(InputEvent::MouseMove, p, MouseButton::None);
  send_mouse(InputEvent::MouseDown, p, button);
  send_mouse(InputEvent::MouseUp, p, button);
  return *this;
}

Harness& Harness::click(View& v, MouseButton button) {
  if (v.bounds().empty()) throw std::runtime_error("pulseui::testing: cannot click '" + v.id() + "': it has no size");
  return click_at(center(v), button);
}

Harness& Harness::click(std::string_view id, MouseButton button) { return click(view(id), button); }

Harness& Harness::double_click(std::string_view id) {
  const Point p = center(view(id));
  send_mouse(InputEvent::MouseMove, p, MouseButton::None);
  send_mouse(InputEvent::MouseDown, p, MouseButton::Left, 1);
  send_mouse(InputEvent::MouseUp, p, MouseButton::Left, 1);
  send_mouse(InputEvent::MouseDown, p, MouseButton::Left, 2);
  send_mouse(InputEvent::MouseUp, p, MouseButton::Left, 2);
  return *this;
}

Harness& Harness::hover(std::string_view id) {
  send_mouse(InputEvent::MouseMove, center(view(id)), MouseButton::None);
  return *this;
}

Harness& Harness::drag(Point from, Point to, int steps) {
  steps = std::max(1, steps);
  send_mouse(InputEvent::MouseMove, from, MouseButton::None);
  send_mouse(InputEvent::MouseDown, from, MouseButton::Left);
  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    send_mouse(InputEvent::MouseMove, {from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, MouseButton::Left);
  }
  send_mouse(InputEvent::MouseUp, to, MouseButton::Left);
  return *this;
}

Harness& Harness::drag(std::string_view id, Point delta, int steps) {
  const Point from = center(view(id));
  return drag(from, from + delta, steps);
}

Harness& Harness::scroll(std::string_view id, float lines) {
  const Point p = center(view(id));
  send_mouse(InputEvent::MouseMove, p, MouseButton::None);
  InputEvent e;
  e.type = InputEvent::Wheel;
  e.pos = e.window_pos = p;
  e.wheel_dy = -lines; // wheel up (positive) scrolls content up
  window_.send(e);
  return *this;
}

Harness& Harness::focus(std::string_view id) {
  view(id).focus();
  return *this;
}

Harness& Harness::type(std::string_view text) {
  InputEvent e;
  e.type = InputEvent::Text;
  e.text = std::string(text);
  window_.send(e);
  return *this;
}

Harness& Harness::press(std::string_view keys) {
  const Shortcut sc = Shortcut::parse(keys);
  if (sc.key == Key::Unknown) throw std::runtime_error("pulseui::testing: unknown key in '" + std::string(keys) + "'");
  InputEvent e;
  e.type = InputEvent::KeyDown;
  e.key = sc.key;
  e.mods = sc.mods;
  window_.send(e);
  e.type = InputEvent::KeyUp;
  window_.send(e);
  return *this;
}

Harness& Harness::fill(std::string_view id, std::string_view text) {
  focus(id);
  press("Ctrl+A");
  if (text.empty()) press("Backspace");
  else type(text);
  return *this;
}

Harness& Harness::run_for(std::chrono::milliseconds d) {
  App::process_events(d);
  layout();
  return *this;
}

bool Harness::wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= end) return pred();
    App::process_events(std::chrono::milliseconds(10));
  }
  layout();
  return true;
}

Image Harness::grab() { return window_.grab(); }

bool Harness::screenshot(const std::string& png_path) { return window_.screenshot(png_path); }

bool Harness::matches_snapshot(const std::string& png_path, SnapshotOptions options) {
  namespace fs = std::filesystem;
  const Image actual = grab();
  if (!actual.valid()) return false;
  const char* update = std::getenv("PULSEUI_UPDATE_SNAPSHOTS");
  std::error_code ec;
  if (!fs::exists(png_path, ec) || (update && std::string(update) == "1")) {
    const fs::path dir = fs::path(png_path).parent_path();
    if (!dir.empty()) fs::create_directories(dir, ec);
    return actual.save_png(png_path);
  }
  const ImageDiff d = compare(Image::load(png_path), actual, options.tolerance);
  const bool ok = d.same_size && d.ratio() <= options.max_ratio;
  if (!ok) actual.save_png(png_path + ".actual.png");
  return ok;
}

} // namespace pulseui::testing
