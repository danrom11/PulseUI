#include <pulseui/gfx/svg.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/widgets.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cmath>

namespace pulseui {

namespace {

// UTF-8 helpers (byte offsets on code point boundaries)
std::size_t utf8_prev(const std::string& s, std::size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}
std::size_t utf8_next(const std::string& s, std::size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}
std::size_t cp_index(const std::string& s, std::size_t byte) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < byte && i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
  return n;
}
std::size_t byte_of_cp(const std::string& s, std::size_t cp) {
  std::size_t i = 0;
  while (cp > 0 && i < s.size()) {
    i = utf8_next(s, i);
    --cp;
  }
  return i;
}

void focus_ring(Painter& p, const Rect& r, float radius) {
  p.stroke_rect(r.outset(2), th().border_focus.with_alpha(0.7f), 2, radius + 2);
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Text
// ═════════════════════════════════════════════════════════════════════════════
Text::Text(Bind<std::string> text) {
  bind(text, [](View& v, const std::string& s) { static_cast<Text&>(v).set_text(s); });
}

void Text::set_text(std::string t) {
  if (t == text_) return;
  text_ = std::move(t);
  invalidate_layout();
}

Font Text::font() const {
  const Theme& t = th();
  Font f = mono_ ? t.font_mono : t.font;
  if (font_size_ > 0) f.size = font_size_;
  else if (mono_) f.size = t.font.size;
  f.weight = weight_;
  return f;
}

Size Text::measure_content(Size avail) {
  const Font f = font();
  if (text_.empty()) return {0, line_height(f)};
  const float maxw = wrap_ && std::isfinite(avail.w) ? avail.w : 0;
  Size s = measure_text(text_, f, maxw);
  s.w = std::ceil(s.w);
  s.h = std::ceil(s.h);
  return s;
}

void Text::paint_content(Painter& p) {
  const Theme& t = th();
  const Color c = color_ ? *color_ : (dim_ ? t.text_dim : t.text);
  p.text(content_rect(), text_, font(), c, align_, wrap_);
}

// ═════════════════════════════════════════════════════════════════════════════
// Button
// ═════════════════════════════════════════════════════════════════════════════
Button::Button(Bind<std::string> label, std::function<void()> on_click) {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  style_.padding = Insets(14, 0);
  bind(label, [](View& v, const std::string& s) { static_cast<Button&>(v).set_label(s); });
  if (on_click) clicked.connect(std::move(on_click));
}

Size Button::measure_content(Size) {
  const Theme& t = th();
  if (is_svg_icon(icon_)) {
    const Size ts = label_.empty() ? Size{0, 0} : measure_text(label_, t.font.with_weight(FontWeight::Medium), 0);
    const float icon_w = t.font.size * 1.25f;
    return {std::ceil(ts.w) + icon_w + (label_.empty() ? 0 : 6) + 2, std::max(ts.h, t.control_height)};
  }
  std::string s = icon_.empty() ? label_ : (label_.empty() ? icon_ : icon_ + "  " + label_);
  Size ts = measure_text(s, t.font.with_weight(FontWeight::Medium), 0);
  return {std::ceil(ts.w) + 2, std::max(ts.h, t.control_height)};
}

void Button::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  const float rad = style_.radius > 0 ? style_.radius : t.radius;
  Color bg, fg = t.text, border = Color::transparent();
  switch (kind_) {
    case ButtonKind::Primary:
      bg = pressed_ ? t.accent_pressed : hovered_ ? t.accent_hover : t.accent;
      fg = t.on_accent;
      break;
    case ButtonKind::Danger:
      bg = pressed_ ? t.danger.darker(0.15f) : hovered_ ? t.danger.lighter(0.1f) : t.danger;
      fg = Color::white();
      break;
    case ButtonKind::Flat:
      bg = pressed_ ? t.control_pressed : hovered_ ? t.control_hover : Color::transparent();
      break;
    default:
      bg = pressed_ ? t.control_pressed : hovered_ ? t.control_hover : t.control;
      border = t.border;
      break;
  }
  if (style_.background.a > 0 && kind_ == ButtonKind::Normal) bg = style_.background;
  p.fill_rect(r, bg, rad);
  if (border.a > 0) p.stroke_rect(r, border, 1, rad);
  if (is_svg_icon(icon_)) {
    // SVG icon + label, centred together
    const Font f = t.font.with_weight(FontWeight::Medium);
    const float icon_w = t.font.size * 1.25f;
    const float tw = label_.empty() ? 0 : std::ceil(measure_text(label_, f, 0).w);
    const float total = icon_w + (label_.empty() ? 0 : 6 + tw);
    const Rect c = content_rect();
    const float x = c.x + (c.w - total) / 2;
    draw_icon(p, icon_, {x, c.y, icon_w, c.h}, fg, t.font);
    if (!label_.empty()) p.text({x + icon_w + 6, c.y, tw + 4, c.h}, label_, f, fg, Align::LeftCenter);
  } else {
    std::string s = icon_.empty() ? label_ : (label_.empty() ? icon_ : icon_ + "  " + label_);
    p.text(content_rect(), s, t.font.with_weight(FontWeight::Medium), fg, Align::Center);
  }
  if (focused()) focus_ring(p, r, rad);
}

bool Button::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left && pressed_ && bounds().contains(e.pos)) {
        std::weak_ptr<bool> alive = alive_;
        auto handlers = click_handlers_;
        clicked.emit();
        if (alive.expired()) return true;
        for (auto& fn : handlers) fn();
      }
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Enter || e.key == Key::Space) {
        auto handlers = click_handlers_;
        clicked.emit();
        for (auto& fn : handlers) fn();
        return true;
      }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// TextField
