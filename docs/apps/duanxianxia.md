# duanxianxia.com 首页复刻（apps/duanxianxia，dxx）

复刻依据：`docs/superpowers/specs/2026-08-04-duanxianxia-clone-design.md`（已批准）。
原站素材：HTML/CSS 静态快照（/tmp/dxx/{index.html,index.css,common.css,global.html}，抓取于 2026-08-04）。

## 进度

- **M14b**：应用骨架——顶栏（logo + 4 组下拉菜单 + 6 个平铺项 + 注册/登录）、指数条（12 列）、页脚（免责声明 + ICP）；站点配色 theme；快照数据表；字体 fallback 链。
- **M14c**：双列直播区（情绪直播 750x800 + 涨停直播/异动/股票池/成交额四卡，feed 关键词红绿高亮、内嵌 scroll_view 滚动）；涨停股票池晋级表（表头 + 分享图片按钮 + 6 行 130 项完整快照，股票项 = 市场徽标 + 状态色名称 + [涨幅] + 题材，flow 流式换行 + 行高自适应）；整页 scroll_view 滚动（右侧 scroll_bar）；股票项 hover 高亮 + tooltip + 点击弹个股卡片 dialog；分享图片弹模拟 dialog。**框架修复**：`my_window_on_pal_event` 此前漏路由 `MY_EVENT_POINTER_WHEEL`（真实事件路径滚轮从不生效，M14c 测试暴露，已修）。
- **M14d**：导航切换（顶栏全部可点项 → 占位面板：16px 标题 + 灰副文本 + 返回首页按钮；logo/涨停表现回首页；当前项 #E64C62 变色模拟 bootstrap active）；登录/注册 dialog（edit hint/password + MVVM 全链路：TwoWay + not_empty validator + submit 命令 + 红字错误提示，注册多确认密码一致性检查）；分享图片**真实导出**（晋级表离屏渲染 lcd_mem → BGRA→RGBA → stb_image_write PNG，默认 ztpool_share.png，路径可注入，dialog 报真实文件名）。
- **M15（本期）**：情绪直播面板统计区 + 走势图——副标题（居中 16px bold）、3 行 × 4 统计按钮（btn-sm 风格：橙/红/绿彩底白字 + 白底 #ccc 边框，上涨家数数字红/下跌家数数字绿）、涨幅分布柱状图（左 1/4 宽，红绿竖条）+ 主折线图（白底卡片、5 条浅灰网格、y 轴三档刻度、x 轴 09:30-15:00 时间刻度、#E64C62 2px 折线 AA）；点击 情绪指标/涨停家数/跌停家数 切换主图曲线 + 当前按钮加深底色（×0.85）；量能按钮仅日志占位。**数值与曲线全部为模拟快照**（dxx_data.c 注明；原站走 echarts + 动态接口）。Wayland/x11 双 port 实跑冒烟通过（各 3 秒无协议错误）。

### M15 追加差异

13. **走势图实现**：原站用 echarts（canvas 富交互），复刻为 vgcanvas 自绘折线/柱状——网格/刻度/样式近似，无 echarts 的悬浮十字线/缩放交互。
14. **统计数值**：原站动态接口实时计算，复刻为模拟快照（情绪指标 62 / 涨停 72 / 跌停 3 / 亏钱效应 0.31 / 主力流入 -182亿 / 连板高度 7 / 上涨 3102 / 下跌 1856 / 封板率 71% / 昨涨停 +2.34% / 昨连板 +3.12%）。
15. **按钮配色**：bootstrap btn-warning/danger/success/default 标准色（#F0AD4E/#D9534F/#5CB85C/白+#ccc），与原站一致；active 加深为 ×0.85 近似 bootstrap active 态。

## 视觉逐项比对（M14d 定稿）

依据 = /tmp/dxx/ 静态快照（index.html 结构 + index.css/inline 色值）。一致 = 结构+色值+内容对齐；近似 = 形式对齐、度量/外观有差。

