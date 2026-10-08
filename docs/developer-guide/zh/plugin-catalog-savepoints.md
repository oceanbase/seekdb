# Routine catalog 视图保存点

本阶段为查询期 catalog 写入补充宿主基础：routine schema 与权限视图可以成对回滚。
当前已用于 Root 安装/更新的单次 staging 失败清理，**不是用户 SQL SAVEPOINT、数据
事务回滚或查询期 CREATE FUNCTION 已实现**。C ABI/Rust SDK 本轮不增加写入权限。

## 视图内的保存点

`RoutineCatalogSavepoint` 同时持有 `RoutineSchemaOverlay` 和它绑定的可变
`RoutinePrivilegeOverlay`。不允许将一个 schema 视图与另一份权限视图混合。
共享所有权让保存点存活期间 owner 不会销毁/复用地址；它不是跨线程同步机制。

权限视图有两种宿主构造方式：安装/更新继续使用
`RoutinePrivilegeOverlay(database, principal)` 固定作用域；事务级使用默认构造，
支持多数据库和多 owner。两种方式共用实现，名称索引均以 database/type/name
为键，自动权限归属每个 routine 的 owner，与正常 writer 的持久授权一致。
当前 schema 必须与授权记录的 database/ID/owner/name/type/version 匹配；不能
把另一数据库同名对象的授权套用过来。切换数据库或创建者不重置视图的总预算。

默认构造仅放开记录的作用域，不是权限入口，也不批准 definer 或会话身份变更。
宿主必须先完成正常授权，并让 schema 与权限记录对应同一真实写入；现有 Root
安装路径的严格约束保留。session 配对归属与公共 SQL 初始化绑定见下节；查询期
公开 mutator 仍待接入。

kernel 回归通过真实 guard/ACL 与 Rust journal 验证两个数据库的同名对象分别
授予不同 owner，旧用户/角色权限不会跨对象继承，函数与过程独立；同 barrier
的跨库变更由 Rust 倒序回滚，原对象 ACL 和角色权限恢复，借出 schema 仍有效。
独立 guard 不继承私有授权。用户、基础 schema 和授权输入仍为受控 fixture，
不代表已经验证真实 USE、身份切换、数据库事务可见性或并发。

- 构造只记录两个视图的当前分支位置，O(1)，不复制名字表或完整 routine。
- `release()` 放弃该标记的自动回滚，不提交任何事务；外层保存点仍能回滚其修改。
- `rollback()` 先检查两个标记都在当前分支祖先链上，再无分配地恢复两份索引。
  已回滚分支中的后续标记无效，不能“向前恢复”出被撤销的对象或权限。
- 析构时若未 release/rollback，尝试恢复。因此异常、早退或后续权限 staging 失败
  不会留下只更新了 schema 的半份视图。正常使用要求串行访问整个视图。
- 标记一次性消费。本类不管理 SQL 名称或数据保存点。下述事务日志将标记放在每次
  变更之前，按数据序号撤销，因此可重复回滚到同一个 SQL 保存点，无需复制名称栈。

回滚恢复的是“当前可见索引”，不释放记录。旧 planner/PL/guard 借出的 schema 指针
仍有有效的 backing storage，但它们不自动变为最新对象；后续解析必须重新查当前
guard。tombstone 回滚可恢复为“由 base guard 查询”，不能残留 NULL 或零权限遮蔽。

保留记录也意味着保留内存成本。原 schema 字节计数继续累计，不因回滚减少；原
64 MiB schema/16384 记录上限不变。权限 undo history 另有 16384 条上限，防止
反复创建后回滚绕过只按当前身份数计算的容量限制。该计数不等同于整个进程 RSS。

视图回滚不使已经预留的持久 object ID/schema version 可回收。真正事务协调者
必须废弃对应 reservation token；新对象继续通过真实身份分配器预留，不能用
回滚后索引不存在来声称旧 ID 可以分配给另一对象。

## 现有生产路径接入

Root 的安装 staging 在发布 schema 之前建立标记，完成权限记录和拥有的 node
入队后 release；中途错误恢复两份视图。更新路径在 admission/staging 周围采用
相同标记。更新的其它命令队列、身份/版本 reservation 和持久写入仍由原安装/更新
事务协调器负责；失败依然终止整个序列，不允许仅恢复视图后继续执行旧命令队列。

这一步让现有 staging 具备成对失败清理，也为查询事务参与提供必要的视图回退操作。
后续仍需把它与调用者事务、语句/用户保存点、预留身份、系统表写入、schema 发布及
最终提交/回滚绑定，才能开放查询期创建/修改/删除。不能通过独立提交 DDL 绕过此工作。

