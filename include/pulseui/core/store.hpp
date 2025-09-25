#pragma once
#include <memory>
#include <utility>
#include <pulse/pulse.hpp>

namespace pulseui::core {

// State storage on top of observable<Action>.
// We keep the state in a shared_ptr inside the closure so that it survives all on_next.
template <class Model, class Action, class Reducer>
auto make_store(pulse::observable<Action> actions, Reducer reducer) {
  auto state = std::make_shared<Model>(); // initial state Model{}
  return actions | pulse::map([state, reducer](const Action& a) {
    // Update the accumulated state and send a copy outside
    *state = reducer(*state, a);
    return *state;
  });
}

} // namespace pulseui::core

