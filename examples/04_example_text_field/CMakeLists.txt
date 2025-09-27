cmake_minimum_required(VERSION 3.21)

add_executable(example_04_text_field
  main.cpp
)

set_property(TARGET example_04_text_field PROPERTY CXX_STANDARD 20)
set_property(TARGET example_04_text_field PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(example_04_text_field
  PRIVATE
    PulseUI::PulseUI
    pulse
)
