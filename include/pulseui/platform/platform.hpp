#pragma once
#include <memory>
#include <string>
#include <pulseui/core/executor.hpp>
#include <pulseui/ui/window.hpp>

namespace pulseui::platform {
  // Cross-platform factories (route to current platform at link-time)
  std::unique_ptr<pulseui::core::executor> make_ui_executor();
  std::unique_ptr<pulseui::ui::Window> make_window(int width, int height, const std::string& title);

  void app_init();
  void app_run();
}

