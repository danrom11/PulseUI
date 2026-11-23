// UI Automation: exposes PulseUI views to Narrator, NVDA, JAWS and test tools.
//
// One provider object per exposed view (created on demand, cached per window),
// plus a root provider for the window. Providers keep weak references: once a
// view is gone its provider answers UIA_E_ELEMENTNOTAVAILABLE.
#include "win32_common.hpp" // windows.h first

#if !PULSEUI_HAS_UIA
// Without usable UI Automation headers (old MinGW): no screen reader support.
#include <pulseui/ui/window.hpp>
namespace pulseui::platform::win32 {
void* uia_create(HWND, Window*) { return nullptr; }
LRESULT uia_get_object(void*, HWND, WPARAM, LPARAM) { return 0; }
void uia_focus_changed(void*, View*) {}
void uia_destroy(void*, HWND) {}
} // namespace pulseui::platform::win32
#else

#include <ole2.h>
#include <uiautomation.h>

#include <unordered_map>

#include <pulseui/ui/window.hpp>

#include "../platform.hpp"

namespace pulseui {
namespace detail {
void a11y_children(const View* v, std::vector<View*>& out);
void a11y_window_children(const Window& w, std::vector<View*>& out);
View* a11y_parent(const View* v);
View* a11y_exposed(View* v);
} // namespace detail
} // namespace pulseui

namespace pulseui::platform::win32 {

namespace {

UINT dpi_of(HWND h) {
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  return fn && h ? fn(h) : 96;
}

long control_type(Role r) {
  switch (r) {
    case Role::Window: return UIA_WindowControlTypeId;
    case Role::Text:
    case Role::Heading:
    case Role::Alert: return UIA_TextControlTypeId;
    case Role::Button: return UIA_ButtonControlTypeId;
    case Role::Link: return UIA_HyperlinkControlTypeId;
    case Role::CheckBox:
    case Role::Switch: return UIA_CheckBoxControlTypeId;
    case Role::RadioButton: return UIA_RadioButtonControlTypeId;
    case Role::Edit: return UIA_EditControlTypeId;
    case Role::Document: return UIA_DocumentControlTypeId;
    case Role::Slider: return UIA_SliderControlTypeId;
    case Role::Spinner: return UIA_SpinnerControlTypeId;
    case Role::ProgressBar: return UIA_ProgressBarControlTypeId;
    case Role::ComboBox: return UIA_ComboBoxControlTypeId;
    case Role::List: return UIA_ListControlTypeId;
    case Role::ListItem: return UIA_ListItemControlTypeId;
    case Role::Table: return UIA_DataGridControlTypeId;
    case Role::Tree: return UIA_TreeControlTypeId;
    case Role::TreeItem: return UIA_TreeItemControlTypeId;
    case Role::Tab: return UIA_TabControlTypeId;
    case Role::TabItem: return UIA_TabItemControlTypeId;
    case Role::Menu: return UIA_MenuControlTypeId;
    case Role::MenuBar: return UIA_MenuBarControlTypeId;
    case Role::MenuItem: return UIA_MenuItemControlTypeId;
    case Role::Image: return UIA_ImageControlTypeId;
    case Role::Calendar: return UIA_CalendarControlTypeId;
    case Role::Toolbar: return UIA_ToolBarControlTypeId;
    case Role::Separator: return UIA_SeparatorControlTypeId;
    case Role::Pane: return UIA_PaneControlTypeId;
    default: return UIA_GroupControlTypeId;
  }
}

BSTR bstr(const std::string& s) {
  const std::wstring w = widen(s);
  return SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
}

void set_bstr(VARIANT* v, const std::string& s) {
  v->vt = VT_BSTR;
  v->bstrVal = bstr(s);
}
void set_bool(VARIANT* v, bool b) {
  v->vt = VT_BOOL;
  v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
}
void set_i4(VARIANT* v, long i) {
  v->vt = VT_I4;
  v->lVal = i;
}

class ViewProvider;
class RootProvider;
struct Context;
// The provider of a view (AddRef'd; the window root for null), created on demand.
IRawElementProviderSimple* provider(const std::shared_ptr<Context>& ctx, View* v);

// Per-window state shared by all providers of the window.
struct Context {
  HWND hwnd = nullptr;
  Window* window = nullptr;          // null once the window is gone
  std::weak_ptr<bool> window_alive;
  RootProvider* root = nullptr;      // owned (one reference)
  std::unordered_map<const View*, ViewProvider*> cache;
  int next_id = 1;

