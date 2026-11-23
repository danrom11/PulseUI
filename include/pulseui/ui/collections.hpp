#pragma once
// Large collections: List (virtualized rows of views), Table (columns, sorting,
// selection) and TreeView (hierarchy). They only create or paint what is visible,
// so 100 000 items scroll as smoothly as 10.
#include <functional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include <pulseui/ui/view.hpp>

namespace pulseui {

namespace detail {
// Vertical scrolling shared by the collection views.
struct ScrollState {
  float offset = 0, content = 0, viewport = 0;
  bool dragging = false;
  float grab = 0, grab_offset = 0;
  float max() const { return content > viewport ? content - viewport : 0.f; }
  void clamp();
  Rect thumb(const Rect& track) const;
  void paint(Painter& p, const Rect& track) const;
  // wheel and thumb dragging; returns true when the event was used
  bool event(const InputEvent& e, const Rect& track, float wheel_step);
};
} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// List: one view per item, but only for the rows on screen.
//   List(contacts, [](const Contact& c) { return HStack{Text(c.name).flex(), Text(c.phone).dim()}; })
//       .row_height(32).selected(current).on_activate([](int i) { ... })
// Rows have a fixed height. Selection: click, Up/Down/PageUp/PageDown/Home/End;
// double click or Enter activates.
// ─────────────────────────────────────────────────────────────────────────────
class ListBase : public View {
public:
  int selected() const { return selected_; }
  void set_selected(int i, bool notify = true);
  void scroll_to_row(int i);
  std::size_t row_count() const { return count_; }
  std::size_t built_rows() const { return rows_.size(); } // views alive right now
  float row_height() const { return row_h_; }
  float scroll_offset() const { return sb_.offset; }
  void set_scroll_offset(float y);

  pulse::signal<int> selection_changed;
  pulse::signal<int> activated;

  Size measure_content(Size avail) override;
  void layout() override;
  void paint(Painter& p) override;
  void paint_overlay(Painter& p) override;
  bool on_event(InputEvent& e) override;
  bool hit_test_children(Point local) const override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

protected:
  ListBase();
  virtual Node make_row(std::size_t i) = 0;
  void set_count(std::size_t n); // the data changed: visible rows are rebuilt
  Rect track() const;
  int row_at(Point local) const;

  float row_h_ = 30;
  std::size_t count_ = 0;
  int selected_ = -1;
  int hover_row_ = -1;
  Model<int>* model_ = nullptr;
  std::vector<std::pair<std::size_t, View*>> rows_; // index -> live row view
  bool rebuild_ = false;
  detail::ScrollState sb_;
};

template <class T>
class List : public ViewT<List<T>, ListBase> {
public:
  using RowFn = std::function<Node(const T&, std::size_t)>;
  template <class F>
  List(Bind<std::vector<T>> items, F fn) : fn_(make(std::move(fn))) {
    this->bind(items, [](View& v, const std::vector<T>& l) {
      auto& self = static_cast<List&>(v);
      self.items_ = l;
      self.set_count(l.size());
    });
  }
  const char* type_name() const override { return "List"; }
  PULSEUI_MOD(List, row_height, (float h), this->row_h_ = h;)
  PULSEUI_MOD(List, selected, (Model<int>& m), this->model_ = &m;
              this->bind(Bind<int>(m), [](View& v, int i) { static_cast<List&>(v).set_selected(i, false); });)
  PULSEUI_MOD(List, on_select, (std::function<void(int)> fn), this->selection_changed.connect(std::move(fn));)
  PULSEUI_MOD(List, on_activate, (std::function<void(int)> fn), this->activated.connect(std::move(fn));)
  const std::vector<T>& items() const { return items_; }

protected:
  Node make_row(std::size_t i) override { return i < items_.size() ? fn_(items_[i], i) : Node(); }

private:
  template <class F>
  static RowFn make(F fn) {
    if constexpr (std::is_invocable_v<F&, const T&, std::size_t>) return RowFn(std::move(fn));
    else return [fn](const T& v, std::size_t) { return Node(fn(v)); };
  }
  std::vector<T> items_;
  RowFn fn_;
};

template <class Src, class F>
  requires detail::has_as_observable<Src>
List(Src&, F) -> List<typename Src::value_type::value_type>;
template <class U, class F>
List(pulse::observable<std::vector<U>>, F) -> List<U>;
template <class U, class F>
List(std::vector<U>, F) -> List<U>;

// ─────────────────────────────────────────────────────────────────────────────
// Table: rows and columns of text, painted directly (no view per cell).
//   Table(people, {
//       Column<Person>{"Name", 200, [](const Person& p) { return p.name; }},
//       Column<Person>{"Age", 80, [](const Person& p) { return std::to_string(p.age); }},
//   }).selected(index).on_activate([](int i) { ... })
// Click a header to sort (again to reverse), drag a header edge to resize.
// Numbers sort numerically. Selection and activation report item indexes.
// ─────────────────────────────────────────────────────────────────────────────
struct TableColumnInfo {
  std::string title;
  float width = 120;
  int align = Align::LeftCenter;
};

class TableBase : public View {
public:
  int selected() const { return selected_; } // item index, -1 = none
  void set_selected(int item, bool notify = true);
  void sort_by(int column, bool ascending = true);
  int sort_column() const { return sort_col_; }
  bool sort_ascending() const { return asc_; }
  std::size_t row_count() const { return order_.size(); }
  std::size_t item_at_row(std::size_t row) const { return order_[row]; }
  const std::string& cell(std::size_t item, std::size_t column) const { return cells_[item][column]; }
  float column_width(std::size_t column) const { return cols_[column].width; }
  void scroll_to_item(int item);

