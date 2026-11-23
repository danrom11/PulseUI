#include <pulseui/ui/code_editor.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

namespace pulseui {

namespace {

constexpr float kTopPad = 4;
constexpr float kTextPad = 6;
constexpr std::size_t kMaxUndo = 300;

bool is_cont(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::size_t cp_count(std::string_view s) {
  std::size_t n = 0;
  for (char c : s)
    if (!is_cont(c)) ++n;
  return n;
}

std::size_t byte_of_cp(std::string_view s, std::size_t cp) {
  std::size_t i = 0;
  while (cp > 0 && i < s.size()) {
    ++i;
    while (i < s.size() && is_cont(s[i])) ++i;
    --cp;
  }
  return i;
}

std::size_t prev_cp(const std::string& s, std::size_t i) {
  if (i == 0) return 0;
  --i;
  while (i > 0 && is_cont(s[i])) --i;
  return i;
}

std::size_t next_cp(const std::string& s, std::size_t i) {
  if (i >= s.size()) return s.size();
  ++i;
  while (i < s.size() && is_cont(s[i])) ++i;
  return i;
}

bool is_word(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) & 0x80);
}

std::size_t first_non_space(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  return i;
}

// Normalizes incoming text: CRLF -> LF, tabs -> 4 spaces.
std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> out(1);
  for (char c : text) {
    if (c == '\r') continue;
    if (c == '\n') out.emplace_back();
    else if (c == '\t') out.back().append(4 - (cp_count(out.back()) % 4), ' ');
    else out.back().push_back(c);
  }
  return out;
}

double now_s() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

CodeEditor::CodeEditor() {
  focusable_ = true;
  cursor_ = Cursor::IBeam;
  style_.clip = true;
  // right click: Undo / Cut / Copy / Paste ... (.context_menu(...) replaces it)
  extras().context_menu = [this] { return standard_menu(); };
}

// ── clipboard / undo, also used by the standard context menu ──────────────────

void CodeEditor::select_all() {
  anchor_ = {0, 0};
  caret_ = {lines_.size() - 1, lines_.back().size()};
  invalidate();
}

void CodeEditor::copy() {
  if (has_selection()) App::set_clipboard(selected_text());
}

void CodeEditor::cut() {
  if (!has_selection()) return;
  App::set_clipboard(selected_text());
  if (read_only_) return;
  push_undo(0);
  erase_selection();
  edited();
}

void CodeEditor::paste() {
  if (read_only_) return;
  push_undo(0);
  insert(App::clipboard());
  edited();
}

void CodeEditor::undo_redo(bool redo) {
  if (read_only_) return;
  auto& from = redo ? redo_ : undo_;
  auto& to = redo ? undo_ : redo_;
  if (from.empty()) return;
  to.push_back({lines_, caret_, anchor_});
  Snapshot s = std::move(from.back());
  from.pop_back();
  lines_ = std::move(s.lines);
  caret_ = s.caret;
  anchor_ = s.anchor;
  last_edit_kind_ = 0;
  edited();
}

MenuItems CodeEditor::standard_menu() {
  MenuItems m;
  if (!read_only_) {
    m.push_back(Action(i18n_undo_, [this] { undo(); }).shortcut("Ctrl+Z").enabled(can_undo()));
    m.push_back(Action(i18n_redo_, [this] { redo(); }).shortcut("Ctrl+Shift+Z").enabled(can_redo()));
    m.push_back(Separator());
    m.push_back(Action(i18n_cut_, [this] { cut(); }).shortcut("Ctrl+X").enabled(has_selection()));
  }
  m.push_back(Action(i18n_copy_, [this] { copy(); }).shortcut("Ctrl+C").enabled(has_selection()));
  if (!read_only_) m.push_back(Action(i18n_paste_, [this] { paste(); }).shortcut("Ctrl+V"));
  m.push_back(Separator());
  m.push_back(Action(i18n_select_all_, [this] { select_all(); }).shortcut("Ctrl+A"));
  return m;
}

CodeEditor::CodeEditor(Model<std::string>& model) : CodeEditor() { bind_model(model); }

void CodeEditor::bind_model(Model<std::string>& m) {
  model_ = &m;
  bind(Bind<std::string>(m), [](View& v, const std::string& s) { static_cast<CodeEditor&>(v).set_text(s); });
}

// ── text access ──────────────────────────────────────────────────────────────

std::string CodeEditor::text() const {
  std::string out;
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    if (i) out.push_back('\n');
    out += lines_[i];
  }
  return out;
}

void CodeEditor::set_text(std::string_view t) {
  auto lines = split_lines(t);
  if (lines == lines_) return;
  lines_ = std::move(lines);
  caret_.line = std::min(caret_.line, lines_.size() - 1);
  caret_.col = std::min(caret_.col, lines_[caret_.line].size());
  anchor_ = caret_;
  undo_.clear();
  redo_.clear();
  update_matches();
  hide_hover();
  clamp_scroll();
  invalidate();
}

void CodeEditor::append(std::string_view t) {
  metrics();
  const float h = text_area().h;
  const bool follow = scroll_.y + h >= static_cast<float>(lines_.size()) * line_h_ - line_h_ * 2;
  auto parts = split_lines(t);
  lines_.back() += parts[0];
  for (std::size_t i = 1; i < parts.size(); ++i) lines_.push_back(std::move(parts[i]));
  if (follow) scroll_.y = std::max(0.f, static_cast<float>(lines_.size()) * line_h_ + kTopPad * 2 - h);
  invalidate();
}

void CodeEditor::set_diagnostics(std::vector<Diagnostic> d) {
  diags_ = std::move(d);
  invalidate();
}

void CodeEditor::go_to(int line, int column) {
  metrics();
  caret_.line = static_cast<std::size_t>(std::clamp(line - 1, 0, static_cast<int>(lines_.size()) - 1));
  caret_.col = byte_of_cp(lines_[caret_.line], static_cast<std::size_t>(std::max(0, column - 1)));
  anchor_ = caret_;
  // center the line
  scroll_.y = std::max(0.f, static_cast<float>(caret_.line) * line_h_ - text_area().h / 2);
  clamp_scroll();
  focus();
  invalidate();
}

// ── geometry ─────────────────────────────────────────────────────────────────

void CodeEditor::metrics() {
  Font f = th().font_mono;
  f.size = th().font.size;
  char_w_ = measure_text("MMMMMMMMMM", f, 0).w / 10.f;
  line_h_ = std::ceil(line_height(f)) + 3;
}

void CodeEditor::set_execution_line(int line) {
  if (line == exec_line_) return;
  exec_line_ = line;
  if (line > 0 && static_cast<std::size_t>(line) <= lines_.size()) {
    // scroll it into view without moving the caret
    const float y = kTopPad + static_cast<float>(line - 1) * line_h_;
    const float h = frame_.h;
    if (h > 0 && (y < scroll_.y || y + line_h_ > scroll_.y + h)) {
      scroll_.y = y - h / 3;
      clamp_scroll();
    }
  }
  invalidate();
}

