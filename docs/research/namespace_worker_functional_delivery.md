# Worker 功能交付：独立入口、原生协议、通用 DDL

> 2026-09-18 状态：本节是当前实现基线。后文保留的是 V19 的排障时间线，其中“尚未完成”等中间状态不再代表当前结论。

## 当前架构基线

- 每个 namespace 由一个完整 SQL Worker 承载。Worker 拥有网络入口、session、SQL、DDL、inner SQL、SchemaService 和 SQL 对象缓存，启动后固定绑定一个 namespace，执行期不切换 namespace。
- 共享进程承载事务、锁、日志、tablet、物理页缓存、B+Tree 和 COW。用户 SQL 不在共享进程执行；Worker 通过 typed storage IPC 调用共享存储。
- Worker 模式本身就禁止共享进程执行 SQL，不再依赖第二个 bootstrap 实验开关。共享后台服务调用 `GCTX.sql_proxy_` 时，`ObInnerSQLConnection` 将读、写和事务操作转发给已绑定 namespace 的 Worker，再由 Worker 经过原生 SQL 链路执行。
- Worker 激活时创建一个构造后不可变的 namespace Channel 绑定。普通存储帧不携带 namespace_id；共享端从 Channel 取得唯一作用域。`Exchange`、`SessionBinding` 和 `DirectStorageContext` 不再各保存一份可能不一致的 ID。
- SQL/DAS 与共享存储的边界使用 `StorageSpaceHandle`，显式区分 `NAMESPACE(id)` 与 `GLOBAL` 作用域。普通请求继承启动时绑定在 Channel 上的 namespace，只在访问全局表时发送一个 GLOBAL 作用域标记，不重复携带 namespace ID；共享入口只允许默认 namespace 的 Channel 使用 GLOBAL。当前用户数据路径只在入口适配层展开 namespace ID，事务、tablet 和 B+Tree 等深层接口继续使用物理对象 ID。
- 客户端目前直接连接 Worker 独立端口。一条用户 SQL 只在 Worker 内完成 SQL 处理，再按 DAS/事务操作访问共享存储，不经共享 SQL 入口往返转发。
- 已发布 Channel 异常断开时，共享进程向现有 server runtime 提交一次带 generation 校验的 Worker 恢复任务；新 Worker 先完成未发布 schema delta 的启动恢复，再更新 endpoint 表并开放端口。DROP 和共享进程正常停机先关闭自动恢复标志，不会把已删除的 namespace 重新拉起；这条路径不增加常驻后台线程。
- Worker 的原生连接上下文直接持有本地 session 引用；不对每个 packet 做全局 `session_id` 查找。
- namespace 中的用户表、索引、`all_*` 系统表和 `ddl_operation` 由该 Worker 的 native SchemaService 管理。不再使用一份额外的“namespace catalog”作为 schema 权威。
- `schema_version` 在 namespace 内独立增长。fork 时 child 继承 source 当时的 schema version，之后父子各自演进。
- 普通 DDL 在目标 Worker 内走原生 SQL/DDL 链路。旧的共享端 root-command SQL 执行路径已移除；共享端只接收存储及生命周期类型化操作。
- 系统包仍由共享启动流程装载，但“系统包已就绪”状态会通过 Worker bootstrap 和运行时广播同步。Worker 的 PL/存储过程不再等待一份进程私有、永远不会变为 true 的 `GCTX.sys_package_ready_`。
- 游标事务快照由 Worker 保存值、共享事务服务保存稳定的注册副本；每次 `FETCH` 前显式刷新失效/提交状态，`CLOSE` 和 PL 异常清理时显式注销。共享副本的数量因此只随当前打开且需要校验的游标增长，不随长连接历史累计。

## fork 与 DDL 一致性

- namespace 目录是不可变 B+Tree/COW 根。fork 复制 root/snapshot 引用，不枚举表、索引或 LOB tablet，数据量和表数不进入 fork 主路径复杂度。
- DDL 通过通用 `ObDDLSQLTransaction` 边界登记，不针对 `CREATE TABLE` / `CREATE INDEX` / `DROP INDEX` 分别打补丁。
- 持久化栅栏使用 `active_schema_changes` 和 `pending_schema_version`：前者表示 DDL 未提交，后者表示 DDL 已提交但 namespace 存储目录尚未发布。fork/drop 锁定 source 后只在两者均为零时继续。
- Worker 在 DDL 真正提交后发布 schema delta，成功后与目录变更在同一控制元数据事务中清除 pending version。进程在两步之间崩溃时，重启 Worker 会先完成全量 schema/目录对齐，再开放端口。
- 干净的 child 启动不做全量目录重建，因此保留 fork 时的同一根页。
- 原生 schema 模式的目录准入只验证实际存储能力，不再按自增列、生成列等 DDL 类型维护白名单。schema delta 命中已有 tablet 时保留原物理绑定，避免一次 `ALTER` 或恢复发布把继承 tablet 错标成本 namespace 已物化。
- 目录按 tablet 而非 table 登记。分区 schema 的全部 tablet ID 在 Worker/存储边界统一编解码，扫描和写入使用 DAS 请求携带的具体 tablet，因此 HASH 分区表可以跨多级 fork 按分区惰性物化。
- Worker 的 DROP 事务只修改本 namespace 的原生 schema，不再在事务中查询共享控制目录或删除物理 tablet。提交后的通用 schema delta 在共享存储端逐 tablet 判断所有权，只回收该 namespace 已经物化的物理 tablet；仍继承自父 namespace 的 tablet 只删除逻辑绑定。
- schema delta 传输整个 DDL 批次内所有变更表的前后 schema，并在一次目录根事务中计算完整的旧、新 tablet 所有权集合。新集合仍包含的 tablet 会完整保留原 `bound` 和快照 `cap`，即使它从一张表移动到另一张表；只有从整个新集合消失的本 namespace 私有 tablet 才进入物理回收。DROP、TRUNCATE、重分区和 `EXCHANGE PARTITION` 共用这一条 batch replacement 路径，child 的原生 DELETE_TABLET MDS 不再成为第二个物理清理者。
- 分区表、分区索引和分区 LOB 的主表/LOB meta/LOB piece 通过相同分区序号组成物化单元。首次访问某个分区只物化这一组 tablet，不会把整张表或其他分区提前复制到 child。
- child 对控制 schema 的隐藏规则统一放在 latest-schema guard 边界，避免常规 SchemaService、DDL latest guard 和显式表 ID 路径得到不同可见性。fork 后 `all_*` 中允许保留不可访问的冗余控制 schema 行；GLOBAL 表的实际 Tablet 始终由 gateway identity root 寻址，child 无法访问。

