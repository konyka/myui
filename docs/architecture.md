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
- 两实现：`my_font_bitmap`（内置 8x8 等宽位图字体，ASCII 32..126，零依赖兜底，数据由 `tools/gen_bitmap_font.c` 从 Liberation Sans(OFL) 生成并提交，可用该脚本重新生成）；`my_font_stb`（stb_truetype 后端，编译选项 `MYUI_FONT_STB` 默认 ON，OFF 时嵌入式裁剪；LRU 字形缓存默认 256 项，容量可配，命中/淘汰有诊断计数器；M14b 起支持 .ttc 首 face——"ttcf" 头检测 + `stbtt_GetFontOffsetForIndex`，注意 CFF2 可变字体如 NotoSansCJK-VF 仍不可解析）；**`my_font_ft`（M16，FreeType 后端，选项 `MYUI_FONT_FREETYPE` 默认 ON、无 freetype2 自动 OFF）：hinted 渲染（FT_LOAD_DEFAULT + RENDER_MODE_NORMAL），小字号明显比无 hinting 的 stb 锐利（测试以"中间覆盖率像素占比"断言 ft < stb×0.95）；LRU 缓存同构仿写**。
- **fallback 链（M14b 起，M16 后端无关化）**：`my_font_create_chain`——face 按路径数组加载（有 FT 优先 FT、否则 stb），每个 codepoint 经新 vtable 槽 `has_glyph`（追加槽，NULL=假定有）路由到第一个含该字形的 face；dxx 即 [DroidSansFallback, LiberationSans]。
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

均可经 widget_target 绑定（text/visible/enable/x,y,w,h/value/hint）。后续里程碑新增：list_view/scroll_bar/image（M8b/M9c）、text_area（M9a）、dialog/menu/tooltip 复合控件（M13c，见对应章节）。

## XML UI 加载器（M8a）

- `my_xml.h/.c`：自研最小 XML parser（零依赖）：元素/属性（单双引号）/文本/注释/CDATA/五预定义转义/自闭合；单根；不做 DTD/命名空间/实体全集（未知实体报错）。小 DOM（`my_xml_node`：name/attrs/children/text/line），错误带行列号。
- `my_ui_loader.h/.c`（编译选项 `MYUI_UI_XML` 默认 ON）：标签→控件工厂注册表（`my_ui_loader_register`；内置 window/button/label/edit/checkbox/slider/progress_bar）；通用属性 name/x/y/w/h/visible/enable/tooltip/lp/layout 由 loader 统一应用，控件特有属性（text/hint/password/min/max/step/value/checked...）由工厂自取；`v:*` 属性按 `name=value;` 拼进 `bind_rules`（my_mvvm_bind 直接消费）；`<style>` 文本段喂给窗口 theme；`my_ui_load_str/my_ui_load_file`，未知标签报错带行号。demo_mvvm 主页已 XML 驱动。

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
- stroke 圆 cap/join：`my_vgcanvas_set_line_cap/join`（BUTT/ROUND、MITER/ROUND，入 save/restore 状态）。soft 实现为 lw/2 圆盘点（cap 取端点、join 取内部顶点），走覆盖率路径自动 AA；关节处相邻段与圆盘重叠区域对半透明描边有轻微过混合（接受并注释，合并单轮廓是 TODO）；GLES 端 M9c 只存状态，M10d 已补齐（见下）。

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

## GLES round cap/join 与行高失效（M10d）

- **GLES 描边 cap/join 补齐**（与 soft 几何对齐）：stroke 折线本就是法线扩展四边形（与 soft 同几何），本期补——ROUND cap = 端点半圆三角扇（8 段，沿端点切线外凸，退化线段回落整圆盘）；ROUND join = 顶点半径 lw/2 圆盘三角扇（8 段，覆盖规则与 soft 一致：`i+1 < count`——开放折线末端也有盘，闭合轮廓顶点 0 无盘）。cap/join 入 gles state（save/restore 有效）。GLES 无 AA，边缘为硬边（几何扇近似圆）。至此两后端 cap/join 能力对齐：BUTT/MITER/ROUND 全支持，差异仅剩 soft 的覆盖率 AA。
- **变高行动态行高失效**：`my_list_view_invalidate_row_heights(list)`（丢弃全部已测行高，psum 清回 [0]）与 `my_list_view_invalidate_row_height(list, index)`（psum 截断到 index 之后，前缀已测值保留，尾部惰性重算）。两者立即：钳制 scroll_offset 到新（估算）总高、重建可视行、同步滚动条。典型场景：行内容更新导致行高变化后调用 index 版；全局字号/密度切换调全量版。

## 文本管线：BiDi 重排 + 阿拉伯整形（M11a）