  bool alive() const { return window && !window_alive.expired(); }
  IRawElementProviderSimple* provider_for(View* v); // AddRef'd; root for null
};

// ═════════════════════════════════════════════════════════════════════════════
class ViewProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IInvokeProvider,
                           public IToggleProvider,
                           public IValueProvider,
                           public IRangeValueProvider,
                           public IExpandCollapseProvider {
public:
  ViewProvider(std::shared_ptr<Context> ctx, View* v) : ctx_(std::move(ctx)), view_(v), alive_(v->alive_token()) {
    id_ = ctx_->next_id++;
  }
  ~ViewProvider() {
    auto it = ctx_->cache.find(view_);
    if (it != ctx_->cache.end() && it->second == this) ctx_->cache.erase(it);
  }
  View* view() const { return !alive_.expired() && ctx_->alive() ? view_ : nullptr; }

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
    else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
    else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
    else if (riid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
    else if (riid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
    else {
      *out = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return static_cast<ULONG>(r);
  }

  // IRawElementProviderSimple
  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
    *o = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
    *out = nullptr;
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    const A11yInfo i = v->a11y_info();
    bool ok = false;
    switch (pattern) {
      case UIA_InvokePatternId:
        ok = i.checked < 0 && (i.role == Role::Button || i.role == Role::Link || i.role == Role::MenuItem ||
                               i.role == Role::ListItem || i.role == Role::TabItem);
        break;
      case UIA_TogglePatternId: ok = i.checked >= 0; break;
      case UIA_ValuePatternId:
        ok = i.role == Role::Edit || i.role == Role::Document || i.role == Role::ComboBox || i.role == Role::Spinner ||
             i.role == Role::Calendar || ((i.role == Role::List || i.role == Role::Tab || i.role == Role::Tree || i.role == Role::Table) && !i.value.empty()) ||
             (i.role == Role::Group && !i.value.empty());
        break;
      case UIA_RangeValuePatternId: ok = i.has_range; break;
      case UIA_ExpandCollapsePatternId: ok = i.expanded >= 0; break;
      default: break;
    }
    if (ok) {
      QueryInterface(pattern == UIA_InvokePatternId       ? __uuidof(IInvokeProvider)
                     : pattern == UIA_TogglePatternId     ? __uuidof(IToggleProvider)
                     : pattern == UIA_ValuePatternId      ? __uuidof(IValueProvider)
                     : pattern == UIA_RangeValuePatternId ? __uuidof(IRangeValueProvider)
                                                          : __uuidof(IExpandCollapseProvider),
                     reinterpret_cast<void**>(out));
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID prop, VARIANT* out) override {
    out->vt = VT_EMPTY;
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    const A11yInfo i = v->a11y_info();
    switch (prop) {
      case UIA_ControlTypePropertyId: set_i4(out, control_type(i.role)); break;
      case UIA_NamePropertyId:
        if (!i.name.empty()) set_bstr(out, i.name);
        break;
      case UIA_AutomationIdPropertyId:
        if (!v->id().empty()) set_bstr(out, v->id());
        break;
      case UIA_ClassNamePropertyId: set_bstr(out, v->type_name()); break;
      case UIA_FrameworkIdPropertyId: set_bstr(out, "PulseUI"); break;
      case UIA_HelpTextPropertyId:
        if (!i.description.empty()) set_bstr(out, i.description);
        break;
      case UIA_IsEnabledPropertyId: set_bool(out, i.enabled); break;
      case UIA_IsKeyboardFocusablePropertyId: set_bool(out, i.focusable && i.enabled); break;
      case UIA_HasKeyboardFocusPropertyId: set_bool(out, i.focused); break;
      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId: set_bool(out, true); break;
      case UIA_IsOffscreenPropertyId: set_bool(out, false); break;
      case UIA_IsPasswordPropertyId: set_bool(out, false); break;
      case UIA_LiveSettingPropertyId:
        if (i.role == Role::Alert) set_i4(out, Polite);
        break;
      case UIA_HeadingLevelPropertyId:
        if (i.role == Role::Heading) set_i4(out, HeadingLevel2);
        break;
      default: break;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
    *out = nullptr;
    return S_OK;
  }

  // IRawElementProviderFragment
  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override;
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
    int ids[2] = {UiaAppendRuntimeId, id_};
    SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, 2);
    for (LONG k = 0; k < 2; ++k) SafeArrayPutElement(sa, &k, &ids[k]);
    *out = sa;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override {
    *r = {};
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    const float s = static_cast<float>(dpi_of(ctx_->hwnd)) / 96.f;
    const Point tl = v->to_window({0, 0});
    POINT p{static_cast<LONG>(tl.x * s), static_cast<LONG>(tl.y * s)};
    ClientToScreen(ctx_->hwnd, &p);
    r->left = p.x;
    r->top = p.y;
    r->width = v->frame().w * s;
    r->height = v->frame().h * s;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetFocus() override {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    v->accessibility_action(A11yAction::Focus);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;

  // IInvokeProvider
  HRESULT STDMETHODCALLTYPE Invoke() override { return act(A11yAction::Press); }
  // IToggleProvider
  HRESULT STDMETHODCALLTYPE Toggle() override { return act(A11yAction::Toggle, {}, true); }
  HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* s) override {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    *s = v->a11y_info().checked == 1 ? ToggleState_On : ToggleState_Off;
    return S_OK;
  }
  // IValueProvider
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR val) override { return act(A11yAction::SetValue, narrow(val ? val : L""), true); }
  HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = bstr(v->a11y_info().value);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    const A11yInfo i = v->a11y_info();
    *out = i.read_only || !i.enabled;
    return S_OK;
  }
  // IRangeValueProvider
  HRESULT STDMETHODCALLTYPE SetValue(double val) override {
    return act(A11yAction::SetValue, std::to_string(val), true);
  }
  HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return range(out, [](const A11yInfo& i) { return i.now; }); }
  HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return range(out, [](const A11yInfo& i) { return i.max; }); }
  HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return range(out, [](const A11yInfo& i) { return i.min; }); }
  HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
    return range(out, [](const A11yInfo& i) { return i.step * 10; });
  }
  HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override { return range(out, [](const A11yInfo& i) { return i.step; }); }
  // IExpandCollapseProvider
  HRESULT STDMETHODCALLTYPE Expand() override { return act(A11yAction::Expand); }
  HRESULT STDMETHODCALLTYPE Collapse() override { return act(A11yAction::Collapse); }
  HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* s) override {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    *s = v->a11y_info().expanded == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
  }

