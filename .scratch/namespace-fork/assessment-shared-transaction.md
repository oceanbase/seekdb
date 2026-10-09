# 事务共用：tablet 物化的实现评估

日期：2026-10-02。源码基线：`50557ddbe3f3956afa2b71addbfcf15d380738d6`。

状态：已完成实现、生产编译及本地四件套验收，并提交推送 `a9bf8740a` 至 `codex/namespace-worker-proxy-v20`，远端 SHA 已核对。以下现状描述保留评估时基线；最终实现、故障与续跑证据见 [实施记录](shared-transaction-progress.md) 和 [验收清单](shared-transaction-acceptance.json)。测试与文档仅留本地，不提交分支。

## 0. 当前实现核对（a9bf8740a）

本次重新核对当前源码及既有验收日志。**实现方向明确，而且当前分支已经落地；无需再为此选择一套事务架构。** 第 1～7 节是原始评估，涉及“双提交”的描述均指 `50557ddbe`。

| 改动位置 | 当前实现 | 要保证的约束 |
| --- | --- | --- |
| `InstanceMetaStore::attach/detach` | 借用已有、活跃、非 shadow 的 RC 原生事务；点读写和扫描继续使用原实现 | KV 不提交、回滚或释放借来的描述符；快照和目录 GC 保护持续到外部事务结束 |
| `ObInnerSQLConnectionAccess::with_native_transaction` 及 observer 桥接 | 从指定内部连接找到存储侧实际 `ObTxDesc`，执行回调后同步 SQL 侧视图 | SQL 会话中的视图不能当作实际事务；失败操作也要同步状态，不按 Namespace 编号另选事务 |
| `NamespaceForkKernelPrototype::ensure_tablet_impl`（后续 `a73ae2b0c` 已并入 `ensure_tablet`） | 开启内部 SQL 事务 → KV attach → 锁 Namespace 记录并读最新状态 → CREATE/LOB/序列/映射 → KV owned → 唯一一次事务结束 → detach | 主/LOB 绑定单元统一提交或回滚；锁后检查不能依赖等待前的旧快照 |
| 物化后的 owned 缓存 | 提交成功才增量发布；失败、结果不确定或被并发请求完成时使缓存失效 | 缓存不决定持久化结果，不通过补写 owned 猜测提交是否成功 |

这里的事务共用，是**物化内部几个步骤共用一笔事务**。触发物化的用户 INSERT 仍有自己的事务；用户回滚 INSERT 后，可以留下一个完整、可用但不含该次插入的物理副本。不能把这理解成所有用户 SQL、KV、DDL 都已经共用事务。

### 三种事务关系

| 范围 | 当前结果 | 是否包含在本项 |
| --- | --- | --- |
| 物理 CREATE、主/LOB 绑定、序列、SQL tablet 映射、KV owned | 共用物化内部事务 T，由内部 SQL 事务唯一提交或回滚 | 是，已落地 |
| 触发物化的用户 INSERT/UPDATE | 使用用户事务 U；T 成功后才修改本地副本，U 回滚不撤销 T | 不合并，维持现有语义 |
| 普通 DDL 的 schema 更新及其后续 KV schema delta 发布 | 当前仍分开提交，使用 pending 标记及恢复流程衔接 | 未纳入；不能仅凭 attach 接口就宣称已原子化 |

因而本项可以确定为已有引擎内的事务接入改造。若下一步要求 DDL 发布也共用事务，需要另行核对其锁阶段、提交时点和发布生命周期；不能把它混入已经完成的物化内部原子提交。

### 工作量和实际难点

主体接入集中在上表前三处，属于已有事务接口和生命周期的改造；无需另建 WAL、两阶段提交协调器或 Namespace 专用事务协议。实际提交覆盖 20 个生产文件，还包含故障验证暴露出的必要收尾：