- **`my_text_layout`**（src/myr/my_text_layout.[ch]）：逻辑序 UTF-8 → 视觉序 codepoint 数组 + 视觉→逻辑索引映射 + 重编码 visual_utf8（给 font vtable measure 用——测量与顺序无关但与整形有关）。流程：UTF-8 解码 → **纯 LTR 快速路径**（扫描无希伯来/阿拉伯/presentation forms/bidi 控制字符 → 恒等映射，SheenBidi 零触碰）→ **阿拉伯整形**（见下）→ **SheenBidi UBA**（段落方向按 P2/P3 首强字符；visual runs 已按视觉序返回，奇数 level 段内反转）。
- **阿拉伯整形自研**（src/myr/my_arabic_shape.[ch]）：SheenBidi 3.0.0 只有 UBA 没有 shaping，整形按 UCD 自建——连接类别表 + presentation forms 映射表（`my_arabic_shape_data.h`，tools/gen_arabic_shape_data.py 从本地 ArabicShaping.txt + UnicodeData.txt 生成，离线）。规则：逐字母看逻辑邻居（跳过透明记号），双连接/右连接/左连接/tatweel 决定词首/中/尾/独立形，1:1 原位替换（先拷贝原文再判型，因为 presentation forms 本身无连接类）。Lam-Alef 合字不做（TODO，字体通常有 GSUB 兜底）。
- **绘制接入**：soft/gles2 的 draw_text 与 measure_text 入口先 `my_text_layout_may_need_bidi` 预扫描，纯 LTR 走原逐字形循环（零开销），否则取布局结果按视觉序绘制/按 visual_utf8 测宽。**语义：x 永远是左缘**——RTL 段落的视觉序自然从右读起，对齐交给控件。编辑控件（edit/text_area）的 RTL 光标视觉-逻辑映射本期不做（TODO，draw_text 级与展示控件完整受益）。镜像（UBA L4）未做（TODO）。
- **缓存**：进程级 LRU 64 项，key = 文本（布局结果与字体/字号无关，故不纳入 key）；`my_text_layout_process` 返回**调用方所有的拷贝**（缓存持主副本），`my_text_layout_cache_flush/size` 为测试钩子。
- **裁剪**：`MYUI_BIDI=OFF`（默认 ON）不编 SheenBidi（省 ~100KB），process 退化为恒等布局，draw/measure 路径不变。
- 集成偏离记录：任务书假定"SheenBidi 的 shaping"，实际 3.0.0 无此模块（只有重排/镜像查找表），整形为 myui 自研（数据同来自本地 UCD）；SheenBidi 许可证为 Apache-2.0（非任务书所述 ISC，同为宽松许可，LICENSE 已 vendor）。

## RTL 编辑模型：视觉-逻辑映射（M12a）

- **边界语义定案（单光标）**：光标永远在两个逻辑字符之间（逻辑边界 0..len）。其视觉位置由一条规则确定——**前一个逻辑字符的逻辑尾边**（RTL 字符向左"收尾"）：LTR 字符尾边=右缘，RTL 字符尾边=左缘。run 交界处两个逻辑边界可共享同一视觉点（经典双光标场景，我们保持单光标：点击/方向键只落"规范"视觉边界——往返映射能回到自身的边界；别名边界仍可由打字/退格/Home/End 到达）。已知怪癖（所有单光标模型共有）：run 边界处新敲的字符可能出现在光标以外的位置。
- **text_layout 新 API**：`visual_x(logical_boundary)`（光标 x）、`logical_at_x`（点击，左右半字宽取最近规范边界）、`boundary_left/right`（方向键**按视觉方向**，内部只走规范边界，LTR 下退化为恒等）、`boundary_home/end`（视觉行首/尾）、`visual_rects(l0,l1)`（选区分段矩形，跨 run 时多段）、`visual_of_logical/logical_at_visual`（goal 列换算）。宽度经 font 字形 advance；NULL font = 8px 格子（控件无字体兜底约定）。布局新增 per-cp run 方向与 logical→visual 逆映射（缓存随文本自然失效）。
- **edit 接入**：光标 x/点击定位/选区高亮（跨 run 分段）/方向键/Home/End 全部经映射——**仅当文本 may_need_bidi 时**；纯 LTR 走原等宽/measure 路径（逐位零回归，现有测试未动）。text_area 接入按视觉行段同理：wrap 折行本身保持逻辑序（**wrap+RTL 混排的视觉行级重排留 TODO**）；上下移动 goal_col 语义统一为**视觉边界索引**（LTR 下恒等于 codepoint 列，零回归）；跨行左右移动回落逻辑行首/尾（跨行视觉连续性同 TODO）。
- BIDI=OFF 下 RTL 分支整体裁掉（identity 布局 + 测试按宏跳过）；M12a 还修复了 OFF 模式 `may` 未归零导致映射数组未初始化的段错误（tl_master_compute 强制 identity）。

