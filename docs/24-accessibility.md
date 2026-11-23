# Accessibility

PulseUI windows can be used with screen readers - Narrator, NVDA and JAWS on Windows (UI Automation),
VoiceOver on macOS (NSAccessibility) - and driven by automation tools that use the same APIs.

## What is exposed

Every view describes itself with a **role** (button, edit, checkbox, slider, list...), a **name**, a
**value** and **states** (focusable, focused, enabled, checked, expanded, selected, a numeric range).
Screen readers can also act on views:

| Action | Views |
|---|---|
| press | `Button`, views with `on_click`, menu items, `Select`/`DatePicker`/`ColorPicker` (open), lists (activate) |
| toggle | `Checkbox`, `Toggle`, check items of menus |
| set value | `TextField`, `TextArea`, `CodeEditor`, `Select`, `Segmented`, `RadioGroup`, `Tabs`, `Slider`, `NumberField`, `Calendar`, `DatePicker`, `ColorPicker`/`ColorPanel` |
| increment / decrement | `Slider`, `NumberField` |
| expand / collapse | `Select`, `DatePicker`, `ColorPicker`, `TreeView`, submenus |
| focus | every focusable view |

Built-in views describe themselves; the keyboard focus is reported to the screen reader when it moves.
Layout containers (`VStack`, `Panel`, ...) are transparent: their children appear in their place.
Popups, menus and toasts (announced politely, like a live region) are part of the window's tree.

## Your part

Most of the time nothing - use the built-in views and give buttons readable labels. Where a view has no
text of its own, name it:

```cpp
Button("\xC3\x97").a11y_label("Close");                         // an icon button
Canvas(draw_chart).a11y_label("Sales by month").a11y_role(Role::Image);
TextField(email).placeholder("Email");                    // the placeholder is the field's name
Text("decoration").a11y_hidden();                         // hidden from screen readers (with its children)
Spacer().a11y_description("...");                         // longer help text
```

Tooltips also become the description of a view.

## Custom views

Override two functions:

```cpp
class Rating : public ViewT<Rating> {
public:
  void accessibility(A11yInfo& info) const override {
    View::accessibility(info);          // focus / enabled states, tooltip
    info.role = Role::Slider;
    info.name = "Rating";
    info.has_range = true;
    info.min = 0; info.max = 5; info.now = stars_; info.step = 1;
    info.value = std::to_string(stars_) + " stars";
  }
  bool accessibility_action(A11yAction a, const std::string& arg) override {
    if (a == A11yAction::Increment) { set_stars(stars_ + 1); return true; }
    if (a == A11yAction::Decrement) { set_stars(stars_ - 1); return true; }
    return View::accessibility_action(a, arg);
  }
  ...
};
```

`view.a11y_info()` returns the description with the `.a11y_*()` overrides applied - useful in tests.

## Testing with UI Automation

`tests/a11y_tests.cpp` is an example of a real UI Automation client: it reads a window's element tree in
another thread (names, control types, automation ids = view ids) and invokes buttons, toggles check
boxes, types into fields and moves sliders through UIA patterns - exactly what Narrator or a test tool
does. The same API (`IUIAutomation`) works from Python (`pywinauto`, `uiautomation`) or Accessibility
Insights for inspecting a running PulseUI app.

## Platform notes

- Windows: UI Automation provider (fragment root per window, one element per exposed view) with the
  Invoke, Toggle, Value, RangeValue and ExpandCollapse patterns. Building with old MinGW-w64 headers
  that lack UI Automation compiles without it (CMake reports it).
- macOS: `NSAccessibilityElement` per exposed view, with press, increment/decrement, value and focus.

Next: [back to the contents](README.md).
