# Task3 自定义全文词典补全：任务分析与设计

## 目标与边界

以当前仓库为基线，将 `task3.md` 中的自定义 IK 主词典、停用词词典、量词词典补全为可用的端到端能力。用户通过 `FULLTEXT_DICT='Y'` 创建受约束的词典表，通过 FULLTEXT 索引的 `PARSER_PROPERTIES` 或 `TOKENIZE()` 的 `additional_args` 关联词典，并以 `ALTER SYSTEM REFRESH FULLTEXT DICT` 显式刷新缓存。

本工作只实现 Task3 所述的 MySQL 模式能力；不重构内置 IK 词典、不改变既有全文索引结构、不引入第二套分词缓存。

**测试约束：不创建、修改或更新任何测试文件。** 验收唯一使用已存在的 `tools/deploy/mysql_test/test_suite/ai_funcs/t/ik_custom_dict.test` 及其当前 `.result`。

## 当前基线与缺口

| 链路 | 已存在 | 缺口 / 缺陷 |
| --- | --- | --- |
| 索引属性语法 | `dict_table`、`stopword_table`、`quantifier_table` 已能生成 `T_PARSER_*` 节点 | 尚未在索引创建时验证被引用的表；`quantifier_table` 的 JSON 常量拼写为 `quanitfier_table`，需做兼容修正。 |
| 属性 JSON | `ObFTParserJsonProps` 已能序列化/读取三个属性 | `ObFTParserProperty::parse_for_parser_helper()` 把成员设置为键名而非 JSON 的表名值，运行时永远拿不到用户配置。 |
| 词典缓存 | 内置 IK 词典可从文本/内表构建 DAT 缓存 | `ObFTDictHub` 只以 `dict_type` 标识缓存；`ObIKFTParser::init_dict()` 固定 `main_dict` 等名字；无法并存多个用户词典、无法按刷新版本失效。 |
| 用户表读取 | `ObFTDictTableIter` 可读取列 `word` | 查询固定为 `oceanbase.<table_name>`，不能支持用户数据库、限定名或安全引用。 |
| 词典表 DDL | 无 | 缺少 `FULLTEXT_DICT` 语法、schema 持久化、列/字符集/IOT 等建表校验。 |
| 刷新命令 | 无 | `ALTER SYSTEM REFRESH FULLTEXT DICT` 没有 parse node、resolver、stmt/operator、RPC 或各 server 缓存失效路径。 |
| 依赖保护 | 无 | 未阻止已被全文索引引用的词典表 DROP、RENAME、ADD/MODIFY/DROP/RENAME COLUMN。 |

## 设计决策

采用一套以 **词典表 schema ID + 词典类型 + 版本** 作为身份的共享缓存。

- schema ID 让同名但不同数据库的表不会冲突；词典类型区分主词典、停用词、量词。
- 每次 refresh 读取词典表的当前 schema/data 版本（具体采用现有 schema-version 与刷新代次字段），在所有 observer 上清除该身份的 KV cache 及 `ObFTDictHub` 元数据；下一次索引写入或 `TOKENIZE()` 惰性重建 DAT。
- 全文索引创建时立即解析并校验所有指定词典，且主动构建/装载一次缓存，满足“建索引无需先 refresh”的要求。
- 词典表引用在 schema 层以规范化的 `(database_id, table_id)` 保存/可反查；不要用裸字符串做 DROP/ALTER 依赖判断。

不采用“仅让现有正向测试通过”的最小修补：它会漏掉 Task3 明定的词典表约束、跨库表名、DDL 保护和刷新语义。也不采用独立于 `ObFTDictHub` 的新缓存，以免 `TOKENIZE()` 与全文索引出现不一致的词典视图。

## 目标数据流

```text
CREATE DICT TABLE                 CREATE / ALTER FULLTEXT INDEX
  FULLTEXT_DICT='Y'                         |
         |                                  v
parser -> table schema flag          parse properties -> resolve dictionary names
         |                                  |
         +--------------------------> validate dictionary schemas
                                             |
ALTER SYSTEM REFRESH ... -> broadcast invalidation -> ObFTDictHub / ObDictCache
                                             |
FULLTEXT write or TOKENIZE -> ObIKFTParser::init_dict()
                                             |
       normalized dictionary refs -> table iterator -> DAT range cache -> IK segmentation
```

## 新增与修改位置

