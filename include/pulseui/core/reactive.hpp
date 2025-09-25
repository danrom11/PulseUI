#pragma once
#include <functional>
#include <pulse/pulse.hpp>
#include <pulseui/core/executor.hpp>

namespace pulseui::core {

  // Adapter: Turn our UI executor into pulse::executor
  class pulse_executor_adapter final : public pulse::executor {
    pulseui::core::executor& ex_;
  public:
    explicit pulse_executor_adapter(pulseui::core::executor& ex) : ex_(ex) {}
    void post(std::function<void()> fn) override { ex_.post(std::move(fn)); }
  };

  inline pulse_executor_adapter as_pulse_executor(pulseui::core::executor& ex) {
    return pulse_executor_adapter{ex};
  }

} // namespace pulseui::core
