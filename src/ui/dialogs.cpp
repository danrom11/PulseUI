// Message boxes: NSAlert on macOS, a themed PulseUI window elsewhere.
#include <pulseui/ui/dialogs.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/widgets.hpp>
#include <pulseui/ui/window.hpp>

#include <memory>

#include "../platform/platform.hpp"

namespace pulseui::dialogs {

namespace {

std::function<Answer(const Message&)>& responder() {
  static std::function<Answer(const Message&)> r;
  return r;
}

Color icon_color(Icon i) {
  const Theme& t = th();
  switch (i) {
    case Icon::Warning: return t.warning;
    case Icon::Error: return t.danger;
    case Icon::Question:
    case Icon::Info:
    default: return t.accent;
  }
}

const char* icon_glyph(Icon i) {
  switch (i) {
    case Icon::Warning: return "!";
    case Icon::Error: return "\xC3\x97"; // ×
    case Icon::Question: return "?";
    default: return "i";
  }
}

struct DialogState {
  std::unique_ptr<Window> window;
  Answer answer;
  pulse::property<std::string> text;
  bool finished = false;
  std::function<void(Answer)> done;
  std::shared_ptr<DialogState> self; // keeps the dialog alive while it is open
};

// Builds and shows the themed dialog. The window deletes itself after it closes.
std::shared_ptr<DialogState> open_themed(Window* parent, const Message& m, std::function<void(Answer)> done) {
  auto st = std::make_shared<DialogState>();
  st->done = std::move(done);
  st->self = st;
  st->answer.button = cancel_index(m);
  st->text = m.input_text;
  std::weak_ptr<DialogState> weak = st;

  st->window = std::make_unique<Window>(m.title.empty() ? std::string(" ") : m.title, 420, 160);
  Window& w = *st->window;
  w.set_resizable(false);

  auto finish = [weak](int button) {
    auto s = weak.lock();
    if (!s || s->finished) return;
    s->answer.button = button;
    s->answer.text = s->text.get();
    if (s->window) s->window->close();
  };

  HStack buttons;
  buttons.spacing(8).justify(Justify::End);
  buttons.add(Spacer());
  for (std::size_t i = 0; i < m.buttons.size(); ++i) {
    Button b(m.buttons[i], [finish, i] { finish(static_cast<int>(i)); });
    b.id("dialog.button." + std::to_string(i)).min_width(84);
    if (static_cast<int>(i) == m.default_button) {
      if (m.danger) b.danger();
      else b.primary();
    }
    buttons.add(std::move(b));
  }

  VStack text;
  text.spacing(6).flex();
  if (!m.title.empty()) text.add(Text(m.title).weight(FontWeight::SemiBold).font_size(15).wrap().id("dialog.title"));
  if (!m.text.empty()) text.add(Text(m.text).wrap().id("dialog.text"));
  if (!m.detail.empty()) text.add(Text(m.detail).dim().wrap().font_size(12).id("dialog.detail"));
  if (m.input) {
    TextField field(st->text);
    field.placeholder(m.input_placeholder).id("dialog.input");
    field.on_submit([finish, d = m.default_button](const std::string&) { finish(d); });
    text.add(std::move(field));
  }

  HStack body;
  body.spacing(14).align_items(Alignment::Start);
  if (m.icon != Icon::None) {
    const Icon icon = m.icon;
    body.add(Canvas([icon](Painter& p, const Rect& r) {
               const Color c = icon_color(icon);
               p.fill_ellipse(r, c.with_alpha(0.16f));
               p.stroke_ellipse(r.inset(0.75f), c, 1.5f);
               p.text(r, icon_glyph(icon), th().font.with_size(17).with_weight(FontWeight::Bold), c, Align::Center);
             }).size(36, 36));
  }
  body.add(std::move(text));

  w.content(VStack{std::move(body), Spacer(4), std::move(buttons)}.spacing(14).padding(20).min_width(m.input ? 380.f : 340.f).id("dialog"));
  w.fit_content(440);
  const int cancel = cancel_index(m);
  w.shortcut("Escape", [finish, cancel] { finish(cancel); });
  if (!m.input) w.shortcut("Enter", [finish, d = m.default_button] { finish(d); });

  w.closed.connect([weak] {
    auto s = weak.lock();
    if (!s || s->finished) return;
    s->finished = true;
    // deliver the answer and drop the window outside of its own event handling
    platform::post([s] {
      if (s->done) s->done(s->answer);
      s->window.reset();
      s->self.reset();
    });
  });

  if (parent && parent->is_open()) w.show_modal(*parent);
  else w.show();
  if (View* f = w.find("dialog.input")) w.set_focus(f);
  else if (View* b = w.find("dialog.button." + std::to_string(m.default_button))) w.set_focus(b);
  return st;
}

} // namespace

int cancel_index(const Message& m) {
  if (m.cancel_button >= 0) return m.cancel_button;
  return m.buttons.size() > 1 ? static_cast<int>(m.buttons.size()) - 1 : 0;
}

void set_responder(std::function<Answer(const Message&)> r) { responder() = std::move(r); }

void message_async(Window* parent, Message m, std::function<void(Answer)> done) {
  if (m.buttons.empty()) m.buttons = {"OK"};
  if (responder()) {
    Answer a = responder()(m);
    platform::post([done, a] {
      if (done) done(a);
    });
    return;
  }
  if (platform::native_message_async(parent ? parent->native_window() : nullptr, m, done)) return;
  open_themed(parent, m, std::move(done));
}

namespace {
Answer run_blocking(Window* parent, Message m) {
  if (m.buttons.empty()) m.buttons = {"OK"};
  if (responder()) return responder()(m);
  if (auto a = platform::native_message(parent ? parent->native_window() : nullptr, m)) return *a;
  auto result = std::make_shared<std::optional<Answer>>();
  auto st = open_themed(parent, m, [result](Answer a) { *result = std::move(a); });
  platform::run_modal_loop([result] { return result->has_value(); });
  return result->value_or(Answer{cancel_index(m), {}});
}
} // namespace

int message(Window* parent, const Message& m) { return run_blocking(parent, m).button; }

void alert(Window* parent, const std::string& title, const std::string& text, Icon icon) {
  Message m;
  m.title = title;
  m.text = text;
  m.icon = icon;
  message(parent, m);
}

bool confirm(Window* parent, const std::string& title, const std::string& text, const std::string& ok,
             const std::string& cancel, bool danger) {
  Message m;
  m.title = title;
  m.text = text;
  m.icon = danger ? Icon::Warning : Icon::Question;
  m.buttons = {ok, cancel};
  m.danger = danger;
  m.cancel_button = 1;
  return message(parent, m) == 0;
}

void confirm_async(Window* parent, const std::string& title, const std::string& text, std::function<void(bool)> done,
                   const std::string& ok, const std::string& cancel) {
  Message m;
  m.title = title;
  m.text = text;
  m.icon = Icon::Question;
  m.buttons = {ok, cancel};
  m.cancel_button = 1;
  message_async(parent, std::move(m), [done](Answer a) {
    if (done) done(a.button == 0);
  });
}

std::optional<std::string> prompt(Window* parent, const std::string& title, const std::string& label,
                                  const std::string& initial, const std::string& ok, const std::string& cancel) {
  Message m;
  m.title = title;
  m.text = label;
  m.icon = Icon::None;
  m.buttons = {ok, cancel};
  m.cancel_button = 1;
  m.input = true;
  m.input_text = initial;
  Answer a = run_blocking(parent, m);
  if (a.button != 0) return std::nullopt;
  return a.text;
}

} // namespace pulseui::dialogs
