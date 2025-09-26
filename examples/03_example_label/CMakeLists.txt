cmake_minimum_required(VERSION 3.21)

add_executable(03_example_label
  main.cpp
)

set_property(TARGET 03_example_label PROPERTY CXX_STANDARD 20)
set_property(TARGET 03_example_label PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(03_example_label
  PRIVATE
    PulseUI::PulseUI
    pulse
)
