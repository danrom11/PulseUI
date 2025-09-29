cmake_minimum_required(VERSION 3.21)

add_executable(06_exmaple_windows_navigation
    main.cpp
    FirstScreen.hpp
    FirstScreen.cpp
    SecondScreen.hpp
    SecondScreen.cpp
)

set_property(TARGET 06_exmaple_windows_navigation PROPERTY CXX_STANDARD 20)
set_property(TARGET 06_exmaple_windows_navigation PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(06_exmaple_windows_navigation
  PRIVATE
    PulseUI::PulseUI
    pulse
)
