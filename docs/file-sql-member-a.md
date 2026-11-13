# Lab 2 Part A：文件即数据库公共框架接入说明

## 范围与状态

本实现对应团队计划的① SQL 接口与文件发现、⑥ 单文件 SQL、⑧ 安全、稳定性与工程质量，
并提供 B、C 复用���公共出口：表函数入口、`enable_file_sql` 开关、路径校验与格式读取器接口。

当前分支 `lab2/file-discovery` 基于 `2026-competition`（叠加 sxy-xd 的 `lab2/csv` 以便编译与联调）。
本提交已完成下列可独立验证的部分：

| 模块 | 文件 | 状态 |
| --- | --- | --- |
| 会话开关 | `src/share/system_variable/ob_system_variable_init.json` 及生成文件 | 已完成 |
| 路径安全 | `src/sql/engine/basic/file_sql/file_sql_path.{h,cpp}` | 已完成，含单元测试 |
| 读取器接口 | `src/sql/engine/basic/file_sql/file_sql_reader.{h,cpp}` | 已完成，CSV 适配器已接 B 的模块 |
| SQL 入口与执行算子 | 解析、解析器、计划、算子 | 进行中 |

未完成的 SQL 入口部分不得被当作 `file_sql_basic.test` 或 Lab 2 官方评测通过。
本分支尚未包含 `read_csv` 的 SQL 语法，表函数暂时只能通过模块接口调用。

来源：

