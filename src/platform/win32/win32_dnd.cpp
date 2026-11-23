// OLE drag and drop: files and text into PulseUI windows, and out of them.
#include <shlobj.h>
#include <shellapi.h>

#include <pulseui/ui/window.hpp>

#include "../platform.hpp"
#include "win32_common.hpp"

namespace pulseui::platform {
using namespace win32;

namespace win32 {

namespace {

DragData read_data(IDataObject* obj) {
  DragData d;
  if (!obj) return d;
  FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM m{};
  if (SUCCEEDED(obj->GetData(&files, &m))) {
    if (auto h = static_cast<HDROP>(GlobalLock(m.hGlobal))) {
      const UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < n; ++i) {
        const UINT len = DragQueryFileW(h, i, nullptr, 0);
        std::wstring w(len, L'\0');
        DragQueryFileW(h, i, w.data(), len + 1);
        d.files.push_back(narrow(w));
      }
      GlobalUnlock(m.hGlobal);
    }
    ReleaseStgMedium(&m);
  }
  FORMATETC text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  if (SUCCEEDED(obj->GetData(&text, &m))) {
    if (auto p = static_cast<const wchar_t*>(GlobalLock(m.hGlobal))) {
      d.text = narrow(p);
      GlobalUnlock(m.hGlobal);
    }
    ReleaseStgMedium(&m);
  }
  return d;
}

class DropTarget final : public IDropTarget {
public:
  DropTarget(HWND hwnd, std::function<bool(Window::DragPhase, Point, const DragData&)> fn)
      : hwnd_(hwnd), fn_(std::move(fn)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
      *out = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return static_cast<ULONG>(r);
  }

  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
    data_ = read_data(obj);
    *effect = call(Window::DragPhase::Enter, pt) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
    *effect = call(Window::DragPhase::Over, pt) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DragLeave() override {
    call(Window::DragPhase::Leave, POINTL{0, 0});
    data_ = {};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
    data_ = read_data(obj);
    *effect = call(Window::DragPhase::Drop, pt) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    data_ = {};
    return S_OK;
  }

  void detach() { fn_ = nullptr; }

private:
  bool call(Window::DragPhase phase, POINTL pt) {
    if (!fn_) return false;
    POINT p{pt.x, pt.y};
    ScreenToClient(hwnd_, &p);
    const float s = static_cast<float>(GetDpiForWindowSafe(hwnd_)) / 96.f;
    return fn_(phase, {p.x / s, p.y / s}, data_);
  }
  static UINT GetDpiForWindowSafe(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    return fn ? fn(h) : 96;
  }

  LONG ref_ = 1;
  HWND hwnd_;
  std::function<bool(Window::DragPhase, Point, const DragData&)> fn_;
  DragData data_;
};

class DropSource final : public IDropSource {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *out = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; } // lives on the stack
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
    if (escape) return DRAGDROP_S_CANCEL;
    if (!(keys & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

} // namespace

// Registers `hwnd` as a drop target; the returned object is released with unregister_drop.
void* register_drop(HWND hwnd, std::function<bool(Window::DragPhase, Point, const DragData&)> fn) {
  auto* t = new DropTarget(hwnd, std::move(fn));
  if (FAILED(RegisterDragDrop(hwnd, t))) {
    t->Release();
    return nullptr;
  }
  return t;
}

void unregister_drop(HWND hwnd, void* target) {
  if (!target) return;
  auto* t = static_cast<DropTarget*>(target);
  t->detach();
  if (hwnd) RevokeDragDrop(hwnd);
  t->Release();
}

} // namespace win32

bool start_drag(NativeWindow*, const DragData& data) {
  Com<IDataObject> obj;
  // files: a shell data object (CF_HDROP and friends) for the files of one folder
  std::vector<PIDLIST_ABSOLUTE> owned;
  PIDLIST_ABSOLUTE parent = nullptr;
  std::vector<PCUITEMID_CHILD> children;
  if (!data.files.empty()) {
    std::wstring first = widen(data.files.front());
    std::wstring folder = first.substr(0, first.find_last_of(L"\\/"));
    if (SUCCEEDED(SHParseDisplayName(folder.c_str(), nullptr, &parent, 0, nullptr))) {
      for (auto& f : data.files) {
        std::wstring w = widen(f);
        if (w.substr(0, w.find_last_of(L"\\/")) != folder) continue; // other folders are left out
        PIDLIST_ABSOLUTE abs = nullptr;
        if (SUCCEEDED(SHParseDisplayName(w.c_str(), nullptr, &abs, 0, nullptr))) {
          owned.push_back(abs);
          children.push_back(ILFindLastID(abs));
        }
      }
    }
  }
  HRESULT hr = SHCreateDataObject(parent, static_cast<UINT>(children.size()), children.empty() ? nullptr : children.data(),
                                  nullptr, IID_IDataObject, reinterpret_cast<void**>(obj.put()));
  for (auto p : owned) ILFree(p);
  if (parent) ILFree(parent);
  if (FAILED(hr) || !obj) return false;
  if (!data.text.empty()) {
    const std::wstring w = widen(data.text);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
      auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
      std::copy(w.begin(), w.end(), dst);
      dst[w.size()] = 0;
      GlobalUnlock(mem);
      FORMATETC fmt{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
      STGMEDIUM m{};
      m.tymed = TYMED_HGLOBAL;
      m.hGlobal = mem;
      if (FAILED(obj->SetData(&fmt, &m, TRUE))) GlobalFree(mem);
    }
  }
  DropSource src;
  DWORD effect = DROPEFFECT_NONE;
  hr = DoDragDrop(obj.get(), &src, DROPEFFECT_COPY, &effect);
  return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}

} // namespace pulseui::platform
