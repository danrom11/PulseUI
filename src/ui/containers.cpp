#include <pulseui/ui/containers.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pulseui {

namespace {
constexpr float kInf = std::numeric_limits<float>::infinity();

float main_of(Size s, Orientation o) { return o == Orientation::Vertical ? s.h : s.w; }
float cross_of(Size s, Orientation o) { return o == Orientation::Vertical ? s.w : s.h; }
Size make_size(float main, float cross, Orientation o) {
  return o == Orientation::Vertical ? Size{cross, main} : Size{main, cross};
}
} // namespace

namespace detail {

Size stack_measure(View& v, Orientation o, float spacing, Size avail) {
  float main = 0, cross = 0;
  int n = 0;
  const Size child_avail = make_size(kInf, cross_of(avail, o), o);
  for (auto& c : v.children()) {
    if (!c->is_visible() || c->style().positioned) continue;
    const Size s = c->measure(child_avail);
    main += main_of(s, o);
    cross = std::max(cross, cross_of(s, o));
    ++n;
  }
  if (n > 1) main += spacing * static_cast<float>(n - 1);
  return make_size(main, cross, o);
}

void stack_layout(View& v, Orientation o, float spacing, Alignment cross_align, Justify justify) {
  const Rect cr = v.content_rect();
  const float avail_main = main_of(cr.size(), o);
  const float avail_cross = cross_of(cr.size(), o);

  struct Item {
    View* view;
    Size pref;
    float main;
  };
  std::vector<Item> items;
  float fixed = 0, flex_total = 0;
  for (auto& c : v.children()) {
    if (!c->is_visible()) continue;
    if (c->style().positioned) {
      Size s = c->measure(cr.size());
      c->set_frame({cr.x + c->style().position.x, cr.y + c->style().position.y, s.w, s.h});
      c->layout();
      continue;
    }
    const Size pref = c->measure(make_size(kInf, avail_cross, o));
    Item it{c.get(), pref, main_of(pref, o)};
    if (c->style().flex > 0) {
      flex_total += c->style().flex;
      it.main = o == Orientation::Vertical ? c->style().min_height : c->style().min_width;
    }
    fixed += it.main;
    items.push_back(it);
  }
  if (items.empty()) return;
  const float gaps = spacing * static_cast<float>(items.size() - 1);
  float free = avail_main - fixed - gaps;

  if (flex_total > 0 && free > 0) {
    for (auto& it : items) {
      const float f = it.view->style().flex;
      if (f <= 0) continue;
      const float mx = o == Orientation::Vertical ? it.view->style().max_height : it.view->style().max_width;
      it.main = std::min(mx, it.main + free * (f / flex_total));
    }
    free = 0;
  }

  float pos = 0, gap = spacing;
  if (flex_total <= 0 && free > 0) {
    switch (justify) {
      case Justify::Center: pos = free / 2; break;
      case Justify::End: pos = free; break;
      case Justify::SpaceBetween:
        if (items.size() > 1) gap = spacing + free / static_cast<float>(items.size() - 1);
        break;
      default: break;
    }
  }

  for (auto& it : items) {
    const Style& st = it.view->style();
    Alignment a = st.align == Alignment::Auto ? cross_align : st.align;
    const bool fixed_cross = o == Orientation::Vertical ? !is_auto(st.width) : !is_auto(st.height);
    if (a == Alignment::Stretch && fixed_cross) a = Alignment::Start;
    float csize = a == Alignment::Stretch ? avail_cross : std::min(avail_cross, cross_of(it.pref, o));
    const float cmax = o == Orientation::Vertical ? st.max_width : st.max_height;
    csize = std::min(csize, cmax);
    float cpos = 0;
    if (a == Alignment::Center) cpos = (avail_cross - csize) / 2;
    else if (a == Alignment::End) cpos = avail_cross - csize;

    Rect f = o == Orientation::Vertical ? Rect{cr.x + cpos, cr.y + pos, csize, it.main}
                                        : Rect{cr.x + pos, cr.y + cpos, it.main, csize};
    f.x = std::round(f.x);
    f.y = std::round(f.y);
    it.view->set_frame(f);
    it.view->layout();
    pos += it.main + gap;
  }
}

} // namespace detail

// ── Layer ────────────────────────────────────────────────────────────────────
Size Layer::measure_content(Size avail) { return View::measure_content(avail); }
void Layer::layout() { View::layout(); }

// ── Panel ────────────────────────────────────────────────────────────────────
Panel::Panel() {
  style_.padding = Insets(12);
  spacing_ = 8;
}

