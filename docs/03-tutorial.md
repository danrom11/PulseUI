# 3. Tutorial: your first apps

This course builds seven small programs. Each one introduces a few ideas and explains why PulseUI
works the way it does. Type them in and run them; every listing is a complete `main.cpp` for the
CMake project from [Getting started](02-getting-started.md).

| Part | Program | You learn |
|---|---|---|
| 1 | Counter | properties, `map`, buttons, stacks |
| 2 | Temperature converter | `computed`, two-way models, `visible`, input parsing |
| 3 | Todo list | lists with `ForEach`, `Scroll`, filters, editing collections |
| 4 | The todo list as a class | the standard structure of a PulseUI app |
| 5 | Several windows | dialogs, shared state, file dialogs |
| 6 | Background work | threads, progress, cancellation, the UI executor |
| 7 | A drawing pad | `Canvas`, mouse events, custom painting |

---

## Part 1: Counter

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  App app;

  pulse::property<int> count{0};                                   // (1) state

  Window win("Counter", 320, 220);
  win.content(VStack{                                              // (2) the view tree
      Text(count.map([](int c) { return std::to_string(c); }))     // (3) bound text
          .font_size(48)
          .text_align(Align::Center),
      HStack{
          Button("-", [&] { count = count - 1; }),                 // (4) actions change state
          Button("Reset", [&] { count = 0; }).flat(),
          Button("+", [&] { count = count + 1; }).primary(),
      }
          .spacing(8)
          .justify(Justify::Center),
  }
                  .spacing(16)
                  .padding(24));
  win.show();

  return app.run();
}
```

**(1) State lives in a `pulse::property`.** A property stores a value and emits `changed` when it
actually changes. It is the single source of truth for the number on screen.

**(2) The UI is a tree of values.** `VStack` arranges its children vertically, `HStack`
horizontally. Children are passed in braces, and modifiers like `.spacing(16)` and `.padding(24)` are
chained. Each modifier returns the same view, so you can keep chaining.

**(3) `count.map(f)` is a stream** of strings: the current text now, and a new text on every change.
`Text` accepts either a plain string or such a stream. Nothing else is needed for the label to stay
correct; there is no `label.set_text(...)` anywhere.

**(4) Buttons change state, never the UI.** The click handler sets `count`; the label follows.
`count = count - 1` reads the property (it converts to `int`) and assigns the result.

Why is it safe to capture `count` by reference? `count` is declared before `win`, so it is destroyed
after the window. When the window closes, its views unmount and drop their subscriptions to `count`
first. As a rule, **declare state before the windows that use it.**

### Try it

- Add a `Button("x2", ...)` that doubles the value.
- Show "even" or "odd" under the number: `Text(count.map([](int c) { return c % 2 ? "odd" : "even"; }))`.
  This returns `const char*`; `Text` accepts anything convertible to `std::string`, including a
  stream of `const char*`.

---

## Part 2: Temperature converter

```cpp
#include <pulseui/pulseui.hpp>
#include <cstdio>
using namespace pulseui;

static std::string fmt(double v, const char* unit) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f %s", v, unit);
  return buf;
}

