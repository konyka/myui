# myr — rendering abstraction: color/rect/dirty-rects, lcd interface + mem
# backend, vgcanvas interface + software rasterizer backend.
add_library(myr STATIC
  my_dirty_rects.c
  my_lcd_mem.c
  my_vgcanvas_soft.c
  my_vgcanvas_gles2.c
  my_gl_real.c
)

target_include_directories(myr PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(myr PUBLIC myc)

# math library for the software rasterizer (floorf/ceilf/sqrtf)
if(NOT MSVC)
  target_link_libraries(myr PUBLIC m)
endif()

# optional GLES2 backend support (real GL table; mock tests work without it)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GLESV2 QUIET glesv2)
endif()
if(GLESV2_FOUND)
  target_compile_definitions(myr PUBLIC MYUI_HAS_GLES2)
  target_include_directories(myr PUBLIC ${GLESV2_INCLUDE_DIRS})
  target_link_libraries(myr PUBLIC ${GLESV2_LIBRARIES})
  message(STATUS "myui: GLES2 backend enabled")
else()
  message(STATUS "myui: GLES2 backend stubbed (glesv2 not found)")
endif()
