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

## 当前状态

| 阶段 | 状态 | 说明 |
| --- | --- | --- |
| 会话开关 | 已完成 | `enable_file_sql` 默认关闭 |
| 路径安全 | 已完成 | 单元测试 43 项通过 |
| 读取器接口 | 已完成 | CSV 适配器已接 B 的 `CsvTableScan` |
| SQL 语法 | 已完成 | `table_factor` 增加表函数产生式，bison 2.4.1 `-Werror` 无冲突 |
| 解析与 Schema 推断 | 已完成 | 生成表项与列项，列类型来自推断结果 |
| 值转换 | 已完成 | `file_sql_scan` 把推断类型映射为列类型，把扫描值写入 ObDatum |
| 计划与执行算子 | 已完成 | `ObLogFileTable` + `ObFileTableOp`，按批次流式读取 |
| 列的可见性 | 已完成 | `SELECT *` 展开与 `f.col` 限定列解析 |
| `glob`、`DESCRIBE`、`COPY` | **未完成** | 属于后续评分项 |

`read_csv` 的读取路径已经打通：`read_csv(path)` 会被解析成表项，
解析阶段推断 Schema 并检查路径，执行算子按批次读取文件并写入列。
本分支还没有 `glob`、`DESCRIBE` 与 `COPY`，也没有 JSONL 与 Parquet 的读取器，
不得当作 Lab 2 的完整实现。

还没有验证的部分：

- 完整的 `file_sql_basic.test` 还没有跑通，`read_csv` 的端到端行为需要在集成机上验证。
- 文件在计划期与执行期各打开一次，两次之间文件发生变化时算子会报错，
  这个路径还没有测试覆盖。
- 表函数的语句目前不可比较，计划缓存不会复用文件表语句的计划，
  每次执行都会重新推断 Schema。
- 路径在解析阶段校验一次，算子执行时重新打开文件，两次之间文件被换成指向别处的
  软链接时不会被发现。执行前再校验一次路径可以关闭这个窗口，属于下一步。
- `enable_file_sql` 与 `secure_file_priv` 都是会话级变量，`global_enable_file_sql`
  与 `global_secure_file_priv` 还没有实现，隐藏测试用到全局变量时需要补上。

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

生成文件由构建生成，不需要提交：`sql_parser_mysql_mode_tab.c`、`sql_parser_mysql_mode_tab.h`、
`sql_parser_mysql_mode_lex.c` 与 `type_name.c` 都在 `.gitignore` 中，
`src/sql/CMakeLists.txt` 在配置阶段调用 `gen_parser.sh` 重新生成。
该脚本使用 `bison -Werror`，出现语法冲突时构建会直接中止，
因此新增产生式必须先用 `deps/3rd/usr/local/oceanbase/devtools/bin/bison`（2.4.1）验证。
手动重新生成：

```bash
cd src/sql/parser && NEED_PARSER_CACHE=OFF bash gen_parser.sh
```

### 计划与算子的落点清单

按 `JSON_TABLE` 的既有路径实现，每一步都有可直接照搬的参照：

| 步骤 | 文件 | 参照 |
| --- | --- | --- |
| 逻辑算子类型 | `src/sql/optimizer/ob_log_operator_factory.h` | `LOG_OP_DEF(LOG_JSON_TABLE, "JSON_TABLE")` |
| 逻辑算子 | 新建 `src/sql/optimizer/ob_log_file_table.{h,cpp}` | `ObLogJsonTable` |
| 逻辑算子分配 | `src/sql/optimizer/ob_log_operator_factory.cpp` | `case LOG_JSON_TABLE` |
| 访问路径类型 | `src/query/api/query/ob_sql_define.h` | `JSON_TABLE_ACCESS` |
| 访问路径 | `src/sql/optimizer/ob_join_order.h` | `class JsonTablePath` |
| 访问路径生成 | `src/sql/optimizer/ob_join_order.cpp` | `ObJoinOrder::generate_json_table_paths` |
| 基表类型分发 | `src/sql/optimizer/ob_join_order.cpp` | `init_base_join_order` 与 `generate_base_paths` |
| 逻辑算子实例化 | `src/sql/optimizer/ob_log_plan.cpp` | `ObLogPlan::allocate_json_table_path` |
| 物理算子类型 | `src/query/api/query/engine/ob_phy_operator_type.h` | `PHY_OP_DEF(PHY_JSON_TABLE)` |
| 算子注册 | `src/query/api/query/engine/ob_operator_reg.h` | `REGISTER_OPERATOR(ObLogJsonTable, PHY_JSON_TABLE, ...)` |
| 算子与 spec | 新建 `src/sql/engine/basic/ob_file_table_op.{h,cpp}` | `ObJsonTableOp` 与 `ObJsonTableSpec` |
| plan 到 spec | `src/sql/code_generator/ob_static_engine_cg.cpp` | `generate_spec(ObLogJsonTable &op, ObJsonTableSpec &spec, ...)` |
| 逻辑算子到物理算子 | `src/sql/code_generator/ob_static_engine_cg.cpp` | `LOG_JSON_TABLE` 分支 |

算子的关键点：

- spec 需要携带规范路径、格式、选项和输出列表达式，输出列表达式取自语句中该表项的列项，
  与 `ObJsonTableSpec::column_exprs_` 的生成方式相同。
- 算子打开文件、创建读取器并执行 `prepare()`，逐批取行并写入
  `expr->locate_datum_for_write(eval_ctx_)`。
- 字符串列的文本必须复制到算子自己的分配器中，且分配器按批次重置，
  否则整列数据会随文件大小增长，违反流式扫描的要求。
- 文件在计划期已推断过一次 Schema，算子执行时重新推断可以发现文件变化，
  变化时必须报错而不是继续使用旧 Schema 读新数据。

## 风险

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| `DESCRIBE` 语句在本版本解析器中不存在 | ① 的 `DESCRIBE` 需要新增语句类型 | 与表函数入口一起实现，先保证 `SHOW`/`DESC` 的等价输出 |
| `BOOLEAN` 不是 OB 原生列类型 | `DESCRIBE` 期望输出 `BOOLEAN` | 列类型按 `tinyint(1)` 承载，展示层输出 `BOOLEAN` |
| 计划缓存复用 | 开关与路径变化后计划失效 | 开关带 `INFLUENCE_PLAN`，路径不入计划缓存键，表函数语句不进入计划缓存 |
| 解析期打开文件 | 计划阶段即访问文件系统 | 只在开关打开且路径校验通过后打开；失败返回明确错误，不产生半成品计划 |
