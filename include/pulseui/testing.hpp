#pragma once
// pulseui/testing.hpp - drive real windows from tests, without a screen.
//
//   App app;                                  // once per test program
//   Window w("Login", 360, 240);
//   w.content(...);
//   pulseui::testing::Harness t(w);           // lays the window out offscreen
//   t.fill("email", "ada@example.com");       // focus, select all, type
//   t.click("submit");                        // by id: move, press, release
//   t.press("Ctrl+S");                        // keys as in Window::shortcut
//   assert(t.text("status") == "Saved");
//   assert(t.matches_snapshot("tests/snapshots/login.png"));
//
// Windows are never shown. Events go through Window::send, so hit testing,
// focus, capture, bubbling and shortcuts behave exactly as with real input.
// Bindings are synchronous on the UI thread, so state is visible right after
// each call. For timers and posted work, use run_for() / wait_until().
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <pulseui/core/geometry.hpp>
#include <pulseui/gfx/image.hpp>
#include <pulseui/ui/events.hpp>

namespace pulseui {
class View;
class Window;
} // namespace pulseui

namespace pulseui::testing {

// Result of comparing two images.
struct ImageDiff {
  bool same_size = false;
  long differing = 0;         // pixels whose channels differ by more than the tolerance
  long total = 0;
  int max_delta = 0;          // largest channel difference found
  double ratio() const { return total ? static_cast<double>(differing) / static_cast<double>(total) : 1.0; }
};

struct SnapshotOptions {
  int tolerance = 16;         // per-channel difference (0..255) still considered equal (antialiasing)
  double max_ratio = 0.002;   // fraction of differing pixels still accepted
};

ImageDiff compare(const Image& expected, const Image& actual, int tolerance = 16);

class Harness {
public:
  // Gives the window a size (its current size if `size` is empty) and lays it out.
  explicit Harness(Window& window, Size size = {});

  Window& window() { return window_; }

  // ── finding views ──
  View& view(std::string_view id);                     // throws std::runtime_error if missing or hidden
  std::string text(std::string_view id);               // Text, TextField, Button, Checkbox/Toggle label, CodeEditor
  bool visible(std::string_view id);                   // exists, and it and its ancestors are visible
  bool enabled(std::string_view id);
  Point center(View& v);                               // window coordinates

  // ── mouse ──
  Harness& click(std::string_view id, MouseButton button = MouseButton::Left);
  Harness& click(View& v, MouseButton button = MouseButton::Left);
  Harness& click_at(Point window_pos, MouseButton button = MouseButton::Left);
  Harness& double_click(std::string_view id);
  Harness& hover(std::string_view id);
  Harness& drag(std::string_view id, Point delta, int steps = 8);   // press at the center, move by delta, release
  Harness& drag(Point from, Point to, int steps = 8);
  Harness& scroll(std::string_view id, float lines);               // positive = down
  // Drag files (from "another application") over a view and drop them there.
  // Returns whether they were taken.
  bool drop_files(std::string_view id, const std::vector<std::string>& files);
  bool drop_text(std::string_view id, const std::string& text);

  // ── keyboard ──
  Harness& focus(std::string_view id);
  Harness& type(std::string_view text);                            // text into the focused view
  Harness& fill(std::string_view id, std::string_view text);       // focus, select all, type
  Harness& press(std::string_view keys);                           // "Enter", "Tab", "Ctrl+Z", "Shift+Tab"

  // ── time ──
  // Process events (timers, App::post, queued signals) for `d`, without showing windows.
  Harness& run_for(std::chrono::milliseconds d);
  // Process events until `pred` is true or `timeout` passes. Returns pred().
  bool wait_until(const std::function<bool()>& pred, std::chrono::milliseconds timeout = std::chrono::seconds(5));

  // ── pictures ──
  Image grab();                                        // lays out and renders offscreen
  bool screenshot(const std::string& png_path);
  // Visual regression: compares the window with a reference PNG. When the reference
  // does not exist, or the environment variable PULSEUI_UPDATE_SNAPSHOTS=1 is set,
  // it is (re)written and the check passes. On a mismatch, "<path>.actual.png" is
  // written next to it. References are platform specific (fonts, antialiasing).
  bool matches_snapshot(const std::string& png_path, SnapshotOptions options = {});

private:
  void layout();
  void send_mouse(InputEvent::Type type, Point p, MouseButton button, int clicks = 1);
  Window& window_;
};

} // namespace pulseui::testing
