cmake_minimum_required(VERSION 3.20)

project(myui VERSION 0.1.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MYUI_BUILD_TESTS "Build unit tests" ON)
option(MYUI_BUILD_DEMOS "Build demos" ON)
option(MYUI_FONT_STB "TrueType fonts via stb_truetype (OFF = bitmap font only)" ON)
set(MYUI_C_STANDARD "99" CACHE STRING "C standard to compile with (99/11/17/23)")
set_property(CACHE MYUI_C_STANDARD PROPERTY STRINGS 99 11 17 23)

if(NOT MYUI_C_STANDARD MATCHES "^(99|11|17|23)$")
  message(FATAL_ERROR "MYUI_C_STANDARD must be one of 99/11/17/23, got '${MYUI_C_STANDARD}'")
endif()

set(CMAKE_C_STANDARD ${MYUI_C_STANDARD})
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Global compile settings: strict warnings, all myui code must stay clean.
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  add_compile_options(-Wall -Wextra -Werror -pedantic)
elseif(MSVC)
  add_compile_options(/W4 /WX)
endif()

# ---------------------------------------------------------------------------
# PAL port selection: auto resolves to x11 when available, else dummy.
# ---------------------------------------------------------------------------
set(MYUI_PAL "auto" CACHE STRING "PAL port: auto/x11/dummy/linux_fb/wayland")
set_property(CACHE MYUI_PAL PROPERTY STRINGS auto x11 dummy linux_fb wayland)
# auto (Linux and FreeBSD alike): x11 preferred, dummy fallback.
if(MYUI_PAL STREQUAL "auto")
  find_package(X11 QUIET)
  if(X11_FOUND)
    set(MYUI_PAL_RESOLVED "x11")
  else()
    set(MYUI_PAL_RESOLVED "dummy")
  endif()
elseif(MYUI_PAL STREQUAL "x11")
  find_package(X11 REQUIRED)
  set(MYUI_PAL_RESOLVED "x11")
elseif(MYUI_PAL STREQUAL "dummy")
  set(MYUI_PAL_RESOLVED "dummy")
elseif(MYUI_PAL STREQUAL "linux_fb")
  set(MYUI_PAL_RESOLVED "linux_fb")
elseif(MYUI_PAL STREQUAL "wayland")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
  set(MYUI_PAL_RESOLVED "wayland")
else()
  message(FATAL_ERROR "MYUI_PAL must be auto/x11/dummy/linux_fb/wayland, got '${MYUI_PAL}'")
endif()

# Public include root: sources include as "myc/my_darray.h".
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src/myc)
add_subdirectory(src/myr)
add_subdirectory(src/mypal)
add_subdirectory(src/myui)
add_subdirectory(src/mymvvm)
add_subdirectory(src/mymvvm_myui)

if(MYUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MYUI_BUILD_DEMOS)
  add_subdirectory(demos/demo_hello)
  add_subdirectory(demos/demo_widgets)
  add_subdirectory(demos/demo_mvvm)
endif()

message(STATUS "myui ${PROJECT_VERSION}: C${MYUI_C_STANDARD}, tests=${MYUI_BUILD_TESTS}, demos=${MYUI_BUILD_DEMOS}")
