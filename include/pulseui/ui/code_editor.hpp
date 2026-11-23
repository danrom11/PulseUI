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
  std::string insert; // text to insert (defaults to label); "$0" marks where the caret ends up
  bool operator==(const CompletionItem&) const = default;
};

struct CompletionResult {
  std::vector<CompletionItem> items;
  std::size_t replace_from = 0; // byte column on the caret line where the typed prefix starts
  // Parameter hint for the call around the caret, e.g. "resize(float w, float h)".
  // [active_begin, active_end) is the byte range of the current parameter in it.
  std::string signature;
  std::size_t active_begin = 0, active_end = 0;
};

// A colored range of one line (byte offsets).
struct TextSpan {
  std::size_t begin = 0, end = 0;
  Color color;
};

class CodeEditor : public ViewT<CodeEditor> {
public:
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

  // Returns colored spans for `line`. `in_block_comment` carries /* */ state
  // from the previous line and must be updated for the next one.
  using Highlighter = std::function<std::vector<TextSpan>(std::string_view line, bool& in_block_comment)>;
  // Completion is language specific and pluggable: given the text and the caret
  // (0-based line, byte column) return the candidates for the word at the caret.
  using CompletionProvider =
      std::function<CompletionResult(const std::vector<std::string>& lines, std::size_t line, std::size_t col)>;
  // Text for a tooltip when the mouse rests on (line, col); empty = nothing to show.
  using HoverProvider = std::function<std::string(const std::vector<std::string>& lines, std::size_t line, std::size_t col)>;

  CodeEditor();
  explicit CodeEditor(Model<std::string>& model);
  const char* type_name() const override { return "CodeEditor"; }

  PULSEUI_MOD(CodeEditor, model, (Model<std::string>& m), bind_model(m);)
  PULSEUI_MOD(CodeEditor, read_only, (bool r = true), read_only_ = r; focusable_ = true;)
  PULSEUI_MOD(CodeEditor, line_numbers, (bool on = true), line_numbers_ = on;)
  PULSEUI_MOD(CodeEditor, highlighter, (Highlighter h), highlighter_ = std::move(h);)
  PULSEUI_MOD(CodeEditor, completion, (CompletionProvider p), completion_ = std::move(p);)
  PULSEUI_MOD(CodeEditor, hover, (HoverProvider p), hover_ = std::move(p);)
  // typing ( [ { " ' inserts the closing character; typing it again steps over it
  PULSEUI_MOD(CodeEditor, auto_close, (bool on = true), auto_close_ = on;)
  // line comment token for Ctrl+/ (e.g. "//", "#"); empty disables the shortcut
  PULSEUI_MOD(CodeEditor, line_comment, (std::string token), comment_ = std::move(token);)
  PULSEUI_MOD(CodeEditor, on_change, (std::function<void(const std::string&)> fn), changed.connect(std::move(fn));)
  PULSEUI_MOD(CodeEditor, diagnostics, (Bind<std::vector<Diagnostic>> d),
              bind(d, [](View& v, const std::vector<Diagnostic>& x) { static_cast<CodeEditor&>(v).set_diagnostics(x); });)

  // Debugging: breakpoint markers in the gutter (1-based lines; a click there emits
  // breakpoint_toggled - the application owns the list) and the line where execution stopped.
  PULSEUI_MOD(CodeEditor, breakpoints, (Bind<std::vector<int>> lines), gutter_clicks_ = true;
              bind(lines, [](View& v, const std::vector<int>& x) { static_cast<CodeEditor&>(v).set_breakpoints(x); });)
  PULSEUI_MOD(CodeEditor, execution_line, (Bind<int> line),
              bind(line, [](View& v, int x) { static_cast<CodeEditor&>(v).set_execution_line(x); });)
  pulse::signal<int> breakpoint_toggled;

  pulse::signal<std::string> changed; // after every edit (whole text)
  // Ctrl+click (Cmd+click on macOS) on the text: 0-based line and byte column
  pulse::signal<std::size_t, std::size_t> definition_requested;

