# 12. Custom drawing and custom views

When the built-in widgets are not enough (charts, gauges, editors, games, diagrams), you have two
tools:

- **`Canvas`**: a view with a draw function. Quick and usually enough.
- **A custom view class**: derive from `ViewT<YourView>` for full control over measuring, painting,
  input and modifiers.

## `Canvas`

```cpp
Canvas([&](Painter& p, const Rect& r) {
  p.fill_rect(r, th().surface, 8);
  p.text(r, "Hello", th().font_title, th().text, Align::Center);
})
    .redraw_on(data)          // repaint whenever `data` emits
    .height(200);
```

| | |
|---|---|
| `explicit Canvas(std::function<void(Painter&, const Rect&)> draw)` | `r` is the content rect (bounds minus padding) |
| `redraw_on(pulse::observable<T>)` | repaint on every emission |
| `redraw_on(source)` | any `property`, `computed` or object with `as_observable()` |

The draw function is called during painting, on the UI thread. **It should read state and draw,
nothing else**: no layout changes, no property writes. It is called whenever the window repaints,
which happens more often than your data changes.

A canvas has no preferred size: give it `width`/`height` or `flex()`.

### Example: a live sparkline

```cpp
pulse::property<std::vector<double>> samples;

Canvas([&](Painter& p, const Rect& r) {
  const auto v = samples.get();                         // take one copy
  if (v.size() < 2) return;
  const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
  const double span = std::max(1e-9, *hi - *lo);
  std::vector<Point> pts;
  for (std::size_t i = 0; i < v.size(); ++i)
    pts.push_back({r.x + r.w * float(i) / float(v.size() - 1),
                   r.bottom() - r.h * float((v[i] - *lo) / span)});
  p.polyline(pts, th().accent, 2);
})
    .redraw_on(samples)
    .height(60);
```

## `Painter`

The drawing API. All coordinates are logical pixels, relative to the view's top-left corner. HiDPI
scaling and antialiasing are handled by the backend.

### State

| Method | |
|---|---|
| `save()`, `restore()` | push / pop transform, clip and opacity |
| `translate(dx, dy)` | move the origin |
| `clip(Rect)` | intersect the clip with a rectangle |
| `set_opacity(a)` | multiply the opacity of everything drawn afterwards |

### Shapes

| Method | |
|---|---|
| `fill_rect(Rect, Color, radius = 0)` | filled (rounded) rectangle |
| `stroke_rect(Rect, Color, width = 1, radius = 0)` | outline |
| `fill_ellipse(Rect, Color)`, `stroke_ellipse(Rect, Color, width = 1)` | ellipse in a rect (a circle if square) |
| `line(Point a, Point b, Color, width = 1)` | |
| `polyline(points, Color, width = 1, closed = false)` | connected lines, round joins |
| `fill_polygon(points, Color)` | filled polygon (areas under charts, arrows) |
| `fill_gradient(Rect, Color top, Color bottom, radius = 0)` | vertical gradient |

### Text

```cpp
void text(const Rect& r, std::string_view s, const Font& f, Color c, int align = Align::LeftCenter, bool wrap = false);
```

The text is placed inside `r` according to `align` (`Align::Left/HCenter/Right` combined with
`Top/VCenter/Bottom`, or `Center`, `LeftCenter`, `RightCenter`). Single-line text that does not fit is
elided with "…"; with `wrap = true` it wraps within `r.w`.

Measuring text (also available outside painting, for layout):

```cpp
Size measure_text(std::string_view s, const Font& f, float max_width = 0);   // max_width > 0 wraps
float line_height(const Font& f);
std::vector<float> caret_stops(std::string_view s, const Font& f);           // x of each code point boundary
```

### Images

```cpp
void image(const Image& img, const Rect& dst, float opacity = 1.f);
```

`Image` is a cheap-to-copy, CPU-side RGBA8 image:

| | |
|---|---|
| `Image::load(path)` | decode PNG, JPEG, BMP, GIF, TIFF... with the OS codecs |
| `Image::from_rgba(w, h, pixels)` | from raw RGBA8 (straight alpha, top-down rows) |
| `save_png(path)` | |
| `valid()`, `width()`, `height()`, `pixels()` | |

The backend uploads an image to the GPU once and caches it, keyed by the image data. Keep the same
`Image` object for repeated drawing instead of reloading it every frame.

## Writing a custom view

Derive from `ViewT<YourView>`. The CRTP base gives your view every common modifier, returning
`YourView&` / `YourView&&` so chains keep the concrete type.