## Rust 事务视图日志

新增 `query_transaction.rs` 与宿主 `RoutineCatalogTransaction`，管理一个真实
transaction ID 下的视图 undo 标记。session 生命周期接入见下节；**尚未开放查询期
写入**，不能把以下组件协议描述为用户 SQL 已具备的 catalog DDL 能力。

宿主先成功获取数据事务 barrier，再调用 `record`，最后修改配对视图。Rust 成功
接收记录后拥有标记；失败仍由 C++ 释放。数据层回滚成功后传入实际目标序号，日志
倒序撤销所有 `record.sequence >= target` 的变更。尚无 catalog 修改时不需要预先
记录每一个用户保存点；目标早于第一条记录时自然撤销全部视图修改。

比较使用 `ObTxSEQ::get_seq()`，不能传 packed raw value。序号高水位在回滚后不减少，
同一 barrier 可有多条串行记录。当前 adapter 明确只接收 root branch；非 root branch
返回不支持，未来接入不能忽略分支后允许并行 catalog mutation。所有调用及回调需
外部串行化，不允许重入，同一 session 的异步提交接管也必须遵守唯一所有者协议。

新增数据事务接口重载可返回具名保存点的实际回滚 barrier：名称选择、数据回滚、
后续保存点失效及结果赋值在原事务锁内完成。原三参数入口委托到该实现；其他实现
默认返回不支持且不回滚数据，调用者不能再 fallback 到不返回目标的旧入口。接口
本身不追踪 catalog，也不提供查询写入授权。

undo 回调无论成功失败均消费标记；第一次错误保留为原始宿主错误，后续成功不能
覆盖它。日志仍清理选中范围内其它标记，但拒绝新变更和 commit 通知；最终 abort
继续清理剩余标记，不能重试已经消费的 undo。日志最多同时拥有 16384 条记录，
分配失败不转移所有权；schema/权限视图已有的累计预算独立生效。

无 schema 写入的数据提交前调用 `prepare_commit()`；有写入的事务必须使用后述
begin/complete 准备协议。二者检查首错并永久关闭记录/保存点回滚入口；
重复 prepare 或未 prepare 就通知 commit 均拒绝，提交失败后不重新打开旧日志。
`finish(true)` 仅用于已经确认的持久提交，释放标记，不发布 schema；`finish(false)`
仅通知已经确认的 abort，并恢复私有视图。未知提交结果应使所有查询访问失效，再
销毁私有日志/视图，不能调用一个猜测的 finish，也不能声称析构撤销了数据库提交。
真正的数据事务、系统表写入、schema invalidation/publication 仍由宿主统一协调。

## 事务归属的 routine 缓存失效请求

`record_invalidation` 将 database/routine ID 和单调 ticket 记入 Rust 日志，与视图和
schema operation 共用 16384 条预算。必须已有 DDL 准入且在同一 root barrier 成功
记录 schema 写入；它不授予 DROP 权限，也不调用缓存或 SQL。记录失败必须触发
对应数据操作回滚。保存点回滚清除请求并恢复最近 schema 写入 barrier，但不回退
序号高水位和 ticket；重复对象保留不同 ticket，失效应可幂等执行。

未绑定后台队列时，`finish(true)` 在原地无分配地释放私有视图标记，保留失效请求；`peek_invalidation`
只在已知提交后返回最早请求，空队列返回全零。宿主退出 FFI 后完成可靠接收或实际
失效，再用准确 ticket 调用 `ack_invalidation`。失败可重复 peek，错误/重复 ack
不删除其它请求，不重试数据库提交。abort/未知结果不授予读取投递接口的权力。

`CallerCatalogTransaction::invalidation_sink()` 只在有效且已准入的查询 transport
返回借用的宿主 sink。每次调用重新检查 session、实际事务、数据库、线程与 barrier，
commit transport 不提供该 sink；close 后不得保留指针。接口位于独立宿主头文件
`routine_cache_invalidation.h`，不是插件 SDK。

### 提交前预留与后台接收

每个 experimental 构建中的计划缓存现在拥有一份 Rust `InvalidationQueue`，C++
以 `RoutineInvalidationQueue` 包装。使用已有 `ObPlanCacheEliminationTask` 定时器，
不新增线程。默认上限 256 个事务批次、65536 个请求槽；预算按 Vec 的实际 capacity
计费，整批释放后归还，不能通过部分 ack 却保留大 buffer 绕过容量约束。