float CodeEditor::gutter_width() const {
  if (!line_numbers_) return 10;
  const int digits = std::max(3, static_cast<int>(std::to_string(lines_.size()).size()));
  return std::ceil(static_cast<float>(digits) * char_w_ + 30);
}

Rect CodeEditor::text_area() const {
  const Rect b = bounds();
  const float g = gutter_width();
  return {g, 0, std::max(0.f, b.w - g), b.h};
}

float CodeEditor::x_of(std::size_t line, std::size_t col) const {
  const std::string& s = lines_[line];
  return text_area().x + kTextPad - scroll_.x +
         static_cast<float>(cp_count(std::string_view(s).substr(0, std::min(col, s.size())))) * char_w_;
}

CodeEditor::Pos CodeEditor::pos_at(Point p) const {
  const float fy = (p.y + scroll_.y - kTopPad) / line_h_;
  Pos r;
  r.line = static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(lines_.size()) - 1));
  const float fx = (p.x - text_area().x - kTextPad + scroll_.x) / char_w_;
  const auto cp = static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(fx))));
  r.col = byte_of_cp(lines_[r.line], cp);
  return r;
}

void CodeEditor::clamp_scroll() {
  const Rect a = text_area();
  std::size_t longest = 0;
  for (auto& l : lines_) longest = std::max(longest, cp_count(l));
  const float max_y = std::max(0.f, static_cast<float>(lines_.size()) * line_h_ + kTopPad * 2 - a.h + line_h_ * 3);
  const float max_x = std::max(0.f, static_cast<float>(longest) * char_w_ + kTextPad * 2 + 40 - a.w);
  scroll_.y = std::clamp(scroll_.y, 0.f, max_y);
  scroll_.x = std::clamp(scroll_.x, 0.f, max_x);
}

void CodeEditor::ensure_visible() {
  const Rect a = text_area();
  const float top = static_cast<float>(caret_.line) * line_h_;
  if (top < scroll_.y) scroll_.y = top;
  if (top + line_h_ + kTopPad * 2 > scroll_.y + a.h) scroll_.y = top + line_h_ + kTopPad * 2 - a.h;
  const float cx = static_cast<float>(cp_count(std::string_view(lines_[caret_.line]).substr(0, caret_.col))) * char_w_;
  if (cx < scroll_.x) scroll_.x = std::max(0.f, cx - char_w_ * 4);
  if (cx > scroll_.x + a.w - kTextPad * 2 - char_w_ * 2) scroll_.x = cx - a.w + kTextPad * 2 + char_w_ * 6;
  clamp_scroll();
}

// ── selection / editing ──────────────────────────────────────────────────────

std::pair<CodeEditor::Pos, CodeEditor::Pos> CodeEditor::selection() const {
  return caret_ < anchor_ ? std::make_pair(caret_, anchor_) : std::make_pair(anchor_, caret_);
}

std::string CodeEditor::selected_text() const {
  auto [a, b] = selection();
  if (a.line == b.line) return lines_[a.line].substr(a.col, b.col - a.col);
  std::string out = lines_[a.line].substr(a.col);
  for (std::size_t l = a.line + 1; l < b.line; ++l) out += "\n" + lines_[l];
  out += "\n" + lines_[b.line].substr(0, b.col);
  return out;
}

void CodeEditor::push_undo(int kind) {
  static double last = 0;
  const double t = now_s();
  // consecutive typing / deleting within a second becomes one undo step
  if (kind != 0 && kind == last_edit_kind_ && t - last < 1.0 && !undo_.empty()) {
    last = t;
    return;
  }
  last = t;
  last_edit_kind_ = kind;
  undo_.push_back({lines_, caret_, anchor_});
  if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
  redo_.clear();
}

void CodeEditor::edited() {
  update_matches();
  hide_hover();
  ensure_visible();
  invalidate();
  std::weak_ptr<bool> alive = alive_;
  const std::string t = text();
  if (model_) model_->set(t);
  if (!alive.expired()) changed.emit(t);
}

void CodeEditor::erase_selection() {
  if (!has_selection()) return;
  auto [a, b] = selection();
  std::string tail = lines_[b.line].substr(b.col);
  lines_[a.line].erase(a.col);
  lines_[a.line] += tail;
  lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(a.line) + 1, lines_.begin() + static_cast<std::ptrdiff_t>(b.line) + 1);
  caret_ = anchor_ = a;
}

void CodeEditor::insert(std::string_view s) {
  erase_selection();
  auto parts = split_lines(s);
  std::string tail = lines_[caret_.line].substr(caret_.col);
  lines_[caret_.line].erase(caret_.col);
  lines_[caret_.line] += parts[0];
  for (std::size_t i = 1; i < parts.size(); ++i)
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(caret_.line + i), parts[i]);
  caret_.line += parts.size() - 1;
  caret_.col = lines_[caret_.line].size();
  lines_[caret_.line] += tail;
  anchor_ = caret_;
}

void CodeEditor::move_caret(Pos p, bool select) {
  caret_ = p;
  if (!select) anchor_ = p;
  preferred_col_cp_ = cp_count(std::string_view(lines_[caret_.line]).substr(0, caret_.col));
  ensure_visible();
  invalidate();
  refresh_hint(false); // follows the caret while open (closes outside the call)
}

void CodeEditor::indent_selection(bool unindent) {
  auto [a, b] = selection();
  const std::size_t last = (b.col == 0 && b.line > a.line) ? b.line - 1 : b.line;
  for (std::size_t l = a.line; l <= last; ++l) {
    if (unindent) {
      const std::size_t n = std::min<std::size_t>(4, first_non_space(lines_[l]));
      lines_[l].erase(0, n);
      if (l == caret_.line) caret_.col = caret_.col >= n ? caret_.col - n : 0;
      if (l == anchor_.line) anchor_.col = anchor_.col >= n ? anchor_.col - n : 0;
    } else {
      lines_[l].insert(0, "    ");
      if (l == caret_.line && caret_.col > 0) caret_.col += 4;
      if (l == anchor_.line && anchor_.col > 0) anchor_.col += 4;
    }
  }
}

// ── layout / paint ───────────────────────────────────────────────────────────

Size CodeEditor::measure_content(Size) { return {320, 200}; }