## Lam-Alef 合字、UBA L4 镜像与 wayland 剪贴板（M12b）

- **Lam-Alef 合字**（my_arabic_shape 两遍制）：pass 1 合并——Lam(U+0644) 后紧跟 Alef 系（0622/0623/0625/0627）→ 对应合字（FEF5..FEFC），**按 Lam 在原文的连接上下文选形**（前字符可前连 → final 形，否则 isolated 形；数组变短，返回值=新长度）；pass 2 原判型逻辑跑在合并后序列上（合字按右连接参与邻居判型：`my_arabic_join_class` 对 FEF5..FEFC 特判 R）。数据由 tools/gen_arabic_shape_data.py 扩展生成（UnicodeData 双码位分解，限 4 个强制 Alef 变体——"LAM WITH JEEM/HAH" 等非强制连字不收）。整形调用方（text_layout）用新长度喂 UBA（合字在重排中算一个字符）。Naskh 渲染目检：سلام 词中 لا 与独立 لا 均为合字字形。
- **UBA L4 镜像**（my_text_layout）：重排后对 RTL run 内码点查表替换镜像码点；表由 tools/gen_bidi_mirror_data.py 从本地 BidiMirroring.txt 生成（全表 428 对——任务书说"约 20 对手表"，本地有完整 UCD 故全量生成）。"（نص)" 实测：RTL 级内左右括号镜像互换，LTR 不动。
- **wayland 剪贴板（wl_data_device）**：seat 键盘 enter 时记录 serial；set = 建 wl_data_source（offer utf-8+plain 双 mime，send 事件写 fd）+ `wl_data_device_set_selection(serial)`；get = selection 事件存 offer → `wl_data_offer_receive` 到 pipe → 显示 fd+pipe 双 poll 同步收（2s 超时，与 x11 同模式）；外部无 selection 回落内存缓存。**实跑卡点（如实）**：set/get 的协议握手都依赖**键盘焦点**（set_selection 要近期 enter serial、selection 事件只发给焦点客户），自动化测试窗口拿不到合成器焦点——双连接互测中 B 的 get 返回 NOT_FOUND。协议对象/事件全程无错误（连接存活），内存往返照常；跨连接验证需要一个能授予焦点的合成器会话（或人工聚焦窗口）。

## INCR 增量剪贴板与窗口级 undo 管理器（M11b）

- **INCR（x11，ICCCM 2.7.2）**：阈值 64KB——clipboard 文本超过即走增量协议。**发送端**（本应用为 owner）：SelectionRequest 应答改为写 INCR 类型属性（32 位下界值）+ 对 requestor 窗口 `XSelectInput(PropertyChangeMask)`；对方每次删除属性（PropertyNotify/PropertyDelete）追加下一片（片大小 = min(64KB, XMaxRequestSize×4−64)，本机实测 4 片/200KB），最后一片删除后补零长度片收尾；同时只允许一个传输（并发请求拒绝），clipboard_set 中途换文档会发零长度片礼貌收尾。**接收端**：SelectionNotify 读到 INCR 类型即进收片循环——删属性（=向对方要下一片）→ 等 PropertyNewValue（单片 2s 超时，事件泵与 M9d 同款，其他事件照常分发）→ 读片拼接，直到零长度片；buf 满后继续收片但丢弃（让对方正常结束），总长上限 16MB 防挂死。**结构性修复**：SelectionRequest 服务与 INCR 发送推进移到 x11_dispatch 顶部、先于 handler NULL 检查——剪贴板服务不再依赖应用注册事件处理器（M9d 时代隐藏依赖，本次 fork 测试暴露）。窗口创建加 PropertyChangeMask。dummy/wayland 内存往返无 INCR 概念，行为不变。
- **undo 管理器**（src/myui/my_undo_manager.[ch]）：单条共享时间序栈（`my_undo_stack` 扩展 tag——条目带控件标识，tag 不同不合并批）。`my_undo_stack_record_*_tagged` / `*_undo/redo_tagged` / `clear_tagged` 为栈级新 API（旧 API 等价 tag=NULL）。edit/text_area 共享模式（`my_edit_set_undo_shared` / text_area 同款，borrowed mgr）：用户编辑补丁投共享栈；**路由 undo/redo 语义**：弹顶条目 → 先经窗口分发器把焦点切到条目所属控件（无窗口根则跳过）→ 对该控件 apply。跨控件顺序天然正确（A 打字→B 打字→undo 两次先撤 B 再撤 A）。边界语义（注释写清）：切回私有模式（set_undo_shared(NULL)）**丢弃**该控件的共享条目（owner 已注销的条目无法被路由应用）；set_text 只清本控件条目（clear_tagged），共享模式 blur 仍断批；`my_window_set_undo_manager` 挂载供 `my_window_undo_manager_of_widget` 查找。管理器注册表存 apply 回调（darray 持堆分配 undo_target_t*——darray 是指针数组，此前按值压栈变量地址的写法被测试抓出）。

