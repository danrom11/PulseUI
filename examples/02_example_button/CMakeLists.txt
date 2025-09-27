cmake_minimum_required(VERSION 3.21)

add_executable(example_02_button
  main.cpp
)

set_property(TARGET example_02_button PROPERTY CXX_STANDARD 20)
set_property(TARGET example_02_button PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_02_button
  PRIVATE
    PulseUI::PulseUI
    pulse
)