void CodeEditor::paint(Painter& p) {
  metrics();
  clamp_scroll();
  const Theme& t = th();
  const Rect b = bounds();
  const Rect a = text_area();
  Font f = t.font_mono;
  f.size = t.font.size;
  const Color text_color = t.text;

  p.fill_rect(b, t.input);
  p.fill_rect({0, 0, a.x, b.h}, t.surface_alt);
  p.fill_rect({a.x - 1, 0, 1, b.h}, t.border);

  const std::size_t first = static_cast<std::size_t>(std::max(0.f, (scroll_.y - kTopPad) / line_h_));
  const std::size_t last = std::min(lines_.size(), first + static_cast<std::size_t>(b.h / line_h_) + 2);

  // block comment state up to the first visible line
  bool in_comment = false;
  if (highlighter_)
    for (std::size_t l = 0; l < first; ++l) highlighter_(lines_[l], in_comment);

  auto [sa, sb] = selection();
  const bool sel = has_selection();
  const bool show_caret = focused();

  p.save();
  p.clip({a.x, 0, a.w, b.h});
  // search matches (the selected one is outlined)
  if (!matches_.empty()) {
    auto it = std::lower_bound(matches_.begin(), matches_.end(), first,
                               [](const std::pair<Pos, Pos>& m, std::size_t l) { return m.first.line < l; });
    for (; it != matches_.end() && it->first.line < last; ++it) {
      const float y = kTopPad + static_cast<float>(it->first.line) * line_h_ - scroll_.y;
      const Rect r{x_of(it->first.line, it->first.col), y + 1, std::max(char_w_ * 0.5f, x_of(it->first.line, it->second.col) - x_of(it->first.line, it->first.col)), line_h_ - 2};
      const bool current = sel && it->first == sa && it->second == sb;
      p.fill_rect(r, t.warning.with_alpha(current ? 0.45f : 0.22f), 2);
      if (current) p.stroke_rect(r, t.warning, 1, 2);
    }
  }
  for (std::size_t l = first; l < last; ++l) {
    const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y;
    const std::string& line = lines_[l];
    if (show_caret && !sel && l == caret_.line && !read_only_) p.fill_rect({a.x, y, a.w, line_h_}, t.text.with_alpha(0.045f));
    if (exec_line_ > 0 && l + 1 == static_cast<std::size_t>(exec_line_)) p.fill_rect({a.x, y, a.w, line_h_}, t.warning.with_alpha(0.22f));
    if (sel && l >= sa.line && l <= sb.line) {
      const float x0 = l == sa.line ? x_of(l, sa.col) : a.x + kTextPad - scroll_.x;
      const float x1 = l == sb.line ? x_of(l, sb.col) : x_of(l, line.size()) + char_w_;
      p.fill_rect({x0, y, std::max(0.f, x1 - x0), line_h_}, t.selection);
    }
    // syntax colored segments
    std::vector<TextSpan> spans;
    if (highlighter_) spans = highlighter_(line, in_comment);
    auto draw_seg = [&](std::size_t from, std::size_t to, Color c) {
      if (to <= from) return;
      const std::string_view seg = std::string_view(line).substr(from, to - from);
      const float x = x_of(l, from);
      p.text({x, y, static_cast<float>(cp_count(seg) + 2) * char_w_, line_h_}, seg, f, c, Align::LeftCenter);
    };
    std::size_t pos = 0;
    for (auto& s : spans) {
      const std::size_t s0 = std::min(s.begin, line.size()), s1 = std::min(s.end, line.size());
      if (s0 < pos) continue;
      draw_seg(pos, s0, text_color);
      draw_seg(s0, s1, s.color);
      pos = s1;
    }
    draw_seg(pos, line.size(), text_color);
  }

  // diagnostics: squiggle under the token + message at the end of the line
  std::vector<std::size_t> messaged;
  for (auto& d : diags_) {
    if (d.line < 1 || static_cast<std::size_t>(d.line) > lines_.size()) continue;
    const std::size_t l = static_cast<std::size_t>(d.line - 1);
    if (l < first || l >= last) continue;
    const std::string& line = lines_[l];
    const Color c = d.severity == Diagnostic::Error ? t.danger : d.severity == Diagnostic::Warning ? t.warning : t.text_dim;
    std::size_t c0 = d.column > 0 ? byte_of_cp(line, static_cast<std::size_t>(d.column - 1)) : first_non_space(line);
    c0 = std::min(c0, line.size());
    std::size_t c1 = c0;
    if (d.column > 0) {
      while (c1 < line.size() && is_word(line[c1])) ++c1;
      if (c1 == c0) c1 = next_cp(line, c0);
    } else {
      c1 = line.find_last_not_of(' ');
      c1 = c1 == std::string::npos ? line.size() : c1 + 1; // no column: the whole statement
    }
    const float x0 = x_of(l, c0), x1 = std::max(x0 + char_w_, x_of(l, c1));
    const float yb = kTopPad + static_cast<float>(l + 1) * line_h_ - scroll_.y - 2;
    std::vector<Point> wave;
    for (float x = x0, k = 0; x <= x1; x += 2.5f, k += 1) wave.push_back({x, yb + (static_cast<int>(k) % 2 ? -1.5f : 0.f)});
    p.polyline(wave, c, 1.2f);
    if (std::find(messaged.begin(), messaged.end(), l) == messaged.end()) {
      messaged.push_back(l);
      const float mx = x_of(l, line.size()) + char_w_ * 3;
      const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y;
      const Size ms = measure_text(d.message, t.font_small, 0);
      p.fill_rect({mx - 6, y + 2, ms.w + 12, line_h_ - 4}, c.with_alpha(0.12f), 3);
      p.text({mx, y, ms.w + 4, line_h_}, d.message, t.font_small, c, Align::LeftCenter);
    }
  }

  // the bracket at the caret and its partner
  if (show_caret && !sel) {
    Pos ba, bb;
    if (bracket_match(ba, bb))
      for (const Pos& q : {ba, bb}) {
        const float y = kTopPad + static_cast<float>(q.line) * line_h_ - scroll_.y;
        const Rect r{x_of(q.line, q.col), y + 1, char_w_, line_h_ - 2};
        p.fill_rect(r, t.accent.with_alpha(0.16f), 2);
        p.stroke_rect(r, t.accent.with_alpha(0.55f), 1, 2);
      }
  }
  // Ctrl + hover: the word looks like a link (Ctrl+click asks for its definition)
  if (link_ && hover_target_ && hovered() && hover_a_.line < lines_.size()) {
    const float y = kTopPad + static_cast<float>(hover_a_.line + 1) * line_h_ - scroll_.y - 2;
    p.fill_rect({x_of(hover_a_.line, hover_a_.col), y, x_of(hover_b_.line, hover_b_.col) - x_of(hover_a_.line, hover_a_.col), 1}, t.accent);
  }

  if (show_caret) {
    const float cx = std::round(x_of(caret_.line, caret_.col));
    const float cy = kTopPad + static_cast<float>(caret_.line) * line_h_ - scroll_.y;
    p.fill_rect({cx, cy + 1, read_only_ ? 1.f : 2.f, line_h_ - 2}, read_only_ ? t.text_dim : t.accent);
  }
  p.restore();

  // gutter: line numbers + diagnostic markers
  if (line_numbers_) {
    for (std::size_t l = first; l < last; ++l) {
      const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y;
      const bool cur = l == caret_.line && show_caret;
      p.text({0, y, a.x - 10, line_h_}, std::to_string(l + 1), f, cur ? t.text : t.text_disabled, Align::RightCenter);
    }
  }
  // breakpoints and the execution arrow
  for (int bl : breakpoints_) {
    if (bl < 1 || static_cast<std::size_t>(bl) > lines_.size()) continue;
    const std::size_t l = static_cast<std::size_t>(bl - 1);
    if (l < first || l >= last) continue;
    const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y;
    p.fill_ellipse({3, y + (line_h_ - 11) / 2, 11, 11}, Color::hex(0xE5484D));
  }
  if (exec_line_ > 0 && static_cast<std::size_t>(exec_line_) <= lines_.size()) {
    const std::size_t l = static_cast<std::size_t>(exec_line_ - 1);
    if (l >= first && l < last) {
      const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y + line_h_ / 2;
      p.fill_polygon({{4, y - 5}, {13, y}, {4, y + 5}}, t.warning);
    }
  }
  for (auto& d : diags_) {
    if (d.line < 1 || static_cast<std::size_t>(d.line) > lines_.size()) continue;
    const std::size_t l = static_cast<std::size_t>(d.line - 1);
    if (l < first || l >= last) continue;
    const float y = kTopPad + static_cast<float>(l) * line_h_ - scroll_.y;
    const Color c = d.severity == Diagnostic::Error ? t.danger : d.severity == Diagnostic::Warning ? t.warning : t.text_dim;
    p.fill_ellipse({5, y + (line_h_ - 8) / 2, 8, 8}, c);
  }

  // vertical scrollbar
  const float content_h = static_cast<float>(lines_.size()) * line_h_ + kTopPad * 2 + line_h_ * 3;
  if (content_h > b.h) {
    const float th_h = std::max(24.f, b.h * b.h / content_h);
    const float ty = (b.h - th_h) * (scroll_.y / std::max(1.f, content_h - b.h));
    p.fill_rect({b.w - 9, ty, 6, th_h}, t.scrollbar, 3);
    // error positions on the scrollbar track
    for (auto& d : diags_)
      if (d.severity == Diagnostic::Error && d.line >= 1)
        p.fill_rect({b.w - 10, (static_cast<float>(d.line) * line_h_ / content_h) * b.h, 8, 2}, t.danger);
  }
  if (!hint_.signature.empty() && focused()) paint_hint(p);
  if (!hover_text_.empty() && hovered() && !popup_open_) paint_hover(p);
  if (popup_open_) paint_popup(p);
}

