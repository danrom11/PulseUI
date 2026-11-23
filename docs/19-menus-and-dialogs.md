# Menus, dialogs and notifications

Menus are described with three values - `Action`, `Menu` and `Separator` - and the same description
serves the window's menu bar, context menus and pop-up menus. Dialogs are functions; notifications are
one-liners.

## Menu entries

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;

pulse::property<bool> word_wrap{true};
pulse::property<bool> dirty{false};

MenuItems file_menu{
    Action("&New", [&] { doc.clear(); }).shortcut("Ctrl+N").icon("icons/new.svg"),
    Action("&Save", [&] { save(); }).shortcut("Ctrl+S").enabled(dirty),   // follows a Bind<bool>
    Separator(),
    Menu("Open &recent", [&] { return recent_entries(); }),             // built each time it opens
    Action("&Quit", [] { App::quit(); }),
};
```

| Function | Meaning |
|---|---|
| `Action(label, fn = {})` | An item that runs `fn`. |
| `Separator()` | A line between groups. |
| `Menu(label, MenuItems)` | A submenu. |
| `Menu(label, std::function<MenuItems()>)` | A submenu whose entries are built when it opens (recent files, open windows). |

Modifiers of an entry:

| Modifier | Meaning |
|---|---|
| `.shortcut("Ctrl+S")` | Shown on the right; in a window menu it also works without opening the menu. `Ctrl` is Cmd on macOS. |
| `.enabled(Bind<bool>)` | A disabled entry is dimmed and does nothing (its shortcut neither). |
| `.checked(Model<bool>&)` | A check item: a click flips the property, then runs `fn` (if any). |
| `.checked(Bind<bool>)` | A check mark following a value or stream; `fn` decides what a click does. |
| `.radio()` | Draw the check mark as a dot (for groups of exclusive options). |
| `.icon(glyph)` | A glyph, SVG markup or an `.svg` file shown left of the label. |

`&` before a letter makes it the mnemonic: `Alt+F` opens `&File` in the menu bar, the letter opens an
entry of an open menu. Write `&&` for a literal ampersand.

## The window menu bar

```cpp
Window win("Notes", 900, 600);
win.menu({
    Menu("&File", file_menu),
    Menu("&Edit", {Action("&Undo", undo).shortcut("Ctrl+Z"), Action("&Redo", redo).shortcut("Ctrl+Y")}),
    Menu("&View", {Action("Word &wrap").checked(word_wrap)}),
});
```

- **macOS:** the menu becomes the native application menu bar while the window is active. The standard
  application menu (About, Hide, Hide Others, Show All, Quit) is added in front automatically.
- **Windows:** the bar is drawn at the top of the window in the current theme (a native Win32 menu bar is
  always light). `Alt` alone or `F10` moves the keyboard into it: arrows choose a menu, `Enter` or `↓`
  opens it, `Esc` leaves. `Alt+letter` opens a menu by its mnemonic.
- Shortcuts of all actions work at any time, also when the menu is closed. A disabled action's shortcut
  does nothing.
- Calling `menu(...)` again replaces the menu (for example after the language changed).

Inside an open menu: `↑`/`↓` move, `→` opens a submenu, `←` closes it (or moves to the neighbouring menu
of the bar), `Enter`/`Space` choose, `Esc` closes, typing a letter chooses the entry with that mnemonic
or first letter.

## Context menus

Any view gets one with a modifier:

```cpp
Text(note.title).context_menu({
    Action("Rename...", [&] { rename(note); }),
    Action("Delete", [&] { remove(note); }).icon("\xF0\x9F\x97\x91"),
});

// built when it opens - e.g. for the row under the mouse
List(notes, row).context_menu([&] {
  return MenuItems{Action("Open", [&] { open(selected.get()); })};
});
```

It opens with the right mouse button (after the click reached the view, so a list can select the row
first), `Shift+F10` or the Menu key. The nearest ancestor with a context menu answers.

`win.popup_menu(items, point)` opens a menu anywhere (window coordinates), for example under a "⋯"
button: `win.popup_menu(items, button->to_window({0, button->frame().h}))`.

## Message boxes

```cpp
dialogs::alert(&win, "Saved", "The file was written.");