  pulse::signal<int> selection_changed;
  pulse::signal<int> activated;

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

protected:
  TableBase();
  void set_cells(std::vector<std::vector<std::string>> cells);
  virtual bool less(std::size_t column, std::size_t a, std::size_t b) const;
  void sort_rows();
  Rect track() const;
  int column_edge_at(float x) const;
  int column_at(float x) const;
  int row_at(float y) const;

  std::vector<TableColumnInfo> cols_;
  std::vector<std::vector<std::string>> cells_; // [item][column]
  std::vector<std::size_t> order_;              // row -> item
  int sort_col_ = -1;
  bool asc_ = true;
  float row_h_ = 26, header_h_ = 30;
  int selected_ = -1, hover_row_ = -1;
  int resizing_ = -1;
  float resize_from_ = 0, width_from_ = 0;
  Model<int>* model_ = nullptr;
  detail::ScrollState sb_;
};

template <class T>
struct Column {
  std::string title;
  float width = 120;
  std::function<std::string(const T&)> text;
  std::function<bool(const T&, const T&)> less = {}; // custom sort order (optional)
  int align = Align::LeftCenter;
};

template <class T>
class Table : public ViewT<Table<T>, TableBase> {
public:
  Table(Bind<std::vector<T>> items, std::vector<Column<T>> columns) : columns_(std::move(columns)) {
    for (auto& c : columns_) this->cols_.push_back({c.title, c.width, c.align});
    this->bind(items, [](View& v, const std::vector<T>& l) {
      auto& self = static_cast<Table&>(v);
      self.items_ = l;
      std::vector<std::vector<std::string>> cells(l.size());
      for (std::size_t i = 0; i < l.size(); ++i)
        for (auto& c : self.columns_) cells[i].push_back(c.text ? c.text(l[i]) : std::string());
      self.set_cells(std::move(cells));
    });
  }
  const char* type_name() const override { return "Table"; }
  PULSEUI_MOD(Table, row_height, (float h), this->row_h_ = h;)
  PULSEUI_MOD(Table, selected, (Model<int>& m), this->model_ = &m;
              this->bind(Bind<int>(m), [](View& v, int i) { static_cast<Table&>(v).set_selected(i, false); });)
  PULSEUI_MOD(Table, sorted, (int column, bool ascending = true), this->sort_col_ = column; this->asc_ = ascending;)
  PULSEUI_MOD(Table, on_select, (std::function<void(int)> fn), this->selection_changed.connect(std::move(fn));)
  PULSEUI_MOD(Table, on_activate, (std::function<void(int)> fn), this->activated.connect(std::move(fn));)
  const std::vector<T>& items() const { return items_; }

protected:
  bool less(std::size_t column, std::size_t a, std::size_t b) const override {
    if (column < columns_.size() && columns_[column].less) return columns_[column].less(items_[a], items_[b]);
    return TableBase::less(column, a, b);
  }

private:
  std::vector<Column<T>> columns_;
  std::vector<T> items_;
};

template <class Src>
  requires detail::has_as_observable<Src>
Table(Src&, std::vector<Column<typename Src::value_type::value_type>>) -> Table<typename Src::value_type::value_type>;
template <class U>
Table(pulse::observable<std::vector<U>>, std::vector<Column<U>>) -> Table<U>;
template <class U>
Table(std::vector<U>, std::vector<Column<U>>) -> Table<U>;

// ─────────────────────────────────────────────────────────────────────────────
// TreeView: a hierarchy with expandable nodes.
//   TreeView(files).selected(current_id).on_activate([](const TreeItem& it) { open(it.id); })
// Items are identified by `id` (or by their label path when id is empty).
// Keys: Up/Down move, Right expands, Left collapses / goes to the parent, Enter activates.
// ─────────────────────────────────────────────────────────────────────────────
struct TreeItem {
  std::string label;
  std::vector<TreeItem> children;
  std::string id;     // stable identity (optional)
  std::string detail; // dimmed text on the right (optional)
  bool operator==(const TreeItem&) const = default;
};

class TreeView : public ViewT<TreeView> {
public:
  explicit TreeView(Bind<std::vector<TreeItem>> roots);
  const char* type_name() const override { return "TreeView"; }
  PULSEUI_MOD(TreeView, selected, (Model<std::string>& id), model_ = &id;
              bind(Bind<std::string>(id), [](View& v, const std::string& s) { static_cast<TreeView&>(v).select(s, false); });)
  PULSEUI_MOD(TreeView, on_select, (std::function<void(const TreeItem&)> fn), selection_changed.connect(std::move(fn));)
  PULSEUI_MOD(TreeView, on_activate, (std::function<void(const TreeItem&)> fn), activated.connect(std::move(fn));)
  PULSEUI_MOD(TreeView, expanded, (bool all = true), expand_all_ = all;)
  PULSEUI_MOD(TreeView, row_height, (float h), row_h_ = h;)

