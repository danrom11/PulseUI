#pragma once
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/signal.hpp>
#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/gfx/painter.hpp>
#include <pulseui/ui/bind.hpp>
#include <pulseui/ui/events.hpp>

// Declares a chainable modifier in two flavours: on an lvalue it returns
// Self&, on a temporary it returns Self&& - so `return Text("x").padding(4);`
// moves the view out instead of trying to copy it.
#define PULSEUI_MOD(Self, Name, Params, ...)                                   \
  Self& Name Params& {                                                         \
    __VA_ARGS__ return static_cast<Self&>(*this);                              \
  }                                                                            \
  Self&& Name Params&& {                                                       \
    __VA_ARGS__ return static_cast<Self&&>(*this);                             \
  }

namespace pulseui {

class Window;
class View;

// Cross-axis alignment of a child inside a stack.
enum class Alignment { Auto, Start, Center, End, Stretch };

inline constexpr float kAuto = std::numeric_limits<float>::quiet_NaN();
inline bool is_auto(float v) { return std::isnan(v); }

// Node: an owning, type-erased handle to a view description.
// Any view value converts to Node, so containers accept them directly:
//   VStack{ Text("a"), Button("b") }
class Node {
public:
  Node() = default;
  Node(std::nullptr_t) {}
  template <class V>
    requires std::is_base_of_v<View, std::decay_t<V>>
  Node(V&& v) : p_(std::make_unique<std::decay_t<V>>(std::move(v))) {}
  template <class V>
    requires std::is_base_of_v<View, V>
  Node(std::unique_ptr<V> p) : p_(std::move(p)) {}

  explicit operator bool() const { return static_cast<bool>(p_); }
  View* get() const { return p_.get(); }
  View* operator->() const { return p_.get(); }
  std::unique_ptr<View> release() { return std::move(p_); }

private:
  std::unique_ptr<View> p_;
};

// Layout and appearance attributes shared by every view.
struct Style {
  Insets padding{};
  float width = kAuto, height = kAuto;
  float min_width = 0, min_height = 0;
  float max_width = std::numeric_limits<float>::infinity();
  float max_height = std::numeric_limits<float>::infinity();
  float flex = 0;                  // share of the free space in a stack
  Alignment align = Alignment::Auto;
  bool positioned = false;         // absolute position inside a Layer
  Point position{};
  Color background = Color::transparent();
  Color border = Color::transparent();
  float border_width = 0;
  float radius = 0;
  bool clip = false;
  float opacity = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
// View: a node of the UI tree.
//
// A view is a *description* until it is mounted into a window: before that it
// can be freely moved around (that's what makes the declarative syntax work).
// On mount, its bindings subscribe to their sources; on unmount (or
// destruction) all subscriptions are cancelled automatically.
// ─────────────────────────────────────────────────────────────────────────────
class View {
public:
  View() = default;
  View(View&&) noexcept;
  View& operator=(View&&) = delete;
  View(const View&) = delete;
  View& operator=(const View&) = delete;
  virtual ~View();

  virtual const char* type_name() const { return "View"; }

  // ── identity / tree ──────────────────────────────────────────────────────
  const std::string& id() const { return id_; }
  void set_id(std::string id) { id_ = std::move(id); }
  View* parent() const { return parent_; }
  Window* window() const { return window_; }
  bool mounted() const { return window_ != nullptr; }
  const std::vector<std::unique_ptr<View>>& children() const { return children_; }

  View& add(Node n);
  View& insert(std::size_t index, Node n);
  std::unique_ptr<View> take(View* child);
  void remove(View* child) { take(child); }
  void clear_children();
  void move_child(View* child, std::size_t new_index);
  int index_of(const View* child) const;

  View* find(std::string_view id);
  template <class V>
  V* find(std::string_view id) { return dynamic_cast<V*>(find(id)); }

