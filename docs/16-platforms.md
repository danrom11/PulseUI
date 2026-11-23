# 16. Platforms

PulseUI has one cross-platform core (views, layout, input, theming, forms) and a thin backend per
platform: Windows, macOS and Linux (X11). Application code is identical on all platforms.

| | Windows | macOS | Linux |
|---|---|---|---|
| Windows and events | Win32 | AppKit (Cocoa) | X11 (Xlib), also on Wayland desktops through XWayland |
| Drawing | Direct2D | CoreGraphics | Cairo |
| Text | DirectWrite | CoreText / AppKit | Pango (fontconfig fallback for any script and emoji) |
| Images | WIC | ImageIO | gdk-pixbuf (PNG through Cairo without it) |
| File dialogs | Common Item Dialog (`IFileDialog`) | `NSOpenPanel` / `NSSavePanel` | xdg-desktop-portal, else zenity or kdialog |
| HiDPI | per-monitor DPI awareness (v2) | Retina backing scale | `Xft.dpi` (and `GDK_SCALE`); `PULSEUI_SCALE` overrides |
| Primary modifier (`Mod::Primary`, "Ctrl" in shortcuts) | Ctrl | Cmd | Ctrl |

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

## Linux specifics

- The backend talks to an X server. On Wayland desktops (GNOME, KDE Plasma) the app runs through
  XWayland, which they start automatically; a native Wayland backend is planned.
- Build dependencies are found with `pkg-config`: `x11`, `xcursor`, `xkbcommon`, `cairo`, `cairo-xlib`,
  `pangocairo` (required), `gdk-pixbuf-2.0` and `gio-2.0` (optional). See
  [Getting started](02-getting-started.md#requirements) for the package names.
- **HiDPI:** the scale is `Xft.dpi / 96` (desktops set `Xft.dpi` to 192 for 200%), multiplied by
  `GDK_SCALE` when set. `PULSEUI_SCALE=1.5` forces a factor. The scale is read once at startup.
- **Keyboard:** shortcuts use the key's Latin meaning, so `Ctrl+C` works with any keyboard layout. Text
  goes through the X input method (XIM, `XMODIFIERS`), so dead keys and compose sequences work; IMEs that
  only support Wayland protocols are not used through XWayland.
- **Menus and message boxes** are drawn by PulseUI in the current theme, as on Windows. Modal windows are
  transient dialogs of their parent (`_NET_WM_STATE_MODAL`); the parent ignores input while one is open.
- **File dialogs** use the desktop's own dialog through xdg-desktop-portal when it runs (GNOME, KDE and
  most desktops), else `zenity` or `kdialog`. `PULSEUI_NO_PORTAL=1` skips the portal.
- **Clipboard:** the `CLIPBOARD` selection with UTF-8 text. The data lives in the application: after it
  exits the copied text is gone unless a clipboard manager kept it.
- **No X server** (`DISPLAY` unset, CI, WSL without WSLg): windows exist only in memory. Tests
  (`pulseui::testing`) and `PULSEUI_SCREENSHOT` work, as rendering is done by Cairo offscreen. Run the
  app under `xvfb-run` to test a real X connection without a screen.
- Not yet: drag and drop between applications (XDND) and screen readers (AT-SPI).
- `Window::native_handle()` returns the X11 window id (`::Window`) cast to `void*`.
- **Clang and libstdc++:** Clang uses the newest GCC's C++ library it finds. A Clang older than that library may
  fail on its headers (errors inside `<string>` or `<vector>` such as "member access into incomplete type
  `__normal_iterator`", for example Clang 16 with GCC 16). Build with `g++`, install a newer Clang, or point
  Clang to an older GCC with `-DCMAKE_CXX_FLAGS=--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/<version>`.

## Fonts

The theme's fonts use the system UI font by default: Segoe UI on Windows, the San Francisco system
font on macOS, fontconfig's `Sans` on Linux (Ubuntu, Cantarell, Noto Sans, DejaVu Sans... depending on
the distribution). `Font::family` selects another installed font by name; `"monospace"` maps to the
platform's monospace font (Consolas on Windows, the system monospaced font or Menlo on macOS,
fontconfig's `Monospace` on Linux). Metrics differ slightly between fonts, so leave some room in fixed
sizes.

## Packaging

PulseUI links statically into your executable. There are no runtime libraries to ship besides the
compiler runtime (MSVC redistributable or MinGW's `libstdc++`/`libgcc` DLLs, unless you link them
statically). On macOS, sign and notarize the `.app` as you would any other app. On Linux the program
uses the system's X11, Cairo and Pango libraries, which every desktop has; ship the executable (or
wrap it in an AppImage or a distribution package).

Next: [Patterns, pitfalls and FAQ](17-patterns-and-faq.md).
