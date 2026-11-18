#include <pulseui/ui/meta.hpp>
#include <pulseui/ui/widgets.hpp>

#include <cmath>
#include <cstdio>
#include <sstream>

namespace pulseui::meta {

namespace {

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') { out += "\\n"; continue; }
    out += c;
  }
  return out + "\"";
}

std::string num(double d) {
  char buf[32];
  if (std::floor(d) == d) std::snprintf(buf, sizeof buf, "%.0f", d);
  else std::snprintf(buf, sizeof buf, "%g", d);
  return buf;
}

double as_num(const Value& v) { return std::holds_alternative<double>(v) ? std::get<double>(v) : 0.0; }
bool as_bool(const Value& v) { return std::holds_alternative<bool>(v) && std::get<bool>(v); }
std::string as_str(const Value& v) { return std::holds_alternative<std::string>(v) ? std::get<std::string>(v) : ""; }
Color as_color(const Value& v) { return std::holds_alternative<Color>(v) ? std::get<Color>(v) : Color::transparent(); }

std::string color_code(Color c) {
  char buf[64];
  if (c.a < 1) std::snprintf(buf, sizeof buf, "Color::hex(0x%s, %.2ff)", c.to_hex().substr(1, 6).c_str(), c.a);
  else std::snprintf(buf, sizeof buf, "Color::hex(0x%s)", c.to_hex().substr(1).c_str());
  return buf;
}

std::vector<std::string> split_csv(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto b = item.find_first_not_of(' ');
    const auto e = item.find_last_not_of(' ');
    out.push_back(b == std::string::npos ? std::string() : item.substr(b, e - b + 1));
  }
  return out;
}

std::string join_csv(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
  return out;
}

// ── property builders ──
template <class V>
Property number(std::string name, std::string group, double (*get)(V&), void (*set)(V&, double),
                std::string modifier = {}, double mn = -1e6, double mx = 1e6) {
  Property p;
  p.name = std::move(name);
  p.group = std::move(group);
  p.type = PropType::Number;
  p.min = mn;
  p.max = mx;
  p.get = [get](View& v) -> Value { return get(static_cast<V&>(v)); };
  p.set = [set](View& v, const Value& x) { set(static_cast<V&>(v), as_num(x)); };
  if (!modifier.empty()) p.code = [modifier](View&, const Value& x) { return "." + modifier + "(" + num(as_num(x)) + ")"; };
  return p;
}

template <class V>
Property boolean(std::string name, std::string group, bool (*get)(V&), void (*set)(V&, bool), std::string modifier = {}) {
  Property p;
  p.name = std::move(name);
  p.group = std::move(group);
  p.type = PropType::Bool;
  p.get = [get](View& v) -> Value { return get(static_cast<V&>(v)); };
  p.set = [set](View& v, const Value& x) { set(static_cast<V&>(v), as_bool(x)); };
  if (!modifier.empty()) p.code = [modifier](View&, const Value& x) { return "." + modifier + "(" + (as_bool(x) ? "true" : "false") + ")"; };
  return p;
}

template <class V>
Property string(std::string name, std::string group, std::string (*get)(V&), void (*set)(V&, const std::string&),
                std::string modifier = {}) {
  Property p;
  p.name = std::move(name);
  p.group = std::move(group);
  p.type = PropType::String;
  p.get = [get](View& v) -> Value { return get(static_cast<V&>(v)); };
  p.set = [set](View& v, const Value& x) { set(static_cast<V&>(v), as_str(x)); };
  if (!modifier.empty()) p.code = [modifier](View&, const Value& x) { return "." + modifier + "(" + quote(as_str(x)) + ")"; };
  return p;
}

template <class V>
Property color(std::string name, std::string group, Color (*get)(V&), void (*set)(V&, Color), std::string modifier = {}) {
  Property p;
  p.name = std::move(name);
  p.group = std::move(group);
  p.type = PropType::Color;
  p.get = [get](View& v) -> Value { return get(static_cast<V&>(v)); };
  p.set = [set](View& v, const Value& x) { set(static_cast<V&>(v), as_color(x)); };
  if (!modifier.empty()) p.code = [modifier](View&, const Value& x) { return "." + modifier + "(" + color_code(as_color(x)) + ")"; };
  return p;
}

