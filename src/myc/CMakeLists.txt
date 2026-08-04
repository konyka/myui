# myc — myui base library (allocator, containers, utf8 str, emitter, value, object).
add_library(myc STATIC
  my_mem.c
  my_darray.c
  my_str.c
  my_emitter.c
  my_value.c
  my_object.c
)

target_include_directories(myc PUBLIC ${CMAKE_SOURCE_DIR}/src)