## 待修改 / 待实现接口清单

本节是实施的接口契约。除特别标注“修改现有”外，均为待新增接口；参数类型遵循本仓库现有的 `ObString`、`ObIArray`、`ObTableSchema` 与 `ObFTDictType` 用法。实施时不得另建同职责接口。

### A. 表 schema 与建表校验

```cpp
// src/share/schema/ob_table_schema.h，ObTableSchema：新增持久化状态访问器
void set_fulltext_dict_table(const bool is_dict);
bool is_fulltext_dict_table() const;

// src/sql/resolver/ddl/ob_ddl_resolver.h/.cpp，ObDDLResolver：新增私有 helper
int resolve_fulltext_dict_option_(const ParseNode &option_node,
                                  bool &is_fulltext_dict);

// src/sql/resolver/ddl/ob_create_table_resolver.h/.cpp，ObCreateTableResolver：新增私有 helper
int check_fulltext_dict_table_(const share::schema::ObTableSchema &table_schema) const;
```

- `set_fulltext_dict_table` 在 `resolve_table_option()` 成功解析 `FULLTEXT_DICT='Y'` 后调用，并必须进入 schema 的 assign、serialize/deserialize 与 schema SQL DML。
- `resolve_fulltext_dict_option_` 输入 `T_FULLTEXT_DICT` parse node；仅当值为 `Y` 时输出 `true`，否则返回用户可见的非法 table option 错误。
- `check_fulltext_dict_table_` 不修改 schema，只校验：单列 `word`、varchar(1..500)、主键、utf8mb4、IOT；失败时直接阻断 CREATE TABLE。

### B. 词典引用解析与属性 JSON

```cpp
// src/sql/resolver/ddl/ob_fts_parser_resolver.h/.cpp：新增静态 helper
static int resolve_and_validate_dict_table_(
    ObSchemaChecker &schema_checker,
    const common::ObString &raw_table_name,
    const storage::ObFTDictType dict_type,
    const uint64_t tenant_id,
    const uint64_t current_database_id,
    common::ObString &canonical_table_name,
    uint64_t &dict_table_id);

// src/storage/fts/ob_fts_parser_property.h/.cpp：修改现有接口的行为
int ObFTParserProperty::parse_for_parser_helper(
    const ObFTParser &parser, const common::ObString &json_str);
```

- `resolve_and_validate_dict_table_` 负责将 `table` 或 `db.table` 解析为全限定、已转义的名称，并返回 table ID；它检查表存在、`is_fulltext_dict_table()` 为真及表结构仍合法。三个 `T_PARSER_*_TABLE` 分支均调用该接口。
- `parse_for_parser_helper` 必须把 JSON 中的值赋给 `dict_table_`、`stopword_table_`、`quantifier_table_`；不得再赋值为 `"dict_table"` 等键名。读取时兼容历史拼写 `quanitfier_table`，写出时仅使用 `quantifier_table`。

### C. 词典描述、用户表读取和缓存

```cpp
// src/storage/fts/dict/ob_ft_dict_def.h：扩展已有 struct/class
class ObFTDictDesc {
public:
  ObFTDictDesc(const common::ObString &name, storage::ObFTDictType type,
               common::ObCharsetType charset, common::ObCollationType coll_type,
               uint64_t tenant_id, uint64_t table_id, int64_t version,
               bool is_builtin);
  bool is_builtin() const;
  uint64_t tenant_id_;
  uint64_t table_id_;
  int64_t version_;
  bool is_builtin_;
};

// src/storage/fts/dict/ob_ft_dict_table_iter.h/.cpp：替换现有 init
int ObFTDictTableIter::init(const ObFTDictDesc &dict_desc);

// src/storage/fts/dict/ob_ft_range_dict.h/.cpp：新增用户表构建入口
static int ObFTRangeDict::build_cache_from_table(
    const ObFTDictDesc &dict_desc, ObFTCacheRangeContainer &range_container);

// src/storage/fts/dict/ob_ft_dict_hub.h/.cpp：新增失效接口，修改 key 语义
int ObFTDictHub::invalidate_cache(const ObFTDictDesc &dict_desc);
```