template <class V>
Property enumeration(std::string name, std::string group, std::vector<std::string> options, int (*get)(V&),
                     void (*set)(V&, int), std::function<std::string(int)> code = {}) {
  Property p;
  p.name = std::move(name);
  p.group = std::move(group);
  p.type = PropType::Enum;
  p.options = std::move(options);
  p.get = [get](View& v) -> Value { return static_cast<double>(get(static_cast<V&>(v))); };
  p.set = [set](View& v, const Value& x) { set(static_cast<V&>(v), static_cast<int>(as_num(x))); };
  if (code) p.code = [code](View&, const Value& x) { return code(static_cast<int>(as_num(x))); };
  return p;
}

std::vector<Property>& common_props() {
  static std::vector<Property> props = [] {
    std::vector<Property> c;
    c.push_back(string<View>("id", "Identity", [](View& v) { return v.id(); },
                             [](View& v, const std::string& s) { v.set_id(s); }, "id"));
    auto auto_get = [](float f) { return is_auto(f) ? 0.0 : static_cast<double>(f); };
    (void)auto_get;
    c.push_back(number<View>("x", "Position", [](View& v) { return static_cast<double>(v.style().position.x); },
                             [](View& v, double d) { v.style().positioned = true; v.style().position.x = static_cast<float>(d); v.invalidate_layout(); }));
    c.back().code = [](View& v, const Value&) {
      return v.style().positioned ? ".position(" + num(v.style().position.x) + ", " + num(v.style().position.y) + ")" : std::string();
    };
    c.push_back(number<View>("y", "Position", [](View& v) { return static_cast<double>(v.style().position.y); },
                             [](View& v, double d) { v.style().positioned = true; v.style().position.y = static_cast<float>(d); v.invalidate_layout(); }));
    c.back().code = [](View& v, const Value& x) {
      // emitted together with x unless x is at its default
      return v.style().positioned && v.style().position.x == 0 ? ".position(0, " + num(as_num(x)) + ")" : std::string();
    };
    c.push_back(number<View>("width", "Size", [](View& v) { return is_auto(v.style().width) ? 0.0 : v.style().width; },
                             [](View& v, double d) { v.style().width = d > 0 ? static_cast<float>(d) : kAuto; v.invalidate_layout(); }, "width", 0, 10000));
    c.push_back(number<View>("height", "Size", [](View& v) { return is_auto(v.style().height) ? 0.0 : v.style().height; },
                             [](View& v, double d) { v.style().height = d > 0 ? static_cast<float>(d) : kAuto; v.invalidate_layout(); }, "height", 0, 10000));
    c.push_back(number<View>("min_width", "Size", [](View& v) { return static_cast<double>(v.style().min_width); },
                             [](View& v, double d) { v.style().min_width = static_cast<float>(d); v.invalidate_layout(); }, "min_width", 0, 10000));
    c.push_back(number<View>("min_height", "Size", [](View& v) { return static_cast<double>(v.style().min_height); },
                             [](View& v, double d) { v.style().min_height = static_cast<float>(d); v.invalidate_layout(); }, "min_height", 0, 10000));
    c.push_back(number<View>("flex", "Size", [](View& v) { return static_cast<double>(v.style().flex); },
                             [](View& v, double d) { v.style().flex = static_cast<float>(d); v.invalidate_layout(); }, "flex", 0, 100));
    c.push_back(number<View>("padding", "Size", [](View& v) { return static_cast<double>(v.style().padding.left); },
                             [](View& v, double d) { v.style().padding = Insets(static_cast<float>(d)); v.invalidate_layout(); }, "padding", 0, 500));
    c.push_back(enumeration<View>("align", "Size", {"Auto", "Start", "Center", "End", "Stretch"},
                                  [](View& v) { return static_cast<int>(v.style().align); },
                                  [](View& v, int i) { v.style().align = static_cast<Alignment>(i); v.invalidate_layout(); },
                                  [](int i) {
                                    static const char* n[] = {"Auto", "Start", "Center", "End", "Stretch"};
                                    return std::string(".align(Alignment::") + n[i] + ")";
                                  }));
    c.push_back(color<View>("background", "Appearance", [](View& v) { return v.style().background; },
                            [](View& v, Color col) { v.style().background = col; v.invalidate(); }, "background"));
    c.push_back(number<View>("radius", "Appearance", [](View& v) { return static_cast<double>(v.style().radius); },
                             [](View& v, double d) { v.style().radius = static_cast<float>(d); v.invalidate(); }, "radius", 0, 200));
    c.push_back(boolean<View>("visible", "Behavior", [](View& v) { return v.is_visible(); },
                              [](View& v, bool b) { v.set_visible(b); }, "visible"));
    c.push_back(boolean<View>("enabled", "Behavior", [](View& v) { return v.enabled_self(); },
                              [](View& v, bool b) { v.set_enabled(b); }, "enabled"));
    c.push_back(string<View>("tooltip", "Behavior", [](View& v) { return v.tooltip(); },
                             [](View& v, const std::string& s) { v.set_tooltip(s); }, "tooltip"));
    return c;
  }();
  return props;
}

