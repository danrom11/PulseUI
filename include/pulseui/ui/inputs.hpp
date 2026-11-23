#pragma once
// More input views: TextArea, NumberField, RadioGroup, Calendar / DatePicker,
// ColorPanel / ColorPicker, and toast notifications.
#include <chrono>
#include <climits>
#include <cmath>
#include <compare>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <pulse/core/signal.hpp>
#include <pulseui/ui/collections.hpp>
#include <pulseui/ui/view.hpp>
#include <pulseui/ui/widgets.hpp>

namespace pulseui {

class Window;

// ─────────────────────────────────────────────────────────────────────────────
// TextArea - multi-line plain text with word wrap.
//   TextArea(notes).placeholder("Notes").rows(6)       // Model<std::string>&, two-way
// Keys: arrows (Ctrl: by word), Home/End (Ctrl: whole text), PageUp/PageDown,
// Ctrl+A/C/X/V, Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z). Tab moves the focus on.
// ─────────────────────────────────────────────────────────────────────────────
class TextArea : public ViewT<TextArea> {
public:
  TextArea();
  explicit TextArea(Model<std::string>& model);
  explicit TextArea(Bind<std::string> text);
  const char* type_name() const override { return "TextArea"; }

  PULSEUI_MOD(TextArea, placeholder, (Bind<std::string> p),
              bind(p, [](View& v, const std::string& x) { static_cast<TextArea&>(v).set_placeholder(x); });)
  PULSEUI_MOD(TextArea, model, (Model<std::string>& m), model_ = &m;
              bind(Bind<std::string>(m), [](View& v, const std::string& x) { static_cast<TextArea&>(v).set_text(x); });)
  PULSEUI_MOD(TextArea, on_change, (std::function<void(const std::string&)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(TextArea, bind_text, (Bind<std::string> t),
              bind(t, [](View& v, const std::string& x) { static_cast<TextArea&>(v).set_text(x); });)
  PULSEUI_MOD(TextArea, read_only, (bool r = true), read_only_ = r;)
  PULSEUI_MOD(TextArea, wrap, (bool w = true), wrap_ = w;)
  PULSEUI_MOD(TextArea, rows, (int n), rows_ = std::max(1, n);)

  pulse::signal<std::string> changed;

  const std::string& text() const { return text_; }
  void set_text(std::string t);
  void set_placeholder(std::string p) { placeholder_ = std::move(p); invalidate(); }
  const std::string& placeholder_value() const { return placeholder_; }
  std::size_t caret() const { return caret_; }
  void select(std::size_t anchor, std::size_t caret);
  std::string selected_text() const;
  std::size_t visual_line_count(); // after wrapping (lays out at the current width)
  bool is_read_only() const { return read_only_; }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void on_focus(bool f) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  struct Line {
    std::size_t start = 0, end = 0;  // byte range (end excludes the newline)
    std::vector<std::size_t> bytes;  // caret positions on this line
    std::vector<float> xs;           // their x offsets
  };
  void ensure_lines();
  int line_of(std::size_t byte) const;
  float x_of(int line, std::size_t byte) const;
  std::size_t byte_at(int line, float x) const;
  std::size_t index_at(Point local);
  void edit(std::size_t a, std::size_t b, const std::string& with, bool coalesce = false);
  void move_caret(std::size_t pos, bool extend, bool keep_x = false);
  void ensure_caret_visible();
  Rect text_rect() const;
  Rect track() const;
  bool has_selection() const { return sel_ != caret_; }

  struct Snapshot {
    std::string text;
    std::size_t caret, sel;
  };
  Model<std::string>* model_ = nullptr;
  std::string text_, placeholder_;
  std::size_t caret_ = 0, sel_ = 0;
  float want_x_ = -1;
  std::vector<Line> lines_;
  float laid_width_ = -1;
  bool lines_dirty_ = true;
  detail::ScrollState sb_;
  std::vector<Snapshot> undo_, redo_;
  bool last_was_typing_ = false;
  bool read_only_ = false, wrap_ = true, selecting_ = false;
  int rows_ = 4;
};

// ─────────────────────────────────────────────────────────────────────────────
// NumberField - a number with arrows.
//   NumberField(width, 0, 2000).step(10).suffix(" px")    // Model<double>& or Model<int>&
// Up/Down or the mouse wheel step the value (Shift: x10), typing commits on Enter
// or when the field loses focus. Values are clamped to the range.
// ─────────────────────────────────────────────────────────────────────────────
class NumberField : public ViewT<NumberField> {
public:
  NumberField(Model<double>& model, double min = -std::numeric_limits<double>::infinity(),
              double max = std::numeric_limits<double>::infinity(), double step = 1);
  NumberField(Model<int>& model, int min = INT_MIN, int max = INT_MAX, int step = 1);
  explicit NumberField(Bind<double> value = 0.0, double min = -std::numeric_limits<double>::infinity(),
                       double max = std::numeric_limits<double>::infinity(), double step = 1);
  const char* type_name() const override { return "NumberField"; }

  PULSEUI_MOD(NumberField, step, (double s), step_ = s;)
  PULSEUI_MOD(NumberField, decimals, (int d), decimals_ = std::max(0, d); refresh_text();)
  PULSEUI_MOD(NumberField, suffix, (std::string s), suffix_ = std::move(s); refresh_text();)
  PULSEUI_MOD(NumberField, range, (double lo, double hi), min_ = lo; max_ = hi; set_value(value_, false);)
  PULSEUI_MOD(NumberField, on_change, (std::function<void(double)> fn), changed.connect(std::move(fn));)
  // two-way after construction (forms), and one-way
  PULSEUI_MOD(NumberField, model, (Model<double>& m), write_ = [&m](double x) { m.set(x); };
              bind(Bind<double>(m), [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });)
  PULSEUI_MOD(NumberField, model, (Model<int>& m), write_ = [&m](double x) { m.set(static_cast<int>(std::lround(x))); };
              bind(Bind<double>(m), [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });)
  PULSEUI_MOD(NumberField, bind_value, (Bind<double> b),
              bind(b, [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });)
  double min_value() const { return min_; }
  double max_value() const { return max_; }
  double step_value() const { return step_; }
  int decimals_value() const { return decimals_; }
  const std::string& suffix_value() const { return suffix_; }
  void set_step(double s) { step_ = s; }
  void set_decimals(int d) { decimals_ = std::max(0, d); refresh_text(); }
  void set_suffix(std::string s) { suffix_ = std::move(s); refresh_text(); }
  // A min above the max (forms set "min" before "max") raises the max with it.
  // The value asked for is kept, so value-then-range loads end where they should.
  void set_range(double lo, double hi) { min_ = lo; max_ = std::max(lo, hi); set_value(requested_, false); }

  pulse::signal<double> changed;

  double value() const { return value_; }
  void set_value(double v, bool notify = true);
  void step_by(int steps);
  std::string format(double v) const;
  std::optional<double> parse(std::string_view s) const;
  TextField& field() const { return *static_cast<TextField*>(children_.front().get()); }

  Size measure_content(Size avail) override;
  void layout() override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  bool hit_test_children(Point local) const override { return !arrows().contains(local); }
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

protected:
  void on_mount() override;

private:
  void init();
  void refresh_text();
  Rect arrows() const;
  std::function<void(double)> write_;
  double value_ = 0, requested_ = 0, min_, max_, step_;
  int decimals_ = 0;
  std::string suffix_;
  int hover_arrow_ = 0, press_arrow_ = 0; // +1 up, -1 down
  pulse::subscription repeat_;
};

// ─────────────────────────────────────────────────────────────────────────────
// RadioGroup - one of several options, all visible.
//   RadioGroup({"Small", "Medium", "Large"}, size)       // Model<int>&
// ─────────────────────────────────────────────────────────────────────────────
class RadioGroup : public ViewT<RadioGroup> {
public:
  RadioGroup() : RadioGroup(Bind<std::vector<std::string>>(std::vector<std::string>{})) {}
  RadioGroup(Bind<std::vector<std::string>> options, Model<int>& index);
  explicit RadioGroup(Bind<std::vector<std::string>> options, Bind<int> index = 0);
  const char* type_name() const override { return "RadioGroup"; }

  PULSEUI_MOD(RadioGroup, horizontal, (bool h = true), horizontal_ = h;)
  PULSEUI_MOD(RadioGroup, on_change, (std::function<void(int)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(RadioGroup, model, (Model<int>& m), model_ = &m;
              bind(Bind<int>(m), [](View& v, int i) { static_cast<RadioGroup&>(v).set_index(i, false); });)
  PULSEUI_MOD(RadioGroup, bind_index, (Bind<int> b), bind(b, [](View& v, int i) { static_cast<RadioGroup&>(v).set_index(i, false); });)
  PULSEUI_MOD(RadioGroup, bind_options, (Bind<std::vector<std::string>> o),
              bind(o, [](View& v, const std::vector<std::string>& x) { static_cast<RadioGroup&>(v).set_options(x); });)
  void set_horizontal(bool h) { horizontal_ = h; invalidate_layout(); }
  pulse::signal<int> changed;

  int index() const { return index_; }
  void set_index(int i, bool notify = true);
  const std::vector<std::string>& options() const { return options_; }
  void set_options(std::vector<std::string> o) { options_ = std::move(o); invalidate_layout(); }
  bool is_horizontal() const { return horizontal_; }

  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  std::vector<Rect> option_rects() const;
  int at(Point p) const;
  Model<int>* model_ = nullptr;
  std::vector<std::string> options_;
  int index_ = 0, hover_ = -1;
  bool horizontal_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Dates
// ─────────────────────────────────────────────────────────────────────────────
struct Date {
  int year = 1970, month = 1, day = 1; // month 1..12

  static Date today();
  static int days_in_month(int year, int month);
  static std::optional<Date> parse(std::string_view iso); // "2026-11-21"
  bool valid() const;
  int weekday() const;                // 0 = Monday ... 6 = Sunday
  Date add_days(int n) const;
  Date add_months(int n) const;       // clamps the day (Jan 31 + 1 month = Feb 28/29)
  std::string iso() const;            // "2026-11-21"
  long days_since_epoch() const;      // 1970-01-01 = 0
  static Date from_days(long days);
  auto operator<=>(const Date&) const = default;
};

// Localizable names for Calendar / DatePicker.
struct CalendarLabels {
  std::vector<std::string> months{"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
  std::vector<std::string> weekdays{"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"}; // Monday first
  bool week_starts_sunday = false;
};

// A month grid.  Calendar(due)  // Model<Date>&
class Calendar : public ViewT<Calendar> {
public:
  Calendar(Model<Date>& model);
  explicit Calendar(Bind<Date> date = Date::today());
  const char* type_name() const override { return "Calendar"; }
  PULSEUI_MOD(Calendar, labels, (CalendarLabels l), labels_ = std::move(l);)
  PULSEUI_MOD(Calendar, range, (Date lo, Date hi), min_ = lo; max_ = hi;)
  PULSEUI_MOD(Calendar, on_change, (std::function<void(Date)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(Calendar, on_pick, (std::function<void(Date)> fn), picked.connect(std::move(fn));)
  pulse::signal<Date> changed; // selection moved (keyboard or click)
  pulse::signal<Date> picked;  // a day was clicked or Enter pressed

  Date date() const { return date_; }
  void set_date(Date d, bool notify = true);
  int shown_year() const { return year_; }
  int shown_month() const { return month_; }
  void show_month(int year, int month);

  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  Rect grid_rect() const;
  Date first_cell() const;
  int cell_at(Point p) const; // 0..41, -1 = none, -2 = prev, -3 = next
  Model<Date>* model_ = nullptr;
  Date date_ = Date::today();
  int year_ = 1970, month_ = 1;
  int hover_ = -1;
  std::optional<Date> min_, max_;
  CalendarLabels labels_;
};

// A field showing the date; click opens a Calendar.
class DatePicker : public ViewT<DatePicker> {
public:
  DatePicker(Model<Date>& model);
  explicit DatePicker(Bind<Date> date = Date::today());
  const char* type_name() const override { return "DatePicker"; }
  PULSEUI_MOD(DatePicker, labels, (CalendarLabels l), labels_ = std::move(l);)
  PULSEUI_MOD(DatePicker, format, (std::function<std::string(const Date&)> f), format_ = std::move(f);)
  PULSEUI_MOD(DatePicker, on_change, (std::function<void(Date)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(DatePicker, model, (Model<Date>& m), model_ = &m;
              bind(Bind<Date>(m), [](View& v, const Date& d) { static_cast<DatePicker&>(v).set_date(d, false); });)
  PULSEUI_MOD(DatePicker, bind_date, (Bind<Date> b),
              bind(b, [](View& v, const Date& d) { static_cast<DatePicker&>(v).set_date(d, false); });)
  pulse::signal<Date> changed;

  Date date() const { return date_; }
  void set_date(Date d, bool notify = true);
  std::string display_text() const;
  void open_popup();
  bool popup_open() const { return popup_ != nullptr; }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  Model<Date>* model_ = nullptr;
  Date date_ = Date::today();
  CalendarLabels labels_;
  std::function<std::string(const Date&)> format_;
  View* popup_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Colors
//   ColorPicker(accent)            // Model<Color>&: a swatch; click opens a ColorPanel
//   ColorPanel(accent).alpha()     // saturation/value square, hue and alpha bars, hex field
// ─────────────────────────────────────────────────────────────────────────────
class ColorPanel : public ViewT<ColorPanel> {
public:
  ColorPanel(Model<Color>& model);
  explicit ColorPanel(Bind<Color> color = Color::hex(0x3B82F6));
  const char* type_name() const override { return "ColorPanel"; }
  PULSEUI_MOD(ColorPanel, alpha, (bool a = true), alpha_ = a;)
  PULSEUI_MOD(ColorPanel, swatches, (std::vector<Color> s), swatches_ = std::move(s);)
  PULSEUI_MOD(ColorPanel, on_change, (std::function<void(Color)> fn), changed.connect(std::move(fn));)
  pulse::signal<Color> changed;

  Color color() const { return color_; }
  void set_color(Color c, bool notify = true);
  float hue() const { return h_; }

  Size measure_content(Size avail) override;
  void layout() override;
  void paint_content(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

protected:
  void on_mount() override;

private:
  void init();
  void set_hsv(float h, float s, float v, float a, bool notify = true);
  Rect sv_rect() const;
  Rect hue_rect() const;
  Rect alpha_rect() const;
  Rect swatch_rect(std::size_t i) const;
  Model<Color>* model_ = nullptr;
  Color color_;
  float h_ = 0, s_ = 0, v_ = 0, a_ = 1;
  int drag_ = 0; // 1 sv, 2 hue, 3 alpha
  bool alpha_ = false;
  std::vector<Color> swatches_;
};

class ColorPicker : public ViewT<ColorPicker> {
public:
  ColorPicker(Model<Color>& model);
  explicit ColorPicker(Bind<Color> color = Color::hex(0x3B82F6));
  const char* type_name() const override { return "ColorPicker"; }
  PULSEUI_MOD(ColorPicker, alpha, (bool a = true), alpha_ = a;)
  PULSEUI_MOD(ColorPicker, on_change, (std::function<void(Color)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(ColorPicker, model, (Model<Color>& m), model_ = &m;
              bind(Bind<Color>(m), [](View& v, const Color& c) { static_cast<ColorPicker&>(v).set_color(c, false); });)
  PULSEUI_MOD(ColorPicker, bind_color, (Bind<Color> b),
              bind(b, [](View& v, const Color& c) { static_cast<ColorPicker&>(v).set_color(c, false); });)
  bool alpha_value() const { return alpha_; }
  void set_alpha(bool a) { alpha_ = a; }
  pulse::signal<Color> changed;

  Color color() const { return color_; }
  void set_color(Color c, bool notify = true);
  void open_popup();
  bool popup_open() const { return popup_ != nullptr; }

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  Model<Color>* model_ = nullptr;
  Color color_;
  bool alpha_ = false;
  View* popup_ = nullptr;
  std::shared_ptr<pulse::property<Color>> popup_model_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Toasts: short notifications in the bottom-right corner that go away by themselves.
//   toast(win, "Saved");
//   toast(win, "Note deleted", {.kind = ToastKind::Info, .action_label = "Undo", .action = undo});
// ─────────────────────────────────────────────────────────────────────────────
enum class ToastKind { Info, Success, Warning, Error };

struct ToastOptions {
  ToastKind kind = ToastKind::Info;
  std::chrono::milliseconds duration{3500}; // 0 = until closed
  std::string action_label;                 // an optional button
  std::function<void()> action;
};

View* toast(Window& w, std::string text, ToastOptions options = {});
View* toast(Window& w, std::string text, ToastKind kind, std::chrono::milliseconds duration = std::chrono::milliseconds(3500));

} // namespace pulseui
