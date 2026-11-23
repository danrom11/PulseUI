// X11 backend: windows and input.
#include "x11_common.hpp"

#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

#include <pulseui/ui/window.hpp>

#include "../cairo/cairo_painter.hpp"
#include "../platform.hpp"

namespace pulseui::platform {
namespace x11 {

namespace {

std::unordered_map<XWindow, X11Window*>& registry() {
  static std::unordered_map<XWindow, X11Window*> r;
  return r;
}

unsigned mods_of(unsigned state) {
  unsigned m = 0;
  if (state & ShiftMask) m |= Mod::Shift;
  if (state & ControlMask) m |= Mod::Ctrl;
  if (state & Mod1Mask) m |= Mod::Alt;
  if (state & Mod4Mask) m |= Mod::Super;
  return m;
}

Key map_keysym(KeySym k) {
  if (k >= XK_a && k <= XK_z) return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(k - XK_a));
  if (k >= XK_A && k <= XK_Z) return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(k - XK_A));
  if (k >= XK_0 && k <= XK_9) return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(k - XK_0));
  if (k >= XK_F1 && k <= XK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(k - XK_F1));
  switch (k) {
    case XK_Escape: return Key::Escape;
    case XK_Tab:
    case XK_ISO_Left_Tab: return Key::Tab;
    case XK_BackSpace: return Key::Backspace;
    case XK_Return:
    case XK_KP_Enter: return Key::Enter;
    case XK_space: return Key::Space;
    case XK_Insert:
    case XK_KP_Insert: return Key::Insert;
    case XK_Delete:
    case XK_KP_Delete: return Key::Delete;
    case XK_Home:
    case XK_KP_Home: return Key::Home;
    case XK_End:
    case XK_KP_End: return Key::End;
    case XK_Prior:
    case XK_KP_Prior: return Key::PageUp;
    case XK_Next:
    case XK_KP_Next: return Key::PageDown;
    case XK_Left:
    case XK_KP_Left: return Key::Left;
    case XK_Right:
    case XK_KP_Right: return Key::Right;
    case XK_Up:
    case XK_KP_Up: return Key::Up;
    case XK_Down:
    case XK_KP_Down: return Key::Down;
    case XK_Shift_L:
    case XK_Shift_R: return Key::Shift;
    case XK_Control_L:
    case XK_Control_R: return Key::Control;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R: return Key::Alt;
    case XK_Super_L:
    case XK_Super_R: return Key::Super;
    case XK_slash:
    case XK_question: return Key::Slash;
    case XK_Menu: return Key::Menu;
    default: return Key::Unknown;
  }
}

// Letters and digits by physical position (evdev codes), so shortcuts such as
// Ctrl+C work with a non-Latin layout as the only layout.
Key map_keycode(unsigned keycode) {
  static const Key letters[] = {
      /*16*/ Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I, Key::O, Key::P, Key::Unknown, Key::Unknown,
      Key::Unknown, Key::Unknown,
      /*30*/ Key::A, Key::S, Key::D, Key::F, Key::G, Key::H, Key::J, Key::K, Key::L, Key::Unknown, Key::Unknown,
      Key::Unknown, Key::Unknown, Key::Unknown,
      /*44*/ Key::Z, Key::X, Key::C, Key::V, Key::B, Key::N, Key::M, Key::Unknown, Key::Unknown, Key::Slash};
  if (keycode < 8) return Key::Unknown;
  const unsigned ev = keycode - 8;
  if (ev >= 2 && ev <= 10) return static_cast<Key>(static_cast<int>(Key::Num1) + static_cast<int>(ev - 2));
  if (ev == 11) return Key::Num0;
  if (ev >= 16 && ev <= 53) return letters[ev - 16];
  return Key::Unknown;
}

