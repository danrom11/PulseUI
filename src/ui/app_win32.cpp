#include <windows.h>

namespace pulseui::platform {

void app_init() {

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

} // namespace pulseui::platform