- [团队分工](https://github.com/xiaoheto/dataforge-openbase-comp/blob/main/docs/lab-2-execution-plan.md)
- [B 的 CSV 与类型推断接入说明](docs/file-sql-member-b.md)
- 官方 `tools/deploy/mysql_test/test_suite/file_sql/` 数据与测试期望。

官方文字同时出现 50 分与“沿用 30 分”，评分表相加为 50；本提交不自行解释评分口径。

## `enable_file_sql` 开关

新增会话与全局变量 `enable_file_sql`，布尔类型，默认 `0`。

- 变量定义在 `src/share/system_variable/ob_system_variable_init.json`，元数据与工厂类由仓库自带的
  `src/share/system_variable/gen_ob_sys_variables.py` 生成；该脚本使用 Python 2 的 `iteritems()`，
  在 Python 3 下需要替换为 `items()` 后运行，运行目录为 `src/share/system_variable`。
- `src/share/system_variable/ob_system_variable_init.cpp` 的变量初始化块按仓库既有排版手工补充，
  避免重新生成带来的整文件差异。
- 会话读取接口为 `ObBasicSessionInfo::get_enable_file_sql(bool &)`。
- 关闭时调用表函数返回 `1235`（`OB_ERR_NOT_SUPPORTED`）；开启方式为 `set session enable_file_sql = ON`。
- 该变量带 `INFLUENCE_PLAN` 标记：开关值不同，生成的执行计划不能复用。

## 路径安全（⑧）

公共出口：`src/sql/engine/basic/file_sql/file_sql_path.{h,cpp}`。
格式模块不得自行打开路径，只能接收框架已经打开并授权的 `FILE *`。

| 函数 | 语义 |
| --- | --- |
| `canonicalize_path` | `realpath` 解析，得到绝对规范路径；软链接按目标判定 |
| `lexical_canonicalize_path` | 不访问文件系统的规范化，用于通配模式 |
| `path_is_under` | 判断规范路径是否位于允许目录内，分隔符边界必须匹配 |
| `check_secure_path` | 唯一入口：规范化并校验是否位于 `secure_file_priv` 之内 |
| `expand_glob_pattern` | 展开通配模式，每个匹配项都必须仍在允许目录内 |

约定：

- 校验失败时不返回任何可用路径，`canonical` 输出被清空。
- 允许目录之外的软链接按其目标判定，越界即拒绝；`secure_file_priv` 不可用时拒绝全部访问。
- `glob()` 展开中只要有一个匹配项越界，整次展开失败，不返回部分结果，避免隐藏越界文件。
- 目录不是可读表，`canonicalize_path` 返回 `NOT_REGULAR_FILE`。

单元测试：`unittest/sql/engine/basic/test_file_sql_path.cpp`，覆盖目录边界、相对路径规范化、
越界拒绝、软链接逃逸、软链接指向允许目录、通配展开与越界通配失败。

## 格式读取器接口（B、C 接入点）

公共出口：`src/sql/engine/basic/file_sql/file_sql_reader.h`。

```cpp
// 一行数据，值的类型来自 B 的共享类型模块。
using FileTableRow = std::vector<ScalarValue>;

class FileTableScanner
{
public:
  virtual ~FileTableScanner() = default;
  // 全量推断 Schema；句柄归调用方所有，扫描期间必须保持有效。
  virtual FileStatus prepare(FILE *input, const FileScanOptions &options) = 0;
  // 产出不超过 limit 行；END 表示读完，其他非 OK 状态必须向上传播。
  virtual FileStatus next_batch(size_t limit, std::vector<FileTableRow> &rows) = 0;
  // 回到数据起点，不重新推断 Schema。
  virtual void rescan() = 0;
  virtual const std::vector<ColumnSchema> &columns() const = 0;
};

FileStatus register_file_scanner(FileFormat format, FileScannerFactory factory);
FileTableScanner *create_file_scanner(FileFormat format);
```

接入约定：

- 格式模块只负责把一个已打开的文件变成 Schema 和行批次，不做路径判断、不做 SQL 语义、不做过滤与聚合。
- CSV 已有实现：B 的 `CsvTableScan` 与该接口一致，A 在 `file_sql_reader.cpp` 中提供了适配器
  `CsvFileScanner`，只负责把 `FileScanOptions` 映射到 `CsvOptions`。
- C 的 JSONL 与 Parquet 模块实现同一接口，并用 `register_file_scanner` 注册，
  注册后框架用 `create_file_scanner` 获取实例；未注册时框架返回“该格式不可用”。
- 行值使用 B 的 `ScalarValue`：`BIGINT` 用 `integer`，`DOUBLE` 用 `real`，`BOOLEAN` 用 `integer`
  （0/1），其余类型用 `text`；`DATETIME` 的文本以空格分隔。SQL 适配层负责写入 ObDatum。
- 内存占用只与列数和批次大小相关，不得随文件总大小增长；每批次结束后上一批的内存必须可复用。

## SQL 表面

计划中的表函数签名（与官方公开测试一致）：

```sql
select * from read_csv('/path/basic.csv') f;
select * from read_csv('/path/basic.csv', header = true, sample_size = -1) f;
select * from read_json('/path/basic.jsonl', format = 'newline_delimited') f;
select * from read_parquet('/path/simple.parquet') p;
select * from glob('/path/*');
describe select * from read_csv('/path/basic.csv');
```

约束：

- 第一个位置参数是路径，其余参数必须具名；位置参数只接受字符串常量路径。
- `read_csv(path, 'xml')` 属于语法错误，返回 `1064`。
- `sample_size` 只支持 `-1`，其它值返回 `1235`。
- `glob()` 只返回一列 `file`，值为文件完整路径。
- `DESCRIBE` 返回 `column_name`、`column_type`、`null`、`key`、`default`、`extra` 六列。

## 引擎接入路径

seekdb 没有通用的表函数框架，`read_csv` 这类 FROM 子句中的表函数按 OB 现有的
`JSON_TABLE`/`UNNEST` 路径实现，即“表项 + 动态列 + 独立算子”：

| 阶段 | 改动位置 | 说明 |
| --- | --- | --- |
| 解析 | `src/sql/parser/sql_parser_mysql_mode.y` | `table_factor` 增加表函数产生式，参数支持具名形式 |
| 解析节点 | `src/query/api/query/parser/ob_item_type.h` | 新增表函数节点类型，`type_name.c` 由 `gen_type_name.sh` 生成 |
| 解析器 | `src/sql/resolver/dml/ob_dml_resolver.cpp` | 校验开关与参数，推断 Schema，生成表项与列项 |
| 计划 | `src/sql/optimizer/ob_log_file_table.{h,cpp}`、`ob_join_order.cpp` | 新增逻辑算子与访问路径 |
| 执行 | `src/sql/engine/basic/ob_file_table_op.{h,cpp}` | 流式读取并写入输出 ObDatum |
| 注册 | `src/query/api/query/engine/ob_operator_reg.h`、`ob_phy_operator_type.h` | 物理算子注册 |

生成文件必须一并提交：仓库构建使用已生成的 `sql_parser_mysql_mode_tab.c`，
修改语法后需要用 `deps/3rd/usr/local/oceanbase/devtools/bin/bison`（2.4.1）与 flex 重新生成，
`gen_parser.sh` 在出现冲突时会中止，因此新增产生式不能引入新的语法冲突。

## 风险

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| `DESCRIBE` 语句在本版本解析器中不存在 | ① 的 `DESCRIBE` 需要新增语句类型 | 与表函数入口一起实现，先保证 `SHOW`/`DESC` 的等价输出 |
| `BOOLEAN` 不是 OB 原生列类型 | `DESCRIBE` 期望输出 `BOOLEAN` | 列类型按 `tinyint(1)` 承载，展示层输出 `BOOLEAN` |
| 计划缓存复用 | 开关与路径变化后计划失效 | 开关带 `INFLUENCE_PLAN`，路径不入计划缓存键，表函数语句不进入计划缓存 |
| 解析期打开文件 | 计划阶段即访问文件系统 | 只在开关打开且路径校验通过后打开；失败返回明确错误，不产生半成品计划 |