1. **回滚后重新创建同一个物理 ID**：已分配但未提交的 tablet 可能仍在内存或有 redo，不能仅凭“对象存在”判定重复创建。复用原生 GC 资格判断；有 redo 的对象经持久化空壳及 checkpoint 清理。
2. **回收与重试的等待关系**：从未提交的创建对象不可能被孩子继承，其清理不能等待正在重试创建的 Namespace 访问全部退出；已提交后删除的历史对象仍需原来的引用保护。
3. **崩溃后没有最终决议的事务**：本地日志回放结束、开放 SQL 前，用原生持久化 abort 收尾，结束行和 MDS 回调。不能在 Namespace 目录里补记录以掩盖未结束事务。

因此可以明确采用这一实现方向，但工作量不能只按“增加一个 ObTxDesc 参数”估计；正常提交路径较集中，故障恢复和重试是主要验证成本。

### 仍独立存在的事项

- `publish_schema_delta` 明确在 DDL 提交后另行提交 KV。DDL 新建对象的 owned 登记、pending 标记和恢复不能随物化的 `repair_owned` 一并删除。后续是否让这些 DDL 更新也共用事务，需要单独核对 DDL 提交及发布时点。
- 不扩展为任意用户 SQL/KV 混合事务、保存点或并行执行接口；当前桥接用于串行内部操作。
- 源 tablet 的历史解析、快照 pin、主/LOB 绑定和对象回收仍有各自职责；一次提交不会代替这些机制。

### 本次核对的证据范围

已核对上述源码，以及验收清单所列 bootstrap-native-kv、bootstrap、sql、tls、direct 续跑日志的存在性和 SHA-256，全部与清单一致。本次没有重新运行数据库或编译。此前失败和修复的场景已纳入本地四件套；direct 首轮的既有 bootstrap 超时溢出保留为独立 TODO，续跑通过不代表它已修复。

后续方案 A 访问入口重构已提交推送 `a73ae2b0c`，有独立的最终编译及四件套验收；事务共用的原始验收只对应 `a9bf8740a`。其 `/tmp/seekdb-tablet-access-smoke-4.log` 的并发 INSERT `4023 / Try again` 已拆成两个窗口：未提交 CREATE 的无效 SCN 被误当作最大有效 SCN；事务已提交但 MDS 回调尚未发布时原生读取没有等待。后续提交已分别修正，8 轮双连接创建、原有物化回归和最终四件套通过（旧 bootstrap 超时失败保留后续跑）。它们属于状态发布与等待问题，并非物理创建和 owned 分属两个提交。失败、修正和验证日志见 [物化重构实施记录](tablet-materialization-progress.md)。

## 1. 结论

**方向明确，可以利用现有原生事务实现。建议保留物化的内部 SQL 事务作为唯一拥有者，让 InstanceMetaStore 借用它实际持有的底层事务。** CREATE MDS、LOB 绑定、自增序列、tablet 映射和 EXCEPTIONS 中的 `owned` 登记，统一提交或回滚。

`owned` 表示这个 Namespace 已有该逻辑 tablet 的本地物理副本。在评估基线中，物化与 `owned` 分别提交，原因在接口各自开了事务；二者已经使用同一个原生存储及事务系统，无须新增 WAL、事务协调协议或持久化格式。

主要改动集中在三处：KV 事务借用、内部连接的原生事务接入、物化工厂提交顺序。相较于“首次读是否也物化”，这里没有访问策略或物理状态模型的重新选择。主要工作是处理好现有事务的所有权、执行上下文和失败收尾。

范围限定为物化的一笔专用内部事务。它仍独立于触发物化的用户 INSERT 事务；用户 INSERT 回滚可以留下一个已完整物化但没有该次用户写入的副本。任意用户 SQL/KV 混合事务、保存点、并行执行等不在本轮实施范围。

## 2. 评估基线的现状与证据

### 2.1 基线中的物化开了两笔事务

