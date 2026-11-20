# 5. Views, nodes and modifiers

## The view tree

Every visible element is a `pulseui::View`. Views form a tree: containers (stacks, `Layer`, `Scroll`...)
own their children. A `Window` owns one root view.

```cpp
VStack{                      // container
  Text("Title"),             // leaf
  HStack{                    // nested container
    Button("OK"),
    Button("Cancel"),
  },
}
```

## Views are values until mounted

A view object is a **description** until it is placed into a window. Descriptions are movable (not
copyable), so you can build them in helper functions and return them:

```cpp
Node labeled(std::string label, Node editor) {
  return HStack{Text(std::move(label)).width(90), std::move(editor)}.align_items(Alignment::Center);
}

VStack{
  labeled("Name", TextField(name)),
  labeled("Volume", Slider(volume)),
};
```

When the tree is attached with `window.content(...)`, every view is **mounted**:

1. bindings subscribe to their streams (deferred until now, so building a tree has no side effects);
2. `ref(...)` pointers are filled in;
3. `on_mount_fn` hooks run.

When a view is removed (`take`, `remove`, `clear_children`, `ForEach` rebuilding, a new
`window.content(...)`) or the window is destroyed, it is **unmounted**: all its subscriptions are
cancelled, and it stops reacting. This is why bindings never need manual cleanup.

## `Node`

`Node` is an owning, type-erased handle to a view (`std::unique_ptr<View>` inside). Any view value
converts to `Node`, which is how containers accept children of different types.

```cpp
Node n = Text("x");                  // from a view value
Node m = std::make_unique<Text>("y");
Node empty;                          // or nullptr; containers ignore empty nodes
View* v = n.get();                   // peek
std::unique_ptr<View> owned = n.release();
```

Use `Node` as the return type of functions that build UI, and as the parameter type of functions that
accept arbitrary content.

> A function returning `HStack{...}.padding(8)` should return `Node` or `HStack`, not `auto&&`. The
> modifiers return an rvalue reference to the temporary, which is moved into the return value.

## Modifiers

Modifiers configure a view and return it, so they chain:

```cpp
Text("Hello").font_size(18).padding(8).background(th().surface).radius(6)
```

Every modifier exists in two forms: on a temporary it returns `View&&` (so the chain can be moved
into a container or returned); on a named view or pointer it returns `View&`:

```cpp
auto t = Text("x");
t.padding(4).flex();                       // modifies t in place

if (auto* b = window.find<Button>("ok"))
  b->primary().tooltip("Save and close");  // on a live view
```

### Modifiers on every view

| Modifier | Effect |
|---|---|
| `.id("name")` | Name for `find`; also used by forms and tests |
| `.padding(Insets)` | Space inside the view: `padding(8)`, `padding(Insets(h, v))`, `padding(Insets(l, t, r, b))` |
| `.width(w)`, `.height(h)`, `.size(w, h)` | Fixed size in logical pixels |
| `.min_width(w)`, `.min_height(h)`, `.max_width(w)`, `.max_height(h)` | Size constraints |
| `.flex(n = 1)` | Share of the free space in the parent stack (see [Layout](07-layout.md)) |
| `.align(Alignment)` | Cross-axis alignment in the parent stack, overriding the stack's `align_items` |
| `.position(x, y)` | Absolute position inside a `Layer` (or any container); the view keeps its own size |
| `.background(Bind<Color>)` | Fill color (value or stream) |
| `.border(Bind<Color>, width = 1)` | Border color and width |
| `.radius(r)` | Corner radius for background and border |
| `.clip(true)` | Clip the view and its children to its bounds |
| `.opacity(a)` | 0..1, multiplies with the parent's |
| `.visible(Bind<bool>)` | Hidden views take no space and receive no input |
| `.enabled(Bind<bool>)` | Disabled views are dimmed and ignore input, including their children |
| `.tooltip(Bind<std::string>)` | Shown after hovering for about half a second |
| `.cursor(Cursor)` | Mouse cursor over the view: `Arrow`, `IBeam`, `Hand`, `Crosshair`, `SizeWE`... |
| `.focusable(true)` | Can receive keyboard focus (Tab order) |
| `.on_click(fn)` | Makes any view clickable (also sets the hand cursor) |
| `.on_hover(fn(bool))` | Called when the mouse enters (`true`) or leaves (`false`) |
| `.on_event_fn(fn(const InputEvent&))` | Every input event delivered to the view |
| `.ref(ptr)` | After mounting, `ptr` points to the live view |
| `.on_mount_fn(fn(V&))` | Runs when the view is mounted, with access to the live view |