int main() {
  App app;

  pulse::property<double> celsius{20};
  pulse::computed<double> fahrenheit([&] { return celsius.get() * 9 / 5 + 32; }, celsius);   // (1)

  Window win("Converter", 380, 260);
  win.content(VStack{
      Text("Temperature").title(),
      Slider(celsius, -40, 100).step(0.5),                                      // (2)
      HStack{
          Text(celsius.map([](double c) { return fmt(c, "°C"); })).font_size(20).flex(),
          Text(fahrenheit.map([](double f) { return fmt(f, "°F"); })).font_size(20),
      },
      Text("Below freezing!")
          .color(Color::hex(0x3D7EFF))
          .visible(celsius.map([](double c) { return c < 0; })),                // (3)
      HStack{
          TextField()
              .placeholder("Type a value in °C and press Enter")
              .on_submit([&](const std::string& s) {                            // (4)
                try { celsius = std::stod(s); } catch (...) {}
              })
              .flex(),
          Button("Boil", [&] { celsius = 100; }),
      },
  }
                  .spacing(14)
                  .padding(20));
  win.show();
  return app.run();
}
```

**(1) `computed` derives state.** `fahrenheit` is recomputed whenever `celsius` changes. You list the
sources explicitly (`celsius`) after the function. Derived values should never be stored by hand;
otherwise they eventually go stale.

**(2) `Slider(celsius, ...)` is a two-way binding.** Passing a property by reference (a *model*)
means the slider shows its value **and** writes back when dragged. Click "Boil" and the slider jumps
to 100, because the button changed the property the slider follows. Widgets that edit a value
(`Slider`, `TextField`, `Checkbox`, `Toggle`, `Select`, `Segmented`) all accept a model.

**(3) Any attribute can be a stream.** `.visible(...)`, `.enabled(...)`, `.color(...)`,
`.background(...)` and `.tooltip(...)` accept a value or a stream. Here the warning appears exactly
while the temperature is below zero.

**(4) Events with data.** `on_submit` fires when Enter is pressed and gives you the field's text.
Here the field is **not** bound to a model; it is a plain input whose text you read on submit.

### Value, stream, or model?

| You pass | Example | Meaning |
|---|---|---|
| a value | `Text("Hello")`, `.enabled(false)` | constant |
| a stream | `Text(name.map(...))`, `.enabled(can_save)` | follows the stream (one-way) |
| a model (a `pulse::property` by reference to an editor) | `TextField(name)`, `Slider(volume)` | follows **and** writes back (two-way) |

This one rule covers every widget. The details are in [Reactive bindings](06-reactive-bindings.md).

---

## Part 3: Todo list

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

struct Todo {
  int id = 0;
  std::string title;
  bool done = false;
  bool operator==(const Todo&) const = default;      // (1)
};

int main() {
  App app;

  pulse::property<std::vector<Todo>> todos{{{1, "Learn PulseUI", true}, {2, "Build something"}}};
  pulse::property<std::string> draft;
  pulse::property<int> filter{0};                    // 0 all, 1 active, 2 done
  int next_id = 3;

  pulse::computed<std::vector<Todo>> visible(        // (2)
      [&] {
        std::vector<Todo> out;
        for (const Todo& t : todos.get())
          if (filter.get() == 0 || (filter.get() == 1) == !t.done) out.push_back(t);
        return out;
      },
      todos, filter);

  pulse::computed<std::string> left(
      [&] {
        int n = 0;
        for (const Todo& t : todos.get()) n += !t.done;
        return std::to_string(n) + " left";
      },
      todos);

  auto add = [&] {
    if (draft.get().empty()) return;
    todos.update([&](std::vector<Todo>& v) { v.push_back({next_id++, draft.get()}); });   // (3)
    draft = "";
  };
  auto toggle = [&](int id) {
    todos.update([id](std::vector<Todo>& v) {
      for (Todo& t : v)
        if (t.id == id) t.done = !t.done;
    });
  };
  auto remove = [&](int id) {
    todos.update([id](std::vector<Todo>& v) { std::erase_if(v, [id](const Todo& t) { return t.id == id; }); });
  };

  Window win("Todo", 440, 520);
  win.content(VStack{
      Text("Todos").title(),
      HStack{
          TextField(draft).placeholder("What needs to be done?").on_submit([&](auto&) { add(); }).flex(),
          Button("Add", add).primary().enabled(draft.map([](const std::string& s) { return !s.empty(); })),
      },
      HStack{
          Segmented({"All", "Active", "Done"}, filter),
          Spacer(),
          Text(left).dim(),
      },
      Scroll(ForEach(visible, [&](const Todo& t) {                                        // (4)
               const int id = t.id;                                                       // (5)
               return HStack{
                   Checkbox(t.title, t.done).on_change([&, id](bool) { toggle(id); }).flex(),
                   Button("✕", [&, id] { remove(id); }).flat(),
               }
                   .padding(Insets(8, 2));
             }))
          .flex(),
  }
                  .spacing(12)
                  .padding(20));
  win.show();
  return app.run();
}
```

**(1) Value types with `operator==`.** Properties emit only when the value really changes, so the
element type needs `operator==`. `= default` is enough.

**(2) Filtering is derived state.** `visible` depends on `todos` *and* `filter`. Click a segment, and
`filter` changes, `visible` recomputes, and the list updates. There is no "refresh list" function.