private:
  // Runs the action. Commands that may open dialogs run after the UIA call returns.
  HRESULT act(A11yAction a, std::string arg = {}, bool now = false) {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!v->is_enabled()) return UIA_E_ELEMENTNOTENABLED;
    if (now) return v->accessibility_action(a, arg) ? S_OK : UIA_E_INVALIDOPERATION;
    std::weak_ptr<bool> alive = alive_;
    View* raw = view_;
    post([alive, raw, a, arg] {
      if (!alive.expired()) raw->accessibility_action(a, arg);
    });
    return S_OK;
  }
  template <class F>
  HRESULT range(double* out, F f) {
    View* v = view();
    if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = f(v->a11y_info());
    return S_OK;
  }

  LONG ref_ = 1;
  std::shared_ptr<Context> ctx_;
  View* view_;
  std::weak_ptr<bool> alive_;
  int id_ = 0;
};

// ═════════════════════════════════════════════════════════════════════════════
class RootProvider final : public IRawElementProviderSimple,
                           public IRawElementProviderFragment,
                           public IRawElementProviderFragmentRoot {
public:
  explicit RootProvider(std::shared_ptr<Context> ctx) : ctx_(std::move(ctx)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    else {
      *out = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return static_cast<ULONG>(r);
  }

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
    *o = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override {
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID prop, VARIANT* out) override {
    out->vt = VT_EMPTY;
    if (prop == UIA_FrameworkIdPropertyId) set_bstr(out, "PulseUI");
    return S_OK; // name, type and bounds come from the HWND host provider
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
    if (!ctx_->hwnd) {
      *out = nullptr;
      return S_OK;
    }
    return UiaHostProviderFromHwnd(ctx_->hwnd, out);
  }

  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!ctx_->alive()) return S_OK;
    if (dir != NavigateDirection_FirstChild && dir != NavigateDirection_LastChild) return S_OK;
    std::vector<View*> kids;
    detail::a11y_window_children(*ctx_->window, kids);
    if (kids.empty()) return S_OK;
    View* v = dir == NavigateDirection_FirstChild ? kids.front() : kids.back();
    IRawElementProviderSimple* p = provider(ctx_, v);
    p->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
    p->Release();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
    *out = nullptr; // the host HWND provides it
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override {
    *r = {}; // from the host
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    *out = this;
    AddRef();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!ctx_->alive()) return S_OK;
    POINT p{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(ctx_->hwnd, &p);
    const float s = static_cast<float>(dpi_of(ctx_->hwnd)) / 96.f;
    View* v = detail::a11y_exposed(ctx_->window->view_at({p.x / s, p.y / s}));
    IRawElementProviderSimple* prov = provider(ctx_, v);
    prov->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
    prov->Release();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!ctx_->alive()) return S_OK;
    View* v = detail::a11y_exposed(ctx_->window->focus_view());
    if (!v) return S_OK;
    IRawElementProviderSimple* prov = provider(ctx_, v);
    prov->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
    prov->Release();
    return S_OK;
  }

