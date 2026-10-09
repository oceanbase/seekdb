# Worker 功能交付：独立入口、原生协议、通用 DDL

> 2026-09-24 状态：下文的 Worker 架构和排障记录是历史原型，已由单进程 namespace 实现取代。下方四条门禁命令运行当前单进程用例；历史 Worker 行为与旧 `--case` 选项不再可执行。

## 当前架构基线

- 每个 namespace 由一个完整 SQL Worker 承载。Worker 拥有网络入口、session、SQL、DDL、inner SQL、SchemaService 和 SQL 对象缓存，启动后固定绑定一个 namespace，执行期不切换 namespace。
- 共享进程承载事务、锁、日志、tablet、物理页缓存、B+Tree 和 COW。用户 SQL 不在共享进程执行；Worker 通过 typed storage IPC 调用共享存储。
- Worker 模式本身就禁止共享进程执行 SQL，不再依赖第二个 bootstrap 实验开关。共享后台服务调用 `GCTX.sql_proxy_` 时，`ObInnerSQLConnection` 将读、写和事务操作转发给已绑定 namespace 的 Worker，再由 Worker 经过原生 SQL 链路执行。
- Worker 激活时创建一个构造后不可变的 namespace Channel 绑定。普通存储帧不携带 namespace_id；共享端从 Channel 取得唯一作用域。`Exchange`、`SessionBinding` 和 `DirectStorageContext` 不再各保存一份可能不一致的 ID。
- SQL/DAS 与共享存储的边界使用 `StorageSpaceHandle`，显式区分 `NAMESPACE(id)` 与 `GLOBAL` 作用域。普通请求继承启动时绑定在 Channel 上的 namespace，只在访问全局表时发送一个 GLOBAL 作用域标记，不重复携带 namespace ID；共享入口只允许默认 namespace 的 Channel 使用 GLOBAL。当前用户数据路径只在入口适配层展开 namespace ID，事务、tablet 和 B+Tree 等深层接口继续使用物理对象 ID。
- 客户端连接 Worker 的 Unix socket，不再有独立 TCP 端口。Worker 恒定初始化 NIO runtime 并绑定 `<实例目录>/run/namespace-worker-<ns>-<generation>/run/sql.sock`，`mysql_port_mode=disabled` 关闭 TCP 监听。一条用户 SQL 只在 Worker 内完成 SQL 处理，再按 DAS/事务操作访问共享存储，不经共享 SQL 入口往返转发。UDS 直连同时保留为排障通道。
- Worker 就绪帧发布相对于实例目录的 endpoint 路径（`run/namespace-worker-<ns>-<generation>/run/sql.sock`），注册表列由 `port` 改为 `endpoint VARCHAR(512)`。绝对路径在深部署目录下会超过 AF_UNIX `sun_path` 上限（编码 namespace ID 有 7 位数字），相对路径约 45 字节与部署位置无关；共享进程（未来的代理）与实例同 cwd，可直接使用。
- 已发布 Channel 异常断开时，共享进程向现有 server runtime 提交一次带 generation 校验的 Worker 恢复任务；新 Worker 先完成未发布 schema delta 的启动恢复，再更新 endpoint 表并开放入口。session open 的懒路径可能先于恢复任务拉起替代 Worker，该路径无法在持锁期间发布 endpoint（注册表 GLOBAL 写要路由回本 namespace），恢复任务现在会认领这种"已拉起但未发布"的 generation 并补发注册表行（按 `published_generation` 判重）。DROP 和共享进程正常停机先关闭自动恢复标志，不会把已删除的 namespace 重新拉起；这条路径不增加常驻后台线程。
- Worker 的原生连接上下文直接持有本地 session 引用；不对每个 packet 做全局 `session_id` 查找。
- namespace 中的用户表、索引、`all_*` 系统表和 `ddl_operation` 由该 Worker 的 native SchemaService 管理。不再使用一份额外的“namespace catalog”作为 schema 权威。
- `schema_version` 在 namespace 内独立增长。fork 时 child 继承 source 当时的 schema version，之后父子各自演进。
- 普通 DDL 在目标 Worker 内走原生 SQL/DDL 链路。旧的共享端 root-command SQL 执行路径已移除；共享端只接收存储及生命周期类型化操作。
- 系统包仍由共享启动流程装载，但“系统包已就绪”状态会通过 Worker bootstrap 和运行时广播同步。Worker 的 PL/存储过程不再等待一份进程私有、永远不会变为 true 的 `GCTX.sys_package_ready_`。
- 游标事务快照由 Worker 保存值、共享事务服务保存稳定的注册副本；每次 `FETCH` 前显式刷新失效/提交状态，`CLOSE` 和 PL 异常清理时显式注销。共享副本的数量因此只随当前打开且需要校验的游标增长，不随长连接历史累计。

## fork 与 DDL 一致性

- namespace 目录是不可变 B+Tree/COW 根。fork 复制 root/snapshot 引用，不枚举表、索引或 LOB tablet，数据量和表数不进入 fork 主路径复杂度。
- DDL 通过通用 `ObDDLSQLTransaction` 边界登记，不针对 `CREATE TABLE` / `CREATE INDEX` / `DROP INDEX` 分别打补丁。

## fork 门禁测试（如何运行）

四个 prototype 套件构成合入门禁，全部在 `tools/obtest/` 下，用 `--binary` 指向待测二进制。统一前置：

```bash
source ~/.bashrc && cd build_release && CARGO_NET_OFFLINE=true make -j80 seekdb   # 产出 build_release/src/observer/seekdb
export SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/data/1/nijia.nj/test                       # 测试实例与证据归档根目录
```

```bash
# 1. 冷启动门禁：单进程启动、子空间登录和继承读取
python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb

# 2. SQL 门禁：事务、索引、二级 fork 和重启恢复
python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb --case full

# 3. 直连门禁：DDL、DML、分区、索引和 LOB
python3 tools/obtest/namespace_worker_direct_prototype.py --binary build_release/src/observer/seekdb --case full

# 4. TLS 变体：入口 TLS 端到端
python3 tools/obtest/namespace_worker_direct_prototype.py --binary build_release/src/observer/seekdb --case tls
```

- 通过判据：进程 exit 0 且输出含 `{"event": "PASS", ...}`；每个套件自动把实例数据打包为 `$SEEKDB_FORK_PROTOTYPE_TEST_ROOT/namespace_fork_PROTOTYPE_<套件>_<随机串>/data.tar.gz` 作为证据（文档中"四套件 PASS"后附的 tar.gz 路径即来源于此）。
- 排障：PROTOTYPE 打印在实例目录的 `log/seekdb.log`（不是 shared-stdout.log）；套件输出里的 `event` 行给出失败阶段。
- 注意：四个套件各自独立部署临时实例，串行跑即可；不要与手工实例（a0_measure 等）占用同一 TEST_ROOT 下的运行端口段同时压测。
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
- Worker 激活（spawn/bootstrap/健康探测）在持有 Channel 锁期间依赖 server runtime 线程池处理新 Worker 的存储帧。反向依赖已从根因上切断：Worker 对 `__all_*` 内部表的扫描和写入统一携带调用方已解析的逻辑 schema，共享端存储帧路径不走 SchemaService 懒加载——懒加载的 inner SQL 必须路由回正在激活的 Worker，会构成循环等待；未携带调用方 schema 的 scan/write 帧直接拒绝（`OB_NOT_SUPPORTED`），不再保留 encoded-id 存量回退。激活健康探测改用 sid-less internal 直连路由，跳过可能递归回 Worker 的默认变量装载。共享端 SchemaService 只服务于 ns1 catalog 读取等本地管理路径，Worker 数据路径不依赖它。

