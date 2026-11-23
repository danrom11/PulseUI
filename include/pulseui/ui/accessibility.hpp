#pragma once
// Accessibility: what screen readers (Narrator, NVDA, JAWS, VoiceOver) see.
//
// Every view describes itself with a role, a name, a value and states; the
// platform layer turns that into UI Automation (Windows) or NSAccessibility
// (macOS) elements and routes their actions (press, toggle, set value, ...)
// back to the view. Built-in views describe themselves; for the rest:
//   Canvas(draw).a11y_label("Sales chart").a11y_role(Role::Image)
//   Button("×").a11y_label("Close")
//   Spacer().a11y_hidden()
#include <string>

namespace pulseui {

enum class Role {
  None,        // not exposed by itself (layout containers); its children are
  Group,
  Window,
  Text,
  Heading,
  Button,
  Link,
  CheckBox,
  Switch,
  RadioButton,
  Edit,        // single or multi-line text input
  Document,    // a code editor
  Slider,
  Spinner,     // a number with arrows
  ProgressBar,
  ComboBox,    // a drop-down
  List,
  ListItem,
  Table,
  Tree,
  TreeItem,
  Tab,
  TabItem,
  Menu,
  MenuBar,
  MenuItem,
  Image,
  Calendar,
  Toolbar,
  Separator,
  Pane,
  Alert,       // a toast / notification
};

struct A11yInfo {
  Role role = Role::None;
  std::string name;         // what it is called ("Save", "Email")
  std::string value;        // its content ("ada@example.com", "75 %")
  std::string description;  // longer help text (tooltips end up here)
  bool focusable = false, focused = false, enabled = true;
  bool selected = false, read_only = false, multi_line = false;
  int checked = -1;         // -1: not checkable, 0 off, 1 on
  int expanded = -1;        // -1: not expandable, 0 collapsed, 1 expanded
  bool has_range = false;
  double min = 0, max = 0, now = 0, step = 0;
};

enum class A11yAction {
  Press,      // click / invoke
  Toggle,
  SetValue,   // with a string argument
  Increment,
  Decrement,
  Expand,
  Collapse,
  Focus,
};

const char* role_name(Role r);

} // namespace pulseui
