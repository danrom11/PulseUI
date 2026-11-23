#include <windowsx.h>

#include <pulseui/ui/window.hpp>

#include "../platform.hpp"
#include "d2d_painter.hpp"
#include "win32_common.hpp"

namespace pulseui::platform {
namespace win32 {
void* register_drop(HWND hwnd, std::function<bool(Window::DragPhase, Point, const DragData&)> fn);
void unregister_drop(HWND hwnd, void* target);
// win32_uia.cpp
void* uia_create(HWND hwnd, Window* owner);
LRESULT uia_get_object(void* host, HWND hwnd, WPARAM w, LPARAM l);
void uia_focus_changed(void* host, View* v);
void uia_destroy(void* host, HWND hwnd);
} // namespace win32
using namespace win32;

namespace {

UINT dpi_for(HWND h) {
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  if (fn && h) return fn(h);
  HDC dc = GetDC(nullptr);
  const UINT d = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
  ReleaseDC(nullptr, dc);
  return d;
}

unsigned current_mods() {
  unsigned m = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) m |= Mod::Shift;
  if (GetKeyState(VK_CONTROL) & 0x8000) m |= Mod::Ctrl;
  if (GetKeyState(VK_MENU) & 0x8000) m |= Mod::Alt;
  if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= Mod::Super;
  return m;
}

Key map_vk(WPARAM vk) {
  if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
  if (vk >= '0' && vk <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (vk - '0'));
  if (vk >= VK_F1 && vk <= VK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));
  switch (vk) {
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_RETURN: return Key::Enter;
    case VK_SPACE: return Key::Space;
    case VK_INSERT: return Key::Insert;
    case VK_DELETE: return Key::Delete;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_SHIFT: return Key::Shift;
    case VK_CONTROL: return Key::Control;
    case VK_MENU: return Key::Alt;
    case VK_LWIN:
    case VK_RWIN: return Key::Super;
    case VK_OEM_2: return Key::Slash;
    case VK_APPS: return Key::Menu;
    default: return Key::Unknown;
  }
}

LPCWSTR cursor_id(Cursor c) {
  switch (c) {
    case Cursor::IBeam: return IDC_IBEAM;
    case Cursor::Hand: return IDC_HAND;
    case Cursor::Crosshair: return IDC_CROSS;
    case Cursor::SizeWE: return IDC_SIZEWE;
    case Cursor::SizeNS: return IDC_SIZENS;
    case Cursor::SizeNWSE: return IDC_SIZENWSE;
    case Cursor::SizeNESW: return IDC_SIZENESW;
    case Cursor::SizeAll: return IDC_SIZEALL;
    default: return IDC_ARROW;
  }
}

