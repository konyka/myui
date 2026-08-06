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

## 字体子系统（M7a）

- `my_font.h`：字体抽象 vtable（measure/get_glyph(8bpp alpha 位图+bearing+advance)/ascent/descent/line_height/destroy）；UTF-8 解码 `my_utf8_next`。
- 两实现：`my_font_bitmap`（内置 8x8 等宽位图字体，ASCII 32..126，零依赖兜底，数据由 `tools/gen_bitmap_font.c` 从 Liberation Sans(OFL) 生成并提交，可用该脚本重新生成）；`my_font_stb`（stb_truetype 后端，编译选项 `MYUI_FONT_STB` 默认 ON，OFF 时嵌入式裁剪；LRU 字形缓存默认 256 项，容量可配，命中/淘汰有诊断计数器）。
- 渲染落地：vgcanvas vtable 增加 `set_font(font, size)`（font 可 NULL 仅改字号）与 `measure_text`；soft backend 的 draw_text 把字形 alpha 经 `my_lcd_blend_span`（lcd vtable 新增的 src-over span 混合，各格式特化）写入；gles2 backend 把字形上传为 alpha 纹理（64 项直接映射缓存），第二套纹理着色器 + quad 绘制（纹理图集批提交为 TODO）。
- 控件：button/label 用 measure 居中真实文本，主题键 `font_size`/`fg_color` 生效；无字体时回退旧占位条（兼容老测试）。窗口 `my_window_set_font` 设默认字体。
- 第三方说明：`3rd/stb/stb_truetype.h`（public domain），其实现宏单独成 TU 并仅对该文件放宽警告，项目自身标准不变。

## edit 文本输入控件（M7b）

`src/myui/widgets/my_edit.h/.c`：单行输入，focusable。状态机简表：

| 输入 | 行为 |
|------|------|
| 可打印 ASCII (32..126) | 有选区先删选区，插入字节，emit "changed"；max_len（按 codepoint）截断 |
| Left/Right | 按 codepoint 步进（UTF-8 不碎）；Shift = 扩选 |
| Home/End | 行首/行尾 |
| Backspace/Delete | 有选区删选区，否则删前/后一个 codepoint |
| Ctrl+A | 全选（anchor=0, cursor=end） |
| Enter | emit "activate" |
| POINTER_DOWN | 点击定位光标（font measure 逐字前缀宽，无 font 退化为等宽估算） |

- 光标/选区按字节偏移存于 codepoint 边界；password 模式缓存 `*` 掩码串；readonly 拒绝编辑；hint 空文本未聚焦时灰色显示。
- 焦点：分发器在 POINTER_DOWN 时切换焦点并发 "focus"/"blur" emitter 事件（点空白处 blur）；非焦点控件收不到 KEY 路径（edit 自身也检查 focused）。
- 绘制：bg/border（**focused 复用样式 HOVER 槽**，文档约定）、选区高亮、光标竖线（常亮，闪烁 TODO）、文本超宽时 scroll_x 跟随光标。
- MVVM：widget_target 映射 edit 的 text/hint；"changed" 事件驱动 TwoWay 回写，validator 拒绝时自动恢复。

## 渲染质量（M7c：alpha 混合 + 抗锯齿）

- **混合**：`my_color_t.a` 全链生效。`lcd_mem` 的 `fill_rect` 在 a<255 时走 src-over（`out = (src*a + dst*(255-a))/255`，整除截断；RGB565 展开-混合-重打包，MONO 阈值化），a=255 保持原快速替换路径。文本 span 混合（M7a 的 `blend_span`）同一公式。混合公式与期望值在 my_lcd_mem_test 中以像素级断言固化（如 50% 红盖白 = (255,127,127)）。
- **抗锯齿**：路径填充与圆角的扫描线做 **x 方向 4 子采样覆盖率**（子采样中心 (2k+1)/8），边缘像素以 `color.a * cov/4` src-over；**y 方向不采样**（成本控制，视觉已明显改善，权衡已注明）。轴对齐直边覆盖率恒满（零回归）。`my_vgcanvas_soft_set_antialias(vg, on/off)` 运行时开关，默认开；golden 基准场景固定 AA off，AA 场景单独建基准。
- **GLES2**：着色器混合 src-over（uniform 色含 a），EGL 冒烟断言半透明读回；GLES AA（MSAA/顶点覆盖）TODO。
- 性能（-O0 Debug）：半透明矩形 2.03ms/帧（与不透明 2.19ms 相当）；路径填充 AA on 1.00ms vs off 0.68ms（约 +46%，仅限路径场景，矩形填充零开销）。

## 控件清单（M7d 完成态）

