// A compact SVG reader for icons: parses the markup into flattened contours.
#include <pulseui/gfx/painter.hpp>
#include <pulseui/gfx/svg.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace pulseui {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Affine {
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // x' = a x + c y + e, y' = b x + d y + f
  Point apply(Point p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  Affine operator*(const Affine& o) const { // this ∘ o
    return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e,
            b * o.e + d * o.f + f};
  }
  float scale() const { return std::sqrt(std::abs(a * d - b * c)); }
};

struct Paint {
  bool set = false;
  bool none = false;
  bool current = false;
  Color color;
};

struct StyleState {
  Affine m;
  Paint fill{true, false, false, Color::black()};
  Paint stroke{true, true, false, Color::black()};
  float stroke_width = 1;
  bool even_odd = false;
  float opacity = 1;
  Color color = Color::black(); // the CSS `color` property (what currentColor means)
  bool color_set = false;
};

// ── small parsing helpers ────────────────────────────────────────────────────

void skip_ws(std::string_view s, std::size_t& i) {
  while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
}

bool read_number(std::string_view s, std::size_t& i, float& out) {
  skip_ws(s, i);
  if (i >= s.size()) return false;
  const std::size_t start = i;
  if (s[i] == '+' || s[i] == '-') ++i;
  bool digits = false, dot = false;
  while (i < s.size()) {
    const char c = s[i];
    if (std::isdigit(static_cast<unsigned char>(c))) { digits = true; ++i; }
    else if (c == '.' && !dot) { dot = true; ++i; }
    else break;
  }
  if (digits && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    std::size_t j = i + 1;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
    if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
      i = j;
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }
  }
  if (!digits) {
    i = start;
    return false;
  }
  out = std::strtof(std::string(s.substr(start, i - start)).c_str(), nullptr);
  return true;
}

// arc flags may be written without separators: "a1 1 0 01.5 2"
bool read_flag(std::string_view s, std::size_t& i, bool& out) {
  skip_ws(s, i);
  if (i < s.size() && (s[i] == '0' || s[i] == '1')) {
    out = s[i] == '1';
    ++i;
    return true;
  }
  return false;
}

float parse_length(std::string_view v, float fallback = 0) {
  std::size_t i = 0;
  float f = fallback;
  if (!read_number(v, i, f)) return fallback;
  return f;
}

const std::map<std::string, std::uint32_t>& named_colors() {
  static const std::map<std::string, std::uint32_t> m = {
      {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},     {"green", 0x008000}, {"blue", 0x0000FF},
      {"yellow", 0xFFFF00}, {"orange", 0xFFA500}, {"purple", 0x800080}, {"gray", 0x808080}, {"grey", 0x808080},
      {"silver", 0xC0C0C0}, {"navy", 0x000080}, {"teal", 0x008080},     {"lime", 0x00FF00}, {"maroon", 0x800000},
      {"olive", 0x808000}, {"aqua", 0x00FFFF}, {"fuchsia", 0xFF00FF},   {"cyan", 0x00FFFF}, {"magenta", 0xFF00FF},
      {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700},      {"crimson", 0xDC143C}};
  return m;
}

Paint parse_paint(std::string v) {
  while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
  while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
  Paint p;
  p.set = true;
  std::string low = v;
  for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (low == "none" || low == "transparent") { p.none = true; return p; }
  if (low == "currentcolor") { p.current = true; return p; }
  if (low.rfind("url(", 0) == 0) { p.current = true; return p; } // gradients: use the current color
  if (auto c = Color::parse(v)) { p.color = *c; return p; }
  if (low.rfind("rgb", 0) == 0) {
    std::size_t i = low.find('(');
    float r = 0, g = 0, b = 0, a = 1;
    if (i != std::string::npos) {
      ++i;
      read_number(low, i, r);
      if (i < low.size() && low[i] == '%') { r *= 2.55f; ++i; }
      read_number(low, i, g);
      if (i < low.size() && low[i] == '%') { g *= 2.55f; ++i; }
      read_number(low, i, b);
      if (i < low.size() && low[i] == '%') { b *= 2.55f; ++i; }
      read_number(low, i, a);
    }
    p.color = Color(r / 255.f, g / 255.f, b / 255.f, a);
    return p;
  }
  auto it = named_colors().find(low);
  if (it != named_colors().end()) { p.color = Color::hex(it->second); return p; }
  p.set = false;
  return p;
}