  std::string text() const;
  void set_text(std::string_view text);
  void append(std::string_view text);  // keeps following the end if it was at the end
  void clear() { set_text({}); }
  void set_diagnostics(std::vector<Diagnostic> d);
  void set_breakpoints(std::vector<int> lines) { breakpoints_ = std::move(lines); invalidate(); }
  const std::vector<int>& breakpoints_value() const { return breakpoints_; }
  void set_execution_line(int line); // 0 = none; scrolls it into view
  int execution_line_value() const { return exec_line_; }
  const std::vector<Diagnostic>& diagnostics_value() const { return diags_; }
  void go_to(int line, int column = 1); // 1-based, scrolls into view and focuses
  int caret_line() const { return static_cast<int>(caret_.line) + 1; }
  int caret_column() const { return static_cast<int>(caret_.col) + 1; }
  std::size_t line_count() const { return lines_.size(); }
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  bool has_selection() const { return !(caret_ == anchor_); }
  void undo() { undo_redo(false); }
  void redo() { undo_redo(true); }
  void cut();
  void copy();
  void paste();
  void select_all();
  // Undo, Redo, Cut, Copy, Paste, Select All - the default right-click menu.
  // An application menu can append it: .context_menu([&] { auto m = mine(); ...; return m; })
  MenuItems standard_menu();
  // labels of the standard menu (translate them with set_menu_labels)
  void set_menu_labels(std::string undo, std::string redo, std::string cut, std::string copy, std::string paste, std::string select_all) {
    i18n_undo_ = std::move(undo); i18n_redo_ = std::move(redo); i18n_cut_ = std::move(cut);
    i18n_copy_ = std::move(copy); i18n_paste_ = std::move(paste); i18n_select_all_ = std::move(select_all);
  }
  const std::vector<std::string>& lines() const { return lines_; }

  // Ranges use 0-based lines and byte columns.
  void select(std::size_t line0, std::size_t col0, std::size_t line1, std::size_t col1); // anchor -> caret
  std::string selected_text() const;
  void replace_range(std::size_t line0, std::size_t col0, std::size_t line1, std::size_t col1, std::string_view text);
  void replace_all_text(std::string_view text); // one undo step, keeps the caret where it was
  std::string word_at(std::size_t line, std::size_t col) const;
  void toggle_comment(); // Ctrl+/ on the caret line or the selected lines

  // search: matches are highlighted; find_next selects one and scrolls to it
  void set_search(std::string query, bool match_case = false, bool whole_word = false);
  const std::string& search_query() const { return search_; }
  std::size_t search_count() const { return matches_.size(); }
  int search_current() const; // index of the selected match, -1 if none is selected
  // include_current: a match starting at the selection start counts (incremental search)
  bool find_next(bool backwards = false, bool include_current = false);
  bool replace_current(std::string_view with); // replaces the selected match, then selects the next one
  std::size_t replace_all(std::string_view with); // one undo step; returns the number of replacements
  const std::string& hover_text() const { return hover_text_; }

  // completion popup (Ctrl+Space opens it manually)
  bool completion_open() const { return popup_open_; }
  const std::vector<CompletionItem>& completion_items() const { return popup_.items; }
  void open_completion();
  void close_completion();
  void accept_completion();
  // parameter hint, shown after '(' and ',' while the caret is inside a call
  const std::string& signature_hint() const { return hint_.signature; }

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
  std::pair<Pos, Pos> selection() const;
  void undo_redo(bool redo);
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
  std::vector<int> breakpoints_;
  std::string i18n_undo_ = "Undo", i18n_redo_ = "Redo", i18n_cut_ = "Cut", i18n_copy_ = "Copy", i18n_paste_ = "Paste",
              i18n_select_all_ = "Select All";
  int exec_line_ = 0;
  bool gutter_clicks_ = false;
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
  CompletionResult hint_;
  void refresh_hint(bool open_if_closed);
  void close_hint();
  void paint_hint(Painter& p);
  HoverProvider hover_;
  bool auto_close_ = false;
  std::string comment_;
  std::string search_;
  bool search_case_ = false, search_word_ = false;
  std::vector<std::pair<Pos, Pos>> matches_;
  void update_matches();
  std::pair<std::size_t, std::size_t> word_range(std::size_t line, std::size_t col) const;
  Pos char_at(Point p) const; // the character under p (not the nearest boundary)
  std::string hover_text_;
  Pos hover_a_{}, hover_b_{};
  bool hover_target_ = false, link_ = false;
  pulse::subscription hover_timer_;
  void hide_hover();
  void track_hover(const InputEvent& e);
  void paint_hover(Painter& p);
  bool bracket_match(Pos& a, Pos& b) const;
  bool read_only_ = false, line_numbers_ = true, selecting_ = false, dragging_thumb_ = false;
  float thumb_grab_ = 0;
};

} // namespace pulseui