if (dialogs::confirm(&win, "Delete the note?", "This cannot be undone.", "Delete", "Cancel", /*danger*/ true))
  remove(note);

dialogs::Message m;
m.title = "Unsaved changes";
m.text = "Save the document before closing?";
m.buttons = {"Save", "Don't save", "Cancel"};
m.icon = dialogs::Icon::Warning;
m.cancel_button = 2;
switch (dialogs::message(&win, m)) {
  case 0: save(); break;
  case 1: break;
  default: return false; // cancelled
}

if (auto name = dialogs::prompt(&win, "Rename", "New name:", note.title)) note.title = *name;
```

`message`, `alert`, `confirm` and `prompt` **block** until answered: the parent window ignores input
meanwhile and events keep being processed. `message_async` and `confirm_async` return at once and
deliver the answer to a callback on the UI thread - the natural form inside streams:

```cpp
dialogs::confirm_async(&win, "Quit?", "Unsaved changes will be lost.", [](bool yes) { if (yes) App::quit(); });
```

`Message` fields: `title`, `text`, `detail` (smaller text below), `buttons`, `icon`
(`None`, `Info`, `Warning`, `Error`, `Question`), `default_button` (Enter), `cancel_button` (Escape and
closing the window; by default the last button when there are several), `danger` (the default button is
destructive), `input` / `input_text` / `input_placeholder` (a text field; the answer's `text`).

On macOS message boxes are `NSAlert`s (sheets on the parent window for the async form). Elsewhere PulseUI
draws them in the current theme.

**Tests** answer dialogs without showing them: `dialogs::set_responder([](const dialogs::Message& m) {
return dialogs::Answer{0, ""}; })`, and `set_responder({})` to restore.

## Modal windows

```cpp
Window settings("Settings", 480, 360);
settings.content(settings_page());
settings.set_resizable(false);
settings.show_modal(main_window);   // above the main window, which ignores input until it closes
// or: settings.run_modal(&main_window);  - blocks until the window is closed
```

- `show_modal(parent)` returns at once; `run_modal(parent)` processes events until the window closes.
- On macOS a modal window is a sheet of its parent.
- `fit_content(max_width)` sizes a window to the preferred size of its content (dialogs do this).

## Sheets inside a window

`win.present(view)` shows a view centred over the window, dims the rest and blocks input to it - a
lightweight modal panel that is part of the window. `Esc` closes it, and so does
`win.close_popup(view)`.

```cpp
View* sheet = win.present(Panel{
    Text("Export").title(),
    Select({"PNG", "SVG", "PDF"}, format),
    HStack{Spacer(), Button("Export", [&] { do_export(); win.close_popup(sheet); }).primary()},
}.width(320));
```

Lower level: `win.open_popup(view, PopupOptions{...})` places a popup at a point, centred, bottom-right
or top-centre, with or without a backdrop and dismissal by an outside click; `win.open_dropdown(view,
anchor)` drops a popup down from a view (above it when there is no room below).

## Toasts

```cpp
toast(win, "Saved", ToastKind::Success);

ToastOptions o;
o.kind = ToastKind::Info;
o.action_label = "Undo";
o.action = [&] { restore(note); };
toast(win, "Note moved to the trash", o);
```

Toasts appear in the bottom-right corner, stack upwards, fade in and out, and close after
`duration` (3.5 s by default; `0ms` keeps them until closed). While the mouse is over a toast it stays.
The × closes it; the action button runs the action and closes it. Clicks elsewhere do not close toasts.
Kinds: `Info`, `Success`, `Warning`, `Error` (the colour of the stripe).

Next: [Collections](20-collections.md).
