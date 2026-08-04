# Unit tests: one executable per *_test.c, all registered with ctest.
file(GLOB MYUI_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.c")

# x11-specific tests are registered only when the x11 port is built
set(MYUI_X11_TESTS
  "${CMAKE_CURRENT_SOURCE_DIR}/my_pal_x11_keymap_test.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/x11_smoke_test.c"
)
# gles2 smoke test needs EGL+GLES2 dev libraries
set(MYUI_GLES2_TESTS
  "${CMAKE_CURRENT_SOURCE_DIR}/gles2_smoke_test.c"
)
# wayland tests need the wayland port built
set(MYUI_WAYLAND_TESTS
  "${CMAKE_CURRENT_SOURCE_DIR}/my_pal_wayland_keymap_test.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/wayland_smoke_test.c"
)
# linux_fb test needs POSIX (my_osal)
set(MYUI_LINUX_FB_TESTS
  "${CMAKE_CURRENT_SOURCE_DIR}/my_pal_linux_fb_test.c"
)
list(REMOVE_ITEM MYUI_TEST_SOURCES ${MYUI_X11_TESTS} ${MYUI_GLES2_TESTS}
     ${MYUI_WAYLAND_TESTS} ${MYUI_LINUX_FB_TESTS})

foreach(test_src ${MYUI_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE myc myr mypal myui mymvvm mymvvm_myui)
  target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(MYUI_PAL_RESOLVED STREQUAL "x11")
  foreach(test_src ${MYUI_X11_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE myc myr mypal myui mymvvm mymvvm_myui)
    target_include_directories(${test_name}
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${X11_INCLUDE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

# real-context gles2 smoke (skips gracefully when no EGL context works)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(MYUI_EGL QUIET egl)
  pkg_check_modules(MYUI_GLESV2 QUIET glesv2)
endif()
if(MYUI_GLESV2_FOUND AND MYUI_EGL_FOUND)
  foreach(test_src ${MYUI_GLES2_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE myc myr ${MYUI_EGL_LIBRARIES})
    target_include_directories(${test_name}
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYUI_EGL_INCLUDE_DIRS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

if(MYUI_PAL_RESOLVED STREQUAL "wayland")
  foreach(test_src ${MYUI_WAYLAND_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE myc myr mypal myui)
    target_include_directories(${test_name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR} ${WAYLAND_CLIENT_INCLUDE_DIRS}
      ${XKBCOMMON_INCLUDE_DIRS})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

if(UNIX)
  foreach(test_src ${MYUI_LINUX_FB_TESTS})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE myc myr mypal myui mymvvm mymvvm_myui)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()

# golden-image tests need the reference directory path
target_compile_definitions(my_golden_test
  PRIVATE MY_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# golden-image reference generator (NOT a ctest test; run manually to
# regenerate tests/golden/*.ppm after intentional rendering changes)
add_executable(my_golden_gen golden_gen.c)
target_link_libraries(my_golden_gen PRIVATE myc myr)
target_include_directories(my_golden_gen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# performance baselines (label: bench)
foreach(bench_name bench_render bench_widget)
  add_executable(${bench_name} bench/${bench_name}.c)
  target_link_libraries(${bench_name} PRIVATE myc myr mypal myui)
  add_test(NAME ${bench_name} COMMAND ${bench_name})
  set_tests_properties(${bench_name} PROPERTIES LABELS bench)
endforeach()
