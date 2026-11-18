// Todo list: a reactive list rendered with ForEach, a two-way bound
// TextField and a filter driven by a Select.
#include <pulseui/pulseui.hpp>

using namespace pulseui;

struct Todo {
  std::string title;
  bool done = false;
  bool operator==(const Todo&) const = default;
};

int main() {
  App app;

  pulse::property<std::vector<Todo>> todos{{{"Try PulseUI", true}, {"Build something reactive"}}};
  pulse::property<std::string> draft;
  pulse::property<int> filter{0}; // 0 all, 1 active, 2 done

  // Visible items = todos filtered by `filter` (recomputed when either changes).
  pulse::computed<std::vector<Todo>> visible(
      [&] {
        std::vector<Todo> out;
        for (auto& t : todos.get())
          if (filter.get() == 0 || (filter.get() == 1) == !t.done) out.push_back(t);
        return out;
      },
      todos, filter);
  pulse::computed<std::string> summary(
      [&] {
        int left = 0;
        for (auto& t : todos.get()) left += !t.done;
        return std::to_string(left) + " left";
      },
      todos);

  auto add = [&] {
    if (draft.get().empty()) return;
    todos.update([&](auto& v) { v.push_back({draft.get()}); });
    draft = "";
  };
  auto toggle = [&](const std::string& title) {
    todos.update([&](auto& v) {
      for (auto& t : v)
        if (t.title == title) t.done = !t.done;
    });
  };

  Window win("Todo", 460, 520);
  win.content(VStack{
      Text("Todos").title(),
      HStack{
          TextField(draft).placeholder("What needs to be done?").on_submit([&](auto&) { add(); }).flex(),
          Button("Add", add).primary(),
      },
      HStack{
          Select({"All", "Active", "Done"}, filter).width(120),
          Spacer(),
          Text(summary).dim(),
      },
      Scroll(ForEach(visible,
                     [&](const Todo& t) {
                       return HStack{
                           Checkbox(t.title, t.done).on_change([&, title = t.title](bool) { toggle(title); }),
                           Spacer(),
                           Button("✕", [&, title = t.title] {
                             todos.update([&](auto& v) { std::erase_if(v, [&](auto& x) { return x.title == title; }); });
                           }).flat(),
                       }
                           .padding(Insets(8, 2));
                     }))
          .flex(),
  }
                  .spacing(12)
                  .padding(20));
  win.shortcut("Ctrl+N", [&] { draft = ""; });
  return app.run();
}