  // ── style / geometry ─────────────────────────────────────────────────────
  Style& style() { return style_; }
  const Style& style() const { return style_; }
  const Rect& frame() const { return frame_; }         // relative to parent
  Rect bounds() const { return {0, 0, frame_.w, frame_.h}; }
  Rect content_rect() const { return bounds().inset(style_.padding); }
  Point to_window(Point local) const;
  Point from_window(Point p) const { return p - to_window({0, 0}); }
  void set_frame(const Rect& r);

  bool is_visible() const { return visible_; }
  void set_visible(bool v);
  bool is_enabled() const; // includes ancestors
  void set_enabled(bool e);
  bool hovered() const { return hovered_; }
  bool pressed() const { return pressed_; }
  bool focused() const;
  bool focusable() const { return focusable_; }
  void focus();
  const std::string& tooltip() const { return tooltip_; }
  void set_tooltip(std::string t) { tooltip_ = std::move(t); }
  Cursor cursor() const { return cursor_; }
  void set_cursor(Cursor c) { cursor_ = c; }
  bool enabled_self() const { return enabled_; }

  // ── reactivity ───────────────────────────────────────────────────────────
  // Follow `b`: apply(view, value) runs now for a static value, or on every
  // emission once mounted (always on the UI thread).
  template <class T, class F>
  void bind(const Bind<T>& b, F apply);
  // Lifetime-bound subscriptions (cancelled on unmount).
  void keep(pulse::subscription s);

  void invalidate();        // repaint
  void invalidate_layout(); // re-measure + repaint

  // Stream of input events delivered to this view.
  pulse::signal<InputEvent>& events();

  // ── layout protocol ──────────────────────────────────────────────────────
  // Preferred size within `avail` (padding included, constraints applied).
  Size measure(Size avail);
  // Content size without padding. Containers measure children here.
  virtual Size measure_content(Size avail);
  // Position children inside content_rect(). Default: children fill it
  // (or use their absolute position when positioned).
  virtual void layout();

  // ── painting ─────────────────────────────────────────────────────────────
  void paint_tree(Painter& p);
  virtual void paint(Painter& p);         // background/border + content
  virtual void paint_content(Painter&) {}
  virtual void paint_overlay(Painter&) {} // drawn above children

  // ── input ────────────────────────────────────────────────────────────────
  // Return true when the event is consumed (stops bubbling to the parent).
  virtual bool on_event(InputEvent& e);
  virtual void hover_changed(bool) {} // override to react to hover (on_hover(fn) is the modifier)
  virtual void on_focus(bool) {}
  virtual bool hit_test(Point local) const { return bounds().contains(local); }
  virtual bool hit_test_children(Point /*local*/) const { return true; }

  // internal: used by Window
  void mount(Window* w);
  void unmount();
  void set_hovered(bool h);
  void set_pressed(bool p);

protected:
  virtual void on_mount() {}
  virtual void on_unmount() {}
  // Install a deferred binder (runs on mount).
  void defer(std::function<void(View&)> binder);

  View* parent_ = nullptr;
  Window* window_ = nullptr;
  std::string id_;
  Style style_;
  Rect frame_{};
  std::vector<std::unique_ptr<View>> children_;
  std::vector<std::function<void(View&)>> binders_;
  std::vector<std::function<void()>> click_handlers_;
  std::vector<std::function<void(bool)>> hover_handlers_;
  std::function<void(View&)> ref_setter_;
  std::unique_ptr<pulse::composite_subscription> subs_;
  std::unique_ptr<pulse::signal<InputEvent>> events_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::string tooltip_;
  Cursor cursor_ = Cursor::Arrow;
  bool visible_ = true;
  bool enabled_ = true;
  bool hovered_ = false;
  bool pressed_ = false;
  bool focusable_ = false;
  bool hover_repaint_ = false; // repaint on hover / press changes