// Xcursor theme names first (they follow the desktop's cursor theme), then the
// core cursor font.
XCursorId load_cursor(Display* d, pulseui::Cursor c) {
  static std::unordered_map<int, XCursorId> cache;
  if (auto it = cache.find(static_cast<int>(c)); it != cache.end()) return it->second;
  const char* name = "default";
  unsigned shape = XC_left_ptr;
  switch (c) {
    case pulseui::Cursor::IBeam: name = "text"; shape = XC_xterm; break;
    case pulseui::Cursor::Hand: name = "pointer"; shape = XC_hand2; break;
    case pulseui::Cursor::Crosshair: name = "crosshair"; shape = XC_crosshair; break;
    case pulseui::Cursor::SizeWE: name = "ew-resize"; shape = XC_sb_h_double_arrow; break;
    case pulseui::Cursor::SizeNS: name = "ns-resize"; shape = XC_sb_v_double_arrow; break;
    case pulseui::Cursor::SizeNWSE: name = "nwse-resize"; shape = XC_bottom_right_corner; break;
    case pulseui::Cursor::SizeNESW: name = "nesw-resize"; shape = XC_bottom_left_corner; break;
    case pulseui::Cursor::SizeAll: name = "move"; shape = XC_fleur; break;
    default: break;
  }
  XCursorId cur = XcursorLibraryLoadCursor(d, name);
  if (!cur) cur = XCreateFontCursor(d, shape);
  cache.emplace(static_cast<int>(c), cur);
  return cur;
}

void set_utf8_property(Display* d, XWindow w, Atom prop, const std::string& s) {
  XChangeProperty(d, w, prop, atoms().utf8_string, 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(s.data()), static_cast<int>(s.size()));
}