## 全局元数据边界

- 当前原型把 namespace 注册、endpoint、snapshot 和页目录放在 namespace 1 的 `__fork_proto_meta`。namespace 1 是可承载正常用户数据的默认 Worker，同时是管理入口，不能删除且可作为 fork source。
- child 的物理快照可以含有这些页，但 child SchemaService 不发布控制 schema，因此 `SHOW` 和显式访问都不可见，管理语句也在变更前被拒绝。
- 控制表的扫描和普通 DML 已显式选择 `StorageSpaceHandle::GLOBAL`；GLOBAL 请求沿用同一事务服务，但不进入 namespace 的逻辑到物理对象路由。当前分类仍在 Worker schema 边界根据原型控制数据库识别。
- 表锁、Tablet create/delete MDS、Tablet binding、Rootserver 物理 DDL、Range split、LOB 读取和 Tablet 自增缓存失效也使用同一 storage-space wire discriminator。表锁和 CREATE_TABLET MDS 在 Worker 的 schema 边界完成分类；共享事务、锁和 Tablet 服务只接收已经选定的作用域与物理对象。
- GLOBAL 已作为 storage gateway 的显式 identity root：它直接使用原生物理 Tablet，不经过 namespace 逻辑 ID 编码，也不参与 fork/COW。GLOBAL 没有快照语义，因此无需再复制一棵仅用于形式对称的 COW 目录；未来只有在全局元数据也需要快照时才增加相应 root。namespace 1 Worker 仍是 SQL 管理入口；共享存储深层只处理 tablet/page 句柄，不执行 SQL，也不解析 schema。
- namespace DROP 在控制 Worker 完成语义解析，但访问排空和兼容 schema holder 回收通过类型化 storage RPC 在共享进程执行。child Worker 停止时直接释放其 SchemaService；共享端 native 路径不建立第二份 schema 对象缓存。
- `ALTER SYSTEM` 先由共享进程验证并持久化，再通过已有多路复用 Channel 将动态参数广播到发起命令的默认 Worker 和所有已运行 child。Worker 动态覆盖项单独持久化在控制 SQLite，共享进程崩溃后也能在发布 endpoint 前向新 Worker 回放。静态参数只持久化并标记下次启动生效，不会制造“配置值已变但 runtime 尚未重定容”的假象。Worker session 的全局 debug-sync broadcaster 绑定到 `RemoteRootserverLocalRuntime`，物理同步点在共享进程执行。
- Worker 内存定容不再写死在子进程中。共享进程在 spawn 时通过启动帧传入 `namespace_sql_worker_memory_budget`，Worker 用该值定容分配器、KV cache 和 runtime；参数修改在 Worker 下次启动时生效。共享存储仍使用 `memory_budget`，两者是显式的独立配额。
- TLS 启动配置与 Worker 内存预算一样由共享进程通过 bootstrap 帧下发。子进程的 Rust NIO 直接读取实例 wallet 的绝对路径，不依赖 Worker 独立工作目录中的证书副本。TLS 开关与最低协议版本在 NIO 启动时生效，运行中 `ALTER SYSTEM` 将它们标记为 Worker 重启项，不会只改参数表而不改监听器。

## 2026-09-18 验收证据

