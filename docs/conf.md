# myconf 配置文件支持（src/myc/myconf/，并入 myc 库）

## 格式总览

| 格式 | 读 | 写 | 状态 |
|------|----|----|------|
| JSON | 全集（RFC 8259） | 紧凑 + pretty（缩进 2） | M17a ✅ |
| BSON | 常用类型全集（映射表见下） | object/array/string/int64/double/bool/null | M17a ✅ |
| TOML | 常用子集（键值/[table]/[[array]]/行内） | 经 JSON 兼容导出 | 下一批（M17b） |
| YAML | 缩进子集（key:/- item/flow） | 不做 | 下一批（M17b） |

## 文档树（my_conf）

节点类型：`MY_CONF_NULL/BOOL/INT64/DOUBLE/STR/OBJECT/ARRAY`。OBJECT 保插入序；标量统一 INT64/DOUBLE（BSON int32 读入即归一 INT64）。

```c
my_conf_node_t* doc = my_conf_new_object(NULL);
my_conf_object_set(doc, "name", my_conf_new_str(NULL, "dxx"));
my_conf_object_set(doc, "retry", my_conf_new_int64(NULL, 3));
my_conf_node_t* arr = my_conf_new_array(NULL);
my_conf_array_push(arr, my_conf_new_bool(NULL, true));
my_conf_object_set(doc, "flags", arr);

/* 点路径：数字段在 ARRAY 上是下标，在 OBJECT 上是字面键 */
int64_t v = my_conf_get_int64(doc, "retry", -1);          /* 3 */
my_conf_node_t* f0 = my_conf_get(doc, "flags.0");          /* true */

char* s = my_conf_to_json_str(NULL, doc, true);            /* pretty */
my_conf_save_file(doc, "/tmp/cfg.json");
my_conf_destroy(doc);
```

错误：`my_conf_error_t{line, col, offset, msg}`——JSON 带 1 基行列，BSON 带字节偏移。

边界：点路径里含 `.` 的 object 键不可达（文档化限制）；数组段必须全数字。

## JSON 子集说明

全集。数字规则：纯整数→INT64（strtoll，ERANGE 溢出回落 DOUBLE——文档化）；含小数/指数→DOUBLE。字符串全转义 + `\uXXXX` 代理对；非 ASCII UTF-8 直出/直入。畸形输入（尾逗号/裸 NaN/前导零/错误转义/残缺代理对/超深嵌套 64 层）全部带位置报错。序列化时整数值的 DOUBLE 打印为 `%.1f`（如 2.0）以保住类型往返。

## BSON 类型映射

| 字节 | BSON 类型 | 树类型 |
|------|-----------|--------|
| 0x01 | double | DOUBLE |
| 0x02 | utf8 string | STR |
| 0x03 | embedded document | OBJECT |
| 0x04 | array（键 "0","1".. 忽略，按位置） | ARRAY |
| 0x07 | objectId | STR（24 位十六进制） |
| 0x08 | bool | BOOL |
| 0x09 | datetime | INT64（毫秒，语义由调用方解释） |
| 0x0A | null | NULL |
| 0x10 | int32 | INT64 |
| 0x12 | int64 | INT64 |
| 其余 | binData/regex/decimal128… | **报错**（数据完整性优先于宽容跳过） |

写出：INT64 值在 int32 范围内写 0x10、否则 0x12。读侧严格校验长度自洽（文档长度越界/截断/缺终止符/字符串无 NUL/bool 非 0-1 均拒绝），嵌套上限 64 层；任意前缀截断 fuzz 测试保证不越界不崩溃。
