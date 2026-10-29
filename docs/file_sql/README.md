# 赛题二：文件即数据库（File SQL）

本目录是 seekdb 2026 赛题二「文件即数据库」的设计与提测说明。

- 实现方式：完全基于 seekdb（OceanBase 4.x 内核）自研 SQL 引擎实现，**未引入任何外部 SQL 引擎**（不使用 DuckDB / SQLite / Polars 等做代执行），所有文件读取、格式解析、类型推断均为本仓库代码。
- 功能默认关闭，通过会话变量 `enable_file_sql` 显式开启；开启后仍受 `secure_file_priv` 约束。

---

## 1. 功能开关

| 变量 | 类型 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `enable_file_sql` | bool | `0` | 会话级变量；为 0 时任何文件表函数报 `1235 (OB_NOT_SUPPORTED)` |

```sql
select @@session.enable_file_sql;   -- 0
set session enable_file_sql = ON;   -- 开启
...
set session enable_file_sql = OFF;
```

---

## 2. SQL 接口

### 2.1 文件表函数

四个表函数，统一以「表函数」形式出现在 `FROM` 子句中，均可带别名：

| 函数 | 说明 | 支持的 format |
| --- | --- | --- |
| `read_csv(path, ...)` | 读取 CSV 文件 | `csv`（默认） |
| `read_json(path, ...)` | 读取 JSON / JSON Lines 文件 | `json`、`newline_delimited`/`jsonl`/`ndjson`（默认 `newline_delimited`） |
| `read_parquet(path, ...)` | 读取 Parquet 文件 | `parquet`（默认） |
| `glob(pattern)` | 按通配符列出目录下文件，仅返回一列 `file`（VARCHAR） | — |

参数（位置参数与命名参数均可）：

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `path` | string | 文件路径或 glob 模式（必填，首个位置参数） |
| `format` | string | 文件格式；取值必须与函数匹配，否则报 `1064` |
| `header` | bool | CSV 是否含表头行，默认 true |
| `sample_size` | int | 仅支持 `-1`（表示全量扫描），其它值报 `1235` |
| `delimiter` | string | CSV 分隔符，长度必须为 1 |
| `nullstr` / `null_string` | string | 空值字符串；未指定时「未加引号的空字段」视为 NULL |

示例：

```sql
select * from read_csv('/data/basic.csv');
select id, name from read_csv('/data/basic.csv') t where id > 1 order by id;
select * from read_json('/data/basic.jsonl', format = 'newline_delimited');
select * from read_parquet('/data/simple.parquet');
select substring_index(file, '/', -1) as file from glob('/data/*') order by file;
```

两个及以上文件表可直接 JOIN：

```sql
select a.id, a.name, b.score
from read_csv('/data/basic.csv') a
join read_csv('/data/other.csv') b on a.id = b.id;
```

### 2.2 DESCRIBE

`describe select * from read_csv('...')` 被改写为普通查询，返回 6 列：
`column_name`、`column_type`、`null`、`key`、`default`、`extra`。
`column_type` 使用大写类型名（`BIGINT` / `VARCHAR` / `DOUBLE` / `BOOLEAN` / `DATE` / `DATETIME`），
`null` 为 `YES`/`NO`，`key`/`default`/`extra` 固定为文本 `NULL`。

### 2.3 COPY ... TO（导出，不计分但已支持）

```sql
copy (select id, name, score from read_csv('/data/basic.csv') order by id)
to '/data/export.csv' (format csv, header, nullstr 'NULL');
```

- 支持 `format csv`（其它格式报 `1235`）、`header`（写出表头行）、`nullstr`（NULL 的输出文本）。
- 导出为 MySQL `INTO OUTFILE` 风格：字段以 `,` 分隔、记录以 `\n` 换行、默认不加引号。
- 导出路径同样受 `secure_file_priv` 约束。
- COPY 语句不产生结果集（与 `SELECT ... INTO OUTFILE` 一致）。

---

## 3. 总体架构

