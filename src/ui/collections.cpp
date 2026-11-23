#include <pulseui/ui/collections.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/window.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pulseui {

namespace {
constexpr float kBar = 10; // scrollbar track width

// numbers compare as numbers ("9" < "10"), everything else as text, ignoring case
bool text_less(const std::string& a, const std::string& b) {
  char* ea = nullptr;
  char* eb = nullptr;
  const double da = std::strtod(a.c_str(), &ea), db = std::strtod(b.c_str(), &eb);
  const bool na = !a.empty() && ea && *ea == 0, nb = !b.empty() && eb && *eb == 0;
  if (na && nb) return da < db;
  if (na != nb) return na; // numbers first
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
  });
}
} // namespace

// ── scrolling ────────────────────────────────────────────────────────────────

void detail::ScrollState::clamp() { offset = std::clamp(offset, 0.f, max()); }

Rect detail::ScrollState::thumb(const Rect& track) const {
  if (content <= viewport || content <= 0) return {};
  const float h = std::max(24.f, track.h * viewport / content);
  const float y = track.y + (track.h - h) * (max() > 0 ? offset / max() : 0.f);
  return {track.x + 2, y, track.w - 4, h};
}

void detail::ScrollState::paint(Painter& p, const Rect& track) const {
  const Rect t = thumb(track);
  if (t.h > 0) p.fill_rect(t, th().scrollbar, t.w / 2);
}