// ═════════════════════════════════════════════════════════════════════════════
TextField::TextField() {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::IBeam;
  style_.padding = Insets(10, 0);
}

TextField::TextField(Model<std::string>& model) : TextField() {
  model_ = &model;
  bind(Bind<std::string>(model), [](View& v, const std::string& s) { static_cast<TextField&>(v).set_text(s); });
}

TextField::TextField(Bind<std::string> text) : TextField() {
  bind(text, [](View& v, const std::string& s) { static_cast<TextField&>(v).set_text(s); });
}

void TextField::set_text(std::string t) {
  if (t == text_) return;
  text_ = std::move(t);
  caret_ = std::min(caret_, text_.size());
  sel_ = std::min(sel_, text_.size());
  ensure_caret_visible();
  invalidate();
}

std::string TextField::display_text() const {
  if (!password_) return text_;
  std::string out;
  const std::size_t n = cp_index(text_, text_.size());
  for (std::size_t i = 0; i < n; ++i) out += "\xE2\x80\xA2"; // •
  return out;
}

void TextField::edit(std::string new_text, std::size_t caret) {
  text_ = std::move(new_text);
  caret_ = sel_ = std::min(caret, text_.size());
  ensure_caret_visible();
  invalidate();
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(text_);
  if (!alive.expired()) changed.emit(text_);
  if (!alive.expired() && suggest_ && focused()) refresh_suggestions();
}

// ── suggestions ──────────────────────────────────────────────────────────────
namespace {
class SuggestPopup : public ViewT<SuggestPopup> {
public:
  static constexpr float kRow = 26;
  static constexpr int kMaxRows = 8;
  SuggestPopup() { hover_repaint_ = true; }
  const char* type_name() const override { return "Suggestions"; }
  std::vector<Suggestion> items;
  int index = -1, first = 0;
  std::function<void(int)> on_pick;
  std::function<void(int)> on_hover;

  void keep_visible() {
    if (index < first) first = std::max(0, index);
    if (index >= first + kMaxRows) first = index - kMaxRows + 1;
  }
  Size measure_content(Size) override {
    const Theme& t = th();
    float w = 160;
    for (auto& s : items) {
      float rw = measure_text(s.label, t.font, 0).w + 24;
      if (!s.detail.empty()) rw += measure_text(s.detail, t.font_small, 0).w + 20;
      w = std::max(w, std::ceil(rw));
    }
    const int rows = std::min<int>(kMaxRows, static_cast<int>(items.size()));
    return {std::min(w, 520.f), rows * kRow + 6};
  }
  void paint(Painter& p) override {
    const Theme& t = th();
    p.fill_rect(bounds(), t.surface, t.radius);
    p.stroke_rect(bounds(), t.border, 1, t.radius);
    const int rows = std::min<int>(kMaxRows, static_cast<int>(items.size()) - first);
    for (int r = 0; r < rows; ++r) {
      const int i = first + r;
      const Rect row{3, 3 + r * kRow, frame_.w - 6, kRow};
      const bool sel = i == index;
      if (sel) p.fill_rect(row, t.accent, t.radius_small);
      const Suggestion& s = items[static_cast<std::size_t>(i)];
      p.text({row.x + 8, row.y, row.w - 16, row.h}, s.label, t.font, sel ? t.on_accent : t.text, Align::LeftCenter);
      if (!s.detail.empty())
        p.text({row.x + 8, row.y, row.w - 16, row.h}, s.detail, t.font_small, sel ? t.on_accent.with_alpha(0.85f) : t.text_dim,
               Align::RightCenter);
    }
    if (static_cast<int>(items.size()) > kMaxRows) { // a thin scroll indicator
      const float h = frame_.h - 6, th_h = std::max(16.f, h * kMaxRows / items.size());
      const float y = 3 + (h - th_h) * first / std::max<int>(1, static_cast<int>(items.size()) - kMaxRows);
      p.fill_rect({frame_.w - 5, y, 3, th_h}, t.scrollbar, 1.5f);
    }
  }
  int row_at(Point pt) const {
    const int r = static_cast<int>((pt.y - 3) / kRow);
    const int i = first + r;
    return r >= 0 && r < kMaxRows && i < static_cast<int>(items.size()) ? i : -1;
  }
  bool on_event(InputEvent& e) override {
    switch (e.type) {
      case InputEvent::MouseMove: {
        const int i = row_at(e.pos);
        if (i >= 0 && i != index && on_hover) on_hover(i);
        return true;
      }
      case InputEvent::MouseDown: {
        const int i = row_at(e.pos);
        if (i >= 0 && on_pick) on_pick(i);
        return true;
      }
      case InputEvent::Wheel:
        first = std::clamp(first - static_cast<int>(e.wheel_dy * 3), 0, std::max(0, static_cast<int>(items.size()) - kMaxRows));
        invalidate();
        return true;
      default:
        return e.is_mouse();
    }
  }
};
} // namespace

