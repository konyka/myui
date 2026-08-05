# M8 功能设计：XML UI 加载器 / 列表虚拟化与图片 / 渲染质量与剪贴板 / MVVM 注册开放

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M7 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| XML UI 加载器 | 声明式 UI，设计与代码分离，MVVM 的 v:* 规则可写进 XML | 是 |
| 列表虚拟化（list_view） | 大数据列表性能，items 绑定的工业化能力 | 是 |
| image 控件（stb_image） | 真实应用必需 | 是（stb 本地有） |
| y 向 AA / stroke AA | 渲染质量收尾 | 是（像素断言 + golden） |
| 剪贴板（PAL 接口 + x11）+ edit Ctrl+C/V/X | 输入体验闭环 | 是（x11 实跑 + dummy） |
| converter/validator 开放注册 | MVVM 扩展性 | 是 |
| 光标闪烁、多行编辑、IME、shaping | 打磨/重工程 | 部分，顺延 M9 |
| win32/sdl2 port、Metal、移动/Web 实机 | 平台覆盖 | 否（无 SDK/依赖，顺延） |

## 2. M8 范围与切分

- **M8a XML UI 加载器**：自研最小 XML parser（零依赖，元素/属性/文本/注释/转义，不做 DTD/命名空间全集）；widget 工厂注册表（window/button/label/edit/checkbox/slider/progress_bar + 布局属性 + 主题内联 `style` 段）；`v:*` 属性直接进 widget `bind_rules`（MVVM 天然衔接）；`my_ui_load_str/my_ui_load_file`；demo 改为 XML 驱动一个页面。性能：加载一次性解析建树，编译期可选 `MYUI_UI_XML`（嵌入式可裁剪，用 XML→C 生成器思路留 TODO）。
- **M8b list_view 虚拟化 + image 控件**：`my_list_view`（可视区行裁剪 + 行回收复用，固定行高先行，变高 TODO）；items 绑定对接虚拟化（rebuild_items 改为按需建可视行，滚动增量更新）；`my_image` 控件 + stb_image 后端（vendored 3rd/stb，可选 `MYUI_IMAGE_STB`）支持 png/jpg 解码到 lcd 格式 blit。
- **M8c 渲染质量 + 剪贴板**：y 向 AA（扫描线升级为 4x4 或 y 向 2 级覆盖率，bench 决定取舍）；stroke 折线 AA；PAL 剪贴板接口（get/set_text）+ x11 实现（selection 协议）+ dummy 实现 + edit 的 Ctrl+C/X/V；edit 光标 500ms 闪烁（timer 驱动 invalidate）。
- **M8d MVVM 开放注册 + 收尾**：converter/validator 自定义注册 API（按名引用进规则字符串）；文档总收尾（架构/移植/mvvm/README/roadmap）；bench 刷新。

## 3. 性能与效果平衡

- XML 只在启动/开页时解析，不进热路径；提供 `MYUI_UI_XML=OFF` 裁剪。
- 虚拟化把列表渲染从 O(N) 降到 O(可视行)；items 绑定不再全量重建（保留全量模式作 fallback，小列表无回归）。
- y 向 AA 以 bench 数据定默认：若路径场景开销 >2.5x 则默认关（x 向 AA 已覆盖大部分视觉收益），文档记录数据。
- image 解码一次性入缓存（按路径键 LRU），绘制走 draw_pixels 快速路径。

## 4. TDD 与文档

- 每子里程碑先测试后实现；XML parser 用全套畸形输入用例；虚拟化用行数/复用计数断言；剪贴板 dummy 往返 + x11 实跑（有 DISPLAY）；AA 像素区间断言 + 新 golden。
- 四档 C 标准零警告 + ctest 全绿后按 M8a–M8d 逐个 commit，最终统一 push。
- 文档随码更新：architecture（XML 加载器、list_view、image、剪贴板）、mvvm（XML 中的 v:* 绑定、注册 API）、porting（裁剪矩阵补 MYUI_UI_XML/MYUI_IMAGE_STB）、roadmap（勾选 + bench 数值）、README（特性表）。
