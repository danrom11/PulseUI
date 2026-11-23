#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <queue>
#include <unordered_map>

#include <shobjidl.h>

#include "../platform.hpp"
#include "d2d_painter.hpp"
#include "win32_common.hpp"

namespace pulseui::platform {
namespace win32 {

Factories& factories() {
  static Factories f;
  return f;
}

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}

IDWriteTextFormat* text_format(const Font& f) {
  static std::map<std::tuple<std::string, int, int, bool>, Com<IDWriteTextFormat>> cache;
  const auto key = std::make_tuple(f.family, static_cast<int>(f.size * 100),
                                   static_cast<int>(f.weight), f.italic);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second.get();
  std::wstring family = f.family.empty()         ? L"Segoe UI"
                        : f.family == "monospace" ? L"Consolas"
                                                  : widen(f.family);
  Com<IDWriteTextFormat> fmt;
  factories().dwrite->CreateTextFormat(
      family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(static_cast<int>(f.weight)),
      f.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
      f.size, L"", fmt.put());
  auto* raw = fmt.get();
  cache.emplace(key, std::move(fmt));
  return raw;
}

} // namespace win32

using namespace win32;

namespace {

constexpr UINT WM_PULSE_POST = WM_APP + 1;

DWORD g_main_thread = 0;
HWND g_msg_hwnd = nullptr;
std::mutex g_post_mx;
std::queue<std::function<void()>> g_posts;

struct TimerEntry {
  std::function<void()> fn;
  bool repeat;
};
// Never destroyed: timers may be cancelled by static objects during shutdown.
std::unordered_map<UINT_PTR, TimerEntry>& g_timers = *new std::unordered_map<UINT_PTR, TimerEntry>();
UINT_PTR g_next_timer = 1;

void drain_posts() {
  for (;;) {
    std::function<void()> fn;
    {
      std::lock_guard<std::mutex> lock(g_post_mx);
      if (g_posts.empty()) return;
      fn = std::move(g_posts.front());
      g_posts.pop();
    }
    if (fn) fn();
  }
}

LRESULT CALLBACK msg_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_PULSE_POST) {
    drain_posts();
    return 0;
  }
  if (m == WM_TIMER) {
    auto it = g_timers.find(static_cast<UINT_PTR>(w));
    if (it == g_timers.end()) {
      KillTimer(h, w);
      return 0;
    }
    auto fn = it->second.fn;
    if (!it->second.repeat) {
      KillTimer(h, w);
      g_timers.erase(it);
    }
    if (fn) fn();
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void enable_dpi_awareness() {
  using SetCtx = BOOL(WINAPI*)(HANDLE);
  if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
    if (auto fn = reinterpret_cast<SetCtx>(
            reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext")))) {
      if (fn(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) // PER_MONITOR_AWARE_V2
        return;
    }
  }
  SetProcessDPIAware();
}

} // namespace

void init() {
  if (g_main_thread) return;
  g_main_thread = GetCurrentThreadId();
  enable_dpi_awareness();
  OleInitialize(nullptr); // COM (single-threaded apartment) + OLE drag and drop

  auto& f = factories();
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                    reinterpret_cast<void**>(f.d2d.put()));
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown**>(f.dwrite.put()));
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                   __uuidof(IWICImagingFactory), reinterpret_cast<void**>(f.wic.put()));

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = msg_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"PulseUIMessageWindow";
  RegisterClassExW(&wc);
  g_msg_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                               wc.hInstance, nullptr);
}

int run() {
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  drain_posts();
  return static_cast<int>(msg.wParam);
}

void run_modal_loop(const std::function<bool()>& done) {
  MSG msg{};
  while (!done()) {
    const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
    if (r <= 0) {
      if (r == 0) PostQuitMessage(static_cast<int>(msg.wParam)); // leave the quit for the main loop
      return;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

std::optional<dialogs::Answer> native_message(NativeWindow*, const dialogs::Message&) { return std::nullopt; }
bool native_message_async(NativeWindow*, const dialogs::Message&, std::function<void(dialogs::Answer)>) { return false; }

void process_events(int timeout_ms) {
  const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(std::max(0, timeout_ms));
  for (;;) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        PostQuitMessage(static_cast<int>(msg.wParam)); // leave it for the real loop
        return;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    const ULONGLONG now = GetTickCount64();
    if (now >= end) return;
    MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(end - now), QS_ALLINPUT);
  }
}

void quit(int code) {
  if (is_main_thread()) PostQuitMessage(code);
  else post([code] { PostQuitMessage(code); });
}

void post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(g_post_mx);
    g_posts.push(std::move(fn));
  }
  if (g_msg_hwnd) PostMessageW(g_msg_hwnd, WM_PULSE_POST, 0, 0);
}