- `source ~/.bashrc && make -j80 seekdb`：通过。
- 完整 direct suite：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_m7cbgu7v/data.tar.gz`。覆盖原生客户端协议、认证/权限、显式事务、断连回滚、取消、预处理/长参数、并行查询/DML、普通/唯一索引、LOB schema、通用 DDL、多 namespace 并发写、fork/drop 和 Worker 崩溃恢复；另覆盖分区索引、分区 out-row LOB、按分区惰性物化以及部分物化后的整表 DROP。
- 严格冷启动及崩溃恢复：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_2hrzjd_e/data.tar.gz`。两次均由 Worker 执行系统 inner SQL，共享 SQL 执行保持禁止。
- IPC/兼容入口完整矩阵：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13__r42alin/data.tar.gz`。覆盖多 session 隔离、慢查询、排队超时、取消、Worker 死亡唤醒、重启、慢客户端背压和资源释放。
- Channel 单一绑定收敛后，fork/恢复回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_0fl_haz9/data.tar.gz` 和同 session 多层 inner SQL/事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_yrln8rob/data.tar.gz` 通过。
- `StorageSpaceHandle` 收敛后的完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_vti5umb4/data.tar.gz` 与嵌套事务 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_6rr2ng0q/data.tar.gz` 通过。
- 自增列、虚拟生成列及多级 fork 的目录所有权修复通过完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_1ez1vcs_/data.tar.gz`；同 session 嵌套事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_jr_vjy3p/data.tar.gz` 通过。
- 四分区 HASH 表已通过建表、跨分区写入、两级 fork、child 全表扫描、父子分别更新/插入、COW 隔离及崩溃恢复：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_do6wu3dg/data.tar.gz`。
- 同 session 嵌套 SQL 与事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_wh4k4wf9/data.tar.gz` 通过：覆盖外键递归读取未提交数据、内层语句失败只回滚当前语句、多层级联与保存点、取消、断连及 Worker 死亡后的事务回滚。
- 部分物化的三分区表在 child 执行整表 TRUNCATE 后，child/二级 child 只看到新写入行，source 仍看到原三行；随后多 namespace 并发写、按逆序删除 namespace 和共享进程崩溃恢复均通过：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_fp5vi4s4/data.tar.gz`。
- 部分物化的三分区表在 child 在线重分区为五分区后，9 行数据及更新值保持正确；二级 child 继承五分区 schema，source 仍保持原三分区 schema 和数据。完整 direct suite 同时覆盖协议、权限、事务、并行查询/DML、索引、LOB、DDL/fork 栅栏、多 namespace 并发写、逆序删除和共享进程崩溃恢复：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_o2vcb_dl/data.tar.gz`。
- 显式 GLOBAL 扫描/DML 作用域接通后的完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_b2zrm9vu/data.tar.gz` 通过；默认 Worker 可读写控制元数据，child 仍无法解析或修改控制表。嵌套事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_nyr9qzxt/data.tar.gz` 同时通过。
- storage-space 继续收敛到锁、MDS、Tablet binding、物理 DDL、Range、LOB 和自增缓存路径后，完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19__f434jsw/data.tar.gz` 通过；其中新增 child `parallel(2)` 扫描，验证 fork 后 Range split 会按 child root 惰性物化并返回逻辑 range。嵌套事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_q249homx/data.tar.gz` 通过。
- DROP 生命周期进程边界修正后的完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_psqjrdhn/data.tar.gz` 通过。一个 child scan 在共享存储完成准入后被强制暂停，namespace 先进入关闭态，而 DROP 在 300 ms 内没有越过共享端 drain；释放 scan 后删除完成。3 个连续删除和崩溃恢复后的第 4 个删除均由共享进程完成访问排空；共享端回收前的 schema holder 计数全部为 0，验证 native SchemaService 路径没有在共享进程产生重复 schema 对象缓存，4 个 child Worker 均退出。
- session debug-sync 远程运行时与 Worker 配置刷新改造后，同 session 嵌套事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_8gvmkldg/data.tar.gz` 通过，覆盖外键递归读取未提交数据、语句级回滚、多层级联和保存点、取消、断连及 Worker 死亡后的事务回滚。
- Worker 资源启动帧改造后，冷启动/共享进程崩溃恢复 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_7uw_9gty/data.tar.gz` 与多 namespace fork/删除/恢复 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_dwhk70q9/data.tar.gz` 通过。测试将 `namespace_sql_worker_memory_budget` 设为 640 MiB，namespace 1/2/3/4/5 以及崩溃后重建的 namespace 5 Worker 均记录为 671088640 字节，证明预算来自共享进程配置而非子进程常量。同一用例还在 namespace 2/3/4/5 已运行时执行 `ALTER SYSTEM SET debug_sync_timeout='600s'`，4 个 child 的虚拟参数表均立即返回 `600s`，后续 DROP 访问排空时序仍通过。
- Worker 动态配置持久恢复回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_c3gvy7sh/data.tar.gz` 通过：运行中的 4 个 child 都立即观察到 `debug_sync_timeout=600s`；强制杀掉共享进程后，自动重建的 namespace 5 Worker 仍通过原生虚拟参数表返回 `600s`，同时数据、endpoint 目录、DROP 排空及全部 6 个 Worker 的 640 MiB 启动预算均通过。
- 配置持久改造后的完整直连矩阵 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_hah8_xfj/data.tar.gz` 通过，覆盖原生协议、权限、事务、通用 DDL/索引、分区、多 namespace 并发、DROP 排空及崩溃恢复。
- Worker TLS 回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_dhh0f492/data.tar.gz` 通过：namespace 1 与 fork 出的 child 均使用 wallet 完成证书验证，协商 `TLSv1.3 / TLS_AES_256_GCM_SHA384`，child 通过 TLS 读到 fork 前数据并正常删除。非 TLS 的严格冷启动及崩溃恢复 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_pfjj828l/data.tar.gz` 也通过。
- 强制 DDL/fork 竞态已验证：人为将 DDL 提交后的目录发布延迟 500 ms，fork 在 pending version 清零前不返回；child 能直接读写该 DDL 创建的表。
- 旧原型数据目录 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_6c_dprkn/data.tar.gz` 实测升级：启动时幂等增加两个栅栏列，18 行旧用户数据仍可读，重启后能从原 namespace 1 正常 fork 出 child。
- 系统包就绪同步及远端游标快照生命周期接通后，完整 direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_hoyafdk9/data.tar.gz` 通过。新增用例连续打开并正常关闭三个 `FOR UPDATE` 游标，每次共享注册数都回到 0；第四个游标在创建它之前的 savepoint 被回滚后，下一次 `FETCH` 返回 4138，PL 异常清理仍将共享注册数归零。namespace 2 创建的游标存储过程也由二级 child namespace 4 从相同 root 继承并成功调用，共享日志记录 child 的注册、刷新和最终注销。该归档同时覆盖多级 fork/COW、DDL、锁中断、DROP 排空、Worker 崩溃恢复和全部 Worker 内存预算。
- 同一构建的严格冷启动及共享进程崩溃恢复 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_jhq8enyj/data.tar.gz` 通过，两次启动都只由 Worker 执行系统 inner SQL，共享 SQL 入口保持禁止。嵌套 SQL/事务回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_4bad7n0o/data.tar.gz` 通过，覆盖外键递归、语句级回滚、保存点、取消、断连和 Worker 死亡回滚。
- Worker-only SQL 的第二个 bootstrap 开关已删除；冷启动和崩溃恢复仍必须出现 Worker inner-SQL 执行证据，且不得出现共享 SQL 执行拒绝日志。本次回归归档为 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_kpll5owc/data.tar.gz`；同一构建的完整 direct suite 为 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_gb3sgade/data.tar.gz`，嵌套 SQL/事务回归为 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_nested_session_v17_xnicoinl/data.tar.gz`。
- RANGE 分区维护回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_73pn9tij/data.tar.gz` 通过：child 对继承表先物化单分区，再执行 `ADD PARTITION` 和 `DROP PARTITION`；二级 child 继承新分区 schema 后再次增删分区。source、child 和二级 child 的分区列表与行集合各自隔离，共享进程崩溃后 endpoint 和数据恢复仍通过。这条路径继续使用通用前后 schema tablet 集合差分，未新增分区语句特判。
- DDL 批量目录替换和 Worker 故障恢复回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19__bcnddyx/data.tar.gz` 通过：child 对继承表执行 `RENAME TABLE` 后改回原名并读到已物化数据；一级 child 与二级 child 分别执行 `EXCHANGE PARTITION`，交换后的两张表行集合正确且 source 保持隔离。用例还在一组独立交换表的 native schema 已提交、目录尚未发布时 `SIGKILL` namespace 2 Worker；共享进程自动启动 generation 3 Worker，新进程在发布 endpoint 前完成整个双表 delta，交换数据正确且两个持久化栅栏归零。该实现保留继承目录项的物理 `bound` 与快照 `cap`，没有增加 rename 或 exchange 语句特判；同一归档还通过并发写、DDL/fork 栅栏、DROP 排空、共享进程崩溃恢复和 Worker 内存预算检查。

