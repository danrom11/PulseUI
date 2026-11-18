// A tour of the built-in views, theme switching and streams.
#include <pulseui/pulseui.hpp>

#include <cmath>

using namespace pulseui;
using namespace std::chrono_literals;

static Node section(std::string title, Node body) {
  return Panel{Text(std::move(title)).weight(FontWeight::SemiBold), std::move(body)};
}

int main() {
  App app;

  pulse::property<std::string> name{"Ada"};
  pulse::property<bool> dark{true};
  pulse::property<bool> notify{false};
  pulse::property<double> volume{0.4};
  pulse::property<int> size{1};
  pulse::property<int> clicks{0};

  // The theme follows a property.
  dark.changed.connect([](bool d) { theme() = d ? Theme::dark() : Theme::light(); });

  // A stream: progress animates on a UI timer.
  pulse::property<double> progress{0};
  auto ticker = App::every(30ms, [&] {
    progress = std::fmod(progress.get() + 0.005, 1.0);
  });

  Button hello("Say hello");
  hello.primary();
  // Clicks are a stream: throttle double-clicks away, count the rest.
  auto click_sub = (hello.clicks() | pulse::throttle(300ms)).subscribe([&](pulse::unit) { clicks = clicks + 1; });

  Window win("PulseUI Gallery", 860, 620);
  win.content(Split(
      // left: navigation-like column
      VStack{
          Text("PulseUI").title(),
          Text("Declarative reactive UI on Pulse").dim().wrap(),
          Divider(),
          Toggle("Dark theme", dark),
          Checkbox("Notifications", notify),
          Show(notify, Text("You will be notified").color(Color::hex(0x3FB950)), Text("Silent mode").dim()),
          Spacer(),
          Text(clicks.map([](int c) { return "hello x" + std::to_string(c); })).dim(),
      }
          .padding(16)
          .background(Color::transparent()),
      // right: content
      Scroll(VStack{
          section("Text input", VStack{
                                    TextField(name).placeholder("Your name"),
                                    Text(name.map([](const std::string& n) { return n.empty() ? std::string("Hello, stranger") : "Hello, " + n + "!"; })),
                                }),
          section("Buttons", HStack{std::move(hello), Button("Normal"), Button("Flat").flat(), Button("Delete").danger(),
                                    Button("Disabled").enabled(false)}),
          section("Values", VStack{
                                HStack{Text("Volume").width(70), Slider(volume, 0, 1).flex(),
                                       Text(volume.map([](double v) { return std::to_string(static_cast<int>(v * 100)) + "%"; })).width(44)},
                                ProgressBar(volume),
                                ProgressBar(progress),
                                HStack{Text("Size").width(70), Select({"Small", "Medium", "Large"}, size).width(160)},
                            }),
          section("Tabs", Tabs{Tab("First", Text("Tabs keep their content mounted.").padding(12)),
                               Tab("Second", Text(name.map([](auto& n) { return "Bound text in a tab: " + n; })).padding(12)),
                               Tab("Third", Canvas([&](Painter& p, const Rect& r) {
                                              const float f = static_cast<float>(progress.get());
                                              p.fill_ellipse({r.x + f * (r.w - 40), r.y + 10, 40, 40}, th().accent);
                                            }).redraw_on(progress).height(60))}
                              .height(140)),
      }
                 .spacing(12)
                 .padding(16))
          .flex(),
      Orientation::Horizontal, 240));

  win.shortcut("Ctrl+T", [&] { dark = !dark.get(); });
  return app.run();
}
