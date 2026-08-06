# myui 路线图

每个里程碑遵循 TDD（测试先行），全部 ctest 绿后进入下一里程碑。

- **M0 地基** ✅ 已完成：CMake 脚手架 + mytest 测试框架 + myc 第一批（错误码/日志、allocator、darray、str、emitter、value、object）；C99/C11/C17/C23 四档严格编译验证。
- **M1 渲染** ✅ 已完成：vgcanvas / lcd 接口冻结；software backend（RGB565/RGB888/ARGB8888/BGRA8888/MONO 像素格式特化）；my_lcd_mem 内存帧缓冲；脏矩形收集器；扫描线 even-odd 路径填充；golden-image 测试（tests/golden/，5 场景全格式）。抗锯齿与 alpha 混合推迟到 M3+ 评估。
- **M2 PAL + 窗口** ✅ 已完成：PAL 接口冻结（窗口/主循环/时钟/入口四职责 + 统一事件 my_event_t）；dummy port（headless 单测全基于它，可注入假时钟）；x11 port（XImage 上屏、select + 自唤醒 pipe 主循环、keysym 映射表单测）；timer 管理器（假时钟确定性测试）；demo_hello 集成（x11 实窗 + dummy dump PPM）；x11 冒烟测试 DISPLAY 存在时实跑。遗留 TODO：dirty-rect 部分上屏、IME、HiDPI、sdl2 port。
- **M3 控件核心** ✅ 已完成：widget 基类（引用计数树/坐标/绘制/emitter）、linear 布局器（px/%/flex）、事件分发（hit/冒泡/grab/焦点）、window + window_manager + my_app_run、主题样式（style/theme + 文本加载 + 默认浅色主题）、动画（插值 + easing + yoyo/repeat，主循环 timer 驱动）、button/label 内置控件、demo_widgets。性能基线（AMD 桌面，GCC 16，-O0 Debug）：bench_render 50 按钮全帧重绘平均 **2.25 ms/帧**；bench_widget 1051 控件构建 0.30 ms、全树 relayout 0.05 ms、10 万次 hit_test 26.3 ms。遗留 TODO：网格布局、字体、XML 加载器、抗锯齿/alpha 混合评估。
- **M4 MVVM** ✅ 已完成：mymvvm/base（view_model + dummy、binding_context、data/command/items/condition 绑定、converter、validator、navigator、规则 parser）；view_model_array（可观察列表）；myui 适配层 mymvvm_myui（widget target、my_mvvm_bind、模板注册表、navigator_wm）；demo_mvvm；docs/mvvm.md 语法规范。遗留 TODO：列表虚拟化、条件渲染模板、文本输入控件、自定义 converter/validator 注册表开放。
- **M5 平台铺开** ◐ 部分完成：GLES2 vgcanvas backend（CPU 三角化 + my_gl_t 隔离；mock 单测 + EGL 真实冒烟通过）；linux_fb port（my_osal_t 注入，假设备单测全绿，无 /dev/fb0 未实机）；cmake 工具链文件（emscripten/android/harmonyos/ios，无 SDK 未验证）+ porting.md 各平台移植路线图。顺延 M6：macOS(cocoa)、Android/Web 实机接入（本机无 SDK/设备）。
- **M6 收尾** ✅ 已完成：wayland port（xdg-shell + wl_shm + xkbcommon，真实合成器冒烟通过）；CI 全矩阵（.github/workflows/ci.yml：gcc/clang × C99–C23 + dummy + wayland + MSVC + macOS + doxygen job）；FreeBSD 适配确认（x11/wayland 逻辑共用，linux_fb 不适用已注明）；Doxyfile + API 文档管线。
- **M7a 字体系统** ✅ 已完成：my_font 抽象（measure/字形 8bpp alpha/ascent/line_height）；内置 8x8 位图字体（Liberation Sans 生成，tools/gen_bitmap_font.c 可再生）；stb_truetype 后端（MYUI_FONT_STB 可裁剪，LRU 字形缓存 256 可配）；soft draw_text（lcd blend_span src-over 各格式特化）+ gles2 draw_text（alpha 纹理 + quad，纹理缓存）；vgcanvas 接口 +set_font/measure_text；button/label 真实文本（font_size/fg_color 主题键生效）；EGL 真实冒烟含文本像素断言；全部 demo 显示真实文字。
- **M7b edit 输入控件** ✅ 已完成：my_edit 单行输入（UTF-8 光标/选区/hint/readonly/max_len/password/点击定位/水平滚动）；分发器焦点切换发 focus/blur 事件；widget_target 映射 edit text/hint；MVVM TwoWay 表单端到端（键入→vm、vm→edit、validator 拒绝恢复）；demo_mvvm 真实表单（name edit + submit + greeting label）。
- **M7c alpha 混合 + 抗锯齿** ✅ 已完成：fill_rect 全量 src-over（a<255 混合、a=255 快速路径，RGB565 展开-重打包，像素级公式断言）；soft 路径/圆角 x 向 4 子采样覆盖率 AA（运行时开关默认开，y 向不采样的权衡已注明）；GLES src-over 冒烟通过；golden 基准策略（旧场景 AA off、新增 AA 场景）。bench（-O0）：半透明矩形 2.03ms/帧、路径 AA on 1.00ms vs off 0.68ms。
- **M7d 基础控件** ✅ 已完成：checkbox（两态+mixed 显示、对勾 stroke 绘制、value 可绑）、slider（grab 拖动/轨道点击/min-max-step、value 可绑）、progress_bar（展示型 OneWay）；widget_target 三控件 value 映射；slider↔progress MVVM 联动 e2e；demo_widgets 扩充（checkbox/slider/progress 一行，dummy dump 目检）。
- **M7 完成**。
- **M8a XML UI 加载器** ✅ 已完成：自研零依赖 XML parser（小 DOM、错误行列号、全套畸形输入测试）；UI 加载器（控件工厂注册表、布局/lp/v:*/style 内联、`MYUI_UI_XML` 可裁剪）；demo_mvvm 主页 XML 驱动。
- **M8b list_view 虚拟化 + image 控件** ✅ 已完成：my_list_view（固定行高、可视行+缓冲、回收池复用、wheel/拖动滚动、滚动条指示；10000 行仅建 ~22 行控件，滚动 0.002ms/次）；POINTER_WHEEL 事件四 port 接入；my_list_adapter 抽象 + items 绑定自动虚拟化；my_image（stb_image、按路径 LRU 缓存 8 项、4 种缩放、alpha 预合成、可裁剪）；bench 数值已刷新。
- **M8c 渲染质量 + 剪贴板** ✅ 已完成：y 向 AA（x4y2，bench 1.42x < 2.5x 阈值故 level2 默认开；level0/1/2 = 0.72/1.70/2.41ms 每帧）；stroke 四边形条带化（共享覆盖率路径，奇数线宽半像素对齐）；剪贴板（pal 接口 + dummy/linux_fb/wayland 内存实现 + x11 selection 拥有与应答，外部获取 TODO）+ edit Ctrl+C/X/V；edit 光标 500ms 闪烁。
- **M8d MVVM 开放注册 + 收尾** ✅ 已完成：converter/validator 自定义注册（自定义优先、覆盖告警、unregister 回落）；文档总收尾；bench 汇总见下。

