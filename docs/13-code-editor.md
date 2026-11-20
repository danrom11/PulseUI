# 13. CodeEditor

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
| `completion(CompletionProvider)` | completion popup (see below) |
| `diagnostics(Bind<std::vector<Diagnostic>>)` | errors and warnings, as a value or a stream |
| `on_change(fn(const std::string&))` | after every edit |

## Members

| Member | |
|---|---|
| `pulse::signal<std::string> changed` | after every edit, with the whole text |
| `text()`, `set_text(sv)`, `clear()` | |
| `append(sv)` | add text at the end; if the view was scrolled to the end, it keeps following (ideal for logs) |
| `line_count()`, `caret_line()`, `caret_column()` | 1-based positions |
| `go_to(line, column = 1)` | move the caret, scroll there and focus the editor |
| `can_undo()` | |
| `set_diagnostics(v)`, `diagnostics_value()` | |
| `open_completion()`, `close_completion()`, `accept_completion()`, `completion_open()`, `completion_items()` | drive the popup from code or tests |

## Keyboard and mouse

| Keys | Action |
|---|---|
| arrows, Home/End, PageUp/PageDown | move; with Shift, select |
| Ctrl+Left / Ctrl+Right | word-wise movement |
| Ctrl+Home / Ctrl+End | start / end of the document |
| Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V | select all, copy, cut, paste |
| Ctrl+Z, Ctrl+Y (also Ctrl+Shift+Z) | undo, redo (consecutive typing is merged into one step) |
| Enter | new line with the current indentation, one level more after `{`, `(` or `:` |
| Tab / Shift+Tab | insert indentation; with a multi-line selection, indent / unindent the lines |
| Backspace in leading whitespace | removes back to the previous tab stop |
| Ctrl+Space | open completion |
| double-click | select a word |

"Ctrl" is Cmd on macOS.

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
};
struct CompletionItem {
  enum Kind { Variable, Function, Type, Keyword, Member, Value };
  std::string label;                 // shown and inserted
  std::string detail;                // shown dimmed on the right
  Kind kind;                         // decides the icon
  std::string insert;                // text to insert instead of label (optional)
};
```

The provider receives all lines and the caret position (0-based line, byte column) and returns the
candidates plus where the word being completed starts. The editor:

- opens the popup automatically after two identifier characters, after `.`, `->`, `::` and `"`, or on
  Ctrl+Space;
- refilters by calling the provider again as you type, and closes the popup when there are no items;
- navigates with Up/Down/PageUp/PageDown; Enter or Tab inserts; Escape closes.

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
