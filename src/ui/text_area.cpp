// TextArea: multi-line plain text with word wrap.
#include <pulseui/ui/inputs.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace pulseui {

namespace {
std::size_t u8_prev(const std::string& s, std::size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  return i;
}
std::size_t u8_next(const std::string& s, std::size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}
bool is_word(unsigned char c) { return c >= 0x80 || std::isalnum(c) || c == '_'; }

constexpr float kBar = 10; // scrollbar track width
} // namespace

TextArea::TextArea() {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::IBeam;
  style_.padding = Insets(10, 7);
  style_.clip = true;
}

TextArea::TextArea(Model<std::string>& model) : TextArea() {
  model_ = &model;
  bind(Bind<std::string>(model), [](View& v, const std::string& s) { static_cast<TextArea&>(v).set_text(s); });
}

TextArea::TextArea(Bind<std::string> text) : TextArea() {
  bind(text, [](View& v, const std::string& s) { static_cast<TextArea&>(v).set_text(s); });
}

void TextArea::set_text(std::string t) {
  if (t == text_) return;
  text_ = std::move(t);
  caret_ = std::min(caret_, text_.size());
  sel_ = std::min(sel_, text_.size());
  while (caret_ > 0 && caret_ < text_.size() && (static_cast<unsigned char>(text_[caret_]) & 0xC0) == 0x80) --caret_;
  sel_ = std::min(sel_, text_.size());
  lines_dirty_ = true;
  invalidate_layout();
}

void TextArea::select(std::size_t anchor, std::size_t caret) {
  sel_ = std::min(anchor, text_.size());
  caret_ = std::min(caret, text_.size());
  want_x_ = -1;
  ensure_caret_visible();
  invalidate();
}

std::string TextArea::selected_text() const {
  const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
  return text_.substr(a, b - a);
}

std::size_t TextArea::visual_line_count() {
  ensure_lines();
  return lines_.size();
}

Rect TextArea::text_rect() const {
  Rect r = content_rect();
  r.w = std::max(0.f, r.w - kBar + 4); // room for the scrollbar
  return r;
}

Rect TextArea::track() const { return {frame_.w - kBar - 2, 3, kBar, std::max(0.f, frame_.h - 6)}; }

// ── wrapping ─────────────────────────────────────────────────────────────────

void TextArea::ensure_lines() {
  const float width = text_rect().w;
  if (!lines_dirty_ && std::abs(width - laid_width_) < 0.5f) return;
  lines_dirty_ = false;
  laid_width_ = width;
  lines_.clear();
  const Font& font = th().font;
  std::size_t pstart = 0;
  for (;;) {
    std::size_t pend = text_.find('\n', pstart);
    if (pend == std::string::npos) pend = text_.size();
    const std::string para = text_.substr(pstart, pend - pstart);
    const std::vector<float> stops = caret_stops(para, font); // one per code point boundary
    std::vector<std::size_t> bytes;                           // matching byte offsets
    bytes.reserve(stops.size());
    for (std::size_t i = 0;; i = u8_next(para, i)) {
      bytes.push_back(pstart + i);
      if (i >= para.size()) break;
    }
    const std::size_t n = std::min(bytes.size(), stops.size());
    std::size_t ls = 0; // index of the line start in bytes/stops
    while (true) {
      std::size_t le = n - 1; // end index (exclusive caret at the end)
      if (wrap_ && width > 20) {
        std::size_t last_break = 0;
        for (std::size_t k = ls + 1; k < n; ++k) {
          const char prev = text_[bytes[k] - 1];
          if (prev == ' ' || prev == '\t' || prev == '-') last_break = k;
          if (stops[k] - stops[ls] > width) {
            le = last_break > ls ? last_break : std::max(ls + 1, k - 1);
            break;
          }
        }
      }
      Line line;
      line.start = bytes[ls];
      line.end = bytes[le];
      for (std::size_t k = ls; k <= le; ++k) {
        line.bytes.push_back(bytes[k]);
        line.xs.push_back(stops[k] - stops[ls]);
      }
      lines_.push_back(std::move(line));
      if (le >= n - 1) break;
      ls = le;
    }
    if (pend >= text_.size()) break;
    pstart = pend + 1;
  }
  const float lh = line_height(font);
  sb_.content = static_cast<float>(lines_.size()) * lh;
  sb_.viewport = content_rect().h;
  sb_.clamp();
}