- `ObFTDictTableIter::init` 只接受已完成 schema 校验的 descriptor，使用其中的全限定库表名生成 `SELECT word ... ORDER BY word`；不能再硬编码 `oceanbase.` 或拼接未经引用处理的字符串。
- `build_cache_from_table` 以 iterator 读取用户表并复用现有 `build_ranges` / DAT 构建逻辑。
- `ObFTDictHub::invalidate_cache` 删除 `(tenant_id, table_id, dict_type, version)` 相关元数据与 KV cache；内置词典不受该接口影响。
- `ObDictCacheKey` 与 `ObFTDictInfoKey` 的现有构造、`hash`、`operator==`、深拷贝接口必须同步扩展为使用词典身份和版本，不能继续仅以 `dict_type` 区分。

### D. IK 分词器接入

```cpp
// src/storage/fts/ob_ik_ft_parser.h/.cpp：新增 private helper
int ObIKFTParser::build_dict_descs_(
    const ObFTParserProperty &property,
    ObFTDictDesc &main_dict_desc,
    ObFTDictDesc &quantifier_dict_desc,
    ObFTDictDesc &stopword_dict_desc);

// src/storage/fts/ob_ik_ft_parser.h/.cpp：修改现有签名
int ObIKFTParser::init_single_dict(const ObFTDictDesc &desc,
                                   ObFTCacheRangeContainer &container);
```

- `build_dict_descs_` 将 `ObFTParserProperty` 的三项真实配置转换为 descriptor；未配置时返回对应内置词典 descriptor。
- `init_dict()` 改为先调用 `build_dict_descs_`，再依次调用 `init_single_dict`。现有分词器、range container 与 segmenter 接口不变。

### E. 刷新 SQL、执行与 observer 广播

```cpp
// src/sql/resolver/ddl/ob_refresh_fulltext_dict_stmt.h/.cpp：新建
class ObRefreshFulltextDictStmt final : public ObStmt {
public:
  ObRefreshFulltextDictStmt();
  uint64_t get_dict_table_id() const;
  void set_dict_table_id(uint64_t table_id);
  // 同时持有 tenant_id、database_id、规范化 table_name。
};

// src/sql/resolver/ddl/ob_ddl_resolver.h/.cpp：新增 resolver 入口
int ObDDLResolver::resolve_refresh_fulltext_dict_(
    const ParseNode &parse_tree, ObRefreshFulltextDictStmt &stmt);

// src/sql/engine/cmd/ob_refresh_fulltext_dict_executor.h/.cpp：新建
class ObRefreshFulltextDictExecutor {
public:
  static int execute(ObExecContext &ctx, ObRefreshFulltextDictStmt &stmt);
};

// src/share/ob_rpc_struct.h/.cpp：新建 RPC 参数
struct ObRefreshFulltextDictArg : public obrpc::ObRpcArg {
  OB_UNIS_VERSION(1);
  uint64_t tenant_id_;
  uint64_t table_id_;
  int64_t refresh_version_;
  bool is_valid() const;
};

// src/rootserver/ob_root_service.h/.cpp：新增 root RPC handler
int ObRootService::refresh_fulltext_dict(const obrpc::ObRefreshFulltextDictArg &arg);
```

- `resolve_refresh_fulltext_dict_` 解析单/双引号与带/不带库名，检查目标必须是词典表，并把稳定的 table ID 写入 stmt。
- executor 不直接操作本机缓存；它向 root service 发 RPC。
- root service 生成单调递增 `refresh_version_`，将其广播至所有 observer；observer processor 用同一 descriptor 调用 `ObFTDictHub::invalidate_cache`。广播接口和 processor 的命名、注册位置须遵循仓库现有 root-to-observer RPC 模式。

### F. DDL 依赖保护

```cpp
// src/share/schema/ob_schema_service.h/.cpp：新增查询接口
int ObSchemaService::get_fulltext_indexes_referencing_dict_(
    uint64_t tenant_id, uint64_t dict_table_id,
    common::ObIArray<uint64_t> &index_table_ids) const;

// src/rootserver/ob_ddl_service.h/.cpp：新增统一校验接口
int ObDDLService::check_fulltext_dict_ddl_allowed_(
    const share::schema::ObTableSchema &dict_table_schema,
    const ObDictTableDdlOperation operation) const;
```

- `get_fulltext_indexes_referencing_dict_` 扫描 FULLTEXT 索引 parser JSON 中规范化的词典引用并返回引用者 ID。
- `check_fulltext_dict_ddl_allowed_` 由 DROP TABLE、RENAME TABLE 和 ALTER TABLE 的提交前路径调用；有引用时 DROP 返回 4179，其他受限 DDL 返回 1235。INSERT/UPDATE/DELETE 不调用此接口。

