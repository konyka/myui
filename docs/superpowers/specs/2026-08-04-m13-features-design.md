# M13 功能设计：IME 输入法 / 盒式行缓冲与 RTL 行级重排 / 复合控件 / 收尾

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M12 之后）

| 缺口 | 价值 | 本机可验证 |
|---|---|---|
| IME（输入法：预编辑/提交/候选区定位） | 中文等 CJK 输入的最后一公里 | 是（ibus+libpinyin 在跑，XMODIFIERS=@im=ibus，XIM 可实机验证） |
| 盒式 pass 行缓冲滑动窗 | 大图缩小帧耗再降（M12c 遗留 ~9ms） | 是（bench） |
| wrap+RTL 混排的视觉行级重排 | RTL 长文本换行正确性 | 是 |
| 复合控件：dialog（模态/按钮区）、menu/context_menu、tooltip | 真实应用刚需 | 是 |
| fractional-scale、多屏/RandR、竖排、自由合字、UAX#14 全规则 | 边际完善 | 部分/否，顺延 |
| SDK 平台项 | 平台移植 | 否，顺延 |

## 2. M13 范围与切分

- **M13a IME（X11 XIM）**：x11 port 建 XIM 连接（XOpenIM，XMODIFIERS 探测）+ 每焦点控件 IC（XCreateIC，preedit callbacks）；PAL 事件扩展：预编辑串事件（IME_PREEDIT，含文本+光标位）与提交事件（IME_COMMIT，UTF-8 文本——复用 KEY 通道之外独立事件类型）；edit/text_area 接入：preedit 显示（下划线样式，不入撤销栈）、commit 插入文本（入撤销栈、走 changed 事件与 MVVM）、光标区定位（IC 的 spot location 跟随光标，候选窗跟随）；无 IM 环境自动回落（XOpenIM 失败=纯键盘路径，现有测试零回归）。实机验证：x11 demo 窗口聚焦 edit，用 ibus-libpinyin 真实打字提交中文（自动化可行则做，否则手动步骤文档化 + 代码路径用 fake IM 事件单测）。wayland text-input 协议留 TODO。
- **M13b 盒式行缓冲滑动窗 + wrap RTL 行级重排**：盒式平均改为行缓冲滑动窗口（目标 2000x1500→400x300 总帧耗 <8ms，-O0），像素级等价；wrap+RTL：视觉行重排按段落方向逐行应用（视觉行内 run 重排已在字符级完成，行级=RTL 段落折行后视觉行内顺序与对齐修正），测试向量固化。
- **M13c 复合控件**：`my_dialog`（模态窗口封装：标题+内容区+按钮区，open/close/结果回调，基于 window_manager 模态标志）；`my_menu`/`my_context_menu`（菜单项列表、弹出定位、级联子菜单基础、点击外关闭）；`my_tooltip`（悬停延迟显示、跟随或固定位置）；主题接入；XML 标签注册；MVVM 属性映射。
- **M13d 收尾**：文档/bench/roadmap 总收尾。

## 3. 性能与效果平衡

- IME 路径零开销于无 IM 环境（连接失败即旁路）；预编辑串不重排撤销栈。
- 盒式行缓冲：内存 O(行宽×档数)，目标帧耗数据说话。
- 复合控件复用现有 widget/主题/动画，无新渲染路径。

## 4. TDD 与文档

- IME：fake IM 事件（dummy 构造 IME_PREEDIT/IME_COMMIT）全逻辑单测 + x11 实机（可用则自动化注入，否则代码路径覆盖 + 手动验证步骤写 porting.md）。
- 复合控件：状态机/模态/弹出定位/级联/悬停延迟（假时钟）。
- 四档 C 标准零警告 + ctest 全绿，按 M13a–M13d 逐个 commit，最终统一 push。
- 文档：architecture（IME 管线、复合控件）、porting（IME 移植要点、手动验证步骤）、roadmap（勾选+bench）、README。