[ensure_tablet_impl](../../src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp)（1333 起）：

1. 1388 起：KV 自己开启 `directory_tx`，锁 Namespace 记录，检查来源和快照保护。
2. 1555 起：开启 `ObMySQLTransaction trans`，登记物理 CREATE、LOB 绑定及序列，写入 SQL tablet 映射。
3. 1609：先结束 SQL 事务。
4. 1610 起：再写 KV `owned` 并结束 KV 事务。
5. 1624 起：成功后更新内存中的 owned 缓存。

所以，SQL 提交成功后崩溃或 KV 提交失败，会留下“物理副本已提交、owned 尚未登记”。1411 起的 `repair_owned` 分支包含对这种状态的补登。

### 2.2 KV 已经直接使用原生事务

[InstanceMetaStore](../../src/storage/instance_meta/instance_meta_store.cpp)：

- `begin_impl`（152）：自行 acquire/start 原生 `ObTxDesc`。
- `write`（402）：把该描述符传给 AccessService，写同一个 LS 内的 KV tablet；写上下文在函数返回前收尾，合并写入序号与执行结果。
- `end`（238）：自行 commit/rollback、release 描述符。
- 活跃事务链表还承担快照保留；目录页 GC 与普通 KV 事务互斥。这些保护不能因为改为借用而丢掉。

[ObTabletCreator::execute](../../src/rootserver/ob_tablet_creator.cpp)（318）通过已有 SQL 连接注册 CREATE MDS；[copy_sequences_for_fork](../../src/storage/ob_tablet_autoincrement_service.cpp)（371）也通过传入的事务注册序列 MDS。因此可以继续使用这些接口。

### 2.3 SQL 会话里的描述符不能直接传给 KV

当前 Namespace 内部 SQL 路径中，[InProcessTransactionService](../../src/observer/namespace_inprocess_transaction_services.ipp) 创建的是 SQL 侧事务视图；实际持有锁和写入状态的原生描述符在 `InProcessStorage::writes->tx`。

[事务桥接](../../src/observer/namespace_worker_gateway_prototype.ipp) 的 `call_in_process_tx_state`（302）、`call_in_process_tx_register_mds`（531）已经示范了正确模式：绑定连接对应的存储上下文、核对事务身份、操作原生描述符，再同步 SQL 侧视图。

**“共用”必须指同一个原生事务对象；只取得相同 tx_id 的视图副本并不够。** 这是本项最主要的接入细节。

## 3. 建议接口与职责

### 3.1 KV 增加借用方式，复用既有读写实现

在 [InstanceMetaStore::Transaction](../../src/storage/instance_meta/instance_meta_store.h) 中显式区分自有和借用的事务；增加 attach/detach 一类的小接口，具体命名实施时确定。

| 操作 | KV 自有事务 | KV 借用事务 |
| --- | --- | --- |
| 创建、启动原生事务 | KV | 外部唯一拥有者 |
| KV 点读写、扫描、锁定读 | 现有实现 | 复用同一实现 |
| 登记快照、阻止目录页 GC | KV | KV，持续到外部事务结束 |
| 提交、回滚、释放原生描述符 | KV | 外部唯一拥有者 |
| KV 包装对象析构 | 回滚尚未结束的自有事务 | 只解除借用及自身保护，不结束外部事务 |

借用方式不允许调用 KV 自有事务的 commit/rollback。调用方持有原生事务的完整生存期；KV 不保存 SQL 连接或 Namespace Runtime，也不选择 Namespace。

### 3.2 通过内部连接受控接入原生事务

接口放在现有 [ObInnerSQLConnectionAccess](../../src/query/api/query/session/ob_inner_sql_connection_access.h) 及 observer 实现这一接缝。由调用方传入具体连接，内部参照现有 MDS 桥接恢复连接所属的 `StorageSessionScope`，校验 SQL 视图和原生描述符对应关系。

