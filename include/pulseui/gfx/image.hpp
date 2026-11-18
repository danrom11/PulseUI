#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pulseui {

// CPU-side RGBA8 image (straight alpha, top-down). Cheap to copy.
class Image {
public:
  Image() = default;
  static Image from_rgba(int w, int h, const std::uint8_t* rgba);
  static Image load(const std::string& path); // decoded by the OS codecs (PNG, JPEG, ...)
  bool save_png(const std::string& path) const;

  bool valid() const { return data_ && data_->w > 0 && data_->h > 0; }
  int width() const { return data_ ? data_->w : 0; }
  int height() const { return data_ ? data_->h : 0; }
  const std::uint8_t* pixels() const { return data_ ? data_->rgba.data() : nullptr; }
  const void* cache_key() const { return data_.get(); }

private:
  struct Data {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
  };
  std::shared_ptr<Data> data_;
};

} // namespace pulseui
