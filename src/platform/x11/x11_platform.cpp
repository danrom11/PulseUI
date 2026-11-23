// X11 backend: connection, event loop, timers, posting, clipboard.
#include "x11_common.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "../platform.hpp"

namespace pulseui::platform {

namespace {

using Clock = std::chrono::steady_clock;

bool g_inited = false;
std::thread::id g_main_thread;
Display* g_dpy = nullptr;
x11::Atoms g_atoms{};
XWindow g_helper = 0;
XIM g_xim = nullptr;
Time g_user_time = 0;
float g_scale = 1;
int g_input_blocked = 0;

int g_wake[2] = {-1, -1};
std::mutex g_post_mx;
std::queue<std::function<void()>> g_posts;

std::atomic<bool> g_quit{false};
std::atomic<int> g_exit_code{0};

struct Timer {
  Clock::time_point due;
  int interval_ms;
  bool repeat;
  std::function<void()> fn;
};
// Never destroyed: timers may be cancelled by static objects during shutdown.
std::map<std::uint64_t, Timer>& g_timers = *new std::map<std::uint64_t, Timer>();
std::uint64_t g_next_timer = 1;

// Clipboard (CLIPBOARD selection, owned by the helper window)
std::string g_clip_text;
bool g_clip_owned = false;

int x_error_handler(Display* d, XErrorEvent* e) {
  // Races with the window manager (a window already gone, ...) are expected;
  // the default handler would exit the process.
  if (std::getenv("PULSEUI_X11_DEBUG")) {
    char buf[256] = {};
    XGetErrorText(d, e->error_code, buf, sizeof buf);
    std::fprintf(stderr, "PulseUI: X error %s (request %d)\n", buf, e->request_code);
  }
  return 0;
}

float read_scale(Display* dpy) {
  if (const char* s = std::getenv("PULSEUI_SCALE")) {
    const float v = std::strtof(s, nullptr);
    if (v >= 0.5f && v <= 4.f) return v;
  }
  float scale = 1;
  if (dpy) {
    // Xft.dpi is what desktops set for HiDPI (192 = 2x); GDK_SCALE multiplies on top
    if (const char* rms = XResourceManagerString(dpy)) {
      XrmInitialize();
      if (XrmDatabase db = XrmGetStringDatabase(rms)) {
        char* type = nullptr;
        XrmValue v{};
        if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &v) && v.addr) {
          const float dpi = std::strtof(v.addr, nullptr);
          if (dpi >= 48 && dpi <= 480) scale = dpi / 96.f;
        }
        XrmDestroyDatabase(db);
      }
    }
  }
  if (const char* g = std::getenv("GDK_SCALE")) {
    const int k = std::atoi(g);
    if (k >= 1 && k <= 4) scale *= static_cast<float>(k);
  }
  return std::clamp(scale, 0.5f, 4.f);
}

