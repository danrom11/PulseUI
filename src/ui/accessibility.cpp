// How each view describes itself to screen readers, and the actions they can ask for.
#include <pulseui/ui/accessibility.hpp>
#include <pulseui/ui/code_editor.hpp>
#include <pulseui/ui/collections.hpp>
#include <pulseui/ui/inputs.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/widgets.hpp>
#include <pulseui/ui/window.hpp>

#include <cmath>
#include <cstdlib>

namespace pulseui {

const char* role_name(Role r) {
  switch (r) {
    case Role::None: return "none";
    case Role::Group: return "group";
    case Role::Window: return "window";
    case Role::Text: return "text";
    case Role::Heading: return "heading";
    case Role::Button: return "button";
    case Role::Link: return "link";
    case Role::CheckBox: return "checkbox";
    case Role::Switch: return "switch";
    case Role::RadioButton: return "radio";
    case Role::Edit: return "edit";
    case Role::Document: return "document";
    case Role::Slider: return "slider";
    case Role::Spinner: return "spinner";
    case Role::ProgressBar: return "progressbar";
    case Role::ComboBox: return "combobox";
    case Role::List: return "list";
    case Role::ListItem: return "listitem";
    case Role::Table: return "table";
    case Role::Tree: return "tree";
    case Role::TreeItem: return "treeitem";
    case Role::Tab: return "tab";
    case Role::TabItem: return "tabitem";
    case Role::Menu: return "menu";
    case Role::MenuBar: return "menubar";
    case Role::MenuItem: return "menuitem";
    case Role::Image: return "image";
    case Role::Calendar: return "calendar";
    case Role::Toolbar: return "toolbar";
    case Role::Separator: return "separator";
    case Role::Pane: return "pane";
    case Role::Alert: return "alert";
  }
  return "none";
}

// ── View ─────────────────────────────────────────────────────────────────────

void View::accessibility(A11yInfo& info) const {
  info.role = click_handlers_.empty() ? Role::None : Role::Button;
  info.description = tooltip_;
  info.focusable = focusable_;
  info.focused = focused();
  info.enabled = is_enabled();
}

bool View::accessibility_action(A11yAction a, const std::string&) {
  switch (a) {
    case A11yAction::Press:
      if (click_handlers_.empty() || !is_enabled()) return false;
      {
        auto handlers = click_handlers_; // a handler may destroy the view
        for (auto& fn : handlers) fn();
      }
      return true;
    case A11yAction::Focus:
      if (!focusable_ || !window_) return false;
      focus();
      return true;
    default:
      return false;
  }
}

A11yInfo View::a11y_info() const {
  A11yInfo info;
  accessibility(info);
  if (extras_) {
    if (extras_->a11y_role) info.role = *extras_->a11y_role;
    if (extras_->a11y_label) info.name = *extras_->a11y_label;
    if (!extras_->a11y_description.empty()) info.description = extras_->a11y_description;
    // a named custom view (Canvas with a label, ...) is worth exposing
    if (info.role == Role::None && extras_->a11y_label && !extras_->a11y_label->empty()) info.role = Role::Group;
  }
  return info;
}

std::vector<View*> Window::top_views() const {
  std::vector<View*> out;
  if (menu_bar_) out.push_back(menu_bar_.get());
  if (root_) out.push_back(root_.get());
  for (auto& p : popups_) out.push_back(p.view.get());
  return out;
}

namespace detail {
// The accessible children of a view: visible descendants with a role; views
// without a role are see-through (their children take their place).
void a11y_children(const View* v, std::vector<View*>& out) {
  for (auto& c : v->children()) {
    if (!c->is_visible() || c->a11y_hidden()) continue;
    if (c->a11y_info().role != Role::None) out.push_back(c.get());
    else a11y_children(c.get(), out);
  }
}
void a11y_window_children(const Window& w, std::vector<View*>& out) {
  for (View* v : w.top_views()) {
    if (!v->is_visible() || v->a11y_hidden()) continue;
    if (v->a11y_info().role != Role::None) out.push_back(v);
    else a11y_children(v, out);
  }
}
// The nearest exposed ancestor (nullptr = the window itself).
View* a11y_parent(const View* v) {
  for (View* p = v->parent(); p; p = p->parent())
    if (!p->a11y_hidden() && p->a11y_info().role != Role::None) return p;
  return nullptr;
}
// The view itself if exposed, otherwise its nearest exposed ancestor.
View* a11y_exposed(View* v) {
  for (View* x = v; x; x = x->parent())
    if (x->is_visible() && !x->a11y_hidden() && x->a11y_info().role != Role::None) return x;
  return nullptr;
}
} // namespace detail

namespace {
std::string percent(double v) {
  return std::to_string(static_cast<int>(std::lround(v * 100))) + " %";
}
std::optional<double> number(const std::string& s) {
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str()) return std::nullopt;
  return v;
}
} // namespace