## GLES MSAA 与盒式 pass 优化（M11c）

- **MSAA**：两 GL port 的 EGL config 协商改为**优先 EGL_SAMPLE_BUFFERS=1 + EGL_SAMPLES=4**，拿不到回落无 AA config（`egl_msaa` 记录，`my_pal_gl_has_multisample` 上报）。ES2 核心**没有** GL_MULTISAMPLE 开关——AA 实际由 surface config 驱动；`my_vgcanvas_gles2_set_antialias(vg, bool)` 落地为状态记录 + 经 `my_gl_t.set_multisample`（新槽位）调 `GL_MULTISAMPLE_EXT`（0x809D 与桌面同值；无 EXT 的驱动会报 INVALID_ENUM，实现吞掉并注释：AA 状态仍由 surface 决定）。本机 mesa 实测：x11/wayland 真窗口与 pbuffer 均拿到 4x MSAA，对角描边边缘像素中间值断言通过；fill 走像素对齐扫描线跨度，对 MSAA 测试无效（须用 stroke 几何）。bench（400x300 8 描边路径场景，mesa/llvmpipe）：no-AA 0.025ms vs MSAA 0.026ms/帧——surface 级开销可忽略。
- **盒式 pass 优化**（soft draw_image 盒式平均，M10c 续）：瓶颈不是内存访问量（块不重叠本就 O(src)），而是 -O0 下逐像素 4 通道变址累加的指令密度。优化：**SWAR 打包累加**——每像素一次 32 位读入，按 r|b、g|a 两个 16 位通道对打包进两个 uint32 累加器（块最大 8x8=64px，通道和峰值 255×64=16320 不越界进邻道），big-endian 回落原标量路径。和与旧实现**逐项相同 → 逐像素完全等价**（非容差，gradient 精确值断言）。bench（2000x1500→400x300，含双线性尾）：-O0 28.93→**11.96ms/帧**（2.4x，盒式 pass 本身 24→~7ms，3.4x）；-O2 9.81→**4.76ms/帧**（2.1x）。-O0 <10ms 目标未完全达成（11.96），尾部双线性 O(dst) 已占 ~4.8ms，继续优化须改双线性采样器（TODO）。

## 双线性定点化与 HiDPI 基础（M12c）

- **双线性整数化**（sample_bilinear 重写）：坐标 16.16 定点、权重量化 1/256（0..256，±1 容差，测试内浮点参照逐像素断言）；LE 快路径把 r|b、g|a 通道对的 x 合成打包成各 2 次 uint32 乘法（lane 峰值 65280 不进位），BE 回落标量整数；权重退化（ax=ay=0，整数倍缩放）直接返回源像素。bench A/B（同会话 -O0，2000x1500→400x300）：纯双线性腿 4.8→**1.8ms/帧**（2.7x）；盒式+双线性总帧耗 12.9→**11.0ms**（-15%，<9ms 目标未达——盒式 pass ~9ms 已是下限，须行缓冲滑动窗，TODO）；上采样 480x270→800x600：15.85（M9b float 基线）→ **12.9ms/帧**。
- **HiDPI 坐标模型（最小闭环）**：**PAL 边界一律逻辑像素**——窗口尺寸/事件坐标/控件树/布局全部逻辑单位；`my_pal_get_scale_factor`（pal vtable 新槽）检测：x11 = Xft.dpi（XResourceManagerString 解析，/96 四舍五入到 0.25 档）→ 物理 DPI（HeightMMOfScreen）→ 1.0；wayland = wl_output.scale 事件（整数，fractional 协议 TODO）；dummy 可注入（my_pal_dummy_set_scale_factor）；linux_fb = MYUI_SCALE 环境变量。各 port 内部物理化：x11 的 X 窗口/back buffer/XImage 按 logical*scale 建，ConfigureNotify/指针坐标 ÷scale 回报逻辑；wayland 的 shm 缓冲按 logical*scale 建 + `wl_surface_set_buffer_scale`，事件本就是 surface-local 逻辑值直通（GL：wl_egl_window 物理尺寸，EGL 表面不受 buffer_scale 约束）；dummy 的 lcd = logical*scale。**渲染生效点**：soft/gles2 vgcanvas 各加 `set_scale`（状态栈内）：设备坐标=(user+translate)*scale、字号按 size*scale 请求、measure 回报逻辑单位（phys/scale）；gles2 顶点批提交处统一乘 scale、viewport 保持物理。my_window 创建时缓存 pal 的 scale 并注入 vg（soft ensure / GL enable 两处）；scale=1 全部直通零回归（既有 59 项测试未动）。dummy scale=2 全链路断言（物理 lcd 尺寸/逻辑 rect/2x 字形位置）；真实高分屏无设备未验证（如实）。未做：fractional-scale、跨屏移动感知、x11 RandR 多屏、wayland 输出热插拔。