void TextField::refresh_suggestions() {
  if (!suggest_ || !window()) return;
  std::vector<Suggestion> list = suggest_(text_);
  if (list.empty()) {
    close_suggestions();
    return;
  }
  // keep the chosen entry when it is still there
  const std::string chosen = sugg_index_ >= 0 && sugg_index_ < static_cast<int>(sugg_items_.size())
                                 ? sugg_items_[static_cast<std::size_t>(sugg_index_)].label : std::string();
  sugg_items_ = std::move(list);
  sugg_index_ = 0;
  for (std::size_t i = 0; i < sugg_items_.size(); ++i)
    if (sugg_items_[i].label == chosen) sugg_index_ = static_cast<int>(i);
  if (auto* pop = static_cast<SuggestPopup*>(sugg_popup_)) {
    pop->items = sugg_items_;
    pop->index = sugg_index_;
    pop->first = 0;
    pop->keep_visible();
    pop->invalidate_layout();
    return;
  }
  SuggestPopup pop;
  pop.items = sugg_items_;
  pop.index = sugg_index_;
  std::weak_ptr<bool> alive = alive_;
  pop.on_pick = [this, alive](int i) {
    if (!alive.expired()) accept_suggestion(i);
  };
  pop.on_hover = [this, alive](int i) {
    if (alive.expired() || !sugg_popup_) return;
    sugg_index_ = i;
    static_cast<SuggestPopup*>(sugg_popup_)->index = i;
    sugg_popup_->invalidate();
  };
  sugg_popup_ = window()->open_dropdown(std::move(pop), *this, [this, alive] {
    if (alive.expired()) return;
    sugg_popup_ = nullptr;
  });
}

void TextField::close_suggestions() {
  if (sugg_popup_ && window()) {
    View* p = sugg_popup_;
    sugg_popup_ = nullptr;
    window()->close_popup(p);
  }
  sugg_popup_ = nullptr;
  sugg_index_ = -1;
}

void TextField::accept_suggestion(int index) {
  if (index < 0 || index >= static_cast<int>(sugg_items_.size())) return;
  const Suggestion s = sugg_items_[static_cast<std::size_t>(index)];
  close_suggestions();
  const std::string v = s.value.empty() ? s.label : s.value;
  std::weak_ptr<bool> alive = alive_;
  text_ = v;
  caret_ = sel_ = text_.size();
  ensure_caret_visible();
  invalidate();
  if (model_) model_->set(text_);
  if (!alive.expired()) changed.emit(text_);
  if (!alive.expired()) submitted.emit(text_);
  if (!alive.expired()) committed.emit(text_);
}

void TextField::delete_selection() {
  const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
  std::string t = text_;
  t.erase(a, b - a);
  edit(std::move(t), a);
}

std::size_t TextField::index_at(float x) const {
  const auto stops = caret_stops(display_text(), th().font);
  const float local = x - content_rect().x + scroll_;
  std::size_t best = 0;
  float bestd = 1e9f;
  for (std::size_t i = 0; i < stops.size(); ++i) {
    const float d = std::abs(stops[i] - local);
    if (d < bestd) {
      bestd = d;
      best = i;
    }
  }
  return byte_of_cp(text_, best);
}

void TextField::ensure_caret_visible() {
  const float w = content_rect().w;
  if (w <= 0) return;
  const std::string d = display_text();
  const std::string disp_prefix = password_ ? d.substr(0, cp_index(text_, caret_) * 3) : text_.substr(0, caret_);
  const float cx = measure_text(disp_prefix, th().font, 0).w;
  const float total = measure_text(d, th().font, 0).w;
  if (cx - scroll_ > w - 2) scroll_ = cx - w + 2;
  if (cx - scroll_ < 0) scroll_ = cx;
  scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, total - w + 2));
}

Size TextField::measure_content(Size) {
  const Theme& t = th();
  return {160, std::max(line_height(t.font), t.control_height)};
}

void TextField::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  const bool f = focused();
  p.fill_rect(r, t.input, t.radius);
  p.stroke_rect(r, f ? t.border_focus : (hovered_ ? t.border.lighter(0.15f) : t.border), f ? 1.5f : 1.f, t.radius);

  const Rect cr = content_rect();
  p.save();
  p.clip(cr);
  const std::string d = display_text();
  const auto stops = caret_stops(d, t.font);
  auto x_of = [&](std::size_t byte) {
    const std::size_t i = std::min(cp_index(text_, byte), stops.size() - 1);
    return cr.x + stops[i] - scroll_;
  };
  if (f && has_selection()) {
    const float a = x_of(std::min(caret_, sel_)), b = x_of(std::max(caret_, sel_));
    p.fill_rect({a, cr.y + (cr.h - line_height(t.font)) / 2, b - a, line_height(t.font)}, t.selection);
  }
  if (text_.empty()) {
    if (!placeholder_.empty()) p.text(cr, placeholder_, t.font, t.text_disabled, Align::LeftCenter);
  } else {
    p.text({cr.x - scroll_, cr.y, cr.w + scroll_ + 4000, cr.h}, d, t.font, t.text, Align::LeftCenter);
  }
  if (f) {
    const float cx = std::round(x_of(caret_));
    const float lh = line_height(t.font);
    p.fill_rect({cx, cr.y + (cr.h - lh) / 2, 1.5f, lh}, t.text);
  }
  p.restore();
}