session 的 CommitHost 在 Rust journal 已 Preparing/freeze 后、提交准备 SQL 前，
通过正常 server-service slot 取得计划缓存并预留失效请求快照。快照仅保存
database/routine/ticket/schema version，不持有 session、C++ view、query context
或插件回调。队列满、关闭或分配失败在数据提交前报错；后续提交准备失败仍走原
事务中止路径，不能忽略预留失败后继续提交。

已知 commit 的 `finish(true)` 将预留批次标记 ready，并从原 journal 清除对应请求，
无需新分配、SQL 或驱逐回调；session 可以随后 discard 私有 journal。已知 abort
释放预留，不驱逐。手工 peek/ack 仍服务未绑定队列的内部调用者，但不能替代生产
session 的预留协议。

后台每轮最多处理 8 条：退出 Rust FFI 后检查已刷新 schema 版本达到请求门槛，
再调用正常的本地 PL cache 按 database/routine ID 驱逐路径，成功后确认。版本
未就绪时仅请求后台 schema refresh 并保留请求，不在数据提交回调里等待或执行
全局 SQL。已知提交使用最终 end-sign 版本；队列处理失败/宿主异常保留原请求，
轮换到下一批，下次定时器继续尝试。这个条数预算不是驱逐耗时的硬实时上限。

本地驱逐的错误路径也必须释放临时引用：`foreach_cache_evict` 在遍历失败后
取得部分 collector 列表，只清理引用、不删除 map；批量删除中途失败后仍释放
全部已收集引用，返回原错误。否则队列每次重试都会额外固定一批 PL 节点。
kernel 回归已用真实 map、PL 节点与引用计数验证重复遍历失败、部分删除失败，
以及 Rust queue 保留请求后由正常 PL cache 后端重试成功；schema 就绪检查和
访问服务仍是 fixture，并不证明真实定时器、并发查询或 bytecode 最终析构。

`ObPlanCache` 的队列 owner 在共享头中保持无条件布局，构造/使用由插件宏控制，
deleter 在实现文件中定义。SQL 与未定义插件宏的 main 等目标因此共享相同布局；
关闭宏的 plan-cache 对象编译及符号检查确认没有 Rust 队列未解析依赖。不能通过
只给测试添加插件宏，掩盖真实生产消费者的布局差异。

### 未知结果与关闭

结果未知不能调用猜测的 finish，也不能发布 provisional schema。但丢弃已预留
请求可能遗漏实际成功的提交，因此未解决的 reservation 随 journal 销毁时会触发
**保守驱逐请求**。门槛保留冻结时的最大 schema operation 版本：后台仍必须等已
刷新 schema 达到该值才驱逐。若实际已回滚且没有后续版本，该请求可能一直保留到
后续 schema 推进或缓存销毁；不伪造提交、不重试数据事务，也不提前推进 schema。
这是对先前“未知结果只清理私有日志”的补充，不改变未知结果的数据库状态判断。

计划缓存 stop 关闭新预留，不撤销已接受的承诺；迟到提交仍可进入预留批次。
destroy 先停止并 join 缓存定时器，且只能在原缓存不可再访问后退休队列。此时
volatile cache 本身已销毁，剩余驱逐请求失去目标，可随队列释放；尚存 reservation
通过 Arc 保持标量状态内存安全，不再访问已销毁的 C++ cache/队列句柄。这不是
持久 outbox；重启后的正确性依赖正常持久 schema 恢复和重新建立空缓存。

公开查询 DROP 仍未接入；真实数据库提交、其它 session/旧 schema 查询并发、
定时器失败/恢复以及关闭竞态仍需端到端验证，不能用此内存协议的测试替代。

## 事务归属的 schema operation 版本

Rust 日志现在还接收已经成功写入的 catalog operation 版本，不把“已预留版本”当成
“已执行变更”。版本项与视图标记共用同一 barrier 顺序、16384 条容量和回滚栈。
每项保存此前的最大版本；回滚同时恢复 surviving 最大版本和 operation 数量，不需
重新分配内存。这里的数量是成功记录的 DDL operation 数，不是 SQL 对象数量。
版本可能提前预留且乱序写入，因此使用最大值，而不是最后一次记录的值；版本本身
不会因回滚被回收。零版本/零数量表示没有剩余 schema operation。

