#pragma once
#include <vector>
#include <pulseui/ui/widget.hpp>
#include <pulseui/ui/layout.hpp>

namespace pulseui::ui {

struct ChildItem {
  Widget* w{};
  float fixed_px = -1.f;
  float flex = 0.f;
};

class Container : public Widget {
public:
  void add_child(Widget* w, float fixed_px=-1.f, float flex=0.f) {
    children_.push_back({w, fixed_px, flex});
  }

protected:
  std::vector<ChildItem> children_;
};

} // namespace pulseui::ui
