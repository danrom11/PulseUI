# 13. CodeEditor

> Debugging support: `.breakpoints(Bind<std::vector<int>>)` draws breakpoint markers in the gutter
> (a click there emits `breakpoint_toggled(line)` - the application keeps the list), and
> `.execution_line(Bind<int>)` highlights the line where a debugger stopped (0 = none) and scrolls it
> into view. Pulse Layer uses both for its debugger.

`CodeEditor` is a multi-line text editor for source code, logs, notes and configuration. It is
language-neutral: syntax highlighting and completion are plugged in by the application. (Pulse
Layer's C++ highlighting and completion are application code built on these hooks.)

```cpp
pulse::property<std::string> source;

CodeEditor(source)                         // two-way bound to the text
    .highlighter(my_highlighter)
    .completion(my_completion)
    .diagnostics(errors)                   // squiggles and inline messages
    .flex();

CodeEditor().read_only().line_numbers(false)   // a log view; feed it with append()
```

## Construction and binding

| | |
|---|---|
| `CodeEditor()` | empty, unbound |
| `explicit CodeEditor(Model<std::string>& model)` | two-way: the editor follows the property and writes edits back |
| `model(Model<std::string>&)` | the same, after construction |

The model receives the whole text after every edit. Line endings are normalized to `\n`, and tab
characters are expanded to spaces (tab stops every 4 columns).

## Modifiers

| Modifier | Effect |
|---|---|
| `read_only(bool = true)` | no editing; selection and copying still work |
| `line_numbers(bool = true)` | show or hide the gutter |
| `highlighter(Highlighter)` | syntax coloring (see below) |
| `completion(CompletionProvider)` | completion popup and parameter hints (see below) |
| `hover(HoverProvider)` | a tooltip for the word under the resting mouse (see below) |
| `auto_close(bool = true)` | typing `(` `[` `{` `"` `'` inserts the closing character; typing it again steps over it; Backspace removes an empty pair; with a selection, wraps it |
| `line_comment(std::string)` | the comment token for Ctrl+/ (for example `"//"` or `"#"`) |
| `diagnostics(Bind<std::vector<Diagnostic>>)` | errors and warnings, as a value or a stream |
| `on_change(fn(const std::string&))` | after every edit |

## Members

| Member | |
|---|---|
| `pulse::signal<std::string> changed` | after every edit, with the whole text |
| `pulse::signal<std::size_t, std::size_t> definition_requested` | Ctrl+click on the text (0-based line, byte column): implement "go to definition" here |
| `text()`, `set_text(sv)`, `clear()` | |
| `append(sv)` | add text at the end; if the view was scrolled to the end, it keeps following (ideal for logs) |
| `line_count()`, `caret_line()`, `caret_column()` | 1-based positions |
| `go_to(line, column = 1)` | move the caret, scroll there and focus the editor |
| `can_undo()` | |
| `set_diagnostics(v)`, `diagnostics_value()` | |
| `open_completion()`, `close_completion()`, `accept_completion()`, `completion_open()`, `completion_items()` | drive the popup from code or tests |
| `signature_hint()` | the parameter hint currently shown (empty when hidden) |
| `hover_text()` | the hover tooltip currently shown |
| `lines()` | the text as lines |
| `select(l0, c0, l1, c1)`, `selected_text()` | select a range (0-based lines, byte columns; anchor → caret) |
| `replace_range(l0, c0, l1, c1, text)` | replace a range, as one undo step |
| `replace_all_text(text)` | replace everything, as one undo step, keeping the caret (formatters, refactorings) |
| `word_at(line, col)` | the identifier around a position |
| `toggle_comment()` | what Ctrl+/ does |
| `set_search(query, match_case = false, whole_word = false)` | highlight every match (empty query clears) |
| `search_count()`, `search_current()`, `search_query()` | number of matches, index of the selected one (-1) |
| `find_next(backwards = false, include_current = false)` | select the next match and scroll to it, wrapping around |
| `replace_current(text)`, `replace_all(text)` | replace the selected match (then select the next) / all of them in one undo step |

## Keyboard and mouse

| Keys | Action |
|---|---|
| arrows, Home/End, PageUp/PageDown | move; with Shift, select |
| Ctrl+Left / Ctrl+Right | word-wise movement |
| Ctrl+Home / Ctrl+End | start / end of the document |
| Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V | select all, copy, cut, paste |
| Ctrl+Z, Ctrl+Y (also Ctrl+Shift+Z) | undo, redo (consecutive typing is merged into one step) |
| Enter | new line with the current indentation, one level more after `{`, `(` or `:`; between `{}` or `()` the closing bracket moves to its own line |
| Tab / Shift+Tab | insert indentation; with a multi-line selection, indent / unindent the lines |
| Backspace in leading whitespace | removes back to the previous tab stop |
| Ctrl+Space | open completion |
| Escape | close the completion popup, then the parameter hint |
| Ctrl+/ | comment or uncomment the caret line or the selected lines (with `line_comment`) |
| Ctrl+click | `definition_requested` (the word looks like a link while Ctrl is held) |
| double-click | select a word |
| right click | moves the caret to the click (unless it is inside the selection) and opens the context menu |

The bracket next to the caret and its partner are highlighted. Alt+Enter is left to the application
(it is a common "quick action" key), as are F-keys and shortcuts the editor does not use.

"Ctrl" is Cmd on macOS.

### Context menu

By default a right click shows Undo, Redo, Cut, Copy, Paste and Select All (only Copy and Select All for a
`read_only()` editor). The same actions are members: `undo()`, `redo()`, `cut()`, `copy()`, `paste()`,
`select_all()`, with `can_undo()`, `can_redo()` and `has_selection()`. Replace the menu with `.context_menu(...)`,
and append `standard_menu()` to keep the clipboard entries:

```cpp
CodeEditor(source)
    .ref(editor)
    .context_menu([&] {
      MenuItems m{Action("Go to Definition", go_to_definition).shortcut("F12"), Separator()};
      for (auto& e : editor->standard_menu()) m.push_back(std::move(e));
      return m;
    });
```

`set_menu_labels("Отменить", "Повторить", "Вырезать", "Копировать", "Вставить", "Выделить всё")` translates
the standard entries.

## Syntax highlighting

```cpp
using Highlighter = std::function<std::vector<TextSpan>(std::string_view line, bool& in_block_comment)>;
struct TextSpan { std::size_t begin, end; Color color; };   // byte offsets within the line
```

The highlighter is called per line, for visible lines, during painting. It returns colored spans;
anything not covered uses the default text color. `in_block_comment` carries multi-line state (such
as `/* ... */`) from one line to the next: read it at the start, and update it for the next line.

A small highlighter for an INI-like format:

```cpp
CodeEditor::Highlighter ini = [](std::string_view line, bool&) {
  std::vector<TextSpan> spans;
  const auto first = line.find_first_not_of(' ');
  if (first == std::string_view::npos) return spans;
  if (line[first] == ';' || line[first] == '#') {                    // comment
    spans.push_back({first, line.size(), Color::hex(0x6A9955)});
  } else if (line[first] == '[') {                                    // [section]
    spans.push_back({first, line.size(), Color::hex(0xC586C0)});
  } else if (auto eq = line.find('='); eq != std::string_view::npos) {
    spans.push_back({first, eq, Color::hex(0x9CDCFE)});              // key
    spans.push_back({eq + 1, line.size(), Color::hex(0xCE9178)});    // value
  }
  return spans;
};

CodeEditor(config_text).highlighter(ini);
```

Keep highlighters fast and allocation-light: they run on every repaint of the visible lines.

## Completion

```cpp
using CompletionProvider =
    std::function<CompletionResult(const std::vector<std::string>& lines, std::size_t line, std::size_t col)>;

struct CompletionResult {
  std::vector<CompletionItem> items;
  std::size_t replace_from;          // byte column on the caret line where the typed prefix starts
  std::string signature;             // parameter hint for the call around the caret (optional)
  std::size_t active_begin, active_end; // byte range of the current parameter inside `signature`
};
struct CompletionItem {
  enum Kind { Variable, Function, Type, Keyword, Member, Value };
  std::string label;                 // shown and inserted
  std::string detail;                // shown dimmed on the right
  Kind kind;                         // decides the icon
  std::string insert;                // text to insert instead of label (optional); "$0" = caret position
};
```

The provider receives all lines and the caret position (0-based line, byte column) and returns the
candidates plus where the word being completed starts. The editor:

- opens the popup automatically after two identifier characters, after `.`, `->`, `::` and `"`, or on
  Ctrl+Space;
- refilters by calling the provider again as you type, and closes the popup when there are no items;
- navigates with Up/Down/PageUp/PageDown; Enter or Tab inserts; Escape closes.

### Snippets and the caret

`insert` may contain `$0`: the text is inserted and the caret is placed at the marker. Use it for calls
(`"resize($0)"` puts the caret between the parentheses) and for multi-line snippets. The provider knows the
caret line, so it can indent the following lines itself:

```cpp
const std::string ind = indent_of(lines[line]);               // your helper: the leading spaces
r.items.push_back({"if", "if (...) { }", CompletionItem::Keyword,
                   "if ($0) {\n" + ind + "    \n" + ind + "}"});
```

### Parameter hints

When the user types `(` or `,` the editor asks the same provider again and shows `signature` above the
line, with `[active_begin, active_end)` in bold: the parameter being typed. The hint follows the caret,
disappears when the provider returns an empty `signature` (the caret left the call), and closes on
Escape or a click. A provider usually finds the innermost unclosed `(` before the caret, looks the
function up, and counts the commas:

```cpp
r.signature = "resize(float w, float h)";
r.active_begin = 16;   // "float h" when the caret is after the comma
r.active_end = 23;
```

```cpp
CodeEditor(text).completion([](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
  CompletionResult r;
  std::size_t from = col;
  while (from > 0 && std::isalnum(static_cast<unsigned char>(lines[line][from - 1]))) --from;
  r.replace_from = from;
  const std::string prefix = lines[line].substr(from, col - from);
  for (const char* word : {"select", "from", "where", "group", "order", "limit"})
    if (std::string_view(word).starts_with(prefix)) r.items.push_back({word, "keyword", CompletionItem::Keyword});
  return r;
});
```

## Hover

```cpp
using HoverProvider =
    std::function<std::string(const std::vector<std::string>& lines, std::size_t line, std::size_t col)>;

CodeEditor(text).hover([](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
  return type_of_symbol(lines, line, col); // "" shows nothing
});
```

When the mouse rests on a word for about half a second, the editor calls the provider with the
position of the character under the pointer and shows the text in a box below the word (`\n` starts
a new line). Moving away, typing, clicking or scrolling hides it.

## Search and replace

The editor highlights matches and moves between them; the find bar is yours to build, so it fits your
application:

```cpp
pulse::property<std::string> query;
CodeEditor* ed = nullptr;

VStack{
  HStack{
    TextField(query).on_submit([&](const std::string&) { ed->find_next(); }),
    Button("Replace all", [&] { ed->replace_all("new"); }),
  },
  CodeEditor(text).ref(ed).flex(),
};
query.changed.connect([&](const std::string& q) {
  ed->set_search(q);
  ed->find_next(false, true); // incremental: keep the match at the selection
});
```

Matches are single-line. `whole_word` uses identifier characters as word characters.

## Diagnostics

```cpp
struct Diagnostic {
  enum Severity { Error, Warning, Note };
  int line;          // 1-based
  int column;        // 1-based, 0 = underline the whole line
  Severity severity;
  std::string message;
};
```

Errors are underlined with a red squiggle, warnings in yellow, and the message is shown at the end of
the line. Bind a stream to update them as results arrive:

```cpp
pulse::property<std::vector<Diagnostic>> problems;
CodeEditor(source).diagnostics(problems);

auto lint = source.as_observable()
          | pulse::debounce(400ms, App::ui())
          | pulse::map(run_linter)                 // std::vector<Diagnostic>
          ;
auto s = lint.subscribe([&](const std::vector<Diagnostic>& d) { problems = d; });
```

## A log view

```cpp
CodeEditor* log = nullptr;
VStack{ CodeEditor().read_only().line_numbers(false).ref(log).flex() };

// later, from the UI thread:
log->append("Build started\n");
// from another thread:
App::post([log, line] { log->append(line); });
```

Next: [Forms and reflection](14-forms-and-meta.md).