class Win32Window final : public NativeWindow {
public:
  Win32Window(Window* owner, const WindowConfig& cfg) : owner_(owner) {
    static bool registered = false;
    if (!registered) {
      WNDCLASSEXW wc{};
      wc.cbSize = sizeof wc;
      wc.style = CS_DBLCLKS;
      wc.lpfnWndProc = &Win32Window::proc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
      wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
      wc.lpszClassName = L"PulseUIWindow";
      RegisterClassExW(&wc);
      registered = true;
    }
    const float s = dpi_for(nullptr) / 96.f;
    RECT rc{0, 0, static_cast<LONG>(cfg.width * s), static_cast<LONG>(cfg.height * s)};
    AdjustWindowRectEx(&rc, style_, FALSE, 0);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    const int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    hwnd_ = CreateWindowExW(0, L"PulseUIWindow", widen(cfg.title).c_str(), style_,
                            std::max(0, sx), std::max(0, sy), ww, wh, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    // dark title bar when available (Windows 10 2004+ / 11)
    using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
      if (auto fn = reinterpret_cast<DwmSet>(
              reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")))) {
        BOOL dark = TRUE;
        fn(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
      }
    }
    drop_ = register_drop(hwnd_, [this](Window::DragPhase phase, Point p, const DragData& d) {
      return owner_ ? owner_->handle_drag(phase, p, d) : false;
    });
  }

  ~Win32Window() override {
    if (hwnd_) {
      owner_ = nullptr;
      release_modal();
      DestroyWindow(hwnd_);
    }
  }

  void set_resizable(bool r) override {
    style_ = r ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    const Size s = size();
    const LONG_PTR visible = IsWindowVisible(hwnd_) ? WS_VISIBLE : 0;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(style_) | visible);
    set_size(s.w, s.h);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void set_modal_parent(NativeWindow* parent) override {
    release_modal();
    HWND p = parent ? static_cast<HWND>(parent->handle()) : nullptr;
    SetWindowLongPtrW(hwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(p)); // owned: stays above the parent
    if (!p) return;
    modal_parent_ = p;
    RECT pr{}, wr{};
    GetWindowRect(p, &pr);
    GetWindowRect(hwnd_, &wr);
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    const int x = pr.left + ((pr.right - pr.left) - w) / 2;
    const int y = pr.top + std::max(0L, ((pr.bottom - pr.top) - h) / 3);
    SetWindowPos(hwnd_, nullptr, x, std::max(0, y), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    EnableWindow(p, FALSE);
  }

  void activate() override {
    if (hwnd_) SetForegroundWindow(hwnd_);
  }

  void a11y_focus_changed(View* v) override {
    if (uia_) uia_focus_changed(uia_, v);
  }

  // The owner must be enabled again before this window goes away, or Windows
  // activates some other application.
  void release_modal() {
    if (!modal_parent_) return;
    HWND p = modal_parent_;
    modal_parent_ = nullptr;
    EnableWindow(p, TRUE);
    if (hwnd_ && GetActiveWindow() == hwnd_) SetActiveWindow(p);
  }

  void set_title(const std::string& t) override { SetWindowTextW(hwnd_, widen(t).c_str()); }

  void set_size(float w, float h) override {
    const float s = scale();
    RECT rc{0, 0, static_cast<LONG>(w * s), static_cast<LONG>(h * s)};
    AdjustWindowRectEx(&rc, style_, FALSE, 0);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  void set_min_size(float w, float h) override { min_ = {w, h}; }

  Size size() const override {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float s = scale();
    return {(rc.right - rc.left) / s, (rc.bottom - rc.top) / s};
  }
  void show() override {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }
  void hide() override { ShowWindow(hwnd_, SW_HIDE); }
  void destroy() override {
    release_modal();
    if (hwnd_) DestroyWindow(hwnd_);
  }
  void invalidate() override {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
  }
  void set_cursor(Cursor c) override {
    if (c == cursor_) return;
    cursor_ = c;
    SetCursor(LoadCursorW(nullptr, cursor_id(c)));
  }
  void capture_mouse(bool on) override {
    if (on) SetCapture(hwnd_);
    else if (GetCapture() == hwnd_) ReleaseCapture();
  }
  float scale() const override { return dpi_for(hwnd_) / 96.f; }
  void* handle() const override { return hwnd_; }

private:
  static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Win32Window* self = nullptr;
    if (m == WM_NCCREATE) {
      self = static_cast<Win32Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = h;
    } else {
      self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    return self->handle(m, w, l);
  }

  void ensure_target() {
    if (rt_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float dpi = static_cast<float>(dpi_for(hwnd_));
    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    factories().d2d->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right),
                                                            static_cast<UINT32>(rc.bottom))),
        rt_.put());
    cache_.clear();
  }

  InputEvent mouse_event(InputEvent::Type t, LPARAM l, MouseButton b, int clicks = 1) {
    InputEvent e;
    e.type = t;
    const float s = scale();
    e.pos = e.window_pos = {GET_X_LPARAM(l) / s, GET_Y_LPARAM(l) / s};
    e.button = b;
    e.clicks = clicks;
    e.mods = current_mods();
    return e;
  }

  void track_leave() {
    if (tracking_) return;
    TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&tme);
    tracking_ = true;
  }

  void emit(InputEvent e) {
    if (owner_) owner_->handle_input(std::move(e));
  }

