# myui

一个跨平台的纯 C(C99) GUI 框架（内置 MVVM 架构）。零第三方强制依赖：框架自身只依赖 libc 与各平台系统 API，可在 C99/C11/C17/C23 四档 `-Wall -Wextra -Werror -pedantic` 下零警告编译。

## 特性

- **分层架构**：myc 基础库 / mypal 平台抽象 / myr 渲染抽象 / myui 控件核心 / mymvvm MVVM，层间只经 vtable 接口通信
- **双渲染 backend**：软件光栅化（零依赖，5 种像素格式）+ GLES2（CPU 三角化，GL 调用经函数表隔离），同一冻结 vgcanvas 接口；全量 alpha 混合（src-over）+ 扫描线覆盖率抗锯齿（三级可开关：x4 / x4y2）；剪贴板（x11 selection + 内存实现）
- **字体系统**：stb_truetype（TTF + LRU 字形缓存）+ 内置 8x8 位图字体兜底（`MYUI_FONT_STB=OFF` 嵌入式裁剪），soft/GLES2 双 backend 文本渲染
- **国际化文本**：BiDi 重排（vendored SheenBidi，UBA 全算法）+ 阿拉伯字母整形（presentation forms，UCD 数据自研），纯 LTR 零开销快速路径，`MYUI_BIDI=OFF` 可裁剪；编辑控件 RTL 光标为 TODO
- **控件系统**：引用计数控件树、linear 布局（px/%/flex）、事件分发（grab/焦点）、主题样式（文本加载）、属性动画、脏矩形局部重绘；内置 button/label/edit（单行输入）/text_area（多行编辑）/checkbox/slider/progress_bar/list_view（虚拟化）/scroll_bar/image
- **编辑体验**：撤销/重做（批合并撤销栈）、Tab 焦点环、PageUp/Down、光标闪烁、剪贴板
- **MVVM**：data/command/items/condition 绑定、converter/validator、navigator、声明式规则字符串，UI 适配经 binding_target 抽象（base 可独立单测）
- **XML UI 加载器**：自研零依赖 parser + 控件工厂注册表，`v:*` 绑定/主题/布局内联，可裁剪（`MYUI_UI_XML=OFF`）
- **TDD**：自研 mytest 最小框架，35 个 ctest 全绿；golden-image 渲染回归；性能基线（50 按钮全帧 2.25ms）

## 平台状态

| 平台 port | 状态 | 验证 |
|-----------|------|------|
| dummy（headless） | ✅ 已验证 | 全部单测基础 |
| x11 | ✅ 已验证 | 单测 + 真机冒烟 |
| wayland（xdg-shell + wl_shm） | ✅ 已验证 | keymap 单测 + 真实合成器冒烟 |
| linux-fb | ✅ 编译通过 | my_osal_t 假设备单测；未实机（无 /dev/fb0） |
| GLES2 backend | ✅ 已验证 | mock GL 单测 + EGL 真实冒烟 + x11/wayland 真窗口 GL 渲染（`MYUI_DEMO_GLES=1`） |
| Windows（核心库+dummy） | ✅ CI 构建 | MSVC 构建+测试（CI） |
| macOS（核心库+dummy） | ✅ CI 构建 | CI |
| FreeBSD | ◐ 未实机 | x11/wayland 逻辑同 Linux，见 docs/porting.md |
| iOS/Android/HarmonyOS/Web/win32/sdl2/Metal | 📋 M7+ 规划 | 工具链文件与移植路线图已就位 |

## 构建

```sh
cmake -S . -B build -DMYUI_PAL=x11      # 或 dummy / linux_fb / wayland；auto 默认 x11→dummy
cmake --build build
ctest --test-dir build --output-on-failure
```

选项（`cmake -LH` 可查）：`MYUI_C_STANDARD=99|11|17|23`、`MYUI_PAL=auto|x11|dummy|linux_fb|wayland`、`MYUI_FONT_STB`、`MYUI_IMAGE_STB`、`MYUI_UI_XML`、`MYUI_BUILD_TESTS`、`MYUI_BUILD_DEMOS`。无 X11 时自动使用 dummy port（headless 可开发可测试）。

交叉编译：`cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-<android|emscripten|harmonyos|ios>.cmake`（未经 SDK 验证，M7+ 落实）。

## demos

- `demo_hello`：最小窗口 + 软件渲染场景
- `demo_widgets`：控件/布局/主题切换/动画
- `demo_mvvm`：MVVM 全绑定（计数器、items 列表、导航）

headless 验证：`MYUI_PAL=dummy` 构建后 `MYUI_DEMO_DUMP_PPM=out.ppm ./build/demos/demo_mvvm/demo_mvvm` 可渲染并导出 PPM。

## 文档

- [docs/architecture.md](docs/architecture.md) 分层与各层现状
- [docs/roadmap.md](docs/roadmap.md) 里程碑（M0–M6 已完成）
- [docs/mvvm.md](docs/mvvm.md) MVVM 语法与用法
- [docs/porting.md](docs/porting.md) 新平台移植指南
- API 参考：本地 `doxygen Doxyfile` → `docs/api/html/index.html`（CI docs job 同）
- CI：`.github/workflows/ci.yml`（gcc/clang × C99–C23、dummy、wayland、MSVC、macOS、doxygen）

## 目录结构

```
src/myc/         基础库（allocator/容器/事件/值/对象）
src/myr/         渲染（vgcanvas/lcd 接口，soft + gles2 backend）
src/mypal/       平台抽象（dummy/x11/wayland/linux_fb ports）
src/myui/        控件核心（widget/布局/事件/窗口/主题/动画/内置控件）
src/mymvvm/      MVVM 核心（GUI 无关）
src/mymvvm_myui/ MVVM → myui 适配层
tests/           mytest + 单测 + golden 基准 + bench
demos/           demo_hello / demo_widgets / demo_mvvm
cmake/           交叉工具链文件
docs/            架构/路线图/MVVM/移植指南/设计 spec
```
