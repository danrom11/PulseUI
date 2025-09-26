#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace pulseui::ui;

int main() {
  platform::app_init();
  auto win = platform::make_window(640, 400, "Label Demo");

  Manager ui(*win);
  ui.set_background([](Canvas& g){
    g.clear({0.10f, 0.12f, 0.14f, 1.0f});
  });

  auto& title = ui.add<Label>(Rect{24, 24, 592, 40}, "Hello, PulseUI!");
  LabelStyle s; s.font.size = 24.f; s.align_h = AlignH::Left; s.align_v = AlignV::Center;
  title.set_style(s);

  ui.add<Label>(Rect{24, 70, 592, 24}, "This is a static label with padding and alignment.");

  auto& btn = ui.add<Button>(Rect{24, 110, 160, 44}, "Change title");
  btn.on_click([&]{
    static int n = 0;
    title.set_text("Clicked " + std::to_string(++n) + " times");
    ui.window().invalidate();
  });

  platform::app_run();
  return 0;
}
