# 15. Testing and automation

PulseUI applications are unusually easy to test, at three levels:

1. **Models** (properties, computed values, signals) need no UI at all.
2. **Real windows** can be driven without a screen with `pulseui::testing::Harness`: click by id, type,
   press keys, drag, scroll, wait for timers, and check what the views show.
3. **Pictures**: render windows offscreen and compare them with reference images.

## Level 1: test the model

```cpp
LoginModel m;
m.email = "user@example.com";
m.password = "short";
assert(!m.can_submit.get());
m.password = "long-enough";
assert(m.can_submit.get());
```

No window, no event loop. Keep as much logic here as you can; the reactive style makes it natural.

## Level 2: drive a real window

```cpp
#include <cassert>
#include <pulseui/pulseui.hpp>
#include <pulseui/testing.hpp>

using namespace pulseui;

int main() {
  App app;                                     // required once; run() is never called

  pulse::property<std::string> name;
  pulse::property<int> count{0};
  Window w("test", 320, 200);                  // never shown
  w.content(VStack{
      TextField(name).id("name"),
      Text(count.map([](int c) { return std::to_string(c); })).id("count"),
      Button("+", [&] { count = count + 1; }).id("inc"),
  });

  testing::Harness t(w);                       // sizes and lays out the window offscreen
  t.fill("name", "Ada").click("inc").click("inc");

  assert(name.get() == "Ada");
  assert(t.text("count") == "2");             // bindings are synchronous: no waiting needed
}
```

Put this in an ordinary executable registered with CTest (`add_test(NAME ui COMMAND my_ui_tests)`).
Any test framework works; the harness only needs a `pulseui::App` to exist.

### How it works, and why you can trust it

- **Windows are never shown.** Layout, focus, input dispatch and offscreen rendering all work on a hidden
  window, so tests run in CI and do not steal focus.
- **Events go through `Window::send`**, the same path as real input: hit testing, bubbling, mouse
  capture, focus changes, shortcuts and disabled views behave exactly as for a user. Clicking a disabled
  button does nothing, just like in real life.
- **Bindings are synchronous on the UI thread.** After `t.click(...)`, every property and every bound view
  is already updated.
- **Actions find views by id** and fail loudly: a missing id, a hidden view or a view without text throws
  `std::runtime_error` with a message naming it.

### `testing::Harness` reference

```cpp
explicit Harness(Window& window, Size size = {});   // the window's size, or 800x600 if it has none
```

| Finding | |
|---|---|
| `View& view(id)` | the view; throws if it is missing or hidden |
| `std::string text(id)` | the text of a `Text`, `TextField`, `CodeEditor`; the label of a `Button`, `Checkbox`, `Toggle` |
| `bool visible(id)` | exists, and it and all its ancestors are visible |
| `bool enabled(id)` | enabled, including its ancestors |
| `Point center(View&)` | the view's center in window coordinates |
| `Window& window()` | the window under test |

| Mouse | |
|---|---|
| `click(id, button = Left)`, `click(View&)` | move to the center, press, release |
| `click_at(Point)` | the same at a window position |
| `double_click(id)` | two clicks, the second with `clicks = 2` |
| `hover(id)` | move the pointer over the view (hover state, tooltips) |
| `drag(id, Point delta, steps = 8)`, `drag(Point from, Point to, steps = 8)` | press, move in steps, release |
| `scroll(id, lines)` | wheel over the view; positive scrolls down |

| Keyboard | |
|---|---|
| `focus(id)` | give the view keyboard focus |
| `type(text)` | a `Text` event into the focused view |
| `fill(id, text)` | focus, select all, type (or delete when `text` is empty) |
| `press(keys)` | key down and up: `"Enter"`, `"Tab"`, `"Ctrl+Z"`, `"Shift+Tab"`, `"Ctrl+End"`; same names as `Window::shortcut`; `Ctrl` means Cmd on macOS |

| Time | |
|---|---|
| `run_for(ms)` | process events (timers, `App::post`, queued signals) for a while, without showing windows |
| `wait_until(pred, timeout = 5s)` | process events until `pred()` is true; returns whether it became true |

| Pictures | |
|---|---|
| `Image grab()` | lay out and render offscreen |
| `bool screenshot(path)` | render into a PNG |
| `bool matches_snapshot(path, SnapshotOptions = {})` | visual regression check (below) |

All actions return the harness, so they chain: `t.click("tf").type("abc").press("Enter")`.

### Examples

```cpp
// a form
t.fill("email", "not-an-email");
assert(!t.enabled("submit"));
assert(t.text("hint") == "Enter a valid email");
t.fill("email", "ada@example.com").fill("password", "correct horse");
t.click("submit");

// keyboard navigation and shortcuts
t.focus("name").press("Tab");
assert(window.focus_view() == &t.view("email"));
t.press("Ctrl+S");

// a slider by dragging
t.drag("volume", {-1000, 0});      // all the way left
assert(volume.get() == 0);

// lists
items.update([](auto& v) { v.push_back("new"); });
assert(t.view("list").children().size() == 4);
t.scroll("list_scroll", 10);

// timers and asynchronous work
start_download();
assert(t.wait_until([&] { return progress.get() >= 1.0; }, 10s));
assert(t.text("status") == "Done");
```

### Forms designed in Pulse Layer

The harness works on any window, including those built with `ui_.setup(window_, *this)`. Expose the
window from your form class (the default templates have `window()`), then:

```cpp
MainWindow form;
testing::Harness t(form.window());
t.fill("email", "ada@example.com").click("submit");
```

A missing id throws, so such a test also catches a designer change that broke the code's assumptions.

## Level 3: visual regression

```cpp
assert(t.matches_snapshot("tests/snapshots/login.png"));
```

- **First run:** the reference does not exist, so it is written and the check passes. Review the image
  and commit it.
- **Later runs:** the window is rendered and compared with the reference. On a mismatch the check fails
  and `login.png.actual.png` is written next to the reference for inspection.
- **Updating references** after an intended change: run the tests with `PULSEUI_UPDATE_SNAPSHOTS=1`.

```cpp
struct SnapshotOptions {
  int tolerance = 16;         // per-channel difference (0..255) still considered equal
  double max_ratio = 0.002;   // fraction of differing pixels still accepted
};
testing::ImageDiff testing::compare(const Image& expected, const Image& actual, int tolerance = 16);
// ImageDiff: same_size, differing, total, max_delta, ratio()
```

The tolerance absorbs antialiasing noise. **References are platform-specific**: fonts and text rendering
differ between Windows and macOS (and between DPI scales), so keep a set per platform, for example
`tests/snapshots/windows/` and `tests/snapshots/macos/`. Snapshot one theme explicitly by setting
`theme() = Theme::dark();` in the test.

## Screenshots without a test

Run any PulseUI application with `PULSEUI_SCREENSHOT=out.png`: the first window is rendered about 0.5 s
after startup, saved, and the app exits. It is handy for documentation images. Combine it with
`PULSEUI_THEME=light` / `dark`.

## Automation and demos

Because the harness uses the real input path, it can also drive an application for demos or for
reproducing a bug report: build the window, run a sequence of `click` / `fill` / `press` calls with
`run_for` pauses between them, and save screenshots along the way. Pulse Layer's own self-test
(`PULSELAYER_SELFTEST=1`) works the same way: it clicks through the designer, drags items from the
palette, edits properties and bindings, and builds programs.

Next: [Platforms](16-platforms.md).
