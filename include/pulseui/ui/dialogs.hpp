#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pulseui {

class Window;

namespace dialogs {

struct Filter {
  std::string name;     // "Pulse forms"
  std::string patterns; // "*.pform;*.json"
};

// Native file dialogs (modal). Return std::nullopt when cancelled.
std::optional<std::string> open_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {});
std::optional<std::string> save_file(Window* parent, const std::string& title, const std::vector<Filter>& filters = {},
                                     const std::string& default_name = {});

// ─────────────────────────────────────────────────────────────────────────────
// Message boxes
//   dialogs::alert(&win, "Saved", "The file was written.");
//   if (dialogs::confirm(&win, "Delete note?", "This cannot be undone.", "Delete")) ...
//   int i = dialogs::message(&win, {.title = "Unsaved changes", .text = "Save before closing?",
//                                   .buttons = {"Save", "Don't save", "Cancel"}, .icon = Icon::Warning,
//                                   .cancel_button = 2});
//   auto name = dialogs::prompt(&win, "Rename", "New name:", old_name);
//
// They block until answered (other windows of the app are disabled meanwhile) and
// return the answer. The _async variants return at once and deliver the answer to
// a callback on the UI thread - handy inside streams. On macOS they are NSAlerts;
// elsewhere PulseUI draws them in the current theme.
// ─────────────────────────────────────────────────────────────────────────────
enum class Icon { None, Info, Warning, Error, Question };

struct Message {
  std::string title;
  std::string text;
  std::string detail;                    // smaller text below (optional)
  std::vector<std::string> buttons{"OK"};
  Icon icon = Icon::Info;
  int default_button = 0;                // Enter
  int cancel_button = -1;                // Escape / closing the window (-1: last button if there are several)
  bool danger = false;                   // the default button is destructive (drawn red)
  bool input = false;                    // show a text field (prompt)
  std::string input_text;                // its initial text
  std::string input_placeholder;
};

struct Answer {
  int button = -1;      // index into Message::buttons
  std::string text;     // the text field (input dialogs)
};

int message(Window* parent, const Message& m);
void message_async(Window* parent, Message m, std::function<void(Answer)> done);

void alert(Window* parent, const std::string& title, const std::string& text, Icon icon = Icon::Info);
bool confirm(Window* parent, const std::string& title, const std::string& text, const std::string& ok = "OK",
             const std::string& cancel = "Cancel", bool danger = false);
void confirm_async(Window* parent, const std::string& title, const std::string& text, std::function<void(bool)> done,
                   const std::string& ok = "OK", const std::string& cancel = "Cancel");
std::optional<std::string> prompt(Window* parent, const std::string& title, const std::string& label,
                                  const std::string& initial = {}, const std::string& ok = "OK",
                                  const std::string& cancel = "Cancel");

// Tests and automation: answer message boxes without showing them.
// The responder gets each message and returns the answer; pass {} to restore.
void set_responder(std::function<Answer(const Message&)> responder);

// The index Escape / closing the window stands for.
int cancel_index(const Message& m);

} // namespace dialogs
} // namespace pulseui
