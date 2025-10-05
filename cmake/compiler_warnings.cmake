# compiler_warnings.cmake (PulseUI)
# Unified warnings for different authors.
#
# API:
#   pulseui_enable_warnings(<target> [AS_ERROR] [NO_PEDANTIC])
#   pulseui_disable_warnings(<target>)
#   pulseui_create_warnings_interface(TARGET <name> [AS_ERROR] [NO_PEDANTIC])

include(CMakeDependentOption)

function(_pulseui_collect_warning_flags out as_error no_pedantic)
  set(_gcc_like_base
    -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion
    -Wformat=2 -Wundef -Wcast-qual -Wcast-align -Wnon-virtual-dtor
    -Woverloaded-virtual -Wold-style-cast -Wimplicit-fallthrough
    -Wduplicated-branches -Wduplicated-cond -Wnull-dereference
    -Wno-psabi
  )
  if (NOT ${no_pedantic})
    list(APPEND _gcc_like_base -Wpedantic)
  endif()

  set(_clang_extra
    -Wno-c++98-compat -Wno-c++98-compat-pedantic
    -Wno-missing-prototypes
  )

  set(_msvc_base
    /W4 /permissive- /Zc:__cplusplus
    /w44265
    /w44062
    /w44061
  )

  if (${as_error})
    set(_gcc_like_error -Werror)
    set(_msvc_error /WX)
  else()
    set(_gcc_like_error "")
    set(_msvc_error "")
  endif()

  set(${out}
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:${_gcc_like_base} ${_gcc_like_error}>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:${_gcc_like_base} ${_gcc_like_error}>
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:${_clang_extra}>
    $<$<C_COMPILER_ID:Clang,AppleClang>:${_clang_extra}>
    $<$<CXX_COMPILER_ID:MSVC>:${_msvc_base} ${_msvc_error}>
    $<$<C_COMPILER_ID:MSVC>:${_msvc_base} ${_msvc_error}>
  PARENT_SCOPE)
endfunction()

function(pulseui_enable_warnings target)
  if (NOT TARGET ${target})
    message(FATAL_ERROR "pulseui_enable_warnings: target '${target}' does not exist")
  endif()
  set(options AS_ERROR NO_PEDANTIC)
  cmake_parse_arguments(PW "${options}" "" "" ${ARGN})
  _pulseui_collect_warning_flags(_flags ${PW_AS_ERROR} ${PW_NO_PEDANTIC})
  target_compile_options(${target} PRIVATE ${_flags})
endfunction()

function(pulseui_create_warnings_interface)
  set(oneValueArgs TARGET)
  set(options AS_ERROR NO_PEDANTIC)
  cmake_parse_arguments(PWI "${options}" "${oneValueArgs}" "" ${ARGN})
  if (NOT PWI_TARGET)
    message(FATAL_ERROR "pulseui_create_warnings_interface: pass TARGET <name>")
  endif()
  add_library(${PWI_TARGET} INTERFACE)
  _pulseui_collect_warning_flags(_flags ${PWI_AS_ERROR} ${PWI_NO_PEDANTIC})
  target_compile_options(${PWI_TARGET} INTERFACE ${_flags})
endfunction()

function(pulseui_disable_warnings target)
  if (NOT TARGET ${target})
    message(FATAL_ERROR "pulseui_disable_warnings: target '${target}' does not exist")
  endif()
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<CXX_COMPILER_ID:MSVC>:/w>
    $<$<C_COMPILER_ID:MSVC>:/w>
  )
endfunction()
