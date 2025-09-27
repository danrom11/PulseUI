#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <pulseui/ui/input.hpp>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/widget.hpp>

namespace pulseui::ui {

// ---------------- Style ----------------
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

// ---------------- UTF-8 helpers ----------------
inline int utf8_length(const std::string& s) {
  int len = 0;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char)s[i];
    size_t step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
    i += step; ++len;
  }
  return len;
}

inline int utf8_byte_offset(const std::string& s, int codepoint_index) {
  int cp = 0, byte = 0;
  while (byte < (int)s.size() && cp < codepoint_index) {
    unsigned char c = (unsigned char)s[byte];
    int step = 1;
    if      ((c & 0x80) == 0x00) step = 1;
    else if ((c & 0xE0) == 0xC0) step = 2;
    else if ((c & 0xF0) == 0xE0) step = 3;
    else if ((c & 0xF8) == 0xF0) step = 4;
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

inline int utf8_next_cp_byte(const std::string& s, int byte_pos) {
  byte_pos = std::clamp(byte_pos, 0, (int)s.size());
  if (byte_pos >= (int)s.size()) return (int)s.size();
  unsigned char c = (unsigned char)s[byte_pos];
  int step = 1;
  if      ((c & 0x80) == 0x00) step = 1;
  else if ((c & 0xE0) == 0xC0) step = 2;
  else if ((c & 0xF0) == 0xE0) step = 3;
  else if ((c & 0xF8) == 0xF0) step = 4;
  return std::min((int)s.size(), byte_pos + step);
}

// ---------------- TextField ----------------
class TextField : public Widget {
public:
  TextField() = default;
  TextField(Rect r, std::string text = {}, std::string placeholder = {})
    : text_(std::move(text)), placeholder_(std::move(placeholder)) {
    rect_ = r;
    caret_cp_ = utf8_length(text_);
    invalidate_metrics_ = true;
    need_scroll_adjust_ = true;
  }

  TextField& set_text(std::string s) {
    text_ = std::move(s);
    caret_cp_ = utf8_length(text_);
    invalidate_metrics_ = true;
    need_scroll_adjust_ = true;
    return *this;
  }
  TextField& set_placeholder(std::string s) { placeholder_ = std::move(s); return *this; }
  TextField& set_style(TextFieldStyle s) { style_ = std::move(s); invalidate_metrics_ = true; clamp_scroll_after_resize(); return *this; }

  const std::string& text() const { return text_; }
  bool focused() const { return focused_; }

  // ----- paint -----
  void paint(Canvas& g) override {
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
      scroll_px_ = 0.f;
      need_scroll_adjust_ = false;
      return;
    }

    if (invalidate_metrics_) recompute_metrics(g);
    if (need_scroll_adjust_) { ensure_caret_visible(); need_scroll_adjust_ = false; }

    // вычисляем видимую часть текста
    const float w = inner_width();
    int start_cp = 0, end_cp = (int)prefix_x_.size() - 1;
    if (!prefix_x_.empty()) {
      auto lb = std::lower_bound(prefix_x_.begin(), prefix_x_.end(), scroll_px_);
      start_cp = (int)std::distance(prefix_x_.begin(), lb);
      auto ub = std::upper_bound(prefix_x_.begin(), prefix_x_.end(), scroll_px_ + w);
      end_cp = (int)std::distance(prefix_x_.begin(), ub);
      end_cp = std::clamp(end_cp, start_cp, (int)prefix_x_.size() - 1);
    }

    const int start_byte = utf8_byte_offset(text_, start_cp);
    const int end_byte   = utf8_byte_offset(text_, end_cp);
    std::string_view vis_sv(text_.data() + start_byte, (size_t)(end_byte - start_byte));

    const float start_cp_x = prefix_x_.empty() ? 0.f : prefix_x_[start_cp];
    const float draw_x = x0 - (start_cp_x - scroll_px_);

    g.draw_text({draw_x, baseline}, vis_sv, style_.font, style_.fg);

    // caret
    if (focused_) {
      const int cp_len = prefix_x_.empty() ? 0 : ((int)prefix_x_.size() - 1);
      const int cp = std::clamp(caret_cp_, 0, cp_len);
      const float caret_x = x0 + (prefix_x_[cp] - scroll_px_);
      if (caret_x >= x0 && caret_x <= x0 + w) {
        g.fill_rect(Rect{caret_x, rect_.y + 4, 1, rect_.h - 8}, style_.fg);
      }
    }
  }

  // ----- input -----
  bool handle_mouse_move(Point) override { return false; }

  bool handle_mouse_down(Point p, MouseButton b) override {
    if (b != MouseButton::Left) return false;
    bool changed = false;
    if (contains(p)) {
      if (!focused_) { focused_ = true; changed = true; }
      const float x0 = rect_.x + style_.padding;
      float target_x = p.x - x0 + scroll_px_;
      if (target_x < 0) target_x = 0;
      if (prefix_x_.empty()) { caret_cp_ = utf8_length(text_); need_scroll_adjust_ = true; return true; }
      int best = 0; float best_d = 1e9f;
      for (int cp = 0; cp < (int)prefix_x_.size(); ++cp) {
        float d = std::abs(prefix_x_[cp] - target_x);
        if (d < best_d) { best_d = d; best = cp; }
      }
      if (best != caret_cp_) { caret_cp_ = best; changed = true; }
      ensure_caret_visible();
    } else {
      if (focused_) { focused_ = false; changed = true; }
    }
    return changed;
  }

  bool handle_mouse_up(Point, MouseButton) override { return false; }

  bool handle_keydown(int keycode, const std::string& text_utf8) override {
    const bool is_left   = (keycode == 37 || keycode == 263 || keycode == 123);
    const bool is_right  = (keycode == 39 || keycode == 262 || keycode == 124);
    const bool is_home   = (keycode == 36 || keycode == 268 || keycode == 115);
    const bool is_end    = (keycode == 35 || keycode == 269 || keycode == 119);
    const bool is_del    = (keycode == 46 || keycode == 261 || keycode == 117);
    const bool is_bsp    = (keycode == 8 /* DOM/Win */ || keycode == 259 /* GLFW */ || keycode == 8 /* macOS Backspace FIXME */);

    if (focused_ && is_left)  { if (caret_cp_ > 0) caret_cp_--; need_scroll_adjust_ = true; return true; }
    if (focused_ && is_right) { if (caret_cp_ < utf8_length(text_)) caret_cp_++; need_scroll_adjust_ = true; return true; }
    if (focused_ && is_home)  { caret_cp_ = 0; need_scroll_adjust_ = true; return true; }
    if (focused_ && is_end)   { caret_cp_ = utf8_length(text_); need_scroll_adjust_ = true; return true; }

    if (focused_ && is_del) {
      int cp_len = utf8_length(text_);
      if (caret_cp_ < cp_len) {
        int byte_start = utf8_byte_offset(text_, caret_cp_);
        int byte_end   = utf8_next_cp_byte(text_, byte_start);
        text_.erase(byte_start, byte_end - byte_start);
        invalidate_metrics_ = true; need_scroll_adjust_ = true;
        return true;
      }
      return false;
    }

    if (text_utf8 == "\b" || (focused_ && is_bsp)) {
      if (focused_ && caret_cp_ > 0) {
        int byte_end   = utf8_byte_offset(text_, caret_cp_);
        int byte_start = utf8_prev_cp_byte(text_, byte_end);
        text_.erase(byte_start, byte_end - byte_start);
        caret_cp_--;
        invalidate_metrics_ = true; need_scroll_adjust_ = true;
        return true;
      }
      return false;
    }

    if (text_utf8 == "\n" || text_utf8 == "\r") return false;

    if (focused_ && keycode == 0 && !text_utf8.empty()) {
      int ins_cp   = caret_cp_;
      int ins_byte = utf8_byte_offset(text_, ins_cp);
      text_.insert((size_t)ins_byte, text_utf8);
      caret_cp_ += utf8_count_codepoints(text_utf8);
      invalidate_metrics_ = true; need_scroll_adjust_ = true;
      return true;
    }

    return false;
  }

  bool handle_keyup(int) override { return false; }

private:
  bool contains(Point p) const {
    return p.x >= rect_.x && p.x <= rect_.x + rect_.w &&
           p.y >= rect_.y && p.y <= rect_.y + rect_.h;
  }

  float inner_width() const {
    return std::max(0.f, rect_.w - style_.padding * 2.f);
  }

  void recompute_metrics(Canvas& g) {
    int cp_len = utf8_length(text_);
    prefix_x_.resize(cp_len + 1);
    for (int cp = 0; cp <= cp_len; ++cp) {
      int byte_off = utf8_byte_offset(text_, cp);
      prefix_x_[cp] = g.text_width(std::string_view(text_.data(), (size_t)byte_off), style_.font);
    }
    invalidate_metrics_ = false;
    float w = inner_width();
    float text_w = prefix_x_.empty() ? 0.f : prefix_x_.back();
    float max_scroll = std::max(0.f, text_w - w);
    scroll_px_ = std::clamp(scroll_px_, 0.f, max_scroll);
  }

  void ensure_caret_visible() {
    float w = inner_width();
    if (prefix_x_.empty()) { scroll_px_ = 0.f; return; }
    int cp_len = (int)prefix_x_.size() - 1;
    int cp = std::clamp(caret_cp_, 0, cp_len);
    float caret_x = prefix_x_[cp];
    if (caret_x < scroll_px_) scroll_px_ = caret_x;
    else if (caret_x > scroll_px_ + w) scroll_px_ = caret_x - w;
    float text_w = prefix_x_.empty() ? 0.f : prefix_x_.back();
    float max_scroll = std::max(0.f, text_w - w);
    scroll_px_ = std::clamp(scroll_px_, 0.f, max_scroll);
  }

  void clamp_scroll_after_resize() {
    float w = inner_width();
    float text_w = prefix_x_.empty() ? 0.f : prefix_x_.back();
    float max_scroll = std::max(0.f, text_w - w);
    scroll_px_ = std::clamp(scroll_px_, 0.f, max_scroll);
    need_scroll_adjust_ = true;
  }

private:
  std::string text_;
  std::string placeholder_;
  TextFieldStyle style_{};

  bool focused_{false};
  int  caret_cp_{0};
  bool invalidate_metrics_{true};
  bool need_scroll_adjust_{true};
  std::vector<float> prefix_x_;
  float scroll_px_{0.f};
};

} // namespace pulseui::ui