### G. TOKENIZE 复用接口

```cpp
// src/sql/engine/expr/ob_expr_tokenize.h/.cpp：修改现有成员函数行为
int ObExprTokenize::TokenizeParam::reform_parser_properties(
    const common::ObString &properties);
```

该函数必须复用 B 节的属性规范化/词典引用校验结果，确保 `additional_args` 使用的 JSON 与 DDL 持久化 JSON 相同；禁止在 `ob_expr_tokenize.cpp` 重新实现表名解析、表结构校验或缓存构建。

### 1. SQL 语法与 parse node

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/sql/parser/sql_parser_mysql_mode.y` | 在非保留关键字、`table_option` 产生式中加入 `FULLTEXT_DICT = 'Y'`；在 `alter_system_stmt` 中加入 `SYSTEM REFRESH FULLTEXT DICT relation_factor`。 | 新产生式生成 `T_FULLTEXT_DICT`；刷新语句生成 `T_REFRESH_FULLTEXT_DICT`，子节点保存带/不带库名及双引号引用后的 relation name。 |
| `src/sql/parser/ob_item_type.h`、`src/objit/include/objit/common/ob_item_type.h` | 分配上述 parse node 类型。 | 新增 `T_FULLTEXT_DICT`、`T_REFRESH_FULLTEXT_DICT` 枚举值。 |
| 由 parser 构建规则生成的 `sql_parser_mysql_mode_tab.*`、`sql_parser_mysql_mode_lex.c`、`non_reserved_keywords_mysql_mode.c`、`type_name.c` | 不手工编辑；执行项目既有 parser 生成目标。 | 将 `FULLTEXT_DICT`、语法 token 和 item type 同步为生成物。 |

### 2. 词典表 schema 标记及创建校验

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/share/schema/ob_table_schema.h/.cpp` | 在可持久化 table schema 属性中增加 `is_fulltext_dict_table` 标记，并纳入初始化、`assign`、序列化/反序列化、schema 比较与展示。 | `set_fulltext_dict_table(bool)`、`is_fulltext_dict_table() const`。 |
| `src/sql/resolver/ddl/ob_ddl_resolver.h/.cpp` | 解析 `T_FULLTEXT_DICT`，仅接受字符串 `Y`（大小写不敏感）；将标记传入待建表 schema。 | 在 `resolve_table_option()` 的 switch 新增分支；新增私有辅助 `resolve_fulltext_dict_option_(const ParseNode &, bool &)`。 |
| `src/sql/resolver/ddl/ob_create_table_resolver.h/.cpp` | 在全部列、主键、表 option、字符集都已解析后执行词典表结构校验。 | 新增 `check_fulltext_dict_table_(const ObTableSchema &) const`：仅一列且名为 `word`、该列为主键、varchar 长度 1–500、utf8mb4、IOT；不满足返回与 Task3 对应的用户错误。 |
| `src/share/schema/ob_table_sql_service.cpp`（及相应 schema service 序列化路径） | 将新标记写入/读出 `__all_table` 历史与当前 schema 记录。 | 在现有 table schema DML/抽取字段中加 `is_fulltext_dict_table`；若 inner table 字段由定义脚本生成，则修改 `src/share/inner_table/ob_inner_table_schema_def.py` 后生成对应 `ob_inner_table_schema.*` 文件。 |