| 控件 | 关键属性/说明 |
|------|---------------|
| window | 顶层根 widget，bg_color 主题 |
| button | text、四态颜色、click 事件 |
| label | text、bg/fg |
| edit | text/hint/readonly/max_len/password、光标与选区、changed/activate 事件 |
| checkbox | text、checked(+mixed 显示态)、value(bool) 可绑、changed 事件 |
| slider | value/min/max/step、拖动与轨道点击（grab）、value(float) 可绑、changed 事件 |
| progress_bar | value [0,100]（展示型，OneWay 友好） |

均可经 widget_target 绑定（text/visible/enable/x,y,w,h/value/hint）。

## XML UI 加载器（M8a）

- `my_xml.h/.c`：自研最小 XML parser（零依赖）：元素/属性（单双引号）/文本/注释/CDATA/五预定义转义/自闭合；单根；不做 DTD/命名空间/实体全集（未知实体报错）。小 DOM（`my_xml_node`：name/attrs/children/text/line），错误带行列号。
- `my_ui_loader.h/.c`（编译选项 `MYUI_UI_XML` 默认 ON）：标签→控件工厂注册表（`my_ui_loader_register`；内置 window/button/label/edit/checkbox/slider/progress_bar）；通用属性 name/x/y/w/h/visible/enable/lp/layout 由 loader 统一应用，控件特有属性（text/hint/password/min/max/step/value/checked...）由工厂自取；`v:*` 属性按 `name=value;` 拼进 `bind_rules`（my_mvvm_bind 直接消费）；`<style>` 文本段喂给窗口 theme；`my_ui_load_str/my_ui_load_file`，未知标签报错带行号。demo_mvvm 主页已 XML 驱动。

## list_view 虚拟化与 image 控件（M8b）

- `my_list_view`：固定行高虚拟化。可视行数 + 1 行缓冲，滚出可视区的行进回收池（`pool` 持引用），新进可视区的行从池取出经 `bind_row` 重绑数据；滚动钳制、POINTER_WHEEL（新事件，x11 Button4/5、wayland axis、linux_fb REL_WHEEL 已接）与拖动滚动；右侧指示条。数据源抽象 `my_list_adapter_t`（get_count/create_row/bind_row）；items 绑定落在 list_view 上时 widget_target 自动装 adapter 走虚拟化（普通容器保持全量重建）；items_changed 全表刷新（增量 diff TODO）。10000 行实测只创建 ~22 个行控件。
- `my_image` + `my_image_loader`：stb_image 后端（vendored 3rd/stb，`MYUI_IMAGE_STB` 可裁剪，OFF 显示占位框）；解码统一 RGBA8888，按路径 LRU 缓存（默认 8 项）；缩放 none/center/fit/fill（最近邻，双线性 TODO）；透明像素按控件主题 bg_color 预合成后走 lcd draw_pixels 快速路径；vgcanvas vtable 新增 `draw_image`（soft 实现格式特化 + 最近邻缩放 + 裁剪，GLES 端 NOT_SUPPORTED 留 TODO）。

## AA 级别与剪贴板（M8c）

- **AA 三级**（`my_vgcanvas_soft_set_antialias_level`）：0=关（像素中心硬边）、1=x 向 4 子采样、2=x4×y2（每条扫描线在 +0.25/+0.75 两次求值合并覆盖率）。默认 **2**——bench（-O0，8 大三角形/帧）：level0 0.72ms、level1 1.70ms、level2 2.41ms，2 级相对 1 级 +42%（< 2.5x 阈值）。`set_antialias(bool)` 兼容映射 false→0、true→2。覆盖率缓冲按行收紧到多边形包围盒（初版全幅扫描慢 10 倍，已修）。stroke 折线改为**法线扩展四边形条带**，与 fill 共用覆盖率路径（混合+AA 免费获得；奇数线宽偏移 0.5px 保证细线落在像素中心；方 cap/join，半透明描边关节处可能过混合，TODO）。
- **剪贴板**：pal vtable 增加 `clipboard_set_text/get_text`（UTF-8）。dummy/linux_fb/wayland 为内存往返；x11 拥有 CLIPBOARD selection 并缓存、响应 SelectionRequest（UTF8_STRING/STRING/TARGETS）——**从外部应用获取是 TODO**（get 目前只返回自有缓存；接口语义已写清）。edit 接入 Ctrl+C/X/V（粘贴剔除换行、尊重 max_len、UTF-8 保留）。
- **edit 光标闪烁**：500ms 主循环 timer（聚焦启动/失焦停止/销毁摘除），toggle 可见性并 invalidate。

## text_area 多行编辑（M9a）