template <class S>
void add_stack_props(TypeInfo& t) {
  t.props.push_back(number<S>("spacing", "Layout", [](S& v) { return static_cast<double>(v.spacing_value()); },
                              [](S& v, double d) { v.set_spacing(static_cast<float>(d)); }, "spacing", 0, 500));
  t.props.push_back(enumeration<S>("align_items", "Layout", {"Start", "Center", "End", "Stretch"},
                                   [](S& v) { return static_cast<int>(v.cross_alignment()) - 1; },
                                   [](S& v, int i) { v.set_align_items(static_cast<Alignment>(i + 1)); },
                                   [](int i) {
                                     static const char* n[] = {"Start", "Center", "End", "Stretch"};
                                     return std::string(".align_items(Alignment::") + n[i] + ")";
                                   }));
  t.props.push_back(enumeration<S>("justify", "Layout", {"Start", "Center", "End", "SpaceBetween"},
                                   [](S& v) { return static_cast<int>(v.justification()); },
                                   [](S& v, int i) { v.set_justify(static_cast<Justify>(i)); },
                                   [](int i) {
                                     static const char* n[] = {"Start", "Center", "End", "SpaceBetween"};
                                     return std::string(".justify(Justify::") + n[i] + ")";
                                   }));
}

int align_index(int a) { return (a & Align::HCenter) ? 1 : (a & Align::Right) ? 2 : 0; }