```
SQL 文本
  │
  ├─ 解析器 sql_parser_mysql_mode.y
  │     file_table_expr / file_table_arg_list      → T_FILE_TABLE_EXPRESSION
  │     copy_stmt                                   → 改写为 select + T_INTO_OUTFILE
  │     explain_or_desc（DESCRIBE）                 → T_EXPLAIN.value_ = 1
  │
  ├─ 语义解析 ob_dml_resolver.cpp
  │     resolve_file_table_item()：解析参数、校验开关/格式/路径权限、
  │                                推断 schema、建 TableItem(file_*)
  │     resolve_file_table_column_items()：按文件列顺序建 ColumnItem
  │
  ├─ 优化器 ob_log_function_table.h / ob_log_plan.cpp
  │     文件表复用 FUNCTION_TABLE 通道，file_* 透传到 LogOp
  │
  ├─ 代码生成 ob_static_engine_cg.cpp
  │     generate_spec(ObLogFunctionTable&, ObFunctionTableSpec&)：写 spec.file_*
  │
  └─ 物理算子 ob_function_table_op.cpp
        inner_open() → 内联类 ObFileSourceCtx 打开文件
        inner_get_next_row_file() → 逐行读取并按列类型写入 datum
                │
                ▼
        文件源库 src/sql/engine/file_source/（自研，无 OB 依赖）
          file_source_reader   门面：open / open_glob / infer_schema / next_row
          file_csv_reader      CSV 词法与解析（流式）
          file_jsonl_reader    JSON Lines 解析（流式）
          file_parquet_reader  Parquet 读取（按 row group 读列块）
          file_parquet_format  Thrift Compact 解析 + Snappy 解压 + 编码解码
          file_type_inference  确定性类型推断与宽化
          file_source_util     路径归一、secure_file_priv 校验、glob、格式识别
```

**为什么复用 FUNCTION_TABLE**：OceanBase 已有「表函数」完整链路（解析 → 解析器 → 优化器 → 物理算子），
文件表只需在 `TableItem` 上附加文件元信息，并在物理算子中增加一个分支即可，
避免新增一整套 `TableItem` 类型 / 路径算子 / LogOp / 物理算子，改动面小、可复用现有 JOIN、
下推、聚合等能力。

---

## 4. 文件源库（src/sql/engine/file_source）

命名空间 `oceanbase::sql::file_source`，仅使用标准 C++（stdio/vector/string），不依赖 OB 头文件与宏，
因此既能被 OB 物理算子调用，也能脱离 OB 单独编译做单元测试。

### 4.1 关键类型

| 类型 | 说明 |
| --- | --- |
| `enum class ColumnType` | `NULL_TYPE, BOOLEAN, BIGINT, DOUBLE, VARCHAR, DATE, DATETIME` |
| `enum class FileFormat` | `AUTO, CSV, JSON, JSONL, PARQUET, GLOB` |
| `enum class FileErrorCode` | `OK, PATH_NOT_ALLOWED, FILE_NOT_FOUND, UNSUPPORTED_FORMAT, PARSE_ERROR, SCHEMA_MISMATCH, FILE_CHANGED, IO_ERROR, NOT_SUPPORTED, LIMIT_EXCEEDED` |
| `class FileSourceError` | 错误码 + 消息 + 行号，`is_ok()` 判断成功 |
| `struct FileColumn / FileSchema` | 列名、类型、是否可空 |
| `struct FileReadOptions` | format / has_header / sample_size / delimiter / quote / escape / null_string |
| `struct FileRow` | 一行的字符串值 + NULL 标记 |
| `struct FileSignature` | 文件大小 + mtime，用于读过程中检测文件被替换 |

### 4.2 读写模型（流式）

库层提供 **pull 接口**，物理算子按需逐行拉取：

```cpp
FileSourceError err = reader.open(path, options);   // 打开并校验
err = reader.infer_schema(schema);                  // 推断列与类型
bool ok = reader.next_row(row, err);                // 取下一行（读完返回 false）
```

