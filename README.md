# PulseUI ⚡️

**PulseUI** is a lightweight, cross-platform **declarative reactive UI library for C++20**, built on top of [Pulse](https://github.com/danrom11/Pulse).

Instead of manually handling state and events, you describe the UI once and it **reacts to observables and data streams in real time**. No `invalidate()`, no `set_text()` scattered through your code — state lives in `pulse::property` / `observable`, the UI follows it.

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  pulse::property<int> count{0};

  return run("Counter", 380, 220,
    VStack{
      Text(count.map([](int c) { return "Clicked " + std::to_string(c) + " times"; })).title(),
      HStack{
        Button("-", [&] { count = count - 1; }),
        Button("+", [&] { count = count + 1; }).primary(),
      },
    }.padding(24));
}
```

## 📚 Documentation

Everything is documented in [`docs/`](docs/README.md), from a step-by-step course to a complete reference:

- **Course:** [your first apps](docs/03-tutorial.md) (counter → converter → todo list → app structure → several
  windows → background work → custom drawing) and [forms with the Pulse Layer designer](docs/04-forms-tutorial.md)
- **Reference:** [views and modifiers](docs/05-views-and-modifiers.md) · [reactive bindings](docs/06-reactive-bindings.md) ·
  [layout](docs/07-layout.md) · [widgets](docs/08-widgets.md) · [styling and themes](docs/09-styling-and-themes.md) ·
  [app and windows](docs/10-app-and-windows.md) · [input](docs/11-input-and-events.md) ·
  [custom drawing](docs/12-custom-drawing.md) · [CodeEditor](docs/13-code-editor.md) · [forms](docs/14-forms-and-meta.md) ·
  [testing](docs/15-testing.md) · [platforms](docs/16-platforms.md) · [menus and dialogs](docs/19-menus-and-dialogs.md) ·
  [collections](docs/20-collections.md) · [more widgets](docs/21-more-widgets.md) · [animation](docs/22-animation.md) ·
  [drag and drop](docs/23-drag-and-drop.md) · [accessibility](docs/24-accessibility.md)
- **Practice:** [patterns, pitfalls and FAQ](docs/17-patterns-and-faq.md) · [cheat sheet](docs/18-cheatsheet.md)

---

## ✨ Ideas

- **State is Pulse.** `pulse::property<T>`, `pulse::computed<T>`, any `pulse::observable<T>` — one source of truth.
- **Every attribute is a `Bind<T>`:** pass a value *or* a stream. `Text("hi")`, `Text(name)`, `Text(count.map(...))`, `.visible(logged_in)`, `.background(accent$)`.
- **Two-way bindings for inputs:** `TextField(name)`, `Checkbox("Remember", remember)`, `Slider(volume, 0, 100)`, `Select({"A","B"}, index)`.
- **Events are streams:** `button.clicks() | pulse::throttle(300ms)`, `window.input()` — filter, debounce, combine them with Pulse operators.
- **Declarative composition:** `VStack{...}`, `HStack{...}`, `ForEach(list, fn)`, `Show(cond, a, b)`; components are plain functions returning `Node`.
- **Automatic threading:** a stream may emit on any thread — updates are delivered on the UI thread.
- **Lifetime-safe:** bindings subscribe on mount and are cancelled on unmount/destroy.
- **Native and fast:** Direct2D + DirectWrite on Windows (per-monitor HiDPI, antialiasing), CoreGraphics + AppKit on macOS (Retina),
  X11 + Cairo + Pango on Linux (HiDPI from `Xft.dpi`, runs on Wayland desktops through XWayland).
- **Designer:** forms created in the **Pulse Layer** designer load at runtime (`.pform`) or export to declarative C++.

- **Testable.** `pulseui::testing::Harness` drives real windows offscreen: `t.fill("email", "a@b.c").click("submit")`,
  then check properties, view texts or a reference screenshot. See [testing](docs/15-testing.md).

---

## 🧱 Building blocks

| Category   | Views |
|------------|-------|
| Layout     | `VStack`, `HStack`, `Grid`, `Wrap`, `Layer` (overlay / absolute positions), `Panel` (card), `Scroll`, `Split`, `Tabs`, `Spacer`, `Divider` |
| Dynamic    | `ForEach(list, key, fn)` (keyed rows keep their state), `Show(cond, then, else).transition()` |
| Collections | `List` (virtualized), `Table` (columns, sorting), `TreeView` |
| Display    | `Text`, `ImageView`, `Icon` (SVG), `ProgressBar`, `Canvas` (custom drawing with `redraw_on(stream)`) |
| Controls   | `Button`, `TextField` (with suggestions), `TextArea`, `NumberField`, `Checkbox`, `Toggle`, `RadioGroup`, `Slider`, `Select`, `Segmented`, `DatePicker`/`Calendar`, `ColorPicker`/`ColorPanel` |
| App        | menu bar and context menus, message boxes and prompts, modal windows, toasts, drag and drop, screen readers |

Common modifiers (chainable on every view): `id`, `padding`, `width`, `height`, `size`, `min_*`/`max_*`,
`flex`, `align`, `position`, `background`, `border`, `radius`, `opacity`, `visible`, `enabled`,
`tooltip`, `cursor`, `on_click`, `on_hover`, `on_event_fn`, `ref`, `on_mount_fn`.

Layout is a one-axis flexbox: children keep their preferred size, `flex(n)` children share the rest,
cross axis stretches by default (`align_items`, `justify`, `spacing` on stacks).

---

## 🔁 Reactive patterns

```cpp
pulse::property<std::vector<Todo>> todos;
pulse::property<std::string> draft;
pulse::property<int> filter{0};