std::vector<TypeInfo>& registry() {
  static std::vector<TypeInfo> reg = [] {
    std::vector<TypeInfo> r;

    // ── layout ──
    {
      TypeInfo t{"VStack", "Layout", "⇕", true, [] { return std::make_unique<VStack>(); }, [](View&) { return std::string("VStack"); }, {}};
      add_stack_props<VStack>(t);
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"HStack", "Layout", "⇔", true, [] { return std::make_unique<HStack>(); }, [](View&) { return std::string("HStack"); }, {}};
      add_stack_props<HStack>(t);
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Panel", "Layout", "▢", true, [] { return std::make_unique<Panel>(); }, [](View&) { return std::string("Panel"); }, {}};
      add_stack_props<Panel>(t);
      r.push_back(std::move(t));
    }
    r.push_back({"Layer", "Layout", "◫", true, [] { return std::make_unique<Layer>(); }, [](View&) { return std::string("Layer"); }, {}});
    r.push_back({"Scroll", "Layout", "↕", true, [] { return std::make_unique<Scroll>(); }, [](View&) { return std::string("Scroll"); }, {}});
    r.push_back({"Spacer", "Layout", "↔", false, [] { return std::make_unique<Spacer>(); }, [](View&) { return std::string("Spacer()"); }, {}});
    {
      TypeInfo t{"Divider", "Layout", "—", false, [] { return std::make_unique<Divider>(); },
                 [](View& v) {
                   return static_cast<Divider&>(v).orientation() == Orientation::Vertical ? std::string("Divider(Orientation::Vertical)")
                                                                                          : std::string("Divider()");
                 }, {}};
      t.props.push_back(enumeration<Divider>("orientation", "Divider", {"Horizontal", "Vertical"},
                                             [](Divider& d) { return static_cast<int>(d.orientation()); },
                                             [](Divider& d, int i) { d.set_orientation(static_cast<Orientation>(i)); }));
      r.push_back(std::move(t));
    }

    // ── display ──
    {
      TypeInfo t{"Text", "Display", "T", false, [] { return std::make_unique<Text>("Text"); },
                 [](View& v) { return "Text(" + quote(static_cast<Text&>(v).text()) + ")"; }, {}};
      t.props.push_back(string<Text>("text", "Text", [](Text& x) { return x.text(); }, [](Text& x, const std::string& s) { x.set_text(s); }));
      t.props.push_back(number<Text>("font_size", "Text", [](Text& x) { return static_cast<double>(x.font_size_value()); },
                                     [](Text& x, double d) { x.set_font_size(static_cast<float>(d)); }, "font_size", 0, 200));
      t.props.push_back(enumeration<Text>("weight", "Text", {"Light", "Normal", "Medium", "SemiBold", "Bold"},
                                          [](Text& x) { return (static_cast<int>(x.weight_value()) - 300) / 100; },
                                          [](Text& x, int i) { x.set_weight(static_cast<FontWeight>(300 + i * 100)); },
                                          [](int i) {
                                            static const char* n[] = {"Light", "Normal", "Medium", "SemiBold", "Bold"};
                                            return std::string(".weight(FontWeight::") + n[i] + ")";
                                          }));
      t.props.push_back(enumeration<Text>("text_align", "Text", {"Left", "Center", "Right"},
                                          [](Text& x) { return align_index(x.text_align_value()); },
                                          [](Text& x, int i) { x.set_text_align(i == 1 ? Align::Center : i == 2 ? Align::RightCenter : Align::LeftCenter); },
                                          [](int i) {
                                            static const char* n[] = {"Align::LeftCenter", "Align::Center", "Align::RightCenter"};
                                            return std::string(".text_align(") + n[i] + ")";
                                          }));
      t.props.push_back(boolean<Text>("wrap", "Text", [](Text& x) { return x.wrap_value(); }, [](Text& x, bool b) { x.set_wrap(b); }, "wrap"));
      t.props.push_back(color<Text>("color", "Text", [](Text& x) { return x.color_value().value_or(Color::transparent()); },
                                    [](Text& x, Color c) { x.set_color(c.a > 0 ? std::optional<Color>(c) : std::nullopt); }, "color"));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Image", "Display", "▣", false, [] { return std::make_unique<ImageView>(); },
                 [](View& v) { return "ImageView(std::string(" + quote(static_cast<ImageView&>(v).source()) + "))"; }, {}};
      t.props.push_back(string<ImageView>("source", "Image", [](ImageView& x) { return x.source(); },
                                          [](ImageView& x, const std::string& s) { x.set_source(s); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"ProgressBar", "Display", "▬", false, [] { return std::make_unique<ProgressBar>(Bind<double>(0.4)); },
                 [](View& v) { return "ProgressBar(" + num(static_cast<ProgressBar&>(v).value()) + ")"; }, {}};
      t.props.push_back(number<ProgressBar>("value", "Value", [](ProgressBar& x) { return x.value(); },
                                            [](ProgressBar& x, double d) { x.set_value(d); }, {}, 0, 1));
      r.push_back(std::move(t));
    }

    // ── controls ──
    {
      TypeInfo t{"Button", "Controls", "▭", false, [] { return std::make_unique<Button>("Button"); },
                 [](View& v) { return "Button(" + quote(static_cast<Button&>(v).label()) + ")"; }, {}};
      t.props.push_back(string<Button>("label", "Button", [](Button& x) { return x.label(); }, [](Button& x, const std::string& s) { x.set_label(s); }));
      t.props.push_back(enumeration<Button>("kind", "Button", {"Normal", "Primary", "Flat", "Danger"},
                                            [](Button& x) { return static_cast<int>(x.kind_value()); },
                                            [](Button& x, int i) { x.set_kind(static_cast<ButtonKind>(i)); },
                                            [](int i) {
                                              static const char* n[] = {"", ".primary()", ".flat()", ".danger()"};
                                              return std::string(n[i]);
                                            }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"TextField", "Controls", "⌨", false, [] { return std::make_unique<TextField>(); },
                 [](View&) { return std::string("TextField()"); }, {}};
      t.props.push_back(string<TextField>("placeholder", "TextField", [](TextField& x) { return x.placeholder_value(); },
                                          [](TextField& x, const std::string& s) { x.set_placeholder(s); }, "placeholder"));
      t.props.push_back(string<TextField>("text", "TextField", [](TextField& x) { return x.text(); },
                                          [](TextField& x, const std::string& s) { x.set_text(s); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Checkbox", "Controls", "☑", false, [] { return std::make_unique<Checkbox>("Checkbox"); },
                 [](View& v) {
                   auto& c = static_cast<Checkbox&>(v);
                   return "Checkbox(" + quote(c.label()) + (c.checked() ? ", true)" : ")");
                 }, {}};
      t.props.push_back(string<Checkbox>("label", "Checkbox", [](Checkbox& x) { return x.label(); }, [](Checkbox& x, const std::string& s) { x.set_label(s); }));
      t.props.push_back(boolean<Checkbox>("checked", "Checkbox", [](Checkbox& x) { return x.checked(); }, [](Checkbox& x, bool b) { x.set_checked(b, false); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Toggle", "Controls", "◐", false, [] { return std::make_unique<Toggle>("Toggle"); },
                 [](View& v) {
                   auto& c = static_cast<Toggle&>(v);
                   return "Toggle(" + quote(c.label()) + (c.checked() ? ", true)" : ")");
                 }, {}};
      t.props.push_back(string<Toggle>("label", "Toggle", [](Toggle& x) { return x.label(); }, [](Toggle& x, const std::string& s) { x.set_label(s); }));
      t.props.push_back(boolean<Toggle>("checked", "Toggle", [](Toggle& x) { return x.checked(); }, [](Toggle& x, bool b) { x.set_checked(b, false); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Slider", "Controls", "⊸", false, [] { return std::make_unique<Slider>(Bind<double>(0.5)); },
                 [](View& v) {
                   auto& s = static_cast<Slider&>(v);
                   return "Slider(" + num(s.value()) + ", " + num(s.min()) + ", " + num(s.max()) + ")";
                 }, {}};
      t.props.push_back(number<Slider>("value", "Slider", [](Slider& x) { return x.value(); }, [](Slider& x, double d) { x.set_value(d, false); }));
      t.props.push_back(number<Slider>("min", "Slider", [](Slider& x) { return x.min(); }, [](Slider& x, double d) { x.set_range(d, x.max()); }));
      t.props.push_back(number<Slider>("max", "Slider", [](Slider& x) { return x.max(); }, [](Slider& x, double d) { x.set_range(x.min(), d); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Select", "Controls", "▾", false,
                 [] { return std::make_unique<Select>(Bind<std::vector<std::string>>(std::vector<std::string>{"One", "Two", "Three"})); },
                 [](View& v) {
                   auto& s = static_cast<Select&>(v);
                   std::string o = "Select({";
                   for (std::size_t i = 0; i < s.options().size(); ++i) o += (i ? ", " : "") + quote(s.options()[i]);
                   return o + "}, " + std::to_string(s.index()) + ")";
                 }, {}};
      t.props.push_back(string<Select>("options", "Select", [](Select& x) { return join_csv(x.options()); },
                                       [](Select& x, const std::string& s) { x.set_options(split_csv(s)); }));
      t.props.push_back(number<Select>("index", "Select", [](Select& x) { return static_cast<double>(x.index()); },
                                       [](Select& x, double d) { x.set_index(static_cast<int>(d), false); }, {}, 0, 1000));
      r.push_back(std::move(t));
    }
    return r;
  }();
  return reg;
}

} // namespace

const std::vector<TypeInfo>& types() { return registry(); }

const TypeInfo* find(std::string_view name) {
  for (auto& t : registry())
    if (t.name == name) return &t;
  return nullptr;
}

void register_type(TypeInfo info) {
  for (auto& t : registry())
    if (t.name == info.name) {
      t = std::move(info);
      return;
    }
  registry().push_back(std::move(info));
}

std::unique_ptr<View> create(std::string_view name) {
  const TypeInfo* t = find(name);
  return t && t->create ? t->create() : nullptr;
}

std::vector<const Property*> properties(const View& v) {
  // type-specific properties first: they are what you edit most
  std::vector<const Property*> out;
  if (const TypeInfo* t = find(v.type_name()))
    for (auto& p : t->props) out.push_back(&p);
  for (auto& p : common_props()) out.push_back(&p);
  return out;
}

const Property* property(const View& v, std::string_view name) {
  for (auto* p : properties(v))
    if (p->name == name) return p;
  return nullptr;
}

Value get(View& v, std::string_view prop) {
  const Property* p = property(v, prop);
  return p && p->get ? p->get(v) : Value{};
}

void set(View& v, std::string_view prop, const Value& value) {
  if (const Property* p = property(v, prop); p && p->set) p->set(v, value);
}

bool equal(const Value& a, const Value& b) {
  if (a.index() != b.index()) return false;
  if (std::holds_alternative<double>(a)) return std::abs(std::get<double>(a) - std::get<double>(b)) < 1e-9;
  return a == b;
}

std::string to_string(const Value& v) {
  if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
  if (std::holds_alternative<double>(v)) return num(std::get<double>(v));
  if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
  if (std::holds_alternative<Color>(v)) return std::get<Color>(v).to_hex();
  return {};
}

} // namespace pulseui::meta
