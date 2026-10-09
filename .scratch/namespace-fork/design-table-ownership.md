# 设计：实例级表与 Namespace 级表的归属

> 2026-09-29：本文的独立精简实例 SQL 上下文方案因维护成本已撤回。随后新增完整内部 Runtime 的方案也因资源成本被拒绝。当前约束与候选方案见 [复用现有执行环境](design-table-ownership-existing-runtime.md)。下文保留供追溯，不作为实施依据。

状态：设计稿，尚未实现。2026-09-28。本文和测试留在本地 `.scratch`，不提交代码分支。

## 1. 决策摘要

为表定义明确的 INSTANCE / NAMESPACE 归属。归属同时决定 schema 定义的来源、数据地址、生命周期和 fork 行为；可见性单独定义。

本次范围是内表归属和普通 Namespace 表的统一访问。实例级表采用代码生成的固定内表定义，复用现有 schema 类型和内表生成器。普通用户表继续由各 Namespace 的 DDL 和 `__all_*` 管理。本次不提供用户动态创建实例级表或 ALTER TABLE 切换归属的语法。

实例级内表的完整定义由一个只读 `InstanceTableRegistry` 模块持有，数据仍可使用 seekdb 唯一 LS 中的普通 tablet。定义不写入任何 Namespace 的 `__all_*`，包括 Namespace 1。实例 tablet 使用与 Namespace tablet 不相交的物理地址域，因此父链探测也不会把实例 tablet 当作父空间数据。

内部 SQL 的执行目标采用有类型的 INSTANCE / NAMESPACE 上下文。每个 Namespace 的 proxy 固定绑定自己的上下文；实例模块持有固定绑定实例上下文的内部 proxy。实例操作不借用 Namespace 1 的 proxy，也不创建伪 Namespace。共享 SQL 执行引擎和物理事务实现。

## 2. 已核实的现状

以下是现有实现，不是目标设计：

- `src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp::ensure_control_schema()` 用普通 CREATE DATABASE / CREATE TABLE 建立 `__fork_proto_meta`，定义进入 Namespace 1 的原生 schema 表。
- 同文件中的 `directory_schema_service()` 和 `directory_sql_proxy()` 只是取 Namespace 1 的服务；没有独立的实例目录实现。
- fork 保存父链和 snapshot cap，没有逐表登记或排除。子空间的原生 `__all_*` tablet 也按父链继承。
- 子空间全量 schema 装载在 `src/share/schema/ob_server_schema_service.cpp` 中按 `__fork_proto_meta` 名字前缀从临时数组移除库、表。原生表记录没有删除。
- 本地实测：根和子空间的 `__all_database` 均出现 database_id 500002；`__all_table` 均出现 pages、roots、namespaces、exceptions、snapshots 以及 name 唯一索引。子空间 SHOW DATABASES 隐藏该库，直接访问表被拒绝。证据：`/tmp/seekdb-ns-control-fork-probe.log`、`/tmp/seekdb-ns-control-table-schema-probe.log`。
- `StorageSpaceHandle::tablet_namespace_id()` 把 GLOBAL 转为 1；物理地址因此仍落在 Namespace 1 的地址域。
- `NamespaceObjectKey` 使用 MARK、25 位空间字段和 37 位 local_id 编码 tablet。schema/database/table ID 不编码 Namespace。`owner_namespace()` 还把未编码 ID 推断为 1，不能把该函数用于新增的实例地址域。
- 已有 `NamespaceCatalogTree` 是 fork 的树与快照结构，不承担这里所需的统一内表归属定义。
- 内表生成器已有固定 schema、索引/LOB 辅助表、虚拟表和 SQLite 表定义。`construct_inner_table_schemas()` 当前未按实例/Namespace 归属组织输出。

## 3. 必须保持的规则