`record_schema_version` 与 `schema_state` 是内部 C ABI，由
`RoutineCatalogTransaction` 桥接；snapshot 仍检查 transaction ID 和首错，seal 后
可以读取提交输入，finish 后不可读取。它们只保存事务状态，不写入/推进全局 schema
watermark，不启动恢复或发布任务。schema operation 记录失败必须使宿主回滚对应
数据操作，不能因为系统表 INSERT 已成功就忽略内存记录失败。

正常 `ObDDLSqlService::log_operation()` 按 SQL client 的
`ICatalogOperationRecorder` 能力选择路径：借用 client 先检查记录上下文，再生成并
写入 DDL operation，确认 affected rows 为 1 后记录版本；不调用会修改
`TSILastOper` 的生成方法。没有该能力的原 Root/client 路径继续使用原线程局部记录。
借用 client 未绑定 recorder 时在写入前拒绝，不静默退回 TSI；SQL/形状/记录错误
保留为该适配层首错。直接使用 `log_operation_dml` 的批量生成路径不在此接入范围，
后续若开放给查询事务必须另行接入，不能假设所有 DDL 已摆脱 TSI。

`CallerCatalogTransaction::open(context, barrier)` 由宿主传入当前对象操作的真实
root 数据 barrier，并要求 session 已登记配对视图参与者。调用期间不得交错其它
catalog mutation；下一次对象操作使用其自己的新 barrier。session 记录时再次核对
活动事务身份/sequence base。无 barrier 的原 open 仍可提供内部 SQL transport，
但不能通过上述 logger 写入 DDL operation。

session 的 surviving schema operation 现已进入下述提交准备协议，不再无条件返回
不支持；缺少准入、准备失败或恢复失败仍禁止提交。插件查询期 CREATE/ALTER/DROP
尚未开放，不能把提交接线等同于完整的查询 catalog 写入能力。

## 可复用的提交前数据库步骤

宿主新增 `CatalogCommitPreparation`，实现仍位于 Root 的生产 DDL 单元中。
它接收已有 `ObMySQLTransaction &`、明确的 surviving schema version、是否有
schema 变更及是否需要 end-sign，而不是自行读取 `TSILastOper`。当有变更且要求
end-sign 时，通过原 schema service 分配更高版本并写入 `OB_DDL_END_SIGN`；随后
通过同一连接登记 `DDL_TRANS`，最后使用原 `ObGlobalStatProxy` 在同一事务内推进
normal schema watermark。无 schema 变更时不写 end-sign/watermark；登记事务
信号的原 Root 行为保留。旧 bootstrap 的零版本输入仍可由正的 end-sign 版本结束。

该组件只允许一次 prepare 尝试。任何版本分配、SQL、MDS、watermark 或异常失败
都会停止后续步骤，结果版本保持为零；准备成功返回最终版本，不等于提交成功。
组件不调用 start/end/commit/rollback，不发布 schema，不持有调用者 SQL frame。
错误后必须由真正的事务所有者中止事务，不能重新构造组件来重试同一份半完成状态。

Root 的 `ObDDLSQLTransaction::end()` 已复用此组件：它仍在外层检查 DDL epoch，
把原 TSI 状态转换为显式输入；只有准备成功才调用原数据 commit，失败调用原
rollback，并保留首错。组件调用方仍必须先取得 DDL 锁、校验 captured epoch，并
完成并行 DDL 的 wait_task_ready 顺序屏障。watermark 不能提前到对象写入阶段。

这是 C++ 数据库接入层，不是第二套 Rust 插件 registry 或独立持久 catalog。
Rust 日志提供跨语句的明确输入，借用 logger 将 end-sign 记入同一日志；session
通过下述宿主协调流程调用该组件，不假定 COMMIT 有普通查询的物理计划。真实
数据库中的锁、事务原子性和 schema publication 仍需端到端验证。

## 事务归属的 DDL 准入（2026-09-08）

`CatalogDDLAdmission` 是宿主数据库接入层，不是插件权限接口或新的事务所有者。
调用者先完成对象 ACL、配对视图与实际 root data barrier，再捕获已有 DDL epoch，
在同一借用事务取得 `OBJ_TYPE_RUNTIME / id=1 / IN_TRANS_COMMON_LOCK` 排他锁，最后
比较 guard 的已刷新 schema version 与锁后的最新已提交版本。不匹配返回 EAGAIN，
不允许带着旧 schema 继续修改。该组件不初始化/提升 epoch，不在准入阶段锁 epoch
或推进 watermark；捕获值必须在实际提交前经原 epoch 协议重新检查。