// ── Text / Button / TextField ────────────────────────────────────────────────

void Text::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = (font_size_ >= 18 || weight_ >= FontWeight::SemiBold) && text_.size() < 80 ? Role::Heading : Role::Text;
  if (info.role == Role::Text && !click_handlers_.empty()) info.role = Role::Link;
  info.name = text_;
}
bool Text::accessibility_action(A11yAction a, const std::string& arg) { return View::accessibility_action(a, arg); }

void Button::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Button;
  info.name = label_.empty() && !is_svg_icon(icon_) ? icon_ : label_;
}
void Button::press() {
  std::weak_ptr<bool> alive = alive_;
  auto handlers = click_handlers_;
  clicked.emit();
  if (alive.expired()) return;
  for (auto& fn : handlers) fn();
}
bool Button::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::Press) {
    if (!is_enabled()) return false;
    press();
    return true;
  }
  return View::accessibility_action(a, arg);
}

void TextField::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Edit;
  info.name = placeholder_;
  info.value = password_ ? std::string() : text_;
}
bool TextField::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    if (!is_enabled()) return false;
    edit(arg, arg.size());
    committed.emit(text_);
    return true;
  }
  return View::accessibility_action(a, arg);
}

// ── Slider / ProgressBar / Select / Segmented / Tabs ─────────────────────────

void Slider::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Slider;
  info.has_range = true;
  info.min = min_;
  info.max = max_;
  info.now = value_;
  info.step = step_ > 0 ? step_ : (max_ - min_) / 20;
  info.value = max_ - min_ == 1 && min_ == 0 ? percent(value_) : std::to_string(value_);
}
bool Slider::accessibility_action(A11yAction a, const std::string& arg) {
  const double step = step_ > 0 ? step_ : (max_ - min_) / 20;
  switch (a) {
    case A11yAction::Increment: set_value(value_ + step); return true;
    case A11yAction::Decrement: set_value(value_ - step); return true;
    case A11yAction::SetValue:
      if (auto v = number(arg)) {
        set_value(*v);
        return true;
      }
      return false;
    default: return View::accessibility_action(a, arg);
  }
}

void ProgressBar::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::ProgressBar;
  info.has_range = true;
  info.min = 0;
  info.max = 1;
  info.now = value_;
  info.read_only = true;
  info.value = percent(value_);
}
bool ProgressBar::accessibility_action(A11yAction a, const std::string& arg) { return View::accessibility_action(a, arg); }

void Select::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::ComboBox;
  info.value = index_ >= 0 && index_ < static_cast<int>(options_.size()) ? options_[static_cast<std::size_t>(index_)] : "";
  info.expanded = popup_ ? 1 : 0;
}
bool Select::accessibility_action(A11yAction a, const std::string& arg) {
  switch (a) {
    case A11yAction::Expand:
    case A11yAction::Press: open_popup(); return true;
    case A11yAction::Collapse:
      if (popup_ && window()) window()->close_popup(popup_);
      return true;
    case A11yAction::SetValue:
      for (std::size_t i = 0; i < options_.size(); ++i)
        if (options_[i] == arg) {
          set_index(static_cast<int>(i));
          return true;
        }
      return false;
    default: return View::accessibility_action(a, arg);
  }
}

void Segmented::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::List;
  info.value = index_ >= 0 && index_ < static_cast<int>(options_.size()) ? options_[static_cast<std::size_t>(index_)] : "";
}
bool Segmented::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    for (std::size_t i = 0; i < options_.size(); ++i)
      if (options_[i] == arg) {
        set_index(static_cast<int>(i));
        return true;
      }
    return false;
  }
  return View::accessibility_action(a, arg);
}

void Tabs::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Tab;
  info.value = current_ >= 0 && current_ < static_cast<int>(titles_.size()) ? titles_[static_cast<std::size_t>(current_)] : "";
}
bool Tabs::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    for (std::size_t i = 0; i < titles_.size(); ++i)
      if (titles_[i] == arg) {
        set_current(static_cast<int>(i));
        return true;
      }
    return false;
  }
  return View::accessibility_action(a, arg);
}

// ── collections ──────────────────────────────────────────────────────────────

void ListBase::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::List;
  if (selected_ >= 0) info.value = "item " + std::to_string(selected_ + 1) + " of " + std::to_string(count_);
}
bool ListBase::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    if (auto v = number(arg)) {
      set_selected(static_cast<int>(*v) - 1);
      scroll_to_row(selected_);
      return true;
    }
    return false;
  }
  if (a == A11yAction::Press && selected_ >= 0) {
    activated.emit(selected_);
    return true;
  }
  return View::accessibility_action(a, arg);
}

