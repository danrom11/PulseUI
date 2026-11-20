# 1. Introduction

## The reactive idea

In a classic widget toolkit you build widgets once and then **change them by hand** whenever the
data changes:

```cpp
// classic, imperative
void on_add_clicked() {
  items.push_back(new_item);
  list_widget->addItem(new_item.title);          // keep the widget in sync
  counter_label->setText(QString::number(items.size()) + " items");   // and this one
  clear_button->setEnabled(!items.empty());      // and this one
  save_button->setEnabled(true);                 // and remember this one too
}
```

Every handler has to know every widget that depends on the data. Forget one, and the UI shows
stale information. Add a widget, and you must update every handler.

PulseUI flips this around. **Views declare what they show; data does not know about views.**

```cpp
// PulseUI, reactive
pulse::property<std::vector<Item>> items;
pulse::computed<bool> has_items([&] { return !items.get().empty(); }, items);

VStack{
  ForEach(items, [](const Item& i) { return Text(i.title); }),
  Text(items.map([](const auto& v) { return std::to_string(v.size()) + " items"; })),
  Button("Clear", [&] { items = std::vector<Item>{}; }).enabled(has_items),
};

void on_add_clicked() { items.update([&](auto& v) { v.push_back(new_item); }); }   // that is all
```

The handler changes the data. The list, the label and the button follow on their own, because they
are bound to it. Adding another view that depends on `items` needs no change to any handler.

## What PulseUI is, and what it is not

PulseUI is:

- **Declarative.** The UI is a tree of values: `VStack{ Text(...), Button(...) }`. You can build it
  in functions, return it, store it and compose it.
- **Reactive.** Any view attribute can be a plain value *or* a stream: `Text("Hi")` and
  `Text(name_property)` are the same type of view.
- **Small.** A couple of dozen view types, one layout model (flex stacks), one event model.
- **Native.** It draws with Direct2D/DirectWrite on Windows and CoreGraphics/AppKit on macOS, with
  proper HiDPI, native text rendering, native file dialogs and the system clipboard.
- **Designable.** Every standard view is described by a small reflection layer, so the
  Pulse Layer designer can edit forms visually and save
  them as JSON or generate C++.

PulseUI is **not**:

- a port or clone of any existing framework. There are no object hierarchies to subclass, no
  meta-object compiler, no signals-and-slots macros, and no model/view classes;
- a retained scene graph with animations and 3D. It is for application UIs: tools, editors,
  dashboards and utilities;
- a web view. Nothing runs in a browser engine.

## How the pieces fit

```
                         your code
        ┌───────────────────────────────────────────┐
        │  state: pulse::property / computed /      │
        │         observable / signal               │
        └──────────────┬────────────────────────────┘
                       │ Bind<T>  (values or streams)
        ┌──────────────▼────────────────────────────┐
        │  views: VStack, Text, Button, TextField,  │   modifiers: .padding() .flex()
        │         ForEach, Show, Canvas ...         │   .enabled(stream) .visible(stream)
        └──────────────┬────────────────────────────┘
                       │ mounted into
        ┌──────────────▼────────────────────────────┐
        │  Window: layout, painting, input dispatch,│   shortcuts, focus, popups,
        │          focus, tooltips                   │   input() stream
        └──────────────┬────────────────────────────┘
                       │
        ┌──────────────▼────────────────────────────┐
        │  App: event loop, UI executor, timers     │
        └──────────────┬────────────────────────────┘
                       │
        ┌──────────────▼────────────────────────────┐
        │  platform: Win32 + Direct2D / Cocoa + CG  │
        └───────────────────────────────────────────┘
```

### Views are descriptions until mounted

`Text("Hello")` creates a small object that *describes* a text view. It can be moved around freely,
which is what makes `VStack{ ... }` and returning views from functions work. When a description is
placed into a window with `window.content(...)`, it is **mounted**: its bindings subscribe to their
streams, and it starts reacting. When it is removed or the window closes, it is **unmounted** and every
subscription it made is cancelled automatically.

This is why you never write cleanup code for bindings, and why views cannot leak subscriptions.

### One thread for the UI

All views live on the UI (main) thread. Your data can change on any thread: bindings automatically
deliver values to the UI thread before touching a view. See
[Reactive bindings](06-reactive-bindings.md#threads).

### Layout is simple and predictable

There is essentially one layout primitive: a **stack** that arranges children along one axis
(vertical or horizontal). Children keep their preferred size, or share the free space with `flex`.
`Layer` stacks children on top of each other, which is used for overlays and free positioning.
That is enough for almost every application layout, and you can predict the result without running
the program. See [Layout](07-layout.md).

## A complete program

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  App app;

  pulse::property<std::string> name;
  auto greeting = name.map([](const std::string& n) {
    return n.empty() ? std::string("What is your name?") : "Hello, " + n + "!";
  });

  Window win("Greeter", 360, 160);
  win.content(VStack{
      Text(greeting).title(),
      TextField(name).placeholder("Type your name"),
  }.spacing(12).padding(20));
  win.show();

  return app.run();
}
```

Type into the field: the title changes with every keystroke. The text field writes into `name`
(two-way binding); `greeting` is a stream derived from `name`; `Text` displays the stream.

Next: [Getting started](02-getting-started.md).
