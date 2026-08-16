# myui 新平台移植指南

> M2 后 PAL 接口已冻结（`src/mypal/my_pal.h`）。移植 = 新增一个 port 目录 + 实现下面四块 vtable。

## 移植步骤

1. 新建 `src/mypal/ports/<platform>/`（参考 `dummy/` 与 `x11/`）。
2. 按下面清单实现 vtable，先全部打桩返回 `MY_RET_NOT_SUPPORTED`，再逐项填实。
3. 选择渲染 backend（见下），确认 `my_pal_window_t.get_lcd()` 的 lcd 实现。
4. CMake：顶层 `MYUI_PAL` 加入新选项，`src/mypal/CMakeLists.txt` 按选项追加源文件与 `MYUI_PAL_<PLATFORM>` 宏，`src/mypal/my_pal.c` 的 `my_pal_create()` 加入分发。
5. 验证：dummy 单测不受影响；本 port 跑 demo_hello 出窗口，事件/定时器工作。

## 必须实现的接口（照 my_pal.h 冻结形态）

最小实现顺序建议：**时钟 → 窗口 → 主循环 → 事件**。

- [ ] `my_pal_<port>_create(allocator)` / pal vtable
  - [ ] `time_now_ms`：单调时钟（毫秒），最先做——主循环和 timer 都依赖它
  - [ ] `set_event_handler`：保存应用唯一 handler
  - [ ] `destroy`
- [ ] 窗口 `my_pal_window_t` vtable
  - [ ] `window_create(w, h, title)`：创建隐藏窗口 + 关联一个 lcd
  - [ ] `get_lcd()`：给 myr 绘制的 `my_lcd_t*`；最简单方案 = 内嵌 `my_lcd_mem`，`end_frame` 时整块拷贝/上屏（x11 port 就是这么做的）
  - [ ] `set_title` / `resize`（重建 lcd 缓冲）/ `show` / `get_size` / `destroy`
  - [ ] `gl_enable`（M10c，可打桩返回 NULL）：窗口的 GL 挂载点，见下节
  - [ ] `set_cursor`（M21a，可打桩 noop/NULL 槽）：鼠标光标形状，见"光标决策点"节
  - [ ] `get_scale_factor`（M12c，可返回 1.0 打桩）：显示器缩放比。PAL 边界一律**逻辑像素**——窗口尺寸与事件坐标报逻辑值；port 内部把渲染缓冲物理化（x11 照抄：窗口/缓冲按 logical*scale 建、事件 ÷scale；wayland：shm 缓冲 ×scale + `wl_surface_set_buffer_scale`，事件直通；dummy：注入即可测全链路）。scale 检测来源：x11=Xft.dpi→物理 DPI→1.0，wayland=wl_output.scale，嵌入式=环境变量或 1.0。
- [ ] 主循环 `my_pal_main_loop_t` vtable
  - [ ] `run`：阻塞等待（select/poll/平台 wait），超时取 `my_timer_manager_due_in_ms`，分发事件 + `my_timer_manager_fire`
  - [ ] `quit`：可跨线程调用，须唤醒 run（pipe/事件/wakeup 消息）
  - [ ] `post_event`：拷贝事件入队 + 自唤醒（跨线程安全）
  - [ ] `add_timer` / `remove_timer`：包一层 `my_timer_manager_t` 即可
  - [ ] `destroy`
- [ ] 事件翻译：原生事件 → `my_event_t`（POINTER/KEY/RESIZE/PAINT/QUIT）
  - [ ] 键码映射表：可打印 ASCII 直通（32..126），特殊键 → `my_key_t`（参考 `x11/my_pal_x11_keymap.c`，独立成纯逻辑函数便于单测）

## 渲染 backend 选择

