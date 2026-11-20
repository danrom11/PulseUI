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
  [testing](docs/15-testing.md) · [platforms](docs/16-platforms.md)
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
- **Native and fast:** Direct2D + DirectWrite on Windows (per-monitor HiDPI, antialiasing), CoreGraphics + AppKit on macOS (Retina).
- **Designer:** forms created in the **Pulse Layer** designer load at runtime (`.pform`) or export to declarative C++.

- **Testable.** `pulseui::testing::Harness` drives real windows offscreen: `t.fill("email", "a@b.c").click("submit")`,
  then check properties, view texts or a reference screenshot. See [testing](docs/15-testing.md).

---

## 🧱 Building blocks

| Category   | Views |
|------------|-------|
| Layout     | `VStack`, `HStack`, `Layer` (overlay / absolute positions), `Panel` (card), `Scroll`, `Split`, `Tabs`, `Spacer`, `Divider` |
| Dynamic    | `ForEach(list, fn)`, `Show(cond, then, else)` |
| Display    | `Text`, `ImageView`, `ProgressBar`, `Canvas` (custom drawing with `redraw_on(stream)`) |
| Controls   | `Button`, `TextField`, `Checkbox`, `Toggle`, `Slider`, `Select` |

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
pulseui_add_app(my_app main.cpp)          # GUI executable (no console on Windows, .app on macOS)
# or: target_link_libraries(my_target PRIVATE PulseUI::PulseUI)
```

### Handy environment variables

| Variable | Effect |
|----------|--------|
| `PULSEUI_SCREENSHOT=out.png` | render the first window to PNG and exit (docs, visual checks) |
| `PULSEUI_THEME=light\|dark` | force a theme at startup |

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
```

- A view is a movable *description* until it is mounted; on mount its `Bind`s subscribe, on unmount they are cancelled.
- Layout runs lazily before painting; repaints are coalesced by the OS.
- `Window` routes input: hit testing, hover/press, mouse capture, focus & Tab navigation, shortcuts (`"Ctrl+S"` is Cmd on macOS), popups and tooltips.

## 🛣 Roadmap

- Linux backend (Wayland/X11)
- Multi-line text editor, menus, tree view
- Keyed diffing for `ForEach`, animations

## 📝 Changelog

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
