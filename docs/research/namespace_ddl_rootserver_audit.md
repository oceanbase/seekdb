# DDL/rootserver namespace 审计记录

状态：本轮静态全量归口和定向动态验证完成；无法由 SQL 触发的旧 RPC 与未覆盖的任务异常分支列为非阻塞 TODO。历史设计文档的“152”是当时默认当前版本 schema guard 的调用点数量，不是 152 个已确认缺陷。当前源码在 `src/rootserver/**/*.cpp` 有 168 个 `get_runtime_schema_guard(` 调用点，其中 161 个只传 guard、4 个传两个参数、3 个传三个参数。与历史 152 可对照的是当前 161，而 168 是所有参数形式的总数；两者都不能单独作为缺陷数。

## 审计方法与边界

逐条追踪 DDL 请求、异步任务和重启恢复的 namespace 来源，核对 schema service、SQL proxy、tablet ID 与 MDS payload 是否指向同一 namespace。进程级运行状态和系统配置允许使用全局服务；表、索引、任务记录和 tablet 元数据必须使用所属 namespace。动态检查同时比较子空间、父空间及重启后结果。

本轮静态筛查中，`get_runtime_schema_guard(` 最集中的文件是 `ob_ddl_operator.cpp`（41）、`ob_ddl_service.cpp`（17）、`ob_index_build_task.cpp`（10）、`ob_ddl_redefinition_task.cpp`（8）和 `ob_constraint_task.cpp`（8）。直接使用 `GCTX.schema_service_` 获取 guard 的三处在本轮变更后仍位于本地管理服务的系统变量校验（2）和 major freeze 的 server runtime 检查（1）；它们读取进程级状态。其他调用不能仅凭函数名认定安全，仍需沿各自调用链核对服务归属。

### 168 个 guard 调用点的静态归口

| 目录或文件 | 数量 | schema service 的来源 |
|---|---:|---|
| `ddl_task/` | 77 | 任务 `task_schema_service()`、所属 root service，或由任务显式传给辅助函数；scheduler 的 `start_redef_table` 本轮改为接收调用者 context |
| `ob_ddl_operator.cpp` | 41 | 构造时注入的 schema service；构造者使用所属 DDL 服务/辅助服务 |
| `ob_ddl_service.cpp` | 17 | DDL 服务初始化时注入；子空间服务由 `init_sql_worker` 绑定 |
| `freeze/` | 9 | 进程级 major freeze、server runtime 与 checksum 校验 |
| `parallel_ddl/` | 6 | 创建/删除辅助类初始化时注入所属服务 |
| `fork_table/` | 5 | 4 个全局 guard 用于 ns1 bootstrap/注册表/全局清理；1 个 fork task 用所属服务 |
| 对象权限 DDL operator、本地管理服务、PL DDL | 9 | 构造/初始化时注入；本地管理服务其中 2 个读取进程级系统变量 |
| 约束检查、bootstrap、索引 builder、truncate info | 4 | 所属 DDL 服务或显式参数；bootstrap 为 ns1 |
| **合计** | **168** | 此表只覆盖 schema guard 调用；SQL proxy、MDS、tablet runtime 另行审计 |

子空间服务来源可追到 `namespace_worker_inprocess_prototype.ipp:activate_in_process_namespace` 和 `ObLocalManagementService::init_sql_worker`；任务恢复在 `inprocess_refresh_schema` 中把同一组服务写入 `ObDDLTaskContext`。`ObDDLTask::task_schema_service()` 仅在 namespace 1 缺省回退全局服务。这个归口验证不代表每个 DDL 的其他依赖都已显式化。

任务调度器中多处 `fetch_new_task_id(*GCTX.sql_proxy_, ...)` 看似读全局任务表；实现实际上忽略 SQL proxy，使用 `ObCommonIDUtils::gen_unique_id` 生成进程级唯一 ID。已核对的 `schedule_*_task` 路径在具体任务的 `init(task_record)` 中设置 context，表/列重定义也由 scheduler 或任务 `init` 设置。仍须审计任务运行中的辅助对象与异常路径；不能只凭调度时 context 正确推断全程隔离。

