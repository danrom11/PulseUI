#include <pulseui/ui/form.hpp>

#include <algorithm>
#include <cctype>
#include <pulseui/ui/meta.hpp>

#include <fstream>
#include <sstream>

namespace pulseui {

namespace {

json::Value value_to_json(const meta::Value& v) {
  if (std::holds_alternative<bool>(v)) return json::Value(std::get<bool>(v));
  if (std::holds_alternative<double>(v)) return json::Value(std::get<double>(v));
  if (std::holds_alternative<std::string>(v)) return json::Value(std::get<std::string>(v));
  if (std::holds_alternative<Color>(v)) return json::Value(std::get<Color>(v).to_hex());
  return {};
}

meta::Value value_from_json(const json::Value& j, meta::PropType type) {
  switch (type) {
    case meta::PropType::Bool: return j.as_bool();
    case meta::PropType::Number:
    case meta::PropType::Enum: return j.as_number();
    case meta::PropType::String: return j.as_string();
    case meta::PropType::Color: return Color::parse(j.as_string()).value_or(Color::transparent());
  }
  return {};
}

} // namespace

json::Value view_to_json(View& v) {
  json::Value j = json::Value::object();
  j["type"] = v.type_name();
  auto def = meta::create(v.type_name());
  json::Value props = json::Value::object();
  for (const meta::Property* p : meta::properties(v)) {
    if (!p->get) continue;
    const meta::Value cur = p->get(v);
    if (def && meta::equal(cur, p->get(*def))) continue;
    props[p->name] = value_to_json(cur);
  }
  if (!props.fields().empty()) j["props"] = std::move(props);
  json::Value bind = json::Value::object(), on = json::Value::object();
  for (auto& [key, value] : v.annotations()) {
    if (key.rfind("bind.", 0) == 0) bind[key.substr(5)] = value;
    else if (key.rfind("on.", 0) == 0) on[key.substr(3)] = value;
  }
  if (!bind.fields().empty()) j["bind"] = std::move(bind);
  if (!on.fields().empty()) j["on"] = std::move(on);
  if (!v.children().empty()) {
    json::Value kids = json::Value::array();
    for (auto& c : v.children()) kids.push_back(view_to_json(*c));
    j["children"] = std::move(kids);
  }
  return j;
}

Node view_from_json(const json::Value& j, std::string* error) {
  const std::string type = j["type"].as_string();
  auto v = meta::create(type);
  if (!v) {
    if (error) *error = "unknown view type '" + type + "'";
    return {};
  }
  // children first, so container props (e.g. spacing) apply to a complete tree
  for (auto& c : j["children"].items()) {
    Node child = view_from_json(c, error);
    if (!child) return {};
    v->add(std::move(child));
  }
  for (auto& [key, val] : j["props"].fields()) {
    if (const meta::Property* p = meta::property(*v, key); p && p->set) p->set(*v, value_from_json(val, p->type));
  }
  for (auto& [key, val] : j["bind"].fields())
    if (val.is_string()) v->set_annotation("bind." + key, val.as_string());
  for (auto& [key, val] : j["on"].fields())
    if (val.is_string()) v->set_annotation("on." + key, val.as_string());
  return Node(std::move(v));
}

json::Value form_to_json(const std::string& title, float width, float height, View& root) {
  json::Value j = json::Value::object();
  j["format"] = "pulseui-form";
  j["version"] = 1;
  j["title"] = title;
  j["width"] = width;
  j["height"] = height;
  j["root"] = view_to_json(root);
  return j;
}

std::optional<Form> form_from_json(const json::Value& j, std::string* error) {
  if (j["format"].as_string() != "pulseui-form") {
    if (error) *error = "not a PulseUI form";
    return std::nullopt;
  }
  Form f;
  f.title = j["title"].as_string("Form");
  f.width = static_cast<float>(j["width"].as_number(640));
  f.height = static_cast<float>(j["height"].as_number(480));
  f.root = view_from_json(j["root"], error);
  if (!f.root) return std::nullopt;
  return f;
}

std::optional<Form> load_form(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open " + path;
    return std::nullopt;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  auto j = json::Value::parse(ss.str(), error);
  if (!j) return std::nullopt;
  return form_from_json(*j, error);
}

bool save_form(const std::string& path, const std::string& title, float width, float height, View& root) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << form_to_json(title, width, height, root).dump(2) << "\n";
  return static_cast<bool>(out);
}