建议采用作用域回调，限制原生事务操作的范围，不向业务调用点暴露一个可以随意提交和释放的裸指针。桥接实现负责：

- 使用该连接已绑定的原生事务，不按 Namespace 编号猜测或回退。
- 恢复存储侧会话和超时上下文，退出时还原。
- KV 操作完成后同步 SQL 事务视图中的序号、执行结果等状态，再允许后续 SQL/MDS 操作；失败收尾保留首个业务错误。
- 借用期间禁止替换或复用描述符，操作串行执行，不允许扫描迭代器或写上下文越过提交点。

当前工厂可以分两段进入这个作用域：第一段 attach 并完成 KV 锁定/检查；中间调用现有 CREATE、序列、SQL 映射接口；第二段写入 owned。借用保护跨越两段并持续至最终提交。回调内部不得结束外部 SQL 事务，以免桥接返回时同步已释放的描述符。

### 3.3 工厂只保留一个事务结束点

物化工厂拥有内部 SQL 事务和 KV 借用包装对象，统一执行结束与清理。普通 KV 调用者仍可使用原有自有事务接口。

选择 SQL 事务为拥有者，可以继续使用现有 creator、序列和映射接口。反过来由 KV 拥有，再把 SQL 连接接到它上面，需要改造更多连接生命周期；当前没有这个必要。

## 4. 修改后的执行顺序

```text
持有现有物理回收保护 MetadataReadGuard
  开启一笔专用内部 SQL 事务 T
  KV attach 到 T 的原生描述符，登记快照及目录页 GC 保护
  KV 锁 Namespace 记录
  锁内按最新已提交状态重查 owned、墓碑和物理创建状态
  确认 schema/LOB 绑定、来源及 fork 快照仍有效
  在 T 中登记 CREATE、绑定及序列，写入 tablet 映射
  在 T 中写入整组主/辅 tablet 的 owned
  T 统一 COMMIT；提交前失败则统一 ROLLBACK
  KV detach 并解除自身保护
  成功后更新 owned 缓存；结果不确定时使缓存失效
释放物理回收保护
```

采用专用、可写的 RC 内部事务和统一截止时间。开始 SQL 事务后先 attach、再获取元数据锁，不在 attach 前执行其他持锁业务。维持“物理回收保护 → 目录页 GC 保护 → Namespace 行锁 → 物理创建/映射”的锁顺序。

一次提交保证同一事务的持久化结果一致；不代表 KV 缓存、tablet manager、MDS 提交回调在所有线程中同一时刻发布。已有物理状态检查及必要重试仍然存在。回滚也不要求已经分配的未提交物理对象当场消失；这些对象不得成为已提交可用副本，清理由原生 MDS 生命周期负责。

## 5. 必须处理的收尾与并发细节

1. **快照与 GC 保护覆盖到事务结束。** attach 延用当前“先登记活跃读者，再取得快照”的顺序。不能在最后一次 KV 写完时提前 detach。物化用新开的 RC 事务，不尝试给任意已有的旧快照补做历史保护。
2. **锁后检查必须读新状态。** `get_namespace(..., true)` 会在行锁后取新快照，但普通 KV 读取仍使用开始时固定的快照。等待另一创建者后，要用现有锁定读检查 `owned`，不能只查缓存或开始时的快照。其依据是 [get_for_update 与 scan_rows](../../src/storage/instance_meta/instance_meta_store.cpp)（471、300）。
3. **只有拥有者结束事务。** [ObMySQLTransaction::end](../../src/oblib/common/mysqlclient/ob_mysql_transaction.cpp) 会关闭连接，原生描述符可能随之释放。因此随后 KV detach 只能移除自己的链表节点、快照及 GC 保护，不得解引用或再次释放原生指针。整段结束逻辑要集中，避免调用点自行排列析构顺序。
4. **所有失败出口显式结束。** `ObMySQLTransaction` 析构会依据自身 errno 决定提交或回滚；不能认为局部 `ret` 失败就一定自动回滚，尤其错误来自 KV 时。统一清理入口必须显式执行 `end(false)`。
5. **提交超时不等于回滚。** 返回结果不确定时，原生事务仍负责最终原子结果。使该 Namespace 的 owned 缓存失效，下一次重试在新的事务中锁定并读取持久状态；不因一次超时手动补 owned 或删除物理对象。可复用现有 `control_state().drop_exceptions()`，无需新增持久化补偿状态。
6. **缓存只反映已提交状态。** owned 在提交成功后才发布；重试、DDL 发布和缓存重载的竞争仍按已有失效机制协调。持久化行锁内的判断不能以缓存代替权威读。

