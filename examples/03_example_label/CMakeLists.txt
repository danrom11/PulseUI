cmake_minimum_required(VERSION 3.21)

add_executable(example_03_label
  main.cpp
)

set_property(TARGET example_03_label PROPERTY CXX_STANDARD 20)
set_property(TARGET example_03_label PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_03_label
  PRIVATE
    PulseUI::PulseUI
    pulse
)
