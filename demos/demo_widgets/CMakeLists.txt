add_executable(demo_widgets main.c)
target_link_libraries(demo_widgets PRIVATE myc myr mypal myui mymvvm mymvvm_myui)
target_include_directories(demo_widgets PRIVATE ${CMAKE_SOURCE_DIR}/3rd)
