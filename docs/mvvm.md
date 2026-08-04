# myui MVVM（mymvvm）

> M4a 已完成：GUI 无关核心（src/mymvvm/，只依赖 myc）。Items 绑定、条件绑定、navigator、myui 适配层在 M4b。

## 分层与解耦

```
应用 Model/ViewModel (实现 my_view_model_t 契约)
        │  "prop:<name>" / "props" 通知 (my_emitter)
my_binding_context_t ── 按规则字符串绑定 ── my_binding_target_t (vtable)
        │                                        │
   view_model (属性/命令)                  UI 端点（M4b: widget；测试: mock）
```

binding_context 不直接操作控件，只通过 `my_binding_target_t` vtable（set_prop/get_prop/on_event/off_event）对接 UI——base 因此可在无 GUI 环境完整单测。更新分发是**同步**的（不排队）。

## view_model 契约（my_view_model.h）

- vtable：`get_prop(name, &value)` / `set_prop(name, &value)` / `can_exec(cmd, args)` / `exec(cmd, args)`（后两者可空）。
- 变更通知约定：属性变化后调用 `my_view_model_notify_change(vm, "name")` → 发射 `"prop:<name>"`；`name == NULL` 发射 `"props"`（批量）。
- 生命周期：继承 my_object_t 引用计数；子类析构链 `my_view_model_destroy → my_object_destroy`。
- `my_view_model_dummy_create()`：通用属性包实现（set 自动通知），支持 `my_view_model_dummy_add_command` 注册命令。

## 绑定规则语法（EBNF 简表）

```
rule      = "v:" widget_prop "={" vm_prop [ "," option ]* "}"
widget_prop = ident | "on_" ident            ; on_ 前缀 = 命令绑定
option    = "Mode=" ("OneWay" | "TwoWay" | "Once")
          | "Converter=" ident               ; 内置: upper lower int_to_str bool_negate
          | "Validator=" ident [ "(" args ")" ]  ; 内置: not_empty, range(min,max)
          | "Args=" text                     ; 命令参数
          | "CloseWindow=" ("true"|"false")  ; 由 M4b 适配层处理
```

示例：`v:text={name, Mode=TwoWay, Converter=upper}`、`v:value={age, Validator=range(0,150)}`、`v:on_click={save, Args=btn1}`。
`v:items={...}` 与 `Condition=` 已识别，暂返回 `MY_RET_NOT_SUPPORTED`。

## 绑定模式

| 模式 | 初始同步 | vm→target 实时 | target→vm 回写 |
|------|---------|---------------|----------------|
| OneWay（默认） | ✓ | ✓（`prop:<name>` 监听） | — |
| TwoWay | ✓ | ✓ | ✓（target `"changed"` 事件） |
| Once | ✓ | — | — |

回写路径：`convert_back` → `Validator` 校验 → **失败则拒绝**：vm 不变、target 被恢复为 vm 当前值（测试固化此语义）。Converter 正向用于推送。命令绑定：target 事件（`on_click` → `"click"`）触发，先 `can_exec` 再 `exec(args)`。

手动全量同步：`my_binding_context_update_to_view()` / `update_to_vm()`（后者只处理 TwoWay）。

## 端到端示例（文字）

```c
my_view_model_t* vm = my_view_model_dummy_create(NULL);
my_binding_context_t* ctx = my_binding_context_create(NULL, vm);
/* target 由 M4b 的 widget 适配器提供，此处用任意 my_binding_target_t 实现 */
my_binding_context_bind(ctx, target, "v:text={user_name, Mode=TwoWay, Validator=not_empty}");
my_binding_context_bind(ctx, target, "v:on_click={save, CloseWindow=true}");
```

用户在输入框键入 → target 发 `"changed"` → 校验通过 → 写回 `vm.user_name`；vm 侧属性变化 → 推送回 target。点击按钮 → `can_exec("save")` → `exec`。

## 自定义 converter/validator

实现 `my_value_converter_t`（convert/convert_back + ctx）或 `my_value_validator_t`（is_valid/fix + ctx）委托即可；注册表式按名引用目前只覆盖内置项，自定义实例可通过 M4b 的代码 API 直接挂接（TODO：开放注册表）。

## M4b：items / 条件 / 导航 / myui 适配层

### items 绑定

```
v:items={persons, ItemTemplate=person_row}
```

- 绑定的 vm 属性必须是 `MY_VALUE_POINTER`，指向一个 `my_view_model_array_t*`（vtable：get_count/get_item/insert/remove/clear + 内嵌 emitter 发 `"items_changed"`；`my_view_model_array_dummy_create()` 是通用实现）。
- 行为：bind 时全量构建一次，之后任何 `"items_changed"` 都**全量重建**（虚拟化 TODO）。行属性由 base 层从每行子 view_model 实时读取，经 `rebuild_items(template, count, props_cb, ctx)`（binding_target vtable 第 5 项）交给 UI。
- 数组对象本身被替换（vm 重设该属性）时自动重新订阅并重建。

### 条件绑定

```
v:visible={Condition=is_admin}
v:visible={Condition=!is_admin}
```

bool 求值（bool/int/非空字符串），`!` 取反；属性变化即推送。只单向（vm→target）。

### 导航

```
v:on_click={goto, ToPage=detail, Args=Id={id}}
v:on_click={back, CloseWindow=true}
```

- `ToPage`：命令触发时构造 `my_navigator_request_t{MY_NAV_TO, target, args}` 交给默认 navigator（`my_navigator_set_default`）；Args 中 `{prop}` 在执行时替换为当前 vm 属性值。
- `CloseWindow=true`：由适配层处理——命令执行后关闭当前窗口（经 window_manager）。
- myui 实现 `my_navigator_wm_t`：TO 查页面工厂注册表建窗并 open、BACK 关顶窗、HOME 回首页、REPLACE 替换顶窗。

### myui 适配层（库 mymvvm_myui）

- `my_widget_target_create(widget)`：属性映射 `text`（button/label）/ `visible` / `enable` / `x,y,w,h` / `value`（通用槽）；事件走 widget emitter；rebuild_items 清空容器并按模板工厂重建子控件。
- 模板注册表：`my_mvvm_register_template("person_row", builder, ctx)`；builder 签名 `(parent, index, props_cb, props_ctx, builder_ctx) -> my_widget_t*`。
- `my_mvvm_bind(wm, win, vm)`：遍历控件树，对带 `bind_rules`（`my_widget_set_bind_rules`，多条用 `;` 分隔）的控件逐个建 target 并应用规则；返回的 `my_mvvm_context_t` 负责销毁全部 binding 与 target。
- 注意：行内删除等"事件触发即重建列表"的操作要**延迟执行**（post/timer），否则会销毁正在分发事件的控件（demo_mvvm 用 1ms 定时器示范）。

### demo_mvvm 逐段讲解

- 计数器：label `v:text={count, Converter=int_to_str}`（OneWay，int 自动转字符串）；`+1`/`-1` 按钮 `v:on_click={inc}/{dec}` → dummy vm 命令改 count → 自动推送刷新。
- 列表：`v:items={persons, ItemTemplate=person_row}`；模板把每行建成一个按钮（文字来自行 vm 的 `name`）；行内点击经定时器延迟删除该人 → `items_changed` → 自动重建。
- 导航：`v:on_click={goto, ToPage=detail}` 打开 detail 页；detail 页按钮 `v:on_click={back, CloseWindow=true}` 关闭自己返回。
- 全 demo 没有一行手动刷新 UI 的代码。
