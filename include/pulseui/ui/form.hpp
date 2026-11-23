#pragma once
// Forms: view trees saved as JSON (.pform), produced by the Pulse Layer designer.
//
//   auto form = pulseui::load_form("login.pform");
//   Window win(form->title, form->width, form->height);
//   win.content(std::move(form->root));
//   win.find<TextField>("user")->model(user_name);   // bind state by id
//   win.find<Button>("ok")->clicked.connect([&]{ login(); });
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <pulseui/core/json.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui {

// The designer's stand-in for a list bound to a std::vector: its children are
// the template of one row, shown with a few dimmed copies below. Generated code
// turns it into
//   ForEach(m.todos, [&](const auto& item, std::size_t index) -> pulseui::Node { return <row>; })
// (or a virtualized List). Inside the row, bindings starting with the item name
// ("item.title") refer to the row's element; events may take the item or index.
class ForEachTemplate : public ViewT<ForEachTemplate> {
public:
  ForEachTemplate() = default;
  const char* type_name() const override { return "ForEach"; }

  int preview_rows = 3;          // copies shown in the designer
  float spacing = 4;
  bool horizontal = false;
  std::string item_name = "item"; // the row's element in bindings
  std::string key;               // member used as the key (keyed ForEach), "" = none
  bool virtualized = false;      // a List (only visible rows exist) instead of ForEach
  float row_height = 32;         // for virtualized lists

  Size row_size(Size avail);
  Size measure_content(Size avail) override;
  void layout() override;
  void paint_content(Painter& p) override;
  void paint_overlay(Painter& p) override;
};

// Another form used as a part of this one (a reusable piece of UI designed
// separately). Generated code: Ui::<Form>::part(m) - its bindings refer to the
// members of `m`, or of `m.<model>` when a model member is bound.
// The designer shows the referenced form through `resolver`.
class FormComponent : public ViewT<FormComponent> {
public:
  FormComponent() = default;
  const char* type_name() const override { return "Component"; }
  const std::string& form() const { return form_; }
  void set_form(std::string cls);
  // Builds a preview of a form by class name (set by the designer); null = a placeholder.
  static std::function<Node(const std::string& form_class)>& resolver();
  void refresh(); // rebuild the preview (the referenced form changed)
  Size measure_content(Size avail) override;
  void layout() override;
  void paint_content(Painter& p) override;
  bool hit_test_children(Point) const override { return false; } // one piece in the designer

private:
  std::string form_;
};

// A view class of the project (class Rating : public pulseui::ViewT<Rating> {...}).
// The designer can't run project code, so it shows a placeholder; generated code
// is ClassName(args) followed by the modifiers typed in.
class CustomView : public ViewT<CustomView> {
public:
  CustomView() { style_.min_height = 32; }
  const char* type_name() const override { return "Custom"; }
  std::string class_name = "MyView";
  std::string header;     // "rating.hpp" - included by the generated code
  std::string args;       // constructor arguments, C++
  std::string modifiers;  // ".value(m.rating).stars(5)", C++
  Size measure_content(Size avail) override;
  void paint_content(Painter& p) override;
};

struct Form {
  std::string title = "Form";
  float width = 640, height = 480;
  Node root;
};

json::Value view_to_json(View& v);
Node view_from_json(const json::Value& j, std::string* error = nullptr);

json::Value form_to_json(const std::string& title, float width, float height, View& root);
std::optional<Form> form_from_json(const json::Value& j, std::string* error = nullptr);

std::optional<Form> load_form(const std::string& path, std::string* error = nullptr);
bool save_form(const std::string& path, const std::string& title, float width, float height, View& root);

// ── bindings ─────────────────────────────────────────────────────────────────
// A form can bind view properties to members of a "model" object and connect
// events to its methods. They are stored as view annotations
//   "bind.<property>" -> "member"      e.g. "bind.text"  -> "email"
//   "on.<event>"      -> "method"      e.g. "on.click"   -> "submit"
// and in JSON as "bind": {...} and "on": {...}. Generated code turns them into
// ordinary modifiers: TextField().model(m.email), Button("OK").on_click([&m] { m.submit(); }).
bool is_cpp_identifier(std::string_view s);
bool is_binding_path(std::string_view s); // "name" or "settings.dark"
bool form_has_bindings(View& root);

// Views whose id can be a C++ member name (first occurrence of each id, not in `reserved`).
struct FormRef {
  std::string id;
  std::string cpp_class; // "Button", "ImageView"
  View* view = nullptr;
};
std::vector<FormRef> form_refs(View& root, const std::vector<std::string>& reserved = {});

struct CodegenOptions {
  std::string model = "m";             // name of the model object used by bindings
  bool refs = false;                   // emit .ref(<id>) for the views listed by form_refs()
  std::vector<std::string> reserved;   // ids that must not become members
};

// Declarative C++ for a view tree (what the designer exports). With bindings the
// function becomes `template <class Model> Node name(Model& m)`.
std::string generate_cpp(View& root, const std::string& function_name = "make_form", int indent = 0);
// Only the declarative expression ("VStack{ ... }.padding(16)"), indented by `indent` levels.
std::string generate_cpp_expression(View& root, int indent = 0);
std::string generate_cpp_expression(View& root, int indent, const CodegenOptions& options);

// Used by generated code: a handler in a list row may take (item, value),
// (value), (item), (index) or nothing. form_call calls `f` with the first
// argument pack it accepts. (A plain function template instead of
// `if constexpr (requires {...})` in the row lambda: Apple clang 15 evaluates
// such requires-expressions on captured variables wrongly.)
namespace detail {
template <class F, class Tuple>
struct form_invocable : std::false_type {};
template <class F, class... A>
struct form_invocable<F, std::tuple<A...>> : std::is_invocable<F&, A...> {};
template <class>
inline constexpr bool form_no_handler = false;
} // namespace detail

template <class F, class Tuple, class... Rest>
void form_call(F&& f, Tuple&& args, Rest&&... rest) {
  if constexpr (detail::form_invocable<F, std::decay_t<Tuple>>::value) std::apply(f, std::forward<Tuple>(args));
  else if constexpr (sizeof...(Rest) > 0) form_call(std::forward<F>(f), std::forward<Rest>(rest)...);
  else static_assert(detail::form_no_handler<F>, "the model has no handler method with a matching signature");
}

} // namespace pulseui
