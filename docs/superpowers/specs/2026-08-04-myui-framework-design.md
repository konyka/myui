# myui — 跨平台 C GUI 框架（MVVM）总体设计与实施计划

## 1. 目标与原则

从零设计并实现 `myui`：纯 C 跨平台 GUI 框架，覆盖 Windows / Linux / FreeBSD / macOS / iOS / Android / HarmonyOS / 嵌入式(裸机与 RTOS、Linux-FB) / Web(WASM)，内置 MVVM 架构，性能与效果最佳平衡，全程 TDD，文档与代码同步维护。

参考代码（只读参考，不拷贝大段代码，借鉴分层与接口设计）：
- `~/opensource/awtk`：整体分层（tkc 基础库 / hal / lcd / vgcanvas / widgets / 平台 ports）
- `~/opensource/awtk-mvvm`：MVVM 核心（binding_context、data/command/items_binding、view_model、value_converter/validator、navigator）— 结构清晰、GUI 可移植，是 myui MVVM 层的主要蓝本
- `~/opensource/awtk-linux-fb`：嵌入式 Linux-FB 移植方式
- `~/opensource/blender-git/blender`：GHOST 式平台窗口抽象与立即模式 UI 事件分发思路

设计原则：
- **依赖为零**：框架自身只依赖 libc 与各平台系统 API；第三方库（如需要）仅作为可选 backend，编译期可裁剪。
- **C99 基线**：代码以 C99 编写，并在 CI 上以 C99/C11/C17/C23 四档 `-std=` 严格编译（`-Wall -Wextra -Werror -pedantic`），保证"兼容到最新标准"。
- **接口即隔离**：层与层之间只通过头文件中的抽象接口（结构体 + 函数指针表，awtk 式 `vtable` 惯例）通信，可单测、可替换。
- **TDD**：每个模块先写测试（自研最小测试框架 `mytest`，约 150 行宏），测试先行（红→绿→重构）；平台相关代码用 dummy/mock backend 在无显示环境跑通单测。

## 2. 分层架构

```
┌─────────────────────────────────────────────────────┐
│ 应用层 (demos / examples)                             │
├─────────────────────────────────────────────────────┤
│ mymvvm   MVVM：view_model / binding_context /        │
│          data_binding / command_binding /            │
│          items_binding / value_converter / validator │
│          / navigator（参考 awtk-mvvm，GUI 无关核心 +   │
│          myui 适配层）                                 │
├─────────────────────────────────────────────────────┤
│ myui     控件核心：widget 基类 / 布局器 / 窗口管理器 /   │
│          事件分发 / 动画 / 主题样式 / XML UI 加载器      │
├─────────────────────────────────────────────────────┤
│ myr      渲染抽象：vgcanvas 2D 矢量接口 + lcd 帧缓冲接口 │
│          backend：software(自研光栅化) / GLES2/GL3 /    │
│          Metal(shim) / WebGL(WASM) —— 运行时/编译期可选 │
├─────────────────────────────────────────────────────┤
│ mypal    平台抽象层：窗口/事件主循环/输入法/剪贴板/       │
│          定时器/线程/文件系统/电源                      │
│          ports：win32, x11|wayland, cocoa, uikit,     │
│          android-ndk, harmonyos-ndk, sdl2(快速移植),  │
│          linux-fb(裸机/RTOS), emscripten(web)          │
├─────────────────────────────────────────────────────┤
│ myc      基础库：内存(池/arena)/容器(darray, slist,     │
│          hash)/str(utf8)/value/emitter(事件)/object/   │
│          错误码/日志/时间（≈ awtk 的 tkc）              │
└─────────────────────────────────────────────────────┘
```

关键取舍（性能 vs 效果）：
- 渲染默认走 **software backend（自研光栅化，参考 lcd_mem_* 的像素格式特化套路：RGB565/RGB888/ARGB8888/BGRA/Mono 各自特化 + dirty-rect 局部刷新）**，保证嵌入式与零依赖可跑；有 GPU 的平台编译进 GLES/Metal/WebGL backend，同一份 vgcanvas 调用代码零改动切换。
- 动画与重绘以 **dirty-rect + vsync/timer 驱动**，避免全帧重绘。
- MVVM 绑定更新走 **属性变更通知（emitter）+ 惰性求值**，避免轮询。

## 3. 目录结构（仓库根 = /home/timeshift/opensource/myui）

```
myui/
├── CMakeLists.txt              # 顶层：选项 MYUI_BACKEND_* / MYUI_PAL_* / MYUI_BUILD_TESTS
├── cmake/                      # toolchain: harmonyos, emscripten, ios, android, mingw
├── docs/
│   ├── architecture.md         # 本文档的沉淀版
│   ├── roadmap.md
│   ├── porting.md              # 新平台移植指南
│   ├── mvvm.md                 # 绑定规则语法文档
│   ├── api/                    # Doxygen 输出（CI 生成）
│   └── superpowers/specs/      # 设计 spec（brainstorming 产物）
├── src/
│   ├── myc/                    # 基础库
│   ├── mypal/                  # PAL 接口 + 各平台 ports/
│   ├── myr/                    # vgcanvas/lcd 接口 + backends/
│   ├── myui/                   # 控件/窗口/布局/动画/主题
│   └── mymvvm/                 # base(GUI 无关) + myui 适配
├── tests/                      # mytest.h + 每模块 *_test.c，ctest 驱动
├── demos/                      # demo_hello / demo_mvvm / demo_widgets
└── tools/                      # 资源打包、XML→C 代码生成（后续里程碑）
```

## 4. 构建与平台策略

