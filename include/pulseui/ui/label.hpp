#pragma once
#include <string>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::ui {

enum class AlignH { Left, Center, Right };
enum class AlignV { Top, Center, Bottom };

struct LabelStyle {
  Color fg {1,1,1,1};
  Color bg {0,0,0,0};
  bool  opaque = false;

  Font  font{16.f};
  float padding_px = 8.f;

  AlignH align_h = AlignH::Left;
  AlignV align_v = AlignV::Center;
};

class Label {
public:
  Label() = default;
  Label(Rect rect, std::string text)
    : rect_(rect), text_(std::move(text)) {}

  Label& set_rect(Rect r)          { rect_ = r; return *this; }
  Label& set_text(std::string txt) { text_ = std::move(txt); return *this; }
  Label& set_style(LabelStyle s)   { style_ = std::move(s);   return *this; }

  const Rect&       rect()  const { return rect_; }
  const std::string& text() const { return text_; }
  const LabelStyle&  style() const{ return style_; }

  void paint(Canvas& g) {
    if (style_.opaque) {
      g.fill_rect(rect_, style_.bg);
    }

    const float inner_x = rect_.x + style_.padding_px;
    const float inner_y = rect_.y + style_.padding_px;
    const float inner_w = rect_.w - style_.padding_px * 2.f;
    const float inner_h = rect_.h - style_.padding_px * 2.f;

    float text_baseline_y = inner_y;

    const float ascent_est = style_.font.size * 0.8f;
    const float descent_est = style_.font.size * 0.2f;
    switch (style_.align_v) {
      case AlignV::Top:
        text_baseline_y = inner_y + ascent_est;
        break;
      case AlignV::Center:
        text_baseline_y = inner_y + inner_h * 0.5f + (ascent_est - descent_est) * 0.5f;
        break;
      case AlignV::Bottom:
        text_baseline_y = inner_y + inner_h - descent_est;
        break;
    }

    float text_x = inner_x;
    if (style_.align_h == AlignH::Center) {
      text_x = inner_x + inner_w * 0.5f;
      text_x -= style_.padding_px;
    } else if (style_.align_h == AlignH::Right) {
      text_x = inner_x + inner_w - style_.padding_px;
    }

    g.draw_text({text_x, text_baseline_y}, text_, style_.font, style_.fg);
  }

  bool handle_mouse_move(Point)                   { return false; }
  bool handle_mouse_down(Point, MouseButton)      { return false; }
  bool handle_mouse_up(Point, MouseButton)        { return false; }

private:
  Rect        rect_{};
  std::string text_;
  LabelStyle  style_{};
};

} // namespace pulseui::ui