**(3) Collections change through `update`.** `update` gives you a copy to modify and then stores it,
which emits once. Never keep pointers or references into a property's value: `get()` returns a copy.

**(4) `ForEach` renders a list.** It takes a list (value or stream) and a function that builds one
view per item. When the list changes, the rows are rebuilt. `Scroll` makes its content scrollable;
`.flex()` lets it take all the remaining height.

**(5) Identify items by a stable id, not by reference.** Rows are rebuilt on every change, so a row
must not capture `t` by reference: it would point into a vector that no longer exists. Capture the
id and look the item up when the action happens.

> **Why rebuild rows instead of patching them?** It keeps `ForEach` trivial to reason about: the rows
> always match the list exactly. For the list sizes typical of application UIs (hundreds of rows),
> rebuilding is fast. For very large lists, filter or page the data in a `computed`.

---

## Part 4: The todo list as a class

Lambdas in `main` are fine for small programs. Real applications put each window in a class: the
class owns its window and its state, and binds them in the constructor. This is also exactly the
structure that Pulse Layer generates for designed forms.

```cpp
// todo_window.hpp
#pragma once
#include <pulseui/pulseui.hpp>

struct Todo {
  int id = 0;
  std::string title;
  bool done = false;
  bool operator==(const Todo&) const = default;
};

class TodoWindow {
public:
  TodoWindow();
  void show() { window_.show(); }

private:
  pulseui::Node build();                      // the view tree
  pulseui::Node row(const Todo& t);
  void add();
  void toggle(int id);

  // state first...
  pulse::property<std::vector<Todo>> todos_;
  pulse::property<std::string> draft_;
  int next_id_ = 1;
  pulse::computed<std::string> summary_;

  // ...the window last: it is destroyed first, so no view outlives the state it shows
  pulseui::Window window_{"Todo", 440, 520};
};
```

```cpp
// todo_window.cpp
#include "todo_window.hpp"
using namespace pulseui;

TodoWindow::TodoWindow()
    : summary_([this] { return std::to_string(todos_.get().size()) + " items"; }, todos_) {
  window_.content(build());
  window_.shortcut("Ctrl+N", [this] { if (auto* f = window_.find("draft")) f->focus(); });
}

Node TodoWindow::build() {
  return VStack{
      HStack{
          TextField(draft_).placeholder("New item").on_submit([this](auto&) { add(); }).id("draft").flex(),
          Button("Add", [this] { add(); }).primary(),
      },
      Scroll(ForEach(todos_, [this](const Todo& t) { return row(t); })).flex(),
      Text(summary_).dim(),
  }
      .spacing(10)
      .padding(16);
}

Node TodoWindow::row(const Todo& t) {
  const int id = t.id;
  return Checkbox(t.title, t.done).on_change([this, id](bool) { toggle(id); });
}

void TodoWindow::add() {
  if (draft_.get().empty()) return;
  todos_.update([&](std::vector<Todo>& v) { v.push_back({next_id_++, draft_.get()}); });
  draft_ = "";
}

void TodoWindow::toggle(int id) {
  todos_.update([id](std::vector<Todo>& v) {
    for (Todo& t : v)
      if (t.id == id) t.done = !t.done;
  });
}
```

```cpp
// main.cpp
#include "todo_window.hpp"

int main() {
  pulseui::App app;
  TodoWindow w;
  w.show();
  return app.run();
}
```

The rules this structure follows:

- **Capturing `this` is safe** because every view lives inside `window_`, a member. Views cannot
  outlive the object.
- **Member order is lifetime order.** Members are destroyed in reverse order of declaration. Putting
  `window_` last destroys the views first, then the state they were bound to.
  (Pulse Layer's generated classes declare the window first; that is also safe, because bindings
  tolerate destroyed sources. Pick one convention and keep it.)
- **`computed` members are initialized in the constructor's initializer list**, after their sources.
- **Ids give you access to views after building.** `.id("draft")` and later
  `window_.find<TextField>("draft")` (or the strict `window_.get<TextField>("draft")`) let you focus or
  configure a view. Forms designed in Pulse Layer go one step further: bindings are set in the designer
  and views are reached through typed members (`ui_.draft`), see the next chapter.

---

## Part 5: Several windows

