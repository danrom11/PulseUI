# 16. Platforms

PulseUI has one cross-platform core (views, layout, input, theming, forms) and a thin backend per
platform. Application code is identical on all platforms.

| | Windows | macOS |
|---|---|---|
| Windows and events | Win32 | AppKit (Cocoa) |
| Drawing | Direct2D | CoreGraphics |
| Text | DirectWrite | CoreText / AppKit |
| Images | WIC | ImageIO |
| File dialogs | Common Item Dialog (`IFileDialog`) | `NSOpenPanel` / `NSSavePanel` |
| HiDPI | per-monitor DPI awareness (v2) | Retina backing scale |
| Primary modifier (`Mod::Primary`, "Ctrl" in shortcuts) | Ctrl | Cmd |

## Coordinates and HiDPI

All PulseUI coordinates and sizes are **logical pixels**. On a 150% display, a 100 px wide button is
150 physical pixels wide; on a Retina Mac, 200. `window.scale()` reports the factor. Text and shapes
are rendered at the physical resolution, so they stay sharp. Images are drawn into logical rects; for
crisp images on HiDPI screens, provide higher-resolution images and draw them into a smaller rect.

When a window moves between monitors with different scale factors, it is re-laid out and re-rendered.

## The UI thread

- Create the `App` and all windows on the **main thread** and run `app.run()` there.
- Touch views only on that thread. Use `App::post`, `App::ui()` or property bindings to get results
  from worker threads to the UI (see [Reactive bindings](06-reactive-bindings.md#threads)).
- `App::is_ui_thread()` tells you where you are.

## Windows specifics

- Use `pulseui_add_app` (or link with `/SUBSYSTEM:WINDOWS` and `/ENTRY:mainCRTStartup`) to get a GUI
  app with a normal `int main()` and no console window. For debugging, a plain console executable works
  as well.
- Source files are UTF-8. With MSVC, compile with `/utf-8` (`pulseui_add_app` adds it) so string
  literals such as `"✕"` or `"°C"` are encoded correctly.
- Supported toolchains: MSVC (Visual Studio 2022) and MinGW-w64 GCC 11+. Clang targeting the MSVC
  runtime needs a version that supports your Visual Studio STL (Clang 19+ for recent VS 2022).
- `Window::native_handle()` returns the `HWND`.

## macOS specifics

- `pulseui_add_app` creates an `.app` bundle (`MACOSX_BUNDLE`). Set bundle properties as usual:

  ```cmake
  set_target_properties(my_app PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "My App"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.myapp")
  ```
- The backend is Objective-C++ (`.mm` files) compiled with ARC. CMake enables the `OBJCXX` language
  automatically on Apple platforms.
- "Ctrl" in shortcuts and `e.primary()` mean Cmd, so `"Ctrl+S"` is Cmd+S as Mac users expect.
- `Window::native_handle()` returns the `NSWindow*`.

## Linux

Not supported yet. The core is portable; a backend (for example Wayland/X11 with Cairo and Pango) is
the missing piece. Contributions are welcome.

## Fonts

The theme's fonts use the system UI font by default: Segoe UI on Windows, the San Francisco system
font on macOS. `Font::family` selects another installed font by name; `"monospace"` maps to the
platform's monospace font (Consolas on Windows, the system monospaced font or Menlo on macOS).

## Packaging

PulseUI links statically into your executable. There are no runtime libraries to ship besides the
compiler runtime (MSVC redistributable or MinGW's `libstdc++`/`libgcc` DLLs, unless you link them
statically). On macOS, sign and notarize the `.app` as you would any other app.

Next: [Patterns, pitfalls and FAQ](17-patterns-and-faq.md).
