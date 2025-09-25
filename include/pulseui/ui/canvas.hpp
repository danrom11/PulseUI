#pragma once
#include <string_view>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {
  struct Font { float size = 14.f; /* future: family/weight */ };

  struct Canvas {
    virtual ~Canvas() = default;
    virtual void clear(Color c) = 0;
    virtual void fill_rect(Rect r, Color c) = 0;
    virtual void draw_text(Point p, std::string_view text, const Font& f, Color c) = 0;
  };
}