## 终态设计：共享入口薄路由（已确认方向）

> **2026-09-22 注意**：本节与下一节（代理下沉 ns1 Worker）属于 IPC 多进程路线，已被文末"2026-09-22 方向修订：单进程 + NamespaceRegistry"取代；保留作历史记录。

- 终态形态：共享进程 = 存储引擎 + fork 控制面 + 薄 TCP 路由器；Worker 是唯一 MySQL 端点。不再保留单体进程兼容。
- 登录方式：`root@分支名`（不带 `@` 默认 ns1）。分支名由登录者自己管理（fork 时给定，登录时按名字路由）。
- 2881 共享入口的代理流水线：`发 greeting →（TLS upgrade 钩子，v1 空实现）→ 读用户名路由 → 字节流代理到目标 Worker 的 UDS`。代理不懂协议内容：TLS 端到端、协议特性全通、无 session 影子状态。
- 代理向 Worker 发送 PROXY v2 头携带真实客户端 IP/端口（否则 Worker 侧 `user@host` 权限匹配全部变成 127.0.0.1）；v1 只带地址，TLV 前向兼容。
- 已否决：fd 移交（SCM_RIGHTS）。字节流代理在资源占用、扩展性、跨平台上更均衡；跨机留给 endpoint 字符串的 `tcp:` 扩展位。
- TLS：终态必须终结在边缘（MySQL 无 SNI，TLS 之后用户名不可路由）。v1 入口明文，设计上留三处坑位：PROXY v2 TLV、握手流水线中的 upgrade 钩子、greeting 能力位集中处理。
- 客户端数据通道第一版不依赖端口（无跨机）；内部控制/存储通道保持 fork 管道帧（stdin/stdout，slot 复用），不动。
- Windows：首选 AF_UNIX（Win10 1803+ 原生支持，与 socket 模型同构）；named pipe 只在"官方客户端直连 Worker"的可选场景才需要——mariadb-c-connect 在 Windows 仅支持 named pipe 的限制不影响两端都是我们自己代码的内部通道。

### 工作清单

- Step 1（已完成）：Worker 客户端接入点从随机 TCP 端口改为 Unix socket；就绪帧发布 endpoint 字符串；删除 `SEEKDB_NAMESPACE_SQL_WORKER_LISTEN` 开关；UDS 直连保留为排障通道。
- Step 2（已完成）：2881 入口 `root@分支名` 路由 + 字节流代理 + PROXY v2 头 + TLS upgrade 空钩子。
- Step 3（已完成）：删帧转发层。共享端：删 `namespace_worker_query_prototype.ipp`（COM_QUERY/COM_INIT_DB 转发）、`obmp_connect` 的 `__fork_ns_` 登录选址与 `open_session`/`namespace_worker_id_` 挂载、转发连接的 COM 白名单、gateway `query()`；`ObSMConnection::namespace_worker_id_` 字段删除（`namespace_worker_binding_` 保留，Worker 侧直连存储会话仍在用）。Worker 侧：删 `'Q'/'U'` 帧分发与 `execute` lambda、`namespace_worker_sql_request_prototype.ipp` 整文件（`check_worker_sql`/`check_worker_plan`/`WorkerPacketSender`）。保留 inner SQL 通道（`'I'/'a'/'C'` 帧 + `inner_call/inner_read`，控制面与共享端 inner SQL 在用）；`__fork_ns_` 限定名在 schema 解析层的拒绝语义保留（`root@b` 下跨 namespace 引用仍 1146）。
- Step 4（已完成）：去兼容代码。`NamespaceForkKernelPrototype` 五级开关（enabled/namespace_mode/lifetime_mode/lineage_mode/metadata_gc_mode）与 `namespace_worker_prototype::enabled()` 全部恒定化，`SEEKDB_NAMESPACE_FORK_PROTOTYPE`/`SEEKDB_NAMESPACE_SQL_WORKER_PROTOTYPE` 环境开关删除（保留 `SEEKDB_NAMESPACE_SQL_WORKER_THREADS` 并发调优与 `SEEKDB_NAMESPACE_SQL_WORKER_DIRECT_PROBE` 开发探针两个运行时旋钮）。`check_sql_execution_role` 固化为 `!worker_process` 即拒。level 1/2 旧实验 `NamespaceForkPrototype` 整体删除（类、头文件、`fork_database_prototype_`、vanilla fork 死代码、fork_table 里的 prototype 快照旁路）。kernel 文件内部仍有约 50 处 `namespace_mode() ?` 风格三元表达式由常量折叠消除，行为不变，文本清扫留作后续。

## 2026-09-21 验收证据

- 大 schema 实测（demo 实例 12881，main 库 3000 表、每表 1 索引 ≈ 6000 schema 对象）：`FORK DATABASE main TO big` 0.27s（O(1) 确认，不枚举表）；分支首次登录 35.8s（触发该 namespace 的全量 schema 装载，约 360 条系统表 inner SQL 逐条经 IPC 走 COW/快照版本读，单条 50-200ms）；二次登录 31ms；稳态查询正常。对照：ns1 Worker 崩溃重生+首次登录合计 4.3s（同 schema 规模但读最新版本，无快照链遍历）。结论：fork 本身与表数无关；首次登录 O(schema 对象数) 且分支因快照/COW 读有单查询放大。优化方向：fork 后异步预装载/物化分支 schema 系统表、全量刷新管线并行化、schema 快照读路径加速。另测得原型 CREATE TABLE 约 120ms/张（3000 张 368s），是 DDL 串行化开销而非 fork 路径。
- Step 4 落地（去兼容代码）：IPC 矩阵三连过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13_{79i2cmoa,r0usk0we,pkf2fv9w}/data.tar.gz`、direct suite `--case full` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_gru33ixs/data.tar.gz`（或 n9ve8fct，两轮均过）、`--case tls` 同批通过、bootstrap 回归两轮通过（`namespace_fork_PROTOTYPE_bootstrap_v18_qluib7mb`、`yywosfnj`）。
- 解析超时抖动已根因修复：共享端 `StorageDispatch::Processor::run`（代理 resolve、channel 存储服务共用）复用 runtime 线程时不重置 `THIS_WORKER` 的 timeout_ts，若该线程此前跑过带截止时间的请求处理器，残留值已过期，inner SQL 期限计算 `min(残留值, now+query_timeout)` 取到过去时间 → Exchange 立即判超时并发送 'Z' 取消 → Worker 侧扫描以 -5065 QUERY_INTERRUPTED 中止 → 代理解析报 `namespace worker unavailable (err=-4012)`。修复：dispatch 任务入口统一 `THIS_WORKER.set_timeout_ts(INT64_MAX)`（与 Worker 侧 `before_process` 同款）。修复前该抖动在 Step 3/4 二进制上各复现一次，修复后 IPC 矩阵 3/3 通过。

