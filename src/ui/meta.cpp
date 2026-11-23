#include <pulseui/ui/form.hpp>
#include <pulseui/ui/inputs.hpp>
#include <pulseui/ui/meta.hpp>
#include <pulseui/ui/theme.hpp>
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

// ".model(" + expr + ")"
std::function<std::string(const std::string&)> binder(std::string modifier) {
  return [modifier](const std::string& expr) { return "." + modifier + "(" + expr + ")"; };
}

void make_bindable(std::vector<Property>& props, const std::string& name, const std::string& modifier, std::string hint) {
  for (auto& p : props)
    if (p.name == name) {
      p.bind = binder(modifier);
      p.bind_hint = std::move(hint);
    }
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
    make_bindable(c, "background", "background", "Color");
    make_bindable(c, "visible", "visible", "bool");
    make_bindable(c, "enabled", "enabled", "bool");
    make_bindable(c, "tooltip", "tooltip", "std::string");
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
      TypeInfo t{"VStack", "Layout", "⇕", true, [] { return std::make_unique<VStack>(); }, [](View&) { return std::string("VStack"); }, {}, {}, {}, {}, {}};
      add_stack_props<VStack>(t);
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"HStack", "Layout", "⇔", true, [] { return std::make_unique<HStack>(); }, [](View&) { return std::string("HStack"); }, {}, {}, {}, {}, {}};
      add_stack_props<HStack>(t);
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Panel", "Layout", "▢", true, [] { return std::make_unique<Panel>(); }, [](View&) { return std::string("Panel"); }, {}, {}, {}, {}, {}};
      add_stack_props<Panel>(t);
      r.push_back(std::move(t));
    }
    r.push_back({"Layer", "Layout", "◫", true, [] { return std::make_unique<Layer>(); }, [](View&) { return std::string("Layer"); }, {}, {}, {}, {}, {}});
    r.push_back({"Scroll", "Layout", "↕", true, [] { return std::make_unique<Scroll>(); }, [](View&) { return std::string("Scroll"); }, {}, {}, {}, {}, {}});
    r.push_back({"Spacer", "Layout", "↔", false, [] { return std::make_unique<Spacer>(); }, [](View&) { return std::string("Spacer()"); }, {}, {}, {}, {}, {}});
    {
      TypeInfo t{"Divider", "Layout", "—", false, [] { return std::make_unique<Divider>(); },
                 [](View& v) {
                   return static_cast<Divider&>(v).orientation() == Orientation::Vertical ? std::string("Divider(Orientation::Vertical)")
                                                                                          : std::string("Divider()");
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(enumeration<Divider>("orientation", "Divider", {"Horizontal", "Vertical"},
                                             [](Divider& d) { return static_cast<int>(d.orientation()); },
                                             [](Divider& d, int i) { d.set_orientation(static_cast<Orientation>(i)); }));
      r.push_back(std::move(t));
    }

    // ── display ──
    {
      TypeInfo t{"Text", "Display", "T", false, [] { return std::make_unique<Text>("Text"); },
                 [](View& v) { return "Text(" + quote(static_cast<Text&>(v).text()) + ")"; }, {}, {}, {}, {}, {}};
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
                 [](View& v) { return "ImageView(std::string(" + quote(static_cast<ImageView&>(v).source()) + "))"; }, {}, {}, {}, {}, {}};
      t.props.push_back(string<ImageView>("source", "Image", [](ImageView& x) { return x.source(); },
                                          [](ImageView& x, const std::string& s) { x.set_source(s); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"ProgressBar", "Display", "▬", false, [] { return std::make_unique<ProgressBar>(Bind<double>(0.4)); },
                 [](View& v) { return "ProgressBar(" + num(static_cast<ProgressBar&>(v).value()) + ")"; }, {}, {}, {}, {}, {}};
      t.props.push_back(number<ProgressBar>("value", "Value", [](ProgressBar& x) { return x.value(); },
                                            [](ProgressBar& x, double d) { x.set_value(d); }, {}, 0, 1));
      r.push_back(std::move(t));
    }

    // ── controls ──
    {
      TypeInfo t{"Button", "Controls", "▭", false, [] { return std::make_unique<Button>("Button"); },
                 [](View& v) { return "Button(" + quote(static_cast<Button&>(v).label()) + ")"; }, {}, {}, {}, {}, {}};
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
                 [](View&) { return std::string("TextField()"); }, {}, {}, {}, {}, {}};
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
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Checkbox>("label", "Checkbox", [](Checkbox& x) { return x.label(); }, [](Checkbox& x, const std::string& s) { x.set_label(s); }));
      t.props.push_back(boolean<Checkbox>("checked", "Checkbox", [](Checkbox& x) { return x.checked(); }, [](Checkbox& x, bool b) { x.set_checked(b, false); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Toggle", "Controls", "◐", false, [] { return std::make_unique<Toggle>("Toggle"); },
                 [](View& v) {
                   auto& c = static_cast<Toggle&>(v);
                   return "Toggle(" + quote(c.label()) + (c.checked() ? ", true)" : ")");
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Toggle>("label", "Toggle", [](Toggle& x) { return x.label(); }, [](Toggle& x, const std::string& s) { x.set_label(s); }));
      t.props.push_back(boolean<Toggle>("checked", "Toggle", [](Toggle& x) { return x.checked(); }, [](Toggle& x, bool b) { x.set_checked(b, false); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Slider", "Controls", "⊸", false, [] { return std::make_unique<Slider>(Bind<double>(0.5)); },
                 [](View& v) {
                   auto& s = static_cast<Slider&>(v);
                   return "Slider(" + num(s.value()) + ", " + num(s.min()) + ", " + num(s.max()) + ")";
                 }, {}, {}, {}, {}, {}};
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
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Select>("options", "Select", [](Select& x) { return join_csv(x.options()); },
                                       [](Select& x, const std::string& s) { x.set_options(split_csv(s)); }));
      t.props.push_back(number<Select>("index", "Select", [](Select& x) { return static_cast<double>(x.index()); },
                                       [](Select& x, double d) { x.set_index(static_cast<int>(d), false); }, {}, 0, 1000));
      r.push_back(std::move(t));
    }
    // ── more layout: grids, wrapping rows, tabs, lists ──
    {
      TypeInfo t{"Grid", "Layout", "\xE2\x96\xA6", true, [] { return std::make_unique<Grid>(); }, [](View&) { return std::string("Grid"); }, {}, {}, {}, {}, {}};
      t.props.push_back(number<Grid>("columns", "Grid", [](Grid& g) { return static_cast<double>(g.column_count()); },
                                     [](Grid& g, double d) { g.set_columns(std::vector<Track>(static_cast<std::size_t>(std::max(1.0, d)), Track::fr(1))); },
                                     "columns", 1, 24));
      t.props.push_back(number<Grid>("spacing", "Grid", [](Grid& g) { return static_cast<double>(g.row_gap()); },
                                     [](Grid& g, double d) { g.set_gaps(static_cast<float>(d), static_cast<float>(d)); }, "spacing", 0, 200));
      t.init_new = [](View& v) {
        static_cast<Grid&>(v).set_columns({Track::fr(1), Track::fr(1)});
        for (const char* s : {"Cell 1", "Cell 2", "Cell 3", "Cell 4"}) v.add(Text(s));
      };
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Wrap", "Layout", "\xE2\x86\xA9", true, [] { return std::make_unique<Wrap>(); }, [](View&) { return std::string("Wrap"); }, {}, {}, {}, {}, {}};
      t.props.push_back(number<Wrap>("spacing", "Wrap", [](Wrap& w) { return static_cast<double>(w.spacing_value()); },
                                     [](Wrap& w, double d) { w.set_spacing(static_cast<float>(d)); }, "spacing", 0, 200));
      t.init_new = [](View& v) {
        for (const char* s : {"one", "two", "three"}) v.add(Button(s));
      };
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Tabs", "Layout", "\xE2\x8A\x9F", true, [] { return std::make_unique<Tabs>(); }, [](View&) { return std::string("Tabs"); }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Tabs>("titles", "Tabs",
                                     [](Tabs& x) {
                                       std::vector<std::string> ts;
                                       for (std::size_t i = 0; i < x.page_count(); ++i) ts.push_back(x.title_of(i));
                                       return join_csv(ts);
                                     },
                                     [](Tabs& x, const std::string& s) { x.set_titles(split_csv(s)); }));
      t.props.push_back(number<Tabs>("current", "Tabs", [](Tabs& x) { return static_cast<double>(x.current()); },
                                     [](Tabs& x, double d) { x.set_current(static_cast<int>(d), false); }, {}, 0, 100));
      t.child_title = [](View& v, std::size_t i) { return static_cast<Tabs&>(v).title_of(i); };
      t.init_new = [](View& v) {
        auto& tabs = static_cast<Tabs&>(v);
        tabs.add(VStack{Text("First page")}.padding(12));
        tabs.add(VStack{Text("Second page")}.padding(12));
        tabs.set_titles({"First", "Second"});
        tabs.height(160);
      };
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"ForEach", "Layout", "\xE2\x98\xB0", true, [] { return std::make_unique<ForEachTemplate>(); },
                 [](View&) { return std::string("ForEach"); }, {}, {}, {}, {}, {}};
      t.props.push_back(string<ForEachTemplate>("items", "List", [](ForEachTemplate&) { return std::string(); },
                                                [](ForEachTemplate&, const std::string&) {}));
      t.props.push_back(string<ForEachTemplate>("item", "List", [](ForEachTemplate& f) { return f.item_name; },
                                                [](ForEachTemplate& f, const std::string& s) { f.item_name = s.empty() ? "item" : s; }));
      t.props.push_back(string<ForEachTemplate>("key", "List", [](ForEachTemplate& f) { return f.key; },
                                                [](ForEachTemplate& f, const std::string& s) { f.key = s; }));
      t.props.push_back(boolean<ForEachTemplate>("virtualized", "List", [](ForEachTemplate& f) { return f.virtualized; },
                                                 [](ForEachTemplate& f, bool b) { f.virtualized = b; f.invalidate_layout(); }));
      t.props.push_back(number<ForEachTemplate>("row_height", "List", [](ForEachTemplate& f) { return static_cast<double>(f.row_height); },
                                                [](ForEachTemplate& f, double d) { f.row_height = static_cast<float>(d); }, {}, 8, 400));
      t.props.push_back(number<ForEachTemplate>("spacing", "List", [](ForEachTemplate& f) { return static_cast<double>(f.spacing); },
                                                [](ForEachTemplate& f, double d) { f.spacing = static_cast<float>(d); f.invalidate_layout(); }, {}, 0, 200));
      t.props.push_back(boolean<ForEachTemplate>("horizontal", "List", [](ForEachTemplate& f) { return f.horizontal; },
                                                 [](ForEachTemplate& f, bool b) { f.horizontal = b; f.invalidate_layout(); }));
      t.props.push_back(number<ForEachTemplate>("preview_rows", "List", [](ForEachTemplate& f) { return static_cast<double>(f.preview_rows); },
                                                [](ForEachTemplate& f, double d) { f.preview_rows = static_cast<int>(d); f.invalidate_layout(); }, {}, 1, 20));
      t.props.back().group = "Designer";
      t.init_new = [](View& v) {
        HStack row{Checkbox(""), Text("Item").flex()};
        row.spacing(8).align_items(Alignment::Center);
        v.add(std::move(row));
      };
      r.push_back(std::move(t));
    }

    // ── more display ──
    {
      TypeInfo t{"Icon", "Display", "\xE2\x98\x85", false, [] { return std::make_unique<Icon>(std::string("\xE2\x98\x85")); },
                 [](View& v) { return "Icon(std::string(" + quote(static_cast<Icon&>(v).source()) + "))"; }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Icon>("source", "Icon", [](Icon& x) { return x.source(); }, [](Icon& x, const std::string& s) { x.set_source(s); }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Canvas", "Display", "\xE2\x9C\x8E", false,
                 [] {
                   auto c = std::make_unique<Canvas>([](Painter& p, const Rect& r) {
                     // a placeholder: the real drawing comes from the bound draw(...) method
                     const Theme& th_ = th();
                     p.stroke_rect(r.inset(0.5f), th_.border, 1, th_.radius_small);
                     p.line({r.x, r.y}, {r.right(), r.bottom()}, th_.border.with_alpha(0.5f), 1);
                     p.line({r.right(), r.y}, {r.x, r.bottom()}, th_.border.with_alpha(0.5f), 1);
                     p.text(r, "Canvas", th_.font_small, th_.text_dim, Align::Center);
                   });
                   c->height(120);
                   return c;
                 },
                 [](View&) { return std::string("Canvas"); }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Canvas>("redraw_on", "Canvas", [](Canvas&) { return std::string(); }, [](Canvas&, const std::string&) {}));
      r.push_back(std::move(t));
    }

    // ── more controls ──
    {
      TypeInfo t{"Segmented", "Controls", "\xE2\x96\xAD\xE2\x96\xAD", false,
                 [] { return std::make_unique<Segmented>(Bind<std::vector<std::string>>(std::vector<std::string>{"Day", "Week", "Month"})); },
                 [](View& v) {
                   auto& s = static_cast<Segmented&>(v);
                   std::string o = "Segmented({";
                   for (std::size_t i = 0; i < s.options().size(); ++i) o += (i ? ", " : "") + quote(s.options()[i]);
                   return o + "}, " + std::to_string(s.index()) + ")";
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<Segmented>("options", "Segmented", [](Segmented& x) { return join_csv(x.options()); },
                                          [](Segmented& x, const std::string& s) { x.set_options(split_csv(s)); }));
      t.props.push_back(number<Segmented>("index", "Segmented", [](Segmented& x) { return static_cast<double>(x.index()); },
                                          [](Segmented& x, double d) { x.set_index(static_cast<int>(d), false); }, {}, 0, 100));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"RadioGroup", "Controls", "\xE2\x97\x89", false,
                 [] { return std::make_unique<RadioGroup>(Bind<std::vector<std::string>>(std::vector<std::string>{"Small", "Medium", "Large"})); },
                 [](View& v) {
                   auto& s = static_cast<RadioGroup&>(v);
                   std::string o = "RadioGroup({";
                   for (std::size_t i = 0; i < s.options().size(); ++i) o += (i ? ", " : "") + quote(s.options()[i]);
                   return o + "}, " + std::to_string(s.index()) + ")";
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<RadioGroup>("options", "RadioGroup", [](RadioGroup& x) { return join_csv(x.options()); },
                                           [](RadioGroup& x, const std::string& s) { x.set_options(split_csv(s)); }));
      t.props.push_back(number<RadioGroup>("index", "RadioGroup", [](RadioGroup& x) { return static_cast<double>(x.index()); },
                                           [](RadioGroup& x, double d) { x.set_index(static_cast<int>(d), false); }, {}, 0, 100));
      t.props.push_back(boolean<RadioGroup>("horizontal", "RadioGroup", [](RadioGroup& x) { return x.is_horizontal(); },
                                            [](RadioGroup& x, bool b) { x.set_horizontal(b); }, "horizontal"));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"TextArea", "Controls", "\xC2\xB6", false, [] { return std::make_unique<TextArea>(); },
                 [](View& v) {
                   auto& a = static_cast<TextArea&>(v);
                   return a.text().empty() ? std::string("TextArea()") : "TextArea(std::string(" + quote(a.text()) + "))";
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<TextArea>("placeholder", "TextArea", [](TextArea& x) { return x.placeholder_value(); },
                                         [](TextArea& x, const std::string& s) { x.set_placeholder(s); }, "placeholder"));
      t.props.push_back(string<TextArea>("text", "TextArea", [](TextArea& x) { return x.text(); }, [](TextArea& x, const std::string& s) { x.set_text(s); }));
      t.props.push_back(boolean<TextArea>("read_only", "TextArea", [](TextArea& x) { return x.is_read_only(); },
                                          [](TextArea& x, bool b) { x.read_only(b); }, "read_only"));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"NumberField", "Controls", "#", false, [] { return std::make_unique<NumberField>(Bind<double>(0.0), 0, 100, 1); },
                 [](View& v) {
                   auto& n = static_cast<NumberField&>(v);
                   return "NumberField(" + num(n.value()) + ", " + num(n.min_value()) + ", " + num(n.max_value()) + ", " + num(n.step_value()) + ")";
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(number<NumberField>("value", "NumberField", [](NumberField& x) { return x.value(); }, [](NumberField& x, double d) { x.set_value(d, false); }));
      t.props.push_back(number<NumberField>("min", "NumberField", [](NumberField& x) { return x.min_value(); },
                                            [](NumberField& x, double d) { x.set_range(d, x.max_value()); }));
      t.props.push_back(number<NumberField>("max", "NumberField", [](NumberField& x) { return x.max_value(); },
                                            [](NumberField& x, double d) { x.set_range(x.min_value(), d); }));
      t.props.push_back(number<NumberField>("step", "NumberField", [](NumberField& x) { return x.step_value(); },
                                            [](NumberField& x, double d) { x.set_step(d); }));
      t.props.push_back(number<NumberField>("decimals", "NumberField", [](NumberField& x) { return static_cast<double>(x.decimals_value()); },
                                            [](NumberField& x, double d) { x.set_decimals(static_cast<int>(d)); }, "decimals", 0, 10));
      t.props.push_back(string<NumberField>("suffix", "NumberField", [](NumberField& x) { return x.suffix_value(); },
                                            [](NumberField& x, const std::string& s) { x.set_suffix(s); }, "suffix"));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"DatePicker", "Controls", "\xF0\x9F\x93\x85", false, [] { return std::make_unique<DatePicker>(Date::today()); },
                 [](View& v) {
                   const Date d = static_cast<DatePicker&>(v).date();
                   return "DatePicker(pulseui::Date{" + std::to_string(d.year) + ", " + std::to_string(d.month) + ", " + std::to_string(d.day) + "})";
                 }, {}, {}, {}, {}, {}};
      t.props.push_back(string<DatePicker>("date", "DatePicker", [](DatePicker& x) { return x.date().iso(); },
                                           [](DatePicker& x, const std::string& s) {
                                             if (auto d = Date::parse(s)) x.set_date(*d, false);
                                           }));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"ColorPicker", "Controls", "\xE2\x97\x95", false, [] { return std::make_unique<ColorPicker>(Color::hex(0x3B82F6)); },
                 [](View& v) { return "ColorPicker(" + color_code(static_cast<ColorPicker&>(v).color()) + ")"; }, {}, {}, {}, {}, {}};
      t.props.push_back(color<ColorPicker>("color", "ColorPicker", [](ColorPicker& x) { return x.color(); },
                                           [](ColorPicker& x, Color c) { x.set_color(c, false); }));
      t.props.push_back(boolean<ColorPicker>("alpha", "ColorPicker", [](ColorPicker& x) { return x.alpha_value(); },
                                             [](ColorPicker& x, bool b) { x.set_alpha(b); }, "alpha"));
      r.push_back(std::move(t));
    }

    // ── project parts: other forms and the project's own view classes ──
    {
      TypeInfo t{"Component", "Project", "\xE2\x96\xA3", false, [] { return std::make_unique<FormComponent>(); },
                 [](View& v) { return "Ui::" + static_cast<FormComponent&>(v).form() + "::part(m)"; }, {}, {}, {}, {}, {}};
      t.props.push_back(string<FormComponent>("form", "Component", [](FormComponent& c) { return c.form(); },
                                              [](FormComponent& c, const std::string& s) { c.set_form(s); }));
      t.props.push_back(string<FormComponent>("model", "Component", [](FormComponent&) { return std::string(); },
                                              [](FormComponent&, const std::string&) {}));
      r.push_back(std::move(t));
    }
    {
      TypeInfo t{"Custom", "Project", "\xE2\x97\x87", false, [] { return std::make_unique<CustomView>(); },
                 [](View& v) { return static_cast<CustomView&>(v).class_name + "()"; }, {}, {}, {}, {}, {}};
      t.props.push_back(string<CustomView>("class", "Custom view", [](CustomView& c) { return c.class_name; },
                                           [](CustomView& c, const std::string& s) { c.class_name = s; c.invalidate_layout(); }));
      t.props.push_back(string<CustomView>("header", "Custom view", [](CustomView& c) { return c.header; },
                                           [](CustomView& c, const std::string& s) { c.header = s; }));
      t.props.push_back(string<CustomView>("args", "Custom view", [](CustomView& c) { return c.args; },
                                           [](CustomView& c, const std::string& s) { c.args = s; }));
      t.props.push_back(string<CustomView>("modifiers", "Custom view", [](CustomView& c) { return c.modifiers; },
                                           [](CustomView& c, const std::string& s) { c.modifiers = s; }));
      r.push_back(std::move(t));
    }

    // ── bindings and events available in forms ──
    auto type = [&r](const char* name) -> TypeInfo& {
      for (auto& t : r)
        if (t.name == name) return t;
      return r.front();
    };
    make_bindable(type("Text").props, "text", "bind_text", "std::string (property, computed or stream)");
    make_bindable(type("Text").props, "color", "color", "Color");
    make_bindable(type("Button").props, "label", "bind_label", "std::string");
    type("Button").events = {{"click", "on_click", ""}};
    make_bindable(type("TextField").props, "text", "model", "pulse::property<std::string> (two-way)");
    make_bindable(type("TextField").props, "placeholder", "placeholder", "std::string");
    type("TextField").events = {{"change", "on_change", "const std::string&"},
                                {"submit", "on_submit", "const std::string&"},
                                {"commit", "on_commit", "const std::string&"}};
    for (const char* n : {"Checkbox", "Toggle"}) {
      make_bindable(type(n).props, "checked", "model", "pulse::property<bool> (two-way)");
      type(n).events = {{"change", "on_change", "bool"}};
    }
    make_bindable(type("Slider").props, "value", "model", "pulse::property<double> (two-way)");
    type("Slider").events = {{"change", "on_change", "double"}};
    make_bindable(type("Select").props, "index", "model", "pulse::property<int> (two-way)");
    make_bindable(type("Select").props, "options", "bind_options", "std::vector<std::string>");
    type("Select").events = {{"change", "on_change", "int"}};
    make_bindable(type("ProgressBar").props, "value", "bind_value", "double 0..1");
    type("Image").cpp_class = "ImageView";
    // the new types
    make_bindable(type("ForEach").props, "items", "", "std::vector<T> (property, computed or stream)");
    make_bindable(type("Component").props, "model", "", "a member object the form's bindings refer to (unbound: this form's class)");
    make_bindable(type("Canvas").props, "redraw_on", "redraw_on", "any property or stream (redraws when it changes)");
    type("Canvas").events = {{"draw", "", "pulseui::Painter&, const pulseui::Rect&"}};
    make_bindable(type("Tabs").props, "current", "selected", "pulse::property<int> (two-way)");
    make_bindable(type("Segmented").props, "index", "model", "pulse::property<int> (two-way)");
    make_bindable(type("Segmented").props, "options", "bind_options", "std::vector<std::string>");
    type("Segmented").events = {{"change", "on_change", "int"}};
    make_bindable(type("RadioGroup").props, "index", "model", "pulse::property<int> (two-way)");
    make_bindable(type("RadioGroup").props, "options", "bind_options", "std::vector<std::string>");
    type("RadioGroup").events = {{"change", "on_change", "int"}};
    make_bindable(type("TextArea").props, "text", "model", "pulse::property<std::string> (two-way)");
    type("TextArea").events = {{"change", "on_change", "const std::string&"}};
    make_bindable(type("NumberField").props, "value", "model", "pulse::property<double> or <int> (two-way)");
    type("NumberField").events = {{"change", "on_change", "double"}};
    make_bindable(type("DatePicker").props, "date", "model", "pulse::property<pulseui::Date> (two-way)");
    type("DatePicker").events = {{"change", "on_change", "pulseui::Date"}};
    make_bindable(type("ColorPicker").props, "color", "model", "pulse::property<Color> (two-way)");
    type("ColorPicker").events = {{"change", "on_change", "pulseui::Color"}};
    // one-way forms for list rows (an item field cannot be written back)
    auto once = [&](const char* tn, const char* prop, const char* modifier) {
      for (auto& p : type(tn).props)
        if (p.name == prop) p.bind_once = binder(modifier);
    };
    once("TextField", "text", "bind_text");
    once("Checkbox", "checked", "bind_checked");
    once("Toggle", "checked", "bind_checked");
    once("Slider", "value", "bind_value");
    once("Select", "index", "bind_index");
    once("Segmented", "index", "bind_index");
    once("RadioGroup", "index", "bind_index");
    once("TextArea", "text", "bind_text");
    once("NumberField", "value", "bind_value");
    once("DatePicker", "date", "bind_date");
    once("ColorPicker", "color", "bind_color");
    once("Tabs", "current", "selected");
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

const std::vector<Event>& events(const View& v) {
  static const std::vector<Event> none;
  const TypeInfo* t = find(v.type_name());
  return t ? t->events : none;
}

std::string cpp_class(std::string_view type_name) {
  const TypeInfo* t = find(type_name);
  if (t && !t->cpp_class.empty()) return t->cpp_class;
  return std::string(type_name);
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
