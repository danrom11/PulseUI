# Drag and drop

Files and text can be dragged into PulseUI windows from Explorer, Finder or other applications, and out
of them into other applications.

## Dropping files on a view

```cpp
pulse::property<std::string> picture;

ImageView(picture.map([](auto& p) { return Image::load(p); }))
    .on_drop_files([&](const std::vector<std::string>& files) { picture = files.front(); },
                   {".png", ".jpg", ".jpeg"})          // optional: only these extensions
    .size(240, 160);

TextArea(notes).on_drop_text([&](const std::string& text) { notes = notes.get() + text; });
```

- While files are dragged over a view that takes them, the view is highlighted and the cursor shows a
  copy. Views that would not take the data (wrong extension) are not highlighted.
- The handler receives the files that match the extensions (all files when none are given), as
  absolute paths.
- The nearest view under the mouse that takes the data receives it.

Files dropped where no view takes them go to `Window::on_drop_files` when it is set:

```cpp
win.on_drop_files = [&](const std::vector<std::string>& files) { for (auto& f : files) open(f); };
```

## Dragging out of the application

```cpp
Text(file.name)
    .drag_source([&] { return DragData{{file.path}, ""}; })   // files and/or text
```

Pressing the mouse on the view and moving it a few pixels starts a system drag with the returned
`DragData{files, text}`. Drop it into Explorer or Finder (the file is copied), a text editor, a mail, or
another PulseUI window or view.

## Tests

`Harness::drop_files(id, files)` and `drop_text(id, text)` simulate a drop on a view;
`window.handle_drag(phase, point, data)` drives the individual phases; `window.last_drag()` returns
what the last drag out carried (in tests no system drag starts).

## Platform notes

- Windows: OLE drag and drop (`IDropTarget`, `DoDragDrop`); dragged files are offered as a shell data
  object (Explorer copies them), text as `CF_UNICODETEXT`.
- macOS: `NSDraggingDestination` (file URLs and strings) and `NSDraggingSource`.

Next: [Accessibility](24-accessibility.md).