std::set<unsigned>& pressed_keys() {
  static std::set<unsigned> keys;
  return keys;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// X11Window
// ─────────────────────────────────────────────────────────────────────────────
class X11Window final : public NativeWindow {
public:
  X11Window(pulseui::Window* owner, const WindowConfig& cfg) : owner_(owner), title_(cfg.title) {
    dpy_ = display();
    scale_ = screen_scale();
    size_ = {cfg.width, cfg.height};
    if (!dpy_) return; // in-memory window (no X server)

    const int screen = DefaultScreen(dpy_);
    pw_ = std::max(1, static_cast<int>(std::lround(cfg.width * scale_)));
    ph_ = std::max(1, static_cast<int>(std::lround(cfg.height * scale_)));
    const int sx = std::max(0, (DisplayWidth(dpy_, screen) - pw_) / 2);
    const int sy = std::max(0, (DisplayHeight(dpy_, screen) - ph_) / 2);

    XSetWindowAttributes a{};
    a.background_pixmap = XNone; // we paint everything: no flicker
    a.bit_gravity = NorthWestGravity;
    a.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                   ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask |
                   PropertyChangeMask;
    win_ = XCreateWindow(dpy_, RootWindow(dpy_, screen), sx, sy, static_cast<unsigned>(pw_),
                         static_cast<unsigned>(ph_), 0, CopyFromParent, InputOutput, CopyFromParent,
                         CWBackPixmap | CWBitGravity | CWEventMask, &a);
    registry()[win_] = this;

    Atom protocols[] = {atoms().wm_delete_window, atoms().net_wm_ping};
    XSetWMProtocols(dpy_, win_, protocols, 2);
    const long pid = static_cast<long>(::getpid());
    XChangeProperty(dpy_, win_, atoms().net_wm_pid, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&pid), 1);
    const Atom type = atoms().net_wm_window_type_normal;
    XChangeProperty(dpy_, win_, atoms().net_wm_window_type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&type), 1);

    // WM_CLASS: the executable name (desktop files match it) and "PulseUI"
    std::string res_name = "pulseui";
    if (const char* p = std::getenv("RESOURCE_NAME")) res_name = p;
#ifdef __GLIBC__
    else if (program_invocation_short_name && *program_invocation_short_name)
      res_name = program_invocation_short_name;
#endif
    XClassHint ch{};
    ch.res_name = res_name.data();
    char cls[] = "PulseUI";
    ch.res_class = cls;
    XSetClassHint(dpy_, win_, &ch);

    XWMHints wm{};
    wm.flags = InputHint | StateHint;
    wm.input = 1;
    wm.initial_state = NormalState;
    XSetWMHints(dpy_, win_, &wm);

    set_title(cfg.title);
    update_size_hints(sx, sy);

    if (XIM im = input_method()) {
      xic_ = XCreateIC(im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, win_, XNFocusWindow,
                       win_, nullptr);
      if (xic_) {
        long filter = 0;
        if (!XGetICValues(xic_, XNFilterEvents, &filter, nullptr) && filter)
          XSelectInput(dpy_, win_, a.event_mask | filter);
      }
    }
  }

  ~X11Window() override {
    owner_ = nullptr;
    destroy_native();
  }

  // ── NativeWindow ──
  void set_title(const std::string& t) override {
    title_ = t;
    if (!win_) return;
    XStoreName(dpy_, win_, t.c_str());
    set_utf8_property(dpy_, win_, atoms().net_wm_name, t);
    XFlush(dpy_);
  }

  void set_size(float w, float h) override {
    size_ = {w, h};
    if (!win_) return;
    pw_ = std::max(1, static_cast<int>(std::lround(w * scale_)));
    ph_ = std::max(1, static_cast<int>(std::lround(h * scale_)));
    update_size_hints();
    XResizeWindow(dpy_, win_, static_cast<unsigned>(pw_), static_cast<unsigned>(ph_));
    XFlush(dpy_);
  }

  void set_min_size(float w, float h) override {
    min_ = {w, h};
    if (win_) update_size_hints();
  }

  void set_resizable(bool r) override {
    resizable_ = r;
    if (win_) update_size_hints();
  }

  Size size() const override { return size_; }

  void show() override {
    if (!win_) {
      static bool warned = false;
      if (!dpy_ && !warned && !std::getenv("PULSEUI_SCREENSHOT")) {
        warned = true;
        std::fprintf(stderr, "PulseUI: cannot open the X display (DISPLAY=%s); windows are not shown\n",
                     std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "");
      }
      return;
    }
    XMapRaised(dpy_, win_);
    if (modal_parent_) activate();
    XFlush(dpy_);
  }

  void hide() override {
    if (!win_) return;
    XWithdrawWindow(dpy_, win_, DefaultScreen(dpy_));
    XFlush(dpy_);
  }

  void destroy() override {
    if (destroyed_) return;
    pulseui::Window* o = owner_;
    destroy_native();
    destroyed_ = true;
    if (o) o->handle_destroyed();
  }

  void invalidate() override { dirty_ = true; }

  void set_cursor(pulseui::Cursor c) override {
    if (c == cursor_) return;
    cursor_ = c;
    if (!win_) return;
    XDefineCursor(dpy_, win_, load_cursor(dpy_, c));
    XFlush(dpy_);
  }

  void capture_mouse(bool) override {} // the implicit pointer grab of a pressed button routes drags here
  float scale() const override { return scale_; }
  void* handle() const override { return reinterpret_cast<void*>(win_); }

  void set_modal_parent(NativeWindow* parent) override {
    release_modal();
    if (!win_) return;
    auto* p = static_cast<X11Window*>(parent);
    if (!p || !p->win_) {
      XSetTransientForHint(dpy_, win_, 0);
      XDeleteProperty(dpy_, win_, atoms().net_wm_state);
      return;
    }
    modal_parent_ = p->win_;
    p->modal_child_ = win_;
    XSetTransientForHint(dpy_, win_, p->win_);
    const Atom st = atoms().net_wm_state_modal;
    XChangeProperty(dpy_, win_, atoms().net_wm_state, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&st), 1);
    const Atom type = atoms().net_wm_window_type_dialog;
    XChangeProperty(dpy_, win_, atoms().net_wm_window_type, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&type), 1);
    // centred over the parent, a third from its top (like on Windows)
    int px = 0, py = 0;
    XWindow child = 0;
    XTranslateCoordinates(dpy_, p->win_, DefaultRootWindow(dpy_), 0, 0, &px, &py, &child);
    const int x = px + (p->pw_ - pw_) / 2;
    const int y = py + std::max(0, (p->ph_ - ph_) / 3);
    XMoveWindow(dpy_, win_, std::max(0, x), std::max(0, y));
    update_size_hints(std::max(0, x), std::max(0, y));
  }

  void activate() override {
    if (!win_) return;
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = win_;
    e.xclient.message_type = atoms().net_active_window;
    e.xclient.format = 32;
    e.xclient.data.l[0] = 1; // from an application
    e.xclient.data.l[1] = static_cast<long>(last_user_time());
    XSendEvent(dpy_, DefaultRootWindow(dpy_), 0, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XRaiseWindow(dpy_, win_);
    XFlush(dpy_);
  }

  // ── events ──
  void handle_event(XEvent& e) {
    switch (e.type) {
      case Expose:
        if (e.xexpose.count == 0) dirty_ = true;
        break;
      case MapNotify:
        mapped_ = true;
        dirty_ = true;
        break;
      case UnmapNotify:
        mapped_ = false;
        break;
      case ConfigureNotify: {
        const int w = e.xconfigure.width, h = e.xconfigure.height;
        if (w != pw_ || h != ph_ || !configured_) {
          configured_ = true;
          pw_ = w;
          ph_ = h;
          size_ = {w / scale_, h / scale_};
          if (owner_) owner_->handle_resize(size_);
          dirty_ = true;
        }
        break;
      }
      case ClientMessage:
        if (e.xclient.message_type == atoms().wm_protocols) {
          const Atom what = static_cast<Atom>(e.xclient.data.l[0]);
          if (what == atoms().wm_delete_window) {
            if (modal_child_ && find_window(modal_child_)) break; // a modal dialog is open
            if (!owner_ || owner_->handle_close_request()) destroy();
          } else if (what == atoms().net_wm_ping) {
            XEvent r = e;
            r.xclient.window = DefaultRootWindow(dpy_);
            XSendEvent(dpy_, r.xclient.window, 0, SubstructureRedirectMask | SubstructureNotifyMask, &r);
          }
        }
        break;
      case FocusIn:
        if (xic_) XSetICFocus(xic_);
        break;
      case FocusOut:
        if (xic_) XUnsetICFocus(xic_);
        pressed_keys().clear();
        break;
      case ButtonPress:
      case ButtonRelease:
        set_last_user_time(e.xbutton.time);
        if (blocked(e.type == ButtonPress)) break;
        on_button(e.xbutton);
        break;
      case MotionNotify: {
        if (blocked(false)) break;
        // only the latest position matters
        while (XCheckTypedWindowEvent(dpy_, win_, MotionNotify, &e)) {
        }
        InputEvent ev;
        ev.type = InputEvent::MouseMove;
        ev.pos = ev.window_pos = {e.xmotion.x / scale_, e.xmotion.y / scale_};
        ev.mods = mods_of(e.xmotion.state);
        emit(ev);
        break;
      }
      case LeaveNotify:
        if (e.xcrossing.mode != NotifyNormal || e.xcrossing.detail == NotifyInferior) break;
        {
          InputEvent ev;
          ev.type = InputEvent::MouseLeave;
          emit(ev);
        }
        break;
      case KeyPress:
      case KeyRelease:
        set_last_user_time(e.xkey.time);
        if (blocked(false)) break;
        on_key(e.xkey);
        break;
      default:
        break;
    }
  }

  void paint_if_needed() {
    if (!dirty_ || !win_ || !mapped_ || !owner_) return;
    dirty_ = false;
    if (!back_ || back_w_ != pw_ || back_h_ != ph_) {
      free_backbuffer();
      back_w_ = pw_;
      back_h_ = ph_;
      const int screen = DefaultScreen(dpy_);
      back_ = XCreatePixmap(dpy_, win_, static_cast<unsigned>(pw_), static_cast<unsigned>(ph_),
                            static_cast<unsigned>(DefaultDepth(dpy_, screen)));
      surface_ = cairo_xlib_surface_create(dpy_, back_, DefaultVisual(dpy_, screen), pw_, ph_);
      if (!gc_) gc_ = XCreateGC(dpy_, win_, 0, nullptr);
    }
    cairo_t* cr = cairo_create(surface_);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_scale(cr, scale_, scale_);
    {
      cairo::CairoPainter p(cr);
      owner_->handle_paint(p);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface_);
    if (win_) XCopyArea(dpy_, back_, win_, gc_, 0, 0, static_cast<unsigned>(pw_), static_cast<unsigned>(ph_), 0, 0);
  }

private:
  void emit(InputEvent e) {
    if (owner_) owner_->handle_input(std::move(e));
  }

  // Input is dropped while a modal child or a system dialog is open; a click
  // brings the modal child forward.
  bool blocked(bool press) {
    if (input_blocked()) return true;
    if (!modal_child_) return false;
    X11Window* child = find_window(modal_child_);
    if (!child) {
      modal_child_ = 0;
      return false;
    }
    if (press) child->activate();
    return true;
  }

  void on_button(const XButtonEvent& b) {
    const Point pos{b.x / scale_, b.y / scale_};
    if (b.button >= 4 && b.button <= 7) {
      if (b.type != ButtonPress) return;
      InputEvent ev;
      ev.type = InputEvent::Wheel;
      ev.pos = ev.window_pos = pos;
      ev.mods = mods_of(b.state);
      if (b.button == 4) ev.wheel_dy = 1;
      else if (b.button == 5) ev.wheel_dy = -1;
      else if (b.button == 6) ev.wheel_dx = 1;
      else ev.wheel_dx = -1;
      emit(ev);
      return;
    }
    MouseButton mb = MouseButton::None;
    if (b.button == Button1) mb = MouseButton::Left;
    else if (b.button == Button2) mb = MouseButton::Middle;
    else if (b.button == Button3) mb = MouseButton::Right;
    else return; // back/forward buttons
    InputEvent ev;
    ev.type = b.type == ButtonPress ? InputEvent::MouseDown : InputEvent::MouseUp;
    ev.pos = ev.window_pos = pos;
    ev.button = mb;
    ev.mods = mods_of(b.state);
    if (b.type == ButtonPress) {
      // multi-clicks: same button, close in time and space
      const bool chain = mb == last_click_button_ && b.time - last_click_time_ <= 400 &&
                         std::abs(b.x - last_click_x_) <= 4 && std::abs(b.y - last_click_y_) <= 4;
      click_count_ = chain ? click_count_ + 1 : 1;
      last_click_button_ = mb;
      last_click_time_ = b.time;
      last_click_x_ = b.x;
      last_click_y_ = b.y;
      ev.clicks = click_count_;
    }
    emit(ev);
  }

  void on_key(XKeyEvent& k) {
    const bool press = k.type == KeyPress;
    if (k.keycode == 0) { // text committed by an input method
      if (press) emit_text(k);
      return;
    }
    KeySym sym = XLookupKeysym(&k, 0);
    Key key = map_keysym(sym);
    if (key == Key::Unknown) key = map_keycode(k.keycode);
    unsigned mods = mods_of(k.state);
    // the state is from before this key: a modifier key counts itself (as on Windows)
    unsigned self = key == Key::Shift ? Mod::Shift : key == Key::Control ? Mod::Ctrl : key == Key::Alt ? Mod::Alt
                    : key == Key::Super ? Mod::Super : 0u;
    mods = press ? (mods | self) : (mods & ~self);

    InputEvent ev;
    ev.type = press ? InputEvent::KeyDown : InputEvent::KeyUp;
    ev.key = key;
    ev.mods = mods;
    if (press) {
      ev.repeat = !pressed_keys().insert(k.keycode).second;
    } else {
      pressed_keys().erase(k.keycode);
    }
    emit(ev);
    if (press && !(k.state & (ControlMask | Mod1Mask | Mod4Mask))) emit_text(k);
  }

  void emit_text(XKeyEvent& k) {
    char buf[64];
    std::string text;
    KeySym sym = 0;
    if (xic_) {
      int status = 0;
      int n = Xutf8LookupString(xic_, &k, buf, sizeof buf - 1, &sym, &status);
      if (status == XBufferOverflow) {
        std::string big(static_cast<std::size_t>(n) + 1, '\0');
        n = Xutf8LookupString(xic_, &k, big.data(), n, &sym, &status);
        big.resize(static_cast<std::size_t>(std::max(0, n)));
        text = std::move(big);
      } else if (status == XLookupChars || status == XLookupBoth) {
        text.assign(buf, static_cast<std::size_t>(std::max(0, n)));
      }
    } else {
      XLookupString(&k, buf, sizeof buf - 1, &sym, nullptr);
      char u[16] = {};
      if (sym && xkb_keysym_to_utf8(static_cast<xkb_keysym_t>(sym), u, sizeof u) > 0) text = u;
    }
    if (text.empty()) return;
    const auto c = static_cast<unsigned char>(text[0]);
    if (c < 0x20 || c == 0x7F) return; // control characters come as KeyDown
    InputEvent ev;
    ev.type = InputEvent::Text;
    ev.text = std::move(text);
    ev.mods = mods_of(k.state);
    emit(ev);
  }

  void update_size_hints(int x = -1, int y = -1) {
    XSizeHints* h = XAllocSizeHints();
    h->flags = PMinSize;
    if (x >= 0 && y >= 0) {
      h->flags |= PPosition;
      h->x = x;
      h->y = y;
    }
    h->min_width = std::max(1, static_cast<int>(min_.w * scale_));
    h->min_height = std::max(1, static_cast<int>(min_.h * scale_));
    if (!resizable_) {
      h->flags |= PMaxSize;
      h->min_width = h->max_width = pw_;
      h->min_height = h->max_height = ph_;
    }
    XSetWMNormalHints(dpy_, win_, h);
    XFree(h);
  }

  void release_modal() {
    if (!modal_parent_) return;
    if (X11Window* p = find_window(modal_parent_))
      if (p->modal_child_ == win_) {
        p->modal_child_ = 0;
        if (win_ && p->win_) p->activate();
      }
    modal_parent_ = 0;
  }

  void free_backbuffer() {
    if (surface_) cairo_surface_destroy(surface_);
    surface_ = nullptr;
    if (back_) XFreePixmap(dpy_, back_);
    back_ = 0;
  }

  void destroy_native() {
    if (!win_) return;
    release_modal();
    free_backbuffer();
    if (gc_) XFreeGC(dpy_, gc_);
    gc_ = nullptr;
    if (xic_) XDestroyIC(xic_);
    xic_ = nullptr;
    registry().erase(win_);
    XDestroyWindow(dpy_, win_);
    XFlush(dpy_);
    win_ = 0;
  }

  pulseui::Window* owner_;
  Display* dpy_ = nullptr;
  XWindow win_ = 0;
  XIC xic_ = nullptr;
  Pixmap back_ = 0;
  cairo_surface_t* surface_ = nullptr;
  GC gc_ = nullptr;
  int back_w_ = 0, back_h_ = 0;
  int pw_ = 0, ph_ = 0; // physical size
  Size size_;           // logical size
  Size min_{200, 120};
  float scale_ = 1;
  std::string title_;
  pulseui::Cursor cursor_ = pulseui::Cursor::Arrow;
  XWindow modal_parent_ = 0, modal_child_ = 0;
  bool resizable_ = true, mapped_ = false, configured_ = false, dirty_ = true, destroyed_ = false;
  MouseButton last_click_button_ = MouseButton::None;
  Time last_click_time_ = 0;
  int last_click_x_ = 0, last_click_y_ = 0, click_count_ = 0;
};

X11Window* find_window(XWindow xid) {
  auto it = registry().find(xid);
  return it == registry().end() ? nullptr : it->second;
}

void dispatch_window_event(XEvent& e) {
  if (X11Window* w = find_window(e.xany.window)) w->handle_event(e);
}

void paint_pending_windows() {
  if (!display()) return;
  // painting may close windows: iterate over a snapshot
  std::vector<XWindow> ids;
  ids.reserve(registry().size());
  for (auto& [id, w] : registry()) ids.push_back(id);
  for (XWindow id : ids)
    if (X11Window* w = find_window(id)) w->paint_if_needed();
}

} // namespace x11

std::unique_ptr<NativeWindow> create_window(Window* owner, const WindowConfig& cfg) {
  return std::make_unique<x11::X11Window>(owner, cfg);
}

// XDND (drag and drop between applications) is not implemented yet: drags stay
// inside the application.
bool start_drag(NativeWindow*, const DragData&) { return false; }

} // namespace pulseui::platform
