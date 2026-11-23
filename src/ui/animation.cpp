#include <pulseui/ui/animation.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace pulseui {

namespace anim {

float ease(Easing e, float t) {
  t = std::clamp(t, 0.f, 1.f);
  switch (e) {
    case Easing::Linear: return t;
    case Easing::EaseIn: return t * t * t;
    case Easing::EaseOut: { const float u = 1 - t; return 1 - u * u * u; }
    case Easing::EaseInOut: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2;
    case Easing::Spring: {
      // a damped oscillation that settles at 1
      if (t >= 1) return 1;
      return 1 - std::exp(-6 * t) * std::cos(10.5f * t);
    }
  }
  return t;
}

namespace {
using Clock = std::chrono::steady_clock;

struct Active {
  Clock::time_point start;
  double duration_ms;
  Easing easing;
  std::function<void(float)> step;
  std::function<void()> done;
  std::shared_ptr<std::atomic<bool>> cancelled;
};

float g_speed = 1;
std::vector<Active>& active() {
  static auto* v = new std::vector<Active>(); // never destroyed (timers may outlive statics)
  return *v;
}
pulse::subscription& ticker() {
  static auto* t = new pulse::subscription();
  return *t;
}

void tick() {
  auto& list = active();
  const auto now = Clock::now();
  // run a snapshot: steps may start or cancel animations
  std::vector<Active> run = list;
  std::vector<std::shared_ptr<std::atomic<bool>>> finished;
  for (auto& a : run) {
    if (a.cancelled->load()) continue;
    const double ms = std::chrono::duration<double, std::milli>(now - a.start).count();
    const float t = a.duration_ms <= 0 ? 1.f : static_cast<float>(std::min(1.0, ms / a.duration_ms));
    if (a.step) a.step(ease(a.easing, t));
    if (t >= 1) {
      a.cancelled->store(true);
      finished.push_back(a.cancelled);
      if (a.done) a.done();
    }
  }
  list.erase(std::remove_if(list.begin(), list.end(), [](const Active& a) { return a.cancelled->load(); }), list.end());
  if (list.empty()) ticker().reset();
}
} // namespace

pulse::subscription run(Transition tr, std::function<void(float)> step, std::function<void()> done) {
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  const double ms = static_cast<double>(tr.duration.count()) * g_speed;
  if (ms <= 0) {
    if (step) step(1.f);
    if (done) done();
    return {};
  }
  active().push_back({Clock::now(), ms, tr.easing, std::move(step), std::move(done), cancelled});
  if (!ticker()) ticker() = App::every(std::chrono::milliseconds(16), [] { tick(); });
  return pulse::subscription([cancelled] { cancelled->store(true); });
}

void set_speed(float f) { g_speed = std::max(0.f, f); }
float speed() { return g_speed; }
bool running() {
  for (auto& a : active())
    if (!a.cancelled->load()) return true;
  return false;
}

} // namespace anim

} // namespace pulseui
