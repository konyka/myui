# M21 设计：光标体系 + 节点编辑器第二批增强

日期：2026-08-14　状态：已批准执行

## 1. Bug 修复（M21a）

1. **光标不对（变成输入光标）**：框架 PAL 层从未实现鼠标光标管理——合成器/根窗口默认光标不受控。新增 PAL 光标 API：`my_pal_window_set_cursor(win, my_cursor_t)`（ARROW/TEXT/HAND/RESIZE_H？先做 ARROW/TEXT/HAND 三种）。wayland：wl_cursor_theme（"default"→arrow, "text"→I-beam, "pointer"→hand）+ set_cursor(serial 用最近 pointer enter serial)；x11：XCreateFontCursor(XC_left_ptr/XC_xterm/XC_hand2)；dummy：记录断言。语义：edit/text_area hover→TEXT；button/菜单/链接/节点接口 hover→HAND；其余→ARROW（默认基线在分发器 hover 切换处统一处理）。
2. **小地图底部裁剪**：CSD 模式下 my_window_widget 返回内容容器（高 864 而非 900），小地图 overlay 坐标若按窗口高 900 计算，底部 36px 被容器 clip 掉。修：小地图定位一律相对 overlay（容器）rect 计算；回归测试覆盖 CSD 容器高度差场景。
3. **流动画过快**：1.5px/33ms(≈45px/s) → 0.5px/tick(≈15px/s)，dash 6/4 → 8/6 更舒缓；步长可 CSS 调（`node_link.flow_speed`？不做键，常量即可，注释）。

## 2. 增强（M21b）

4. **节点尺寸自适应内容**：节点不指定 w/h（传 0）时按内容计算：宽 = max(标题宽, 最宽接口行, 内嵌控件宽) + 边距；高 = 标题栏 + 接口行数×行高 + 内嵌控件高。显式尺寸优先。
5. **连线箭头 + 类型着色**：贝塞尔末端画小箭头（沿切线方向三角）；连线默认色取**源接口类型色**（socket type_color，有主题键时主题优先），多类型连线一眼可辨。
6. **磁吸环 vs 选中描边层级**：选中描边线宽 2 → 1；磁吸环线宽 2 且最后绘制（在选中描边之上）；磁吸环颜色键已存在 `node_socket.magnet`。

## 3. 验证与文档

- 光标：wayland 实机验证（edit hover I-beam、按钮 hand、其余 arrow——如实汇报）；x11/dummy 构建测试；小地图 CSD 裁剪回归；流动速度断言（假时钟 offset 步进）；自适应尺寸计算断言；箭头几何；层级绘制顺序（rec_vgcanvas）。
- dump 目检；四档+wayland/dummy/trim 全绿；文档（architecture/css/porting 光标 API）+ roadmap + commit/push。