1. Namespace 1 与其他 Namespace 的普通请求走相同的 schema、缓存、DDL 和数据访问规则。
2. table_id、database_id 等 schema ID 保持逻辑 ID；所属目标通过独立的上下文表达。
3. 存储引擎接收物理 tablet ID、读快照和事务，不读取 NamespaceRegistry、不判断表名或表归属。
4. 实例表既不复制数据到子空间，也不作为父链继承对象；子空间 fork 的时间不冻结实例表数据。
5. 每次管理/后台操作在入口携带访问目标和能力。缺失目标报错，不回退到 Namespace 1。
6. schema 来源、物理地址和缓存身份由同一份绑定结果产生，调用方不能分别猜测三者。
7. 本版本按新实例启动设计，不加入旧磁盘格式识别、在线迁移或升级兼容分支。

## 4. 表定义：归属与可见性独立

概念接口如下，具体命名在实现中按现有 schema 类型收敛：

```cpp
enum class TableScope { NAMESPACE, INSTANCE };
enum class TableExposure { INTERNAL_ONLY, SQL_VISIBLE };

struct BuiltinTableDefinition {
  TableScope scope;
  TableExposure exposure;
  // 既有 schema creator，以及主表、索引、LOB 等依赖关系。
};
```

- 普通用户 DDL 创建的表归 NAMESPACE。
- NAMESPACE 表沿用当前 fork 语义；个别任务或运行状态若需要在 fork 后重新初始化，应在该模块的 fork 生命周期设计中明确，不能为了避免复制而误分类为 INSTANCE。
- 内表生成器要求每个持久化主表显式声明 scope，遗漏时生成失败。索引和 LOB 从主表派生归属，禁止跨归属依赖。
- SQL_VISIBLE 不表示允许写入；读写权限仍按既有权限模型检查。
- INSTANCE / INTERNAL_ONLY 的例子是 `__fork_proto_meta` 下的管理表。
- 一个数据库名下可有不同归属的内表，不能用 database_id、库名或 `is_inner_table()` 一刀切。数据库名在此也是名字解析所需的容器，不能替代表归属。
- Namespace schema 记录中的对象关系和索引/列/history 记录必须完整地随对象归属，不能只处理 `__all_table` 一张表。
- 已由 SQLite、虚拟表等承载的实例状态仍复用现有后端；scope 不能强制这些表转成 LS tablet。不同后端的事务能力也不能混同。

归属的判定依据是数据含义和生命周期：按逻辑 schema 对象、会话或 Namespace 任务驱动的状态通常归 Namespace；描述实例成员、共享物理 tablet、物理快照/回收的状态通常归实例。当前只有一份数据、使用某个全局 proxy 或名字里有 global，都不足以证明归属。

## 5. schema 怎么保存

### 5.1 Namespace schema

各 Namespace 保留现有 SchemaService 和 `__all_database`、`__all_table`、列、索引、history 等原生持久化记录。bootstrap 只把 NAMESPACE 内表定义及相关记录写入这套表。后续 DDL 仍走原有事务与版本发布机制。

子空间依旧继承这套 `__all_*` 的快照；其中从源头就没有 INSTANCE 内表的记录，故无需在 fork、全量刷新或增量刷新后删除这些记录。

### 5.2 Instance schema

`InstanceTableRegistry` 保存由代码生成的完整、只读 schema：库名容器、主表、列、索引、LOB 和物理 tablet 绑定。复用 `ObTableSchema`、schema manager/guard 的读取能力，不另写一套 SQL schema 类型。

实例内表版本固定于当前二进制的内表定义。启动时构造，正常运行不执行用户 DDL，不从某个 Namespace 的 `__all_table_history` 延迟补取定义。

当前 `ObSchemaGetterGuard` 依赖 `ObMultiVersionSchemaService`。实现需要提供固定 schema 快照的装载入口：完整定义一次性装载并 pin 住，使通用查询接口读取已绑定的快照。不能在每个 get_database/get_table/get_column 方法里添加 ns==1 或表名判断，也不能把 registry miss 回退到根 SchemaService。该适配是本方案的实际工程成本，不能当作只加一个 enum。

Registry 是实例内表的只读定义和绑定模块；不重新引入全局 `GCTX.schema_service_`，也不保存任何 Namespace 用户 schema 的副本。

### 5.3 可见性与元数据查询的明确语义