锁后读取使用宿主 fresh-reader，而不是调用者潜在的旧 RR snapshot；它是独立的
已提交元数据读取，不是独立 catalog 写事务。API 无法仅凭一个 C++ client 指针
证明其读新鲜度，宿主必须提供正确的 current-read client。Root 原 DDL 锁路径也
复用相同底层 lock_transaction，保留 parallel 的 SHARE 模式；caller 先使用
EXCLUSIVE 串行基线。锁可能跨多条用户语句持有，其等待/并发成本仍需实库测量。

Rust 日志新增固定 epoch/sequence 槽。`admit_ddl` 只在 Open 且尚无 schema operation
时接受，禁止提交阶段补录授权；sequence 不低于日志高水位。`ddl_admission` 返回
捕获值或零/零，`check_ddl_write` 检查 Open、首错、准入及写入 barrier 顺序。
真实数据回滚成功后，若目标不晚于准入 barrier，就撤销该记录；更晚的保存点回滚
保留它，高水位仍不回退。保守撤销不声称 native 锁一定已释放；再次修改须重新准入。
实际数据事务的锁所有权不能被这份内存记录替代。

`begin_prepare` 拒绝有 surviving schema operation 却没有预先准入的日志。底层
record_schema_version 仍是记录机制，不单独授予写权限；真实 Caller transport
在 admit_ddl 成功之前拒绝 catalog DML，之后每次操作核对 session/journal 归属
及 Open 准入状态。新连接可以复用同一事务尚有效的准入，不会重新捕获当前 epoch
覆盖旧值。普通查询和提交 transport 的事务所有权约束不变。

该路径尚未接到公开查询期 mutator。session 提交现在校验**捕获的** epoch，并执行
end-sign/MDS/watermark、连接恢复和 seal；异步完成只投递刷新任务，不执行同步
Root refresh。真实并发 DDL、提交和缓存可见性仍需实库验证。

## Rust 提交准备状态（2026-09-08）

Rust journal 现在区分 Open、Preparing、Sealed、Failed，完成通知另行标记
finished。普通 mark/schema operation 和保存点回滚仅在 Open 接受。
`prepare_commit` 只保留无 surviving schema operation 的视图事务快捷路径；有
schema 写入时，不能仅封存私有视图就宣称提交前协议已完成。

宿主 `RoutineCatalogTransaction` 增加以下协议，底层是内部 C ABI，不是公开插件
获得 catalog 写权限的新途径：

1. `begin_prepare` 冻结普通修改，返回事务归属的 surviving version/count。
2. 宿主在已获得的 DDL 锁、捕获的 epoch 和实际数据事务上执行准备工作。每次
   Rust 调用已经返回，不持有 Rust 可变借用再进入宿主 SQL/callback。
3. 真实 end-sign SQL 成功后，通过 `record_end_sign` 记录唯一且严格更高的版本。
   它使用固定槽，不占用/扩容普通 16384 条记录的 Vec；快照计数包含这额外一条。
   Preparing 不支持回滚到保存点，所以不伪造一个可局部回滚的 data barrier。
4. 所有数据库准备步骤和 transport 恢复结束后，宿主调用一次 `complete_prepare`。
   成功要求最终版本与记录一致（无 schema 变更时为零），随后 Sealed；任何失败
   或最终版本矛盾进入 Failed，不允许重试开放。数据库首错保留到最终 abort，
   不能被私有视图 undo 错误覆盖。此调用仍不是数据 commit 或 schema publication。

固定 end-sign 槽避免成功写入 SQL 后发生 journal 分配失败；不消除此前 SQL/MDS/
watermark 的失败，也不授予 Rust 自行验证它们的能力。`host_result` 必须来自
真实宿主协议，而不是仅看到一个 end-sign 就报告成功。完整 abort 通知恢复所有
私有 mark；未知结果只能销毁私有 owner，不能声称数据事务已回滚。

提交准备 fixture 将可适用的原 15 场景接到真实 Rust 状态机：普通写入被冻结，
真实 DDL logger 使用最终版本接口，SQL/MDS/watermark 失败禁止提交，成功后封存。
原 Root 的无 end-sign/bootstrap 场景保持独立，不将其误当作 caller finalization。
C ABI 回归补充身份/参数、重复准备、遗漏或错误最终版本、满日志、失败后重试、
首错以及未知结果清理。session 提交阶段现已调用这个协议；上述受控效果测试仍
不证明实库事务与发布。

