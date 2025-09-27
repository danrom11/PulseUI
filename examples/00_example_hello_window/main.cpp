#include <pulseui/pulseui.hpp>

int main() {
  using namespace pulseui;
  platform::app_init();

  auto exec = platform::make_ui_executor();
  auto win  = platform::make_window(600, 360, "PulseUI Hello");

  win->on_paint([](ui::Canvas& g){
    g.clear({0.10f, 0.12f, 0.14f, 1.0f});
    g.fill_rect({20,20,300,120}, {0.2f, 0.6f, 0.8f, 1.0f});
    g.draw_text({30, 40}, "Hello, PulseUI (cross-platform)!", ui::Font{18.f}, {1,1,1,1});
  });

  platform::app_run();
  return 0;
}
