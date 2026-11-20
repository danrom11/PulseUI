# 11. Input and events

Most of the time you never see raw input: buttons give you `clicked`, text fields give you text, sliders
give you values. This chapter is for when you need more, such as clickable custom views, hover
effects, drag gestures, keyboard handling or global key streams.

## `InputEvent`

All input arrives as one struct, so it can flow through a single stream and be filtered with Pulse
operators.

```cpp
struct InputEvent {
  enum Type { MouseDown, MouseUp, MouseMove, MouseLeave, Wheel, KeyDown, KeyUp, Text } type;
  Point pos;              // in the receiving view's coordinates (window coordinates for window.input())
  Point window_pos;       // always window coordinates
  MouseButton button;     // None, Left, Right, Middle
  int clicks;             // 1, or 2 for a double click
  float wheel_dx, wheel_dy;   // wheel / trackpad delta in "lines"
  Key key;                // for KeyDown / KeyUp
  unsigned mods;          // Mod::Shift | Mod::Ctrl | Mod::Alt | Mod::Super
  bool repeat;            // auto-repeated key
  std::string text;       // UTF-8 text for Text events (typed characters)

  bool is_mouse() const;  bool is_key() const;
  bool primary() const;   // Ctrl on Windows, Cmd on macOS
};
```

`Mod::Primary` is `Mod::Ctrl` on Windows and `Mod::Super` (Cmd) on macOS. Prefer `e.primary()` for
shortcuts so they feel native on both systems.

**Key versus Text events.** `KeyDown` reports *keys* (`Key::A`, `Key::Enter`, arrows), good for
commands. `Text` reports *characters* after keyboard layout and dead keys (`"é"`, `"Я"`, `"€"`), good
for text input. A text widget uses both: `KeyDown` for navigation and editing, `Text` for inserting.

## How events travel

### Mouse

1. **Hit testing** finds the deepest visible view under the pointer. Open popups are tested first;
   among siblings, the last child (drawn on top) wins.
2. The event **bubbles** from that view up through its parents. For each enabled view on the way:
   - its `events()` signal is emitted (this is what `on_event_fn` connects to);
   - its `on_event(e)` is called; returning `true` consumes the event and stops bubbling.
   Disabled views are skipped.
3. **Capture**: the view that consumes a `MouseDown` receives all mouse moves and the `MouseUp` until the
   button is released, even outside its bounds. This makes dragging reliable.
4. **Focus**: on `MouseDown`, the nearest focusable, enabled ancestor of the hit view gets keyboard focus.
5. **Hover**: views under the pointer get `set_hovered(true)`, which triggers `on_hover` handlers and a
   repaint for interactive views. `MouseLeave` clears hover when the pointer leaves the window.
6. **Popups**: a `MouseDown` outside all open popups closes them and is not delivered further.

`e.pos` is converted to each view's local coordinates as the event bubbles.

### Keyboard

1. `KeyDown`, `KeyUp` and `Text` go to the **focused view** first and bubble up from there.
2. If nobody consumed a `KeyDown`: Escape closes the topmost popup; otherwise the window's
   **shortcuts** are checked; otherwise Tab / Shift+Tab move the focus.

### Wheel

Wheel events go to the view under the pointer and bubble up. `Scroll` consumes them while it can
scroll in that direction, so nested scroll areas work naturally.

## Handling events on any view

### Clicks: `on_click`

```cpp
Text("Show details").color(th().accent).on_click([&] { expanded = !expanded.get(); })
VStack{ Text(title).bold(), Text(subtitle).dim() }.padding(10).radius(6).on_click([&, id] { select(id); })
```

`on_click` makes any view clickable: press and release over the view, with the left button. It also
sets the hand cursor and makes hovering repaint.

### Hover: `on_hover`

```cpp
pulse::property<bool> hot{false};
Panel{ ... }
    .on_hover([&](bool inside) { hot = inside; })
    .border(hot.map([](bool h) { return h ? th().accent : th().border; }));
```

### Everything: `on_event_fn`

```cpp
Canvas(draw).on_event_fn([&](const InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown: start_drag(e.pos); break;
    case InputEvent::MouseMove: if (dragging) drag_to(e.pos); break;
    case InputEvent::MouseUp:   end_drag(); break;
    case InputEvent::Wheel:     zoom(e.wheel_dy); break;
    default: break;
  }
})
```

`on_event_fn` *observes* events; it does not consume them, so they keep bubbling. To consume events
(for example to capture the mouse for dragging), write a custom view and override `on_event`; see
[Custom views](12-custom-drawing.md#writing-a-custom-view).

The underlying `view.events()` is a `pulse::signal<InputEvent>`, so you can also use it as a stream:

```cpp
auto drags = canvas->events().as_observable()
           | pulse::filter([](const InputEvent& e) { return e.type == InputEvent::MouseMove; })
           | pulse::throttle(16ms);
```

## Keyboard focus

- Focusable views: `Button`, `TextField`, `Checkbox`, `Toggle`, `Slider`, `Select`, `Segmented`,
  `CodeEditor`, and any view with `.focusable()`.
- `view->focus()` focuses a view; `window.focus_view()` tells you which one has focus.
- Tab / Shift+Tab cycle through focusable views in tree order.
- Focused controls draw a focus ring with `th().border_focus`.
- Views can react to focus changes by overriding `on_focus(bool)` (custom views), or through
  `TextField::on_commit` (fired on focus loss).

```cpp
win.shortcut("Ctrl+F", [&] { if (auto* f = win.find("search")) f->focus(); });
```

## Shortcuts

```cpp
win.shortcut("Ctrl+S", save);
win.shortcut("Ctrl+Shift+Z", redo);
win.shortcut("F1", help);
```

See [App and windows](10-app-and-windows.md#keyboard-shortcuts) for the key names. `Shortcut::parse`
and `Shortcut::matches(e)` are public if you want to match shortcuts in your own handlers.

## The window input stream

`window.input()` is a `pulse::observable<InputEvent>` of **every** input event of the window, before
it is dispatched, in window coordinates. It is ideal for global behaviour expressed with Pulse
operators:

```cpp
// idle detection: no input for 5 minutes
auto idle = win.input()
          | pulse::debounce(std::chrono::minutes(5), App::ui())
          | pulse::map([](const InputEvent&) { return true; });

// a status bar showing the pointer position
auto pos = win.input()
         | pulse::filter([](const InputEvent& e) { return e.type == InputEvent::MouseMove; })
         | pulse::throttle_latest(50ms, App::ui())
         | pulse::map([](const InputEvent& e) {
             return std::to_string(int(e.window_pos.x)) + ", " + std::to_string(int(e.window_pos.y));
           });
Text(pos).dim();
```

Observing the stream does not prevent normal dispatch.

## Cursors

`.cursor(Cursor::X)` sets the mouse cursor shown over a view: `Arrow`, `IBeam`, `Hand`, `Crosshair`,
`SizeWE`, `SizeNS`, `SizeNWSE`, `SizeNESW`, `SizeAll`. Text fields use `IBeam`, clickable views use `Hand`.

## Injecting events

`window.send(InputEvent)` dispatches an event as if the user produced it. This is the basis of UI tests
and automation; see [Testing](15-testing.md).

Next: [Custom drawing and custom views](12-custom-drawing.md).
