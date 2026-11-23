#pragma once
// Internal platform abstraction. Each backend (win32, cocoa) implements this.
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <pulseui/gfx/painter.hpp>
#include <pulseui/ui/events.hpp>
#include <pulseui/ui/dialogs.hpp>
#include <pulseui/ui/menu.hpp>

namespace pulseui {
class Window;
class View;
}

namespace pulseui::platform {

struct WindowConfig {
  std::string title;
  float width = 800, height = 600; // logical
};

class NativeWindow {
public:
  virtual ~NativeWindow() = default;
  virtual void set_title(const std::string& t) = 0;
  virtual void set_size(float w, float h) = 0; // logical
  virtual void set_min_size(float w, float h) = 0;
  virtual Size size() const = 0;               // logical client size
  virtual void show() = 0;
  virtual void hide() = 0;
  virtual void destroy() = 0;                  // closes without asking
  virtual void invalidate() = 0;
  virtual void set_cursor(Cursor c) = 0;
  virtual void capture_mouse(bool on) = 0;
  virtual float scale() const = 0;
  virtual void* handle() const = 0;
  virtual void set_resizable(bool) {}
  // Owned by `parent`, centred over it, and the parent is disabled until this
  // window closes (null: back to a normal window).
  virtual void set_modal_parent(NativeWindow* /*parent*/) {}
  virtual void activate() {}
  // Accessibility: the keyboard focus moved to `v` (null: the window itself).
  virtual void a11y_focus_changed(View* /*v*/) {}
};

// System drag and drop with this data (files and/or text). On Windows it returns
// when the drag ends; true when the data was dropped somewhere.
bool start_drag(NativeWindow* w, const DragData& data);

// Native menu bar for the window. Returns false when the platform draws none
// (PulseUI then draws its own at the top of the window).
bool set_native_menu(NativeWindow* w, const MenuItems& items);

// The window receives events through pulseui::Window::handle_* methods.
std::unique_ptr<NativeWindow> create_window(Window* owner, const WindowConfig& cfg);

void init();
int run();
void quit(int code);
void post(std::function<void()> fn);
// Dispatch events until done() returns true (nested loop for blocking dialogs).
// Returns early when the application quits (the quit request stays pending).
void run_modal_loop(const std::function<bool()>& done);

// Native message boxes. Return nullopt / false when the platform has none
// (PulseUI then shows its own themed dialog).
std::optional<dialogs::Answer> native_message(NativeWindow* parent, const dialogs::Message& m);
bool native_message_async(NativeWindow* parent, const dialogs::Message& m, std::function<void(dialogs::Answer)> done);

// Dispatch events for up to `timeout_ms` (returns early only on quit). Does not show windows.
void process_events(int timeout_ms);
bool is_main_thread();

std::uint64_t start_timer(int ms, bool repeat, std::function<void()> fn);
void stop_timer(std::uint64_t id);

Size measure_text(std::string_view s, const Font& f, float max_width);
float line_height(const Font& f);

void set_clipboard(const std::string& s);
std::string get_clipboard();

struct FileFilter {
  std::string name;     // "Pulse forms"
  std::string patterns; // "*.pform;*.json"
};
std::optional<std::string> open_file_dialog(NativeWindow* parent, const std::string& title,
                                            const std::vector<FileFilter>& filters);
std::optional<std::string> save_file_dialog(NativeWindow* parent, const std::string& title,
                                            const std::vector<FileFilter>& filters,
                                            const std::string& default_name);

bool decode_image(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba);
bool encode_png(const std::string& path, int w, int h, const std::uint8_t* rgba);

// Offscreen rendering (screenshots). Fills straight-alpha RGBA8.
bool render_offscreen(int w, int h, float scale, const std::function<void(Painter&)>& paint,
                      std::vector<std::uint8_t>& rgba);

} // namespace pulseui::platform
