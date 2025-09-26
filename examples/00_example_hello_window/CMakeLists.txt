cmake_minimum_required(VERSION 3.21)

add_executable(example_00_hello
  main.cpp
)

set_property(TARGET example_00_hello PROPERTY CXX_STANDARD 20)
set_property(TARGET example_00_hello PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_00_hello
  PRIVATE
    PulseUI::PulseUI
    pulse
)
