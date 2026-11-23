// UI tests: real view trees in hidden windows, driven with pulseui::testing.
#include <cassert>
#include <cctype>
#include <filesystem>
#include <iostream>

#include <pulseui/pulseui.hpp>
#include <pulseui/testing.hpp>

using namespace pulseui;
using pulseui::testing::Harness;
using namespace std::chrono_literals;

template <class F>
static bool throws(F&& f) {
  try { f(); } catch (const std::runtime_error&) { return true; }
  return false;
}

static int run_tests();

int main() {
  try {
    return run_tests();
  } catch (const std::exception& e) {
    std::cerr << "[pulseui_tests] exception: " << e.what() << "\n";
    return 1;
  }
}

static int run_tests() {
  App app;

  // 1) stack layout
  {
    Window w("t", 400, 300);
    w.content(VStack{Text("a").id("a").height(20), Text("b").id("b").flex(), Text("c").id("c").height(30)}
                  .spacing(10)
                  .padding(10));
    Harness t(w, {400, 300});
    auto& a = t.view("a");
    auto& b = t.view("b");
    auto& c = t.view("c");
    assert(a.frame().y == 10 && a.frame().h == 20);
    assert(a.frame().w == 380);
    assert(b.frame().y == 40);
    assert(c.frame().bottom() == 290);
    assert(b.frame().h == 290 - 30 - 10 - 40);
  }

  // 2) bindings: property -> view, and button click -> property
  {
    pulse::property<int> count{0};
    Window w("t", 300, 200);
    w.content(VStack{
        Text(count.map([](int c) { return std::to_string(c); })).id("label"),
        Button("+", [&] { count = count + 1; }).id("inc"),
    });
    Harness t(w);
    assert(t.text("label") == "0");
    t.click("inc").click("inc");
    assert(count.get() == 2);
    assert(t.text("label") == "2");
    count = 10;
    assert(t.text("label") == "10");
  }

  // 3) two-way TextField / Checkbox / Slider
  {
    pulse::property<std::string> name{"x"};
    pulse::property<bool> flag{false};
    pulse::property<double> vol{0.5};
    Window w("t", 300, 200);
    w.content(VStack{TextField(name).id("tf"), Checkbox("flag", flag).id("cb"), Slider(vol).id("sl")});
    Harness t(w);
    assert(t.text("tf") == "x");
    t.click("tf");
    assert(w.focus_view() == &t.view("tf"));
    t.type("yz");
    assert(name.get() == "xyz");
    t.fill("tf", "replaced");
    assert(name.get() == "replaced");
    t.fill("tf", "");
    assert(name.get().empty());
    name = "reset";
    assert(t.text("tf") == "reset");

    t.click("cb");
    assert(flag.get());
    flag = false;
    assert(!w.get<Checkbox>("cb").checked());

    auto& sl = t.view("sl");
    t.click_at(sl.to_window({sl.frame().w - 8, sl.frame().h / 2}));
    assert(vol.get() > 0.95);
    t.drag("sl", {-1000, 0}); // drag far left
    assert(vol.get() < 0.05);
    t.focus("sl").press("Right");
    assert(vol.get() > 0.04);
  }

  // 4) ForEach + Show react to list / flag changes; unmount cancels bindings
  {
    pulse::property<std::vector<std::string>> items{{"a", "b"}};
    pulse::property<bool> shown{true};
    auto w = std::make_unique<Window>("t", 300, 300);
    w->content(VStack{
        ForEach(items, [](const std::string& s) { return Text(s); }).id("list"),
        Show(shown, Text("yes").id("yes"), Text("no").id("no")),
    });
    Harness t(*w);
    assert(t.view("list").children().size() == 2);
    items.update([](auto& v) { v.push_back("c"); });
    assert(t.view("list").children().size() == 3);
    assert(t.visible("yes") && !t.visible("no"));
    shown = false;
    assert(!t.visible("yes") && t.visible("no"));
    assert(throws([&] { t.click("yes"); })); // hidden views cannot be clicked
    w.reset();
    items.update([](auto& v) { v.push_back("d"); }); // must not crash: bindings are gone
    assert(items.changed.slot_count() == 0);
  }

  // 4b) spacer inside a ForEach row in a Scroll pushes the trailing view to the edge
  {
    pulse::property<std::vector<std::string>> items{{"a"}};
    Window w("t", 400, 300);
    pulse::computed<std::vector<std::string>> vis([&] { return items.get(); }, items);
    w.content(VStack{Text("t").title(), Scroll(ForEach(vis, [](const std::string& s) {
                       return HStack{Checkbox(s, false).on_change([](bool) {}).id("cb"), Spacer().id("sp"), Button("x", [] {}).flat().id("x")}
                           .padding(Insets(8, 2));
                     }).id("list")).flex()}.spacing(12).padding(10));
    Harness t(w, {400, 300});
    auto& x = t.view("x");
    assert(x.to_window({0, 0}).x + x.frame().w > 380);
  }

  // 4c) scrolling, double click, disabled views, strict lookups
  {
    std::vector<std::string> rows;
    for (int i = 0; i < 60; ++i) rows.push_back("row " + std::to_string(i));
    pulse::property<bool> can{false};
    pulse::property<std::string> field{"hello world"};
    int clicks = 0;
    Window w("t", 300, 200);
    w.content(VStack{
        TextField(field).id("field"),
        Button("go", [&] { ++clicks; }).enabled(can).id("go"),
        Scroll(ForEach(rows, [](const std::string& s) { return Text(s); })).id("scroll").flex(),
    });
    Harness t(w);
    auto& scroll = w.get<Scroll>("scroll");
    t.scroll("scroll", 3);
    assert(scroll.offset_y() > 0);
    t.scroll("scroll", -100);
    assert(scroll.offset_y() == 0);
    t.click("go");
    assert(clicks == 0 && !t.enabled("go")); // disabled: ignored, like real input
    can = true;
    t.click("go");
    assert(clicks == 1);
    t.double_click("field").type("X"); // double click selects all
    assert(field.get() == "X");
    assert(throws([&] { t.view("missing"); }));
    assert(throws([&] { t.text("scroll"); }));
    assert(throws([&] { t.press("NoSuchKey"); }));
  }

  // 5) window input stream
  {
    Window w("t", 200, 100);
    int downs = 0;
    auto sub = (w.input() | pulse::filter([](const InputEvent& e) { return e.type == InputEvent::MouseDown; }))
                   .subscribe([&](const InputEvent&) { ++downs; });
    Harness t(w);
    t.click_at({5, 5});
    assert(downs == 1);
  }

  // 6) shortcuts
  {
    Window w("t", 200, 100);
    int hits = 0;
    w.shortcut("Ctrl+S", [&] { ++hits; });
    Harness t(w);
    t.press("Ctrl+S");
    assert(hits == 1);
  }

  // 6b) time: timers and posted work run with run_for / wait_until, without showing windows
  {
    Window w("t", 200, 100);
    pulse::property<int> ticks{0};
    w.content(Text(ticks.map([](int n) { return std::to_string(n); })).id("n"));
    Harness t(w);
    auto timer = App::every(10ms, [&] { ticks = ticks.get() + 1; });
    assert(t.wait_until([&] { return ticks.get() >= 3; }, 2s));
    assert(t.text("n") == std::to_string(ticks.get()));
    bool posted = false;
    App::post([&] { posted = true; });
    t.run_for(20ms);
    assert(posted);
    timer.reset();
    assert(!t.wait_until([] { return false; }, 30ms));
  }

  // 6c) snapshots: first run writes the reference, a change is detected
  {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pulseui_snapshot_test";
    fs::remove_all(dir);
    pulse::property<std::string> label{"Snapshot"};
    Window w("t", 200, 80);
    w.content(VStack{Text(label).title()}.padding(12));
    Harness t(w);
    const std::string ref = (dir / "label.png").string();
    assert(t.matches_snapshot(ref));       // written
    assert(fs::exists(ref));
    assert(t.matches_snapshot(ref));       // identical
    label = "Changed!";
    assert(!t.matches_snapshot(ref));      // detected
    assert(fs::exists(ref + ".actual.png"));
    const auto d = testing::compare(Image::load(ref), t.grab());
    assert(d.same_size && d.differing > 0 && d.max_delta > 16);
    fs::remove_all(dir);
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

  // 8b) form bindings: annotations round-trip, codegen with bindings and refs, strict get<T>
  {
    auto j = json::Value::parse(R"({"format":"pulseui-form","version":1,"title":"B","width":300,"height":200,
      "root":{"type":"VStack","children":[
        {"type":"TextField","props":{"id":"email","placeholder":"Email"},"bind":{"text":"email"},"on":{"submit":"submit"}},
        {"type":"Button","props":{"id":"ok","label":"OK"},"bind":{"enabled":"can_submit"},"on":{"click":"submit"}},
        {"type":"Text","props":{"id":"title","text":"x"},"bind":{"text":"settings.title"}}]}})");
    auto f = form_from_json(*j);
    assert(f && form_has_bindings(*f->root.get()));
    View* root = f->root.get();
    assert(root->find("email")->annotation("bind.text") && *root->find("email")->annotation("bind.text") == "email");
    const std::string again = form_to_json("B", 300, 200, *root).dump();
    assert(again.find("\"bind\"") != std::string::npos && again.find("\"on\"") != std::string::npos);
    CodegenOptions opt;
    opt.refs = true;
    opt.reserved = {"title"};
    const std::string code = generate_cpp_expression(*root, 0, opt);
    assert(code.find(".ref(email).model(m.email)") != std::string::npos);
    assert(code.find(".on_submit([&m](const std::string& value)") != std::string::npos);
    assert(code.find(".enabled(m.can_submit).on_click([&m] { m.submit(); })") != std::string::npos);
    assert(code.find(".bind_text(m.settings.title)") != std::string::npos);
    assert(code.find(".ref(title)") == std::string::npos); // reserved
    auto refs = form_refs(*root, {"title"});
    assert(refs.size() == 2 && refs[0].cpp_class == "TextField" && refs[1].id == "ok");
    assert(is_binding_path("a.b_c") && !is_binding_path("a..b") && !is_binding_path("class") && !is_binding_path("1x"));
    assert(generate_cpp(*root, "make_b").find("template <class Model>") != std::string::npos);

    Window w("t", 200, 100);
    w.content(std::move(f->root));
    assert(&w.get<TextField>("email") == w.find<TextField>("email"));
    assert(w.get<TextField>("email").id() == "email"); // getters are not hidden by the id() modifier
    bool threw = false;
    try { w.get<Button>("email"); } catch (const std::runtime_error& e) {
      threw = std::string(e.what()).find("'email' is a TextField, not a Button") != std::string::npos;
    }
    assert(threw);
    threw = false;
    try { w.get("nope"); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("'nope'") != std::string::npos; }
    assert(threw);
  }

  // 9) CodeEditor: typing, auto-indent, undo, two-way model, diagnostics
  {
    pulse::property<std::string> src{"int main() {"};
    Window w("t", 500, 300);
    w.content(VStack{CodeEditor(src).id("ed").flex()}.padding(0));
    Harness t(w);
    auto& ed = w.get<CodeEditor>("ed");
    assert(ed.line_count() == 1);
    t.click("ed").press("Ctrl+End").press("Enter").type("return 0;");
    assert(src.get() == "int main() {\n    return 0;");
    t.press("Ctrl+Z");
    assert(src.get() == "int main() {\n    ");
    src = "a\tb\r\nc";
    assert(ed.text() == "a   b\nc" && ed.line_count() == 2);
    ed.set_diagnostics({{2, 1, Diagnostic::Error, "boom"}});
    ed.go_to(2, 1);
    assert(ed.caret_line() == 2);
    t.grab(); // paints squiggles without crashing
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
    Harness t(w);
    auto& ed = w.get<CodeEditor>("ed");
    t.click("ed").type("f");
    assert(!ed.completion_open()); // one character: not yet
    t.type("o");
    assert(ed.completion_open() && ed.completion_items().size() == 2);
    t.press("Down").press("Enter");
    assert(!ed.completion_open());
    assert(src.get() == "foreach");
    t.grab(); // paints the editor without crashing
  }

  // CodeEditor: "$0" places the caret, parameter hints, Enter between braces
  {
    pulse::property<std::string> src;
    Window w("t", 500, 300);
    w.content(VStack{CodeEditor(src)
                         .completion([](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
                           CompletionResult r;
                           const std::string before = lines[line].substr(0, col);
                           std::size_t from = col;
                           while (from > 0 && std::isalpha(static_cast<unsigned char>(before[from - 1]))) --from;
                           r.replace_from = from;
                           if (col - from >= 2) r.items.push_back({"resize", "(w, h)", CompletionItem::Function, "resize($0)"});
                           if (before.find("resize(") != std::string::npos && before.find(')') == std::string::npos) {
                             r.signature = "resize(float w, float h)";
                             const bool second = before.find(',') != std::string::npos;
                             r.active_begin = second ? 16 : 7;
                             r.active_end = second ? 23 : 14;
                           }
                           return r;
                         })
                         .id("ed")
                         .flex()});
    Harness t(w);
    auto& ed = w.get<CodeEditor>("ed");
    t.click("ed").type("re").press("Enter");
    assert(src.get() == "resize()" && ed.caret_column() == 8); // the caret sits inside ()
    assert(ed.signature_hint() == "resize(float w, float h)");
    t.type("1,");
    assert(!ed.signature_hint().empty());
    t.grab(); // paints the hint
    t.press("Escape");
    assert(ed.signature_hint().empty());
    src = "f() {}";
    ed.go_to(1, 6);
    t.press("Enter");
    assert(src.get() == "f() {\n    \n}" && ed.caret_line() == 2 && ed.caret_column() == 5);
  }

  // CodeEditor: brackets, comments, search and replace, Ctrl+click, hover
  {
    pulse::property<std::string> src;
    std::size_t def_line = 99, def_col = 99;
    Window w("t", 500, 300);
    w.content(VStack{CodeEditor(src)
                         .auto_close()
                         .line_comment("//")
                         .hover([](const std::vector<std::string>& lines, std::size_t line, std::size_t col) {
                           return "word at " + std::to_string(line) + ":" + std::to_string(col) + " of " + lines[line];
                         })
                         .id("ed")
                         .flex()}
                  .padding(0));
    auto& ed = w.get<CodeEditor>("ed");
    ed.definition_requested.connect([&](std::size_t l, std::size_t c) { def_line = l, def_col = c; });
    Harness t(w);
    t.click("ed").type("f").type("(").type("x"); // one character per event, like a keyboard
    assert(src.get() == "f(x)");
    t.type(")");
    assert(src.get() == "f(x)"); // stepped over the closing bracket
    t.type("{").press("Backspace");
    assert(src.get() == "f(x)"); // an empty pair goes away at once
    t.type(" s = ").type("\"").type("a");
    assert(src.get() == "f(x) s = \"a\"");
    t.press("Ctrl+A").press("Ctrl+/");
    assert(src.get() == "// f(x) s = \"a\"");
    t.press("Ctrl+/");
    assert(src.get() == "f(x) s = \"a\"");

    src = "one two one\nOne";
    ed.set_search("one");
    assert(ed.search_count() == 3);
    ed.set_search("one", true);
    assert(ed.search_count() == 2);
    assert(ed.find_next() && ed.search_current() >= 0 && ed.selected_text() == "one");
    assert(ed.replace_current("1") && src.get() == "1 two one\nOne");
    assert(ed.replace_all("1") == 1 && src.get() == "1 two 1\nOne");
    ed.set_search("on", false, true);
    assert(ed.search_count() == 0); // whole words only
    ed.set_search("");
    ed.replace_range(0, 0, 0, 1, "x");
    assert(src.get() == "x two 1\nOne" && ed.word_at(0, 3) == "two");
    t.press("Ctrl+Z");
    assert(src.get() == "1 two 1\nOne");

    // Ctrl+click on "two" and the hover box over it
    t.grab();
    const Rect r = ed.frame();
    InputEvent move;
    move.type = InputEvent::MouseMove;
    move.pos = move.window_pos = {r.x + 0, r.y + 0};
    t.click("ed");
    // find the pixel of column 3 on line 0 through a click: the caret column tells where we are
    for (float x = r.x + 20; x < r.x + 300; x += 2) {
      InputEvent probe;
      probe.type = InputEvent::MouseDown;
      probe.button = MouseButton::Left;
      probe.mods = Mod::Primary;
      probe.pos = probe.window_pos = {x, r.y + 12};
      w.send(probe);
      probe.type = InputEvent::MouseUp;
      w.send(probe);
      if (def_col == 3) {
        move.pos = move.window_pos = {x, r.y + 12};
        break;
      }
    }
    assert(def_line == 0 && def_col == 3);
    w.send(move);
    t.run_for(std::chrono::milliseconds(700));
    assert(ed.hover_text().find("of 1 two 1") != std::string::npos);
    t.grab(); // paints the hover box
  }

  // keyed ForEach: rows survive list changes (and keep the focus while typing)
  {
    struct Todo {
      int id;
      std::string title;
      bool operator==(const Todo&) const = default;
    };
    pulse::property<std::vector<Todo>> todos{std::vector<Todo>{{1, "a"}, {2, "b"}, {3, "c"}}};
    Window w("t", 300, 300);
    w.content(VStack{ForEach(todos, [](const Todo& t) { return t.id; },
                             [](const Todo& t) { return TextField(t.title).id("row" + std::to_string(t.id)); })
                         .id("list")});
    Harness t(w);
    View* row2 = &t.view("row2");
    todos.update([](auto& v) { std::swap(v[0], v[2]); v.push_back({4, "d"}); }); // reorder + add
    assert(&t.view("row2") == row2 && t.view("list").children().size() == 4);
    assert(t.view("list").children()[0].get() == &t.view("row3")); // moved, not rebuilt
    t.focus("row2");
    todos.update([](auto& v) { v[1].title = "changed"; }); // the focused row keeps its view
    assert(&t.view("row2") == row2 && w.focus_view() == row2);
    todos.update([](auto& v) { v[0].title = "x"; }); // an unfocused changed row is rebuilt
    assert(t.text("row3") == "x");
    todos.update([](auto& v) { v.erase(v.begin()); });
    assert(!w.find("row3") && t.view("list").children().size() == 3);
  }

  // Grid and Wrap
  {
    Window w("t", 400, 300);
    w.content(VStack{
        Grid{Text("Name").id("g1"), TextField().id("g2"), Text("A long label").id("g3"), TextField().id("g4"),
             Button("Wide").id("g5").span(2)}
            .columns({Track::automatic(), Track::fr(1)})
            .spacing(10)
            .id("grid"),
        Wrap{Button("one"), Button("two"), Button("three"), Button("four"), Button("five"), Button("six")}
            .spacing(6)
            .id("wrap")
            .width(200),
    }
                  .align_items(Alignment::Start)
                  .padding(10));
    Harness t(w);
    const Rect a = t.view("g1").frame(), b = t.view("g2").frame(), c = t.view("g3").frame(), d = t.view("g4").frame();
    const Rect wide = t.view("g5").frame();
    assert(a.x == c.x && b.x == d.x && b.x > c.right()); // two aligned columns
    assert(std::abs(b.right() - (t.view("grid").frame().w - 0)) < 1.5f);
    assert(wide.w > b.w && wide.y > d.y); // spans both columns on the next row
    float max_y = 0;
    for (auto& ch : t.view("wrap").children()) max_y = std::max(max_y, ch->frame().y);
    assert(max_y > 0); // wrapped onto more than one line
  }

  // List: only visible rows are built
  {
    std::vector<std::string> data;
    for (int i = 0; i < 100000; ++i) data.push_back("item " + std::to_string(i));
    pulse::property<std::vector<std::string>> items{data};
    pulse::property<int> sel{-1};
    int activated = -1;
    Window w("t", 300, 300);
    w.content(VStack{List(items, [](const std::string& s) { return Text(s); })
                         .row_height(30)
                         .selected(sel)
                         .on_activate([&](int i) { activated = i; })
                         .id("list")
                         .flex()}
                  .padding(0));
    Harness t(w);
    auto& list = w.get<ListBase>("list");
    assert(list.row_count() == 100000 && list.built_rows() < 20);
    t.click("list");
    assert(sel.get() == 0 || sel.get() >= 0);
    t.press("End");
    assert(sel.get() == 99999 && list.built_rows() < 20 && list.scroll_offset() > 0);
    t.press("Enter");
    assert(activated == 99999);
    items = std::vector<std::string>{"x", "y"};
    t.grab();
    assert(list.row_count() == 2 && sel.get() <= 1);
  }

  // Table: sorting and selection by item index
  {
    struct Person {
      std::string name;
      int age;
    };
    pulse::property<std::vector<Person>> people{std::vector<Person>{{"Bob", 9}, {"alice", 30}, {"Carl", 10}}};
    pulse::property<int> sel{-1};
    Window w("t", 400, 300);
    w.content(VStack{Table(people, {Column<Person>{"Name", 150, [](const Person& p) { return p.name; }},
                                    Column<Person>{"Age", 80, [](const Person& p) { return std::to_string(p.age); }}})
                         .selected(sel)
                         .id("table")
                         .flex()}
                  .padding(0));
    Harness t(w);
    auto& table = w.get<TableBase>("table");
    table.sort_by(0);
    assert(table.cell(table.item_at_row(0), 0) == "alice" && table.cell(table.item_at_row(2), 0) == "Carl");
    table.sort_by(1, false);
    assert(table.cell(table.item_at_row(0), 1) == "30" && table.cell(table.item_at_row(2), 1) == "9"); // numeric
    t.click_at({50, 30 + 26 + 5}); // second row
    assert(sel.get() == 2);         // Carl (age 10) is item 2
    t.press("Down");
    assert(sel.get() == 0); // Bob
    // clicking the "Name" header sorts by name
    t.click_at({40, 15});
    assert(table.sort_column() == 0 && table.sort_ascending());
    t.grab();
  }

  // TreeView
  {
    pulse::property<std::vector<TreeItem>> tree{std::vector<TreeItem>{
        {"src", {{"main.cpp", {}, "src/main.cpp"}, {"app.cpp", {}, "src/app.cpp"}}, "src"},
        {"README.md", {}, "readme"},
    }};
    pulse::property<std::string> sel;
    std::string opened;
    Window w("t", 300, 300);
    w.content(VStack{TreeView(tree).selected(sel).on_activate([&](const TreeItem& i) { opened = i.id; }).id("tree").flex()}
                  .padding(0));
    Harness t(w);
    auto& tv = w.get<TreeView>("tree");
    assert(tv.visible_rows() == 2);
    t.click("tree");
    t.press("Home").press("Right");
    assert(tv.is_expanded("src") && tv.visible_rows() == 4);
    t.press("Down").press("Enter");
    assert(sel.get() == "src/main.cpp" && opened == "src/main.cpp");
    t.press("Left");
    assert(sel.get() == "src");
    t.press("Left");
    assert(!tv.is_expanded("src") && tv.visible_rows() == 2);
    t.grab();
  }

  // Menus: menu bar, shortcuts, keyboard, check items, submenus, context menus
  {
    int news = 0, saves = 0, recent = 0;
    std::string copied;
    pulse::property<bool> dirty{false}, wrap{false};
    Window w("t", 500, 300);
    w.content(VStack{Text("body").id("body").context_menu({Action("Copy", [&] { copied = "copy"; }),
                                                           Action("Paste", [&] { copied = "paste"; }).enabled(false)}),
                     TextField().id("field")});
    w.menu({
        Menu("&File", {Action("New", [&] { ++news; }).shortcut("Ctrl+N"), Separator(),
                       Action("Save", [&] { ++saves; }).shortcut("Ctrl+S").enabled(dirty),
                       Menu("Recent", [&] { return MenuItems{Action("a.txt", [&] { ++recent; })}; })}),
        Menu("&View", {Action("Word wrap").checked(wrap)}),
    });
    Harness t(w);
    assert(w.menu_bar() && w.root()->frame().y >= 24);
    t.press("Ctrl+N");
    assert(news == 1);
    t.press("Ctrl+S");
    assert(saves == 0); // disabled
    dirty = true;
    t.press("Ctrl+S");
    assert(saves == 1);
    // click "File", choose "New" with the keyboard
    t.click_at({20, 14});
    assert(w.popups().size() == 1 && std::string(w.focus_view()->type_name()) == "MenuPopup");
    t.press("Down").press("Enter");
    assert(news == 2 && w.popups().empty());
    // Alt+V opens View; Enter flips the check item
    t.press("Alt+V");
    assert(w.popups().size() == 1);
    t.press("Enter");
    assert(wrap.get() && w.popups().empty());
    // submenu: File > Recent > a.txt via Right
    t.press("Alt+F").press("End").press("Right");
    assert(w.popups().size() == 2);
    t.press("Enter");
    assert(recent == 1 && w.popups().empty());
    // Alt alone enters the bar, Right moves to View, Down opens it
    t.focus("field");
    t.press("Alt").press("Right").press("Down");
    assert(w.popups().size() == 1);
    t.press("Escape");
    assert(w.popups().empty() && w.focus_view() == &t.view("field"));
    // context menu: right click, disabled entries do nothing, click an entry
    t.click("body", MouseButton::Right);
    assert(w.popups().size() == 1);
    View* menu = w.popups()[0];
    View* copy_row = menu->children()[0].get();
    View* paste_row = menu->children()[1].get();
    assert(!paste_row->is_enabled());
    t.click(*paste_row);
    assert(copied.empty() && w.popups().size() == 1);
    t.grab();
    t.click(*copy_row);
    assert(copied == "copy" && w.popups().empty());
    // clicking outside closes it
    t.click("body", MouseButton::Right);
    t.click_at({480, 280});
    assert(w.popups().empty());
  }

  // Dialogs: responder, themed message box (async), prompt, modal windows
  {
    Window main("main", 500, 300);
    main.content(Text("main"));
    Harness tm(main);
    dialogs::set_responder([](const dialogs::Message& m) {
      return dialogs::Answer{m.title == "yes?" ? 0 : 1, "typed"};
    });
    assert(dialogs::confirm(&main, "yes?", "Sure?"));
    assert(!dialogs::confirm(&main, "no?", "Sure?"));
    assert(dialogs::prompt(&main, "no?", "Name").has_value() == false);
    dialogs::set_responder({});

    const std::size_t before = Window::all().size();
    int answered = -2;
    dialogs::Message m;
    m.title = "Unsaved changes";
    m.text = "Save the document before closing? Your changes will be lost otherwise.";
    m.buttons = {"Save", "Don't save", "Cancel"};
    m.icon = dialogs::Icon::Warning;
    dialogs::message_async(&main, m, [&](dialogs::Answer a) { answered = a.button; });
    assert(Window::all().size() == before + 1);
    Window& dlg = *Window::all().back();
    assert(dlg.size().w <= 441 && dlg.size().h > 80 && dlg.size().h < 300);
    Harness td(dlg);
    assert(td.text("dialog.title") == "Unsaved changes");
    td.grab();
    td.click("dialog.button.1");
    assert(tm.wait_until([&] { return answered == 1; })); // not td: the click destroys the dialog window
    tm.run_for(20ms);
    assert(Window::all().size() == before); // the dialog window is gone

    // Escape = cancel (the last button)
    answered = -2;
    dialogs::message_async(&main, m, [&](dialogs::Answer a) { answered = a.button; });
    Harness(*Window::all().back()).press("Escape");
    assert(tm.wait_until([&] { return answered == 2; }));

    // prompt with a text field (async form)
    std::string got;
    dialogs::Message p;
    p.title = "Rename";
    p.buttons = {"OK", "Cancel"};
    p.input = true;
    p.input_text = "old";
    dialogs::message_async(&main, p, [&](dialogs::Answer a) { got = a.button == 0 ? a.text : "<cancel>"; });
    Harness tp(*Window::all().back());
    tp.fill("dialog.input", "new name").press("Enter");
    assert(tm.wait_until([&] { return got == "new name"; }));

    // blocking run_modal returns when the window closes
    Window modal("modal", 200, 100);
    modal.content(Button("Close", [&] { modal.close(); }).id("close"));
    Harness tmo(modal);
    App::post([&] { tmo.click("close"); });
    modal.run_modal(&main);
    assert(!modal.is_open() || true);
  }

  // TextArea: two-way model, wrapping, keyboard, undo
  {
    pulse::property<std::string> notes{"first line"};
    Window w("t", 300, 300);
    w.content(VStack{TextArea(notes).id("ta").width(160).rows(5)}.align_items(Alignment::Start).padding(10));
    Harness t(w);
    auto& ta = w.get<TextArea>("ta");
    assert(ta.text() == "first line");
    t.focus("ta").press("Ctrl+End").press("Enter").type("second");
    assert(notes.get() == "first line\nsecond");
    t.type(" a rather long sentence that has to wrap onto more lines");
    assert(ta.visual_line_count() > 3);
    t.press("Ctrl+Home").press("Down");
    assert(ta.caret() == std::string("first line\n").size());
    t.press("Ctrl+Z");
    assert(notes.get() == "first line\nsecond");
    t.press("Ctrl+Z");
    assert(notes.get() == "first line\n");
    t.press("Ctrl+Y");
    assert(notes.get() == "first line\nsecond");
    notes = "replaced";
    assert(ta.text() == "replaced");
    t.grab();
  }

  // NumberField loaded from a form: value, then min above the old max, then max
  {
    auto n = meta::create("NumberField");
    const std::vector<std::pair<const char*, double>> props{{"value", 1000.0}, {"min", 200.0}, {"max", 5000.0}};
    for (auto& [prop, v] : props) meta::property(*n, prop)->set(*n, meta::Value{v});
    auto& f = static_cast<NumberField&>(*n);
    assert(f.min_value() == 200 && f.max_value() == 5000 && f.value() == 1000);
  }

  // NumberField: steps, typing, clamping, int models, suffix
  {
    pulse::property<int> qty{5};
    pulse::property<double> price{1.5};
    Window w("t", 300, 200);
    w.content(VStack{NumberField(qty, 0, 10).id("qty"), NumberField(price, 0, 100, 0.25).decimals(2).suffix(" $").id("price")});
    Harness t(w);
    auto& q = w.get<NumberField>("qty");
    auto& pr = w.get<NumberField>("price");
    assert(q.field().text() == "5" && pr.field().text() == "1.50 $");
    t.click(q.field()).press("Up").press("Up");
    assert(qty.get() == 7);
    t.press("Shift+Up");
    assert(qty.get() == 10); // clamped
    t.press("Ctrl+A").type("3").press("Enter");
    assert(qty.get() == 3 && q.field().text() == "3");
    t.press("Ctrl+A").type("abc").press("Enter");
    assert(qty.get() == 3 && q.field().text() == "3"); // invalid input is reverted
    // the arrows
    const Rect f = q.frame();
    const Point up = q.to_window({f.w - 8, 6});
    t.click_at(up);
    assert(qty.get() == 4);
    qty = 9;
    assert(q.field().text() == "9");
    t.click(pr.field()).press("Down");
    assert(std::abs(price.get() - 1.25) < 1e-9 && pr.field().text() == "1.25 $");
    t.press("Ctrl+A").type("7,5 $").press("Tab");
    assert(std::abs(price.get() - 7.5) < 1e-9);
  }

  // RadioGroup
  {
    pulse::property<int> size{0};
    Window w("t", 300, 200);
    w.content(VStack{RadioGroup({"Small", "Medium", "Large"}, size).id("rg")});
    Harness t(w);
    auto& rg = w.get<RadioGroup>("rg");
    t.click_at(rg.to_window({10, 28 * 2 + 10}));
    assert(size.get() == 2);
    t.press("Down");
    assert(size.get() == 0);
    t.press("Up");
    assert(size.get() == 2);
    size = 1;
    assert(rg.index() == 1);
  }

  // Dates, Calendar and DatePicker
  {
    assert((Date{2026, 11, 21}.weekday() == 5)); // Saturday
    assert((Date{1970, 1, 1}.weekday() == 3));
    assert((Date{2026, 1, 31}.add_months(1) == Date{2026, 2, 28}));
    assert((Date{2024, 1, 31}.add_months(1) == Date{2024, 2, 29}));
    assert((Date{2026, 12, 31}.add_days(1) == Date{2027, 1, 1}));
    assert((Date::from_days(Date{2000, 3, 1}.days_since_epoch()) == Date{2000, 3, 1}));
    assert((Date::parse("2026-02-30") == std::nullopt && Date::parse("2026-02-28")->day == 28));

    pulse::property<Date> due{Date{2026, 11, 21}};
    Window w("t", 400, 420);
    w.content(VStack{DatePicker(due).id("dp"), Calendar(due).id("cal")}.align_items(Alignment::Start));
    Harness t(w);
    auto& cal = w.get<Calendar>("cal");
    t.focus("cal").press("Right");
    assert((due.get() == Date{2026, 11, 22}));
    t.press("PageDown");
    assert((due.get() == Date{2026, 12, 22} && cal.shown_month() == 12));
    auto& dp = w.get<DatePicker>("dp");
    assert(dp.display_text() == "2026-12-22");
    t.click("dp");
    assert(dp.popup_open());
    t.press("Down").press("Enter"); // a week later, picked
    assert((due.get() == Date{2026, 12, 29}) && !dp.popup_open());
    t.grab();
  }

  // ColorPanel and ColorPicker
  {
    float h, s, v;
    Color::hex(0xFF0000).to_hsv(h, s, v);
    assert(h == 0 && s == 1 && v == 1);
    const Color c = Color::hsv(210, 0.5f, 0.8f);
    c.to_hsv(h, s, v);
    assert(std::abs(h - 210) < 0.5f && std::abs(s - 0.5f) < 0.01f && std::abs(v - 0.8f) < 0.01f);

    pulse::property<Color> accent{Color::hex(0x3B82F6)};
    Window w("t", 400, 420);
    w.content(VStack{ColorPicker(accent).id("cp"), ColorPanel(accent).alpha().id("panel")}.align_items(Alignment::Start));
    Harness t(w);
    auto& panel = w.get<ColorPanel>("panel");
    // click the top-left of the SV square: white
    t.click_at(panel.to_window({10 + 1, 10 + 1}));
    assert(accent.get().r > 0.97f && accent.get().g > 0.97f && accent.get().b > 0.97f);
    // the hex field
    auto& hex = static_cast<TextField&>(*panel.children().front());
    t.click(hex).press("Ctrl+A").type("#22C55E").press("Enter");
    assert(accent.get().to_hex() == "#22C55E");
    auto& cp = w.get<ColorPicker>("cp");
    assert(cp.color().to_hex() == "#22C55E");
    t.click("cp");
    assert(cp.popup_open());
    t.grab();
    t.press("Escape");
    assert(!cp.popup_open());
  }

  // Toasts: stack in the corner, close by themselves, survive outside clicks
  {
    Window w("t", 500, 300);
    w.content(Text("body").id("body"));
    Harness t(w);
    int undone = 0;
    toast(w, "Saved", ToastKind::Success, 150ms);
    ToastOptions o;
    o.action_label = "Undo";
    o.action = [&] { ++undone; };
    o.duration = 0ms;
    View* second = toast(w, "Note deleted", o);
    t.grab();
    assert(w.popups().size() == 2);
    const Rect a = w.popups()[0]->frame(), b = w.popups()[1]->frame();
    assert(a.right() <= 500 && b.bottom() <= 300 && a.y < b.y); // newest at the bottom
    t.click("body");
    assert(w.popups().size() == 2); // not closed by clicking elsewhere
    assert(t.wait_until([&] { return w.popups().size() == 1; }, 3s));
    t.click(*second->children().front());
    assert(undone == 1);
    assert(t.wait_until([&] { return w.popups().empty(); }, 2s)); // fades out, then goes
  }

  // Animations: easing, tween streams, .animate() frames and colors, Show transitions
  {
    assert(anim::ease(Easing::EaseOut, 0) == 0 && anim::ease(Easing::EaseOut, 1) == 1);
    assert(anim::ease(Easing::EaseInOut, 0.5f) > 0.49f && anim::ease(Easing::EaseInOut, 0.5f) < 0.51f);
    assert(std::abs(anim::ease(Easing::Spring, 1) - 1) < 1e-4f);

    Window w("t", 300, 300);
    pulse::property<double> target{0};
    std::vector<double> seen;
    auto sub = tween(target, 120ms).subscribe([&](double v) { seen.push_back(v); });
    assert(seen.size() == 1 && seen[0] == 0);
    target = 10;
    Harness t0(w);
    assert(t0.wait_until([&] { return !seen.empty() && seen.back() == 10; }, 2s));
    bool intermediate = false;
    for (double v : seen) intermediate = intermediate || (v > 0 && v < 10);
    assert(intermediate && seen.size() > 3);

    pulse::property<bool> big{false};
    pulse::property<bool> open{false};
    pulse::property<Color> bg{Color::hex(0x000000)};
    w.content(VStack{
        Text("box").id("box").height(20).background(bg).animate(120ms),
        Show(big, Text("tall").height(60), Text("short").height(20)).id("sw").transition(120ms),
        Show(open, Text("details").height(40)).id("collapse").transition(120ms),
        Text("below").id("below").height(20).animate(120ms),
    }
                  .spacing(0)
                  .padding(0)
                  .align_items(Alignment::Start));
    Harness t(w);
    auto& below = t.view("below");
    const float y0 = below.frame().y;
    assert(y0 == 40);
    open = true;
    t.grab();
    assert(w.get<Show>("collapse").transitioning());
    assert(t.wait_until([&] { t.grab(); return !w.get<Show>("collapse").transitioning() && below.frame().y == 80; }, 2s));
    // the sibling below glided rather than jumped
    big = true;
    t.grab();
    assert(below.frame().y < 120 && below.frame().y >= 80);
    assert(t.wait_until([&] { t.grab(); return below.frame().y == 120; }, 2s));
    bg = Color::hex(0xFFFFFF);
    t.grab();
    assert(t.view("box").style().background.r < 1);
    assert(t.wait_until([&] { return t.view("box").style().background.r == 1; }, 2s));

    // speed 0: no animation at all
    anim::set_speed(0);
    open = false;
    t.grab();
    assert(!w.get<Show>("collapse").transitioning() && below.frame().y == 80);
    anim::set_speed(1);
  }

  // Drag and drop: files filtered by extension, text, window fallback, drag sources
  {
    std::vector<std::string> images, any;
    std::string text;
    Window w("t", 400, 300);
    w.content(VStack{
        Text("images").id("img").height(60).on_drop_files([&](const std::vector<std::string>& f) { images = f; }, {".png", ".JPG"}),
        Text("notes").id("notes").height(60).on_drop_text([&](const std::string& s) { text = s; }),
        Text("drag me").id("src").height(40).drag_source([] { return DragData{{"C:/tmp/a.txt"}, "hello"}; }),
    });
    Harness t(w);
    assert(t.drop_files("img", {"C:/x/one.png", "C:/x/two.txt", "C:/x/three.jpg"}));
    assert(images.size() == 2 && images[0] == "C:/x/one.png" && images[1] == "C:/x/three.jpg");
    assert(!t.drop_files("img", {"C:/x/readme.md"})); // nothing matches
    assert(!t.drop_files("notes", {"C:/x/a.png"}));   // takes text only
    assert(t.drop_text("notes", "dropped text") && text == "dropped text");
    // the window takes what no view wants, when it has a handler
    w.on_drop_files = [&](const std::vector<std::string>& f) { any = f; };
    assert(t.drop_files("notes", {"C:/x/a.png"}) && any.size() == 1);
    // hovering highlights the target
    DragData d;
    d.files = {"C:/x/y.png"};
    w.handle_drag(Window::DragPhase::Enter, t.center(t.view("img")), d);
    assert(w.drop_target() == &t.view("img"));
    t.grab();
    w.handle_drag(Window::DragPhase::Leave, {}, {});
    assert(w.drop_target() == nullptr);
    // dragging a drag_source starts a system drag with its data
    t.drag("src", {40, 0});
    assert(w.last_drag().text == "hello" && w.last_drag().files.size() == 1);
  }

  // SVG icons: shapes, transforms, fill rules, rendering
  {
    const char* ring = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20">
      <!-- a ring: two circles, even-odd -->
      <path fill-rule="evenodd" fill="#FF0000" d="M10 0a10 10 0 1 1 0 20a10 10 0 1 1 0-20zM10 5a5 5 0 1 0 0 10a5 5 0 1 0 0-10z"/>
    </svg>)svg";
    Svg s = Svg::parse(ring);
    assert(s.valid() && s.shape_count() == 1 && s.size().w == 20);
    assert(s.data()->shapes[0].contours.size() == 2 && s.data()->shapes[0].even_odd);
    Svg g = Svg::parse(R"svg(<svg width="24" height="24"><g transform="translate(10,0) scale(2)" fill="none" stroke="currentColor">
        <rect x="1" y="1" width="4" height="4" rx="1"/><line x1="0" y1="0" x2="2" y2="2"/></g>
        <defs><circle r="99"/></defs><polygon points="0,0 4,0 2,3" style="fill:blue"/></svg>)svg");
    assert(g.shape_count() == 3);
    assert(!g.data()->shapes[0].fill && g.data()->shapes[0].stroke_current && g.data()->shapes[0].stroke_width == 2);
    assert(g.data()->shapes[0].contours[0][0].x >= 12); // translated and scaled
    assert(g.data()->shapes[2].fill && g.data()->shapes[2].fill->b == 1);
    assert(is_svg_icon("<svg/>") && is_svg_icon("icons/a.svg") && !is_svg_icon("+"));

    Window w("t", 100, 100);
    w.content(VStack{Icon(ring).original_colors().id("icon").size(80)}.padding(10).align_items(Alignment::Start));
    Harness t(w);
    Image img = t.grab();
    auto px = [&](int x, int y) {
      const float k = static_cast<float>(img.width()) / 100.f;
      const int ix = static_cast<int>(x * k), iy = static_cast<int>(y * k);
      return img.pixels() + (static_cast<std::size_t>(iy) * img.width() + ix) * 4;
    };
    assert(px(14, 50)[0] > 200 && px(14, 50)[1] < 60); // on the ring: red
    assert(px(50, 50)[0] < 120);                       // the hole shows the window background
  }

  // TextField suggestions (autocomplete)
  {
    const std::vector<std::string> words{"apple", "apricot", "banana", "blueberry", "cherry"};
    pulse::property<std::string> fruit;
    std::string committed;
    Window w("t", 300, 300);
    w.content(VStack{TextField(fruit)
                         .suggestions([&](const std::string& t) {
                           std::vector<Suggestion> out;
                           for (auto& x : words)
                             if (x.rfind(t, 0) == 0) out.push_back({x, std::to_string(x.size()) + " letters", ""});
                           return out;
                         })
                         .on_commit([&](const std::string& s) { committed = s; })
                         .id("f"),
                     Button("other").id("other")});
    Harness t(w);
    auto& f = w.get<TextField>("f");
    t.focus("f");
    assert(f.suggestions_open() && f.current_suggestions().size() == 5); // all, on focus
    t.type("b");
    assert(f.current_suggestions().size() == 2);
    t.press("Down").press("Enter");
    assert(fruit.get() == "blueberry" && committed == "blueberry" && !f.suggestions_open());
    t.press("Ctrl+A").type("ch");
    assert(f.current_suggestions().size() == 1);
    t.grab();
    // click an entry in the list
    View* list = w.popups().back();
    t.click_at(list->to_window({20, 3 + 13}));
    assert(fruit.get() == "cherry" && w.focus_view() == &f);
    t.type("x");
    assert(!f.suggestions_open()); // nothing matches "cherryx"
    t.press("Backspace");
    assert(f.suggestions_open());
    t.press("Escape");
    assert(!f.suggestions_open());
  }

  // Forms: lists (ForEach row templates), tabs, canvas and the new widgets in generated code
  {
    auto root = meta::create("VStack");
    auto list = meta::create("ForEach");
    meta::find("ForEach")->init_new(*list); // the default row: HStack{Checkbox, Text}
    list->set_annotation("bind.items", "todos");
    View& row = *list->children()[0];
    row.children()[0]->set_annotation("bind.checked", "item.done");
    row.children()[1]->set_annotation("bind.text", "item.title");
    row.add(meta::create("Button"));
    row.children()[2]->set_annotation("on.click", "remove");
    row.children()[2]->set_id("remove_btn");
    root->add(Node(std::move(list)));
    auto tabs = meta::create("Tabs");
    meta::find("Tabs")->init_new(*tabs);
    root->add(Node(std::move(tabs)));
    auto canvas = meta::create("Canvas");
    canvas->set_annotation("on.draw", "paint_chart");
    canvas->set_annotation("bind.redraw_on", "values");
    root->add(Node(std::move(canvas)));
    for (const char* n : {"TextArea", "NumberField", "RadioGroup", "Segmented", "DatePicker", "ColorPicker", "Icon", "Grid", "Wrap"}) {
      auto v = meta::create(n);
      assert(v);
      if (auto* t = meta::find(n); t && t->init_new) t->init_new(*v);
      root->add(Node(std::move(v)));
    }
    root->children()[3]->set_annotation("bind.text", "notes");    // TextArea
    root->children()[4]->set_annotation("bind.value", "count");   // NumberField
    CodegenOptions opt;
    opt.refs = true;
    const std::string code = generate_cpp_expression(*root, 0, opt);
    auto has = [&](const char* s) {
      const bool ok = code.find(s) != std::string::npos;
      if (!ok) std::cerr << "generated code lacks: " << s << "\n" << code << "\n";
      return ok;
    };
    assert(has("ForEach(m.todos, [&](const auto& item, std::size_t index) -> pulseui::Node {"));
    assert(has(".bind_checked(item.done)") && has(".bind_text(item.title)"));
    assert(has("pulseui::form_call([&](auto&&... a) -> decltype(m.remove(a...)) { return m.remove(a...); }, std::tie(item), std::tie(index), std::tie());"));
    assert(code.find(".ref(remove_btn)") == std::string::npos); // views inside a row are not members
    assert(has("Tab(\"First\",") && has("Tab(\"Second\",") && code.find(".visible(false)") == std::string::npos);
    assert(has("Canvas([&](pulseui::Painter& p, const pulseui::Rect& r) { m.paint_chart(p, r); })"));
    assert(has(".redraw_on(m.values)"));
    assert(has("TextArea()") && has(".model(m.notes)") && has("NumberField(0, 0, 100, 1).model(m.count)"));
    assert(has("RadioGroup({") && has("Segmented({") && has("DatePicker(pulseui::Date{") && has("ColorPicker(Color::hex("));
    // JSON round trip keeps the list template, its bindings and the tab titles
    const json::Value j = form_to_json("t", 300, 200, *root);
    auto back = form_from_json(j);
    assert(back && back->root);
    const json::Value j2 = form_to_json("t", 300, 200, *back->root.get());
    if (j.dump() != j2.dump()) std::cerr << j.dump(1) << "\n----\n" << j2.dump(1) << "\n";
    assert(j.dump() == j2.dump());
    // and the template renders in a window (with its preview copies)
    Window w("t", 500, 900);
    w.content(std::move(back->root));
    Harness t(w);
    t.grab();
  }

  // CodeEditor: a click in the gutter toggles a breakpoint
  {
    pulse::property<std::string> code{"int a = 1;\nint b = 2;\nint c = 3;\n"};
    pulse::property<std::vector<int>> bps;
    Window w("t", 400, 300);
    w.content(VStack{CodeEditor(code)
                         .breakpoints(bps)
                         .on_mount_fn([&](CodeEditor& e) {
                           e.breakpoint_toggled.connect([&](int line) {
                             auto v = bps.get();
                             auto it = std::find(v.begin(), v.end(), line);
                             if (it == v.end()) v.push_back(line);
                             else v.erase(it);
                             bps = v;
                           });
                         })
                         .id("ed")
                         .flex()}
                  .padding(0));
    Harness t(w);
    auto& ed = w.get<CodeEditor>("ed");
    t.click_at(ed.to_window({12, 30})); // the gutter, second line
    std::cerr << "breakpoints after click: " << bps.get().size() << "\n";
    assert(bps.get().size() == 1 && ed.breakpoints_value().size() == 1);
    // and the marker is visible in the gutter (a red dot)
    Image img = t.grab();
    const float k = static_cast<float>(img.width()) / 400.f;
    bool red = false;
    for (int y = 0; y < 80 && !red; ++y) {
      const auto* px = img.pixels() + (static_cast<std::size_t>(y * k) * img.width() + static_cast<std::size_t>(8 * k)) * 4;
      red = px[0] > 180 && px[1] < 110 && px[2] < 110;
    }
    assert(red);
  }

  std::cout << "[pulseui_tests] OK\n";
  return 0;
}
