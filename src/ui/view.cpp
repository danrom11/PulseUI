#include <pulseui/ui/view.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#if defined(__GNUG__)
#include <cxxabi.h>
#endif

#include "../platform/platform.hpp"

namespace pulseui {

namespace detail {
void post_to_ui(std::function<void()> fn) { platform::post(std::move(fn)); }
bool on_ui_thread() { return platform::is_main_thread(); }
} // namespace detail

namespace detail {
void throw_view_lookup(std::string_view id, const char* wanted, View* found) {
  std::string w = wanted;
#if defined(__GNUG__)
  int status = 0;
  if (char* d = abi::__cxa_demangle(wanted, nullptr, nullptr, &status)) {
    w = d;
    std::free(d);
  }
#endif
  for (const char* prefix : {"class ", "struct "})
    if (w.rfind(prefix, 0) == 0) w.erase(0, std::char_traits<char>::length(prefix));
  if (const auto p = w.rfind("::"); p != std::string::npos) w.erase(0, p + 2);
  std::string msg = "pulseui: no view with id '" + std::string(id) + "'";
  if (found) msg = "pulseui: view '" + std::string(id) + "' is a " + found->type_name() + ", not a " + w;
  else if (!w.empty() && w != "View") msg += " (expected a " + w + ")";
  throw std::runtime_error(msg);
}
} // namespace detail

View::View(View&& o) noexcept
    : parent_(nullptr),
      window_(nullptr),
      id_(std::move(o.id_)),
      style_(o.style_),
      frame_(o.frame_),
      children_(std::move(o.children_)),
      binders_(std::move(o.binders_)),
      click_handlers_(std::move(o.click_handlers_)),
      hover_handlers_(std::move(o.hover_handlers_)),
      ref_setter_(std::move(o.ref_setter_)),
      subs_(std::move(o.subs_)),
      events_(std::move(o.events_)),
      tooltip_(std::move(o.tooltip_)),
      cursor_(o.cursor_),
      visible_(o.visible_),
      enabled_(o.enabled_),
      focusable_(o.focusable_),
      hover_repaint_(o.hover_repaint_),
      annotations_(std::move(o.annotations_)),
      extras_(std::move(o.extras_)) {
  for (auto& c : children_) c->parent_ = this;
}

namespace {
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::vector<std::string> matching_files(const ViewExtras& x, const std::vector<std::string>& files) {
  if (x.drop_extensions.empty()) return files;
  std::vector<std::string> out;
  for (auto& f : files) {
    const std::string lf = lower(f);
    for (auto& ext : x.drop_extensions) {
      const std::string le = lower(ext);
      if (lf.size() >= le.size() && lf.compare(lf.size() - le.size(), le.size(), le) == 0) {
        out.push_back(f);
        break;
      }
    }
  }
  return out;
}
} // namespace

bool View::accepts_drop(const DragData& d) const {
  if (!extras_) return false;
  if (!d.files.empty() && extras_->on_drop_files && !matching_files(*extras_, d.files).empty()) return true;
  if (!d.text.empty() && extras_->on_drop_text) return true;
  return false;
}

bool View::deliver_drop(const DragData& d) {
  if (!extras_) return false;
  if (!d.files.empty() && extras_->on_drop_files) {
    auto files = matching_files(*extras_, d.files);
    if (!files.empty()) {
      auto fn = extras_->on_drop_files;
      fn(files);
      return true;
    }
  }
  if (!d.text.empty() && extras_->on_drop_text) {
    auto fn = extras_->on_drop_text;
    fn(d.text);
    return true;
  }
  return false;
}

ViewExtras& View::extras() {
  if (!extras_) extras_ = std::make_unique<ViewExtras>();
  return *extras_;
}

View::~View() {
  *alive_ = false;
  if (window_) unmount();
  subs_.reset();
}

// ── tree ─────────────────────────────────────────────────────────────────────

View& View::add(Node n) { return insert(children_.size(), std::move(n)); }

View& View::insert(std::size_t index, Node n) {
  auto p = n.release();
  if (!p) return *this;
  View* raw = p.get();
  raw->parent_ = this;
  index = std::min(index, children_.size());
  children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(index), std::move(p));
  if (window_) raw->mount(window_);
  invalidate_layout();
  return *raw;
}

std::unique_ptr<View> View::take(View* child) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [child](const std::unique_ptr<View>& c) { return c.get() == child; });
  if (it == children_.end()) return nullptr;
  auto p = std::move(*it);
  children_.erase(it);
  if (p->window_) p->unmount();
  p->parent_ = nullptr;
  invalidate_layout();
  return p;
}

void View::clear_children() {
  auto old = std::move(children_);
  children_.clear();
  for (auto& c : old)
    if (c->window_) c->unmount();
  old.clear();
  invalidate_layout();
}

void View::move_child(View* child, std::size_t new_index) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [child](const std::unique_ptr<View>& c) { return c.get() == child; });
  if (it == children_.end()) return;
  auto p = std::move(*it);
  children_.erase(it);
  new_index = std::min(new_index, children_.size());
  children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(new_index), std::move(p));
  invalidate_layout();
}

