#pragma once
#include <functional>
namespace pulseui::core {
  struct executor {
    virtual ~executor() = default;
    virtual void post(std::function<void()> fn) = 0;
  };
}
