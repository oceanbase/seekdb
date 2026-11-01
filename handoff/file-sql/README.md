# File SQL（文件即数据库）交接文档

分支：`feat/file-sql`（基于 `2026-competition` @ `5bce9e0`）
仓库：`Funnyvalentine00/seekdb1`
对应 issue：`#2`

## 1. 题目与验收标准

在 seekdb 中把本地 CSV / JSONL / Parquet 文件当作可查询的表，并支持文件发现与导出：

- `read_csv('path'[, header=true][, sample_size=-1])`
- `read_json('path', format='newline_delimited')`
- `read_parquet('path')`
- `glob('path/*')`（单列 `file`）
- `DESCRIBE SELECT * FROM read_csv(...)`（DuckDB 风格六列元数据）
- `COPY (query) TO 'path' (format csv, header, nullstr 'NULL')`

官方验收：

```bash
cd tools/deploy && ./obd.sh mysqltest -n <cluster> --suite file_sql
```

金样只读：`tools/deploy/mysql_test/test_suite/file_sql/r/mysql/file_sql_basic.result`。
本次结果：**通过（mysqltest 最后一行 `ok`，逐行与金样一致）**。

## 2. 接口契约

### 2.1 表函数

| 函数 | 参数 | 说明 |
| --- | --- | --- |
| `read_csv` | 路径 + 可选 `header`(bool)、`sample_size=-1` | 默认 `header=true`，只支持 `sample_size=-1`（全量推断） |
| `read_json` | 路径 + `format='newline_delimited'` | JSONL，对象并集出 schema |
| `read_parquet` | 路径 | 扁平非重复 schema，PLAIN / RLE_DICTIONARY |
| `glob` | 路径通配符 | 返回单列 `file`，值为 realpath 后的绝对路径 |

路径必须是字符串字面量；非字符串、非法格式、未开启会话变量均报 `1235`（`OB_NOT_SUPPORTED`），语法/格式错误报 `1064`。

### 2.2 类型推断（CSV/JSONL）

`BIGINT` / `DOUBLE` / `BOOLEAN` / `VARCHAR` / `DATE` / `DATETIME` / `NULL`；
全 NULL 列 → `NULL`；重名列自动加后缀 `__2`（GENERAL_CI 逐列去重）；`2026-08-03T12:13:14` 归一为 `DATETIME`。

`DESCRIBE SELECT` 输出：`column_name, column_type, null, key, default, extra`，
`null` 依据列是否可空（`NO`/`YES`），其余三列为 `NULL`。

### 2.3 COPY

`COPY (query) TO 'path' (format csv, header, nullstr 'NULL')`
- 复用 `ObSelectIntoOp`，默认 `,` 分隔、`\n` 换行；
- `header` 输出查询投影列名；
- `nullstr` 指定 NULL 的输出文本；
- 复用 `secure_file_priv` 校验；
- 未开启 `enable_file_sql` 报 `1235`；非法 format/option 报 `1064`。

### 2.4 会话变量与安全

- `enable_file_sql`（session，默认 `off`）：未开启时所有表函数/COPY 返回 `1235`。
- `secure_file_priv`：`realpath` + `ObResolverUtils::check_secure_path`；打开后再次用
  `/proc/self/fd` 校验，文件必须为普通文件、`O_NOFOLLOW` 打开。
- 推断时记录 `(dev, ino, size, mtime, ctime)` 指纹，扫描结束时复核；文件变更立即报错。

## 3. 改动文件清单

新增：
- `src/sql/engine/file_sql/csv_reader.{h,cpp}`（流式 CSV 解析 + 类型推断）
- `src/sql/engine/file_sql/jsonl_reader.{h,cpp}`（有界 JSONL 解析）
- `src/sql/engine/file_sql/file_sql_scan.{h,cpp}`（CSV/JSONL/Parquet/glob 统一扫描）
- `src/sql/engine/file_sql/parquet_reader.{h,cpp}`（Thrift compact + Snappy + 页解码）
- `unittest/sql/engine/file_sql/{test_csv_reader,test_csv_streaming,test_csv_cancellation}.cpp`

