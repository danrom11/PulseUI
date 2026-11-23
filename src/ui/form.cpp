#include <pulseui/ui/form.hpp>

#include <algorithm>
#include <cctype>
#include <pulseui/ui/meta.hpp>
#include <pulseui/ui/theme.hpp>

#include <limits>

#include <fstream>
#include <sstream>

namespace pulseui {

// ── ForEachTemplate ──────────────────────────────────────────────────────────

Size ForEachTemplate::row_size(Size avail) {
  float w = 0, h = 0;
  for (std::size_t i = 0; i < children_.size(); ++i) {
    if (!children_[i]->is_visible()) continue;
    const Size s = children_[i]->measure({avail.w, std::numeric_limits<float>::infinity()});
    w = std::max(w, s.w);
    h += s.h + (h > 0 ? 4 : 0);
  }
  if (children_.empty()) return {200, 32};
  return {w, h};
}

Size ForEachTemplate::measure_content(Size avail) {
  const Size r = row_size(avail);
  const float n = static_cast<float>(std::max(1, preview_rows));
  if (horizontal) return {r.w * n + spacing * (n - 1), r.h};
  return {r.w, r.h * n + spacing * (n - 1)};
}

void ForEachTemplate::layout() {
  const Rect c = content_rect();
  const Size r = row_size({c.w, c.h});
  const float row_w = horizontal ? r.w : c.w;
  float y = c.y;
  for (auto& ch : children_) {
    if (!ch->is_visible()) continue;
    const Size s = ch->measure({row_w, std::numeric_limits<float>::infinity()});
    ch->set_frame({c.x, y, row_w, s.h});
    ch->layout();
    y += s.h + 4;
  }
}

void ForEachTemplate::paint_content(Painter& p) {
  if (!children_.empty()) return;
  const Theme& t = th();
  const Rect c = content_rect();
  p.stroke_rect(c.inset(1), t.border, 1, t.radius_small);
  p.text(c, "ForEach: drop a row here", t.font_small, t.text_dim, Align::Center);
}

void ForEachTemplate::paint_overlay(Painter& p) {
  if (children_.empty()) return;
  const Rect c = content_rect();
  const Size r = row_size({c.w, c.h});
  for (int k = 1; k < std::max(1, preview_rows); ++k) {
    p.save();
    if (horizontal) p.translate((r.w + spacing) * static_cast<float>(k), 0);
    else p.translate(0, (r.h + spacing) * static_cast<float>(k));
    p.set_opacity(0.4f);
    for (auto& ch : children_)
      if (ch->is_visible()) ch->paint_tree(p);
    p.restore();
  }
}

// ── FormComponent / CustomView ───────────────────────────────────────────────

std::function<Node(const std::string&)>& FormComponent::resolver() {
  static std::function<Node(const std::string&)> r;
  return r;
}

void FormComponent::set_form(std::string cls) {
  if (cls == form_ && !children_.empty()) return;
  form_ = std::move(cls);
  refresh();
}

void FormComponent::refresh() {
  clear_children();
  if (!form_.empty() && resolver()) {
    Node n = resolver()(form_);
    if (n) add(std::move(n));
  }
  invalidate_layout();
}

Size FormComponent::measure_content(Size avail) {
  if (children_.empty()) return {200, 64};
  return children_.front()->measure(avail);
}

void FormComponent::layout() {
  if (!children_.empty()) {
    children_.front()->set_frame(content_rect());
    children_.front()->layout();
  }
}

void FormComponent::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect c = content_rect();
  if (children_.empty()) {
    p.stroke_rect(c.inset(1), t.accent.with_alpha(0.6f), 1, t.radius_small);
    p.text(c, form_.empty() ? std::string("Component: choose a form") : "Ui::" + form_, t.font_small, t.accent, Align::Center);
  }
}

Size CustomView::measure_content(Size) {
  return {std::max(120.f, measure_text(class_name, th().font, 0).w + 48), 40};
}

