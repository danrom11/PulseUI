#pragma once
#include <string>
#include <vector>
#include <functional>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/widget.hpp>

namespace pulseui::ui {

inline void draw_rect_border(Canvas& g, Rect r, float w, Color c) {
  g.fill_rect({r.x, r.y, r.w, w}, c);                     // top
  g.fill_rect({r.x, r.y + r.h - w, r.w, w}, c);           // bottom
  g.fill_rect({r.x, r.y, w, r.h}, c);                     // left
  g.fill_rect({r.x + r.w - w, r.y, w, r.h}, c);           // right
}

struct CheckboxStyle {
  Color bg_normal   {0.12f, 0.14f, 0.16f, 1.0f};
  Color bg_hover    {0.16f, 0.18f, 0.22f, 1.0f};
  Color bg_down     {0.10f, 0.12f, 0.14f, 1.0f};
  Color border      {0.70f, 0.75f, 0.80f, 1.0f};
  Color check_fill  {0.20f, 0.75f, 0.35f, 1.0f};
  Color text        {1,1,1,1};
  Font  font        {16.f};

  float box_size = 18.f;  // size of the square
  float gap      = 10.f;  // distance from the square to the text
  float border_w = 1.0f;  // frame thickness
  float pad      = 3.0f;  // indent for the check mark (inner square)
};

class Checkbox : public Widget {
public:
  Checkbox(Rect r, std::string text, bool checked = false)
    : text_(std::move(text)), checked_(checked) { rect_ = r; }

  void set_text(std::string t) { text_ = std::move(t); }
  const std::string& text() const { return text_; }

  void set_checked(bool v) {
    if (checked_ == v) return;
    checked_ = v;
    for (auto& cb : change_handlers_) cb(checked_);
  }
  bool checked() const { return checked_; }

  void set_style(const CheckboxStyle& s) { style_ = s; }
  const CheckboxStyle& style() const { return style_; }

  void on_change(std::function<void(bool)> cb) { change_handlers_.push_back(std::move(cb)); }

  // ---- Widget ----
  void paint(Canvas& g) override {
    const float x = rect_.x, y = rect_.y, w = rect_.w, h = rect_.h;

    const float bx = x;
    const float by = y + (h - style_.box_size) * 0.5f;
    Rect box{bx, by, style_.box_size, style_.box_size};

    Color bg = pressed_ ? style_.bg_down : (hovered_ ? style_.bg_hover : style_.bg_normal);
    g.fill_rect(box, bg);
    draw_rect_border(g, box, style_.border_w, style_.border);

    if (checked_) {
      const float p = style_.pad;
      g.fill_rect({box.x + p, box.y + p, box.w - 2*p, box.h - 2*p}, style_.check_fill);
    }

    const float text_x = bx + style_.box_size + style_.gap;
    const float baseline = y + (h * 0.1f) + style_.font.size * 0.35f;
    g.draw_text({text_x, baseline}, text_, style_.font, style_.text);
  }

  bool handle_mouse_move(Point p) override {
    bool hov = contains_(p);
    if (hov != hovered_) { hovered_ = hov; return true; }
    return false;
  }

  bool handle_mouse_down(Point p, MouseButton btn) override {
    if (btn != MouseButton::Left) return false;
    if (!contains_(p)) return false;
    pressed_ = true;
    return true;
  }

  bool handle_mouse_up(Point p, MouseButton btn) override {
    if (btn != MouseButton::Left) return false;
    bool was = pressed_;
    pressed_ = false;
    if (was && contains_(p)) {
      checked_ = !checked_;
      for (auto& cb : change_handlers_) cb(checked_);
    }
    return true;
  }

private:
  bool contains_(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

  std::string text_;
  CheckboxStyle style_{};
  bool checked_{false};
  bool hovered_{false}, pressed_{false};
  std::vector<std::function<void(bool)>> change_handlers_;
};

} // namespace pulseui::ui
