#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::ui {

struct TextFieldStyle {
  Color bg        {0.15f, 0.16f, 0.18f, 1.0f};
  Color bg_focus  {0.17f, 0.18f, 0.22f, 1.0f};
  Color border    {0.35f, 0.40f, 0.50f, 1.0f};
  Color border_f  {0.25f, 0.55f, 0.90f, 1.0f};
  Color fg        {1,1,1,1};
  Color placeholder_fg {0.75f,0.75f,0.78f,1};
  Font  font{16.f};
  float padding = 8.f;
};

// -------- UTF-8 helpers (codepoint-based) --------
inline int utf8_length(const std::string& s) {
  int len = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    size_t step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    else step = 1;
    i += step; ++len;
  }
  return len;
}

inline int utf8_byte_offset(const std::string& s, int codepoint_index) {
  int cp = 0;
  int byte = 0;
  while (byte < (int)s.size() && cp < codepoint_index) {
    unsigned char c = (unsigned char)s[byte];
    int step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    else step = 1;
    byte += step; ++cp;
  }
  return byte;
}

inline int utf8_count_codepoints(std::string_view sv) {
  int len = 0;
  for (size_t i = 0; i < sv.size();) {
    unsigned char c = (unsigned char)sv[i];
    size_t step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    else step = 1;
    i += step; ++len;
  }
  return len;
}

inline int utf8_prev_cp_byte(const std::string& s, int byte_pos) {
  byte_pos = std::clamp(byte_pos, 0, (int)s.size());
  if (byte_pos == 0) return 0;
  int i = byte_pos - 1;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
  return i;
}

// ---------------- TextField ----------------
class TextField {
public:
  TextField() = default;
  TextField(Rect r, std::string text = {}, std::string placeholder = {})
    : rect_(r), text_(std::move(text)), placeholder_(std::move(placeholder)) {
    caret_cp_ = utf8_length(text_);
    invalidate_metrics_ = true;
  }

  TextField& set_rect(Rect r)                { rect_ = r; return *this; }
  TextField& set_text(std::string s)         { text_ = std::move(s); caret_cp_ = utf8_length(text_); invalidate_metrics_ = true; return *this; }
  TextField& set_placeholder(std::string s)  { placeholder_ = std::move(s); return *this; }
  TextField& set_style(TextFieldStyle s)     { style_ = std::move(s); invalidate_metrics_ = true; return *this; }

  const std::string& text() const { return text_; }
  const Rect& rect() const        { return rect_; }
  bool focused() const            { return focused_; }

  void paint(Canvas& g) {
    const bool show_placeholder = text_.empty() && !placeholder_.empty() && !focused_;
    const Color bg = focused_ ? style_.bg_focus : style_.bg;
    const Color br = focused_ ? style_.border_f : style_.border;

    // фон и рамка
    g.fill_rect(rect_, bg);
    g.fill_rect(Rect{rect_.x, rect_.y, rect_.w, 1}, br);
    g.fill_rect(Rect{rect_.x, rect_.y + rect_.h - 1, rect_.w, 1}, br);
    g.fill_rect(Rect{rect_.x, rect_.y, 1, rect_.h}, br);
    g.fill_rect(Rect{rect_.x + rect_.w - 1, rect_.y, 1, rect_.h}, br);

    const float x0 = rect_.x + style_.padding;
    const float baseline = rect_.y + rect_.h * 0.5f - style_.font.size * 0.35f;

    if (show_placeholder) {
      g.draw_text({x0, baseline}, placeholder_, style_.font, style_.placeholder_fg);
      prefix_x_.assign(1, 0.f);
      invalidate_metrics_ = false;
      return;
    }

    g.draw_text({x0, baseline}, text_, style_.font, style_.fg);

    if (invalidate_metrics_) {
      const int cp_len = utf8_length(text_);
      prefix_x_.resize(cp_len + 1);
      for (int cp = 0; cp <= cp_len; ++cp) {
        const int byte_off = utf8_byte_offset(text_, cp);
        prefix_x_[cp] = g.text_width(std::string_view(text_.data(), (size_t)byte_off), style_.font);
      }
      invalidate_metrics_ = false;
    }

    if (focused_) {
      const int cp_len = (int)prefix_x_.empty() ? 0 : ((int)prefix_x_.size() - 1);
      const int cp = std::clamp(caret_cp_, 0, cp_len);
      const float cx = x0 + prefix_x_[cp];
      g.fill_rect(Rect{cx, rect_.y + 4, 1, rect_.h - 8}, style_.fg);
    }
  }

  bool handle_mouse_move(Point) { return false; }

  bool handle_mouse_down(Point p, MouseButton b) {
    if (b != MouseButton::Left) return false;
    bool changed = false;
    const bool inside = contains(p);
    if (inside) {
      if (!focused_) { focused_ = true; changed = true; }
      const float x0 = rect_.x + style_.padding;
      float target_x = p.x - x0;
      if (target_x < 0) target_x = 0;

      if (prefix_x_.empty()) {
        caret_cp_ = utf8_length(text_);
        return true;
      }

      int best = 0;
      float best_d = 1e9f;
      for (int cp = 0; cp < (int)prefix_x_.size(); ++cp) {
        float d = std::abs(prefix_x_[cp] - target_x);
        if (d < best_d) { best_d = d; best = cp; }
      }
      if (best != caret_cp_) { caret_cp_ = best; changed = true; }
    } else {
      if (focused_) { focused_ = false; changed = true; }
    }
    return changed;
  }

  bool handle_mouse_up(Point, MouseButton) { return false; }

  bool handle_keydown(int keycode, const std::string& text_utf8) {
    bool changed = false;
    if (text_utf8 == "\b" || keycode == 8) {
      if (focused_ && caret_cp_ > 0) {
        const int byte_end   = utf8_byte_offset(text_, caret_cp_);
        const int byte_start = utf8_prev_cp_byte(text_, byte_end);
        text_.erase(byte_start, byte_end - byte_start);
        caret_cp_ -= 1;
        invalidate_metrics_ = true;
        return true;
      }
      return false;
    }

    if (text_utf8 == "\n" || text_utf8 == "\r")
      return false;

    if (focused_ && keycode == 0 && !text_utf8.empty()) {
      const int ins_cp   = caret_cp_;
      const int ins_byte = utf8_byte_offset(text_, ins_cp);
      text_.insert((size_t)ins_byte, text_utf8);
      caret_cp_ += utf8_count_codepoints(text_utf8);
      invalidate_metrics_ = true;
      return true;
    }
    return changed;
  }


  bool handle_keyup(int /*keycode*/) { return false; }

private:
  bool contains(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

private:
  Rect rect_{};
  std::string text_;
  std::string placeholder_;
  TextFieldStyle style_{};

  bool focused_{false};
  int  caret_cp_{0};
  bool invalidate_metrics_{true};
  std::vector<float> prefix_x_;
};

} // namespace pulseui::ui
