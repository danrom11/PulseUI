cmake_minimum_required(VERSION 3.21)

project(PulseUI VERSION 0.0.1 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

set(PULSE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PULSE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/pulseui/config.hpp
  @ONLY
)

# ------------------------------------------------------------------
# Options
# ------------------------------------------------------------------
option(PULSEUI_BACKEND "Backend to use (cocoa|win32)" "")
option(PULSEUI_FETCH_PULSE "Fetch Pulse library automatically" ON)

# ------------------------------------------------------------------
# Dependency: Pulse (via FetchContent)
# ------------------------------------------------------------------
include(FetchContent)
if(PULSEUI_FETCH_PULSE)
  FetchContent_Declare(
    pulse
    GIT_REPOSITORY https://github.com/danrom11/Pulse.git
    GIT_TAG        main
  )
  FetchContent_MakeAvailable(pulse)
endif()

# ------------------------------------------------------------------
# PulseUI Public Headers
# ------------------------------------------------------------------
if(NOT TARGET PulseUI_ui)
  add_library(PulseUI_ui INTERFACE)
  add_library(PulseUI::ui ALIAS PulseUI_ui)

  target_include_directories(PulseUI_ui
    INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

# ------------------------------------------------------------------
# Platform backends
# ------------------------------------------------------------------

# ---- macOS Cocoa ----
if(APPLE AND NOT TARGET PulseUI_platform_cocoa)
  add_library(PulseUI_platform_cocoa STATIC
    src/ui/app.mm
    src/platform/cocoa/aliases.mm
    src/platform/cocoa/window_cocoa.mm
    src/platform/cocoa/canvas_cg.mm
    src/platform/cocoa/executor_cocoa.mm
    src/platform/cocoa/timers_cocoa.mm
  )

  target_link_libraries(PulseUI_platform_cocoa
    PUBLIC
      PulseUI::ui
      "-framework AppKit"
      "-framework CoreGraphics"
      "-framework CoreText"
      "-framework QuartzCore"
      "-framework Foundation"
  )

  set_target_properties(PulseUI_platform_cocoa PROPERTIES
    OBJCXX_EXTENSIONS OFF
  )

  target_compile_options(PulseUI_platform_cocoa PRIVATE
    -fobjc-arc
    -fobjc-arc-exceptions
  )

  target_include_directories(PulseUI_platform_cocoa
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

# ---- Windows Win32 + GDI ----
if(WIN32 AND NOT TARGET PulseUI_platform_win32)
  add_library(PulseUI_platform_win32 STATIC
    src/ui/app_win32.cpp
    src/platform/win32/aliases.cpp
    src/platform/win32/window_win32.cpp
    src/platform/win32/executor_win32.cpp
  )
  target_link_libraries(PulseUI_platform_win32 PUBLIC PulseUI::ui user32 gdi32)

  target_include_directories(PulseUI_platform_win32
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

# ------------------------------------------------------------------
# Selecting an active platform target
# ------------------------------------------------------------------
if(PULSEUI_BACKEND STREQUAL "cocoa")
  if(NOT TARGET PulseUI_platform_cocoa)
    message(FATAL_ERROR "Requested backend 'cocoa' but target PulseUI_platform_cocoa is not available.")
  endif()
  add_library(PulseUI_platform ALIAS PulseUI_platform_cocoa)
elseif(PULSEUI_BACKEND STREQUAL "win32")
  if(NOT TARGET PulseUI_platform_win32)
    message(FATAL_ERROR "Requested backend 'win32' but target PulseUI_platform_win32 is not available.")
  endif()
  add_library(PulseUI_platform ALIAS PulseUI_platform_win32)
else()
  if(APPLE)
    if(NOT TARGET PulseUI_platform_cocoa)
      message(FATAL_ERROR "macOS detected but Cocoa backend is missing.")
    endif()
    add_library(PulseUI_platform ALIAS PulseUI_platform_cocoa)
  elseif(WIN32)
    if(NOT TARGET PulseUI_platform_win32)
      message(FATAL_ERROR "Windows detected but Win32 backend is missing.")
    endif()
    add_library(PulseUI_platform ALIAS PulseUI_platform_win32)
  else()
    message(FATAL_ERROR "No backend available for this platform (add x11/sdl/qt backend).")
  endif()
endif()

# ------------------------------------------------------------------
# Summary interface target
# ------------------------------------------------------------------
if(NOT TARGET PulseUI)
  add_library(PulseUI INTERFACE)
endif()
target_link_libraries(PulseUI INTERFACE PulseUI_platform)
add_library(PulseUI::PulseUI ALIAS PulseUI)

# ------------------------------------------------------------------
# INSTALL / PACKAGE
# ------------------------------------------------------------------
include(CMakePackageConfigHelpers)

set(PULSEUI_CMAKE_INSTALL_DIR "lib/cmake/PulseUI")

set(PULSEUI_INSTALL_TARGETS PulseUI_ui PulseUI)
if(TARGET PulseUI_platform_cocoa)
  list(APPEND PULSEUI_INSTALL_TARGETS PulseUI_platform_cocoa)
endif()
if(TARGET PulseUI_platform_win32)
  list(APPEND PULSEUI_INSTALL_TARGETS PulseUI_platform_win32)
endif()

install(TARGETS ${PULSEUI_INSTALL_TARGETS}
  EXPORT PulseUITargets
  ARCHIVE       DESTINATION lib
  LIBRARY       DESTINATION lib
  RUNTIME       DESTINATION bin
  INCLUDES      DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/include/pulseui/config.hpp
  DESTINATION include/pulseui
)

export(EXPORT PulseUITargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/PulseUITargets.cmake"
  NAMESPACE PulseUI::
)

set(PULSEUI_CONFIG_IN   "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PulseUIConfig.cmake.in")
set(PULSEUI_WARNINGS_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_warnings.cmake")

set(PACKAGE_CMAKE_INSTALL_DIR "${PULSEUI_CMAKE_INSTALL_DIR}")
set(PACKAGE_INCLUDE_INSTALL_DIR "include")

configure_package_config_file(
  "${PULSEUI_CONFIG_IN}"
  "${CMAKE_CURRENT_BINARY_DIR}/PulseUIConfig.cmake"
  INSTALL_DESTINATION "${PULSEUI_CMAKE_INSTALL_DIR}"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PulseUIConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PulseUIConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PulseUIConfigVersion.cmake"
  "${PULSEUI_WARNINGS_IN}"
  DESTINATION "${PULSEUI_CMAKE_INSTALL_DIR}"
)

install(EXPORT PulseUITargets
  NAMESPACE PulseUI::
  DESTINATION ${PULSEUI_CMAKE_INSTALL_DIR}
)

# ------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------
add_subdirectory(examples/00_example_hello_window)
add_subdirectory(examples/01_example_counter_reactive)
add_subdirectory(examples/02_example_button)
add_subdirectory(examples/03_example_label)
add_subdirectory(examples/04_example_text_field)
add_subdirectory(examples/05_example_layout)
add_subdirectory(examples/06_example_windows_navigation)
add_subdirectory(examples/07_example_checkbox)

message(STATUS "PulseUI ${PROJECT_VERSION} configured.")
