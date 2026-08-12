# duanxianxia.com 首页复刻（apps/duanxianxia，dxx）

复刻依据：`docs/superpowers/specs/2026-08-04-duanxianxia-clone-design.md`（已批准）。
原站素材：HTML/CSS 静态快照（/tmp/dxx/{index.html,index.css,common.css,global.html}，抓取于 2026-08-04）。

## 进度

- **M14b（本期）**：应用骨架——顶栏（logo + 4 组下拉菜单 + 6 个平铺项 + 注册/登录）、指数条（12 列）、页脚（免责声明 + ICP）；站点配色 theme；快照数据表；字体 fallback 链。
- M14c（待做）：双列直播区 + 涨停股票池表格。
- M14d（待做）：导航切换/登录 dialog/分享图片交互、视觉逐项比对。

## 数据来源（如实）

指数条 12 项中 **5 项为真实抓取**（2026-08-12 14:20 CST，新浪 `hq.sinajs.cn`：上证指数 3942.37 / 深证成指 14372.94 / 创业板指 3586.28 / 上证50 2931.32 / 沪深300 4660.47；后两项的涨跌幅由昨收计算），其余 7 项（恒生/恒生科技/富时A50/中证2000/道指/纳指/COMEX黄金）该接口当日返回空，为**合理静态约值**，逐行标注于 `dxx_data.c`。

## 字体方案

- 系统无同时含 Latin+CJK 的 glyf 表 TTF：NotoSansCJK-VF.ttc 是 CFF2 可变字体（stb_truetype 不支持），DroidSansFallbackFull.ttf 无 Latin 字形。
- 为此 my_font_stb 新增**两个框架能力**（M14b）：TTC 首 face 支持（"ttcf" 头检测 + `stbtt_GetFontOffsetForIndex`）与 `my_font_stb_create_chain` **逐码点 fallback 链**（每个 codepoint 路由到第一个含该字形的 face）。
- dxx 使用链 `[DroidSansFallbackFull(CJK), LiberationSans(Latin)]`；都找不到则内置 8x8 位图字体兜底（中文变 tofu，仅 trim 构建出现）。

## 与原站差异点（累计）

1. **logo**：原站是 150px 图片（/static/img/logo6.png），复刻用白色 20px 伪粗体文字"短线侠"。
2. **下拉指示符**：原站是 bootstrap caret 图片/CSS 三角，复刻用字符 ▼（U+25BC，LiberationSans 含此字形；▾ U+25BE 两款字体都没有）。
3. **卡片阴影**：框架无 box-shadow 能力，用 1px #eee 边框模拟（指数条底边）。
4. **字号近似**：框架文本按 vgcanvas 当前字号绘制，顶栏项 14px / 指数名 12px / 数值 14px 与原站一致，但字体度量（Noto/Droid vs 原站系统字体栈）不同，间距为近似值（`dxx_text_estimate`：CJK=字号宽、ASCII=半宽）。
5. **注册/登录**：原站为 `注册 / 登录`（含斜杠分隔文本），复刻为两个独立可点项（斜杠省略）。
6. **指数数值行**：原站数值非粗体（仅红绿色），复刻用了伪粗体（+1px 二次绘制）增强可读性——视觉略有差异，如需严格对齐可去掉。
7. **搜索框**：原站顶栏有一个默认隐藏的搜索框（display:none），复刻未做。
8. **菜单项分隔线**：原站 dropdown-menu 项间有 divider 线；my_menu 当前无 divider 项类型，复刻菜单无分隔线（M14c/d 如需可加）。

## 交互现状（M14b）

- 下拉菜单：点击展开/点外收起可用（my_menu）；项点击仅打印日志（导航 M14d）。
- 平铺项/注册/登录：hover 高亮（M14a hover 态），点击打印日志。
- dummy dump：`MYUI_DEMO_DUMP_PPM=out.ppm ./build-dummy/apps/duanxianxia/dxx` 出主页 + /tmp/dxx_menu.ppm（复盘菜单展开）。