- 默认的 Namespace SQL schema 包含 Namespace 对象，以及生成器明确声明为 SQL_VISIBLE 的实例内表定义。后者是对只读 registry 的绑定，不写成本地 schema 记录，也不参与继承。
- 两种来源在构造读取快照时按声明建立绑定，禁止“先查本地，没找到就查实例”的回退。实例公开名使用保留的系统名字和 ID，禁止被用户对象遮蔽。
- INTERNAL_ONLY 实例对象不发布到任何普通 Namespace SQL schema，包括 Namespace 1。管理操作使用显式实例 schema 上下文访问。
- SHOW / INFORMATION_SCHEMA 展示当前 SQL schema 里允许访问的对象。公开实例对象的列、索引等信息来自同一 registry。
- 直接查 `__all_database` / `__all_table` 的含义是读取当前 Namespace 持久化的 schema 记录，INSTANCE 内表在任何 Namespace 的这些表里都没有记录。管理端若需枚举实例定义，使用 registry 的显式枚举入口。

这里明确区分“对象归谁保存”和“SQL 可以访问谁”。公开实例内表属于显式共享定义，不能被误计入 Namespace DDL 枚举和 fork 的对象集合。

## 6. 数据如何避免进入父链

仅分开 schema 还不够。现有 GLOBAL→Namespace 1 的地址转换必须同时移除。

采用统一的物理 ID 格式：沿用当前 MARK / 25 位地址域 / 37 位 local_id 布局，在上层物理地址编解码模块中将地址域 0 保留给实例，正数域用于现有 Namespace ID。

```text
实例 tablet       = encode(space=0, local_tablet_id)
Namespace 1 tablet = encode(space=1, local_tablet_id)
Namespace N tablet = encode(space=N, local_tablet_id)
```

0 是物理地址域，不创建 Namespace 0 或 NamespaceRuntime 0。类型接口分别提供 instance() 和 namespace_space(ns)，后者仍拒绝 ns=0；不能让整数 0 表示缺失上下文。

这是对现有物理地址含义的明确扩展。Namespace tablet 的现有正数域编码不变，schema ID 不变。编解码返回带 INSTANCE / NAMESPACE 类型的 owner，删除“无法识别就属于 Namespace 1”的回退。原有 `is_encoded()` 只能说明物理编码格式，不能继续当作“这是 Namespace tablet”的判断。

引擎保留的特殊物理 tablet 仍可通过明确的物理接口访问；它们不是缺少编码的 Namespace 1 表。需要反查 schema 时，必须有明确的系统对象绑定或返回不适用，不能用根空间补全未知 owner。

读写路由绑定在 observer/Namespace 适配层完成：

- NAMESPACE 表：按目标 Namespace 查物化状态、父链和 cap；写入时按现有 COW 规则物化。
- INSTANCE 表：直接得到实例物理 tablet；没有父链和 Namespace fork cap。读可见性遵守其 SQL 事务快照，不保证无条件读到最新提交。
- 所有 LS tablet 最终仍由同一个 LS、事务引擎、日志和存储接口处理。

Namespace 父链只探测正数 Namespace 地址域，因此不可能落入实例域。这条规则不需要知道控制库名称，也不需要维护“不能 fork 的 tablet 黑名单”。

物理 tablet→schema 的后台查询同样解析带类型的 owner：实例域查注入的 registry，Namespace 域查该 Runtime。Namespace DROP/GC 只枚举该 Namespace 的对象；实例物理表由实例生命周期管理，不能被 Namespace GC 删除。

## 7. 请求、缓存、事务和锁

### 7.1 绑定结果

名字/ID 解析后形成不可变的表绑定，至少包含：逻辑 schema ID、schema 版本、归属、对应 schema 快照、物理地址解析方式。scan、DML、索引/LOB 访问、锁和 MDS 都消费这一绑定，不再独立从线程或数据库名重新决定归属。

普通会话只携带所属 Namespace 和明确发布的可见定义。实例管理代码从启动组合处得到实例 SQL client，其上下文已绑定 registry、实例数据访问和所需执行服务。客户端登录 Namespace 1 获得普通 Namespace 1 上下文，不能取得实例内部 SQL 上下文。

