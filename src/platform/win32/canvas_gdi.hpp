#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <memory>
#include <pulseui/ui/canvas.hpp>

namespace pulseui::platform {

inline std::wstring utf8_to_utf16(std::string_view s) {
  if (s.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out(len, L'\0');
  if (len) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
  return out;
}

class GdiCanvas final : public ui::Canvas {
public:
  explicit GdiCanvas(HDC hdc) : hdc_(hdc) {
    SetBkMode(hdc_, TRANSPARENT);
  }

  void clear(ui::Color c) override {
    RECT rc;
    GetClipBox(hdc_, &rc);
    HBRUSH brush = CreateSolidBrush(to_colorref(c));
    FillRect(hdc_, &rc, brush);
    DeleteObject(brush);
  }

  void fill_rect(ui::Rect r, ui::Color c) override {
    RECT rc{ (LONG)r.x, (LONG)r.y, (LONG)(r.x + r.w), (LONG)(r.y + r.h) };
    HBRUSH brush = CreateSolidBrush(to_colorref(c));
    FillRect(hdc_, &rc, brush);
    DeleteObject(brush);
  }

  void draw_text(ui::Point p,
                 std::string_view text,
                 const ui::Font& f,
                 ui::Color c) override
  {
    HFONT font = CreateFontW(
      -(int)(f.size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc_, font);

    SetTextColor(hdc_, to_colorref(c));
    auto w = utf8_to_utf16(text);
    TextOutW(hdc_, (int)p.x, (int)p.y, w.c_str(), (int)w.size());

    SelectObject(hdc_, old);
    DeleteObject(font);
  }

  float text_width(std::string_view text, const ui::Font& f) const override {
    HFONT font = CreateFontW(
      -(int)(f.size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc_, font);

    SIZE sz{0,0};
    auto w = utf8_to_utf16(text);
    GetTextExtentPoint32W(hdc_, w.c_str(), (int)w.size(), &sz);

    SelectObject(hdc_, old);
    DeleteObject(font);
    return (float)sz.cx;
  }

  Rect bounds() const override {
    RECT rc{};
    GetClipBox(hdc_, &rc);
    return ui::Rect{ (float)rc.left, (float)rc.top,
                     (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
  }

private:
  static COLORREF to_colorref(const ui::Color& c) {
    auto clamp = [](float v){ return (BYTE)(v < 0 ? 0 : v > 1 ? 255 : (int)(v*255 + 0.5f)); };
    return RGB(clamp(c.r), clamp(c.g), clamp(c.b));
  }

  HDC hdc_{};
};

} // namespace pulseui::platform
