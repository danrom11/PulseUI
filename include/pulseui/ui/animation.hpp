#pragma once
// Animation: smooth changes instead of jumps.
//
//   Panel{...}.animate()                       // frame, opacity, background and border glide (200 ms)
//   Text("Saved").opacity(visible.map(...)).animate(300ms, Easing::EaseInOut)
//   ProgressBar(tween(progress, 400ms))        // a stream that glides to each new value
//   Show(expanded, details).transition()      // fades and grows/shrinks instead of popping
//
// Animations run on the UI thread from one shared frame timer (about 60 fps)
// that only exists while something is animating. anim::set_speed(0) turns them
// off (everything jumps to its final state) - handy in tests.
#include <chrono>
#include <functional>
#include <memory>

#include <pulse/core/observable.hpp>
#include <pulse/core/subscription.hpp>
#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/ui/bind.hpp>

namespace pulseui {

enum class Easing { Linear, EaseIn, EaseOut, EaseInOut, Spring };

struct Transition {
  std::chrono::milliseconds duration{200};
  Easing easing = Easing::EaseOut;
  Transition() = default;
  Transition(std::chrono::milliseconds d, Easing e = Easing::EaseOut) : duration(d), easing(e) {}
};

namespace anim {

float ease(Easing e, float t);

// Calls step(eased progress 0..1) every frame for tr.duration, then done().
// The animation stops early when the returned subscription is reset or destroyed.
pulse::subscription run(Transition tr, std::function<void(float)> step, std::function<void()> done = {});

// 1 = normal, 2 = twice as slow, 0 = no animations (jump to the end).
void set_speed(float factor);
float speed();
bool running(); // anything animating right now

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline double lerp(double a, double b, float t) { return a + (b - a) * t; }
inline Color lerp(Color a, Color b, float t) { return Color::mix(a, b, t); }
inline Point lerp(Point a, Point b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
inline Size lerp(Size a, Size b, float t) { return {lerp(a.w, b.w, t), lerp(a.h, b.h, t)}; }
inline Rect lerp(const Rect& a, const Rect& b, float t) {
  return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.w, b.w, t), lerp(a.h, b.h, t)};
}

} // namespace anim

namespace detail {
template <class T>
pulse::observable<T> tween_impl(Bind<T> target, Transition tr) {
  return pulse::observable<T>::create([target, tr](auto on_next, auto, auto) {
    struct State {
      T current{};
      bool has = false;
      pulse::subscription anim;
    };
    auto st = std::make_shared<State>();
    auto src = target.attach([st, tr, on_next](const T& v) {
      if (!st->has) {
        st->has = true;
        st->current = v;
        on_next(v);
        return;
      }
      const T from = st->current;
      std::weak_ptr<State> weak = st;
      st->anim = anim::run(tr, [weak, from, v, on_next](float t) {
        auto s = weak.lock();
        if (!s) return;
        s->current = static_cast<T>(anim::lerp(from, v, t));
        on_next(s->current);
      });
    });
    auto holder = std::make_shared<pulse::subscription>(std::move(src));
    return pulse::subscription([st, holder] {
      st->anim.reset();
      holder->reset();
    });
  });
}
} // namespace detail

// A stream that follows a property / stream, gliding from the current value to
// every new one (the first value comes through as is). Works for double, float,
// Color, Point, Size and Rect.
//   ProgressBar(tween(progress, 400ms)), Text("x").color(tween(status_color))
template <class Src>
  requires detail::has_as_observable<Src>
auto tween(Src& source, Transition tr = {}) {
  using T = typename Src::value_type;
  return detail::tween_impl<T>(Bind<T>(source), tr);
}
template <class T>
pulse::observable<T> tween(pulse::observable<T> source, Transition tr = {}) {
  return detail::tween_impl<T>(Bind<T>(std::move(source)), tr);
}

} // namespace pulseui
