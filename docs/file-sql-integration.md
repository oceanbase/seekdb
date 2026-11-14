# Lab 2 集成说明：框架与格式模块的接合

本文记录 `lab2/integration` 分支上把成员 A 的框架与成员 C 的格式模块合到一起时
所做的改动，以及这些改动的原因。

## 合并的背景

两个分支各自实现了 Lab 2 的一部分：

- `lab2/file-discovery`（成员 A）：表函数入口、`enable_file_sql` 开关、路径校验、
  读取器接口 `FileTableScanner`、计划与执行算子。
- `lab2/jsonl-parquet`（成员 B 的 CSV 与类型推断 + 成员 C 的 JSONL 与 Parquet）：
  CSV 读取、确定性类型推断、JSONL 读取、Parquet 读取。

两者都基于 `2026-competition`（`5bce9e06d`），所以直接合并时两个分支各自新增的
同名文件会冲突。集成分支把框架与格式模块放在同一个目录 `src/sql/engine/basic/file_sql/`
之下，模块之间只通过 `file_sql_types.h` 的共享类型与 `file_sql_reader.h` 的接口相连。

## 合并时解决的冲突

| 文件 | 冲突 | 处理 |
| --- | --- | --- |
| `file_sql_types.h` | 两个分支都新增，两边各自把 `ScalarRow` 别名放在自己新增的位置 | 别名属于共享的类型模块，保留在 `file_sql_types.h` |
| `file_sql_csv.h` | 同上，A 把别名放在 CSV 模块里 | 删除 CSV 模块里的副本，统一用类型模块的别名 |
| `sql_source_inventory.bzl` | 两边各自把新增的源文件加进清单 | 取并集，并补上新加的 `file_sql_adapters.cpp` |
| `unittest/sql/engine/basic/CMakeLists.txt` | 两边各自新增测试目标 | 取并集，保留双方的全部测试 |

别名只有一份之后，`file_sql_csv.h`、`file_sql_jsonl.h` 与 `file_sql_parquet.h`
都能直接使用 `ScalarRow`，适配层不需要做任何类型转换。

## 适配层：框架接口与格式模块的接合

### 接口不兼容的地方

A 的框架要求每个格式模块实现 `FileTableScanner`：

```cpp
virtual FileStatus prepare(FILE *input, const FileScanOptions &options) = 0;
virtual FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) = 0;
virtual void rescan() = 0;
virtual const std::vector<ColumnSchema> &columns() const = 0;
```

C 的两个模块有各自不同的形状：

- `JsonlTableScan::prepare(FILE *, const JsonlOptions &, int64_t sample_size)`，
  选项是模块自己的 `JsonlOptions`；
- `ParquetReader::init(FILE *, const ParquetOptions &)`，选项是 `ParquetOptions`，
  而且方法名不是 `prepare`。

两者的 `next_batch` 与 `columns` 的形状和框架一致，`rescan` 也一致，只有
`prepare` 的选项类型与名字不同。

### 改动一：框架的选项带上模块需要的字段

`file_sql_reader.h` 的 `FileScanOptions` 增加了两块：

- `FileScanLimits limits`：资源上限。框架统一持有这些上限，模块只读取适用于自己的那几项。
  取值与 C 的模块原本自己使用的默认值相同，因此行为不变。
- `std::vector<std::string> projection`：列裁剪。空表示读全部列，目前只有 Parquet 使用。

适配层把这两块映射到模块自己的选项结构上，模块的代码一行未改。

### 改动二：适配层新增 `file_sql_adapters.{h,cpp}`

新增的文件实现两个适配类：

- `JsonlFileScanner`：把 `FileScanOptions` 映射到 `JsonlOptions`，转调
  `JsonlTableScan`。
- `ParquetFileScanner`：把 `FileScanOptions` 映射到 `ParquetOptions`，转调
  `ParquetReader`。

两个适配类在文件加载时通过 `register_file_scanner` 注册自己，注册是幂等的。
调用方（解析器与执行算子）不需要改动。

### 改动三：文件结束的表示方式不一致（重要）

这是一个会在运行时挂住的问题，不是编译期能发现的。

三个模块报告"文件读完"的方式不同：

| 模块 | 文件读完时 `next_batch` 的返回 |
| --- | --- |
| CSV | 返回 `FileCode::END` |
| JSONL | 返回 OK，行数为 0 |
| Parquet | 返回 OK，行数为 0 |

框架的执行算子只把 `END` 当作扫描结束：

```cpp
const file_sql::FileStatus status = scanner_->next_batch(FILE_TABLE_BATCH_SIZE, rows_);
if (file_sql::FileCode::END == status.code) {
  ret = OB_ITER_END;
} else if (!status.ok()) { ... }
```

而 `inner_get_next_row` 在批次用完后会继续取下一批。如果 JSONL 或 Parquet 在
文件末尾返回 OK 加空批次，算子就会永远取空批次，查询挂死。

适配层把这个差异统一掉：模块返回 OK 但行数为 0 时，适配层返回 `FileCode::END`。
CSV 的适配器不需要改，因为它本来就是这样。

### 改动四：`rescan` 的返回值

框架的接口把 `rescan` 声明成 `void`，而三个模块的 `rescan` 都返回 `FileStatus`。
A 的 CSV 适配器直接丢弃了返回值：

```cpp
void rescan() override { scan_.rescan(); }
```

`rescan` 会失败（例如文件已被替换导致 seek 失败），丢弃返回值会让失败变成静默的
错误结果。集成时把接口改成返回 `FileStatus`，三个适配器都把状态透传给调用方。

## 验证

`unittest/sql/engine/basic/test_file_sql_adapters.cpp` 是新增的测试，它通过框架的
接口驱动 JSONL 与 Parquet 两个模块，覆盖：

- `create_file_scanner` 能拿到 JSONL 与 Parquet 的扫描器；
- 字段并集与官方样例一致（6 列、3 条记录）；
- Parquet 的形状与官方样例一致（1 列 `c1`、7 行、`min=0`、`max=6`、`sum=21`）；
- `rescan` 之后能重新读到全部数据；
- 框架的资源上限能传到模块（超限时返回 `RESOURCE_LIMIT`）；
- 不存在的 projection 被拒绝，而不是产出全 NULL 的行；
- 文件末尾报告 `END`，而不是返回空批次。

用例的有效性做过验证：把适配层里"空批次转 `END`"的转换去掉后，测试报 8 条 FAIL，
确认这条用例覆盖的正是会挂死的那条路径。

## 尚未解决的部分

- A 的框架里 `glob`、`DESCRIBE`、`COPY` 还没有实现，属于 ① 与 ⑥ 的评分项。
- 资源上限目前是框架里的常量，还没有做成会话变量。
- 框架的路径校验只在解析阶段做一次，执行阶段重新打开文件之前没有再校验一次，
  两次之间文件被换成指向别处的软链接不会被发现。
- 表函数的语句不可比较，计划缓存不会复用文件表语句的计划，每次执行都会重新推断 Schema。
- 一个查询会把文件读三遍：解析阶段推断一次，算子执行时为了发现文件变化再推断一次，
  读数据时再读一遍。
