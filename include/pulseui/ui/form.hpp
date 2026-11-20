#pragma once
// Forms: view trees saved as JSON (.pform), produced by the Pulse Layer designer.
//
//   auto form = pulseui::load_form("login.pform");
//   Window win(form->title, form->width, form->height);
//   win.content(std::move(form->root));
//   win.find<TextField>("user")->model(user_name);   // bind state by id
//   win.find<Button>("ok")->clicked.connect([&]{ login(); });
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <pulseui/core/json.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui {

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

} // namespace pulseui
