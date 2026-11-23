# 7. Layout

PulseUI layout has one main primitive, the **stack**, plus a few special containers. The rules are
small enough to predict any layout in your head.

## The box model

Every view has:

```
┌──────────── frame (set by the parent) ────────────┐
│ background, border, radius                        │
│   ┌──────────── content_rect ───────────────┐     │
│   │  children / text / drawing              │     │
│   └─────────────────────────────────────────┘     │
│ ◄── padding ──►                                   │
└───────────────────────────────────────────────────┘
```

- **Preferred size**: what the view would like to be. It is the content's natural size plus padding:
  the text extent for `Text`, the label plus insets for `Button`, the children for containers.
- **Fixed size**: `width(w)` / `height(h)` replace the preferred size on that axis.
- **Constraints**: `min_width`, `max_width`, `min_height`, `max_height` clamp the result.
- **Padding** is inside the view (`content_rect = bounds - padding`). There is no margin; use the
  parent's `spacing`, a `Spacer`, or wrap the view in a stack with padding.

All sizes are in logical pixels. The backend scales them for HiDPI screens.

## Stacks: `VStack`, `HStack`, `Panel`

A stack arranges its children along one axis: vertical for `VStack` and `Panel`, horizontal for
`HStack`. The axis the children follow is the **main axis**; the other one is the **cross axis**.

```cpp
VStack{ a, b, c }.spacing(8).padding(12).align_items(Alignment::Stretch).justify(Justify::Start)
```

| Stack modifier | Default | Meaning |
|---|---|---|
| `spacing(px)` | 8 | gap between consecutive children |
| `align_items(Alignment)` | `Stretch` | cross-axis placement of children: `Start`, `Center`, `End`, `Stretch` |
| `justify(Justify)` | `Start` | main-axis placement when nothing flexes: `Start`, `Center`, `End`, `SpaceBetween` |

### The algorithm

On the main axis:

1. Every visible, non-positioned child is measured. **Fixed children** take their preferred size.
   **Flexible children** (`flex > 0`) start from their **minimum** size (`min_width` / `min_height`,
   usually 0), not their preferred size.
2. The free space is the stack's content size, minus the children's sizes, minus the gaps.
3. If there are flexible children and free space, it is divided between them in proportion to their
   `flex` values, each capped by its `max_width` / `max_height`. `justify` then has no effect.
4. If nothing flexes, `justify` positions the group: at the start, centred, at the end, or with the
   extra space distributed between the children (`SpaceBetween`).

On the cross axis, each child is placed according to its own `align(...)` if set, otherwise the
stack's `align_items`:

- `Stretch`: the child takes the full cross size, unless it has a fixed size on that axis, in which
  case it behaves like `Start`;
- `Start` / `Center` / `End`: the child keeps its preferred cross size (capped by the stack) and is
  placed accordingly.

```
HStack{ Text("Name"), TextField().flex(), Button("Go") }      in 400 px:

| Name |[ TextField ............................. ]| Go |
  pref      flex: everything that is left             pref
```

Consequences worth remembering:

- **`flex()` is how a view grows.** Without it, views keep their preferred size.
- **A flexible view's preferred size is ignored on the main axis.** Give it a `min_width` /
  `min_height` if it must not shrink below something.
- **Stacks stretch children across by default.** A `Button` in a `VStack` is as wide as the stack. Use
  `.align(Alignment::Start)` on the button, or `align_items(...)` on the stack, to prevent that.
- **When there is not enough room, nothing wraps.** Fixed children keep their size and the content
  is clipped or overflows. Use `Scroll`, or `flex` with `min_*` sizes, for content that may not fit.

### Common recipes

```cpp
// push items to the right
HStack{ Text("Title"), Spacer(), Button("Close") }

// centre a group
HStack{ Button("Yes"), Button("No") }.justify(Justify::Center)

// a form row: label column of fixed width, editor takes the rest
HStack{ Text("Email").width(90), TextField(email).flex() }.align_items(Alignment::Center)

// header / content / footer
VStack{ header, Scroll(content).flex(), footer }

// two columns with a 1:2 ratio
HStack{ left.flex(1), right.flex(2) }

// a fixed-width sidebar and a flexible main area
HStack{ sidebar.width(240), Divider(Orientation::Vertical), main.flex() }.spacing(0)
```

