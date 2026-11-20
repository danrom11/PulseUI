# 4. Tutorial: forms with Pulse Layer

Writing view trees in C++ is pleasant, but for windows with many controls it is faster to **draw**
them. Pulse Layer is the visual designer for PulseUI, and it is written in PulseUI itself. This
tutorial walks through the complete workflow: design a form, **bind it to a C++ class in the
designer**, write the logic, build and run it, add a second form, and load forms at runtime.

## The idea

A form has three parts:

| Part | Where | Stored in |
|---|---|---|
| **Layout**: views, order, sizes, texts, ids | Pulse Layer, Design mode | `name.pform` |
| **Bindings**: "this field edits `email`", "this button calls `submit()`" | Pulse Layer, the Properties panel | `name.pform` |
| **State and behaviour**: properties, computed values, methods | your C++ class, Code mode | `name.hpp` / `name.cpp` |

At build time the form becomes a small generated header. Its bindings are written as ordinary
PulseUI code (`TextField().model(m.email)`, `Button("Sign up").on_click([&m] { m.submit(); })`), so
**the compiler checks them**: a misspelled or deleted member is a build error on a clear line, not a
silently dead button.

Your class contains state and logic only, with no "find the field and connect it" code.

## Step 1: Create a project

Start Pulse Layer, choose **New** (Ctrl+N), then **Save** (Ctrl+S) into an empty folder, for example
`Signup/Signup.pulse`:

```
Signup/
├── Signup.pulse          the project: list of forms and files
├── main.cpp              creates the App and the first window
├── mainwindow.pform      layout and bindings of the MainWindow form
├── mainwindow.hpp        class MainWindow: state and actions
└── mainwindow.cpp
```

## Step 2: Design the layout

Switch to **Design** (Ctrl+1):

- **Palette** (left): click a view type to add it to the selected container, or drag it to an exact
  position on the canvas.
- **Canvas** (centre): real PulseUI views. Click to select, drag to move (a blue line shows where it
  lands), and drag the handles to resize. Middle-drag or Alt-drag pans; Esc selects the parent.
- **Hierarchy** (top right): the view tree.
- **Properties** (bottom right): the selection's properties, its bindings and its events. With nothing
  selected, it edits the form's title and size.

Build this sign-up form:

```
VStack (padding 24, spacing 12)
├── Text "Create an account"        font_size 22, weight SemiBold
├── TextField   id "name"           placeholder "Name"
├── TextField   id "email"          placeholder "Email"
├── TextField   id "password"       placeholder "Password (8+ characters)"
├── Checkbox    id "terms"          label "I accept the terms"
├── Text        id "hint"
├── VStack      id "recent"         (a placeholder, filled from code in step 5)
├── Spacer
└── HStack (spacing 8)
    ├── Spacer
    ├── Button  id "cancel"         label "Cancel", kind Flat
    └── Button  id "submit"         label "Sign up", kind Primary
```

Tips:

- **Give an id to every view you will refer to.** A view with an id becomes a typed member of the
  generated form struct (`ui_.email`). Ids must be valid C++ names.
- **Let stacks do the layout**: `flex` for views that take the remaining space, `Spacer` to push things
  apart, few fixed sizes. Resize the form in the Properties panel to check that the layout adapts.
- Press **F5** for a live preview window.

| Palette group | Views |
|---|---|
| Layout | `VStack`, `HStack`, `Panel`, `Layer`, `Scroll`, `Spacer`, `Divider` |
| Display | `Text`, `Image`, `ProgressBar` |
| Controls | `Button`, `TextField`, `Checkbox`, `Toggle`, `Slider`, `Select` |

## Step 3: Bind the form to your class

Select the `name` field. In the **Properties** panel, a **chain icon** appears next to every property that can be
bound. Click it: the property's editor is replaced by the class name and a field holding the name of a
**member of the form class**, so it reads like C++: `MainWindow::` `name`. The suggestion is the view id (`name`); type another name if you like
and press Enter. Click the chain again to remove the binding and go back to a fixed value. A bound
property shows a blue chain; an unbound one shows a grey chain. In the hierarchy, views that have
bindings or event handlers are marked with a small blue chain after their id.

Note the difference between a view's **id** and a binding: the id names the *view* (it becomes the
member `ui_.name` of the generated form struct), while a binding names a member of *your class*
(`MainWindow::name`). They often share a name because the designer suggests the id, but they are
independent: renaming the id changes only `ui_.<id>`.

Below the properties, the **Events** section lists the view's events (`click` for buttons; `change`,
`Enter` and `commit` for text fields...). Type the name of a method to call, or press **+** for a
suggestion.

Bind the sign-up form like this:

| View | Bind | To member |
|---|---|---|
| `name` | text | `name` |
| `email` | text | `email` |
| `password` | text | `password` |
| `terms` | checked | `accepted` |
| `hint` | text | `problem` |
| `submit` | enabled | `can_submit` |
| `submit` | event click | `submit` |
| `cancel` | event click | `cancel` |

### What can be bound