bool detail::ScrollState::event(const InputEvent& e, const Rect& track, float wheel_step) {
  switch (e.type) {
    case InputEvent::Wheel: {
      if (max() <= 0) return false;
      const float before = offset;
      offset -= e.wheel_dy * wheel_step;
      clamp();
      return offset != before || true;
    }
    case InputEvent::MouseDown:
      if (!track.contains(e.pos) || max() <= 0) return false;
      if (!thumb(track).contains(e.pos)) { // jump: center the thumb on the click
        const Rect t = thumb(track);
        offset = (e.pos.y - track.y - t.h / 2) / std::max(1.f, track.h - t.h) * max();
        clamp();
      }
      dragging = true;
      grab = e.pos.y;
      grab_offset = offset;
      return true;
    case InputEvent::MouseMove:
      if (!dragging) return false;
      {
        const Rect t = thumb(track);
        offset = grab_offset + (e.pos.y - grab) / std::max(1.f, track.h - t.h) * max();
        clamp();
      }
      return true;
    case InputEvent::MouseUp:
      if (!dragging) return false;
      dragging = false;
      return true;
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// List
// ═════════════════════════════════════════════════════════════════════════════

ListBase::ListBase() {
  style_.clip = true;
  focusable_ = true;
  hover_repaint_ = true;
}

void ListBase::set_count(std::size_t n) {
  count_ = n;
  if (selected_ >= static_cast<int>(n)) {
    selected_ = n ? static_cast<int>(n) - 1 : -1;
    if (model_) model_->set(selected_); // keep the bound index valid
    selection_changed.emit(selected_);
  }
  rebuild_ = true;
  invalidate_layout();
}

void ListBase::set_selected(int i, bool notify) {
  i = count_ ? std::clamp(i, -1, static_cast<int>(count_) - 1) : -1;
  if (i == selected_) return;
  selected_ = i;
  invalidate();
  if (notify) {
    if (model_) model_->set(i);
    selection_changed.emit(i);
  }
}

void ListBase::set_scroll_offset(float y) {
  sb_.offset = y;
  sb_.clamp();
  invalidate_layout();
}

void ListBase::scroll_to_row(int i) {
  if (i < 0) return;
  const float top = static_cast<float>(i) * row_h_;
  if (top < sb_.offset) sb_.offset = top;
  else if (top + row_h_ > sb_.offset + sb_.viewport) sb_.offset = top + row_h_ - sb_.viewport;
  sb_.clamp();
  invalidate_layout();
}

Rect ListBase::track() const {
  const Rect b = bounds();
  return {b.w - kBar, 0, kBar, b.h};
}

int ListBase::row_at(Point local) const {
  const float y = local.y - content_rect().y + sb_.offset;
  if (y < 0) return -1;
  const int r = static_cast<int>(y / row_h_);
  return r < static_cast<int>(count_) ? r : -1;
}

Size ListBase::measure_content(Size avail) {
  // as tall as the rows (capped by the parent), at least a few rows
  const float h = std::min(static_cast<float>(count_) * row_h_, std::isinf(avail.h) ? 300.f : avail.h);
  return {std::isinf(avail.w) ? 200.f : avail.w, std::max(h, row_h_ * 3)};
}

void ListBase::layout() {
  const Rect cr = content_rect();
  sb_.viewport = cr.h;
  sb_.content = static_cast<float>(count_) * row_h_;
  sb_.clamp();
  const std::size_t first = static_cast<std::size_t>(std::max(0.f, sb_.offset / row_h_));
  const std::size_t last = std::min(count_, static_cast<std::size_t>(std::ceil((sb_.offset + cr.h) / row_h_)) + 1);
  // drop rows that scrolled away (or all of them when the data changed)
  for (auto it = rows_.begin(); it != rows_.end();) {
    if (rebuild_ || it->first < first || it->first >= last) {
      take(it->second);
      it = rows_.erase(it);
    } else {
      ++it;
    }
  }
  rebuild_ = false;
  for (std::size_t i = first; i < last; ++i) {
    if (std::any_of(rows_.begin(), rows_.end(), [i](const auto& r) { return r.first == i; })) continue;
    Node n = make_row(i);
    if (!n) continue;
    rows_.push_back({i, &add(std::move(n))});
  }
  const float w = cr.w - (sb_.max() > 0 ? kBar : 0);
  for (auto& [i, v] : rows_) {
    v->set_frame({cr.x, cr.y + static_cast<float>(i) * row_h_ - sb_.offset, w, row_h_});
    v->layout();
  }
}

void ListBase::paint(Painter& p) {
  const Theme& t = th();
  View::paint(p);
  const Rect cr = content_rect();
  auto row_rect = [&](int i) {
    return Rect{cr.x, cr.y + static_cast<float>(i) * row_h_ - sb_.offset, cr.w - (sb_.max() > 0 ? kBar : 0), row_h_};
  };
  if (hover_row_ >= 0 && hover_row_ != selected_ && hovered_) p.fill_rect(row_rect(hover_row_), t.control_hover, t.radius_small);
  if (selected_ >= 0) p.fill_rect(row_rect(selected_), focused() ? t.selection : t.selection.with_alpha(0.55f), t.radius_small);
}

void ListBase::paint_overlay(Painter& p) { sb_.paint(p, track()); }

bool ListBase::hit_test_children(Point local) const { return !track().contains(local) || sb_.max() <= 0; }

bool ListBase::on_event(InputEvent& e) {
  if (sb_.event(e, track(), row_h_ * 3)) {
    invalidate_layout();
    return true;
  }
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int r = row_at(e.pos);
      if (r != hover_row_) {
        hover_row_ = r;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return false;
      const int r = row_at(e.pos);
      if (r >= 0) set_selected(r);
      if (r >= 0 && e.clicks >= 2) activated.emit(r);
      return true;
    }
    case InputEvent::MouseUp:
      return true;
    case InputEvent::KeyDown: {
      const int page = std::max(1, static_cast<int>(sb_.viewport / row_h_) - 1);
      int to = selected_;
      switch (e.key) {
        case Key::Up: to = selected_ < 0 ? 0 : selected_ - 1; break;
        case Key::Down: to = selected_ + 1; break;
        case Key::PageUp: to = selected_ - page; break;
        case Key::PageDown: to = selected_ + page; break;
        case Key::Home: to = 0; break;
        case Key::End: to = static_cast<int>(count_) - 1; break;
        case Key::Enter:
          if (selected_ >= 0) activated.emit(selected_);
          return true;
        default: return false;
      }
      if (count_ == 0) return true;
      set_selected(std::clamp(to, 0, static_cast<int>(count_) - 1));
      scroll_to_row(selected_);
      return true;
    }
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Table
// ═════════════════════════════════════════════════════════════════════════════

TableBase::TableBase() {
  style_.clip = true;
  focusable_ = true;
  hover_repaint_ = true;
}

void TableBase::set_cells(std::vector<std::vector<std::string>> cells) {
  cells_ = std::move(cells);
  order_.resize(cells_.size());
  for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = i;
  sort_rows();
  if (selected_ >= static_cast<int>(cells_.size())) {
    selected_ = -1;
    if (model_) model_->set(-1);
    selection_changed.emit(-1);
  }
  invalidate_layout();
}

bool TableBase::less(std::size_t column, std::size_t a, std::size_t b) const {
  return text_less(cells_[a][column], cells_[b][column]);
}

void TableBase::sort_rows() {
  if (sort_col_ < 0 || sort_col_ >= static_cast<int>(cols_.size())) return;
  const std::size_t c = static_cast<std::size_t>(sort_col_);
  std::stable_sort(order_.begin(), order_.end(), [&](std::size_t a, std::size_t b) { return asc_ ? less(c, a, b) : less(c, b, a); });
}

void TableBase::sort_by(int column, bool ascending) {
  sort_col_ = column;
  asc_ = ascending;
  for (std::size_t i = 0; i < order_.size(); ++i) order_[i] = i;
  sort_rows();
  invalidate();
}

void TableBase::set_selected(int item, bool notify) {
  if (item >= static_cast<int>(cells_.size())) item = -1;
  if (item == selected_) return;
  selected_ = item;
  invalidate();
  if (notify) {
    if (model_) model_->set(item);
    selection_changed.emit(item);
  }
}

void TableBase::scroll_to_item(int item) {
  const auto it = std::find(order_.begin(), order_.end(), static_cast<std::size_t>(std::max(0, item)));
  if (it == order_.end()) return;
  const float top = static_cast<float>(it - order_.begin()) * row_h_;
  if (top < sb_.offset) sb_.offset = top;
  else if (top + row_h_ > sb_.offset + sb_.viewport) sb_.offset = top + row_h_ - sb_.viewport;
  sb_.clamp();
  invalidate();
}

Rect TableBase::track() const {
  const Rect b = bounds();
  return {b.w - kBar, header_h_, kBar, b.h - header_h_};
}

int TableBase::column_edge_at(float x) const {
  float edge = content_rect().x;
  for (std::size_t c = 0; c < cols_.size(); ++c) {
    edge += cols_[c].width;
    if (std::abs(x - edge) <= 4) return static_cast<int>(c);
  }
  return -1;
}

int TableBase::column_at(float x) const {
  float left = content_rect().x;
  for (std::size_t c = 0; c < cols_.size(); ++c) {
    const float w = c + 1 == cols_.size() ? 1e9f : cols_[c].width;
    if (x >= left && x < left + w) return static_cast<int>(c);
    left += cols_[c].width;
  }
  return -1;
}

int TableBase::row_at(float y) const {
  const float ry = y - content_rect().y - header_h_ + sb_.offset;
  if (ry < 0 || y < content_rect().y + header_h_) return -1;
  const int r = static_cast<int>(ry / row_h_);
  return r < static_cast<int>(order_.size()) ? r : -1;
}

Size TableBase::measure_content(Size avail) {
  float w = 0;
  for (auto& c : cols_) w += c.width;
  const float h = header_h_ + std::min(static_cast<float>(order_.size()) * row_h_, std::isinf(avail.h) ? 300.f : avail.h);
  return {w, std::max(h, header_h_ + row_h_ * 3)};
}

void TableBase::paint(Painter& p) {
  const Theme& t = th();
  View::paint(p);
  const Rect cr = content_rect();
  sb_.viewport = cr.h - header_h_;
  sb_.content = static_cast<float>(order_.size()) * row_h_;
  sb_.clamp();
  const float right = cr.right() - (sb_.max() > 0 ? kBar : 0);
  const Font& f = t.font;

  // rows
  p.save();
  p.clip({cr.x, cr.y + header_h_, cr.w, cr.h - header_h_});
  const std::size_t first = static_cast<std::size_t>(std::max(0.f, sb_.offset / row_h_));
  const std::size_t last = std::min(order_.size(), first + static_cast<std::size_t>(sb_.viewport / row_h_) + 2);
  for (std::size_t r = first; r < last; ++r) {
    const std::size_t item = order_[r];
    const Rect row{cr.x, cr.y + header_h_ + static_cast<float>(r) * row_h_ - sb_.offset, right - cr.x, row_h_};
    if (static_cast<int>(item) == selected_) p.fill_rect(row, focused() ? t.selection : t.selection.with_alpha(0.55f));
    else if (static_cast<int>(r) == hover_row_ && hovered_) p.fill_rect(row, t.control_hover);
    else if (r % 2 == 1) p.fill_rect(row, t.text.with_alpha(0.025f));
    float x = cr.x;
    for (std::size_t c = 0; c < cols_.size(); ++c) {
      const float w = c + 1 == cols_.size() ? std::max(cols_[c].width, right - x) : cols_[c].width;
      p.save();
      p.clip({x, row.y, w, row.h});
      p.text({x + 8, row.y, w - 16, row.h}, cells_[item][c], f, t.text, cols_[c].align);
      p.restore();
      x += w;
    }
  }
  p.restore();

  // header
  const Rect head{cr.x, cr.y, cr.w, header_h_};
  p.fill_rect(head, t.surface_alt);
  p.fill_rect({cr.x, cr.y + header_h_ - 1, cr.w, 1}, t.border);
  float x = cr.x;
  for (std::size_t c = 0; c < cols_.size(); ++c) {
    const float w = c + 1 == cols_.size() ? std::max(cols_[c].width, right - x) : cols_[c].width;
    Font hf = f;
    hf.weight = FontWeight::SemiBold;
    std::string title = cols_[c].title;
    if (static_cast<int>(c) == sort_col_) title += asc_ ? "  \xE2\x96\xB2" : "  \xE2\x96\xBC"; // ▲ ▼
    p.save();
    p.clip({x, cr.y, w, header_h_});
    p.text({x + 8, cr.y, w - 16, header_h_}, title, hf, t.text_dim, cols_[c].align);
    p.restore();
    if (c + 1 < cols_.size()) p.fill_rect({x + w - 1, cr.y + 6, 1, header_h_ - 12}, t.border);
    x += w;
  }
  sb_.paint(p, track());
  if (focused()) p.stroke_rect(bounds(), t.border_focus.with_alpha(0.6f), 1, style_.radius);
}

bool TableBase::on_event(InputEvent& e) {
  if (resizing_ < 0 && sb_.event(e, track(), row_h_ * 3)) {
    invalidate();
    return true;
  }
  const Rect cr = content_rect();
  const bool in_header = e.pos.y >= cr.y && e.pos.y < cr.y + header_h_;
  switch (e.type) {
    case InputEvent::MouseMove: {
      if (resizing_ >= 0) {
        cols_[static_cast<std::size_t>(resizing_)].width = std::max(30.f, width_from_ + e.pos.x - resize_from_);
        invalidate_layout();
        return true;
      }
      cursor_ = in_header && column_edge_at(e.pos.x) >= 0 ? Cursor::SizeWE : Cursor::Arrow;
      const int r = in_header ? -1 : row_at(e.pos.y);
      if (r != hover_row_) {
        hover_row_ = r;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return false;
      if (in_header) {
        if (const int edge = column_edge_at(e.pos.x); edge >= 0) {
          resizing_ = edge;
          resize_from_ = e.pos.x;
          width_from_ = cols_[static_cast<std::size_t>(edge)].width;
        }
        return true;
      }
      const int r = row_at(e.pos.y);
      if (r >= 0) {
        set_selected(static_cast<int>(order_[static_cast<std::size_t>(r)]));
        if (e.clicks >= 2) activated.emit(selected_);
      }
      return true;
    }
    case InputEvent::MouseUp: {
      if (resizing_ >= 0) {
        resizing_ = -1;
        return true;
      }
      if (in_header && e.button == MouseButton::Left) { // sort by the clicked column
        const int c = column_at(e.pos.x);
        if (c >= 0) sort_by(c, c == sort_col_ ? !asc_ : true);
      }
      return true;
    }
    case InputEvent::KeyDown: {
      if (order_.empty()) return false;
      const auto pos = std::find(order_.begin(), order_.end(), static_cast<std::size_t>(std::max(0, selected_)));
      int row = selected_ < 0 || pos == order_.end() ? -1 : static_cast<int>(pos - order_.begin());
      const int page = std::max(1, static_cast<int>(sb_.viewport / row_h_) - 1);
      switch (e.key) {
        case Key::Up: row = std::max(0, row - 1); break;
        case Key::Down: row = row + 1; break;
        case Key::PageUp: row = row - page; break;
        case Key::PageDown: row = row + page; break;
        case Key::Home: row = 0; break;
        case Key::End: row = static_cast<int>(order_.size()) - 1; break;
        case Key::Enter:
          if (selected_ >= 0) activated.emit(selected_);
          return true;
        default: return false;
      }
      row = std::clamp(row, 0, static_cast<int>(order_.size()) - 1);
      set_selected(static_cast<int>(order_[static_cast<std::size_t>(row)]));
      scroll_to_item(selected_);
      return true;
    }
    default:
      return false;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// TreeView
// ═════════════════════════════════════════════════════════════════════════════

TreeView::TreeView(Bind<std::vector<TreeItem>> roots) {
  style_.clip = true;
  focusable_ = true;
  hover_repaint_ = true;
  bind(roots, [](View& v, const std::vector<TreeItem>& r) { static_cast<TreeView&>(v).set_roots(r); });
}

std::string TreeView::key_of(const TreeItem& item, const std::string& parent_key) {
  return !item.id.empty() ? item.id : (parent_key.empty() ? "" : parent_key + "/") + item.label;
}

void TreeView::set_roots(std::vector<TreeItem> roots) {
  roots_ = std::move(roots);
  if (first_data_ && expand_all_) {
    std::function<void(const std::vector<TreeItem>&, const std::string&)> all = [&](const std::vector<TreeItem>& items,
                                                                                    const std::string& parent) {
      for (auto& it : items) {
        const std::string k = key_of(it, parent);
        if (!it.children.empty()) expanded_.insert(k);
        all(it.children, k);
      }
    };
    all(roots_, "");
  }
  first_data_ = false;
  flatten();
}

void TreeView::flatten() {
  rows_.clear();
  std::function<void(const std::vector<TreeItem>&, const std::string&, int)> walk = [&](const std::vector<TreeItem>& items,
                                                                                         const std::string& parent, int depth) {
    for (auto& it : items) {
      const std::string k = key_of(it, parent);
      rows_.push_back({&it, k, parent, depth});
      if (!it.children.empty() && expanded_.count(k)) walk(it.children, k, depth + 1);
    }
  };
  walk(roots_, "", 0);
  invalidate_layout();
}

int TreeView::row_of(const std::string& key) const {
  for (std::size_t i = 0; i < rows_.size(); ++i)
    if (rows_[i].key == key) return static_cast<int>(i);
  return -1;
}

const TreeItem* TreeView::selected_item() const {
  const int r = row_of(selected_);
  return r >= 0 ? rows_[static_cast<std::size_t>(r)].item : nullptr;
}

void TreeView::select(const std::string& id, bool notify) {
  if (id == selected_) return;
  selected_ = id;
  invalidate();
  if (notify) {
    if (model_) model_->set(id);
    if (const TreeItem* it = selected_item()) selection_changed.emit(*it);
  }
}

void TreeView::expand(const std::string& id, bool open) {
  if (open) expanded_.insert(id);
  else expanded_.erase(id);
  flatten();
}

void TreeView::expand_all(bool open) {
  expanded_.clear();
  if (open) {
    expand_all_ = true;
    first_data_ = true;
    set_roots(roots_);
  } else {
    flatten();
  }
}

Rect TreeView::track() const {
  const Rect b = bounds();
  return {b.w - kBar, 0, kBar, b.h};
}

void TreeView::scroll_to_row(int r) {
  if (r < 0) return;
  const float top = static_cast<float>(r) * row_h_;
  if (top < sb_.offset) sb_.offset = top;
  else if (top + row_h_ > sb_.offset + sb_.viewport) sb_.offset = top + row_h_ - sb_.viewport;
  sb_.clamp();
  invalidate();
}

Size TreeView::measure_content(Size avail) {
  const float h = std::min(static_cast<float>(rows_.size()) * row_h_, std::isinf(avail.h) ? 300.f : avail.h);
  return {std::isinf(avail.w) ? 220.f : avail.w, std::max(h, row_h_ * 3)};
}

void TreeView::paint(Painter& p) {
  const Theme& t = th();
  View::paint(p);
  const Rect cr = content_rect();
  sb_.viewport = cr.h;
  sb_.content = static_cast<float>(rows_.size()) * row_h_;
  sb_.clamp();
  const float right = cr.right() - (sb_.max() > 0 ? kBar : 0);
  const std::size_t first = static_cast<std::size_t>(std::max(0.f, sb_.offset / row_h_));
  const std::size_t last = std::min(rows_.size(), first + static_cast<std::size_t>(cr.h / row_h_) + 2);
  for (std::size_t r = first; r < last; ++r) {
    const Row& row = rows_[r];
    const Rect rr{cr.x, cr.y + static_cast<float>(r) * row_h_ - sb_.offset, right - cr.x, row_h_};
    if (row.key == selected_) p.fill_rect(rr, focused() ? t.selection : t.selection.with_alpha(0.55f), t.radius_small);
    else if (static_cast<int>(r) == hover_row_ && hovered_) p.fill_rect(rr, t.control_hover, t.radius_small);
    const float x = rr.x + 6 + static_cast<float>(row.depth) * indent_;
    if (!row.item->children.empty()) { // disclosure triangle
      const float cx = x + 6, cy = rr.y + rr.h / 2;
      if (expanded_.count(row.key)) p.fill_polygon({{cx - 4, cy - 2}, {cx + 4, cy - 2}, {cx, cy + 3}}, t.text_dim);
      else p.fill_polygon({{cx - 2, cy - 4}, {cx + 3, cy}, {cx - 2, cy + 4}}, t.text_dim);
    }
    const float dw = row.item->detail.empty() ? 0.f : std::min(rr.w * 0.4f, measure_text(row.item->detail, t.font_small, 0).w + 8);
    p.text({x + 16, rr.y, rr.right() - x - 22 - dw, rr.h}, row.item->label, t.font, t.text, Align::LeftCenter);
    if (dw > 0) p.text({rr.right() - dw - 6, rr.y, dw, rr.h}, row.item->detail, t.font_small, t.text_dim, Align::RightCenter);
  }
  sb_.paint(p, track());
}

bool TreeView::on_event(InputEvent& e) {
  if (sb_.event(e, track(), row_h_ * 3)) {
    invalidate();
    return true;
  }
  const Rect cr = content_rect();
  auto row_at = [&](float y) {
    const float ry = y - cr.y + sb_.offset;
    const int r = ry < 0 ? -1 : static_cast<int>(ry / row_h_);
    return r < static_cast<int>(rows_.size()) ? r : -1;
  };
  switch (e.type) {
    case InputEvent::MouseMove: {
      const int r = row_at(e.pos.y);
      if (r != hover_row_) {
        hover_row_ = r;
        invalidate();
      }
      return false;
    }
    case InputEvent::MouseDown: {
      if (e.button != MouseButton::Left) return false;
      const int r = row_at(e.pos.y);
      if (r < 0) return true;
      const Row row = rows_[static_cast<std::size_t>(r)];
      const float tri = cr.x + 6 + static_cast<float>(row.depth) * indent_;
      const bool on_triangle = e.pos.x >= tri - 2 && e.pos.x <= tri + 16;
      if (!row.item->children.empty() && (on_triangle || e.clicks >= 2)) expand(row.key, !expanded_.count(row.key));
      select(row.key);
      if (e.clicks >= 2) {
        if (const TreeItem* it = selected_item()) activated.emit(*it);
      }
      return true;
    }
    case InputEvent::MouseUp:
      return true;
    case InputEvent::KeyDown: {
      if (rows_.empty()) return false;
      int r = row_of(selected_);
      switch (e.key) {
        case Key::Up: r = std::max(0, r - 1); break;
        case Key::Down: r = std::min(static_cast<int>(rows_.size()) - 1, r + 1); break;
        case Key::Home: r = 0; break;
        case Key::End: r = static_cast<int>(rows_.size()) - 1; break;
        case Key::Right:
          if (r >= 0 && !rows_[static_cast<std::size_t>(r)].item->children.empty()) {
            const std::string k = rows_[static_cast<std::size_t>(r)].key;
            if (!expanded_.count(k)) expand(k, true);
            else if (r + 1 < static_cast<int>(rows_.size())) ++r;
          }
          break;
        case Key::Left:
          if (r >= 0) {
            const std::string k = rows_[static_cast<std::size_t>(r)].key;
            if (expanded_.count(k)) expand(k, false);
            else if (const int pr = row_of(rows_[static_cast<std::size_t>(r)].parent); pr >= 0) r = pr;
          }
          break;
        case Key::Enter:
          if (const TreeItem* it = selected_item()) activated.emit(*it);
          return true;
        default:
          return false;
      }
      if (r >= 0 && r < static_cast<int>(rows_.size())) {
        select(rows_[static_cast<std::size_t>(r)].key);
        scroll_to_row(r);
      }
      return true;
    }
    default:
      return false;
  }
}

} // namespace pulseui