| 区域 | 原站依据 | 状态 | 说明 |
|------|----------|------|------|
| 顶栏 #444 50px | index.html:212 `background:#444;height:50px` | 一致 | 含全部 4 组下拉 + 6 平铺项 + 分隔线 + 注册/登录 |
| 下拉 4 组项数 | index.html:222-270 | 一致 | 竞价2/挖掘3/复盘4/热点2；divider 线差异见清单#8 |
| 平铺项颜色 | inline style: 看盘插件 orange、PC端版面 yellow bold | 一致 | 其余白色 14px |
| 当前项 active | bootstrap `.active` 类 | 近似 | 原站 active 样式由 bootstrap 决定，复刻用主红 #E64C62 变色 |
| 指数条 12 列 | global.html:24-99 + red/green inline | 一致 | 名称/数值/涨跌幅三行，红涨绿跌；数值 5 项真实快照 |
| 双列 750/530 | index.html 直播区结构 | 一致 | 间距 20；四卡高 400/160/430/250 |
| 卡片标题栏 | 各面板标题 + 分隔线 | 近似 | 框架无边框阴影，1px #eee 边代替（清单#3） |
| 晋级表 6 行 | 任务书快照（2026-08-04 完整数据） | 一致 | 表头 进度60/晋级率85、单元格 1px #ddd、徽标/成败炸三色/题材灰字、flow 换行 |
| 分享图片按钮 | 表头右侧红色按钮 | 一致 | #D9534F 白字；导出为真实 PNG（原站为 canvas 合成图） |
| 股票 tooltip/个股卡片 | 原站 layer.js 弹层 | 近似 | 框架原生 tooltip/dialog 样式（清单#12） |
| 登录/注册弹窗 | layer.js 表单 | 近似 | my_dialog + edit + MVVM；原站为 layui 表单样式 |
| 页脚两行 | index.html:1110-1111 | 一致 | 免责声明 + ICP 照抄 |
| 整页滚动 | 原站页面流式高度 | 一致 | 内容 ~2600px，scroll_view + scroll_bar |

## 运行与截图清单

```sh
cmake -S . -B build-dummy -DMYUI_PAL=dummy && cmake --build build-dummy -j
MYUI_DEMO_DUMP_PPM=/tmp/dxx_home.ppm ./build-dummy/apps/duanxianxia/dxx
# 产出：dxx_home(首屏) dxx_menu(复盘菜单) dxx_subpage(占位页) dxx_login(登录)
#       dxx_chart_zt(切换涨停家数曲线) dxx_pool(晋级表) dxx_first_board(首板行)
#       dxx_tooltip dxx_dialog(个股卡) dxx_share(分享提示)
#       + 当前目录 ztpool_share.png(真实导出 1300x~900)
```

目检结论（M14d 全 10 场景 PNG 过 ReadMediaFile）：顶栏布局/菜单展开/指数红绿/直播区双色关键词/晋级表六行徽标配色/tooltip 文本/个股卡数据/登录表单/占位页/分享导出均与预期一致。

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

### M14c 追加

9. **市场徽标色**：原站徽标颜色由 JS 运行时设置，静态快照无法取得；复刻用近似值（沪 #E64C62 / 深 #347DFA / 创 #EC971F / 科 #9B59B6 / 北 #17A2B8）。
10. **直播/异动/股票池/成交额内容为模拟**：原站这些数据走动态接口，快照无静态文本；晋级表为 2026-08-04 真实完整快照（dxx_data.c 注明）。
11. **晋级表行高**：复刻按 flow 内容自适应（原站 CSS 同理）；股票项宽度为估算值（`dxx_stock_item_width`），与原站逐项像素级宽度有偏差。
12. **tooltip/dialog 为框架原生样式**（my_tooltip 深色浮层 / my_dialog 模态窗），与原站 layer.js 弹窗外观不同。

## 交互现状（M14d 定稿）

- **导航**：顶栏全部可点项（4 组下拉逐项 + 6 平铺项）切换到占位面板（标题 + 副文本 + 返回首页）；logo/涨停表现回首页；当前项主红 #E64C62 高亮。
- **登录/注册**：模态 dialog（用户名/密码 edit，hint + password 模式；注册多确认框）；MVVM 全链路（TwoWay 文本 + not_empty validator + submit 命令 + 红字错误提示"用户名不能为空/密码不能为空/两次密码不一致"）；无后端，成功提交打印日志。
- **分享图片**：真实导出晋级表 PNG（离屏渲染 + stb_image_write，默认 `ztpool_share.png`），dialog 报文件名。
- 直播 feed：关键词红绿高亮，wheel 滚动（内层吃掉不冒泡，单测钉死）。
- 晋级表：hover 浅灰 + tooltip（真实数据），点击弹个股卡片 dialog。
- 整页滚动：内容 ~2600px，右侧 scroll_bar 双向同步。
- dummy dump 10 场景（见下节）。

## 差异点（追加）

- ~~涨幅分布图为竖条红绿~~（M17 起改为与原站一致的 echarts 横向分布条形：类别左、数值右、跌停 #C4E7CF/跌档 #4FB771/平盘 #ACB0C0/涨档 #E5562C；数据仍为模拟快照）
