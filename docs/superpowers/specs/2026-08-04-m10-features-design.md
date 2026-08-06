# M10 功能设计：撤销重做与键盘导航 / word wrap / 图像预降采样与 wayland-egl / GLES cap-join 收尾

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M9 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| 撤销/重做（edit、text_area） | 编辑控件工业可用性的关键 | 是 |
| 键盘导航（Tab 焦点链、PageUp/PageDown） | 无鼠标可用性 | 是 |
| text_area word wrap | 长文本可读性 | 是 |
| 图像盒式预降采样 | 缩小显示质量（最近邻锯齿/双线性慢） | 是 |
| wayland-egl 窗口（GLES 上真窗口） | GLES 在 wayland port 落地 | 是（有活合成器） |
| GLES round cap/join | GLES 与 soft 描边对齐 | 是（mock + EGL 冒烟） |
| 变高行动态行高失效 API | list_view 正确性补全 | 是 |
| INCR 增量剪贴板、shaping/Bidi、IME | 重工程/大依赖 | 部分，顺延 M11+ |
| win32/sdl2/Metal/移动/Web/FreeBSD 实机 | 需 SDK/设备 | 否，顺延 |

## 2. M10 范围与切分

- **M10a 撤销/重做 + 键盘导航**：通用 `my_undo_stack_t`（命令 = 前向/反向文本补丁（offset+删串+插串），合并相邻插入为一批，容量上限可配，溢出丢最老）；edit 与 text_area 接入（每次用户编辑入栈，程序 set_text 不入栈；Ctrl+Z/Y 或 Ctrl+Shift+Z）；键盘导航：Tab/Shift+Tab 焦点环（window 内 focusable 遍历）、list_view 与 text_area 的 PageUp/PageDown、scroll_bar 键盘支持。
- **M10b text_area word wrap**：`wrap` 属性（off/on）；视觉行模型——物理行偏移缓存 + 视觉行映射（wrap 开时按控件宽与 font measure 折行）；光标/选区/滚动基于视觉行；wrap 开时无水平滚动。行偏移缓存失效策略扩展。bench：万行 wrap 文档滚动帧耗。
- **M10c 图像盒式预降采样 + wayland-egl**：缩小时（scale<0.5）先做整数倍盒式平均降到 ≥0.5x 再走双线性（质量接近直接双线性、速度数倍提升——bench 对比三档：nearest/bilinear/box+bilinear，像素与帧耗双断言）；wayland-egl：wayland port 窗口可选 GLES 模式（wl_egl_window + EGL window surface，wayland-egl 库探测），x11 已有 GLX/EGL 吗——若 x11 上 GLES 也未接窗口则本期两个 port 都补"PAL 窗口 → GL 上下文"挂载点，demo 可用 `--gles` 跑真窗口。
- **M10d GLES round cap/join + 行高失效 + 收尾**：GLES 端 cap/join 几何生成（圆盘三角扇）与 soft 对齐；`my_list_view_invalidate_row_heights()`（psum 缓存失效重建）；INCR 剪贴板顺延；文档/bench/roadmap 总收尾。

## 3. 性能与效果平衡

- undo 栈按批合并（连续打字一条目），默认上限 100 批；嵌入式可关 `MYUI_UNDO=OFF`？不增裁剪项，栈本身开销极小。
- wrap 模式视觉行缓存随编辑局部失效；bench 决定是否需要可视区缓存。
- 盒式预降采样只做 2 的幂次降档（2/4/8），避免任意比例盒滤的复杂度。
- wayland-egl 为运行时可选（窗口创建参数），soft 路径零影响。

## 4. TDD 与文档

- undo 命令栈全路径（合并/上限/编辑交错/redo 失效）；Tab 焦点环顺序；wrap 视觉行断言（折行点、光标跨视觉行）；盒式采样像素断言；wayland-egl 实机冒烟（活合成器）；GLES cap/join mock 顶点断言。
- 四档 C 标准零警告 + ctest 全绿，按 M10a–M10d 逐个 commit，最终统一 push。
- 文档：architecture（undo/wrap/wayland-egl）、roadmap（勾选+bench）、porting、README 随码更新。
