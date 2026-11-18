#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <pulse/core/observable.hpp>
#include <pulse/core/signal.hpp>
#include <pulseui/ui/containers.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui {

// ─────────────────────────────────────────────────────────────────────────────
// Text
// ─────────────────────────────────────────────────────────────────────────────
class Text : public ViewT<Text> {
public:
  Text() = default;
  Text(Bind<std::string> text);
  const char* type_name() const override { return "Text"; }

  PULSEUI_MOD(Text, font_size, (float s), font_size_ = s;)
  PULSEUI_MOD(Text, weight, (FontWeight w), weight_ = w;)
  PULSEUI_MOD(Text, bold, (), weight_ = FontWeight::Bold;)
  PULSEUI_MOD(Text, title, (), font_size_ = 20; weight_ = FontWeight::SemiBold;)
  PULSEUI_MOD(Text, dim, (), dim_ = true;)
  PULSEUI_MOD(Text, wrap, (bool w = true), wrap_ = w;)
  PULSEUI_MOD(Text, text_align, (int a), align_ = a;)
  PULSEUI_MOD(Text, color, (Bind<Color> c),
              bind(c, [](View& v, const Color& x) { static_cast<Text&>(v).set_color(x); });)
  PULSEUI_MOD(Text, mono, (), mono_ = true;)
  // Follow a value/stream after construction (e.g. on a view loaded from a form).
  PULSEUI_MOD(Text, bind_text, (Bind<std::string> t),
              bind(t, [](View& v, const std::string& x) { static_cast<Text&>(v).set_text(x); });)

  const std::string& text() const { return text_; }
  void set_text(std::string t);
  Font font() const;

  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;

  // designer access
  float font_size_value() const { return font_size_; }
  void set_font_size(float s) { font_size_ = s; invalidate_layout(); }
  FontWeight weight_value() const { return weight_; }
  void set_weight(FontWeight w) { weight_ = w; invalidate_layout(); }
  int text_align_value() const { return align_; }
  void set_text_align(int a) { align_ = a; invalidate(); }
  bool wrap_value() const { return wrap_; }
  void set_wrap(bool w) { wrap_ = w; invalidate_layout(); }
  std::optional<Color> color_value() const { return color_; }
  void set_color(std::optional<Color> c) { color_ = c; invalidate(); }

private:
  std::string text_;
  float font_size_ = 0; // 0 = theme default
  FontWeight weight_ = FontWeight::Normal;
  std::optional<Color> color_;
  int align_ = Align::LeftCenter;
  bool wrap_ = false, dim_ = false, mono_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Button
//   Button("Save", [&]{ save(); })
//   Button("+").on_click(...)            // same thing
//   btn.clicks() | pulse::throttle(...)  // clicks as a stream
// ─────────────────────────────────────────────────────────────────────────────
enum class ButtonKind { Normal, Primary, Flat, Danger };

class Button : public ViewT<Button> {
public:
  Button() : Button(std::string{}) {}
  Button(Bind<std::string> label, std::function<void()> on_click = {});
  const char* type_name() const override { return "Button"; }

  PULSEUI_MOD(Button, kind, (ButtonKind k), kind_ = k;)
  PULSEUI_MOD(Button, primary, (), kind_ = ButtonKind::Primary;)
  PULSEUI_MOD(Button, flat, (), kind_ = ButtonKind::Flat;)
  PULSEUI_MOD(Button, danger, (), kind_ = ButtonKind::Danger;)
  PULSEUI_MOD(Button, icon, (std::string glyph), icon_ = std::move(glyph);)
  PULSEUI_MOD(Button, bind_label, (Bind<std::string> t),
              bind(t, [](View& v, const std::string& x) { static_cast<Button&>(v).set_label(x); });)

  pulse::signal<> clicked;
  pulse::observable<pulse::unit> clicks() const { return clicked.as_observable(); }

  const std::string& label() const { return label_; }
  void set_label(std::string l) { label_ = std::move(l); invalidate_layout(); }
  ButtonKind kind_value() const { return kind_; }
  void set_kind(ButtonKind k) { kind_ = k; invalidate(); }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;

private:
  std::string label_, icon_;
  ButtonKind kind_ = ButtonKind::Normal;
};

// ─────────────────────────────────────────────────────────────────────────────
// TextField - single line text input.
//   TextField(name)                        // two-way bound to Model<std::string>
//   TextField().placeholder("Search").on_change([](auto& s){ ... })
// ─────────────────────────────────────────────────────────────────────────────
class TextField : public ViewT<TextField> {
public:
  TextField();
  explicit TextField(Model<std::string>& model);
  explicit TextField(Bind<std::string> text);
  const char* type_name() const override { return "TextField"; }

