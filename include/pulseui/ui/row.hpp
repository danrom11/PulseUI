#pragma once
#include <pulseui/ui/container.hpp>

namespace pulseui::ui {

class Row : public Container {
public:
  void paint(Canvas& g) override {
    std::vector<Item> items;
    items.reserve(children_.size());

    for (auto& c : children_) {
      if (c.fixed_px >= 0)
        items.push_back(Fixed(c.fixed_px, [c](Rect r){ c.w->set_rect(r); }));
      else
        items.push_back(Expand(c.flex, [c](Rect r){ c.w->set_rect(r); }));
    }

    layout_row(rect_, items);

    for (auto& c : children_)
      c.w->paint(g);
  }
};

} // namespace pulseui::ui