后续接入新增 `check_preparing` 只读检查：Preparing 前后都拒绝，Preparing 中允许
重复检查及 end-sign 后继续执行 MDS/watermark，失败状态返回原始宿主错误。它不
消耗最终版本槽、不启动 SQL、不转移日志所有权。提交专用 `CallerCatalogTransaction`
以 session 归属和实际事务检查在外层约束该接口，见
[提交阶段 transport](plugin-query-catalog.md)。该连接必须在 complete_prepare
之前关闭；不能先 seal 再让最后一个结果集或连接清理继续访问 Preparing 接口。

## 查询期单次 catalog 操作协调（2026-09-09）

`run_catalog_operation(ICatalogOperationHost &, CatalogOperationResult &)` 是宿主内部
的 C++→Rust 协调入口，不是 SDK 写 catalog 的公开入口，也不创建自己的数据库事务。
Rust 只保留操作状态和错误码；每个宿主调用结束后再进入下一阶段，不跨 SQL 借用
`RoutineCatalogTransaction`。对象解析/普通授权、barrier、DDL admission、writer
仍由宿主 frame 提供，并且每次使用前都必须核对捕获的 session/transaction 身份。

成功顺序为 preflight → prepare → apply → close → check transaction；结果 APPLIED
只是调用者事务内成功，不等于 commit。prepare 必须先设置外层语句所有权，再建立
实际数据保存点和配对视图 mark，apply 才能写入。prepare 部分失败也必须 close。
preflight 不得留下需要数据库回滚的副作用，所需解析资源由宿主 frame 的 RAII 持有。

操作失败时，只有 close 和身份核对均成功才尝试数据回滚。数据回滚确认成功后才
回退视图；两者成功返回 ROLLED_BACK，仅撤销该操作，不能替调用者整体 rollback。
close、身份核对或任一回滚失败都进入 REQUIRES_ABORT，调用 poison，原操作首错
和各清理错误分开保存。close 失败表示 context 恢复不可信，即使资源已释放也不能
继续用它执行回滚。poison 即使失败，结果也不能变成可提交或已经回滚。

新增 journal `fail(transaction_id, nonzero_error)` 是无分配、无 SQL、无 undo 的
单调失败标记，保留首错并拒绝后续普通操作、savepoint rollback 和提交。已知 abort
及最终销毁仍消费 mark。宿主 poison 必须先禁用该日志并退休对应 schema/ACL，再
考虑实际数据 abort；不能因 abort 失败而恢复可见性，也不能作用于换代后的事务。

当前已具备 Rust 状态机、C ABI 和 C++ exception-safe bridge。实际 Query frame
`run_caller_catalog_operation` 现已调用 `prepare_plugin_sql`、数据匿名保存点、
session 配对视图登记/绑定、borrowed SQL、DDL epoch/lock admission 以及独立的
data/view rollback。`ICallerCatalogMutation` 是可信 Query 实现，不接受原生插件
注册；它负责实际普通 resolver/ACL 和 writer，不能保留超出 apply 的 SQL 结果或
reservation，不能提交/发布。具体 routine mutation 与公开 SPI/SDK 仍待接入。

frame 保留 journal/视图的 shared ownership，防止内部 SQL 改变 session 归属后
对象析构；每次操作仍检查实际 ID/base/context，引用本身不是继续写入的凭证。
poison 先撤销原 owner，再只对匹配的活动数据事务尝试 abort。session 的
`fail_plugin_catalog_transaction` 不修改数据描述符，即使描述符已换代也只会
撤销匹配的旧参与者，拒绝对新参与者应用旧操作的失败通知。

transport 的 `close(cleanup_result)` 将先前的 SQL/操作错误与实际恢复错误分开，
原 `close()` 的 sticky 返回值保持不变。Rust 依据 cleanup_result 判断上下文
是否可信，而不把普通 SQL 错误误判为不可回滚。恢复异常也捕获并报告为恢复失败。
受控测试不证明真实数据事务回滚、授权、并发或客户端跨语句执行；实现进度另见
[实施记录](plugin-implementation-status.md)。

## Session 提交准备与刷新交接（2026-09-08）

`prepare_plugin_catalog_commit(expire_ts)` 先验证实际活动事务身份，再保留 journal
的 shared owner，避免嵌套 SQL reset session 后造成悬空引用。无 surviving schema
operation 的事务继续使用不执行 SQL 的 view-only 路径。有写入时，宿主
`RoutineCatalogTransaction::prepare_commit(id, ICatalogCommitHost&)` 依次执行：

