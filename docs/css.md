# myui CSS 子集规范（M18a，src/myui/my_css.h/.c）

把 CSS 子集样式表桥接进主题系统：`my_theme_load_css(theme, css_str)`。与既有文本格式共存（同键后写覆盖先写）。

## 选择器

| 形态 | 支持 | 说明 |
|------|------|------|
| `type` | ✅ | 匹配 widget_type（如 `button`） |
| `.class` | ✅ | 匹配 widget `style_class` 的单词（widget 可带多个 class） |
| `#id` | ✅ | 匹配 widget name |
| `type.class` / `type#id` | ✅ | 复合 |
| 伪类后缀 `:hover` `:pressed` `:disabled` | ✅ | 无伪类 = 只写 NORMAL 槽（state→normal 回落覆盖其余状态——pseudo 规则恒胜裸规则，见下） |
| `A B` 后代 | ✅ 简化 | A 必须是裸类型；B 匹配且**任一祖先**类型为 A 即中（不记录完整路径） |
| 逗号分组 | ✅ | `a, b { ... }` |
| `A > B` 直接子代 | ❌ 报错 | |
| `.class#id`（无类型的 class+id 组合）/ 一个选择器多 class / 多 id | ❌ 报错 | |
| 其他伪类 / `*` 通用选择器 | ❌ 报错 | |
| `@media`/`@import` 等 @规则 | 整块跳过 + MY_LOGW 告警 | |

## 声明值

| 形态 | 类型 | 说明 |
|------|------|------|
| `#rgb` `#rrggbb` `#rrggbbaa` | UINT32 (rgba32) | |
| `rgb(r,g,b)` `rgba(r,g,b,a)` | UINT32 | a 支持 0-1 浮点与 0-255 整数 |
| 命名色 red/green/blue/white/black/gray/grey/orange/yellow/purple/pink/cyan/transparent | UINT32 | |
| `Npx` / 整数 | INT32 | px 后缀直接去掉 |
| 浮点 | DOUBLE | |
| `"..."`/`'...'` 引号串 | STR | |
| 其他标识符 | STR | 原样透传（如 `bold`） |

## 键别名

`background-color`/`background`→`bg_color`、`color`→`fg_color`、`border-color`→`border_color`、`border-width`→`border_width`、`border-radius`→`round_radius`、`font-size`→`font_size`；其余键原名直传（bg_color 等框架原生键直接可用）。

## 级联优先级

`#id` > `.class` > `type`；每级内 state→normal 回落；后代选择器附加祖先类型条件。**伪类比裸规则更具体**（specificity 优先于 source order：裸规则只写 NORMAL 槽，伪类写对应状态槽，style 内部 state→normal 回落完成其余）；同级同键后写覆盖先写（source order）。与文本格式共存：同键后写覆盖。

## 错误模型

- 结构错误（选择器不合法/缺 `{}`/未闭合注释/@规则未闭合）→ 硬错误，`my_css_error_t{line,col,msg}`。
- 声明级问题（缺冒号/坏值）→ 跳过该声明 + MY_LOGW，规则其余部分存活（宽松模式，文档化）。

## 与文本格式共存 / 迁移示例（M18b，dxx 实证）

`<style>` 块按内容路由：含 `{` → CSS（`my_theme_load_css`），否则旧文本格式——两个 `<style>` 块可并存。widget 加 `class="..."`（XML 属性 / `my_widget_set_style_class`）。

dxx 的 `dxx_theme_create` 迁移片段（站点色值不变）：

```css
window { background-color: white }
label { background-color: white; color: #333333 }
label.muted { color: #999999 }           /* 页脚 label class="muted" */
.danger { background-color: #D9534F; color: white }
.danger:hover { background-color: #C9302C }  /* 分享按钮 class="danger" */
```

注意 specificity 语义：`.danger:hover` 恒胜 `.danger`（裸规则只写 NORMAL 槽，伪类写状态槽，级内 state→normal 回落），与真实 CSS 一致。像素级验证：迁移前后 dummy dump 首屏逐像素 diff = 0；页脚灰色化/分享按钮主题化为有意变更（dump 目检 + 单测断言）。

## 节点编辑器部件选择器（M19b 实证）

```css
node_view { background-color: #101010 }       /* 画布底色 */
node { background-color: #202020 }            /* 节点主体 */
node.shader .header { background-color: #663300 }  /* 标题栏：类别 class + 后代 */
node_socket.output { background-color: #00FF00 }   /* 接口圆点（虚拟部件） */
node_link { color: #FF00FF }                  /* 连线 */
node_link.selected { color: #E0A030 }         /* 选中态 */
node_link.preview { color: #70C0E8 }          /* 拖线预览态 */
```

header/socket/link 不是真 widget——node/node_view 绘制时经 `my_widget_part_color(owner, type, class, state, key, fallback)` 查主题（owner 含自身作后代锚点），回退值为模型自带色（接口类型色）或内建默认。
