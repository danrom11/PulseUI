# Collections: List, Table and TreeView

`ForEach` creates a view for every item, which is fine for dozens or hundreds of rows. Large data sets
need views that only create - or only paint - what is visible. These three scroll through 100 000 items
as smoothly as through 10.

## `List`: rows of views, virtualized

```cpp
pulse::property<std::vector<Contact>> contacts;
pulse::property<int> current{-1};

List(contacts, [](const Contact& c) {
  return HStack{ Text(c.name).flex(), Text(c.phone).dim() }.spacing(8).padding(Insets(10, 0));
})
    .row_height(32)
    .selected(current)                          // Model<int>&: two-way selection
    .on_activate([&](int i) { open(contacts.get()[i]); })
    .flex();
```

- Rows have a fixed height (`row_height`, 30 by default). Only the rows on screen exist as views; they
  are created and dropped while scrolling. `built_rows()` tells how many exist right now.
- The row function takes `(item)` or `(item, index)`, like `ForEach`.
- Selection: a click, `↑` `↓` `PageUp` `PageDown` `Home` `End`. `selected(Model<int>&)` binds it both ways;
  `on_select(fn)` reports changes. A double click or `Enter` activates (`on_activate`).
- When the list shrinks below the selection, the selection moves to the last item (and the bound
  property follows).
- `scroll_to_row(i)`, `set_scroll_offset(y)`.

## `Table`: columns of text

```cpp
struct Person { std::string name; int age; };
pulse::property<std::vector<Person>> people;
pulse::property<int> selected{-1};

Table(people, {
    Column<Person>{"Name", 200, [](const Person& p) { return p.name; }},
    Column<Person>{"Age", 80, [](const Person& p) { return std::to_string(p.age); }, {}, Align::RightCenter},
})
    .selected(selected)
    .sorted(0)                                    // initial sort column (and direction)
    .on_activate([&](int item) { edit(people.get()[item]); });
```

- Cells are painted directly (no view per cell), so a table of any size costs the same.
- A click on a header sorts by that column, a second click reverses. Numbers sort numerically, text
  case-insensitively; `Column::less` gives a custom order.
- Drag the edge of a header to resize a column.
- Selection and activation report **item indexes** (positions in your vector), not row positions, so
  they stay correct while the table is sorted.

## `TreeView`: a hierarchy

```cpp
pulse::property<std::vector<TreeItem>> files{{
    {"src", {{"main.cpp", {}, "src/main.cpp"}, {"app.cpp", {}, "src/app.cpp"}}, "src"},
    {"README.md", {}, "readme"},
}};
pulse::property<std::string> current;

TreeView(files).selected(current).on_activate([&](const TreeItem& it) { open(it.id); }).expanded();
```

- `TreeItem{label, children, id, detail}`: the `id` identifies the item (the label path when empty);
  `detail` is dimmed text on the right.
- Keys: `↑` `↓` move, `→` expands (or moves to the first child), `←` collapses (or moves to the parent),
  `Enter` activates. A click on the arrow toggles, a double click activates.
- `expand(id, open)`, `expand_all(open)`, `is_expanded(id)`, `select(id)`. Expansion state survives
  updates of the data.

## Choosing

| Need | Use |
|---|---|
| A few dozen rows of any shape, animated or with editable rows | `ForEach` (with a key) |
| Many rows of views of the same height | `List` |
| Many rows of text in columns, sortable | `Table` |
| A hierarchy | `TreeView` |

Next: [More widgets](21-more-widgets.md).
