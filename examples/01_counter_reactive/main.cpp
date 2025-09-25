#include <pulseui/pulseui.hpp>

struct Model {
  int counter = 0;
  bool operator==(const Model& other) const { return counter == other.counter; }
};
struct Action { enum Kind { Inc, Dec } kind; };

int main() {
  using namespace pulseui;
  using namespace pulseui::core;

  platform::app_init();

  auto ui_exec = platform::make_ui_executor();
  auto ex = as_pulse_executor(*ui_exec);
  pulse::executor& pex = ex;

  auto win  = platform::make_window(640, 400, "Counter (Reactive)");

  pulse::topic<Action> actions;

  auto model$ =
      ( make_store<Model, Action>(pulse::as_observable(actions, pex),
            [](Model m, Action a){
              if (a.kind == Action::Inc) m.counter++;
              else                       m.counter--;
              return m;
            })
        | pulse::distinct_until_changed()
      );

  Model state{};
  auto sub = model$.subscribe([&](const Model& m){
    state = m;
    win->invalidate();
  });

  win->on_input([&](const ui::InputEvent& e){
    if (e.type == ui::InputEvent::MouseDown) {
      actions.publish({Action::Inc});
    }
  });

  win->on_paint([&](ui::Canvas& g){
    g.clear({0.12f,0.12f,0.12f,1});
    g.draw_text({24,32}, "Click to increment the counter", ui::Font{16.f}, {1,1,1,1});
    g.fill_rect({24,60,200,80}, {0.25f,0.55f,0.90f,1});
    g.draw_text({34,110}, "Counter: " + std::to_string(state.counter), ui::Font{20.f}, {1,1,1,1});
  });

  platform::app_run();
  return 0;
}