## 断行规则（UAX#14 子集）与 INCR 并发（M12d）

- **断行类查表**（src/myr/my_line_break.[ch] + 生成表 1017 区间）：codepoint → 7 个简化类——AL（字母/数字/标记，词内不断）、SP（ASCII 空格，断点消费）、HY（连字符，断于其后）、ID（表意/默认，字间可断）、NS（行首禁则：，。！？；：、）」』等，**前查优先于"空格后"规则**——空格后跟 NS 也不得把 NS 推上行首）、OP（行尾禁则：「『（等开括号）、BK（物理行逻辑处理）。数据由 tools/gen_line_break_data.py 从 awtk vendored libunibreak 的 linebreakdata.c（UCD LineBreak-11.0.0）映射生成；表空隙默认 ID。子集边界：数字标点粘合、希伯来引号、RI 旗标序列、SA 字典断行不做（注释）。
- **折行算法**（ta_vlines_rebuild_from 升级）：扫描时维护**最近合法断点**（`ta_break_ok_before` 七条规则）；超宽时——溢出字符是空格 → 其前断开并消费；否则回退最近合法断点断开（**边界上的空格同时消费**：跨边界空格不属于任何视觉行不计宽）；再否则硬折。CJK 逐字可断、英文单词不断、连字符后断、NS 不落行首、OP 不留行尾，全部有测试向量钉住；M10b ASCII 行为逐条回归一致。
- **INCR 并发传输**（x11）：发送端单传输 → **4 槽并发**（`incr_tx[4]`，SelectionRequest 找空槽启动，满槽拒绝；PropertyNotify 按 (requestor, property) 路由推进对应槽；clipboard_set 取消全部）。fork 实测：5 子进程同时拉 200KB——4 个完整收讫（各自分片 >1），第 5 个收到 REFUSED。

## IME 输入法（X11 XIM，M13a）

- **事件模型**：`MY_EVENT_IME_PREEDIT`（UTF-8 组合串 + 光标记位，借用指针仅分发期有效）与 `MY_EVENT_IME_COMMIT`（提交文本）；分发器把它们投递给焦点控件（与 KEY 同路）。
- **x11 XIM**（独立 TU `my_pal_x11_ime.c`，内部结构共享经 `my_pal_x11_int.h`）：pal 级 `XOpenIM`（失败=纯键盘路径零差异）；窗口级 XIC（优先 `XIMPreeditCallbacks|XIMStatusNothing`——ibus 支持、控件自绘预编辑；不支持回落 `XIMPreeditNothing`，IM 自绘、只收提交）；KeyPress 先 `XFilterEvent`（IM 导航消费）再 `Xutf8LookupString`——多字节结果 → IME_COMMIT，单字节 ASCII 回退原 keysym 路径；FocusIn/Out → XSetICFocus/XUnsetICFocus（窗口加 FocusChangeMask）；spot location 经窗口 vtable 新槽 `ime_set_spot`（edit/text_area 在焦点/光标移动时上报，myui 层传逻辑坐标、port 转物理）；preedit draw 的 chg_first/chg_length 按**字符**偏移做 UTF-8 字节级替换。IM 重启（XRegisterIMInstantiateCallback）为 TODO。
- **控件接入**：IME_PREEDIT 只进显示态（光标处绘制 + 下划线，**不入文档、不入撤销、不发 changed**、blur 清除）；IME_COMMIT 清预编辑后走 `user_insert`（入撤销栈单步、发 changed、驱动 MVVM TwoWay）。dummy port 记录 spot（测试钩子）。
- **验证层级（如实）**：① 单测 fake 事件全逻辑（preedit 替换/清除/不污染文档、commit 撤销单步、MVVM 回写、spot 上报、泄漏）；② x11 冒烟实跑：ibus 下 XOpenIM 连接成功、IC 创建/销毁干净、合成 KeyPress 经 XFilterEvent+Xutf8LookupString 路径到达（ASCII 'a'）；③ **真实 ibus 打字未自动化**（合成事件驱动 ibus 组合不可靠，且需抢占用户桌面焦点——手动验证步骤见 porting.md）。wayland text-input 协议为 TODO。

