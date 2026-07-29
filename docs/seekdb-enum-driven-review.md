# seekdb 枚举定义驱动的功能纵切审查记录

> 审查基线：`53d42a2c9621`
>
> 审查日期：2026-07-16
>
> 审查口径：以源码枚举值为抓手，沿“语法/协议入口 → resolver/RPC → schema/持久化 → 执行/存储 → 展示/测试”纵向追踪。只记录能够形成独立功能删除面的中等以上代码链；零散废枚举值不单独成项。

## 1. 结论摘要

本轮在 `src` 下命中 **1,453 处具名枚举声明、782 个文件**（正则口径，不含部分匿名枚举和生成器展开）。将枚举值按功能聚类，并与 `nijia.nj/public` 下现有 seekdb 审查记录逐项去重后，当前得到 **15 条新增评审项**；逐条对齐后的结论为 **删除 13 条、保留 2 条**：

| 编号 | 候选功能 | 核心枚举抓手 | 建议 | 置信度 | 直接覆盖规模 |
| --- | --- | --- | --- | --- | --- |
| ENUM-01 | 旧垂直分区辅助表体系 | `ObTableType::AUX_VERTIAL_PARTITION_TABLE` | 整体删除 | 高 | 21 个源码文件、79 处直接命中，另有专用 builder |
| ENUM-02 | Oracle DIRECTORY Schema Object | `ObObjectType::DIRECTORY`、`ObSchemaType::DIRECTORY_SCHEMA` | 整体删除 | 高 | 43 个源码文件、180 处直接命中；专用文件约 1,752 行 |
| ENUM-03 | Synonym 残留链 | `ObDependencyTableType::DEPENDENCY_SYNONYM`、`ObSchemaType::SYNONYM_SCHEMA`、`ObObjectType::SYNONYM` | 整体删除 | 高 | 24 个源码文件、49 处聚焦命中，横跨依赖、plan cache、权限和 resolver |
| ENUM-04 | 用户可选的 HASH Index 类型 | `ObIndexUsingType::USING_HASH` | **保留** | 已对齐 | 19 个源码文件、35 处核心命中，另有大量内表生成定义 |
| ENUM-05 | Oracle Schema 级用户定义类型与对象方法 | `ObSchemaType::UDT_SCHEMA`、`ObObjectType::{TYPE, TYPE_BODY}`、`ObRoutineType::ROUTINE_UDT_TYPE`、`ObPackageType::PL_UDT_OBJECT_SPEC/BODY` | 删除 Schema 级功能链 | 高 | 45 个源码文件、261 处聚焦命中；另有权限、错误码和虚表残留 |
| ENUM-06 | Oracle 高级 Trigger 子类型 | `ObTriggerInfo::TriggerType::{TT_COMPOUND_DML, TT_INSTEAD_DML, TT_SYSTEM}`、`SystemTriggerEvent` | 删除高级子类型，保留普通行级 Trigger | 高 | 25 个源码文件、108 处聚焦命中 |
| ENUM-07 | `foreign_key_checks=OFF` 下的 Mock FK Parent Table | `ObSchemaType::MOCK_FK_PARENT_TABLE_SCHEMA`、`ObMockFKParentTableOperationType`、`ObForeignKeyInfo::is_parent_table_mock_` | **保留** | 已对齐 | 58 个源码文件、1,701 处直接命中；专用 manager 666 行、4 张专用内表 |
| ENUM-08 | Oracle DML Error Logging | `ObErrLogType::{OB_ERR_LOG_INSERT, OB_ERR_LOG_UPDATE, OB_ERR_LOG_DELETE}`、`LOG_ERR_LOG`、`PHY_ERR_LOG` | 整体删除 | 高 | 34 个源码文件、168 处聚焦命中；专用实现文件约 682 行 |
| ENUM-09 | Oracle PL 自治事务 | `ObPLFunctionBase::IS_AUTONOMOUS_TRANSACTION`、`T_SP_PRAGMA_AUTONOMOUS_TRANSACTION`、`UnregisterPath::AUTONOMOUS_TRANS` | 删除 PL 自治事务链，保留并泛化内部事务切换机制 | 高 | 22 个源码文件、97 处聚焦命中；另有 13 个测试文件、58 处 pragma |
| ENUM-10 | Oracle Interval Partition | `ObPartitionFuncType::PARTITION_FUNC_TYPE_INTERVAL`、`MayAddIntervalPart`、`ObAlterTableArg::{SET_INTERVAL, INTERVAL_TO_RANGE}` | 删除 Interval 专属链，保留 RANGE、Auto Split 和 Dynamic Partition | 高 | 23 个源码文件、154 处聚焦命中，横跨 schema、DDL、优化器和 DML 自动补分区 |
| ENUM-11 | Oracle PL `NOCOPY` 参数语义 | `SP_PARAM_NOCOPY_MASK`、`ObPLRoutineParam::is_nocopy_`、`nocopy_params_` | 删除按引用别名和提前回写链，保留普通 `IN/OUT/INOUT` | 高 | 21 个源码文件、116 处聚焦命中，横跨 schema、resolver、codegen、interpreter、UDF 和 SPI |
| ENUM-12 | 失活的 PL 逐行 Profiler / `DBMS_PROFILER` | `ObPLObjectKey::ObjectMode::PROFILE` | 整体删除旧 Code Generator 插桩残链，保留普通 PL Cache 与耗时统计 | 高 | 11 个直接命名文件、42 处聚焦命中，另有 2 个 Cache Key 实现文件；未装载系统包 406 行 |
| ENUM-13 | Oracle PL `FORALL` 批量 DML 残链 | `ObPLStmtType::PL_FORALL`、`ObPLGetCursorAttrInfo::PL_CURSOR_BULK_*`、`T_SP_FORALL` | 整体删除不可达的 FORALL、`SAVE EXCEPTIONS` 与隐式游标批量状态链 | 高 | 19 个生产文件、170 处聚焦命中，横跨 PL AST、Cursor、SPI、表达式与 array binding 优化 |
| ENUM-14 | Oracle PL `BULK COLLECT` 批量取数残链 | `PL_MOD_IDX::OB_PL_BULK_INTO`、`ObPLInto::bulk_`、SPI `is_bulk` | 整体删除当前 Grammar 无生产者的批量 Collection 取数链 | 高 | 14 个生产文件、76 处聚焦命中；SPI 约 500 行主体，另有 18 个测试/样例文件、117 处旧语法 |
| ENUM-15 | Oracle Collection `MULTISET` 运算与条件残链 | `T_OP_MULTISET`、`T_OP_COLL_PRED`、`ObMultiSetType`、`ObMultiSetModifier` | 整体删除无 Grammar 生产者且 Runtime 明确拒绝的 nested-table 集合表达式链 | 高 | 29 个生产文件、218 处聚焦命中；四个专用表达式文件共 1,036 行 |

这 15 条均未在已有“seekdb + 审查记录”文档中被标记为在途删除。`ENUM-01/02/03/05/06/08/09/10/11/12/13/14/15` 进入删除清单；`ENUM-04/07` 按产品决策保留，不进入裁剪任务。

此外，PL 已完全切换到解释执行。自 2026-07-18 起采用一条独立于产品功能逐条对齐的硬边界：**仅服务旧 PL native/JIT/编译执行、且不被解释器、PL AST 构建或 SQL 表达式运行时依赖的代码，自动进入删除清单，不再逐条讨论。** 这批实现残留不增加上表的产品候选数量，详见 2.1。

## 2. 去重基线

本轮已对照以下现有记录，不重复提交其中已明确在途删除的功能：

1. seekdb 配置驱动的功能纵切审查记录
2. seekdb Session 变量驱动的功能纵切审查记录
3. seekdb 三方扩展库驱动的功能纵切审查记录
4. seekdb 语法文件驱动的功能纵切审查记录
5. seekdb 编译宏驱动的功能纵切审查记录

因此，枚举扫描命中的下列大类只计入覆盖面、不再列为新增候选：多 Observer/多副本/迁移/均衡、Tenant/MTL、External Catalog/Location/外表、对象存储归档备份、Tablegroup、OBKV/Table API、Oracle compatibility mode、OLS/XMLTYPE/ROWID、Sequence、Context、CCL、TTL、XA、SQL Event、远程 DAS/DTL/PX、PL Native/Debugger/Result Cache、TDE/KMS、Storage Cache Policy、Merge Engine 旧模式、压缩器兼容别名等。

### 2.1 PL 解释执行后的编译执行残留：授权自动删除

#### 判定规则

满足以下三点即直接列入删除，不再作为产品功能候选逐条询问：

1. 代码的唯一目的，是生成、装载、调用或优化旧 PL native/JIT 可执行体；
2. 当前所有可达 PL 调用都经 `ObPLExecState::execute()` 进入 `ObPLInterpreter`，不存在 native/JIT fallback；
3. 删除面不被解释器、PL parser/resolver/AST build、`ObStaticEngineExprCG`/`ObExprGeneratorImpl`、SPI 或普通 PL Cache 依赖。

“名字中含 compile/codegen/JIT”本身不是证据。SQL 表达式构建、PL AST/package cache build、依赖失效与 build lock 仍服务解释执行，必须保留。

#### 自动删除清单

| 编号 | 自动删除面 | 原来服务什么 | 当前为何可删 | 关键边界 |
| --- | --- | --- | --- | --- |
| PL-AUTO-01 | native dispatch 与 shortcut 骨架：`ObFuncPtr`、`action_`、`ObPLSqlInfo/sql_infos_`、AST `sql_stmts_`、`simple_execute()`、`interface_execute()`、`interface_name_` | 保存 JIT 生成的过程入口，或绕过旧 native body 直接执行纯 SQL/interface procedure | builder 已明确把 `action_` 留为 0；解释器直接遍历 AST，interface statement 直接经 SPI 调用，现有字段和 shortcut 均无执行消费者 | 暂保留仍影响匿名块 NULL/cast 语义的 `is_all_sql_stmt_`；保留 `PRAGMA INTERFACE` 与 MySQL system package interface 机制 |
| PL-AUTO-02 | visitor/JIT callable 辅助层：`ObPLStmtVisitor` 及全部 `accept()`、`ObPLSPIWrapper`、`ObPL::set_user_type_var()`、FORALL implicit-cursor wrapper | 供旧 code generator 遍历 statement 树，并向 LLVM/JIT 注册可调用 C++ 符号 | 当前 interpreter 以 statement type switch 执行；visitor 没有子类和调用者；symbol-registration 生产者已经删除 | 保留 router 使用的 `get_child_size()/get_child_stmt()`、SPI 本体及普通用户类型赋值逻辑；FORALL 产品残链按 ENUM-13 删除 |
| PL-AUTO-03 | native metadata：`stack_size_`、`di_buf_/di_len_`、`simple_calc_bitset_`、`_ob_enable_pl_dynamic_stack_check` 及 bootstrap 强制赋值 | 记录 native frame 栈尺寸、JIT debug image 和 LLVM simple-calc 优化位，并控制动态 native stack check | 当前无生产者或消费者；debug image 只剩析构释放；stack 配置不再被 PL 执行读取 | JIT symbol debug metadata 并入既有“PL Debugger 在途删除”任务，不重复成项 |
| PL-AUTO-04 | object-access getter 函数指针：raw/runtime expr 的 `get_attr_func_` 及其 serialize/copy/hash/branch、builder 清零循环 | 让 JIT 为 record/collection/package attribute access 注入专用 getter 地址 | 当前唯一 writer 只写 0，运行期恒走通用 `get_attr_func` | 两阶段删除：先把 `obj_access_exprs_` 承担的 external-record default-expression 提取副作用迁到显式逻辑，再删集合；回归 Trigger `OLD/NEW`、record/collection、package var、cursor param、`OUT/INOUT` |
| PL-AUTO-05 | Windows JIT unwind/SEH 桥与空 native unwind wrapper：`win32_pl_seh.h`、`win32_unwind_stubs.c`、`pl_execute_callee_seh`、`force_restore_pl_stack_ctx`、纯 `catch (...) { throw; }` | 让异常穿越 LLVM/JIT frame，并在 Windows 恢复 JIT PL 栈 | JIT frame 与 `ObJitMemoryManager` 生产链已不存在；现有解释器按普通 C++/错误码路径执行 | 保留 Windows 通用 crash tracing 和仍服务 `__udivti3` 的 compiler-rt builtins；有 schema guard/package state/final cleanup 的 catch 先改成 scope guard 再删除 |
| PL-AUTO-06 | codegen 监控、测试与术语幽灵：`pl_cg_mem_hold_` 列、永远写 0 的 builder 赋值、`test_compile.result` LLVM IR golden、`PlJit/PlCodeGen` 测试与 benchmark 标签 | 观察和验证旧 PL code generator/JIT 的内存、IR 与执行阶段 | 指标已恒为 0，golden 没有当前生成者，标签不再对应真实执行阶段 | 保留仍有意义的 parse/resolve/expression build 耗时，可将 compile/codegen 文案改为 build/finalize；`_ob_pl_compile_max_concurrency` 仍限制 AST build，保留 |
| PL-AUTO-07 | `PRAGMA INLINE`、`PRAGMA UDF` 及其 statement item、resolver no-op、`ObPLCompileFlag::UDF` | 向旧编译器提供 inline/UDF 优化提示与编译标记 | MySQL-mode grammar 只产生 `PRAGMA INTERFACE`；`INLINE` resolver 已是 no-op，`UDF` 标记没有当前 optimizer/interpreter 消费者 | 数值 ID 留空洞，不复用；不删除 `PRAGMA INTERFACE`。`RESTRICT_REFERENCES` 是前端语义约束，`SERIALLY_REUSABLE` 是 package-state 生命周期，均不冒充编译残留 |

这些项不是用户可见产品能力，MySQL 也没有需要兼容的对应语法或行为；删除目标是收回已失去执行主体的内部实现。Persistent native DLL、PL recompile job、PL Debugger、显式 `ALTER COMPILE`、PL Result Cache、远程 Package State 和 `PLSQL_OPTIMIZE_LEVEL` 已在既有审查记录中标记在途删除，只把上表发现的补充文件并入原任务，不重复编号。

更完整的调用链、历史提交和文件级删除顺序见仓库内 `docs/seekdb-pl-interpreter-legacy-research.md`。

## 3. 新增候选逐条分析

### ENUM-01：删除旧垂直分区辅助表体系

> 对齐结论（2026-07-16）：**按建议删除**。实施前先检查存量 schema 中是否存在 `table_type = 11`；普通分区、LOB 及 FTS/GIS/Vector 辅助表不在删除范围。

#### 枚举抓手

- `src/share/schema/ob_schema_struct.h` 中 `ObTableType::AUX_VERTIAL_PARTITION_TABLE = 11`。
- 配套的 `T_VERTICAL_COLUMNS_PARTITION`、`ObCreateVertialPartitionArg`、`vertical_partition_arg_list_`、列级 primary/aux vertical partition 标记。

#### 可达性与功能现状