namespace {

const std::vector<std::string>& cpp_keywords() {
  static const std::vector<std::string> k{
      "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const",
      "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else",
      "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
      "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator", "or", "private", "protected", "public",
      "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
      "static_cast", "struct", "switch", "template", "this", "throw", "true", "try", "typedef", "typeid", "typename",
      "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor", "co_await", "co_return", "co_yield",
      "concept", "consteval", "constinit", "char8_t", "char16_t", "char32_t", "wchar_t"};
  return k;
}

void collect_refs(View& v, const std::vector<std::string>& reserved, std::vector<FormRef>& out) {
  const std::string& id = v.id();
  if (is_cpp_identifier(id) && std::find(reserved.begin(), reserved.end(), id) == reserved.end() &&
      std::none_of(out.begin(), out.end(), [&](const FormRef& r) { return r.id == id; }))
    out.push_back({id, meta::cpp_class(v.type_name()), &v});
  for (auto& c : v.children()) collect_refs(*c, reserved, out);
}

struct Gen {
  const CodegenOptions& opt;
  std::vector<FormRef> refs;

  void view(View& v, std::string& out, int depth) {
    const std::string pad(static_cast<std::size_t>(depth * 2), ' ');
    const meta::TypeInfo* t = meta::find(v.type_name());
    if (!t) {
      out += pad + "/* unsupported: " + std::string(v.type_name()) + " */ Spacer()";
      return;
    }
    out += pad + t->ctor_code(v);
    if (t->container) {
      out += "{\n";
      for (std::size_t i = 0; i < v.children().size(); ++i) {
        view(*v.children()[i], out, depth + 1);
        out += ",\n";
      }
      out += pad + "}";
    }
    auto def = meta::create(v.type_name());
    for (const meta::Property* p : meta::properties(v)) {
      if (!p->code || !p->get) continue;
      if (p->bind && v.annotation("bind." + p->name)) continue; // bound: the binding sets it
      const meta::Value cur = p->get(v);
      if (def && meta::equal(cur, p->get(*def))) continue;
      out += p->code(v, cur);
    }
    if (opt.refs && std::any_of(refs.begin(), refs.end(), [&](const FormRef& r) { return r.view == &v; }))
      out += ".ref(" + v.id() + ")";
    // bindings: properties first, then events
    for (const meta::Property* p : meta::properties(v)) {
      const std::string* target = p->bind ? v.annotation("bind." + p->name) : nullptr;
      if (!target) continue;
      if (is_binding_path(*target)) out += p->bind(opt.model + "." + *target);
      else out += " /* invalid binding '" + *target + "' */";
    }
    for (const meta::Event& e : meta::events(v)) {
      const std::string* method = v.annotation("on." + e.name);
      if (!method) continue;
      if (!is_binding_path(*method)) {
        out += " /* invalid handler '" + *method + "' */";
        continue;
      }
      const std::string call = opt.model + "." + *method;
      if (e.arg.empty()) {
        out += "." + e.modifier + "([&" + opt.model + "] { " + call + "(); })";
      } else {
        // the handler may take the value or nothing
        out += "." + e.modifier + "([&" + opt.model + "](" + e.arg + " value) { if constexpr (requires { " + call +
               "(value); }) " + call + "(value); else " + call + "(); })";
      }
    }
  }
};

bool any_binding(View& v) {
  for (auto& [key, value] : v.annotations())
    if (key.rfind("bind.", 0) == 0 || key.rfind("on.", 0) == 0) return true;
  for (auto& c : v.children())
    if (any_binding(*c)) return true;
  return false;
}

} // namespace

bool is_cpp_identifier(std::string_view s) {
  if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
  for (char c : s)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  const auto& k = cpp_keywords();
  return std::find(k.begin(), k.end(), s) == k.end();
}

bool is_binding_path(std::string_view s) {
  if (s.empty()) return false;
  std::size_t start = 0;
  for (;;) {
    const std::size_t dot = s.find('.', start);
    if (!is_cpp_identifier(s.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start))) return false;
    if (dot == std::string_view::npos) return true;
    start = dot + 1;
  }
}

bool form_has_bindings(View& root) { return any_binding(root); }

std::vector<FormRef> form_refs(View& root, const std::vector<std::string>& reserved) {
  std::vector<FormRef> out;
  collect_refs(root, reserved, out);
  return out;
}

std::string generate_cpp_expression(View& root, int indent, const CodegenOptions& options) {
  Gen g{options, options.refs ? form_refs(root, options.reserved) : std::vector<FormRef>{}};
  std::string body;
  g.view(root, body, indent);
  return body;
}

std::string generate_cpp_expression(View& root, int indent) { return generate_cpp_expression(root, indent, CodegenOptions{}); }

std::string generate_cpp(View& root, const std::string& function_name, int indent) {
  const std::string body = generate_cpp_expression(root, indent + 2);
  std::string out;
  out += "// Generated by Pulse Layer.\n";
  out += "#pragma once\n#include <pulseui/pulseui.hpp>\n\n";
  if (form_has_bindings(root)) {
    out += "// Bindings refer to members of `m`: call " + function_name + "(*this) from the class that owns the state.\n";
    out += "template <class Model>\ninline pulseui::Node " + function_name + "(Model& m) {\n";
  } else {
    out += "inline pulseui::Node " + function_name + "() {\n";
  }
  out += "  using namespace pulseui;\n";
  out += "  return\n" + body + ";\n}\n";
  return out;
}

} // namespace pulseui