## 2026-09-20 验收证据
- Step 3 落地（删帧转发层）：四套件通过——IPC 矩阵 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13_54szewxm/data.tar.gz`、direct suite `--case full` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_ofiza4w_/data.tar.gz`、`--case tls` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_9f652jrz/data.tar.gz`、bootstrap 回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_kifee9rj/data.tar.gz`。手动冒烟（fork b→b2、`root@b2` 读写、ns 间隔离、`__fork_ns_` 跨 namespace 引用保持 1146、CONNECTION_ID 经 PROXY v2 TLV 透传）正常。注意：本轮 IPC 矩阵首次运行复现了已记录的解析超时抖动（fork c 后第二次连接分支 b 的 resolve 报 -4012，ns1 Worker 日志中对应 inner SQL 读被 QUERY_INTERRUPTED 取消，疑似共享端 StorageDispatch 任务线程携带过期 timeout_ts 导致期限被立即判超时；重跑即通过，与 Step 3 删除无关，根因待查）。
- Step 2 落地：2881 入口 `root@分支名` 路由 + 字节流代理 + PROXY v2 头（携带真实客户端地址与连接号 TLV）。无 `@` 默认 ns1；未知分支返回 1049 `Unknown namespace`；Worker 崩溃后下一次连接自动触发重生（新 generation/pid），旧连接被拒。IPC 矩阵 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13_ou_rjekx/data.tar.gz`、direct suite `--case full` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_11t3fun0/data.tar.gz`、`--case tls` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_1_dqrwk0/data.tar.gz`、bootstrap 回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_he220qpj/data.tar.gz` 全部通过。
- UPDATE IGNORE 丢行修复：共享端 `ObWriteContext`（StoreCtxGuard）跨 batch 持有，写状态只在语句结束释放时才 merge 进 tx 描述符，导致 savepoint 回滚时 tx 仍是 IDLE，`rollback_to_global_implicit_savepoint_` 走 IDLE 分支只释放 savepoint 不做 undo，冲突行的 DELETE 被静默保留。修复对齐 vanilla 时序：处理隐式 savepoint 回滚（'B'）前先释放该会话所有打开的写上下文（`revert_store_ctx` 自然 merge 写状态，tx 转 IMPLICIT_ACTIVE），下一次写 batch 懒重 acquire。vanilla 本来就是每行写完释放 store ctx 再回滚，此改动只是把 IPC 模型拉回同一时序。修复后 ns1/分支上 `UPDATE IGNORE`（单行/多行链式冲突）、`INSERT IGNORE`、`REPLACE`、`ON DUPLICATE KEY UPDATE` 全部正确。
- DML 语义面扫描：23 组场景（INSERT/IGNORE/ON DUPLICATE/REPLACE/UPDATE IGNORE/多表 UPDATE/多表 DELETE/显式 savepoint/事务内 IGNORE/FOR UPDATE/INSERT...SELECT/auto_increment 冲突/批量冲突）同一脚本分别在 vanilla（14444）、ns1 Worker、分支 Worker（`root@b`）执行，输出逐字节对比：数据、行数、错误码全部一致。仅两处消息保真差异（非数据语义）：dup-key 错误文本缺 `Duplicate entry 'x' for key 'y'` 明细（LOG_USER_ERROR 明细写在共享进程线程本地 buffer，未随 IPC 回传）；`DROP TABLE IF EXISTS` 不存在表时缺 1051 note（Worker 本地 DDL 警告路径）。两者已记录为已知差异，不影响 Step 3。
- 已知抖动：IPC 矩阵曾观察到一次 Worker 被杀后重生延迟超过 60s 解析超时（respawn 的 gen-N Worker 卡在 bootstrap 本地步骤，同环境重跑及后续两轮均秒级恢复），暂未复现，保持观察。
- Worker 客户端入口改为 Unix socket 后：direct suite `--case full` `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_s3zng7cd/data.tar.gz`、`--case tls`（UDS 上 TLSv1.3 直连验证）`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_rnrz9osz/data.tar.gz`、bootstrap 回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_7_uwlz0w/data.tar.gz`、IPC 矩阵 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13_5ttywd5o/data.tar.gz` 全部通过。IPC 矩阵的 fd 断言更新为：Worker 只允许持有自己的 UDS listen socket，其余 `socket:`/storage fd 仍视为泄漏；Worker 私有内存约 34MB，线程数 19（含 1 个 NIO io 线程）。
- `source ~/.bashrc && make -j80 seekdb`：通过。
- 反向依赖根因修复后，进一步移除激活期线程名 fail-fast 启发式，并把共享端 scan/write 的 SchemaService 回退改为硬拒绝（`OB_NOT_SUPPORTED`）：direct suite `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_29srmc01/data.tar.gz`（ns1 SIGKILL 恢复 0.672s，全程未触发硬拒绝标记）、bootstrap 回归 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_ayoq6kmq/data.tar.gz`、IPC 矩阵 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13__9gcdik_/data.tar.gz` 全部通过。
- 默认 Worker（ns1）崩溃恢复死锁修复后的完整 direct suite 两次通过：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19__662wht_/data.tar.gz`、`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_8jtwrj3w/data.tar.gz`。ns1 Worker SIGKILL 后自动恢复从原先超 90 秒卡死降为约 0.52 秒完成，endpoint 重新发布、全局目录重发布与生命周期命令恢复均验证通过；重启后 endpoint 恢复（`endpoint_recovery`）同时通过。
- 严格冷启动及崩溃恢复回归：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_jyk_3p4c/data.tar.gz`（`cold_bootstrap`、`shared_sql_forbidden`、`recovery` 全通过）。

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
- 统一公网入口已落地（2881 `root@分支名` 路由 + 字节流代理）；Worker 另有 UDS 直连作为排障通道。跨主机 Worker 及其传输尚未落地（endpoint 字符串预留 `tcp:` 扩展位）。入口 TLS 终结留有三处坑位但 v1 为明文。
- 入口消息保真两处已知差异：dup-key 错误文本缺行键明细（共享端线程本地 LOG_USER_ERROR 未回传）、`DROP TABLE IF EXISTS` 缺 1051 note。
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
- 2026-09-21 记录（暂不做）：fork_clean3 实例 `__fork_proto_meta.pages` 实测 38503 行，手动 150 轮 `FORK DATABASE __gc__ TO __gc__` 后剩 2669 行，93% 为不可达死页。成因：FANOUT=8 下 6000 对象树高 4~5，每次目录登记 COW 路径拷贝产生 ~5 新页/废 ~5 旧页（big 库 6000 次登记 ≈ 3 万废页），且 GC 仅手动触发、单次上限 256。待办：1) GC 自动化（周期任务或按垃圾量自适应，调大单次上限）；2) FANOUT 调大（如 128，树高降到 2~3）；3) 根源解法是 directory 例外表化（只记本地物化 tablet + 父指针），稳态登记量趋零后垃圾问题自然消失。
- 2026-09-21 架构讨论结论与 TODO（按优先级）：
  1. 【性能，下一步】schema 快照包：worker 冷启动刷新改为共享端按 fork 版本点查 __all_* 历史表、原始字节批量直发 worker（共享端不反序列化、不建 schema_service），worker 装底后走既有增量追平。目标刷新 5.4s→~0.5s，端到端 fork+首次登录 ~13s→~6s。顺带解决 b1 实测"首次刷新 55k 条串行点查阻塞 CREATE TABLE 126.8s 并最终报错"的 DDL 饿死问题（需回归验证）。
  2. 【性能】tablet 读路径 COW 重定向：check_read_allowed_ 摘掉 ensure_tablet，继承 tablet 读直接打开 bound 源 tablet 按 fork 快照读，物化推迟到首次写。首次登录物化 ~4s→0s。风险：fork 链读放大（需链深控制/压实策略）；源快照钉住已由 V6/V7 快照保护覆盖。
  3. 【已否决】worker 预热池不做。spawn+激活 ~3.2s 的优化路径另行考虑（如模块裁剪）。
  4. 【架构收敛，性能达标后做】directory 例外表化（只记未物化 tablet→源 + 父指针，物化删条目，本地 tablet 走 NamespaceObjectKey 纯函数）+ ns1 tablet 纳入编码公式 + catalog 树退役 + entry 瘦身（删 object/table_id 冗余字段）。收益：消灭 pages 垃圾根源（实测 38503 行中 93% 为死页）、DROP/GC 走查范围缩小、共享存储只感知 tablet。风险最高（改元数据格式+DDL/DROP/GC 四条路径），须在 1、2 稳定后进行。
  5. 【运维债，随 4 或提前】fork 元数据 GC 自动化（现仅手动 FORK DATABASE __gc__ TO __gc__ 触发、单次上限 256 页）+ FANOUT 8→128（树高 4~5→2~3，登记 churn 减半）。
  6. 【待修】big16 首次登录挂起（refresh 完成后代理错过 worker 就绪唤醒）；12881 实例重启验证 tablet_table_cache 修复。
