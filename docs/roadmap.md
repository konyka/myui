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
- **M11c GLES MSAA + 盒式优化** ✅ 已完成：EGL config 优先 EGL_SAMPLES=4（x11/wayland/pbuffer 本机全拿到，对角描边边缘中间值断言通过，拿不到走文档化回落）；`my_vgcanvas_gles2_set_antialias` 落地（ES2 无核心开关，EXT 切换 + surface 驱动语义写清）；盒式 pass SWAR 打包累加（和逐项相同逐像素等价）——2000x1500→400x300：-O0 28.9→12.0ms、-O2 9.8→4.8ms/帧。
- **M11d 对齐 + 描边关节合并 + 收尾** ✅ 已完成：`my_text_align_t`（LEFT/CENTER/RIGHT/JUSTIFY）+ label/text_area 对齐（CENTER/RIGHT 行宽偏移，选区/光标同偏；JUSTIFY 仅 wrap 非末段视觉行拉伸词距，无 wrap 无效）；XML/MVVM align 属性；soft stroke 关节覆盖率合并（单次调用内全部条带+圆盘共享覆盖率缓冲饱和加，半透明关节 alpha 不再翻倍：~224→~128，跨调用与 AA level 0 边界注释钉死）；golden round_cap 场景按流程再生（仅关节重叠像素变化）。
- **M11 完成**。
- **M12a 编辑控件 RTL** ✅ 已完成：text_layout 边界-视觉双向映射（单光标"前字符逻辑尾边"语义 + 规范边界往返一致、别名边界仅逻辑可达）；方向键/Home/End 按视觉方向（LTR 恒等零回归）；edit/text_area 光标 x/点击/选区跨 run 分段矩形接入；goal_col 统一为视觉边界索引；wrap+RTL 行级重排与跨行视觉连续性列 TODO；修 OFF 模式映射数组未初始化段错误；BIDI=OFF 构建 59/59 绿。
- **M12b Lam-Alef + L4 镜像 + wayland 剪贴板** ✅ 已完成：lam-alef 强制合字（两遍制整形，按连接上下文选 isolated/final 形，UCD 生成数据；Naskh 目检合字字形）；UBA L4 镜像（本地 BidiMirroring.txt 全表 428 对生成，RTL 级内括号镜像断言）；wayland wl_data_device 剪贴板（source offer/send、selection receive+pipe 同步读、内存兜底）——**跨连接实测被合成器焦点策略阻断**（set/get 握手依赖键盘焦点，协议无错误但拿不到 enter serial/焦点 selection 事件，如实记录于 architecture.md）。
- **M12c 双线性整数化 + HiDPI 基础** ✅ 已完成：sample_bilinear 定点化（16.16 坐标 + 1/256 权重 ±1 容差 + LE 打包乘法 + 退化权重短路；纯双线性腿 4.8→1.8ms/帧 2.7x，盒式+双线性 12.9→11.0ms，上采样 15.85→12.9ms）；PAL `get_scale_factor` 全链路（x11 Xft.dpi/物理 DPI、wayland wl_output.scale、dummy 注入、fb 环境变量）；坐标模型=PAL 边界全逻辑像素、port 内部物理化（x11 窗口缓冲×scale+事件 ÷scale、wayland shm×scale+set_buffer_scale）；soft/gles2 vgcanvas set_scale（设备坐标/字号缩放、measure 回报逻辑值）；my_window 缓存注入；dummy scale=2 全链路测试；真实高分屏无设备未验证。
- **M12d UAX#14 子集 + INCR 并发 + 收尾** ✅ 已完成：断行类查表（1017 区间，libunibreak/UCD 生成；AL/SP/HY/ID/NS/OP/BK 七类，行首禁则优先于空格规则）驱动 wrap 折行（CJK 可断、英文不断、连字符后断、NS 不落行首、OP 不留行尾、边界空格消费；中/日/英/混排测试向量 + ASCII 零回归）；INCR 发送端 4 槽并发（满槽拒绝，按 requestor+property 路由推进；fork 实测 5 并发拉取 4 OK + 1 REFUSED）。
- **M12 完成**。
- **M13a IME（X11 XIM）** ✅ 已完成：IME_PREEDIT/IME_COMMIT 事件 + 焦点投递；x11 XIM 独立 TU（XOpenIM、每窗 XIC、PreeditCallbacks 优先回落 Nothing、XFilterEvent+Xutf8LookupString 键路径、spot location 跟随光标）；edit/text_area 预编辑显示（下划线、不入文档/撤销/changed）+ 提交插入（撤销单步 + MVVM 回写）；fake 事件单测全覆盖；实机 ibus：XOpenIM 连接 + IC 生命周期 + 合成键过 IM 路径冒烟通过；真实打字自动化不可靠已文档化手动步骤（porting.md）。
- **M13b 盒式滑动窗实验 + wrap RTL 行级** ✅ 已完成：行缓冲滑动窗实现后实测更慢（-O0 13.9 vs 11.0ms），**回退保留逐块 SWAR**（数据与结论入档）；wrap+RTL：rtl_base 段落基方向接入，默认对齐跟随段落方向（RTL 段落视觉行右对齐、显式 align 优先、混排 LTR 段不动）、上下移动进入 RTL 视觉行落视觉起点，测试固化。
- **M13c 复合控件（dialog/menu/tooltip）** ✅ 已完成：浮层基础设施（widget `floating` 布局跳过 + `user_data`、PAL 窗口 `move` 槽四 port、dummy `inject_event` 测试钩子）；wm 模态阻断（top modal 吞下层 POINTER/KEY/IME）+ 被遮窗 scrim 半透明幕；`my_dialog` 组合式模态对话框（真实子窗口、内容容器 ESC=CANCEL、按钮 result 回调）；`my_menu` 数据模型 + 窗口内 overlay 弹出（外部点关、边缘翻转钳位、级联上限 3、Up/Down/Enter/ESC 键盘导航）；tooltip（widget `tooltip` 字段 + XML 通用属性，窗口 500ms hover 定时器、光标旁钳位浮层、移除/销毁全路径清态无泄漏）；主题默认色四组；demo_widgets 演示 + dummy dump 四图目检。级联深度、悬停开级联、dialog 拖拽留 TODO。
- **M13d XML=OFF 回归修复 + CI 裁剪 job + 收尾** ✅ 已完成：`my_ui_loader_test` 的 ui2c golden 死代码从 `#ifndef MYUI_UI_XML` 分支删除（OFF 分支的拷贝永远不会运行）+ tests/CMake 仅在 XML ON 时定义 `MY_UI2C_SAMPLE_C`/生成样例（修 `-Werror=unused-function`）；`my_text_align_test` 的 XML 用例加 `MYUI_UI_XML` guard（OFF 时 my_ui_load_str 返回 NULL 段错误）；`my_wrap_rtl_test` 补 `MYUI_BIDI` guard（M13b 遗漏，全 OFF 裁剪暴露）；CI 新增 linux-minimal job（dummy + FONT_STB/IMAGE_STB/UI_XML/BIDI 全 OFF）防裁剪回归；bench 汇总刷新；M14+ 清单整理。
- **M13 完成**。
- **M14a 框架补齐（flow/rich_label/scroll_view/hover）** ✅ 已完成（duanxianxia 复刻前置，spec: docs/superpowers/specs/2026-08-04-duanxianxia-clone-design.md）：`my_layouter_flow` 流式换行布局器（行高取最大、LEFT/CENTER 行对齐、PX/% 尺寸、FLEX 按 AUTO、`flow_measure` 内容测高）；`my_rich_label` 行内多段富文本（段级颜色 + 1px 二次绘制伪粗体、垂直居中、超宽截断）；`my_scroll_view` 通用滚动容器（单内容子树、wheel + 外挂 scroll_bar 双向同步无反馈环、clip 复用 paint 既有机制零新代码、内容高显式/flow measure/rect 三级来源）；hover 状态机收进分发器（`hovered` 弱引用 + hover_enter/leave 事件 + widget 基类 `hovered` 标志驱动 MY_STATE_HOVER 样式槽，grab 期间跟随被压控件、UP 按落点 re-hit；button 迁移修掉"移出不复位"旧缺陷）。测试：flow 9 + rich_label 5 + scroll_view 6 + hover 4 全绿。
- **M14b duanxianxia 应用骨架** ✅ 已完成：apps/duanxianxia（`dxx` 可执行 + `dxx_core` 静态库供测试链接）；顶栏（#444、logo 文字、4 组下拉菜单 my_menu 展开/收起、6 平铺项含 orange/yellow-bold、1px 分隔线、注册/登录，hover 高亮用 M14a 机制）；指数条 12 列（名称/伪粗体数值/涨跌幅，红涨绿跌，5 项真实新浪快照 2026-08-12 + 7 项约值逐行标注）；页脚两行（免责声明 + ICP）；站点配色 dxx_theme；**框架新增**：my_font_stb 的 TTC 首 face 支持 + `my_font_stb_create_chain` 逐码点 fallback 链（解决 DroidSansFallback 无 Latin / Noto CJK VF 为 CFF2 stb 不可解析的双重阻塞）；dummy dump 主页+菜单展开两图目检通过；差异点累计入 docs/apps/duanxianxia.md。
- **M14c 直播面板区 + 晋级天梯表** ✅ 已完成：双列直播区（情绪直播 750x800 + 涨停直播/异动/股票池/成交额四卡，feed 关键词红绿高亮经 rich_label，卡内 scroll_view 滚动）；涨停股票池晋级表（表头进度/晋级率/标题 + 分享图片红按钮，6 行 130 项**完整真实快照**（2026-08-04，dxx_data.c 注明；行数/项数/成数与晋级率分子一致性单测钉死），股票项=徽标五色+状态三色+[涨幅]+题材，flow 流式换行 + `flow_measure` 行高自适应，1px #ddd 单元格边框）；整页 scroll_view + 右侧 scroll_bar；tooltip/个股 dialog/分享 dialog 交互全通（dialog 继承根窗口字体——框架行为：my_dialog 新建窗口无字体，复制 rw->font 解决）；**框架修复**：`my_window_on_pal_event` 漏路由 POINTER_WHEEL（滚轮此前从未走通过真实事件路径，嵌套滚动测试暴露）；嵌套 wheel 内层消费不冒泡（单测钉死）；dummy dump 6 场景 PNG 目检通过。