void open_display() {
  // XIM and Xutf8LookupString need a UTF-8 aware LC_CTYPE
  const char* ctype = std::setlocale(LC_CTYPE, nullptr);
  if (!ctype || std::strcmp(ctype, "C") == 0 || std::strcmp(ctype, "POSIX") == 0) {
    if (!std::setlocale(LC_CTYPE, "") || std::strcmp(std::setlocale(LC_CTYPE, nullptr), "C") == 0)
      std::setlocale(LC_CTYPE, "C.UTF-8");
  }
  XInitThreads();
  g_dpy = XOpenDisplay(nullptr);
  if (!g_dpy) return;
  // programs started by the application must not inherit the connection
  ::fcntl(ConnectionNumber(g_dpy), F_SETFD, ::fcntl(ConnectionNumber(g_dpy), F_GETFD) | FD_CLOEXEC);
  XSetErrorHandler(x_error_handler);

  struct {
    Atom* slot;
    const char* name;
  } names[] = {
      {&g_atoms.wm_protocols, "WM_PROTOCOLS"},
      {&g_atoms.wm_delete_window, "WM_DELETE_WINDOW"},
      {&g_atoms.net_wm_ping, "_NET_WM_PING"},
      {&g_atoms.net_wm_name, "_NET_WM_NAME"},
      {&g_atoms.net_wm_pid, "_NET_WM_PID"},
      {&g_atoms.net_wm_state, "_NET_WM_STATE"},
      {&g_atoms.net_wm_state_modal, "_NET_WM_STATE_MODAL"},
      {&g_atoms.net_wm_window_type, "_NET_WM_WINDOW_TYPE"},
      {&g_atoms.net_wm_window_type_dialog, "_NET_WM_WINDOW_TYPE_DIALOG"},
      {&g_atoms.net_wm_window_type_normal, "_NET_WM_WINDOW_TYPE_NORMAL"},
      {&g_atoms.net_active_window, "_NET_ACTIVE_WINDOW"},
      {&g_atoms.utf8_string, "UTF8_STRING"},
      {&g_atoms.clipboard, "CLIPBOARD"},
      {&g_atoms.targets, "TARGETS"},
      {&g_atoms.text, "TEXT"},
      {&g_atoms.text_plain_utf8, "text/plain;charset=utf-8"},
      {&g_atoms.incr, "INCR"},
      {&g_atoms.pulse_selection, "PULSEUI_SELECTION"},
      {&g_atoms.net_wm_user_time, "_NET_WM_USER_TIME"},
  };
  for (auto& n : names) *n.slot = XInternAtom(g_dpy, n.name, 0);

  int supported = 0;
  XkbSetDetectableAutoRepeat(g_dpy, 1, &supported); // repeats arrive as presses only

  XSetWindowAttributes a{};
  a.event_mask = PropertyChangeMask;
  g_helper = XCreateWindow(g_dpy, DefaultRootWindow(g_dpy), -10, -10, 1, 1, 0, 0, InputOnly, CopyFromParent,
                           CWEventMask, &a);

  if (XSupportsLocale()) {
    XSetLocaleModifiers("");
    g_xim = XOpenIM(g_dpy, nullptr, nullptr, nullptr);
    if (!g_xim) {
      XSetLocaleModifiers("@im=none");
      g_xim = XOpenIM(g_dpy, nullptr, nullptr, nullptr);
    }
  }
  g_scale = read_scale(g_dpy);
}

void wake() {
  if (g_wake[1] < 0) return;
  const char c = 1;
  [[maybe_unused]] auto n = ::write(g_wake[1], &c, 1); // a full pipe already wakes the loop
}

void drain_wake() {
  char buf[64];
  while (::read(g_wake[0], buf, sizeof buf) > 0) {
  }
}

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

void run_due_timers() {
  const auto now = Clock::now();
  std::vector<std::uint64_t> due;
  for (auto& [id, t] : g_timers)
    if (t.due <= now) due.push_back(id);
  std::sort(due.begin(), due.end(), [](std::uint64_t a, std::uint64_t b) { return g_timers[a].due < g_timers[b].due; });
  for (std::uint64_t id : due) {
    auto it = g_timers.find(id); // an earlier callback may have stopped it
    if (it == g_timers.end()) continue;
    std::function<void()> fn = it->second.fn;
    if (it->second.repeat) {
      it->second.due += std::chrono::milliseconds(it->second.interval_ms);
      if (it->second.due <= now) it->second.due = now + std::chrono::milliseconds(it->second.interval_ms);
    } else {
      g_timers.erase(it);
    }
    if (fn) fn();
  }
}

// Milliseconds until the next timer (-1: none).
int next_timer_ms() {
  if (g_timers.empty()) return -1;
  auto next = Clock::time_point::max();
  for (auto& [id, t] : g_timers) next = std::min(next, t.due);
  const auto d = std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
  return d <= 0 ? 0 : static_cast<int>(std::min<long long>(d + 1, 1 << 30));
}

void dispatch_x_events() {
  if (!g_dpy) return;
  while (XPending(g_dpy)) {
    XEvent e;
    XNextEvent(g_dpy, &e);
    if (XFilterEvent(&e, 0)) continue; // consumed by the input method
    if (e.xany.window == g_helper) {
      x11::handle_selection_event(e);
      continue;
    }
    x11::dispatch_window_event(e);
  }
}

void dispatch_ready() {
  dispatch_x_events();
  drain_posts();
  run_due_timers();
  drain_posts();
  x11::paint_pending_windows();
}

