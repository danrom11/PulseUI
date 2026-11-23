#pragma once
// PulseUI - declarative reactive UI for C++20 on top of Pulse.
//
//   #include <pulseui/pulseui.hpp>
//   using namespace pulseui;
//
//   int main() {
//     pulse::property<int> count{0};
//     return run("Counter", 360, 200,
//       VStack{
//         Text(count.map([](int c) { return "Count: " + std::to_string(c); })).title(),
//         HStack{
//           Button("-", [&] { count = count - 1; }),
//           Button("+", [&] { count = count + 1; }).primary(),
//         },
//       }.padding(24));
//   }

#include <pulse/pulse.hpp>
#include <pulseui/version.hpp>

#include <pulseui/core/color.hpp>
#include <pulseui/core/geometry.hpp>
#include <pulseui/core/json.hpp>
#include <pulseui/gfx/font.hpp>
#include <pulseui/gfx/image.hpp>
#include <pulseui/gfx/painter.hpp>
#include <pulseui/gfx/svg.hpp>
#include <pulseui/ui/accessibility.hpp>
#include <pulseui/ui/animation.hpp>
#include <pulseui/ui/bind.hpp>
#include <pulseui/ui/code_editor.hpp>
#include <pulseui/ui/collections.hpp>
#include <pulseui/ui/containers.hpp>
#include <pulseui/ui/dialogs.hpp>
#include <pulseui/ui/events.hpp>
#include <pulseui/ui/form.hpp>
#include <pulseui/ui/inputs.hpp>
#include <pulseui/ui/menu.hpp>
#include <pulseui/ui/meta.hpp>
#include <pulseui/ui/theme.hpp>
#include <pulseui/ui/view.hpp>
#include <pulseui/ui/widgets.hpp>
#include <pulseui/ui/window.hpp>

namespace pui = pulseui;