当前 `src/rootserver/**/*.cpp` 中直接引用 `GCTX.schema_service_` 或 `GCTX.sql_proxy_` 共 154 处。这是文本调用点数，不是缺陷数。其中 fork kernel 的 64 处访问进程级 namespace 注册表、schema blob 和清理元数据；freeze 子系统访问全局冻结版本、snapshot GC、major merge 状态；DDL scheduler 中大部分是忽略 proxy 参数的进程级 task ID 生成，另有已单列的 compaction checksum 检查和仅 ns1 允许的回退。`ob_ddl_service.cpp` 的 5 处中有进程级 proxy 存在性检查及 task ID 生成；本地管理服务中的 guard 用于 server runtime/系统变量。其他分散引用包括进程级会话、磁盘空间、内部表装载、系统配置和 DBMS job。按服务类型筛查后仍须沿具体异常分支核对对象 ID；尤其 checksum 错误表没有 namespace 字段，不能因为读取来自进程级服务就认定其表 ID 安全。

## 已确认并修复：TRUNCATE PARTITION 保留全局索引

子空间自有分区表带全局唯一索引，开启 `_ob_enable_truncate_partition_preserve_global_index` 后执行 `TRUNCATE PARTITION`，此前返回 1146。`ObTruncatePartKeyInfo` 解析分区表达式时从 `GCTX.schema_service_` 取 ns1 schema guard，却用它查子空间表；现在从 `ObDDLService` 显式传入所属 schema service。

继续执行后，`SYNC_TRUNCATE_INFO` MDS 中的 index tablet ID 仍是逻辑 ID，存储事务返回 4725。namespace 写边界现在转换该 payload 中的 tablet ID。对于继承自父空间的全局索引，先按 fork 快照把索引 tablet 物化到子空间，再登记 truncate MDS，避免修改父空间物理 tablet。直接改用异步重建索引的尝试卡在 checksum validation，因此没有保留。

四件套的 direct full DDL 回归覆盖子空间自有表和继承表两种全局索引截断；每种都强制走索引查询，继承表还检查父空间数据，重启后重复核对。独立最小复现的通过日志为 `/tmp/seekdb-ddl-audit-truncate-green2.log` 和 `/tmp/seekdb-ddl-audit-truncate-inherited-materialized2.log`。本轮最终编译及四件套均通过：`/tmp/seekdb-ddl-audit-build-final.log`、`/tmp/seekdb-ddl-audit-gate-{bootstrap,sql,direct-final,tls}.log`。

## 后续审计入口