- CSV：64KB 缓冲的字符级 tokenizer，支持引号、双写引号转义、字段内换行、CRLF 归一；
- JSONL：逐行读取、逐行解析扁平 JSON 对象；
- Parquet：解析 footer（Thrift Compact）+ Snappy 解压，按 row group 读取列块字节区间；
- 三者都不做「整文件载入内存」，行数据在读取时即产出。

### 4.3 格式识别

`detect_format_by_extension(path)` 只按最后一个路径分量的扩展名（小写）判定：

| 扩展名 | 格式 |
| --- | --- |
| `.csv` | CSV |
| `.json` | JSON |
| `.jsonl` / `.ndjson` | JSONL |
| `.parquet` | Parquet |
| 其它 / 无扩展名 | 不支持（报 `1235`）或按函数默认格式（显式传 `format` 时按 `format` 走） |

### 4.4 类型推断规则（确定性）

推断分两阶段：**先逐字面量定类型，再按列宽化**。字面量类型判定顺序：

1. 空串 / 纯 NULL → `NULL`；
2. `true` / `false`（大小写不敏感）→ `BOOLEAN`；
3. 纯整数（可带符号）→ `BIGINT`；
4. 浮点数（含小数点或指数）→ `DOUBLE`；
5. `YYYY-MM-DD` → `DATE`；
6. `YYYY-MM-DD[T ]HH:MM:SS[.fff]` → `DATETIME`；
7. 其余 → `VARCHAR`。

列宽化（取所有值的最宽类型）：

| 参与类型 | 结果 |
| --- | --- |
| 全为 NULL | `NULL`（列可空） |
| NULL + 其它 | 其它类型（NULL 可空） |
| BOOLEAN + 整数 | `BIGINT` |
| 整数 + 浮点 | `DOUBLE` |
| DATE + DATETIME | `DATETIME` |
| 数值 + 日期/时间 | `VARCHAR` |
| 含字符串 | `VARCHAR` |

宽度序：`NULL < BOOLEAN < BIGINT < DOUBLE`、`NULL < DATE < DATETIME`，跨族相遇一律降级为 `VARCHAR`。

列名规则：取首行表头（CSV）或首个出现顺序的键（JSONL）；重名自动加后缀 `__2`、`__3`…
（官方数据 `id,name,score,active,day,created,note,id` → 最后一列 `id__2`）；去除 UTF-8 BOM。

---

## 5. 安全

- **默认关闭**：`enable_file_sql` 默认 `0`，未开启直接报 `1235`。
- **路径白名单**：所有读写路径都经过 `file_source::check_secure_path(secure_file_priv, path)`：
  - `secure_file_priv` 为 `/`（默认）时放行所有绝对路径；
  - 否则只允许位于 `secure_file_priv` 指定目录下的路径（支持前缀匹配、路径归一化、glob 模式）；
  - 越权报 `OB_ERR_NO_PRIVILEGE (1044/1045)`。
- **格式白名单**：仅 csv/json/jsonl/ndjson/parquet，其它扩展名直接拒绝，避免把任意文件当 SQL/文本读取。
- **JSON 深度**：JSONL 仅支持扁平对象，嵌套对象/数组报 `1235`（带行号），避免递归解析带来的资源风险。
- **导出路径**同样受 `secure_file_priv` 约束。

---

## 6. 错误码

| 场景 | 错误码 |
| --- | --- |
| `enable_file_sql` 未开启 | `1235`（`OB_NOT_SUPPORTED`） |
| `sample_size` 非 `-1` | `1235` |
| 文件扩展名不支持 / 格式与函数不匹配 | `1235` / `1064` |
| 未知参数名、非法参数类型、路径为空 | `1064`（`OB_ERR_PARSER_SYNTAX`） |
| 文件不存在 | `1044`（`OB_FILE_NOT_EXIST`） |
| 路径超出 `secure_file_priv` | `1045`（`OB_ERR_NO_PRIVILEGE`） |
| CSV 行字段数与表头不一致、JSON 解析失败 | `1235`（含行号信息） |