void TableBase::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Table;
  if (selected_ >= 0 && selected_ < static_cast<int>(cells_.size())) {
    std::string v;
    for (std::size_t c = 0; c < cols_.size() && c < cells_[static_cast<std::size_t>(selected_)].size(); ++c) {
      if (!v.empty()) v += ", ";
      v += cols_[c].title + ": " + cells_[static_cast<std::size_t>(selected_)][c];
    }
    info.value = v;
  }
}
bool TableBase::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::Press && selected_ >= 0) {
    activated.emit(selected_);
    return true;
  }
  return View::accessibility_action(a, arg);
}

void TreeView::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Tree;
  if (const TreeItem* it = selected_item()) info.value = it->label;
}
bool TreeView::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    select(arg);
    return true;
  }
  if (a == A11yAction::Expand || a == A11yAction::Collapse) {
    expand(selected_, a == A11yAction::Expand);
    return true;
  }
  if (a == A11yAction::Press) {
    if (const TreeItem* it = selected_item()) activated.emit(*it);
    return true;
  }
  return View::accessibility_action(a, arg);
}

void CodeEditor::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Document;
  info.multi_line = true;
  info.value = text();
}
bool CodeEditor::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    set_text(arg);
    return true;
  }
  return View::accessibility_action(a, arg);
}

// ── inputs ───────────────────────────────────────────────────────────────────

void TextArea::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Edit;
  info.multi_line = true;
  info.name = placeholder_;
  info.value = text_;
  info.read_only = read_only_;
}
bool TextArea::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue && !read_only_) {
    edit(0, text_.size(), arg);
    return true;
  }
  return View::accessibility_action(a, arg);
}

void NumberField::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Spinner;
  info.value = format(value_);
  info.has_range = true;
  info.min = std::isfinite(min_) ? min_ : -1e9;
  info.max = std::isfinite(max_) ? max_ : 1e9;
  info.now = value_;
  info.step = step_;
}
bool NumberField::accessibility_action(A11yAction a, const std::string& arg) {
  switch (a) {
    case A11yAction::Increment: step_by(1); return true;
    case A11yAction::Decrement: step_by(-1); return true;
    case A11yAction::SetValue:
      if (auto v = parse(arg)) {
        set_value(*v);
        return true;
      }
      return false;
    default: return View::accessibility_action(a, arg);
  }
}

void RadioGroup::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::List;
  info.value = index_ >= 0 && index_ < static_cast<int>(options_.size()) ? options_[static_cast<std::size_t>(index_)] : "";
}
bool RadioGroup::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    for (std::size_t i = 0; i < options_.size(); ++i)
      if (options_[i] == arg) {
        set_index(static_cast<int>(i));
        return true;
      }
    return false;
  }
  return View::accessibility_action(a, arg);
}

void Calendar::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Calendar;
  info.value = date_.iso();
}
bool Calendar::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    if (auto d = Date::parse(arg)) {
      set_date(*d);
      return true;
    }
    return false;
  }
  return View::accessibility_action(a, arg);
}

void DatePicker::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::ComboBox;
  info.value = display_text();
  info.expanded = popup_ ? 1 : 0;
}
bool DatePicker::accessibility_action(A11yAction a, const std::string& arg) {
  switch (a) {
    case A11yAction::Press:
    case A11yAction::Expand: open_popup(); return true;
    case A11yAction::Collapse:
      if (popup_ && window()) window()->close_popup(popup_);
      return true;
    case A11yAction::SetValue:
      if (auto d = Date::parse(arg)) {
        set_date(*d);
        return true;
      }
      return false;
    default: return View::accessibility_action(a, arg);
  }
}

void ColorPanel::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::Group;
  info.name = "Color";
  info.value = color_.to_hex();
}
bool ColorPanel::accessibility_action(A11yAction a, const std::string& arg) {
  if (a == A11yAction::SetValue) {
    if (auto c = Color::parse(arg)) {
      set_color(*c);
      return true;
    }
    return false;
  }
  return View::accessibility_action(a, arg);
}

void ColorPicker::accessibility(A11yInfo& info) const {
  View::accessibility(info);
  info.role = Role::ComboBox;
  info.value = color_.to_hex();
  info.expanded = popup_ ? 1 : 0;
}
bool ColorPicker::accessibility_action(A11yAction a, const std::string& arg) {
  switch (a) {
    case A11yAction::Press:
    case A11yAction::Expand: open_popup(); return true;
    case A11yAction::Collapse:
      if (popup_ && window()) window()->close_popup(popup_);
      return true;
    case A11yAction::SetValue:
      if (auto c = Color::parse(arg)) {
        set_color(*c);
        return true;
      }
      return false;
    default: return View::accessibility_action(a, arg);
  }
}

} // namespace pulseui