- `ObRedefCallback::modify_info` 的队列未命中检查已改用任务所属 SQL proxy；缺失 child context 时显式报错。
- `start_redef_table` 已从本地管理服务接收 DDL context，用所属 schema/SQL 服务建任务，并把 context 附到新任务记录；该入口在当前 SQL 四件套中没有直接可触发语句，尚缺动态专项验证。
- 同组 `abort_redef_table`、`finish_redef_table`、`copy_table_dependents` 现由本地管理服务传入所属 DDL context；scheduler 以所属 SQL proxy 锁定、回读和更新任务记录，队列未命中回读后补上 context。原实现把回读错误留在临时变量中，可能把空记录当作可恢复任务，现直接传播错误。队列命中时检查 namespace ID，防止任务 ID 指向别的空间。旧 RPC 当前没有 SQL 调用链，尚缺动态专项验证。
- 心跳超时清理原只按任务 ID 调用 `abort_redef_table`；现从队列任务取 namespace context，并按任务类型调用表重定义或通用队列取消。队列已移除的过期条目只清除心跳，不再以默认 ns1 context 访问任务表。此处与 `renew_ddl_task_lease` 一样是进程级入口；任务 ID 必须在队列中才能判定所属 namespace。
- 任务辅助路径复查：异步索引 SSTable SQL 构建、DDL 任务恢复、事务结束等待及列 checksum 上报的服务回退限定为 namespace 1。子空间 context 缺少 schema/SQL/DDL proxy 或 local runtime 时返回错误，不再静默读取全局服务；正常子空间 context 由 `init_sql_worker` 和重启恢复装配。`construct_domain_index_arg` 以前只检查全局 root service 是否存在，实际不使用它，已删掉此无关依赖。任务类中的 root service 回退见下文统一入口。
- `ObDDLTabletScheduler::init` 接收所属 schema/SQL 服务，却自行取得进程级本地管理服务；进一步检查发现该服务指针只在初始化赋值，没有任何使用，现已移除。分区本地索引 child 创建、强制索引查询、重启查询已加入 direct full，聚焦运行 `/tmp/seekdb-ddl-audit4-part-local-probe.log` PASS。其查询运行 SQL 的全局 proxy 带有“会话为进程级”的注释，专项用例通过未证明运行会话查询的并发分支已覆盖。`ObDDLLocalBuildExecutor::check_build_end(true)` 使用全局 schema/SQL 校验 checksum；调用链只有表/列重定义父类和 `ObDropVecIndexTask`。子空间表重定义走自己的 SQL 构建，子空间列重定义覆写 `wait_data_complement` 走 SQL 构建，删除向量索引调用 `check_build_end(false)`，所以该全局 checksum 分支目前只可由 ns1 的旧 DAG 构建触达。
- 任务类的 root service 获取原散落在 16 个源文件、31 个回退表达式中；统一到 `ObDDLTask::task_root_service()` 后，仅 ns1 可回退进程级服务。表/列重定义生效与自动递增 schema 更新的直接调用增加缺失服务检查，避免 child context 丢失时空指针或误操作 ns1。`schedule_ddl_task` 入口要求子空间任务记录同时具备 schema、SQL、root service，拦截不完整 context；新建任务的短暂构造阶段仍有进程级服务赋值，但记录在调度前由调用方覆盖 context。
- 旧 `init(表 schema, ...)` 构造函数仍有取全局本地管理服务的路径；这些函数用于构造、序列化新任务记录，实际调度由 `init(task_record)` 重新装配服务。已检索 DDL 服务、索引 builder、本地管理服务、父任务和 fork table 的 `create_ddl_task` 调用，并逐处检查 `schedule_ddl_task` 调用前的赋值：直接调度者均设置 `task_record.context_`，恢复路径在 `recover_task(context)` 装配。自动递增异步子任务由父任务显式传入本地管理服务，深拷贝沿用该指针，已移除其无条件全局回退。构造阶段多余的全局前置条件可单独清理。
- `recover_task(context)` 的子空间类型白名单与 `schedule_ddl_task` 的 switch 均含 44 个任务类型，逐项集合对比无差异；恢复时先给每条记录装配所属 context，随后用其 SQL proxy 锁任务行并调度。这个静态一致性检查不代表每种任务的中断恢复都已经动态通过，现有四件套仍只对部分长事务任务做了重启验证。
- 子空间恢复还有一次性闩锁问题：`recover_task(context)` 原先把逐条恢复错误清零，schema 版本未发布时也只跳过记录，`inprocess_refresh_schema` 因此将 `recovery_loaded` 保持为 true；child 没有 ns1 的周期扫描来补救。现在逐条记录错误并继续处理其余任务，已在队列中的任务视作已恢复；子空间遇到读写暂不可用、schema 版本未到或任务恢复失败时返回错误，让下一次 schema refresh 重试。direct full 的中断 `DROP PRIMARY KEY` 用例临时把任务记录 schema 版本抬高，重启第一次恢复应推迟；恢复版本后要求同一任务继续运行并完成，验证这个重试入口。
- 任务实例的 context 装配核对：`ddl_task/` 与 `fork_table/` 下有 13 个 `init(const ObDDLTaskRecord&)` 实现，12 个在初始化时直接读取 `task_record.context_`，列重定义由唯一调度入口 `schedule_column_redefinition_task` 在调用 `init` 前先执行 `set_context(task_record.context_)`。本地管理服务创建的并行 DDL helper 接收该服务的 `schema_service_`，随后 `ObDDLHelper::init` 从同一个 `ddl_service_` 取得 SQL proxy；rootserver 中 `ObDDLOperator` 的构造参数均来自所属 DDL 服务或成对注入的 schema/SQL 成员，没有直接传入全局 `GCTX`。这覆盖了默认 guard 调用背后的服务来源，不能代替异步回调、MDS 与 tablet 实际执行的逐路径动态验证。
- 其余 5 处 `GCTX.ddl_sql_proxy_` 在索引任务和 DDL 服务中只允许 ns1 回退；约束、重定义和自动递增异步任务的子空间调用者传入所属本地管理服务，由它取得 child DDL proxy。4 处 `GSCHEMASERVICE.get_runtime_schema_guard` 均在 fork kernel 中处理 ns1 bootstrap、注册表、全局清理或控制库保护；直接 `GCTX.schema_service_`/`GCTX.sql_proxy_` 的文本引用仍为 154 处。这里的结论限定为源码当前调用链，不把死代码中的全局 fallback 视为可直接调度的子空间路径。
- 直接全局 SQL 访问中，`ObDDLTaskRecordOperator::get_or_insert_tablet_schedule_info`、`ObDDLTaskUtil::get_task_tablet_slice_count` 和无 proxy 参数的 `ObDDLTask::push_task_execution_id` 当前均无源码调用者；它们如果重新启用，需要先接收所属 namespace SQL proxy。`ObForkTableTask::init(record)` 原有无条件全局 root service 回退，即使调度入口当前已要求子空间 context 完整，任务自身仍可能在其他初始化入口误取 ns1；现改为使用仅允许 ns1 回退的 `task_root_service()`。
- 添加外键的异步校验尝试：已有数据的表执行 `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY` 在 ns1 和子空间均返回 1210，子表显式建立引用列索引后结果仍相同。最小双侧日志 `/tmp/seekdb-ddl-audit5-fk-minimal.log`；这是当前 SQL 能力或用例前置条件问题，不能归为子空间跨服务缺陷，暂不将必败用例加入门禁。相应补充了子空间已有无效数据添加 `CHECK` 约束的失败路径测试，与原有成功建约束、插入时拒绝及重启后校验构成完整约束回归。
- 降低 `AUTO_INCREMENT` 起点时，`ObDDLService` 为核对表内最大值执行 user SQL，原从 `GCTX.ddl_sql_proxy_` 读取。子空间独有表从 100 降至 2 的最小复现返回 1146（`/tmp/seekdb-ddl-audit5-autoinc-red.log`）；此处是表数据而非进程级状态，应使用任务 context 中的 DDL proxy。修订后子空间缺失该 proxy 显式报错；direct full 增加降低起点、继续插入、重启后持续递增的断言。
- 建任务前的 compaction checksum 检查读取进程级虚表。底层 `__all_column_checksum_error_info` SQLite 表没有 namespace 字段，写入者 `ObTableCkmItems` 直接记录 schema 的逻辑 table ID；major freeze 校验器从进程级 schema guard 取表。SQL 无法写该底表，本轮在一次性实例停机后向其 SQLite 文件注入错误行并重启：子空间自有表 ID 对应错误行导致 `CREATE INDEX` 返回 1235（`/tmp/seekdb-ddl-audit7-checksum-child-red.log`）；ns1 表的错误行也按预期使建索引返回 1235（`/tmp/seekdb-ddl-audit7-checksum-own-red2.log`）。继承表与 ns1 原表共享逻辑 ID，故进程级行不能仅按 ID 作用于子空间。`create_ddl_task` 现通过 SQL 事务的 `target_namespace()` 限定这项进程级 major-compaction 检查只作用于 ns1；子空间索引继续使用所属任务的 checksum 校验。direct full 在已有重启点注入 ns1/child 两条错误行，要求 child 建索引、强制索引查询成功，同时 ns1 建索引仍返回 1235。将来如果子空间也参与 major-compaction 错误上报，元数据表和读写链须增加 namespace/物理对象归属，不能沿用此 ns1 专属表 ID 查询。
- `ObPartitionExchange::update_table_all_monitor_modified_` 过去用全局 SQL proxy 读统计、在所属 DDL 事务中写统计；现读写均用同一事务。交换分区虽非 seekdb 核心功能，该错误跨越了通用 namespace/事务边界。
- `ObDDLTaskUtil::get_domain_index_share_table_snapshot` 的离线重建分支原用全局 root/schema/SQL 服务。显式主键、FTS 索引、两行数据的子空间表执行 `ALTER TABLE ... MODIFY COLUMN v VARCHAR(20)` 时，父任务 type 1001 在复制依赖索引阶段返回 `OB_ERR_UNEXPECTED(-4016)`，客户端超时；同一 SQL 在 ns1 成功。临时阶段日志确认：ns1 schema guard 对子空间 rowkey-doc 表 ID 500020 返回空 schema。现在表/列重定义父任务都把所属 root service 传给 FTS/向量子任务的 snapshot 辅助函数；新建索引的 snapshot 入口也接收所属 root service。聚焦 `/tmp/seekdb-ddl-audit2-fts-route-focused.log` 和 `/tmp/seekdb-ddl-audit2-fts-column-focused.log` 均 PASS，两类重定义及重启查询已写入 direct full 四件套。临时诊断代码已移除。
- 168 个 guard、154 处全局 schema/SQL 引用的静态来源已归口，44 种可调度任务的恢复白名单一致，任务初始化的 context 来源已核对。专项动态覆盖了索引、重定义、分区、约束、自动递增、向量/FTS、fork table 和中断恢复的代表性成功与失败路径；旧 RPC 的队列未命中分支、所有 44 种任务的独立异常重试，以及会话并发分支仍缺动态用例。这些限制不阻塞本轮静态审计结论，也不能推断未运行的路径一定正确。