- **CMake ≥ 3.20** 唯一构建系统（awtk 的 SCons+CMake 双轨教训：只维护一套）。
- 平台通过 `MYUI_PAL=<port>` + 工具链文件选择：
  - Windows: MSVC/MinGW(win32 port)；Linux/FreeBSD: x11 port 起步，wayland 后续；macOS/iOS: cocoa/uikit port（.m 薄 shim，仅系统桥接处用 ObjC，框架本体仍纯 C）；Android/HarmonyOS: NDK 工具链 + GLES backend；嵌入式: linux-fb 或裸机 port + software backend；Web: Emscripten → WASM + WebGL backend。
  - 另提供 **sdl2 port** 作为"任何平台 1 天跑起来"的保底后端（桌面调试也方便）。
- CI（GitHub Actions，后续里程碑接入）：Linux GCC/Clang × C99/C11/C17/C23 矩阵 + Windows MSVC + macOS Clang + WASM 构建；单测在 ctest 下全绿。

## 5. MVVM 设计（核心蓝本 awtk-mvvm，GUI 无关层 + 薄适配）

- **mymvvm/base**（不依赖 myui，可独立测试与移植）：
  - `view_model`：属性 get/set、`can_exec`/`exec` 命令、变更通知 emitter。
  - `binding_rule` + parser：声明式绑定规则字符串，如 `v:text={name, Converter=upper}`、`v:on_click={save}`、`v:items={list, ItemTemplate=item}`。
  - `binding_context`：持有一组 rule，对 view_model 属性变化做分发；data/command/items/condition 四类绑定。
  - `value_converter` / `value_validator`：委托模式（delegate，函数指针 + ctx）。
  - `navigator`：窗口导航请求（To/Back/Home…）。
- **mymvvm/myui 适配**：`binding_context_myui` 把规则应用到 widget 属性与事件；`ui_loader_mvvm` 在 XML 加载时提取 `v:*` 属性。

## 6. TDD 策略

- `tests/mytest.h`：最小框架（`MYTEST_ASSERT_*`、`MYTEST_RUN(suite)`、失败打印文件/行号、返回失败数），零依赖，可跑在嵌入式宿主模拟与 WASM node 环境。
- 每个模块：`tests/<module>/<unit>_test.c`，CMake 注册进 ctest；先写测试再写实现。
- 平台相关层用 dummy backend（如 `lcd_mem` + 虚拟窗口）保证 90% 测试可在无显示 CI 上运行。
- 性能基线：M3 起加 `tests/bench`（帧率/内存），每次发布前对比。

## 7. 里程碑（每步测试先行，全部 ctest 绿后进入下一步）

- **M0 地基（本计划的执行范围）**
  1. 脚手架：顶层 CMake、选项、mytest.h、ctest 接入、`demo_hello` 空壳；四档 C 标准本地编译验证。
  2. myc 第一批（TDD）：错误码/日志 → mem(allocator 接口 + 默认 malloc 实现 + 计数调试分配器) → darray → str(utf8 安全) → emitter(事件) → value(动态类型) → object(引用计数基类)。
- **M1 渲染**：vgcanvas 接口冻结 → software backend（RGB565/RGB888/ARGB8888/BGRA/Mono + dirty-rect）→ lcd 帧缓冲接口 → 离屏渲染 golden-image 测试。
- **M2 PAL + 窗口**：PAL 接口冻结 → sdl2 port（快速全平台验证）→ linux x11 port → win32 port → 事件/主循环/定时器。
- **M3 控件核心**：widget 基类/布局/窗口管理器/事件分发/主题/动画 + demo_widgets + 性能基线。
- **M4 MVVM**：mymvvm/base 全部（TDD，纯逻辑可充分单测）→ myui 适配 → demo_mvvm。
- **M5 平台铺开**：macOS(cocoa)/Linux-FB/Android(NDK)/Web(Emscripten+WebGL)/GLES backend。
- **M6 收尾**：iOS(uikit)/HarmonyOS(NDK)/FreeBSD 验证/Wayland/Metal backend/文档与 API 参考完善/CI 全矩阵。

## 8. 文档维护

- `docs/architecture.md`、`roadmap.md`、`porting.md` 随代码里程碑同步更新（每次里程碑收尾必查）。
- `docs/mvvm.md` 在 M4 写绑定语法规范。
- 头文件注释即 Doxygen 源，CI 生成 `docs/api/`。
- 本设计沉淀为 `docs/superpowers/specs/2026-08-04-myui-framework-design.md`（实施第一步提交）。

## 8.5 备选方案对比

- **方案 A（本计划，推荐）：自研渲染抽象 + 自研基础库（AWTK 路线）**。零第三方依赖、嵌入式/裸机可裁剪、长期可控；代价是 M1 自研光栅化工作量较大。
- **方案 B：SDL2 + NanoVG 快速起步**。窗口与 2D 渲染直接站在成熟库上，M1–M3 明显更快出效果；代价是引入两个外部依赖（裸机/某些 RTOS 不可用，需额外裁剪），且底层性能调优空间受限于库的抽象。MVVM、控件、文档、TDD 流程与方案 A 完全相同，只是 myr/mypal 的首个实现换成 SDL2/NanoVG 包装，后续仍可补自研 backend。

## 9. 风险与对策

- 范围极大 → 严格按里程碑切片，M0–M4 是"可开发应用"的最小闭环，平台端口逐里程碑增加而非一次铺开。
- ObjC/Java/ETS 桥接不可避免 → 全部收敛在 mypal 对应 port 的少量 shim 文件，框架本体保持纯 C。
- 渲染性能 → 接口冻结时即写 bench；software backend 用像素格式特化 + dirty-rect；GPU backend 后置但接口先行，避免返工。