// Windows: the menu bar is drawn by PulseUI so it follows the theme (a native
// Win32 menu bar is always light).
bool set_native_menu(NativeWindow*, const MenuItems&) { return false; }

bool is_main_thread() { return GetCurrentThreadId() == g_main_thread; }

std::uint64_t start_timer(int ms, bool repeat, std::function<void()> fn) {
  const UINT_PTR id = g_next_timer++;
  g_timers[id] = TimerEntry{std::move(fn), repeat};
  SetTimer(g_msg_hwnd, id, static_cast<UINT>(ms < USER_TIMER_MINIMUM ? USER_TIMER_MINIMUM : ms),
           nullptr);
  return id;
}

void stop_timer(std::uint64_t id) {
  KillTimer(g_msg_hwnd, static_cast<UINT_PTR>(id));
  g_timers.erase(static_cast<UINT_PTR>(id));
}

Size measure_text(std::string_view s, const Font& f, float max_width) {
  IDWriteTextFormat* fmt = text_format(f);
  if (!fmt) return {};
  const std::wstring w = widen(s.empty() ? std::string_view("Ag") : s);
  Com<IDWriteTextLayout> layout;
  factories().dwrite->CreateTextLayout(w.c_str(), static_cast<UINT32>(w.size()), fmt,
                                       max_width > 0 ? max_width : 100000.f, 100000.f,
                                       layout.put());
  if (!layout) return {};
  layout->SetWordWrapping(max_width > 0 ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return {s.empty() ? 0.f : m.widthIncludingTrailingWhitespace, m.height};
}

float line_height(const Font& f) { return platform::measure_text("Ag", f, 0).h; }

void set_clipboard(const std::string& s) {
  if (!OpenClipboard(g_msg_hwnd)) return;
  EmptyClipboard();
  const std::wstring w = widen(s);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (mem) {
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    std::copy(w.begin(), w.end(), dst);
    dst[w.size()] = 0;
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
  }
  CloseClipboard();
}

std::string get_clipboard() {
  std::string out;
  if (!OpenClipboard(g_msg_hwnd)) return out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
      out = narrow(p);
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

namespace {
std::optional<std::string> file_dialog(bool save, NativeWindow* parent, const std::string& title,
                                       const std::vector<FileFilter>& filters,
                                       const std::string& default_name) {
  Com<IFileDialog> dlg;
  const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
  if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileDialog),
                              reinterpret_cast<void**>(dlg.put()))))
    return std::nullopt;
  std::vector<std::wstring> store;
  std::vector<COMDLG_FILTERSPEC> specs;
  store.reserve(filters.size() * 2);
  for (auto& f : filters) {
    store.push_back(widen(f.name));
    store.push_back(widen(f.patterns));
  }
  for (std::size_t i = 0; i < filters.size(); ++i)
    specs.push_back({store[i * 2].c_str(), store[i * 2 + 1].c_str()});
  if (!specs.empty()) dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
  dlg->SetTitle(widen(title).c_str());
  if (save) {
    if (!default_name.empty()) dlg->SetFileName(widen(default_name).c_str());
    if (!filters.empty()) {
      auto ext = filters[0].patterns;
      auto dot = ext.find('.');
      auto semi = ext.find(';');
      if (dot != std::string::npos)
        dlg->SetDefaultExtension(widen(ext.substr(dot + 1, semi == std::string::npos ? std::string::npos : semi - dot - 1)).c_str());
    }
  }
  HWND owner = parent ? static_cast<HWND>(parent->handle()) : nullptr;
  if (FAILED(dlg->Show(owner))) return std::nullopt;
  Com<IShellItem> item;
  if (FAILED(dlg->GetResult(item.put()))) return std::nullopt;
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
  std::string out = narrow(path);
  CoTaskMemFree(path);
  return out;
}
} // namespace

std::optional<std::string> open_file_dialog(NativeWindow* parent, const std::string& title,
                                            const std::vector<FileFilter>& filters) {
  return file_dialog(false, parent, title, filters, {});
}

