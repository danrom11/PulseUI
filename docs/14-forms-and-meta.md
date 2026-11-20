# 14. Forms and reflection

A **form** is a view tree stored as data: a `.pform` JSON file with a title, a size and a root view.
Forms are what the Pulse Layer designer produces. The workflow is covered in the
[forms tutorial](04-forms-tutorial.md); this chapter documents the format and the APIs behind it.

## The `.pform` format

```json
{
  "format": "pulseui-form",
  "version": 1,
  "title": "Sign in",
  "width": 380,
  "height": 330,
  "root": {
    "type": "VStack",
    "props": { "spacing": 12, "padding": 24 },
    "children": [
      { "type": "Text", "props": { "text": "Welcome back", "font_size": 22, "weight": 3, "id": "title" } },
      { "type": "TextField", "props": { "placeholder": "Email", "id": "email" }, "bind": { "text": "email" } },
      { "type": "Spacer" },
      {
        "type": "HStack",
        "children": [
          { "type": "Spacer" },
          { "type": "Button", "props": { "label": "Sign in", "kind": 1, "id": "ok" },
            "bind": { "enabled": "can_sign_in" }, "on": { "click": "sign_in" } }
        ]
      }
    ]
  }
}
```

| Key | Meaning |
|---|---|
| `format` | always `"pulseui-form"` |
| `version` | format version, currently `1` |
| `title`, `width`, `height` | window title and size |
| `root` | the root view |

Each view object has:

