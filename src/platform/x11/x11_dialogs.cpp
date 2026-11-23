// X11 backend: file dialogs. The desktop's own dialog through
// xdg-desktop-portal (D-Bus), else zenity or kdialog when installed.
#include "x11_common.hpp"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#if PULSEUI_HAS_GIO
#include <gio/gio.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "../platform.hpp"

extern char** environ;

namespace pulseui::platform {

namespace {

std::vector<std::string> split_patterns(const std::string& p) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= p.size()) {
    std::size_t end = p.find(';', start);
    std::string item = p.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    while (!item.empty() && item.back() == ' ') item.pop_back();
    if (!item.empty()) out.push_back(item);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

// Blocks the application's windows (they keep repainting) while `wait` runs.
struct InputBlock {
  InputBlock() { x11::block_input(true); }
  ~InputBlock() { x11::block_input(false); }
};

#if PULSEUI_HAS_GIO
// org.freedesktop.portal.FileChooser. nullopt in `available` when there is no
// portal (no session bus, no portal service).
struct PortalResult {
  bool available = false;
  std::optional<std::string> path;
};

PortalResult portal_dialog(bool save, NativeWindow* parent, const std::string& title,
                           const std::vector<FileFilter>& filters, const std::string& default_name) {
  PortalResult result;
  GError* err = nullptr;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
  if (!bus) {
    if (err) g_error_free(err);
    return result;
  }
  GMainContext* ctx = g_main_context_new();
  g_main_context_push_thread_default(ctx);

  static int counter = 0;
  const std::string token = "pulseui" + std::to_string(++counter);
  std::string sender = g_dbus_connection_get_unique_name(bus);
  if (!sender.empty() && sender[0] == ':') sender.erase(0, 1);
  for (auto& c : sender)
    if (c == '.') c = '_';
  const std::string handle = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

  struct State {
    bool done = false;
    std::optional<std::string> path;
  } state;
  const guint sub = g_dbus_connection_signal_subscribe(
      bus, "org.freedesktop.portal.Desktop", "org.freedesktop.portal.Request", "Response", handle.c_str(), nullptr,
      G_DBUS_SIGNAL_FLAGS_NONE,
      [](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer user) {
        auto* st = static_cast<State*>(user);
        guint32 response = 2;
        GVariant* results = nullptr;
        g_variant_get(params, "(u@a{sv})", &response, &results);
        if (response == 0 && results) {
          const gchar** uris = nullptr;
          if (g_variant_lookup(results, "uris", "^a&s", &uris) && uris && uris[0]) {
            if (gchar* p = g_filename_from_uri(uris[0], nullptr, nullptr)) {
              st->path = p;
              g_free(p);
            }
          }
          g_free(uris);
        }
        if (results) g_variant_unref(results);
        st->done = true;
      },
      &state, nullptr);

  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
  g_variant_builder_add(&opts, "{sv}", "modal", g_variant_new_boolean(TRUE));
  if (!filters.empty()) {
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("a(sa(us))"));
    for (auto& f : filters) {
      GVariantBuilder pats;
      g_variant_builder_init(&pats, G_VARIANT_TYPE("a(us)"));
      for (auto& p : split_patterns(f.patterns)) g_variant_builder_add(&pats, "(us)", 0u, p.c_str());
      g_variant_builder_add(&list, "(sa(us))", f.name.c_str(), &pats);
    }
    g_variant_builder_add(&opts, "{sv}", "filters", g_variant_builder_end(&list));
  }
  if (save && !default_name.empty())
    g_variant_builder_add(&opts, "{sv}", "current_name", g_variant_new_string(default_name.c_str()));

  std::string parent_id;
  if (parent && parent->handle()) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "x11:%lx", static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(parent->handle())));
    parent_id = buf;
  }
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser",
      save ? "SaveFile" : "OpenFile", g_variant_new("(ssa{sv})", parent_id.c_str(), title.c_str(), &opts),
      G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &err);
  if (reply) {
    result.available = true;
    g_variant_unref(reply);
    InputBlock block;
    // the application keeps repainting while the portal's dialog is open
    while (!state.done) {
      while (g_main_context_iteration(ctx, FALSE)) {
      }
      if (state.done || !x11::pump_until([&] { return g_main_context_pending(ctx) != 0; }, 30)) break;
    }
    result.path = state.path;
  } else if (err) {
    g_error_free(err);
  }
  g_dbus_connection_signal_unsubscribe(bus, sub);
  g_main_context_pop_thread_default(ctx);
  g_main_context_unref(ctx);
  g_object_unref(bus);
  return result;
}
#endif