void View::reorder(const std::vector<View*>& order) {
  std::vector<std::unique_ptr<View>> out;
  out.reserve(children_.size());
  for (View* v : order)
    for (auto& c : children_)
      if (c.get() == v) {
        out.push_back(std::move(c));
        break;
      }
  for (auto& c : children_)
    if (c) out.push_back(std::move(c));
  children_ = std::move(out);
  invalidate_layout();
}

bool View::contains(const View* v) const {
  for (; v; v = v->parent_)
    if (v == this) return true;
  return false;
}

int View::index_of(const View* child) const {
  for (std::size_t i = 0; i < children_.size(); ++i)
    if (children_[i].get() == child) return static_cast<int>(i);
  return -1;
}

View* View::find(std::string_view id) {
  if (id_ == id) return this;
  for (auto& c : children_)
    if (View* v = c->find(id)) return v;
  return nullptr;
}

const std::string* View::annotation(std::string_view key) const {
  if (!annotations_) return nullptr;
  for (auto& [k, v] : *annotations_)
    if (k == key) return &v;
  return nullptr;
}

void View::set_annotation(std::string_view key, std::string value) {
  if (!annotations_) {
    if (value.empty()) return;
    annotations_ = std::make_unique<std::vector<std::pair<std::string, std::string>>>();
  }
  auto& a = *annotations_;
  for (auto it = a.begin(); it != a.end(); ++it)
    if (it->first == key) {
      if (value.empty()) a.erase(it);
      else it->second = std::move(value);
      return;
    }
  if (!value.empty()) a.emplace_back(std::string(key), std::move(value));
}

const std::vector<std::pair<std::string, std::string>>& View::annotations() const {
  static const std::vector<std::pair<std::string, std::string>> none;
  return annotations_ ? *annotations_ : none;
}

// ── geometry / state ─────────────────────────────────────────────────────────

Point View::to_window(Point local) const {
  Point p = local;
  for (const View* v = this; v; v = v->parent_) p = p + v->frame_.pos();
  return p;
}

void View::set_frame(const Rect& r) {
  ViewExtras* x = extras_.get();
  if (x && x->transition && window_ && x->frame_valid && visible_ && anim::speed() > 0) {
    if (r == x->frame_target) return; // already on its way there
    x->frame_target = r;
    const Rect from = frame_;
    if (from == r) {
      x->frame_anim.reset();
      return;
    }
    std::weak_ptr<bool> alive = alive_;
    x->frame_anim = anim::run(*x->transition, [this, alive, from, r](float t) {
      if (alive.expired()) return;
      frame_ = anim::lerp(from, r, t);
      layout(); // children follow the size in flight
      if (parent_) parent_->invalidate();
      else invalidate();
    });
    return;
  }
  frame_ = r;
  if (x) {
    x->frame_target = r;
    x->frame_valid = window_ != nullptr && r.w > 0;
    x->frame_anim.reset();
  }
}

namespace {
template <class T, class Apply>
void animate_value(View& v, ViewExtras* x, const std::shared_ptr<bool>& alive_token, T from, T to,
                   pulse::subscription ViewExtras::*slot, Apply apply) {
  if (!x || !x->transition || !v.mounted() || anim::speed() <= 0 || from == to) {
    if (x) (x->*slot).reset();
    apply(to);
    return;
  }
  std::weak_ptr<bool> alive = alive_token;
  x->*slot = anim::run(*x->transition, [alive, from, to, apply](float t) {
    if (!alive.expired()) apply(anim::lerp(from, to, t));
  });
}
} // namespace

void View::set_opacity(float o) {
  animate_value(*this, extras_.get(), alive_, style_.opacity, o, &ViewExtras::opacity_anim, [this](float v) {
    style_.opacity = v;
    invalidate();
  });
}

void View::set_background(Color c) {
  Color from = style_.background;
  if (from.a == 0) from = c.with_alpha(0); // fade in from transparent of the same hue
  animate_value(*this, extras_.get(), alive_, from, c, &ViewExtras::background_anim, [this](Color v) {
    style_.background = v;
    invalidate();
  });
}

void View::set_border_color(Color c) {
  Color from = style_.border;
  if (from.a == 0) from = c.with_alpha(0);
  animate_value(*this, extras_.get(), alive_, from, c, &ViewExtras::border_anim, [this](Color v) {
    style_.border = v;
    invalidate();
  });
}

void View::set_visible(bool v) {
  if (visible_ == v) return;
  visible_ = v;
  if (parent_) parent_->invalidate_layout();
  else invalidate_layout();
}

bool View::is_enabled() const {
  for (const View* v = this; v; v = v->parent_)
    if (!v->enabled_) return false;
  return true;
}

void View::set_enabled(bool e) {
  if (enabled_ == e) return;
  enabled_ = e;
  invalidate();
}

bool View::focused() const { return window_ && window_->focus_view() == this; }

void View::focus() {
  if (window_) window_->set_focus(this);
}

