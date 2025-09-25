#pragma once
#include <vector>
#include <pulse/pulse.hpp>

namespace pulseui::core {
  struct subs_bag {
    std::vector<pulse::subscription> subs;
    void add(pulse::subscription s){ subs.emplace_back(std::move(s)); }
    ~subs_bag(){ for(auto& s: subs) if(s) s.reset(); }
  };
}