bool TextField::on_event(InputEvent& e) {
  if (sugg_popup_ && e.type == InputEvent::KeyDown) {
    auto* pop = static_cast<SuggestPopup*>(sugg_popup_);
    const int n = static_cast<int>(sugg_items_.size());
    switch (e.key) {
      case Key::Down:
      case Key::Up:
        sugg_index_ = n ? (sugg_index_ + (e.key == Key::Down ? 1 : -1) + n) % n : -1;
        pop->index = sugg_index_;
        pop->keep_visible();
        pop->invalidate();
        return true;
      case Key::Enter:
        if (sugg_index_ >= 0) {
          accept_suggestion(sugg_index_);
          return true;
        }
        break;
      case Key::Tab:
        if (sugg_index_ >= 0) accept_suggestion(sugg_index_);
        return false; // the focus moves on
      case Key::Escape:
        close_suggestions();
        return true;
      default:
        break;
    }
  }
  switch (e.type) {
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return true;
      if (e.clicks >= 2) {
        sel_ = 0;
        caret_ = text_.size();
      } else {
        caret_ = index_at(e.pos.x);
        if (!(e.mods & Mod::Shift)) sel_ = caret_;
        selecting_ = true;
      }
      invalidate();
      return true;
    }
    case InputEvent::MouseMove:
      if (selecting_) {
        caret_ = index_at(e.pos.x);
        ensure_caret_visible();
        invalidate();
        return true;
      }
      return false;
    case InputEvent::MouseUp:
      selecting_ = false;
      return true;
    case InputEvent::Text: {
      std::string t = text_;
      const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
      t.replace(a, b - a, e.text);
      edit(std::move(t), a + e.text.size());
      return true;
    }
    case InputEvent::KeyDown: {
      const bool shift = (e.mods & Mod::Shift) != 0;
      const bool primary = e.primary();
      auto move_to = [&](std::size_t pos) {
        caret_ = pos;
        if (!shift) sel_ = caret_;
        ensure_caret_visible();
        invalidate();
      };
      switch (e.key) {
        case Key::Left:
          if (!shift && has_selection()) move_to(std::min(caret_, sel_));
          else move_to(utf8_prev(text_, caret_));
          return true;
        case Key::Right:
          if (!shift && has_selection()) move_to(std::max(caret_, sel_));
          else move_to(utf8_next(text_, caret_));
          return true;
        case Key::Home: move_to(0); return true;
        case Key::End: move_to(text_.size()); return true;
        case Key::Backspace:
          if (has_selection()) delete_selection();
          else if (caret_ > 0) {
            const std::size_t p = utf8_prev(text_, caret_);
            std::string t = text_;
            t.erase(p, caret_ - p);
            edit(std::move(t), p);
          }
          return true;
        case Key::Delete:
          if (has_selection()) delete_selection();
          else if (caret_ < text_.size()) {
            const std::size_t n = utf8_next(text_, caret_);
            std::string t = text_;
            t.erase(caret_, n - caret_);
            edit(std::move(t), caret_);
          }
          return true;
        case Key::Enter: {
          std::weak_ptr<bool> alive = alive_;
          submitted.emit(text_);
          if (!alive.expired()) committed.emit(text_);
          return true;
        }
        case Key::A:
          if (primary) {
            sel_ = 0;
            caret_ = text_.size();
            invalidate();
            return true;
          }
          return false;
        case Key::C:
        case Key::X:
          if (primary && has_selection() && !password_) {
            const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
            App::set_clipboard(text_.substr(a, b - a));
            if (e.key == Key::X) delete_selection();
            return true;
          }
          return primary;
        case Key::V:
          if (primary) {
            std::string clip = App::clipboard();
            clip.erase(std::remove_if(clip.begin(), clip.end(), [](char c) { return c == '\r' || c == '\n'; }), clip.end());
            std::string t = text_;
            const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
            t.replace(a, b - a, clip);
            edit(std::move(t), a + clip.size());
            return true;
          }
          return false;
        default:
          // swallow printable keys (they arrive as Text events)
          return e.key >= Key::A && e.key <= Key::Num9 && !primary;
      }
    }
    default:
      return false;
  }
}

void TextField::on_focus(bool f) {
  invalidate();
  if (f && suggest_) refresh_suggestions();
  if (!f) close_suggestions();
  if (!f) {
    sel_ = caret_;
    committed.emit(text_);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Checkbox / Toggle
// ═════════════════════════════════════════════════════════════════════════════
namespace detail {
Size bool_input_measure(const std::string& label, float indicator_w) {
  const Theme& t = th();
  Size ts = label.empty() ? Size{0, 0} : measure_text(label, t.font, 0);
  return {indicator_w + (label.empty() ? 0 : 8 + std::ceil(ts.w)), std::max(22.f, std::ceil(ts.h))};
}

bool bool_input_event(View& v, InputEvent& e, bool& toggled) {
  switch (e.type) {
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left && v.pressed() && v.bounds().contains(e.pos)) toggled = true;
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Space || e.key == Key::Enter) {
        toggled = true;
        return true;
      }
      return false;
    default:
      return false;
  }
}
} // namespace detail

void Checkbox::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect cr = content_rect();
  const Rect box{cr.x, cr.y + (cr.h - 18) / 2, 18, 18};
  if (checked_) {
    p.fill_rect(box, hovered_ ? t.accent_hover : t.accent, t.radius_small);
    p.polyline({{box.x + 4.5f, box.y + 9.5f}, {box.x + 7.8f, box.y + 12.8f}, {box.x + 13.5f, box.y + 5.5f}},
               t.on_accent, 2.f);
  } else {
    p.fill_rect(box, t.input, t.radius_small);
    p.stroke_rect(box, hovered_ ? t.border_focus : t.border, 1.2f, t.radius_small);
  }
  if (focused()) focus_ring(p, box, t.radius_small);
  if (!label_.empty())
    p.text({box.right() + 8, cr.y, cr.w - 26, cr.h}, label_, t.font, t.text, Align::LeftCenter);
}

