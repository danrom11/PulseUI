cmake_minimum_required(VERSION 3.21)

add_executable(04_example_text_field
  main.cpp
)

set_property(TARGET 04_example_text_field PROPERTY CXX_STANDARD 20)
set_property(TARGET 04_example_text_field PROPERTY CXX_STANDARD_REQUIRED ON)

target_link_libraries(04_example_text_field
  PRIVATE
    PulseUI::PulseUI
    pulse
)