- **M9a text_area 多行编辑** ✅ 已完成：行偏移缓存（局部重建，10k 行：载入 0.13ms/2000 次移动 0.09ms/100 次插入 0.09ms）；目标列语义；选区/剪贴板（保留换行）；滚动保光标；MVVM TwoWay + XML 标签；demo_widgets 接入。
- **M9b 图像质量** ✅ 已完成：GLES draw_image（RGBA 纹理 + (ptr,w,h) LRU 16 项 + 专用采样着色器，EGL 四象限读回通过）；soft 双线性缩放（像素中心映射，默认开；bench：480x270→800x600 最近邻 2.2ms vs 双线性 15.5ms/帧（7x，-O0）——嵌入式建议 NEAREST，已写进 architecture.md）。
- **M9c 滚动条 + 变高列表 + 圆 cap/join** ✅ 已完成：my_scroll_bar（拖拽/翻页/min-thumb/主题态）；list_view/text_area 显式挂接双向同步；变高行前缀和缓存 + 估算总高（200 次变高滚动 ≤200 行控件）；stroke ROUND cap/join（覆盖率圆盘，过混合注释）+ golden 场景。
- **M9d XML→C 生成器 + 剪贴板收尾** ✅ 已完成：tools/ui2c（XML→C 构建函数，golden 等价测试：运行时加载 vs 生成代码逐节点比对）；x11 外部剪贴板获取（XConvertSelection + 500ms 同步等待 + 事件重入分发，INCR 留 TODO；双进程外部 owner 实跑通过）。
- **M10a 撤销/重做 + 键盘导航** ✅ 已完成：my_undo_stack（文本补丁 + 打字/退格批合并，容量 100，程序 set_text 清栈）；edit/text_area Ctrl+Z/Y；Tab 焦点环、PageUp/Down 翻页、scroll_bar 键盘微调。
- **M10b text_area word wrap** ✅ 已完成：视觉行缓存（编辑行局部重建、resize 全量、wrap off 零回归）；贪心折行（溢出空格前断开并消费 / 最后空格后断开 / 硬折，非 UAX#14）；Up/Down/Home/End 视觉行语义、`(row,col)`→视觉行二分查找、wrap 下禁水平滚动；XML `wrap` 属性 + MVVM wrap 绑定。bench（-O0，万行 ~80cp → 25843 视觉行）：载入+构建 1.57ms、1000 次视觉移动 0.10ms、滚动重绘 0.30ms/帧。
- **M10c 盒式预降采样 + GL 真窗口** ✅ 已完成：soft draw_image 双线性模式缩放比 <0.5 时自动 2/4/8 档盒式预降采样（高频内容零混叠，代价 ~4x 纯双线性——质量特性非速度特性，结论写进 architecture.md）；PAL 窗口 `gl_enable` GL 挂载点（x11 EGL / wayland wl_egl_window，vsync swap interval 1，dummy/linux_fb 打桩）；`my_window_enable_gl` 一行切 GLES 渲染（帧末 swap、RESIZE 更新 viewport）；`MYUI_DEMO_GLES=1` demo 开关；`gl_window_smoke_test` x11/wayland 真窗口实跑通过（渲染 + swap 前 glReadPixels 像素断言 + 300ms 存活）。
- **M10d GLES cap/join + 行高失效 + 收尾** ✅ 已完成：GLES ROUND cap（端点半圆 8 段三角扇）/ROUND join（顶点 lw/2 圆盘 8 段三角扇，覆盖规则同 soft），两后端描边能力对齐；`my_list_view_invalidate_row_height(s)` 变高行动态行高失效（全量重建 / index 起截断前缀和，滚动钳制 + 可视区 + 滚动条即时同步）；mock 顶点断言 + EGL 端点外像素读回；bench -L 全跑无回归。
- **M10 完成**。
- **M11a BiDi + 阿拉伯整形** ✅ 已完成：vendored SheenBidi-3.0.0（Apache-2.0，3rd/SheenBidi，unity TU 放宽警告，`MYUI_BIDI` 默认 ON 可裁剪）；`my_text_layout`（逻辑序→LTR 快速路径→整形→UBA 重排，文本键 LRU 64 + 调用方拷贝语义）；阿拉伯整形自研（3.0.0 无 shaping 模块，按本地 UCD 生成连接类/形表，محمد→词首/中/尾形逐断言）；soft/gles2 draw_text+measure 接入（x 恒为左缘，编辑控件 RTL 光标列 TODO）；demo_widgets 阿/希 i18n 窗口 + dummy dump 目检（字形连接正确、词序 RTL 反转正确）；Noto 字体渲染测试（缺字体 skip）+ GLES 阿拉伯读回；BIDI=OFF 构建 54/54 绿。
- **M11b INCR 剪贴板 + 窗口级 undo** ✅ 已完成：x11 INCR 双向（阈值 64KB、片 min(64KB, maxreq−64)、零长度片收尾、接收 16MB 上限 + buf 满后排空丢弃；fork 子进程双向实测——收 13 片/200KB 完整、发 4 片校验通过）；结构性修复：剪贴板服务/INCR 推进不再依赖应用事件处理器；undo_stack tag 扩展 + `my_undo_manager` 共享时间序栈（跨控件 undo 顺序 + 焦点路由 + 切回私有丢共享历史的边界语义），edit/text_area `set_undo_shared` 接入。

