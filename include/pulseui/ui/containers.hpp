#pragma once
#include <functional>
#include <memory>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulseui/ui/view.hpp>

namespace pulseui {

enum class Justify { Start, Center, End, SpaceBetween };

// ─────────────────────────────────────────────────────────────────────────────
// Stacks: the main layout primitive (flexbox-like, one axis).
//   VStack{ Text("Title"), HStack{ Button("Ok"), Button("Cancel") }.spacing(8) }
// Children keep their preferred size on the main axis; `flex(n)` children
// share the remaining space. On the cross axis children stretch by default.
// ─────────────────────────────────────────────────────────────────────────────
template <class D>
class StackT : public ViewT<D> {
public:
  StackT() = default;
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, D> && ...)))
  StackT(C&&... children) {
    (this->add(Node(std::forward<C>(children))), ...);
  }
  explicit StackT(std::vector<Node> children) {
    for (auto& c : children) this->add(std::move(c));
  }

  PULSEUI_MOD(D, spacing, (float s), spacing_ = s;)
  PULSEUI_MOD(D, align_items, (Alignment a), cross_ = a;)
  PULSEUI_MOD(D, justify, (Justify j), justify_ = j;)

  float spacing_value() const { return spacing_; }
  Alignment cross_alignment() const { return cross_; }
  Justify justification() const { return justify_; }
  Orientation orientation() const { return orientation_; }
  void set_orientation(Orientation o) { orientation_ = o; this->invalidate_layout(); }
  void set_spacing(float s) { spacing_ = s; this->invalidate_layout(); }
  void set_align_items(Alignment a) { cross_ = a; this->invalidate_layout(); }
  void set_justify(Justify j) { justify_ = j; this->invalidate_layout(); }

  Size measure_content(Size avail) override;
  void layout() override;

protected:
  Orientation orientation_ = Orientation::Vertical;
  float spacing_ = 8;
  Alignment cross_ = Alignment::Stretch;
  Justify justify_ = Justify::Start;
};

class VStack : public StackT<VStack> {
public:
  using StackT<VStack>::StackT;
  VStack() = default;
  const char* type_name() const override { return "VStack"; }
};

class HStack : public StackT<HStack> {
public:
  HStack() { orientation_ = Orientation::Horizontal; }
  explicit HStack(std::vector<Node> children) : StackT<HStack>(std::move(children)) {
    orientation_ = Orientation::Horizontal;
  }
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, HStack> && ...)))
  HStack(C&&... children) : StackT<HStack>(std::forward<C>(children)...) {
    orientation_ = Orientation::Horizontal;
  }
  const char* type_name() const override { return "HStack"; }
};

// Takes all the free space in a stack.
class Spacer : public ViewT<Spacer> {
public:
  Spacer() { style_.flex = 1; }
  explicit Spacer(float fixed) { style_.width = fixed; style_.height = fixed; }
  const char* type_name() const override { return "Spacer"; }
};

// Layer: children are placed on top of each other. A child fills the layer
// unless it has an absolute position(x, y) - then it keeps its own size.
// Used for overlays and free-form (designer) layouts.
class Layer : public ViewT<Layer> {
public:
  Layer() = default;
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, Layer> && ...)))
  Layer(C&&... children) {
    (add(Node(std::forward<C>(children))), ...);
  }
  const char* type_name() const override { return "Layer"; }
  Size measure_content(Size avail) override;
  void layout() override;
};

// Panel: a VStack with card styling (surface background, border, radius, padding).
class Panel : public StackT<Panel> {
public:
  Panel();
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, Panel> && ...)))
  Panel(C&&... children) : Panel() {
    (add(Node(std::forward<C>(children))), ...);
  }
  const char* type_name() const override { return "Panel"; }
  void paint(Painter& p) override;
};

// Scroll: vertical (and optionally horizontal) scrolling of one content view.
class Scroll : public ViewT<Scroll> {
public:
  Scroll() { style_.clip = true; }
  explicit Scroll(Node content) : Scroll() { add(std::move(content)); }
  const char* type_name() const override { return "Scroll"; }

