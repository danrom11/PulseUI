#include <pulseui/pulseui.hpp>

using namespace pulseui;
using namespace pulseui::ui;

int main() {
  platform::app_init();

  auto win = platform::make_window(640, 400, "PulseUI Button");

  Button btn(Rect{40, 40, 200, 48}, "Click me");
  btn.on_click([&]{
    static int n = 0;
    btn.set_text("Clicked: " + std::to_string(++n));
    win->invalidate();
  });

  win->on_paint([&](Canvas& g){
    g.clear({0.10f, 0.12f, 0.14f, 1.0f});
    btn.paint(g);
  });

  win->on_input([&](const InputEvent& e){
    switch (e.type) {
      case InputEvent::MouseMove:
        btn.handle_mouse_move(e.pos);
        win->invalidate();
        break;
      case InputEvent::MouseDown:
        btn.handle_mouse_down(e.pos, MouseButton::Left);
        win->invalidate();
        break;
      case InputEvent::MouseUp:
        btn.handle_mouse_up(e.pos, MouseButton::Left);
        win->invalidate();
        break;
      default:
        break;
    }
  });

  platform::app_run();
  return 0;
}
