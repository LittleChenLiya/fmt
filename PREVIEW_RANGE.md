# preview_range — 日志范围预览包装

## 功能

`fmt::preview_range` 包装一个范围，格式化时只展示前 `max_items` 项，适用于日志场景：
范围可能很大或由单遍输入迭代器即时生成，不应为了日志而收集全部元素。

## 行为

- 最多读取 `max_items` 个保留项，外加**一个**探测项用于判断是否截断。
- 超过上限时显示明确截断标记 `...(N)`（`N` 为已知总数）或 `...(unknown)`（总数未知）。
- **不**凭一个探测项伪造总长度；单遍输入范围的总数始终报告为 `unknown`。
- 每个保留值**只格式化一次**。
- `max_items=0` 仍可区分空范围（`[]`）与非空范围（`[...(N)]`）。
- 格式化异常会停止对源的消耗，不再递增迭代器。
- 原普通 `range` 格式化行为不变。

## 输出格式

| 场景 | 输出 |
|------|------|
| 已知总数，未截断 | `[item1, item2]` |
| 已知总数，截断 | `[item1, item2, ...(5)]` |
| 未知总数，截断 | `[item1, item2, ...(unknown)]` |
| `max_items=0`，非空 | `[...(3)]` |
| 空范围 | `[]` |

字符串元素会被引号包裹并转义，与 `range` 格式化器一致：
`["a\nb", "c\td", ...(3)]`

## 接口

```cpp
// 范围重载
fmt::preview_range(range, max_items);

// 迭代器对重载
fmt::preview_range(begin, end, max_items);
```

## 演示

只编译单个程序（无需构建系统）：

```sh
g++ -std=c++17 -DFMT_HEADER_ONLY -I include demo_preview.cc -o demo_preview
./demo_preview
```

演示程序包含 13 个用例，覆盖：已知/未知总数、截断、空范围、
`max_items=0`、字符串转义、元素格式说明符、单遍迭代器、异常停止消耗、
迭代器对重载。

## 测试

单元测试位于 `test/ranges-test.cc`，覆盖：

- `preview_range_basic` — 基本截断与总数
- `preview_range_empty` — 空范围
- `preview_range_max_items_zero` — `max_items=0` 区分空与非空
- `preview_range_unknown_total` — 未知总数写 `unknown`，读取次数 ≤ max_items + 1
- `preview_range_one_shot_iterator` — 单遍输入迭代器，不收集元素
- `preview_range_string_escape` — 字符串元素转义
- `preview_range_exception_stops_consumption` — 异常停止消耗
- `preview_range_iterator_pair` — 迭代器对重载
- `preview_range_element_specs` — 元素格式说明符透传
