# myui 架构

myui 分五层，自下而上：

1. **myc** — 基础库：allocator / darray / str(utf8) / emitter / value / object / 错误码 / 日志。只依赖 libc，被其余各层使用。
2. **mypal** — 平台抽象层（M2 已落地接口 + dummy/x11 两个 port，见下）。
3. **myr** — 渲染抽象（M1 已落地接口 + 软件实现，见下）。
4. **myui** — 控件核心（M3a 已落地控件树/布局/事件分发/窗口管理，见下；主题/动画/XML 在 M3b）。
5. **mymvvm** — MVVM（M4a 已落地 GUI 无关核心，见下；myui 适配在 M4b）。

## 接口隔离原则

层与层之间只允许通过头文件中的抽象接口通信，上层不得包含下层的实现细节头文件。可替换组件（allocator、渲染 backend、PAL port、view_model 等）一律采用 **vtable 惯例**：一个 `my_xxx_vtable_t` 结构体装函数指针，对象结构体首成员持有 `const vtable*`，调用方经由头文件里的 `static inline` 包装函数跳转。这样每层可在无显示、无 OS 的 CI 上用 dummy/mock 实现做单测，backend 也可在编译期或运行时替换。

## myr 渲染层（M1 现状）

接口（`src/myr/`，均已冻结）：

- `my_color.h`：`my_color_t`（RGBA8888）及打包/解析辅助（header-only）。
- `my_rect.h`：`my_rect_t`（int32，lcd 设备坐标）/ `my_rectf_t`（float，vgcanvas 用户坐标），相交/合并/判空（header-only）。
- `my_dirty_rects.h/.c`：脏矩形收集器（重叠或相邻即合并、级联合并、满 16 个塌缩为包围盒），供局部刷新。
- `my_lcd.h`：lcd vtable（宽高/格式/begin_frame/end_frame/draw_pixels/fill_rect/destroy）。`draw_pixels` 要求源像素已是 lcd 原生格式（原始 blit，不做转换）。
- `my_vgcanvas.h`：2D 矢量画布 vtable（frame/save/restore/translate/clip_rect/颜色与线宽/fill_rect/stroke_rect/fill_rounded_rect/路径 begin_path·move_to·line_to·close_path·fill·stroke/draw_text 占位）。所有控件绘制只面对它；后续 GLES/Metal/WebGL backend 实现同一 vtable。

实现：

- `my_lcd_mem.h/.c`：内存帧缓冲，像素格式特化 RGB565 / RGB888 / ARGB8888 / BGRA8888 / MONO(1bpp)，`my_lcd_mem_get_buffer()` 供测试与平台层取帧缓冲。
- `my_vgcanvas_soft.h/.c`：软件光栅化 backend，画在任意 my_lcd_t 上。路径填充用**扫描线 even-odd**（跨子路径，凹/自交多边形正确、嵌套轮廓打洞）；描边为 Bresenham + 方形线宽画刷（近似）；圆角矩形 = 三段矩形 + 四角扫描线圆；clip 为嵌套交集；帧内记录脏矩形（`my_vgcanvas_soft_get_dirty_rects()`）。

明确不做（M3+ 再评估）：抗锯齿、alpha 混合、rotate/scale 变换、join/cap、字体（`draw_text` 现返回 `MY_RET_NOT_SUPPORTED`）。接口设计不排除这些能力。

测试：每模块单测（含格式打包断言、裁剪、even-odd 洞、凹多边形、clip 交集、debug allocator 零泄漏）+ golden-image 测试（5 个场景覆盖全部像素格式，PPM 逐字节比对；基准在 `tests/golden/`，用 `my_golden_gen` 再生成）。

## mypal 平台抽象层（M2 现状）

接口（`src/mypal/`，已冻结）：一个 port 只需提供四样东西——

- **窗口** `my_pal_window_t`：set_title / resize / show / get_size / get_lcd / destroy。窗口给出一个 `my_lcd_t` 供 myr 绘制，`my_lcd_end_frame()` 即上屏。
- **主循环** `my_pal_main_loop_t`：run / quit / post_event（跨线程安全、自唤醒）/ add_timer / remove_timer / destroy。
- **时钟** `my_pal_time_now_ms()`（单调）。
- **入口** `my_pal_create()`：编译期按 `MYUI_PAL=x11|dummy|auto`（auto = 有 X11 用 x11，否则 dummy）选择 port；`my_timer.h` 的定时器管理器时钟可注入，测试确定。

事件模型：统一 `my_event_t`（QUIT/POINTER_*/KEY_*/RESIZE/PAINT/USER，具名 union payload，键码 = ASCII 直通 + 0x100 起命名键），应用经 `my_pal_set_event_handler()` 注册唯一 handler，窗口事件带窗口指针、post_event 投递的事件窗口为 NULL。