| View | Property | The member should be |
|---|---|---|
| `TextField` | text | `pulse::property<std::string>` (two-way) |
| `Checkbox`, `Toggle` | checked | `pulse::property<bool>` (two-way) |
| `Slider` | value | `pulse::property<double>` (two-way) |
| `Select` | index | `pulse::property<int>` (two-way) |
| `Select` | options | a `std::vector<std::string>` value or stream |
| `Text` | text, color | a string / `Color` value or stream |
| `Button` | label | a string value or stream |
| `ProgressBar` | value | a `double` (0..1) value or stream |
| `TextField` | placeholder | a string value or stream |
| every view | visible, enabled | a `bool` value or stream |
| every view | background | a `Color` value or stream |
| every view | tooltip | a string value or stream |

"Value or stream" means a `pulse::property`, a `pulse::computed`, a `pulse::observable` or a plain
value. Editors that write back (marked two-way) need a `pulse::property`.

| View | Events | The method may take |
|---|---|---|
| `Button` | click | nothing |
| `TextField` | change, Enter (`submit`), commit | `const std::string&`, or nothing |
| `Checkbox`, `Toggle` | change | `bool`, or nothing |
| `Slider` | change | `double`, or nothing |
| `Select` | change | `int`, or nothing |

A member can also be a path into another object: `settings.dark` binds to the `dark` member of
your class's `settings` member.

## Step 4: Write the class

Switch to **Code** (Ctrl+2) and open `mainwindow.hpp`:

```cpp
// mainwindow.hpp
#pragma once
#include "ui_mainwindow.hpp"

class MainWindow {
public:
  MainWindow();
  void show() { window_.show(); }

private:
  friend struct Ui::MainWindow;          // (1) the generated code may bind private members

  // actions (called by the form)
  void submit();
  void cancel() { window_.close(); }

  // state (bound by the form)
  pulse::property<std::string> name, email, password;
  pulse::property<bool> accepted{false};
  pulse::computed<std::string> problem;  // "" when the form is valid
  pulse::computed<bool> can_submit;

  Ui::MainWindow ui_;                    // (2) the views with an id: ui_.email, ui_.recent...
  pulseui::Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
};
```

```cpp
// mainwindow.cpp
#include "mainwindow.hpp"
using namespace pulseui;

MainWindow::MainWindow()
    : problem([this] {
        if (name.get().empty()) return std::string("Enter your name");
        if (email.get().find('@') == std::string::npos) return std::string("Enter a valid email");
        if (password.get().size() < 8) return std::string("The password needs 8+ characters");
        if (!accepted.get()) return std::string("Please accept the terms");
        return std::string();
      }, name, email, password, accepted),
      can_submit([this] { return problem.get().empty(); }, problem) {
  ui_.setup(window_, *this);             // (3) build the form; its bindings connect to *this
  ui_.password->password();              // (4) anything else: use the views directly
  ui_.hint->dim();
}

void MainWindow::submit() {
  window_.set_title("Welcome, " + name.get() + "!");
}
```

That is the whole class. There are no lookups, no null checks and no connecting code.

**(1) `friend struct Ui::MainWindow;`** lets the generated code reach private members, so the state can
stay encapsulated. (Making the bound members public works too.)

**(2) `Ui::MainWindow ui_`** is the generated form struct. It has a typed pointer for every view with an
id: `ui_.email` is a `pulseui::TextField*`, `ui_.submit` is a `pulseui::Button*`, and so on.

**(3) `ui_.setup(window_, *this)`** builds the form into the window, fills the pointers, and connects the
bindings to the object you pass, here the window class itself. The object is called the form's
**model**, and it can be any object: the class, a separate view model, or a shared store (step 7).

**(4)** For what bindings do not express, use the typed pointers. They are valid after `setup`.
Code mode completes `ui_.` with the form's views and `ui_.submit->` with the members of a `Button`.

### What the build generates

`ui_mainwindow.hpp` is regenerated from the form on every build. You never edit it, but it helps to see
it once:

```cpp
namespace Ui {
struct MainWindow {
  static constexpr const char* form_title = "Sign up";
  static constexpr float form_width = 380, form_height = 460;

  // Views with an id (valid after setup).
  pulseui::TextField* name = nullptr;
  pulseui::TextField* email = nullptr;
  pulseui::Button* submit = nullptr;
  // ...

  template <class Model>
  void setup(pulseui::Window& window, Model& m) { window.content(build(m)); }

  template <class Model>
  pulseui::Node build(Model& m) {
    using namespace pulseui;
    return VStack{
        Text("Create an account").font_size(22).weight(FontWeight::SemiBold),
        TextField().placeholder("Name").id("name").ref(name).model(m.name),
        TextField().placeholder("Email").id("email").ref(email).model(m.email),
        // ...
        Button("Sign up").primary().id("submit").ref(submit).enabled(m.can_submit).on_click([&m] { m.submit(); }),
    }.spacing(12).padding(24);
  }
};
}
```

