# M26a 菜单悬停级联与导航补全 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 `my_menu` 增加悬停自动展开级联子菜单、可配置级联深度、ESC 在子菜单层逐层回退的交互能力。

**Architecture:** 复用 M14a 引入的 widget hover 状态机（`hover_enter`/`hover_leave` 事件），在 `menu_item_event` 中处理悬停；用一次性 timer 实现 120ms 延迟开级联；ESC 行为按 `m->parent` 链区分顶层/子层；级联深度从编译时常数改为 `my_menu_t` 字段并暴露 API。

**Tech Stack:** C99, myui 内部 my_timer/my_event, dummy port 事件注入测试。

## Global Constraints

- 保持 C99 兼容，四档 C 标准零警告。
- 所有改动必须通过既有 ctest 矩阵（build/build-c99/build-c11/build-c17/build-c23/build-wl/build-dummy/build-min/build-noimg/build-trim）。
- TDD：先写/扩展测试，再实现代码。
- API 只增不删；默认行为不变（默认级联深度仍为 3）。
- 文档同步更新 `docs/architecture.md` 与 `docs/roadmap.md`。

---

### Task 1: 扩展 `my_menu` 公共 API 与状态字段

**Files:**
- Modify: `src/myui/widgets/my_menu.h`
- Modify: `src/myui/widgets/my_menu.c`（结构体与创建/销毁）
- Test: `tests/my_menu_test.c`

**Interfaces:**
- Consumes: 现有 `my_menu_t` 结构。
- Produces:
  - `void my_menu_set_max_depth(my_menu_t* menu, int32_t depth);`
  - `int32_t my_menu_max_depth(const my_menu_t* menu);`

- [ ] **Step 1: Write failing tests for max-depth API**

```c
static void test_menu_max_depth_api(void) {
  my_menu_t* m = my_menu_create(NULL);
  assert(my_menu_max_depth(m) == 3);
  my_menu_set_max_depth(m, 5);
  assert(my_menu_max_depth(m) == 5);
  my_menu_set_max_depth(m, 1);
  assert(my_menu_max_depth(m) == 1);
  my_menu_set_max_depth(m, 0); /* clamp to 1 */
  assert(my_menu_max_depth(m) == 1);
  my_menu_destroy(m);
}
```

- [ ] **Step 2: Add fields and API implementation**

在 `struct my_menu_t` 中新增 `int32_t max_depth;`；`my_menu_create` 初始化为 3；实现 setter（`<1` 钳位到 1）和 getter。

- [ ] **Step 3: Run test**

Run: `ctest --test-dir build -R my_menu_test --output-on-failure`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/myui/widgets/my_menu.h src/myui/widgets/my_menu.c tests/my_menu_test.c
git commit -m "M26a-1: my_menu 级联深度 API，默认保持 3"
```

---

### Task 2: 悬停开级联与延迟 Timer

**Files:**
- Modify: `src/myui/widgets/my_menu.c`
- Test: `tests/my_menu_test.c`

**Interfaces:**
- Consumes: `my_widget_on(..., "hover_enter", ...)`, `my_pal_main_loop_add_timer/remove_timer`。
- Produces: 内部 `menu_open_sub_delayed` / `menu_cancel_open_timer`。

- [ ] **Step 1: Write failing hover-cascade test**

```c
static void test_menu_hover_opens_submenu(void) {
  my_window_t* win = my_window_create(NULL, dummy_pal, 400, 300, "menu");
  my_menu_t* root = my_menu_create(NULL);
  my_menu_t* sub = my_menu_add_submenu(root, "Sub");
  my_menu_add_item(sub, "Leaf", 1);
  my_menu_popup(win, root, 10, 10, NULL, NULL);
  /* hover over the submenu item (index 0) */
  my_widget_t* item = menu_item_at(root, 0);
  inject_hover_enter(item);
  /* flush timers: 120ms */
  dummy_pal_advance_ms(dummy_pal, 150);
  assert(root->open_sub != NULL);
  my_menu_dismiss(root);
  my_widget_unref(my_window_widget(win));
}
```

- [ ] **Step 2: Implement hover handling**

1. 在 `menu_item_widget_t` 中新增 `my_menu_t* menu`（已存在）、`int32_t index`（已存在）。
2. `menu_item_event` 增加 `MY_EVENT_HOVER_ENTER` 分支：
   - 设置 `iw->menu->active = iw->index`。
   - 如果 `iw->item->sub != NULL`，启动一次性 120ms timer，回调中调用 `menu_open_sub(iw->menu, iw->item, widget->rect.y)`。
   - 保存 timer id 到 `iw->menu->open_timer_id`。
3. `MY_EVENT_HOVER_LEAVE` 分支：
   - 如果离开的项持有待打开 timer，取消 timer。
   - 不关闭已打开的同级子菜单（避免误关）。
4. 当 hover 到另一项且该项无子菜单时，如果父菜单存在 `open_sub`，立即 `my_menu_dismiss(open_sub)` 并清 `open_sub`。

- [ ] **Step 3: Run tests**

Run: `ctest --test-dir build -R my_menu_test --output-on-failure`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/myui/widgets/my_menu.c tests/my_menu_test.c
git commit -m "M26a-2: 菜单悬停 120ms 延迟开级联"
```