- 2026-09-21 方向修正：快照包（eager 批量导出）不是终态——vanilla 单体基线刷新 6.7s 说明逐表反序列化/构建才是大头（IPC 只是部分），eager 方案省不掉组装；且"全量预装 6000 张"与 fork 的 COW 哲学冲突（分支典型只碰几张表）。schema 侧终态改为【按需加载】：worker 启动只记基线版本 V，首次访问表 T 按 (table_id, ≤V) 点查装配单张，固定版本天然保证快照一致；快照包降级为可选的后台流式预取。落地前先给刷新路径加"查询段 vs 组装段"分段计时 profile 验证。执行顺序调整为：1) 读路径 COW 重定向（进行中）；2) 刷新分段 profile → schema 按需加载；3) 元数据层收敛（catalog 树退役 + directory 例外表化 + ns1 编码统一 + entry 瘦身 + GC 自动化 + FANOUT 调大）。
- 2026-09-21 读路径 COW 重定向已落地：`check_read_allowed_` 改走新增的 `NamespaceForkKernelPrototype::resolve_read_tablet`（本地已物化→自查；未物化继承→directory 查 bound 源 tablet + cap 快照，读快照钳制到 cap），读不再触发物化；写路径 ensure_tablet 不变。新增 tablet_redirect_cache（共享进程内，物化提交时同步失效）消除每次 open 的目录查询。fork_clean3 实测：新分支首次登录读到 fork 前全部数据、看不到 main fork 后的写入、分支写后物化且对 main 不可见；二次登录 43ms。回归：bootstrap / sql_worker full / direct full / direct tls 四套件 PASS。首次登录仍 ~11.4s，瓶颈回到刷新 NLJ（6.6s）与 spawn（~3s），待 schema 按需加载解决。
- 2026-09-21 例外表化实现设计定稿（P0~P2 一次切换，prototype 不做老实例迁移）：
  1. 数据模型：namespaces 加 parent_namespace/fork_cap 两列（fork 时写入，不可变，main=(0,0)）；新表 __fork_proto_meta.exceptions(namespace_id,tablet_id,table_id,kind,drop_scn, PK(ns,tablet))，kind 0=owned 1=tombstone。owned 行与物理 tablet 同事务维护。
  2. 核心洞察：读/物化路径的"源在哪"不需要目录树——物理 tablet id 是 (ns,local) 纯函数，沿父链逐层探测 tablet manager 即可（encode(1,t)=t 恒等，链到 ns1 自然终止）；例外表只服务 tombstone、DROP 清单、GC 保留判断。cap=min(沿途 fork_cap)。
  3. 读路径 resolve_read_tablet：物理探测本层→命中直达；owned 行在但物理未就绪→返回 self 等 transient（保持原语义）；tombstone→OB_TABLET_NOT_EXIST；否则沿链探测 + cap 累积。tablet_redirect_cache 删除（父链不可变→永久缓存；例外集合懒加载+同事务同步更新）。
  4. 物化 ensure_tablet_impl：链走查找 (source,cap)，snapshots 点查 snapshot_id=cap 得 schema_version（替代 lineage 逐跳）；建 tablet 不变；同事务 INSERT owned 行；不再写 directory/save_roots。
  5. DDL delta（publish_schema_delta）：replace_namespace_directory 重写为 exceptions diff——removed: owned→物理 drop+删行+tombstone，inherited→tombstone；added/moved→REPLACE owned 行。collect_directory_tablets 的 diff 逻辑复用。
  6. DROP ns：begin 时存在 state∈(0,1) 子 ns 则 OB_OP_NOT_ALLOW（语义变化：原模型允许+快照保留，v1 粗粒度阻断，链压实后解除）；lock 枚举 owned 行；finish 删例外行。父 DROP TABLE 的物理 tablet 在有 LIVE 后代期间保留（与原快照保护同构的粗粒度）。
  7. GC protect_snapshot_tablets 重写：encoded 候选 X(属 ns0)：∃LIVE 后代链过 ns0 且无更近 owned/tombstone→保留；raw 候选 L：∃LIVE ns 无 tombstone(L) 且链上无 owned(L)→保留。
  8. 退役：directory/catalog 树全部写入（bootstrap enrollment、observe/forget native 路径变 no-op）、tablet_redirect_cache。保留：pages/roots/snapshots 表结构（兼容）、namespace_state/tablet_table 缓存、collect_metadata（老实例垃圾清理）。
  9. Worker 侧零改动（协议不变）。验证：四套件回归 + fork_clean3 重建实测（fork O(1)、两级链读写隔离、cap 钳制、pages 不再膨胀、DROP）。