| Key | Meaning |
|---|---|
| `type` | a registered type name: `VStack`, `HStack`, `Panel`, `Layer`, `Scroll`, `Spacer`, `Divider`, `Text`, `Image`, `ProgressBar`, `Button`, `TextField`, `Checkbox`, `Toggle`, `Slider`, `Select`, or a [custom type](#custom-view-types) |
| `props` | optional: properties that differ from the type's defaults |
| `bind` | optional: bindings, property name → member of the model (`"text": "email"`, `"enabled": "settings.ready"`) |
| `on` | optional: events, event name → method of the model (`"click": "sign_in"`) |
| `children` | optional: child views, for containers |

Property values by type:

| Property type | JSON | Example |
|---|---|---|
| number | number | `"font_size": 22` |
| bool | `true` / `false` | `"wrap": true` |
| string | string | `"label": "OK"` |
| color | `"#RRGGBB"` or `"#RRGGBBAA"` (any case when reading) | `"background": "#3D7EFF"` |
| enum | the option index | `"kind": 1` (Primary), `"align_items": 1` (Center) |

Only non-default properties are written, so files stay small and readable. Unknown properties are
ignored when loading.

### Bindings in the file

`bind` and `on` are what the designer's chain icons and Events section edit (see the
[forms tutorial](04-forms-tutorial.md#step-3-bind-the-form-to-your-class)). Member names are C++ names,
or dotted paths into members (`store.dark`). They are not evaluated by the loader: they become code
when the form is generated into C++, which is how the compiler checks them. In memory they are kept as
view **annotations** (`"bind.text" → "email"`, `"on.click" → "sign_in"`), so loading and saving a form
preserves them:

```cpp
const std::string* View::annotation(std::string_view key) const;          // nullptr if absent
void View::set_annotation(std::string_view key, std::string value);        // "" removes
const std::vector<std::pair<std::string, std::string>>& View::annotations() const;
```

Which properties can be bound, and to what, is listed in the
[forms tutorial](04-forms-tutorial.md#what-can-be-bound).

### Properties of each type

Common to all views:

| Property | Type | Notes |
|---|---|---|
| `id` | string | |
| `x`, `y` | number | absolute position (sets `positioned`) |
| `width`, `height`, `min_width`, `min_height` | number | 0 = automatic |
| `flex` | number | |
| `padding` | number | the same on all sides |
| `align` | enum | Auto, Start, Center, End, Stretch |
| `background` | color | |
| `radius` | number | |
| `visible`, `enabled` | bool | |
| `tooltip` | string | |

Type-specific:

| Type | Properties |
|---|---|
| `VStack`, `HStack`, `Panel` | `spacing`; `align_items` (Start, Center, End, Stretch); `justify` (Start, Center, End, SpaceBetween) |
| `Divider` | `orientation` (Horizontal, Vertical) |
| `Text` | `text`; `font_size`; `weight` (Light, Normal, Medium, SemiBold, Bold); `text_align` (Left, Center, Right); `wrap`; `color` |
| `Image` | `source` (file path) |
| `ProgressBar` | `value` (0..1) |
| `Button` | `label`; `kind` (Normal, Primary, Flat, Danger) |
| `TextField` | `placeholder`; `text` |
| `Checkbox`, `Toggle` | `label`; `checked` |
| `Slider` | `value`; `min`; `max` |
| `Select` | `options` (comma-separated); `index` |

## Loading and saving

```cpp
struct Form { std::string title; float width, height; Node root; };

std::optional<Form> load_form(const std::string& path, std::string* error = nullptr);
bool save_form(const std::string& path, const std::string& title, float width, float height, View& root);

json::Value form_to_json(const std::string& title, float width, float height, View& root);
std::optional<Form> form_from_json(const json::Value& j, std::string* error = nullptr);

json::Value view_to_json(View& v);                                    // a single subtree
Node view_from_json(const json::Value& j, std::string* error = nullptr);
```

Errors (missing file, invalid JSON, unknown type) are reported through `error`.

```cpp
std::string err;
if (auto f = load_form("settings.pform", &err)) {
  Window w(f->title, f->width, f->height);
  w.content(std::move(f->root));
  // bind by id...
} else {
  report(err);
}
```

`view_to_json` / `view_from_json` work on any subtree. They are useful for copy and paste of view
trees, storing user-arranged layouts, or building templates.

## Generating C++

```cpp
std::string generate_cpp(View& root, const std::string& function_name = "make_form", int indent = 0);
std::string generate_cpp_expression(View& root, int indent = 0);
std::string generate_cpp_expression(View& root, int indent, const CodegenOptions& options);

struct CodegenOptions {
  std::string model = "m";             // the model object bindings refer to
  bool refs = false;                   // emit .ref(<id>) for views listed by form_refs()
  std::vector<std::string> reserved;   // ids that must not become members
};

struct FormRef { std::string id; std::string cpp_class; View* view; };
std::vector<FormRef> form_refs(View& root, const std::vector<std::string>& reserved = {});
bool form_has_bindings(View& root);
bool is_cpp_identifier(std::string_view s);
bool is_binding_path(std::string_view s);     // "name", "settings.dark"
```

`generate_cpp_expression` produces the declarative expression; bindings become modifiers on the model:

```cpp
VStack{
  Text("Welcome back").id("title").font_size(22).weight(FontWeight::SemiBold),
  TextField().placeholder("Email").id("email").ref(email).model(m.email),
  Spacer(),
  HStack{ Spacer(), Button("Sign in").primary().id("ok").ref(ok).enabled(m.can_sign_in).on_click([&m] { m.sign_in(); }) },
}.spacing(12).padding(24)
```

`generate_cpp` wraps it in a header with an inline function, which is a template taking the model
(`template <class Model> Node make_form(Model& m)`) when the form has bindings. `form_refs` lists the
views whose id can become a C++ member: valid identifiers, first occurrence, not reserved.

Pulse Layer generates a `ui_<form>.hpp` per form from these pieces: a struct with `form_title`,
`form_width` and `form_height`, a typed pointer per `form_refs()` entry, and
`setup(Window&, Model&)` / `build(Model&)`. The generated code uses only the public, declarative API, so
it reads like hand-written code.

## The JSON library

`pulseui/core/json.hpp` contains the small JSON value type used by forms. Apps can use it for settings
files:

```cpp
json::Value v = json::Value::object();
v["name"] = "Pulse";
v["size"] = 13;
v["tags"] = json::Value::array();
v["tags"].push_back("ui");
std::string text = v.dump(2);                 // pretty-printed; dump(0) is compact

auto parsed = json::Value::parse(text, &error);   // std::optional<json::Value>
(*parsed)["name"].as_string();               // "Pulse"; as_number(def), as_bool(def) too
parsed->find("missing");                     // nullptr when absent (non-const operator[] would insert it)
for (const json::Value& t : (*parsed)["tags"].items()) { ... }
for (const auto& [key, value] : parsed->fields()) { ... }
```

Objects keep insertion order, which makes saved files diff-friendly.

## Reflection: `pulseui::meta`

The designer and the form loader need to create views by name and get or set their properties
generically. That is the job of the small reflection layer in `pulseui/ui/meta.hpp`.

```cpp
namespace pulseui::meta {
  enum class PropType { Bool, Number, String, Color, Enum };
  using Value = std::variant<std::monostate, bool, double, std::string, Color>;

  struct Property {
    std::string name, group;                 // "font_size", "Text"
    PropType type;
    std::vector<std::string> options;        // for Enum
    double min, max;                         // for Number
    std::function<Value(View&)> get;
    std::function<void(View&, const Value&)> set;
    std::function<std::string(View&, const Value&)> code;   // C++ modifier for codegen, e.g. ".font_size(18)"
    std::function<std::string(const std::string& expr)> bind;   // bindable: modifier for a binding, e.g. ".model(m.x)"
    std::string bind_hint;                                     // shown by the designer ("pulse::property<bool>")
  };

  struct Event {
    std::string name;       // "click" (the key under "on")
    std::string modifier;   // "on_click"
    std::string arg;        // handler argument type, "" = none
  };

  struct TypeInfo {
    std::string name, category, glyph;       // "Button", "Controls", "▭"
    bool container;
    std::function<std::unique_ptr<View>()> create;
    std::function<std::string(View&)> ctor_code;            // "Button(\"OK\")"
    std::vector<Property> props;             // type-specific properties
    std::vector<Event> events;               // events forms can connect to methods
    std::string cpp_class;                   // when the C++ class differs from name ("Image" -> "ImageView")
  };

  const std::vector<TypeInfo>& types();
  const TypeInfo* find(std::string_view type_name);
  void register_type(TypeInfo info);         // add or replace
  std::unique_ptr<View> create(std::string_view type_name);
  std::vector<const Property*> properties(const View& v);  // type-specific first, then common
  const Property* property(const View& v, std::string_view name);
  const std::vector<Event>& events(const View& v);
  std::string cpp_class(std::string_view type_name);
  Value get(View& v, std::string_view prop);
  void set(View& v, std::string_view prop, const Value& value);
}
```

Only **editable** properties are described: what a designer can change. Bindings, callbacks and
streams are code, and are not part of forms.

### Custom view types

To use your own view in forms and in the designer palette, register a `TypeInfo` at startup, before
loading forms:

```cpp
// Gauge from the custom-view chapter, with a `label_value()` getter added.
void register_gauge() {
  using namespace pulseui::meta;
  TypeInfo t;
  t.name = "Gauge";                          // must equal Gauge::type_name()
  t.category = "Display";
  t.glyph = "◔";
  t.container = false;
  t.create = [] { return std::make_unique<Gauge>(pulseui::Bind<double>(0.5)); };
  t.ctor_code = [](pulseui::View&) { return std::string("Gauge(0.5)"); };

  Property label;
  label.name = "label";
  label.group = "Gauge";
  label.type = PropType::String;
  label.get = [](pulseui::View& v) -> Value { return static_cast<Gauge&>(v).label_value(); };
  label.set = [](pulseui::View& v, const Value& x) { static_cast<Gauge&>(v).label(std::get<std::string>(x)); };
  label.code = [](pulseui::View&, const Value& x) { return ".label(\"" + std::get<std::string>(x) + "\")"; };
  t.props.push_back(label);

  t.events = {{"reset", "on_reset", ""}};      // needs an on_reset(fn) modifier on Gauge

  register_type(std::move(t));
}
```

Rules for a registered type:

- `type_name()` of the view must return exactly `TypeInfo::name`.
- `create` must return a view in its default state; files store only properties that differ from it.
- Every property needs `get` and `set`. `code` is optional; without it, the property is not emitted in
  generated C++.
- To make a property bindable in the designer, set `bind` to a function returning the binding modifier,
  for example `[](const std::string& e) { return ".value(" + e + ")"; }` for a modifier taking
  `Bind<double>`, and describe the expected member in `bind_hint`.
- Events need a modifier on the view that accepts a callable (`on_reset(std::function<void()>)`).
- Common properties (id, size, padding, colors...) are added automatically.

Next: [Testing and automation](15-testing.md).