修改（接线部分）：
- `src/query/api/query/parser/ob_item_type.h`：新增 `T_FILE_SQL_TABLE` / `T_FILE_SQL_COPY_*`
- `src/sql/parser/sql_parser_mysql_mode.y`：表函数 FROM 规则、`COPY ... TO`、DESCRIBE 标记
- `src/sql/resolver/dml/ob_dml_resolver.{h,cpp}`、`ob_dml_stmt.h`：解析表函数、推断 schema、表类型
- `src/sql/resolver/dml/ob_select_resolver.{h,cpp}`、`ob_select_stmt.h`：COPY 选项、`with_header_`/`nullstr`
- `src/sql/resolver/ddl/ob_explain_resolver.cpp`：`DESCRIBE SELECT` 元数据
- `src/sql/engine/basic/ob_json_table_op.{h,cpp}`：文件表类型分派
- `src/sql/engine/basic/ob_select_into_op.{h,cpp}`、`src/sql/optimizer/ob_log_select_into.h`、`ob_log_plan.cpp`、`src/sql/code_generator/ob_static_engine_cg.cpp`：COPY 表头/空值输出
- `src/share/system_variable/*`、`src/sql/session/ob_system_variable_factory.*`：`enable_file_sql`
- `src/sql/ob_sql_context.h`、`src/sql/ob_sql.cpp`：`has_file_sql_`（禁用计划缓存）
- `src/sql/printer/ob_dml_stmt_printer.cpp`：文件表打印
- `src/sql/sql_source_inventory.bzl`、`src/sql/sql_header_inventory.bzl`：登记新源码
- `src/sql/rewrite/ob_transform_utils.cpp`：文件表列 NOT NULL 传播

完整补丁：`handoff/file-sql/changes.patch`（3 个 commit，38 个文件，+2647/-15）。

## 4. 构建与运行命令

```bash
cd src/sql/parser && ./gen_parser.sh          # 改过 .y 必须执行（bison 2.4.1，deps/3rd）
cd build_release && make -j8                  # 退出码 0
./bin/seekdb --port 2882 --data-dir <dir> --base-dir <dir> \
  --parameter memory_budget=4G --parameter datafile_size=4G \
  --parameter datafile_maxsize=8G --parameter log_disk_size=2G --parameter cpu_count=4
```

会话内使用：

```sql
set session enable_file_sql = ON;
select * from read_csv('/path/basic.csv');
describe select * from read_csv('/path/basic.csv');
copy (select id,name,score from read_csv('/path/basic.csv') order by id)
  to '/path/export.csv' (format csv, header, nullstr 'NULL');
```

## 5. 官方用例结果

`file_sql_basic`（唯一用例，含 12 条断言）全部通过，包括：

- 默认关闭时 `1235`
- 非法选项 `1064` / 非法 `sample_size` `1235`
- CSV 扫描、类型转换、重复表头、多行引号
- JSONL 并集 schema 与缺失字段
- `DESCRIBE SELECT` 六列元数据
- Parquet 聚合（7 / 0 / 6 / 21）
- `glob` 文件名列表
- `COPY` 导出并回读

完整输出见 `logs/verify.log`。

## 6. 日志位置

- `logs/build.log`：构建命令 + 退出码 + 编译输出
- `logs/verify.log`：官方 mysqltest 完整输出（最后一行 `ok`）
- `logs/test.log`：双文件 JOIN、过滤/投影、会话开关、非法格式、非 File SQL 回归
- `logs/notes.log`：设计取舍、踩坑与已放弃方案
- `changes.patch`：本分支相对 `5bce9e0` 的完整补丁

## 7. 已知风险

- Parquet 仅支持扁平非重复 schema；不支持 GZIP/ZSTD/BROTLI/LZO、DELTA_* / BYTE_STREAM_SPLIT、嵌套列。
- csv_reader / jsonl_reader 为自研解析，超长记录（>64MiB）显式报错而非截断。
- `DESCRIBE` 通过再生一个常量 SELECT 生成元数据，不执行文件数据；列类型取自推断结果与 NOT NULL 标记。
- `COPY` 的表头/空值开关默认关闭，`SELECT ... INTO OUTFILE` 行为不变。
- 文件表查询禁用计划缓存（`has_file_sql_`），每次重新推断 schema。
- 本地验证时官方夹具的 `ssh/scp` 使用等价本地替身；测试数据本身未修改（曾误覆盖的
  `data/basic.jsonl` 已用 `git checkout` 还原，`git status` 干净）。

## 8. 回滚方式

```bash
git checkout feat/file-sql
git revert 72bbd39 dade8da a7c89ce      # 或 git reset --hard 5bce9e0
cd src/sql/parser && ./gen_parser.sh
cd build_release && make -j8
```

回滚后 `enable_file_sql` 变量与 File SQL 表函数一并消失，原有 SQL 行为不受影响。
