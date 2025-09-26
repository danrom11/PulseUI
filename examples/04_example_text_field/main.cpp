#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace pulseui::ui;

int main() {
  platform::app_init();
  auto win = platform::make_window(640, 240, "TextField demo");
  Manager ui(*win);

  ui.set_background([](Canvas& g){
    g.clear({0.10f, 0.12f, 0.14f, 1.0f});
  });

  auto& label = ui.add<Label>(Rect{24, 24, 592, 24}, "Type below:");
  (void)label;

  auto& tf = ui.add<TextField>(Rect{24, 60, 400, 36}, "", "Enter your name...");
  auto& btn = ui.add<Button>(Rect{24, 110, 180, 40}, "Say hello");
  btn.on_click([&]{
    std::string s = tf.text().empty() ? "<empty>" : tf.text();
    label.set_text(s);
  });

  platform::app_run();
  return 0;
}