### 3. 词典引用规范化、校验与索引属性持久化

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/storage/fts/ob_fts_literal.h` | 修正 `CONFIG_NAME_QUANTIFIER_TABLE` 的拼写，并保留旧键作为读取兼容别名。 | 新增 `CONFIG_NAME_QUANTIFIER_TABLE_LEGACY`。 |
| `src/storage/fts/ob_fts_parser_property.h/.cpp` | 让 parser property 保存三个 JSON 值，而不是常量键名；IK DDL rebuild 正确检查 stopword，而非第二次检查 quantifier。 | 在 `parse_for_parser_helper()` 调用 `config_get_dict_table()`、`config_get_stopword_table()`、`config_get_quantifier_table()`；新增 `normalize_ik_dict_properties(...)`（或等价 helper）用于旧 JSON 键兼容与默认表填充。 |
| `src/sql/resolver/ddl/ob_fts_parser_resolver.h/.cpp` | 解析每个 `db.table` / `table` 字符串，省略 db 时使用 session 当前库；查 schema 并验证 `is_fulltext_dict_table()`。将规范化的 table ID/全限定表名写回 parser JSON。 | 新增 `resolve_and_validate_dict_table_(const ObString &raw_name, ObFTDictType type, ObString &canonical_name)`；由 `resolve_fts_index_parser_properties()` 的三个词典分支调用。 |
| `src/sql/resolver/ddl/ob_create_index_resolver.cpp`、`ob_alter_table_resolver.cpp`、`ob_create_table_resolver.cpp` | 在三个 FULLTEXT 索引入口统一调用上述校验；创建成功前触发词典预加载。 | 复用 `ObFTParserResolverHelper::resolve_parser_properties()`，不得复制三套校验。 |

### 4. 用户词典缓存与 IK 分词接入

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/storage/fts/dict/ob_ft_dict_def.h` | 扩展 `ObFTDictDesc`，携带 dictionary table ID、规范化全限定名和 refresh/version；内置词典继续使用固定身份。 | 新增字段 `table_id_`、`version_`、`is_builtin_` 与比较/有效性辅助。 |
| `src/storage/fts/dict/ob_ft_cache.h` | 缓存 key 从仅 `name + type + range_id` 改为包含词典唯一身份与版本。 | 扩展 `ObDictCacheKey` 的构造、hash、比较、深拷贝和序列化字段。 |
| `src/storage/fts/dict/ob_ft_dict_hub.h/.cpp` | `ObFTDictInfoKey` 改为 `(tenant_id, table_id, dict_type)`；支持按一个词典失效以及按描述构建/读取缓存。 | 新增 `invalidate_cache(const ObFTDictDesc &)`；调整 `build_cache()` 与 `load_cache()`，只对内置词典走 `build_cache_from_ik_dict()`，用户表走表扫描构建。 |
| `src/storage/fts/dict/ob_ft_range_dict.h/.cpp` | 支持从 `ObFTDictDesc` 指向的用户表建立 range/DAT。 | 新增 `build_cache_from_table(const ObFTDictDesc &, ObFTCacheRangeContainer &)`；保留现有 `build_cache()` 作为内置表兼容入口或将其委托给该函数。 |
| `src/storage/fts/dict/ob_ft_dict_table_iter.h/.cpp` | 去掉硬编码 `oceanbase.`，以已规范化、可安全引用的库表名读取 `word`，并在查询前验证结果列。 | `init(const ObFTDictDesc &desc)` 替换 `init(const ObString &table_name)`；SQL 形如 `SELECT word FROM <escaped db>.<escaped table> ORDER BY word`。 |
| `src/storage/fts/ob_ik_ft_parser.h/.cpp` | 用 `ObFTParserProperty` 中三个真实词典引用构造 descriptor；默认属性仍落到内置词典。 | 新增 `build_dict_descs_(const ObFTParserProperty &, ObFTDictDesc &, ObFTDictDesc &, ObFTDictDesc &)`；`init_dict()` 改用该函数，并将 descriptor 传给 `init_single_dict()`。 |

