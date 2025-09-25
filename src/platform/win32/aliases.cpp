#include <pulseui/platform/platform.hpp>
#include <memory>
#include <string>

namespace pulseui::platform {

std::unique_ptr<core::executor> make_win32_executor();
std::unique_ptr<ui::Window>     make_win32_window(int width, int height, const std::string& title);

std::unique_ptr<core::executor> make_ui_executor() { return make_win32_executor(); }
std::unique_ptr<ui::Window>     make_window(int w, int h, const std::string& title) {
  return make_win32_window(w, h, title);
}

} // namespace pulseui::platform