- 2026-09-21 例外表化已落地并四套件 PASS（bootstrap / sql_worker full / direct full / direct tls）。实施要点与实测：
  1. 实现：chain_links 父链缓存（不可变、永久）+ ExceptionSet 例外集合缓存（懒加载+同事务同步更新+单互斥锁）；`probe_physical_tablet`/`resolve_inherited_tablet`（沿父链探测，cap 累积钳制，encode(1,x)=x 恒等终止）；`resolve_read_tablet`/`ensure_tablet_impl`/`control_namespace`/`replace_namespace_exceptions`/DROP 三件套全部重写；bootstrap 不再逐表登记；native 侧 `observe/forget_schema_in_namespace` 在 native authority 下早退。
  2. 兼容代码清除：`ensure_control_column`（SHOW COLUMNS + ALTER 加列）连函数带 4 处调用整体删除，控制表列只由 CREATE TABLE 定义；prototype 不做老实例迁移，老实例需重建。顺带修复了 ALTER 兜底与 CREATE TABLE 之间 `fork_cap` 符号性不一致（signed BIGINT vs BIGINT UNSIGNED 会在 worker 回传结果集触发 -4001 OB_OBJ_TYPE_ERROR）。
  3. 修复的回归：a) 控制表 ALTER 触发 global tablet 锁路由 -4002 → 列并入 CREATE TABLE + 接收端显式 tablet 路由加 `storage_space.is_global()` 分支；b) `namespace_chain_link` 对 UNSIGNED 列 get_int → get_uint；c) 【关键】`replace_namespace_exceptions` 的 added 分支无条件 REPLACE owned 行：worker 重启恢复性 delta 会把"仅继承、本地无物理 tablet"的表误标 owned，读路径走"owned 但物理未就绪=transient"分支导致 tablet open 死循环 -4725 → 加 `probe_physical_tablet` 门控，物理存在才写 owned 行，否则保持链式继承（安全性：物理出现后读路径探测自动本地服务）。
  4. 实测（exc_measure 实例，big 库 501 表 + 8 分区表，三级链 b1→b2→b3）：fork 语句 ~0.3s（O(1)，pages 全程 0 行，不再逐表登记）；worker 激活+首查询 3.2s（瓶颈=schema 加载，下一阶段按需加载解决）；首次读继承表 0.19s（读重定向 + 顺带物化，每请求最多 3 个 tablet），二次读 0.03s；两级链读写隔离 ✓；cap 钳制 ✓（父新写入对子/孙不可见，b2 新建表对 b3 不可见）；tombstone 三级语义 ✓（b2 DROP t5：b2 1146、b1 正常、b3 因 fork 早于 drop 仍可见，靠 tombstone 的 drop_scn 与 fork_cap 比较实现）；DROP 含子 ns 拒绝 4179 ✓；权限继承 ✓（b1 CREATE USER alice 后 fork b6，b6 的 mysql.user 含 alice——系统表与用户表走同一套继承+COW）。
  5. 语义澄清（修正两处此前误读）：a) worker 激活期的 ~74-121 条 inner owned tablet 是激活过程对 __all_core_table 等核心内部表的**内部写入触发的 COW 物化**，内容继承自父，不是全新自举空表——系统表数据语义上与用户表无差别；b) "读不再触发物化"的表述过于绝对——**扫描数据路径**（`check_read_allowed_`→`resolve_read_tablet`）确实不物化（未物化即重定向祖先+cap 钳制），但**查询计划打开的 range service 路径**（范围估算/切分）会捎带 materialization_schemas（主表+LOB 辅助表，≤3）同步 ensure_tablet 物化，故 SQL 查询一张继承表整体上仍会物化。这是 PX/range service 落地时的务实选择（range split 需要本地 tablet 句柄，未接入重定向），代价是首次查询继承表付一次物化、例外表随"读过的表"增长；若目标是纯读不物化，需让 range service 也走重定向（待决策）。
  6. 遗留：V24 调试插桩待清理（`PROTOTYPE_V24_LOCK_FAIL`/`_GLOBAL_SPACE`/`_RESOLVE_FAIL`）；b1 内 CREATE DATABASE+CREATE TABLE 卡 2m15s（首次刷新 55k 条串行点查阻塞 DDL）待 schema 懒加载回归；目录树残留删除、pages/roots 老垃圾清理、链深压实、GC 自动化。
- 2026-09-22 读路径全面重定向化已落地并四套件 PASS（direct full / direct tls / bootstrap / sql_worker full）。上节"range service 是否走重定向"的待决策项已拍板落地——读不物化：
  1. 设计：物化严格只在写路径发生，owned 语义 = "本 ns 写过"。依据：worker 无本地存储，读祖先 tablet 与读本 ns tablet 在共享进程内成本相同，读物化零收益只有代价（多一次建 tablet、例外表随"读过的表"膨胀）。
  2. 实现：去掉 scan `'O'` 帧与 range `'G'` 帧的捎带物化——发送端（`namespace_worker_scan_prototype.ipp`/`namespace_worker_range_prototype.ipp`）不再组 `worker_materialization_schemas` 与帧字段；接收端（`EngineScan::open`/`process_ranges`）删除 materialization 解析与 `ensure_tablet` 调用，range 接收端改为 `resolve_read_tablet` 重定向到祖先 tablet（range cost/split 是 rowkey 级，祖先结果对子 ns 有效）。写路径（`namespace_worker_write_prototype.ipp`）物化保留不动。
  3. 【关键 bug】`resolve_read_tablet` 重定向后 ctx/tablet_handle 指向祖先 tablet，但 `param.tablet_id_` 仍是编码 id，迭代器 fetch 拿编码 id 访问存储 → -4725 死循环。修复：`ob_access_service.cpp` 的 `check_read_allowed_` 增加出参 `resolved_tablet_id`，4 个调用点（`table_scan`/`table_rescan`/`scan_block_stat`/`do_table_scan_`）成功后回写 `param.tablet_id_`；cap 钳制仍在 `check_read_allowed_` 内基于 redirect_cap 完成，语义不变。
  4. 根因再修正（推翻上节 5a 的判读）：worker 激活期 owned 的 74~121 条 inner tablet 不是"内部写入触发的 COW 物化"，而是 schema 刷新读 `__all_*_history` 多版本历史表时经 scan/range 帧捎带物化的副作用。本轮去掉捎带物化后该现象消失，系统表与用户表语义完全一致：读继承永不物化，写才物化。
  5. 实测（exc_measure 实例，b9/ns 1000002）：worker 激活+首查询 1.6s（上轮 3.2s）；例外表仅 1 条 owned（实际写入的 t7），inner tablet 零物化；`__all_table_history` 1219 行经重定向读全部正确；写隔离正常；pages=0。
  6. 套件证据：direct full `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_o1okzz5o/data.tar.gz`、direct tls `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_direct_v19_sn1x0h5e/data.tar.gz`、bootstrap `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_0ahq8hll/data.tar.gz`、sql_worker full `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_timeout_v13_dy1r_l7x/data.tar.gz`。
  7. V24 调试插桩已清理（`_LOCK_FAIL`/`_GLOBAL_SPACE`/`_RESOLVE_FAIL` 连同只服务打印的 `lock_stage`/`plan_marker`/`plan_count`/hop 变量一并删除，direct full 回归 PASS `namespace_fork_PROTOTYPE_direct_v19_q60326gc`）；V23 性能剖析插桩（`PROTOTYPE_V23_SCAN_*`、`SCHEMA_REFRESH_PROF`）保留，供下一阶段 schema 懒加载测量使用。遗留：fork→可用 1s 目标的剩余大头是激活期 schema 全量刷新（55k 串行点查），下一阶段做 schema 懒加载；b1 内 CREATE DATABASE+CREATE TABLE 卡 2m15s 的回归、3000 表规模公平对比测试随懒加载一并验证。
  8. 运维坑位（非代码 bug）：worker spawn 走 `std::env::current_exe()`（/proc/self/exe）。运行中的实例若被 `make` 重链接替换二进制，/proc/self/exe 变为 " (deleted)"，此后所有 spawn 立即 ENOENT，表现为 fork 成功但 worker 永不起（activate_namespace 报 -4124 OB_CONNECT_ERROR，代理侧 1105 namespace worker unavailable），且每次代理重试都会留一个空 generation 目录。遇到即重启实例。二进制热升级场景如需支持，后续 spawn 改用启动时记录的稳定路径。
  9. 2026-09-22 实测复核（exc_measure 重启后）：fork b10（ns 2000002）语句 0.26s；代理首连触发 spawn+激活+首查询合计 1.09s；连读 big.t1/big.t2/mysql.user/__all_table_history（1219 行）后例外表 0 行（零物化）；INSERT 触发恰好 1 条 owned（tablet 200008），child 读 4 行、parent 仍 2 行（COW 隔离正确）；pages=0。
  10. 语法命名遗留（用户已拍板，暂不动）：`FORK DATABASE` 语句复用 vanilla 语法，但语义已是 namespace fork（操作数是 ns 名而非库名，vanilla 库内 fork db 能力在本分支不复存在）。终态方向：`FORK INSTANCE` + `FORK TABLE` 并存，`FORK DATABASE` 可能删除。先维持现状，命名正名留到后续。
