# M26a 菜单悬停级联与导航补全

日期：2026-08-16　状态：已批准执行

## 1. 缺口盘点（M13c/M16 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| 菜单悬停自动开级联 | 桌面菜单标准交互 | 是（dummy 注入事件 + dump 目检） |
| 级联深度 >3 | 复杂菜单可用性 | 是（dummy 断言） |
| ESC 焦点回退父层 | 键盘导航闭环 | 是（dummy 注入事件） |

当前 `my_menu` 已实现点击/Enter 开级联、级联上限 3、键盘 Up/Down/Enter/ESC。桌面用户期望鼠标悬停带子菜单的项时自动展开，移动到非子菜单项时关闭已展开的同级子菜单；键盘 ESC 在子菜单层应逐层回退而非直接关闭全部。

## 2. M26a 范围与切分

- **M26a-1 悬停开级联**：菜单项响应 `hover_enter`（或 `POINTER_MOVE` 命中），对带 `sub` 的项延迟 120ms 后调用 `menu_open_sub`；延迟避免鼠标快速扫过子菜单区域时频繁开关。同级已开子菜单在 hover 到另一项时立即关闭（无延迟，保持响应）。
- **M26a-2 级联深度可调**：`MENU_MAX_DEPTH` 从硬编码 3 改为运行时 `my_menu_set_max_depth(menu, depth)`，默认保持 3；深度校验上移到 `my_menu_popup` 与 `my_menu_add_submenu` 共同使用。支持 >3 的测试用例。
- **M26a-3 ESC 焦点回退父层**：子菜单 overlay 捕获 ESC 时，先 `my_menu_dismiss(this)` 关闭自身并返回 OK；顶层菜单 ESC 行为保持为关闭全部。通过 `m->parent` 链判断层级。

## 3. 性能与效果平衡

- 悬停延迟 120ms 用一次性 timer（非重复），timer 在项移出/关闭时取消。
- 不引入全局 hover 计时器，避免每帧开销。
- 级联深度 >3 仅在模型树存在时展开，不预创建控件。

## 5. 事件分发器安全（M26a 附属修复）

菜单 overlay 在 `POINTER_DOWN`（外部点击）和 `POINTER_MOVE`（光标离开 box）时会同步 `my_menu_dismiss` 自身；叶项 `POINTER_UP` 会同步 `menu_close_all`。这些 handler 在事件分发中途释放控件树，原先会导致 ASan heap-use-after-free。修复方案：

- `my_event_dispatch.c` 的 `deliver` 在调用 `on_event` 前临时 `my_widget_ref` 当前目标；
- 若 handler 返回 `MY_RET_OK`（吃掉事件）且可能已移除/销毁自身，直接 `return true` 停止冒泡，不再访问该 widget；
- 未吃事件的控件仍通过 `is_self_or_descendant` 确认仍在树中后再 emit 通用事件；
- 父指针在 unref 前捕获，避免释放后继续冒泡。

因此菜单叶项可恢复同步关闭，无需延迟 1ms timer；测试也无需在两次点击之间额外 pump timer。

## 6. TDD 与文档

- dummy port 注入事件：hover_enter 到子菜单项 → 断言子菜单 overlay 创建；hover 到普通项 → 断言同级子菜单关闭；ESC 在子菜单层 → 断言仅该层关闭且父菜单仍打开。
- 新增 `my_menu_test` 用例（或扩展既有 `my_menu_test`）。
- 四档 C 标准 + dummy/wl/min/noxml/trim 全绿；`build-asan/tests/my_menu_test` 无 UAF。
- 文档：更新 `docs/architecture.md` 事件流节说明自销毁 handler 安全；更新 `docs/roadmap.md` M19+ 候选勾选状态。