// One turn of the loop: dispatch what is ready, then wait for more for up to
// `timeout_ms` (-1: until something happens).
void iterate(int timeout_ms) {
  dispatch_ready();
  if (g_quit) return;
  int wait = timeout_ms;
  const int t = next_timer_ms();
  if (t >= 0) wait = wait < 0 ? t : std::min(wait, t);
  {
    std::lock_guard<std::mutex> lock(g_post_mx);
    if (!g_posts.empty()) wait = 0;
  }
  if (g_dpy) {
    XFlush(g_dpy);
    if (XPending(g_dpy)) wait = 0;
  }
  pollfd fds[2] = {{g_wake[0], POLLIN, 0}, {g_dpy ? ConnectionNumber(g_dpy) : -1, POLLIN, 0}};
  if (wait != 0) ::poll(fds, g_dpy ? 2 : 1, wait);
  drain_wake();
  dispatch_ready();
  if (g_dpy) XFlush(g_dpy);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// x11:: shared state
// ─────────────────────────────────────────────────────────────────────────────
namespace x11 {
Display* display() { return g_dpy; }
const Atoms& atoms() { return g_atoms; }
XWindow helper_window() { return g_helper; }
XIM input_method() { return g_xim; }
Time last_user_time() { return g_user_time; }
void set_last_user_time(Time t) {
  if (t) g_user_time = t;
}
float screen_scale() { return g_scale; }
void block_input(bool on) { g_input_blocked += on ? 1 : -1; }
bool input_blocked() { return g_input_blocked > 0; }

bool pump_until(const std::function<bool()>& done, int timeout_ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!done() && !g_quit) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
    if (left <= 0) break;
    iterate(static_cast<int>(std::min<long long>(left, 50)));
  }
  return !g_quit;
}
} // namespace x11

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────
void init() {
  if (g_inited) return;
  g_inited = true;
  g_main_thread = std::this_thread::get_id();
  if (::pipe(g_wake) == 0) {
    for (int fd : g_wake) {
      ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
      ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
  }
  open_display();
  if (!g_dpy && !std::getenv("PULSEUI_SCREENSHOT"))
    std::fprintf(stderr, "PulseUI: cannot open the X display (DISPLAY=%s); windows will not be shown\n",
                 std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "");
}

int run() {
  while (!g_quit) iterate(-1);
  drain_posts();
  g_quit = false; // consumed: a later run() starts fresh
  return g_exit_code;
}

void run_modal_loop(const std::function<bool()>& done) {
  while (!done() && !g_quit) iterate(-1);
}

void process_events(int timeout_ms) {
  const auto end = Clock::now() + std::chrono::milliseconds(std::max(0, timeout_ms));
  do {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
    iterate(static_cast<int>(std::max<long long>(0, left)));
  } while (!g_quit && Clock::now() < end);
}

void quit(int code) {
  g_exit_code = code;
  g_quit = true;
  wake();
}

void post(std::function<void()> fn) {
  {
    std::lock_guard<std::mutex> lock(g_post_mx);
    g_posts.push(std::move(fn));
  }
  wake();
}

bool is_main_thread() { return !g_inited || std::this_thread::get_id() == g_main_thread; }

std::uint64_t start_timer(int ms, bool repeat, std::function<void()> fn) {
  const std::uint64_t id = g_next_timer++;
  const int interval = std::max(ms, 1);
  g_timers[id] = Timer{Clock::now() + std::chrono::milliseconds(interval), interval, repeat, std::move(fn)};
  return id;
}

void stop_timer(std::uint64_t id) { g_timers.erase(id); }

// The menu bar and message boxes are drawn by PulseUI (themed, like on Windows).
bool set_native_menu(NativeWindow*, const MenuItems&) { return false; }
std::optional<dialogs::Answer> native_message(NativeWindow*, const dialogs::Message&) { return std::nullopt; }
bool native_message_async(NativeWindow*, const dialogs::Message&, std::function<void(dialogs::Answer)>) { return false; }

// ─────────────────────────────────────────────────────────────────────────────
// Clipboard
// ─────────────────────────────────────────────────────────────────────────────
void set_clipboard(const std::string& s) {
  g_clip_text = s;
  if (!g_dpy) return;
  XSetSelectionOwner(g_dpy, g_atoms.clipboard, g_helper, g_user_time ? g_user_time : 0L);
  g_clip_owned = XGetSelectionOwner(g_dpy, g_atoms.clipboard) == g_helper;
  XFlush(g_dpy);
}

namespace {
struct ClipRead {
  bool done = false;
  bool incr = false;
  std::string data;
};
ClipRead* g_clip_read = nullptr;

std::string read_property(XWindow w, Atom prop, Atom* type_out) {
  std::string out;
  long offset = 0;
  for (;;) {
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(g_dpy, w, prop, offset, 1 << 20, 1 /*delete*/, AnyPropertyType, &type, &format, &n,
                           &after, &data) != 0)
      break;
    if (type_out) *type_out = type;
    if (data) {
      out.append(reinterpret_cast<const char*>(data), n * static_cast<unsigned long>(format / 8));
      XFree(data);
    }
    if (after == 0) break;
    offset += static_cast<long>(n * static_cast<unsigned long>(format / 8) / 4);
  }
  return out;
}
} // namespace