void Toggle::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect cr = content_rect();
  const Rect track{cr.x, cr.y + (cr.h - 20) / 2, 36, 20};
  const float k = knob_;
  const Color off = hovered_ ? t.control_hover : t.control, on = hovered_ ? t.accent_hover : t.accent;
  p.fill_rect(track, Color::mix(off, on, k), 10);
  if (k < 1) p.stroke_rect(track, t.border.with_alpha(t.border.a * (1 - k)), 1, 10);
  const float kx = track.x + 2 + (track.w - 20) * k;
  p.fill_ellipse({kx, track.y + 2, 16, 16}, Color::mix(t.text_dim, t.on_accent, k));
  if (focused()) focus_ring(p, track, 10);
  if (!label_.empty())
    p.text({track.right() + 8, cr.y, cr.w - 44, cr.h}, label_, t.font, t.text, Align::LeftCenter);
}

// ═════════════════════════════════════════════════════════════════════════════
// Slider / ProgressBar
// ═════════════════════════════════════════════════════════════════════════════
Slider::Slider(Model<double>& model, double min, double max) : min_(min), max_(max) {
  model_ = &model;
  focusable_ = true;
  hover_repaint_ = true;
  bind(Bind<double>(model), [](View& v, double x) { static_cast<Slider&>(v).set_value(x, false); });
}

Slider::Slider(Bind<double> value, double min, double max) : min_(min), max_(max) {
  focusable_ = true;
  hover_repaint_ = true;
  bind(value, [](View& v, double x) { static_cast<Slider&>(v).set_value(x, false); });
}

void Slider::set_value(double v, bool notify) {
  if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
  v = std::clamp(v, std::min(min_, max_), std::max(min_, max_));
  if (v == value_) return;
  value_ = v;
  invalidate();
  if (notify) {
    if (model_) model_->set(v);
    changed.emit(v);
  }
}

void Slider::set_from_x(float x) {
  const Rect cr = content_rect().inset(Insets(8, 0));
  if (cr.w <= 0) return;
  const double f = std::clamp((x - cr.x) / cr.w, 0.f, 1.f);
  set_value(min_ + f * (max_ - min_));
}

Size Slider::measure_content(Size) { return {160, 24}; }

void Slider::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect cr = content_rect().inset(Insets(8, 0));
  const float cy = cr.y + cr.h / 2;
  const double span = max_ - min_;
  const float f = span != 0 ? static_cast<float>((value_ - min_) / span) : 0.f;
  const float kx = cr.x + cr.w * f;
  p.fill_rect({cr.x, cy - 2, cr.w, 4}, t.control, 2);
  p.fill_rect({cr.x, cy - 2, kx - cr.x, 4}, t.accent, 2);
  const float kr = (pressed_ || hovered_) ? 9.f : 8.f;
  p.fill_ellipse({kx - kr, cy - kr, kr * 2, kr * 2}, t.on_accent);
  p.stroke_ellipse({kx - kr, cy - kr, kr * 2, kr * 2}, t.accent, 2);
  if (focused()) p.stroke_ellipse({kx - kr - 3, cy - kr - 3, kr * 2 + 6, kr * 2 + 6}, t.border_focus.with_alpha(0.6f), 2);
}

bool Slider::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown:
      if (e.button != MouseButton::Left) return false;
      set_from_x(e.pos.x);
      return true;
    case InputEvent::MouseMove:
      if (pressed_) {
        set_from_x(e.pos.x);
        return true;
      }
      return false;
    case InputEvent::MouseUp:
      return true;
    case InputEvent::KeyDown: {
      const double st = step_ > 0 ? step_ : (max_ - min_) / 20;
      if (e.key == Key::Left || e.key == Key::Down) { set_value(value_ - st); return true; }
      if (e.key == Key::Right || e.key == Key::Up) { set_value(value_ + st); return true; }
      return false;
    }
    default:
      return false;
  }
}

ProgressBar::ProgressBar(Bind<double> fraction) {
  bind(fraction, [](View& v, double x) { static_cast<ProgressBar&>(v).set_value(x); });
}

void ProgressBar::set_value(double v) {
  v = std::clamp(v, 0.0, 1.0);
  if (v == value_) return;
  value_ = v;
  invalidate();
}

Size ProgressBar::measure_content(Size) { return {160, 8}; }

