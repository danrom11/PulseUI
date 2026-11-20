# 9. Styling and themes

PulseUI deliberately has no style sheets. Appearance comes from two places:

1. **The theme**: a struct of colors, fonts and metrics that every widget reads when it paints. Change
   the theme, and the whole application follows.
2. **Per-view style**: a few modifiers (`background`, `border`, `radius`, `padding`, text `color`...)
   for the cases where one view should look different.

This keeps applications consistent by default and makes light/dark switching automatic.

## Colors

```cpp
struct Color { float r, g, b, a; };      // 0..1, straight (not premultiplied) alpha
```

| Create | |
|---|---|
| `Color::hex(0x3D7EFF)` | from `0xRRGGBB` |
| `Color::hex(0x000000, 0.4f)` | with alpha |
| `Color::rgb8(61, 126, 255)`, `Color::rgb8(r, g, b, a)` | 0..255 channels |
| `Color(0.2f, 0.5f, 1.0f, 1.0f)` | 0..1 floats |
| `Color::parse("#3d7eff")` | `std::optional<Color>`; accepts `#rgb`, `#rrggbb`, `#rrggbbaa` |
| `Color::transparent()`, `Color::white()`, `Color::black()` | |

| Use | |
|---|---|
| `c.with_alpha(0.5f)` | same color, other alpha |
| `Color::mix(a, b, t)` | linear blend, `t` from 0 (a) to 1 (b) |
| `c.to_hex()` | `"#3D7EFF"`, or `"#3D7EFF80"` when not opaque |
| `c.lighter(k = 0.1f)`, `c.darker(k = 0.1f)` | move towards white / black by `k` |
| `c.luminance()` | perceived brightness 0..1 (for choosing readable text colors) |
| `a == b` | exact comparison |

## The theme

```cpp
pulse::property<Theme>& theme();    // the application theme: a property, so it can be watched and bound
const Theme& th();                  // the current theme, for reading in paint code and when building views
```

`Theme` fields:

| Field | Used for |
|---|---|
| `window` | window background |
| `surface`, `surface_alt` | panels and cards; alternate rows and headers |
| `control`, `control_hover`, `control_pressed` | buttons and other controls |
| `input` | text field and editor background |
| `border`, `border_focus` | borders; the focus ring |
| `text`, `text_dim`, `text_disabled` | primary text, secondary text (`Text::dim()`), disabled text |
| `accent`, `accent_hover`, `accent_pressed`, `on_accent` | primary buttons, selection, sliders; text on accent |
| `selection` | text selection, selected rows |
| `danger`, `success`, `warning` | status colors (`Button::danger()` uses `danger`) |
| `shadow`, `tooltip`, `scrollbar` | popups, tooltips, scrollbars |
| `radius`, `radius_small` | corner radii (6 and 4) |
| `padding`, `spacing` | default metrics (12 and 8) |
| `control_height` | height of buttons, fields, selects (30) |
| `font`, `font_small`, `font_title`, `font_mono` | fonts (13, 11.5, 20 SemiBold, monospace) |
| `name` | `"dark"` or `"light"` |

Two built-in themes: `Theme::dark()` (the default) and `Theme::light()`. Start with a specific one with
the environment variable `PULSEUI_THEME=light`.

### Switching themes

```cpp
theme() = Theme::light();
```

Every window repaints. Widgets read `th()` while painting, so they all pick up the new colors.

A typical setting:

```cpp
pulse::property<bool> dark{true};
auto apply = dark.as_observable().subscribe([](bool d) { theme() = d ? Theme::dark() : Theme::light(); });

Toggle("Dark theme", dark);
```

### A custom theme

Start from a built-in theme and change what you need:

```cpp
Theme brand() {
  Theme t = Theme::dark();
  t.name = "brand";
  t.accent = Color::hex(0x8B5CF6);
  t.accent_hover = Color::hex(0x9F7AEA);
  t.accent_pressed = Color::hex(0x7C3AED);
  t.selection = t.accent.with_alpha(0.35f);
  t.radius = 10;
  t.font = Font(14);
  return t;
}

theme() = brand();
```

`Font` has `family` (empty = system UI font), `size`, `weight` and `italic`, plus `with_size(s)` and
`with_weight(w)` helpers.

## Static and dynamic colors

There is one subtlety. **`th()` returns the theme at the moment you call it.** When you write

```cpp
Text("Warning").color(th().danger)
```

the color is captured when the view is built. If the theme changes later, this text keeps the old
red. Built-in widgets do not have this problem because they read the theme while painting.

For custom colors that must follow the theme, bind to the theme property instead:

```cpp
Text("Warning").color(theme().map([](const Theme& t) { return t.danger; }))
Panel{...}.background(theme().map([](const Theme& t) { return t.surface_alt; }))
```

If your app never switches themes at runtime, `th().xxx` is fine.

## Per-view style

Modifiers available on every view (see [Views and modifiers](05-views-and-modifiers.md)):

```cpp
VStack{ ... }
    .padding(16)
    .background(th().surface)
    .border(th().border, 1)
    .radius(12)
    .opacity(0.9f)
    .clip()
```

Text-specific: `font_size`, `weight`, `bold`, `title`, `dim`, `mono`, `color`, `text_align`, `wrap`.
Button-specific: `primary`, `flat`, `danger`, `icon`.

### The `Style` struct

All of these modifiers write into `view.style()`, which you can also access directly on a live view:

```cpp
struct Style {
  Insets padding;
  float width, height;                      // kAuto (NaN) = preferred size
  float min_width, min_height, max_width, max_height;
  float flex;
  Alignment align;
  bool positioned; Point position;          // absolute position
  Color background, border; float border_width, radius;
  bool clip;
  float opacity;
};
```

After changing `style()` directly, call `invalidate_layout()` (sizes) or `invalidate()` (colors).

## Reusable styles are just functions

Instead of style classes, write functions that return configured views:

```cpp
Node card(Node content) {
  return VStack{std::move(content)}.padding(16).background(th().surface).radius(12).border(th().border);
}

Text heading(std::string s) { return Text(std::move(s)).font_size(16).weight(FontWeight::SemiBold); }

Node badge(Bind<std::string> text, Color c) {
  return Text(std::move(text)).font_size(11).bold().color(Color::white())
      .padding(Insets(6, 2)).background(c).radius(8).align(Alignment::Start);
}
```

They compose, take parameters, and can be unit-tested like any other code.

Next: [App and windows](10-app-and-windows.md).