## 盒式滑动窗实验与 wrap+RTL 行级语义（M13b）

- **盒式行缓冲滑动窗（已实现-实测-回退）**：行缓冲版把每目标行的 k 个源行先累加进打包列缓冲（O(src_w) 行缓冲），x 向块求和——但盒式块**不重叠**，源像素本来就只读一次，行缓冲反而增加整块 read-modify-write 流量；实测 -O0 13.9ms vs 逐块寄存器累加 11.0ms、-O2 5.2 vs 4.8——**回退保留 M11c 逐块 SWAR 版本**，结论与数据留在代码注释与此处（行缓冲/滑动窗只在重叠窗口或积分图场景才有意义，与不重叠盒式降采样不匹配）。
- **wrap+RTL 行级语义定案**：wrap 折行保持逻辑序（断点本就逻辑序，行序=阅读顺序自上而下——RTL 段落亦如此，无需行级重排）；text_layout 新增**段落基方向 rtl_base**（区别于"含 RTL run"的 has_rtl），text_area 绘制/光标的**默认对齐跟随段落方向**：未显式设 align 时 RTL 段落视觉行右对齐（LTR 与混排 LTR 段保持 LEFT；显式 CENTER/RIGHT/JUSTIFY 优先）；上下移动进入 RTL 视觉行落在其**视觉起点**（=逻辑末边界，M12a goal 视觉边界语义的自然延伸，测试固化）。混排段落跨行视觉连续性仍为 TODO。

## 复合控件：dialog / menu / tooltip（M13c）

- **浮层基础设施**：widget 新 `floating` 标志（overlay 子控件，linear 布局两个 pass 均跳过，rect 由 owner 绝对设置）+ `user_data` 指针（core 不用，owner 回链）；PAL 窗口 vtable 新 `move` 槽（dummy 记录、x11 物理换算 XMoveWindow、wayland NOT_SUPPORTED、fb noop——dialog 拖拽移动留 TODO）；dummy port 新 `my_pal_dummy_inject_event`（走注册 handler/wm 全路由，测试用）。
- **模态与遮罩**：window 新 `modal`/`scrim` 标志；window_manager 路由事件时 top 窗 modal 且目标非 top 则吞掉 POINTER/KEY/IME；被遮窗 scrim=true 时 paint 逐脏区叠加 rgba(0,0,0,96) 半透明幕。
- **my_dialog（组合式，非控件子类）**：真实 PAL 子窗口（win 字段公开）；内容容器 `my_dialog_content()`（focusable，挂 ESC→CANCEL 的 key_down 监听）+ 底部按钮行（按钮 ctx darray，click→`my_dialog_close(result)`）；close 回报 result 回调并清理 wm 窗口。模态靠 wm 阻断 + scrim，无额外事件循环。
- **my_menu（数据模型 + 窗口内浮层，不开 PAL 窗）**：`my_menu_popup(win, menu, x, y, cb, ctx)` 在窗口 root 末尾挂**全窗口 floating overlay**（吃外部点击→dismiss，吞其余事件，键盘导航经焦点），menu box 为其子（绝对定位、内部点击吃掉）；item 是 box 子控件（DOWN 高亮、UP 叶项报 id/有子菜单则开级联）。**边缘翻转**：x+bw 超窗宽则贴右缘（y 同理），再 clamp ≥0；级联上限 `MENU_MAX_DEPTH=3`，子菜单继承父 cb/win；键盘 Up/Down 环绕移动高亮、Enter 激活、ESC 关当前层；选叶项沿 parent 链全关。文本宽按 8px 格子估算（无字体测量）。
- **tooltip（窗口级悬停浮层）**：widget 新 owned `tooltip` 字符串（set/get，destroy 释放；XML 通用属性 `tooltip="..."`）；my_window 在事件分发**前**做 hover 跟踪（POINTER_MOVE hit_test 后沿祖先找带 tooltip 者，排除 tip 自身）：目标变更→取消 500ms one-shot 定时器（win->loop，回调返回 FAIL 即单次）并隐藏旧 tip，到时→在光标 +(12,16) 处弹 floating "tooltip" 小控件（越界钳进窗口、底部越界翻到光标上方）；POINTER_DOWN/KEY_DOWN 立即取消+隐藏；目标子树被移除经 removed_hook 清态（tip_hide 先清指针再 remove 防重入）；窗口 destroy 取消定时器并收 tip（window/tree 各持一引用，平衡释放）。
- **主题默认色**：menu_box/menu_item（hover 高亮）/dialog_content/tooltip 四组键入 `my_theme_default_create`；demo_widgets 增 Dialog/Menu 按钮、各按钮 tooltip，dummy dump 出 scrim/dialog/menu/tooltip 四张目检图。级联深度>3、菜单鼠标悬停开级联（现要点/Enter）、dialog 拖拽移动为 TODO。

