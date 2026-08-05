# M9 功能设计：多行编辑 / 图像质量与 GLES 补齐 / 滚动条与变高列表 / XML→C 与剪贴板收尾

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M8 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| 多行文本编辑（text_area） | 输入控件闭环最后一块 | 是（dummy port 全测） |
| GLES draw_image（RGBA 纹理） | GLES backend 功能对齐 soft | 是（EGL 冒烟） |
| 双线性缩放（soft image） | 图像显示质量 | 是（像素断言 + bench） |
| 滚动条拖拽 + 滚动条控件化 | list_view 可用性 | 是 |
| 变高列表 | 真实业务列表 | 是 |
| stroke 圆 cap/join | 渲染细节收尾 | 是（像素断言） |
| XML→C 生成器 | 嵌入式免解析路径，裁剪闭环 | 是（生成+编译+比对） |
| x11 从外部应用取剪贴板 | 剪贴板协议补全 | 部分（需外部 owner，可用第二进程模拟） |
| shaping/Bidi、IME、win32/sdl2/Metal/移动/Web | 重工程/需 SDK | 否，顺延 M10+ |

## 2. M9 范围与切分

- **M9a text_area 多行编辑**：行模型（darray 存行起点偏移，单一 UTF-8 缓冲）；光标 row/col、四方向移动（上下移动保持目标列）、Enter 拆行、Backspace 合行；选区（Shift+方向/Ctrl+A）与 Ctrl+C/X/V（保留换行）；垂直滚动保光标可见；绘制走 draw_text 逐行；word wrap 不做（留 TODO，水平滚动）。复用 edit 的字体/主题/焦点机制；emit "changed"。
- **M9b 图像质量**：GLES draw_image（RGBA 纹理上传 + quad，纹理按路径/像素缓存复用 image 的 LRU 结果，mock-GL 与 EGL 冒烟）；soft 双线性缩放（fit/fill 路径可切换 nearest/bilinear，默认 bilinear 仅在缩小时？——bench 定：放大双线性视觉收益大，缩小最近邻质量差明显，默认全开 bilinear，bench 记录开销）；`my_image_set_scale_filter`。
- **M9c 滚动条 + 变高列表 + cap/join**：`my_scroll_bar` 独立控件（拖拽滑块、点击轨道翻页、value=scroll 比例），list_view 接入（可关）；list_view 支持变高行（adapter 增 row_height(index)，可视行计算用前缀和缓存，行数大时分段缓存策略，bench 验证）；stroke 圆 cap/join（round 模式：cap 画半圆、join 画圆，走覆盖率路径自动 AA；`set_line_cap/join` 接口，butt/miter 保持现状）。
- **M9d XML→C 生成器 + 剪贴板收尾**：tools/ui2c（读 XML 输出等价 C 构建代码，含 style 段与 v:* 规则；golden 比对：同一 XML 运行时加载树 vs 生成代码构建树，逐节点属性断言）；x11 外部剪贴板获取（向 owner 发 SelectionRequest + 事件泵同步等待，重入安全；测试用第二个 X 连接扮演外部 owner 设置 selection——无需真实外部应用）；文档与 bench 总收尾。

## 3. 性能与效果平衡

- text_area 行偏移缓存 + 变更局部失效；长文档（10k 行）光标移动 O(1) 摊销。
- 双线性缩放 bench 驱动默认策略；缩小时可考虑先做 2x 盒式预降采样（TODO，数据说话）。
- 变高列表前缀和缓存分段失效；bench 对比固定行高回归。
- GLES 纹理缓存与 image LRU 打通，避免双份像素内存。

## 4. TDD 与文档

- text_area 键盘状态机全路径（含 UTF-8 多字节跨行）；变高列表可视区间断言；双线性像素公式断言；cap/join 像素覆盖率断言；ui2c golden 树比对；剪贴板双 X 连接往返。
- 四档 C 标准零警告 + ctest 全绿，按 M9a–M9d 逐个 commit，最终统一 push。
- 文档：architecture（text_area/scroll_bar/ui2c）、mvvm（text_area 绑定）、porting（XML→C 嵌入式路径）、roadmap（勾选 + bench）、README。