void ProgressBar::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect cr = content_rect();
  const float h = std::min(cr.h, 8.f);
  const Rect track{cr.x, cr.y + (cr.h - h) / 2, cr.w, h};
  p.fill_rect(track, t.control, h / 2);
  if (value_ > 0) p.fill_rect({track.x, track.y, std::max(h, track.w * static_cast<float>(value_)), h}, t.accent, h / 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// Select
// ═════════════════════════════════════════════════════════════════════════════
namespace {
class MenuItem : public ViewT<MenuItem> {
public:
  MenuItem(std::string label, bool selected) : label_(std::move(label)), selected_(selected) {
    hover_repaint_ = true;
    style_.padding = Insets(10, 0);
  }
  const char* type_name() const override { return "MenuItem"; }
  Size measure_content(Size) override {
    Size s = measure_text(label_, th().font, 0);
    return {std::ceil(s.w) + 16, 28};
  }
  void paint(Painter& p) override {
    const Theme& t = th();
    if (hovered_) p.fill_rect(bounds(), t.accent, t.radius_small);
    else if (selected_) p.fill_rect(bounds(), t.selection, t.radius_small);
    p.text(content_rect(), label_, t.font, hovered_ ? t.on_accent : t.text, Align::LeftCenter);
  }

private:
  std::string label_;
  bool selected_;
};
} // namespace

Select::Select(Bind<std::vector<std::string>> options, Model<int>& index) {
  model_ = &index;
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  style_.padding = Insets(10, 0);
  bind(options, [](View& v, const std::vector<std::string>& o) { static_cast<Select&>(v).set_options(o); });
  bind(Bind<int>(index), [](View& v, int i) { static_cast<Select&>(v).set_index(i, false); });
}

Select::Select(Bind<std::vector<std::string>> options, Bind<int> index) {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  style_.padding = Insets(10, 0);
  bind(options, [](View& v, const std::vector<std::string>& o) { static_cast<Select&>(v).set_options(o); });
  bind(index, [](View& v, int i) { static_cast<Select&>(v).set_index(i, false); });
}

void Select::set_options(std::vector<std::string> o) {
  options_ = std::move(o);
  invalidate_layout();
}

void Select::set_index(int i, bool notify) {
  if (i == index_) return;
  index_ = i;
  invalidate();
  if (notify) {
    if (model_) model_->set(i);
    changed.emit(i);
  }
}

Size Select::measure_content(Size) {
  const Theme& t = th();
  float w = 60;
  for (auto& o : options_) w = std::max(w, measure_text(o, t.font, 0).w);
  return {std::ceil(w) + 24, t.control_height};
}

void Select::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  p.fill_rect(r, pressed_ || popup_ ? t.control_pressed : hovered_ ? t.control_hover : t.control, t.radius);
  p.stroke_rect(r, popup_ ? t.border_focus : t.border, 1, t.radius);
  const Rect cr = content_rect();
  const std::string cur = index_ >= 0 && index_ < static_cast<int>(options_.size()) ? options_[index_] : std::string();
  p.text({cr.x, cr.y, cr.w - 18, cr.h}, cur, t.font, t.text, Align::LeftCenter);
  const float cx = cr.right() - 7, cy = cr.y + cr.h / 2;
  p.polyline({{cx - 4, cy - 2}, {cx, cy + 2}, {cx + 4, cy - 2}}, t.text_dim, 1.6f);
  if (focused()) focus_ring(p, r, t.radius);
}

void Select::open_popup() {
  if (!window() || popup_) return;
  VStack list;
  list.spacing(1).padding(Insets(4));
  for (std::size_t i = 0; i < options_.size(); ++i) {
    list.add(MenuItem(options_[i], static_cast<int>(i) == index_).on_click([this, i] {
      set_index(static_cast<int>(i));
      if (popup_ && window()) window()->close_popup(popup_);
    }));
  }
  Panel box;
  box.padding(Insets(0)).width(std::max(frame_.w, 120.f));
  box.add(Scroll(std::move(list)).max_height(320));
  popup_ = window()->open_dropdown(std::move(box), *this, [this] {
    popup_ = nullptr;
    invalidate();
  });
}

bool Select::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left && pressed_ && bounds().contains(e.pos)) open_popup();
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Space || e.key == Key::Enter) { open_popup(); return true; }
      if (e.key == Key::Down) { set_index(std::min<int>(index_ + 1, static_cast<int>(options_.size()) - 1)); return true; }
      if (e.key == Key::Up) { set_index(std::max(index_ - 1, 0)); return true; }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Segmented
// ═════════════════════════════════════════════════════════════════════════════
Segmented::Segmented(Bind<std::vector<std::string>> options, Model<int>& index) : Segmented(std::move(options)) {
  model_ = &index;
  bind(Bind<int>(index), [](View& v, int i) { static_cast<Segmented&>(v).set_index(i, false); });
}

Segmented::Segmented(Bind<std::vector<std::string>> options, Bind<int> index) {
  focusable_ = true;
  cursor_ = Cursor::Hand;
  bind(options, [](View& v, const std::vector<std::string>& o) { static_cast<Segmented&>(v).set_options(o); });
  bind(index, [](View& v, int i) { static_cast<Segmented&>(v).set_index(i, false); });
}

void Segmented::set_index(int i, bool notify) {
  if (i == index_) return;
  index_ = i;
  invalidate();
  if (notify) {
    if (model_) model_->set(i);
    changed.emit(i);
  }
}

std::vector<float> Segmented::widths() const {
  std::vector<float> w;
  for (auto& o : options_) w.push_back(std::ceil(measure_text(o, th().font.with_weight(FontWeight::Medium), 0).w) + 28);
  return w;
}

Size Segmented::measure_content(Size) {
  float total = 4;
  for (float w : widths()) total += w;
  return {total, th().control_height};
}

int Segmented::at(Point p) const {
  float x = 2;
  const auto w = widths();
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (p.x >= x && p.x < x + w[i]) return static_cast<int>(i);
    x += w[i];
  }
  return -1;
}

void Segmented::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  p.fill_rect(r, t.control, t.radius);
  p.stroke_rect(r, t.border, 1, t.radius);
  float x = 2;
  const auto w = widths();
  for (std::size_t i = 0; i < w.size(); ++i) {
    const Rect seg{x, 2, w[i], r.h - 4};
    const bool cur = static_cast<int>(i) == index_;
    if (cur) p.fill_rect(seg, t.accent, t.radius - 1);
    else if (static_cast<int>(i) == hover_) p.fill_rect(seg, t.control_hover, t.radius - 1);
    p.text(seg, options_[i], t.font.with_weight(FontWeight::Medium), cur ? t.on_accent : t.text, Align::Center);
    x += w[i];
  }
  if (focused()) p.stroke_rect(r.outset(2), t.border_focus.with_alpha(0.7f), 2, t.radius + 2);
}

