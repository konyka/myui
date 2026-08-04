# myui 路线图

每个里程碑遵循 TDD（测试先行），全部 ctest 绿后进入下一里程碑。

- **M0 地基** ✅ 已完成：CMake 脚手架 + mytest 测试框架 + myc 第一批（错误码/日志、allocator、darray、str、emitter、value、object）；C99/C11/C17/C23 四档严格编译验证。
- **M1 渲染** ✅ 已完成：vgcanvas / lcd 接口冻结；software backend（RGB565/RGB888/ARGB8888/BGRA8888/MONO 像素格式特化）；my_lcd_mem 内存帧缓冲；脏矩形收集器；扫描线 even-odd 路径填充；golden-image 测试（tests/golden/，5 场景全格式）。抗锯齿与 alpha 混合推迟到 M3+ 评估。
- **M2 PAL + 窗口** ✅ 已完成：PAL 接口冻结（窗口/主循环/时钟/入口四职责 + 统一事件 my_event_t）；dummy port（headless 单测全基于它，可注入假时钟）；x11 port（XImage 上屏、select + 自唤醒 pipe 主循环、keysym 映射表单测）；timer 管理器（假时钟确定性测试）；demo_hello 集成（x11 实窗 + dummy dump PPM）；x11 冒烟测试 DISPLAY 存在时实跑。遗留 TODO：dirty-rect 部分上屏、IME、HiDPI、sdl2 port。
- **M3 控件核心** ✅ 已完成：widget 基类（引用计数树/坐标/绘制/emitter）、linear 布局器（px/%/flex）、事件分发（hit/冒泡/grab/焦点）、window + window_manager + my_app_run、主题样式（style/theme + 文本加载 + 默认浅色主题）、动画（插值 + easing + yoyo/repeat，主循环 timer 驱动）、button/label 内置控件、demo_widgets。性能基线（AMD 桌面，GCC 16，-O0 Debug）：bench_render 50 按钮全帧重绘平均 **2.25 ms/帧**；bench_widget 1051 控件构建 0.30 ms、全树 relayout 0.05 ms、10 万次 hit_test 26.3 ms。遗留 TODO：网格布局、字体、XML 加载器、抗锯齿/alpha 混合评估。
- **M4 MVVM** ✅ 已完成：mymvvm/base（view_model + dummy、binding_context、data/command/items/condition 绑定、converter、validator、navigator、规则 parser）；view_model_array（可观察列表）；myui 适配层 mymvvm_myui（widget target、my_mvvm_bind、模板注册表、navigator_wm）；demo_mvvm；docs/mvvm.md 语法规范。遗留 TODO：列表虚拟化、条件渲染模板、文本输入控件、自定义 converter/validator 注册表开放。
- **M5 平台铺开** ◐ 部分完成：GLES2 vgcanvas backend（CPU 三角化 + my_gl_t 隔离；mock 单测 + EGL 真实冒烟通过）；linux_fb port（my_osal_t 注入，假设备单测全绿，无 /dev/fb0 未实机）；cmake 工具链文件（emscripten/android/harmonyos/ios，无 SDK 未验证）+ porting.md 各平台移植路线图。顺延 M6：macOS(cocoa)、Android/Web 实机接入（本机无 SDK/设备）。
- **M6 收尾** ✅ 已完成：wayland port（xdg-shell + wl_shm + xkbcommon，真实合成器冒烟通过）；CI 全矩阵（.github/workflows/ci.yml：gcc/clang × C99–C23 + dummy + wayland + MSVC + macOS + doxygen job）；FreeBSD 适配确认（x11/wayland 逻辑共用，linux_fb 不适用已注明）；Doxyfile + API 文档管线。
- **M7+（待有 SDK 环境）**：iOS(uikit)、HarmonyOS(NDK)、Android(NDK)、Web(Emscripten/WebGL，工具链文件已备)、win32 port、sdl2 port（1 天保底后端）、Metal backend、FreeBSD 实机、linux_fb 实机复核、字体系统（draw_text 落地）、抗锯齿/alpha 混合评估。
