#pragma once
#include <string>
namespace pulseui::core {
  template<class T>
  struct identity_eq {
    bool operator()(const T& a, const T& b) const { return a == b; }
  };
}
