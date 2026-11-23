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

  // Process pending events (timers, posted work) for up to `d` without running
  // the full loop or showing windows. For tests and tools.
  static void process_events(std::chrono::milliseconds d);

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
// Where and how a popup (menu, drop-down, toast, sheet) is shown.
struct PopupOptions {
  enum class Place { At, Center, BottomRight, TopCenter };
  Place place = Place::At;       // At: the given point (window coordinates)
  bool dismiss_on_outside = true; // a click elsewhere / Escape closes it
  bool backdrop = false;         // dim the window behind and block input to it (modal sheet)
  bool shadow = true;
  float margin = 16;             // distance from the window edge (Center/BottomRight/TopCenter)
  Rect anchor{};                 // At: the view it drops down from (window coordinates);
                                 // opens above it when there is no room below
};

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
  // The view must exist and have type V, otherwise std::runtime_error.
  template <class V = View>
  V& get(std::string_view id) const {
    if (!root_) detail::throw_view_lookup(id, typeid(V).name(), nullptr);
    return root_->template get<V>(id);
  }

  void show();
  void hide();
  void activate(); // bring to the front and give it the keyboard
  // ── modal windows ──
  // Shows the window above `parent`; the parent ignores input until this one closes.
  void show_modal(Window& parent);
  // Same (or a plain show without a parent), then blocks - processing events -
  // until this window is closed.
  void run_modal(Window* parent = nullptr);
  void set_resizable(bool resizable);
  // Resizes the window to the preferred size of its content (width at most max_width).
  void fit_content(float max_width = 640);
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
  // Files dropped on the window where no view takes them (null: such drops are refused).
  std::function<void(const std::vector<std::string>&)> on_drop_files;

  // ── drag and drop ──
  enum class DragPhase { Enter, Over, Leave, Drop };
  // From the platform layer (and tests): data dragged over the window at `pos`.
  // Returns true when it would be / was accepted there.
  bool handle_drag(DragPhase phase, Point pos, const DragData& data);
  View* drop_target() const { return drop_target_; }
  // Starts a system drag of `data` (normally done by .drag_source()).
  bool start_drag(const DragData& data);
  const DragData& last_drag() const { return last_drag_; } // the last drag started (tests)

  Window& shortcut(std::string_view keys, std::function<void()> fn);

  // ── menus ──
  // The window's menu bar (native on macOS, drawn at the top elsewhere).
  // Shortcuts of its actions work at any time.
  Window& menu(MenuItems items);
  View* menu_bar() const { return menu_bar_.get(); }   // the drawn bar (null when native)
  const MenuItems& menu_items() const { return menu_items_; }
  // A pop-up menu at a point (window coordinates). Returns the menu view.
  View* popup_menu(MenuItems items, Point at, std::function<void()> on_closed = {});
  // Opens the context menu of `v` or its nearest ancestor that has one.
  bool open_context_menu(View* v, Point window_pos);

  // ── popups (menus, drop-downs) ──
  View* open_popup(Node popup, Point at, std::function<void()> on_dismiss = {});
  View* open_popup(Node popup, PopupOptions options, std::function<void()> on_dismiss = {}, Point at = {});
  // A drop-down below `anchor` (above it when there is no room), at least as wide as it.
  View* open_dropdown(Node popup, View& anchor, std::function<void()> on_dismiss = {});
  // A modal sheet inside the window: centred, the rest dimmed and blocked. Escape closes it.
  View* present(Node sheet, std::function<void()> on_dismiss = {});
  void close_popup(View* popup);
  std::vector<View*> popups() const; // open popups, bottom to top

  // ── focus ──
  View* focus_view() const { return focus_; }
  View* view_at(Point window_pos) { ensure_layout(); return hit(window_pos); } // the topmost view under a point
  std::weak_ptr<bool> alive_token() const { return alive_; }
  // Top-level views for accessibility: the menu bar, the content and the popups.
  std::vector<View*> top_views() const;
  void set_focus(View* v);
  void focus_next(bool backwards = false);

  // ── utilities ──
  bool screenshot(const std::string& png_path);
  Image grab();
  void send(InputEvent e); // inject an event (automation / tests)
  void request_paint();
  void request_layout();
  void update_layout(); // lay out now if needed (normally done before painting and input)
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
  // Headless mode (set by testing::Harness): windows are never put on screen,
  // everything else - layout, input, modality - works as usual.
  static void set_headless(bool on);
  static bool headless();

private:
  struct Popup {
    std::unique_ptr<View> view;
    std::function<void()> on_dismiss;
    Point at;
    PopupOptions options;
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
  std::vector<ShortcutEntry> menu_shortcuts_;
  std::vector<pulse::subscription> menu_subs_;
  std::unique_ptr<View> menu_bar_;
  MenuItems menu_items_;
  bool alt_alone_ = false;
  pulse::subject<InputEvent> input_;
  pulse::subscription theme_sub_, title_sub_, tooltip_timer_;
  View* hover_ = nullptr;
  View* capture_ = nullptr;
  View* focus_ = nullptr;
  View* tooltip_view_ = nullptr;
  View* drop_target_ = nullptr;
  View* drag_candidate_ = nullptr;
  Point drag_from_{};
  DragData last_drag_;
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
