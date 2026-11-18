#include <pulseui/ui/containers.hpp>
#include <pulseui/ui/theme.hpp>

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
  if (!children_.empty()) children_[0]->set_visible(c);
  if (has_else_ && children_.size() > 1) children_[1]->set_visible(!c);
  invalidate_layout();
}

Size Show::measure_content(Size avail) { return View::measure_content(avail); }
void Show::layout() { View::layout(); }

} // namespace pulseui
