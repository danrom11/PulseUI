// NumberField, RadioGroup, Calendar/DatePicker, ColorPanel/ColorPicker, toasts.
#include <pulseui/ui/inputs.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace pulseui {

namespace {
void ring(Painter& p, const Rect& r, float radius) {
  p.stroke_rect(r.outset(2), th().border_focus.with_alpha(0.7f), 2, radius + 2);
}
} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// NumberField
// ═════════════════════════════════════════════════════════════════════════════
constexpr float kArrowW = 20;

NumberField::NumberField(Model<double>& model, double min, double max, double step) : min_(min), max_(max), step_(step) {
  init();
  write_ = [&model](double v) { model.set(v); };
  bind(Bind<double>(model), [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });
}

NumberField::NumberField(Model<int>& model, int min, int max, int step)
    : min_(min == INT_MIN ? -std::numeric_limits<double>::infinity() : min),
      max_(max == INT_MAX ? std::numeric_limits<double>::infinity() : max),
      step_(step) {
  init();
  write_ = [&model](double v) { model.set(static_cast<int>(std::lround(v))); };
  bind(Bind<double>(model), [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });
}

NumberField::NumberField(Bind<double> value, double min, double max, double step) : min_(min), max_(max), step_(step) {
  init();
  bind(value, [](View& v, double x) { static_cast<NumberField&>(v).set_value(x, false); });
}

void NumberField::init() {
  TextField f;
  f.style().padding = Insets(10, 0, kArrowW + 6, 0); // the arrows sit on the right
  add(std::move(f));
  refresh_text();
}

void NumberField::on_mount() {
  // connect to the live child (the view may have been moved before mounting)
  std::weak_ptr<bool> alive = alive_;
  keep(field().committed.connect([this, alive](const std::string& s) {
    if (alive.expired()) return;
    if (auto v = parse(s)) set_value(*v);
    refresh_text(); // invalid input goes back to the current value
  }).scoped());
}

std::string NumberField::format(double v) const {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", decimals_, v);
  std::string s = buf;
  if (s == "-0") s = "0";
  return s + suffix_;
}

std::optional<double> NumberField::parse(std::string_view s) const {
  std::string t(s);
  if (!suffix_.empty() && t.size() >= suffix_.size() && t.compare(t.size() - suffix_.size(), suffix_.size(), suffix_) == 0)
    t.erase(t.size() - suffix_.size());
  std::replace(t.begin(), t.end(), ',', '.');
  while (!t.empty() && t.back() == ' ') t.pop_back();
  while (!t.empty() && t.front() == ' ') t.erase(t.begin());
  if (t.empty()) return std::nullopt;
  char* end = nullptr;
  const double v = std::strtod(t.c_str(), &end);
  if (!end || *end != '\0' || !std::isfinite(v)) return std::nullopt;
  return v;
}

void NumberField::refresh_text() {
  if (children_.empty()) return;
  field().set_text(format(value_));
}