// ── completion ───────────────────────────────────────────────────────────────

namespace {
constexpr int kPopupRows = 9;
constexpr float kPopupRowH = 22;
} // namespace

void CodeEditor::open_completion() { refresh_completion(true); }

void CodeEditor::close_completion() {
  if (!popup_open_) return;
  popup_open_ = false;
  invalidate();
}

void CodeEditor::refresh_completion(bool open_if_closed) {
  if (!completion_ || read_only_ || has_selection() || (!popup_open_ && !open_if_closed)) return;
  CompletionResult r = completion_(lines_, caret_.line, caret_.col);
  // nothing to offer, or the only candidate is exactly what is typed already
  const std::string typed = lines_[caret_.line].substr(std::min(r.replace_from, caret_.col), caret_.col - std::min(r.replace_from, caret_.col));
  if (r.items.empty() || (r.items.size() == 1 && r.items[0].label == typed)) {
    close_completion();
    return;
  }
  const bool same = popup_open_ && r.items == popup_.items;
  popup_ = std::move(r);
  popup_.replace_from = std::min(popup_.replace_from, caret_.col);
  if (!same) {
    popup_index_ = 0;
    popup_scroll_ = 0;
  }
  popup_open_ = true;
  invalidate();
}

void CodeEditor::accept_completion() {
  if (!popup_open_ || popup_.items.empty()) return;
  const CompletionItem& it = popup_.items[static_cast<std::size_t>(std::clamp(popup_index_, 0, static_cast<int>(popup_.items.size()) - 1))];
  std::string text = it.insert.empty() ? it.label : it.insert;
  const std::size_t mark = text.find("$0");
  std::string after;
  if (mark != std::string::npos) {
    after = text.substr(mark + 2);
    text.erase(mark);
  }
  push_undo(0);
  anchor_ = {caret_.line, popup_.replace_from};
  insert(text);
  if (!after.empty()) { // the caret stays at the $0 marker
    const Pos keep = caret_;
    insert(after);
    caret_ = anchor_ = keep;
  }
  popup_open_ = false;
  edited();
  const bool in_call = caret_.col > 0 && lines_[caret_.line][caret_.col - 1] == '(';
  refresh_hint(in_call);
}

// ── parameter hint ───────────────────────────────────────────────────────────

void CodeEditor::refresh_hint(bool open_if_closed) {
  if (!completion_ || read_only_ || (hint_.signature.empty() && !open_if_closed)) return;
  CompletionResult r = completion_(lines_, caret_.line, caret_.col);
  if (r.signature != hint_.signature || r.active_begin != hint_.active_begin) invalidate();
  hint_.signature = std::move(r.signature);
  hint_.active_begin = r.active_begin;
  hint_.active_end = r.active_end;
}

void CodeEditor::close_hint() {
  if (hint_.signature.empty()) return;
  hint_.signature.clear();
  invalidate();
}

void CodeEditor::paint_hint(Painter& p) {
  const Theme& t = th();
  const std::string& s = hint_.signature;
  const std::size_t b = std::min(hint_.active_begin, s.size()), e = std::clamp(hint_.active_end, b, s.size());
  const std::string pre = s.substr(0, b), act = s.substr(b, e - b), post = s.substr(e);
  Font bold = t.font_mono;
  bold.weight = FontWeight::Bold;
  const float w0 = measure_text(pre, t.font_mono, 0).w, w1 = measure_text(act, bold, 0).w,
              w2 = measure_text(post, t.font_mono, 0).w;
  const float w = w0 + w1 + w2 + 20, h = line_h_ + 6;
  const Rect bb = bounds();
  const float line_top = kTopPad + static_cast<float>(caret_.line) * line_h_ - scroll_.y;
  const float above = line_top - h - 2, below = line_top + line_h_ + 2;
  float y = above;
  if (popup_open_) {
    const Rect pr = popup_rect();
    if (pr.y < line_top) y = below; // the popup is above the line: the hint goes below
  } else if (above < 2) {
    y = below;
  }
  const float x = std::clamp(x_of(caret_.line, caret_.col) - 40, 4.f, std::max(4.f, bb.w - w - 12));
  const Rect r{x, y, w, h};
  p.fill_rect(r.outset(2).translated(0, 2), t.shadow.with_alpha(0.2f), t.radius + 2);
  p.fill_rect(r, t.surface_alt, t.radius_small);
  p.stroke_rect(r, t.border, 1, t.radius_small);
  float cx = x + 10;
  p.text({cx, y, w0 + 2, h}, pre, t.font_mono, t.text_dim, Align::LeftCenter);
  cx += w0;
  p.text({cx, y, w1 + 2, h}, act, bold, t.accent, Align::LeftCenter);
  cx += w1;
  p.text({cx, y, w2 + 2, h}, post, t.font_mono, t.text_dim, Align::LeftCenter);
}

