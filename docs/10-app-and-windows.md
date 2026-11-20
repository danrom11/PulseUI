# 10. App and windows

## `App`

The application object owns the platform event loop. Create **exactly one**, on the main thread,
before creating any `Window`.

```cpp
int main(int argc, char** argv) {
  pulseui::App app(argc, argv);    // or App app;
  // ... create windows ...
  return app.run();
}
```

| Member | Meaning |
|---|---|
| `App()`, `App(int argc, char** argv)` | constructors; the second fills `args` |
| `std::vector<std::string> args` | command-line arguments (UTF-8) |
| `int run()` | runs the event loop; returns the exit code when the last window closes or `quit()` is called |
| `static void quit(int code = 0)` | ends `run()` |
| `bool quit_on_last_window_closed = true` | set to `false` for apps that keep running without windows |
| `static App* instance()` | the current app |

### Running code on the UI thread

| Member | Meaning |
|---|---|
| `static void post(std::function<void()> fn)` | run `fn` on the UI thread soon; safe from any thread |
| `static bool is_ui_thread()` | whether the caller is on the UI thread |
| `static pulse::executor& ui()` | a Pulse executor for the UI thread (for `observe_on`, `debounce`, queued `connect`...) |

```cpp
std::thread([] {
  auto data = load_big_file();
  pulseui::App::post([data] { show(data); });     // back on the UI thread
}).detach();

auto s = (sensor_stream | pulse::observe_on(pulseui::App::ui())).subscribe(update_gauge);
```

### Timers

```cpp
static pulse::subscription after(std::chrono::milliseconds d, std::function<void()> fn);   // once
static pulse::subscription every(std::chrono::milliseconds d, std::function<void()> fn);   // repeatedly
```

Both run `fn` on the UI thread. **Keep the returned subscription**: destroying it cancels the timer.

```cpp
class Clock {
  pulse::property<std::string> time;
  pulse::subscription tick_ = pulseui::App::every(1000ms, [this] { time = now_string(); });
};
```

For one-shot timers that should run even though you do not keep them, call `.release()` on the
subscription.

### Clipboard

```cpp
static void set_clipboard(const std::string& text);   // UTF-8
static std::string clipboard();
```

## `Window`

```cpp
Window(Bind<std::string> title = "PulseUI", float width = 800, float height = 600);
```

The size is in logical pixels (the backend scales for HiDPI). The **title may be a stream**:

```cpp
pulse::computed<std::string> title([&] { return (dirty.get() ? "• " : "") + file_name.get() + " - Editor"; },
                                   dirty, file_name);
Window win(title, 900, 600);
```

### Content

| Member | Meaning |
|---|---|
| `Window& content(Node root)` | set the root view (replaces and unmounts the previous one) |
| `View* root()` | the root view |
| `V* find<V = View>(id)` | find a view by id in this window, or `nullptr` |
| `V& get<V = View>(id)` | the same, but throws `std::runtime_error` naming the id and type if it does not match |

### Showing, hiding, closing

| Member | Meaning |
|---|---|
| `show()` | show the window (and bring it to the front) |
| `hide()` | hide it; it keeps its state and still counts as open |
| `close()` | ask to close: calls `on_close`, then closes if allowed |
| `bool is_open()` | the native window exists |
| `std::function<bool()> on_close` | return `false` to veto closing (for "unsaved changes" or hide-instead-of-close) |
| `pulse::signal<> closed` | emitted after the window closed |

```cpp
win.on_close = [&] {
  if (!dirty.get()) return true;
  save();                                  // or ask the user
  return true;
};
```

### Size and title

| Member | Meaning |
|---|---|
| `set_title(s)` | change the title (if the title was not bound to a stream) |
| `resize(w, h)`, `Size size()` | logical size |
| `set_min_size(w, h)` | the smallest size the user can resize to |
| `float scale()` | device pixels per logical pixel (1, 1.25, 2...) |
| `pulse::signal<Size> resized` | emitted when the size changes |

### Keyboard shortcuts

```cpp
Window& shortcut(std::string_view keys, std::function<void()> fn);
```