int TextArea::line_of(std::size_t byte) const {
  // a position at the wrap point belongs to the next line
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    const Line& l = lines_[i];
    const bool last_of_para = i + 1 == lines_.size() || lines_[i + 1].start != l.end;
    if (byte >= l.start && (byte < l.end || (byte == l.end && last_of_para))) return static_cast<int>(i);
  }
  return lines_.empty() ? 0 : static_cast<int>(lines_.size()) - 1;
}

float TextArea::x_of(int line, std::size_t byte) const {
  if (line < 0 || line >= static_cast<int>(lines_.size())) return 0;
  const Line& l = lines_[static_cast<std::size_t>(line)];
  for (std::size_t k = 0; k < l.bytes.size(); ++k)
    if (l.bytes[k] >= byte) return l.xs[k];
  return l.xs.empty() ? 0 : l.xs.back();
}

std::size_t TextArea::byte_at(int line, float x) const {
  if (lines_.empty()) return 0;
  line = std::clamp(line, 0, static_cast<int>(lines_.size()) - 1);
  const Line& l = lines_[static_cast<std::size_t>(line)];
  const bool wrapped = static_cast<std::size_t>(line) + 1 < lines_.size() && lines_[static_cast<std::size_t>(line) + 1].start == l.end;
  std::size_t best = l.start;
  float bestd = 1e9f;
  for (std::size_t k = 0; k < l.bytes.size(); ++k) {
    if (wrapped && k + 1 == l.bytes.size()) break; // the wrap point shows on the next line
    const float d = std::abs(l.xs[k] - x);
    if (d < bestd) {
      bestd = d;
      best = l.bytes[k];
    }
  }
  return best;
}

std::size_t TextArea::index_at(Point local) {
  ensure_lines();
  const Rect tr = text_rect();
  const float lh = line_height(th().font);
  const int line = static_cast<int>(std::floor((local.y - tr.y + sb_.offset) / lh));
  return byte_at(line, local.x - tr.x);
}

// ── editing ──────────────────────────────────────────────────────────────────

void TextArea::edit(std::size_t a, std::size_t b, const std::string& with, bool coalesce) {
  if (read_only_) return;
  if (!(coalesce && last_was_typing_)) {
    undo_.push_back({text_, caret_, sel_});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
  }
  redo_.clear();
  last_was_typing_ = coalesce;
  text_.replace(a, b - a, with);
  caret_ = sel_ = a + with.size();
  want_x_ = -1;
  lines_dirty_ = true;
  ensure_caret_visible();
  invalidate_layout();
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(text_);
  if (!alive.expired()) changed.emit(text_);
}

void TextArea::move_caret(std::size_t pos, bool extend, bool keep_x) {
  caret_ = std::min(pos, text_.size());
  if (!extend) sel_ = caret_;
  if (!keep_x) want_x_ = -1;
  last_was_typing_ = false;
  ensure_caret_visible();
  invalidate();
}

void TextArea::ensure_caret_visible() {
  ensure_lines();
  const float lh = line_height(th().font);
  const float y = static_cast<float>(line_of(caret_)) * lh;
  const float vh = content_rect().h;
  if (vh <= 0) return;
  if (y < sb_.offset) sb_.offset = y;
  else if (y + lh > sb_.offset + vh) sb_.offset = y + lh - vh;
  sb_.clamp();
}

// ── layout & paint ───────────────────────────────────────────────────────────

Size TextArea::measure_content(Size avail) {
  const float lh = line_height(th().font);
  const float w = std::isfinite(avail.w) ? std::max(80.f, avail.w) : 240.f;
  return {std::min(w, 240.f), lh * static_cast<float>(rows_)};
}