- 2026-09-22 A0 测量与 tables 阶段修复（四套件 PASS：bootstrap `e3dx7wfy` / direct full `3rsqn3rl` / direct tls `5uqyiq2c` / sql_worker full `lpt3674l`）：
  1. A0 测量（新建 a0_measure 实例 29814，main 库 3000 表、每表 1 索引 ≈ 6000 对象，重定向版二进制）：fork 语句 0.29s；fork→首查询 3.74s。激活 full refresh 3.35s 中 tables 阶段独占 3.27s，其中一条 SQL（`FETCH_ALL_TABLE_HISTORY_FULL_SCHEMA`，派生表 GROUP BY + NL join）占 2.9s——执行成 6648 次对 `__all_table_history` 的 IPC 点查（scan open wait 1.09s + fetch 0.79s + close 0.43s）。其余对象 46ms、mgr 构建 18ms、首查询懒加载 58 条 inner SQL 251ms。对照 ns1 读最新版本同规模 tables 阶段仅 365ms。旧文档 35.8s 数字是重定向前快照链放大所致，已过时。
  2. 设计规则（通用）：worker 分离后 inner SQL 里任何 per-object NL/lateral 点查都被 IPC 往返放大约 35 倍（进程内 ~10μs → IPC ~0.35ms）。刷新/元数据链路必须全部是"批量 scan + 内存归并"形态；column/partition/constraint 的 fetch 本来就是这个形态，唯独 table 主 fetch 用了 NL join。
  3. 修复：`fetch_full_table_schema` 非增量分支改为 plain scan（`WHERE schema_version<=cap AND table_id != core ORDER BY table_id DESC, schema_version DESC`）+ `retrieve_table_schema` 内存归并（同 table_id 取首行=最新≤cap，is_deleted 跳过——与 JOIN 的 `MAX(ver)` 后 `is_deleted=0` 过滤语义等价）。`FETCH_ALL_TABLE_HISTORY_FULL_SCHEMA` 与未使用的 `FETCH_ALL_TABLE_HISTORY_SQL3` 宏已删除。`FETCH_ALL_TABLE_HISTORY_WITH_ROWKEY`（lateral 逐 id）仅增量小批量路径使用，保留。
  4. 修复后实测：tables 阶段 3291ms→634ms，refresh 总 3.35s→0.70s（199 条 inner SQL sum 589ms、max 70ms），fork 语句 0.28s、fork→首查询 **1.19s**。剩余大头仍是 tables 阶段的批量 scan 本身，进一步优化空间：空对象类型短路、快照 scan 提速；B 方案（快照包直传）暂不启动。
  5. 遗留：首查询仍有按表懒加载全 schema（每张表首访一次 IPC），宽 join 首查询会串行放大；3000 表规模的 fork 前后公平对比测试（含 DDL 阻塞回归 b1 2m15s 场景）待做。
- 2026-09-22 批量放大第二轮（四套件 PASS：bootstrap `y9id` / direct full `wpya4puj`（复测 PASS）/ direct tls / sql_worker full；a0_measure 同规模复测）：
  1. 三处改动：`MAX_IN_QUERY_PER_TIME` 100→1000（删掉 `FIXME@xiyu` 调试残留注释，恢复上游默认值）；`fetch_all_table_info` 的 table_ids 分支由 VALUES+LATERAL 逐 id 点查改为 `WHERE table_id IN (...) AND schema_version<=cap ORDER BY table_id DESC, schema_version DESC` 单 scan + `retrieve_table_schema` 内存归并（语义等价性已核实：归并逻辑本就同 table_id 取首行、is_deleted 跳过）；`fetch_full_table_schema` 后处理循环的硬编码 `BATCH_FETCH_NUM=100` 改为 `MAX_IN_QUERY_PER_TIME`——这才是 FK/constraint/partition 分批的真正出处，上一轮只改常量没生效就是因为漏了它。
  2. 连带修复：扫描通道原型单请求 ranges 上限 256→8192（`namespace_worker_scan_prototype.ipp`）。1000 的 IN 批在通道上展开为逐元素 range，超过 256 直接 OB_NOT_SUPPORTED，导致换二进制后 worker bootstrap 失败、进程静默 exit(1)——排查手段：strace -f -e trace=execve,exit_group 看到 spawn 的 worker 立即 exit_group(1)。
  3. 实测（a0_measure，main 6000 对象）：refresh 782→499ms（tables 687→401ms，before_tables 27ms）；FK/constraint 查询数 69→9（每批 1000）；fork→首查询约 1.2s（fork 语句 0.30~0.35s + spawn/init ~0.2s + refresh 0.5s + 首查询懒加载 ~0.25s）。
  4. 剩余耗时明细（tables 401ms 内）：`check_sys_schema_change` 的 `SELECT 1 FROM __all_ddl_operation ... TABLE_ID IN(230 个 sys 表)` 单条 81ms（TABLE_ID 非前导列，全表扫 + 无 LIMIT，单机体感被内存掩盖，IPC 下暴露）；`__all_core_table` 冷启动首查 48ms 一次性开销；`SELECT count(*) FROM __all_table_history` 23ms；FK/constraint 各 9 批 ×5~12ms。1s 目标剩余空间：ddl_operation 存在性检查改造（限 1 或换索引形态）、首查询懒加载批化、spawn 提速。
- 2026-09-22 sysbench 首轮对比（oltp_point_select，4 表 × 10 万行，ps-mode=disable，10s；单体基线 = lt-local vanilla seekdb 新部署 vanilla_sysbench:29815，worker = a0_measure:29814 main/ns1）：
  1. 数字：1 线程 单体 7868 QPS/0.13ms vs worker 直连 317/3.15ms vs 经代理 190/5.26ms；8 线程 单体 53231/0.15ms vs worker 直连 600/13.3ms vs 经代理 280/28.5ms。点查差距 24~40 倍，1→8 线程单体扩 6.8 倍、worker 直连仅 1.9 倍、经代理几乎不扩。prepare 同体量数据 单体 ~6s vs worker ~2.5min（写路径 ~25 倍）。
  2. 差距构成（直连 3.15ms 里）：每条用户查询固定伴随一条 `SELECT MAX(schema_version)... FROM __all_ddl_operation` 版本探测 inner SQL（~0.8ms IPC，3599 条/3170 查询 ≈1:1）；单条 inner SQL IPC 地板 ~0.7ms（点查本身也是一条）；V17/V23 插桩每查询 2 行 fprintf；代理层额外 +2.1ms（proxy/gateway 全无 TCP_NODELAY，loopback Nagle/延迟 ACK 嫌疑最大，pump 为每连接单线程 poll 泵）。
  3. 判断：当前差距主体是原型开销（每查询版本探测、插桩、代理 TCP 参数、IPC 通道地板未调优），不是架构定论；但即使全部修掉，点查类负载的结构地板 = 每查询一次存储 IPC（优化到位估 50~100μs），对单体 130μs 的整语句耗时，最好情况约 1.5~2 倍差；扫描/批量类负载差距应显著更小。写路径（事务+redo 跨进程）是最大风险项，与此前设计判断一致。
  4. 待办（性能方向）：杀每查询版本探测（缓存+控制通道推送失效）、代理 TCP_NODELAY、IPC 地板排查（poll 超时/线程唤醒）、写路径批量化、并发串行点排查（8 线程仅 600 QPS，疑 session 池/通道锁）。sysbench 需 --db-ps-mode=disable，COM_STMT_EXECUTE 在代理/worker 路径报错（errno=0 空错）待修。
  5. 注意：vanilla_sysbench 是独立单体实例；a0_measure 继续跑 worker 模式；两者数据独立可反复对比。
