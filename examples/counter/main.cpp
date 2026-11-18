// The smallest reactive app: state lives in a pulse::property,
// the UI just describes how it looks for any value of that state.
#include <pulseui/pulseui.hpp>

using namespace pulseui;

int main() {
  pulse::property<int> count{0};

  // Derived state: no manual updates anywhere.
  pulse::computed<std::string> label([&] { return "Clicked " + std::to_string(count.get()) + " times"; },
                                     count);
  pulse::computed<bool> can_reset([&] { return count.get() != 0; }, count);

  return run("Counter", 380, 220,
             VStack{
                 Text(label).title(),
                 Text(count.map([](int c) { return c % 2 ? "odd" : "even"; })).dim(),
                 HStack{
                     Button("-", [&] { count = count - 1; }),
                     Button("+", [&] { count = count + 1; }).primary(),
                     Spacer(),
                     Button("Reset", [&] { count = 0; }).flat().enabled(can_reset),
                 },
             }
                 .spacing(12)
                 .padding(24));
}
