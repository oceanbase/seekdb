# Lab 2 Part B：CSV 与类型推断接入说明

## 范围与状态

本实现对应团队计划的② CSV 读取和⑤确定性类型推断，并提供供 COPY sink 使用的 CSV 编码器。
基于团队 `2026-competition` 分支开发。A 的公共扫描接口和 `lab2/integration` 尚未发布，因此本提交提供扫描器可复用的产品 C++ 模块，尚不注册 SQL 语法、系统变量或执行算子。

本模块不会直接打开任何路径，也不执行查询。SQL 入口、`enable_file_sql`、`secure_file_priv`、路径规范化、符号链接校验、COPY 原子发布和原生执行计划接入由 A 的公共框架负责。接入完成前，不得把组件测试通过解释为 `file_sql_basic.test` 或 Lab2 官方评测通过。

来源：

- [团队分工](https://github.com/xiaoheto/dataforge-openbase-comp/blob/main/docs/lab-2-execution-plan.md)
- [课程当前 Lab 页面](https://wiki.pdy.app:8443/s/abcb1d4c-a38b-42e4-9ce3-8a7e29e96c03/doc/lab-4S4OeZ7hO2)
- [官方赛题二](https://oceanbase.yuque.com/org-wiki-obtech-vh7w9r/hc3pio/gy2a66vag0028msk)
- 官方 `tools/deploy/mysql_test/test_suite/file_sql/` 数据与测试期望。

官方文字同时出现 50 分与“沿用 30 分”，评分表相加为 50；本提交不自行解释评分口径。

## 类型推断

头文件：`sql/engine/basic/file_sql/file_sql_types.h`，命名空间 `oceanbase::sql::file_sql`。

`SchemaInference::init(names)` 初始化列，逐条 `observe(TextRow)` 更新类型和可空性，不保留行。
`columns()` 返回列名、`ScalarType` 和 `nullable`，供 CSV、JSONL 共用。

| 输入/混合 | 结果 |
|---|---|
| NULL 与任意类型 | 保留非 NULL 类型，同时标记可空 |
| 全 NULL | `NULL_VALUE`，可空 |
| 有符号 64 位范围内整数字面量 | BIGINT |
| 超出 BIGINT 范围的整数字面量 | VARCHAR，避免经 DOUBLE 静默舍入 |
| 有限十进制/科学计数法 | DOUBLE；拒绝 NaN、Infinity、溢出和下溢 |
| BIGINT 与 DOUBLE | DOUBLE |
| true/false、TRUE/FALSE | BOOLEAN；不把 0/1 推断成 BOOLEAN |
| 合法 YYYY-MM-DD | DATE；校验闰年及月份天数 |
| 合法 YYYY-MM-DD HH:MM:SS 或 T 分隔 | DATETIME |
| DATE 与 DATETIME | DATETIME，日期补零点时间 |
| 其他不兼容混合 | VARCHAR，保留原字符串 |

空字符串与 NULL 分开表示。文本不 trim，引用中的空格与数值格式不会在 VARCHAR 列中丢失。
类型宽化满足交换律、结合律和幂等律，与输入行顺序无关。`convert_scalar` 产生带标签的
`ScalarValue`，BIGINT/BOOLEAN 使用 `integer`，DOUBLE 使用 `real`，其余使用 `text`。
SQL adapter 将这些值写入正确生命周期内的 ObDatum/ObObj，不能把临时 string 的地址保留到批次之外。

## CSV 扫描接口

头文件：`sql/engine/basic/file_sql/file_sql_csv.h`。

```cpp
// authorized_input 由上层在通过开关与路径权限校验后打开。
CsvTableScan scan;
FileStatus status = scan.prepare(authorized_input, options, -1);
// 仅在 status.ok() 后发布 scan.columns()。
std::vector<ScalarRow> rows;
while ((status = scan.next_batch(256, rows)).ok()) {
  // 交给 seekdb 原生扫描器；WHERE、JOIN、聚合、排序等由 SQL 引擎完成。
}
// END 表示正常结束，其他非 OK 状态必须向上传播。
```

- `FILE*` 是借用句柄，模块不会 fclose；调用方必须保证句柄直到扫描结束仍然有效，不能并发移动其文件位置。
- `prepare` 全量推断后重扫；每次只保留当前记录和列状态。`sample_size != -1` 失败，上层应按题面映射为“不支持”。
- `next_batch` 返回不超过请求数量的批次；达到内存预算时可以提前返回。没有数据时返回 `END`。
- `rescan` 重新回到数据起点并跳过表头；不会重新推断一个已被修改的文件。
- 支持 UTF-8 BOM、严格 UTF-8（拒绝 NUL）、LF/CRLF、分隔符、双引号转义、可配置转义字符、字段内换行、无末尾换行。
- 默认有表头；无表头时生成 `column0`、`column1` 等列名。空表头也生成同类名字。
- 重复列名按 ASCII 大小写不敏感去重，后缀从 `__2` 开始，并避开已经存在的原始列名。例如 `id,id,id__2` 变为 `id,id__3,id__2`。
- 默认 NULL 标记为空字符串，仅未加引号的匹配字段为 NULL；`""` 是空字符串。可设置 `null_string`。
- 默认保留空物理记录，使单列 NULL 可往返；显式 `skip_blank_lines=true` 可以忽略空记录。多列文件中的空记录不匹配列数，会报告错误。
- 空文件无法推断 Schema；仅有表头的文件返回空数据集。列数不匹配、非法 UTF-8、坏引号均失败，不发布部分批次。
- 默认限制每条物理 CSV 记录（可跨行）16 MiB、4096 列、每批文本及 cell 槽位预算 32 MiB；另有参数硬上限。内存不随文件总大小增长。
- 推断结束、批次前后和重扫时用 fstat 校验 inode、设备、大小、mtime、ctime。变更返回 `FILE_CHANGED`，不会继续使用旧 Schema 读取新数据。
- `FileStatus` 提供错误类别、逻辑记录编号、物理行号和一基列号。上层添加文件名与 seekdb 错误码，不得吞掉错误后继续输出成功。

## CSV 导出编码器

`CsvWriter::init(authorized_output, names, options)` 可写表头；`write_row(TextRow)` 流式编码，
`finish()` 检查 fflush。字段中的分隔符、换行、引号和转义字符会被正确引用；与 NULL 标记相同的非 NULL 文本会引用，以便读回时保留身份。写入和校验失败会保留在 writer 状态中，后续 finish 不能误报成功。

本类只编码已由原生查询产生的行。A 的 COPY 执行器负责：授权目标路径、排他创建临时文件、
目标已存在时报错、查询或写入失败时清理临时文件、成功后不覆盖既有文件地原子发布。
本提交不声称仅凭编码器即可支持 SQL `COPY`。

## 自测命令

在仓库根目录运行（编译器可用 GCC 13；CMake 不在 PATH 时使用工具链中的绝对路径）：

```bash
cmake -S unittest/sql/engine/basic -B build_file_sql_tests \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build_file_sql_tests -j1
ctest --test-dir build_file_sql_tests -V --output-on-failure
FILE_SQL_CHECK_RSS=1 /usr/bin/time -v \
  build_file_sql_tests/test_file_sql_csv --streaming
```

Sanitizer 使用独立构建目录，加 `-fsanitize=address,undefined -fno-omit-frame-pointer`。
大文件内存断言应使用非 Sanitizer 构建，避免把 ASan 的隔离区当作产品内存占用。
测试直接编译产品源码，不使用外部查询引擎，也不使用替代实现。公开 `basic.csv` 由测试只读访问，未修改官方输入或期望。

## 接入验收

当 A 的公共接口可用后，需要把借用句柄、动态 Schema 和 ScalarValue 接入原生 FileScan，
并逐项执行：CSV 投影、过滤、聚合、DESCRIBE、LIMIT/重扫、跨格式 JOIN、COPY 读回、
默认关闭、secure_file_priv 越界和符号链接检查。随后运行完整 `file_sql`、普通 SQL 和 AI 回归。
C 的 JSONL 可直接复用 `SchemaInference`；Parquet 有 Footer Schema 时不需要文本推断。