- 2026-09-22 每查询版本探测消除（四套件 PASS；a0_measure 实测）：
  1. 语义拍板：会话内 read-your-own-DDL（session 水位 + 提交后同步追赶），会话间不强一致；worker 不追其他 worker 的 DDL（ns 的 schema 只被自己的 worker 改，`__all_ddl_operation` 按 ns 隔离）。
  2. 改动三处：`ObMPBase::before_process` 删掉每查询 `refresh_and_add_schema(false)`（真凶：带全局 `schema_refresh_mutex_`，每查询全量刷新探测+串行化）；`ob_multi_version_schema_service` 三个版本访问器改本地读（guard stamp / runtime refreshed 去掉 remote IPC 分支），`get_published_schema_version` 故意保留 live probe（DDL 栅栏 `update_session_last_schema_version` 需要真实共享版本）；`process_schema_version_changes` 在 DDL 成功后 `async_refresh_schema(last_ddl_schema_version)` 同步追赶。
  3. 实测（oltp_point_select 4×100k，ps-mode=disable）：worker 直连 1t 317→**1096 QPS**（3.15→0.91ms）、8t 600→**5472**（13.3→1.46ms），经代理 8t 280→**5000**（28.5→1.60ms），代理与直连已持平。对比单体基线 1t 7868/0.13ms、8t 53231/0.15ms：点查还差 7~10 倍，剩余是 IPC 通道结构地板。
  4. DDL 栅栏实测通过：单会话 CREATE→INSERT→ALTER→SELECT 立即见新列；跨会话建表立即可见。
  5. 测试锚点修复：探测消除后 sql_worker 套件 `run_timeouts` 失去 SCANS_RELEASED 打印来源（原来靠探测会话短命 ReadScans 析构）。改为 `ReadScans::process` 的 'X' close 分支每次关闭打印 `remaining=<关闭后剩余>`——取消关闭未耗尽 scan、慢客户端排空后关闭耗尽 scan 都能观测，与 V10_SCAN_OPEN 每 open 打印对称（均属插桩去留 TODO）。
  6. obperf 采样（worker pid）：IPC 同步原语主导——cond_wait/broadcast 乒乓、malloc churn、gettimeofday 插桩、`PendingRequest::take`、`ObTxDesc` 每请求序列化。后续结构优化方向：通道批化/唤醒模型、事务描述复用、插桩剥离。
- 2026-09-22 通信协议优化第一轮（sql_worker full / direct full PASS；a0_measure 实测）：
  1. 帧级核算（1t sysbench，按日志计数）：优化前每条点查 = 'O'+'F'+'X'+'e' + 事务 RPC S×2/G/U ≈ 7~8 次同步往返。单板 RTT ~55μs。
  2. scan 协议改造：'O' open 融合首批 fetch（点查一次往返拿到行）；耗尽即自动关闭（共享端 fetch 到 end 直接erase，worker 端 end 即弃 handle 不再发 'X'）。取消路径仍走 'X'，慢客户端排空走自动关闭，SCANS_RELEASED 两个观测点都保留。不变式：handle>0 ⟺ 共享端 scan 存活。
  3. 事务 RPC 去重：`start_stmt` 对 `prepare_tx_for_statement` 相邻调用两次（ob_sql_trans_control 上游固有），服务端 prepare 幂等，worker shim 层用 thread_local 记录上次 prepared 的 tx，同 tx 任何其他 RPC 或失败即失效——第二个 S 跳过。修过一个错位 bug：fetch_batch 多写一个 ret 号导致 'O' 回包错位，bootstrap 全崩 -4002。
  4. 实测（oltp_point_select 4×100k，ps-mode=disable）：直连 1t 1096→**1412 QPS**（0.91→0.71ms）、8t 5472→**7384**（1.46→1.08ms）；代理 8t **7243**/1.10ms（代理开销≈0）。帧数降到 S+G+U+O+e = 5 往返/查询。
  5. 剩余结构（按火焰图）：ObTxDesc 相关栈 56.6%——每次 tx RPC 回包携带完整 ObTxDesc 序列化（S/G/U 各一次），prepare/snapshot/reuse 本身也是远程调用；PendingRequest condvar 机制 16%。下一步候选：autocommit 只读语句无状态读（S/G/U 全省，复用 reserved_snapshot_version 钉住）、'e' 惰性释放、回包 desc 裁剪。

## 2026-09-22 方向修订：单进程 + NamespaceRegistry（讨论结论，先不做）

### 动因与前提

- IPC 通信优化已到基线：点查帧数从 7~8 往返压到 5 往返（S+G+U+O+e），单板 RTT ~55μs，8t 直连 7384 QPS vs 单体 53231。剩余差距主体是结构性的：每查询多次同步往返 + tx 镜像每 RPC 全量序列化（火焰图 56.6% CPU）。shm ringbuffer 通道估到顶仍差单体 1.5~2 倍。**结论：SQL↔存储的进程边界本身是性能根因，消息模型优化到顶也追不平单体。**
- 前提变化（用户拍板）：不需要故障隔离；不需要资源隔离（挤爆可接受）；v1 不跨机。
- 关键洞察：fork 能力全部在存储层（目录树/COW/例外表），与进程边界无关。worker 进程里真正 per-ns 的只有 3 样（schema service、到存储的连接、生命周期管理），bootstrap 15 步里其余 12 步（SQL factories、executor 单例、tz、kvcache 等）全是全局初始化、只因多进程才被重复 N 份。

### 终态形态

- 回到单进程：SQL+存储同进程（热路径回到原生函数调用，单体性能天然持平）；namespace = 进程内轻量对象。
- 命名三件套（已定）：
  - `Namespace`：身份与持久元数据（ns_id、名字、fork 血缘、存储根引用）。长久存在，crate/fork/drop 才变。
  - `NamespaceRuntime`：ns 在进程内的运行时持有物（schema service 实例、plan cache 实例、刷新状态）。**纯数据对象，不绑线程**；懒创建（首次登录）、可销毁。等价于原 worker 进程的内核。
  - `NamespaceRegistry`：唯一新全局，`ns_id → (Namespace, NamespaceRuntime*)`，生命周期归它管。
  - 不叫 NsContext：`Context` 在库内已是 per-查询/执行语义（ObSqlContext/ObExecContext），会误导生命周期判断。`Ns` 只做局部变量缩写。