// derived state
pulse::computed<std::vector<Todo>> visible([&] { /* filter todos */ }, todos, filter);

VStack{
  HStack{
    TextField(draft).placeholder("What needs to be done?").on_submit([&](auto&) { add(); }).flex(),
    Button("Add", add).primary(),
  },
  Select({"All", "Active", "Done"}, filter),
  Scroll(ForEach(visible, [&](const Todo& t) {
    return HStack{ Checkbox(t.title, t.done), Spacer(), Button("✕", [&] { remove(t); }).flat() };
  })).flex(),
};
```

```cpp
// Clicks as a stream
Button hello("Say hello");
auto sub = (hello.clicks() | pulse::throttle(300ms)).subscribe([&](pulse::unit) { ++count; });

// The theme is a property too
theme() = Theme::light();          // every window repaints
dark.changed.connect([](bool d) { theme() = d ? Theme::dark() : Theme::light(); });

// Window input stream
auto clicks = win.input() | pulse::filter([](const InputEvent& e) { return e.type == InputEvent::MouseDown; });
```

### Forms from the designer

Draw a form in Pulse Layer and bind its views to members of your class right in the Properties panel
(`email` field ← `email`, *Sign in* enabled ← `can_sign_in`, click → `sign_in()`). The build turns the
form into ordinary declarative code, so the compiler checks every binding:

```cpp
class LoginWindow {
  friend struct Ui::LoginWindow;                     // the generated code binds these members
  pulse::property<std::string> email, password;
  pulse::computed<bool> can_sign_in;
  void sign_in();

  Ui::LoginWindow ui_;                               // typed views: ui_.email, ui_.status...
  Window window_{Ui::LoginWindow::form_title, Ui::LoginWindow::form_width, Ui::LoginWindow::form_height};
public:
  LoginWindow() : can_sign_in(...) { ui_.setup(window_, *this); }
};
```

Forms can also be loaded at runtime with `load_form("login.pform")` and connected with
`win.get<TextField>("email").model(email)` (a strict lookup that throws on a mismatch).

---

## 📦 Requirements

- C++20, CMake ≥ 3.21
- Windows 10+: MSVC 2022 (or MinGW-w64 GCC ≥ 11)
- macOS 11+: Xcode / AppleClang 14+
- Linux (X11 or Wayland with XWayland): GCC ≥ 11 or Clang ≥ 14, and the development packages of X11, Xcursor,
  xkbcommon, Cairo and Pango (gdk-pixbuf and GIO are optional: JPEG/GIF images and the desktop's file dialog).
  Debian/Ubuntu: `sudo apt install pkg-config libx11-dev libxcursor-dev libxkbcommon-dev libcairo2-dev libpango1.0-dev libgdk-pixbuf-2.0-dev`
- [Pulse](https://github.com/danrom11/Pulse) — taken from `../Pulse` when present, otherwise fetched automatically

## 🛠️ Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Examples (in `build/examples`): `pulseui_counter`, `pulseui_todo`, `pulseui_gallery`, `pulseui_form_app`.

Use it from your project:

```cmake
add_subdirectory(PulseUI)                 # or FetchContent
pulseui_add_app(my_app main.cpp)          # GUI executable (no console on Windows, .app on macOS, plain on Linux)
# or: target_link_libraries(my_target PRIVATE PulseUI::PulseUI)
```

### Handy environment variables

| Variable | Effect |
|----------|--------|
| `PULSEUI_SCREENSHOT=out.png` | render the first window to PNG and exit (docs, visual checks) |
| `PULSEUI_THEME=light\|dark` | force a theme at startup |
| `PULSEUI_SCALE=1.5` | Linux: force the HiDPI scale (default: `Xft.dpi / 96`) |

---

## 🏗 Architecture

```
include/pulseui/
  core/      geometry, color, json
  gfx/       Painter (backend-neutral drawing), Font, Image, text measurement
  ui/        Bind, View/ViewT (modifiers), containers, widgets, Window/App, theme,
             meta (editable properties for the designer), form (.pform + C++ codegen), dialogs