Affine parse_transform(std::string_view s) {
  Affine m;
  std::size_t i = 0;
  while (i < s.size()) {
    skip_ws(s, i);
    std::size_t name_start = i;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
    const std::string name(s.substr(name_start, i - name_start));
    skip_ws(s, i);
    if (name.empty() || i >= s.size() || s[i] != '(') break;
    ++i;
    float v[6] = {};
    int n = 0;
    while (n < 6 && read_number(s, i, v[n])) ++n;
    skip_ws(s, i);
    if (i < s.size() && s[i] == ')') ++i;
    Affine t;
    if (name == "matrix" && n == 6) t = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (name == "translate") t = {1, 0, 0, 1, v[0], n > 1 ? v[1] : 0};
    else if (name == "scale") t = {v[0], 0, 0, n > 1 ? v[1] : v[0], 0, 0};
    else if (name == "rotate") {
      const float r = v[0] * kPi / 180.f, cs = std::cos(r), sn = std::sin(r);
      t = {cs, sn, -sn, cs, 0, 0};
      if (n >= 3) t = Affine{1, 0, 0, 1, v[1], v[2]} * t * Affine{1, 0, 0, 1, -v[1], -v[2]};
    } else if (name == "skewX") t = {1, 0, std::tan(v[0] * kPi / 180.f), 1, 0, 0};
    else if (name == "skewY") t = {1, std::tan(v[0] * kPi / 180.f), 0, 1, 0, 0};
    m = m * t;
  }
  return m;
}

// ── path data ────────────────────────────────────────────────────────────────

struct PathBuilder {
  std::vector<std::vector<Point>> contours;
  std::vector<bool> closed;
  std::vector<Point> cur;
  Point start{}, pos{};

  void flush(bool close) {
    if (cur.size() >= 2) {
      contours.push_back(std::move(cur));
      closed.push_back(close);
    }
    cur.clear();
  }
  void move(Point p) {
    flush(false);
    cur.push_back(p);
    start = pos = p;
  }
  void line(Point p) {
    if (cur.empty()) cur.push_back(pos);
    cur.push_back(p);
    pos = p;
  }
  void cubic(Point c1, Point c2, Point p) {
    if (cur.empty()) cur.push_back(pos);
    const Point p0 = pos;
    const float len = std::hypot(c1.x - p0.x, c1.y - p0.y) + std::hypot(c2.x - c1.x, c2.y - c1.y) + std::hypot(p.x - c2.x, p.y - c2.y);
    const int n = std::clamp(static_cast<int>(len / 1.5f), 4, 40);
    for (int k = 1; k <= n; ++k) {
      const float t = static_cast<float>(k) / n, u = 1 - t;
      cur.push_back({u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p.x,
                     u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p.y});
    }
    pos = p;
  }
  void quad(Point c, Point p) {
    const Point p0 = pos;
    cubic({p0.x + 2.f / 3 * (c.x - p0.x), p0.y + 2.f / 3 * (c.y - p0.y)}, {p.x + 2.f / 3 * (c.x - p.x), p.y + 2.f / 3 * (c.y - p.y)}, p);
  }
  void arc(float rx, float ry, float phi_deg, bool large, bool sweep, Point p) {
    const Point p0 = pos;
    if (rx == 0 || ry == 0) { line(p); return; }
    rx = std::abs(rx);
    ry = std::abs(ry);
    const float phi = phi_deg * kPi / 180.f, cs = std::cos(phi), sn = std::sin(phi);
    const float dx = (p0.x - p.x) / 2, dy = (p0.y - p.y) / 2;
    const float x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
    float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
      rx *= std::sqrt(lambda);
      ry *= std::sqrt(lambda);
    }
    const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    float coef = den > 0 ? std::sqrt(std::max(0.f, num / den)) : 0;
    if (large == sweep) coef = -coef;
    const float cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
    const float cx = cs * cx1 - sn * cy1 + (p0.x + p.x) / 2, cy = sn * cx1 + cs * cy1 + (p0.y + p.y) / 2;
    auto angle = [](float ux, float uy, float vx, float vy) {
      const float a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
      return a;
    };
    const float t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    float dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sweep && dt > 0) dt -= 2 * kPi;
    else if (sweep && dt < 0) dt += 2 * kPi;
    const int n = std::clamp(static_cast<int>(std::abs(dt) * std::max(rx, ry) / 1.5f), 4, 64);
    if (cur.empty()) cur.push_back(p0);
    for (int k = 1; k <= n; ++k) {
      const float t = t1 + dt * static_cast<float>(k) / n;
      const float ex = rx * std::cos(t), ey = ry * std::sin(t);
      cur.push_back({cs * ex - sn * ey + cx, sn * ex + cs * ey + cy});
    }
    pos = p;
  }
  void close() {
    if (!cur.empty()) {
      flush(true);
    }
    pos = start;
  }
};

