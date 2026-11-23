#pragma once
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include <pulse/core/composite_subscription.hpp>
#include <pulse/core/signal.hpp>
#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/gfx/painter.hpp>
#include <optional>

#include <pulse/core/subscription.hpp>
#include <pulseui/ui/accessibility.hpp>
#include <pulseui/ui/animation.hpp>
#include <pulseui/ui/bind.hpp>
#include <pulseui/ui/events.hpp>
#include <pulseui/ui/menu.hpp>

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
  // placement inside a Grid (-1 = next free cell)
  int grid_row = -1, grid_col = -1, row_span = 1, col_span = 1;
};

// Rarely used per-view data, allocated on first use so plain views stay small.
struct ViewExtras {
  MenuProvider context_menu; // right click / Shift+F10 / the Menu key
  // .animate(): frame, opacity, background and border changes glide
  std::optional<Transition> transition;
  Rect frame_target{};
  bool frame_valid = false;
  pulse::subscription frame_anim, opacity_anim, background_anim, border_anim;
  // drag and drop
  std::function<void(const std::vector<std::string>&)> on_drop_files;
  std::vector<std::string> drop_extensions; // ".png" (lower case); empty = any file
  std::function<void(const std::string&)> on_drop_text;
  std::function<DragData()> drag_source;
  // accessibility overrides
  std::optional<Role> a11y_role;
  std::optional<std::string> a11y_label;
  std::string a11y_description;
  bool a11y_hidden = false;
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
  // Expires when the view is destroyed (to check a stored View* before use).
  std::weak_ptr<bool> alive_token() const { return alive_; }
  const std::vector<std::unique_ptr<View>>& children() const { return children_; }

  View& add(Node n);
  View& insert(std::size_t index, Node n);
  std::unique_ptr<View> take(View* child);
  void remove(View* child) { take(child); }
  void clear_children();
  void move_child(View* child, std::size_t new_index);
  // Puts the children in this order without unmounting them (all must be children; others go last).
  void reorder(const std::vector<View*>& order);
  bool contains(const View* v) const; // v is this view or one of its descendants
  int index_of(const View* child) const;

  View* find(std::string_view id);
  template <class V>
  V* find(std::string_view id) { return dynamic_cast<V*>(find(id)); }
  // Like find, but the view must exist and have type V; otherwise throws
  // std::runtime_error naming the id and the type that was found.
  template <class V = View>
  V& get(std::string_view id);

  // ── annotations ──────────────────────────────────────────────────────────
  // Free-form key/value data attached to a view. Forms use it to carry
  // designer bindings ("bind.text" -> "email", "on.click" -> "submit").
  const std::string* annotation(std::string_view key) const;
  void set_annotation(std::string_view key, std::string value); // empty value removes
  const std::vector<std::pair<std::string, std::string>>& annotations() const;

  // ── style / geometry ─────────────────────────────────────────────────────
  Style& style() { return style_; }
  const Style& style() const { return style_; }
  const Rect& frame() const { return frame_; }         // relative to parent
  Rect bounds() const { return {0, 0, frame_.w, frame_.h}; }
  Rect content_rect() const { return bounds().inset(style_.padding); }
  Point to_window(Point local) const;
  Point from_window(Point p) const { return p - to_window({0, 0}); }
  void set_frame(const Rect& r);

  // Appearance setters (animated when the view has .animate()).
  void set_opacity(float o);
  void set_background(Color c);
  void set_border_color(Color c);
  bool animated() const { return extras_ && extras_->transition.has_value(); }
  const Rect& target_frame() const { return extras_ && extras_->frame_valid ? extras_->frame_target : frame_; }

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

  // ── extras ───────────────────────────────────────────────────────────────
  ViewExtras& extras();                                   // created on first use
  const ViewExtras* extras_if() const { return extras_.get(); }
  bool has_context_menu() const { return extras_ && extras_->context_menu; }
  // Would this view take the dragged data? (files filtered by extension)
  bool accepts_drop(const DragData& d) const;
  // Delivers a drop; returns false when nothing was taken.
  bool deliver_drop(const DragData& d);

