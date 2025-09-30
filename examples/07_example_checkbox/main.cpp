#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace pulseui::ui;

int main() {
  platform::app_init();
  auto win = platform::make_window(640, 200, "Checkbox Demo");
  Manager ui(*win);

  auto& root = ui.add<Column>();
  root.set_rect({0,0,640,200});

  auto& title = ui.add<Label>(Rect{}, "Options:");
  auto& cb1   = ui.add<Checkbox>(Rect{}, "Remember me", true);
  auto& cb2   = ui.add<Checkbox>(Rect{}, "Send usage stats", false);

  root.add_child(&title, 40);
  root.add_child(&cb1,   32);
  root.add_child(&cb2,   32);

  cb1.on_change([&](bool v){ printf("Remember me: %s\n", v ? "ON" : "OFF"); });
  cb2.on_change([&](bool v){ printf("Usage stats: %s\n", v ? "ON" : "OFF"); });

  platform::app_run();
}