  PULSEUI_MOD(Scroll, horizontal, (bool h = true), horizontal_ = h;)
  float offset_y() const { return offset_.y; }
  void scroll_to(float y);
  void scroll_into_view(const Rect& content_rect);

  Size measure_content(Size avail) override;
  void layout() override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  bool hit_test_children(Point local) const override;

private:
  Rect thumb_rect(bool vertical) const;
  Point offset_{};
  Size content_size_{};
  bool horizontal_ = false;
  bool dragging_ = false;
  float drag_start_ = 0, drag_offset_start_ = 0;
};

// Show: displays `then` while the condition is true, otherwise `otherwise`.
// Both branches stay mounted (their state survives toggling).
// .transition() cross-fades the branches and grows/shrinks the space they take
// (a Show without `otherwise` then expands and collapses smoothly).
class Show : public ViewT<Show> {
public:
  Show(Bind<bool> cond, Node then, Node otherwise = {});
  const char* type_name() const override { return "Show"; }
  PULSEUI_MOD(Show, transition, (Transition tr = {}), transition_ = tr;)
  PULSEUI_MOD(Show, transition, (std::chrono::milliseconds d, Easing e), transition_ = Transition(d, e);)
  bool condition() const { return cond_; }
  bool transitioning() const { return from_ >= 0 || to_ >= 0; }
  Size measure_content(Size avail) override;
  void layout() override;

private:
  void apply(bool c);
  void finish_transition();
  View* branch(int i) const { return i >= 0 && i < static_cast<int>(children_.size()) ? children_[static_cast<std::size_t>(i)].get() : nullptr; }
  bool has_else_ = false;
  bool cond_ = false, initialized_ = false, clip_before_ = false;
  std::optional<Transition> transition_;
  pulse::subscription anim_;
  float t_ = 1;
  int from_ = -1, to_ = -1;
};

namespace detail {
Size wrap_measure(View& v, float gap, float line_gap, Size avail);
void wrap_layout(View& v, float gap, float line_gap, Justify justify);
} // namespace detail

// ForEach: renders one child per item of a reactive list.
//   ForEach(todos, [](const Todo& t, std::size_t i) { return Text(t.title); })
// Without a key the children are rebuilt whenever the list changes.
//
// Keyed: ForEach(todos, [](const Todo& t) { return t.id; }, row_fn)
// Rows are matched by key: unchanged rows are kept (with their state: focus,
// selection, scroll, text being typed), moved rows are moved, and only new or
// changed items build a new row. A changed item keeps its row while the row
// has the keyboard focus, so typing in a row is never interrupted.
namespace detail {
const View* focused_view(const Window& w);
template <class K>
std::string key_string(const K& k) {
  if constexpr (std::is_arithmetic_v<K>) return std::to_string(k);
  else return std::string(k);
}
template <class T>
bool same_item(const T& a, const T& b) {
  if constexpr (requires { { a == b } -> std::convertible_to<bool>; }) return a == b;
  else return true; // not comparable: a row with the same key is reused
}
} // namespace detail

template <class T>
class ForEach : public StackT<ForEach<T>> {
public:
  using ItemFn = std::function<Node(const T&, std::size_t)>;
  using KeyFn = std::function<std::string(const T&)>;