## 尚未完成的目标形态

- 共享存储的普通请求已由 Channel 绑定 `StorageSpaceHandle`；Gateway 将 namespace 逻辑对象转成编码后的物理 ID，事务、锁、Tablet 和 B+Tree 深层不传递独立 namespace 标量。显式 ID 仅保留在 fork/drop/启动等跨 namespace 生命周期操作及 Gateway 目录解析中。
- Worker 已有显式的每进程内存配额；动态资源治理与按整体节点预算自动分配尚未做。TLS 直连及 forked child 已在 Linux 通过功能验收，其他目标平台仍未做生产级验收。
- `ALTER SYSTEM` 已刷新所有已运行 Worker，并将 Worker 动态覆盖项单独持久化到控制 SQLite；后续启动的 child 和共享进程崩溃后重建的 Worker 都会回放它们。静态参数仍只在 Worker 重启时生效。
- 当前每 Worker 一个端口；统一公网入口、跨主机 Worker 及其传输尚未落地。
- 分区表的 CREATE/读写/fork/DROP/TRUNCATE、在线重分区、RANGE `ADD/DROP PARTITION`、二级分区维护、`EXCHANGE PARTITION`、分区 LOB 和分区索引已经覆盖；生产规模压力仍未验收。

## 历史排障记录

起点：`ae890cffe`（V18）。用户目标是完整实现以下 1～3；单独增加监听端口或通过少量 SQL 探针不代表完成。

## 1. 独立客户端入口与共享存储服务

- worker 暂时各自监听不同端口，复用原生 NIO、认证、协议处理器及结果发送。
- 共享存储可以独立接收和处理 worker 发起的请求，不依赖共享端先收到用户 SQL。
- 接通连接、会话、事务、扫描的创建、使用、关闭和异常清理。
- 验证两个 worker 并发读写、断连、超时、worker 死亡；严格禁止共享进程执行 SQL。
- 多端口是临时入口；未来入口变化不改变 SQL 执行与存储接口。

## 2. 原生协议、会话与权限

- 文本查询、二进制预处理、参数绑定、长参数、多结果、重置连接、切库、取消。
- 字符集、TLS、普通用户和权限校验。
- 复用原生协议及执行逻辑；在底层服务接通后移除对应原型限制。
- 验证 CLI、常见驱动、连接池复用，以及会话隔离、资源回收和认证失败。
- BEGIN、savepoint、原生内部 SQL 和同 session 嵌套执行继续回归。

## 3. 通用 DDL、表与普通索引

- 创建/删除数据库，创建/修改/删除表，创建/删除索引。
- 复合主键、普通/唯一索引、NULL、字符串、数字、日期时间和大字段。
- schema 版本、计划失效及 DDL 后立即访问。
- 从空目录启动，建库表及索引，读写事务与回滚，修改 schema，再重启验证恢复。
- 验证约束与索引一致性；管理回调发起的 SQL 仍在 worker 执行。

## 实现约束

- 先完成上述功能；本轮不做内存调优，不增加重复的进程级对象缓存。
- 存储接口传递值和受生命周期约束的句柄，不传递跨进程指针。
- 复用现有跨平台网络、线程和进程设施；本轮不宣称已完成远程 worker 或所有平台验收。
- 不把 namespace 全对象 fork 和跨平台部署的后续工作算入 1～3，也不把 1～3 缩成小探针。

## 进展与证据

