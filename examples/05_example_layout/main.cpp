#include <pulseui/pulseui.hpp>

using namespace pulseui::ui;
using namespace pulseui;

int main() {
  platform::app_init();
  auto win = platform::make_window(800,600,"Layout Demo");
  Manager ui(*win);

  auto& root = ui.add<Column>();
  root.set_rect({0,0,800,600});

  auto& title  = ui.add<Label>(Rect{}, "PulseUI Layout");
  auto& field  = ui.add<TextField>(Rect{}, "", "Enter your name...");
  auto& ok     = ui.add<Button>(Rect{}, "OK");
  auto& cancel = ui.add<Button>(Rect{}, "Cancel");

  // In Column: header fixed at 40px, field Expand, footer fixed at 56px
  root.add_child(&title, 40);        // fixed height
  root.add_child(&field, -1, 1);     // flex-grow = 1
  auto& footer = ui.add<Row>();
  root.add_child(&footer, 56);       // footer is fixed

  // In the footer: buttons of fixed width
  footer.add_child(&cancel, 120);    
  footer.add_child(&ok, 120);

  // Button handlers
  ok.on_click([&] {
    printf("Clicked OK! Text = %s\n", field.text().c_str());
  });
  cancel.on_click([&] {
    printf("Cancel pressed!\n");
  });

  platform::app_run();
}