void NumberField::set_value(double v, bool notify) {
  requested_ = v;
  v = std::clamp(v, min_, std::max(min_, max_));
  const double q = std::pow(10.0, decimals_);
  v = std::round(v * q) / q;
  const bool same = v == value_;
  value_ = v;
  refresh_text();
  invalidate();
  if (same || !notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (write_) write_(v);
  if (!alive.expired()) changed.emit(v);
}

void NumberField::step_by(int steps) {
  double base = value_;
  if (auto typed = parse(field().text())) base = std::clamp(*typed, min_, std::max(min_, max_));
  set_value(base + step_ * steps);
}

Rect NumberField::arrows() const { return {frame_.w - kArrowW - 1, 1, kArrowW, std::max(0.f, frame_.h - 2)}; }

Size NumberField::measure_content(Size) {
  const Theme& t = th();
  return {120, t.control_height};
}

void NumberField::layout() {
  if (children_.empty()) return;
  children_.front()->set_frame({0, 0, std::max(0.f, frame_.w), frame_.h});
  children_.front()->layout();
}

void NumberField::paint_overlay(Painter& p) {
  const Theme& t = th();
  const Rect a = arrows();
  const float mid = a.y + a.h / 2;
  const Rect up{a.x, a.y, a.w, a.h / 2}, down{a.x, mid, a.w, a.h / 2};
  p.line({a.x, a.y + 4}, {a.x, a.bottom() - 4}, t.border, 1);
  const bool enabled = is_enabled();
  auto draw = [&](const Rect& r, int dir) {
    if (enabled && (hover_arrow_ == dir || press_arrow_ == dir))
      p.fill_rect(r.inset(Insets(2, 1)), press_arrow_ == dir ? t.control_pressed : t.control_hover, t.radius_small);
    const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const Color c = enabled ? t.text_dim : t.text_disabled;
    if (dir > 0) p.polyline({{cx - 3.5f, cy + 1.5f}, {cx, cy - 2}, {cx + 3.5f, cy + 1.5f}}, c, 1.4f);
    else p.polyline({{cx - 3.5f, cy - 1.5f}, {cx, cy + 2}, {cx + 3.5f, cy - 1.5f}}, c, 1.4f);
  };
  draw(up, 1);
  draw(down, -1);
}

bool NumberField::on_event(InputEvent& e) {
  const Rect a = arrows();
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int h = a.contains(e.pos) ? (e.pos.y < a.y + a.h / 2 ? 1 : -1) : 0;
      if (h != hover_arrow_) {
        hover_arrow_ = h;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseLeave:
      hover_arrow_ = 0;
      invalidate();
      return false;
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left || !a.contains(e.pos)) return false;
      press_arrow_ = e.pos.y < a.y + a.h / 2 ? 1 : -1;
      const int mult = (e.mods & Mod::Shift) ? 10 : 1;
      step_by(press_arrow_ * mult);
      // hold to repeat
      std::weak_ptr<bool> alive = alive_;
      repeat_ = App::after(std::chrono::milliseconds(400), [this, alive, mult] {
        if (alive.expired() || !press_arrow_) return;
        repeat_ = App::every(std::chrono::milliseconds(60), [this, alive, mult] {
          if (!alive.expired() && press_arrow_) step_by(press_arrow_ * mult);
        });
      });
      invalidate();
      return true;
    }
    case InputEvent::MouseUp:
      if (press_arrow_) {
        press_arrow_ = 0;
        repeat_.reset();
        invalidate();
        return true;
      }
      return false;
    case InputEvent::Wheel:
      if (!hovered_ && !focused()) return false;
      if (e.wheel_dy != 0) step_by((e.wheel_dy > 0 ? 1 : -1) * ((e.mods & Mod::Shift) ? 10 : 1));
      return true;
    case InputEvent::KeyDown: {
      const int mult = (e.mods & Mod::Shift) ? 10 : 1;
      if (e.key == Key::Up) { step_by(mult); return true; }
      if (e.key == Key::Down) { step_by(-mult); return true; }
      if (e.key == Key::PageUp) { step_by(10 * mult); return true; }
      if (e.key == Key::PageDown) { step_by(-10 * mult); return true; }
      return false;
    }
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// RadioGroup
// ═════════════════════════════════════════════════════════════════════════════
RadioGroup::RadioGroup(Bind<std::vector<std::string>> options, Model<int>& index) : RadioGroup(std::move(options)) {
  model_ = &index;
  bind(Bind<int>(index), [](View& v, int i) { static_cast<RadioGroup&>(v).set_index(i, false); });
}

RadioGroup::RadioGroup(Bind<std::vector<std::string>> options, Bind<int> index) {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  bind(options, [](View& v, const std::vector<std::string>& o) { static_cast<RadioGroup&>(v).set_options(o); });
  bind(index, [](View& v, int i) { static_cast<RadioGroup&>(v).set_index(i, false); });
}

void RadioGroup::set_index(int i, bool notify) {
  if (i == index_) return;
  index_ = i;
  invalidate();
  if (!notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(i);
  if (!alive.expired()) changed.emit(i);
}

std::vector<Rect> RadioGroup::option_rects() const {
  std::vector<Rect> out;
  const Rect c = content_rect();
  const Theme& t = th();
  float x = c.x, y = c.y;
  for (auto& o : options_) {
    const float w = 18 + 8 + std::ceil(measure_text(o, t.font, 0).w);
    if (horizontal_) {
      out.push_back({x, y, w, 24});
      x += w + 16;
    } else {
      out.push_back({x, y, std::max(w, c.w), 24});
      y += 28;
    }
  }
  return out;
}

int RadioGroup::at(Point p) const {
  const auto rs = option_rects();
  for (std::size_t i = 0; i < rs.size(); ++i)
    if (rs[i].contains(p)) return static_cast<int>(i);
  return -1;
}

Size RadioGroup::measure_content(Size) {
  const Theme& t = th();
  float w = 0, h = 0;
  for (auto& o : options_) {
    const float ow = 18 + 8 + std::ceil(measure_text(o, t.font, 0).w);
    if (horizontal_) w += ow + (w > 0 ? 16 : 0);
    else w = std::max(w, ow);
  }
  if (horizontal_) h = options_.empty() ? 0 : 24;
  else h = options_.empty() ? 0 : 28.f * static_cast<float>(options_.size()) - 4;
  return {w, h};
}

void RadioGroup::paint_content(Painter& p) {
  const Theme& t = th();
  const auto rs = option_rects();
  const bool enabled = is_enabled();
  for (std::size_t i = 0; i < rs.size(); ++i) {
    const Rect& r = rs[i];
    const bool sel = static_cast<int>(i) == index_;
    const Rect dot{r.x, r.y + (r.h - 18) / 2, 18, 18};
    const bool hot = enabled && static_cast<int>(i) == hover_;
    p.fill_ellipse(dot, sel ? (enabled ? t.accent : t.text_disabled) : (hot ? t.control_hover : t.input));
    if (!sel) p.stroke_ellipse(dot.inset(0.5f), t.border, 1);
    else p.fill_ellipse(dot.inset(5), t.on_accent);
    if (sel && focused()) p.stroke_ellipse(dot.outset(2), t.border_focus.with_alpha(0.7f), 2);
    p.text({r.x + 26, r.y, r.w - 26, r.h}, options_[i], t.font, enabled ? t.text : t.text_disabled, Align::LeftCenter);
  }
}

bool RadioGroup::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int h = at(e.pos);
      if (h != hover_) {
        hover_ = h;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseLeave:
      hover_ = -1;
      invalidate();
      return false;
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left) {
        const int i = at(e.pos);
        if (i >= 0) set_index(i);
      }
      return true;
    case InputEvent::KeyDown: {
      const int n = static_cast<int>(options_.size());
      if (!n) return false;
      if (e.key == Key::Down || e.key == Key::Right) { set_index((std::max(index_, -1) + 1) % n); return true; }
      if (e.key == Key::Up || e.key == Key::Left) { set_index((index_ - 1 + n) % n); return true; }
      if (e.key == Key::Home) { set_index(0); return true; }
      if (e.key == Key::End) { set_index(n - 1); return true; }
      return false;
    }
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Date
// ═════════════════════════════════════════════════════════════════════════════
Date Date::today() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

int Date::days_in_month(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : d[m - 1];
}

bool Date::valid() const { return month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month); }

// days from civil (Howard Hinnant's algorithm)
long Date::days_since_epoch() const {
  const int y = year - (month <= 2);
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

Date Date::from_days(long z) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long y = static_cast<long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return {static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

int Date::weekday() const {
  const long d = days_since_epoch(); // 1970-01-01 was a Thursday (3)
  return static_cast<int>(((d % 7) + 7 + 3) % 7);
}

Date Date::add_days(int n) const { return from_days(days_since_epoch() + n); }

Date Date::add_months(int n) const {
  int m = month - 1 + n;
  int y = year + (m >= 0 ? m / 12 : (m - 11) / 12);
  m = ((m % 12) + 12) % 12 + 1;
  return {y, m, std::min(day, days_in_month(y, m))};
}

std::string Date::iso() const {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
  return buf;
}

std::optional<Date> Date::parse(std::string_view s) {
  int y = 0, m = 0, d = 0;
  const std::string str(s);
  if (std::sscanf(str.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return std::nullopt;
  Date out{y, m, d};
  if (!out.valid()) return std::nullopt;
  return out;
}

// ═════════════════════════════════════════════════════════════════════════════
// Calendar
// ═════════════════════════════════════════════════════════════════════════════
constexpr float kCalHeader = 34, kCalWeek = 24, kCell = 32;

Calendar::Calendar(Model<Date>& model) : Calendar(Bind<Date>(model)) { model_ = &model; }

Calendar::Calendar(Bind<Date> date) {
  focusable_ = true;
  hover_repaint_ = true;
  style_.padding = Insets(8);
  year_ = date_.year;
  month_ = date_.month;
  bind(date, [](View& v, const Date& d) { static_cast<Calendar&>(v).set_date(d, false); });
}

void Calendar::show_month(int year, int month) {
  Date d = Date{year, 1, 1}.add_months(month - 1);
  year_ = d.year;
  month_ = d.month;
  invalidate();
}

void Calendar::set_date(Date d, bool notify) {
  if (!d.valid()) return;
  if (min_ && d < *min_) d = *min_;
  if (max_ && d > *max_) d = *max_;
  const bool same = d == date_;
  date_ = d;
  year_ = d.year;
  month_ = d.month;
  invalidate();
  if (same || !notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(d);
  if (!alive.expired()) changed.emit(d);
}

Size Calendar::measure_content(Size) { return {kCell * 7, kCalHeader + kCalWeek + kCell * 6}; }

Rect Calendar::grid_rect() const {
  const Rect c = content_rect();
  return {c.x, c.y + kCalHeader + kCalWeek, kCell * 7, kCell * 6};
}

Date Calendar::first_cell() const {
  const Date first{year_, month_, 1};
  int wd = first.weekday(); // Monday = 0
  if (labels_.week_starts_sunday) wd = (wd + 1) % 7;
  return first.add_days(-wd);
}

int Calendar::cell_at(Point p) const {
  const Rect c = content_rect();
  if (p.y >= c.y && p.y < c.y + kCalHeader) {
    if (p.x < c.x + 32) return -2;
    if (p.x > c.x + kCell * 7 - 32) return -3;
    return -1;
  }
  const Rect g = grid_rect();
  if (!g.contains(p)) return -1;
  const int col = static_cast<int>((p.x - g.x) / kCell), row = static_cast<int>((p.y - g.y) / kCell);
  return row * 7 + col;
}

void Calendar::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect c = content_rect();
  // header: < Month Year >
  const std::string title = (month_ >= 1 && month_ <= static_cast<int>(labels_.months.size()) ? labels_.months[static_cast<std::size_t>(month_ - 1)] : "") +
                            " " + std::to_string(year_);
  p.text({c.x, c.y, kCell * 7, kCalHeader}, title, t.font.with_weight(FontWeight::SemiBold), t.text, Align::Center);
  auto arrow = [&](float cx, int dir, bool hot) {
    const float cy = c.y + kCalHeader / 2;
    if (hot) p.fill_rect({cx - 12, cy - 12, 24, 24}, t.control_hover, t.radius_small);
    p.polyline({{cx + 2.5f * dir * -1, cy - 5}, {cx + 2.5f * dir, cy}, {cx + 2.5f * dir * -1, cy + 5}}, t.text_dim, 1.6f);
  };
  arrow(c.x + 16, -1, hover_ == -2);
  arrow(c.x + kCell * 7 - 16, 1, hover_ == -3);
  // weekday names
  for (int i = 0; i < 7; ++i) {
    int wd = labels_.week_starts_sunday ? (i + 6) % 7 : i;
    const std::string& n = wd < static_cast<int>(labels_.weekdays.size()) ? labels_.weekdays[static_cast<std::size_t>(wd)] : std::string();
    p.text({c.x + kCell * static_cast<float>(i), c.y + kCalHeader, kCell, kCalWeek}, n, t.font_small, t.text_dim, Align::Center);
  }
  // days
  const Rect g = grid_rect();
  const Date first = first_cell();
  const Date today = Date::today();
  for (int i = 0; i < 42; ++i) {
    const Date d = first.add_days(i);
    const Rect cell{g.x + kCell * static_cast<float>(i % 7), g.y + kCell * static_cast<float>(i / 7), kCell, kCell};
    const Rect dot = cell.inset(2);
    const bool in_month = d.month == month_;
    const bool sel = d == date_;
    const bool disabled = (min_ && d < *min_) || (max_ && d > *max_);
    if (sel) p.fill_rect(dot, t.accent, t.radius_small);
    else if (i == hover_ && !disabled) p.fill_rect(dot, t.control_hover, t.radius_small);
    if (d == today && !sel) p.stroke_rect(dot, t.accent, 1, t.radius_small);
    const Color fg = sel ? t.on_accent : disabled ? t.text_disabled : in_month ? t.text : t.text_disabled;
    p.text(cell, std::to_string(d.day), t.font, fg, Align::Center);
  }
  if (focused()) ring(p, bounds().inset(1), t.radius);
}

bool Calendar::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int h = cell_at(e.pos);
      if (h != hover_) {
        hover_ = h;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseLeave:
      hover_ = -1;
      invalidate();
      return false;
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp: {
      if (e.button != MouseButton::Left) return true;
      const int c = cell_at(e.pos);
      if (c == -2) show_month(year_, month_ - 1);
      else if (c == -3) show_month(year_, month_ + 1);
      else if (c >= 0) {
        const Date d = first_cell().add_days(c);
        if ((min_ && d < *min_) || (max_ && d > *max_)) return true;
        set_date(d);
        std::weak_ptr<bool> alive = alive_;
        picked.emit(d);
        if (alive.expired()) return true;
      }
      return true;
    }
    case InputEvent::Wheel:
      if (e.wheel_dy != 0) show_month(year_, month_ + (e.wheel_dy > 0 ? -1 : 1));
      return true;
    case InputEvent::KeyDown:
      switch (e.key) {
        case Key::Left: set_date(date_.add_days(-1)); return true;
        case Key::Right: set_date(date_.add_days(1)); return true;
        case Key::Up: set_date(date_.add_days(-7)); return true;
        case Key::Down: set_date(date_.add_days(7)); return true;
        case Key::PageUp: set_date(date_.add_months((e.mods & Mod::Shift) ? -12 : -1)); return true;
        case Key::PageDown: set_date(date_.add_months((e.mods & Mod::Shift) ? 12 : 1)); return true;
        case Key::Home: set_date(Date::today()); return true;
        case Key::Enter:
        case Key::Space: picked.emit(date_); return true;
        default: return false;
      }
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// DatePicker
// ═════════════════════════════════════════════════════════════════════════════
DatePicker::DatePicker(Model<Date>& model) : DatePicker(Bind<Date>(model)) { model_ = &model; }

DatePicker::DatePicker(Bind<Date> date) {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  style_.padding = Insets(10, 0);
  bind(date, [](View& v, const Date& d) { static_cast<DatePicker&>(v).set_date(d, false); });
}

void DatePicker::set_date(Date d, bool notify) {
  if (!d.valid()) return;
  const bool same = d == date_;
  date_ = d;
  invalidate();
  if (same || !notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(d);
  if (!alive.expired()) changed.emit(d);
}

std::string DatePicker::display_text() const { return format_ ? format_(date_) : date_.iso(); }

void DatePicker::open_popup() {
  if (!window() || popup_) return;
  Calendar cal(date_);
  cal.labels(labels_);
  std::weak_ptr<bool> alive = alive_;
  cal.on_pick([this, alive](Date d) {
    if (alive.expired()) return;
    set_date(d);
    if (popup_ && window()) window()->close_popup(popup_);
  });
  Panel box;
  box.padding(Insets(2));
  box.add(std::move(cal));
  popup_ = window()->open_dropdown(std::move(box), *this, [this, alive] {
    if (alive.expired()) return;
    popup_ = nullptr;
    invalidate();
  });
  // keyboard goes to the calendar
  if (popup_ && !popup_->children().empty()) window()->set_focus(popup_->children().front().get());
}

Size DatePicker::measure_content(Size) {
  const Theme& t = th();
  return {std::ceil(measure_text(display_text(), t.font, 0).w) + 32, t.control_height};
}

void DatePicker::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  p.fill_rect(r, t.input, t.radius);
  p.stroke_rect(r, popup_ ? t.border_focus : (hovered_ ? t.border.lighter(0.15f) : t.border), 1, t.radius);
  const Rect c = content_rect();
  p.text({c.x, c.y, c.w - 22, c.h}, display_text(), t.font, is_enabled() ? t.text : t.text_disabled, Align::LeftCenter);
  // a small calendar glyph
  const float x = r.right() - 24, y = r.h / 2 - 7;
  p.stroke_rect({x, y + 1, 14, 13}, t.text_dim, 1.2f, 2);
  p.line({x, y + 5}, {x + 14, y + 5}, t.text_dim, 1.2f);
  p.line({x + 4, y - 1}, {x + 4, y + 3}, t.text_dim, 1.2f);
  p.line({x + 10, y - 1}, {x + 10, y + 3}, t.text_dim, 1.2f);
  if (focused()) ring(p, r, t.radius);
}

bool DatePicker::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left && pressed_ && bounds().contains(e.pos)) open_popup();
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Space || e.key == Key::Enter || (e.key == Key::Down && (e.mods & Mod::Alt))) {
        open_popup();
        return true;
      }
      if (e.key == Key::Up) { set_date(date_.add_days(1)); return true; }
      if (e.key == Key::Down) { set_date(date_.add_days(-1)); return true; }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// ColorPanel
// ═════════════════════════════════════════════════════════════════════════════
constexpr float kSvW = 220, kSvH = 150, kBarH = 14, kGap = 10, kSwatch = 20;

namespace {
void checker(Painter& p, const Rect& r) {
  p.fill_rect(r, Color::white());
  const float s = 5;
  for (float y = r.y; y < r.bottom(); y += s)
    for (float x = r.x; x < r.right(); x += s) {
      const int ix = static_cast<int>((x - r.x) / s), iy = static_cast<int>((y - r.y) / s);
      if ((ix + iy) % 2) p.fill_rect({x, y, std::min(s, r.right() - x), std::min(s, r.bottom() - y)}, Color::hex(0xCCCCCC));
    }
}
std::vector<Color> default_swatches() {
  return {Color::hex(0xEF4444), Color::hex(0xF97316), Color::hex(0xEAB308), Color::hex(0x22C55E), Color::hex(0x14B8A6),
          Color::hex(0x3B82F6), Color::hex(0x8B5CF6), Color::hex(0xEC4899), Color::hex(0x6B7280), Color::hex(0x111827),
          Color::white()};
}
} // namespace

ColorPanel::ColorPanel(Model<Color>& model) : ColorPanel(Bind<Color>(model)) { model_ = &model; }

ColorPanel::ColorPanel(Bind<Color> color) {
  init();
  bind(color, [](View& v, const Color& c) { static_cast<ColorPanel&>(v).set_color(c, false); });
}

void ColorPanel::init() {
  focusable_ = false;
  style_.padding = Insets(10);
  swatches_ = default_swatches();
  TextField hex;
  hex.width(96);
  add(std::move(hex));
}

void ColorPanel::on_mount() {
  std::weak_ptr<bool> alive = alive_;
  auto* field = static_cast<TextField*>(children_.front().get());
  field->set_text(color_.to_hex());
  keep(field->committed.connect([this, alive, field](const std::string& s) {
    if (alive.expired()) return;
    if (auto c = Color::parse(s)) set_color(alpha_ ? *c : c->with_alpha(1));
    field->set_text(color_.to_hex());
  }).scoped());
}

void ColorPanel::set_color(Color c, bool notify) {
  float h, s, v;
  c.to_hsv(h, s, v);
  // keep the hue while the color is grey (it carries no hue)
  if (s <= 0.0001f || v <= 0.0001f) h = h_;
  if (v <= 0.0001f) s = s_;
  set_hsv(h, s, v, c.a, notify);
}

void ColorPanel::set_hsv(float h, float s, float v, float a, bool notify) {
  h_ = std::clamp(h, 0.f, 359.999f);
  s_ = std::clamp(s, 0.f, 1.f);
  v_ = std::clamp(v, 0.f, 1.f);
  a_ = alpha_ ? std::clamp(a, 0.f, 1.f) : 1.f;
  const Color c = Color::hsv(h_, s_, v_, a_);
  const bool same = c == color_;
  color_ = c;
  if (!children_.empty()) {
    auto* field = static_cast<TextField*>(children_.front().get());
    if (!field->focused()) field->set_text(color_.to_hex());
  }
  invalidate();
  if (same || !notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(c);
  if (!alive.expired()) changed.emit(c);
}

Rect ColorPanel::sv_rect() const {
  const Rect c = content_rect();
  return {c.x, c.y, kSvW, kSvH};
}
Rect ColorPanel::hue_rect() const {
  const Rect s = sv_rect();
  return {s.x, s.bottom() + kGap, kSvW, kBarH};
}
Rect ColorPanel::alpha_rect() const {
  const Rect h = hue_rect();
  return {h.x, h.bottom() + kGap, kSvW, alpha_ ? kBarH : 0};
}
Rect ColorPanel::swatch_rect(std::size_t i) const {
  const Rect a = alpha_rect();
  const float y0 = (alpha_ ? a.bottom() : hue_rect().bottom()) + kGap + 30 + kGap;
  const int per_row = static_cast<int>((kSvW + 4) / (kSwatch + 4));
  const int row = static_cast<int>(i) / per_row, col = static_cast<int>(i) % per_row;
  return {a.x + col * (kSwatch + 4), y0 + row * (kSwatch + 4), kSwatch, kSwatch};
}

Size ColorPanel::measure_content(Size) {
  const float top = kSvH + kGap + kBarH + (alpha_ ? kGap + kBarH : 0) + kGap + 30;
  float sw = 0;
  if (!swatches_.empty()) {
    const int per_row = static_cast<int>((kSvW + 4) / (kSwatch + 4));
    const int rows = (static_cast<int>(swatches_.size()) + per_row - 1) / per_row;
    sw = kGap + rows * (kSwatch + 4) - 4;
  }
  return {kSvW, top + sw};
}

void ColorPanel::layout() {
  if (children_.empty()) return;
  const Rect a = alpha_ ? alpha_rect() : hue_rect();
  children_.front()->set_frame({a.right() - 96, a.bottom() + kGap, 96, 30});
  children_.front()->layout();
}

void ColorPanel::paint_content(Painter& p) {
  const Theme& t = th();
  // saturation (x) / value (y) square: columns of white->hue blended down to black
  const Rect sv = sv_rect();
  const int cols = 110;
  for (int i = 0; i < cols; ++i) {
    const float s0 = static_cast<float>(i) / (cols - 1);
    const float x0 = sv.x + sv.w * static_cast<float>(i) / cols, x1 = sv.x + sv.w * static_cast<float>(i + 1) / cols;
    p.fill_gradient({x0, sv.y, x1 - x0 + 0.6f, sv.h}, Color::hsv(h_, s0, 1), Color::black());
  }
  p.stroke_rect(sv, t.border, 1, 0);
  const Point knob{sv.x + s_ * sv.w, sv.y + (1 - v_) * sv.h};
  p.stroke_ellipse({knob.x - 6, knob.y - 6, 12, 12}, Color::white(), 2);
  p.stroke_ellipse({knob.x - 7, knob.y - 7, 14, 14}, Color::black().with_alpha(0.5f), 1);
  // hue bar
  const Rect hr = hue_rect();
  const int segs = 36;
  for (int i = 0; i < segs; ++i) {
    const float x0 = hr.x + hr.w * static_cast<float>(i) / segs;
    p.fill_rect({x0, hr.y, hr.w / segs + 0.6f, hr.h}, Color::hsv(360.f * (static_cast<float>(i) + 0.5f) / segs, 1, 1));
  }
  p.stroke_rect(hr, t.border, 1, 0);
  const float hx = hr.x + h_ / 360.f * hr.w;
  p.stroke_rect({hx - 3, hr.y - 2, 6, hr.h + 4}, t.text, 2, 2);
  // alpha bar
  if (alpha_) {
    const Rect ar = alpha_rect();
    checker(p, ar);
    for (int i = 0; i < segs; ++i) {
      const float x0 = ar.x + ar.w * static_cast<float>(i) / segs;
      p.fill_rect({x0, ar.y, ar.w / segs + 0.6f, ar.h}, color_.with_alpha((static_cast<float>(i) + 0.5f) / segs));
    }
    p.stroke_rect(ar, t.border, 1, 0);
    const float ax = ar.x + a_ * ar.w;
    p.stroke_rect({ax - 3, ar.y - 2, 6, ar.h + 4}, t.text, 2, 2);
  }
  // preview next to the hex field
  const Rect last = alpha_ ? alpha_rect() : hue_rect();
  const Rect prev{last.x, last.bottom() + kGap, kSvW - 96 - 8, 30};
  checker(p, prev);
  p.fill_rect(prev, color_, 0);
  p.stroke_rect(prev, t.border, 1, t.radius_small);
  // swatches
  for (std::size_t i = 0; i < swatches_.size(); ++i) {
    const Rect r = swatch_rect(i);
    p.fill_rect(r, swatches_[i], t.radius_small);
    p.stroke_rect(r, swatches_[i] == color_.with_alpha(swatches_[i].a) ? t.text : t.border, swatches_[i] == color_ ? 2.f : 1.f,
                  t.radius_small);
  }
}

bool ColorPanel::on_event(InputEvent& e) {
  auto apply = [&](Point pt) {
    if (drag_ == 1) {
      const Rect sv = sv_rect();
      set_hsv(h_, (pt.x - sv.x) / sv.w, 1 - (pt.y - sv.y) / sv.h, a_);
    } else if (drag_ == 2) {
      const Rect hr = hue_rect();
      set_hsv(360.f * (pt.x - hr.x) / hr.w, s_, v_, a_);
    } else if (drag_ == 3) {
      const Rect ar = alpha_rect();
      set_hsv(h_, s_, v_, (pt.x - ar.x) / ar.w);
    }
  };
  switch (e.type) {
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return true;
      if (sv_rect().contains(e.pos)) drag_ = 1;
      else if (hue_rect().outset(3).contains(e.pos)) drag_ = 2;
      else if (alpha_ && alpha_rect().outset(3).contains(e.pos)) drag_ = 3;
      else {
        for (std::size_t i = 0; i < swatches_.size(); ++i)
          if (swatch_rect(i).contains(e.pos)) {
            set_color(alpha_ ? swatches_[i] : swatches_[i].with_alpha(1));
            return true;
          }
        return true;
      }
      apply(e.pos);
      return true;
    }
    case InputEvent::MouseMove:
      if (!drag_) return false;
      apply(e.pos);
      return true;
    case InputEvent::MouseUp:
      drag_ = 0;
      return true;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// ColorPicker
// ═════════════════════════════════════════════════════════════════════════════
ColorPicker::ColorPicker(Model<Color>& model) : ColorPicker(Bind<Color>(model)) { model_ = &model; }

ColorPicker::ColorPicker(Bind<Color> color) {
  focusable_ = true;
  hover_repaint_ = true;
  cursor_ = Cursor::Hand;
  style_.padding = Insets(6, 0, 10, 0);
  bind(color, [](View& v, const Color& c) { static_cast<ColorPicker&>(v).set_color(c, false); });
}

void ColorPicker::set_color(Color c, bool notify) {
  const bool same = c == color_;
  color_ = c;
  invalidate();
  if (popup_model_ && popup_model_->get() != c) popup_model_->set(c);
  if (same || !notify) return;
  std::weak_ptr<bool> alive = alive_;
  if (model_) model_->set(c);
  if (!alive.expired()) changed.emit(c);
}

void ColorPicker::open_popup() {
  if (!window() || popup_) return;
  popup_model_ = std::make_shared<pulse::property<Color>>(color_);
  std::weak_ptr<bool> alive = alive_;
  ColorPanel panel(*popup_model_);
  panel.alpha(alpha_);
  panel.on_change([this, alive](Color c) {
    if (!alive.expired()) set_color(c);
  });
  Panel box;
  box.padding(Insets(2));
  box.add(std::move(panel));
  auto keep_model = popup_model_;
  popup_ = window()->open_dropdown(std::move(box), *this, [this, alive, keep_model] {
    if (alive.expired()) return;
    popup_ = nullptr;
    popup_model_.reset();
    invalidate();
  });
}

Size ColorPicker::measure_content(Size) {
  const Theme& t = th();
  return {24 + 8 + std::ceil(measure_text("#FFFFFFFF", t.font, 0).w) + 4, t.control_height};
}

void ColorPicker::paint(Painter& p) {
  const Theme& t = th();
  const Rect r = bounds();
  p.fill_rect(r, t.input, t.radius);
  p.stroke_rect(r, popup_ ? t.border_focus : (hovered_ ? t.border.lighter(0.15f) : t.border), 1, t.radius);
  const Rect c = content_rect();
  const Rect sw{c.x, c.y + (c.h - 18) / 2, 24, 18};
  checker(p, sw);
  p.fill_rect(sw, color_, 3);
  p.stroke_rect(sw, t.border, 1, 3);
  p.text({sw.right() + 8, c.y, c.w - 32, c.h}, color_.to_hex(), t.font.with_size(t.font.size - 0.5f), t.text, Align::LeftCenter);
  if (focused()) ring(p, r, t.radius);
}

bool ColorPicker::on_event(InputEvent& e) {
  switch (e.type) {
    case InputEvent::MouseDown:
      return e.button == MouseButton::Left;
    case InputEvent::MouseUp:
      if (e.button == MouseButton::Left && pressed_ && bounds().contains(e.pos)) open_popup();
      return true;
    case InputEvent::KeyDown:
      if (e.key == Key::Space || e.key == Key::Enter) {
        open_popup();
        return true;
      }
      return false;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Toasts
// ═════════════════════════════════════════════════════════════════════════════
namespace {
// Fades a toast out, then removes it.
void dismiss_toast(Window* w, View* v) {
  std::weak_ptr<bool> alive = v->alive_token();
  const float from = v->style().opacity;
  v->keep(anim::run(
      Transition(std::chrono::milliseconds(160), Easing::EaseIn),
      [v, alive, from](float t) {
        if (alive.expired()) return;
        v->style().opacity = from * (1 - t);
        v->invalidate();
      },
      [w, v, alive] {
        if (!alive.expired()) App::post([w, v, alive] {
          if (!alive.expired()) w->close_popup(v);
        });
      }));
}

class ToastView : public ViewT<ToastView> {
public:
  ToastView(std::string text, ToastKind kind) : text_(std::move(text)), kind_(kind) {
    style_.padding = Insets(14, 10, 10, 10);
  }
  const char* type_name() const override { return "Toast"; }
  Color stripe() const {
    const Theme& t = th();
    switch (kind_) {
      case ToastKind::Success: return t.success;
      case ToastKind::Warning: return t.warning;
      case ToastKind::Error: return t.danger;
      default: return t.accent;
    }
  }
  Size measure_content(Size avail) override {
    const Theme& t = th();
    const float maxw = std::min(360.f, std::max(160.f, avail.w - 40));
    float extra = 24; // close button
    for (auto& c : children_) extra += c->measure({200, 40}).w + 8;
    const Size ts = measure_text(text_, t.font, maxw - extra);
    return {std::min(maxw, std::ceil(ts.w) + extra + 4), std::max(24.f, std::ceil(ts.h))};
  }
  void layout() override {
    const Rect c = content_rect();
    float x = c.right() - 24;
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
      const Size s = (*it)->measure({200, 40});
      x -= s.w + 8;
      (*it)->set_frame({x, c.y + (c.h - s.h) / 2, s.w, s.h});
      (*it)->layout();
    }
    text_right_ = x - 4;
  }
  void paint(Painter& p) override {
    const Theme& t = th();
    const Rect r = bounds();
    p.fill_rect(r, t.surface, t.radius);
    p.stroke_rect(r, t.border, 1, t.radius);
    p.fill_rect({r.x + 4, r.y + 8, 3, r.h - 16}, stripe(), 1.5f);
    const Rect c = content_rect();
    p.text({c.x, c.y, text_right_ - c.x, c.h}, text_, t.font, t.text, Align::LeftCenter, true);
    // close ×
    const float cx = c.right() - 10, cy = c.y + c.h / 2;
    const Color xc = close_hot_ ? t.text : t.text_dim;
    p.line({cx - 4, cy - 4}, {cx + 4, cy + 4}, xc, 1.4f);
    p.line({cx + 4, cy - 4}, {cx - 4, cy + 4}, xc, 1.4f);
  }
  Rect close_rect() const {
    const Rect c = content_rect();
    return {c.right() - 22, c.y + c.h / 2 - 11, 24, 22};
  }
  bool on_event(InputEvent& e) override {
    if (e.type == InputEvent::MouseMove) {
      const bool h = close_rect().contains(e.pos);
      if (h != close_hot_) {
        close_hot_ = h;
        invalidate();
      }
      return true;
    }
    if (e.type == InputEvent::MouseDown) return true;
    if (e.type == InputEvent::MouseUp && e.button == MouseButton::Left && close_rect().contains(e.pos)) {
      if (window()) dismiss_toast(window(), this);
      return true;
    }
    return e.is_mouse();
  }
  void accessibility(A11yInfo& info) const override {
    View::accessibility(info);
    info.role = Role::Alert;
    info.name = text_;
  }
  void hover_changed(bool h) override {
    hovering_ = h;
    if (!h && close_hot_) {
      close_hot_ = false;
      invalidate();
    }
  }
  bool hovering_ = false;

private:
  std::string text_;
  ToastKind kind_;
  float text_right_ = 0;
  bool close_hot_ = false;
};
} // namespace

View* toast(Window& w, std::string text, ToastOptions o) {
  ToastView v(std::move(text), o.kind);
  if (!o.action_label.empty()) {
    auto action = o.action;
    Button b(o.action_label);
    b.flat();
    b.on_click([action] {
      if (action) action();
    });
    v.add(std::move(b));
  }
  PopupOptions po;
  po.place = PopupOptions::Place::BottomRight;
  po.dismiss_on_outside = false;
  View* view = w.open_popup(std::move(v), po);
  if (!view) return nullptr;
  // the action button closes the toast too
  if (!view->children().empty()) {
    auto* btn = static_cast<Button*>(view->children().front().get());
    Window* win = &w;
    std::weak_ptr<bool> alive = view->alive_token();
    view->keep(btn->clicked.connect([win, view, alive] {
      if (!alive.expired()) dismiss_toast(win, view);
    }).scoped());
  }
  // fade in
  {
    std::weak_ptr<bool> alive = view->alive_token();
    view->style().opacity = anim::speed() > 0 ? 0.f : 1.f;
    view->keep(anim::run(Transition(std::chrono::milliseconds(180)), [view, alive](float t) {
      if (alive.expired()) return;
      view->style().opacity = t;
      view->invalidate();
    }));
  }
  if (o.duration.count() > 0) {
    Window* win = &w;
    std::weak_ptr<bool> alive = view->alive_token();
    const auto d = o.duration;
    // wait while the mouse is over the toast
    auto check = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak_check = check;
    *check = [win, view, alive, weak_check] {
      if (alive.expired()) return;
      if (static_cast<ToastView*>(view)->hovering_) {
        if (auto c = weak_check.lock()) view->keep(App::after(std::chrono::milliseconds(800), [c] { (*c)(); }));
        return;
      }
      dismiss_toast(win, view);
    };
    view->keep(App::after(d, [check] { (*check)(); }));
  }
  return view;
}

View* toast(Window& w, std::string text, ToastKind kind, std::chrono::milliseconds duration) {
  ToastOptions o;
  o.kind = kind;
  o.duration = duration;
  return toast(w, std::move(text), std::move(o));
}

} // namespace pulseui
