#pragma once
#include <functional>
#include <memory>
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
class Show : public ViewT<Show> {
public:
  Show(Bind<bool> cond, Node then, Node otherwise = {});
  const char* type_name() const override { return "Show"; }
  Size measure_content(Size avail) override;
  void layout() override;

private:
  void apply(bool c);
  bool has_else_ = false;
};

// ForEach: renders one child per item of a reactive list.
//   ForEach(todos, [](const Todo& t, std::size_t i) { return Text(t.title); })
// The children are rebuilt whenever the list changes.
template <class T>
class ForEach : public StackT<ForEach<T>> {
public:
  using ItemFn = std::function<Node(const T&, std::size_t)>;

  template <class F>
  ForEach(Bind<std::vector<T>> items, F fn) : fn_(make_fn(std::move(fn))) {
    this->spacing_ = 4;
    this->bind(items, [](View& v, const std::vector<T>& list) {
      static_cast<ForEach&>(v).rebuild(list);
    });
  }
  const char* type_name() const override { return "ForEach"; }
  PULSEUI_MOD(ForEach, horizontal, (), this->orientation_ = Orientation::Horizontal;)

private:
  template <class F>
  static ItemFn make_fn(F fn) {
    if constexpr (std::is_invocable_v<F&, const T&, std::size_t>)
      return ItemFn(std::move(fn));
    else
      return [fn](const T& v, std::size_t) { return Node(fn(v)); };
  }
  void rebuild(const std::vector<T>& list) {
    this->clear_children();
    for (std::size_t i = 0; i < list.size(); ++i) this->add(fn_(list[i], i));
    this->invalidate_layout();
  }
  ItemFn fn_;
};

// property / computed of a vector
template <class Src, class F>
  requires detail::has_as_observable<Src>
ForEach(Src&, F) -> ForEach<typename Src::value_type::value_type>;
// a stream of vectors
template <class U, class F>
ForEach(pulse::observable<std::vector<U>>, F) -> ForEach<U>;
// a plain vector
template <class U, class F>
ForEach(std::vector<U>, F) -> ForEach<U>;

namespace detail {
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