It is the same declarative code you would write by hand, so errors read naturally. If you rename
`can_submit` in the class but not in the form, the compiler reports that `MainWindow` has no member
`can_submit`, pointing at this line, and Pulse Layer shows it in **Issues**.

Event handlers may take the event's value or nothing. The generated code calls whichever form your
method has.

## Step 5: Add views from code

Some content is created from data: lists (`ForEach`), segmented controls, charts (`Canvas`), editors
(`CodeEditor`). Design an empty container with an id as a placeholder and fill it after `setup`:

```cpp
// in the class
pulse::property<std::vector<std::string>> recent;

// in the constructor, after ui_.setup(...)
ui_.recent->add(ForEach(recent, [](const std::string& n) { return Text(n); }));

// in submit()
recent.update([&](auto& v) { v.insert(v.begin(), name.get()); });
```

`host->add(...)` appends; `host->insert(0, ...)` puts the new view first, for example a `Canvas` behind
the other children of a `Layer`.

## Step 6: Build and run

1. Open **⚙ Settings → Kits**. Pulse Layer detects compilers in the background: Visual Studio (MSVC),
   LLVM Clang and MinGW on Windows; Apple Clang and Homebrew compilers on macOS. It also finds CMake
   and Ninja. Choose a kit.
2. Press **Build** (Ctrl+B). Pulse Layer writes a CMake project into `.pulselayer-build/` next to your
   project and compiles it. The first build compiles PulseUI once (about 10–20 s); later builds are
   incremental.
3. Errors appear in **Issues** and are underlined in the editor; click to jump.
4. **Run** (Ctrl+R) builds and starts the program; **Stop** ends it.

## Step 7: More forms and shared state

In Code mode press **+ Form** and name it `Preferences`. The project gets `preferences.pform`,
`preferences.hpp` and `preferences.cpp`.

Forms of one application usually share state. Put it in a model object and let each form bind to it:

```cpp
struct Settings {
  pulse::property<bool> dark{true};
  pulse::property<int> language{0};
};
```

In the Preferences form, bind the `dark` toggle's *checked* to `settings.dark`, the language select's
*index* to `settings.language`, and the *Done* button's click to `close`:

```cpp
class Preferences {
public:
  explicit Preferences(Settings& s) : settings(s) {
    ui_.setup(window_, *this);
    window_.on_close = [this] { close(); return false; };   // closing only hides
  }
  void show() { window_.show(); }

private:
  friend struct Ui::Preferences;
  void close() { window_.hide(); }
  Settings& settings;                     // bindings reach into it: m.settings.dark

  Ui::Preferences ui_;
  pulseui::Window window_{Ui::Preferences::form_title, Ui::Preferences::form_width, Ui::Preferences::form_height};
};
```

The main window owns it and opens it on demand:

```cpp
std::unique_ptr<Preferences> prefs_;
void open_preferences() {                 // bound to a button's click in the main form
  if (!prefs_) prefs_ = std::make_unique<Preferences>(settings_);
  prefs_->show();
}
```

Every window bound to `settings` updates as soon as a toggle changes; there is no "Apply" step.
A form can also be set up with a separate model directly: `ui_.setup(window_, settings)`.

## Step 8 (optional): Load forms at runtime

A program can also load `.pform` files when it runs. That is useful for user-customizable layouts and for
tools. There is no generated code then, so bindings stored in the file are not applied automatically.
Connect the views in code. `Window::get<T>(id)` returns a reference and throws a clear
`std::runtime_error` (`view 'email' is a Select, not a TextField`) if the form does not match the code:

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

int main() {
  App app;
  std::string error;
  auto form = load_form("signup.pform", &error);
  if (!form) { std::fprintf(stderr, "cannot load form: %s\n", error.c_str()); return 1; }

  Window win(form->title, form->width, form->height);
  win.content(std::move(form->root));

  pulse::property<std::string> name;
  win.get<TextField>("name").model(name);
  win.get<Button>("submit").on_click([&] { win.set_title("Hi " + name.get()); });

  win.show();
  return app.run();
}
```

`win.find<T>(id)` is the non-throwing variant that returns `nullptr`, for views that may legitimately be
missing. See [Forms and reflection](14-forms-and-meta.md) for the file format and the API.

## Exporting a layout

**Export form** (Ctrl+E, Design mode) saves the current form as a standalone header with a function that
returns the view tree. For a form with bindings it is `template <class Model> Node make_<title>(Model& m)`;
call it with the object that has the bound members. It contains only the design, not your code files.

## Good practices

- **Bind in the form, compute in the class.** Every bindable fact ("is the form valid?", "what does the
  status say?") is one `pulse::computed` member. The form binds to it.
- **Keep handlers small.** A bound method should do one action; derived UI state follows by itself.
- **Use `ui_.x` for the rest.** Typed pointers replace string lookups in compiled forms; reserve
  `get<T>()` / `find<T>()` for forms loaded at runtime.
- **Name things after their meaning**, not their widget: `can_submit` rather than `submit_enabled`.
- **Put `.pform` files under version control.** They are formatted JSON with stable key order, so diffs
  are readable, including bindings.
