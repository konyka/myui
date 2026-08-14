# M19 设计：Blender 风格节点编辑器（连连看）

日期：2026-08-14　状态：已批准执行

## 1. 目标与参考

Blender 着色器节点编辑器核心体验（见参考图）：节点盒（标题栏+主体+左右接口圆点）、贝塞尔连线（水平切线）、拖接口连线、节点可拖动。每个部分颜色可配置（主题/CSS）。

## 2. 缺口分析

- vgcanvas 只有 move_to/line_to——**无贝塞尔曲线**（连线必需）：新增 `curve_to`（三次贝塞尔），soft 后端自适应细分折线化（走现有覆盖率 AA 路径），GLES 后端同样三角化细分；接口冻结式扩展（vtable 末尾加槽）。
- 无节点编辑器控件——新建 `my_node_view`（画布）+ `my_node`（节点）。

## 3. 里程碑

- **M19a vgcanvas curve_to**：`my_vgcanvas_curve_to(vg, cx1,cy1,cx2,cy2,x,y)`；soft：de Casteljau 自适应细分（平坦度容差）→ 折线走 stroke 条带化 AA 路径；gles2：同细分→三角扇；rec mock 测试 + golden（一条 S 曲线目检）+ bench（细分开销）。
- **M19b 节点编辑器**（src/myui/widgets/my_node_view.h/.c + my_node.h/.c）：
  - 数据模型：节点（id/标题/x/y/w/h/类别）+ 接口（输入左/输出右，类型色点）+ 连线（out 节点+槽 → in 节点+槽，输入槽唯一——新连线替换旧连线）。
  - my_node：标题栏（类别色背景+标题文字）+ 接口行（左输入圆点+名，右输出圆点+名）+ 主体可嵌控件（滑条等，第一版只放 label/接口行）；拖标题栏移动节点（grab，移动 invalidate 连线）。
  - 连线绘制：out 接口右缘 → in 接口左缘，三次贝塞尔水平切线（handle dx = max(40, |dx|*0.5)），走 curve_to。
  - 交互：从输出接口拖出 → 实时预览连线（跟随光标）→ 落在输入接口上松手连接（替换旧连线）；拖到空白取消。点选连线高亮（选中态色），Del 删选中连线（键盘可达性）。
  - **颜色全可配**（主题/CSS，验证 CSS 命中）：`node_view`（画布底色）、`node`（主体）、`node .header`（标题栏——后代选择器）、`node_socket.input/.output`（接口色）、`node_link`（连线色 + 选中态）、`node_link.preview`（预览态）——正好用上 M18 的 class/后代/CSS。
  - 测试：连线模型（连接/替换/删除）、拖动位移、接口命中、预览连线、CSS 各部件命中断言、泄漏。
- **M19c 演示与收尾**：demos/demo_nodes（复刻参考图局部：Principled BSDF 大节点 + Mapping/Color/Environment 小节点 + 滑条 + 连线）；dummy dump 目检；文档（architecture/css.md 选择器表扩充/roadmap/README）；commit + push。

## 4. 平衡

- 贝塞尔细分自适应（曲率驱动），连线绘制只走可视区裁剪；节点数 <100 场景零压力。
- 画布平移（拖空白）支持，缩放留 TODO（接口不排除）。