  // ── accessibility ────────────────────────────────────────────────────────
  // Describes the view for screen readers. Overrides call View::accessibility
  // first (focus/enabled states, tooltip) and then fill in role, name and value.
  virtual void accessibility(A11yInfo& info) const;
  // Performs an action asked for by a screen reader. Returns false if unsupported.
  virtual bool accessibility_action(A11yAction a, const std::string& arg = {});
  // accessibility() with the .a11y_*() overrides applied
  A11yInfo a11y_info() const;
  bool a11y_hidden() const { return extras_ && extras_->a11y_hidden; }

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
  std::unique_ptr<std::vector<std::pair<std::string, std::string>>> annotations_;
  std::unique_ptr<ViewExtras> extras_;

  template <class D, class Base>
  friend class ViewT;
  friend class Window;
};

namespace detail {
void post_to_ui(std::function<void()> fn);
bool on_ui_thread();
[[noreturn]] void throw_view_lookup(std::string_view id, const char* wanted, View* found);
} // namespace detail

template <class V>
V& View::get(std::string_view id) {
  View* v = find(id);
  if (auto* typed = dynamic_cast<V*>(v)) return *typed;
  detail::throw_view_lookup(id, typeid(V).name(), v);
}

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
// `Base` lets a non-template view class (with code in a .cpp) get the
// modifiers too: class List : public ViewT<List, ListBase>.
template <class D, class Base = View>
class ViewT : public Base {
public:
  D& self() { return static_cast<D&>(*this); }

  // The modifiers below would hide these getters of View: keep both usable
  // (text_field->id() reads, text_field->id("x") sets).
  using View::cursor;
  using View::id;
  using View::tooltip;
  using Base::Base;

  PULSEUI_MOD(D, id, (std::string v), this->set_id(std::move(v));)
  PULSEUI_MOD(D, padding, (Insets p), this->style_.padding = p;)
  PULSEUI_MOD(D, width, (float w), this->style_.width = w;)
  PULSEUI_MOD(D, height, (float h), this->style_.height = h;)
  PULSEUI_MOD(D, size, (float w, float h), this->style_.width = w; this->style_.height = h;)
  PULSEUI_MOD(D, min_width, (float w), this->style_.min_width = w;)
  PULSEUI_MOD(D, min_height, (float h), this->style_.min_height = h;)
  PULSEUI_MOD(D, max_width, (float w), this->style_.max_width = w;)
  PULSEUI_MOD(D, max_height, (float h), this->style_.max_height = h;)
  PULSEUI_MOD(D, flex, (float grow = 1), this->style_.flex = grow;)
  PULSEUI_MOD(D, align, (Alignment a), this->style_.align = a;)
  PULSEUI_MOD(D, position, (float x, float y), this->style_.positioned = true; this->style_.position = {x, y};)
  PULSEUI_MOD(D, radius, (float r), this->style_.radius = r;)
  PULSEUI_MOD(D, clip, (bool c = true), this->style_.clip = c;)
  PULSEUI_MOD(D, opacity, (Bind<float> o), this->bind(o, [](View& v, float x) { v.set_opacity(x); });)
  // Changes of frame (position and size), opacity, background and border glide
  // instead of jumping: .animate(), .animate(300ms), .animate(250ms, Easing::Spring)
  PULSEUI_MOD(D, animate, (Transition tr = {}), this->extras().transition = tr;)
  PULSEUI_MOD(D, animate, (std::chrono::milliseconds d, Easing e), this->extras().transition = Transition(d, e);)
  PULSEUI_MOD(D, tooltip, (Bind<std::string> t), this->bind(t, [](View& v, const std::string& x) { v.set_tooltip(x); });)
  PULSEUI_MOD(D, cursor, (Cursor c), this->cursor_ = c;)
  PULSEUI_MOD(D, focusable, (bool f = true), this->focusable_ = f;)
  // Grid placement: .cell(row, col) and spans; .span(n) spans n columns in auto-flow.
  PULSEUI_MOD(D, cell, (int row, int col, int row_span = 1, int col_span = 1), this->style_.grid_row = row;
              this->style_.grid_col = col; this->style_.row_span = std::max(1, row_span); this->style_.col_span = std::max(1, col_span);)
  PULSEUI_MOD(D, span, (int cols), this->style_.col_span = std::max(1, cols);)

