# M7 功能补全设计：字体/输入控件/Alpha 与抗锯齿/基础控件

日期：2026-08-04　状态：已批准执行

## 1. 缺口盘点（M0–M6 之后）

| 缺口 | 影响 | 本机可验证 |
|---|---|---|
| 字体系统（draw_text 全链占位） | 所有文本无法显示，button/label/edit 都依赖 | 是（系统 TTF + golden） |
| 文本输入控件 edit | MVVM TwoWay 表单无法真正落地 | 是（dummy port + 键事件） |
| 无 alpha 混合/抗锯齿 | 圆角/文字锯齿明显，视觉效果差 | 是（像素断言 + bench） |
| 基础控件稀少（仅 button/label） | demo 与真实应用差距大 | 是 |
| win32/sdl2 port、Metal、移动/Web 平台实机 | 平台覆盖 | 否（无 SDK/依赖，继续顺延） |
| XML UI 加载器 | 声明式 UI | 是，但本期不做，顺延 M8 |

## 2. M7 范围与切分

- **M7a 字体系统**：`my_font` 抽象（度量/字形位图）+ stb_truetype 后端（3rd/stb 单头，public domain，编译期可裁剪 `MYUI_FONT_STB`）+ 内置 8x8 位图字体兜底（零依赖、嵌入式可只用它）+ 字形缓存（LRU，上限可配）。soft backend 实现 draw_text（alpha 位图 blit，等 M7c 混合就绪后自动获益）；gles2 backend 实现 draw_text（字形位图上传纹理 + quad 批渲染）。
- **M7b edit 控件**：单行输入（文本缓冲/光标/左右移动/Home/End/退格/删除/选中全选/点击定位），复用 stb_textedit？不——自己写最小实现（stb_textedit 接口偏重）；焦点与键盘事件已就绪；emit "changed" 供 MVVM TwoWay；demo_mvvm 增加真实输入表单。
- **M7c alpha + 抗锯齿**：soft backend 所有填充走 src-over 混合（my_color_t 的 a 通道生效）；路径/圆角/文字边缘 AA 采用**扫描线覆盖率**（每像素 4x 子采样沿 x 方向 accumulate，面积解析计算，非超采样整帧，性能可控）；AA 为运行时开关（默认开），bench 对比开/关帧耗。GLES2 端：开启 GL_BLEND src-over；AA 由顶点覆盖率或 MSAA 留 TODO，本期 GLES 端只补混合。
- **M7d 基础控件**：checkbox（三态？只做两态+mixed 字段）、slider（拖动取值，value 属性 TwoWay 友好）、progress_bar；demo_widgets 扩充；全部接入主题。

## 3. 性能与效果平衡策略

- 字形缓存避免重复光栅化；同字号同字体 LRU 256 项默认。
- AA 只对矢量边缘做覆盖率计算，实心矩形填充零额外开销；dirty-rect 限定重绘区域。
- bench_render 增加：文本场景、AA 开/关对比场景，数值写入 roadmap；回归上限保持宽松。
- 嵌入式裁剪路径：`MYUI_FONT_STB=OFF` + 内置位图字体 + AA 关闭 = 最小体积配置，CMake 选项固化并在 porting.md 记录。

## 4. TDD 与文档

- 每个子里程碑先写测试：字体度量/缓存行为/文本 blit 像素断言（用系统 LiberationSans TTF，缺字体时测试自动跳过并提示）；edit 状态机全部键盘路径；混合像素级公式断言（手工算期望值）；AA 边缘像素覆盖率区间断言；新控件状态与值语义。
- 四档 C 标准（99/11/17/23）零警告 + ctest 全绿后提交，按 M7a–M7d 各一个 commit，全部完成后推送。
- 文档：architecture.md（font 子系统、混合/AA 说明）、roadmap.md（M7 各项勾选 + bench 数值）、porting.md（最小裁剪配置）、mvvm.md（edit TwoWay 示例）、README（特性表更新）。