src/platform/
  win32/     Direct2D/DirectWrite painter, HWND window, message loop, WIC images, file dialogs
  cocoa/     CoreGraphics painter, NSView/NSWindow, NSApplication loop, ImageIO, NSOpenPanel
  x11/       Xlib windows and input (XIM), poll() loop, CLIPBOARD selection, file dialogs (portal, zenity, kdialog)
  cairo/     Cairo painter and Pango text, PNG/gdk-pixbuf images, offscreen rendering (shared by Linux backends)
```

- A view is a movable *description* until it is mounted; on mount its `Bind`s subscribe, on unmount they are cancelled.
- Layout runs lazily before painting; repaints are coalesced by the OS.
- `Window` routes input: hit testing, hover/press, mouse capture, focus & Tab navigation, shortcuts (`"Ctrl+S"` is Cmd on macOS), popups and tooltips.

## 🛣 Roadmap

- Linux: native Wayland backend (X11 works today, also through XWayland), drag and drop between applications
  (XDND), screen readers (AT-SPI)
- Complex text input: IME for Chinese and Japanese, right-to-left scripts

## 📝 Changelog

### 0.5.0
- **Linux backend (X11 + Cairo + Pango).** PulseUI apps build and run on Linux: on X11 desktops and on Wayland
  desktops through XWayland. Application code does not change.
  - Windows: title, size and minimum size, resizing, close requests, modal windows (transient dialogs that block
    their parent), activation, themed cursors (Xcursor).
  - Drawing with Cairo, text with Pango (fontconfig picks fonts and falls back per character for Cyrillic, CJK,
    emoji), clipping, even-odd paths, gradients, images.
  - Input: mouse buttons, wheel (vertical and horizontal), double and triple clicks, keyboard through XKB keysyms
    (shortcuts keep working with non-Latin layouts), text through the X input method (XIM: dead keys, compose).
  - HiDPI: the scale comes from `Xft.dpi` and `GDK_SCALE`; the new `PULSEUI_SCALE` variable overrides it.
  - Clipboard: the `CLIPBOARD` selection with UTF-8 text (large transfers from other apps included).
  - File dialogs: the desktop's own dialog through xdg-desktop-portal, else zenity or kdialog.
  - Images: PNG through Cairo; JPEG, GIF and others with gdk-pixbuf (optional).
  - Menu bars and message boxes are drawn by PulseUI in the current theme, as on Windows.
  - Without an X server (CI, containers, WSL without WSLg) windows live in memory: `pulseui::testing` and
    `PULSEUI_SCREENSHOT` work headless.
  - Build dependencies (found with `pkg-config`): x11, xcursor, xkbcommon, cairo, pangocairo; optional
    gdk-pixbuf-2.0 and gio-2.0. Tested with GCC 13 and 16 and Clang 18.
  - Not yet: drag and drop between applications (XDND), screen readers (AT-SPI), a native Wayland backend.
- The platform code is split so a future Wayland backend can reuse the Cairo/Pango painter (`src/platform/cairo/`).

### 0.4.1
- `quit_on_last_window_closed` ignores windows hidden with `hide()`: a hidden window (for example a second form
  that hides itself instead of closing, to be shown again later) no longer keeps an invisible process alive after
  the main window closes.

### 0.4.0
- **Keyed `ForEach(items, key, row)`**: unchanged rows keep their views and state (focus, selection, text being
  typed), moved rows move, only new or changed items build rows. `.wrap()` lays rows out like `Wrap`.
- **Collections**: virtualized `List`, `Table` (columns, sorting, resizing, selection by item), `TreeView`.
- **Layout**: `Grid` (tracks, cells, spans) and `Wrap`.
- **Menus**: `Action` / `Menu` / `Separator` values; `Window::menu()` (native menu bar on macOS, themed bar on
  Windows with Alt/F10 and mnemonics), `.context_menu(...)`, `popup_menu`, check and radio items, shortcuts.
- **Dialogs**: `dialogs::message / alert / confirm / prompt` (blocking) and `_async` forms (NSAlert on macOS);
  modal windows (`show_modal`, `run_modal`), in-window sheets (`present`), popup placement options, `open_dropdown`.
- **Widgets**: `TextArea`, `NumberField`, `RadioGroup`, `Calendar` / `DatePicker` (`Date`), `ColorPanel` /
  `ColorPicker` (`Color::hsv`), `Icon`, toasts (`toast(win, ...)`), `TextField::suggestions(...)`.
- **Animation**: `.animate()` for frames, opacity, background and border; `Show(...).transition()`; `tween(...)`
  streams; `anim::run`, easings, `anim::set_speed`. The Toggle knob slides, toasts fade.
- **Drag and drop**: `.on_drop_files(fn, extensions)`, `.on_drop_text(fn)`, `.drag_source(fn)`,
  `Window::on_drop_files` (OLE on Windows, NSDragging on macOS).
- **SVG icons**: `Svg::parse / load / draw`, SVG in `Button::icon` and menu icons, `Painter::fill_path` (even-odd).
- **Accessibility**: UI Automation provider on Windows, NSAccessibility on macOS; `View::accessibility()`,
  `accessibility_action()`, `.a11y_label / a11y_role / a11y_description / a11y_hidden`.
- `CodeEditor`: breakpoints in the gutter (`breakpoints`, `breakpoint_toggled`) and `execution_line`.
- Forms: `ForEach` row templates, `Tabs`, `Canvas`, `Grid`, `Wrap`, components (`Component`: another form,
  `Ui::Form::part(m)`), project view classes (`Custom`) and every new widget; `meta::Property::bind_once`,
  `TypeInfo::init_new`, `TypeInfo::child_title`.
- `Window::set_headless` (tests never put windows on screen), `Window::activate`, `popups()`, `top_views()`.
- `CodeEditor`: a right-click menu (Undo, Redo, Cut, Copy, Paste, Select All; `standard_menu()`,
  `set_menu_labels`) and the same actions as members (`undo`, `redo`, `cut`, `copy`, `paste`, `select_all`).
- Generated form code calls list-row handlers through `pulseui::form_call` (Apple clang 15 compiles it).
- `NumberField`: setting value, min and max in any order keeps the value (forms load them one by one).
- Builds without warnings with Apple clang, clang and GCC (`-Wall -Wextra`).
- `CodeEditor`: completion items can place the caret with `$0` in `insert` (`"resize($0)"`, multi-line snippets).
- `CodeEditor`: **parameter hints**: a provider can return `CompletionResult::signature` with the current
  parameter range; the editor shows it after `(` and `,` and highlights the parameter being typed.
- `CodeEditor`: Enter between `{}` or `()` puts the closing bracket on its own line.
- `CodeEditor`: `auto_close()` brackets and quotes, `line_comment("//")` with Ctrl+/, matching-bracket highlight,
  `hover(provider)` tooltips, `definition_requested` on Ctrl+click, search and replace (`set_search`, `find_next`,
  `replace_current`, `replace_all`) and range editing (`select`, `replace_range`, `replace_all_text`).
- `Key::Slash`; shortcuts accept `"/"`, `"PageUp"`, `"PageDown"`, `"Insert"`.

### 0.3.0
- **Form bindings**: a `.pform` can bind view properties to members of a model (`"bind": {"text": "email"}`) and
  connect events to its methods (`"on": {"click": "submit"}`). Code generation turns them into ordinary modifiers,
  so the compiler checks them. See the [forms tutorial](docs/04-forms-tutorial.md).
- Generated form structs expose typed pointers to views with an id (`ui_.email`) and `setup(window, model)`.
- `View::get<T>(id)` / `Window::get<T>(id)`: strict lookup that throws a descriptive error instead of returning `nullptr`.
- **`pulseui/testing.hpp`**: drive real windows from tests without a screen (`click("submit")`, `fill("email", ...)`,
  `press("Ctrl+S")`, `wait_until(...)`) and visual regression with `matches_snapshot(...)`. `App::process_events()`.
- `meta::Property::bind`, `meta::Event`, `TypeInfo::cpp_class`; view annotations (`annotation`, `set_annotation`).
- `ForEach` accepts a plain `std::vector`; `text_field->id()`, `->tooltip()`, `->cursor()` getters work on concrete widgets.
- Complete documentation in `docs/`.

## License

Apache-2.0