void CustomView::paint_content(Painter& p) {
  const Theme& t = th();
  const Rect c = content_rect();
  p.fill_rect(c, t.accent.with_alpha(0.08f), t.radius_small);
  p.stroke_rect(c.inset(0.5f), t.accent.with_alpha(0.55f), 1, t.radius_small);
  p.text({c.x + 10, c.y, 20, c.h}, "\xE2\x97\x87", t.font, t.accent, Align::Center);
  p.text({c.x + 32, c.y, c.w - 40, c.h}, class_name, t.font.with_weight(FontWeight::Medium), t.text, Align::LeftCenter);
}

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
  const meta::TypeInfo* info = meta::find(v.type_name());
  if (!v.children().empty() && (!info || info->container)) { // other views' children are their own parts
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
  const std::string_view type = v.type_name();
  // a list is a class template (ForEach<T>) and its rows repeat: no pointers to them;
  // components and project views have their own classes
  if (type == "ForEach") return;
  if (type != "Component" && type != "Custom" && is_cpp_identifier(id) &&
      std::find(reserved.begin(), reserved.end(), id) == reserved.end() &&
      std::none_of(out.begin(), out.end(), [&](const FormRef& r) { return r.id == id; }))
    out.push_back({id, meta::cpp_class(v.type_name()), &v});
  for (auto& c : v.children()) collect_refs(*c, reserved, out);
}

std::string quote_str(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n') { o += "\\n"; continue; }
    o += c;
  }
  return o + "\"";
}

struct Gen {
  const CodegenOptions& opt;
  std::vector<FormRef> refs;
  std::vector<std::string> scope; // names of list items (and "index") visible here

  bool in_scope(const std::string& path) const {
    const std::string head = path.substr(0, path.find('.'));
    return std::find(scope.begin(), scope.end(), head) != scope.end();
  }
  // a binding path as C++: a member of the model, or a field of a list item
  std::string expr(const std::string& path) const { return in_scope(path) ? path : opt.model + "." + path; }
  std::string captures() const {
    std::string c = "[&";
    for (auto& s : scope) c += ", " + s;
    return c + "]";
  }

  // ForEach(m.items, [&](const auto& item, std::size_t index) -> pulseui::Node { return row; })
  void foreach_view(ForEachTemplate& f, std::string& out, int depth) {
    const std::string pad(static_cast<std::size_t>(depth * 2), ' ');
    const std::string* items = f.annotation("bind.items");
    const std::string src = items && is_binding_path(*items) ? expr(*items) : std::string("std::vector<int>{} /* bind items to a std::vector */");
    const std::string item = is_cpp_identifier(f.item_name) ? f.item_name : std::string("item");
    out += pad + (f.virtualized ? "List(" : "ForEach(") + src + ", ";
    if (!f.key.empty() && !f.virtualized && is_binding_path(f.key))
      out += "[](const auto& " + item + ") { return " + item + "." + f.key + "; }, ";
    out += captures() + "(const auto& " + item + ", std::size_t index) -> pulseui::Node {\n";
    scope.push_back(item);
    scope.push_back("index");
    std::string row;
    if (f.children().size() == 1) {
      view(*f.children()[0], row, depth + 2);
    } else {
      row += std::string(static_cast<std::size_t>((depth + 2) * 2), ' ') + "VStack{\n";
      for (auto& c : f.children()) {
        view(*c, row, depth + 3);
        row += ",\n";
      }
      row += std::string(static_cast<std::size_t>((depth + 2) * 2), ' ') + "}.spacing(4)";
    }
    scope.pop_back();
    scope.pop_back();
    if (f.children().empty()) row = std::string(static_cast<std::size_t>((depth + 2) * 2), ' ') + "Text(\"\")";
    out += std::string(static_cast<std::size_t>((depth + 1) * 2), ' ') + "(void)index;\n";
    out += std::string(static_cast<std::size_t>((depth + 1) * 2), ' ') + "return\n" + row + ";\n" + pad + "})";
    if (f.virtualized) out += ".row_height(" + std::to_string(static_cast<int>(f.row_height)) + ")";
    else {
      if (f.spacing != 4) out += ".spacing(" + std::to_string(static_cast<int>(f.spacing)) + ")";
      if (f.horizontal) out += ".horizontal()";
    }
  }