### 7.2 内部 SQL 和缓存

内部调用必须显式区分 Namespace 目标与实例目标，实例访问上下文由受控内部接口产生。连接池复用、reset、嵌套 SQL、后台任务和重启恢复均不能丢失或残留该上下文。

具体改动入口：`ObCommonSqlProxy::namespace_id_`、`ObISQLClient::target_namespace()`、`create_inner_sql_connection_for_proxy()`、`ObInnerSQLConnection::target_namespace_` 目前只表达 Namespace ID，统一改为有类型的 `SqlTarget`，区分 INVALID、INSTANCE 和 NAMESPACE(id)。代理创建后目标固定；不能把无目标的数值 0 当成实例目标。

连接创建时绑定完整的 SQL 执行上下文。现有 `ObSQLSessionInfo::effective_schema_service()` 等方法直接从 `ns_runtime_` 取依赖，需要改为从已绑定的 SQL 执行服务中读取；Namespace 上下文由自己的 Runtime 提供这些服务，实例上下文由实例 SQL 模块提供。分派集中在上下文装配处，算子不反复判断 Namespace ID。Namespace 身份仍可用于普通会话授权和管理，但不作为所有 SQL 执行依赖的唯一容器。

实例 SQL 模块由启动组合处持有和注入，包含固定 schema 读取、有容量上限的计划缓存及实例表读写所需的锁/事务适配。PS 缓存按实际 prepared-statement 路径需求启用，不能无条件创建。复用既有实现，仅装配内部表 DQL/DML 必需能力；不复制 Namespace 的 DDL、向量、统计采集和调度服务组。优化器所需的统计读取能力仍须通过依赖审计提供，不能直接删掉依赖或用空服务伪装成功。它没有 get_instance() 或 GCTX 默认入口，普通 Namespace SQL 找不到对象时也不能改用它。

复用同一个 SQL 引擎时，实例 SQL 缓存由实例上下文持有，Namespace SQL 缓存继续各自持有；共享的底层缓存身份包含 schema 目标和 schema 版本。实例只读 schema 快照不进入某个 Namespace 的动态 schema history/cache。已有 `ObSchemaCacheKey::cache_scope_` 可作为实现入口；计划缓存/PS cache 也需隔离目标。

这是 schema 读取和物理地址选择的两个真实实现，分派集中在访问绑定模块；不会变成每个 SQL 算子里的表名/ns-ID 特判。

### 7.3 事务

fork 记录、snapshot pin、引用计数等必须保留当前要求的原子性。LS 承载的实例表能在同一个物理事务中读写，不能因为换 proxy 而拆成独立提交。

DDL 若同时更新 Namespace schema 和实例元数据，应显式向两个访问入口传递同一事务描述符，通过受控的事务附着接口参与同一提交；各 proxy 自身的目标不变，连接不借切换 Namespace 身份跨目标。需覆盖事务附着的引用寿命、错误回滚和锁释放。两阶段发布/恢复标记若本来跨事务，仍需逐点保持并测试原有崩溃恢复协议。

SQLite 等其他后端不具备同一个 LS 事务的原子提交能力；需要共同提交的 fork 管理状态继续放在 LS 表中。

### 7.4 ID 分配与锁

- `__fork_proto_meta.namespaces` 当前的 AUTO_INCREMENT 不能继续借 Namespace 1 的逻辑表自增状态。改为实例控制计数器事务性分配 Namespace ID，状态只保存一份；计数器由同一个实例内表模块持有。回滚和重启不能让已发布 ID 被重用。
- 计数器采用固定实例内表的一行持久化高水位，管理事务加行锁、递增并显式写入 namespace_id；首次 bootstrap 从保留的初始 ID 之后分配。分配与 Namespace 记录发布同事务，已提交的 ID 永不回收。该表也由静态 schema 定义创建，不依赖 Namespace 自增服务完成自身 bootstrap。
- 其他实例表如需自增，分配状态同样归实例，并使用显式实例绑定。
- 表级锁的物理键由绑定中的 owner 和逻辑资源 ID 在上层生成；实例表在所有访问入口得到同一个锁键。tablet 锁使用最终物理 tablet ID。`ObLockTable` 继续只处理物理锁键。
- 本设计没有解决先前保留的 fork 整表锁与 DML 冲突覆盖问题，不把新增归属机制当作该问题的修复。

