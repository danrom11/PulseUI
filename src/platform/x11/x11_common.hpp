#pragma once
// Shared state of the X11 backend (internal).
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xresource.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xcursor/Xcursor.h>
#include <cairo-xlib.h>

// X and PulseUI share some names (Window, Cursor, Font): X types are used
// through these aliases.
using XWindow = ::Window;
using XCursorId = ::Cursor;

// Xlib defines macros that clash with C++ code and PulseUI names
#ifdef None
#undef None
constexpr long XNone = 0L;
#endif
#ifdef Bool
#undef Bool
using XBool = int;
#endif
#ifdef Status
#undef Status
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Always
#undef Always
#endif
#ifdef Success
#undef Success
#endif
#ifdef CurrentTime
#undef CurrentTime
#endif

#include <functional>
#include <string>

namespace pulseui::platform::x11 {

struct Atoms {
  Atom wm_protocols, wm_delete_window, net_wm_ping, net_wm_name, net_wm_pid, net_wm_state, net_wm_state_modal,
      net_wm_window_type, net_wm_window_type_dialog, net_wm_window_type_normal, net_active_window, utf8_string,
      clipboard, targets, text, text_plain_utf8, incr, pulse_selection, net_wm_user_time;
};

// Null when no X server could be reached: windows then exist only in memory,
// which is enough for tests and PULSEUI_SCREENSHOT renders.
Display* display();
const Atoms& atoms();
XWindow helper_window();  // unmapped; owns the clipboard
XIM input_method();      // may be null
Time last_user_time();
void set_last_user_time(Time t);

// Physical pixels per logical pixel (Xft.dpi / 96, PULSEUI_SCALE overrides).
float screen_scale();

// While positive, windows drop user input (a blocking system dialog is open).
void block_input(bool on);
bool input_blocked();

// x11_window.cpp
class X11Window;
X11Window* find_window(XWindow xid);
void dispatch_window_event(XEvent& e); // event for one of our windows
void paint_pending_windows();

// x11_platform.cpp: handles selection traffic of the helper window.
void handle_selection_event(XEvent& e);

// Dispatch events (X, posted work, timers) for up to `timeout_ms` or until
// `done()`; used by blocking helpers (dialogs, clipboard reads). False when
// the application is quitting (nothing is dispatched then).
bool pump_until(const std::function<bool()>& done, int timeout_ms);

} // namespace pulseui::platform::x11
