# 2. Getting started

## Requirements

| | Windows | macOS | Linux |
|---|---|---|---|
| OS | Windows 10 or 11 | macOS 11+ (Intel and Apple Silicon) | X11 desktop, or Wayland with XWayland |
| Compiler | Visual Studio 2022 (MSVC), or MinGW-w64 GCC 11+ | Xcode 14+ (AppleClang) | GCC 11+ or Clang 14+ |
| CMake | 3.21+ | 3.21+ | 3.21+ |
| Graphics | Direct2D, DirectWrite, WIC (built into Windows) | AppKit, CoreGraphics, CoreText (built in) | X11, Cairo, Pango (system packages) |

On Windows and macOS there are no other dependencies. On Linux install the development packages
(found with `pkg-config`); on Debian and Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libx11-dev libxcursor-dev libxkbcommon-dev                  libcairo2-dev libpango1.0-dev libgdk-pixbuf-2.0-dev
```

`libgdk-pixbuf-2.0-dev` is optional (without it `Image::load` reads PNG only, and file dialogs fall back
from the desktop portal to zenity/kdialog). Fedora: `libX11-devel libXcursor-devel libxkbcommon-devel
cairo-devel pango-devel gdk-pixbuf2-devel`.

[Pulse](https://github.com/danrom11/Pulse) is header-only and is picked up automatically.

## Adding PulseUI to a project

### FetchContent (recommended)

```cmake
cmake_minimum_required(VERSION 3.21)
project(HelloPulseUI CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(pulseui URL https://github.com/danrom11/PulseUI/archive/0.5.0.tar.gz)
FetchContent_MakeAvailable(pulseui)

pulseui_add_app(hello main.cpp)
```

`pulseui_add_app(name sources...)` is a helper provided by PulseUI. It:

- creates a GUI executable: no console window on Windows (`WIN32`), an `.app` bundle on macOS
  (`MACOSX_BUNDLE`), a plain executable on Linux;
- links `PulseUI::PulseUI`;
- on MSVC, adds `/utf-8` and `/permissive-` and keeps `int main()` as the entry point, so you do not
  need `WinMain`.

If you want a console window as well (handy for `printf` debugging), use a plain target:

```cmake
add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE PulseUI::PulseUI)
```

PulseUI finds Pulse in this order: an existing `Pulse::pulse` target, a sibling checkout
`../Pulse` (configurable with `PULSEUI_PULSE_DIR`), and otherwise it downloads Pulse with
FetchContent.

### add_subdirectory

```cmake
add_subdirectory(external/PulseUI)
pulseui_add_app(hello main.cpp)
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `PULSEUI_BUILD_EXAMPLES` | ON when top-level | Build `examples/` (counter, todo, gallery, form_app) |
| `PULSEUI_BUILD_TESTS` | ON when top-level | Build the UI tests |
| `PULSEUI_PULSE_DIR` | `../Pulse` | Where to look for a local Pulse checkout |

## Hello, window

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  return run("Hello", 320, 120, Text("Hello, PulseUI!").title().padding(20));
}
```

`run(title, width, height, root)` creates the `App`, a `Window` with that content, shows it and runs
the event loop. It is perfect for small tools and experiments.

## `App` and `Window` in two minutes

For anything bigger, you create them yourself:

```cpp
int main(int argc, char** argv) {
  App app(argc, argv);                 // exactly one, on the main thread, before any Window

  Window win("My app", 640, 480);      // title (a value or a stream), size in logical pixels
  win.content(VStack{ /* ... */ });    // the root view; replaces any previous one
  win.set_min_size(400, 300);
  win.show();

  return app.run();                    // returns when the last window closes (or App::quit())
}
```

- **`App`** owns the platform event loop. It also gives you the UI executor (`App::ui()`), timers
  (`App::after`, `App::every`), `App::post` to run code on the UI thread from anywhere, and the
  clipboard. See [App and windows](10-app-and-windows.md).
- **`Window`** is a native window holding one root view. It lays out and paints the tree, dispatches
  input, manages keyboard focus, tooltips, popups and shortcuts.

## Build and run

```bash
cmake -S . -B build
cmake --build build --config Release
```

The executable is in `build/` (Ninja, Makefiles) or `build/Release/` (Visual Studio). On macOS it is
an app bundle: `open build/hello.app`. On Linux run `./build/hello`.

## Handy environment variables

| Variable | Effect |
|---|---|
| `PULSEUI_THEME=light` or `dark` | Start with that theme |
| `PULSEUI_SCREENSHOT=out.png` | Render the first window to a PNG about 0.5 s after start, then exit (for docs and visual checks) |
| `PULSEUI_SCALE=1.5` | Linux: force the HiDPI scale (default: `Xft.dpi / 96`) |

## Examples in the repository

| Example | Shows |
|---|---|
| `examples/counter` | The smallest reactive app |
| `examples/todo` | `ForEach`, `computed` filters, two-way `TextField`, `Select` |
| `examples/gallery` | Every widget, themes, `Tabs`, `Split`, `Canvas` |
| `examples/form_app` | Loading a `.pform` form at runtime and binding it by id |

Larger sample projects (multi-form apps, dashboards with Pulse streams) come with Pulse Layer in its
`samples/` folder.

Next: [Tutorial: your first apps](03-tutorial.md).
