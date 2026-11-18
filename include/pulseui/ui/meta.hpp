#pragma once
// Minimal reflection used by forms (.pform) and the Pulse Layer designer.
// Only the *editable* properties of each view type are described here.
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <pulseui/core/color.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui::meta {

enum class PropType { Bool, Number, String, Color, Enum };

// Numbers are stored as double, enums as the option index.
using Value = std::variant<std::monostate, bool, double, std::string, Color>;

struct Property {
  std::string name;  // also the JSON key
  std::string group; // "Layout", "Appearance", ...
  PropType type = PropType::String;
  std::vector<std::string> options; // for Enum
  double min = -1e6, max = 1e6;
  std::function<Value(View&)> get;
  std::function<void(View&, const Value&)> set;
  // C++ modifier for code generation, e.g. ".font_size(18)". Empty = not emitted.
  std::function<std::string(View&, const Value&)> code;
};

struct TypeInfo {
  std::string name;     // "Button"
  std::string category; // palette group: "Layout", "Controls", "Display"
  std::string glyph;    // short icon text for the palette
  bool container = false;
  std::function<std::unique_ptr<View>()> create;
  std::function<std::string(View&)> ctor_code; // "Button(\"OK\")"
  std::vector<Property> props;
};

const std::vector<TypeInfo>& types();
const TypeInfo* find(std::string_view type_name);
void register_type(TypeInfo info);

std::unique_ptr<View> create(std::string_view type_name);
// The type's own properties followed by the common ones (id, size, padding, ...).
std::vector<const Property*> properties(const View& v);
const Property* property(const View& v, std::string_view name);

Value get(View& v, std::string_view prop);
void set(View& v, std::string_view prop, const Value& value);
bool equal(const Value& a, const Value& b);
std::string to_string(const Value& v);

} // namespace pulseui::meta