### `Panel`

A `VStack` with card styling: surface background, border, the theme's corner radius, `padding(12)`
and `spacing(8)`. Its background and border can be overridden with `.background(...)` and
`.border(...)`.

```cpp
Panel{ Text("Storage").bold(), ProgressBar(used_fraction), Text(used_label).dim() }
```

## `Spacer`

```cpp
Spacer()        // flex(1): takes the free space
Spacer(16)      // a fixed 16x16 gap
```

`Spacer()` is just a view with `flex = 1`. Several spacers share the free space equally, which is
useful for centring: `HStack{ Spacer(), content, Spacer() }`.

## `Divider`

A thin line: `Divider()` is horizontal (for `VStack`s), `Divider(Orientation::Vertical)` is vertical
(for `HStack`s).

## `Layer`: overlap and absolute positions

`Layer` places its children **on top of each other**, in order (the last one is drawn on top).

- A child **without** a position fills the whole layer (minus padding).
- A child **with** `.position(x, y)` keeps its preferred (or fixed) size and is placed at that offset.

The layer's preferred size is the union of its children.

```cpp
// a badge over an icon
Layer{
  ImageView(std::string("inbox.png")),
  Text("3").bold().padding(Insets(6, 2)).background(th().danger).radius(8).position(22, -4),
}

// free-form design surface (what the designer uses for Layer forms)
Layer{
  Button("A").position(20, 20),
  Button("B").position(140, 60),
}.size(400, 300)
```

`position(x, y)` also works inside stacks: a positioned child is taken out of the flow and placed at
that offset.

## `Scroll`

```cpp
Scroll(content)                  // vertical scrolling
Scroll(content).horizontal()     // both directions
```

- The content is measured with unlimited height (and, with `horizontal()`, unlimited width).
  Without `horizontal()` the content is always exactly as wide as the viewport, so text wraps and
  stacks stretch as you would expect.
- Mouse wheel and trackpad scroll; Shift+wheel scrolls horizontally; scrollbars can be dragged.
- Children outside the viewport are not painted, so long lists stay cheap.
- `scroll_to(y)`, `scroll_into_view(rect)` and `offset_y()` are available on a live `Scroll`.

A `Scroll` usually needs `.flex()` (or a fixed height) in its parent stack, otherwise it takes its
content's full height and has nothing to scroll.

## `Split`: resizable panes

```cpp
Split(first, second, Orientation::Horizontal, 260)   // first pane 260 px wide, draggable divider
    .min_pane(120)                                    // neither pane smaller than 120 px
    .keep_second()                                    // on window resize, the second pane keeps its size
```

`Orientation::Horizontal` puts the panes side by side; `Vertical` stacks them.

## `Tabs`

```cpp
pulse::property<int> page{0};
Tabs{
  Tab("General", general_page()),
  Tab(advanced_title, advanced_page()),          // titles may be streams (e.g. translations)
}.selected(page)                                 // optional two-way binding of the current tab
```

Only the current page is visible; all pages stay mounted, so their state survives switching. The
`changed` signal reports the new index. `add_tab(Tab)` adds pages at runtime.

## `Show`: conditional content

```cpp
Show(logged_in,
     VStack{ Text("Welcome back"), Button("Log out", logout) },
     Button("Log in", login))                    // optional "else" branch
```

`Show` displays the first child while the condition is true and the second otherwise. Both branches
**stay mounted**, so text typed into a hidden form is still there when it reappears. For a single view,
`.visible(cond)` is simpler.

`.transition()` makes the switch smooth: the branches cross-fade and the space they take grows or shrinks
over the transition, so the views below slide instead of jumping. A `Show` without an `else` branch
then expands and collapses:

