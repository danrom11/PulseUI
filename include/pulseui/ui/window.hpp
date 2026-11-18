#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/property.hpp>
#include <pulse/core/scheduler.hpp>
#include <pulse/core/signal.hpp>
#include <pulse/core/subject.hpp>
#include <pulseui/ui/bind.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui {

namespace platform {
class NativeWindow;
}

// ─────────────────────────────────────────────────────────────────────────────
// App: the event loop. Create exactly one, on the main thread.
// ─────────────────────────────────────────────────────────────────────────────
class App {
public:
  App();
  App(int argc, char** argv);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  int run();               // returns when quit() is called or the last window closes
  static void quit(int code = 0);
  static App* instance();

  // Run on the UI thread (thread-safe, from anywhere).
  static void post(std::function<void()> fn);
  static bool is_ui_thread();
  // Pulse executor bound to the UI thread: stream | pulse::observe_on(App::ui())
  static pulse::executor& ui();

  // UI-thread timers. Cancel by resetting the subscription.
  static pulse::subscription after(std::chrono::milliseconds d, std::function<void()> fn);
  static pulse::subscription every(std::chrono::milliseconds d, std::function<void()> fn);

  static void set_clipboard(const std::string& text);
  static std::string clipboard();

  bool quit_on_last_window_closed = true;
  std::vector<std::string> args;
};

// ─────────────────────────────────────────────────────────────────────────────
// Window
//   Window win("Counter", 400, 300);
//   win.content(VStack{ ... });
// ─────────────────────────────────────────────────────────────────────────────
class Window {
public:
  Window(Bind<std::string> title = std::string("PulseUI"), float width = 800, float height = 600);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  Window& content(Node root);
  View* root() const { return root_.get(); }
  template <class V = View>
  V* find(std::string_view id) const {
    return root_ ? root_->template find<V>(id) : nullptr;
  }

  void show();
  void hide();
  void close();           // asks on_close first
  bool is_open() const { return native_ != nullptr; }
  void set_title(const std::string& t);
  void resize(float w, float h);
  Size size() const { return size_; }
  void set_min_size(float w, float h);
  float scale() const;

  // ── reactive surface ──
  pulse::signal<> closed;
  pulse::signal<Size> resized;
  // Every input event of the window (window coordinates), as a stream.
  pulse::observable<InputEvent> input() const { return input_.as_observable(); }
  // Return false to veto closing.
  std::function<bool()> on_close;

  Window& shortcut(std::string_view keys, std::function<void()> fn);

  // ── popups (menus, drop-downs) ──
  View* open_popup(Node popup, Point at, std::function<void()> on_dismiss = {});
  void close_popup(View* popup);

  // ── focus ──
  View* focus_view() const { return focus_; }
  void set_focus(View* v);
  void focus_next(bool backwards = false);

  // ── utilities ──
  bool screenshot(const std::string& png_path);
  Image grab();
  void send(InputEvent e); // inject an event (automation / tests)
  void request_paint();
  void request_layout();
  void* native_handle() const;
  platform::NativeWindow* native_window() const { return native_.get(); }

  // internal (called by the platform layer / views)
  void handle_paint(Painter& p);
  void handle_resize(Size s);
  void handle_input(InputEvent e);
  bool handle_close_request();
  void handle_destroyed();
  void view_detached(View* v);
  static const std::vector<Window*>& all();

private:
  struct Popup {
    std::unique_ptr<View> view;
    std::function<void()> on_dismiss;
    Point at;
  };
  struct ShortcutEntry {
    Shortcut sc;
    std::function<void()> fn;
  };

  void ensure_layout();
  View* hit(Point window_pos);
  View* hit_in(View* v, Point local);
  void update_hover(View* v);
  void update_cursor(View* v);
  void dispatch_mouse(InputEvent& e);
  View* bubble(View* v, InputEvent& e, bool capture);
  void collect_focusable(View* v, std::vector<View*>& out);
  void show_tooltip(View* v);
  void hide_tooltip();

  std::unique_ptr<platform::NativeWindow> native_;
  std::unique_ptr<View> root_;
  std::vector<Popup> popups_;
  std::vector<ShortcutEntry> shortcuts_;
  pulse::subject<InputEvent> input_;
  pulse::subscription theme_sub_, title_sub_, tooltip_timer_;
  View* hover_ = nullptr;
  View* capture_ = nullptr;
  View* focus_ = nullptr;
  View* tooltip_view_ = nullptr;
  std::string tooltip_text_;
  Point tooltip_pos_{};
  std::vector<std::unique_ptr<View>> graveyard_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  Size size_{};
  bool layout_dirty_ = true;
  bool shown_ = false;
  bool hide_requested_ = false;
  bool destroyed_ = false;
  friend class App;
};

// One-liner for small apps:
//   int main() { return pulseui::run("Hello", 400, 300, Text("Hi")); }
int run(std::string title, float width, float height, Node root);

} // namespace pulseui
