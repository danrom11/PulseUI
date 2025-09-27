#pragma once
#include <string>
#include <functional>
#include <vector>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/widget.hpp>

namespace pulseui::ui {

inline Color mul(const Color& c, float k) { return { c.r*k, c.g*k, c.b*k, c.a }; }

struct ButtonStyle {
  Color bg_normal {0.20f, 0.22f, 0.26f, 1.0f};
  Color bg_hover  {0.24f, 0.28f, 0.34f, 1.0f};
  Color bg_down   {0.16f, 0.18f, 0.22f, 1.0f};
  Color fg        {1,1,1,1};
  Font  font{16.f};
  float padding_px = 12.f;
};

class Button : public Widget {
public:
  Button(Rect rect, std::string text)
    : text_(std::move(text)) { rect_ = rect; }

  Button& set_text(std::string t)  { text_ = std::move(t); return *this; }
  Button& set_style(ButtonStyle s) { style_ = std::move(s); return *this; }

  void on_click(std::function<void()> fn) { click_handlers_.push_back(std::move(fn)); }

  void paint(Canvas& g) override {
    const Color bg = pressed_ ? style_.bg_down : (hovered_ ? style_.bg_hover : style_.bg_normal);
    g.fill_rect(rect_, bg);

    if (pressed_) {
      g.fill_rect(Rect{rect_.x, rect_.y, rect_.w, 1},              mul(bg, 0.8f));
      g.fill_rect(Rect{rect_.x, rect_.y + rect_.h - 1, rect_.w, 1}, mul(bg, 1.2f));
    }

    const float baseline = rect_.y + rect_.h*0.5f - style_.font.size*0.35f;
    g.draw_text(Point{rect_.x + style_.padding_px, baseline}, text_, style_.font, style_.fg);
  }

  bool handle_mouse_move(Point p) override {
    bool new_hover = contains(p);
    if (new_hover != hovered_) { hovered_ = new_hover; return true; }
    return false;
  }

  bool handle_mouse_down(Point p, MouseButton b) override {
    if (b != MouseButton::Left) return false;
    bool new_pressed = contains(p);
    if (new_pressed != pressed_) { pressed_ = new_pressed; return true; }
    return false;
  }

  bool handle_mouse_up(Point p, MouseButton b) override {
    if (b != MouseButton::Left) return false;
    bool was_pressed = pressed_;
    bool need_redraw = false;
    if (pressed_) { pressed_ = false; need_redraw = true; }
    if (was_pressed && contains(p)) {
      for (auto &fn : click_handlers_) if (fn) fn();
      need_redraw = true;
    }
    return need_redraw;
  }

private:
  bool contains(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

  std::string text_;
  ButtonStyle style_{};

  bool hovered_{false}, pressed_{false};
  std::vector<std::function<void()>> click_handlers_;
};

} // namespace pulseui::ui