1. 读取最初 DDL 准入的 epoch，Rust begin_prepare 冻结普通修改与保存点回滚。
2. 检查 session 仍拥有同一活动 transaction ID/sequence base，打开提交专用借用连接。
3. 在同一借用事务检查并锁定**最初捕获的** epoch，不重新捕获新 epoch 覆盖旧值。
4. 执行最终 end-sign、DDL_TRANS 与 normal schema watermark。
5. 无论准备成功失败都 close；首错保留。成功时再次检查实际 session/事务身份。
6. complete_prepare 成功后封存，失败后仅允许完整 abort/discard；真正的数据提交
   仍由原同步/异步事务控制路径执行。

宿主接口是内部数据库效果边界，不是允许插件插入任意提交回调的新 SPI。Rust FFI
调用已返回后才执行 host SQL，不跨 SQL 持有 Rust 可变借用。DDL admission 的
EXCLUSIVE 锁排斥 Root parallel DDL，当前 caller 不另行启动并行 DDL 子任务。
准备失败时外层只回滚仍匹配原 ID/sequence base 的 descriptor，不访问已清空的
descriptor，也不回滚嵌套 SQL 换入的另一个事务。

已知 committed 结果先读取最终版本、finish(true)，随后单调推进 session 的
last_ddl_schema_version，并调用 `publish_plugin_catalog_commit`，先以原子 max
推进 schema service 的已提交插件版本，再投递既有后台队列。该入口不等待、
不执行 SQL；原 `async_refresh_schema` 是等待式接口，不能用于
数据完成回调。队列错误保留共享与 session fence 并记录日志，不将已提交数据伪装为回滚
或可重试的提交失败。未知结果不猜测目标版本、不推进 fence，只销毁私有日志。

2026-09-16 修正跨连接竞态：MySQL 普通请求、prepare/execute 等既有客户端
刷新入口使用 `refresh_schema_for_client`，在取得本语句 schema guard 前等待
`max(session last_ddl_schema_version, shared committed plugin version)`。
共享版本在提交响应前记录，所以独立连接不需要复制 writer 的会话变量。
入队成功仍不等于缓存已刷新；等待失败时新请求报错，不能继续以旧 schema 执行。
仅采样请求开始时的屏障，不追赶之后并发提交；内部 schema refresh SQL 不经过
该等待，避免自等待。service 销毁时重置共享版本。此屏障不等于所有内部 SPI/PL
独立入口均有跨请求保证；这些
可见性、缓存失效、队列拥塞及恢复场景仍在端到端验收范围内。未证明持久提交时
不能用一个可能永远不会发布的 provisional version 强制推进屏障。

## Session 生命周期接入

`prepare_plugin_catalog_view(barrier, schema, privileges, journal)` 是宿主写入准备
接口，不是插件 ABI。必须在真实活动、可写且非反序列化事务中调用；它不取得
对象权限或数据保存点。首次调用惰性创建事务级配对视图/日志，成功记录实际
barrier 的 pre-mutation mark 后才返回 shared owner；后续调用复用同一对视图。
失败清空三个输出。调用者仍需按普通 resolver/ACL、DDL 准入和 writer 协议执行
真实写入，并在当前 guard 上调用 bind 后才依赖私有对象查找，不能只修改视图。

`bind_plugin_catalog_view(guard)` 在事务身份与 Rust 日志状态仍有效时，将 session
持有的视图绑定到一个正常 runtime guard；已有相同视图时成功，不替换 foreign
overlay。`ObSql::init_exec_context` 在创建 plan context 前调用它，使通过此公共
路径的后续 SQL/PS/PL 初始化在缓存查找前得到私有视图。guard 仍持有自己的 base
snapshot，权限/缓存隔离沿用原路径。Preparing/Sealed 期间可读冻结视图，但不能
继续记录修改；日志失败或已结束，即使 descriptor 暂时仍活动也拒绝绑定。

回滚到首次使用之前会恢复空视图，但不更换 session 的 owner；数据库或已授权
创建者变化也不另建视图。reset/已知结果/未知结果先退休配对视图，再清理 session
ownership，下一事务创建新视图。退休是不可撤销的生命周期状态，不属于 savepoint
历史：旧 mark 仍能执行私有索引回滚，但不能恢复访问能力。schema/ACL 查询清空输出
并返回 `OB_STATE_NOT_MATCH`，新 staging、mark 和 guard 绑定/继承/捕获也拒绝。
外部 guard 和短期 host lease 可继续保留旧 backing storage，因此已借出的 schema
指针可供结果清理使用；它们不是后续查询的事务身份凭证。