## 8. bootstrap 与恢复

1. 构造代码生成的两组内表定义、实例 registry 和显式访问能力；实例定义无需读 `__all_*` 或 Namespace 父链。
2. 按现有存储 bootstrap 阶段创建/恢复实例 tablet 和 Namespace 1 的原生元数据 tablet。实例 tablet 的初始创建走物理 bootstrap 路径，不能先经过依赖 Namespace 注册表的普通 DDL。
3. 构造实例内部 SQL 上下文与固定目标的 proxy；它不依赖 NamespaceRuntime。NamespaceRegistry 的内存容器可先建立，持久化成员记录在实例表就绪后初始化。
4. 通过实例 SQL 入口初始化 Namespace 注册/计数器，再构造 Namespace 1 的正常 Runtime、proxy；初始化 template 和其他 Namespace。
5. 启动后台消费者之前，实例 registry/tablet 与对应 Namespace Runtime 必须已就绪。实例控制模块的引用寿命覆盖后台消费者，并在停调度、排空之后销毁。

重启时按静态定义恢复实例绑定，从实例管理表恢复 Namespace 成员和父链，然后构造各自 Runtime。Namespace 1 不再通过 CREATE DATABASE 建控制库。实例元数据读取不能依赖尚未恢复的 Namespace 父链，否则重新形成循环。

## 9. 第一批表如何分类

| 表/数据 | 设计归属 | 依据与处理 |
| --- | --- | --- |
| `__fork_proto_meta` 的 namespaces、pages、roots、snapshots、exceptions 及索引 | INSTANCE / INTERNAL_ONLY | 管理整个实例的 Namespace 成员、父链、快照和引用；转为固定内表定义 |
| `__all_database`、`__all_table`、列、schema history 等 Namespace 原生元数据 | NAMESPACE | fork 后允许独立 DDL；存储内容仅包含 Namespace 持有的定义 |
| 普通用户表及其索引、LOB、全文/向量辅助表 | NAMESPACE | 随所属逻辑对象继承及修改 |
| DBMS_SCHEDULER job/program、逻辑表统计、Namespace 锁 owner 清理记录 | NAMESPACE | 已按所属 Namespace 的服务驱动；不能因是内表就改成实例级 |
| `__all_acquired_snapshot` 等物理快照保留状态 | INSTANCE 候选 | 要逐条核对写入、GC 和 pin 的事务依赖，禁止只替换 proxy |
| `__all_global_stat` | 需要拆分审计 | 当前同一接口初始化 schema 版本、snapshot_gc_scn、change stream 位置等字段，不能按 global 名称整体归类 |
| 向量任务表、change stream 位点表 | 待完成归属审计 | 当前集中保存不代表必须实例级；结合已记录的 Namespace 调度器设计判断任务和消费位置语义 |
| 物理 tablet 元信息、校验/实例参数等 SQLite/虚拟表 | 按数据含义审计 | 后端不变，校验逻辑 ID 字段是否需要 owner；不能仅凭物理存储后端定归属 |

若一张表混合两种生命周期的数据，应拆表或拆记录存储，使每个持久化表只有一种归属。INSTANCE 表可以有 namespace_id 列（如实例的 Namespace 注册表）；这不自动意味着它应该按 Namespace 拆分。

内表审计产物逐表记录：数据含义、scope、可见性、写入者/读取者、事务依赖、索引/LOB、fork 行为、DROP/GC 和恢复规则。生成器在审计补齐前不能给未分类的内表默认为 INSTANCE。

## 10. 改造顺序与删除项

### A. 固定内表归属和物理地址类型

