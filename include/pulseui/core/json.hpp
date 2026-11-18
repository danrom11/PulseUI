#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace pulseui::json {

// Small order-preserving JSON value (used for .pform files and settings).
class Value {
public:
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : v_(b) {}
  Value(int i) : v_(static_cast<double>(i)) {}
  Value(double d) : v_(d) {}
  Value(float f) : v_(static_cast<double>(f)) {}
  Value(const char* s) : v_(std::string(s)) {}
  Value(std::string s) : v_(std::move(s)) {}
  Value(Array a) : v_(std::move(a)) {}
  Value(Object o) : v_(std::move(o)) {}

  static Value array() { return Value(Array{}); }
  static Value object() { return Value(Object{}); }

  bool is_null() const { return v_.index() == 0; }
  bool is_bool() const { return std::holds_alternative<bool>(v_); }
  bool is_number() const { return std::holds_alternative<double>(v_); }
  bool is_string() const { return std::holds_alternative<std::string>(v_); }
  bool is_array() const { return std::holds_alternative<Array>(v_); }
  bool is_object() const { return std::holds_alternative<Object>(v_); }

  bool as_bool(bool def = false) const { return is_bool() ? std::get<bool>(v_) : def; }
  double as_number(double def = 0) const { return is_number() ? std::get<double>(v_) : def; }
  std::string as_string(std::string def = {}) const {
    return is_string() ? std::get<std::string>(v_) : def;
  }
  const Array& items() const;   // empty if not an array
  const Object& fields() const; // empty if not an object

  Value& operator[](std::string_view key); // inserts; turns null into an object
  const Value& operator[](std::string_view key) const; // null value if missing
  const Value* find(std::string_view key) const;
  void push_back(Value v);

  std::string dump(int indent = 2) const;
  static std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

private:
  void dump_to(std::string& out, int indent, int depth) const;
  std::variant<std::monostate, bool, double, std::string, Array, Object> v_;
};

} // namespace pulseui::json
