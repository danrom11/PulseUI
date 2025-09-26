cmake_minimum_required(VERSION 3.21)

add_executable(example_01_counter
  main.cpp
)

set_property(TARGET example_01_counter PROPERTY CXX_STANDARD 20)
set_property(TARGET example_01_counter PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_01_counter
  PRIVATE
    PulseUI::PulseUI
    pulse
)
