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

// Declarative C++ for a view tree (what the designer exports).
std::string generate_cpp(View& root, const std::string& function_name = "make_form", int indent = 0);
// Only the declarative expression ("VStack{ ... }.padding(16)"), indented by `indent` levels.
std::string generate_cpp_expression(View& root, int indent = 0);

} // namespace pulseui
