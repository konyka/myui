add_executable(demo_hello main.c)
target_link_libraries(demo_hello PRIVATE myc myr mypal)
target_compile_definitions(demo_hello PRIVATE MYUI_VERSION="${PROJECT_VERSION}")