- 为什么不是 OB 多租户 2.0：OB MTL 的痛苦来自 per-tenant 复制运行时资源（线程/内存/IO/事务服务）；这里只复制元数据缓存，运行资源全共享（正是"不要故障/资源隔离"换来的架构红利）。

### ns 感知最小化

- ns 边界只存在于 catalog 层。存储引擎/事务/执行器/block cache 全部 tablet_id 寻址，零 ns 感知（"存储只认 tablet"原则的兑现）；存储层看不到表名、SQL、会话。
- ns 上下文注入点只有两处：登录 session 绑定（`root@ns` → registry 查一次 → session 缓存 `NamespaceRuntime*`）、后台任务派发（DDL 任务元数据表本来就 per-ns）。
- 查询热路径改动 ~5-10 个入口（`ob_sql.cpp:1115` 类）：schema guard 从 session 的 ns 取，下游经 `ObSqlSchemaGuard` 传递的现状不动。
- `GCTX.schema_service_` 381 处引用：一次性改完（149 处在 rootserver/ddl_task，任务上下文自带 ns），**不留线程局部当前 ns 之类的过渡机制**（过渡机制容易留下来变成第二类全局）。
- 建议倾向（待最终确认）：plan cache per-ns 实例（失效语义干净）；ns 懒建（boot 不随 ns 数膨胀）；IPC 层代码在单进程跑通后删除（删除即验证无隐藏依赖）；内存分配打 ns tag 只观测不隔离。

### 模块边界：靠机制不靠纪律

- 双缝设计：
  1. SQL↔存储缝 = `INamespaceStorage` 接口（~15 方法：数据面 get_row/open_scan 融合首批/fetch_batch/put、事务面 prepare/commit/abort/resolve_snapshot、namespace 面 fork/create/drop/directory/exceptions）。**v20 IPC 协议的帧语义直接沉淀为进程内接口**——融合 scan、自动关闭、游标不变式、tx 去重全部保留为接口语义，只删序列化。接口按值语义、无共享指针跨界、显式版本号设计，未来跨机可在同一条缝补 remote adapter。
  2. per-ns 状态缝 = `NamespaceRuntime*`，唯一入口 session/task，registry 是唯一新全局；`GCTX.schema_service_` 彻底删除。
- bazel 可见性门禁（仓库已有地基）：`MODULE.bazel`（8.2.1）+ 20 个 BUILD.bazel + `src/storage` 已有 `default_visibility=private` 和 `STORAGE_PUBLIC_HEADER_ROOTS/PRIVATE_HEADERS` 公私划分。缝做成独立 target 显式授 visibility，未声明依赖的包编译期就 include 不到；`layering_check` 掐"include 了但没声明 deps"。若 bazel 尚非完整出货链路，先作为 CI 架构门禁目标，不影响 CMake 出货。
- 实例化生命周期：存储引擎改成实例对象（进程内单实例，boot 建/shutdown 销；构造时向 SQL 层注入 `INamespaceStorage*`，测试注入 fake）。ns 可创建销毁 = fork/drop 的产品语义本身。不搞"所有代码不许碰全局"的原教旨改造，保留少数根全局（registry、config），访问收敛到构造注入+显式传递。

### 空 namespace：模板 ns（已确认方向）

- `CREATE NAMESPACE db1` = fork 内置 `__template__` ns；`FORK INSTANCE db2 FROM db1` = fork db1。**两条用户路径收敛为一套机制**（目录树 fork + 例外表 + 懒物化全部复用）。
- `__template__`：boot 时建好（或首次需要时建），物理持有全部系统表，永不对外服务；registry 标 internal，登录/DDL 路由直接拒绝（机制保证"只放系统表"，锚点大小才有上界）。
- 锚点有界性：模板永不写，钉住的只是创建时刻一个版本的系统表（MB 级固定锚点，不随时间增长）；子 ns 写系统表走已验证的 COW 物化逐页释放；系统升级时重建模板、原子换、旧锚点等引用清零回收。
- 附带收益：系统表结构升级时模板是天然参照物。

### GC/回收语义（代码核实结论）

- 元数据树页：mark-and-sweep（`PROTOTYPE_V9_METADATA_GC`），页粒度，引用只钉可达路径，不整树连坐。
- 物理 tablet：`encoded(ns_id, tablet_id)` 恰好归属一个 ns（最近物化它的祖先），后代沿链解析（`resolve_inherited_tablet`，cap 逐跳取 min）。父删表 = 例外表 tombstone（带 drop_scn），物理 tablet 按 tablet 粒度逐个回收——无"引用一行钉住整个快照"的连坐。
- **MVCC 版本面是粗的（已知限制）**：fork 注册 `tablet_id_=0`（=全部本地 tablet）的全局 `SNAPSHOT_FOR_MULTI_VERSION` pin（`namespace_fork_kernel_prototype.cpp:2110`），fork SCN 之后所有 pre-fork tablet 的行版本全部保留到引用清零；fork 后新建的表不受影响（其版本本就在水位之上）。
- 历史对照：fork database/table v1 逐 tablet `batch_acquire_snapshot`（`ob_fork_table_util.cpp:445`），粒度细但 O(表数×tablet数) 慢；namespace fork v1 在 worker 启动时逐 tablet 物化（3000 表 bootstrap 3.5 分钟的元凶）；当前全局 pin 是 O(1) fork 换来的。**共识：任何 O(tablet 数) 的工作不回 fork 关键路径。**
- pin 粒度收敛方案（记录，先不做）：fork 时全局 pin 不动；子首次物化后/定期评估，对剩余未物化的继承 tablet 批量注册 per-tablet pin（`batch_acquire_snapshot` 现成），撤全局 pin；之后每次物化单独释放一个 pin。只读子认语义地板（全局 pin 挂到 drop——它可能读任何继承表的 fork 时刻数据，这部分保留任何方案省不掉）；写入子渐进松绑，父的热点表逐个解套。

### 已知限制与 TODO（记录，先不做）

- 父 ns 有活子代时拒绝 drop 父（`namespace_fork_kernel_prototype.cpp` 注释自承 prototype 简化；目录模型本意是 source snapshot 保留支持父先删）。
- tablet 级行版本钉住的观测：每血缘共享字节数/最老 pin SCN 视图，让钉子可见。
- 后台 detach 作业（长命只读子的主动物化解套）：TODO，v1 不做。
- `check_sys_schema_change` 81ms 全表扫（用户明确先不做）。
- fork→可用当前 ~1.2s；单进程化后无进程 spawn/bootstrap，预计进入 100ms 量级。

### 回滚记录

- 2026-09-22 回滚 IPC 层性能 commit（92e3d9b9e）：`81e2e1907`（scan 融合/自动关闭/tx 去重）与 `5a6c13262`（探测消除/本地版本读/DDL 栅栏）的代码改动全部回滚——两者优化的是单进程化要删除的 IPC 层，留着只是对 vanilla 语义的额外偏离（每查询本地 schema 刷新、上游双 prepare）。文档中的实测记录保留作历史证据；`b2dd38f5c`（sysbench 对比文档）有意保留。回滚后四套件 PASS：bootstrap `eskhj7_0`、sql_worker full `k23xit4k`、direct full `uimc6c8w`、direct tls `skyg425u`。单进程化落地前门禁以此为准；若过渡期需要性能数字可摘樱桃恢复。
