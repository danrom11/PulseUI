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

int main() {
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

  std::cout << "[pulseui_tests] OK\n";
  return 0;
}