## myconf 配置文档树（M17a）

- `src/myc/myconf/`（并入 myc）：`my_conf_node_t` 七型树（NULL/BOOL/INT64/DOUBLE/STR/OBJECT/ARRAY，OBJECT 保插入序，children=darray，父持子所有权）+ 点路径查询（数字段在 ARRAY 上是下标、OBJECT 上是字面键）+ 类型严格带默认值的 getter（INT64↛DOUBLE 不互转）+ load/save_file（JSON）。
- **JSON 全集**（RFC 8259 自研递归下降）：全转义 + \uXXXX 代理对、整数→INT64（strtoll ERANGE 溢出回落 DOUBLE）/小数指数→DOUBLE、错误带 1 基行列；序列化紧凑/pretty(2) 两式，整数值 DOUBLE 打 %.1f 保类型往返；畸形输入 14 种向量全部明确拒绝。
- **BSON**：严格小端读（长度自洽校验、零越界、嵌套上顶 64、任意前缀截断 fuzz 不崩）；映射 0x01/02/03/04/07(24 hex)/08/09(datetime→INT64 毫秒)/0A/10→INT64/12，**其余类型一律报错**（完整性优先）；写侧 INT64 按范围选 0x10/0x12。TOML/YAML 子集为 M17b（docs/conf.md）。

## 客户端装饰（CSD）标题栏（M16）
- **动机（实测）**：GNOME/mutter 的 wayland 会话对 plain xdg-shell 客户端**不提供 SSD**（registry 不广告 zxdg_decoration_manager_v1）——窗口无标题栏、无法拖动。结论：必须由客户端自绘装饰。
- **PAL 能力槽**：pal vtable 新 `needs_client_decoration`（wayland=true；x11/dummy/linux_fb=false；NULL 槽安全返回 false，`my_pal_needs_client_decoration` 包装）；window vtable 新 `begin_move`（wayland = `xdg_toplevel_move(toplevel, seat, last_button_serial)`，serial 在 on_pointer_button 里记录；x11 由 WM 管移动为 noop；dummy 记录次数供测试）。
- **my_window 结构**：`my_pal_needs_client_decoration(pal)` 为真时，root 挂垂直 linear 布局，下挂 `csd_bar`（h:36：#3C4043 深灰底、居中白色 13px 标题——窗口新增 owned `title` 副本、右侧 32px "×" 关闭钮，on_layout 里贴右缘）+ `csd_content`（h:1f 普通容器）。**`my_window_widget(win)` 在 CSD 模式返回内容容器**（非 CSD 返回 root，行为不变）；paint/hit/dispatch 仍走 root。tooltip/menu 浮层是 `floating`，linear 布局跳过它们，挂哪层都安全。
- **交互**：栏体 POINTER_DOWN → `begin_move`（关闭钮是子控件先吃事件，天然排除）；关闭钮 click → 经 `win->wm`（wm_open 时设置，close/destroy 时清 NULL + 吸收 CSD create ref）**延迟 1ms 定时器**关闭（同步关闭会在 dispatch 中途释放控件树，与 dialog 延迟关闭同因同构）；无 wm 时 no-op（注释注明）。
- **引用计数契约**：通用模式 `unref(my_window_widget(win))` 在 CSD 下落在内容容器上——容器在 setup 时多拿一引用与之平衡；窗口 create ref 由 wm close/destroy 对 CSD 窗口多 unref 一次吸收（代码注释钉死），泄漏测试（debug allocator）固化全路径平衡。dialog 自动获得 CSD（拖动 + 关闭钮）。
- **测试钩子**：dummy port `my_pal_dummy_set_needs_csd` / `my_pal_dummy_begin_move_count`。最大化/最小化钮、边缘 resize、双击最大化为 TODO。
- **圆角窗口角（同里程碑）**：shm 格式 XRGB8888→**ARGB8888**（像素布局不变，alpha 生效）；`present()` 在 memcpy 后对四角做 alpha 冲孔（`myui_wl_corner_mask`，半径 10px×scale，像素中心整数弧判定，每角 ~100 像素开销可忽略）——此 port 目标合成器均不供 SSD，无条件生效（注释钉死）；x11/dummy 不动。

## 框架补齐：flow / rich_label / scroll_view / hover（M14a）

