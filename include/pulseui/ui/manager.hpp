#pragma once
#include <memory>
#include <vector>
#include <type_traits>
#include <pulseui/ui/window.hpp>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::ui {

struct IWidget {
  virtual ~IWidget() = default;
  virtual void paint(Canvas& g) = 0;
  virtual bool mouse_move(Point p) = 0;
  virtual bool mouse_down(Point p, MouseButton b) = 0;
  virtual bool mouse_up(Point p, MouseButton b) = 0;
  virtual bool key_down(int keycode, const std::string& text) { (void)keycode; (void)text; return false; }
  virtual bool key_up(int keycode) { (void)keycode; return false; }
};

template<class W>
class WidgetHolder final : public IWidget {
public:
  template<class...Args>
  explicit WidgetHolder(Args&&...args) : obj(std::forward<Args>(args)...) {}

  void paint(Canvas& g) override { obj.paint(g); }
  bool mouse_move(Point p) override { return obj.handle_mouse_move(p); }
  bool mouse_down(Point p, MouseButton b) override { return obj.handle_mouse_down(p, b); }
  bool mouse_up(Point p, MouseButton b) override { return obj.handle_mouse_up(p, b); }

  bool key_down(int keycode, const std::string& text) override {
    if constexpr (requires(W w){ w.handle_keydown(0, std::string{}); })
      return obj.handle_keydown(keycode, text);
    return false;
  }

  bool key_up(int keycode) override {
    if constexpr (requires(W w){ w.handle_keyup(0); })
      return obj.handle_keyup(keycode);
    return false;
  }

  W obj;
};

class Manager {
public:
  explicit Manager(Window& win) : win_(win) {
    win_.on_paint([this](Canvas& g){
      if (bg_paint_) bg_paint_(g);
      for (auto& w : widgets_) w->paint(g);
    });

    win_.on_input([this](const InputEvent& e){
      bool dirty = false;
      switch (e.type) {
        case InputEvent::MouseMove:
          for (auto& w : widgets_) dirty |= w->mouse_move(e.pos);
          break;
        case InputEvent::MouseDown:
          for (auto& w : widgets_) dirty |= w->mouse_down(e.pos, MouseButton::Left);
          break;
        case InputEvent::MouseUp:
          for (auto& w : widgets_) dirty |= w->mouse_up(e.pos, MouseButton::Left);
          break;
        case InputEvent::KeyDown:
          for (auto& w : widgets_) dirty |= w->key_down(e.keycode, e.text);
          break;
        case InputEvent::KeyUp:
          for (auto& w : widgets_) dirty |= w->key_up(e.keycode);
          break;
        default: break;
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
    auto holder = std::make_unique<WidgetHolder<W>>(std::forward<Args>(args)...);
    W& ref = holder->obj;
    widgets_.push_back(std::move(holder));
    win_.invalidate();
    return ref;
  }

  Window& window() const { return win_; }

private:
  Window& win_;
  Window::PaintCB bg_paint_{};
  std::vector<std::unique_ptr<IWidget>> widgets_;
};

} // namespace pulseui::ui
