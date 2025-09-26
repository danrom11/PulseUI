#pragma once
#include <string>
#include <functional>
#include <vector>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>

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

class Button {
public:
  Button(Rect rect, std::string text)
    : rect_(rect), text_(std::move(text)) {}

  Button& set_rect(Rect r)         { rect_ = r; return *this; }
  Button& set_text(std::string t)  { text_ = std::move(t); return *this; }
  Button& set_style(ButtonStyle s) { style_ = std::move(s); return *this; }

  void on_click(std::function<void()> fn) { click_handlers_.push_back(std::move(fn)); }

  void handle_mouse_move(Point p) {
    hovered_ = contains(p);
    if (hovered_ != last_hovered_) last_hovered_ = hovered_;
  }

  void handle_mouse_down(Point p, MouseButton b) {
    if (b == MouseButton::Left) {
      pressed_ = contains(p);
    }
  }

  void handle_mouse_up(Point p, MouseButton b) {
    if (b == MouseButton::Left) {
      const bool was_pressed = pressed_;
      pressed_ = false;
      if (was_pressed && contains(p)) {
        for (auto &fn : click_handlers_) if (fn) fn();
      }
    }
  }

  void paint(Canvas& g) {
    const Color bg = pressed_ ? style_.bg_down : (hovered_ ? style_.bg_hover : style_.bg_normal);
    g.fill_rect(rect_, bg);

    if (pressed_) {
      g.fill_rect(Rect{rect_.x, rect_.y, rect_.w, 1},              mul(bg, 0.8f));
      g.fill_rect(Rect{rect_.x, rect_.y + rect_.h - 1, rect_.w, 1}, mul(bg, 1.2f));
    }

    const float baseline = rect_.y + rect_.h * 0.5f - style_.font.size * 0.35f;
    g.draw_text(Point{rect_.x + style_.padding_px, baseline}, text_, style_.font, style_.fg);
  }

private:
  bool contains(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

private:
  Rect    rect_{};
  std::string text_;
  ButtonStyle style_{};

  bool hovered_{false}, last_hovered_{false}, pressed_{false};
  std::vector<std::function<void()>> click_handlers_;
};

} // namespace pulseui::ui