ports：

- **dummy**（`src/mypal/dummy/`）：无显示。窗口 = my_lcd_mem(BGRA8888)；主循环 = 手动队列（`my_pal_main_loop_pump_n` 测试钩子）+ 可注入假时钟（`my_pal_dummy_set_now_ms`）；全部单测基于它。
- **x11**（`src/mypal/x11/`）：窗口 = XCreateSimpleWindow + XImage 套在 my_lcd_mem 上，end_frame 全帧 XPutImage 上屏（dirty-rect 部分上屏是 TODO）；主循环 = select(X fd + 自唤醒 pipe)，post_event 跨线程投递；XEvent → my_event_t 翻译，keysym→my_key_t 独立映射表（单测覆盖）。运行时冒烟测试仅在 `DISPLAY` 存在时真正执行，否则跳过计通过。

不做（TODO）：IME、HiDPI、剪贴板、多窗口焦点管理、sdl2/win32 port（roadmap M2 后续/M5）。

## myui 控件核心（M3a 现状）

- **控件模型** `my_widget.h`：widget 继承 `my_object_t`（引用计数）；子类嵌入为首成员，析构链 `子类destroy → my_widget_destroy → my_object_destroy`。属性：name/rect（父坐标系）/visible/enable/focusable/dirty；父弱引用、子列表持 ref。vtable 三项全可选：`on_paint` / `on_event`（返回 MY_RET_OK 吃掉事件）/ `on_layout`；销毁走对象析构链不再单列。绘制入口 `my_widget_paint`：save → translate 到自身原点 → clip 到自身 rect → on_paint → 递归子控件 → restore。
- **脏矩形**：`my_widget_invalidate` 标脏并换算成全局矩形冒泡到根控件的 `dirty_sink`（窗口的 dirty_rects）；`my_window_paint` 只重绘脏区（逐脏矩形 clip + 重绘子树），干净窗口零开销。
- **布局** `my_layout.h`：`my_layouter_t` 单函数接口；内置 default（绝对定位，不动）与 linear（横/纵 + spacing）。子控件用 `my_widget_set_layout_params("w:50% h:1f")` 声明尺寸：`N` 像素 / `N%` 父容器百分比 / `Nf` flex 权重（缺省轴 = AUTO 保持现状）；交叉轴 flex = 填满。网格布局 TODO。
- **事件流** `my_event_dispatch.h`：PAL 事件 → 窗口 → dispatcher。POINTER_DOWN 自顶向下 hit_test（后加的子控件在上层），命中控件先收（vtable on_event，再 emitter 监听），未吃掉则冒泡父链；DOWN 命中者被 grab，后续 MOVE/UP 直达它（拖动语义），UP 释放；焦点 = 命中链上最近的 focusable 控件，KEY 事件发给它。emitter 事件名：`pointer_down/move/up`、`key_down/up`，应用可用 `my_widget_on` 免继承监听。grab/focus 为弱引用，控件中途移除的清理是 TODO。
- **窗口管理** `my_window.h/.c` + `my_window_manager.h/.c`：`my_window_t` 是顶层 widget（根），持有 PAL 窗口与懒创建的 soft vgcanvas（测试可注入 recording vgcanvas）；RESIZE 更新根尺寸并整体标脏，PAINT/指针/键盘路由进 dispatcher，分发后立即重绘脏区。`my_window_manager_t` 注册为 PAL 唯一事件 handler，按 PAL 窗口路由；窗口栈 open/close/back_to_home，QUIT 关窗、栈空退主循环（`quit_requested` 可观测）。`my_app_run(pal, factory, ctx)` 一行启动。
- **内置控件** `src/myui/widgets/`：`my_button`（四态颜色 + 圆角矩形 + 边框，click 事件经 emitter；文本为占位 draw_text）、`my_label`（背景 + 文本占位条）。字体系统落地前 draw_text 返回 NOT_SUPPORTED。

## myui 主题与动画（M3b）