---

### Task 3: 级联深度 >3 与 `my_menu_add_submenu` 使用运行时深度

**Files:**
- Modify: `src/myui/widgets/my_menu.c`

**Interfaces:**
- Consumes: `my_menu_max_depth`。
- Produces: 无新公共 API。

- [ ] **Step 1: Write failing deep-cascade test**

```c
static void test_menu_deep_cascade(void) {
  my_menu_t* m = my_menu_create(NULL);
  my_menu_set_max_depth(m, 5);
  my_menu_t* l2 = my_menu_add_submenu(m, "L2");
  my_menu_t* l3 = my_menu_add_submenu(l2, "L3");
  my_menu_t* l4 = my_menu_add_submenu(l3, "L4");
  my_menu_t* l5 = my_menu_add_submenu(l4, "L5");
  assert(l5 != NULL);
  assert(my_menu_add_submenu(l5, "L6") == NULL); /* depth 6 > 5 */
  my_menu_destroy(m);
}
```

- [ ] **Step 2: Replace MENU_MAX_DEPTH with runtime check**

将 `my_menu_add_submenu` 中的 `if (depth >= MENU_MAX_DEPTH)` 改为 `if (depth >= my_menu_max_depth(menu))`；保留 `MENU_MAX_DEPTH` 宏作为默认值的命名常量。

- [ ] **Step 3: Run tests**

Run: `ctest --test-dir build -R my_menu_test --output-on-failure`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/myui/widgets/my_menu.c tests/my_menu_test.c
git commit -m "M26a-3: 级联深度运行时化，支持 >3"
```

---

### Task 4: ESC 在子菜单层逐层回退

**Files:**
- Modify: `src/myui/widgets/my_menu.c`
- Test: `tests/my_menu_test.c`

**Interfaces:**
- Consumes: `m->parent` 链。
- Produces: 无新 API。

- [ ] **Step 1: Write failing ESC-back test**

```c
static void test_menu_esc_back_one_level(void) {
  my_window_t* win = my_window_create(NULL, dummy_pal, 400, 300, "menu");
  my_menu_t* root = my_menu_create(NULL);
  my_menu_t* sub = my_menu_add_submenu(root, "Sub");
  my_menu_add_item(sub, "Leaf", 1);
  my_menu_popup(win, root, 10, 10, NULL, NULL);
  /* manually open sub */
  my_menu_open_sub(root, 0);
  assert(root->open_sub != NULL);
  inject_key(root->open_sub->overlay, MY_KEY_ESCAPE);
  assert(root->open_sub == NULL);
  assert(root->overlay != NULL); /* root still open */
  my_menu_dismiss(root);
  my_widget_unref(my_window_widget(win));
}
```

- [ ] **Step 2: Modify ESC handling**

在 `menu_key_event` 的 `MY_KEY_ESCAPE` 分支：
- 如果 `m->parent != NULL`，调用 `my_menu_dismiss(m)` 并返回 OK。
- 否则保持现有 `my_menu_dismiss(m)`（关闭全部）。

- [ ] **Step 3: Run tests**

Run: `ctest --test-dir build -R my_menu_test --output-on-failure`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/myui/widgets/my_menu.c tests/my_menu_test.c
git commit -m "M26a-4: ESC 在子菜单层逐层回退"
```

---

### Task 5: 文档更新与全矩阵验证

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/superpowers/specs/2026-08-16-m26a-menu-hover-cascade-design.md`（更新状态）

- [ ] **Step 1: Update architecture.md**

将浮层基础设施节中的 "dialog 拖拽移动留 TODO" 与 "菜单鼠标悬停开级联（现要点/Enter）" 更新为：
- "dialog 拖拽移动：M16 CSD 已覆盖（栏体 POINTER_DOWN → begin_move）。"
- "菜单鼠标悬停开级联（M26a）：带 sub 项 hover 120ms 后自动展开，同级非 sub 项 hover 立即关闭已展开子菜单。"

- [ ] **Step 2: Update roadmap.md**

在 M19+ 候选中将 "菜单悬停开级联/级联深度>3/ESC 焦点回退父层" 标为 ✅ 已完成（M26a）。

- [ ] **Step 3: Run full matrix**

```bash
for d in build build-c99 build-c11 build-c17 build-c23 build-wl build-dummy build-min build-noimg build-trim; do
  make -C "$d" -j$(nproc) && ctest --test-dir "$d" -j1 --output-on-failure || break
done
```

Expected: 全部 PASS。

- [ ] **Step 4: Commit and push**

```bash
git add docs/architecture.md docs/roadmap.md docs/superpowers/specs/2026-08-16-m26a-menu-hover-cascade-design.md
git commit -m "M26a-5: 文档更新"
git push origin main
```

---

## Self-Review

- **Spec coverage:** M26a-1 悬停开级联 → Task 2；M26a-2 级联深度 → Task 1+3；M26a-3 ESC 回退 → Task 4；文档 → Task 5。无缺口。
- **Placeholder scan:** 无 TBD/TODO；测试代码块完整。
- **Type consistency:** `my_menu_set_max_depth` / `my_menu_max_depth` 签名在 Task 1 定义，Task 3 使用，一致。