  PULSEUI_MOD(TextField, placeholder, (Bind<std::string> p),
              bind(p, [](View& v, const std::string& x) { static_cast<TextField&>(v).set_placeholder(x); });)
  PULSEUI_MOD(TextField, password, (bool p = true), password_ = p;)
  // Two-way bind to a model after construction.
  PULSEUI_MOD(TextField, model, (Model<std::string>& m), model_ = &m;
              bind(Bind<std::string>(m), [](View& v, const std::string& x) { static_cast<TextField&>(v).set_text(x); });)
  PULSEUI_MOD(TextField, on_change, (std::function<void(const std::string&)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(TextField, on_submit, (std::function<void(const std::string&)> fn), submitted.connect(std::move(fn));)
  PULSEUI_MOD(TextField, on_commit, (std::function<void(const std::string&)> fn), committed.connect(std::move(fn));)

  pulse::signal<std::string> changed;   // every edit
  pulse::signal<std::string> submitted; // Enter
  pulse::signal<std::string> committed; // Enter or focus lost (end of an edit)
  pulse::observable<std::string> text_changes() const { return changed.as_observable(); }

  const std::string& text() const { return text_; }
  void set_text(std::string t);
  const std::string& placeholder_value() const { return placeholder_; }
  void set_placeholder(std::string p) { placeholder_ = std::move(p); invalidate(); }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void on_focus(bool f) override;

private:
  void edit(std::string new_text, std::size_t caret);
  std::string display_text() const;
  std::size_t index_at(float x) const;
  void ensure_caret_visible();
  bool has_selection() const { return sel_ != caret_; }
  void delete_selection();

  Model<std::string>* model_ = nullptr;
  std::string text_, placeholder_;
  std::size_t caret_ = 0, sel_ = 0; // byte offsets on code point boundaries
  float scroll_ = 0;
  bool password_ = false, selecting_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Checkbox / Toggle - boolean inputs
//   Checkbox("Remember me", remember)   // Model<bool>&
// ─────────────────────────────────────────────────────────────────────────────
template <class D>
class BoolInputT : public ViewT<D> {
public:
  BoolInputT(Bind<std::string> label, Model<bool>& model) : model_(&model) {
    init(std::move(label));
    this->bind(Bind<bool>(model), [](View& v, bool b) { static_cast<BoolInputT&>(v).set_checked(b, false); });
  }
  BoolInputT(Bind<std::string> label, Bind<bool> value = false) {
    init(std::move(label));
    this->bind(value, [](View& v, bool b) { static_cast<BoolInputT&>(v).set_checked(b, false); });
  }
  PULSEUI_MOD(D, on_change, (std::function<void(bool)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(D, model, (Model<bool>& m), model_ = &m;
              this->bind(Bind<bool>(m), [](View& v, bool b) { static_cast<BoolInputT&>(v).set_checked(b, false); });)

  pulse::signal<bool> changed;
  bool checked() const { return checked_; }
  void set_checked(bool c, bool notify = true) {
    if (checked_ == c) return;
    checked_ = c;
    this->invalidate();
    if (notify) {
      if (model_) model_->set(c);
      changed.emit(c);
    }
  }
  const std::string& label() const { return label_; }
  void set_label(std::string l) { label_ = std::move(l); this->invalidate_layout(); }

  Size measure_content(Size avail) override;
  bool on_event(InputEvent& e) override;

protected:
  void init(Bind<std::string> label) {
    this->focusable_ = true;
    this->hover_repaint_ = true;
    this->cursor_ = Cursor::Hand;
    this->bind(label, [](View& v, const std::string& s) { static_cast<BoolInputT&>(v).set_label(s); });
  }
  virtual float indicator_width() const = 0;
  Model<bool>* model_ = nullptr;
  std::string label_;
  bool checked_ = false;
};

class Checkbox : public BoolInputT<Checkbox> {
public:
  using BoolInputT<Checkbox>::BoolInputT;
  Checkbox() : BoolInputT<Checkbox>(std::string{}) {}
  const char* type_name() const override { return "Checkbox"; }
  void paint_content(Painter& p) override;

protected:
  float indicator_width() const override { return 18; }
};

class Toggle : public BoolInputT<Toggle> {
public:
  using BoolInputT<Toggle>::BoolInputT;
  Toggle() : BoolInputT<Toggle>(std::string{}) {}
  const char* type_name() const override { return "Toggle"; }
  void paint_content(Painter& p) override;

protected:
  float indicator_width() const override { return 36; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Slider / ProgressBar
// ─────────────────────────────────────────────────────────────────────────────
class Slider : public ViewT<Slider> {
public:
  Slider() : Slider(Bind<double>(0.0)) {}
  Slider(Model<double>& model, double min = 0, double max = 1);
  explicit Slider(Bind<double> value, double min = 0, double max = 1);
  const char* type_name() const override { return "Slider"; }

  PULSEUI_MOD(Slider, step, (double s), step_ = s;)
  PULSEUI_MOD(Slider, on_change, (std::function<void(double)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(Slider, model, (Model<double>& m), model_ = &m;
              bind(Bind<double>(m), [](View& v, double x) { static_cast<Slider&>(v).set_value(x, false); });)
  pulse::signal<double> changed;

  double value() const { return value_; }
  void set_value(double v, bool notify = true);
  double min() const { return min_; }
  double max() const { return max_; }
  void set_range(double lo, double hi) { min_ = lo; max_ = hi; set_value(value_, false); }

  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;
  bool on_event(InputEvent& e) override;

private:
  void set_from_x(float x);
  Model<double>* model_ = nullptr;
  double value_ = 0, min_ = 0, max_ = 1, step_ = 0;
};

class ProgressBar : public ViewT<ProgressBar> {
public:
  ProgressBar() : ProgressBar(Bind<double>(0.0)) {}
  explicit ProgressBar(Bind<double> fraction);
  const char* type_name() const override { return "ProgressBar"; }
  PULSEUI_MOD(ProgressBar, bind_value, (Bind<double> f),
              bind(f, [](View& v, double x) { static_cast<ProgressBar&>(v).set_value(x); });)
  double value() const { return value_; }
  void set_value(double v);
  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;

private:
  double value_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Select - drop-down list
//   Select({"Low", "Medium", "High"}, quality)   // Model<int>& index
// ─────────────────────────────────────────────────────────────────────────────
class Select : public ViewT<Select> {
public:
  Select() : Select(Bind<std::vector<std::string>>(std::vector<std::string>{})) {}
  Select(Bind<std::vector<std::string>> options, Model<int>& index);
  explicit Select(Bind<std::vector<std::string>> options, Bind<int> index = 0);
  const char* type_name() const override { return "Select"; }

  PULSEUI_MOD(Select, on_change, (std::function<void(int)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(Select, model, (Model<int>& m), model_ = &m;
              bind(Bind<int>(m), [](View& v, int i) { static_cast<Select&>(v).set_index(i, false); });)
  PULSEUI_MOD(Select, bind_options, (Bind<std::vector<std::string>> o),
              bind(o, [](View& v, const std::vector<std::string>& x) { static_cast<Select&>(v).set_options(x); });)
  pulse::signal<int> changed;

  int index() const { return index_; }
  void set_index(int i, bool notify = true);
  const std::vector<std::string>& options() const { return options_; }
  void set_options(std::vector<std::string> o);

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;

private:
  void open_popup();
  Model<int>* model_ = nullptr;
  std::vector<std::string> options_;
  int index_ = 0;
  View* popup_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Segmented - a row of mutually exclusive options (mode switches).
//   Segmented({"Design", "Code"}, mode)    // Model<int>&
// ─────────────────────────────────────────────────────────────────────────────
class Segmented : public ViewT<Segmented> {
public:
  Segmented() : Segmented(Bind<std::vector<std::string>>(std::vector<std::string>{})) {}
  Segmented(Bind<std::vector<std::string>> options, Model<int>& index);
  explicit Segmented(Bind<std::vector<std::string>> options, Bind<int> index = 0);
  const char* type_name() const override { return "Segmented"; }

  PULSEUI_MOD(Segmented, on_change, (std::function<void(int)> fn), changed.connect(std::move(fn));)
  pulse::signal<int> changed;

  int index() const { return index_; }
  void set_index(int i, bool notify = true);
  void set_options(std::vector<std::string> o) { options_ = std::move(o); invalidate_layout(); }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;

private:
  std::vector<float> widths() const;
  int at(Point p) const;
  Model<int>* model_ = nullptr;
  std::vector<std::string> options_;
  int index_ = 0, hover_ = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// Tabs
//   Tabs{ Tab("General", general()), Tab("Advanced", advanced()) }
// ─────────────────────────────────────────────────────────────────────────────
struct Tab {
  Bind<std::string> title; // a value or a stream (e.g. a translation)
  Node content;
  Tab(Bind<std::string> t, Node c) : title(std::move(t)), content(std::move(c)) {}
};

class Tabs : public ViewT<Tabs> {
public:
  Tabs() = default;
  template <class... Ts>
    requires(sizeof...(Ts) > 0 && (std::is_same_v<std::decay_t<Ts>, Tab> && ...))
  Tabs(Ts&&... tabs) {
    (push_tab(std::move(tabs)), ...);
  }
  const char* type_name() const override { return "Tabs"; }

  PULSEUI_MOD(Tabs, selected, (Model<int>& m), bind_model(m);)
  PULSEUI_MOD(Tabs, add_tab, (Tab t), push_tab(std::move(t));)
  pulse::signal<int> changed;

  void bind_model(Model<int>& m);
  void push_tab(Tab t);
  int current() const { return current_; }
  void set_current(int i, bool notify = true);
  const std::vector<std::string>& titles() const { return titles_; }
  void set_title(int i, std::string t);

  Size measure_content(Size avail) override;
  void layout() override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;

private:
  float header_height() const;
  int tab_at(Point p) const;
  std::vector<std::string> titles_;
  int current_ = 0;
  int hover_tab_ = -1;
  Model<int>* model_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Split - two panes with a draggable divider.
// ─────────────────────────────────────────────────────────────────────────────
class Split : public ViewT<Split> {
public:
  Split(Node first, Node second, Orientation o = Orientation::Horizontal, float first_size = 240);
  const char* type_name() const override { return "Split"; }
  PULSEUI_MOD(Split, min_pane, (float m), min_pane_ = m;)
  // Which pane keeps its size on resize (default: first).
  PULSEUI_MOD(Split, keep_second, (), keep_second_ = true;)

  Size measure_content(Size avail) override;
  void layout() override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  bool hit_test_children(Point local) const override;

private:
  Rect handle_rect() const;
  Orientation orientation_;
  float pane_ = 240, min_pane_ = 80;
  bool keep_second_ = false, dragging_ = false, handle_hover_ = false;
  float drag_from_ = 0, pane_from_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Divider, ImageView, Canvas
// ─────────────────────────────────────────────────────────────────────────────
class Divider : public ViewT<Divider> {
public:
  explicit Divider(Orientation o = Orientation::Horizontal) : orientation_(o) {}
  const char* type_name() const override { return "Divider"; }
  Orientation orientation() const { return orientation_; }
  void set_orientation(Orientation o) { orientation_ = o; invalidate(); }
  Size measure_content(Size) override;
  void paint_content(Painter& p) override;

private:
  Orientation orientation_;
};

class ImageView : public ViewT<ImageView> {
public:
  ImageView() = default;
  explicit ImageView(Bind<Image> img);
  explicit ImageView(std::string path) { set_source(std::move(path)); }
  const char* type_name() const override { return "Image"; }
  const std::string& source() const { return source_; }
  void set_source(std::string path) {
    source_ = std::move(path);
    set_image(source_.empty() ? Image{} : Image::load(source_));
  }
  Size measure_content(Size) override;
  void paint_content(Painter& p) override;
  void set_image(Image i) { image_ = std::move(i); invalidate_layout(); }

private:
  Image image_;
  std::string source_;
};

// Custom drawing. Redraws automatically when any `redraw_on` stream emits.
//   Canvas([&](Painter& p, Rect r){ ... }).redraw_on(pos.as_observable())
class Canvas : public ViewT<Canvas> {
public:
  using DrawFn = std::function<void(Painter&, const Rect&)>;
  explicit Canvas(DrawFn fn) : draw_(std::move(fn)) {}
  const char* type_name() const override { return "Canvas"; }

  template <class T>
  Canvas& redraw_on(pulse::observable<T> o) & {
    bind(Bind<T>(std::move(o)), [](View& v, const T&) { v.invalidate(); });
    return *this;
  }
  template <class T>
  Canvas&& redraw_on(pulse::observable<T> o) && {
    return std::move(redraw_on(std::move(o)));
  }
  template <class Src>
    requires detail::has_as_observable<Src>
  Canvas& redraw_on(Src& src) & {
    return redraw_on(src.as_observable());
  }
  template <class Src>
    requires detail::has_as_observable<Src>
  Canvas&& redraw_on(Src& src) && {
    return std::move(redraw_on(src.as_observable()));
  }
  void paint_content(Painter& p) override {
    if (draw_) draw_(p, content_rect());
  }

private:
  DrawFn draw_;
};

namespace detail {
Size bool_input_measure(const std::string& label, float indicator_w);
bool bool_input_event(View& v, InputEvent& e, bool& toggled);
} // namespace detail

template <class D>
Size BoolInputT<D>::measure_content(Size) {
  return detail::bool_input_measure(label_, indicator_width());
}
template <class D>
bool BoolInputT<D>::on_event(InputEvent& e) {
  bool toggled = false;
  const bool handled = detail::bool_input_event(*this, e, toggled);
  if (toggled) set_checked(!checked_);
  return handled;
}

} // namespace pulseui