```cpp
// A circular gauge: value 0..1, a label, clickable to reset.
class Gauge : public pulseui::ViewT<Gauge> {
public:
  explicit Gauge(pulseui::Bind<double> value) {
    bind(value, [](View& v, double x) { static_cast<Gauge&>(v).set_value(x); });   // (1)
    hover_repaint_ = true;
  }
  const char* type_name() const override { return "Gauge"; }

  PULSEUI_MOD(Gauge, label, (std::string s), label_ = std::move(s);)               // (2)
  PULSEUI_MOD(Gauge, color, (pulseui::Color c), color_ = c;)

  pulse::signal<> reset;                                                           // (3)

  void set_value(double v) {
    value_ = std::clamp(v, 0.0, 1.0);
    invalidate();                                                                  // (4)
  }

  pulseui::Size measure_content(pulseui::Size) override { return {120, 120}; }    // (5)

  void paint_content(pulseui::Painter& p) override {                               // (6)
    using namespace pulseui;
    const Rect r = content_rect();
    const float d = std::min(r.w, r.h) - 8;
    const Rect circle{r.center().x - d / 2, r.center().y - d / 2, d, d};
    p.stroke_ellipse(circle, th().control, 8);
    std::vector<Point> arc;
    for (int i = 0; i <= 64; ++i) {
      const float a = -1.5708f + 6.2832f * float(value_) * float(i) / 64.f;
      arc.push_back({circle.center().x + d / 2 * std::cos(a), circle.center().y + d / 2 * std::sin(a)});
    }
    if (value_ > 0) p.polyline(arc, hovered() ? th().accent_hover : color_, 8);
    p.text(r, std::to_string(int(value_ * 100)) + "%", th().font_title, th().text, Align::Center);
    p.text({r.x, r.bottom() - 20, r.w, 20}, label_, th().font_small, th().text_dim, Align::Center);
  }

  bool on_event(pulseui::InputEvent& e) override {                                 // (7)
    if (e.type == pulseui::InputEvent::MouseDown) return true;                     // capture the press
    if (e.type == pulseui::InputEvent::MouseUp && bounds().contains(e.pos)) {
      reset();
      return true;
    }
    return false;
  }

private:
  double value_ = 0;
  std::string label_;
  pulseui::Color color_ = pulseui::Color::hex(0x4F7DF3);
};

// usage
Gauge(cpu_load).label("CPU").color(Color::hex(0x2EB67D)).flex()
```

**(1) Bindings.** `bind(Bind<T>, apply)` handles constants and streams: it runs `apply` now for a
constant, or subscribes when the view is mounted. Values are delivered on the UI thread, and the
subscription is cancelled on unmount.

**(2) Modifiers.** `PULSEUI_MOD(Self, name, (params), body)` declares a chainable modifier in both
forms (`&` and `&&`). The body runs, then the view is returned.

**(3) Events out.** Expose `pulse::signal`s for things that happen. Users connect to them, or use
`as_observable()`.

**(4) Invalidate.** Call `invalidate()` after changing something that affects painting, and
`invalidate_layout()` after changing something that affects size.

**(5) Measuring.** Return the preferred size of the *content* (without padding) for the available
space. Containers call this; `View::measure` adds padding and applies the size constraints.

**(6) Painting.** Override `paint_content(Painter&)`: background and border are drawn by the base
`paint()` before it. Override `paint(Painter&)` instead if you want to draw the background yourself,
and `paint_overlay(Painter&)` to draw above the children.

**(7) Input.** Override `on_event(InputEvent&)` and return `true` to consume an event. Consuming a
`MouseDown` captures the mouse until the button is released.

### Other virtual members

| Override | When |
|---|---|
| `layout()` | your view has children and positions them itself (call `set_frame` and `layout()` on each) |
| `hover_changed(bool)` | react to the pointer entering or leaving |
| `on_focus(bool)` | react to focus changes (set `focusable_ = true` in the constructor) |
| `on_mount()`, `on_unmount()` | the view entered or left a window |
| `hit_test(Point)` | non-rectangular hit areas |
| `hit_test_children(Point)` | return `false` to keep events for yourself (for example a scrollbar area) |

Protected fields you may set in the constructor: `focusable_`, `hover_repaint_` (repaint on hover and
press), `cursor_`.

### Making a custom view available in Pulse Layer

Register it with the reflection system; see
[Forms and reflection](14-forms-and-meta.md#custom-view-types).

Next: [CodeEditor](13-code-editor.md).