- 源码确认：V18 的共享存储处理在 `Exchange::next()` 内，由共享端发起 SQL 的线程驱动。这是独立 worker 监听前必须拆除的依赖。
- 源码确认：原生 `ObSrvXlator` 已覆盖协议命令，原生 Rust NIO 支持独立端口；优先接通这条已有路径。
- 已把共享存储请求从 `Exchange::next()` 移到已有共享 runtime 的请求线程池。IPC reader 只提交任务；同一请求的存储操作串行，关闭时等待已提交任务结束再释放借用的 session 和扫描对象。
- `source ~/.bashrc && make -C build_release -j80 seekdb`：通过，日志 `/data/1/tmp/namespace-v19-dispatch-build.log`。
- 严格模式空目录 bootstrap、崩溃重启、系统表及配置虚拟表：通过，日志 `/data/1/tmp/namespace-v19-dispatch-bootstrap.log`，目录 `/tmp/namespace_fork_PROTOTYPE_bootstrap_v18_6vkovm54`。
- 同 session 嵌套 SQL、事务及 worker 死亡回滚：通过，日志 `/data/1/tmp/namespace-v19-dispatch-nested.log`，目录 `/tmp/namespace_fork_PROTOTYPE_nested_session_v17_92hpayij`。
- **1～3 尚未完成**：独立存储请求和原生监听已初步接通，协议权限、通用 DDL/索引及完整验收仍需继续。

### 独立入口推进记录（进行中）

- 分支：`codex/namespace-worker-direct-v19`。共享端调度拆分已提交为 `fa6d23e24`。
- 增加 worker 主动发起的存储路由：请求编号最高位区分方向，复用同一 IPC。每个直连连接拥有一个路由，沿用当前原型的 32 个路由上限；扩展并发准入仍待完成。
- 共享端 `DirectStorageContext` 持有该连接的原生事务/快照状态；`e` 完成一次请求并释放扫描，`v` 关闭连接并回滚剩余事务。worker 异常退出时将对象清理提交到已有共享请求线程池。
- 独立路由探针随严格 bootstrap 和重启通过：`SEEKDB_NAMESPACE_SQL_WORKER_DIRECT_PROBE=1`，日志 `/data/1/tmp/namespace-v19-direct-storage-bootstrap.log`，目录 `/tmp/namespace_fork_PROTOTYPE_bootstrap_v18_vr69t3fc`。归档内 worker 日志包含 `PROTOTYPE_V19_DIRECT_STORAGE_PROBE ns=1 ret=0`。
- 原生监听正在通过 `SEEKDB_NAMESPACE_SQL_WORKER_LISTEN=1` 接通。worker 初始化现有 runtime 请求调度器，复用 NIO、协议处理器、session manager 和 packet sender；连接绑定持有原生 session 指针，避免逐请求查 session 表。
- 原生直连测试：`tools/obtest/namespace_worker_direct_prototype.py`。最新通过握手、SELECT、BEGIN/ROLLBACK 状态、切库及 8 个连接的会话隔离；日志 `/data/1/tmp/namespace-v19-direct-ingress.log`，目录 `/tmp/namespace_fork_PROTOTYPE_direct_v19_l3o1ff7f`。这不代表目标 1～3 验收通过。
- 已定位并补齐两项原有完整启动路径的依赖：握手随机串初始化；共享端和 worker 的逻辑服务地址。后者通过启动帧 `B` 传递，与 worker 独立的客户端监听端口分开，防止原生事务状态把 worker 误判成事务路由转移节点。原始 IPC 测试入口也已发送启动帧。
- 编译时必须等待完成后才能编辑 `.ipp`；最新构建日志 `/data/1/tmp/namespace-v19-listener-build.log`。

仍需交付的完整内容：

1. 完成并验证原生直连入口（当前仍保留原共享客户端转发路径，需收口）；两个 namespace worker 并发读写与断连/取消/死亡；消除不合理的连接准入上限。
2. 接通真实用户、权限和系统变量元数据，不能把 worker 的 bootstrap schema 当成最终认证数据；预处理/长参数/多结果/重置连接/字符集/TLS/连接池回归。
3. `query::ObIRootCommandService` 的远程适配已实现，复用原有类型接口传输 DDL、安全管理及配置命令；当前仅接入 namespace 1。建库、建表已在原生直连探针通过；需继续完善 schema guard 版本与生命周期、计划失效、通用索引/DML/LOB，并解决 fork namespace 的 DDL 元数据更新，完成重启验收。
4. `RemoteTransactionService::submit_commit_tx` 已接通共享端原生 commit，完成后调用 worker 的原生回调。公共 packet sender 的借用引用释放和回调清理顺序已修复，`/data/1/tmp/namespace-v19-direct-native-matrix.log` 的 `direct_insert_committed` 验证原生自动提交成功；后续显式事务触发独立内部 session 的路由问题，仍在推进。

本轮故障定位证据：

- 冷启动建表最初查询了不存在的 namespace 注册表。旧 `check_ddl` 漏了其他注册钩子已有的就绪检查，现已补齐。
- 接着建表等待超时。`/data/1/tmp/namespace-v19-ddl-worker-stack.log` 确认阻塞于 `wait_local_schema_visible`：元数据来自共享端，刷新版本却读取 worker 的 bootstrap 版本。已在 schema 服务边界转发刷新/发布版本读取，`/data/1/tmp/namespace-v19-direct-schema-version.log` 包含 `direct_table_created`。
- 同一探针的 INSERT 在存储完成写入、提交前返回 -4018 并回滚。`/data/1/tmp/namespace-v19-session-lifetime-stack.log` 确认 session 节点仍存在，但引用数已低于生存基线：公共发送器清除连接指针后，清理错误递减了借用引用。
- BEGIN 后 UPDATE 触发优化器统计读取。`/data/1/tmp/namespace-v19-nested-route2-stack.log` 确认新建的内部 session 5 借用了用户 session 2 的存储路由，因已有活动事务而失败。已在原生 session 切换处加入 `StorageSessionScope`：相同 session 复用路由，独立 session 持有独立路由，执行/迭代/销毁期间切换并恢复调用者。构建 `/data/1/tmp/namespace-v19-inner-storage-scope-build.log` 进行中。
- `/data/1/tmp/namespace-v19-direct-inner-scope.log` 已通过建库建表、自动提交、显式事务/回滚/savepoint、8 客户端隔离、CLI、多结果、UTF-8、二进制预处理及 360 KB 长参数。尚未通过连接重置；不能把长参数的纯表达式验证等同于存储 LOB 支持。
- 连接重置的原生锁清理会创建 session ID 为 0 的临时 session，并先直接调用事务接口，再执行内部 SQL。已允许内部存储路由使用原生匿名 session ID，并在公共显式事务开始/结束边界使用同一 `StorageSessionScope`。构建 `/data/1/tmp/namespace-v19-internal-trans-scope-build.log` 通过。
- 原生直连完整探针通过：`/data/1/tmp/namespace-v19-direct-internal-trans.log`，包括连接重置清空用户变量、回滚未提交事务，以及重置后继续查询。
- 本轮回归通过：严格空目录 bootstrap 和崩溃重启 `/data/1/tmp/namespace-v19-native-bootstrap-regression.log`；同 session 嵌套 SQL、取消及 worker 死亡回滚 `/data/1/tmp/namespace-v19-native-nested-regression.log`；IPC 句柄复用、取消及并发 `/data/1/tmp/namespace-v19-native-handles-regression.log`。这些验证不代表真实权限、通用 DDL 或 namespace 多 worker 全部完成。