bool Segmented::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int h = at(e.pos);
      if (h != hover_) {
        hover_ = h;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseDown:
      if (e.button != MouseButton::Left) return false;
      if (int i = at(e.pos); i >= 0) set_index(i);
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Left) { set_index(std::max(0, index_ - 1)); return true; }
      if (e.key == Key::Right) { set_index(std::min(static_cast<int>(options_.size()) - 1, index_ + 1)); return true; }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Tabs
// ═════════════════════════════════════════════════════════════════════════════
void Tabs::push_tab(Tab t) {
  const int index = static_cast<int>(titles_.size());
  titles_.push_back(t.title.value().value_or(std::string()));
  bind(t.title, [index](View& v, const std::string& s) { static_cast<Tabs&>(v).set_title(index, s); });
  View& v = add(std::move(t.content));
  v.set_visible(static_cast<int>(titles_.size()) - 1 == current_);
}

void Tabs::bind_model(Model<int>& m) {
  model_ = &m;
  bind(Bind<int>(m), [](View& v, int i) { static_cast<Tabs&>(v).set_current(i, false); });
}

void Tabs::sync_pages() {
  while (titles_.size() < children_.size()) titles_.push_back("Tab " + std::to_string(titles_.size() + 1));
  for (std::size_t k = 0; k < children_.size(); ++k) children_[k]->set_visible(static_cast<int>(k) == current_);
}

void Tabs::set_current(int i, bool notify) {
  if (children_.empty()) return;
  i = std::clamp(i, 0, static_cast<int>(children_.size()) - 1);
  if (i == current_) return;
  current_ = i;
  for (std::size_t k = 0; k < children_.size(); ++k) children_[k]->set_visible(static_cast<int>(k) == i);
  invalidate_layout();
  if (notify) {
    if (model_) model_->set(i);
    changed.emit(i);
  }
}

void Tabs::set_title(int i, std::string t) {
  if (i >= 0 && i < static_cast<int>(titles_.size())) titles_[i] = std::move(t);
  invalidate();
}

float Tabs::header_height() const { return 36; }

int Tabs::tab_at(Point p) const {
  if (p.y < 0 || p.y > header_height()) return -1;
  float x = 4;
  for (std::size_t i = 0; i < titles_.size(); ++i) {
    const float w = measure_text(titles_[i], th().font, 0).w + 28;
    if (p.x >= x && p.x < x + w) return static_cast<int>(i);
    x += w;
  }
  return -1;
}

Size Tabs::measure_content(Size avail) {
  Size s{};
  for (auto& c : children_) {
    Size cs = c->measure({avail.w, avail.h - header_height()});
    s.w = std::max(s.w, cs.w);
    s.h = std::max(s.h, cs.h);
  }
  return {s.w, s.h + header_height()};
}

void Tabs::layout() {
  if (titles_.size() != children_.size()) {
    // pages added or removed as plain children (forms, the designer)
    if (titles_.size() > children_.size()) titles_.resize(children_.size());
    sync_pages();
  }
  const Rect cr = content_rect();
  const Rect body{cr.x, cr.y + header_height(), cr.w, std::max(0.f, cr.h - header_height())};
  for (auto& c : children_) {
    if (!c->is_visible()) continue;
    c->set_frame(body);
    c->layout();
  }
}

void Tabs::paint(Painter& p) {
  View::paint(p);
  const Theme& t = th();
  const Rect cr = content_rect();
  p.line({cr.x, cr.y + header_height() - 0.5f}, {cr.right(), cr.y + header_height() - 0.5f}, t.border);
  float x = cr.x + 4;
  for (std::size_t i = 0; i < titles_.size(); ++i) {
    const float w = measure_text(titles_[i], t.font, 0).w + 28;
    const Rect r{x, cr.y, w, header_height()};
    const bool cur = static_cast<int>(i) == current_;
    if (static_cast<int>(i) == hover_tab_ && !cur) p.fill_rect(r.inset(Insets(2, 4)), t.control_hover, t.radius_small);
    p.text(r, titles_[i], t.font.with_weight(cur ? FontWeight::SemiBold : FontWeight::Normal),
           cur ? t.text : t.text_dim, Align::Center);
    if (cur) p.fill_rect({r.x + 8, r.bottom() - 2.5f, r.w - 16, 2.5f}, t.accent, 1);
    x += w;
  }
}