Widget-specific modifiers are listed with each widget in [Widgets](08-widgets.md).

### Value or stream

Modifiers whose parameter is `Bind<T>` accept a plain value **or** a stream: a `pulse::property<T>`,
a `pulse::computed<T>`, any `pulse::observable<T>`, or a `property.map(...)`. See
[Reactive bindings](06-reactive-bindings.md).

```cpp
Button("Save").enabled(dirty)                                   // property<bool>
Text("Offline").visible(online.map([](bool o) { return !o; }))  // derived stream
Panel{...}.background(is_error.map([](bool e) { return e ? th().danger : th().surface; }))
```

## Ids and `find`

```cpp
View* View::find(std::string_view id);              // depth-first, including this view
template <class V> V* find(std::string_view id);    // nullptr if absent or of another type
template <class V = View> V& get(std::string_view id);   // throws std::runtime_error if absent or of another type
V* Window::find<V = View>(std::string_view id) const;
V& Window::get<V = View>(std::string_view id) const;
```

`get` is the strict variant: when the tree does not match the code it fails loudly with a message such
as `pulseui: view 'email' is a Select, not a TextField`, instead of returning `nullptr` that a silent
`if` would skip. For forms designed in Pulse Layer, prefer the generated typed pointers (`ui_.email`),
which the compiler checks; see the [forms tutorial](04-forms-tutorial.md#step-4-write-the-class).

`find` is the bridge between a tree built elsewhere (a designed form, a helper function) and the
code that configures it. It searches depth-first and returns the first match. Keep ids unique within
a window.

`find` works only after the tree exists. It searches the window's root, so call it after
`window.content(...)`.

## Getting a pointer to a view you built: `ref`

When you build a tree in code and need the live view later, use `ref` instead of an id:

```cpp
TextField* search = nullptr;
win.content(VStack{
  TextField(query).ref(search),
  Button("Focus", [&] { if (search) search->focus(); }),
});
```

`ref(ptr)` assigns `ptr` when the view is mounted. Do not use the pointer after the view is removed.

## Changing the tree at runtime

Most dynamic UI is better expressed with `Show`, `ForEach` and `visible(...)`, but you can also
edit the tree directly. Every container is a `View` with these members:

| Member | Effect |
|---|---|
| `add(Node)` | Append a child; mounts it if the parent is mounted; returns the child |
| `insert(index, Node)` | Insert at a position |
| `take(View*)` | Remove a child and return ownership (`std::unique_ptr<View>`); unmounts it |
| `remove(View*)` | Remove and destroy |
| `clear_children()` | Remove and destroy all children |
| `move_child(View*, index)` | Reorder |
| `index_of(View*)` | Position of a child, or -1 |
| `children()` | `const std::vector<std::unique_ptr<View>>&` |
| `parent()`, `window()`, `mounted()` | Navigation |

All of these must be called on the UI thread. Removing a view from inside its own event handler (for
example a "delete" button that removes its own row) is supported.

## Geometry and state accessors

| Member | Meaning |
|---|---|
| `frame()` | Rect relative to the parent, valid after layout |
| `bounds()` | `{0, 0, frame().w, frame().h}` |
| `content_rect()` | `bounds()` minus padding |
| `to_window(Point)`, `from_window(Point)` | Coordinate conversion |
| `style()` | The `Style` struct (all layout and appearance attributes) |
| `is_visible()`, `set_visible(bool)` | Visibility |
| `is_enabled()` | Enabled, taking ancestors into account; `set_enabled(bool)` |
| `hovered()`, `pressed()`, `focused()`, `focus()` | Interaction state |
| `invalidate()` | Request a repaint |
| `invalidate_layout()` | Request re-measuring and a repaint (after changing sizes or texts directly) |
| `type_name()` | `"Text"`, `"Button"`... (used by forms) |

The `set_*` methods of widgets (`Text::set_text`, `Button::set_label`...) call `invalidate` or
`invalidate_layout` for you.

## Subscriptions owned by a view

If you subscribe to something whose lifetime should match a view's, hand the subscription to the
view:

```cpp
if (auto* chart = win.find("chart"))
  chart->keep(prices.subscribe([chart](double) { chart->invalidate(); }));   // cancelled on unmount
```

`keep(subscription)` stores it until the view is unmounted. Inside `on_mount_fn` this is the natural
place to start such subscriptions.

Next: [Reactive bindings](06-reactive-bindings.md).