```cpp
win.shortcut("Ctrl+S", save)
   .shortcut("Ctrl+Shift+S", save_as)
   .shortcut("F5", run)
   .shortcut("Escape", [&] { win.close(); });
```

- **"Ctrl" means Cmd on macOS**, so one string works on both platforms. Use `"Meta"` / `"Win"` for
  the real Super key.
- Modifiers: `Ctrl` (or `Cmd`, `Primary`), `Shift`, `Alt` (or `Option`), `Meta` (or `Super`, `Win`).
- Keys: `A`–`Z`, `0`–`9`, `F1`–`F12`, `Escape`/`Esc`, `Enter`/`Return`, `Tab`, `Space`, `Backspace`,
  `Delete`/`Del`, `Up`, `Down`, `Left`, `Right`, `Home`, `End`. Punctuation keys are not supported.
- The focused view gets the key first. A text field consumes Ctrl+A, Ctrl+C and so on, so window
  shortcuts with the same keys do not fire while it is focused.

### Focus

| Member | Meaning |
|---|---|
| `View* focus_view()` | the focused view, or `nullptr` |
| `set_focus(View*)` | focus a view (`view->focus()` does the same) |
| `focus_next(bool backwards = false)` | move focus like Tab / Shift+Tab |

Tab and Shift+Tab move focus between focusable views (buttons, fields, checkboxes, sliders, selects...)
in tree order.

### Popups

Menus and drop-downs are popups: views drawn above the window content, dismissed by clicking outside
or by Escape.

```cpp
View* open_popup(Node popup, Point at, std::function<void()> on_dismiss = {});
void close_popup(View* popup);
```

```cpp
// a context menu
button->on_event_fn([&win](const InputEvent& e) {
  if (e.type != InputEvent::MouseDown || e.button != MouseButton::Right) return;
  auto menu = std::make_shared<View*>(nullptr);           // the buttons need the popup to close it
  *menu = win.open_popup(Panel{
                             Button("Rename", [&win, menu] { rename(); win.close_popup(*menu); }).flat(),
                             Button("Delete", [&win, menu] { remove(); win.close_popup(*menu); }).flat().danger(),
                         }.padding(4).spacing(2).width(140),
                         e.window_pos);
});
```

### The input stream

```cpp
pulse::observable<InputEvent> input() const;   // every input event of the window, in window coordinates
```

See [Input and events](11-input-and-events.md#the-window-input-stream).

### Utilities

| Member | Meaning |
|---|---|
| `bool screenshot(path)` | render the window into a PNG file |
| `Image grab()` | render the window into an `Image` (forces a layout first) |
| `send(InputEvent)` | inject an event, as if the user did it (tests, automation) |
| `request_paint()`, `request_layout()` | normally called by views through `invalidate*()` |
| `void* native_handle()` | `HWND` on Windows, `NSWindow*` on macOS |
| `static const std::vector<Window*>& all()` | all open windows |

## Several windows

Windows are independent objects. Keep secondary windows alive as members (usually
`std::unique_ptr<Window>` or a class owning a `Window`). Share state by passing references to a common
model. See [Tutorial, part 5](03-tutorial.md#part-5-several-windows).

The app quits when the last window **closes**. Hidden windows count as open, so if your app hides
dialogs instead of closing them, quit from the main window: `main.closed.connect([] { App::quit(); });`.

## File dialogs

```cpp
namespace pulseui::dialogs {
  struct Filter { std::string name; std::string patterns; };   // {"Images", "*.png;*.jpg"}

  std::optional<std::string> open_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {});
  std::optional<std::string> save_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {},
                                       const std::string& default_name = {});
}
```

```cpp
if (auto path = dialogs::save_file(&win, "Export", {{"PNG image", "*.png"}}, "chart.png"))
  win.screenshot(*path);
```

Native, modal and UTF-8. They return `std::nullopt` when cancelled.

## `run()`: the one-liner

```cpp
int run(std::string title, float width, float height, Node root);
```

Creates an `App` and a `Window`, shows the window and runs the loop. Equivalent to the five-line
`main` shown at the top of this chapter.

Next: [Input and events](11-input-and-events.md).