Rect CodeEditor::popup_rect() const {
  float w = 220;
  const Theme& t = th();
  for (std::size_t i = 0; i < popup_.items.size() && i < 60; ++i) {
    const auto& it = popup_.items[i];
    w = std::max(w, measure_text(it.label, t.font, 0).w + measure_text(it.detail, t.font_small, 0).w + 64);
  }
  w = std::min(w, std::max(220.f, bounds().w - 40));
  const float rows = static_cast<float>(std::min<std::size_t>(popup_.items.size(), kPopupRows));
  const float h = rows * kPopupRowH + 6;
  float x = x_of(caret_.line, popup_.replace_from) - 26;
  float y = kTopPad + static_cast<float>(caret_.line + 1) * line_h_ - scroll_.y + 2;
  const Rect b = bounds();
  if (y + h > b.h - 4) y = y - line_h_ - h - 4; // flip above the line
  x = std::clamp(x, 4.f, std::max(4.f, b.w - w - 12));
  return {x, std::max(2.f, y), w, h};
}

void CodeEditor::paint_popup(Painter& p) {
  const Theme& t = th();
  const Rect r = popup_rect();
  p.fill_rect(r.outset(3).translated(0, 2), t.shadow.with_alpha(0.25f), t.radius + 3);
  p.fill_rect(r, t.surface, t.radius);
  p.stroke_rect(r, t.border, 1, t.radius);
  const int n = static_cast<int>(popup_.items.size());
  popup_scroll_ = std::clamp(popup_scroll_, std::max(0, popup_index_ - kPopupRows + 1), popup_index_);
  for (int row = 0; row < kPopupRows && popup_scroll_ + row < n; ++row) {
    const int i = popup_scroll_ + row;
    const auto& it = popup_.items[static_cast<std::size_t>(i)];
    const Rect rr{r.x + 3, r.y + 3 + static_cast<float>(row) * kPopupRowH, r.w - 6, kPopupRowH};
    if (i == popup_index_) p.fill_rect(rr, t.selection, t.radius_small);
    static const char* glyph[] = {"v", "f", "T", "k", "m", "\""};
    static const Color colors[] = {Color::hex(0x82AAFF), Color::hex(0xC792EA), Color::hex(0x4EC9B0),
                                   Color::hex(0x9AA3B2), Color::hex(0xF78C6C), Color::hex(0xC3E88D)};
    const Color kc = colors[static_cast<int>(it.kind)];
    p.fill_rect({rr.x + 4, rr.y + 4, 16, rr.h - 8}, kc.with_alpha(0.2f), 3);
    p.text({rr.x + 4, rr.y, 16, rr.h}, glyph[static_cast<int>(it.kind)], t.font_small, kc, Align::Center);
    const float dw = std::min(rr.w * 0.45f, measure_text(it.detail, t.font_small, 0).w + 4);
    p.text({rr.x + 26, rr.y, rr.w - 32 - dw, rr.h}, it.label, t.font, t.text, Align::LeftCenter);
    p.text({rr.right() - dw - 6, rr.y, dw, rr.h}, it.detail, t.font_small, t.text_dim, Align::RightCenter);
  }
  if (n > kPopupRows) { // scroll indicator
    const float track = r.h - 8;
    const float th_h = std::max(16.f, track * kPopupRows / static_cast<float>(n));
    const float ty = r.y + 4 + (track - th_h) * static_cast<float>(popup_scroll_) / static_cast<float>(n - kPopupRows);
    p.fill_rect({r.right() - 5, ty, 3, th_h}, t.scrollbar, 1.5f);
  }
}

// ── input ────────────────────────────────────────────────────────────────────

void CodeEditor::on_focus(bool f) {
  if (!f) {
    popup_open_ = false;
    hint_.signature.clear();
  }
  invalidate();
}