  template <class D>
  friend class ViewT;
  friend class Window;
};

namespace detail {
void post_to_ui(std::function<void()> fn);
bool on_ui_thread();
} // namespace detail

template <class T, class F>
void View::bind(const Bind<T>& b, F apply) {
  if (b.empty()) return;
  if (!b.is_dynamic()) {
    apply(*this, *b.value());
    return;
  }
  defer([b, apply](View& self) {
    std::weak_ptr<bool> alive = self.alive_;
    View* sp = &self;
    self.keep(b.attach([alive, sp, apply](const T& v) {
      if (detail::on_ui_thread()) {
        if (!alive.expired()) apply(*sp, v);
      } else {
        detail::post_to_ui([alive, sp, apply, v] {
          if (!alive.expired()) apply(*sp, v);
        });
      }
    }));
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// ViewT<D>: chainable modifiers returning the concrete view type.
//   Text("Hi").font_size(18).padding(8).background(th().surface)
// ─────────────────────────────────────────────────────────────────────────────
template <class D>
class ViewT : public View {
public:
  D& self() { return static_cast<D&>(*this); }

  PULSEUI_MOD(D, id, (std::string v), set_id(std::move(v));)
  PULSEUI_MOD(D, padding, (Insets p), style_.padding = p;)
  PULSEUI_MOD(D, width, (float w), style_.width = w;)
  PULSEUI_MOD(D, height, (float h), style_.height = h;)
  PULSEUI_MOD(D, size, (float w, float h), style_.width = w; style_.height = h;)
  PULSEUI_MOD(D, min_width, (float w), style_.min_width = w;)
  PULSEUI_MOD(D, min_height, (float h), style_.min_height = h;)
  PULSEUI_MOD(D, max_width, (float w), style_.max_width = w;)
  PULSEUI_MOD(D, max_height, (float h), style_.max_height = h;)
  PULSEUI_MOD(D, flex, (float grow = 1), style_.flex = grow;)
  PULSEUI_MOD(D, align, (Alignment a), style_.align = a;)
  PULSEUI_MOD(D, position, (float x, float y), style_.positioned = true; style_.position = {x, y};)
  PULSEUI_MOD(D, radius, (float r), style_.radius = r;)
  PULSEUI_MOD(D, clip, (bool c = true), style_.clip = c;)
  PULSEUI_MOD(D, opacity, (float o), style_.opacity = o;)
  PULSEUI_MOD(D, tooltip, (Bind<std::string> t), bind(t, [](View& v, const std::string& x) { v.set_tooltip(x); });)
  PULSEUI_MOD(D, cursor, (Cursor c), cursor_ = c;)
  PULSEUI_MOD(D, focusable, (bool f = true), focusable_ = f;)

  PULSEUI_MOD(D, background, (Bind<Color> c),
              bind(c, [](View& v, const Color& x) { v.style_.background = x; v.invalidate(); });)
  PULSEUI_MOD(D, border, (Bind<Color> c, float width = 1), style_.border_width = width;
              bind(c, [](View& v, const Color& x) { v.style_.border = x; v.invalidate(); });)
  PULSEUI_MOD(D, visible, (Bind<bool> b), bind(b, [](View& v, bool x) { v.set_visible(x); });)
  PULSEUI_MOD(D, enabled, (Bind<bool> b), bind(b, [](View& v, bool x) { v.set_enabled(x); });)

  // Any view can be clicked.
  PULSEUI_MOD(D, on_click, (std::function<void()> fn), click_handlers_.push_back(std::move(fn));
              if (cursor_ == Cursor::Arrow) cursor_ = Cursor::Hand;)
  PULSEUI_MOD(D, on_hover, (std::function<void(bool)> fn), hover_handlers_.push_back(std::move(fn));)
  // Raw event stream callback.
  PULSEUI_MOD(D, on_event_fn, (std::function<void(const InputEvent&)> fn),
              defer([fn](View& v) { v.keep(v.events().connect(fn).scoped()); });)
  // Get a pointer to the live view after mount (for imperative access).
  PULSEUI_MOD(D, ref, (D*& out), D** slot = &out;
              ref_setter_ = [slot](View& v) { *slot = static_cast<D*>(&v); };)
  // Lifecycle hook with access to the mounted view.
  PULSEUI_MOD(D, on_mount_fn, (std::function<void(D&)> fn),
              defer([fn](View& v) { fn(static_cast<D&>(v)); });)
};

} // namespace pulseui
