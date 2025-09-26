#pragma once
#include <functional>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {

class ClickSurface {
public:
  ClickSurface() = default;
  explicit ClickSurface(std::function<void()> cb) : cb_(std::move(cb)) {}

  void on_click(std::function<void()> cb) { cb_ = std::move(cb); }

  void paint(Canvas&) {}
  bool handle_mouse_move(Point) { return false; }
  bool handle_mouse_down(Point, MouseButton b) {
    if (b == MouseButton::Left && cb_) {
      cb_();
      return true;
    }
    return false;
  }
  bool handle_mouse_up(Point, MouseButton) { return false; }

private:
  std::function<void()> cb_;
};

} // namespace pulseui::ui
