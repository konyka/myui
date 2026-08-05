/**
 * @file bench_widget.c
 * @brief Widget-tree benchmark: build 1000 widgets, full relayout,
 * 100k hit_tests. Prints timings with loose regression guards.
 * Registered in ctest with label "bench".
 */
#include <stdio.h>
#include <time.h>

#include "myui/my_layout.h"
#include "myui/widgets/my_list_view.h"

typedef struct mock_adapter_t {
  my_list_adapter_t base;
  size_t count;
} mock_adapter_t;

static size_t bench_row_count(my_list_adapter_t* a) {
  return ((mock_adapter_t*)a)->count;
}

static my_widget_t* bench_create_row(my_list_adapter_t* a) {
  (void)a;
  return my_widget_create(NULL, "row");
}

static void bench_bind_row(my_list_adapter_t* a, my_widget_t* row,
                           size_t index) {
  (void)a;
  (void)row;
  (void)index;
}

static const my_list_adapter_vtable_t BENCH_ADAPTER = {
    bench_row_count, bench_create_row, bench_bind_row, NULL};

static void mock_adapter_init(mock_adapter_t* a, size_t count) {
  a->base.vtable = &BENCH_ADAPTER;
  a->count = count;
}

#define BENCH_ROWS 50
#define BENCH_COLS 20
#define BENCH_HITS 100000

static double now_ms(void) {
  return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

int main(void) {
  my_widget_t* root = my_widget_create(NULL, "root");
  int r, c;
  long hits = 0;
  double t0, t1, t_build, t_layout, t_hit;
  size_t i;

  my_widget_set_rect(root, &(my_rect_t){0, 0, 800, 600});
  my_widget_set_layouter(root, my_layouter_linear_create(NULL, false, 0));

  t0 = now_ms();
  for (r = 0; r < BENCH_ROWS; r++) {
    my_widget_t* row = my_widget_create(NULL, "row");
    my_widget_set_layout_params(row, "h:12");
    my_widget_set_layouter(row, my_layouter_linear_create(NULL, true, 0));
    for (c = 0; c < BENCH_COLS; c++) {
      my_widget_t* cell = my_widget_create(NULL, "cell");
      my_widget_set_layout_params(cell, "w:40 h:12");
      my_widget_add_child(row, cell);
      my_widget_unref(cell);
    }
    my_widget_add_child(root, row);
    my_widget_unref(row);
  }
  t1 = now_ms();
  t_build = t1 - t0;

  t0 = now_ms();
  my_widget_relayout(root);
  t1 = now_ms();
  t_layout = t1 - t0;

  t0 = now_ms();
  for (i = 0; i < BENCH_HITS; i++) {
    my_widget_t* hit = my_widget_hit_test(root, (int32_t)(i % 800),
                                          (int32_t)((i / 800) % 600));
    if (hit != NULL) {
      hits++;
    }
  }
  t1 = now_ms();
  t_hit = t1 - t0;

  printf("bench_widget: build %d widgets %.2f ms, relayout %.2f ms, "
         "%d hit_tests %.2f ms (%ld hits)\n",
         BENCH_ROWS * BENCH_COLS + BENCH_ROWS + 1, t_build, t_layout,
         BENCH_HITS, t_hit, hits);

  /* list_view scroll: 10000 rows, 100 scroll steps */
  {
    mock_adapter_t a;
    my_widget_t* lv;
    int f;
    mock_adapter_init(&a, 10000);
    lv = my_list_view_create(NULL);
    my_widget_set_rect(lv, &(my_rect_t){0, 0, 400, 480});
    my_list_view_set_adapter(lv, (my_list_adapter_t*)&a);
    t0 = now_ms();
    for (f = 0; f < 100; f++) {
      my_list_view_set_scroll_offset(lv, f * 96);
    }
    t1 = now_ms();
    printf("bench_widget: list_view 10000 rows x 100 scrolls: total %.2f ms, "
           "avg %.3f ms/scroll (rows created: %zu)\n",
           t1 - t0, (t1 - t0) / 100, my_list_view_rows_created_total(lv));
    my_widget_unref(lv);
  }

  my_widget_unref(root);

  if (t_build > 5000.0 || t_layout > 1000.0 || t_hit > 5000.0) {
    fprintf(stderr, "bench_widget: too slow\n");
    return 1;
  }
  return 0;
}
