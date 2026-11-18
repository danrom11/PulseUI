#pragma once
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <pulse/core/observable.hpp>
#include <pulse/core/property.hpp>
#include <pulse/core/subject.hpp>
#include <pulse/core/subscription.hpp>

namespace pulseui {

namespace detail {
template <class X>
concept has_as_observable = requires(const X& x) { x.as_observable(); };
template <class X>
struct is_observable : std::false_type {};
template <class U>
struct is_observable<pulse::observable<U>> : std::true_type {};
} // namespace detail

// Bind<T>: "a T, or something that produces T over time".
//
// Every visual attribute of a view takes a Bind, so the same API accepts
//   Text("static")                         - a plain value
//   Text(name)                             - a pulse::property<std::string>
//   Text(count.map([](int c){ ... }))      - any pulse::observable
//   Text(full_name)                        - a pulse::computed / behavior_subject
// and the view follows the source automatically.
template <class T>
class Bind {
public:
  using value_type = T;

  Bind() = default;
  Bind(T v) : value_(std::move(v)) {}

  // Anything convertible to T (e.g. const char* -> std::string, int -> double).
  template <class U>
    requires(!std::is_same_v<std::decay_t<U>, T> && !std::is_same_v<std::decay_t<U>, Bind> &&
             !detail::has_as_observable<std::decay_t<U>> &&
             !detail::is_observable<std::decay_t<U>>::value && std::is_constructible_v<T, U &&>)
  Bind(U&& v) : value_(T(std::forward<U>(v))) {}

  // Brace lists for container values: Select({"Low", "High"}, ...)
  template <class E>
    requires std::is_constructible_v<T, const E*, const E*>
  Bind(std::initializer_list<E> il) : value_(T(il.begin(), il.end())) {}

  Bind(pulse::observable<T> o) : stream_(std::move(o)) {}

  // observable<U> with U convertible to T
  template <class U>
    requires(!std::is_same_v<U, T> && std::is_convertible_v<U, T>)
  Bind(pulse::observable<U> o)
      : stream_(pulse::observable<T>::create([o](auto on_next, auto on_err, auto on_done) {
          return o.subscribe([on_next](const U& u) { on_next(T(u)); }, on_err, on_done);
        })) {}

  // property / computed / behavior_subject (anything with as_observable()).
  template <class Src>
    requires(detail::has_as_observable<Src>)
  Bind(Src& src) : Bind(src.as_observable()) {}

  bool is_dynamic() const { return stream_.valid(); }
  bool empty() const { return !stream_.valid() && !value_; }
  const std::optional<T>& value() const { return value_; }
  const pulse::observable<T>& stream() const { return stream_; }

  // Calls fn with the static value now, or subscribes to the stream.
  pulse::subscription attach(std::function<void(const T&)> fn) const {
    if (stream_.valid()) return stream_.subscribe(std::move(fn));
    if (value_) fn(*value_);
    return {};
  }

private:
  std::optional<T> value_;
  pulse::observable<T> stream_;
};

// Two-way binding target used by input views (TextField, Checkbox, Slider, ...).
// The view writes user edits into the property and follows external changes.
template <class T>
using Model = pulse::property<T>;

} // namespace pulseui
