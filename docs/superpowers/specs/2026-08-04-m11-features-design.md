# M11 功能设计：BiDi 与阿拉伯整形 / INCR 剪贴板与窗口级 undo / GLES AA 与盒式优化 / 对齐与描边收尾

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M10 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| BiDi（阿拉伯/希伯来 RTL 重排）+ 阿拉伯字母整形 | 国际化的最后硬骨头 | 是（SheenBidi 源码在 awtk 3rd；Noto Arabic/Hebrew 字体齐全） |
| INCR 增量剪贴板（x11 大数据传输） | 剪贴板协议完备性 | 是（fork 子进程模拟大数据 owner） |
| 跨控件 undo 管理器（窗口级共享栈） | 编辑体验一致性 | 是 |
| GLES AA（MSAA） | GLES 渲染质量对齐 soft | 是（EGL samples 配置 + 读回） |
| 盒式 pass 优化（滑动窗口/积分图） | 大图缩略帧耗 | 是（bench） |
| justify 两端对齐、stroke 关节单轮廓合并 | 排版/描边收尾 | 是 |
| IME、win32/sdl2/Metal/移动/Web/FreeBSD 实机 | 平台工程 | 否，顺延 |

## 2. M11 范围与切分

- **M11a BiDi + 阿拉伯整形**：vendored SheenBidi-3.0.0（自 ~/opensource/awtk/3rd/，ISC 许可，3rd/SheenBidi，编译选项 `MYUI_BIDI` 默认 ON，独立 TU 放宽警告）；`my_text_layout` 模块：逻辑序 UTF-8 → 段落方向解析（首强字符）→ UBA 重排（视觉序）→ 阿拉伯整形（presentation forms 映射经 SheenBidi 的 shaping）；draw_text 接入：测量与绘制统一走"整形+重排后 codepoint 序列"，RTL 段落默认右对齐起点；测试用 Noto Naskh Arabic / Noto Sans Hebrew（缺字体 skip）：阿拉伯三字母单词的整形映射（独立形→词首/中/尾形）断言、混合英文+阿文的视觉序断言、measure/draw 一致性。edit/text_area 的光标按视觉-逻辑映射（本期可只做 label/button/draw_text 级，编辑控件的 RTL 光标映射列 TODO——如实定边界）。
- **M11b INCR 剪贴板 + 窗口级 undo**：x11 剪贴板发送端：数据 > 阈值（如 64KB）时走 INCR 分片应答；接收端：遇 INCR 属性循环收取分片。fork 子进程扮演大数据 owner 测试。窗口级 undo 管理器：`my_window_undo_manager`（共享栈 + 焦点控件路由 Ctrl+Z/Y），edit/text_area 可选接入共享栈（默认仍私有，API 切换）。
- **M11c GLES MSAA + 盒式优化**：GLES 端 AA——EGL config 带 EGL_SAMPLES=4（探测失败回落无 AA 并注释）；真窗口与 pbuffer 冒烟边缘像素中间值断言；`my_vgcanvas_gles2_set_antialias` 语义落地。盒式 pass 优化：行/列滑动窗口累加分摊（O(src) → O(src) 但常数大降，或积分图——bench 说话，目标 2000x1500→400x300 从 ~29ms 降到 <10ms）。
- **M11d 对齐与描边收尾 + 文档**：text_area/label 水平对齐（left/center/right/justify——justify 仅视觉行非末行拉伸词间距）；stroke 关节单轮廓合并（消除半透明描边关节过混合）；文档/bench/roadmap 总收尾。

## 3. 性能与效果平衡

- BiDi/整形结果按 (font,size,text) LRU 缓存（默认 64 项），measure/draw 共享；纯 LTR 文本快速路径零开销（无 RTL 字符直接旁路）。
- MSAA 4x 由 EGL 承担，零 CPU 开销；失败平台回落关 AA。
- 盒式优化后仍 O(src)，但去掉每目标像素重扫描；bench 数据定最终策略。

## 4. TDD 与文档

- 整形/重排逐 case 断言（已知 Unicode 测试向量）；INCR 分片计数与完整性；undo 管理器路由；MSAA 边缘像素；盒式优化结果与旧实现逐像素等价（容差 1）；justify 词距断言。
- 四档 C 标准零警告 + ctest 全绿，按 M11a–M11d 逐个 commit，最终统一 push。
- 文档：architecture（text_layout/BiDi、INCR、MSAA）、porting（MYUI_BIDI 裁剪）、roadmap（勾选+bench）、README（RTL 支持）。