  template <class F>
  ForEach(Bind<std::vector<T>> items, F fn) : fn_(make_fn(std::move(fn))) {
    this->spacing_ = 4;
    this->bind(items, [](View& v, const std::vector<T>& list) { static_cast<ForEach&>(v).rebuild(list); });
  }
  template <class Key, class F>
    requires std::is_invocable_v<Key&, const T&>
  ForEach(Bind<std::vector<T>> items, Key key, F fn)
      : fn_(make_fn(std::move(fn))), key_([key](const T& v) mutable { return detail::key_string(key(v)); }) {
    this->spacing_ = 4;
    this->bind(items, [](View& v, const std::vector<T>& list) { static_cast<ForEach&>(v).rebuild(list); });
  }
  const char* type_name() const override { return "ForEach"; }
  PULSEUI_MOD(ForEach, horizontal, (), this->orientation_ = Orientation::Horizontal;)
  // Rows side by side, wrapping to the next line like Wrap (cards, tags, thumbnails).
  PULSEUI_MOD(ForEach, wrap, (bool w = true), wrap_ = w; this->orientation_ = Orientation::Horizontal;)
  bool keyed() const { return static_cast<bool>(key_); }
  Size measure_content(Size avail) override {
    return wrap_ ? detail::wrap_measure(*this, this->spacing_, this->spacing_, avail) : StackT<ForEach<T>>::measure_content(avail);
  }
  void layout() override {
    if (wrap_) detail::wrap_layout(*this, this->spacing_, this->spacing_, this->justify_);
    else StackT<ForEach<T>>::layout();
  }

private:
  template <class F>
  static ItemFn make_fn(F fn) {
    if constexpr (std::is_invocable_v<F&, const T&, std::size_t>)
      return ItemFn(std::move(fn));
    else
      return [fn](const T& v, std::size_t) { return Node(fn(v)); };
  }
  void rebuild(const std::vector<T>& list) {
    if (!key_) {
      this->clear_children();
      for (std::size_t i = 0; i < list.size(); ++i) this->add(fn_(list[i], i));
      this->invalidate_layout();
      return;
    }
    // keyed: match the old rows by key
    std::vector<std::string> keys;
    keys.reserve(list.size());
    for (auto& item : list) keys.push_back(key_(item));
    const View* focus = this->window() ? detail::focused_view(*this->window()) : nullptr;
    std::vector<View*> rows(list.size(), nullptr);
    std::vector<bool> used(rows_.size(), false);
    for (std::size_t i = 0; i < list.size(); ++i) {
      for (std::size_t j = 0; j < rows_.size(); ++j) {
        if (used[j] || keys_[j] != keys[i]) continue;
        const bool focused = focus && rows_[j]->contains(focus);
        if (focused || detail::same_item(items_[j], list[i])) {
          rows[i] = rows_[j];
          used[j] = true;
        }
        break;
      }
    }
    for (std::size_t j = 0; j < rows_.size(); ++j)
      if (!used[j]) this->take(rows_[j]);
    for (std::size_t i = 0; i < list.size(); ++i)
      if (!rows[i]) rows[i] = &this->add(fn_(list[i], i));
    this->reorder(rows);
    rows_ = std::move(rows);
    keys_ = std::move(keys);
    items_ = list;
  }
  ItemFn fn_;
  KeyFn key_;
  bool wrap_ = false;
  std::vector<View*> rows_;
  std::vector<std::string> keys_;
  std::vector<T> items_;
};

// property / computed of a vector
template <class Src, class F>
  requires detail::has_as_observable<Src>
ForEach(Src&, F) -> ForEach<typename Src::value_type::value_type>;
// a stream of vectors
template <class U, class F>
ForEach(pulse::observable<std::vector<U>>, F) -> ForEach<U>;
template <class Src, class K, class F>
  requires detail::has_as_observable<Src>
ForEach(Src&, K, F) -> ForEach<typename Src::value_type::value_type>;
template <class U, class K, class F>
ForEach(pulse::observable<std::vector<U>>, K, F) -> ForEach<U>;
// a plain vector
template <class U, class F>
ForEach(std::vector<U>, F) -> ForEach<U>;
template <class U, class K, class F>
ForEach(std::vector<U>, K, F) -> ForEach<U>;

// ─────────────────────────────────────────────────────────────────────────────
// Grid: rows and columns.
//   Grid{ Text("Name"), TextField(name), Text("Email"), TextField(email) }
//       .columns({Track::automatic(), Track::fr(1)}).spacing(8)
// Children fill the cells row by row; .cell(row, col) places one explicitly,
// .span(n) makes it span n columns. Rows are as tall as their tallest child.
// ─────────────────────────────────────────────────────────────────────────────
struct Track {
  enum Kind { Fixed, Auto, Fraction } kind = Fraction;
  float value = 1;
  static Track px(float v) { return {Fixed, v}; }
  static Track automatic() { return {Auto, 0}; }
  static Track fr(float v = 1) { return {Fraction, v}; }
};