扩展内表生成器的 scope/exposure 定义与校验；将实例定义、Namespace 定义分别生成；补齐索引/LOB 继承。实现有类型的物理 owner，核对编码校验、反解 owner、路由和后台查 schema 的所有调用者。生产路径尚未切换时，不声明问题已修复。

### B. 打通完整的一条实例内表路径

接通只读 schema 快照、显式内部 SQL 目标、地址路由、事务、锁和缓存上下文；先用一组主表/索引/LOB 的实例测试定义验证。完成实例 bootstrap 和重启恢复，避免在控制表上边改边引入启动循环。

### C. 切换 fork 控制表

删除 `ensure_control_schema()` 中普通 CREATE DATABASE / CREATE TABLE 的建表方式；用内表 bootstrap 建定义和 tablet，控制 SQL 使用显式实例目标。名称可以保留以减少管理 SQL 改动；表归属从生成定义获取，与名称无关。切换 Namespace ID 分配、snapshot pin、引用计数、parent/exception 查找及 GC。

完整切换后删除：

- schema 装载时按控制库名剔除库/表的循环；
- SHOW/latest guard/scan/DDL 中为掩盖继承结果而加的控制库名字判断；
- GLOBAL 地址转换成 Namespace 1 的行为；
- 未知物理 owner 回退 Namespace 1 的行为；
- 用根服务权限决定普通表归属、以及管理访问缺少 owner 时的回退。

权限校验改用实例访问能力与内表声明。管理命令和 bootstrap 访问实例表时使用注入的实例 SQL client，访问 Namespace 对象时使用明确的 Namespace client；普通请求不按 Namespace ID 区别处理。

### D. 批量内表审计及切换

逐表或按同一事务依赖组切换其他已确定的实例表。混合状态表先拆分。不能因控制库通过就宣称所有内表归属完成。

## 11. 验证计划（纳入本地四件套）

以下是实施后的验收用例，本轮只设计，尚未执行：

1. 新实例：Namespace 1 与两个子空间的 `__all_database`、`__all_table`、列、索引和 history 都不包含 INTERNAL_ONLY 实例表的持久化 schema 记录；普通 SHOW/名字解析结果一致。管理上下文可以解析并访问完整定义。
2. 父子再 fork 孙空间：控制表无继承、无物化；改实例状态后各授权入口按事务隔离规则看到共享状态，Namespace 的 fork cap 不限制该读取。
3. 在实例域和三个 Namespace 域构造相同 local tablet ID，确认物理地址不同。各 Namespace 同 table_id 用户表的 DDL/DML 保持隔离。
4. 将子空间地址传给父链解析，只能查到 Namespace 对象或返回不存在；不会误中实例 tablet。实例 tablet 读取不会递归查询 Namespace 父链。
5. 实例主表、唯一索引、LOB 的读写、回滚及重启恢复；全部落在实例域，锁能跨管理入口正确冲突。
6. 通过 Namespace 1、两个子空间和实例内部 proxy 执行 SQL，验证连接创建/复用、plan/PS cache 和 reset 不串目标、不残留管理能力；实例 SQL 在没有绑定 NamespaceRuntime 时仍能正常执行，缺失/错误的 SqlTarget 可观察地失败。显式附着到同一事务的 Namespace/实例操作共同提交或回滚。
7. 一张 SQL_VISIBLE 实例表：各 Namespace 的 SHOW/INFORMATION_SCHEMA 列和索引结果一致，访问同一物理数据；不出现在各自持久化 `__all_*` 中，不被 DDL/fork/DROP 枚举为本地所有对象。
8. fork/pin/计数器/引用计数在提交前后故障重启，验证无半发布 Namespace、无引用丢失、无已发布 ID 重用。
9. 删除源 Namespace、重启并访问后代；实例表仍在，父链 pin 和 Namespace GC 正常，关闭过程无悬空引用。
10. 正常 Namespace 索引、LOB、全文/向量与 DDL 定向回归，确保新的所有权绑定不会改变已有继承/COW 行为。覆盖此前失败的控制库元数据暴露用例。
11. 生成器拒绝缺 scope、主辅表跨归属、公开实例名字冲突；新增普通内表不会意外采用 Namespace 1 的数据地址。