- **样式** `my_style.h`：`my_style_t` = 四状态槽（normal/hover/pressed/disabled）× 键值属性（`my_value_t`；颜色存 `0xRRGGBBAA` UINT32），查询时状态回退 normal。
- **主题** `my_theme.h`：(控件类型名, 可选 name) → style 的样式表；解析优先级 **控件 local_style > theme name 匹配 > theme 类型匹配 > 调用方默认**。查询沿父链找到最近带 theme 的祖先（`my_widget_apply_theme` 挂在任意子树）。文本加载 `my_theme_load_str`：`button.normal.bg_color=#FF4081`、`button[ok].pressed.bg_color=#C60055`、`label.font_size=16`（无状态段 = 四状态全设）。内置浅色默认主题 `my_theme_default_create()`；窗口创建即带默认主题，`my_window_set_theme` 可切换（demo_widgets 演示浅/深两套）。
- **动画** `my_animator.h`：管理器由 window_manager 创建（绑定 pal 时钟 + 主循环）；仅在有活动动画时挂 16ms 周期 timer，空闲即摘除。属性 "x"/"y"/"w"/"h"/"xy" 插值，duration/delay/easing（linear/ease_in/ease_out/ease_in_out 或自定义函数指针）/repeat_count（额外次数，-1 无限）/yoyo，on_update/on_done 回调；`my_animator_move_to` 从根控件找到管理器直接使用。每帧对旧+新位置 invalidate（经 dirty_sink 合并）。
- **生命周期安全**（M3a 遗留修复）：根控件的 `removed_hook`——子树移出树时窗口清掉 dispatcher 的 grab/focus（`my_event_dispatcher_forget`，含后代判断）并取消其动画（`my_animator_stop_widget`）；窗口销毁时同样取消其全部动画。

## mymvvm MVVM 层（M4a 现状）

GUI 无关核心（`src/mymvvm/`，只链接 myc）：

- `my_view_model.h/.c`：属性 get/set + 命令 can_exec/exec 的 vtable 契约；变更经内嵌 emitter 通知（`prop:<name>` / `props`）；附 `my_view_model_dummy` 属性包实现（测试/demo 用）。
- `my_value_converter.h/.c` + `my_value_validator.h/.c`：委托模式；内置 upper/lower/int_to_str/bool_negate 与 not_empty/range(min,max)。
- `my_binding_rule.h/.c`：规则字符串 parser（数据/命令，items/条件识别后 NOT_SUPPORTED）。
- `my_binding_context.h/.c` + `my_data_binding.c` + `my_command_binding.c`：按规则把 vm 与 **my_binding_target_t**（UI 端点 vtable，M4b 用 widget 实现）连起来；OneWay/TwoWay/Once 三模式，converter 正反向、validator 失败拒绝回写并恢复 target，同步分发不排队。
- 细节与语法见 docs/mvvm.md。

### M4b 补充

- base 增加 `my_view_model_array`（可观察子 vm 列表）、`my_items_binding`（数组→target rebuild_items）、`my_condition_binding`（`Condition=[!]prop` → bool 推送）、`my_navigator`（导航请求 + 默认 handle 注册表）；命令绑定支持 `ToPage=`（导航）与 Args 中 `{prop}` 运行时替换。
- 适配层独立小库 `mymvvm_myui`（src/mymvvm_myui/，链接 myui+mymvvm，避免 base 反向依赖 GUI）：`my_widget_target`（widget ↔ binding_target 属性/事件/items 映射）、`my_mvvm_bind`（一键绑定整棵控件树）、模板注册表、`my_navigator_wm`（窗口栈导航）。
- 重绘驱动修正（M4b）：window_manager 挂 16ms 重绘 tick，统一绘制脏窗口——动画、定时器、模型变更等"非事件分发路径"的标脏也能上屏。

## 渲染 backend 与 PAL port 矩阵（M5 现状）

渲染 backend（同一 vgcanvas vtable）：

| backend | 状态 | 验证 |
|---------|------|------|
| software (my_vgcanvas_soft) | 完成 | 单测 + golden-image + 全 demo |
| GLES2 (my_vgcanvas_gles2) | 完成 | mock GL 单测（三角化/scissor/状态）+ EGL surfaceless 真实冒烟（glReadPixels 断言） |

GLES2 backend 说明：调用方持有 GL 上下文（PAL 窗口创建），backend 只做三角化与提交；路径填充沿用 even-odd 扫描线（span 合并为三角形批，避免扇形三角化对凹多边形的错误）；stroke 为法线扩展线段四边形。`my_gl_t` 函数表隔离全部 GL 调用，可注入 mock/WebGL 变体。

PAL port 矩阵：

| port | 状态 | 验证 |
|------|------|------|
| dummy | 完成 | 全部单测的基础 |
| x11 | 完成 | 单测 + 真机冒烟（DISPLAY 存在时） |
| linux_fb | 完成（未实机） | my_osal_t 注入假设备单测（fb 像素/evdev 事件/泄漏）；真实 /dev/fb0 路径编译通过，未实机验证 |
| sdl2 / win32 / cocoa / android / ios / harmonyos / emscripten | 未开始 | 见 docs/porting.md 路线图 + cmake/ 工具链文件（未验证） |
- **wayland**（`src/mypal/wayland/`）：xdg-shell(wm_base/toplevel) + wl_shm（memfd+mmap 单缓冲，release 后再 present，frame 回调提供 vsync 节奏）；wl_pointer/wl_keyboard（xkbcommon 键表，独立单测）；协议代码由 wayland-scanner 在构建时生成；真实合成器冒烟通过。