void View::set_hovered(bool h) {
  if (hovered_ == h) return;
  hovered_ = h;
  hover_changed(h);
  for (auto& fn : hover_handlers_) fn(h);
  if (hover_repaint_ || !click_handlers_.empty()) invalidate();
}

void View::set_pressed(bool p) {
  if (pressed_ == p) return;
  pressed_ = p;
  if (hover_repaint_ || !click_handlers_.empty()) invalidate();
}

// ── reactivity ───────────────────────────────────────────────────────────────

void View::keep(pulse::subscription s) {
  if (!subs_) subs_ = std::make_unique<pulse::composite_subscription>();
  subs_->add(std::move(s));
}

void View::defer(std::function<void(View&)> binder) {
  binders_.push_back(std::move(binder));
  if (window_) binders_.back()(*this); // already live: attach now
}

pulse::signal<InputEvent>& View::events() {
  if (!events_) events_ = std::make_unique<pulse::signal<InputEvent>>();
  return *events_;
}

void View::invalidate() {
  if (window_) window_->request_paint();
}

void View::invalidate_layout() {
  if (window_) window_->request_layout();
}

void View::mount(Window* w) {
  window_ = w;
  // Binders run on every mount (they are kept so a view can be re-mounted).
  for (auto& b : binders_) b(*this);
  if (ref_setter_) ref_setter_(*this);
  on_mount();
  for (auto& c : children_) c->mount(w);
}

void View::unmount() {
  for (auto& c : children_)
    if (c->window_) c->unmount();
  on_unmount();
  if (subs_) subs_->clear();
  if (window_) window_->view_detached(this);
  hovered_ = pressed_ = false;
  window_ = nullptr;
}

// ── layout ───────────────────────────────────────────────────────────────────

Size View::measure_content(Size avail) {
  // default: like a Layer - the biggest child
  Size s{};
  for (auto& c : children_) {
    if (!c->visible_) continue;
    Size cs = c->measure(avail);
    if (c->style_.positioned) {
      cs.w += c->style_.position.x;
      cs.h += c->style_.position.y;
    }
    s.w = std::max(s.w, cs.w);
    s.h = std::max(s.h, cs.h);
  }
  return s;
}

Size View::measure(Size avail) {
  const Insets& p = style_.padding;
  Size inner{std::max(0.f, (is_auto(style_.width) ? avail.w : style_.width) - p.horizontal()),
             std::max(0.f, (is_auto(style_.height) ? avail.h : style_.height) - p.vertical())};
  Size s{style_.width, style_.height};
  if (is_auto(s.w) || is_auto(s.h)) {
    Size c = measure_content(inner);
    if (is_auto(s.w)) s.w = c.w + p.horizontal();
    if (is_auto(s.h)) s.h = c.h + p.vertical();
  }
  s.w = std::clamp(s.w, style_.min_width, std::max(style_.min_width, style_.max_width));
  s.h = std::clamp(s.h, style_.min_height, std::max(style_.min_height, style_.max_height));
  return s;
}

void View::layout() {
  const Rect cr = content_rect();
  for (auto& c : children_) {
    if (!c->visible_) continue;
    if (c->style_.positioned) {
      Size s = c->measure(cr.size());
      c->set_frame({cr.x + c->style_.position.x, cr.y + c->style_.position.y, s.w, s.h});
    } else {
      c->set_frame(cr);
    }
    c->layout();
  }
}

// ── painting ─────────────────────────────────────────────────────────────────

void View::paint_tree(Painter& p) {
  if (!visible_) return;
  p.save();
  p.translate(frame_.x, frame_.y);
  if (style_.opacity < 1) p.set_opacity(style_.opacity);
  if (!is_enabled()) p.set_opacity(0.55f);
  // clip() confines everything the view draws: itself, its children, its overlay
  if (style_.clip) p.clip(bounds());
  paint(p);
  const Rect visible = bounds();
  for (auto& c : children_) {
    // children fully outside a clipping view are skipped (cheap scroll lists)
    if (style_.clip && !c->frame_.intersects(visible)) continue;
    c->paint_tree(p);
  }
  paint_overlay(p);
  p.restore();
}

void View::paint(Painter& p) {
  if (style_.background.a > 0) p.fill_rect(bounds(), style_.background, style_.radius);
  if (style_.border_width > 0 && style_.border.a > 0)
    p.stroke_rect(bounds(), style_.border, style_.border_width, style_.radius);
  paint_content(p);
}

// ── input ────────────────────────────────────────────────────────────────────

bool View::on_event(InputEvent& e) {
  if (click_handlers_.empty()) return false;
  if (e.type == InputEvent::MouseDown && e.button == MouseButton::Left) {
    set_pressed(true);
    return true;
  }
  if (e.type == InputEvent::MouseUp && e.button == MouseButton::Left) {
    const bool fire = pressed_ && bounds().contains(e.pos);
    set_pressed(false);
    if (fire) {
      auto handlers = click_handlers_; // a handler may destroy the view
      for (auto& fn : handlers) fn();
    }
    return true;
  }
  return e.type == InputEvent::MouseMove && pressed_;
}

} // namespace pulseui
