cmake_minimum_required(VERSION 3.21)

add_executable(02_example_button
  main.cpp
)

set_property(TARGET 02_example_button PROPERTY CXX_STANDARD 20)
set_property(TARGET 02_example_button PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(02_example_button
  PRIVATE
    PulseUI::PulseUI
    pulse
)