```cpp
#include <pulseui/pulseui.hpp>
#include <fstream>
#include <memory>
#include <sstream>
using namespace pulseui;

struct Settings {                                       // shared state
  pulse::property<bool> dark{true};
  pulse::property<double> font_size{14};
};

class SettingsWindow {
public:
  explicit SettingsWindow(Settings& s) {
    window_.content(VStack{
        Text("Settings").title(),
        Toggle("Dark theme", s.dark),
        HStack{Text("Font size"), Slider(s.font_size, 10, 24).step(1).flex()}.align_items(Alignment::Center),
        Spacer(),
        HStack{Spacer(), Button("Close", [this] { window_.hide(); }).primary()},
    }.spacing(12).padding(20));
    window_.on_close = [this] { window_.hide(); return false; };   // (1) closing only hides
  }
  void show() { window_.show(); }

private:
  Window window_{"Settings", 320, 220};
};

class Editor {
public:
  explicit Editor(Settings& s) : settings_(s) {
    window_.content(VStack{
        HStack{
            Button("Open...", [this] { open(); }),
            Button("Settings", [this] {                                  // (2) created on demand
              if (!settings_window_) settings_window_ = std::make_unique<SettingsWindow>(settings_);
              settings_window_->show();
            }),
            Spacer(),
            Text(path_).dim(),
        }.align_items(Alignment::Center),
        CodeEditor(text_).line_numbers(false).flex(),
    }.spacing(8).padding(12));
    window_.closed.connect([] { App::quit(); });                         // (3)
  }
  void show() { window_.show(); }

private:
  void open() {
    auto file = dialogs::open_file(&window_, "Open a text file", {{"Text files", "*.txt;*.md"}});
    if (!file) return;                                                   // cancelled
    std::ifstream in(*file);
    std::stringstream ss;
    ss << in.rdbuf();
    text_ = ss.str();
    path_ = *file;
  }

  Settings& settings_;
  pulse::property<std::string> text_, path_;
  std::unique_ptr<SettingsWindow> settings_window_;
  Window window_{"Editor", 720, 480};
};

int main() {
  App app;
  Settings settings;
  settings.dark.changed.connect([](bool d) { theme() = d ? Theme::dark() : Theme::light(); });   // (4)

  Editor editor(settings);
  editor.show();
  return app.run();
}
```

**(1) Hiding instead of closing.** `on_close` runs when the user clicks the window's close button.
Returning `false` vetoes closing; here the window hides, and its state (and position) survive until
it is shown again.

**(2) Create secondary windows on demand,** and keep them in a `std::unique_ptr` member, so the
owner controls their lifetime.

**(3) Quit explicitly.** The app quits automatically when the last window *closes*. Hidden windows
still count as open, so the main window quits the app when it closes.

**(4) Shared state connects windows.** Both windows use the same `Settings` object. Flip the toggle in
the settings window and the theme changes everywhere: `theme()` is itself a property, and all windows
repaint when it changes.

`dialogs::open_file` / `dialogs::save_file` show the native file dialogs and return
`std::optional<std::string>`.

---

## Part 6: Background work

Long work must not run on the UI thread, or the window freezes. PulseUI's bindings make the hand-off
back to the UI easy: **a property may be set from any thread, and every view bound to it updates on
the UI thread.**

```cpp
#include <pulseui/pulseui.hpp>
#include <thread>
using namespace pulseui;
using namespace std::chrono_literals;

class Job {
public:
  pulse::property<double> progress{0};
  pulse::property<bool> running{false};
  pulse::signal<std::string> finished;

  void start() {
    if (running.get()) return;
    running = true;
    worker_ = std::jthread([this](std::stop_token stop) {         // a worker thread
      for (int i = 1; i <= 100 && !stop.stop_requested(); ++i) {
        std::this_thread::sleep_for(30ms);                          // "work"
        progress = i / 100.0;                                        // safe from any thread
      }
      running = false;
      finished(stop.stop_requested() ? "Cancelled" : "Done");
    });
  }
  void cancel() { worker_.request_stop(); }

private:
  std::jthread worker_;   // joins on destruction
};

int main() {
  App app;
  Job job;
  pulse::property<std::string> status{"Idle"};

  // Slots of a plain signal run on the emitting (worker) thread.
  // A queued connection delivers them on the UI thread instead:
  job.finished.connect(App::ui(), [&](const std::string& s) { status = s; });

  Window win("Background job", 380, 200);
  win.content(VStack{
      ProgressBar(job.progress),                                     // updates on the UI thread
      Text(status),
      HStack{
          Button("Start", [&] { status = "Working..."; job.start(); }).primary()
              .enabled(job.running.map([](bool r) { return !r; })),
          Button("Cancel", [&] { job.cancel(); }).enabled(job.running),
      },
  }.spacing(12).padding(20));
  win.show();
  return app.run();
}
```

