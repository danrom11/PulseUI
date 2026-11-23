// Accessibility test (Windows): a real UI Automation client - the API Narrator,
// NVDA and test tools use - reads the element tree of a PulseUI window and
// drives it through patterns (Invoke, Toggle, Value, RangeValue, ExpandCollapse).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <atomic>
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace std::chrono_literals;

namespace {
std::string narrow(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

struct Element {
  std::string name, id;
  long type = 0;
};

template <class T>
void release(T*& p) {
  if (p) p->Release();
  p = nullptr;
}
} // namespace

int main() {
  App app;
  pulse::property<std::string> email{"ada@example.com"};
  pulse::property<bool> remember{false};
  pulse::property<double> volume{0.5};
  pulse::property<int> size{0};
  int saves = 0;

  Window w("Accessibility test", 420, 360);
  w.content(VStack{
      Text("Sign in").title(),
      TextField(email).placeholder("Email").id("email"),
      Checkbox("Remember me", remember).id("remember"),
      Slider(volume).id("volume"),
      Select({"Small", "Large"}, size).id("size"),
      HStack{Button("Save", [&] { ++saves; }).id("save").primary(), Button("\xC3\x97").a11y_label("Close").id("close")},
      Canvas([](Painter&, const Rect&) {}).a11y_label("Chart").a11y_role(Role::Image).height(20),
      Text("decoration").a11y_hidden(),
  }
                .padding(16));
  w.update_layout();
  HWND hwnd = static_cast<HWND>(w.native_handle());

  std::atomic<bool> done{false};
  std::vector<Element> elements;
  std::string email_seen, failure;
  int toggle_state = -1;
  double range_max = -1;

  std::thread client([&] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                reinterpret_cast<void**>(&uia)))) {
      failure = "no UI Automation";
      done = true;
      return;
    }
    IUIAutomationElement* root = nullptr;
    uia->ElementFromHandle(hwnd, &root);
    IUIAutomationCondition* all_cond = nullptr;
    uia->CreateTrueCondition(&all_cond);
    IUIAutomationElementArray* all = nullptr;
    if (root) root->FindAll(TreeScope_Descendants, all_cond, &all);
    int n = 0;
    if (all) all->get_Length(&n);
    std::map<std::string, IUIAutomationElement*> by_id;
    for (int i = 0; i < n; ++i) {
      IUIAutomationElement* e = nullptr;
      all->GetElement(i, &e);
      Element el;
      BSTR b = nullptr;
      if (SUCCEEDED(e->get_CurrentName(&b)) && b) el.name = narrow(b);
      SysFreeString(b);
      b = nullptr;
      if (SUCCEEDED(e->get_CurrentAutomationId(&b)) && b) el.id = narrow(b);
      SysFreeString(b);
      CONTROLTYPEID t = 0;
      e->get_CurrentControlType(&t);
      el.type = t;
      elements.push_back(el);
      if (!el.id.empty()) by_id[el.id] = e;
      else e->Release();
    }
    auto pattern = [&](const char* id, PATTERNID pid, REFIID iid) -> IUnknown* {
      auto it = by_id.find(id);
      if (it == by_id.end()) return nullptr;
      IUnknown* p = nullptr;
      it->second->GetCurrentPatternAs(pid, iid, reinterpret_cast<void**>(&p));
      return p;
    };
    if (auto* inv = static_cast<IUIAutomationInvokePattern*>(pattern("save", UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern)))) {
      inv->Invoke();
      inv->Release();
    } else failure += "save has no Invoke; ";
    if (auto* tog = static_cast<IUIAutomationTogglePattern*>(pattern("remember", UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern)))) {
      tog->Toggle();
      ToggleState s = ToggleState_Off;
      tog->get_CurrentToggleState(&s);
      toggle_state = s == ToggleState_On ? 1 : 0;
      tog->Release();
    } else failure += "remember has no Toggle; ";
    if (auto* val = static_cast<IUIAutomationValuePattern*>(pattern("email", UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern)))) {
      BSTR b = nullptr;
      val->get_CurrentValue(&b);
      email_seen = narrow(b);
      SysFreeString(b);
      val->SetValue(const_cast<BSTR>(L"grace@example.com"));
      val->Release();
    } else failure += "email has no Value; ";
    if (auto* rng = static_cast<IUIAutomationRangeValuePattern*>(pattern("volume", UIA_RangeValuePatternId, __uuidof(IUIAutomationRangeValuePattern)))) {
      rng->get_CurrentMaximum(&range_max);
      rng->SetValue(0.8);
      rng->Release();
    } else failure += "volume has no RangeValue; ";
    if (auto* val = static_cast<IUIAutomationValuePattern*>(pattern("size", UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern)))) {
      val->SetValue(const_cast<BSTR>(L"Large"));
      val->Release();
    } else failure += "size has no Value; ";
    for (auto& [k, e] : by_id) e->Release();
    release(all);
    release(all_cond);
    release(root);
    release(uia);
    CoUninitialize();
    done = true;
  });
  while (!done) App::process_events(10ms);
  client.join();
  App::process_events(100ms); // Invoke runs after the UIA call returns

  auto has = [&](const std::string& name, long type) {
    for (auto& e : elements)
      if (e.name == name && e.type == type) return true;
    return false;
  };
  for (auto& e : elements) std::cout << "  " << e.type << " '" << e.name << "' #" << e.id << "\n";
  if (!failure.empty()) std::cerr << "failure: " << failure << "\n";
  assert(failure.empty());
  assert(has("Sign in", UIA_TextControlTypeId));
  assert(has("Email", UIA_EditControlTypeId));
  assert(has("Remember me", UIA_CheckBoxControlTypeId));
  assert(has("Save", UIA_ButtonControlTypeId));
  assert(has("Close", UIA_ButtonControlTypeId)); // a11y_label
  assert(has("Chart", UIA_ImageControlTypeId));  // a11y_role on a Canvas
  assert(!has("decoration", UIA_TextControlTypeId));
  assert(email_seen == "ada@example.com");
  assert(saves == 1);
  assert(remember.get() && toggle_state == 1);
  assert(email.get() == "grace@example.com");
  assert(range_max == 1 && std::abs(volume.get() - 0.8) < 1e-6);
  assert(size.get() == 1);
  std::cout << "[pulseui_a11y_tests] OK (" << elements.size() << " elements)\n";
  return 0;
}
