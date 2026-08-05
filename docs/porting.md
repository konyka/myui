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
- 有 GLES2+：GLES backend（M5）。
- Apple 平台：Metal shim（M6）。Web(Emscripten)：WebGL（M5）。

## 构建选项

- `-DMYUI_PAL=auto|x11|dummy`（已实现；新 port 在此扩展）
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
| MYUI_UI_XML | ON | 去掉 XML parser + UI 加载器（代码建 UI 或留 XML→C 生成器 TODO） |
| MYUI_BUILD_TESTS/DEMOS | ON | 构建裁剪 |

## 剪贴板实现要点（M8c 新增 port 须知）

pal vtable 的最后两项 `clipboard_set_text/get_text`：嵌入式/单窗口系统用内存字符串即可（照 dummy 10 行）；桌面系统注意 selection 是"惰性提供"协议（x11 参考实现含 SelectionRequest 应答样板；从外部获取需事件泵重入，建议照 x11 先实现本应用内往返）。

## 嵌入式零解析路径（M9d：XML→C 生成器）

不想带 XML parser 的目标：`MYUI_UI_XML=OFF` 编译框架（无 parser 代码），用宿主工具离线生成 C：

```sh
cmake --build build -t ui2c
./build/ui2c my_page.xml my_page_create > my_page.c   # 编进应用
# my_widget_t* w = my_page_create(MY_ALLOCATOR, pal);  与 my_ui_load_str 完全等价
```

tests/ui2c_sample.xml 的 golden 等价测试（运行时加载 vs 生成代码构建，逐节点递归比对）保证两条路径一致。
