#import <AppKit/AppKit.h>
#include <pulseui/platform/platform.hpp>

namespace pulseui::platform {
  std::unique_ptr<core::executor> make_cocoa_executor();
  std::unique_ptr<ui::Window>     make_cocoa_window(int width, int height, const std::string& title);

  std::unique_ptr<pulseui::core::executor> make_ui_executor() { return make_cocoa_executor(); }
  std::unique_ptr<pulseui::ui::Window> make_window(int w, int h, const std::string& title) { return make_cocoa_window(w,h,title); }
}