  pulse::signal<TreeItem> selection_changed;
  pulse::signal<TreeItem> activated;

  void select(const std::string& id, bool notify = true);
  const std::string& selected_id() const { return selected_; }
  const TreeItem* selected_item() const;
  void expand(const std::string& id, bool open = true);
  void expand_all(bool open = true);
  bool is_expanded(const std::string& id) const { return expanded_.count(id) > 0; }
  std::size_t visible_rows() const { return rows_.size(); }
  static std::string key_of(const TreeItem& item, const std::string& parent_key);

  Size measure_content(Size avail) override;
  void paint(Painter& p) override;
  bool on_event(InputEvent& e) override;
  void accessibility(A11yInfo& info) const override;
  bool accessibility_action(A11yAction a, const std::string& arg) override;

private:
  struct Row {
    const TreeItem* item;
    std::string key, parent;
    int depth;
  };
  void set_roots(std::vector<TreeItem> roots);
  void flatten();
  int row_of(const std::string& key) const;
  void scroll_to_row(int r);
  Rect track() const;

  std::vector<TreeItem> roots_;
  std::vector<Row> rows_;
  std::set<std::string> expanded_;
  std::string selected_;
  Model<std::string>* model_ = nullptr;
  float row_h_ = 26, indent_ = 18;
  int hover_row_ = -1;
  bool expand_all_ = false, first_data_ = true;
  detail::ScrollState sb_;
};

} // namespace pulseui