  PULSEUI_MOD(D, background, (Bind<Color> c), this->bind(c, [](View& v, const Color& x) { v.set_background(x); });)
  PULSEUI_MOD(D, border, (Bind<Color> c, float width = 1), this->style_.border_width = width;
              this->bind(c, [](View& v, const Color& x) { v.set_border_color(x); });)
  PULSEUI_MOD(D, visible, (Bind<bool> b), this->bind(b, [](View& v, bool x) { v.set_visible(x); });)
  PULSEUI_MOD(D, enabled, (Bind<bool> b), this->bind(b, [](View& v, bool x) { v.set_enabled(x); });)

  // Any view can be clicked.
  PULSEUI_MOD(D, on_click, (std::function<void()> fn), this->click_handlers_.push_back(std::move(fn));
              if (this->cursor_ == Cursor::Arrow) this->cursor_ = Cursor::Hand;)
  PULSEUI_MOD(D, on_hover, (std::function<void(bool)> fn), this->hover_handlers_.push_back(std::move(fn));)
  // Raw event stream callback.
  PULSEUI_MOD(D, on_event_fn, (std::function<void(const InputEvent&)> fn),
              this->defer([fn](View& v) { v.keep(v.events().connect(fn).scoped()); });)
  // A context menu (right click, Shift+F10). The function form builds the
  // entries when the menu opens, e.g. from the row under the mouse.
  PULSEUI_MOD(D, context_menu, (MenuItems items), this->extras().context_menu = [items] { return items; };)
  PULSEUI_MOD(D, context_menu, (MenuProvider fn), this->extras().context_menu = std::move(fn);)
  // Files dragged from Explorer / Finder (or another app) onto this view. While
  // they hover over it the view is highlighted. Extensions filter what is taken.
  //   ImageView(path).on_drop_files([&](auto& files) { path = files[0]; }, {".png", ".jpg"})
  PULSEUI_MOD(D, on_drop_files, (std::function<void(const std::vector<std::string>&)> fn, std::vector<std::string> extensions = {}),
              this->extras().on_drop_files = std::move(fn); this->extras().drop_extensions = std::move(extensions);)
  PULSEUI_MOD(D, on_drop_text, (std::function<void(const std::string&)> fn), this->extras().on_drop_text = std::move(fn);)
  // Dragging this view out starts a system drag with the returned files/text
  // (drop them into Explorer, Finder, an editor or another view).
  PULSEUI_MOD(D, drag_source, (std::function<DragData()> fn), this->extras().drag_source = std::move(fn);)
  // Accessibility overrides: the name a screen reader announces, the role, a
  // longer description, or hiding a decorative view (and its children).
  PULSEUI_MOD(D, a11y_label, (Bind<std::string> s),
              this->bind(s, [](View& v, const std::string& x) { v.extras().a11y_label = x; });)
  PULSEUI_MOD(D, a11y_role, (Role r), this->extras().a11y_role = r;)
  PULSEUI_MOD(D, a11y_description, (std::string s), this->extras().a11y_description = std::move(s);)
  PULSEUI_MOD(D, a11y_hidden, (bool h = true), this->extras().a11y_hidden = h;)
  // Get a pointer to the live view after mount (for imperative access).
  PULSEUI_MOD(D, ref, (D*& out), D** slot = &out;
              this->ref_setter_ = [slot](View& v) { *slot = static_cast<D*>(&v); };)
  // Lifecycle hook with access to the mounted view.
  PULSEUI_MOD(D, on_mount_fn, (std::function<void(D&)> fn),
              this->defer([fn](View& v) { fn(static_cast<D&>(v)); });)
};

} // namespace pulseui
