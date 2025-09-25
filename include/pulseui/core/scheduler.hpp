#pragma once
#include <chrono>
#include <pulse/pulse.hpp>
#include <pulseui/core/executor.hpp>
#include <pulseui/core/reactive.hpp>

namespace pulseui::core {
  template<class Rep, class Period>
  inline auto every(std::chrono::duration<Rep,Period> d, executor& ex) {
    return pulse::interval(d, as_pulse_executor(ex));
  }
}
