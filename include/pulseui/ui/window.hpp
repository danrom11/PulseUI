#pragma once
#include <functional>
#include <string>
#include <memory>
#include <pulseui/ui/canvas.hpp>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {
  struct Window {
    using PaintCB = std::function<void(Canvas&)>;
    using InputCB = std::function<void(const InputEvent&)>;
    virtual ~Window() = default;
    virtual void set_title(std::string) = 0;
    virtual void invalidate() = 0;
    virtual float dpi_scale() const = 0;
    virtual void on_paint(PaintCB) = 0;
    virtual void on_input(InputCB) = 0;
    [[deprecated("Use the show() and hide() methods")]]
    virtual void set_visible(bool visible) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void close() = 0;
  };
}
