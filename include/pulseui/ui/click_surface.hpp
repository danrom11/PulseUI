#pragma once
#include <functional>
#include <pulseui/ui/widget.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {

class ClickSurface : public Widget {
public:
  ClickSurface() = default;
  explicit ClickSurface(std::function<void()> cb) : cb_(std::move(cb)) {}

  void on_click(std::function<void()> cb) { cb_ = std::move(cb); }

  void paint(Canvas&) override {}

  bool handle_mouse_move(Point) override { return false; }

  bool handle_mouse_down(Point p, MouseButton b) override {
  if (b == MouseButton::Left && (rect_.w == 0 || rect_.h == 0 || contains(p))) {
    pressed_ = true;
    return true;
  }
  return false;
}

bool handle_mouse_up(Point p, MouseButton b) override {
  if (b == MouseButton::Left && pressed_ && (rect_.w == 0 || rect_.h == 0 || contains(p))) {
    pressed_ = false;
    if (cb_) cb_();
    return true;
  }
  pressed_ = false;
  return false;
}

private:
  bool contains(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

  std::function<void()> cb_;
  bool pressed_{false};
};

} // namespace pulseui::ui