  LRESULT handle(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        ensure_target();
        if (rt_ && owner_) {
          rt_->BeginDraw();
          rt_->Clear(D2D1::ColorF(0, 0, 0, 1));
          {
            D2DPainter p(rt_.get(), cache_);
            owner_->handle_paint(p);
          }
          if (rt_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) rt_.reset();
        }
        EndPaint(hwnd_, &ps);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_SIZE: {
        if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
        if (owner_) owner_->handle_resize(size());
        return 0;
      }
      case WM_DPICHANGED: {
        auto* r = reinterpret_cast<RECT*>(l);
        rt_.reset();
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(l);
        const float s = scale();
        RECT rc{0, 0, static_cast<LONG>(min_.w * s), static_cast<LONG>(min_.h * s)};
        AdjustWindowRectEx(&rc, style_, FALSE, 0);
        mm->ptMinTrackSize.x = std::max<LONG>(mm->ptMinTrackSize.x, rc.right - rc.left);
        mm->ptMinTrackSize.y = std::max<LONG>(mm->ptMinTrackSize.y, rc.bottom - rc.top);
        return 0;
      }
      case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
          SetCursor(LoadCursorW(nullptr, cursor_id(cursor_)));
          return TRUE;
        }
        break;
      case WM_MOUSEMOVE:
        track_leave();
        emit(mouse_event(InputEvent::MouseMove, l, MouseButton::None));
        return 0;
      case WM_MOUSELEAVE: {
        tracking_ = false;
        InputEvent e;
        e.type = InputEvent::MouseLeave;
        emit(e);
        return 0;
      }
      case WM_LBUTTONDOWN: emit(mouse_event(InputEvent::MouseDown, l, MouseButton::Left)); return 0;
      case WM_LBUTTONDBLCLK: emit(mouse_event(InputEvent::MouseDown, l, MouseButton::Left, 2)); return 0;
      case WM_LBUTTONUP: emit(mouse_event(InputEvent::MouseUp, l, MouseButton::Left)); return 0;
      case WM_RBUTTONDOWN:
      case WM_RBUTTONDBLCLK: emit(mouse_event(InputEvent::MouseDown, l, MouseButton::Right)); return 0;
      case WM_RBUTTONUP: emit(mouse_event(InputEvent::MouseUp, l, MouseButton::Right)); return 0;
      case WM_MBUTTONDOWN: emit(mouse_event(InputEvent::MouseDown, l, MouseButton::Middle)); return 0;
      case WM_MBUTTONUP: emit(mouse_event(InputEvent::MouseUp, l, MouseButton::Middle)); return 0;
      case WM_MOUSEWHEEL:
      case WM_MOUSEHWHEEL: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd_, &pt);
        InputEvent e;
        e.type = InputEvent::Wheel;
        const float s = scale();
        e.pos = e.window_pos = {pt.x / s, pt.y / s};
        const float d = GET_WHEEL_DELTA_WPARAM(w) / static_cast<float>(WHEEL_DELTA);
        if (m == WM_MOUSEWHEEL) e.wheel_dy = d;
        else e.wheel_dx = d;
        e.mods = current_mods();
        emit(e);
        return 0;
      }
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP: {
        InputEvent e;
        e.type = (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) ? InputEvent::KeyDown : InputEvent::KeyUp;
        e.key = map_vk(w);
        e.mods = current_mods();
        e.repeat = (l & (1 << 30)) != 0;
        emit(e);
        if (m == WM_SYSKEYDOWN && w == VK_F4) break; // keep Alt+F4
        if (m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) return 0;
        return 0;
      }
      case WM_CHAR: {
        const wchar_t c = static_cast<wchar_t>(w);
        if (c >= 0xD800 && c <= 0xDBFF) {
          high_surrogate_ = c;
          return 0;
        }
        std::wstring ws;
        if (c >= 0xDC00 && c <= 0xDFFF && high_surrogate_) {
          ws = {high_surrogate_, c};
          high_surrogate_ = 0;
        } else {
          if (c < 0x20 || c == 0x7F) return 0;
          ws = {c};
        }
        InputEvent e;
        e.type = InputEvent::Text;
        e.text = narrow(ws);
        e.mods = current_mods();
        emit(e);
        return 0;
      }
      case WM_CLOSE:
        if (!owner_ || owner_->handle_close_request()) {
          release_modal();
          DestroyWindow(hwnd_);
        }
        return 0;
      case WM_DESTROY:
        unregister_drop(hwnd_, drop_);
        drop_ = nullptr;
        if (uia_) uia_destroy(uia_, hwnd_);
        uia_ = nullptr;
        break;
      case WM_GETOBJECT:
        // UI Automation asks for the window's element tree
        if (static_cast<long>(l) == -25 /*UiaRootObjectId*/ && owner_) {
          if (!uia_) uia_ = uia_create(hwnd_, owner_);
          return uia_get_object(uia_, hwnd_, w, l);
        }
        break;
      case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        rt_.reset();
        if (owner_) owner_->handle_destroyed();
        return 0;
      }
      default:
        break;
    }
    return DefWindowProcW(hwnd_, m, w, l);
  }

  Window* owner_;
  HWND hwnd_ = nullptr;
  HWND modal_parent_ = nullptr;
  void* drop_ = nullptr; // OLE drop target
  void* uia_ = nullptr;  // UI Automation providers
  DWORD style_ = WS_OVERLAPPEDWINDOW;
  Com<ID2D1HwndRenderTarget> rt_;
  BitmapCache cache_;
  Cursor cursor_ = Cursor::Arrow;
  Size min_{200, 120};
  bool tracking_ = false;
  wchar_t high_surrogate_ = 0;
};

} // namespace

std::unique_ptr<NativeWindow> create_window(Window* owner, const WindowConfig& cfg) {
  return std::make_unique<Win32Window>(owner, cfg);
}

} // namespace pulseui::platform