MySQL 获取 session cached guard 时识别退休视图，即使 base schema 版本相同也重新
获取 runtime guard，避免回滚或未知结果后缓存旧私有对象/权限。其它内部入口如果
仍传入退休 guard，公共 SQL 初始化直接拒绝，调用者应取得新 guard。完成回调不
立即销毁 guard 或借出存储；整个调用/状态转换沿用 session 的独占访问协议。
此路径需要真实 MySQL 跨语句与并发测试，局部 fixture 不等于端到端验证。

`ObSQLSessionInfo::record_plugin_catalog_view` 只有在当前 session 确有活动事务时才
惰性分配参与者，绑定真实 transaction ID 和 sequence base；反序列化 session 拒绝
挂接。每次记录/恢复/完成都核对身份，不能将旧标记作用于复用的 descriptor。成员
布局不随 SQL target 私有插件宏变化；插件关闭时不链接 Rust adapter 实现。

具名 `ROLLBACK TO` 在有参与者时使用数据层返回 resolved barrier 的重载；普通
事务保留原路径。语句及显式 implicit-barrier 回滚都在数据层成功后通知日志。
视图恢复失败会尝试中止同一数据事务，成功后丢弃视图；中止失败则保留 poisoned
日志阻止提交。数据事务换代、session/reset_tx_variable 清理旧私有状态。

同步结束在调用 data commit/rollback 后通知真实结果。异步结束提交前先 seal；
callback 同时保留数据结果、原事务 ID 与客户端响应错误，不能用被 SQL 错误覆盖的
响应码猜测是否提交。MySQL 回调沿用现有 ARMED/交接/READY 状态机，在持有 query
lock、越过 worker 完成屏障后处理日志，再 reset 事务描述符。交接失败的 cleanup
路径也处理数据结果。提交结果未知只丢弃私有视图，不发布 schema 或声称持久回滚。

这接通视图参与者、公共 SQL 初始化绑定及提交准备/刷新交接，真正查询期创建/删除
还需把 catalog 写入桥接、身份与版本预留、当前 guard、schema 发布/失效接入同一
操作并完成真实事务验证。不能直接提交 Root
安装事务来替代查询调用者的事务，也不能把以上回调接线当成已验证的实库行为。

## 验证范围

事务版本新增 C ABI 回归覆盖 mark/版本混排、乱序预留版本的最大值、保存点恢复、
旧事务/非法参数、序号高水位、seal 后读/禁止写、finish 后拒绝、共享容量和 undo
poison。kernel fixture 使用真实 DDL SQL 生成、borrowed adapter 与 Rust journal，
验证缺失/拒绝 recorder、SQL 失败、错误 affected rows、记录失败、零版本预检、
顺序跨线程所有权交接及不改写各线程 TSI；原 client 仍更新 TSI。只有 transport
是受控替身，不证明真实 DB 事务写入/隔离，也不证明 session 提交保护的实库行为。

新增内核 fixture 覆盖嵌套标记、释放内层后回滚外层、重复使用、废弃分支、权限单独
修改的分支、错误配对、半份 staging 失败、base tombstone 恢复、同 ID ALTER 的
旧指针存活，以及反复回滚后的 schema 字节/权限历史容量。

真实 schema guard 权限用例在同名 DROP/CREATE 后验证新对象不继承原权限，再回滚
确认原身份、借用指针和 EXECUTE/ALTER 权限恢复。独立旧 overlay 用例继续检查
名称/ID/数据库/函数过程分离、历史指针和容量行为。执行结果见
[实施记录](plugin-implementation-status.md)。

新增 C++→Rust C ABI 日志测试验证所有权、序号、容量和错误语义；配对视图 kernel
用例验证同 barrier 的多条变更、反复回滚、早于首次插件使用的目标、同名替换后的
权限和旧指针恢复、commit/abort/析构清理，以及 ancestry 破坏后的 poison。
这些测试提供构造的数据 barrier，不启动数据事务或自行模拟 SQL 保存点名称栈。

这些用例使用生产 schema 对象、guard 和权限逻辑，但不是实库事务或外层 SQL 失败
后的 durable rollback 证明。整体目标仍包括完整 catalog 写入、深层 planner/index
扩展和 AI 资源/轻量化验证。

关联：[查询期查找](plugin-query-catalog.md)、[安装 builder](plugin-catalog-builder.md)、
[完整设计](plugin-next-design.md)。