void parse_path(std::string_view d, PathBuilder& b) {
  std::size_t i = 0;
  char cmd = 0;
  Point last_c{};      // last control point (for S / T)
  char last_cmd = 0;
  while (true) {
    skip_ws(d, i);
    if (i >= d.size()) break;
    if (std::isalpha(static_cast<unsigned char>(d[i]))) {
      cmd = d[i++];
    } else if (!cmd) {
      break;
    }
    const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
    const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
    const Point o = rel ? b.pos : Point{0, 0};
    float v[7];
    auto nums = [&](int n) {
      for (int k = 0; k < n; ++k)
        if (!read_number(d, i, v[k])) return false;
      return true;
    };
    switch (up) {
      case 'M':
        if (!nums(2)) return;
        b.move({o.x + v[0], o.y + v[1]});
        cmd = rel ? 'l' : 'L'; // further pairs are line-tos
        break;
      case 'L':
        if (!nums(2)) return;
        b.line({o.x + v[0], o.y + v[1]});
        break;
      case 'H':
        if (!nums(1)) return;
        b.line({(rel ? b.pos.x : 0) + v[0], b.pos.y});
        break;
      case 'V':
        if (!nums(1)) return;
        b.line({b.pos.x, (rel ? b.pos.y : 0) + v[0]});
        break;
      case 'C':
        if (!nums(6)) return;
        last_c = {o.x + v[2], o.y + v[3]};
        b.cubic({o.x + v[0], o.y + v[1]}, last_c, {o.x + v[4], o.y + v[5]});
        break;
      case 'S': {
        if (!nums(4)) return;
        const char lu = static_cast<char>(std::toupper(static_cast<unsigned char>(last_cmd)));
        const Point c1 = (lu == 'C' || lu == 'S') ? Point{2 * b.pos.x - last_c.x, 2 * b.pos.y - last_c.y} : b.pos;
        last_c = {o.x + v[0], o.y + v[1]};
        b.cubic(c1, last_c, {o.x + v[2], o.y + v[3]});
        break;
      }
      case 'Q':
        if (!nums(4)) return;
        last_c = {o.x + v[0], o.y + v[1]};
        b.quad(last_c, {o.x + v[2], o.y + v[3]});
        break;
      case 'T': {
        if (!nums(2)) return;
        const char lu = static_cast<char>(std::toupper(static_cast<unsigned char>(last_cmd)));
        last_c = (lu == 'Q' || lu == 'T') ? Point{2 * b.pos.x - last_c.x, 2 * b.pos.y - last_c.y} : b.pos;
        b.quad(last_c, {o.x + v[0], o.y + v[1]});
        break;
      }
      case 'A': {
        bool large = false, sweep = false;
        if (!read_number(d, i, v[0]) || !read_number(d, i, v[1]) || !read_number(d, i, v[2]) || !read_flag(d, i, large) ||
            !read_flag(d, i, sweep) || !read_number(d, i, v[3]) || !read_number(d, i, v[4]))
          return;
        b.arc(v[0], v[1], v[2], large, sweep, {o.x + v[3], o.y + v[4]});
        break;
      }
      case 'Z':
        b.close();
        break;
      default:
        return;
    }
    last_cmd = cmd;
  }
  b.flush(false);
}

std::vector<Point> parse_points(std::string_view s) {
  std::vector<Point> out;
  std::size_t i = 0;
  float x = 0, y = 0;
  while (read_number(s, i, x) && read_number(s, i, y)) out.push_back({x, y});
  return out;
}