bool CodeEditor::on_event(InputEvent& e) {
  metrics();
  const Rect b = bounds();
  switch (e.type) {
    case InputEvent::Wheel: {
      hide_hover();
      if (popup_open_ && popup_rect().contains(e.pos)) {
        popup_scroll_ = std::clamp(popup_scroll_ - static_cast<int>(e.wheel_dy * 3), 0,
                                   std::max(0, static_cast<int>(popup_.items.size()) - kPopupRows));
        popup_index_ = std::clamp(popup_index_, popup_scroll_, popup_scroll_ + kPopupRows - 1);
        invalidate();
        return true;
      }
      if (e.mods & Mod::Shift) scroll_.x -= e.wheel_dy * char_w_ * 6;
      else scroll_.y -= e.wheel_dy * line_h_ * 3;
      scroll_.x -= e.wheel_dx * char_w_ * 6;
      clamp_scroll();
      invalidate();
      return true;
    }
    case InputEvent::MouseDown: {
      if (popup_open_) {
        const Rect pr = popup_rect();
        if (pr.contains(e.pos)) {
          popup_index_ = popup_scroll_ + static_cast<int>((e.pos.y - pr.y - 3) / kPopupRowH);
          accept_completion();
          return true;
        }
        close_completion();
      }
      close_hint();
      hide_hover();
      if (e.button == MouseButton::Right) {
        // the context menu acts on what was clicked: move the caret there,
        // unless the click is inside the selection (Copy / Cut keep working)
        if (e.pos.x >= text_area().x) {
          const Pos at = char_at(e.pos);
          const auto [s0, s1] = selection();
          if (!has_selection() || at < s0 || s1 < at) move_caret(at, false);
        }
        return true;
      }
      if (e.button != MouseButton::Left) return true;
      const float content_h = static_cast<float>(lines_.size()) * line_h_ + kTopPad * 2 + line_h_ * 3;
      if (e.pos.x > b.w - 12 && content_h > b.h) { // scrollbar
        dragging_thumb_ = true;
        thumb_grab_ = e.pos.y;
        scroll_.y = (e.pos.y / b.h) * content_h - b.h / 2;
        clamp_scroll();
        invalidate();
        return true;
      }
      Pos p = pos_at(e.pos);
      if (gutter_clicks_ && e.pos.x < text_area().x) { // a click in the gutter toggles a breakpoint
        breakpoint_toggled.emit(static_cast<int>(p.line) + 1);
        return true;
      }
      if (e.clicks == 1 && (e.mods & Mod::Primary) && !(e.mods & Mod::Shift) && e.pos.x >= text_area().x) {
        const Pos at = char_at(e.pos); // Ctrl+click: "go to definition" is the application's job
        move_caret(at, false);
        definition_requested.emit(at.line, at.col);
        return true;
      }
      if (e.clicks >= 2) { // select word
        const std::string& s = lines_[p.line];
        std::size_t a0 = p.col, a1 = p.col;
        while (a0 > 0 && is_word(s[a0 - 1])) --a0;
        while (a1 < s.size() && is_word(s[a1])) ++a1;
        anchor_ = {p.line, a0};
        caret_ = {p.line, a1};
        invalidate();
        return true;
      }
      move_caret(p, (e.mods & Mod::Shift) != 0);
      selecting_ = true;
      return true;
    }
    case InputEvent::MouseMove:
      if (dragging_thumb_) {
        const float content_h = static_cast<float>(lines_.size()) * line_h_ + kTopPad * 2 + line_h_ * 3;
        scroll_.y = (e.pos.y / b.h) * content_h - b.h / 2;
        clamp_scroll();
        invalidate();
        return true;
      }
      if (selecting_) {
        move_caret(pos_at(e.pos), true);
        return true;
      }
      track_hover(e);
      return false;
    case InputEvent::MouseUp:
      selecting_ = dragging_thumb_ = false;
      return true;
    case InputEvent::Text: {
      if (read_only_) return true;
      hide_hover();
      bool paired = false;
      std::string typed = e.text;
      if (auto_close_ && typed.size() == 1) {
        const char ch = typed[0];
        const std::string& l = lines_[caret_.line];
        const char next = caret_.col < l.size() ? l[caret_.col] : 0;
        const char prev = caret_.col > 0 ? l[caret_.col - 1] : 0;
        const bool opener = ch == '(' || ch == '[' || ch == '{';
        const bool quote = ch == '"' || ch == '\'';
        const char closer = ch == '(' ? ')' : ch == '[' ? ']' : ch == '{' ? '}' : ch;
        if ((ch == ')' || ch == ']' || ch == '}' || quote) && !has_selection() && next == ch) { // step over
          move_caret({caret_.line, caret_.col + 1}, false);
          close_completion();
          return true;
        }
        if ((opener || quote) && has_selection()) { // wrap the selection
          push_undo(0);
          insert(std::string(1, ch) + selected_text() + closer);
          edited();
          return true;
        }
        if (opener || quote) {
          const bool free_after = next == 0 || next == ' ' || next == ')' || next == ']' || next == '}' || next == ';' || next == ',';
          bool in_string = false;
          if (quote)
            for (std::size_t i = 0; i < caret_.col; ++i) {
              if (l[i] == '\\') { ++i; continue; }
              if (l[i] == ch) in_string = !in_string;
            }
          if (free_after && !(quote && (is_word(prev) || in_string))) {
            typed += closer;
            paired = true;
          }
        }
      }
      push_undo(1);
      insert(typed);
      if (paired) {
        --caret_.col;
        anchor_ = caret_;
      }
      edited();
      // open on '.', '->', '::', '"' and after two identifier characters; refilter while open
      const std::string& l = lines_[caret_.line];
      const char c = e.text.empty() ? 0 : e.text.back();
      std::size_t w = caret_.col;
      while (w > 0 && is_word(l[w - 1])) --w;
      const bool trigger = c == '.' || c == '"' || (c == '>' && caret_.col >= 2 && l[caret_.col - 2] == '-') ||
                           (c == ':' && caret_.col >= 2 && l[caret_.col - 2] == ':') || (is_word(c) && caret_.col - w >= 2);
      if (is_word(c) || trigger) refresh_completion(trigger);
      else close_completion();
      refresh_hint(c == '(' || c == ',');
      return true;
    }
    case InputEvent::KeyDown:
      break;
    default:
      return false;
  }

  // ── keys ──
  const bool shift = (e.mods & Mod::Shift) != 0;
  const bool primary = e.primary();
  hide_hover();
  if (e.key == Key::Enter && (e.mods & Mod::Alt)) return false; // left to the application (quick actions)
  if (primary && e.key == Key::Slash && !comment_.empty() && !read_only_) {
    toggle_comment();
    return true;
  }
  if (primary && e.key == Key::Space) {
    open_completion();
    return true;
  }
  if (popup_open_) {
    const int n = static_cast<int>(popup_.items.size());
    switch (e.key) {
      case Key::Up: popup_index_ = (popup_index_ + n - 1) % n; invalidate(); return true;
      case Key::Down: popup_index_ = (popup_index_ + 1) % n; invalidate(); return true;
      case Key::PageUp: popup_index_ = std::max(0, popup_index_ - kPopupRows); invalidate(); return true;
      case Key::PageDown: popup_index_ = std::min(n - 1, popup_index_ + kPopupRows); invalidate(); return true;
      case Key::Enter:
      case Key::Tab: accept_completion(); return true;
      case Key::Escape: close_completion(); close_hint(); return true;
      case Key::Backspace: break; // handled below, then refiltered
      default:
        if (e.key == Key::Left || e.key == Key::Right || e.key == Key::Home || e.key == Key::End) close_completion();
        break;
    }
  }
  const std::string& cur = lines_[caret_.line];
  const std::size_t page = static_cast<std::size_t>(std::max(1.f, b.h / line_h_ - 1));
  auto vertical = [&](long delta) {
    const long nl = std::clamp(static_cast<long>(caret_.line) + delta, 0L, static_cast<long>(lines_.size()) - 1);
    const std::size_t keep = preferred_col_cp_;
    Pos p{static_cast<std::size_t>(nl), byte_of_cp(lines_[static_cast<std::size_t>(nl)], keep)};
    move_caret(p, shift);
    preferred_col_cp_ = keep;
  };
  switch (e.key) {
    case Key::Left: {
      if (!shift && has_selection()) { move_caret(selection().first, false); return true; }
      Pos p = caret_;
      if (p.col > 0) {
        p.col = prev_cp(cur, p.col);
        if (primary) {
          while (p.col > 0 && cur[p.col] == ' ') p.col = prev_cp(cur, p.col);
          while (p.col > 0 && is_word(cur[p.col - 1])) --p.col;
        }
      } else if (p.line > 0) {
        --p.line;
        p.col = lines_[p.line].size();
      }
      move_caret(p, shift);
      return true;
    }
    case Key::Right: {
      if (!shift && has_selection()) { move_caret(selection().second, false); return true; }
      Pos p = caret_;
      if (p.col < cur.size()) {
        p.col = next_cp(cur, p.col);
        if (primary) {
          while (p.col < cur.size() && is_word(cur[p.col])) ++p.col;
          while (p.col < cur.size() && cur[p.col] == ' ') ++p.col;
        }
      } else if (p.line + 1 < lines_.size()) {
        ++p.line;
        p.col = 0;
      }
      move_caret(p, shift);
      return true;
    }
    case Key::Up: vertical(-1); return true;
    case Key::Down: vertical(1); return true;
    case Key::PageUp: vertical(-static_cast<long>(page)); return true;
    case Key::PageDown: vertical(static_cast<long>(page)); return true;
    case Key::Home:
      if (primary) move_caret({0, 0}, shift);
      else {
        const std::size_t fns = first_non_space(cur);
        move_caret({caret_.line, caret_.col == fns ? 0 : fns}, shift);
      }
      return true;
    case Key::End:
      if (primary) move_caret({lines_.size() - 1, lines_.back().size()}, shift);
      else move_caret({caret_.line, cur.size()}, shift);
      return true;
    case Key::A:
      if (!primary) return !read_only_;
      select_all();
      return true;
    case Key::C:
    case Key::X:
      if (!primary) return !read_only_;
      if (e.key == Key::X) cut();
      else copy();
      return true;
    default:
      break;
  }
  if (read_only_) return false;

  switch (e.key) {
    case Key::V:
      if (!primary) return true;
      paste();
      return true;
    case Key::Z:
    case Key::Y: {
      if (!primary) return true;
      if (e.key == Key::Y || shift) redo();
      else undo();
      return true;
    }
    case Key::Enter: {
      push_undo(0);
      erase_selection();
      const std::string& line = lines_[caret_.line];
      std::string indent(first_non_space(line), ' ');
      const std::string before = line.substr(0, caret_.col);
      const auto last_char = before.find_last_not_of(' ');
      const bool opened = last_char != std::string::npos &&
                          (before[last_char] == '{' || before[last_char] == '(' || before[last_char] == ':');
      const bool closes = opened && caret_.col < line.size() &&
                          ((before[last_char] == '{' && line[caret_.col] == '}') ||
                           (before[last_char] == '(' && line[caret_.col] == ')'));
      const std::string outer = indent;
      if (opened) indent += "    ";
      insert("\n" + indent);
      if (closes) { // {|} -> the closing brace moves to its own line below the caret
        const Pos keep = caret_;
        insert("\n" + outer);
        caret_ = anchor_ = keep;
      }
      close_hint();
      edited();
      return true;
    }
    case Key::Tab:
      push_undo(0);
      if (has_selection() && selection().first.line != selection().second.line) indent_selection(shift);
      else if (shift) indent_selection(true);
      else insert(std::string(4 - cp_count(std::string_view(lines_[caret_.line]).substr(0, caret_.col)) % 4, ' '));
      edited();
      return true;
    case Key::Backspace: {
      push_undo(2);
      if (has_selection()) erase_selection();
      else if (caret_.col > 0) {
        std::string& line = lines_[caret_.line];
        // inside leading indentation: remove up to the previous tab stop
        const std::size_t fns = first_non_space(line);
        std::size_t from = prev_cp(line, caret_.col);
        if (caret_.col <= fns && caret_.col >= 1) from = caret_.col - ((caret_.col - 1) % 4 + 1);
        std::size_t to = caret_.col;
        if (auto_close_ && to < line.size()) { // (|) -> |
          const char o = line[to - 1], c = line[to];
          if ((o == '(' && c == ')') || (o == '[' && c == ']') || (o == '{' && c == '}') || (o == '"' && c == '"') ||
              (o == '\'' && c == '\''))
            ++to;
        }
        line.erase(from, to - from);
        caret_.col = from;
        anchor_ = caret_;
      } else if (caret_.line > 0) {
        const std::size_t col = lines_[caret_.line - 1].size();
        lines_[caret_.line - 1] += lines_[caret_.line];
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(caret_.line));
        caret_ = anchor_ = {caret_.line - 1, col};
      }
      edited();
      refresh_completion(false);
      refresh_hint(false);
      return true;
    }
    case Key::Delete: {
      push_undo(2);
      if (has_selection()) erase_selection();
      else if (caret_.col < lines_[caret_.line].size()) {
        std::string& line = lines_[caret_.line];
        line.erase(caret_.col, next_cp(line, caret_.col) - caret_.col);
      } else if (caret_.line + 1 < lines_.size()) {
        lines_[caret_.line] += lines_[caret_.line + 1];
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(caret_.line) + 1);
      }
      edited();
      return true;
    }
    case Key::Escape:
      if (hint_.signature.empty()) return false;
      close_hint();
      return true;
    default:
      // printable keys arrive as Text events; keep them from triggering shortcuts
      return !primary && !(e.mods & Mod::Alt) && e.key >= Key::A && e.key <= Key::Num9;
  }
}

