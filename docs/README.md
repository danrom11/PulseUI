# PulseUI documentation

PulseUI is a lightweight, cross-platform **reactive UI library for C++20**, built on
[Pulse](https://github.com/danrom11/Pulse). You describe the interface as a tree of views, bind them
to your data (`pulse::property`, `pulse::computed`, any `pulse::observable`), and the UI keeps itself
up to date. There are no "update the label" calls, no models to subclass, and no code generators.

```cpp
pulse::property<int> count{0};

VStack{
  Text(count.map([](int c) { return "Clicked " + std::to_string(c) + " times"; })),
  Button("Click me", [&] { count = count + 1; }).primary(),
}.padding(20);
```

## Two ways in

- **The course**: start with [Tutorial: your first apps](03-tutorial.md), then
  [Tutorial: forms with Pulse Layer](04-forms-tutorial.md). They build real programs step by step and
  explain *why* things are done the way they are.
- **The reference**: every chapter from 5 on documents one area completely, with every function and
  parameter.

## Contents

### Guide

| # | Chapter | What it covers |
|---|---|---|
| 1 | [Introduction](01-introduction.md) | The reactive idea, how PulseUI differs from widget toolkits, architecture |
| 2 | [Getting started](02-getting-started.md) | CMake setup, a first window, `App` and `Window` in two minutes |
| 3 | [Tutorial: your first apps](03-tutorial.md) | Counter → converter → todo list → a model class → several windows → background work → custom drawing |
| 4 | [Tutorial: forms with Pulse Layer](04-forms-tutorial.md) | Designing windows visually, projects, generated code, binding forms by id |

### Reference

| # | Chapter | What it covers |
|---|---|---|
| 5 | [Views, nodes and modifiers](05-views-and-modifiers.md) | The view tree, `Node`, chainable modifiers, ids, `find`, lifecycle |
| 6 | [Reactive bindings](06-reactive-bindings.md) | `Bind<T>`, `Model<T>`, two-way binding, threads, lifetime |
| 7 | [Layout](07-layout.md) | Stacks, flex, alignment, padding, `Layer`, `Scroll`, `Split`, `Tabs`, `Show`, `ForEach` |
| 8 | [Widgets](08-widgets.md) | `Text`, `Button`, `TextField`, `Checkbox`, `Toggle`, `Slider`, `ProgressBar`, `Select`, `Segmented`, `ImageView`, `Divider`, `Spacer`, `Panel` |
| 9 | [Styling and themes](09-styling-and-themes.md) | `Style`, colors, the theme property, light and dark, custom themes |
| 10 | [App and windows](10-app-and-windows.md) | Event loop, timers, the UI executor, windows, shortcuts, popups, dialogs, clipboard |
| 11 | [Input and events](11-input-and-events.md) | Event flow, clicks and hover, keyboard focus, shortcuts, the input stream |
| 12 | [Custom drawing and custom views](12-custom-drawing.md) | `Canvas`, `Painter`, text measurement, images, writing your own view type |
| 13 | [CodeEditor](13-code-editor.md) | The multi-line editor: models, highlighting, completion, diagnostics |
| 14 | [Forms and reflection](14-forms-and-meta.md) | The `.pform` format, loading and saving, code generation, `meta`, custom widgets in the designer |
| 15 | [Testing and automation](15-testing.md) | `testing::Harness`: click, type and wait in hidden windows; snapshot tests |
| 16 | [Platforms](16-platforms.md) | Windows and macOS backends, HiDPI, threading rules, packaging |
| 17 | [Patterns, pitfalls and FAQ](17-patterns-and-faq.md) | Proven structures for real apps and the mistakes to avoid |
| 18 | [Cheat sheet](18-cheatsheet.md) | Everything on one page |

## Conventions

- Code assumes `#include <pulseui/pulseui.hpp>` and `using namespace pulseui;`.
  Pulse types are written with `pulse::`.
- `using namespace std::chrono_literals;` is assumed where you see `500ms`.
- Knowing the basics of Pulse (`property`, `computed`, `signal`, `subscription`) helps a lot.
  The [Pulse documentation](https://github.com/danrom11/Pulse/tree/main/docs) chapters 1–5 take
  about 30 minutes to read.
