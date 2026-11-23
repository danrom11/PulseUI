# Animation

Changes in PulseUI are instant by default. Three tools make them smooth - and all three stay
declarative: you still bind values; the views glide to them.

## `.animate()`: views that glide

```cpp
Panel{...}
    .background(selected.map([](bool s) { return s ? th().selection : th().surface; }))
    .opacity(visible.map([](bool v) { return v ? 1.f : 0.f; }))
    .animate()                                   // 200 ms, ease-out
Text(status).animate(300ms, Easing::EaseInOut)
```

On a view with `.animate()`:

- **its frame** (position and size) moves to each new layout over the transition - so a list whose row
  was removed closes the gap smoothly, and a panel that grows pushes the others gradually;
- **opacity**, **background** and **border color** set through bindings (or `set_opacity`,
  `set_background`, `set_border_color`) fade to the new value.

The first layout of a view is never animated (it appears where it belongs).

## `Show(...).transition()`

```cpp
Show(expanded, details).transition()                         // expand / collapse
Show(editing, editor(), preview()).transition(250ms, Easing::EaseInOut)  // cross-fade
```

The branches cross-fade and the `Show` itself grows or shrinks between their sizes, so the views around
it move smoothly too. See [Layout](07-layout.md#show-conditional-content).

## `tween(...)`: streams that glide

`tween` turns a property or stream into a stream that moves smoothly from the current value to every new
one. Use it for any bindable attribute:

```cpp
ProgressBar(tween(progress, 400ms))                 // double
Text("Status").color(tween(status_color))           // Color
Canvas(draw).redraw_on(tween(zoom))                 // and read the tweened value in draw
```

It works for `double`, `float`, `Color`, `Point`, `Size` and `Rect` (anything with an `anim::lerp`
overload). The first value comes through as is.

## Easing and timing

`Transition{duration, easing}`; easings: `Linear`, `EaseIn`, `EaseOut` (default), `EaseInOut`, `Spring`
(overshoots slightly and settles).

All animations share one frame timer (about 60 fps) that only exists while something animates.

```cpp
anim::set_speed(0);  // no animations: everything jumps to its final state (tests, "reduce motion")
anim::set_speed(2);  // twice as slow (handy to inspect a transition)
```

For custom effects, `anim::run(Transition, step, done)` calls `step(progress)` every frame with the eased
progress `0..1`; the returned subscription stops it early:

```cpp
pulse::subscription spin = anim::run(Transition(600ms, Easing::Spring), [&](float t) {
  angle = 360 * t;
  canvas->invalidate();
});
```

Built-in widgets animate where it helps: the `Toggle` knob slides, toasts fade in and out.

Next: [Drag and drop](23-drag-and-drop.md).
