#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>

#include <pulseui/gfx/image.hpp>
#include <pulseui/gfx/painter.hpp>

#include "../platform/platform.hpp"

namespace pulseui {

Size measure_text(std::string_view s, const Font& f, float max_width) {
  // Layout measures the same strings over and over; asking the OS text engine
  // every time was the main cost of a relayout.
  static std::unordered_map<std::string, Size> cache;
  std::string key;
  key.reserve(s.size() + f.family.size() + 24);
  key.append(f.family).push_back('\x1f');
  key.append(std::to_string(static_cast<int>(f.size * 64))).push_back('\x1f');
  key.append(std::to_string(static_cast<int>(f.weight) + (f.italic ? 1 : 0))).push_back('\x1f');
  key.append(std::to_string(static_cast<int>(max_width * 4))).push_back('\x1f');
  key.append(s);
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  if (cache.size() > 8192) cache.clear();
  const Size r = platform::measure_text(s, f, max_width);
  cache.emplace(std::move(key), r);
  return r;
}

float line_height(const Font& f) {
  static std::map<std::tuple<std::string, float, int>, float> cache;
  const auto key = std::make_tuple(f.family, f.size, static_cast<int>(f.weight));
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;
  const float h = platform::line_height(f);
  cache.emplace(key, h);
  return h;
}

std::vector<float> caret_stops(std::string_view s, const Font& f) {
  std::vector<float> out;
  out.reserve(s.size() + 1);
  out.push_back(0);
  for (std::size_t i = 1; i <= s.size(); ++i) {
    if (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue; // mid code point
    out.push_back(measure_text(s.substr(0, i), f, 0).w);
  }
  return out;
}

Image Image::from_rgba(int w, int h, const std::uint8_t* rgba) {
  Image img;
  img.data_ = std::make_shared<Data>();
  img.data_->w = w;
  img.data_->h = h;
  img.data_->rgba.assign(rgba, rgba + static_cast<std::size_t>(w) * h * 4);
  return img;
}

Image Image::load(const std::string& path) {
  Image img;
  auto d = std::make_shared<Data>();
  if (platform::decode_image(path, d->w, d->h, d->rgba)) img.data_ = std::move(d);
  return img;
}

bool Image::save_png(const std::string& path) const {
  return valid() && platform::encode_png(path, data_->w, data_->h, data_->rgba.data());
}

} // namespace pulseui
