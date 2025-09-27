cmake_minimum_required(VERSION 3.21)

add_executable(example_05_layout
  main.cpp
)

set_property(TARGET example_05_layout PROPERTY CXX_STANDARD 20)
set_property(TARGET example_05_layout PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_05_layout
  PRIVATE
    PulseUI::PulseUI
    pulse
)
