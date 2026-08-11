# M12 功能设计：编辑控件 RTL / shaping 完善与 wayland 剪贴板 / 双线性整数化与 HiDPI / 断行与收尾

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M11 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| 编辑控件 RTL（光标/点击/选区的视觉-逻辑映射） | RTL 输入闭环（M11a 只做了展示） | 是（Noto 阿/希字体 + dummy port） |
| Lam-Alef 合字、UBA L4 镜像括号 | 阿拉伯排版正确性 | 是（Naskh 字体字形断言） |
| wayland 剪贴板协议（wl_data_device） | wayland port 功能对齐 x11 | 是（活合成器；或双 wl 连接互测） |
| 双线性采样器整数化 | 盒式尾部性能（上次 11.96ms 的 ~4.8ms 尾部） | 是（bench） |
| HiDPI 基础（scale factor 接口 + 检测 + 渲染缩放） | 高分屏可用性 | 部分（接口+dummy 可测，真高分屏无） |
| UAX#14 实用子集（CJK 断行 + 标点规则） | 中日韩 wrap 质量 | 是（折行断言） |
| INCR 并发多传输、IME、竖排 | 边角/重工程 | 部分，INCR 并发可做；IME/竖排顺延 |

## 2. M12 范围与切分

- **M12a 编辑控件 RTL**：edit/text_area 光标逻辑位 ↔ 视觉位双向映射（复用 M11a text_layout 的视觉-逻辑映射表，按 run 方向处理）；左右方向键按**视觉方向**移动（RTL run 内左键=逻辑前移）；点击定位经视觉 x → 逻辑 offset；选区绘制按 run 分段。测试用阿/希字体 + 位图字体混合串。
- **M12b Lam-Alef 合字 + UBA L4 镜像 + wayland 剪贴板**：整形模块补 Lam-Alef 强制合字（U+FEFB 等）；UBA L4：RTL 级内配对括号/尖括号镜像替换；wayland port 实现 wl_data_device 剪贴板（set：offer UTF-8 mime；get：request + 读 fd，同步事件泵），双 wl 连接互测（一设一取）。
- **M12c 双线性整数化 + HiDPI 基础**：bilinear 采样器改定点 16.16（bench 目标尾部 ~4.8ms 减半；像素等价容差 ±1）；PAL 增加 `get_scale_factor()`（x11 读 Xft.dpi/物理 DPI 计算，wayland 用 wl_output scale 事件——简化：x11 按 DPI/96 取整，wayland 取 buffer scale，dummy 可注入）；渲染层 scale=1 时零开销直通，>1 时字体/布局坐标按 scale 放大（先做"逻辑坐标不变、绘制与字体放大"的最小闭环，控件度量按逻辑坐标）。
- **M12d UAX#14 子集 + INCR 并发 + 收尾**：断行规则子集——CJK 表意文字间可断、CJK 后禁断标点（，。！？）行首禁止、英文连字符处可断、数字与字母间不断；优先级表实现 + 测试向量；INCR 并发传输（发送端多 requestor 状态机）；文档/bench/roadmap 总收尾。

## 3. 性能与效果平衡

- RTL 映射表复用 text_layout LRU 缓存，编辑控件只在文本变更时重算。
- 双线性整数化目标：盒式+双线性总帧耗 11.96ms → <9ms（-O0）；像素容差 ±1。
- HiDPI scale=1 直通零开销；scale>1 时文本走字号放大（位图缓存按放大字号分桶，已天然支持）。
- UAX#14 子集为查表驱动（Unicode 区间 → 断行类），每 codepoint O(1)。

## 4. TDD 与文档

- RTL 光标：双向映射往返断言（逻辑序遍历→视觉位单调性按 run）、键盘方向语义、选区分段矩形。
- Lam-Alef：ل + ا 序列输出 U+FEFB 区码点；L4：RTL 段 "(" 视觉显示为 ")"。
- wayland 剪贴板：双连接往返 + 空数据/无 selection 边界。
- 四档 C 标准零警告 + ctest 全绿，按 M12a–M12d 逐个 commit，最终统一 push。
- 文档：architecture（RTL 编辑、镜像、HiDPI 模型）、porting（wayland 剪贴板）、roadmap（勾选+bench）、README。