std::optional<std::string> save_file_dialog(NativeWindow* parent, const std::string& title,
                                            const std::vector<FileFilter>& filters,
                                            const std::string& default_name) {
  return file_dialog(true, parent, title, filters, default_name);
}

bool decode_image(const std::string& path, int& w, int& h, std::vector<std::uint8_t>& rgba) {
  auto& wic = factories().wic;
  if (!wic) return false;
  Com<IWICBitmapDecoder> dec;
  if (FAILED(wic->CreateDecoderFromFilename(widen(path).c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, dec.put())))
    return false;
  Com<IWICBitmapFrameDecode> frame;
  if (FAILED(dec->GetFrame(0, frame.put()))) return false;
  Com<IWICFormatConverter> conv;
  wic->CreateFormatConverter(conv.put());
  if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                              nullptr, 0, WICBitmapPaletteTypeCustom)))
    return false;
  UINT uw = 0, uh = 0;
  conv->GetSize(&uw, &uh);
  w = static_cast<int>(uw);
  h = static_cast<int>(uh);
  rgba.resize(static_cast<std::size_t>(w) * h * 4);
  return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(rgba.size()), rgba.data()));
}

bool encode_png(const std::string& path, int w, int h, const std::uint8_t* rgba) {
  auto& wic = factories().wic;
  if (!wic) return false;
  std::vector<std::uint8_t> bgra(static_cast<std::size_t>(w) * h * 4);
  for (std::size_t i = 0; i < bgra.size(); i += 4) {
    bgra[i] = rgba[i + 2];
    bgra[i + 1] = rgba[i + 1];
    bgra[i + 2] = rgba[i];
    bgra[i + 3] = rgba[i + 3];
  }
  Com<IWICStream> stream;
  wic->CreateStream(stream.put());
  if (FAILED(stream->InitializeFromFilename(widen(path).c_str(), GENERIC_WRITE))) return false;
  Com<IWICBitmapEncoder> enc;
  wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, enc.put());
  enc->Initialize(stream.get(), WICBitmapEncoderNoCache);
  Com<IWICBitmapFrameEncode> frame;
  enc->CreateNewFrame(frame.put(), nullptr);
  frame->Initialize(nullptr);
  frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h));
  WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
  frame->SetPixelFormat(&fmt);
  frame->WritePixels(static_cast<UINT>(h), w * 4, static_cast<UINT>(bgra.size()), bgra.data());
  frame->Commit();
  return SUCCEEDED(enc->Commit());
}

bool render_offscreen(int w, int h, float scale, const std::function<void(Painter&)>& paint,
                      std::vector<std::uint8_t>& rgba) {
  auto& f = factories();
  const UINT pw = static_cast<UINT>(w * scale + 0.5f), ph = static_cast<UINT>(h * scale + 0.5f);
  Com<IWICBitmap> bmp;
  if (FAILED(f.wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 bmp.put())))
    return false;
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.f * scale, 96.f * scale);
  Com<ID2D1RenderTarget> rt;
  if (FAILED(f.d2d->CreateWicBitmapRenderTarget(bmp.get(), props, rt.put()))) return false;
  BitmapCache cache;
  rt->BeginDraw();
  rt->Clear(D2D1::ColorF(0, 0, 0, 0));
  {
    D2DPainter p(rt.get(), cache);
    paint(p);
  }
  if (FAILED(rt->EndDraw())) return false;
  WICRect rc{0, 0, static_cast<INT>(pw), static_cast<INT>(ph)};
  std::vector<std::uint8_t> px(static_cast<std::size_t>(pw) * ph * 4);
  bmp->CopyPixels(&rc, pw * 4, static_cast<UINT>(px.size()), px.data());
  rgba.resize(px.size());
  for (std::size_t i = 0; i < px.size(); i += 4) {
    const unsigned a = px[i + 3];
    auto un = [a](unsigned c) { return static_cast<std::uint8_t>(a ? std::min(255u, c * 255 / a) : 0); };
    rgba[i] = un(px[i + 2]);
    rgba[i + 1] = un(px[i + 1]);
    rgba[i + 2] = un(px[i]);
    rgba[i + 3] = static_cast<std::uint8_t>(a);
  }
  return true;
}

} // namespace pulseui::platform
