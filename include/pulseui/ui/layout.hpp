#pragma once
#include <vector>
#include <functional>
#include <initializer_list>
#include <pulseui/ui/input.hpp>

namespace pulseui::ui {

struct Constraints { float w, h; }; // we don't use it deeply, we'll leave it for the future

struct Item {
  std::function<void(Rect)> place;

  // one of:
  float fixed_px = -1.f; // >=0 => fixed size along the main axis
  float flex     = 0.f;  // >0 => takes up the remaining (weight)
};

struct LayoutParams {
  float gap = 8.f;      // intervals between children
  float pad = 8.f;      // inner padding of the container
};

// --------- Main logic ---------
inline void layout_row(Rect bounds,
                       const std::vector<Item>& items,
                       const LayoutParams& p = {})
{
  float x = bounds.x + p.pad;
  const float y = bounds.y + p.pad;
  const float inner_h = bounds.h - 2*p.pad;

  // How many pixels will fixed take + how much flex-weight
  float fixed_sum = 0.f;
  float total_flex = 0.f;
  for (auto& it : items) {
    if (it.fixed_px >= 0.f) fixed_sum += it.fixed_px;
    else total_flex += std::max(0.f, it.flex);
  }
  const float gaps_sum = std::max(0, (int)items.size() - 1) * p.gap;
  float free_px = std::max(0.f, (bounds.w - 2*p.pad) - fixed_sum - gaps_sum);
  float flex_unit = (total_flex > 0.f) ? (free_px / total_flex) : 0.f;

  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    float w = (it.fixed_px >= 0.f) ? it.fixed_px : it.flex * flex_unit;
    if (it.place) it.place(Rect{ x, y, w, inner_h });
    x += w;
    if (i + 1 < items.size()) x += p.gap;
  }
}

inline void layout_column(Rect bounds,
                          const std::vector<Item>& items,
                          const LayoutParams& p = {})
{
  float y = bounds.y + p.pad;
  const float x = bounds.x + p.pad;
  const float inner_w = bounds.w - 2*p.pad;

  float fixed_sum = 0.f;
  float total_flex = 0.f;
  for (auto& it : items) {
    if (it.fixed_px >= 0.f) fixed_sum += it.fixed_px;
    else total_flex += std::max(0.f, it.flex);
  }
  const float gaps_sum = std::max(0, (int)items.size() - 1) * p.gap;
  float free_px = std::max(0.f, (bounds.h - 2*p.pad) - fixed_sum - gaps_sum);
  float flex_unit = (total_flex > 0.f) ? (free_px / total_flex) : 0.f;

  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    float h = (it.fixed_px >= 0.f) ? it.fixed_px : it.flex * flex_unit;
    if (it.place) it.place(Rect{ x, y, inner_w, h });
    y += h;
    if (i + 1 < items.size()) y += p.gap;
  }
}

inline Item Fixed(float px, std::function<void(Rect)> place) {
  Item it; it.place = std::move(place); it.fixed_px = px; it.flex = 0.f; return it;
}
inline Item Expand(float flex, std::function<void(Rect)> place) {
  Item it; it.place = std::move(place); it.fixed_px = -1.f; it.flex = flex; return it;
}

} // namespace pulseui::ui