// ── XML ──────────────────────────────────────────────────────────────────────

struct Tag {
  std::string name;
  std::unordered_map<std::string, std::string> attrs;
  bool closing = false, self_closing = false;
};

bool next_tag(std::string_view s, std::size_t& i, Tag& t) {
  for (;;) {
    i = s.find('<', i);
    if (i == std::string_view::npos) return false;
    if (s.compare(i, 4, "<!--") == 0) {
      const auto e = s.find("-->", i);
      if (e == std::string_view::npos) return false;
      i = e + 3;
      continue;
    }
    if (s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0) {
      const auto e = s.find('>', i);
      if (e == std::string_view::npos) return false;
      i = e + 1;
      continue;
    }
    break;
  }
  ++i;
  t = Tag{};
  if (i < s.size() && s[i] == '/') {
    t.closing = true;
    ++i;
  }
  const std::size_t ns = i;
  while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
  t.name = std::string(s.substr(ns, i - ns));
  if (const auto colon = t.name.find(':'); colon != std::string::npos) t.name = t.name.substr(colon + 1); // svg:path
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size()) return false;
    if (s[i] == '>') { ++i; return true; }
    if (s[i] == '/') {
      t.self_closing = true;
      ++i;
      continue;
    }
    const std::size_t as = i;
    while (i < s.size() && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
    std::string key(s.substr(as, i - as));
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    std::string value;
    if (i < s.size() && s[i] == '=') {
      ++i;
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
      if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
        const char q = s[i++];
        const auto e = s.find(q, i);
        if (e == std::string_view::npos) return false;
        value = std::string(s.substr(i, e - i));
        i = e + 1;
      }
    }
    if (!key.empty()) t.attrs[key] = value;
  }
  return false;
}

void apply_style_props(StyleState& st, const std::unordered_map<std::string, std::string>& props) {
  auto get = [&](const char* k) -> const std::string* {
    auto it = props.find(k);
    return it == props.end() ? nullptr : &it->second;
  };
  if (auto v = get("color")) {
    Paint p = parse_paint(*v);
    if (p.set && !p.none && !p.current) {
      st.color = p.color;
      st.color_set = true;
    }
  }
  if (auto v = get("fill")) {
    Paint p = parse_paint(*v);
    if (p.set) st.fill = p;
  }
  if (auto v = get("stroke")) {
    Paint p = parse_paint(*v);
    if (p.set) st.stroke = p;
  }
  if (auto v = get("stroke-width")) st.stroke_width = parse_length(*v, st.stroke_width);
  if (auto v = get("fill-rule")) st.even_odd = *v == "evenodd";
  if (auto v = get("opacity")) st.opacity *= std::clamp(parse_length(*v, 1), 0.f, 1.f);
  if (auto v = get("fill-opacity")) st.fill.color.a *= std::clamp(parse_length(*v, 1), 0.f, 1.f);
  if (auto v = get("stroke-opacity")) st.stroke.color.a *= std::clamp(parse_length(*v, 1), 0.f, 1.f);
}

std::unordered_map<std::string, std::string> parse_style_attr(const std::string& style) {
  std::unordered_map<std::string, std::string> out;
  std::size_t i = 0;
  while (i < style.size()) {
    const auto semi = style.find(';', i);
    const std::string item = style.substr(i, semi == std::string::npos ? std::string::npos : semi - i);
    const auto colon = item.find(':');
    if (colon != std::string::npos) {
      auto trim = [](std::string x) {
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
        return x;
      };
      out[trim(item.substr(0, colon))] = trim(item.substr(colon + 1));
    }
    if (semi == std::string::npos) break;
    i = semi + 1;
  }
  return out;
}

void rect_path(PathBuilder& b, float x, float y, float w, float h, float rx, float ry) {
  if (rx <= 0 && ry <= 0) {
    b.move({x, y});
    b.line({x + w, y});
    b.line({x + w, y + h});
    b.line({x, y + h});
    b.close();
    return;
  }
  if (rx <= 0) rx = ry;
  if (ry <= 0) ry = rx;
  rx = std::min(rx, w / 2);
  ry = std::min(ry, h / 2);
  b.move({x + rx, y});
  b.line({x + w - rx, y});
  b.arc(rx, ry, 0, false, true, {x + w, y + ry});
  b.line({x + w, y + h - ry});
  b.arc(rx, ry, 0, false, true, {x + w - rx, y + h});
  b.line({x + rx, y + h});
  b.arc(rx, ry, 0, false, true, {x, y + h - ry});
  b.line({x, y + ry});
  b.arc(rx, ry, 0, false, true, {x + rx, y});
  b.close();
}

