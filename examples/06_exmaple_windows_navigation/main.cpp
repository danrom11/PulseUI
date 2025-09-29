#include <pulseui/pulseui.hpp>

#include "FirstScreen.hpp"
#include "SecondScreen.hpp"

using namespace pulseui;

int main() {
    platform::app_init();

    SecondScreen second;
    FirstScreen first(&second);

    first.show();

    platform::app_run();
    return 0;
}
