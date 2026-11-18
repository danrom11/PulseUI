#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <string>
#include <string_view>

#include <pulseui/gfx/font.hpp>

namespace pulseui::platform::win32 {

// Minimal COM smart pointer (avoids a WRL dependency for MinGW builds).
template <class T>
class Com {
public:
  Com() = default;
  Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
  Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  Com& operator=(Com o) noexcept { std::swap(p_, o.p_); return *this; }
  ~Com() { reset(); }
  void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
  T* get() const { return p_; }
  T* operator->() const { return p_; }
  T** put() { reset(); return &p_; }
  explicit operator bool() const { return p_ != nullptr; }

private:
  T* p_ = nullptr;
};

struct Factories {
  Com<ID2D1Factory> d2d;
  Com<IDWriteFactory> dwrite;
  Com<IWICImagingFactory> wic;
};
Factories& factories();

std::wstring widen(std::string_view s);
std::string narrow(std::wstring_view s);

IDWriteTextFormat* text_format(const Font& f); // cached, owned by the cache

} // namespace pulseui::platform::win32