bool in_path(const char* exe) {
  const char* path = std::getenv("PATH");
  if (!path) return false;
  std::stringstream ss(path);
  std::string dir;
  while (std::getline(ss, dir, ':'))
    if (!dir.empty() && ::access((dir + "/" + exe).c_str(), X_OK) == 0) return true;
  return false;
}

// Runs a dialog program and returns what it printed (nullopt: cancelled).
std::optional<std::string> run_dialog_program(std::vector<std::string> args) {
  int out[2];
  if (::pipe(out) != 0) return std::nullopt;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, out[1], 1);
  posix_spawn_file_actions_addclose(&fa, out[0]);
  posix_spawn_file_actions_addclose(&fa, out[1]);
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  ::close(out[1]);
  if (rc != 0) {
    ::close(out[0]);
    return std::nullopt;
  }
  ::fcntl(out[0], F_SETFL, ::fcntl(out[0], F_GETFL) | O_NONBLOCK);
  std::string text;
  bool eof = false;
  InputBlock block;
  while (!eof) {
    char buf[512];
    for (;;) {
      const ssize_t n = ::read(out[0], buf, sizeof buf);
      if (n > 0) text.append(buf, static_cast<std::size_t>(n));
      else {
        if (n == 0) eof = true;
        break;
      }
    }
    if (!eof) {
      const bool running = x11::pump_until([&] {
        pollfd q{out[0], POLLIN, 0};
        return ::poll(&q, 1, 0) > 0;
      }, 30);
      if (!running) { // quitting: just wait for the program
        pollfd q{out[0], POLLIN, 0};
        ::poll(&q, 1, -1);
      }
    }
  }
  ::close(out[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::nullopt;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  if (text.empty()) return std::nullopt;
  return text;
}

std::optional<std::string> file_dialog(bool save, NativeWindow* parent, const std::string& title,
                                       const std::vector<FileFilter>& filters, const std::string& default_name) {
#if PULSEUI_HAS_GIO
  if (!std::getenv("PULSEUI_NO_PORTAL")) {
    PortalResult r = portal_dialog(save, parent, title, filters, default_name);
    if (r.available) return r.path;
  }
#endif
  if (in_path("zenity")) {
    std::vector<std::string> a = {"zenity", "--file-selection", "--title=" + title};
    if (save) {
      a.push_back("--save");
      a.push_back("--confirm-overwrite");
      if (!default_name.empty()) a.push_back("--filename=" + default_name);
    }
    for (auto& f : filters) {
      std::string spec = "--file-filter=" + f.name + " |";
      for (auto& p : split_patterns(f.patterns)) spec += " " + p;
      a.push_back(spec);
    }
    return run_dialog_program(std::move(a));
  }
  if (in_path("kdialog")) {
    std::string filter;
    for (auto& f : filters) {
      if (!filter.empty()) filter += "\n";
      filter += f.name + " (";
      bool first = true;
      for (auto& p : split_patterns(f.patterns)) {
        filter += (first ? "" : " ") + p;
        first = false;
      }
      filter += ")";
    }
    std::vector<std::string> a = {"kdialog", "--title", title, save ? "--getsavefilename" : "--getopenfilename",
                                  save && !default_name.empty() ? default_name : std::string(".")};
    if (!filter.empty()) a.push_back(filter);
    return run_dialog_program(std::move(a));
  }
  std::fprintf(stderr, "PulseUI: no file dialog available (install xdg-desktop-portal, zenity or kdialog)\n");
  return std::nullopt;
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

} // namespace pulseui::platform