void TextArea::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  const bool f = focused();
  p.fill_rect(r, read_only_ ? t.surface_alt : t.input, t.radius);
  p.stroke_rect(r, f ? t.border_focus : (hovered_ ? t.border.lighter(0.15f) : t.border), f ? 1.5f : 1.f, t.radius);

  ensure_lines();
  sb_.viewport = content_rect().h;
  sb_.clamp();
  const Rect tr = text_rect();
  const float lh = line_height(t.font);
  p.save();
  p.clip(content_rect());
  if (text_.empty() && !placeholder_.empty()) {
    p.text({tr.x, tr.y, tr.w, tr.h}, placeholder_, t.font, t.text_disabled, Align::Left | Align::Top, true);
  }
  const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
  const int first = std::max(0, static_cast<int>(sb_.offset / lh));
  const int last = std::min(static_cast<int>(lines_.size()) - 1, static_cast<int>((sb_.offset + tr.h) / lh) + 1);
  for (int i = first; i <= last; ++i) {
    const Line& l = lines_[static_cast<std::size_t>(i)];
    const float y = tr.y + static_cast<float>(i) * lh - sb_.offset;
    if (f && a != b && b > l.start && a <= l.end) {
      const float x0 = x_of(i, std::max(a, l.start));
      float x1 = x_of(i, std::min(b, l.end));
      if (b > l.end) x1 += 6; // the selection continues past the line end
      p.fill_rect({tr.x + x0, y, std::max(2.f, x1 - x0), lh}, t.selection);
    }
    if (l.end > l.start) {
      std::string s = text_.substr(l.start, l.end - l.start);
      while (!s.empty() && (s.back() == '\r')) s.pop_back();
      p.text({tr.x, y, tr.w + 400, lh}, s, t.font, t.text, Align::LeftCenter);
    }
  }
  if (f && !read_only_) {
    const int cl = line_of(caret_);
    const float cx = std::round(tr.x + x_of(cl, caret_));
    const float cy = tr.y + static_cast<float>(cl) * lh - sb_.offset;
    p.fill_rect({cx, cy, 1.5f, lh}, t.text);
  }
  p.restore();
}

void TextArea::paint_overlay(Painter& p) {
  if (sb_.max() > 0) sb_.paint(p, track());
}

// ── input ────────────────────────────────────────────────────────────────────