void Panel::paint(Painter& p) {
  const Theme& t = th();
  const float r = style_.radius > 0 ? style_.radius : t.radius;
  p.fill_rect(bounds(), style_.background.a > 0 ? style_.background : t.surface, r);
  p.stroke_rect(bounds(), style_.border_width > 0 ? style_.border : t.border,
                std::max(1.f, style_.border_width), r);
  paint_content(p);
}

// ── Scroll ───────────────────────────────────────────────────────────────────
Size Scroll::measure_content(Size avail) {
  if (children_.empty()) return {};
  return children_[0]->measure({horizontal_ ? kInf : avail.w, kInf});
}

void Scroll::layout() {
  if (children_.empty()) return;
  const Rect cr = content_rect();
  View* c = children_[0].get();
  content_size_ = c->measure({horizontal_ ? kInf : cr.w, kInf});
  // vertical-only scrolling: content always takes exactly the viewport width
  content_size_.w = horizontal_ ? std::max(content_size_.w, cr.w) : cr.w;
  content_size_.h = std::max(content_size_.h, cr.h);
  offset_.x = std::clamp(offset_.x, 0.f, std::max(0.f, content_size_.w - cr.w));
  offset_.y = std::clamp(offset_.y, 0.f, std::max(0.f, content_size_.h - cr.h));
  c->set_frame({cr.x - offset_.x, cr.y - offset_.y, content_size_.w, content_size_.h});
  c->layout();
}

void Scroll::scroll_to(float y) {
  offset_.y = y;
  invalidate_layout();
}

void Scroll::scroll_into_view(const Rect& r) {
  const Rect cr = content_rect();
  if (r.y < offset_.y) scroll_to(r.y);
  else if (r.bottom() > offset_.y + cr.h) scroll_to(r.bottom() - cr.h);
}

Rect Scroll::thumb_rect(bool vertical) const {
  const Rect cr = content_rect();
  if (vertical) {
    if (content_size_.h <= cr.h + 0.5f) return {};
    const float h = std::max(24.f, cr.h * cr.h / content_size_.h);
    const float y = cr.y + (cr.h - h) * (offset_.y / (content_size_.h - cr.h));
    return {cr.right() - 8, y, 6, h};
  }
  if (content_size_.w <= cr.w + 0.5f) return {};
  const float w = std::max(24.f, cr.w * cr.w / content_size_.w);
  const float x = cr.x + (cr.w - w) * (offset_.x / (content_size_.w - cr.w));
  return {x, cr.bottom() - 8, w, 6};
}

void Scroll::paint_overlay(Painter& p) {
  const Theme& t = th();
  for (bool vert : {true, false}) {
    Rect r = thumb_rect(vert);
    if (r.empty()) continue;
    Color c = t.scrollbar;
    if (dragging_ || hovered()) c = c.with_alpha(std::min(1.f, c.a * 1.8f));
    p.fill_rect(r, c, 3);
  }
}

bool Scroll::hit_test_children(Point local) const {
  const Rect tr = thumb_rect(true);
  return tr.empty() || !tr.outset(3).contains(local);
}