## 6. 简化收益与范围

可以明确消除：

- 物化工厂独立开启、提交、回滚第二笔 KV 事务。
- 该工厂造成的“物理已提交、owned 未提交”持久化中间状态。
- 为恢复这种双提交中间状态而补登记 owned 的需求。

仍然需要：来源解析、历史 pin、并发创建的 Namespace 行锁、主/LOB 绑定、物理生命周期检查，以及成功提交后的缓存处理。这些职责与是否共用事务无关。

**不能把全项目的 owned 对账一并删除。** 当前 [schema delta 发布](../../src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp)（1099 起）仍在 DDL 提交后另行提交 KV；[stage_schema_delta / reconcile_owned_tablets](../../src/rootserver/fork_table/instance_namespace_metadata.cpp)（1245、1328）确实负责登记其他 DDL 创建的 tablet。物化原子化不会自动将这条发布链变成一个事务。

实施时将 `repair_owned` 的删除限定在物化双提交补偿：核对其他 DDL 新建对象是否会在发布前进入这个共用入口，再决定整段删除还是把仍有实际用途的登记归回 DDL 发布流程。现有 DDL 恢复保留其当前职责；不为旧版本数据或升级保留补偿。此次静态评估不承诺整段补登代码必然全部可删。

本轮也不改变现有显式目录 proxy 的归属。底层 KV 只接收原生事务，不新增 Namespace 分支。实例管理 SQL 通道及全量 DDL 的事务边界仍是各自议题。

## 7. 实施与验收

建议按三个改动组实施，但合并为一个可验收的结果：

1. KV 自有/借用的生命周期与保护共用实现。
2. 连接到原生事务的作用域接入、视图同步及资源收尾。
3. 物化工厂接入，单次提交、锁内新状态检查、缓存失效和补偿代码清理。

聚焦验收纳入本地四件套；测试和文档不提交分支，不跑完整 mysqltest/sysbench：

| 场景 | 验收要点 |
| --- | --- |
| KV → SQL/MDS → KV 交替操作 | 始终使用同一原生事务，能看到自己的前序写入，SQL 视图序号同步正确 |
| 成功物化主 tablet 及 LOB 绑定单元 | 创建、序列、映射、owned 全部已提交，子空间读写正确 |
| CREATE 后、owned 前失败；owned 后、提交前失败 | 回滚后无已提交副本、映射或 owned；允许原生机制清理暂存物理对象 |
| 提交过程中超时/崩溃及重启 | 恢复后统一提交或回滚；不靠手动补 owned 拼接结果 |
| 两个请求并发创建同一个绑定单元 | 等待者锁后看到胜者的新状态，不重复创建；失败创建者可重试 |
| 提交结果不确定前已经装载 owned 缓存 | 失效后重读持久状态，不永久误判为继承态 |
| GC/快照保留与失败清理并发 | 保护覆盖实际事务，结束后解除；无重复释放、意外提交和保护泄漏 |
| 相邻 DDL 创建及发布恢复 | 不误删这些路径仍需要的 owned 登记和恢复能力 |

本轮仅核对源码和设计，不把上述计划当作已经通过的动态证据。
