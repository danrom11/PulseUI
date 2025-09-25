#pragma once

#include <windows.h>
#include <string>
#include <string_view>
#include <cmath>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::platform {

inline std::wstring widen_utf8(std::string_view s) {
  if (s.empty()) return std::wstring();
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(len, L'\0');
  if (len > 0) {
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
  }
  return w;
}

inline LONG to_LONG(float v) { return (LONG)std::lroundf(v); }
inline LONG to_LONG(int   v) { return (LONG)v; }

inline COLORREF to_colorref(ui::Color c) {
  auto clamp = [](float x) {
    if (x < 0.f) return 0.f;
    if (x > 1.f) return 1.f;
    return x;
  };
  const BYTE R = (BYTE)(clamp(c.r) * 255.0f + 0.5f);
  const BYTE G = (BYTE)(clamp(c.g) * 255.0f + 0.5f);
  const BYTE B = (BYTE)(clamp(c.b) * 255.0f + 0.5f);
  return RGB(R, G, B);
}

class GdiCanvas : public ui::Canvas {
public:
  explicit GdiCanvas(HDC hdc) : hdc_(hdc) {}

  void clear(ui::Color c) override {
    RECT rc{};
    GetClipBox(hdc_, &rc);
    HBRUSH br = CreateSolidBrush(to_colorref(c));
    FillRect(hdc_, &rc, br);
    DeleteObject(br);
  }

  void fill_rect(ui::Rect r, ui::Color c) override {
    RECT rc{
      to_LONG(r.x),
      to_LONG(r.y),
      to_LONG(r.x + r.w),
      to_LONG(r.y + r.h)
    };
    HBRUSH br = CreateSolidBrush(to_colorref(c));
    FillRect(hdc_, &rc, br);
    DeleteObject(br);
  }

  void draw_text(ui::Point p,
                 std::string_view utf8,
                 const ui::Font& /*font*/,
                 ui::Color color) override {
    HFONT oldFont = (HFONT)SelectObject(hdc_, GetStockObject(DEFAULT_GUI_FONT));
    int    oldBk  = SetBkMode(hdc_, TRANSPARENT);
    COLORREF oldTx = SetTextColor(hdc_, to_colorref(color));

    std::wstring w = widen_utf8(utf8);
    if (!w.empty()) {
      TextOutW(hdc_, to_LONG(p.x), to_LONG(p.y), w.c_str(), (int)w.size());
    }

    SetTextColor(hdc_, oldTx);
    SetBkMode(hdc_, oldBk);
    SelectObject(hdc_, oldFont);
  }

  HDC hdc() const { return hdc_; }

private:
  HDC hdc_{nullptr};
};

} // namespace pulseui::platform