### 5. `ALTER SYSTEM REFRESH FULLTEXT DICT` 执行与多机失效

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/sql/resolver/ddl/ob_ddl_resolver.h/.cpp`（或当前 ALTER SYSTEM 对应 resolver） | 将 `T_REFRESH_FULLTEXT_DICT` 转为专用 statement；解析当前库/显式库名、检查表存在且为词典表。 | 新增 `resolve_refresh_fulltext_dict_(const ParseNode &, ObRefreshFulltextDictStmt &)`。 |
| `src/sql/resolver/ddl/ob_refresh_fulltext_dict_stmt.h/.cpp`（新建） | 保存 tenant/database/table ID、全限定名与待刷新版本。 | 新增 `ObRefreshFulltextDictStmt`。 |
| `src/sql/engine/cmd/ob_refresh_fulltext_dict_executor.h/.cpp`（新建） | 调 root service 发起刷新 RPC，并把语法/权限/不存在表错误完整返回给 SQL 层。 | 新增 `ObRefreshFulltextDictExecutor::execute(ObExecContext &, ObRefreshFulltextDictStmt &)`。 |
| `src/share/ob_rpc_struct.h/.cpp`、`src/share/ob_rpc_proxy.h`、rootserver RPC 注册位置 | 定义并注册 tenant-scoped 刷新参数与 RPC。 | 新增 `ObRefreshFulltextDictArg`（tenant_id、table_id、schema/data version）和 `refresh_fulltext_dict` RPC。 |
| `src/rootserver/ob_root_service.h/.cpp`、`src/rootserver/ob_ddl_service.h/.cpp` | 校验词典表，生成新的 refresh generation 并广播 observer。 | 新增 `refresh_fulltext_dict(const ObRefreshFulltextDictArg &)`；新增/复用 server 广播 helper。 |
| `src/observer/...` 的 RPC processor 注册位置 | 接收广播，定位 tenant 的 `ObFTDictHub`，失效目标词典的元数据和 KV cache。 | 新增 `ObRefreshFulltextDictP::process()`，调用 `ObFTDictHub::invalidate_cache()`。 |

### 6. 被引用词典表的 DDL 保护

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/share/schema/ob_schema_service.h/.cpp` | 通过所有 FULLTEXT 索引的 parser JSON 查找指定词典表引用；返回引用索引列表。 | 新增 `get_fulltext_indexes_referencing_dict_(tenant_id, dict_table_id, ObIArray<uint64_t> &)`。 |
| `src/rootserver/ob_ddl_service.cpp` | 在 DROP TABLE、RENAME TABLE 和 ALTER TABLE 的 schema 变更提交前调用依赖查询。 | 新增 `check_fulltext_dict_ddl_allowed_(const ObTableSchema &, ObDictDdlOperation)`：有引用时，DROP 返回 `ERROR 4179`；其他受限操作返回 `ERROR 1235`；DML 不进入该检查。 |
| `src/sql/resolver/ddl/ob_alter_table_resolver.cpp` | 尽早识别字典表的 column-level/table rename 动作，传递 operation 类型，避免走到通用变更后才失败。 | 新增 `check_dict_table_alter_action_(...)` 或在现有 action switch 中调用共享校验。 |

### 7. TOKENIZE 和现有验收

| 文件 | 改动 | 新增逻辑 / 函数 |
| --- | --- | --- |
| `src/sql/engine/expr/ob_expr_tokenize.cpp` | `additional_args` 通过与 DDL 相同的属性规范化和字典描述构造逻辑，保证 `TOKENIZE()` 与建索引行为一致；不在表达式层复制 cache 实现。 | 在 `TokenizeParam::reform_parser_properties()` 调用共享 normalize/validate helper；由 parser helper 使用真实表名。 |
| `tools/deploy/mysql_test/test_suite/ai_funcs/t/ik_custom_dict.test`、`.result` | **不修改。** | 仅运行现有用例；其 CREATE、REFRESH、关联索引和动态更新查询必须通过。 |

## 错误处理和兼容性

- 只接受 `FULLTEXT_DICT='Y'`；缺失、`N` 或任意其他值不能把普通表当作词典表。
- `PARSER_PROPERTIES` 的未限定表名必须在当前 database 解析；无当前 database 时返回明确的 SQL 错误。带库名及双引号引用必须保持语义。
- 引用不存在表、普通表或结构不合法的表，在创建 FULLTEXT 索引 / `TOKENIZE()` 时失败，不能延迟到首次写入才报错。
- refresh 成功只影响后续分词；已有全文索引记录不回填，符合任务说明。
- 升级兼容：读 JSON 时同时接受历史错误键 `quanitfier_table`，写出时只使用正确键 `quantifier_table`。

## 验收命令

不变更测试文件，只执行：

```bash
cd tools/deploy/mysql_test
./mysql-test-run.pl --suite=ai_funcs ik_custom_dict
```

预期：现有结果文件中的三次命中结果保持不变；特别是第二次 refresh 后插入的“新词汇”能够命中，而 refresh 前已建立的索引数据不被重建。

## 自检结果

- 无 TBD/TODO 或待定接口；每一个 Task3 SQL 能力均映射到解析、校验、执行和运行时模块。
- 三类词典共享同一套 descriptor/cache/refresh 机制，仅以 `ObFTDictType` 区分行为。
- `TOKENIZE()` 与 FULLTEXT 索引共用属性和缓存入口，避免语义分叉。
- 测试范围明确为只运行当前 `ik_custom_dict`，不修改测试资产。
