# 6. Reactive bindings

Bindings connect your data to views. This chapter explains exactly what you can pass, how values
travel, and what happens with threads and lifetimes.

## `Bind<T>`: a value or a stream

Most view parameters are declared as `Bind<T>`: `Text(Bind<std::string>)`,
`.enabled(Bind<bool>)`, `.background(Bind<Color>)`, `ProgressBar(Bind<double>)` and so on.
A `Bind<T>` holds **either** a constant value **or** a stream. It can be constructed from:

| Argument | Example | Kind |
|---|---|---|
| a `T` | `Text(std::string("Hi"))` | constant |
| anything convertible to `T` | `Text("Hi")` (const char*), `ProgressBar(1)` (int → double) | constant |
| a brace list, for container types | `Select({"Low", "High"})` | constant |
| `pulse::observable<T>` | `Text(stream)` | stream |
| `pulse::observable<U>` with `U` convertible to `T` | `Text(obs_of_const_char_ptr)` | stream |
| anything with `as_observable()`: `pulse::property<T>`, `pulse::computed<T>`, `pulse::behavior_subject<T>` | `Text(name)`, `.enabled(can_save)` | stream |

Because `property::as_observable()` and `computed::as_observable()` emit the current value first,
a view bound to them shows the right value immediately, then follows every change.

### Deriving with `map`

`property::map(f)` and `computed::map(f)` produce a stream of `f(value)`: the most common way to adapt
data for display.

```cpp
Text(price.map([](double p) { return "$" + std::to_string(p); }))
Button("Save").enabled(status.map([](Status s) { return s == Status::Dirty; }))
```

For anything more complex, use Pulse operators directly; the result is an observable, which binds
like anything else:

```cpp
Text(pulse::combine_latest(first.as_observable(), last.as_observable(),
                           [](const std::string& f, const std::string& l) { return f + " " + l; }))
```

## `Model<T>`: two-way bindings

Editors take a **model**: a `pulse::property<T>` passed by reference. `Model<T>` is simply an alias
of `pulse::property<T>`.

| Widget | Model type |
|---|---|
| `TextField(model)` | `Model<std::string>` |
| `Checkbox(label, model)`, `Toggle(label, model)` | `Model<bool>` |
| `Slider(model, min, max)` | `Model<double>` |
| `Select(options, model)`, `Segmented(options, model)` | `Model<int>` (the index) |
| `Tabs{...}.selected(model)` | `Model<int>` |
| `CodeEditor(model)` | `Model<std::string>` |

With a model, the editor shows the property's value, **and** writes the user's edits back into it.
Changing the property in code updates the editor. The equality check in `property::set` prevents
loops.

On a view that already exists (found by id, loaded from a form), use the `.model(prop)` modifier,
which does the same thing.

**One-way versus two-way.** Passing a *stream* to an editor makes it display-only for that value:
`TextField(name.map(...))` shows the text but cannot write it back (there is nowhere to write).
Passing a *model* makes it two-way.

### The model must outlive the view

The editor keeps a pointer to the model. Declare models before the window (or in an object that
outlives it). This is the natural order anyway: state first, then the UI that shows it.

## Following a stream after construction

Views created without data (from a form, or with a default constructor) can bind later:

| Modifier | On |
|---|---|
| `bind_text(Bind<std::string>)` | `Text` |
| `bind_label(Bind<std::string>)` | `Button` |
| `bind_value(Bind<double>)` | `ProgressBar` |
| `bind_options(Bind<std::vector<std::string>>)` | `Select` |
| `model(Model<T>&)` | all editors |
| `placeholder(Bind<std::string>)`, `color(Bind<Color>)`, `visible`, `enabled`, `background`, `border`, `tooltip` | as listed in [Widgets](08-widgets.md) |

## How a binding works

```cpp
template <class T, class F>
void View::bind(const Bind<T>& b, F apply);
```

- For a **constant**, `apply(view, value)` runs immediately.
- For a **stream**, the subscription is **deferred until the view is mounted**. The subscription is
  owned by the view and cancelled when it unmounts. If the view is mounted again, it subscribes again.
- Each emitted value is delivered **on the UI thread**. If it was emitted on another thread, it is
  posted to the UI thread first. If the view is destroyed in the meantime, the value is dropped.

You can use `bind` yourself in custom views (see [Custom drawing](12-custom-drawing.md)).

## Threads

The rules are short:

1. **Views may only be touched on the UI thread** (creating, modifying, adding, removing).
2. **Data may change on any thread.** A `pulse::property` can be set from a worker. Bindings deliver to
   the UI thread.
3. **Plain signal slots and `property::changed` slots run on the thread that emits.** If such a slot
   touches views, connect it with the UI executor, `connect(App::ui(), slot)`, or post with
   `App::post`.

```cpp
// worker thread
progress = 0.5;                                                // fine: a ProgressBar bound to it updates on the UI thread
results_ready("done");                                         // its slots run on the worker...
results_ready.connect(App::ui(), [&](auto& s) { label->set_text(s); });   // ...unless queued like this
App::post([&] { status->set_text("done"); });                  // or post explicitly
```

`App::ui()` is a `pulse::executor` bound to the UI thread. Use it with every Pulse feature that
takes an executor (`observe_on`, `debounce`, `timer`, `interval`, `delay`, `post_delayed`).

## Lifetimes, summarized

| Thing | Lives until | Notes |
|---|---|---|
| A binding (`Text(stream)`, `.enabled(stream)`) | its view unmounts | automatic |
| A model pointer (`TextField(model)`) | its view is destroyed | the model must outlive the view |
| A `connect` on a widget's signal (`button->clicked.connect`) | the widget is destroyed | the signal dies with the widget |
| A `connect` on *your* signal or property from a view callback | until disconnected | use `scoped()` or keep it in the view with `keep()` |
| `view->keep(subscription)` | the view unmounts | for your own subscriptions tied to a view |

Capturing `this` in widget callbacks is safe when the widget lives in a window owned by `this`, which is
the standard structure (see [Tutorial, part 4](03-tutorial.md#part-4-the-todo-list-as-a-class)).

## Patterns

### Derived state as `computed` members

```cpp
pulse::property<std::vector<Item>> items;
pulse::property<std::string> filter;
pulse::computed<std::vector<Item>> visible;     // used by ForEach
pulse::computed<std::string> summary;           // used by a Text
pulse::computed<bool> can_clear;                // used by .enabled
```

One fact, one computed. The view tree only refers to these names, which makes it read like a
specification.

### Formatting at the edge

Keep properties in domain types (`double`, `enum class Status`, `std::chrono::seconds`) and convert to
text in the binding with `map`. That way logic and tests work with real types.

### Feedback without loops

Two widgets editing the same value (a `Slider` and a `TextField` for the same number) should share one
property of the domain type and convert in both directions:

```cpp
pulse::property<double> volume{0.5};
pulse::property<std::string> volume_text;
auto to_text = volume.as_observable().subscribe([&](double v) { volume_text = std::to_string(int(v * 100)); });
auto from_text = volume_text.changed.connect([&](const std::string& s) {
  try { volume = std::clamp(std::stod(s) / 100, 0.0, 1.0); } catch (...) {}
});

VStack{ Slider(volume), TextField(volume_text) };
```

The equality checks stop the round trip as soon as values agree.

Next: [Layout](07-layout.md).
