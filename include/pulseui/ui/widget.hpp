#pragma once
#include <pulseui/core/subscriptions.hpp>

namespace pulseui::ui {
  struct Widget {
    virtual ~Widget() = default;
    virtual void mount() {}
    virtual void unmount() {}
  protected:
    pulseui::core::subs_bag subs_;
  };
}
