#include <windows.h>
#include <pulseui/platform/platform.hpp>

namespace pulseui::platform {

void app_init() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void app_run() {
  MSG msg{};
  while (true) {
    BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r == -1) {
      break;
    }
    if (r == 0) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void app_quit(int exit_code) {
  PostQuitMessage(exit_code);
}

} // namespace pulseui::platform