bool Tabs::on_event(InputEvent& e) {
  const Point local = e.pos - content_rect().pos();
  if (e.type == InputEvent::MouseMove) {
    const int h = tab_at(local);
    if (h != hover_tab_) {
      hover_tab_ = h;
      cursor_ = h >= 0 ? Cursor::Hand : Cursor::Arrow;
      invalidate();
    }
    return false;
  }
  if (e.type == InputEvent::MouseDown && e.button == MouseButton::Left) {
    const int i = tab_at(local);
    if (i >= 0) {
      set_current(i);
      return true;
    }
  }
  return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// Split
// ═════════════════════════════════════════════════════════════════════════════
Split::Split(Node first, Node second, Orientation o, float first_size) : orientation_(o), pane_(first_size) {
  add(std::move(first));
  add(std::move(second));
}

Size Split::measure_content(Size avail) {
  Size s{};
  for (auto& c : children_) {
    Size cs = c->measure(avail);
    if (orientation_ == Orientation::Horizontal) { s.w += cs.w; s.h = std::max(s.h, cs.h); }
    else { s.h += cs.h; s.w = std::max(s.w, cs.w); }
  }
  return s;
}

Rect Split::handle_rect() const {
  const Rect cr = content_rect();
  const float total = orientation_ == Orientation::Horizontal ? cr.w : cr.h;
  float first = std::clamp(keep_second_ ? total - pane_ - 6 : pane_, min_pane_, std::max(min_pane_, total - min_pane_ - 6));
  return orientation_ == Orientation::Horizontal ? Rect{cr.x + first, cr.y, 6, cr.h} : Rect{cr.x, cr.y + first, cr.w, 6};
}

void Split::layout() {
  if (children_.size() < 2) return View::layout();
  const Rect cr = content_rect();
  const Rect h = handle_rect();
  if (orientation_ == Orientation::Horizontal) {
    children_[0]->set_frame({cr.x, cr.y, h.x - cr.x, cr.h});
    children_[1]->set_frame({h.right(), cr.y, cr.right() - h.right(), cr.h});
  } else {
    children_[0]->set_frame({cr.x, cr.y, cr.w, h.y - cr.y});
    children_[1]->set_frame({cr.x, h.bottom(), cr.w, cr.bottom() - h.bottom()});
  }
  for (auto& c : children_) c->layout();
}

void Split::paint_overlay(Painter& p) {
  const Theme& t = th();
  const Rect h = handle_rect();
  const Color c = dragging_ || handle_hover_ ? t.accent : t.border;
  if (orientation_ == Orientation::Horizontal) p.fill_rect({h.x + 2.5f, h.y, 1, h.h}, c);
  else p.fill_rect({h.x, h.y + 2.5f, h.w, 1}, c);
}

bool Split::hit_test_children(Point local) const { return !handle_rect().outset(2).contains(local); }

bool Split::on_event(InputEvent& e) {
  const bool horiz = orientation_ == Orientation::Horizontal;
  const bool over = handle_rect().outset(2).contains(e.pos);
  switch (e.type) {
    case InputEvent::MouseMove:
      if (dragging_) {
        const float d = (horiz ? e.pos.x : e.pos.y) - drag_from_;
        pane_ = keep_second_ ? pane_from_ - d : pane_from_ + d;
        const Rect cr = content_rect();
        const float total = horiz ? cr.w : cr.h;
        pane_ = std::clamp(pane_, min_pane_, std::max(min_pane_, total - min_pane_ - 6));
        invalidate_layout();
        return true;
      }
      if (over != handle_hover_) {
        handle_hover_ = over;
        invalidate();
      }
      cursor_ = over ? (horiz ? Cursor::SizeWE : Cursor::SizeNS) : Cursor::Arrow;
      return false;
    case InputEvent::MouseDown:
      if (over && e.button == MouseButton::Left) {
        dragging_ = true;
        drag_from_ = horiz ? e.pos.x : e.pos.y;
        pane_from_ = pane_;
        return true;
      }
      return false;
    case InputEvent::MouseUp:
      if (dragging_) {
        dragging_ = false;
        invalidate();
        return true;
      }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Divider / ImageView
// ═════════════════════════════════════════════════════════════════════════════
Size Divider::measure_content(Size) { return orientation_ == Orientation::Horizontal ? Size{1, 1} : Size{1, 1}; }

void Divider::paint_content(Painter& p) {
  const Rect cr = content_rect();
  if (orientation_ == Orientation::Horizontal) p.fill_rect({cr.x, cr.y + cr.h / 2 - 0.5f, cr.w, 1}, th().border);
  else p.fill_rect({cr.x + cr.w / 2 - 0.5f, cr.y, 1, cr.h}, th().border);
}

Icon::Icon(std::string source) { set_source(std::move(source)); }

void Icon::set_source(std::string s) {
  source_ = std::move(s);
  if (is_svg_icon(source_)) svg_ = source_.find('<') != std::string::npos ? Svg::parse(source_) : Svg::load(source_);
  else svg_ = Svg();
  invalidate_layout();
}

Size Icon::measure_content(Size) {
  const float s = th().font.size * 1.25f;
  return {s, s};
}

void Icon::paint_content(Painter& p) {
  const Theme& t = th();
  const Color c = color_.value_or(is_enabled() ? t.text : t.text_disabled);
  const Rect r = content_rect();
  if (svg_.valid()) svg_.draw(p, r, c, tint_);
  else if (!source_.empty()) p.text(r, source_, t.font.with_size(std::min(r.w, r.h) * 0.8f), c, Align::Center);
}

ImageView::ImageView(Bind<Image> img) {
  bind(img, [](View& v, const Image& i) { static_cast<ImageView&>(v).set_image(i); });
}

Size ImageView::measure_content(Size) {
  return {static_cast<float>(image_.width()), static_cast<float>(image_.height())};
}

void ImageView::paint_content(Painter& p) {
  if (!image_.valid()) return;
  const Rect cr = content_rect();
  const float k = std::min(cr.w / image_.width(), cr.h / image_.height());
  const Size s{image_.width() * k, image_.height() * k};
  p.image(image_, align_rect(s, cr, Align::Center));
}

} // namespace pulseui
