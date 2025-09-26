#include <windows.h>
#include <windowsx.h>
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <utility>

#include <pulseui/ui/window.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>
#include <pulseui/platform/platform.hpp>
#include "canvas_gdi.hpp"

namespace pulseui::platform {

static std::wstring widen_utf8(std::string_view s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out(len, L'\0');
  if (len) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

class Win32Window final : public ui::Window {
public:
  Win32Window(int width, int height, std::string_view title_utf8) {
    register_class();
    create_window(width, height, widen_utf8(title_utf8));
  }

  ~Win32Window() override {
    if (hwnd_) DestroyWindow(hwnd_);
  }

  // === ui::Window overrides ===
  void set_title(std::string title_utf8) override {
    std::wstring w = widen_utf8(title_utf8);
    SetWindowTextW(hwnd_, w.c_str());
  }

  void invalidate() override {
    if (!hwnd_) return;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  float dpi_scale() const override {
    if (hwnd_) {
      HMODULE u32 = GetModuleHandleW(L"user32.dll");
      using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
      auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
          GetProcAddress(u32, "GetDpiForWindow"));
      if (pGetDpiForWindow) {
        UINT dpi = pGetDpiForWindow(hwnd_);
        if (dpi == 0) dpi = 96;
        return dpi / 96.0f;
      }
      HDC hdc = GetDC(hwnd_);
      int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
      ReleaseDC(hwnd_, hdc);
      return dpiX / 96.0f;
    } else {
      HDC screen = GetDC(nullptr);
      int dpiX = (screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96);
      if (screen) ReleaseDC(nullptr, screen);
      return dpiX / 96.0f;
    }
  }

  void on_paint(PaintCB cb) override {
    paint_cb_ = std::move(cb);
    invalidate();
  }

  void on_input(InputCB cb) override {
    input_cb_ = std::move(cb);
  }

  void set_size(int width, int height) {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  void show() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetFocus(hwnd_);
  }

private:
  static const wchar_t* kClassName() { return L"PulseUIWindow"; }

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Win32Window* self = nullptr;
    if (msg == WM_NCCREATE) {
      auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      self = static_cast<Win32Window*>(cs->lpCreateParams);
      SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hWnd;
    } else {
      self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    switch (msg) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (self && self->paint_cb_) {
          pulseui::platform::GdiCanvas gdi(hdc);
          ui::Canvas& canvas = gdi;
          self->paint_cb_(canvas);
        }
        EndPaint(hWnd, &ps);
        return 0;
      }

      case WM_LBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_RBUTTONDOWN:
      case WM_RBUTTONUP:
      case WM_MBUTTONDOWN:
      case WM_MBUTTONUP:
      case WM_MOUSEMOVE:
      case WM_MOUSEWHEEL:
      case WM_KEYDOWN:
      case WM_KEYUP:
      case WM_CHAR: {
        if (self && self->input_cb_) {
          ui::InputEvent ev{};
          // координаты клиента для мыши
          POINT pt{};
          if (msg == WM_MOUSEWHEEL) {
            POINT scr{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &scr);
            pt = scr;
          } else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
                     msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP ||
                     msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP ||
                     msg == WM_MOUSEMOVE) {
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
          }
          ev.pos.x = static_cast<float>(pt.x);
          ev.pos.y = static_cast<float>(pt.y);

          switch (msg) {
            case WM_LBUTTONDOWN:
              SetFocus(hWnd);
              SetCapture(hWnd);
              ev.type = ui::InputEvent::MouseDown; ev.keycode = VK_LBUTTON; break;
            case WM_LBUTTONUP:
              ReleaseCapture();
              ev.type = ui::InputEvent::MouseUp;   ev.keycode = VK_LBUTTON; break;

            case WM_RBUTTONDOWN: ev.type = ui::InputEvent::MouseDown; ev.keycode = VK_RBUTTON; break;
            case WM_RBUTTONUP:   ev.type = ui::InputEvent::MouseUp;   ev.keycode = VK_RBUTTON; break;
            case WM_MBUTTONDOWN: ev.type = ui::InputEvent::MouseDown; ev.keycode = VK_MBUTTON; break;
            case WM_MBUTTONUP:   ev.type = ui::InputEvent::MouseUp;   ev.keycode = VK_MBUTTON; break;
            case WM_MOUSEMOVE:   ev.type = ui::InputEvent::MouseMove; ev.keycode = 0;          break;

            case WM_MOUSEWHEEL: {
              ev.type = ui::InputEvent::Scroll;
              const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
              ev.scrollY = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
              ev.keycode = 0;
              break;
            }

            case WM_KEYDOWN:
              if (wParam == VK_BACK) return 0;
              ev.type = ui::InputEvent::KeyDown;
              ev.keycode = static_cast<int>(wParam); // VK_*
              break;

            case WM_KEYUP:
              if (wParam == VK_BACK) return 0;
              ev.type = ui::InputEvent::KeyUp;
              ev.keycode = static_cast<int>(wParam);
              break;

            case WM_CHAR: {
              wchar_t wc = static_cast<wchar_t>(wParam);
              if (wc == 8) {
                ev.type = ui::InputEvent::KeyDown;
                ev.keycode = 8; // VK_BACK
                ev.text.clear();
              } else if (wc == L'\r' || wc == L'\n') {
                return 0;
              } else {
                char utf8[4];
                int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, 4, nullptr, nullptr);
                if (len > 0) ev.text.assign(utf8, utf8 + len);
                ev.type = ui::InputEvent::KeyDown;
                ev.keycode = 0;
              }
              break;
            }
          }

          self->input_cb_(ev);
        }
        return 0;
      }

      case WM_DPICHANGED: {
        // RECT* suggested = reinterpret_cast<RECT*>(lParam);
        // SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
        //              suggested->right - suggested->left,
        //              suggested->bottom - suggested->top,
        //              SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }

      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

      default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
  }

  static void register_class() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &Win32Window::WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = nullptr;
    wc.hIconSm       = nullptr;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = kClassName();

    RegisterClassExW(&wc);
    registered = true;
  }

  void create_window(int width, int height, const std::wstring& title) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    RECT rc{0,0,width,height};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    hwnd_ = CreateWindowExW(
      0, kClassName(), title.c_str(), WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, w, h,
      nullptr, nullptr, hInst, this
    );

    SetWindowTextW(hwnd_, title.c_str());
  }

private:
  HWND hwnd_{nullptr};
  ui::Window::PaintCB paint_cb_{};
  ui::Window::InputCB input_cb_{};
};

std::unique_ptr<ui::Window> make_win32_window(int width, int height, const std::string& title_utf8) {
  auto* raw = new Win32Window(width, height, std::string_view(title_utf8));
  raw->show();
  return std::unique_ptr<ui::Window>(raw);
}

} // namespace pulseui::platform