  void view(View& v, std::string& out, int depth, bool page = false) {
    const std::string pad(static_cast<std::size_t>(depth * 2), ' ');
    const meta::TypeInfo* t = meta::find(v.type_name());
    if (!t) {
      out += pad + "/* unsupported: " + std::string(v.type_name()) + " */ Spacer()";
      return;
    }
    const std::string type = v.type_name();
    if (type == "ForEach") {
      foreach_view(static_cast<ForEachTemplate&>(v), out, depth);
    } else if (type == "Component") {
      // another form: Ui::Form::part(m) or Ui::Form::part(m.member)
      auto& c = static_cast<FormComponent&>(v);
      const std::string* model = v.annotation("bind.model");
      const std::string arg = model && is_binding_path(*model) ? expr(*model) : opt.model;
      // wrapped, so the component's own modifiers (id, size, padding...) apply to it
      out += pad + (c.form().empty() ? std::string("pulseui::VStack{} /* no form chosen */")
                                     : "VStack{Ui::" + c.form() + "::part(" + arg + ")}.spacing(0)");
    } else if (type == "Custom") {
      auto& c = static_cast<CustomView&>(v);
      out += pad + (is_cpp_identifier(c.class_name.substr(c.class_name.rfind(':') == std::string::npos ? 0 : c.class_name.rfind(':') + 1))
                        ? c.class_name : std::string("pulseui::Spacer")) + "(" + c.args + ")";
    } else if (type == "Canvas") {
      // the draw event is the canvas' content: Canvas([&](Painter& p, const Rect& r) { m.draw(p, r); })
      const std::string* method = v.annotation("on.draw");
      if (method && is_binding_path(*method))
        out += pad + "Canvas(" + captures() + "(pulseui::Painter& p, const pulseui::Rect& r) { " + expr(*method) + "(p, r); })";
      else
        out += pad + "Canvas([](pulseui::Painter&, const pulseui::Rect&) {})";
    } else {
      out += pad + t->ctor_code(v);
    }
    if (t->container && type != "ForEach") {
      out += "{\n";
      for (std::size_t i = 0; i < v.children().size(); ++i) {
        if (t->child_title) {
          out += pad + "  Tab(" + quote_str(t->child_title(v, i)) + ",\n";
          view(*v.children()[i], out, depth + 2, true);
          out += "),\n";
        } else {
          view(*v.children()[i], out, depth + 1);
          out += ",\n";
        }
      }
      out += pad + "}";
    }
    auto def = meta::create(v.type_name());
    for (const meta::Property* p : meta::properties(v)) {
      if (!p->code || !p->get) continue;
      if (p->bind && v.annotation("bind." + p->name)) continue; // bound: the binding sets it
      if (page && p->name == "visible") continue;                // Tabs shows one page at a time
      const meta::Value cur = p->get(v);
      if (def && meta::equal(cur, p->get(*def))) continue;
      out += p->code(v, cur);
    }
    if (type == "Custom") out += static_cast<CustomView&>(v).modifiers; // typed in by the user
    if (opt.refs && std::any_of(refs.begin(), refs.end(), [&](const FormRef& r) { return r.view == &v; }))
      out += ".ref(" + v.id() + ")";
    // bindings: properties first, then events
    for (const meta::Property* p : meta::properties(v)) {
      const std::string* target = p->bind ? v.annotation("bind." + p->name) : nullptr;
      if (!target) continue;
      if (type == "ForEach" && p->name == "items") continue; // the source of the list
      if (type == "Component" && p->name == "model") continue; // the argument of part()
      if (!is_binding_path(*target)) {
        out += " /* invalid binding '" + *target + "' */";
        continue;
      }
      // a field of a list item can only be shown, not written back
      if (in_scope(*target) && p->bind_once) out += p->bind_once(expr(*target));
      else out += p->bind(expr(*target));
    }
    for (const meta::Event& e : meta::events(v)) {
      if (e.modifier.empty()) continue; // handled by the constructor (Canvas draw)
      const std::string* method = v.annotation("on." + e.name);
      if (!method) continue;
      if (!is_binding_path(*method)) {
        out += " /* invalid handler '" + *method + "' */";
        continue;
      }
      const std::string call = expr(*method);
      // inside a list row the handler may also take the item or its index
      std::vector<std::string> tries;
      if (!e.arg.empty()) {
        for (auto& s : scope)
          if (s != "index") tries.push_back(s + ", value");
        tries.push_back("value");
      }
      for (auto it = scope.rbegin(); it != scope.rend(); ++it)
        if (*it != "index") tries.push_back(*it); // the item first, then its index
      if (!scope.empty()) tries.push_back("index");
      const std::string cap = scope.empty() ? "[&" + opt.model + "]" : captures();
      if (tries.empty()) {
        out += "." + e.modifier + "(" + cap + " { " + call + "(); })";
        continue;
      }
      // pulseui::form_call([&](auto&&... a) -> decltype(m.f(a...)) { return m.f(a...); },
      //                    std::tie(item, value), std::tie(value), ..., std::tie())
      std::string body = "pulseui::form_call([&](auto&&... a) -> decltype(" + call + "(a...)) { return " + call + "(a...); }";
      for (auto& args : tries) body += ", std::tie(" + args + ")";
      body += ", std::tie());";
      out += "." + e.modifier + "(" + cap + "(" + (e.arg.empty() ? std::string() : e.arg + " value") + ") { " + body + " })";
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
  Gen g{options, options.refs ? form_refs(root, options.reserved) : std::vector<FormRef>{}, {}};
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
