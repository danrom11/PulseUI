cmake_minimum_required(VERSION 3.21)

add_executable(example_06_windows_navigation
    main.cpp
    FirstScreen.hpp
    FirstScreen.cpp
    SecondScreen.hpp
    SecondScreen.cpp
)

set_property(TARGET example_06_windows_navigation PROPERTY CXX_STANDARD 20)
set_property(TARGET example_06_windows_navigation PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_06_windows_navigation
  PRIVATE
    PulseUI::PulseUI
    pulse
)
