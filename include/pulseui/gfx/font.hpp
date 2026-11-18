#pragma once
#include <string>

namespace pulseui {

enum class FontWeight { Light = 300, Normal = 400, Medium = 500, SemiBold = 600, Bold = 700 };

struct Font {
  std::string family; // empty = system UI font
  float size = 13.f;  // logical pixels
  FontWeight weight = FontWeight::Normal;
  bool italic = false;

  Font() = default;
  explicit Font(float sz, FontWeight w = FontWeight::Normal) : size(sz), weight(w) {}

  Font with_size(float s) const { Font f = *this; f.size = s; return f; }
  Font with_weight(FontWeight w) const { Font f = *this; f.weight = w; return f; }
  bool operator==(const Font&) const = default;
};

} // namespace pulseui
