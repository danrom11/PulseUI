#pragma once
#include <pulseui/core/subscriptions.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {

struct Widget {
  virtual ~Widget() = default;

  virtual void paint(Canvas& g) = 0;
  virtual bool handle_mouse_move(Point) { return false; }
  virtual bool handle_mouse_down(Point, MouseButton) { return false; }
  virtual bool handle_mouse_up(Point, MouseButton) { return false; }
  virtual bool handle_keydown(int, const std::string&) { return false; }
  virtual bool handle_keyup(int) { return false; }

  void set_rect(Rect r) { rect_ = r; }
  const Rect& rect() const { return rect_; }

protected:
  Rect rect_{};
  pulseui::core::subs_bag subs_;
};

} // namespace pulseui::ui
