# 文件即数据库（file_sql）实现设计

更新：2026-10-27。状态：**基础设施已落地（CSV 文件层），SQL 接入层未实现**。本文记录目标语义、
架构、与测试的对应关系，以及尚未完成的工作，避免后续实现走偏。

## 1. 目标与边界

用 `read_csv()/read_json()/read_ndjson()/read_parquet()` 直接把本地文件当表查询，列结构由文件内容
推断，过滤/聚合/排序/JOIN 全部交给 seekdb 自己的优化器与执行器；不引入任何外部查询引擎。

- 单进程、本地文件、只读输入。
- 功能默认关闭：`enable_file_sql` 会话变量，开启后仍必须通过 `secure_file_priv` 校验。
- 扫描必须流式：按批取数、支持 rescan、不把整文件物化到内存。
- 不改变普通 SQL 行为；异常数据、越权路径、文件变化不得导致崩溃或错误结果。

## 2. 现有地基

| 组件 | 状态 |
| --- | --- |
| `FROM f(...)` 表函数框架 | **不在 master/2026-competition**，只存在于 `feat/table-functions-schema-free` 分支（`src/sql/table_function/`，含 registry/bind/scan 生命周期、JSON_TABLE 与 UNNEST 的首个实现），该分支基于 master `76f871e`，与本题基线同源，可无冲突合并 |
| CSV 解析/写出 | 本仓库原本没有，本轮新增 `src/sql/engine/basic/ob_file_scan_utils.{h,cpp}` |
| Parquet | 无任何实现；仓库也没有 snappy/thrift 依赖，需要自带 |
| `COPY ... TO` | 有 `ob_external_file_writer` 可参考，但语法与 CSV 选项需要新写 |
| `enable_file_sql` / `secure_file_priv` | 无 |

节点：`docs/design/table-functions.md`（表函数框架设计）说明该框架"本轮不扫描实际数据猜测列"，
而 file_sql 恰恰需要**按文件内容推断列**，因此必须在绑定阶段给框架补一个"动态列"扩展点。

## 3. 架构

```
SQL: SELECT ... FROM read_csv('/path/a.csv', header = true) f
        │
        ├─ parser: read_csv(...) 作为表函数调用解析（命名参数 → TableFunctionCall.options_）
        ├─ resolver: bind_read_csv()
        │    1. 求值常量参数（路径、选项）
        │    2. 功能开关 + secure_file_priv 校验 + realpath 规范化
        │    3. 打开文件，做一次全量推断（sample_size 只允许 -1）
        │    4. 生成 FileColumnDesc[] → TableItem 的列定义（动态 schema）
        │    5. 解析出错时把坏行的物理行号带进错误信息
        ├─ optimizer: 普通 table scan 接入，投影下推用 column pruning 表达（Parquet 列裁剪）
        └─ executor: FileScanOp
             open()   → 打开 reader，记录 FileStat
             get_next_row() 逐行/按批；每批前 check_unchanged()
             rescan() → 回卷（JOIN 右表、参数化重扫）
             close()  → 释放 reader
```

关键决定：

1. **不新建存储层**：文件扫描就是一类 scan operator，输出 `ObNewRow`，后续算子（过滤/聚合/JOIN/排序）
   完全复用现有实现，因此"JOIN 走 seekdb 原生计划"天然成立。
2. **schema 在 bind 阶段确定**：一次全量推断后固定列集合，避免执行期变更列导致计划失效；
   `sample_size` 仅支持 `-1`（全量），与题目一致。
3. **流式**：reader 只保留 64KB 读缓冲 + 单行缓冲（上限 16MB），批量接口按行拷贝到算子内存，
   不缓存整表。
4. **文件变化检测**：open 时记录 `(dev, ino, size, mtime_ns)`，每批/每次 rescan 前比对，变化即报错，
   保证同一查询内结果自洽。

## 4. SQL 接口

```sql
-- 读取（位置参数：路径；命名参数：选项）
SELECT * FROM read_csv('/data/a.csv', header = true, delimiter = ',', quote = '"',
                       escape = '"', nullstr = 'NULL', sample_size = -1);
SELECT * FROM read_json('/data/a.jsonl', format = 'newline_delimited');
SELECT * FROM read_ndjson('/data/a.jsonl');
SELECT * FROM read_parquet('/data/a.parquet');

-- 文件发现：只发现文件，返回单列 file
SELECT * FROM glob('/data/*.csv');

-- 元数据
DESCRIBE SELECT * FROM read_csv('/data/a.csv');

-- 导出
COPY (SELECT ... ) TO '/data/out.csv' (format csv, header, nullstr 'NULL');
```

约束：`enable_file_sql` 关闭时任何 `read_*`/`glob` 返回 1235（`OB_NOT_SUPPORTED`）；
未知选项按语法错误处理（1064）；`sample_size != -1` 返回 1235。

### 4.1 列命名

- CSV 有表头：用表头；空表头回退 `column{i}`；重复表头按 `name__2`、`name__3` 去重
  （测试期望 `id__2`）。
- CSV 无表头 / JSONL：`column{i}` / 首次出现顺序的字段名。

### 4.2 类型推断（确定性）

候选类型按顺序判定：NULL → BOOLEAN → BIGINT → DOUBLE → DATE → DATETIME → VARCHAR。