- **flow 流式布局器**（my_layout.c 扩展）：`my_layouter_flow_create(alloc, h_spacing, v_spacing, align)`——子控件按声明宽度横排，`x + cw > 父宽` 且行非空则换行（恰好放下不换行）；行高=行内最高子控件；`MY_FLOW_ALIGN_LEFT/CENTER` 控制每行水平对齐（CENTER 在行收尾时按行宽回溯偏移）。子控件尺寸：layout_params 的 PX/% （% 相对父宽/高），AUTO=保持当前 rect，**FLEX 在 flow 里无意义按 AUTO 处理**；invisible/floating 跳过（同 linear）。只定位子控件、**不改父高**：`my_layouter_flow_measure(parent)` 用同一走行算法纯计算内容总高（父未挂 flow 返回 0），供 scroll_view 等取内容尺寸。
- **my_rich_label 富文本标签**：行内多段（`add_segment(text, rgba, bold)`），单行顺序排布、垂直居中、超宽截断（起点已在界外的段直接跳过，界内部分由 paint clip 截掉）；**伪粗体**=同字形 +1px 二次绘制（backend 无合成加粗，soft/gles 通用）；`content_width()` 给 8px 格子估算（布局提示用，实际绘制以 vg 字体测量为准）；多行场景用 flow 容器组合多个 rich_label。
- **my_scroll_view 通用滚动容器**：单内容子控件（任意子树），wheel 滚动（24px×3/行）+ 外挂 scroll_bar（兄弟控件，`set_scroll_bar` 弱引用双向同步：sv→bar 推 value/page_size 走公开 setter 不回发 changed，bar "changed"→offset=value×max，无反馈环）；内容高度=显式 `set_content_height` 优先 → content 挂 flow 则 `flow_measure` → 否则 content 当前 rect.h；offset 钳制 [0, ch−view_h]；**clip 零新代码**——my_widget_paint 本就先把子树 clip 进父 rect，scroll_view 只把 content 摆到 y=−offset（hit_test 同理天然被父边界裁掉）。
- **hover 状态机（分发器集中维护）**：dispatcher 新 `hovered` 弱引用；POINTER_DOWN/MOVE/UP 时按 target（grab 优先，否则 hit_test）切换：旧控件清 `widget->hovered` + emit "hover_leave"、新控件置位 + emit "hover_enter"，双双 invalidate；UP 释放 grab 后按落点 re-hit（拖拽中 hover 跟随 grabbed）；`forget()` 对移除子树清态不发事件。**接入点 = widget 基类 `hovered` 标志**：控件在状态机里读它即可启用 MY_STATE_HOVER 样式槽——button 已从自维护 MOVE 跟踪迁移（旧实现移出控件后 hover 态不会复位）；pressed 优先于 hover（button_state 顺序）。tooltip（M13c，window 层 hit_test 跟踪）独立共存。

## 文本对齐与描边关节合并（M11d）

- **水平对齐**（`my_text_align_t`：LEFT/CENTER/RIGHT/JUSTIFY，src/myui/my_text_align.h 含 parse/str 辅助）：label `my_label_set_align`（默认 **LEFT**——M11d 前视觉上是居中，要旧观感显式设 CENTER；单行 label 的 JUSTIFY=LEFT）；text_area `my_text_area_set_align`（默认 LEFT 零回归）。语义（M11a "x 恒左缘"之下）：CENTER/RIGHT 按行测量宽（font measure 或 8px 格子兜底）整体偏移基线 x，选区高亮与光标同行偏移（JUSTIFY 的词距拉伸不反映在高亮/光标位置，TODO 注明）；RTL 段落同此设置（整个视觉块右贴）。**JUSTIFY 仅 wrap 模式**：物理行的**非末段视觉行**把 (内宽-行宽) 均摊给每个分隔空格（有后随字符的空格），逐词 draw_text；末段与无分隔空格的行渲染为 LEFT；无 wrap 时无效（注释）。接入：XML `align` 属性（label/text_area）、MVVM `align` string 属性（get 回枚举名）。
- **stroke 关节覆盖率合并**（soft，AA level ≥ 1）：旧路径每段四边形独立 fill_polys、cap/join 圆盘独立 soft_fill_circle——半透明描边关节处跨调用叠加过混合（a=128 关节实测 ~224）。新路径 `soft_stroke_union`：把本次 stroke 的**全部条带四边形（用户空间）与圆盘（设备空间，span 数学与 soft_fill_circle 逐像素一致）**累积进**同一个**逐行覆盖率缓冲（饱和加、maxcov 封顶——像素有效 alpha 恒 ≤ color.a），emit_row 一次。边界：仅同一 stroke() 调用内合并；跨调用仍正常 src-over（a=128 交叉点 ~192，有测试钉住）；AA level 0 保留逐件路径（过混合照旧，注释）；圆盘覆盖率与旧实现逐像素一致，**golden 变化仅限关节重叠像素**（round_cap 场景已按既有流程 golden_gen 再生并目检）。