---

## 7. 构建登记

OceanBase 的 CMake 构建通过 `src/sql/sql_source_inventory.bzl` 采集源文件清单，
新增源文件必须登记，否则不会参与编译：

- `src/sql/sql_source_inventory.bzl`：`ob_sql_engine_basic_0` 组新增 8 个 `.cpp`；
- `src/sql/sql_header_inventory.bzl`：`SQL_PRIVATE_HEADERS` 新增 8 个 `.h`。

> 注意：不要新建独立 unity 组。`src/sql/sql_runtime_group_deps.bzl` 的
> `_materialize_runtime_group_deps` 会校验「inventory 中存在但 deps 表缺失」的组并直接失败。

---

## 8. 测试

### 8.1 官方用例

```
tools/deploy/mysql_test/test_suite/file_sql/
  t/file_sql_basic.test
  r/mysql/file_sql_basic.result
  data/{basic.csv,basic.jsonl,invalid.txt,simple.parquet.b64}
  include/{copy_file_sql_data.inc,remove_file_sql_data.inc}
```

覆盖点：开关默认值、未开启时报错、非法格式、非法 `sample_size`、CSV 明细与聚合、
JSONL、DESCRIBE、Parquet 聚合、glob 列举、COPY 导出并回读。

### 8.2 库层单元测试

`unittest/sql/engine/file_source/file_source_unittest.cpp`：自包含、无 OB 依赖，
覆盖类型推断（含宽化、全 NULL、边界）、`secure_file_priv` 放行/拦截、扩展名识别、
CSV/JSONL/Parquet 读取、glob 列举、空匹配报错、不支持格式报错、缺失文件报错。

脱离 OB 单独编译运行（Windows 示例）：

```bash
g++ -std=c++11 -Isrc -DFILE_SOURCE_STANDALONE_TEST \
    -o file_source_unittest.exe \
    unittest/sql/engine/file_source/file_source_unittest.cpp \
    src/sql/engine/file_source/*.cpp

./file_source_unittest.exe <work_dir> <fixture_dir>
# fixture_dir 默认为 tools/deploy/mysql_test/test_suite/file_sql/data
# work_dir 会被自动创建，用于存放 fixture 的副本（不污染 fixture 目录）
```

`FILE_SOURCE_STANDALONE_TEST` 宏用于隔离 `main()`，避免与评测方自带 unittest 冲突。

路径处理说明：`file_source_util` 统一以 `/` 作为路径分隔符，并识别 windows 盘符前缀
（`C:/...`）；linux 下 `:` 仍是普通文件名字符，行为与 OB 线上（Linux）完全一致。

### 8.3 建议补充的自测用例（官方用例未覆盖）

语雀明确「基础测试未覆盖全部评分用例」，以下路径需自行补测：

- 双文件 JOIN：`read_csv(a) join read_csv(b) on ...`、`left join`、聚合下推；
- 异常路径：文件不存在、扩展名不支持、CSV 行列数不一致、JSON 嵌套、路径越权、
  `secure_file_priv` 收紧后的拒绝；
- 类型推断边界：整数与浮点混合、全 NULL 列、日期与时间混合、布尔与整数混合；
- 流式内存：大文件（数百 MB）读取时内存占用应保持稳定。

---

## 9. 已知限制

- `sample_size` 目前只接受 `-1`（全量扫描），其它值报 `1235`（题目要求仅支持 `-1`）；
- JSON 仅支持扁平对象（数组/嵌套对象报 `1235`）；
- COPY 导出目前仅支持 CSV 格式，且字段默认不加引号；
- Parquet 读取支持 SNAPPY / PLAIN / RLE 编码与 V1、V2 数据页，未覆盖全部 Parquet 特性
  （如 DELTA_BINARY_PACKED、嵌套结构）；
- 类型推断为「读全量」语义（题目要求），大文件首行推断阶段会读取整个文件以确定列宽化结果。
