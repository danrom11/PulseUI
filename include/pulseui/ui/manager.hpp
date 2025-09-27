#pragma once
#include <memory>
#include <vector>
#include <pulseui/ui/window.hpp>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/widget.hpp>

namespace pulseui::ui {

class Manager {
public:
  explicit Manager(Window& win) : win_(win) {
    win_.on_paint([this](Canvas& g){
      if (bg_paint_) bg_paint_(g);
      for (auto& w : widgets_) w->paint(g);
    });

    win_.on_input([this](const InputEvent& e){
      bool dirty = false;
      for (auto& w : widgets_) {
        switch (e.type) {
          case InputEvent::MouseMove: dirty |= w->handle_mouse_move(e.pos); break;
          case InputEvent::MouseDown: dirty |= w->handle_mouse_down(e.pos, MouseButton::Left); break;
          case InputEvent::MouseUp:   dirty |= w->handle_mouse_up(e.pos, MouseButton::Left); break;
          case InputEvent::KeyDown:   dirty |= w->handle_keydown(e.keycode, e.text); break;
          case InputEvent::KeyUp:     dirty |= w->handle_keyup(e.keycode); break;
          default: break;
        }
      }
      if (dirty) win_.invalidate();
    });
  }

  void set_background(Window::PaintCB bg) {
    bg_paint_ = std::move(bg);
    win_.invalidate();
  }

  template<class W, class...Args>
  W& add(Args&&...args) {
    static_assert(std::is_base_of_v<Widget, W>, "W must inherit from Widget");
    auto ptr = std::make_unique<W>(std::forward<Args>(args)...);
    W& ref = *ptr;
    widgets_.push_back(std::move(ptr));
    win_.invalidate();
    return ref;
  }

  Window& window() const { return win_; }

private:
  Window& win_;
  Window::PaintCB bg_paint_{};
  std::vector<std::unique_ptr<Widget>> widgets_;
};

} // namespace pulseui::ui