```cpp
Show(details_open, details()).transition()            // 200 ms, ease-out
Show(big, chart(), sparkline()).transition(300ms, Easing::EaseInOut)
```

See [Animation](22-animation.md).

## `ForEach`: lists

```cpp
ForEach(items, [](const Item& item) { return Text(item.name); })
ForEach(items, [](const Item& item, std::size_t index) { return Text(std::to_string(index + 1) + ". " + item.name); })
ForEach(tags, ...).horizontal().spacing(6)
```

- The first argument is a `Bind<std::vector<T>>`: a vector, a property, a computed or an observable of
  vectors.
- The function builds one view per item. It may take `(item)` or `(item, index)`.
- `ForEach` is a stack: vertical by default with `spacing(4)`; `.horizontal()` lays items out in a row.
  All stack modifiers apply.
- Without a key, all rows are rebuilt when the list changes. Capture **ids**, not references to items,
  in row callbacks (see [Tutorial, part 3](03-tutorial.md#part-3-todo-list)).

### Keyed `ForEach`: rows that keep their state

Pass a key function before the row function:

```cpp
ForEach(todos, [](const Todo& t) { return t.id; }, [&](const Todo& t) {
  return HStack{ Checkbox("", t.done), TextField(t.title).flex() };
})
```

Rows are matched by key when the list changes:

- a row whose item is unchanged is **kept** - with its state: keyboard focus, selection, scroll
  position, text being typed;
- a moved item moves its row (no rebuild);
- only new items and items that changed build a new row; rows of removed items go away;
- while a row has the keyboard focus it is kept even if its item changed, so typing in a row is never
  interrupted by an update of the list.

Keys may be numbers or strings. Items are compared with `==` when the type has it (otherwise a row
with the same key is always reused).

### Wrapping rows

`.wrap()` lays the rows side by side and wraps them onto the next line, like [`Wrap`](#wrap-rows-that-wrap) -
for cards, tags or thumbnails:

```cpp
Scroll(ForEach(photos, photo_card).wrap().spacing(12)).flex()
```

### Thousands of rows

`ForEach` builds a view for every item. For long lists (thousands of rows) use the virtualized
[`List`, `Table` and `TreeView`](20-collections.md), which only create or paint what is on screen.

Combine with `Scroll` for medium lists: `Scroll(ForEach(...)).flex()`.

## `Grid`: rows and columns

```cpp
Grid{
  Text("Name"),     TextField(name),
  Text("Email"),    TextField(email),
  Text("Notes"),    TextArea(notes).cell(2, 1, 2, 1),   // row 2, column 1, spanning 2 rows
  Button("Save", save).span(2),                         // the next free row, both columns
}.columns({Track::automatic(), Track::fr(1)}).spacing(8)
```

- Children fill the cells row by row. `.cell(row, col, row_span, col_span)` places a child explicitly,
  `.span(n)` makes it span `n` columns in the automatic flow.
- Columns are tracks: `Track::px(120)` (fixed), `Track::automatic()` (as wide as the widest child),
  `Track::fr(n)` (a share of the remaining width). `.columns(3)` makes three equal columns.
- Rows are as tall as their tallest child. `.spacing(s)` sets both gaps, `.gaps(rows, cols)` each one.
- `.align_items(...)` aligns children inside their cells (stretch by default).

## `Wrap`: rows that wrap

```cpp
Wrap{ chip("c++"), chip("reactive"), chip("desktop"), chip("ui") }.spacing(6)
```

Children are laid out in a row and continue on the next line when the width runs out (tags, chips,
toolbars that wrap). `.line_spacing(s)` sets the gap between lines, `.justify(...)` the alignment of
each line.

## Layout and performance

- Layout runs only when something requested it (a text changed, a view was added, the window was
  resized), and it is batched until the next frame.
- Text measurement is cached.
- Changing only colors or other paint-only state (`invalidate()`) skips layout.

If you change a view's size-related state directly, for example through `style()`, call
`invalidate_layout()`.

Next: [Widgets](08-widgets.md).