- `ob_create_table_resolver_base.cpp` 已直接写明：`vertical partition is not support in 4.x, remove its code here`。
- parser 侧只剩生成的 `T_VERTICAL_COLUMNS_PARTITION` item type，未找到对应语法产生式。
- create-table resolver 遇到该节点时把它当作无分区节点处理，并不会构造可工作的垂直分区建表路径。
- 与入口失联的同时，后端仍保留 `ObVerticalPartitionBuilder`、RPC 参数序列化、schema 映射、辅助表生命周期和 DDL 分支。

这说明它不是“暂时关掉的完整能力”，而是**入口已经删除、后半条功能链仍长期存活**的典型残留。

#### 建议删除面

1. 删除 `AUX_VERTIAL_PARTITION_TABLE` 及 `is_aux_vp`/垂直分区类型判断。
2. 删除 `ObCreateVertialPartitionArg`、建表参数数组及其序列化/打印。
3. 删除 `ob_vertical_partition_builder.{h,cpp}` 和构建入口。
4. 删除列 schema 中 primary/aux vertical partition 标记及复制、持久化、展示逻辑。
5. 删除 schema mgr/service、DDL operator/service、drop/recycle、partition exchange、数据字典中的专用分支。
6. 删除仅服务于该功能的升级与 obtest 用例。

#### 保留边界

- 不删除普通 range/list/hash partition。
- 不删除 LOB aux table。
- 不删除 FTS、GIS、Vector 等仍在使用的辅助索引表。
- 不因名称相近而触碰当前 partition split/merge 实现。

#### 验收建议

- 全库不再出现 `AUX_VERTIAL_PARTITION_TABLE`、`ObCreateVertialPartitionArg`、`ObVerticalPartitionBuilder`。
- 普通分区、LOB、FTS、Vector 建表及 drop/recycle 回归通过。
- 旧 schema 数据若理论上可能包含 table type 11，应在删除前增加一次离线元数据检查；seekdb 新建链路已经无法产生该对象。

### ENUM-02：删除 Oracle DIRECTORY Schema Object

> 对齐结论（2026-07-16）：**按建议删除**。只删除 Oracle DIRECTORY Schema Object 及专用权限/管理链；seekdb 数据、日志、备份目录和保留功能所需的普通文件 I/O 不在删除范围。

#### 枚举抓手

- `ObObjectType::DIRECTORY = 10`。
- `ObSchemaType::DIRECTORY_SCHEMA = 32`。
- 配套 DDL operation、object privilege、RPC arg 和 parser item type。

#### 可达性与功能现状

- 源码仍有完整的 `ObDirectorySchema`、`ObDirectoryMgr`、`ObDirectorySQLService`、create/drop resolver/stmt/executor、RootService/DDL operator/RPC 链。
- 仅专用文件已约 **1,752 行**，还不含 schema cache、权限、inner table、RPC 和 DDL service 中的分支。
- 但 parser `.y` 文件中未找到 create/drop directory 的语法产生式，只剩生成 item type。
- 即使绕过前端进入 DDL service，`ObDDLService::create_directory()` 和 `drop_directory()` 也固定返回 `OB_NOT_SUPPORTED`。
- 既有审查已决定删除 UTL_FILE；DIRECTORY 作为 Oracle 文件对象的主要消费场景随之消失。

因此该功能同时满足：**外部入口不可达、服务端明确不支持、主要消费者已在途删除、保留成本仍很高**。

#### 建议删除面

1. 删除 `DIRECTORY`/`DIRECTORY_SCHEMA` 及 schema type/object type 映射。
2. 删除 `ObDirectorySchema`、tenant-directory key、schema cache 与统计。
3. 删除 `ob_directory_mgr`、`ob_directory_sql_service`。
4. 删除 create/drop directory resolver、stmt、executor 及构建文件登记。
5. 删除 RootService、DDL service/operator、schema replay、RPC arg/processor 中的 directory 分支。
6. 删除 DIRECTORY object privilege、grant/revoke、系统视图和 inner-table 字段。

#### 保留边界

- 只删除 SQL/PL 层的 Oracle DIRECTORY Schema Object。
- 不删除 seekdb 自身的数据目录、日志目录、临时目录。
- 不删除本地导入导出、备份恢复或普通文件 I/O 所需的路径处理能力。
- `secure_file_priv` 若仍被 LOAD DATA/OUTFILE 等能力消费，应独立保留。

#### 验收建议

- 全库不再有 `ObDirectorySchema`、`ObDirectoryMgr`、`T_CREATE_DIRECTORY`、`T_DROP_DIRECTORY`。
- 普通本地文件能力与本地物理备份恢复回归通过。
- 系统视图和权限位中不再展示永远无法使用的 DIRECTORY 能力。

### ENUM-03：删除 Synonym 残留链

> 对齐结论（2026-07-16）：**按建议删除**。删除 Oracle Schema Synonym 的枚举、权限、依赖及无效解析状态；SQL 表/列别名、View 和真实对象依赖跟踪不在删除范围。

#### 枚举抓手

- `ObDependencyTableType::DEPENDENCY_SYNONYM = 3`。
- `ObSchemaType::SYNONYM_SCHEMA = 9`。
- `ObObjectType::SYNONYM = 13`。

#### 可达性与功能现状

- parser 仅剩 `T_CREATE_SYNONYM`/`T_DROP_SYNONYM` 生成 item type，未找到对应语法产生式，也没有 synonym schema/mgr/sql-service 的主体实现文件。
- `ObSchemaChecker::get_table_schema_with_synonym()` 明确注释 `synonym has been drop in lite version`；实现只调用普通 `get_table_schema()`，并始终令 `has_synonym = false`。
- `ObServerSchemaService::get_increment_synonym_keys()` 及 reverse 版本是直接返回成功的空实现。
- 但依赖模型、plan cache、PL persistent/recompile、view/parallel DDL、raw expr/TableItem、打印、权限与系统视图仍携带 synonym 分支或字段。

这是一条**主体早已删除、调用方为了兼容旧接口继续传播“可能存在 synonym”状态**的残留链。继续保留会让对象依赖和 plan-cache invalidation 的真实状态空间比产品能力更复杂。

#### 建议删除面

1. 删除三个 synonym 枚举值及其 schema/object type 映射。
2. 将 `get_table_schema_with_synonym()` 调用方迁移到普通表解析接口，并删除 `has_synonym`、`synonym_name`、`synonym_db_name` 等状态传播。
3. 删除 plan cache 中 synonym dependency/invalidation 分支。
4. 删除 PL persistent/recompile、view helper、parallel DDL 中的 synonym object 分支。
5. 删除 create/drop synonym stmt type、DDL operation、权限位、grant 展示和 inner-table 列。
6. 删除 synonym 专用 errno、max-id/operation-id 等仅剩兼容常量。

#### 保留边界

- 不删除 SQL 表别名、列别名；它们与 schema synonym 无关。
- 不删除真实存在的 table/view/routine/package 依赖跟踪。
- 不删除 MySQL role/RBAC 或普通 object privilege 框架，只移除 synonym 对象种类和专用权限。

#### 验收建议

- 全库不再出现 `SYNONYM_SCHEMA`、`DEPENDENCY_SYNONYM`、`get_table_schema_with_synonym`。
- plan cache 的表/view 依赖失效测试通过。
- 表别名、view 展开、PL 对真实对象的依赖回归通过。

### ENUM-04：保留用户可选的 HASH Index 类型

> 对齐结论（2026-07-16）：**保留**。`USING HASH` 语法、schema 枚举、持久化和现有优化器语义均不纳入本轮删除任务。下述分析仅作为现状与风险记录，不形成裁剪要求。

#### 枚举抓手

- `ObIndexUsingType::{USING_BTREE, USING_HASH}`。
- `T_USING_HASH` 语法 item、`index_using_type_` schema/RPC/inner-table 持久字段。

#### 可达性与功能现状

- MySQL parser 和 create table/create index/alter table resolver 接受 `USING HASH`，并把 `USING_HASH` 持久化进 table schema。
- schema 复制、序列化、inner table 和 information schema 都会保存/展示 HASH。
- 但未找到独立的 hash-index 建造、存储、查找或执行算子；index builder 与 B-tree 走同一套实际索引构建链。
- 运行期最实质的区别是 `ObTableSchema::is_ordered()` 仅在 `USING_BTREE` 时返回 true。也就是说，用户选择 HASH 并没有获得 hash storage/index，只是让优化器不再把同一个索引视为有序。
- inner/virtual table 生成器又把 `USING_HASH` 当作“虚表索引无序”的内部标记。这是内部能力描述与用户可见物理索引类型共用一个枚举造成的语义混叠。

从当前代码观察，DDL/元数据中的 HASH 与独立物理 HASH 索引实现之间存在语义差异；经逐条对齐，本轮选择保留现状，不据此发起删除。

#### 曾评估的调整方向（本轮不执行）

如果未来重新评估，可考虑分两步处理，而不是直接把所有 `USING_HASH` 替换成 BTREE：

1. 用户侧删除 `USING HASH` 能力：parser 可以保留 token，但 resolver 应明确报 `NOT_SUPPORTED`；不再把它写入用户表 schema。
2. 将虚表“是否有序/是否支持 range scan”的内部语义迁移到明确的 capability flag 或独立 internal enum，避免虚表被误标成一种并不存在的物理 HASH Index。
3. 完成历史元数据检查后，删除用户 schema/RPC 中的 `ObIndexUsingType` 维度；若内部虚表暂时无法同步迁移，则至少先把 internal 与 user-facing 枚举拆开。
4. 更新 information schema：用户索引固定展示真实的 BTREE/实际实现，不再展示虚假的 HASH。
5. 删除 partition exchange、DDL arg、schema serialization 中只为比较/传播该伪类型存在的分支。

#### 保留边界

- 不删除普通 B-tree 索引、unique index、primary key。
- 不删除 hash join、hash aggregate、hash partition、hash map；它们与 HASH Index 无关。
- 不把虚表强行标成“有序 B-tree”。虚表访问能力需要显式建模，否则可能引入错误的 range/order 推断。
- FTS、GIS、Vector 的 domain/special index type 使用的是 `ObIndexType`，不在本项删除范围。

#### 若未来重启评估的验收建议

- 用户 DDL `CREATE INDEX ... USING HASH` 得到稳定、明确的不支持错误，而不是成功创建伪 HASH Index。
- 新建用户索引不再产生 `index_using_type = USING_HASH`。
- optimizer 对普通 B-tree 的 ordered/range 能力不回退；虚表查询计划与原行为一致。
- 历史 schema 中的 HASH 值完成升级归一化或拒绝策略后，再收缩持久化枚举。

### ENUM-05：删除 Oracle Schema 级用户定义类型与对象方法

> 对齐结论（2026-07-16）：**按建议删除**。只删除持久化的 Schema 级 `CREATE TYPE/TYPE BODY`、独立 Object Type/Collection Type 及其对象方法链；Package/过程内部类型和 ARRAY/MAP/VECTOR 等通用 UDT 运行时必须保留。

#### 枚举抓手

- `ObSchemaType::UDT_SCHEMA`。
- `ObObjectType::{TYPE, TYPE_BODY}` 与 `ObDependencyTableType::{DEPENDENCY_TYPE, DEPENDENCY_TYPE_BODY}`。
- `ObRoutineType::ROUTINE_UDT_TYPE`。
- `ObPackageType::{PL_UDT_OBJECT_SPEC, PL_UDT_OBJECT_BODY}`。
- `UdtUdfType::{UDT_UDF_CONS, UDT_UDF_MEMBER, UDT_UDF_STATIC, UDT_UDF_MAP, UDT_UDF_ORDER}`。
- `ObRoutineFlag` 中 `SP_FLAG_STATIC`、`SP_FLAG_UDT_MAP/UDF/FUNC/CONS/ORDER` 及隐藏 `SELF` 参数属性。

#### 服务的产品功能

该枚举族服务于 Oracle 的持久化 Schema Object Type，例如：

```sql
CREATE TYPE address_t AS OBJECT (
  city VARCHAR2(64),
  MEMBER FUNCTION format_addr RETURN VARCHAR2
);

CREATE TYPE BODY address_t AS
  MEMBER FUNCTION format_addr RETURN VARCHAR2 IS ...
END;
```

功能面还包括：

1. Schema 级 Object Type、VARRAY 和 Nested Table 的类型定义及版本管理；
2. 默认/自定义构造器和隐藏 `SELF` 参数；
3. Member/Static Procedure/Function；
4. MAP/ORDER 方法参与对象比较时的表达式改写；
5. Type Spec/Body 与 routine 的 schema、依赖、重编译和 plan-cache invalidation；
6. 对象定义虚表、权限、错误码及并行 DDL 中的 TYPE/TYPE BODY 分支。

#### 可达性与功能现状

- 当前 `sql_parser_mysql_mode.y` 和 `pl_parser_mysql_mode.y` 均没有 `CREATE TYPE`、`CREATE TYPE BODY`、`ALTER TYPE` 或 `DROP TYPE` 的语法产生式。
- parser 侧只剩生成的 `T_SP_CREATE_TYPE*`、`T_CREATE_WRAPPED_TYPE*` item type，无法由当前用户 SQL 入口产生。
- 后端仍完整保留对象方法编译与执行状态：构造器名称校验、Member/Static/MAP/ORDER 标记、隐藏 `SELF` 参数、UDT routine 查找和对象比较改写。
- schema/缓存侧仍传播 `UDT_SCHEMA`、`TYPE_BODY` dependency，参与 PL persistent、PL/SQL plan cache、recompile 和 parallel DDL。
- 聚焦枚举与对象方法状态统计已命中 **45 个源码文件、261 处引用**，还未计入大量通用 UDT/Collection 代码；删除面足以独立立项。
- 对照现有 5 份前置审查记录和本枚举审查记录，未发现该 Schema 级功能已被标记为在途删除。

这是一条**用户创建入口已不存在，但完整的 Oracle Schema Object Type 编译、调用、依赖和缓存状态机仍驻留**的功能链。

#### 为什么建议删除

1. seekdb 当前无法通过 SQL 创建该对象，继续保留对象方法和 Type Body 生命周期没有产品闭环。
2. 它引入独立的 schema/object/dependency/routine 种类，并侵入 PL 编译、表达式解析、Plan Cache、并行 DDL和系统视图。
3. MAP/ORDER、构造器和隐藏 `SELF` 等语义是 Oracle Object Type 专属复杂度，不是保留存储过程或普通集合所必需。
4. 删除后仍可保留 Package 内部 record/collection，因而不会阻断 DBMS_STATS 等当前系统包。

#### MySQL 对应能力

MySQL 8.4 没有 Oracle 式 `CREATE TYPE`/`CREATE TYPE BODY` 或带对象方法的 Schema Object Type。MySQL 的 `CREATE FUNCTION` 创建存储函数或可加载函数，不是持久化用户定义数据类型。