- 数据模型：单一 UTF-8 字节缓冲 + 行起点偏移缓存（darray 存 size_t 值）；编辑只从**被编辑行**起局部重建偏移（二进制查找行、O（编辑点后字节数） 摊销）；10k 行实测：载入 0.13ms、2000 次光标移动 0.09ms、100 次插入 0.09ms。
- 状态机：光标 (row,col) 按 codepoint；上下移动保持目标列（goal_col，水平移动/点击/编辑时重置——实现上 ta_move_to 不更新 goal，调用点按需设置）；Home/End/Ctrl+Home/End；Enter 拆行、行首 Backspace 合行；Shift 扩选、Ctrl+A、选区删除先行；Ctrl+C/X/V 经 PAL 剪贴板（**保留换行**，与单行 edit 相反）。
- 视图：scroll_x/scroll_y 保光标可见（行高 = 字体 line_height）；绘制仅可视行区间（逐行 draw_text + 按行分段选区高亮 + 闪烁光标复用 edit 的 timer 机制）；hint 空文档显示；行号 = TODO（撤销重做见 M10a、word wrap 见 M10b）。
- MVVM：widget_target 映射 text_area 的 text（TwoWay）/hint；XML 标签 `<text_area>`。

## draw_image 后端矩阵与缩放滤波（M9b）

- **soft**：最近邻（默认关闭）或双线性（默认开）采样，alpha 按控件主题 bg 预合成后走 draw_pixels。双线性用像素中心映射 `(dst+0.5)*w/dw-0.5`、四邻域加权（float，未走定点——-O0 下 480x270→800x600 为 15.5ms/帧 vs 最近邻 2.2ms（7x）；**嵌入式建议 `MY_SCALE_FILTER_NEAREST`**，桌面默认 BILINEAR；缩小时的盒式预降采样见 M10c 一节）。`my_vgcanvas_soft_set_scale_filter` + `my_image_set_scale_filter` 透传。
- **gles2**：draw_image 已实现——RGBA8888 上传纹理（`create_texture_rgba`），专用 FS（直接采样不过调制色），quad + scissor；纹理按 (ptr,w,h) 键 LRU 16 项（调用方须保证位图生命周期，my_image 的解码 LRU 语义自洽，头注释注明）；EGL 冒烟四象限读回断言通过。bg 参数先画背景矩形再混合 quad。

## scroll_bar 与变高列表（M9c）

- `my_scroll_bar`：value [0,1] + page_size [0,1]（滑块长，min 16px）；滑块拖拽（grab）、轨道点击翻页；"changed" 事件。容器显式挂接：`my_list_view_set_scroll_bar(lv, bar)` / `my_text_area_set_scroll_bar(ta, bar)`——双向同步（容器滚动 → value/page_size 更新；bar 拖拽 → 容器 offset 更新，无事件回环）。
- list_view 变高行：adapter vtable 增加 `row_height(index)`（NULL = 固定行高，M8b 行为零回归）。变高模式用**前缀和缓存**（darray 存累计高度，滚动到哪儿惰性算到哪儿）；总高在未测完全部行数前用"已测部分 + 未测部分×已见平均高"估算（滚动条位置轻微非线性，Android ListView 式常规取舍，头注释说明）；可视区间二分查找 + 前向累计，回收复用逻辑不变。
- stroke 圆 cap/join：`my_vgcanvas_set_line_cap/join`（BUTT/ROUND、MITER/ROUND，入 save/restore 状态）。soft 实现为 lw/2 圆盘点（cap 取端点、join 取内部顶点），走覆盖率路径自动 AA；关节处相邻段与圆盘重叠区域对半透明描边有轻微过混合（接受并注释，合并单轮廓是 TODO）；GLES 端存状态不生效（TODO）。

## ui2c 与剪贴板协议收尾（M9d）

- `tools/ui2c`：XML→C 生成器（宿主工具，输出 `my_widget_t* fn(allocator, pal)` 构建函数；golden 等价测试保证与运行时加载逐节点一致）；嵌入式走 `MYUI_UI_XML=OFF` + 离线生成 = 零解析开销零 parser 代码（见 porting.md）。
- x11 剪贴板外部获取：`clipboard_get_text` 在 owner 为外部时 `XConvertSelection`（UTF8_STRING→STRING 回退）+ ~500ms 同步等待 SelectionNotify，等待期间其他事件照常分发（重入安全）；INCR 增量传输未做（TODO）。

## 撤销/重做与键盘导航（M10a）