- 无 GPU / 裸机 / RTOS / Linux-FB：**software backend**（默认）：`my_lcd_mem` 或直接包显存的 lcd 实现 + `my_vgcanvas_soft`。
- 有 GLES2+：GLES backend（M5），真窗口经 GL 挂载点（M10c，下节）。
- **GPU 后端全矩阵接入（M25）**：应用侧一行 `my_window_enable_gpu(win, MY_GPU_GLES2/OPENGL/VULKAN/AUTO)`。新 port 想获得全部 GPU 后端，实现 pal window vtable 末尾两个槽即可（均可先置 NULL 打桩）：
  - [ ] `gl_enable_api(window, api)`（M25a，api=GLES2/OPENGL）：建对应类型的 GL 上下文（桌面系统 EGL/GLX/WGL/NSGL 均可——port 内部自由，返回 `my_pal_gl_t` 语义对象：make_current/swap/get_size/has_multisample/destroy）。旧 `gl_enable` 槽保留 = GLES2 转发。
  - [ ] `vk_create_surface(window, vk_instance, &vk_surface)`（M25b）：`vkCreateXlibSurfaceKHR`/`vkCreateWaylandSurfaceKHR`/对应平台的 surface 创建一行转发；Vulkan 后端（swapchain/管线/present）全在 myr 层，port 不需要任何其它 Vulkan 代码。
  - 验证顺序：gl_desktop_smoke/vulkan_smoke 的离屏部分不依赖 port（EGL surfaceless / 离屏 image），可先跑通；再真窗口 enable_gpu 冒烟（照 gl_window_smoke_test）。
  - Vulkan 已知噪音：loader 枚举 `/usr/share/vulkan/icd.d/` 下全部 ICD 时，freedreno 在 Intel 机器上会打印 `TU: error: ...tu_knl.cc...VK_ERROR_INCOMPATIBLE_DRIVER`——这是驱动枚举的固有失败输出，应用代码无法抑制，也不影响后续 Intel ICD 的正常使用；需要干净输出时可用 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.x86_64.json` 限定 ICD（按本机实际 ICD 文件名调整）。
  - **窗口态 Vulkan 的 MSAA 默认策略（M25c）**：`my_vgcanvas_vulkan` 在离屏渲染时仍默认使用 4x MSAA（测试/离屏截图需要精确抗锯齿）；在窗口态（`offscreen == false`）则默认降为单采样 `VK_SAMPLE_COUNT_1_BIT`，因为部分驱动（实测 Mesa ANV）在窗口 MSAA4/MSAA8 下的 sample pattern/resolve 会产生比参考 soft 后端更宽的 1 像素边缘过渡，表现为“涂抹/发虚”。若应用明确需要窗口 MSAA，可设环境变量 `MYUI_VK_MSAA=1` 强制 4x；`MYUI_VK_NOMSAA=1` 仍保留为显式关闭 MSAA 的开关。
- Apple 平台：Metal shim（M6）。Web(Emscripten)：WebGL（M5）。

## IME 移植要点（M13a）

- 事件面只有两个：`MY_EVENT_IME_PREEDIT`（组合串+caret，借用指针仅分发期有效）与 `MY_EVENT_IME_COMMIT`（提交文本）；都投给焦点控件。控件侧 edit/text_area 已接入（preedit 不入文档/撤销/changed；commit 走 user_insert 正常编辑路径）。
- 窗口 vtable `ime_set_spot(x, y)`（逻辑坐标）：控件在焦点/光标移动时上报，port 转换成物理坐标喂候选窗锚点；无 IME 的 port 打桩空函数。
- x11 参考实现 `my_pal_x11_ime.c`（XIM）：XOpenIM → 每窗口 XIC（优先 PreeditCallbacks 自绘预编辑，回落 PreeditNothing 只收提交）→ KeyPress 先 XFilterEvent 再 Xutf8LookupString（多字节结果=提交）→ FocusIn/Out 对 IC 聚焦。IM 不在时一切照旧。
- **手动验证步骤**（x11 + ibus）：`./build-c99/demos/demo_widgets/demo_widgets` → 点击文本框聚焦 → Ctrl+Space 激活 ibus → 打拼音选候选 → 提交：中文落进控件、可 Ctrl+Z 单步撤销；候选窗跟随光标（spot）。
- wayland 对应物是 text-input-v3 协议（已实现：pending 状态 done 前聚合，经 my_window_on_pal_event 转发到焦点控件）。

## 装饰/CSD 决策点（M16）

- pal vtable `needs_client_decoration`：**合成器/WM 给不给窗口装饰（SSD）**——给（x11 各 WM、windows/macOS）返回 false；不给（mutter 的 plain xdg-shell）返回 true，my_window 会自绘标题栏（拖动 + 关闭）。嵌入式/全屏 port（linux_fb 类）返回 false（根本没有装饰概念）。
- 返回 true 的 port 必须实现窗口 vtable `begin_move`：记录最近一次指针 button 的 serial/句柄，调入合成器的交互式移动（wayland 参考实现 = `xdg_toplevel_move`）。返回 false 的 port 打桩 noop 即可。

## 光标决策点（M21a）

- 窗口 vtable `set_cursor(win, my_cursor_t)`（ARROW/TEXT/HAND；末尾槽，NULL 安全 inline——无指针概念的 port（linux_fb 类全屏）打桩 noop 返回 NOT_SUPPORTED 或留 NULL）。语义层在 myui 分发器 hover 切换处统一调用，port 只管"把形状落到系统"。
- **桌面窗口系统**：x11 参考 = `XCreateFontCursor`（核心字体 XC_left_ptr/XC_xterm/XC_hand2）缓存 + `XDefineCursor`，pal destroy 时释放缓存。**wayland 注意**：合成器在每次 `wl_pointer.enter` 时重置指针图像——必须存最近 enter 的 serial 并在 enter 回调里用 `wl_pointer_set_cursor` 重设当前形状（参考实现 = wl_cursor_theme 按别名 fallback 序取主题光标，取不到静默留合成器默认）。
- 无窗口系统的 port 不实现也不影响上层（inline 包装返回 NOT_SUPPORTED，分发器静默跳过）。

## GL 窗口集成（M10c，可选）

新 port 要让 `my_window_enable_gl()` / `MYUI_DEMO_GLES=1` 工作，实现窗口 vtable 的 `gl_enable`：

- 返回 `my_pal_gl_t*`（vtable：`make_current` / `swap_buffers` / `get_size` / `destroy`）；首次调用创建、之后返回同一句柄；**句柄归窗口所有**（窗口 destroy 时释放，`my_pal_gl_destroy` 仅供提前释放且须把窗口里的指针置 NULL——双重释放安全，参考 x11/wayland port）。
- EGL 路线（x11/wayland 同款）：pal 级懒初始化共享 `EGLDisplay`+config（`EGL_WINDOW_BIT` + ES2，永不 eglTerminate），窗口级创建 context+window surface；wayland 额外要 `wl_egl_window_create`（resize 事件里同步 `wl_egl_window_resize`）。
- vsync：`eglSwapInterval(dpy, 1)`（make_current 后设置）；wayland 下 mesa 用合成器 frame 回调节流，语义与 shm 路径一致。
- 无 GL 的平台直接打桩返回 NULL，soft 路径零影响；`gl_window_smoke_test` 自动 skip。

## 构建选项

- `-DMYUI_PAL=auto|x11|dummy`（已实现；新 port 在此扩展）
- `-DMYUI_BIDI=ON|OFF`（M11a；嵌入式无 RTL 需求时 OFF，省 ~100KB 的 SheenBidi + 整形表——draw_text/measure 自动退化为原逻辑序路径，无需改应用代码）
- 交叉工具链文件 `cmake/toolchains/<platform>.cmake`（TODO，M5 起）
- 裁剪开关：日志级别、动画、XML 加载器（TODO）

## 参考

- `src/mypal/dummy/`：最小 port（约 300 行），先读懂它。
- `src/mypal/x11/`：真实桌面 port（窗口 + XImage 上屏 + select 主循环 + 键码表）。

## 平台移植路线图（M5 补充：各平台 shim 职责与预估）

### 通用结论（M5 已验证的接缝）

- **GL 上下文归 PAL 窗口管**：`my_vgcanvas_gles2_create(allocator, w, h)` 只要求调用方已有 current 的 GLES2 上下文（Android EGLContext、iOS EAGLContext、WebGL context 均可）。backend 的 GL 调用经 `my_gl_t` 函数表（`my_gl_real_default()`），换 WebGL 时也可注入替代实现。
- **系统调用归 `my_osal_t` 管**：参考 linux_fb port——注入假设备后整个 port 可 headless 单测，新 port 照此模式写。
- **窗口 lcd 归 `my_lcd_mem_create_from_buffer` 管**：直接把平台显存/mmap 缓冲包成 lcd，软件 backend 立即可用。

### Android（NDK port，M6，预估 2–3 天）

- shim：native-activity `android_main` + `ANativeWindow`（Java 边界全收敛在 port 内）；`android_native_app_glue` 事件泵 → `my_event_t`。
- 渲染：EGL + GLES2 已有 backend；`eglCreateWindowSurface(ANativeWindow)`。
- 事件：触摸 `AMOTION_EVENT_*` → POINTER_*；`AKEY_EVENT_*` → 键码表（照 `my_pal_x11_keymap` 模式）。
- 工具链：`cmake/toolchain-android.cmake`（未验证）。

### iOS（uikit port，M6，预估 2–3 天）

- shim：极薄 ObjC（UIViewController + CADisplayLink）只做系统桥接；`enable_language(OBJC)` 仅限 port 目录。
- 渲染：EAGLContext + CAEAGLLayer，present 在 `end_frame`（仿 x11 上屏路径）或直接用 gles2 backend。
- 事件：UITouch → POINTER_*；键盘外接 UIKeyCommand。
- 工具链：`cmake/toolchain-ios.cmake`（未验证）。

### HarmonyOS（NDK port，M6，预估 2–3 天）

- shim：XComponent (native window) + NativeVsync；ETS/JS 边界在 port 内。
- 渲染：EGL + GLES2 同 Android。
- 事件：OH_NativeXComponent 触摸回调 → POINTER_*。
- 工具链：`cmake/toolchain-harmonyos.cmake`（未验证）。

### Web（Emscripten port，M6，预估 1–2 天）

- shim：emscripten HTML5 API（canvas + 事件回调）；主循环用 `emscripten_set_main_loop` 驱动一帧一次的 pump（`my_pal_main_loop` 语义映射）。
- 渲染：canvas 的 WebGL1 上下文 = GLES2 语义，直接复用 gles2 backend（`my_gl_t` 可注入 emscripten GL）。
- 事件：mouse/touch/key 回调 → `my_event_t`。
- 工具链：`cmake/toolchain-emscripten.cmake`（未验证）。

### FreeBSD（M6 确认，未实机验证）

- x11/wayland port 的 CMake 选择逻辑与 Linux 完全一致（`MYUI_PAL=auto|x11|wayland`，顶层未做系统区分，FreeBSD 自然工作）；依赖用 `pkg install libX11 wayland wayland-protocols libxkbcommon`。
- 差异点：**linux_fb port 不适用**——FreeBSD 无 evdev（输入走 sysmouse/kbdmux 与 USB HID ioctl，接口不同）；framebuffer 设备为 `/dev/tty` + vt(4) 或 drm-kmod 的 KMS，需单独 port（M7+ 候选，建议直接走 wayland port + drm-kmod）。
- POSIX 层（`my_osal.c`）FreeBSD 兼容（open/ioctl/mmap/poll/read 均在）。

## 最小体积配置（M7a）

嵌入式/裸机裁剪路径：

```sh
cmake -S . -B build -DMYUI_FONT_STB=OFF -DMYUI_PAL=linux_fb
```

- `MYUI_FONT_STB=OFF`：去掉 stb_truetype 与 TTF 加载（`my_font_stb_create` 返回 NULL），文本全部走内置 8x8 位图字体（约 760 字节数据）。
- 渲染只保留 software backend + my_lcd_mem（或 from_buffer 直包显存）。
- 该配置下 dummy/linux_fb 测试与全部控件可用；无字体依赖、无文件系统依赖。

## 裁剪矩阵（M8a 更新）

| 选项 | 默认 | OFF 的效果 |
|------|------|-----------|
| MYUI_FONT_STB | ON | 去掉 TTF/stb_truetype，只留内置 8x8 位图字体 |
| MYUI_IMAGE_STB | ON | 去掉 stb_image 解码，image 控件显示占位框 |
| MYUI_UI_XML | ON | 去掉 XML parser + UI 加载器（代码建 UI，或用 ui2c 离线 XML→C，见下节） |
| MYUI_BIDI | ON | 去掉 SheenBidi + 整形表（~100KB），draw_text 退化逻辑序 |
| MYUI_FONT_FREETYPE | ON（无 freetype2 自动 OFF） | 去掉 FreeType hinted 后端，回落 stb |
| MYUI_BUILD_TESTS/DEMOS | ON | 构建裁剪 |

## 剪贴板实现要点（M8c 新增 port 须知）

pal vtable 的最后两项 `clipboard_set_text/get_text`：嵌入式/单窗口系统用内存字符串即可（照 dummy 10 行）；桌面系统注意 selection 是"惰性提供"协议（x11 参考实现含 SelectionRequest 应答样板 + M11b INCR 大数据分片；从外部获取需事件泵重入，建议照 x11 先实现本应用内往返）。**wayland（M12b）**：wl_data_device 已实现——set=wl_data_source+`set_selection(enter serial)`，get=selection offer+`wl_data_offer_receive` 到 pipe 同步读；注意握手依赖键盘焦点（enter serial / 焦点 selection 事件），无焦点环境下协议就绪但不生效，内存缓存兜底。

## 嵌入式零解析路径（M9d：XML→C 生成器）

不想带 XML parser 的目标：`MYUI_UI_XML=OFF` 编译框架（无 parser 代码），用宿主工具离线生成 C：

```sh
cmake --build build -t ui2c
./build/ui2c my_page.xml my_page_create > my_page.c   # 编进应用
# my_widget_t* w = my_page_create(MY_ALLOCATOR, pal);  与 my_ui_load_str 完全等价
```

tests/ui2c_sample.xml 的 golden 等价测试（运行时加载 vs 生成代码构建，逐节点递归比对）保证两条路径一致。

M24a 起两条路径由同一张控件类表（`src/myui/my_widget_class_builtin.inc`，X-macro 纯数据）驱动：生成代码的属性设置发射为 `my_widget_set_prop_str/int/float/bool` 调用（头文件 `myui/my_widget_class.h`，XML=OFF 裁剪档可用），新增控件/属性只需在该 `.inc` 加一行，loader 与 ui2c 同时生效。
