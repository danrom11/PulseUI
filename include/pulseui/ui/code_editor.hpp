#pragma once
// CodeEditor - a multi-line text editor for source code and logs.
//
//   pulse::property<std::string> source;
//   CodeEditor(source)                          // two-way bound
//       .diagnostics(errors$)                   // squiggles + inline messages
//       .flex();
//   CodeEditor().read_only()                    // a build log: use append()
//
// Language support is pluggable: pass a Highlighter (the C++ rules of Pulse
// Layer live in the application, not in the library).
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <pulse/core/signal.hpp>
#include <pulseui/ui/view.hpp>

namespace pulseui {

struct Diagnostic {
  enum Severity { Error, Warning, Note };
  int line = 1;   // 1-based
  int column = 0; // 1-based, 0 = whole line
  Severity severity = Error;
  std::string message;
  bool operator==(const Diagnostic&) const = default;
};

// One entry of the completion popup.
struct CompletionItem {
  enum Kind { Variable, Function, Type, Keyword, Member, Value };
  std::string label;  // shown and inserted
  std::string detail; // shown dimmed on the right (type, signature, ...)
  Kind kind = Variable;
  std::string insert; // text to insert (defaults to label)
  bool operator==(const CompletionItem&) const = default;
};

struct CompletionResult {
  std::vector<CompletionItem> items;
  std::size_t replace_from = 0; // byte column on the caret line where the typed prefix starts
};

// A colored range of one line (byte offsets).
struct TextSpan {
  std::size_t begin = 0, end = 0;
  Color color;
};

class CodeEditor : public ViewT<CodeEditor> {
public:
  // Returns colored spans for `line`. `in_block_comment` carries /* */ state
  // from the previous line and must be updated for the next one.
  using Highlighter = std::function<std::vector<TextSpan>(std::string_view line, bool& in_block_comment)>;
  // Completion is language specific and pluggable: given the text and the caret
  // (0-based line, byte column) return the candidates for the word at the caret.
  using CompletionProvider =
      std::function<CompletionResult(const std::vector<std::string>& lines, std::size_t line, std::size_t col)>;

  CodeEditor();
  explicit CodeEditor(Model<std::string>& model);
  const char* type_name() const override { return "CodeEditor"; }

  PULSEUI_MOD(CodeEditor, model, (Model<std::string>& m), bind_model(m);)
  PULSEUI_MOD(CodeEditor, read_only, (bool r = true), read_only_ = r; focusable_ = true;)
  PULSEUI_MOD(CodeEditor, line_numbers, (bool on = true), line_numbers_ = on;)
  PULSEUI_MOD(CodeEditor, highlighter, (Highlighter h), highlighter_ = std::move(h);)
  PULSEUI_MOD(CodeEditor, completion, (CompletionProvider p), completion_ = std::move(p);)
  PULSEUI_MOD(CodeEditor, on_change, (std::function<void(const std::string&)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(CodeEditor, diagnostics, (Bind<std::vector<Diagnostic>> d),
              bind(d, [](View& v, const std::vector<Diagnostic>& x) { static_cast<CodeEditor&>(v).set_diagnostics(x); });)

  pulse::signal<std::string> changed; // after every edit (whole text)

  std::string text() const;
  void set_text(std::string_view text);
  void append(std::string_view text);  // keeps following the end if it was at the end
  void clear() { set_text({}); }
  void set_diagnostics(std::vector<Diagnostic> d);
  const std::vector<Diagnostic>& diagnostics_value() const { return diags_; }
  void go_to(int line, int column = 1); // 1-based, scrolls into view and focuses
  int caret_line() const { return static_cast<int>(caret_.line) + 1; }
  int caret_column() const { return static_cast<int>(caret_.col) + 1; }
  std::size_t line_count() const { return lines_.size(); }
  bool can_undo() const { return !undo_.empty(); }

  // completion popup (Ctrl+Space opens it manually)
  bool completion_open() const { return popup_open_; }
  const std::vector<CompletionItem>& completion_items() const { return popup_.items; }
  void open_completion();
  void close_completion();
  void accept_completion();

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void on_focus(bool f) override;

private:
  struct Pos {
    std::size_t line = 0, col = 0; // col: byte offset
    bool operator==(const Pos&) const = default;
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
  };
  struct Snapshot {
    std::vector<std::string> lines;
    Pos caret, anchor;
  };

  void bind_model(Model<std::string>& m);
  void metrics();
  float gutter_width() const;
  Rect text_area() const;
  float x_of(std::size_t line, std::size_t col) const;
  Pos pos_at(Point p) const;
  void ensure_visible();
  void clamp_scroll();
  bool has_selection() const { return !(caret_ == anchor_); }
  std::pair<Pos, Pos> selection() const;
  std::string selected_text() const;
  void push_undo(int kind);
  void edited();
  void erase_selection();
  void insert(std::string_view s);
  void move_caret(Pos p, bool select);
  void indent_selection(bool unindent);

  std::vector<std::string> lines_{std::string()};
  Pos caret_, anchor_;
  std::size_t preferred_col_cp_ = 0;
  Point scroll_{};
  float char_w_ = 8, line_h_ = 18;
  std::vector<Diagnostic> diags_;
  std::vector<Snapshot> undo_, redo_;
  int last_edit_kind_ = 0;
  Model<std::string>* model_ = nullptr;
  Highlighter highlighter_;
  CompletionProvider completion_;
  CompletionResult popup_;
  bool popup_open_ = false;
  int popup_index_ = 0, popup_scroll_ = 0;
  void refresh_completion(bool open_if_closed);
  Rect popup_rect() const;
  void paint_popup(Painter& p);
  bool read_only_ = false, line_numbers_ = true, selecting_ = false, dragging_thumb_ = false;
  float thumb_grab_ = 0;
};

} // namespace pulseui
