# Lab 2 Part C：JSONL 与 Parquet 接入说明

## 范围与状态

本实现对应团队计划的③ JSONL 读取、④ Parquet 读取，并提供⑦双文件 JOIN 所需的
两个格式扫描器。基于团队 `2026-competition` 分支与 B 的 `lab2/csv` 分支开发。

A 的公共扫描接口和 `lab2/integration` 尚未发布，因此本提交提供扫描器可复用的产品
C++ 模块，尚不注册 SQL 语法、系统变量或执行算子。接入完成前，不得把组件测试通过
解释为 `file_sql_basic.test` 或 Lab2 官方评测通过。

来源：

- [团队分工](https://github.com/xiaoheto/dataforge-openbase-comp/blob/main/docs/lab-2-execution-plan.md)
- [课程当前 Lab 页面](https://wiki.pdy.app:8443/s/abcb1d4c-a38b-42e4-9ce3-8a7e29e96c03/doc/lab-4S4OeZ7hO2)
- [官方赛题二](https://oceanbase.yuque.com/org-wiki-obtech-vh7w9r/hc3pio/gy2a66vag0028msk)
- 官方 `tools/deploy/mysql_test/test_suite/file_sql/` 数据与测试期望。

官方文字同时出现 50 分与"沿用 30 分"，评分表相加为 50；本提交不自行解释评分口径。

## 依赖结论

基线**没有**任何 Parquet 相关依赖：

```
grep -ril parquet src/ deps/   ->  0 命中
grep -ril snappy  src/ deps/   ->  仅 grpc / pymongo 的无关命中
grep -ril thrift  src/ deps/   ->  0 命中
```

因此 Parquet 读取必须自包含实现，不能引入需要联网下载的组件。实现包含四部分：

| 部件 | 位置 | 说明 |
| --- | --- | --- |
| Thrift compact protocol | `file_sql_parquet.cpp` 匿名命名空间 | 解析 Footer 与每个 PageHeader |
| Snappy | 同上 | 解压 SNAPPY 编码的页数据 |
| RLE / bit-packing hybrid | 同上 | 解码 definition levels |
| PLAIN | 同上 | BOOLEAN / INT32 / INT64 / FLOAT / DOUBLE / BYTE_ARRAY / FIXED_LEN_BYTE_ARRAY |

Rust 工作区（`rust/Cargo.toml`）只有 `config`、`kv`、`local`、`sql-nio` 四个成员，
没有 `serde_json`，也没有 Parquet crate；引入新的 crate 需要联网，与团队的离线构建
约定冲突，因此不采用。

## JSONL 读取

头文件：`sql/engine/basic/file_sql/file_sql_jsonl.h`，命名空间 `oceanbase::sql::file_sql`。

```cpp
JsonlTableScan scan;
FileStatus status = scan.prepare(authorized_input, JsonlOptions(), -1);
// 仅在 status.ok() 后发布 scan.columns()。
std::vector<ScalarRow> rows;
while ((status = scan.next_batch(256, rows)).ok()) {
  // 交给 seekdb 原生扫描器；WHERE、JOIN、聚合、排序等由 SQL 引擎完成。
}
```

### 实现要点

- 记录必须是**扁平 JSON 对象**。嵌套对象与数组被拒绝，字段值为字符串、数字、布尔或
  null 时接受。
- **字段并集**：列顺序是字段首次出现的顺序。某条记录缺少某列时，该列在该行为 NULL，
  该列因此可空。
- **类型推断**复用 B 的 `SchemaInference`：整数与浮点混合的 `score` 统一为 DOUBLE；
  `day` 为 DATE；`active` 为 BOOLEAN。缺字段不影响类型，只影响可空性。
- **两遍扫描**：第一遍只发现字段并集与列类型，不保留任何行；第二遍流式产出记录。
  内存与列数成正比，不随文件大小增长。`sample_size != -1` 直接失败。
- **空行**被跳过，不参与字段并集，也不影响行数。
- JSON 字符串的转义在读入时解析（`\n`、`\t`、`\"`、`\\`、`\uXXXX` 与代理对），
  类型推断看到的是用户看到的文本，而不是源码文本。
- 读取时校验 UTF-8；非法 UTF-8 与非法 JSON 报明确错误，不产出部分批次。
- 文件在扫描期间变化（inode、设备、大小、mtime、ctime 变化）返回 `FILE_CHANGED`，
  不会用旧 Schema 读新数据。
- `rescan` 回到数据起点；`next_batch` 不保留行，调用方拿到的是 `ScalarRow` 值。

### 与 B 的共享接口

`ScalarRow` 由 `file_sql_csv.h` 移到 `file_sql_types.h`，因为 CSV、JSONL 与 Parquet
三个格式共用它。这是本次提交对 B 文件的唯一改动，是纯粹的移动，不改变行为。

## Parquet 读取

头文件：`sql/engine/basic/file_sql/file_sql_parquet.h`。

```cpp
ParquetReader reader;
FileStatus status = reader.init(authorized_input, ParquetOptions());
// 仅在 status.ok() 后发布 reader.columns()。
std::vector<ScalarRow> rows;
while ((status = reader.next_batch(256, rows)).ok()) { ... }
```

### 实现要点

- **Footer Schema**：`FileMetaData` 的 schema 给出叶子列、物理类型与
  `max_definition_level`。类型映射：BOOLEAN → BOOLEAN，INT32 / INT64 → BIGINT，
  FLOAT / DOUBLE → DOUBLE，BYTE_ARRAY / FIXED_LEN_BYTE_ARRAY → VARCHAR。
  Parquet 的 Schema 本身就是列结构，**不需要文本类型推断**。
- **多 Row Group**：按 Row Group 顺序读取，每组独立解压与解码。
- **可空列**：definition levels 低于最大值时该值为 NULL。判断是否可空不能只看
  Schema：pyarrow 写的 Schema 把可空列标成 `repetition_type=1`，与必填列相同，
  因此读取器以列块统计信息里的 `null_count` 为准，`null_count > 0` 的列才可能
  出现 NULL。
- **列裁剪**：`ParquetOptions::projection` 给出需要读取的列名，未列出的列不读取，
  也不占用解码开销。
- **编码**：支持 DATA_PAGE_V1 与 DATA_PAGE_V2，PLAIN、RLE 与 RLE_DICTIONARY
  （字典页）三种编码，以及 SNAPPY 与 UNCOMPRESSED 两种 codec。DATA_PAGE_V2 的值
  是否压缩由页头的 `is_compressed` 决定，而不是由列的 codec 决定。
- **INT96** 时间戳类型不支持，返回明确错误。
- 文件在扫描期间变化返回 `FILE_CHANGED`。

### 页内的 level 布局

level 段的编码方式在两种页版本之间不同，这是实现中最容易出错的地方。

DATA_PAGE_V2 的页载荷是：

```
[repetition levels][definition levels][values]
```

两段都不带长度前缀，长度由页头的 `repetition_levels_byte_length` 与
`definition_levels_byte_length` 给出。values 是否压缩由 `is_compressed` 决定。

DATA_PAGE_V1 的页载荷是：

```
[4 字节长度][第一段 level][其余 level 段][values]
```

只有**第一段**带长度前缀。当列是扁平的（`max_repetition_level` 为 0）时，
第一段就是 definition levels；当列是嵌套的时，第一段是 repetition levels。
页头的 `*_byte_length` 在 v1 里比实际数据多一个字节，不能直接当作长度使用。

两版的 level 数据都不含 bit width 字节，位宽由最大 level 隐含。字典索引则相反：
RLE_DICTIONARY 的索引流**带** bit width 字节，而布尔列的 RLE 编码值**不带**，
它带的是四字节长度前缀。

### 位打包的取值

RLE 的 bit-packed run 把八个值打进 `bit_width` 个字节，八个值连续跨越这些字节，
所以取值必须按整个比特流进行，不能把 `bit_width` 个字节塞进一个 64 位整数。
当 `bit_width` 大于 8 时，八个值需要超过 64 位，单个整数放不下，取出的值会错位。

### 官方样例的字节级结构

`data/simple.parquet.b64` 解码后 461 字节：

```
0..3      PAR1
4..41     PageHeader（type=DATA_PAGE_V2，uncompressed=30，compressed=32）
42..43    definition levels：RLE header=0x03（1 组 8 个），
          packed byte 0x7f -> 7 个 level 全为 1（第 8 个是填充）
44..73    Snappy 压缩的 PLAIN INT32 值（7 × 4 = 28 字节）
74..118   Footer（Thrift compact FileMetaData）
...       末尾 4 字节 footer 长度 + PAR1
```

Footer 中该列的元数据为：`type=INT32`、`encodings=[RLE, PLAIN]`、`codec=SNAPPY`、
`num_values=7`、`data_page_offset=4`。

解码结果为 `0 1 2 3 4 5 6`，与官方 `file_sql_basic.result` 期望的
`row_count=7 min_c1=0 max_c1=6 sum_c1=21` 一致。

### 覆盖的格式组合

官方样例只覆盖了 DATA_PAGE_V2 + PLAIN + SNAPPY 一种组合。为了让隐藏测试用到其他
writer 的样本时不出问题，单元测试准备了 20 个样本，覆盖：

- DATA_PAGE_V1 与 DATA_PAGE_V2；
- PLAIN、RLE 与 RLE_DICTIONARY；
- SNAPPY 与 UNCOMPRESSED；
- 必填列、可空列与带 NULL 的列；
- 单个与多个 Row Group；
- BOOLEAN、INT32、INT64、DOUBLE 与 BYTE_ARRAY；
- 多列与列裁剪；
- 字典被截断后回退到 PLAIN 的写法。

样本放在 `tools/deploy/mysql_test/test_suite/file_sql/data/parquet/`，由
`unittest/sql/engine/basic/test_file_sql_parquet.cpp` 读取。

## 双文件 JOIN

⑦ 的计划明确要求"使用 seekdb 原生执行计划"，因此 JOIN 不由本模块实现：两个格式的
扫描器都必须接入 A 的表函数注册与原生算子，JOIN 由 seekdb 的优化器与 Join 算子完成。

本提交为 JOIN 提供的条件：

- CSV、JSONL、Parquet 三个扫描器接口一致，均为
  `prepare` → `next_batch(limit, ScalarRow)` → `rescan`，并发布 `ColumnSchema`；
- 列名与类型都来自 `ColumnSchema`，A 的适配层不需要为每个格式单独处理；
- JSONL 与 Parquet 的列顺序稳定，重扫后一致，JOIN 的两侧可以按列名对齐。

`tools/deploy/mysql_test/test_suite/file_sql/t/file_sql_jsonl_parquet.test` 覆盖
CSV 与 JSONL、CSV 与 Parquet 的 INNER JOIN 与 LEFT JOIN，以及跨格式 JOIN。
这些用例需要 A 的表函数注册后才能运行。

## 自测命令

在仓库根目录运行：

```bash
DEVT=deps/3rd/usr/local/oceanbase/devtools
$DEVT/bin/cmake -S unittest/sql/engine/basic -B build_file_sql_tests \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=Debug
$DEVT/bin/cmake --build build_file_sql_tests -j1
$DEVT/bin/ctest --test-dir build_file_sql_tests --output-on-failure
```

三个用例全部通过：`file_sql_types`、`file_sql_csv`（B 的）与 `file_sql_jsonl_parquet`。
测试直接编译产品源码，不使用外部查询引擎，也不使用替代实现。公开 `basic.jsonl` 与
`simple.parquet.b64` 只被只读访问，未修改官方输入或期望。

四个用例全部通过：`file_sql_types`、`file_sql_csv`（B 的）、
`file_sql_parquet` 与 `file_sql_jsonl_parquet`。

主构建的验证：

```bash
./build.sh release --make -j2
```

`file_sql_jsonl.cpp` 与 `file_sql_parquet.cpp` 已登记在
`src/sql/sql_source_inventory.bzl` 的 `SQL_STANDALONE_SOURCES` 中，与 B 的
`file_sql_csv.cpp`、`file_sql_types.cpp` 同一组。

## 接入验收

当 A 的公共接口可用后，需要把借用句柄、动态 Schema 和 `ScalarValue` 接入原生
FileScan，并逐项执行：JSONL 投影、过滤、聚合、`DESCRIBE`、LIMIT 与重扫，Parquet
投影与列裁剪，CSV 与 JSONL / Parquet 的 INNER 与 LEFT JOIN，COPY 读回，默认关闭、
`secure_file_priv` 越界和符号链接检查。随后运行完整 `file_sql`、普通 SQL 和 AI 回归。

## 已知限制

- Parquet 支持 DATA_PAGE_V1 与 DATA_PAGE_V2，PLAIN、RLE 与 RLE_DICTIONARY
  三种编码。DELTA_BINARY_PACKED 与 BYTE_STREAM_SPLIT 未实现，遇到时返回明确错误。
- Parquet 的 INT96 时间戳类型不支持。
- Parquet 只支持 SNAPPY 与 UNCOMPRESSED 两种 codec，ZSTD 与 GZIP 未实现。
- Parquet 的嵌套列只读第一层的 definition levels，repetition levels 不参与行
  的重建，因此嵌套列的每一行对应一个叶子值。
- JSONL 只支持扁平对象，嵌套对象与数组被拒绝。
- 两个扫描器都不打开路径：路径授权、`enable_file_sql`、`secure_file_priv`、符号
  链接校验与路径规范化由 A 的公共框架负责。
- 两个扫描器都不执行查询：过滤、聚合、排序与 JOIN 由 seekdb 原生算子完成。