- 整数用溢出检查，超出 int64 时按 DOUBLE 处理；浮点拒绝 `inf/nan` 与部分消费。
- DATE 严格 `YYYY-MM-DD` 且校验日历（含闰年）；DATETIME 支持 `YYYY-MM-DD HH:MM:SS[.ffffff]`
  与 `T` 分隔。
- 宽化：`BIGINT + DOUBLE = DOUBLE`，`DATE + DATETIME = DATETIME`，其它冲突 → VARCHAR。
- 空字段/NULL 不参与宽化，但把列标记为 nullable；整列全 NULL 时类型取 VARCHAR。
- JSONL 按 JSON 原生类型：number → BIGINT/DOUBLE，boolean → BOOLEAN，string → VARCHAR，缺失 → NULL。

### 4.3 CSV

RFC4180：引号包裹、`""` 转义、字段内换行、CRLF/LF、无尾换行的最后一行；坏行（列数与表头不一致）
报错并带物理行号；空行跳过；NULL 由 `nullstr`（默认空串）决定。

### 4.4 JSONL

仅接受扁平对象；出现嵌套对象/数组直接报错并给出行号；字段并集按首次出现顺序，
缺失字段补 NULL。

### 4.5 Parquet

读 Footer 的 Thrift Compact 协议 → FileMetaData/SchemaElement/RowGroup/ColumnChunk；
按 RowGroup 分页读，支持 PLAIN / RLE_DICTIONARY、可空列（Definition/Repetition Level）、
列裁剪（只解码投影列）；需要自带 snappy 解压（可选 zstd 复用仓库 `zstd_1_3_8`）。

## 5. 安全与稳定性

| 项 | 做法 |
| --- | --- |
| 功能开关 | `enable_file_sql` 会话变量，默认 OFF |
| 路径限制 | 绝对路径 + `realpath` 规范化后做前缀匹配（含 `/` 边界），空 `secure_file_priv` 直接拒绝 |
| 越界 | 目标文件/目录必须在 `secure_file_priv` 之下；COPY 校验父目录 |
| 文件变化 | `(dev, ino, size, mtime_ns)` 比对，变化返回错误 |
| 资源上限 | 单行 16MB、列数 4096、按批取数，不物化全文件 |
| COPY | `O_CREAT|O_EXCL` 保证目标不存在即失败；失败路径调用 `abort()` 删除半成品 |

## 6. 当前进度

已完成（`src/sql/engine/basic/ob_file_scan_utils.{h,cpp}`，约 1200 行，尚未加入构建清单）：

- `FileScanUtils`：路径校验（读/写两种）、glob、stat、七类类型推断与宽化、值转换、NULL 判定。
- `CsvFileReader`：流式 RFC4180 解析、表头去重、全量 schema 推断、批量取数、rescan、变化检测。
- `CsvFileWriter`：COPY 用的 CSV 写出（表头、分隔符、引号转义、NULL 文本、独占创建、失败清理）。

未完成：

1. 合并 `feat/table-functions-schema-free` 作为 `FROM f(...)` 入口，并给框架加动态列扩展点。
2. parser：`read_csv/read_json/read_ndjson/read_parquet/glob` 语法、命名参数、`DESCRIBE SELECT`、
   `COPY ... TO ... (format csv, header, nullstr ...)`。
3. resolver/executor：`FileScanOp`、绑定期推断、投影下推、错误码映射（1235/1064/越权）。
4. JSONL reader（union schema + 嵌套拒绝）与 Parquet reader（thrift + snappy）。
5. `enable_file_sql` 系统变量与 `secure_file_priv` 的读取。
6. 单测：CSV 推断/读取选项、JSONL 并集、变化检测、Parquet 编码与 snappy。

## 7. 与测试的对应

`tools/deploy/mysql_test/test_suite/file_sql/t/file_sql_basic.test` 的每条语句与实现点的对应：

| 语句 | 依赖 |
| --- | --- |
| `select @@session.enable_file_sql` | 系统变量注册 |
| 关闭态 `read_csv` 报 1235 | resolver 开关检查 |
| `read_csv(..., 'xml')` 报 1064 | parser 拒绝位置参数形式的选项 |
| `sample_size = 10` 报 1235 | resolver 选项校验 |
| 多行引号/重复表头/DATE/DATETIME/BOOL | CSV reader + 推断（已具备） |
| `read_json(..., format='newline_delimited')` | JSONL reader |
| `describe select * from read_csv(...)` | bind 期 schema + DESCRIBE 输出 |
| `read_parquet` 聚合 | Parquet reader + 投影下推 |
| `glob('dir/*')` | glob 表函数（依赖 FileScanUtils::glob_files） |
| `COPY (...) TO ... (header, nullstr)` + 读回 | COPY 语法 + CsvFileWriter |

## 8. 风险

- 表函数框架分支与本题基线同源，合并冲突可控；但它同时迁移了虚拟表，需要在合并后跑一遍
  现有 mysqltest 回归。
- Parquet 需要引入 snappy/thrift 依赖，构建改动比功能改动更难验证。
- 本环境无法编译 seekdb（bazel 全量依赖 + 8GB 内存不足），上述代码未经编译验证。