void ellipse_path(PathBuilder& b, float cx, float cy, float rx, float ry) {
  if (rx <= 0 || ry <= 0) return;
  b.move({cx + rx, cy});
  b.arc(rx, ry, 0, false, true, {cx - rx, cy});
  b.arc(rx, ry, 0, false, true, {cx + rx, cy});
  b.close();
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════

Svg Svg::parse(std::string_view s) {
  auto data = std::make_shared<Data>();
  std::vector<StyleState> stack{StyleState{}};
  std::size_t i = 0;
  Tag t;
  int skip_depth = 0; // inside <defs>, <clipPath>, <mask>, ... nothing is drawn
  bool have_view_box = false;
  while (next_tag(s, i, t)) {
    static const char* hidden[] = {"defs", "clipPath", "mask", "symbol", "linearGradient", "radialGradient", "pattern",
                                   "filter", "title", "desc", "metadata", "style", "text"};
    bool is_hidden = false;
    for (const char* h : hidden)
      if (t.name == h) is_hidden = true;
    if (t.closing) {
      if (is_hidden && skip_depth > 0) --skip_depth;
      else if ((t.name == "g" || t.name == "svg" || t.name == "a") && stack.size() > 1) stack.pop_back();
      continue;
    }
    if (is_hidden) {
      if (!t.self_closing) ++skip_depth;
      continue;
    }
    if (skip_depth > 0) continue;

    StyleState st = stack.back();
    if (auto it = t.attrs.find("transform"); it != t.attrs.end()) st.m = st.m * parse_transform(it->second);
    apply_style_props(st, t.attrs);
    if (auto it = t.attrs.find("style"); it != t.attrs.end()) apply_style_props(st, parse_style_attr(it->second));
    if (auto it = t.attrs.find("display"); it != t.attrs.end() && it->second == "none") {
      if (!t.self_closing && (t.name == "g" || t.name == "svg")) ++skip_depth; // approximate: skip the group
      continue;
    }

    if (t.name == "svg" || t.name == "g" || t.name == "a") {
      if (t.name == "svg" && !have_view_box) {
        have_view_box = true;
        if (auto it = t.attrs.find("viewBox"); it != t.attrs.end()) {
          std::size_t k = 0;
          float v[4] = {0, 0, 24, 24};
          for (float& x : v) read_number(it->second, k, x);
          data->view_box = {v[0], v[1], v[2], v[3]};
        } else {
          const float w = t.attrs.count("width") ? parse_length(t.attrs["width"], 24) : 24;
          const float h = t.attrs.count("height") ? parse_length(t.attrs["height"], 24) : 24;
          data->view_box = {0, 0, w, h};
        }
      }
      if (!t.self_closing) stack.push_back(st);
      continue;
    }

    PathBuilder b;
    auto num = [&](const char* k, float def = 0) {
      auto it = t.attrs.find(k);
      return it == t.attrs.end() ? def : parse_length(it->second, def);
    };
    if (t.name == "path") {
      if (auto it = t.attrs.find("d"); it != t.attrs.end()) parse_path(it->second, b);
    } else if (t.name == "rect") {
      rect_path(b, num("x"), num("y"), num("width"), num("height"), num("rx"), num("ry"));
    } else if (t.name == "circle") {
      ellipse_path(b, num("cx"), num("cy"), num("r"), num("r"));
    } else if (t.name == "ellipse") {
      ellipse_path(b, num("cx"), num("cy"), num("rx"), num("ry"));
    } else if (t.name == "line") {
      b.move({num("x1"), num("y1")});
      b.line({num("x2"), num("y2")});
      b.flush(false);
    } else if (t.name == "polyline" || t.name == "polygon") {
      auto pts = parse_points(t.attrs["points"]);
      if (pts.size() >= 2) {
        b.move(pts[0]);
        for (std::size_t k = 1; k < pts.size(); ++k) b.line(pts[k]);
        if (t.name == "polygon") b.close();
        else b.flush(false);
      }
    } else {
      continue;
    }
    if (b.contours.empty()) continue;
    Shape shape;
    for (auto& c : b.contours) {
      for (auto& p : c) p = st.m.apply(p);
      shape.contours.push_back(std::move(c));
    }
    shape.closed = std::move(b.closed);
    // lines and polylines are never filled
    const bool fillable = t.name != "line" && t.name != "polyline";
    if (fillable && !st.fill.none) {
      if (st.fill.current && !st.color_set) shape.fill_current = true;
      else shape.fill = st.fill.current ? st.color : st.fill.color;
    }
    if (!st.stroke.none) {
      if (st.stroke.current && !st.color_set) shape.stroke_current = true;
      else shape.stroke = st.stroke.current ? st.color : st.stroke.color;
      shape.stroke_width = st.stroke_width * st.m.scale();
    }
    shape.even_odd = st.even_odd;
    shape.opacity = st.opacity;
    data->shapes.push_back(std::move(shape));
  }
  Svg out;
  if (!data->shapes.empty() || have_view_box) out.data_ = std::move(data);
  return out;
}

Svg Svg::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  return parse(ss.str());
}