以上都是目标 1～3 的剩余工作，不是可省略的后续建议。内存优化、完整 namespace fork 对象覆盖和跨平台部署验收继续维持原边界。

### 统一元数据读取（进行中）

- 原生直连入口及前述回归已提交为 `b3f2949b6`。
- worker 的 schema guard 现在记录共享端的快照版本，表、库、用户及系统变量读取携带该版本。共享端使用原生历史版本接口，若返回版本不同则报 `OB_SCHEMA_EAGAIN`，避免混用版本；仍需 DDL 并发、历史版本及计划失效验收。
- 用户密码、锁定状态和系统变量已从共享端按 guard 生命周期读取，不新增进程级元数据缓存。
- 握手在 session 创建前读取全局 autocommit。公共元数据接口为没有 SQL session 的调用使用临时路由，覆盖原生握手和后台读取。当前为同步 RPC，后续入口性能验收需覆盖这一点。
- 构建 `/data/1/tmp/namespace-v19-metadata-no-session-build.log` 通过。`/data/1/tmp/namespace-v19-direct-metadata-no-session.log` 已通过原直连矩阵、真实用户创建、错误密码/不存在用户拒绝；随后在表级授权用户切库时报 1044，确认权限读取仍在使用 worker 启动时的本地权限表。
- 正在统一原生 `ObPrivMgr` 读取接口：共享端执行原生查找，worker 按 guard 生命周期持有返回值，原生权限判断及错误处理仍在 worker。权限用例覆盖只读授权、拒绝写入、撤权即时生效和列授权；通用 DDL 用例已补复合主键、decimal/date/NULL、普通/唯一索引和 schema 修改，尚未全部通过。
- `ObPrivMgr` 的 30 个底层读取接口已接入远程适配，原生权限聚合逻辑继续复用。构建 `/data/1/tmp/namespace-v19-privilege-reader-build2.log` 通过。
- `/data/1/tmp/namespace-v19-direct-privileges-ddl.log` 已通过真实用户表级只读授权、拒绝写入、已有连接撤权即时生效、列授权，以及直连断连回滚解锁、KILL QUERY 后连接复用、全局 autocommit 修改后的新连接握手。
- 通用表最初在 DECIMAL 读取时报 4016。`/data/1/tmp/namespace-v19-direct-column-diagnosis.log` 验证同一表字符串/date/NULL 均可读取，仅 DECIMAL 失败；扫描和重复键返回代码直接取原始列类型，遗漏精度与小数位。已改用原生读写计划的完整列描述，构建 `/data/1/tmp/namespace-v19-native-column-types-build.log` 通过，完整矩阵重新验证中。
- `/data/1/tmp/namespace-v19-direct-native-columns.log` 已通过复合主键、字符串、DECIMAL、DATE 和 NULL 读写，随后 CREATE INDEX 返回 -4002。
- 分阶段调试确认 CREATE INDEX 的参数、加锁、schema 生成及版本分配、schema 持久化和 tablet 创建均成功；索引任务插入因 trace ID 为空失败。`/data/1/tmp/namespace-v19-index-trace-stack.log` 显示任务已初始化但 trace 四个字均为 0。共享 runtime 的 `OB_TASK` 不会像 MySQL 请求自动建立 trace，上轮存储调度拆分遗漏了这项上下文。
- 公共 worker 存储请求现携带 trace ID，共享任务使用原生 `ObTraceIdGuard` 在调用期间恢复；没有 SQL trace 的后台元数据读取和本地清理建立独立 trace。修复覆盖两个请求方向，未修改原生索引任务规则。构建 `/data/1/tmp/namespace-v19-storage-trace-build.log` 通过。
- `/data/1/tmp/namespace-v19-direct-storage-trace.log` 原有协议、权限、事务和类型用例通过，CREATE INDEX 转为等待阶段超时。`/data/1/tmp/namespace-v19-index-progress-worker.log` 发现 IPC 线程均等待任务队列锁；嵌套任务的最后 session 引用可能在重新加锁后销毁，销毁又发起 RPC 并重入同一等待循环。现把嵌套任务销毁移至加锁之前，构建 `/data/1/tmp/namespace-v19-nested-owner-build.log` 通过；尚需长时间并发销毁回归。
- `/data/1/tmp/namespace-v19-index-progress2-probe.log` 确认索引任务已经持久化并进入状态 3（REDEFINITION），用户等待却立即报超时。worker 跳过完整 `ObServer::start()`，`stop_` 仍为构造时的 true；原生 DDL 等待认为服务已停机。worker 启动/停止现同步发布原生运行状态，构建 `/data/1/tmp/namespace-v19-serving-state-build.log` 通过。
- 修复运行状态后，原有直连矩阵仍通过；CREATE INDEX 进入原生回填而非立即超时。`/data/1/tmp/namespace-v19-index-backfill-sql.log` 捕获实际回填语句为带 `enable_parallel_dml` / `use_px` 的 INSERT … SELECT，内部写返回 -4006 并被原生任务重试，尚未解决。
- `/data/1/tmp/namespace-v19-backfill-phase-stack.log` 另外捕获到协作式嵌套任务在深层 SQL 栈上构造 `RequestWorker` 时栈溢出。公共任务执行入口现复用 `SMART_CALL_LARGE`，在必要时使用原生栈扩展；构建 `/data/1/tmp/namespace-v19-nested-stack-build.log` 通过，回填阶段继续定位中。
- 诊断时额外发现：引用不存在列触发原生外部符号查找后，worker 会等待永远未更新的 `sys_package_ready_`。这是运行时状态尚未统一的另一处，需通过已有本地管理/元数据服务接通，不能简单置 true；尚未修复。
- `/data/1/tmp/namespace-v19-backfill-filtered-stack.log` 将回填的 -4006 定位到原生 `ObPxCoordOp::inner_open`：worker 漏掉了中断管理和 PX 数据通道/调度依赖。已补齐原生 interrupt、shared timer、DTL、Dfc、PX pools、DTL intermediate result manager 的组合启动；构建 `/data/1/tmp/namespace-v19-px-runtime-timer-build.log` 通过。
- `/data/1/tmp/namespace-v19-direct-px-runtime.log` 继续通过原有协议、权限、事务和通用类型用例，CREATE INDEX 进入 PX 后返回 -4007。归档 worker 输出确认 `get_tx_exec_result`、`add_tx_exec_result`、`merge_tx_state` 仍是未接通的事务服务接口。
- 正在接通这组原生事务状态操作：PX 私有描述符按原生 shadow 语义解码和释放，worker 内复用原生描述符汇总方法，主事务的执行结果由共享端原生事务服务接收。编译及索引回归尚未完成，仍需检查 PX 线程的独立存储路由和写状态回传。
- 事务状态适配构建 `/data/1/tmp/namespace-v19-px-tx-state-build2.log` 通过，`/data/1/tmp/namespace-v19-ddl-tx-state.log` 已越过未支持接口，CREATE INDEX 改为 -4016。`/data/1/tmp/namespace-v19-px-sqc-stack.log` 和 `/data/1/tmp/namespace-v19-direct-insert2-stack.log` 定位到 `ObDirectInsertOrchestrator::start`，尚未进入实际 PX 扫描任务。原生 `ObIndependentDag::basic_init` 要求存储 DAG scheduler，worker 没有该服务；应把 DirectInsert 会话/写入接口接入共享存储，而非在 worker 启动另一套存储组件。
- 增加用户 `parallel(2)` 聚合查询回归，先独立收齐 PX 执行链路。`/data/1/tmp/namespace-v19-parallel-route-stack.log` 捕获到 DAS 范围估算服务指针为空导致 worker 崩溃；现通过原生 `ObIRangeService` 远程执行范围估算/切分，边界只传 tablet/range 值，返回 rowkey 使用调用方 allocator，未增加进程级对象缓存。构建 `/data/1/tmp/namespace-v19-range-service-build2.log` 通过。
- `/data/1/tmp/namespace-v19-parallel-task-stack.log` 随后定位到实际 PX 扫描缺少存储路由。扫描、事务和 DML 公共接口现按执行 session 绑定路由；首次使用 PX session 时，共享端通过原生 `acquire_tx(serialized)` 导入 shadow 描述符。清理 shadow 只释放副本，不回滚主事务；未绑定路由的存储请求直接返回错误，不发送无效请求标签。
- 构建 `/data/1/tmp/namespace-v19-px-session-routes-build.log` 通过。`/data/1/tmp/namespace-v19-parallel-session-routes.log` 已通过复合主键及类型读取、两路 PX 聚合（`direct_parallel_scan_verified`），随后 CREATE INDEX 仍在 DirectInsert 准备阶段失败。并行 DML 提交/回滚及完整原生客户端矩阵正在回归，DirectInsert 远程适配尚未实现。
- `/data/1/tmp/namespace-v19-direct-parallel-matrix.log` 已通过原有协议、权限、会话、事务与通用类型用例，以及 PX 查询、PX INSERT … SELECT 的显式回滚和自动提交；共享 SQL 拒绝计数为 0。矩阵仅在 CREATE INDEX 的 DirectInsert 准备阶段失败，仍不代表目标 1～3 完成。
- 冷启动通过，但 `/data/1/tmp/namespace-v19-px-bootstrap-regression.log` 的崩溃重启返回 `OB_SCHEMA_EAGAIN`。定向诊断 `/data/1/tmp/namespace-v19-restart-diagnosis.log` 对应共享日志显示请求版本 1、刷新版本 1，却取得持久化 baseline 版本：将当前 core 版本作为显式历史版本请求，触发了原生 baseline 提升。元数据和权限读取现统一通过 `catalog_schema_guard`：当前版本用原生无版本参数入口，历史版本保持原生历史入口，取得后仍严格比对版本；诊断日志已移除。构建及恢复回归进行中。
- 恢复修复构建 `/data/1/tmp/namespace-v19-recovery-catalog-guard-build.log` 通过。严格空目录 bootstrap 和崩溃重启 `/data/1/tmp/namespace-v19-px-bootstrap-regression2.log`、同 session 多层嵌套 SQL/取消/worker 死亡回滚 `/data/1/tmp/namespace-v19-px-nested-regression.log`、IPC 句柄与取消并发 `/data/1/tmp/namespace-v19-px-handles-regression.log` 全部通过。下一项仍是 DirectInsert 远程接口以及通用索引/DDL 验收；1～3 尚未完成。
- 上述元数据、权限和原生 PX 服务已提交为 `6a8d82c0b`。
- DirectInsert 现增加可绑定的原生/远程服务入口，session 结束通过虚函数释放所属实现。共享端保存原生回填 DAG、slice writer；worker 只持执行期句柄，通过原生 SQC/PX session 的已有路由调用，批数据按最多 32 行传输。不同 PX 路由能并发完成，最终释放排除活动调用；后台 DAG 引用已有共享 session 的生存期对象，没有新增进程级对象缓存。
- DirectInsert 初版构建 `/data/1/tmp/namespace-v19-direct-insert-service-build.log` 通过，`/data/1/tmp/namespace-v19-direct-insert-ddl.log` 的并行读写继续通过，但建索引导致 worker IPC 退出。源码确认新增回复类型未加入接收白名单，现改用已有通用存储回复类型；正在重新构建验收。诊断重跑另一次在 CREATE DATABASE 等待超时，`/data/1/tmp/namespace-v19-insert-preparation.log` 仅确认等待共享 root command，尚未定位，不能据此宣称通用 DDL 稳定。
- worker 已注册原生 DDL slice store，其调度信息持久化通过现有 inner SQL 完成，无需再加 RPC。构建 `/data/1/tmp/namespace-v19-native-slice-registration-build.log` 通过；`/data/1/tmp/namespace-v19-native-slice-ddl.log` 已完成普通/唯一索引回填及一致性、ALTER ADD COLUMN、DROP INDEX/TABLE，最终仅在 DROP DATABASE 被旧原型统一保护拦截。
- 数据库 DDL 保护已改为检查现有 namespace catalog 引用，保留编码 ID 和已登记数据库保护，调用者传播实际错误。补充登记前 ALTER/DROP 和登记后拒绝的回归；构建 `/data/1/tmp/namespace-v19-database-ddl-guard-build.log` 通过。该保护变更仍待完整验收，不能视为 namespace > 1 的通用 DDL 已实现。
- `/data/1/tmp/namespace-v19-database-ddl-native-matrix.log` 的协议、权限和生命周期通过，但 CREATE DATABASE 再次等待超时。双进程诊断 `/data/1/tmp/namespace-v19-create-db-snapshot-{shared,worker}.log` 捕获同类 GRANT 等待：用户命令持有 worker 的 `root_service_serial_mutex`，共享端发布 schema 等待内部查询；内部查询等待 metadata RPC 时，协作调度嵌入无关系统包 DDL，后者等待用户持有的锁，使已就绪的外层回复无法被消费。不是单一 DDL 语句缺少支持。
- 协作等待现仅选择同一调用标识的内部任务。共享端向内部连接/SQL 请求传递原生 trace，worker 将其作为本次调用标识跨存储回调保留，并在嵌套完成后恢复；普通语句可保留自身 trace。构建 `/data/1/tmp/namespace-v19-inner-call-chain-build.log` 进行中，随后须验证原完整矩阵、单执行线程 bootstrap 和嵌套事务。此时尚未宣称死锁消除或数据库保护验收通过。
- `31e8c1500`（tag `namespace-worker-v19-native-ddl`）保存 DirectInsert、slice store、数据库 DDL guard 和调用链 trace 改造；新帧类型修复避免与结果集表头冲突。其后 `2d9dd1302`（tag `namespace-worker-v19-authenticated-ingress`）把认证身份传入 worker，`ff729c529`（tag `namespace-worker-v19-user-privileges`）清除非 root 的默认 CRUD 权限。
- `/data/1/tmp/namespace-v19-posttag-rerun.log`、`namespace-v19-user-privileges-full-regression.log` 的完整直连矩阵通过；`namespace-v19-posttag-bootstrap.log` 和 `namespace-v19-auth-identity-fix-bootstrap.log` 的冷启动/崩溃恢复通过；`namespace-v19-posttag-nested.log`、`namespace-v19-posttag-handles.log` 的嵌套、死亡回滚及句柄回归通过。`namespace-v19-call-chain-concurrent-matrix.log` 的三个客户端并发建库/建表/读写/删库也通过。`namespace-v19-database-protection-regression.log` 确认登记前数据库可 ALTER/DROP、登记后仍受保护。
- `/data/1/tmp/namespace-v19-shared-user-ingress-permission.log` 验证共享入口真实只读用户可读且写入被拒绝。当前仍需把这一用例纳入正式脚本，并继续完成 namespace 多 worker 并发、LOB/TLS、路由容量、取消传播和 namespace > 1 完整 DDL 等目标 1～3 项。
- `db1d5b09b`（tag `namespace-worker-v19-routes-256`）把按需路由容量上限从 32 提至 256；`/data/1/tmp/namespace-v19-256-route-64-handles.log` 已通过 64 session 并发句柄/取消/复用回归。仍不是无界容量，也未宣称多 namespace worker 并发完成。
- `2d9dd1302`（tag `namespace-worker-v19-authenticated-ingress`）和 `ff729c529`（tag `namespace-worker-v19-user-privileges`）之后，外部用户开 session 会携带认证身份；内部 session 不携带该字段。root 保留 ALL，非 root 默认权限为 0，由远程权限服务授予实际权限。构建 `/data/1/tmp/namespace-v19-auth-permission-build.log` 和共享只读用户回归 `/data/1/tmp/namespace-v19-shared-user-ingress-permission.log` 通过。