std::string get_clipboard() {
  if (!g_dpy) return g_clip_text;
  const XWindow owner = XGetSelectionOwner(g_dpy, g_atoms.clipboard);
  if (owner == g_helper) return g_clip_text;
  if (owner == 0) return {};
  for (Atom target : {g_atoms.utf8_string, static_cast<Atom>(XA_STRING)}) {
    ClipRead r;
    g_clip_read = &r;
    XDeleteProperty(g_dpy, g_helper, g_atoms.pulse_selection);
    XConvertSelection(g_dpy, g_atoms.clipboard, target, g_atoms.pulse_selection, g_helper,
                      g_user_time ? g_user_time : 0L);
    x11::pump_until([&] { return r.done; }, 2000);
    g_clip_read = nullptr;
    if (!r.done) return {};
    if (!r.data.empty()) return r.data; // Latin-1 from STRING is ASCII in practice
  }
  return {};
}

namespace x11 {
void handle_selection_event(XEvent& e) {
  switch (e.type) {
    case SelectionClear:
      if (e.xselectionclear.selection == g_atoms.clipboard) g_clip_owned = false;
      break;
    case SelectionRequest: {
      const XSelectionRequestEvent& rq = e.xselectionrequest;
      XSelectionEvent reply{};
      reply.type = SelectionNotify;
      reply.display = rq.display;
      reply.requestor = rq.requestor;
      reply.selection = rq.selection;
      reply.target = rq.target;
      reply.time = rq.time;
      reply.property = 0;
      const Atom prop = rq.property ? rq.property : rq.target; // obsolete clients
      if (rq.selection == g_atoms.clipboard && g_clip_owned) {
        if (rq.target == g_atoms.targets) {
          const Atom list[] = {g_atoms.targets, g_atoms.utf8_string, g_atoms.text_plain_utf8, XA_STRING, g_atoms.text};
          XChangeProperty(g_dpy, rq.requestor, prop, XA_ATOM, 32, PropModeReplace,
                          reinterpret_cast<const unsigned char*>(list), 5);
          reply.property = prop;
        } else if (rq.target == g_atoms.utf8_string || rq.target == g_atoms.text_plain_utf8 ||
                   rq.target == XA_STRING || rq.target == g_atoms.text) {
          const Atom type = rq.target == g_atoms.text ? g_atoms.utf8_string : rq.target;
          XChangeProperty(g_dpy, rq.requestor, prop, type, 8, PropModeReplace,
                          reinterpret_cast<const unsigned char*>(g_clip_text.data()),
                          static_cast<int>(g_clip_text.size()));
          reply.property = prop;
        }
      }
      XSendEvent(g_dpy, rq.requestor, 0, 0, reinterpret_cast<XEvent*>(&reply));
      XFlush(g_dpy);
      break;
    }
    case SelectionNotify: {
      if (!g_clip_read || e.xselection.selection != g_atoms.clipboard) break;
      if (e.xselection.property == 0) { // the owner refused this target
        g_clip_read->done = true;
        break;
      }
      Atom type = 0;
      std::string data = read_property(g_helper, g_atoms.pulse_selection, &type);
      if (type == g_atoms.incr) {
        g_clip_read->incr = true; // chunks follow as property changes
      } else {
        g_clip_read->data = std::move(data);
        g_clip_read->done = true;
      }
      break;
    }
    case PropertyNotify: {
      if (!g_clip_read || !g_clip_read->incr || e.xproperty.atom != g_atoms.pulse_selection ||
          e.xproperty.state != PropertyNewValue)
        break;
      std::string chunk = read_property(g_helper, g_atoms.pulse_selection, nullptr);
      if (chunk.empty()) g_clip_read->done = true; // a zero-length chunk ends the transfer
      else g_clip_read->data += chunk;
      break;
    }
    default:
      break;
  }
}
} // namespace x11

} // namespace pulseui::platform
