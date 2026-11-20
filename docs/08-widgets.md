# 8. Widgets

Every widget accepts all the [common modifiers](05-views-and-modifiers.md#modifiers-on-every-view)
(`padding`, `flex`, `width`, `enabled`, `visible`, `tooltip`, `id`...). This chapter lists what each
widget adds. `Bind<T>` means "a value or a stream" and `Model<T>` means "a `pulse::property<T>` for
two-way binding" (see [Reactive bindings](06-reactive-bindings.md)).

Quick index: [Text](#text) · [Button](#button) · [TextField](#textfield) ·
[Checkbox and Toggle](#checkbox-and-toggle) · [Slider](#slider) · [ProgressBar](#progressbar) ·
[Select](#select) · [Segmented](#segmented) · [ImageView](#imageview) · [Divider](#divider) ·
[Spacer](#spacer) · [Panel](#panel) · [Canvas](12-custom-drawing.md) · [CodeEditor](13-code-editor.md) ·
containers in [Layout](07-layout.md)

---

## Text

Displays a string: one line, elided with "…" when too narrow, or wrapped.

```cpp
Text("Hello")
Text(name)                                        // property<std::string>
Text(count.map([](int c) { return std::to_string(c) + " items"; }))
Text("Settings").title()
Text(long_description).wrap().dim()
```

| Constructor | |
|---|---|
| `Text()` | empty |
| `Text(Bind<std::string>)` | value or stream |

| Modifier | Effect |
|---|---|
| `font_size(px)` | font size; 0 = theme default (13) |
| `weight(FontWeight)` | `Light`, `Normal`, `Medium`, `SemiBold`, `Bold` |
| `bold()` | `weight(FontWeight::Bold)` |
| `title()` | size 20, SemiBold |
| `dim()` | secondary text color (`th().text_dim`) |
| `mono()` | monospace font |
| `wrap(bool = true)` | wrap to the available width; the height grows with the text |
| `text_align(int)` | `Align::LeftCenter` (default), `Align::Center`, `Align::RightCenter`, or any `Align` flags |
| `color(Bind<Color>)` | text color (overrides `dim`) |
| `bind_text(Bind<std::string>)` | follow a stream after construction |

| Member | |
|---|---|
| `text()`, `set_text(s)` | read or replace the text |
| `font()` | the effective `Font` |

**Wrapping** needs a width: in a `VStack` with the default `Stretch` alignment the text gets the
stack's width. In an `HStack`, give it `flex()` or a `width`.

---

## Button

```cpp
Button("Save", [&] { save(); })
Button("Delete").danger().on_click(remove)
Button(label_stream).primary()
Button("").icon("⚙").flat().tooltip("Settings")
```

| Constructor | |
|---|---|
| `Button()` | empty label |
| `Button(Bind<std::string> label, std::function<void()> on_click = {})` | label (value or stream) and an optional handler |

| Modifier | Effect |
|---|---|
| `primary()` | accent-colored: the main action of a window or dialog |
| `flat()` | no background until hovered: toolbars, secondary actions |
| `danger()` | red: destructive actions |
| `kind(ButtonKind)` | `Normal`, `Primary`, `Flat`, `Danger` |
| `icon(glyph)` | a short text glyph (emoji or symbol) shown before the label |
| `bind_label(Bind<std::string>)` | follow a stream after construction |
| `on_click(fn)` | add a click handler (any view has it) |

| Member | |
|---|---|
| `pulse::signal<> clicked` | emitted on click, Enter or Space |
| `clicks()` | `clicked` as `pulse::observable<pulse::unit>`, for operators |
| `label()`, `set_label(s)`, `kind_value()`, `set_kind(k)` | |

```cpp
// rate-limit a button with Pulse
auto s = (save_button->clicks() | pulse::throttle(1s)).subscribe([&](pulse::unit) { save(); });
```

A click fires when the mouse is pressed **and released** over the button. Buttons are focusable; Enter
or Space clicks the focused button. The height is the theme's `control_height` (30 px).

---

## TextField

A single-line text input.

```cpp
TextField(name)                                    // two-way bound to property<std::string>
TextField().placeholder("Search").on_change([](const std::string& s) { ... })
TextField(password).password()
TextField(query).on_submit([&](auto&) { run(); })
```

| Constructor | |
|---|---|
| `TextField()` | empty, unbound |
| `explicit TextField(Model<std::string>&)` | two-way |
| `explicit TextField(Bind<std::string>)` | initial or followed text; edits are not written anywhere |

| Modifier | Effect |
|---|---|
| `placeholder(Bind<std::string>)` | dim hint while empty |
| `password(bool = true)` | shows `•` instead of characters |
| `model(Model<std::string>&)` | two-way bind after construction |
| `on_change(fn(const std::string&))` | every edit |
| `on_submit(fn(const std::string&))` | Enter |
| `on_commit(fn(const std::string&))` | Enter **or** losing focus: "the user finished editing" |

| Member | |
|---|---|
| `pulse::signal<std::string> changed, submitted, committed` | the events above |
| `text_changes()` | `changed` as an observable, for `debounce` and friends |
| `text()`, `set_text(s)`, `placeholder_value()` | |

Editing: click to place the caret, drag or Shift+arrows to select, double-click selects all, Home/End,
Backspace/Delete, Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V (Cmd on macOS). Text is UTF-8; the caret moves by
code points. The view scrolls horizontally to keep the caret visible.

**`on_change` or `on_commit`?** Use a model or `on_change` for live behaviour (filtering a list as the
user types). Use `on_commit` for values that should apply once the user is done (renaming a file,
parsing a number), so half-typed input is never applied.

---

## Checkbox and Toggle

Boolean inputs. They share one API; `Toggle` looks like a switch.

```cpp
Checkbox("Remember me", remember)                  // Model<bool>&
Toggle("Dark theme", dark)
Checkbox("Read only", true)                        // initial value, not bound
Checkbox(item.title, item.done).on_change([&, id = item.id](bool) { toggle(id); })
```

| Constructor | |
|---|---|
| `Checkbox()` | empty label |
| `Checkbox(Bind<std::string> label, Model<bool>& model)` | two-way |
| `Checkbox(Bind<std::string> label, Bind<bool> value = false)` | initial or followed state |

| Modifier / member | |
|---|---|
| `model(Model<bool>&)` | two-way bind after construction |
| `on_change(fn(bool))` | user toggled it |
| `pulse::signal<bool> changed` | the same as a signal |
| `checked()`, `set_checked(bool, bool notify = true)` | `notify = false` changes the state silently |
| `label()`, `set_label(s)` | |

Clicking anywhere on the label toggles it; Space or Enter toggles the focused one.

---

## Slider

```cpp
Slider(volume)                       // Model<double>&, range 0..1
Slider(temperature, -40, 100).step(0.5)
Slider(0.25)                         // initial value, not bound
```

| Constructor | |
|---|---|
| `Slider()` | 0, range 0..1 |
| `Slider(Model<double>& model, double min = 0, double max = 1)` | two-way |
| `explicit Slider(Bind<double> value, double min = 0, double max = 1)` | initial or followed value |

| Modifier / member | |
|---|---|
| `step(s)` | snap to multiples of `s`; also the keyboard step (default: 1/20 of the range) |
| `model(Model<double>&)` | two-way bind after construction |
| `on_change(fn(double))`, `pulse::signal<double> changed` | user moved it |
| `value()`, `set_value(v, notify = true)`, `min()`, `max()`, `set_range(lo, hi)` | |

Click or drag anywhere on the track. Arrow keys move the focused slider.

---

## ProgressBar

```cpp
ProgressBar(download_progress)       // Bind<double>, 0..1
ProgressBar(0.4)
```

| | |
|---|---|
| `ProgressBar()`, `explicit ProgressBar(Bind<double> fraction)` | constructors |
| `bind_value(Bind<double>)` | follow a stream after construction |
| `value()`, `set_value(v)` | values are clamped to 0..1 |

---

## Select

A drop-down list.

```cpp
pulse::property<int> quality{1};
Select({"Low", "Medium", "High"}, quality)                   // Model<int>& = selected index
Select(available_devices, device_index)                      // options can be a stream too
Select({"A", "B"}).on_change([](int i) { ... })
```

| Constructor | |
|---|---|
| `Select()` | no options |
| `Select(Bind<std::vector<std::string>> options, Model<int>& index)` | two-way index |
| `explicit Select(Bind<std::vector<std::string>> options, Bind<int> index = 0)` | initial or followed index |

| Modifier / member | |
|---|---|
| `model(Model<int>&)` | two-way bind after construction |
| `bind_options(Bind<std::vector<std::string>>)` | follow a list of options |
| `on_change(fn(int))`, `pulse::signal<int> changed` | user picked an option |
| `index()`, `set_index(i, notify = true)`, `options()`, `set_options(v)` | |

Click to open the list (a popup above the window content); click an option or press Escape to close.
When focused: Space or Enter opens it, Up and Down change the selection directly.

---

## Segmented

A row of mutually exclusive options, for modes and filters.

```cpp
pulse::property<int> view_mode{0};
Segmented({"List", "Grid", "Columns"}, view_mode)
```

| Constructor | |
|---|---|
| `Segmented()` | no options |
| `Segmented(Bind<std::vector<std::string>> options, Model<int>& index)` | two-way |
| `explicit Segmented(Bind<std::vector<std::string>> options, Bind<int> index = 0)` | initial or followed |

| Modifier / member | |
|---|---|
| `on_change(fn(int))`, `pulse::signal<int> changed` | |
| `index()`, `set_index(i, notify = true)`, `set_options(v)` | |

`Select` or `Segmented`? Segmented shows every option at once and needs one click. It is best for
two to five short options that users switch often. Select saves space for longer lists.

---

## ImageView

```cpp
ImageView(std::string("logo.png"))     // load from a file (PNG, JPEG, BMP, GIF... via the OS codecs)
ImageView(avatar)                      // Bind<Image>: a value or a stream of images
```

| | |
|---|---|
| `ImageView()`, `explicit ImageView(Bind<Image>)`, `explicit ImageView(std::string path)` | constructors |
| `set_source(path)`, `source()` | load another file |
| `set_image(Image)` | show an image you created (for example with `Image::from_rgba`) |

The preferred size is the image's size in pixels. With a fixed `width`/`height` or `flex`, the image is
drawn into the view's content rect. See [Images](12-custom-drawing.md#images).

---

## Divider

```cpp
Divider()                              // horizontal line, for VStack
Divider(Orientation::Vertical)         // vertical line, for HStack
```

---

## Spacer

```cpp
Spacer()           // takes the free space (flex = 1)
Spacer(12)         // fixed 12 x 12 gap
Spacer().flex(0).size(4, 22).background(color).radius(2)   // a small colored bar
```

---

## Panel

A card: a vertical stack with the surface background, a border, rounded corners and `padding(12)`.
See [Layout](07-layout.md#panel).

```cpp
Panel{ Text("Account").bold(), TextField(email), Button("Save").primary().align(Alignment::End) }
```

---

## Containers and other views

| View | Chapter |
|---|---|
| `VStack`, `HStack`, `Layer`, `Scroll`, `Split`, `Tabs`, `Show`, `ForEach` | [Layout](07-layout.md) |
| `Canvas` | [Custom drawing](12-custom-drawing.md) |
| `CodeEditor` | [CodeEditor](13-code-editor.md) |

Next: [Styling and themes](09-styling-and-themes.md).
