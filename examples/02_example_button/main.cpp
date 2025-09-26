#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace pulseui::ui;

int main() {
  platform::app_init();

  auto win = platform::make_window(640, 400, "PulseUI - Button");
  Manager ui(*win);

  ui.set_background([](Canvas& g){
    g.clear({0.10f, 0.12f, 0.14f, 1.0f});
  });

  auto& btn1 = ui.add<Button>(Rect{40, 40, 200, 48}, "Click me");
  btn1.on_click([&]{
    static int n = 0;
    btn1.set_text("Clicked: " + std::to_string(++n));
    ui.window().invalidate();
  });

  auto& btn2 = ui.add<Button>(Rect{40, 100, 200, 48}, "Bottom button");
  btn2.on_click([&]{
    static int m = 0;
    btn2.set_text("Bottom: " + std::to_string(++m));
    ui.window().invalidate();
  });

  platform::app_run();
  return 0;
}