#include <windows.h>
#include <functional>
#include <queue>
#include <mutex>
#include <memory>

#include <pulseui/core/executor.hpp>

namespace pulseui::platform {

class UiExecutor final : public core::executor {
public:
  using Fn = std::function<void()>;

  UiExecutor() { create_message_window(); }
  ~UiExecutor() override { if (hwnd_) DestroyWindow(hwnd_); }

  // === core::executor override ===
  void post(std::function<void()> fn) override {
    {
      std::lock_guard<std::mutex> lock(mx_);
      queue_.push(std::move(fn));
    }
    PostMessageW(hwnd_, WM_APP_EXECUTE, 0, 0);
  }

  HWND hwnd() const { return hwnd_; }

private:
  static const wchar_t* kClassName() { return L"PulseUIExecWindow"; }
  static constexpr UINT WM_APP_EXECUTE = WM_APP + 1;

  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiExecutor* self = nullptr;
    if (msg == WM_NCCREATE) {
      auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      self = static_cast<UiExecutor*>(cs->lpCreateParams);
      SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
      self = reinterpret_cast<UiExecutor*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (msg == WM_APP_EXECUTE && self) {
      std::queue<Fn> local;
      {
        std::lock_guard<std::mutex> lock(self->mx_);
        std::swap(local, self->queue_);
      }
      while (!local.empty()) {
        auto fn = std::move(local.front());
        local.pop();
        if (fn) fn();
      }
      return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
  }

  void create_message_window() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &UiExecutor::WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName();
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kClassName(), L"", WS_OVERLAPPEDWINDOW,
                            0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);
  }

private:
  HWND hwnd_{nullptr};
  std::mutex mx_;
  std::queue<Fn> queue_;
};

std::unique_ptr<core::executor> make_win32_executor() {
  return std::make_unique<UiExecutor>();
}

} // namespace pulseui::platform
