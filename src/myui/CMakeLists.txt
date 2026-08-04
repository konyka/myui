# myui — widget core: widget base, layout, event dispatch, window +
# window manager, built-in widgets.
add_library(myui STATIC
  my_widget.c
  my_layout.c
  my_event_dispatch.c
  my_style.c
  my_theme.c
  my_animator.c
  my_window.c
  my_window_manager.c
  widgets/my_button.c
  widgets/my_label.c
  widgets/my_edit.c
  widgets/my_checkbox.c
  widgets/my_slider.c
  widgets/my_progress_bar.c
)

target_include_directories(myui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(myui PUBLIC myc myr mypal)