private:
  LONG ref_ = 1;
  std::shared_ptr<Context> ctx_;
};

IRawElementProviderSimple* Context::provider_for(View* v) {
  if (!v) {
    if (!root) return nullptr;
    root->AddRef();
    return root;
  }
  auto it = cache.find(v);
  if (it != cache.end()) {
    if (it->second->view() == v) {
      it->second->AddRef();
      return it->second;
    }
    cache.erase(it); // a dead view's address was reused
  }
  return nullptr; // filled in by make_provider (needs the shared_ptr)
}

IRawElementProviderSimple* provider(const std::shared_ptr<Context>& ctx, View* v) {
  if (auto* p = ctx->provider_for(v)) return p;
  if (!v) return nullptr;
  auto* p = new ViewProvider(ctx, v); // the one reference goes to the caller
  ctx->cache[v] = p;
  return p;
}

HRESULT STDMETHODCALLTYPE ViewProvider::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) {
  *out = nullptr;
  View* v = view();
  if (!v) return UIA_E_ELEMENTNOTAVAILABLE;
  View* target = nullptr;
  bool to_root = false;
  std::vector<View*> list;
  switch (dir) {
    case NavigateDirection_Parent:
      target = detail::a11y_parent(v);
      to_root = target == nullptr;
      break;
    case NavigateDirection_FirstChild:
    case NavigateDirection_LastChild:
      detail::a11y_children(v, list);
      if (!list.empty()) target = dir == NavigateDirection_FirstChild ? list.front() : list.back();
      break;
    case NavigateDirection_NextSibling:
    case NavigateDirection_PreviousSibling: {
      View* parent = detail::a11y_parent(v);
      if (parent) detail::a11y_children(parent, list);
      else detail::a11y_window_children(*ctx_->window, list);
      for (std::size_t k = 0; k < list.size(); ++k)
        if (list[k] == v) {
          if (dir == NavigateDirection_NextSibling && k + 1 < list.size()) target = list[k + 1];
          if (dir == NavigateDirection_PreviousSibling && k > 0) target = list[k - 1];
          break;
        }
      break;
    }
  }
  if (!target && !to_root) return S_OK;
  IRawElementProviderSimple* p = provider(ctx_, to_root ? nullptr : target);
  if (!p) return S_OK;
  p->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
  p->Release();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE ViewProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** out) {
  *out = ctx_->root;
  if (*out) (*out)->AddRef();
  return S_OK;
}

} // namespace

// ── glue used by Win32Window ─────────────────────────────────────────────────

struct UiaHost {
  std::shared_ptr<Context> ctx;
};

void* uia_create(HWND hwnd, Window* owner) {
  auto* host = new UiaHost();
  host->ctx = std::make_shared<Context>();
  host->ctx->hwnd = hwnd;
  host->ctx->window = owner;
  host->ctx->window_alive = owner->alive_token();
  host->ctx->root = new RootProvider(host->ctx);
  return host;
}

LRESULT uia_get_object(void* h, HWND hwnd, WPARAM w, LPARAM l) {
  auto* host = static_cast<UiaHost*>(h);
  if (!host || !host->ctx->alive()) return 0;
  return UiaReturnRawElementProvider(hwnd, w, l, host->ctx->root);
}

void uia_focus_changed(void* h, View* v) {
  auto* host = static_cast<UiaHost*>(h);
  if (!host || !host->ctx->alive() || !UiaClientsAreListening()) return;
  View* exposed = detail::a11y_exposed(v);
  if (!exposed) return;
  IRawElementProviderSimple* p = provider(host->ctx, exposed);
  UiaRaiseAutomationEvent(p, UIA_AutomationFocusChangedEventId);
  p->Release();
}

void uia_destroy(void* h, HWND hwnd) {
  auto* host = static_cast<UiaHost*>(h);
  if (!host) return;
  host->ctx->window = nullptr;
  if (hwnd) UiaReturnRawElementProvider(hwnd, 0, 0, nullptr); // releases what UIA holds for the window
  // Providers still held by clients answer "not available" from now on. The
  // root lives on while UIA references it (it keeps the context alive).
  RootProvider* root = host->ctx->root;
  host->ctx->root = nullptr;
  if (root) root->Release();
  delete host;
}

} // namespace pulseui::platform::win32
#endif // PULSEUI_HAS_UIA
