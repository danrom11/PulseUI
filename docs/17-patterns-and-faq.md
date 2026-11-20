# 17. Patterns, pitfalls and FAQ

## Structuring an application

### One class per window, one model per concern

```
app/
├── main.cpp                 App, the shared model, the main window
├── model/
│   ├── document.hpp         pulse::property / computed / signal; no UI includes
│   └── settings.hpp
└── ui/
    ├── mainwindow.hpp/.cpp  owns a Window, binds it to the model
    └── settingsdialog.hpp/.cpp
```

- **Models** hold state as `pulse::property`, derived state as `pulse::computed`, and events as
  `pulse::signal`. They do not include PulseUI, so they are reusable and unit-testable.
- **Window classes** own a `pulseui::Window` and bind views to the model in their constructor. They
  contain no business logic, only "this view shows that property" and "this button calls that
  method".
- **`main.cpp`** creates the `App`, the models and the main window, and connects app-wide reactions
  such as the theme.

### Build views in small functions

```cpp
Node MainWindow::toolbar() { return HStack{ ... }; }
Node MainWindow::sidebar() { return VStack{ ... }.width(240); }
Node MainWindow::build()   { return VStack{ toolbar(), HStack{ sidebar(), editor().flex() }.flex(), status_bar() }; }
```

Functions returning `Node` compose like components, without any component framework.

### Reusable components are functions with parameters

```cpp
Node labeled_slider(std::string label, pulse::property<double>& value, double lo, double hi) {
  return HStack{
      Text(std::move(label)).width(90),
      Slider(value, lo, hi).flex(),
      Text(value.map([](double v) { return std::to_string(int(v)); })).width(40).text_align(Align::RightCenter),
  }.align_items(Alignment::Center);
}
```

When a component needs its own state, make it a class that owns the state and returns its view from a
method, and keep the object alive as long as the view (usually as a member of the window class).

### Commands: one place per action

When a menu item, a toolbar button and a shortcut trigger the same action, write it once as a method
and an "enabled" computed, and bind all three:

```cpp
pulse::computed<bool> can_save;    // dirty && !busy

Button("Save", [this] { save(); }).enabled(can_save);
window_.shortcut("Ctrl+S", [this] { if (can_save.get()) save(); });
```

## Pitfalls

### Wiring designed forms with string lookups

```cpp
if (auto* f = window_.find<TextField>("emial")) f->model(email);   // typo: silently does nothing
```

For forms made in Pulse Layer, bind views in the designer and use the generated typed pointers
(`ui_.email`). Both are checked by the compiler. For forms loaded at runtime, use `window.get<T>(id)`,
which throws a descriptive error instead of returning `nullptr`.

### Calling a signal instead of connecting to it

```cpp
button->clicked([] { ... });           // WRONG: tries to emit, does not compile
button->clicked.connect([] { ... });   // right
button->on_click([] { ... });          // right (modifier)
```

### Keeping references into rebuilt lists

`ForEach` rebuilds its rows when the list changes. Row callbacks must capture ids or values, never
references or pointers to items in the list.

### Iterating `property::get()` twice

`get()` returns a copy. `std::count(p.get().begin(), p.get().end(), x)` compares iterators of two
different temporaries and crashes. Take one copy: `const auto v = p.get();`. The same applies to
returning pointers into `get()`'s result.

### Capturing theme colors at build time

`.background(th().surface)` stores today's color. If your app switches themes, bind to the theme
instead: `.background(theme().map([](const Theme& t) { return t.surface; }))`.

### Touching views from worker threads

Set properties from workers (safe), never call view methods. Slots of plain signals run on the emitting
thread; connect them with `App::ui()` if they touch the UI.

### State declared after the window

```cpp
Window win("x", 300, 200);
pulse::property<int> count;       // destroyed before win: views briefly outlive their model
```

Declare state first. In classes, declare the window member last or make sure models outlive it.

### A `Scroll` that does not scroll

A `Scroll` without `flex()` or a fixed height takes the full height of its content, so there is
nothing to scroll. Give it `.flex()` in a stack.

### A flexible view collapses to nothing

Flexible children start from their minimum size and share the *free* space. If there is no free space,
they get their minimum, which is usually 0. Give them a `min_width` / `min_height`, or make the window
larger.

### Wrapping text that does not wrap

`Text(...).wrap()` wraps to the width it is given. In an `HStack`, give the text `flex()` (or a width);
otherwise it is measured with unlimited width.

### Forgetting `operator==` on model types

`pulse::property<T>` and `pulse::computed<T>` compare values to decide whether to notify. Add
`bool operator==(const T&) const = default;` to your structs.

### Discarding subscriptions

`App::every(...)`, `App::after(...)` and `observable::subscribe(...)` return subscriptions; discarding
them cancels immediately. Keep them as members, or `.release()` one-shot timers deliberately.

## FAQ

**Is there a designer?**
Yes: Pulse Layer, written in PulseUI. It edits `.pform` forms, manages multi-form projects, generates
C++ and builds and runs your program. See the [forms tutorial](04-forms-tutorial.md).

**Can I use PulseUI without Pulse operators?**
Yes. Properties, computed values and signals cover most applications. Operators are there when you
need time (debounce, throttle) or stream combination.

**How do I show a modal dialog?**
Create a second `Window`, show it, and disable the parent's content while it is open:
`main_root->enabled(dialog_open.map([](bool o) { return !o; }))`. Re-enable it when the dialog closes
(`dialog.closed.connect(...)` or the dialog's own button). File dialogs (`dialogs::open_file` and
`save_file`) are natively modal.

**How do I make a menu?**
Use a `flat()` `Button` that opens a popup with `window.open_popup(...)`, containing flat buttons. See
[App and windows](10-app-and-windows.md#popups).

**How do I display a table?**
A `Scroll(ForEach(rows, ...))` where each row is an `HStack` of fixed-width `Text` cells, with an
`HStack` header above. Alternate row backgrounds with the index variant of `ForEach`:
`[](const Row& r, std::size_t i) { return HStack{...}.background(i % 2 ? th().surface_alt : th().surface); }`.

**How do I animate?**
Drive a property with `App::every(16ms, ...)` and bind views (or a `Canvas`) to it. Stop the timer by
resetting its subscription when the animation ends.

**How large can lists get?**
`ForEach` creates a view per item and rebuilds all of them on change. Hundreds to a few thousand
simple rows are fine. For more, show a filtered or paged subset (a `computed` that returns the visible
page), or draw the list yourself in a `Canvas`.

**Can views be shared between windows?**
No: a view belongs to exactly one parent. Build the view twice (a function returning `Node`) and bind
both to the same state.

**Where do I report bugs or contribute?**
On the GitHub repository: <https://github.com/danrom11/PulseUI>.

Next: [Cheat sheet](18-cheatsheet.md).