完整 mysqltest 和 sysbench 不作为本次门禁。按用户约定，测试和非代码文档均留在本地四件套与 `.scratch`。

## 12. 代价和边界

这次不是小范围过滤修补。主要成本是固定实例 schema 快照接入 SQL guard、访问目标在内部 SQL/缓存/事务中传播、物理 owner 类型扩展以及 bootstrap 顺序。

实例固定定义占一份内存；公开定义可以被多个 Namespace 的读取快照引用，不复制动态缓存。Namespace 服务不因这个需求重新合并为全局单例。

### 12.1 上下文内容与资源所有权

上下文是目标身份和执行依赖的引用集合，不是另一套 `NamespaceRuntime::ServiceSlot`。分成长寿命的绑定与每次执行的状态：

| 内容 | 保存方式与所有权 |
| --- | --- |
| INSTANCE 或 NAMESPACE(id)、访问能力 | 长寿命绑定中的值，缺失目标与实例目标明确区分 |
| schema 读取入口、版本 | 引用所属 schema 来源；语句取得 guard 后 pin 住使用的快照 |
| 计划/PS 缓存入口 | 引用所属目标的缓存，不按请求创建；实例 PS 仅在执行能力需要时启用 |
| tablet 解析、scan/DML/LOB、锁与事务适配 | 引用已装配接口，最终复用物理执行能力 |
| session/事务/语句状态 | 按原有生命周期持有参数、事务描述符、快照、超时、取消和语句分配器；不塞入长寿命全局绑定 |

Parser、optimizer、executor 的实现代码及共享物理引擎继续复用；每条 SQL 的编译、执行内存依旧随实际工作产生。共享代码不等于共享 Namespace 可变状态。

实例上下文只创建一次，与 Namespace 数量无关；proxy、会话和任务引用它。新增内存主要是实例固定 schema、一份有上限的计划缓存、并发内部 SQL 的执行状态以及必要适配对象。不能在没有测量时承诺具体 KB/MB 或声称零开销。

### 12.2 当前实现的实际开销与约束

- `NamespaceRuntime` 当前有 25 个服务槽；其中有共享接口/轻量适配，不能把它们都当成独立重型服务。但 `has_request_services()` 要求所有槽非空，适用于完整客户端 Namespace，不应被用来验证实例内部 SQL。
- `activate_in_process_namespace()` 的确初始化 schema 后端、schema 发布/刷新、计划缓存、PS 缓存、DDL 管理、自增、统计管理/监控、DBMS 调度器和表锁服务。实例 SQL 禁止调用这条整套激活流程。
- 当前子 Namespace 计划缓存初始化传入 `OB_PLAN_CACHE_BUCKET_NUMBER=49157`。`ObPlanCache::init()` 与 `ObPsCache::init()` 各自初始化缓存淘汰定时器。复制默认构造不能称为轻量上下文。
- 实例计划缓存采用明确容量和适配其语句数量的桶数，先测量再确定数值。已有缓存初始化需支持注入定时执行能力，缓存淘汰注册带 owner 的任务并共用执行线程；不为实例上下文默认复制每项服务的线程，也不新增遍历 NamespaceRegistry 的调度循环。
- 固定 schema 不需要多版本动态刷新、历史表加载、DDL 管理线程。实例 DQL/DML 所需的系统变量、权限、优化器统计读取、锁和事务依赖必须追踪完整；仅减小结构体或跳过初始化不能证明 SQL 路径可用。
- 第一条完整实例 SQL 路径的验证须记录空闲常驻内存、缓存实际分配/上限、额外线程、并发查询峰值和控制 SQL 延迟；同时增加多个 Namespace，确认实例上下文资源只计一份。发现 SQL 核心仍强制完整 Runtime 时，先拆依赖接口，再进行控制表切换。

内表的确切 ID 需按生成器保留区分配，完整表分类需要在实施 A/D 中逐表审计；本文没有宣称已完成全量内表审计。若以后允许用户动态创建实例级表，再设计实例级持久化 schema 与 DDL；当前不为该未提出的能力复制完整的目录服务。
