// Headless-ish UI tests: build view trees in a (hidden) window, drive them with
// synthetic input and check that state flows both ways.
#include <cassert>
#include <cctype>
#include <iostream>

#include <pulseui/pulseui.hpp>

using namespace pulseui;

static InputEvent mouse(InputEvent::Type t, Point p) {
  InputEvent e;
  e.type = t;
  e.pos = e.window_pos = p;
  e.button = MouseButton::Left;
  return e;
}

static void click(Window& w, View* v) {
  const Point c = v->to_window(v->bounds().center());
  w.send(mouse(InputEvent::MouseMove, c));
  w.send(mouse(InputEvent::MouseDown, c));
  w.send(mouse(InputEvent::MouseUp, c));
}

static void type(Window& w, const std::string& s) {
  InputEvent e;
  e.type = InputEvent::Text;
  e.text = s;
  w.send(e);
}

int main() {
  App app;

  // 1) stack layout
  {
    Window w("t", 400, 300);
    w.resize(400, 300);
    w.handle_resize({400, 300});
    w.content(VStack{Text("a").id("a").height(20), Text("b").id("b").flex(), Text("c").id("c").height(30)}
                  .spacing(10)
                  .padding(10));
    w.grab(); // forces layout
    auto* a = w.find("a");
    auto* b = w.find("b");
    auto* c = w.find("c");
    assert(a && b && c);
    assert(a->frame().y == 10 && a->frame().h == 20);
    assert(a->frame().w == 380);
    assert(b->frame().y == 40);
    assert(c->frame().bottom() == 290);
    assert(b->frame().h == 290 - 30 - 10 - 40);
  }

  // 2) bindings: property -> view, and button click -> property
  {
    pulse::property<int> count{0};
    Window w("t", 300, 200);
    w.handle_resize({300, 200});
    w.content(VStack{
        Text(count.map([](int c) { return std::to_string(c); })).id("label"),
        Button("+", [&] { count = count + 1; }).id("inc"),
    });
    w.grab();
    auto* label = w.find<Text>("label");
    assert(label->text() == "0");
    click(w, w.find("inc"));
    click(w, w.find("inc"));
    assert(count.get() == 2);
    assert(label->text() == "2");
    count = 10;
    assert(label->text() == "10");
  }

  // 3) two-way TextField / Checkbox / Slider
  {
    pulse::property<std::string> name{"x"};
    pulse::property<bool> flag{false};
    pulse::property<double> vol{0.5};
    Window w("t", 300, 200);
    w.handle_resize({300, 200});
    w.content(VStack{TextField(name).id("tf"), Checkbox("flag", flag).id("cb"), Slider(vol).id("sl")});
    w.grab();
    auto* tf = w.find<TextField>("tf");
    assert(tf->text() == "x");
    click(w, tf);
    assert(w.focus_view() == tf);
    type(w, "yz");
    assert(name.get() == "xyz");
    name = "reset";
    assert(tf->text() == "reset");

    click(w, w.find("cb"));
    assert(flag.get());
    flag = false;
    assert(!w.find<Checkbox>("cb")->checked());

    auto* sl = w.find<Slider>("sl");
    const Rect r = sl->frame();
    const Point right = sl->to_window({r.w - 8, r.h / 2});
    w.send(mouse(InputEvent::MouseDown, right));
    w.send(mouse(InputEvent::MouseUp, right));
    assert(vol.get() > 0.95);
  }

  // 4) ForEach + Show react to list / flag changes; unmount cancels bindings
  {
    pulse::property<std::vector<std::string>> items{{"a", "b"}};
    pulse::property<bool> shown{true};
    auto w = std::make_unique<Window>("t", 300, 300);
    w->handle_resize({300, 300});
    w->content(VStack{
        ForEach(items, [](const std::string& s) { return Text(s); }).id("list"),
        Show(shown, Text("yes").id("yes"), Text("no").id("no")),
    });
    w->grab();
    assert(w->find("list")->children().size() == 2);
    items.update([](auto& v) { v.push_back("c"); });
    assert(w->find("list")->children().size() == 3);
    assert(w->find("yes")->is_visible() && !w->find("no")->is_visible());
    shown = false;
    assert(!w->find("yes")->is_visible() && w->find("no")->is_visible());
    w.reset();
    items.update([](auto& v) { v.push_back("d"); }); // must not crash: bindings are gone
    assert(items.changed.slot_count() == 0);
  }

  // 4b) spacer inside a ForEach row in a Scroll pushes the trailing view to the edge
  {
    pulse::property<std::vector<std::string>> items{{"a"}};
    Window w("t", 400, 300);
    w.handle_resize({400, 300});
    pulse::computed<std::vector<std::string>> vis([&] { return items.get(); }, items);
    w.content(VStack{Text("t").title(), Scroll(ForEach(vis, [](const std::string& s) {
                       return HStack{Checkbox(s, false).on_change([](bool) {}).id("cb"), Spacer().id("sp"), Button("x", [] {}).flat().id("x")}
                           .padding(Insets(8, 2));
                     }).id("list")).flex()}.spacing(12).padding(10));
    w.grab();
    auto* x = w.find("x");
    assert(x->to_window({0, 0}).x + x->frame().w > 380);
  }

  // 5) window input stream
  {
    Window w("t", 200, 100);
    int downs = 0;
    auto sub = (w.input() | pulse::filter([](const InputEvent& e) { return e.type == InputEvent::MouseDown; }))
                   .subscribe([&](const InputEvent&) { ++downs; });
    w.send(mouse(InputEvent::MouseDown, {5, 5}));
    w.send(mouse(InputEvent::MouseUp, {5, 5}));
    assert(downs == 1);
  }

  // 6) shortcuts
  {
    Window w("t", 200, 100);
    int hits = 0;
    w.shortcut("Ctrl+S", [&] { ++hits; });
    InputEvent e;
    e.type = InputEvent::KeyDown;
    e.key = Key::S;
    e.mods = Mod::Primary;
    w.send(e);
    assert(hits == 1);
  }

  // 7) json round trip
  {
    auto v = json::Value::parse(R"({"a": [1, 2.5, "x\n"], "b": {"c": true, "d": null}})");
    assert(v);
    assert((*v)["a"].items().size() == 3);
    assert((*v)["b"]["c"].as_bool());
    auto again = json::Value::parse(v->dump());
    assert(again && again->dump() == v->dump());
  }

  // 8) forms: save -> load round trip, codegen
  {
    VStack root{Text("Hello").id("title").font_size(22), HStack{Button("OK").primary().id("ok"), Checkbox("Remember", true)},
                Slider(0.25, 0, 10)};
    root.spacing(14).padding(20);
    auto j = form_to_json("Login", 320, 200, root);
    auto f = form_from_json(*json::Value::parse(j.dump()));
    assert(f && f->title == "Login" && f->width == 320);
    View* r = f->root.get();
    assert(std::string(r->type_name()) == "VStack");
    auto* title = r->find<Text>("title");
    assert(title && title->text() == "Hello" && title->font_size_value() == 22);
    auto* ok = r->find<Button>("ok");
    assert(ok && ok->kind_value() == ButtonKind::Primary);
    assert(view_to_json(*r).dump() == view_to_json(root).dump());
    const std::string code = generate_cpp(root, "make_login");
    assert(code.find("Text(\"Hello\")") != std::string::npos && code.find(".id(\"title\")") != std::string::npos);
    assert(code.find(".font_size(22)") != std::string::npos);
    assert(code.find(".spacing(14)") != std::string::npos);
    assert(code.find("Checkbox(\"Remember\", true)") != std::string::npos);
    const std::string expr = generate_cpp_expression(root);
    assert(expr.rfind("VStack{", 0) == 0 && expr.find("#pragma") == std::string::npos);
  }

  // 9) CodeEditor: typing, auto-indent, undo, two-way model, diagnostics
  {
    pulse::property<std::string> src{"int main() {"};
    Window w("t", 500, 300);
    w.handle_resize({500, 300});
    w.content(VStack{CodeEditor(src).id("ed").flex()}.padding(0));
    w.grab();
    auto* ed = w.find<CodeEditor>("ed");
    assert(ed && ed->line_count() == 1);
    click(w, ed);
    InputEvent end_key;
    end_key.type = InputEvent::KeyDown;
    end_key.key = Key::End;
    end_key.mods = Mod::Primary;
    w.send(end_key);
    InputEvent enter;
    enter.type = InputEvent::KeyDown;
    enter.key = Key::Enter;
    w.send(enter);
    type(w, "return 0;");
    assert(src.get() == "int main() {\n    return 0;");
    InputEvent undo;
    undo.type = InputEvent::KeyDown;
    undo.key = Key::Z;
    undo.mods = Mod::Primary;
    w.send(undo);
    assert(src.get() == "int main() {\n    ");
    src = "a\tb\r\nc";
    assert(ed->text() == "a   b\nc" && ed->line_count() == 2);
    ed->set_diagnostics({{2, 1, Diagnostic::Error, "boom"}});
    ed->go_to(2, 1);
    assert(ed->caret_line() == 2);
    w.grab(); // paints squiggles without crashing
    CodeEditor log;
    log.read_only();
    log.append("line1\nli");
    log.append("ne2\n");
    assert(log.text() == "line1\nline2\n");
  }

  // CodeEditor completion: a pluggable provider drives the popup
  {
    pulse::property<std::string> src;
    Window w("t", 500, 300);
    w.handle_resize({500, 300});
    w.content(VStack{CodeEditor(src)
                         .completion([](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
                           CompletionResult r;
                           std::size_t from = col;
                           while (from > 0 && std::isalnum(static_cast<unsigned char>(lines[line][from - 1]))) --from;
                           r.replace_from = from;
                           const std::string prefix = lines[line].substr(from, col - from);
                           for (const char* word : {"format", "foreach", "float"})
                             if (std::string(word).rfind(prefix, 0) == 0) r.items.push_back({word, "", CompletionItem::Function});
                           return r;
                         })
                         .id("ed")
                         .flex()});
    w.grab();
    auto* ed = w.find<CodeEditor>("ed");
    click(w, ed);
    type(w, "f");
    assert(!ed->completion_open()); // one character: not yet
    type(w, "o");
    assert(ed->completion_open() && ed->completion_items().size() == 2);
    InputEvent down;
    down.type = InputEvent::KeyDown;
    down.key = Key::Down;
    w.send(down);
    InputEvent enter;
    enter.type = InputEvent::KeyDown;
    enter.key = Key::Enter;
    w.send(enter);
    assert(!ed->completion_open());
    assert(src.get() == "foreach");
    w.grab(); // paints the editor without crashing
  }

  std::cout << "[pulseui_tests] OK\n";
  return 0;
}
