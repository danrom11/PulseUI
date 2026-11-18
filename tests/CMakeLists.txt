add_executable(pulseui_tests ui_tests.cpp)
target_link_libraries(pulseui_tests PRIVATE PulseUI::PulseUI)
target_compile_options(pulseui_tests PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG /utf-8,-UNDEBUG>)
add_test(NAME pulseui_tests COMMAND pulseui_tests)