## 性能基线汇总（M13d 刷新，GCC 16，-O0 Debug，本机）

| 场景 | 数值 |
|------|------|
| 50 按钮全帧 | 2.45 ms/帧 |
| 100 半透明矩形 | 2.00 ms/帧 |
| 路径 AA level0/1/2 | 0.69 / 1.65 / 2.40 ms/帧 |
| 480x270→800x600 图片 nearest/bilinear | 2.23 / 9.57 ms/帧 |
| 2000x1500→400x300 图片 nearest/纯双线性/盒式+双线性 | 0.72 / 1.82 / 11.06 ms/帧（M12c 整数化；M11c 时 0.69 / 4.80 / 11.96） |
| GLES 8 描边路径 no-AA/MSAA4x | 0.025 / 0.026 ms/帧 |
| text_area 万行：载入/2000 移动/100 插入 | 0.13 / 0.09 / 0.09 ms |
| text_area wrap 万行长行：载入+构建/1000 视觉移动/滚动帧 | 1.57 / 0.10 / 0.30 ms |
| list_view 万行滚动（固定/变高） | 0.002 ms/次（~22 行控件） |
| 1051 控件构建 / relayout / 10 万 hit_test | 0.36 / 0.06 / 27.1 ms |

- **M14d 交互收尾 + 视觉比对（复刻收官）** ✅ 已完成：导航切换（顶栏全部可点项→占位面板，logo/涨停表现回首页，当前项 #E64C62 高亮模拟 bootstrap active）；登录/注册 dialog（edit hint/password + MVVM 全链路：TwoWay + not_empty validator + submit 命令 + 红字错误提示，注册带确认密码一致性）；分享图片真实导出（晋级表离屏 lcd_mem 渲染 + BGRA→RGBA + stb_image_write，路径可注入，dialog 报真实文件名）；视觉逐项比对表入 docs/apps/duanxianxia.md（一致/近似逐项标注）；dummy dump 10 场景 PNG 全目检。
- **M14 完成**。
- **M15+ 候选**：wayland text-input-v3、候选窗内嵌、IM 重启（XRegisterIMInstantiateCallback）、IME 真实打字自动化；盒式降采样积分图（SAT）路线、fractional-scale/跨屏/RandR 多屏、真高分屏实测；竖排、自由合字（Lam-Alef 之外）、UAX#14 全规则、wrap+RTL 混排视觉行级重排；INCR 接收端并发、wayland 剪贴板焦点握手实测；菜单悬停开级联/级联深度>3/ESC 焦点回退父层、dialog 拖拽移动（PAL `move` 槽已备）。**SDK 顺延**：iOS/HarmonyOS/Android/Web/win32/sdl2 port、Metal backend、FreeBSD/linux_fb 实机复核。
