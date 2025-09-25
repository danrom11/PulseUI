# PulseUI ⚡️

**PulseUI** is a lightweight, cross-platform **reactive UI library for C++**, built on top of [Pulse](https://github.com/danrom11/Pulse).

It brings the power of reactive programming into the UI layer: instead of manually handling state and events, your UI reacts to **observables** and data streams in real time.

---

## ✨ Features

- **Reactive by design**  
  UI widgets and windows are driven by [Pulse](https://github.com/danrom11/Pulse) observables. Events, rendering and input are all streams you can subscribe to.

- **Cross-platform**  
  Currently supports:
  - **Windows (Win32 + GDI)**
  - **macOS (Cocoa)**  
  Backends can be extended to other platforms.

- **Declarative rendering**  
  Define what should be drawn and how it reacts to state changes. No manual invalidation boilerplate.

- **Event streams**  
  Mouse, keyboard, resize events are delivered as reactive `InputEvent` streams.

- **Integration with Pulse**  
  Uses the same observable operators, schedulers, and executors from Pulse. This makes UI code composable with the rest of your reactive system.

---

## 📦 Requirements

- **C++20**  
- **CMake ≥ 3.21**  
- Tested compilers: GCC ≥ 12, Clang ≥ 14, MSVC ≥ 19.36

---

## 🛠️ Build

### Windows (MSVC + CMake)
```bash
cmake -S . -B build -DPULSEUI_BACKEND=win32 -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### macOS (Clang + CMake)
```bash
cmake -S . -B build -DPULSEUI_BACKEND=cocoa
cmake --build build
```

The examples can be found under `build/examples/` after compilation.

---

## 🚀 Example

```cpp
#include <pulseui/pulseui.hpp>

int main() {
  using namespace pulseui;

  platform::app_init();

  auto exec = platform::make_ui_executor();
  auto win  = platform::make_window(600, 360, "Hello PulseUI");

  win->on_paint([](ui::Canvas& g){
    g.clear({0.1f, 0.1f, 0.12f, 1.0f});
    g.fill_rect({20, 20, 200, 120}, {0.2f, 0.6f, 0.8f, 1.0f});
    g.draw_text({30, 40}, "Hello, reactive UI!", ui::Font{18.f}, {1,1,1,1});
  });

  platform::app_run();
}
```

---

## 🔌 Roadmap

- More backends (Linux)  
- Basic UI controls (buttons, text fields, lists)  
- Higher-level reactive bindings for layout and state management

---

## 📚 Related

- [Pulse](https://github.com/danrom11/Pulse) – the underlying header-only reactive programming library.

---

## License

Apache-2.0 License

---

> ⚠️ **Note**: PulseUI is still under active development. Expect frequent changes.
