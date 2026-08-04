# mypal — platform abstraction: events, timers, pal vtables, ports.
# MYUI_PAL_RESOLVED (x11|dummy) is computed in the top-level CMakeLists.

# dummy port is always compiled; linux_fb (POSIX-only) and its osal only
# on UNIX (tests inject a fake device via my_osal_t)
set(MYPAL_SOURCES
  my_timer.c
  my_pal.c
  dummy/my_pal_dummy.c
)
if(UNIX)
  list(APPEND MYPAL_SOURCES my_osal.c linux_fb/my_pal_linux_fb.c)
endif()

if(MYUI_PAL_RESOLVED STREQUAL "x11")
  list(APPEND MYPAL_SOURCES x11/my_pal_x11.c x11/my_pal_x11_keymap.c)
elseif(MYUI_PAL_RESOLVED STREQUAL "wayland")
  # generate xdg-shell protocol code (wayland-scanner + system protocols)
  pkg_check_modules(WAYLAND_PROTOCOLS QUIET wayland-protocols)
  find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
  if(WAYLAND_PROTOCOLS_FOUND)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  else()
    set(WAYLAND_PROTOCOLS_DIR "/usr/share/wayland-protocols")
  endif()
  set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
  set(XDG_SHELL_H "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
  set(XDG_SHELL_C "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")
  add_custom_command(
    OUTPUT ${XDG_SHELL_H} ${XDG_SHELL_C}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${XDG_SHELL_H}
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${XDG_SHELL_C}
    DEPENDS ${XDG_SHELL_XML}
    COMMENT "Generating xdg-shell protocol code")
  list(APPEND MYPAL_SOURCES
    wayland/my_pal_wayland.c wayland/my_pal_wayland_keymap.c ${XDG_SHELL_C})
endif()

add_library(mypal STATIC ${MYPAL_SOURCES})
target_include_directories(mypal PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mypal PUBLIC myc myr)

if(MYUI_PAL_RESOLVED STREQUAL "x11")
  target_compile_definitions(mypal PUBLIC MYUI_PAL_X11)
  target_include_directories(mypal PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(mypal PUBLIC ${X11_LIBRARIES})
  find_package(Threads REQUIRED)
  target_link_libraries(mypal PUBLIC Threads::Threads)
elseif(MYUI_PAL_RESOLVED STREQUAL "wayland")
  target_compile_definitions(mypal PUBLIC MYUI_PAL_WAYLAND)
  target_include_directories(mypal PRIVATE
    ${WAYLAND_CLIENT_INCLUDE_DIRS} ${XKBCOMMON_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(mypal PUBLIC ${WAYLAND_CLIENT_LIBRARIES}
    ${XKBCOMMON_LIBRARIES})
elseif(MYUI_PAL_RESOLVED STREQUAL "linux_fb")
  target_compile_definitions(mypal PUBLIC MYUI_PAL_LINUX_FB)
else()
  target_compile_definitions(mypal PUBLIC MYUI_PAL_DUMMY)
endif()