The rules:

| From a worker thread you may... | ...and it is safe because |
|---|---|
| set a `pulse::property` that views are bound to | view bindings deliver values to the UI thread |
| emit a signal | but its plain slots run on the worker. Use `connect(App::ui(), slot)` for UI work |
| call `App::post(fn)` | it runs `fn` on the UI thread |

Never call view methods (such as `set_text`) directly from a worker thread.

### Debounced work with Pulse operators

Streams can be used directly in bindings. A search field that only searches when typing pauses:

```cpp
pulse::property<std::string> query;
auto results = query.as_observable()
             | pulse::debounce(300ms, App::ui())                     // the UI executor
             | pulse::map([](const std::string& q) { return search_titles(q); });   // std::vector<std::string>

VStack{
  TextField(query).placeholder("Search"),
  Scroll(ForEach(results, [](const std::string& s) { return Text(s); })).flex(),
};
```

`App::ui()` is a `pulse::executor` that runs tasks on the UI thread. Pass it wherever a Pulse
operator asks for an executor.

---

## Part 7: A drawing pad

`Canvas` lets you draw anything with a `Painter`. Combined with mouse events and a property, you get
a small paint program:

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  App app;

  using Stroke = std::vector<Point>;
  pulse::property<std::vector<Stroke>> strokes;
  pulse::property<double> width{3};
  bool drawing = false;

  auto pad = Canvas([&](Painter& p, const Rect& r) {
                 p.fill_rect(r, th().input, th().radius);                     // background
                 for (const Stroke& s : strokes.get())
                   if (s.size() > 1) p.polyline(s, th().accent, static_cast<float>(width.get()));
               })
                 .redraw_on(strokes)                                          // (1)
                 .redraw_on(width)
                 .on_event_fn([&](const InputEvent& e) {                      // (2)
                   if (e.type == InputEvent::MouseDown && e.button == MouseButton::Left) {
                     drawing = true;
                     strokes.update([&](auto& v) { v.push_back({e.pos}); });
                   } else if (e.type == InputEvent::MouseMove && drawing) {
                     strokes.update([&](auto& v) { v.back().push_back(e.pos); });
                   } else if (e.type == InputEvent::MouseUp) {
                     drawing = false;
                   }
                 })
                 .cursor(Cursor::Crosshair)
                 .flex();

  Window win("Sketch", 640, 480);
  win.content(VStack{
      HStack{
          Text("Width"),
          Slider(width, 1, 12).width(160),
          Spacer(),
          Button("Undo", [&] { strokes.update([](auto& v) { if (!v.empty()) v.pop_back(); }); }),
          Button("Clear", [&] { strokes = std::vector<Stroke>{}; }).danger(),
      }.align_items(Alignment::Center),
      std::move(pad),
  }.spacing(10).padding(12));
  win.show();
  return app.run();
}
```

**(1) `redraw_on(source)`** repaints the canvas whenever the source emits. The draw function reads the
current state; it does not need to know what changed.

**(2) `on_event_fn`** receives every input event delivered to the view. `e.pos` is in the view's own
coordinates, which are the same coordinates the draw function uses.

Notice the pattern again: input changes **state** (`strokes`), and drawing is a pure function of
state. Undo is just "remove the last stroke from the state".

---

## Where to go next

- Design windows visually and bind them in code: [Tutorial: forms with Pulse Layer](04-forms-tutorial.md).
- Learn the layout rules thoroughly: [Layout](07-layout.md).
- See every widget and its options: [Widgets](08-widgets.md).
- Structure bigger apps: [Patterns, pitfalls and FAQ](17-patterns-and-faq.md).