- [MySQL 8.4 Data Definition Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-data-definition-statements.html)
- [MySQL 8.4 Data Types](https://dev.mysql.com/doc/refman/8.4/en/data-types.html)
- [MySQL 8.4 CREATE FUNCTION](https://dev.mysql.com/doc/refman/8.4/en/create-function.html)

#### 建议删除面

1. 删除 `UDT_SCHEMA`、`TYPE/TYPE_BODY`、`DEPENDENCY_TYPE/TYPE_BODY` 等只服务于 Schema Type Object 的种类和映射。
2. 删除 `ROUTINE_UDT_TYPE`、`PL_UDT_OBJECT_SPEC/BODY`、`UdtUdfType` 对象方法修饰状态。
3. 删除对象构造器、Member/Static/MAP/ORDER 方法的解析、隐藏 `SELF` 参数和比较表达式改写。
4. 删除 schema getter/routine mgr 中按 UDT id 查找对象方法的接口。
5. 删除 Type Spec/Body 在 PL persistent/recompile、Plan Cache、parallel DDL、object-definition virtual table 中的分支。
6. 删除 `CREATE TYPE` 专用系统权限、TYPE/TYPE BODY 专用错误码和只服务于该对象的展示字段。
7. 删除已经无法由 grammar 产生的 `T_SP_CREATE_TYPE*`、`T_CREATE_WRAPPED_TYPE*` item type。

#### 必须保留的边界

- 保留存储过程、函数、Package 和 Trigger 本身。
- 保留 Package、过程或函数内部的 `TYPE ... IS RECORD`、Associative Array、Nested Table、VARRAY 声明；多个保留系统包仍依赖这些类型。
- 保留 ARRAY、MAP、VECTOR、GIS、RoaringBitmap 等 SQL 类型所共用的 UDT/subschema/序列化运行时。
- 保留 MySQL `ENUM/SET`；它们不是这里的 Oracle UDT。
- 保留 PL 复合值、Collection 参数传递和通用 `ObUserDefinedType` 基础设施中仍被上述能力消费的部分，不能按名称整类删除。

#### 验收建议

- 全库不再出现 `PL_UDT_OBJECT_SPEC/BODY`、`ROUTINE_UDT_TYPE`、`UDT_UDF_MAP/ORDER/CONS` 和按 UDT id 查找 routine 的接口。
- `UDT_SCHEMA` 若仍被通用 SQL ARRAY/MAP/VECTOR 路径复用，应先拆出明确的新 schema/subschema 类型，再删除其 Oracle Schema Object 语义，不能直接改成无效值。
- 存储过程、函数、Package、内部 RECORD/Collection，以及 DBMS_STATS 等保留系统包回归通过。
- ARRAY/MAP/VECTOR 列、参数绑定、序列化、表达式执行和客户端输出回归通过。

### ENUM-06：删除 Oracle 高级 Trigger 子类型

> 对齐结论（2026-07-16）：**按建议删除**。删除 Compound Trigger、View 上的 INSTEAD OF Trigger、LOGON/LOGOFF System Trigger 及其专用状态；保留 MySQL 表级 `BEFORE/AFTER ... FOR EACH ROW` Trigger。

#### 枚举抓手

- `ObTriggerInfo::TriggerType::{TT_COMPOUND_DML, TT_INSTEAD_DML, TT_SYSTEM}`。
- `SystemTriggerEvent::{SYS_TRIGGER_LOGON, SYS_TRIGGER_LOGOFF}`。
- `ObTriggerEvents` 中 `logon_`、`logoff_`。
- `ObTimingPoints` 中 statement-level、instead-row 等高级时点状态。
- parser item `T_TG_COMPOUND_DML`、`T_TG_INSTEAD_DML`、`T_TG_SYSTEM`。

#### 服务的产品功能

1. **Compound Trigger**：在同一个 Trigger 中组合 `BEFORE STATEMENT`、`BEFORE EACH ROW`、`AFTER EACH ROW`、`AFTER STATEMENT`，并共享声明区状态。
2. **INSTEAD OF Trigger**：挂在 View 上，替代原本的 INSERT/UPDATE/DELETE，实现可更新复杂 View。
3. **System Trigger**：响应用户 LOGON/LOGOFF 等数据库级事件，而不是表行变更。

这些是 Oracle 高级 Trigger 能力，不是 MySQL 普通行级 Trigger 的必要组成。

#### 可达性与功能现状

- 当前 `pl_parser_mysql_mode.y` 的 `trigger_definition` 只接受：

  ```sql
  BEFORE|AFTER INSERT|UPDATE|DELETE
  ON table FOR EACH ROW
  ```

  并且只生成 `T_TG_SIMPLE_DML`。
- 没有 grammar 产生 `T_TG_COMPOUND_DML`、`T_TG_INSTEAD_DML` 或 `T_TG_SYSTEM`。
- `set_system_type()` 全仓没有调用；Compound/Instead setter 只能从上述不可达 parser node 分支进入。
- 后端仍保留 Compound 的多时点解析、共享声明区和 Package 源码生成；INSTEAD OF 的 View DML 改写、RETURNING 限制和专用错误；System Trigger 的 LOGON/LOGOFF 位、RootService DDL 分支、系统触发器执行上下文及自动事务处理。
- 聚焦统计命中 **25 个源码文件、108 处引用**，且未找到这些高级子类型的有效 mysqltest 回归。
- 既有文档虽确认整体保留用户 Trigger，但没有要求保留这三种当前不可创建的 Oracle 高级子类型；普通 Trigger 保留边界不受影响。

#### 为什么建议删除

1. 三类对象均无当前 SQL 创建入口，只有后端状态机和兼容分支。
2. 它们扩大 Trigger Schema 的事件、时点、基对象和执行模型，侵入 DML Resolver、Codegen、View 更新、PL Package 生成及 RootService DDL。
3. seekdb 面向 MySQL Trigger 的产品边界只需要表上的行级 BEFORE/AFTER Trigger。
4. 与其保留无法测试、无法创建的半套 Oracle 行为，不如收缩 Trigger 状态空间，使所有可持久化类型都能由当前 grammar 产生并回归。

#### MySQL 对应能力

MySQL 8.4 支持表上的 `BEFORE`/`AFTER`、`INSERT`/`UPDATE`/`DELETE`、`FOR EACH ROW` Trigger；不提供 Oracle Compound Trigger、View `INSTEAD OF Trigger` 或 LOGON/LOGOFF System Trigger。

- [MySQL 8.4 CREATE TRIGGER](https://dev.mysql.com/doc/refman/8.4/en/create-trigger.html)

#### 建议删除面

1. 删除 `TT_COMPOUND_DML`、`TT_INSTEAD_DML`、`TT_SYSTEM` 和对应 parser item。
2. 删除 `SYS_TRIGGER_LOGON/LOGOFF`、event bit、System Trigger 自动事务和执行上下文。
3. 删除 Compound Trigger 声明区、statement-level timing section、Package source generator 和执行阶段聚合逻辑。
4. 删除 INSTEAD OF Trigger 的 View 基对象、DML Resolver/Codegen/RETURNING 专用分支及错误码。
5. 删除 RootService/PL DDL Operator、Schema Printer、SHOW 和持久化中三种高级类型的分支。
6. 收缩 `is_dml_type()` 和 Trigger Schema 校验，使新 schema 只接受普通行级 `TT_SIMPLE_DML`。

#### 保留边界

- 保留 `TT_SIMPLE_DML`。
- 保留表上的 `BEFORE/AFTER INSERT/UPDATE/DELETE FOR EACH ROW`。
- 保留 `OLD`/`NEW` 变量、只读校验、WHEN（若当前 MySQL Trigger 语法实际支持）及正常 Trigger Body 执行。
- 保留同一表多个 Trigger，以及当前已支持的 `FOLLOWS/PRECEDES` 排序。
- 保留 Trigger 的 Schema、依赖、Definer/Security、DDL、Plan/PL 执行和回收站适配等普通功能链。

#### 验收建议

- grammar、schema 和执行层不再出现 `T_TG_COMPOUND_DML`、`T_TG_INSTEAD_DML`、`T_TG_SYSTEM`、`SYS_TRIGGER_LOGON/LOGOFF`。
- 普通 BEFORE/AFTER 行级 Trigger 的 create/drop/alter、OLD/NEW、多个 Trigger 顺序和 DML 执行回归通过。
- View DML 不再探测不存在的 INSTEAD OF Trigger；继续按 seekdb 当前 View 可更新规则处理。
- 删除前检查存量 Trigger Schema 中是否存在高级 `trigger_type`；若 seekdb 当前版本从未提供创建入口，理论上不应产生。

### ENUM-07：保留 `foreign_key_checks=OFF` 下的 Mock FK Parent Table

> 对齐结论（2026-07-16）：**保留**。继续兼容 MySQL 在关闭 `foreign_key_checks` 后创建悬空外键、删除仍被引用的父表，以及后续由真实父表接管悬空外键的行为；整套 Mock FK Parent Schema、持久化和生命周期不纳入本轮删除任务。

#### 枚举抓手

- `ObSchemaType::MOCK_FK_PARENT_TABLE_SCHEMA`。
- `ObMockFKParentTableOperationType`：创建、加列、删列、删除、更新版本以及被真实父表替换等 8 类状态。
- DDL operation 中 `OB_DDL_CREATE/ALTER/DROP_MOCK_FK_PARENT_TABLE`。
- `ObForeignKeyInfo::is_parent_table_mock_`。

#### 服务的产品功能

当 MySQL Session 设置 `foreign_key_checks=OFF` 时，这套机制允许：

1. 创建引用尚不存在父表的外键，并用 Mock Parent Schema 保存父表名、引用列和外键关系。
2. 删除仍被子表外键引用的真实父表，并把引用关系迁移到 Mock Parent。
3. 在关闭检查时继续对子表执行 DML；重新打开检查后，若父表仍不存在，则阻止需要外键校验的 DML。
4. 后续通过 CREATE TABLE、RENAME、FLASHBACK 等路径出现兼容的真实父表时，校验列、类型和唯一键，并由真实父表替换 Mock Parent。
5. 外键逐步删除后，删除不再被引用的 Mock Parent 及其列元数据。

#### 可达性与实现规模

- 聚焦搜索命中 **58 个源码文件、1,701 处引用**，并非不可达残留。
- 专用 `ob_mock_fk_parent_table_mgr.{h,cpp}` 共 **666 行**。
- 有 `__all_mock_fk_parent_table`、其 history 表、column 表及 column history 表 4 张专用内部表。
- 功能横跨 Schema Cache/Refresh/Service、建表、删表、改名、CTAS、CREATE LIKE、Flashback、Information Schema 和 DML Codegen。
- `foreign_key_checks_ddl_mysql.test` 对 Mock Parent 的创建、扩列、清理、真实表替换、改名、闪回及异常路径有完整回归。

#### 曾评估的删除理由（本轮不执行）

1. 它为临时不完整的外键图引入第二套“虚拟表 Schema”和专用生命周期，维护成本很高。
2. 四张内表和大量 DDL 分支只服务于关闭外键检查时的兼容行为。
3. 若产品允许收缩兼容性，可以要求导入脚本先建父表，或最后统一执行 `ALTER TABLE ADD FOREIGN KEY`。

#### MySQL 对应能力

MySQL 有这项语义：关闭 `foreign_key_checks` 后可以删除仍被引用的表，也可以按非依赖顺序导入表；之后重新创建的父表必须满足仍然存在的外键定义。重新打开检查不会自动扫描并修复既有不一致数据。

- [MySQL 8.4 Foreign Key Constraints](https://dev.mysql.com/doc/refman/8.4/en/create-table-foreign-keys.html)

#### 保留边界

- 保留 Mock FK Parent Schema、operation enum、4 张内表及 Schema 刷新/缓存链。
- 保留 CREATE/DROP/RENAME/FLASHBACK/CTAS/CREATE LIKE 等路径中的 Mock Parent 生命周期处理。
- 保留 `is_parent_table_mock_` 在外键元数据、Information Schema 和 DML Codegen 中的语义。
- 保留现有 mysqltest 回归，继续保证关闭与重新打开 `foreign_key_checks` 时的 MySQL 兼容行为。
- 本项结论不意味着放宽普通外键校验；`foreign_key_checks=ON` 时的父表、唯一键、列类型和 DML 引用完整性检查仍须保留。

### ENUM-08：删除 Oracle DML Error Logging

> 对齐结论（2026-07-16）：**按建议删除**。删除普通 INSERT/UPDATE/DELETE 的 Oracle `LOG ERRORS INTO ... REJECT LIMIT ...` 错误表写入链；保留 MySQL `IGNORE`、`ON DUPLICATE KEY UPDATE`、`SHOW WARNINGS`，以及 `LOAD DATA` 自身的坏行诊断和 Reject Limit 能力。

#### 枚举抓手

- `ObErrLogType::{OB_ERR_LOG_INSERT, OB_ERR_LOG_UPDATE, OB_ERR_LOG_DELETE}`。
- Logical Operator `LOG_ERR_LOG`、Physical Operator `PHY_ERR_LOG`。
- parser item `T_ERR_LOG_CALUSE`、`T_INTO_ERR_LOG_TABLE`、`T_ERR_LOG_SIMPLE_EXPR`、`T_ERR_LOG_LIMIT`。

#### 服务的产品功能

该功能对应 Oracle DML Error Logging。普通 INSERT、UPDATE 或 DELETE 遇到可捕获的单行错误时，不立即终止整条语句，而是继续处理后续行，并把失败行写入指定错误表。错误表要求包含：

- `ORA_ERR_NUMBER$`
- `ORA_ERR_MESG$`
- `ORA_ERR_ROWID$`
- `ORA_ERR_OPTYP$`
- `ORA_ERR_TAG$`

用户语义类似：

```sql
INSERT INTO target_table
SELECT ...
LOG ERRORS INTO err_table
REJECT LIMIT 100;
```

#### 可达性与实现规模

- 当前 MySQL INSERT、UPDATE、DELETE grammar 已将 error-logging 子节点固定为 `NULL`，源码注释也明确标记 `unused in mysql`。
- `T_ERR_LOG_CALUSE` 等 item type 没有当前 grammar 产生式。
- `resolve_err_log_table()`、`resolve_err_log_reject()` 和 `set_is_error_logging()` 均没有调用者。
- 未找到 `DBMS_ERRLOG.CREATE_ERROR_LOG` 或其他为用户创建标准错误表的产品闭环。
- 后端仍保留 Stmt 状态、错误表结构校验、Optimizer `ERROR LOGGING` 算子、Codegen、物理算子、DML Runtime 和动态内部 SQL 写错误表逻辑。
- 聚焦搜索命中 **34 个源码文件、168 处引用**；仅 `ob_err_log_service`、`ob_err_log_op`、`ob_log_err_log` 等专用实现文件约 **682 行**。

#### 为什么建议删除

1. 当前用户 SQL 无法产生 Error Logging AST，后端链路无法正常启用。
2. 功能依赖 Oracle 专用错误表格式，但缺少标准建表入口，无法形成完整可用产品能力。
3. 每个失败行都需要提取列值、拼接带引号的内部 INSERT SQL 并通过 SQL Proxy 再写错误表，增加转义、事务边界和二次失败处理复杂度。
4. MySQL 模式已有 `IGNORE`、`ON DUPLICATE KEY UPDATE` 和 Warning 诊断，不需要保留另一套不可达的 Oracle 行级错误表机制。
5. 未找到当前普通 DML Error Logging 的有效回归用例。

#### MySQL 对应能力

MySQL 没有 Oracle 风格的 `LOG ERRORS INTO ... REJECT LIMIT`。MySQL 的相近能力是 `INSERT/UPDATE IGNORE`、`ON DUPLICATE KEY UPDATE` 以及 `SHOW WARNINGS`，但不会把每条失败记录写入具有 `ORA_ERR_*` 列的错误表。

- [MySQL 8.4 INSERT](https://dev.mysql.com/doc/refman/8.4/en/insert.html)
- [MySQL 8.4 SHOW WARNINGS](https://dev.mysql.com/doc/refman/8.4/en/show-warnings.html)
- [Oracle DML Error Logging](https://docs.oracle.com/en/database/oracle/oracle-database/19/arpls/DBMS_ERRLOG.html)

#### 建议删除面

1. 删除 `ObErrLogType`、`LOG_ERR_LOG`、`PHY_ERR_LOG` 及其 operator factory/registration。
2. 删除四个 `T_ERR_LOG_*` parser item 和 DML parse tree 中永远为 `NULL` 的专用槽位。
3. 删除 `ObErrLogInfo`、Stmt getter/setter、deep-copy、visitor 和打印字段。
4. 删除 `resolve_err_log_table()`、`resolve_err_log_reject()`、`ORA_ERR_*` 强制列校验和支持类型判断。
5. 删除 Optimizer 中 Error Logging logical operator、计划分配、表达式抽取与属性传播。
6. 删除 Codegen、`ObErrLogCtDef/RtDef`、`ObErrLogService`、错误捕获和动态内部 INSERT。
7. 删除只服务于该功能的错误码、构建项和残留测试。

#### 保留边界

- 保留普通 INSERT、UPDATE、DELETE、MERGE 及其事务原子性。
- 保留 MySQL `INSERT/UPDATE/DELETE IGNORE` 当前已实现的行为。
- 保留 `ON DUPLICATE KEY UPDATE`、REPLACE、`SHOW WARNINGS/ERRORS` 和 SQL Mode 错误/告警转换。
- 保留 `LOAD DATA` 自己的 `LOGFILE`、`BADFILE`、坏行告警、`REJECT LIMIT` 等导入诊断能力；它不使用 Oracle `ORA_ERR_*` 错误表。
- 保留内部日志、SQL Audit、诊断事件和普通错误码框架。

#### 验收建议

- 全库不再出现 `LOG_ERR_LOG`、`PHY_ERR_LOG`、`ObErrLogService`、`ObErrLogInfo` 和 `T_ERR_LOG_CALUSE`。
- 普通 DML 错误仍按事务规则回滚；IGNORE、ON DUPLICATE KEY UPDATE 和 SHOW WARNINGS 回归通过。
- `LOAD DATA` 坏行跳过、Reject Limit、错误文件或 Warning 输出保持原行为。
- 检查历史计划序列化或内部 RPC 中是否包含 `PHY_ERR_LOG`；若当前入口长期不可达，理论上不应存在需要恢复的有效计划。

### ENUM-09：删除 Oracle PL 自治事务

> 对齐结论（2026-07-16）：**按建议删除**。删除用户 PL/Trigger 的 Oracle `PRAGMA AUTONOMOUS_TRANSACTION` 状态和专用运行时分支；保留 MySQL 正常事务、Savepoint、PL 隐式 Savepoint，以及表锁等内部流程仍需要的事务上下文切换能力，并将后者从“autonomous”产品语义中拆出。

#### 枚举抓手

- `ObPLFunctionBase::IS_AUTONOMOUS_TRANSACTION = 6`。
- parser item `T_SP_PRAGMA_AUTONOMOUS_TRANSACTION`。
- Deadlock Detector 的 `UnregisterPath::AUTONOMOUS_TRANS`。
- 配套的 `OB_ERR_AUTONOMOUS_TRANSACTION_ROLLBACK`、Trigger `is_has_auto_trans_` 和 PL Context `is_autonomous_`。

#### 服务的产品功能

该功能对应 Oracle PL/SQL 的自治事务。过程、函数或 Trigger 声明：

```sql
PRAGMA AUTONOMOUS_TRANSACTION;
```

后，运行时会暂存调用者的事务上下文，启动一个独立事务；内部可以自行 `COMMIT` 或 `ROLLBACK`，结束后再恢复调用者事务。典型用途是把审计日志、错误日志或通知状态独立提交，使调用者之后回滚也不影响这些记录。

#### 可达性与实现规模

- 当前 `pl_parser_mysql_mode.y` 只保留 `PRAGMA INTERFACE` 等 MySQL PL 产生式，没有 `PRAGMA AUTONOMOUS_TRANSACTION` 产生式。
- `T_SP_PRAGMA_AUTONOMOUS_TRANSACTION` 只剩生成 item type 和高级 Trigger resolver 的识别分支，当前 grammar 无法生成该节点。
- `ObPLFunctionBase::set_autonomous()` 全仓没有调用者，因此新编译的 Procedure/Function 无法设置自治事务标志。
- 后端仍保留完整的 PL 运行时：
  - 保存主事务并启动独立事务；
  - 自治块退出时检测未提交 DML、强制回滚并恢复主事务；
  - 调整 PL 隐式 Savepoint、`has_exec_inner_dml` 和 Autocommit 状态；
  - 为自治事务关闭部分本地重试和 nested-SQL/mutating-table 限制；
  - 在父子事务之间向 Deadlock Detector 注册 Wait-For 依赖；
  - 在 SPI、DML Service、DAS 和 Query Retry 中传播自治状态。
- 聚焦搜索命中 **22 个源码文件、97 处引用**；另有 **13 个测试文件、58 处 `PRAGMA AUTONOMOUS_TRANSACTION`**，主要属于已失去当前语法入口的 Oracle PL 兼容语料。
- 现有审查记录只在通用死锁检测说明中提到 Autonomous Transaction 会注册依赖，未把该产品功能列为在途删除。

#### 为什么建议删除

1. 当前 grammar 无法创建自治 Routine，例程标志也没有设置入口，产品功能已经不可达。
2. 后端残留却侵入 Session 事务状态、PL 生命周期、重试、DAS、DML 和 Deadlock Detector 等高风险主链。
3. 自治事务改变事务隔离和错误恢复边界；在缺少有效入口与回归的情况下，保留这些特殊分支反而扩大正常 MySQL 事务路径的状态空间。
4. 高级 System Trigger 的自动事务消费方已随 `ENUM-06` 进入删除清单，进一步降低保留 Oracle 自治事务产品语义的价值。
5. 审计或错误日志若确实需要独立提交，应由明确的内部连接或服务机制实现，而不应依赖一套用户不可声明的 Oracle PL pragma。

#### MySQL 对应能力

MySQL 没有 `PRAGMA AUTONOMOUS_TRANSACTION` 或等价的存储程序自治事务。Stored Procedure 在调用者的事务上下文中执行；Stored Function 和 Trigger 不能启动事务，也不能执行显式或隐式提交/回滚。

- [MySQL 8.4 Restrictions on Stored Programs](https://dev.mysql.com/doc/refman/8.4/en/stored-program-restrictions.html)
- [MySQL 8.4 Stored Program Binary Logging](https://dev.mysql.com/doc/refman/8.4/en/stored-programs-logging.html)

#### 建议删除面

1. 删除 `IS_AUTONOMOUS_TRANSACTION`、`set_autonomous()/is_autonomous()` 和 PL AST/Function Flag 中的对应状态。
2. 删除 `T_SP_PRAGMA_AUTONOMOUS_TRANSACTION`、Trigger `is_has_auto_trans_` 及高级 Trigger resolver/source generator 中的 pragma 分支。
3. 删除 `ObPLContext` 中自治事务启动、结束、未提交检测、状态恢复和 Deadlock 注册逻辑。
4. 删除 SPI、DML Service、DAS、SQL Utils 和 Query Retry 中只为 PL 自治块存在的分支。
5. 删除 `OB_ERR_AUTONOMOUS_TRANSACTION_ROLLBACK` 及只服务于该错误的处理。
6. 删除 PL 自治事务专用测试和不可达 Oracle 兼容语料。
7. 将表锁等内部调用的 `begin/end_autonomous_session()` 抽成明确的内部事务上下文切换接口；确认所有内部消费者迁移后，再删除或重命名旧接口及 `AUTONOMOUS_TRANS` Deadlock 路径名。

#### 保留边界

- 保留普通 `START TRANSACTION`、`COMMIT`、`ROLLBACK` 和 XA 等已确认能力。
- 保留 MySQL Stored Procedure 在调用者事务上下文中的正常执行。
- 保留 Savepoint、PL 隐式 Savepoint 和存储函数/Trigger 调用时的 Savepoint level。
- 保留表锁、内部 SQL 或其他基础设施确实需要的独立内部事务；只移除其对 Oracle PL 自治事务枚举和命名的耦合。
- 保留通用 Deadlock Detector；只删除或泛化自治 PL 父子事务的专用注册路径。

#### 验收建议

- 全库不再出现 `IS_AUTONOMOUS_TRANSACTION`、`T_SP_PRAGMA_AUTONOMOUS_TRANSACTION`、`OB_ERR_AUTONOMOUS_TRANSACTION_ROLLBACK` 和 PL `in_autonomous()`。
- Procedure、Function、Trigger 的普通事务、异常回滚、Autocommit 和 Savepoint 回归通过。
- nested SQL 的重试、mutating-table 校验和 DML 事务判定在删除特殊豁免后回归通过。
- 表锁内部事务切换、超时恢复和 Deadlock Detector 回归通过，且不再以用户 PL Autonomous Transaction 命名。
- 删除前检查存量 PL cache/schema 中是否可能持久化第 6 位自治标志；当前 grammar 与 setter 均不可达，理论上不应由本版本新建。

### ENUM-10：删除 Oracle Interval Partition

> 对齐结论（2026-07-16）：**按建议删除**。删除 Oracle 风格、按分区键值自动创建新 RANGE 分区的 Interval Partition 专属链；保留普通 RANGE/RANGE COLUMNS、手工分区 DDL、按容量触发的 Auto Split、Dynamic Partition，以及普通 SQL 日期时间 `INTERVAL` 表达式。

#### 枚举抓手

- `ObPartitionFuncType::PARTITION_FUNC_TYPE_INTERVAL`。
- `MayAddIntervalPart::{NO, YES, PART_CHANGE_ERR}`。
- parser item `T_SET_INTERVAL`。
- `ObAlterTableArg::{SET_INTERVAL, INTERVAL_TO_RANGE}`。
- schema operation `OB_DDL_SET_INTERVAL`、`OB_DDL_INTERVAL_TO_RANGE`。
- 配套的 `transition_point_`、`interval_range_` 和 Interval Partition 专用错误码。

#### 服务的产品功能

该功能对应 Oracle Interval Partition。用户先定义 RANGE 分区、transition point 和固定的 interval；当 INSERT/UPDATE 的分区键超过现有边界时，执行器根据 `transition_point_` 与 `interval_range_` 计算新 high bound，内部执行：

```sql
ALTER TABLE ... ADD PARTITION P_SYS_n VALUES LESS THAN (...);
```

然后设置专用错误码令原 DML 重试。`ALTER TABLE ... SET INTERVAL (...)` 还负责在普通 RANGE 与 Interval Partition 之间转换。

#### 可达性与实现规模

- 当前 MySQL `range_partition_option` 始终构造 7 个子节点，但 `RANGE_INTERVAL_NODE = 5` 对应的第六个节点固定传入 `NULL`；`resolve_interval_clause()` 因而不会取得 interval 定义。
- `T_SET_INTERVAL` 只剩生成 item type 和 `ObAlterTableResolver` 的 switch 分支，在当前 `.y/.l` 文件中没有产生该节点的语法。
- 测试目录未找到实际创建 Interval Partition 或执行 `ALTER TABLE ... SET INTERVAL` 的回归；命中的普通 `INTERVAL` 均为日期表达式或标识符。
- 后端仍保留完整链路：
  - create/alter resolver 校验 interval 类型、transition point 和分区边界；
  - executor 将分区类型改为 `PARTITION_FUNC_TYPE_INTERVAL`；
  - schema 在内表和序列化中持久化 `transition_point_`、`interval_range_`；
  - optimizer、codegen 和 DML runtime 传播 `MayAddIntervalPart`；
  - 找不到目标分区时同步发起内部 `ALTER TABLE ... ADD PARTITION`，再触发 DML retry；
  - RootService/DDL Service 处理设置 Interval、转换回 RANGE、重复分区过滤和 schema 更新。
- 聚焦枚举、元数据字段和运行链路共命中 **23 个源码文件、154 处引用**，已经形成独立的中大型删除面。
- 既有审查记录保留的是 Auto Split、Range Auto Partition 和 Dynamic Partition，没有把 Oracle Interval Partition 标为在途删除。

#### 为什么建议删除

1. 当前 grammar 无法创建或修改 Interval Partition，功能对新用户不可达。
2. 不可达后端仍侵入 schema 持久化、分区定位、DML codegen、内部 DDL、RPC 和 retry 主链，维护成本与风险显著。
3. 自动补分区通过 DML 执行期间拼接并执行内部 `ALTER TABLE`，引入并发建分区、重复创建、schema 刷新和重试等特殊状态。
4. seekdb 已有独立的 Auto Split/Dynamic Partition 能力，无须为不可达的 Oracle 值区间自动补分区再保留一套平行机制。
5. MySQL 没有 Oracle 式 Interval Partition，收缩该状态不会损害 MySQL 兼容目标。

#### MySQL 对应能力

MySQL 没有 `PARTITION BY RANGE ... INTERVAL (...)` 或 `ALTER TABLE ... SET INTERVAL`。MySQL 可以按日期或时间表达式使用普通 RANGE/RANGE COLUMNS 分区，但各个 `VALUES LESS THAN` 边界需要显式定义；后续使用 `ADD PARTITION` 或 `REORGANIZE PARTITION` 管理。

- [MySQL 8.4 RANGE Partitioning](https://dev.mysql.com/doc/refman/8.4/en/partitioning-range.html)
- [MySQL 8.4 CREATE TABLE Partitioning Syntax](https://dev.mysql.com/doc/refman/8.4/en/create-table.html)
- [Oracle Interval Partitioning](https://docs.oracle.com/en/database/oracle/oracle-database/26/vldbg/partition-availability.html)

#### 建议删除面

1. 删除 `PARTITION_FUNC_TYPE_INTERVAL`、`is_interval_part()` 及普通 range 判断中的 Interval 分支。
2. 删除 `MayAddIntervalPart` 中自动建 Interval Partition 的语义；其中普通 UPDATE 跨分区报错若仍有消费者，应改为独立、通用的 row-movement 状态。
3. 删除 create/alter resolver 的 interval clause、`T_SET_INTERVAL`、`SET_INTERVAL/INTERVAL_TO_RANGE` 和对应 DDL operation。
4. 删除 `set_interval_value()`、transition/interval 合法性校验和 RANGE/Interval 转换逻辑。
5. 删除 optimizer/codegen/runtime 中自动补 Interval Partition 的状态传播、内部 RPC、动态 `ALTER TABLE` 和专用 retry/error mapping。
6. 删除 RootService/DDL Service 中设置 Interval、自动补分区、重复 Interval Partition 过滤及专用 schema 更新分支。
7. 在兼容迁移完成后，删除 `transition_point_`、`interval_range_` 的 schema 字段、内表读写、十六进制转换和序列化代码。
8. 删除 Interval Partition 专用错误码、展示逻辑和无效测试语料。

#### 保留边界

- 保留普通 RANGE、RANGE COLUMNS、LIST、HASH、KEY 分区和分区裁剪。
- 保留用户显式 `ADD/DROP/REORGANIZE/TRUNCATE/RENAME/SPLIT PARTITION` 等当前支持的分区 DDL。
- 保留 `PARTITIONS AUTO`、`auto_part_`、`auto_part_size_` 和按 tablet 容量触发的 Auto Split；代码现状本身明确禁止 Interval 类型进入 split 链。
- 保留 Dynamic Partition 的策略、调度和分区生命周期。
- 保留普通 SQL `INTERVAL` 日期运算、窗口 frame、Materialized View refresh interval 等同名但无关的语法。
- 保留通用 schema refresh、DDL retry 和 partition-not-found 错误处理，只移除 Interval 专属分支。

#### 兼容风险与验收建议

- `PARTITION_FUNC_TYPE_INTERVAL` 是持久化枚举值，删除阶段不能直接重排后续值；先保留 tombstone 或显式反序列化兼容。
- `transition_point_`、`interval_range_` 已进入 `ObTableSchema` 序列化以及 `__all_table` 系列内表字段。先扫描存量 schema；若存在 Interval 表，应先拒绝升级或转换为显式 RANGE 分区。
- 全库不再出现 `PARTITION_FUNC_TYPE_INTERVAL`、`T_SET_INTERVAL`、`MayAddIntervalPart::YES` 和 `send_add_interval_partition_rpc*`。
- 普通 RANGE/RANGE COLUMNS 建表、分区裁剪和手工分区 DDL 回归通过。
- Auto Split 和 Dynamic Partition 回归通过，确认没有误删 `auto_part_`/`auto_part_size_` 链。
- INSERT/UPDATE 命中不存在分区时按普通 RANGE 规则报错，不再隐式创建分区和重试。

### ENUM-11：删除 Oracle PL `NOCOPY` 参数语义

> 对齐结论（2026-07-16）：**按建议删除**。删除 Oracle `OUT/IN OUT NOCOPY` 的按引用别名、修改即时可见和异常时可能提前暴露结果的专属语义；保留 MySQL `IN/OUT/INOUT`、普通参数转换与深拷贝、成功后 copy-out、异常时不回写和复合对象通用清理。

#### 枚举抓手

- `SP_PARAM_NOCOPY_MASK = 0x100`：持久化在 Routine Param `flag_` 中的参数属性位。
- `ObPLRoutineParam::is_nocopy_`。
- `ObPLCallStmt`、`ObUDFRawExpr`、`ObExprUDF` 和 `ObPLExecCtx` 中的 `nocopy_params_` 别名索引数组。
- `ObSPIService::spi_process_nocopy_params()`。

`SP_PARAM_NOCOPY_MASK` 虽以位掩码而非独立 C++ `enum` 成员定义，但它与参数模式、外部类型和 SELF 属性共用同一个持久化枚举/标志空间，并驱动了一条完整运行时状态分支，因此按同一枚举纵切口径纳入。

#### 服务的产品功能

Oracle PL 默认对 `OUT` 和 `IN OUT` 参数使用 value-result/copy-in-copy-out 语义；`NOCOPY` 是一个编译器提示，请求在条件允许时改为按引用传递大型集合、记录或对象，以降低复制开销。

按引用后，formal parameter 与 actual parameter 可能指向同一对象：

- 被调过程修改 formal parameter 时，调用方变量可能立即可见；
- 同一个 actual parameter 传给多个 formal parameter 时会产生别名；
- 被调过程异常退出时，已经发生的部分修改仍可能留在调用方。

seekdb 为此保留了完整实现：

- Routine Schema 持久化 `0x100` 属性；
- Resolver 根据 formal/actual 类型和表达式同一性生成别名索引；
- Call Stmt、Raw Expr、Codegen、物理 UDF 和 Interpreter 传播该索引；
- SPI 在参数赋值时同步其他别名；
- 异常释放和 OUT 参数回写路径根据是否 NOCOPY 选择不同的复制与析构行为。

#### 当前可达性与实现规模

- 当前 MySQL PL Grammar 的参数产生式只有 `opt_sp_inout ident param_type opt_param_default`，没有 `NOCOPY` token 或产生式。
- `T_SP_PARAM` 由已清零的 `ParseNode` 创建，Grammar 只设置 `value_`/参数模式，不设置 `int32_values_[1]`；`ObCreateRoutineResolver` 虽保留 `int32_values_[1] == 1` 时设置 NOCOPY 的分支，但当前入口永远不会产生该值。
- 包内及嵌套 Routine Resolver 创建 `ObPLRoutineParam` 时，`is_nocopy` 直接固定为 `false`。
- 当前 `syspack_codegen.py` 的自动装载清单只包含 MySQL 分组的系统包，所列 `_mysql.sql` 文件没有 `NOCOPY` 声明。
- 仓库中 `UTL_TCP`、`UTL_SMTP`、旧 `DBMS_SQL` 等 Oracle 包 SQL 仍有大量 `NOCOPY` 文本，但这些文件已退出当前自动装载清单，属于 Oracle Mode 删除后遗留的源码/发布物清理面，不是当前 seekdb 的活跃系统包消费者。
- C++ 聚焦命中 **21 个源码文件、116 处引用**，横跨 schema、PL resolver、Call Stmt、Raw Expr、Codegen、Interpreter、Routine Executor、Trigger、UDF 和 SPI，规模足以形成独立删除任务。
- 既有“seekdb + 审查记录”未将 NOCOPY 参数链标记为在途删除。

#### 为什么建议删除

1. 当前 Grammar、包内 Resolver 和现行系统包装载清单均不能创建新的 NOCOPY 参数，用户入口已经不可达。
2. 后端仍改变参数别名、OUT 回写、异常可见性、对象释放和表达式序列化，是侵入 PL 主执行链的高复杂度状态。
3. Oracle 将 `NOCOPY` 定义为可能被优化器忽略的提示；保留它需要维护不稳定的别名和异常语义，却无法服务 MySQL 兼容目标。
4. MySQL 只定义 `IN/OUT/INOUT`，并明确在未处理异常退出时不把修改后的 `OUT/INOUT` 传播回调用方；删除 NOCOPY 后的普通 copy-out-on-success 模型与该行为一致。
5. 普通 `OUT/INOUT` 和复合对象参数已有独立的转换、深拷贝、成功回写及失败清理逻辑，不需要依赖 NOCOPY 属性才能工作。

#### MySQL 对应能力

MySQL 没有 `NOCOPY` 参数修饰符或第四种参数模式。Stored Procedure 参数只支持 `IN`、`OUT` 和 `INOUT`；Stored Function 参数均按输入参数处理。过程以未处理异常退出时，修改后的 `OUT/INOUT` 不传播给调用方。

- [MySQL 8.4 CREATE PROCEDURE and CREATE FUNCTION](https://dev.mysql.com/doc/refman/8.4/en/create-procedure.html)
- [MySQL 8.4 Condition Handling and OUT or INOUT Parameters](https://dev.mysql.com/doc/refman/8.4/en/conditions-and-parameters.html)
- [Oracle Formal Parameter Declaration：NOCOPY](https://docs.oracle.com/en/database/oracle/oracle-database/23/lnpls/formal-parameter-declaration.html)
- [Oracle Subprogram Parameter Aliasing](https://docs.oracle.com/en/database/oracle/oracle-database/26/lnpls/subprogram-parameters.html)

#### 建议删除面

1. 删除 `SP_PARAM_NOCOPY_MASK`、`set_nocopy_param()/is_nocopy_param()` 和 Routine Param 构造/转换中的属性复制。
2. 删除 `ObPLRoutineParam::is_nocopy_`、setter/getter 及 `make_routine_param()` 的 NOCOPY 参数。
3. 删除 `resolve_nocopy_params()`、formal/actual 同一性分析和别名索引计算。
4. 删除 Call Stmt、Raw Expr、Codegen、物理 UDF、Interpreter、Routine Executor、Trigger Handler 和 PL Aggregate 中 `nocopy_params` 的存储、序列化及机械传递。
5. 删除 `spi_process_nocopy_params()` 和参数赋值时同步其他别名的逻辑。
6. 删除只因 NOCOPY 存在的提前 OUT 回写、异常释放和复合对象析构分支；将剩余路径收敛为普通参数 copy-in/copy-out。
7. 清理已退出装载清单的 Oracle 系统包 SQL 中的 `NOCOPY` 残留；若对应整个包已无消费者，应随包统一删除，而不是仅机械删除关键字。
8. 删除 NOCOPY 专用测试、错误语料、注释和历史生成文件残留。

#### 保留边界

- 保留 MySQL `IN`、`OUT`、`INOUT` 参数模式及默认 `IN` 行为。
- 保留 actual/formal 参数类型校验、隐式转换和普通 scalar/LOB/collection/record/object 参数传递。
- 保留 `OUT/INOUT` 在过程成功返回后的 copy-out，以及未处理异常时不回写。
- 保留复合对象通用的深拷贝、引用计数、析构、失败清理和内存所有权管理；只删除由 NOCOPY 别名状态触发的特殊分支。
- 保留 Trigger `NEW.col`、用户变量、系统变量、包变量等有效 OUT target 的普通写回。
- 不重排 Routine Param `flag_` 中后续位；`0x100` 作为历史空洞保留，`SP_PARAM_DEFAULT_CAST = 0x200` 等值不得前移。

#### 兼容风险与验收建议

- `0x100` 已持久化到 `__all_routine_param.flag`。删除前扫描存量行；加载历史 schema 时应显式忽略或清除该位，不能让旧 NOCOPY Routine 悄然继续走按引用语义。
- 若存量对象存在该位，建议在升级阶段重编译为普通 `OUT/INOUT`，或者拒绝升级并提示先清理，不承诺保留 Oracle 异常可见性差异。
- 全库不再出现 `SP_PARAM_NOCOPY_MASK`、`is_nocopy_param`、`resolve_nocopy_params`、`spi_process_nocopy_params` 和运行时 `nocopy_params_`。
- MySQL Procedure 的 scalar、LOB、collection/record/object `OUT/INOUT` 正常返回和异常退出用例通过。
- 同一 actual variable 传给多个参数时按普通 value-result 规则执行，不再发生修改即时可见或别名同步。
- SQL 中调用 PL Function、PL 内部嵌套调用、Trigger 调用和动态执行路径的参数回写及对象释放回归通过。
- 当前系统包清单中的所有 MySQL 包完成编译与调用回归，确认不依赖 NOCOPY。

### ENUM-12：删除失活的 PL 逐行 Profiler / `DBMS_PROFILER`

> 对齐结论（2026-07-18）：**按建议删除**。删除旧 LLVM Code Generator 的逐语句插桩模式、`PROFILE` 专用 PL Cache Key、已经没有实现者的 profiler runtime 接口以及未进入当前装载清单的 Oracle `DBMS_PROFILER` 包；保留解释器、普通 PL Cache、PL build/execute 耗时统计和此前决定保留的 MySQL `SHOW PROFILE/PROFILES` 兼容空壳。

#### 枚举抓手

- `ObPLObjectKey::ObjectMode::{NORMAL, PROFILE}`。
- `ObPLObjectKey::mode_`：参与 PL Cache Key 的 deep-copy、hash 和 equality。
- 与 `PROFILE` 模式绑定的 `profiler_unit_info_`、`ObPLProfilerTimeStack`、`spi_pl_profiler_before_record()` / `spi_pl_profiler_after_record()`。

`ObjectMode::PROFILE` 原本不是展示标签，而是让带逐语句 profiler 插桩的 PL native body 与普通 body 使用不同 Cache Key。它驱动了一条独立的构建、缓存、运行时和系统包功能链，符合枚举纵切成项门槛。

#### 服务的产品功能

该链服务 Oracle `DBMS_PROFILER` 风格的 PL 逐行性能分析：

1. 启动 profiler 后，以 `PROFILE` 模式重新取得或构建 routine/package cache object；
2. 旧 LLVM Code Generator 在声明、赋值、分支、循环、SQL、Cursor、CALL 等大量 PL statement 前后生成 `spi_pl_profiler_before_record` / `after_record` 调用；
3. `profiler_unit_info_` 标识 package、routine 或匿名块采样单元；
4. runtime 累计每个源码行的执行次数和耗时；
5. `DBMS_PROFILER` 系统包负责开始、暂停、恢复、停止、flush，并把结果写入 `PLSQL_PROFILER_RUNS/UNITS/DATA`。

这是一种依赖编译期插桩的执行后端功能。它与 MySQL `SHOW PROFILE` 的会话级 SQL statement 资源统计不是同一能力。

#### 当前可达性与实现规模

- 当前 `ObPLExecState::execute()` 无条件构造 `ObPLInterpreter` 并 tree-walk AST，没有 native/JIT fallback，也没有 profiler 执行器分支。
- 历史提交 `fa28bea9fbd` 删除旧 LLVM Code Generator 时，一并删除了数十个逐 statement 插桩生成点和 `profile_mode_`；当前解释器没有补建等价插桩。
- `ObSQLSessionInfo::get_pl_profiler()` 恒定返回 `nullptr`，因此 `ob_pl.cpp` 和 `ob_pl_package_manager.cpp` 的四处 `ObjectMode::PROFILE` 选择分支永远不会成立。
- `ObPLProfiler` 当前只有前置声明，没有类定义；`ObPLProfilerTimeStack` 也只剩前置声明、空指针字段和无人调用的 getter/setter。
- 两个 `spi_pl_profiler_*_record()` 当前都是直接返回 `OB_SUCCESS` 的空实现。
- `ObPLBuilder` 仍为匿名块、routine 和 package 设置并递归传播 `profiler_unit_info_`，但当前全仓没有读取者。
- `OB_PL_PROFILER` allocator label 仍存在，但没有分配消费者。
- `dbms_profiler.sql` 与 `dbms_profiler_body.sql` 共 **406 行**，没有出现在当前 `syspack_codegen.py` 的 `syspack_config`；包体声明的 7 个 `DBMS_PROFILER_*` `PRAGMA INTERFACE` 在当前 C++ interface 表中没有对应实现。
- 直接命名残留覆盖 **11 个文件、42 处聚焦命中**，另有 `ob_pl_cache.{h,cpp}` 的 `mode_` hash/equality 实现；连同系统包 SQL，直接删除面超过 500 行。
- 已复核现有“seekdb + 审查记录”最新正文及语法审查汇总，没有把 PL `DBMS_PROFILER` / `ObjectMode::PROFILE` 标记为在途删除。

#### 为什么建议删除

1. feature enable 状态、插桩生产者、runtime 累计器、系统包装载和 C++ interface 实现同时缺失，当前不是“部分可用”，而是完整不可达。
2. 继续保留会让每个 PL Cache Key 多携带一个恒为 `NORMAL` 的维度，并让 builder 递归传播无人读取的 profiler metadata。
3. 在解释器下重新支持逐行 profiler，需要重新定义 statement 进入/退出、异常、handler、循环迭代和嵌套 CALL 的计时语义并重新实现 runtime；这属于重新建设产品功能，不是保留现有兼容代码。
4. 该功能来自 Oracle `DBMS_PROFILER`，不属于 seekdb 的 MySQL 兼容基线；删除不会造成 MySQL Stored Routine 语法缺口。
5. seekdb 已有 PL build/execute 总耗时等观测手段。是否未来建设解释器 profiler，应以新的、可验证的接口单独立项，而不应依赖当前失活壳层。

#### MySQL 对应能力

MySQL 没有 `DBMS_PROFILER` 包，也没有与这条链等价的 Stored Routine 源码逐行 profiler。MySQL Stored Program runtime 解释执行 `sp_instr` 指令；官方 `SHOW PROFILE/PROFILES` 统计当前 session 中 SQL statement 的资源使用，并已标记 deprecated，不能视为 `DBMS_PROFILER` 的对应能力。

- [MySQL Stored Programs Internal Implementation](https://dev.mysql.com/doc/dev/mysql-server/8.0.46/stored_programs.html)
- [MySQL 8.4 SHOW PROFILE](https://dev.mysql.com/doc/refman/8.4/en/show-profile.html)
- [MySQL 8.4 SHOW PROFILES](https://dev.mysql.com/doc/refman/8.4/en/show-profiles.html)

#### 建议删除面

1. 删除 `ObjectMode` enum 以及恒为 `NORMAL` 的 `ObPLObjectKey::mode_`；同步删除构造/reset/deep-copy/hash/equality 中的该维度。
2. 删除 `ob_pl.cpp` 和 `ob_pl_package_manager.cpp` 中四处根据 `get_pl_profiler()` 选择 `PROFILE` 的分支。
3. 删除 `profiler_unit_info_`、getter/setter、匿名块/routine/package 赋值和 nested routine 递归传播。
4. 删除 `ObPLProfilerTimeStack` 前置声明、`ObPLExecState::profiler_time_stack_` 及其 getter/setter。
5. 删除 `ObPLProfiler` 前置声明和恒定返回空的 `get_pl_profiler()` Session API。
6. 删除两个空 `spi_pl_profiler_*_record()` 的声明与定义。
7. 删除 `OB_PL_PROFILER` allocator label。
8. 删除 `ObPLPackageManager::update_special_package_status()` 中空的 `DBMS_PROFILER` 特判；若函数随后无其他职责，一并删除函数和调用。
9. 删除未装载、无 interface 实现的 `dbms_profiler.sql`、`dbms_profiler_body.sql` 及相关发布/测试残留。

#### 保留边界

- 保留 `ObPLInterpreter`、resolved AST、SQL expression runtime、SPI 和 statement location 更新。
- 保留普通 PL Cache、package build lock、依赖版本校验和 schema invalidation；只删除 profiler 专用 key 维度。
- 保留 `compile_time_` 当前代表的 parse/resolve/expression build 耗时，以及 PL/SQL execution time、pure SQL time 等仍有读写者的统计。
- 保留 MySQL `SHOW PROFILE/PROFILES` 当前已经确认的语法和兼容空结果；不在本项中实现或删除 MySQL statement profiling。
- 保留通用性能诊断、外部 `perf`/eBPF/obperf 和普通 SQL/PL 日志。

#### 兼容风险与验收建议

- `ObjectMode` 只参与进程内 Cache Key，不是持久化 schema enum；删除不需要为 `PROFILE` 保留数值 tombstone，重启后自然重建 cache。
- 升级前扫描历史 schema 中是否仍存在 `DBMS_PROFILER` package/spec/body。若存在，应通过系统包升级步骤显式退休，不能只删除源码文件而留下可见但无法执行的对象。
- `PLSQL_PROFILER_RUNS/UNITS/DATA` 可能是旧功能在用户 schema 创建的普通表；不要在升级脚本中无条件删除用户数据，应仅停止创建并提供人工迁移/清理说明。
- 全库不再出现 `ObjectMode::PROFILE`、`get_pl_profiler`、`profiler_unit_info_`、`ObPLProfilerTimeStack` 和 `spi_pl_profiler_*_record`。
- 普通匿名块、procedure/function、package、nested routine、Trigger 和 SQL UDF 的 Cache 命中及失效回归通过。
- PL build/execute 耗时虚表或审计字段继续正常输出；`SHOW PROFILE/PROFILES` 的既有兼容结果不变。
- 当前 `syspack_config` 中全部 MySQL 系统包完成构建和装载回归，确认没有误删通用 `PRAGMA INTERFACE` 机制。

### ENUM-13：删除 Oracle PL `FORALL` 批量 DML 残链

> 对齐结论（2026-07-18）：**按建议删除**。删除已经没有 Grammar、AST、Resolver 或 Interpreter 生产者的 Oracle `FORALL`、`SAVE EXCEPTIONS`、`SQL%BULK_ROWCOUNT`、`SQL%BULK_EXCEPTIONS` 运行时残链；保留普通 MySQL Stored Routine 循环、普通隐式游标行数、客户端批执行、多行 DML、通用数组参数和 batched multi-statement 能力。

#### 枚举抓手

- `ObPLStmtType::PL_FORALL`。
- `ObPLGetCursorAttrInfo::Type::{PL_CURSOR_BULK_ROWCOUNT, PL_CURSOR_BULK_EXCEPTIONS, PL_CURSOR_BULK_EXCEPTIONS_COUNT}`。
- parser item `T_SP_FORALL`、`T_SP_CURSOR_BULK_ROWCOUNT`、`T_SP_CURSOR_BULK_EXCEPTIONS`、`T_SP_CURSOR_BULK_EXCEPTIONS_COUNT` 及 `src/objit` 镜像。
- 配套的 `ObPLSqlStmt::forall_sql_`、`ObPLSqlInfo::forall_sql_`、implicit cursor `in_forall_/save_exception_/forall_rollback_` 和 bulk result 数组。

#### 服务的产品功能

该链原来服务 Oracle PL/SQL Collection Bulk DML：

1. `FORALL i IN ...` 从 Collection 取出多组参数，批量绑定到同一条 `INSERT/UPDATE/DELETE`；
2. 支持普通上下界以及 `INDICES OF`、`VALUES OF` 等 Collection 遍历形态；
3. `SAVE EXCEPTIONS` 使单个元素执行失败后继续处理后续元素；
4. `SQL%BULK_ROWCOUNT(i)` 保存每次执行的影响行数；
5. `SQL%BULK_EXCEPTIONS` 保存失败元素序号和错误码；
6. 旧 Code Generator 优先走数组绑定优化，不满足条件时回退为逐元素循环。

这是一套 PL 层的服务端批量 DML 语义，不等同于 MySQL 客户端批执行、`INSERT ... VALUES (...), (...)` 或普通 batched multi-statement。

#### 当前可达性与实现规模

- 当前 `pl_parser_mysql_mode.y/.l` 中没有 `FORALL`、`SAVE EXCEPTIONS` 或三个 `SQL%BULK_*` 属性的产生式；四个 parser item 只剩固定编号声明和 `src/objit` 镜像。
- `PL_FORALL` 当前全仓只有枚举声明本身；`ObPLForAllStmt`、Factory allocation、Resolver 和 Visitor 主体均已删除。
- 当前 `ObPLInterpreter` 的 statement switch 没有 `PL_FORALL` case。
- `ObPLSqlStmt::set_forall_sql()` 没有调用者，`forall_sql_` 始终保持默认 `false`；解释器虽然把该值继续传给 SPI，但无法产生 true。
- `ObPL::set_implicit_cursor_in_forall()` / `unset_implicit_cursor_in_forall()` 没有调用者。它们原由 LLVM Code Generator 注册为 JIT callable，并由 `visit(ObPLForAllStmt)` 生成调用；Code Generator 删除后没有解释器替代入口。
- 历史提交 `4cf0007c7eb` 删除 Oracle PL 模块时净删约 3,880 行，其中包含完整的 FORALL AST、Resolver、表达式改写、array-binding 判断、Code Generator 与 SPI 辅助主体；后续解释执行重构又删除了最后的 JIT 生产者。
- 后半条残链仍覆盖 **19 个生产文件、170 处聚焦命中**：implicit cursor 每个实例继续携带三个 bool、两个数组及专用方法；SPI 热路径继续机械传递 `is_forall`；Static Engine Codegen、SQL Expression 和 Function Table 仍保留 FORALL 特判。
- 已复核现有“seekdb + 审查记录”正文，未把 FORALL 或 `SQL%BULK_*` 标记为在途删除；PL Native/JIT 在途项只覆盖执行后端，不能自动清掉这条跨 SQL Runtime 的产品专属状态链。

#### 为什么建议删除

1. 语法生产者、AST 节点、Resolver、Factory 和 Interpreter 同时缺失，当前不是受配置控制的可用能力，而是无法构造的状态空间。
2. 所有 `is_forall=true` 的入口都随 Oracle PL 主体或 LLVM Code Generator 删除；保留分支不会提供兼容性，只会让普通 PL SQL 执行持续承担参数、判断和 Cursor 状态成本。
3. 恢复该功能需要重新实现解释器 statement、Collection bounds、逐项异常、隐式 savepoint、批量参数改写与回退语义，属于重新建设 Oracle PL 功能。
4. MySQL Stored Routine 没有 FORALL 及其复合隐式游标属性，删除不会造成 MySQL 语法或行为缺口。
5. 删除后可以把 SPI 的 `is_forall` 维度收敛掉，同时保留并简化真正仍有消费者的 batched multi-statement 和通用 array parameter 路径。

#### MySQL 对应能力

MySQL 没有 `FORALL`、`SAVE EXCEPTIONS`、`SQL%BULK_ROWCOUNT` 或 `SQL%BULK_EXCEPTIONS`。MySQL Stored Routine 提供普通 `LOOP/WHILE/REPEAT`、Cursor、Condition Handler 和逐条 SQL statement；批量写入通常由多值 DML、客户端 prepared/batch 执行或应用循环实现，不具备 Oracle FORALL 的服务端 Collection Bulk Bind 与逐元素异常数组语义。

- [MySQL 8.4 Compound Statement Syntax](https://dev.mysql.com/doc/refman/8.4/en/sql-compound-statements.html)
- [MySQL 8.4 CREATE PROCEDURE and CREATE FUNCTION](https://dev.mysql.com/doc/refman/8.4/en/create-procedure.html)
- [Oracle FORALL / BULK COLLECT Examples](https://docs.oracle.com/en/database/other-databases/timesten/22.1/plsql-developer/examples-using-forall-and-bulk-collect.html)

#### 建议删除面

1. 删除 `ObPLStmtType::PL_FORALL`；当前未发现该 process-local AST tag 的持久化消费者，PL Cache 在升级/重启后重建。
2. 删除四个 `T_SP_*` item symbol 及 `src/objit` 镜像，但保留历史数值 **3954、3972、3973、3974** 为空洞，不重排后续 Item Type。
3. 删除 `ObPLSqlStmt/ObPLSqlInfo` 中的 `forall_sql_`、setter/getter、复制打印和解释器传参。
4. 删除 `ObPL::set_implicit_cursor_in_forall()`、`unset_implicit_cursor_in_forall()` 及声明。
5. 删除 `ObPLCursorInfo` 的 `in_forall_`、`save_exception_`、`forall_rollback_`、`bulk_rowcount_`、`bulk_exceptions_`，以及 set/get/reset/deep-copy/打印和 bulk result 方法。
6. 删除 `ObPLGetCursorAttrInfo` 的三个 `PL_CURSOR_BULK_*` 枚举、索引/错误码 metadata，及 Raw Expr Resolver、ExtraInfo 序列化和 runtime eval 分支。
7. 删除 SPI `SET_FORALL_BULK_EXCEPTION`、`is_forall` 参数传播、FORALL 专属失败吞并和数组绑定回退；通用接口收敛为普通 PL SQL 路径。
8. 删除 Static Engine Codegen、Function Table、Legacy SQL Expression 中基于 `get_in_forall()` 的分支；其中与 batched multi-statement 共用的 array parameter 逻辑只去掉 FORALL 条件，不删除通用实现。
9. 删除 `ObSQLUtils::transform_pl_ext_type/copy_params_to_array_params` 的 `is_forall` 形参和仅服务 FORALL 的额外 deep copy，保留 helper 及其他真实调用方。
10. 删除四个 FORALL 专属错误码和失效的 Oracle SQLQA/生成结果；不要按关键词误删无关的 `for_all` C++ 遍历命名。

#### 保留边界

- 保留 MySQL Procedure/Function/Trigger、普通 `LOOP/WHILE/REPEAT` 和普通 Condition Handler。
- 保留普通 implicit/explicit cursor 的 `%FOUND/%NOTFOUND/%ROWCOUNT` 对应实现及 MySQL `ROW_COUNT()`。
- 保留 MySQL 协议 prepared statement、客户端 array binding、普通 batch execution、batched multi-statement retry 和多值 DML。
- 保留 `ObSqlArrayObj`、通用参数数组变换和仍有非 FORALL 调用者的 SQL 优化；只删除恒为 false 的 FORALL 维度。
- 本项不捆绑删除 Collection、ARRAY/MAP/VECTOR 或普通 `FETCH/SELECT INTO`；`BULK COLLECT` 已按 ENUM-14 独立确认删除，避免和 FORALL 共用状态混在同一提交。
- 不删除 `OB_BATCHED_MULTI_STMT_ROLLBACK` 错误码整体，它仍被普通 batched multi-statement、Plan Cache、Resolver、Optimizer 和 Storage 路径使用；只删除 FORALL 对它的专用处理。

#### 兼容风险与验收建议

- 升级前扫描历史 Routine/Package/Trigger 源文本中的 `FORALL`、`SAVE EXCEPTIONS`、`%BULK_ROWCOUNT` 和 `%BULK_EXCEPTIONS`。若存在，这些对象在当前 Grammar 下已经无法重新 build，应明确报出并要求改写为普通循环，而不是静默接受后在运行期失败。
- `ObExprPLGetCursorAttr::ExtraInfo` 只进入内存 Plan/Cache 序列化；升级或重启时清理旧 Plan Cache，不把三个 bulk type 当作持久 schema contract。
- 全库不再出现 `PL_FORALL`、`T_SP_FORALL`、`get_in_forall`、`forall_sql_`、`SET_FORALL_BULK_EXCEPTION`、`PL_CURSOR_BULK_*` 和四个 FORALL 专属错误码。
- 普通 Stored Routine DML、嵌套 CALL、Trigger、Condition Handler 和 implicit cursor rowcount 回归通过。
- MySQL prepared statement batch、多值 INSERT、batched multi-statement 的优化、失败回退和错误返回回归通过。
- Collection 与普通 array parameter 完成针对性回归，证明没有把共用容器基础设施随 FORALL 误删；`BULK COLLECT` 由 ENUM-14 独立删除和验收。

### ENUM-14：删除 Oracle PL `BULK COLLECT` 批量取数残链

> 对齐结论（2026-07-19）：**按建议删除**。删除当前 MySQL-mode Grammar 无法产生的 `SELECT/FETCH/RETURNING ... BULK COLLECT INTO`、Cursor `LIMIT` 批量取数以及 Collection 聚合写入链；保留普通 `SELECT/FETCH INTO`、Collection 基础设施、Cursor/Handler、客户端批处理和 SQL 执行层批量优化。

#### 枚举抓手

- `src/pl/ob_pl_allocator.h` 中的 `PL_MOD_IDX::OB_PL_BULK_INTO`。
- 从 ENUM-13 的 `ObPLGetCursorAttrInfo::PL_CURSOR_BULK_*` 继续向非 FORALL 的 bulk 状态展开，命中 `ObPLInto::bulk_`、`ObSPIPrepareResult::is_bulk_`、`ObSQLCtx::is_bulk_` 和 SPI `is_bulk` 形参。
- 配套错误码 `OB_ERR_LIMIT_ILLEGAL`、`OB_ERR_BULK_SQL_RESTRICTION`、`OB_ERR_MIX_SINGLE_MULTI`。

#### 服务的产品功能

该链原来服务 Oracle PL/SQL 的批量查询结果装载：

1. `SELECT ... BULK COLLECT INTO collection` 一次把多行结果装入一个或多个 PL Collection；
2. `FETCH cursor BULK COLLECT INTO collection LIMIT n` 分批拉取 Cursor；
3. `DML ... RETURNING ... BULK COLLECT INTO collection` 批量收集 DML 返回值；
4. 对空结果、Collection 初始化/覆盖、Cursor `%NOTFOUND`、record/object 深拷贝和 Package Collection 变更提供专门语义。

这不是客户端 batch，也不是 SQL Engine 的向量化批处理，而是 PL Runtime 面向 Oracle Collection 的服务端多行赋值语义。

#### 当前可达性与实现规模

- 当前 `pl_parser_mysql_mode.y/.l` 没有 `BULK` 或 `COLLECT` token/产生式；`sp_proc_stmt_fetch` 只接受普通 `FETCH ... INTO`，构造固定两个 child，`into_clause` 也只构造普通 `T_INTO_VARIABLES`，从不把 node `value_` 设为 bulk。
- `ObPLResolver::resolve_into()` 只有在 `into_node->value_ == 1` 时才调用 `set_bulk()`；当前 Grammar 无法产生该值。Fetch 的第三个 `LIMIT` child 分支同样没有现役 Grammar 生产者。
- 静态 SQL prepare 在 `src/sql/ob_spi.cpp:2096` 又把 `prepare_result.is_bulk_` 明确固定为 `false`，因此另一处 `static_into.set_bulk()` 也不可达。
- 解释器仍从 `ObPLInto::is_bulk()` 取值并逐层传给 SPI，但当前所有入口恒为 false。
- 后端仍保留完整实现：`ObPLInto` Collection 类型校验、Fetch LIMIT 校验、SPI 空结果 Collection 初始化、逐行 `collect_cells()`、类型转换/深拷贝、Package Collection 地址重取、批量 `store_result()`、Cursor 状态和专用错误处理。
- 聚焦链覆盖 **14 个生产文件、76 处直接命中**；SPI 主体与专用 helper 保守约 500 行。测试与 SQLQA/历史结果另有 **18 个文件、117 处 `BULK COLLECT`**，这些是旧输入/fixture，不构成当前 Grammar 可达证据。
- 已复核现有“seekdb + 审查记录”，未把 `BULK COLLECT` 标记为在途删除；ENUM-13 也明确把它留待独立审查，因此本项不是 FORALL 或 PL Native 任务的重复包装。

#### 为什么建议删除

1. parser、lexer 和 AST 生产端同时缺失，两个可能置 `bulk=true` 的 resolver 入口都无法获得 true；当前不是受开关控制的能力，而是完整后端失去前端生产者。
2. 保留实现不会提供 MySQL 兼容性，却让解释器和 SPI 的静态 SQL、动态 SQL、Cursor 与结果写回接口持续携带 `is_bulk`、`limit`、Collection 转换和错误分支。
3. 恢复功能需要重新建设 Oracle Collection SQL 语法、Parser AST contract、批量 Fetch/DML Returning、Collection 内存生命周期和 Cursor 特殊状态，不是简单补一个关键字。
4. MySQL Stored Routine 没有 `BULK COLLECT`；删除不会造成 MySQL 语法或行为缺口。
5. 独立删除后，ENUM-13 的 FORALL 清理可进一步收敛共用 SPI bulk/forall 组合状态，同时不影响普通多行 SQL 和客户端批执行。

#### MySQL 对应能力

MySQL 没有 Oracle `BULK COLLECT`。MySQL Cursor 的 `FETCH [[NEXT] FROM] cursor_name INTO var_list` 每次取得下一行并写入普通变量；普通查询结果可通过 `SELECT ... INTO var_list` 写入变量，但没有把多行直接填充到 PL Collection、也没有 `FETCH ... LIMIT n` 的服务端 Collection 语义。

- [MySQL 8.4 Cursor FETCH Statement](https://dev.mysql.com/doc/refman/8.4/en/fetch.html)
- [MySQL 8.4 Variables in Stored Programs](https://dev.mysql.com/doc/refman/8.4/en/stored-program-variables.html)

#### 建议删除面

1. 删除 `PL_MOD_IDX::OB_PL_BULK_INTO` 内存标签；bulk 分支消失后不再需要独立 allocator identity。
2. 删除 `ObPLInto::bulk_`、`is_bulk()/set_bulk()`、bulk Collection 类型推导与 `check_into(..., is_bulk)` 维度；普通 INTO 校验保留并简化。
3. 删除 `resolve_into()` 的 node `value_ == 1` 分支、不可达的 Fetch bulk `LIMIT` 处理，以及 `OB_ERR_LIMIT_ILLEGAL`、`OB_ERR_BULK_SQL_RESTRICTION`、`OB_ERR_MIX_SINGLE_MULTI` 中仅服务本功能的部分。
4. 删除 Interpreter、SPI API、Prepare Result、SQL Context 和 Result Set 中的 `is_bulk` 形参与状态传递；`ObPLSqlInfo::bulk_` 随 PL-AUTO-01 的 native shortcut 一并删除，不重复实施。
5. 从 `ObSPIService::get_result()` 删除 bulk 空结果初始化和 `if (is_bulk)` 主分支，删除只由其调用的 `collect_cells()` 与 bulk-Collection `store_result()` overload；保留普通 `store_into_result()`、dynamic SQL `USING OUT` 和 Cursor 单行取数。
6. 删除或改写 `dbms_preprocessor_body.sql`、SQLQA、wrap 输入/输出、历史 `.result` 中已无法由当前 Grammar 运行的 `BULK COLLECT` 语句；不要把 fixture 数量当作保留产品能力的理由。
7. 删除注释、日志、测试预期中的 Oracle Bulk Collect 特殊语义，并收缩相关函数签名。

#### 保留边界

- 保留普通 `SELECT ... INTO`、`FETCH ... INTO`、Cursor `OPEN/CLOSE`、`NOT FOUND` Handler 和单行 `TOO_MANY_ROWS` 检查。
- 保留 PL Collection/Record、Package Variable、object access 与通用 UDT 深拷贝；只删除 SQL 多行批量灌入 Collection 的专用路径。
- 保留普通 DML `RETURNING`、dynamic SQL `USING OUT` 及其共享的结果类型转换。
- 保留 MySQL protocol prepared statement、客户端 array binding、multi-row DML、batched multi-statement 与普通 SQL 多行结果返回。
- 保留 SQL Engine 向量化 batch、Nested Loop bulk join、`OB_MAX_BULK_JOIN_ROWS` 以及递归 CTE `BREADTH_FIRST_BULK`；它们与 PL `BULK COLLECT` 无关。
- ENUM-13 的 FORALL/`SQL%BULK_*` 按独立结论删除；不要因名称重叠重复统计或误删通用数组容器。

#### 兼容风险与验收建议

- 升级前扫描历史 Routine/Package/Trigger 源文本及系统包文件中的 `BULK COLLECT`。若存在，当前版本已无法从同一源码重新 build，应明确报出并要求改写为 Cursor 循环，而不是静默保留不可达 runtime。
- 全库生产代码不再出现 `OB_PL_BULK_INTO`、`ObPLInto::bulk_`、PL/SPI `is_bulk`、`collect_cells()` 或 “BULK COLLECT INTO” 分支。
- 普通 `SELECT/FETCH INTO`、无行/多行异常、Cursor Handler、Trigger/Package Collection、dynamic SQL `USING OUT` 和普通 DML `RETURNING` 回归通过。
- 客户端 batch、多值 DML、vectorized execution、recursive CTE bulk search 和通用 Collection 操作回归通过，证明没有按 `bulk` 关键词误删无关能力。

### ENUM-15：删除 Oracle Collection `MULTISET` 运算与条件残链

> 对齐结论（2026-07-19）：**按建议删除**。删除 Oracle nested-table 的 `MULTISET UNION/INTERSECT/EXCEPT`、Collection Predicate 和 `CAST(MULTISET(SELECT ...))` 纵向残链；保留 MySQL JSON `MEMBER OF()`、普通 Query Set Operation、PL Collection 基础设施及 hybrid-search 内部多子查询容器。

#### 枚举抓手

- `src/sql/parser/ob_item_type.h` 和 Objit 镜像中的 `T_OP_MULTISET`、`T_OP_COLL_PRED`。
- `src/sql/resolver/expr/ob_raw_expr.h` 中的 `ObMultiSetType::{UNION, INTERSECT, EXCEPT, SUBMULTISET, MEMBER_OF, IS_SET, EMPTY}`。
- 同文件中的 `ObMultiSetModifier::{ALL, DISTINCT, NOT}`，以及 `ObMultiSetRawExpr`、`ObCollPredRawExpr` 两类专用 Raw Expression。

#### 服务的产品功能

该链服务 Oracle object-relational nested table 的集合值运算，不是普通查询结果集的集合操作：

1. `nested_table1 MULTISET UNION|INTERSECT|EXCEPT [ALL|DISTINCT] nested_table2`；
2. `element [NOT] MEMBER OF nested_table`；
3. `nested_table1 [NOT] SUBMULTISET OF nested_table2`；
4. `nested_table IS [NOT] A SET` 和 `nested_table IS [NOT] EMPTY`；
5. `CAST(MULTISET(SELECT ...) AS nested_table_type)`。

[Oracle Multiset Operators](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/Multiset-Operators.html) 明确将其定义为 nested table value semantics。

#### 当前可达性与实现规模

- 当前 parser 生成脚本只读取 MySQL-mode SQL Grammar；现行 SQL/PL Grammar 没有产生 `T_OP_MULTISET` 或 `T_OP_COLL_PRED` 的规则。
- 历史提交 `58adac4988a` 删除 Oracle SQL Parser；其父版本的 Oracle Grammar 曾包含 `collection_predicate_expr`、`MULTISET_OP` 和 `MULTISET select_with_parens`，证明当前并非隐藏入口，而是 producer 已随 Oracle Parser 消失。
- 现行 MySQL Grammar 中的 `MEMBER OF` 被构造成 `JSON_MEMBER_OF`，最终使用 `T_FUN_SYS_JSON_MEMBER_OF`，不会生成 Collection Predicate。
- 后端纵向链仍完整保留：Raw Expression Factory、Resolver、Type Deduction、Printer、Rewrite、Code Generator、ExtraInfo、Cast 与 Runtime Operator 都有专用分支。
- 但 `ObExprMultiSet` 和 `ObExprCollPred` 的类型推导、运行时 eval，以及 MULTISET Cast 的 eval/codegen 均直接返回 `OB_NOT_SUPPORTED`；transform 中的 constructor 补全也是空壳。
- 聚焦链覆盖约 **29 个生产文件、218 处命中**；`ob_expr_multiset.{h,cpp}` 与 `ob_expr_coll_pred.{h,cpp}` 四个专用文件合计 **1,036 行**，已经超过零碎常量清理门槛。
- 已复核现有“seekdb + 审查记录”，没有 `MULTISET` 或 Collection Predicate 在途删除项；它也不属于 ENUM-14 的 PL `BULK COLLECT`。

#### 为什么建议删除

1. Oracle Parser 删除后没有 AST 生产者，现行 MySQL Grammar 无法构造这两类表达式。
2. Runtime 关键节点主动返回 `OB_NOT_SUPPORTED`，形成“前端不可达 + 后端拒绝执行”的双重死链。
3. 保留代码不会提供兼容能力，却要求通用 Expression Factory、序列化、Printer、Rewrite 和 Codegen 持续理解永远不会产生的节点类型。
4. 恢复能力需要重建 Oracle nested-table 类型、构造与 CAST、NULL/重复元素比较规则以及完整 Parser/Optimizer/Runtime 测试，不是补回几个关键字。
5. MySQL 没有 Oracle nested-table `MULTISET` 语义，删除不会形成 MySQL 兼容性缺口。

#### MySQL 对应能力

MySQL 没有 Oracle Collection `MULTISET`、`SUBMULTISET`、Collection `IS A SET/IS EMPTY` 或 `CAST(MULTISET(SELECT ...))`。两个名称相近的现役功能必须隔离：

- MySQL JSON `MEMBER OF()` 判断值是否属于 JSON Array，走 `T_FUN_SYS_JSON_MEMBER_OF` 和 `ob_expr_json_member_of.*`；
- `UNION / INTERSECT / EXCEPT` 组合 Query Block 的行结果集，走 `T_SET_UNION / T_SET_INTERSECT / T_SET_EXCEPT` 与 Set Operator。

- [MySQL 8.4 JSON Search Functions](https://dev.mysql.com/doc/refman/8.4/en/json-search-functions.html)
- [MySQL 8.4 Set Operations](https://dev.mysql.com/doc/refman/8.4/en/set-operations.html)

#### 建议删除面

1. 删除 `T_OP_MULTISET`、`T_OP_COLL_PRED` 符号及 Objit 镜像；历史数值 176/177 先保留为空洞或 tombstone，不顺移后续 Item Type。
2. 删除 `ObMultiSetType`、`ObMultiSetModifier`、`ObMultiSetRawExpr`、`ObCollPredRawExpr` 及其 Factory、Assign、Hash、Same-as 分支。
3. 删除四个专用 Runtime Expression 文件及其 CMake、Expression Function Table 和 ExtraInfo 注册。
4. 删除 Resolver、Type Deduction、Printer、Rewrite、Code Generator 中只处理这两类节点的分支。
5. 删除 `ParseNode::is_multiset_`、`ObQueryRefRawExpr::is_multiset_`、MULTISET Cast、`CastMultisetExtraInfo` 及关联的序列化和错误处理。
6. 删除失去被测对象的 Oracle MULTISET SQLQA、Golden 与注释样例，并补充 MySQL 保留边界回归。

#### 保留与迁移边界

- 保留 MySQL JSON `MEMBER OF()` 的全部 Parser、Expression 和测试链。
- 保留普通查询的 `UNION / INTERSECT / EXCEPT [ALL|DISTINCT]`。
- 保留 hybrid-search `ObMultiSetTable`；它是组合多个子查询的内部容器，与 Oracle nested-table expression 无关。
- 保留普通 PL Collection 类型、Collection Method、ARRAY/MAP/VECTOR 与通用 UDT 容器能力；本项只删除 SQL MULTISET 运算和条件。
- `src/sql/engine/expr/ob_expr_in.cpp` 复用了 `ObExprMultiSet::eval_composite_relative_anonymous_block()` 来处理 composite UDT 的 `IN/NOT IN` 比较。先把该通用 helper 迁到 `ObExprIn` 或公共 PL-expression helper，再删除 `ob_expr_multiset.*`。
- Item Type 数值和 Eval Function Table 槽位在完成 Plan/Cache 序列化兼容核查前保留 tombstone；不因删除两个符号重排后续编号。

#### 兼容风险与验收建议

- 升级前扫描存量 View、Routine、Package 与 Trigger 源文本中的 `MULTISET`、`SUBMULTISET`、Collection `MEMBER OF/IS A SET/IS EMPTY`；当前 Grammar 已无法重建的对象应明确报错并要求改写。
- 对 `T_OP_MULTISET|T_OP_COLL_PRED|ObMultiSetType|ObMultiSetModifier|is_multiset_` 做生产代码零引用检查。
- MySQL JSON `MEMBER OF()`、`UNION/INTERSECT/EXCEPT [ALL|DISTINCT]` 和 hybrid-search 多子查询组合回归通过。
- composite UDT `IN/NOT IN` 回归通过，确认 helper 迁移没有改变匿名块复合值比较语义。
- 清理旧 Plan Cache 后验证 Item Type、ExtraInfo 和 Eval Function Table 的反序列化边界；不能用重排编号换取局部代码简化。

## 4. 扫到但不建议成项的枚举族

### 4.1 规模过小，按本轮门槛忽略

| 枚举/功能 | 规模与判断 |
| --- | --- |
| `INDEX_STATUS_UNIQUE_CHECKING` / `UNIQUE_INELIGIBLE` | 仅 1 个文件、3 处引用；属于常量清理，不是功能纵切 |
| `PARTITION_STATUS_LOGICAL_SPLITTING` / `PHYSICAL_SPLITTING` | 仅 2 个文件、6 处引用；当前 partition split 另有真实实现，不能据此删除整体功能 |
| `INDEX_TYPE_DOMAIN_CTXCAT_DEPRECATED` | 6 个文件、9 处引用；可并入索引枚举卫生清理，但不足以独立立项 |
| 旧 backup validation task type | 3 个文件，inner-table 生成器已注明 `abandoned in 4.0`；规模太小 |
| RLS policy/group/context operation id | 聚焦命中 7 个文件，主要是 ID/包声明，未形成足够大的独立 C++ 实现链；暂不成项 |

### 4.2 有规模但明确保留

- `ObDDLSimPointID`/`DDL_SIM`：约 179 个调用点、30 个文件，是 ERRSIM 下的 DDL 故障注入设施。既有审查已明确保留 ERRSIM、Debug Sync、SET_TP 等测试基础设施，本轮不把它包装成业务裁剪项。
- partition split/merge 相关 enum：尽管含旧状态值，但当前有 resolver、DDL task、tablet split helper 等真实执行链，不能从几个 deprecated state 推导出整体可删。
- local PX/PDML/DTL/DAS、物理备库、本地物理全量备份恢复、MV/MLog、Vector/FTS/GIS/JSON、Recycle Bin、LOB、DBMS_JOB/SCHEDULER 等均按既有产品决策保留。

## 5. 建议实施顺序

PL 编译执行残留走一条独立实施线，不等待后续产品候选对齐：先删 `PL-AUTO-01/02/03/07` 的无消费者骨架与语法标记，再完成 `PL-AUTO-04` 的 external-record 副作用迁移和 object-access 回归，随后删除 `PL-AUTO-05` Windows unwind/SEH，最后用 `PL-AUTO-06` 收口监控 schema、测试 golden 与术语。涉及 PL Debugger 等既有在途项的代码，直接补充到原任务。

1. **ENUM-02 DIRECTORY**：入口不可达且 service 固定拒绝，删除证据最闭环，收益也最大。
2. **ENUM-05 Schema 级 UDT/Object Type**：先把 Package 内部 Collection 和 ARRAY/MAP/VECTOR 的通用运行时标清，再删除 Object Type 方法与 Schema 生命周期。
3. **ENUM-06 Oracle 高级 Trigger 子类型**：先确认无存量高级 trigger type，再收缩到普通 MySQL 行级 Trigger。
4. **ENUM-09 Oracle PL 自治事务**：先把表锁等内部事务切换消费者迁移到泛化接口，再删除 PL Flag、重试/DAS/DML 豁免和专用 Deadlock 路径。
5. **ENUM-10 Oracle Interval Partition**：先扫描存量 schema 并保留持久化枚举 tombstone，再删除语法残留、自动补分区 RPC、DML retry 和专属元数据字段；严格隔离 Auto Split/Dynamic Partition。
6. **ENUM-11 Oracle PL NOCOPY**：先扫描 `__all_routine_param.flag` 的历史 `0x100` 位，再删除别名索引、运行时传递、提前回写和专属异常清理；严格保留普通 `OUT/INOUT`。
7. **ENUM-12 PL Profiler / DBMS_PROFILER**：先扫描历史系统包对象，随后删除 `PROFILE` Cache Key、死 metadata、空 Session/SPI API 和未装载包文件；严格保留普通 PL Cache、耗时统计及 MySQL `SHOW PROFILE` 兼容空壳。
8. **ENUM-13 Oracle PL FORALL**：先扫描存量 Routine/Package/Trigger 源文本，再删除不可达的 statement/item type、implicit cursor bulk 状态和 SPI `is_forall` 维度；严格保留普通 batch、array parameter、multi-row DML 和 batched multi-statement。
9. **ENUM-14 Oracle PL BULK COLLECT**：在 FORALL 的 `is_forall` 维度清理后，删除恒为 false 的 `is_bulk`、Collection 多行聚合和 Fetch LIMIT 链；严格保留普通 INTO、Collection 基础设施及所有 SQL Engine/客户端批处理。
10. **ENUM-15 Oracle Collection MULTISET**：先迁移 `ObExprIn` 复用的 composite comparison helper，并为 Item Type/Eval Function Table 保留兼容 tombstone；再删除无生产者且 Runtime 拒绝的 Raw Expr、Cast、Printer、Codegen 与执行链，严格隔离 JSON `MEMBER OF()` 和普通 Query Set Operation。
11. **ENUM-01 垂直分区**：源码已有明确 remove 注释；先检查存量 table type 11，再拆后端残留。
12. **ENUM-03 Synonym**：先迁移调用方接口并保持 plan cache/table dependency 回归，再删枚举和权限残留。
13. **ENUM-08 Oracle DML Error Logging**：入口不可达且后端完整驻留，删除 Stmt/Optimizer/Codegen/Runtime 链，同时隔离并保留 `LOAD DATA` 诊断。
14. **ENUM-04 HASH Index 不实施删除**：按本轮对齐结论保留，不创建裁剪任务。
15. **ENUM-07 Mock FK Parent Table 不实施删除**：按本轮对齐结论保留 MySQL `foreign_key_checks=OFF` 的完整兼容链，不创建裁剪任务。

每项应独立成删除任务和提交序列，避免把 schema 枚举收缩、持久化兼容和不相关功能裁剪揉在一起。

## 6. 覆盖闭环

本轮使用以下层次闭环枚举扫描，而不是只搜索枚举声明：

1. schema/object/table/index/operation 等中央枚举；
2. parser item、stmt type、RPC command、inner-table operation 等接口枚举；
3. storage/DDL/DAG/task/status 等执行状态枚举；
4. optimizer/operator/expression/type 等 SQL 执行枚举；
5. observer/virtual table/diagnose/test-only 等运维枚举；
6. 对命中的大功能链回查 5 份已有 Yuque 审查记录，命中在途结论则排除。

当前结论不是“只有几个废枚举”，而是：在大量枚举值属于当前有效能力或已被既有文档覆盖之后，已经识别出 15 条需要产品判断的中等以上功能链；逐条对齐后，**13 条进入删除清单，2 条明确保留**。此外，PL 解释执行迁移后确认的 **7 组纯编译执行残留已获授权自动删除**，不计入产品候选数量，也不再逐条讨论。后续新增产品项继续按同一口径对齐；后续发现的纯 native/JIT 残留则直接追加到 `PL-AUTO` 清单。

## 7. 2026-07-20 补充对齐项

本节记录初始 ENUM-01～15 闭环之后发现的中大型残链，不回填或重排前述稳定编号。

### 补充-01：删除 `ALTER SYSTEM {LOAD|CHECK} MODULE DATA` 残链

> 对齐结论（2026-07-20）：**按建议删除**。删除私有 Module Data 命令参数、三个死 Importer 和 raw data 发布物；保留普通 `LOAD DATA`、SRS SQL 种子、时区 SQL 清单、cache/manager 与 notifier。

#### 枚举抓手与历史

- `T_MODULE_DATA = 4738`、`T_MODULE_NAME = 4739`、Stmt Type 366。
- `ObModuleDataArg::ObInfoOpType::{LOAD_INFO,CHECK_INFO}`。
- `ObModuleDataArg::ObExecModule::{REDIS,TIMEZONE,GIS}`。
- 逻辑删除提交为 `a17ac0a5269e5cb24fb805ab3f585326cf7358f4`，明确删除 Grammar、Resolver、Stmt、Cmd Executor 分发与 `ObModuleDataExecutor`；当前分支由 squash 提交 `9c6764cc8704a8a022fea2d878458bdc9f7bd8e0` 带入。

当前 MySQL Grammar 已不能生成 `T_MODULE_DATA`。因此 `ALTER SYSTEM LOAD MODULE DATA MODULE=GIS` 会在 Parser 阶段失败，不是进入 Executor 后返回不支持。残余的 `ObRedisImporter`、`ObTimezoneImporter`、`ObSRSImporter` 没有调用方，其中 Redis 固定返回 `OB_NOT_SUPPORTED`，SRS/时区 Importer 仍依赖已过期的 raw data 与多租户字段布局。

#### 保留的真实导入流程

SRS 有两种等价入口：直接执行 `default_srs_data_mysql.sql`，或运行 `import_srs_data.py` 读取并逐条执行同一 SQL 文件。Python 和 SQL 不是前后两次导入；使用脚本时是“先启动 Python，随后由 Python 执行 SQL”，直接 source SQL 时不经过 Python。系统初始化先写 SRID 0，种子 SQL 再写 5151 条，合计 5152 条；首次 GIS 访问时懒加载 cache。

时区由 `import_time_zone_info.py` 读取 `timezone_V1.log`，将 MySQL `time_zone*` DML 改写到 seekdb 四张 `__all_time_zone*` 内表，校验 1750/1750/117043/8593 条记录后更新 `current_timezone_version`；Timezone Manager 每 5 秒检查版本并刷新。两个 Python 工具只随包发布，不由安装或启动流程自动调用。

实施时同步修复了两个工具遗留的 `__all_tenant`/`CHANGE TENANT` 流程；时区工具还删除了 SQL 中旧 `tenant_id/zone` 列并改写到当前内表名。旧 `spatial_reference_systems.data` 和四个 `timezone*.data` 只服务死 C++ Importer，随之删除。

#### 删除与保留边界

- 删除三个 Importer、`ObModuleDataArg`、CMake 注册、parser/stmt/privilege 残片和执行旧命令的测试；4738、4739、Stmt Type 366 保留 tombstone，不重排。
- 将 `ObSRSImporter::get_srs_cnt()` 下沉为 `ObTenantSrs::get_srs_count()`。
- notifier 使用自身的 `TIMEZONE/GIS` 小枚举，不再依赖死命令参数；保留 role-change refresh、SRS cache 和 Timezone Manager。
- SRS Python/SQL、时区 Python/log、普通 `LOAD DATA` 与全部 GIS/时区运行时保留。
- 普通 SRS SQL 导入不会主动使已加载 cache 失效，应在首次 GIS 使用前导入；这是保留流程的已知运维边界。

MySQL 没有统一的 `ALTER SYSTEM ... MODULE DATA`：时区通常由 `mysql_tzinfo_to_sql` 生成 SQL 后导入 `mysql.time_zone*`，SRS 使用 `CREATE/DROP SPATIAL REFERENCE SYSTEM` DDL。删除该私有命令不形成 MySQL 兼容缺口。

### 补充-02：删除 Oracle REF CURSOR / OPEN FOR / Cursor Expression

> 对齐结论（2026-07-20）：**按建议删除**。删除 Oracle 游标句柄值语义和 cursor-valued subquery；保留普通 MySQL Stored Program Cursor、过程结果集及 PS/server cursor。

#### 枚举抓手与功能

- `PL_REF_CURSOR_TYPE`、`PL_REF_CURSOR_1`、`PL_TYPE_SYS_REFCURSOR`。
- `T_SP_REF_CURSOR_TYPE = 3923`、`ObPLStmtType::PL_OPEN_FOR`。
- `SP_EXTERN_SYS_REFCURSOR` 及 Query Ref 的 `is_cursor_` 状态传播。
- `ObRefCursorType` 的参数/返回值、Session 所有权、引用计数、deep-copy 与跨层 transfer 逻辑。

该链服务 Oracle `REF CURSOR`/`SYS_REFCURSOR`：把 Cursor Handle 作为过程参数、返回值或 Package Variable 传递，通过 `OPEN cursor_var FOR query` 打开，并通过 `CURSOR(subquery)` 构造 cursor-valued expression。它不同于普通局部 Cursor 的 `DECLARE/OPEN/FETCH/CLOSE`。

REF CURSOR 主体由历史提交 `2a3fed53ef5` 引入，Cursor Expression 由 `98595bb9f494` 引入。Oracle PL producer 已由 `7978d94b54c` 删除，Oracle SQL `CURSOR(subquery)` producer 已由 `d3e5bd209493` 删除；当前 MySQL Grammar 只生产普通 Cursor，解释器也只处理 `PL_CURSOR/PL_OPEN/PL_FETCH/PL_CLOSE`，没有 `PL_OPEN_FOR` 可达链。

#### 删除与保留边界

- 删除 REF CURSOR 类型身份、参数别名/返回/session-transfer/refcount、`OPEN FOR`、cursor-valued subquery 及 schema/protocol `SYS_REFCURSOR` 分类。
- `T_SP_REF_CURSOR_TYPE` 数值 3923 和持久枚举槽位保留 tombstone；进程内 `PL_OPEN_FOR` 可直接删除。
- 原 `ObRefCursorType` 中仍服务普通 Cursor 的生命周期实现迁移为普通 `ObPLCursorType`，不能整体删除。
- 保留普通 MySQL PL Cursor、`ObPLCursorInfo` 的 open/fetch/close、普通 OUT/INOUT、过程结果集、prepared statement/server cursor 和 scalar subquery。

MySQL 有局部、只读、不可滚动的 Stored Program Cursor，但没有 `SYS_REFCURSOR`、可作为参数/返回值传递的 Cursor Handle、`OPEN ... FOR` 或 `CURSOR(subquery)`，删除不形成 MySQL 兼容缺口。
