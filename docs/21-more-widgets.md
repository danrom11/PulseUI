# More widgets

The views of this chapter live in `<pulseui/ui/inputs.hpp>` (included by `<pulseui/pulseui.hpp>`).
Like the basic widgets, each takes its value as a `Bind<T>` (a value or a stream) or as a `Model<T>&`
for two-way binding.

## `TextArea`: multi-line text

```cpp
pulse::property<std::string> notes;
TextArea(notes).placeholder("Notes").rows(6)
```

- Word wrap (`.wrap(false)` turns it off), a vertical scrollbar, `.read_only()`.
- Keys: arrows (`Ctrl`: by word), `Home`/`End` (`Ctrl`: the whole text), `PageUp`/`PageDown`, `Enter`
  (a new line), `Ctrl+A/C/X/V`, `Ctrl+Z` and `Ctrl+Y`/`Ctrl+Shift+Z` (typing is undone a word at a time).
  `Tab` moves the focus on.
- Double click selects a word.
- `.rows(n)` is the preferred height in lines; use `.flex()` to fill the space.
- `.on_change(fn)`, `text()`, `set_text()`, `select(anchor, caret)`, `selected_text()`.
- For code use [`CodeEditor`](13-code-editor.md).

## `NumberField`

```cpp
pulse::property<int> quantity{1};
pulse::property<double> width{120};
NumberField(quantity, 1, 99)                               // int model, range
NumberField(width, 0, 2000, 10).decimals(1).suffix(" px")  // step 10, "120.0 px"
```

- Up/Down arrows on the right (hold to repeat), the `↑`/`↓` keys, `PageUp`/`PageDown` (×10) and the mouse
  wheel over the field step the value; `Shift` multiplies the step by 10.
- Typing commits on `Enter` or when the field loses the focus; invalid text goes back to the current
  value. Values are clamped to the range and rounded to `decimals`. A decimal comma is accepted.
- Works with `Model<double>&` and `Model<int>&`; `.on_change(fn)` reports the value.

## `RadioGroup`

```cpp
pulse::property<int> size{1};
RadioGroup({"Small", "Medium", "Large"}, size)          // vertical
RadioGroup({"Day", "Week", "Month"}, period).horizontal()
```

Arrow keys move the choice; the options may be a stream (`bind_options`).

## Dates: `Calendar` and `DatePicker`

```cpp
pulse::property<Date> due{Date::today()};
DatePicker(due)                                   // a field; a click opens a calendar
Calendar(due).range(Date::today(), Date::today().add_months(3)).on_pick([&](Date d) { book(d); })
```

- `Date{year, month, day}` with `today()`, `valid()`, `weekday()` (0 = Monday), `add_days()`,
  `add_months()` (Jan 31 + 1 month = the last day of February), `iso()` ("2026-11-21"), `parse(iso)`,
  comparison operators.
- `Calendar` keys: arrows move by a day / a week, `PageUp`/`PageDown` by a month (`Shift`: a year), `Home`
  today, `Enter` picks. The wheel changes the month. `changed` reports every move, `picked` a chosen day.
- Names are localizable: `.labels(CalendarLabels{months, weekdays, week_starts_sunday})`.
- `DatePicker(...).format([](const Date& d) { return my_format(d); })` changes how the field shows the date.

## Colors: `ColorPanel` and `ColorPicker`

```cpp
pulse::property<Color> accent{Color::hex(0x3B82F6)};
ColorPicker(accent)            // a swatch with the hex code; a click opens a ColorPanel
ColorPanel(accent).alpha()     // inline: saturation/value square, hue bar, alpha bar, hex field, swatches
```

- The hex field accepts `#RGB`, `#RRGGBB` and `#RRGGBBAA`.
- `.swatches({...})` sets the preset colors.
- `Color::hsv(h, s, v, a)` and `color.to_hsv(h, s, v)` convert between RGB and HSV.

## `Icon` and SVG

```cpp
Icon("icons/folder.svg").size(20)                         // monochrome, in the text color
Icon(R"(<svg viewBox="0 0 24 24"><path d="M12 2 2 22h20z"/></svg>)").color(th().accent)
Icon("logo.svg").original_colors().size(64)                // keep the SVG's own colors
Button("Save").icon("icons/save.svg")                      // icon strings may be SVG
Action("Open").icon("icons/open.svg")                      // also in menus
```

PulseUI renders the subset of SVG icons use, without dependencies: `<path>` (all commands, arcs
included), `<rect>` (rounded), `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<g>`,
transforms, `fill`, `stroke`, `stroke-width`, `fill-rule` (`evenodd` holes), `opacity`, `style=""`,
`viewBox` and `currentColor`. Gradients, text, masks and filters are ignored.

- An icon string is SVG when it starts with `<svg` or ends with `.svg`; otherwise it is a text glyph
  (an emoji or symbol).
- `Svg::parse(markup)` / `Svg::load(path)` and `svg.draw(painter, rect, color, tint)` draw SVG in a
  `Canvas`.
- `Painter::fill_path(contours, color, even_odd)` - filling several contours as one shape - is available
  to custom drawing too.

## Suggestions in a `TextField`

```cpp
TextField(city).suggestions([&](const std::string& typed) {
  std::vector<Suggestion> out;
  for (auto& c : cities)
    if (starts_with(c.name, typed)) out.push_back({c.name, c.country, ""});
  return out;
})
```

While the field has the focus, a list under it shows what the function returns for the current text
(rebuilt after every edit). `↑`/`↓` choose, `Enter` or `Tab` take an entry, `Esc` closes the list, a
click takes one. `Suggestion{label, detail, value}`: `detail` is shown dimmed on the right, `value` is
what goes into the field (the label when empty). Taking an entry commits the field (`on_commit`,
`on_submit`).

Next: [Animation](22-animation.md).