本轮构建 `/tmp/seekdb-ddl-audit2-fts-sibling-build.log` exit 0；四件套 `/tmp/seekdb-ddl-audit2-final2-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。direct full 覆盖两类 FTS 重定义、恢复后 FTS 查询和既有 EXCHANGE PARTITION。完整 mysqltest/sysbench 未运行。

旧 RPC/心跳路径修订后离线增量编译 `/tmp/seekdb-ddl-audit3-build-final.log` exit 0；四件套 `/tmp/seekdb-ddl-audit3-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。已有失败复现仍保留在 direct full 的 `namespace_inprocess_ddl_regressions.py`。旧 RPC 无现成 SQL 触发入口，因此此次通过的是现有 DDL 功能回归，不视为 RPC 队列未命中分支的动态专项验证。

辅助路径与 root service 统一修订后离线增量编译 `/tmp/seekdb-ddl-audit4-final2-build.log` exit 0；四件套 `/tmp/seekdb-ddl-audit4-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。direct full 此次新增子空间分区本地索引创建、强制索引读和重启核对；完整 mysqltest/sysbench 未运行。

本轮 `AUTO_INCREMENT` 代理修订后离线增量编译 `/tmp/seekdb-ddl-audit5-autoinc-build.log` exit 0，最小红/绿复现为 `/tmp/seekdb-ddl-audit5-autoinc-{red,green}.log`。四件套 `/tmp/seekdb-ddl-audit5-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS；direct full 包括降低自增起点、已有坏数据添加 CHECK 失败、修正后重试成功和重启校验。完整 mysqltest/sysbench 未运行。

fork table root service 回退修订后编译 `/tmp/seekdb-ddl-audit6-fork-service-build.log` exit 0，四件套 `/tmp/seekdb-ddl-audit6-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。checksum 归属修订后编译 `/tmp/seekdb-ddl-audit7-checksum-build.log` exit 0，四件套 `/tmp/seekdb-ddl-audit7-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。恢复重试修订后编译 `/tmp/seekdb-ddl-audit8-recovery-build.log` exit 0，定向延迟恢复 `/tmp/seekdb-ddl-audit8-recovery-focused.log` PASS，四件套 `/tmp/seekdb-ddl-audit8-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。完整 mysqltest/sysbench 未运行。