## 性能基线汇总（M10d 刷新，GCC 16，-O0 Debug，本机）

| 场景 | 数值 |
|------|------|
| 50 按钮全帧 | 2.47 ms/帧 |
| 100 半透明矩形 | 2.05 ms/帧 |
| 路径 AA level0/1/2 | 0.71 / 1.70 / 2.39 ms/帧 |
| 480x270→800x600 图片 nearest/bilinear | 2.44 / 16.26 ms/帧 |
| 2000x1500→400x300 图片 nearest/纯双线性/盒式+双线性 | 0.71 / 5.09 / 28.93 ms/帧 |
| text_area 万行：载入/2000 移动/100 插入 | 0.13 / 0.09 / 0.09 ms |
| text_area wrap 万行长行：载入+构建/1000 视觉移动/滚动帧 | 1.57 / 0.10 / 0.30 ms |
| list_view 万行滚动（固定/变高） | 0.002 ms/次（~22 行控件） |
| 1051 控件构建 / relayout / 10 万 hit_test | 0.30 / 0.05 / 29.0 ms |

- **M11+ 候选**：IME、UAX#14 断行、两端对齐（justify）、wrap 下撤销的视觉位置、编辑控件 RTL 光标（视觉-逻辑映射）、Lam-Alef 合字、UBA 镜像（L4）、竖排、盒式 pass 优化（滑动窗口分摊）、GLES AA（MSAA）、INCR 并发多传输、wayland 剪贴板协议接入（wl_data_device）、stroke 关节单轮廓合并。**SDK 顺延**：iOS/HarmonyOS/Android/Web/win32/sdl2 port、Metal backend、FreeBSD/linux_fb 实机复核。