// ── ranges, comments, search ─────────────────────────────────────────────────

void CodeEditor::select(std::size_t l0, std::size_t c0, std::size_t l1, std::size_t c1) {
  metrics();
  auto at = [&](std::size_t l, std::size_t c) {
    Pos q;
    q.line = std::min(l, lines_.size() - 1);
    q.col = std::min(c, lines_[q.line].size());
    return q;
  };
  anchor_ = at(l0, c0);
  caret_ = at(l1, c1);
  preferred_col_cp_ = cp_count(std::string_view(lines_[caret_.line]).substr(0, caret_.col));
  ensure_visible();
  invalidate();
}

void CodeEditor::replace_range(std::size_t l0, std::size_t c0, std::size_t l1, std::size_t c1, std::string_view text) {
  if (read_only_) return;
  push_undo(0);
  select(l0, c0, l1, c1);
  insert(text);
  edited();
}

void CodeEditor::replace_all_text(std::string_view text) {
  auto lines = split_lines(text);
  if (lines == lines_) return;
  push_undo(0);
  lines_ = std::move(lines);
  caret_.line = std::min(caret_.line, lines_.size() - 1);
  caret_.col = std::min(caret_.col, lines_[caret_.line].size());
  anchor_ = caret_;
  edited();
}

std::pair<std::size_t, std::size_t> CodeEditor::word_range(std::size_t line, std::size_t col) const {
  if (line >= lines_.size()) return {0, 0};
  const std::string& s = lines_[line];
  std::size_t a = std::min(col, s.size()), b = a;
  while (a > 0 && is_word(s[a - 1])) --a;
  while (b < s.size() && is_word(s[b])) ++b;
  return {a, b};
}

std::string CodeEditor::word_at(std::size_t line, std::size_t col) const {
  if (line >= lines_.size()) return {};
  const auto [a, b] = word_range(line, col);
  return lines_[line].substr(a, b - a);
}

CodeEditor::Pos CodeEditor::char_at(Point p) const {
  const float fy = (p.y + scroll_.y - kTopPad) / line_h_;
  Pos r;
  r.line = static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(lines_.size()) - 1));
  const float fx = (p.x - text_area().x - kTextPad + scroll_.x) / char_w_;
  r.col = byte_of_cp(lines_[r.line], static_cast<std::size_t>(std::max(0, static_cast<int>(std::floor(fx)))));
  return r;
}

void CodeEditor::toggle_comment() {
  if (comment_.empty() || read_only_) return;
  auto [a, b] = selection();
  const std::size_t last = (b.col == 0 && b.line > a.line) ? b.line - 1 : b.line;
  bool all = true, any = false;
  std::size_t indent = std::string::npos;
  for (std::size_t l = a.line; l <= last; ++l) {
    const std::string& line = lines_[l];
    const std::size_t fns = first_non_space(line);
    if (fns == line.size()) continue;
    any = true;
    indent = std::min(indent, fns);
    if (line.compare(fns, comment_.size(), comment_) != 0) all = false;
  }
  if (!any) return;
  push_undo(0);
  auto shift = [&](std::size_t l, std::size_t at, long delta) {
    for (Pos* q : {&caret_, &anchor_})
      if (q->line == l && q->col >= at)
        q->col = static_cast<std::size_t>(std::max(static_cast<long>(at), static_cast<long>(q->col) + delta));
  };
  for (std::size_t l = a.line; l <= last; ++l) {
    std::string& line = lines_[l];
    const std::size_t fns = first_non_space(line);
    if (fns == line.size()) continue;
    if (all) {
      std::size_t n = comment_.size();
      if (fns + n < line.size() && line[fns + n] == ' ') ++n;
      line.erase(fns, n);
      shift(l, fns, -static_cast<long>(n));
    } else {
      line.insert(indent, comment_ + " ");
      shift(l, indent, static_cast<long>(comment_.size() + 1));
    }
  }
  edited();
}

void CodeEditor::set_search(std::string query, bool match_case, bool whole_word) {
  search_ = std::move(query);
  search_case_ = match_case;
  search_word_ = whole_word;
  update_matches();
  invalidate();
}

