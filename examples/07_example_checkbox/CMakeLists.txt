cmake_minimum_required(VERSION 3.21)

add_executable(example_07_checkbox
  main.cpp
)

set_property(TARGET example_07_checkbox PROPERTY CXX_STANDARD 20)
set_property(TARGET example_07_checkbox PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_07_checkbox
  PRIVATE
    PulseUI::PulseUI
    pulse
)