- `my_undo_stack_t`：文本补丁模型（entry = {offset, deleted, inserted}；undo 删 inserted 回插 deleted，redo 反之）。**批合并**：连续插入且 offset 连续（打字流）合并；连续删除且 offset 相邻递减（退格流）前插合并；换向/非相邻/其他操作开新条目；`break_batch` 在 blur/光标跳转时调用。容量默认 100（溢出丢最老），新编辑清 redo 支。栈不持有文档：undo/redo 返回 {offset, remove_len, bytes} 由控件套用（套用期间 `applying_history` 抑制再记录）。
- edit/text_area：用户键盘/粘贴/剪切入栈；**程序 set_text 与 MVVM 回写不入栈**（且 set_text 视为文档替换直接清栈——边界语义：撤销只覆盖"用户亲手编辑"）。Ctrl+Z undo、Ctrl+Y 与 Ctrl+Shift+Z redo。
- 键盘导航：Tab/Shift+Tab 焦点环（分发器在未被吃掉时按树序前/后移焦点并回绕，跳过不可见/禁用）；text_area/list_view 的 PageUp/PageDown 按可视区翻页；scroll_bar 获焦后 Up/Down 微调、PgUp/PgDn 翻页、Home/End 到端。

## text_area word wrap 视觉行模型（M10b）

- **视觉行缓存**：`my_visual_line_t {phys, start_cp, len_cp}`（darray 按值存储），仅 wrap on 时维护。编辑只从**被编辑物理行**起局部重建视觉行（之前的行起点不受本行内编辑影响）；resize/改宽度置 `vlines_dirty` 全量重建。wrap off 时 `ta_vline_at` 返回静态临时物理行视图，绘制/光标路径零分支差异、零回归。
- **折行规则（自定，非 UAX#14）**：逐 codepoint 累宽（font measure，位图字体回退 cell 宽），超宽时——若溢出字符本身是空格则在其**之前**断开并消费该空格；否则若当前视觉行内有空格则在**最后一个空格之后**断开（空格留在上一视觉行尾）；否则任意两 codepoint 间硬折。任何视觉行都不以空白开头。明确不做 UAX#14 断行规则（TODO）。
- **光标/滚动语义**：wrap on 时 Up/Down 按视觉行移动（goal_col 按视觉行内列保持）；Home=视觉行首、End=视觉行尾；Left/Right 仍是物理逐 codepoint（跨视觉行无特殊处理）；`(row,col)`→视觉行用二分查找（vlines 按 (phys,start_cp) 有序，共享边界列归**后一个**视觉行）。wrap on 时水平滚动禁用（scroll_x 恒 0），垂直滚动以视觉行计高。撤销/重做只记录文本补丁，视觉位置由缓存自动重建。
- 接入：`my_text_area_set_wrap` / XML `<text_area wrap="true">` / MVVM `wrap` bool 属性（text_area 专用）。bench（-O0，万行 ~80cp 长行 → 25843 视觉行）：载入+构建 1.57ms、1000 次视觉移动 0.10ms、滚动重绘 0.30ms/帧。

## 盒式预降采样与 GL 真窗口（M10c）

- **盒式预降采样**（soft draw_image，双线性模式自动生效）：缩放比 < 0.5 时先按 2/4/8 整数档（取使剩余比例 ≤ 1 的最大档，每轴独立）做盒式平均到临时位图，再走双线性到目标。盒式平均按 straight-alpha 各通道独立求均值（半透明边缘与预乘滤波有轻微偏差，已注释）；边缘不足一档的块只平均有效像素。bench（2000x1500→400x300，-O0）：nearest 0.71ms / 纯双线性 5.09ms / 盒式+双线性 28.93ms——**这是质量特性不是速度特性**：直接双线性只采目标×4 像素 O(dst)，盒式必须读完整源图 O(src)；换来的是高频内容零混叠（64x64 棋盘缩 8x8 后全部 ≈127 中灰，nearest 仍有硬 0/255）。嵌入式继续建议 NEAREST。
- **GL 窗口挂载**：窗口 vtable 新增 `gl_enable` → `my_pal_gl_t*`（`make_current`/`swap_buffers`/`get_size`/`destroy`；句柄归窗口所有，提前释放双重安全——见 porting.md）。x11：`eglGetDisplay` + window surface；wayland：`wl_egl_window_create`（resize 同步 `wl_egl_window_resize`）+ `eglGetPlatformDisplay(WAYLAND)`；两 port 均 pal 级懒初始化共享 EGLDisplay、swap interval 1（wayland 由 mesa 经 frame 回调节流，vsync 语义保持）。dummy/linux_fb 打桩 NULL。CMake 探测 `egl`（+`wayland-egl`）定义 `MYUI_PAL_GL_EGL`，缺库自动回落。
- **集成点**：`my_window_enable_gl(win)` 一行切换——建 gles2 vgcanvas（失败回落 soft）、paint 帧末 swap、RESIZE 更新 GL viewport。demo：`MYUI_DEMO_GLES=1`（demo_hello/demo_widgets）。冒烟 `gl_window_smoke_test`（有显示环境时实跑：渲染→swap 前 glReadPixels 像素断言→300ms 定时退出；无环境 skip），x11/wayland 均实跑通过。