void Svg::draw(Painter& p, const Rect& dst, Color current, bool tint) const {
  if (!data_ || dst.w <= 0 || dst.h <= 0) return;
  const Rect& vb = data_->view_box;
  if (vb.w <= 0 || vb.h <= 0) return;
  const float k = std::min(dst.w / vb.w, dst.h / vb.h);
  const float ox = dst.x + (dst.w - vb.w * k) / 2 - vb.x * k, oy = dst.y + (dst.h - vb.h * k) / 2 - vb.y * k;
  std::vector<std::vector<Point>> pts;
  for (const Shape& s : data_->shapes) {
    pts.clear();
    for (auto& c : s.contours) {
      std::vector<Point> q;
      q.reserve(c.size());
      for (auto& pt : c) q.push_back({ox + pt.x * k, oy + pt.y * k});
      pts.push_back(std::move(q));
    }
    auto resolve = [&](const std::optional<Color>& c, bool is_current) -> std::optional<Color> {
      if (is_current) return current.with_alpha(current.a * s.opacity);
      if (!c) return std::nullopt;
      if (tint) return current.with_alpha(current.a * c->a * s.opacity);
      return c->with_alpha(c->a * s.opacity);
    };
    if (auto fc = resolve(s.fill, s.fill_current)) p.fill_path(pts, *fc, s.even_odd);
    if (auto sc = resolve(s.stroke, s.stroke_current)) {
      const float w = std::max(0.5f, s.stroke_width * k);
      for (std::size_t i = 0; i < pts.size(); ++i) p.polyline(pts[i], *sc, w, i < s.closed.size() && s.closed[i]);
    }
  }
}

bool is_svg_icon(std::string_view icon) {
  std::size_t i = 0;
  while (i < icon.size() && std::isspace(static_cast<unsigned char>(icon[i]))) ++i;
  if (icon.substr(i, 4) == "<svg" || icon.substr(i, 5) == "<?xml") return true;
  return icon.size() > 4 && (icon.substr(icon.size() - 4) == ".svg" || icon.substr(icon.size() - 4) == ".SVG");
}

void draw_icon(Painter& p, std::string_view icon, const Rect& r, Color color, const Font& font) {
  if (icon.empty()) return;
  if (!is_svg_icon(icon)) {
    p.text(r, icon, font, color, Align::Center);
    return;
  }
  static std::unordered_map<std::string, Svg> cache;
  std::string key(icon);
  auto it = cache.find(key);
  if (it == cache.end()) {
    const bool markup = icon.find('<') != std::string_view::npos;
    it = cache.emplace(key, markup ? Svg::parse(icon) : Svg::load(key)).first;
  }
  const float side = std::min({r.w, r.h, font.size * 1.25f});
  it->second.draw(p, {r.x + (r.w - side) / 2, r.y + (r.h - side) / 2, side, side}, color, true);
}

} // namespace pulseui