bool TextArea::on_event(InputEvent& e) {
  const float lh = line_height(th().font);
  switch (e.type) {
    case InputEvent::Wheel:
      ensure_lines();
      if (sb_.event(e, track(), lh * 3)) {
        invalidate();
        return true;
      }
      return false;
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return true;
      if (sb_.event(e, track(), lh)) {
        invalidate();
        return true;
      }
      const std::size_t at = index_at(e.pos);
      if (e.clicks == 2) { // a word
        std::size_t s = at, en = at;
        while (s > 0 && is_word(static_cast<unsigned char>(text_[s - 1]))) --s;
        while (en < text_.size() && is_word(static_cast<unsigned char>(text_[en]))) ++en;
        sel_ = s;
        caret_ = en;
        invalidate();
        return true;
      }
      if (e.clicks >= 3) { // the paragraph
        std::size_t s = text_.rfind('\n', at == 0 ? 0 : at - 1);
        s = s == std::string::npos ? 0 : s + 1;
        std::size_t en = text_.find('\n', at);
        sel_ = s;
        caret_ = en == std::string::npos ? text_.size() : en;
        invalidate();
        return true;
      }
      move_caret(at, (e.mods & Mod::Shift) != 0);
      selecting_ = true;
      return true;
    }
    case InputEvent::MouseMove:
      if (sb_.dragging) {
        sb_.event(e, track(), lh);
        invalidate();
        return true;
      }
      if (selecting_) {
        caret_ = index_at(e.pos);
        ensure_caret_visible();
        invalidate();
        return true;
      }
      return false;
    case InputEvent::MouseUp:
      if (sb_.dragging) sb_.event(e, track(), lh);
      selecting_ = false;
      return true;
    case InputEvent::Text: {
      if (read_only_) return true;
      const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
      // typing is undone a word at a time: a space starts a new undo step
      edit(a, b, e.text, !e.text.empty() && e.text[0] != ' ' && e.text[0] != '	');
      return true;
    }
    case InputEvent::KeyDown: {
      ensure_lines();
      const bool shift = (e.mods & Mod::Shift) != 0;
      const bool primary = e.primary();
      const std::size_t a = std::min(caret_, sel_), b = std::max(caret_, sel_);
      auto vertical = [&](int dl) {
        const int line = line_of(caret_);
        if (want_x_ < 0) want_x_ = x_of(line, caret_);
        const int target = line + dl;
        if (target < 0) { move_caret(0, shift, true); return; }
        if (target >= static_cast<int>(lines_.size())) { move_caret(text_.size(), shift, true); return; }
        move_caret(byte_at(target, want_x_), shift, true);
      };
      switch (e.key) {
        case Key::Left: {
          if (!shift && a != b) { move_caret(a, false); return true; }
          std::size_t p = u8_prev(text_, caret_);
          if (primary) {
            while (p > 0 && !is_word(static_cast<unsigned char>(text_[p]))) --p;
            while (p > 0 && is_word(static_cast<unsigned char>(text_[p - 1]))) --p;
          }
          move_caret(p, shift);
          return true;
        }
        case Key::Right: {
          if (!shift && a != b) { move_caret(b, false); return true; }
          std::size_t p = u8_next(text_, caret_);
          if (primary) {
            while (p < text_.size() && !is_word(static_cast<unsigned char>(text_[p]))) ++p;
            while (p < text_.size() && is_word(static_cast<unsigned char>(text_[p]))) ++p;
          }
          move_caret(p, shift);
          return true;
        }
        case Key::Up: vertical(-1); return true;
        case Key::Down: vertical(1); return true;
        case Key::PageUp: vertical(-std::max(1, static_cast<int>(content_rect().h / lh) - 1)); return true;
        case Key::PageDown: vertical(std::max(1, static_cast<int>(content_rect().h / lh) - 1)); return true;
        case Key::Home:
          if (primary) move_caret(0, shift);
          else if (!lines_.empty()) move_caret(lines_[static_cast<std::size_t>(line_of(caret_))].start, shift);
          return true;
        case Key::End:
          if (primary) move_caret(text_.size(), shift);
          else if (!lines_.empty()) {
            const int l = line_of(caret_);
            const Line& ln = lines_[static_cast<std::size_t>(l)];
            const bool wrapped = static_cast<std::size_t>(l) + 1 < lines_.size() && lines_[static_cast<std::size_t>(l) + 1].start == ln.end;
            move_caret(wrapped ? u8_prev(text_, ln.end) : ln.end, shift);
          }
          return true;
        case Key::Enter:
          if (read_only_) return true;
          edit(a, b, "\n");
          return true;
        case Key::Backspace:
          if (read_only_) return true;
          if (a != b) edit(a, b, "");
          else if (caret_ > 0) {
            std::size_t p = u8_prev(text_, caret_);
            if (primary)
              while (p > 0 && is_word(static_cast<unsigned char>(text_[p - 1]))) --p;
            edit(p, caret_, "");
          }
          return true;
        case Key::Delete:
          if (read_only_) return true;
          if (a != b) edit(a, b, "");
          else if (caret_ < text_.size()) edit(caret_, u8_next(text_, caret_), "");
          return true;
        case Key::Tab:
          return false; // focus navigation
        case Key::A:
          if (!primary) return true;
          sel_ = 0;
          caret_ = text_.size();
          invalidate();
          return true;
        case Key::C:
        case Key::X:
          if (!primary) return true;
          if (a != b) {
            App::set_clipboard(text_.substr(a, b - a));
            if (e.key == Key::X && !read_only_) edit(a, b, "");
          }
          return true;
        case Key::V:
          if (!primary) return true;
          if (!read_only_) {
            std::string clip = App::clipboard();
            clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
            edit(a, b, clip);
          }
          return true;
        case Key::Z:
        case Key::Y: {
          if (!primary) return true;
          const bool redo = e.key == Key::Y || shift;
          auto& from = redo ? redo_ : undo_;
          auto& to = redo ? undo_ : redo_;
          if (from.empty() || read_only_) return true;
          to.push_back({text_, caret_, sel_});
          Snapshot s = from.back();
          from.pop_back();
          text_ = s.text;
          caret_ = s.caret;
          sel_ = s.sel;
          last_was_typing_ = false;
          lines_dirty_ = true;
          ensure_caret_visible();
          invalidate_layout();
          std::weak_ptr<bool> alive = alive_;
          if (model_) model_->set(text_);
          if (!alive.expired()) changed.emit(text_);
          return true;
        }
        default:
          return e.key >= Key::A && e.key <= Key::Num9 && !primary; // printable keys arrive as Text
      }
    }
    default:
      return false;
  }
}

void TextArea::on_focus(bool f) {
  if (!f) {
    sel_ = caret_;
    last_was_typing_ = false;
  }
  invalidate();
}

} // namespace pulseui
