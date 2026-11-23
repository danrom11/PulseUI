#pragma once
// SVG icons: a small, dependency-free renderer for the subset icons use -
// <path>, <rect>, <circle>, <ellipse>, <line>, <polyline>, <polygon>, <g>,
// transforms, fill / stroke / stroke-width / fill-rule / opacity (attributes or
// style=""), viewBox and currentColor. Gradients, text, masks and filters are ignored.
//
//   Svg logo = Svg::load("assets/logo.svg");
//   logo.draw(painter, {0, 0, 32, 32});
//   Icon(R"(<svg viewBox="0 0 24 24"><path d="M12 2 2 22h20z"/></svg>)").size(16)
//   Button("Save").icon("icons/save.svg")      // icon strings may be SVG markup or .svg paths
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/gfx/font.hpp>

namespace pulseui {

class Painter;

class Svg {
public:
  Svg() = default;
  static Svg parse(std::string_view markup);
  static Svg load(const std::string& path); // empty Svg when the file is missing or invalid

  bool valid() const { return data_ != nullptr && !data_->shapes.empty(); }
  Size size() const { return data_ ? Size{data_->view_box.w, data_->view_box.h} : Size{}; }
  std::size_t shape_count() const { return data_ ? data_->shapes.size() : 0; }

  // Draws scaled into dst (aspect ratio kept, centred). `current` replaces
  // currentColor; with `tint` every color is replaced (monochrome icons).
  void draw(Painter& p, const Rect& dst, Color current = Color::black(), bool tint = false) const;

  struct Shape {
    std::vector<std::vector<Point>> contours; // in viewBox units, transforms applied
    std::vector<bool> closed;
    std::optional<Color> fill, stroke;        // nullopt = none
    bool fill_current = false, stroke_current = false;
    float stroke_width = 1;
    bool even_odd = false;
    float opacity = 1;
  };
  struct Data {
    Rect view_box{0, 0, 24, 24};
    std::vector<Shape> shapes;
  };
  const Data* data() const { return data_.get(); }

private:
  std::shared_ptr<const Data> data_;
};

// Is this icon string SVG (markup or a .svg path) rather than a text glyph?
bool is_svg_icon(std::string_view icon);
// Draws an icon string: SVG markup / .svg file (cached) tinted with `color`,
// or a text glyph (emoji, symbol) in `font`.
void draw_icon(Painter& p, std::string_view icon, const Rect& r, Color color, const Font& font);

} // namespace pulseui
