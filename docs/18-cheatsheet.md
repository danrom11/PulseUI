# 18. Cheat sheet

```cpp
#include <pulseui/pulseui.hpp>
using namespace pulseui;
```

## App and window

```cpp
int main(int argc, char** argv) {
  App app(argc, argv);
  Window win("Title", 640, 480);          // title: value or stream
  win.content(VStack{ ... });
  win.set_min_size(400, 300);
  win.shortcut("Ctrl+S", save);           // Ctrl = Cmd on macOS
  win.on_close = [] { return true; };     // false = veto
  win.show();
  return app.run();
}
// or: return run("Title", 640, 480, root);

App::post(fn);  App::ui();  App::after(500ms, fn);  App::every(1s, fn);  App::quit();
App::set_clipboard(s);  App::clipboard();
dialogs::open_file(&win, "Open", {{"Text", "*.txt"}});   dialogs::save_file(&win, "Save", {}, "a.txt");
win.find<Button>("ok");  win.focus_view();  win.open_popup(node, at);  win.input();  win.screenshot("a.png");
```

## State

```cpp
pulse::property<T> p{init};          p = v;  p.get();  p.update([](T& v) { ... });  p.map(f);
pulse::computed<R> c([&] { ... }, p1, p2);
pulse::signal<Args...> s;            s.connect(f);  s.connect(App::ui(), f);  s(args...);
theme() = Theme::light();            th().accent;
```

## Layout

```cpp
VStack{ a, b }.spacing(8).padding(12).align_items(Alignment::Center).justify(Justify::SpaceBetween)
HStack{ a, Spacer(), b }
Panel{ ... }                                  // card
Layer{ base, overlay.position(10, 10) }
Scroll(content).flex()          Scroll(content).horizontal()
Split(left, right, Orientation::Horizontal, 260).min_pane(120)
Tabs{ Tab("A", page_a), Tab("B", page_b) }.selected(index)
Show(cond, then_view, else_view)
ForEach(list, [](const T& item, std::size_t i) { return Text(item.name); }).horizontal()
Divider()   Divider(Orientation::Vertical)   Spacer()   Spacer(16)
```

## Widgets

```cpp
Text("x").font_size(18).weight(FontWeight::SemiBold).bold().title().dim().mono().wrap()
         .text_align(Align::Center).color(c).bind_text(stream)
Button("Save", fn).primary() .flat() .danger() .icon("⚙") .bind_label(stream)   btn->clicked.connect(f)
TextField(model).placeholder("...").password().on_change(f).on_submit(f).on_commit(f)
Checkbox("Label", model)   Toggle("Label", model)   .on_change([](bool) {})
Slider(model, min, max).step(1)   ProgressBar(fraction)
Select({"A", "B"}, index_model).bind_options(stream)   Segmented({"A", "B"}, index_model)
ImageView(std::string("a.png"))   ImageView(image_stream)
Canvas([](Painter& p, const Rect& r) { ... }).redraw_on(src)
CodeEditor(model).highlighter(h).completion(c).diagnostics(d).read_only().line_numbers(false)
```

## Modifiers on every view

```cpp
.id("x") .padding(8) .padding(Insets(h, v)) .width(w) .height(h) .size(w, h)
.min_width(w) .max_width(w) .min_height(h) .max_height(h) .flex() .flex(2) .align(Alignment::End)
.position(x, y) .background(c) .border(c, 1) .radius(6) .clip() .opacity(0.5f)
.visible(b) .enabled(b) .tooltip(s) .cursor(Cursor::Hand) .focusable()
.on_click(f) .on_hover(f) .on_event_fn(f) .ref(ptr) .on_mount_fn(f)
// b, c, s: a value or a stream (property, computed, observable, p.map(...))
```

## Binding rules

| Pass | Meaning |
|---|---|
| a value | constant |
| a stream (`property`, `computed`, `observable`, `p.map(f)`) | one-way: the view follows |
| a model (`property&` to an editor, or `.model(p)`) | two-way |

## Painter

```cpp
p.save(); p.restore(); p.translate(dx, dy); p.clip(r); p.set_opacity(a);
p.fill_rect(r, c, radius); p.stroke_rect(r, c, w, radius); p.fill_ellipse(r, c); p.stroke_ellipse(r, c, w);
p.line(a, b, c, w); p.polyline(pts, c, w, closed); p.fill_polygon(pts, c); p.fill_gradient(r, top, bottom, radius);
p.text(r, "s", font, c, Align::Center, wrap); p.image(img, r, opacity);
measure_text("s", font, max_w); line_height(font);
```

## Colors

```cpp
Color::hex(0x3D7EFF)  Color::hex(0, 0.5f)  Color::rgb8(61, 126, 255)  Color::parse("#3d7eff")
c.with_alpha(0.5f)  c.lighter(0.2f)  c.darker(0.2f)  Color::mix(a, b, 0.3f)  c.to_hex()  // "#3D7EFF"
```

## Forms

```cpp
// designed in Pulse Layer (bindings set in the Properties panel)
class MainWindow {
  friend struct Ui::MainWindow;
  pulse::property<std::string> email;  void submit();      // bound by the form
  Ui::MainWindow ui_;                                       // ui_.email, ui_.submit... (typed)
  Window window_{Ui::MainWindow::form_title, Ui::MainWindow::form_width, Ui::MainWindow::form_height};
public:
  MainWindow() { ui_.setup(window_, *this); }
};
win.get<TextField>("email");          // strict lookup (throws); win.find<T>() returns nullptr

auto f = load_form("x.pform", &err);  Window w(f->title, f->width, f->height);  w.content(std::move(f->root));
save_form(path, title, w, h, root);   generate_cpp(root, "make_x");   generate_cpp_expression(root);
meta::register_type(info);            meta::create("Button");        meta::set(view, "label", std::string("OK"));
```

## Testing

```cpp
#include <pulseui/testing.hpp>
testing::Harness t(window);                                   // hidden window, laid out offscreen
t.click("ok").fill("email", "a@b.c").press("Ctrl+S").type("x").drag("slider", {50, 0}).scroll("list", 3);
t.text("status");  t.visible("x");  t.enabled("ok");  t.view("id");
t.wait_until([&] { return done.get(); }, 5s);  t.run_for(100ms);
assert(t.matches_snapshot("tests/snapshots/login.png"));     // PULSEUI_UPDATE_SNAPSHOTS=1 rewrites
```

## Environment

`PULSEUI_THEME=light|dark` · `PULSEUI_SCREENSHOT=out.png` · `PULSEUI_UPDATE_SNAPSHOTS=1`
