#include <pulseui/core/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pulseui::json {

namespace {
const Value& null_value() {
  static const Value v;
  return v;
}
} // namespace

const Value::Array& Value::items() const {
  static const Array empty;
  return is_array() ? std::get<Array>(v_) : empty;
}

const Value::Object& Value::fields() const {
  static const Object empty;
  return is_object() ? std::get<Object>(v_) : empty;
}

Value& Value::operator[](std::string_view key) {
  if (!is_object()) v_ = Object{};
  auto& o = std::get<Object>(v_);
  for (auto& [k, v] : o)
    if (k == key) return v;
  o.emplace_back(std::string(key), Value{});
  return o.back().second;
}

const Value& Value::operator[](std::string_view key) const {
  const Value* v = find(key);
  return v ? *v : null_value();
}

const Value* Value::find(std::string_view key) const {
  if (!is_object()) return nullptr;
  for (auto& [k, v] : std::get<Object>(v_))
    if (k == key) return &v;
  return nullptr;
}

void Value::push_back(Value v) {
  if (!is_array()) v_ = Array{};
  std::get<Array>(v_).push_back(std::move(v));
}

static void dump_string(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void Value::dump_to(std::string& out, int indent, int depth) const {
  auto nl = [&](int d) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(d * indent), ' ');
  };
  switch (v_.index()) {
    case 0: out += "null"; break;
    case 1: out += std::get<bool>(v_) ? "true" : "false"; break;
    case 2: {
      const double d = std::get<double>(v_);
      char buf[32];
      if (std::floor(d) == d && std::abs(d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", d);
      else std::snprintf(buf, sizeof buf, "%.6g", d);
      out += buf;
      break;
    }
    case 3: dump_string(out, std::get<std::string>(v_)); break;
    case 4: {
      const auto& a = std::get<Array>(v_);
      if (a.empty()) { out += "[]"; break; }
      out += '[';
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) out += ',';
        nl(depth + 1);
        a[i].dump_to(out, indent, depth + 1);
      }
      nl(depth);
      out += ']';
      break;
    }
    case 5: {
      const auto& o = std::get<Object>(v_);
      if (o.empty()) { out += "{}"; break; }
      out += '{';
      for (std::size_t i = 0; i < o.size(); ++i) {
        if (i) out += ',';
        nl(depth + 1);
        dump_string(out, o[i].first);
        out += indent > 0 ? ": " : ":";
        o[i].second.dump_to(out, indent, depth + 1);
      }
      nl(depth);
      out += '}';
      break;
    }
  }
}

std::string Value::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  return out;
}

namespace {
struct Parser {
  std::string_view s;
  std::size_t i = 0;
  std::string err;

  void ws() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
  }
  bool fail(const char* m) {
    if (err.empty()) err = std::string(m) + " at offset " + std::to_string(i);
    return false;
  }
  static void utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  bool string(std::string& out) {
    ++i; // "
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) return fail("bad escape");
      char e = s[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > s.size()) return fail("bad \\u escape");
          unsigned cp = static_cast<unsigned>(std::strtoul(std::string(s.substr(i, 4)).c_str(), nullptr, 16));
          i += 4;
          if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            unsigned lo = static_cast<unsigned>(std::strtoul(std::string(s.substr(i + 2, 4)).c_str(), nullptr, 16));
            i += 6;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    if (i >= s.size()) return fail("unterminated string");
    ++i;
    return true;
  }
  bool value(Value& out, int depth) {
    if (depth > 200) return fail("nesting too deep");
    ws();
    if (i >= s.size()) return fail("unexpected end");
    const char c = s[i];
    if (c == '{') {
      ++i;
      out = Value::object();
      ws();
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      for (;;) {
        ws();
        if (i >= s.size() || s[i] != '"') return fail("expected key");
        std::string k;
        if (!string(k)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return fail("expected ':'");
        ++i;
        Value v;
        if (!value(v, depth + 1)) return false;
        out[k] = std::move(v);
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++i;
      out = Value::array();
      ws();
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      for (;;) {
        Value v;
        if (!value(v, depth + 1)) return false;
        out.push_back(std::move(v));
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      std::string str;
      if (!string(str)) return false;
      out = Value(std::move(str));
      return true;
    }
    if (s.substr(i, 4) == "true") { i += 4; out = Value(true); return true; }
    if (s.substr(i, 5) == "false") { i += 5; out = Value(false); return true; }
    if (s.substr(i, 4) == "null") { i += 4; out = Value(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const std::size_t start = i;
      while (i < s.size() && (s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' ||
                              s[i] == 'E' || (s[i] >= '0' && s[i] <= '9')))
        ++i;
      out = Value(std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr));
      return true;
    }
    return fail("unexpected character");
  }
};
} // namespace

std::optional<Value> Value::parse(std::string_view text, std::string* error) {
  Parser p{text, 0, {}};
  Value v;
  if (!p.value(v, 0)) {
    if (error) *error = p.err;
    return std::nullopt;
  }
  p.ws();
  if (p.i != text.size()) {
    if (error) *error = "trailing characters at offset " + std::to_string(p.i);
    return std::nullopt;
  }
  return v;
}

} // namespace pulseui::json