bool Scroll::on_event(InputEvent& e) {
  const Rect cr = content_rect();
  switch (e.type) {
    case InputEvent::Wheel: {
      const Point before = offset_;
      offset_.y -= e.wheel_dy * 48;
      offset_.x -= e.wheel_dx * 48;
      if ((e.mods & Mod::Shift) && horizontal_) {
        offset_.x -= e.wheel_dy * 48;
        offset_.y = before.y;
      }
      offset_.y = std::clamp(offset_.y, 0.f, std::max(0.f, content_size_.h - cr.h));
      offset_.x = std::clamp(offset_.x, 0.f, std::max(0.f, content_size_.w - cr.w));
      if (offset_ == before) return false;
      invalidate_layout();
      return true;
    }
    case InputEvent::MouseDown: {
      const Rect tr = thumb_rect(true);
      if (!tr.empty() && tr.outset(3).contains(e.pos)) {
        dragging_ = true;
        drag_start_ = e.pos.y;
        drag_offset_start_ = offset_.y;
        return true;
      }
      return false;
    }
    case InputEvent::MouseMove:
      if (dragging_) {
        const Rect tr = thumb_rect(true);
        const float track = cr.h - tr.h;
        if (track > 0)
          offset_.y = drag_offset_start_ + (e.pos.y - drag_start_) * (content_size_.h - cr.h) / track;
        invalidate_layout();
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

// ── Show ─────────────────────────────────────────────────────────────────────
Show::Show(Bind<bool> cond, Node then, Node otherwise) {
  add(std::move(then));
  if (otherwise) {
    add(std::move(otherwise));
    has_else_ = true;
  }
  bind(cond, [](View& v, bool c) { static_cast<Show&>(v).apply(c); });
}

void Show::apply(bool c) {
  const bool animate = transition_ && initialized_ && mounted() && anim::speed() > 0;
  if (initialized_ && c == cond_ && !transitioning()) return;
  if (transitioning()) finish_transition();
  if (!animate || c == cond_) {
    cond_ = c;
    initialized_ = true;
    if (View* a = branch(0)) a->set_visible(c);
    if (View* b = has_else_ ? branch(1) : nullptr) b->set_visible(!c);
    invalidate_layout();
    return;
  }
  from_ = cond_ ? 0 : (has_else_ ? 1 : -1);
  to_ = c ? 0 : (has_else_ ? 1 : -1);
  cond_ = c;
  t_ = 0;
  clip_before_ = style_.clip;
  style_.clip = true;
  if (View* in = branch(to_)) {
    in->style().opacity = 0;
    in->set_visible(true);
  }
  std::weak_ptr<bool> alive = alive_;
  anim_ = anim::run(
      *transition_,
      [this, alive](float t) {
        if (alive.expired()) return;
        t_ = t;
        if (View* out = branch(from_)) out->style().opacity = 1 - t;
        if (View* in = branch(to_)) in->style().opacity = t;
        invalidate_layout();
      },
      [this, alive] {
        if (!alive.expired()) finish_transition();
      });
  invalidate_layout();
}

void Show::finish_transition() {
  if (View* out = branch(from_)) {
    out->set_visible(false);
    out->style().opacity = 1;
  }
  if (View* in = branch(to_)) in->style().opacity = 1;
  from_ = to_ = -1;
  t_ = 1;
  style_.clip = clip_before_;
  invalidate_layout();
}

Size Show::measure_content(Size avail) {
  if (!transitioning()) return View::measure_content(avail);
  const View* a = branch(from_);
  const View* b = branch(to_);
  const Size sa = a ? const_cast<View*>(a)->measure(avail) : Size{0, 0};
  const Size sb = b ? const_cast<View*>(b)->measure(avail) : Size{0, 0};
  return anim::lerp(sa, sb, t_);
}

void Show::layout() {
  if (!transitioning()) {
    View::layout();
    return;
  }
  // both branches at their natural height: the shrinking area reveals or hides them
  const Rect c = content_rect();
  for (int i : {from_, to_}) {
    View* v = branch(i);
    if (!v) continue;
    const Size s = v->measure({c.w, std::numeric_limits<float>::infinity()});
    v->set_frame({c.x, c.y, c.w, std::max(s.h, 0.f)});
    v->layout();
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Grid
// ═════════════════════════════════════════════════════════════════════════════

const View* detail::focused_view(const Window& w) { return w.focus_view(); }

std::vector<Grid::Placement> Grid::placements() const {
  const int ncols = column_count();
  std::vector<Placement> out;
  std::vector<std::vector<bool>> taken; // [row][col]
  auto occupy = [&](int r, int c, int rs, int cs) {
    if (static_cast<int>(taken.size()) < r + rs) taken.resize(static_cast<std::size_t>(r + rs), std::vector<bool>(ncols, false));
    for (int y = r; y < r + rs; ++y)
      for (int x = c; x < std::min(ncols, c + cs); ++x) taken[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = true;
  };
  auto free = [&](int r, int c, int cs) {
    if (c + cs > ncols) return false;
    if (r >= static_cast<int>(taken.size())) return true;
    for (int x = c; x < c + cs; ++x)
      if (taken[static_cast<std::size_t>(r)][static_cast<std::size_t>(x)]) return false;
    return true;
  };
  // explicit cells first, then the others flow into the free cells
  for (auto& ch : children_) {
    const Style& st = ch->style();
    if (!ch->is_visible() || st.positioned || st.grid_row < 0 || st.grid_col < 0) continue;
    const int cs = std::min(st.col_span, ncols);
    out.push_back({ch.get(), st.grid_row, std::min(st.grid_col, ncols - 1), st.row_span, cs});
    occupy(st.grid_row, std::min(st.grid_col, ncols - 1), st.row_span, cs);
  }
  int r = 0, c = 0;
  for (auto& ch : children_) {
    const Style& st = ch->style();
    if (!ch->is_visible() || st.positioned || (st.grid_row >= 0 && st.grid_col >= 0)) continue;
    const int cs = std::min(st.col_span, ncols);
    while (!free(r, c, cs)) {
      if (++c + cs > ncols) {
        c = 0;
        ++r;
      }
    }
    out.push_back({ch.get(), r, c, st.row_span, cs});
    occupy(r, c, st.row_span, cs);
    c += cs;
    if (c >= ncols) {
      c = 0;
      ++r;
    }
  }
  return out;
}

std::vector<float> Grid::column_widths(const std::vector<Placement>& ps, float avail) const {
  const int n = column_count();
  std::vector<Track> tracks = tracks_;
  tracks.resize(static_cast<std::size_t>(n), Track::fr(1));
  std::vector<float> w(static_cast<std::size_t>(n), 0.f), pref(static_cast<std::size_t>(n), 0.f);
  for (auto& p : ps)
    if (p.col_span == 1) {
      const Size s = p.view->measure({kInf, kInf});
      pref[static_cast<std::size_t>(p.col)] = std::max(pref[static_cast<std::size_t>(p.col)], s.w);
    }
  float used = col_gap_ * static_cast<float>(n - 1), fr_total = 0;
  for (int i = 0; i < n; ++i) {
    const Track& t = tracks[static_cast<std::size_t>(i)];
    if (t.kind == Track::Fixed) w[static_cast<std::size_t>(i)] = t.value;
    else if (t.kind == Track::Auto) w[static_cast<std::size_t>(i)] = pref[static_cast<std::size_t>(i)];
    else fr_total += std::max(0.f, t.value);
    if (t.kind != Track::Fraction) used += w[static_cast<std::size_t>(i)];
  }
  if (fr_total > 0) {
    if (std::isinf(avail)) { // measuring: fractions take what their content needs, in proportion
      float unit = 0;
      for (int i = 0; i < n; ++i)
        if (tracks[static_cast<std::size_t>(i)].kind == Track::Fraction && tracks[static_cast<std::size_t>(i)].value > 0)
          unit = std::max(unit, pref[static_cast<std::size_t>(i)] / tracks[static_cast<std::size_t>(i)].value);
      for (int i = 0; i < n; ++i)
        if (tracks[static_cast<std::size_t>(i)].kind == Track::Fraction) w[static_cast<std::size_t>(i)] = unit * tracks[static_cast<std::size_t>(i)].value;
    } else {
      const float rest = std::max(0.f, avail - used);
      for (int i = 0; i < n; ++i)
        if (tracks[static_cast<std::size_t>(i)].kind == Track::Fraction)
          w[static_cast<std::size_t>(i)] = rest * std::max(0.f, tracks[static_cast<std::size_t>(i)].value) / fr_total;
    }
  }
  return w;
}

std::vector<float> Grid::row_heights(const std::vector<Placement>& ps, const std::vector<float>& cols) const {
  int rows = 0;
  for (auto& p : ps) rows = std::max(rows, p.row + p.row_span);
  std::vector<float> h(static_cast<std::size_t>(rows), 0.f);
  for (auto& p : ps) {
    float w = col_gap_ * static_cast<float>(p.col_span - 1);
    for (int i = p.col; i < p.col + p.col_span && i < static_cast<int>(cols.size()); ++i) w += cols[static_cast<std::size_t>(i)];
    const Size s = p.view->measure({w, kInf});
    if (p.row_span == 1) h[static_cast<std::size_t>(p.row)] = std::max(h[static_cast<std::size_t>(p.row)], s.h);
  }
  // row-spanning children: grow the last spanned row if they do not fit
  for (auto& p : ps) {
    if (p.row_span == 1) continue;
    float w = col_gap_ * static_cast<float>(p.col_span - 1);
    for (int i = p.col; i < p.col + p.col_span && i < static_cast<int>(cols.size()); ++i) w += cols[static_cast<std::size_t>(i)];
    const float need = p.view->measure({w, kInf}).h;
    float have = row_gap_ * static_cast<float>(p.row_span - 1);
    for (int r = p.row; r < p.row + p.row_span; ++r) have += h[static_cast<std::size_t>(r)];
    if (need > have) h[static_cast<std::size_t>(p.row + p.row_span - 1)] += need - have;
  }
  return h;
}

Size Grid::measure_content(Size avail) {
  const auto ps = placements();
  const auto cols = column_widths(ps, avail.w);
  const auto rows = row_heights(ps, cols);
  float w = 0, h = 0;
  for (float c : cols) w += c;
  for (float r : rows) h += r;
  if (!cols.empty()) w += col_gap_ * static_cast<float>(cols.size() - 1);
  if (!rows.empty()) h += row_gap_ * static_cast<float>(rows.size() - 1);
  return {w, h};
}

void Grid::layout() {
  const Rect cr = content_rect();
  const auto ps = placements();
  const auto cols = column_widths(ps, cr.w);
  const auto rows = row_heights(ps, cols);
  std::vector<float> xs(cols.size() + 1, cr.x), ys(rows.size() + 1, cr.y);
  for (std::size_t i = 0; i < cols.size(); ++i) xs[i + 1] = xs[i] + cols[i] + col_gap_;
  for (std::size_t i = 0; i < rows.size(); ++i) ys[i + 1] = ys[i] + rows[i] + row_gap_;
  for (auto& p : ps) {
    const std::size_t c0 = static_cast<std::size_t>(p.col), c1 = std::min(cols.size(), c0 + static_cast<std::size_t>(p.col_span));
    const std::size_t r0 = static_cast<std::size_t>(p.row), r1 = std::min(rows.size(), r0 + static_cast<std::size_t>(p.row_span));
    Rect cell{xs[c0], ys[r0], xs[c1] - xs[c0] - col_gap_, ys[r1] - ys[r0] - row_gap_};
    const Alignment a = p.view->style().align != Alignment::Auto ? p.view->style().align : align_;
    if (a != Alignment::Stretch) {
      const Size s = p.view->measure({cell.w, kInf});
      const float w = std::min(cell.w, s.w), h = std::min(cell.h, s.h);
      const float dx = a == Alignment::Center ? (cell.w - w) / 2 : a == Alignment::End ? cell.w - w : 0;
      const float dy = a == Alignment::Center ? (cell.h - h) / 2 : a == Alignment::End ? cell.h - h : 0;
      cell = {cell.x + dx, cell.y + dy, w, h};
    }
    p.view->set_frame(cell);
    p.view->layout();
  }
  for (auto& ch : children_)
    if (ch->is_visible() && ch->style().positioned) {
      const Size s = ch->measure(cr.size());
      ch->set_frame({cr.x + ch->style().position.x, cr.y + ch->style().position.y, s.w, s.h});
      ch->layout();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Wrap
// ═════════════════════════════════════════════════════════════════════════════

namespace {
struct WrapLine {
  std::vector<std::pair<View*, Size>> items;
  float w = 0, h = 0;
};
std::vector<WrapLine> wrap_lines(View& v, float avail_w, float gap) {
  std::vector<WrapLine> lines(1);
  for (auto& c : v.children()) {
    if (!c->is_visible() || c->style().positioned) continue;
    const Size s = c->measure({avail_w, kInf});
    WrapLine* l = &lines.back();
    const float add = (l->items.empty() ? 0 : gap) + s.w;
    if (!l->items.empty() && l->w + add > avail_w + 0.5f) {
      lines.emplace_back();
      l = &lines.back();
    }
    l->w += (l->items.empty() ? 0 : gap) + s.w;
    l->h = std::max(l->h, s.h);
    l->items.push_back({c.get(), s});
  }
  if (lines.back().items.empty()) lines.pop_back();
  return lines;
}
} // namespace

Size detail::wrap_measure(View& self, float gap, float line_gap, Size avail) {
  const auto lines = wrap_lines(self, avail.w, gap);
  float w = 0, h = 0;
  for (auto& l : lines) {
    w = std::max(w, l.w);
    h += l.h;
  }
  if (lines.size() > 1) h += line_gap * static_cast<float>(lines.size() - 1);
  return {w, h};
}

void detail::wrap_layout(View& self, float gap_base, float line_gap, Justify justify) {
  const Rect cr = self.content_rect();
  float y = cr.y;
  for (auto& l : wrap_lines(self, cr.w, gap_base)) {
    const float free = std::max(0.f, cr.w - l.w);
    float x = cr.x, gap = gap_base;
    if (justify == Justify::Center) x += free / 2;
    else if (justify == Justify::End) x += free;
    else if (justify == Justify::SpaceBetween && l.items.size() > 1) gap += free / static_cast<float>(l.items.size() - 1);
    for (auto& [v, s] : l.items) {
      v->set_frame({x, y + (l.h - s.h) / 2, std::min(s.w, cr.w), s.h});
      v->layout();
      x += s.w + gap;
    }
    y += l.h + line_gap;
  }
}

Size Wrap::measure_content(Size avail) { return detail::wrap_measure(*this, gap_, line_gap_, avail); }
void Wrap::layout() { detail::wrap_layout(*this, gap_, line_gap_, justify_); }

} // namespace pulseui
