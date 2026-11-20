cmake_minimum_required(VERSION 3.21)

project(PulseUI VERSION 0.3.0 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

option(PULSEUI_BUILD_EXAMPLES "Build PulseUI examples" ${PROJECT_IS_TOP_LEVEL})
option(PULSEUI_BUILD_TESTS    "Build PulseUI tests"    ${PROJECT_IS_TOP_LEVEL})
set(PULSEUI_PULSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Pulse" CACHE PATH
    "Local Pulse checkout (falls back to FetchContent when missing)")

# ------------------------------------------------------------------
# Dependency: Pulse (local sibling checkout or FetchContent)
# ------------------------------------------------------------------
if(NOT TARGET Pulse::pulse)
  set(PULSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PULSE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  if(EXISTS "${PULSEUI_PULSE_DIR}/CMakeLists.txt")
    add_subdirectory("${PULSEUI_PULSE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_pulse")
  else()
    include(FetchContent)
    FetchContent_Declare(pulse GIT_REPOSITORY https://github.com/danrom11/Pulse.git GIT_TAG main)
    FetchContent_MakeAvailable(pulse)
  endif()
endif()

# ------------------------------------------------------------------
# Library
# ------------------------------------------------------------------
set(PULSEUI_SOURCES
  src/core/json.cpp
  src/gfx/gfx.cpp
  src/ui/theme.cpp
  src/ui/view.cpp
  src/ui/containers.cpp
  src/ui/widgets.cpp
  src/ui/code_editor.cpp
  src/ui/window.cpp
  src/ui/meta.cpp
  src/ui/form.cpp
  src/ui/testing.cpp
)

if(WIN32)
  list(APPEND PULSEUI_SOURCES
    src/platform/win32/win32_platform.cpp
    src/platform/win32/win32_window.cpp
    src/platform/win32/d2d_painter.cpp
  )
elseif(APPLE)
  list(APPEND PULSEUI_SOURCES
    src/platform/cocoa/cocoa_platform.mm
    src/platform/cocoa/cocoa_window.mm
    src/platform/cocoa/cg_painter.mm
  )
else()
  message(FATAL_ERROR "PulseUI: no backend for this platform yet (Windows and macOS are supported).")
endif()

add_library(PulseUI STATIC ${PULSEUI_SOURCES})
add_library(PulseUI::PulseUI ALIAS PulseUI)

target_include_directories(PulseUI PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(PulseUI PUBLIC cxx_std_20)
target_link_libraries(PulseUI PUBLIC Pulse::pulse)

if(WIN32)
  target_compile_definitions(PulseUI PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(PulseUI PRIVATE d2d1 dwrite windowscodecs ole32 shell32 user32 gdi32 uuid)
  if(MSVC)
    target_compile_options(PulseUI PRIVATE /W4 /permissive- /utf-8 /wd4100 /wd4244 /wd4267)
  else()
    target_compile_options(PulseUI PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  endif()
elseif(APPLE)
  set_target_properties(PulseUI PROPERTIES OBJCXX_EXTENSIONS OFF)
  set_source_files_properties(
    src/platform/cocoa/cocoa_platform.mm
    src/platform/cocoa/cocoa_window.mm
    src/platform/cocoa/cg_painter.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(PulseUI PRIVATE
    "-framework AppKit" "-framework CoreGraphics" "-framework CoreText"
    "-framework QuartzCore" "-framework Foundation" "-framework ImageIO")
  target_compile_options(PulseUI PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Helper for GUI executables: no console window on Windows, .app bundle on macOS.
function(pulseui_add_app NAME)
  add_executable(${NAME} WIN32 MACOSX_BUNDLE ${ARGN})
  target_link_libraries(${NAME} PRIVATE PulseUI::PulseUI)
  if(MSVC)
    target_compile_options(${NAME} PRIVATE /utf-8 /permissive-)
    # keep a regular main() as the entry point for WIN32 apps
    target_link_options(${NAME} PRIVATE /ENTRY:mainCRTStartup)
  elseif(WIN32)
    target_link_options(${NAME} PRIVATE -mwindows)
  endif()
endfunction()

if(PULSEUI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PULSEUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