class Grid : public ViewT<Grid> {
public:
  Grid() = default;
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, Grid> && ...)))
  Grid(C&&... children) {
    (add(Node(std::forward<C>(children))), ...);
  }
  explicit Grid(std::vector<Node> children) {
    for (auto& c : children) add(std::move(c));
  }
  const char* type_name() const override { return "Grid"; }

  PULSEUI_MOD(Grid, columns, (int n), tracks_.assign(static_cast<std::size_t>(std::max(1, n)), Track::fr(1));)
  PULSEUI_MOD(Grid, columns, (std::vector<Track> t), tracks_ = std::move(t);)
  PULSEUI_MOD(Grid, spacing, (float s), row_gap_ = col_gap_ = s;)
  PULSEUI_MOD(Grid, gaps, (float rows, float cols), row_gap_ = rows; col_gap_ = cols;)
  PULSEUI_MOD(Grid, align_items, (Alignment a), align_ = a;)

  int column_count() const { return std::max<int>(1, static_cast<int>(tracks_.size())); }
  const std::vector<Track>& tracks() const { return tracks_; }
  float row_gap() const { return row_gap_; }
  float column_gap() const { return col_gap_; }
  void set_columns(std::vector<Track> t) { tracks_ = std::move(t); invalidate_layout(); }
  void set_gaps(float rows, float cols) { row_gap_ = rows; col_gap_ = cols; invalidate_layout(); }

  Size measure_content(Size avail) override;
  void layout() override;

  struct Placement {
    View* view;
    int row, col, row_span, col_span;
  };
  std::vector<Placement> placements() const;

private:
  std::vector<float> column_widths(const std::vector<Placement>& ps, float avail) const;
  std::vector<float> row_heights(const std::vector<Placement>& ps, const std::vector<float>& cols) const;
  std::vector<Track> tracks_{Track::fr(1)};
  float row_gap_ = 8, col_gap_ = 8;
  Alignment align_ = Alignment::Stretch;
};

// ─────────────────────────────────────────────────────────────────────────────
// Wrap: children in a row that wraps to the next line (tags, chips, toolbars).
//   Wrap{ chip("c++"), chip("ui"), ... }.spacing(6)
// ─────────────────────────────────────────────────────────────────────────────
class Wrap : public ViewT<Wrap> {
public:
  Wrap() = default;
  template <class... C>
    requires(sizeof...(C) > 0 && (std::is_convertible_v<C&&, Node> && ...) &&
             !(sizeof...(C) == 1 && (std::is_same_v<std::decay_t<C>, Wrap> && ...)))
  Wrap(C&&... children) {
    (add(Node(std::forward<C>(children))), ...);
  }
  explicit Wrap(std::vector<Node> children) {
    for (auto& c : children) add(std::move(c));
  }
  const char* type_name() const override { return "Wrap"; }
  PULSEUI_MOD(Wrap, spacing, (float s), gap_ = line_gap_ = s;)
  PULSEUI_MOD(Wrap, line_spacing, (float s), line_gap_ = s;)
  PULSEUI_MOD(Wrap, justify, (Justify j), justify_ = j;)
  float spacing_value() const { return gap_; }
  void set_spacing(float s) { gap_ = line_gap_ = s; invalidate_layout(); }

  Size measure_content(Size avail) override;
  void layout() override;

private:
  float gap_ = 6, line_gap_ = 6;
  Justify justify_ = Justify::Start;
};

namespace detail {
Size wrap_measure(View& v, float gap, float line_gap, Size avail);
void wrap_layout(View& v, float gap, float line_gap, Justify justify);
Size stack_measure(View& v, Orientation o, float spacing, Size avail);
void stack_layout(View& v, Orientation o, float spacing, Alignment cross, Justify j);
} // namespace detail

template <class D>
Size StackT<D>::measure_content(Size avail) {
  return detail::stack_measure(*this, orientation_, spacing_, avail);
}
template <class D>
void StackT<D>::layout() {
  detail::stack_layout(*this, orientation_, spacing_, cross_, justify_);
}

} // namespace pulseui