void CodeEditor::update_matches() {
  matches_.clear();
  if (search_.empty() || search_.find('\n') != std::string::npos) return;
  auto low = [](std::string x) {
    for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return x;
  };
  const std::string q = search_case_ ? search_ : low(search_);
  for (std::size_t l = 0; l < lines_.size(); ++l) {
    const std::string line = search_case_ ? lines_[l] : low(lines_[l]);
    for (std::size_t at = line.find(q); at != std::string::npos; at = line.find(q, at + q.size())) {
      if (search_word_ && ((at > 0 && is_word(line[at - 1])) || (at + q.size() < line.size() && is_word(line[at + q.size()]))))
        continue;
      matches_.push_back({{l, at}, {l, at + q.size()}});
      if (matches_.size() >= 20000) return;
    }
  }
}

int CodeEditor::search_current() const {
  if (!has_selection()) return -1;
  const auto [a, b] = selection();
  for (std::size_t i = 0; i < matches_.size(); ++i)
    if (matches_[i].first == a && matches_[i].second == b) return static_cast<int>(i);
  return -1;
}

bool CodeEditor::find_next(bool backwards, bool include_current) {
  if (matches_.empty()) return false;
  const auto [a, b] = selection();
  std::size_t idx = backwards ? matches_.size() - 1 : 0;
  if (!backwards) {
    for (std::size_t i = 0; i < matches_.size(); ++i)
      if (!(matches_[i].first < (include_current ? a : b))) { idx = i; break; }
  } else {
    for (std::size_t i = matches_.size(); i-- > 0;)
      if (matches_[i].first < a) { idx = i; break; }
  }
  anchor_ = matches_[idx].first;
  caret_ = matches_[idx].second;
  ensure_visible();
  invalidate();
  return true;
}

bool CodeEditor::replace_current(std::string_view with) {
  if (read_only_) return false;
  if (search_current() < 0) {
    find_next();
    return false;
  }
  std::string w(with);
  std::erase(w, '\n');
  push_undo(0);
  insert(w);
  edited();
  find_next();
  return true;
}

std::size_t CodeEditor::replace_all(std::string_view with) {
  if (read_only_ || matches_.empty()) return 0;
  std::string w(with);
  std::erase(w, '\n');
  push_undo(0);
  const std::size_t n = matches_.size();
  for (auto it = matches_.rbegin(); it != matches_.rend(); ++it)
    lines_[it->first.line].replace(it->first.col, it->second.col - it->first.col, w);
  caret_.col = std::min(caret_.col, lines_[caret_.line].size());
  anchor_ = caret_;
  edited();
  return n;
}

bool CodeEditor::bracket_match(Pos& a, Pos& b) const {
  const std::string& s = lines_[caret_.line];
  auto try_at = [&](std::size_t col) -> bool {
    if (col >= s.size()) return false;
    static const std::string opens = "([{", closes = ")]}";
    const char c = s[col];
    const auto o = opens.find(c), cl = closes.find(c);
    if (o == std::string::npos && cl == std::string::npos) return false;
    const bool fwd = o != std::string::npos;
    const char other = fwd ? closes[o] : opens[cl];
    int depth = 0;
    std::size_t l = caret_.line;
    long i = static_cast<long>(col);
    for (int scanned = 0; scanned < 3000; ++scanned) {
      const std::string& t = lines_[l];
      for (; fwd ? i < static_cast<long>(t.size()) : i >= 0; fwd ? ++i : --i) {
        const char x = t[static_cast<std::size_t>(i)];
        if (x == c) ++depth;
        else if (x == other && --depth == 0) {
          a = {caret_.line, col};
          b = {l, static_cast<std::size_t>(i)};
          return true;
        }
      }
      if (fwd) {
        if (++l >= lines_.size()) return false;
        i = 0;
      } else {
        if (l == 0) return false;
        --l;
        i = static_cast<long>(lines_[l].size()) - 1;
      }
    }
    return false;
  };
  return try_at(caret_.col) || (caret_.col > 0 && try_at(caret_.col - 1));
}

// ── hover ────────────────────────────────────────────────────────────────────

void CodeEditor::hide_hover() {
  hover_timer_.reset();
  hover_target_ = false;
  if (!hover_text_.empty() || link_) {
    hover_text_.clear();
    invalidate();
  }
}

void CodeEditor::track_hover(const InputEvent& e) {
  const Rect b = bounds();
  const bool in_text = e.pos.x >= text_area().x && e.pos.x < b.w - 12 &&
                       e.pos.y < kTopPad + static_cast<float>(lines_.size()) * line_h_ - scroll_.y;
  const Pos p = char_at(e.pos);
  const auto [wa, wb] = in_text ? word_range(p.line, p.col) : std::pair<std::size_t, std::size_t>{0, 0};
  const bool over = in_text && wb > wa && p.col >= wa && p.col < wb;
  const bool link = over && (e.mods & Mod::Primary) != 0;
  if (link != link_) {
    link_ = link;
    invalidate();
  }
  cursor_ = link ? Cursor::Hand : (e.pos.x < text_area().x || e.pos.x > b.w - 12 ? Cursor::Arrow : Cursor::IBeam);
  if (!over) {
    hide_hover();
    return;
  }
  const Pos na{p.line, wa}, nb{p.line, wb};
  if (hover_target_ && na == hover_a_ && nb == hover_b_) return;
  hide_hover();
  hover_target_ = true;
  hover_a_ = na;
  hover_b_ = nb;
  if (link_) invalidate();
  if (!hover_) return;
  std::weak_ptr<bool> alive = alive_;
  const std::size_t col = p.col;
  hover_timer_ = App::after(std::chrono::milliseconds(450), [this, alive, na, col] {
    if (alive.expired() || !hover_target_ || !(hover_a_ == na) || popup_open_) return;
    hover_text_ = hover_(lines_, na.line, col);
    invalidate();
  });
}

void CodeEditor::paint_hover(Painter& p) {
  const Theme& t = th();
  Font f = t.font_mono;
  f.size = t.font_small.size + 0.5f;
  const Rect bb = bounds();
  const float maxw = std::max(120.f, std::min(560.f, bb.w - 24));
  const Size ts = measure_text(hover_text_, f, maxw);
  const float w = ts.w + 20, h = ts.h + 12;
  const float line_top = kTopPad + static_cast<float>(hover_a_.line) * line_h_ - scroll_.y;
  float y = line_top + line_h_ + 3;
  if (y + h > bb.h - 4) y = line_top - h - 3;
  const float x = std::clamp(x_of(hover_a_.line, hover_a_.col) - 8, 4.f, std::max(4.f, bb.w - w - 12));
  const Rect r{x, y, w, h};
  p.fill_rect(r.outset(2).translated(0, 2), t.shadow.with_alpha(0.2f), t.radius + 2);
  p.fill_rect(r, t.surface, t.radius_small);
  p.stroke_rect(r, t.border, 1, t.radius_small);
  p.text(r.inset(Insets(10, 6)), hover_text_, f, t.text, Align::LeftCenter, true);
}

} // namespace pulseui
