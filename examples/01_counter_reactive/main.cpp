#include <pulseui/pulseui.hpp>
struct Model {
  int counter = 0;
  bool operator==(const Model& other) const { return counter == other.counter; }
};
struct Action { enum Kind { Inc, Dec } kind; };

int main() {
  using namespace pulseui;
  using namespace pulseui::core;
  using namespace pulseui::ui;

  platform::app_init();

  auto ui_exec = platform::make_ui_executor();
  auto ex = as_pulse_executor(*ui_exec);
  pulse::executor& pex = ex;

  auto win = platform::make_window(640, 400, "Counter (Reactive)");
  Manager ui(*win);

  pulse::topic<Action> actions;
  auto model$ =
      ( make_store<Model, Action>(pulse::as_observable(actions, pex),
          [](Model m, Action a){
            if (a.kind == Action::Inc) ++m.counter;
            else                       --m.counter;
            return m;
          })
        | pulse::distinct_until_changed()
      );

  Model state{};
  auto sub = model$.subscribe([&](const Model& m){
    state = m;
    ui.window().invalidate();
  });

  ui.set_background([&](Canvas& g){
    g.clear({0.12f,0.12f,0.12f,1});
    g.draw_text({24,32}, "Click anywhere to increment counter", Font{16.f}, {1,1,1,1});
    g.draw_text({24,90}, "Counter: " + std::to_string(state.counter), Font{22.f}, {1,1,1,1});
  });

  auto& surface = ui.add<ClickSurface>();
  surface.on_click([&]{ actions.publish({Action::Inc}); });

  platform::app_run();
}
