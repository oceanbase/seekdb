# seekdb 小规格空载线程分析

本文记录一次基于 `master c1083605c15` 的小规格 seekdb 空载线程采样，用于判断启动后约 3 分钟时常驻线程主要来自哪些模块，以及哪些线程更像是 baseline 常驻成本。

针对 DDL、BloomFilter、Tablet 元数据更新等任务队列的共享执行方案，见 [seekdb 后台任务共享线程池设计](background-task-thread-pool-design.md)。

## 测试环境

- 代码版本：`master c1083605c15`
- 构建目标：`build_release/src/observer/seekdb`
- 启动方式：

```bash
seekdb \
  --port 40100 \
  --base-dir /tmp/seekdb_idle_threads_40100 \
  --parameter memory_limit=2G \
  --parameter datafile_size=2G \
  --parameter datafile_maxsize=4G \
  --parameter log_disk_size=2G \
  --log-level WARN
```

- 采样时机：启动后空载约 3 分钟。
- 采样方式：主机侧读取 `/proc/$pid/task/*/comm`、`ps -L`、`/proc/$pid/status`、`/proc/$pid/smaps_rollup`。

## 总体观测

原始版本在 3 分钟附近的线程数大约稳定在 `64~68`，其中一次完整采样为：

- 总线程数：`67`
- RSS：约 `233~236 MB`
- `RssAnon`：约 `122 MB`
- `RssFile`：约 `112 MB`
- `VmSize`：约 `720~740 MB`

线程数在短时间内有轻微波动，主要来自少量动态线程或短生命周期后台线程，例如 `ReqWorker`、`IO_SYNC_CH0`、`LogIOCB0` 等。

## 线程职责表

| 类别 | 线程名 | 数量/现象 | 主要职责 | 代码线索 |
|---|---:|---:|---|---|
| SQL 网络入口 | `T1_sql_nio0..7` | 8 | MySQL/TCP 连接监听、收包、解码、投递。空载时也固定存在。 | `deps/oblib/src/rpc/obmysql/ob_sql_nio.cpp`，`ObSqlNio::run()` 设置 `sql_nio` |
| 请求执行 worker | `T1_ReqWorker` | 1~2，动态 | tenant 请求执行线程。空载时也会保留少量 worker。 | `src/observer/omt/ob_th_worker.cpp` |
| 事务后台循环 | `T1_TxLoopWorker` | 原始采样约 9；当前 demo 中它通常是执行过 timer callback 后残留名称的共享 `BGTask` worker | 事务后台维护定时任务：tx gc、retain ctx gc、start_working retry、log callback pool 调整、刷新事务配置、推进 weak read ts、keep-alive 等。mini mode 的到期 callback 当前由 `TimerService HIGH/max=2` Source 执行，不再保留独立 TimerWK。 | `src/storage/tx/ob_tx_loop_worker.cpp`、`src/share/ob_timer_task_background_source.cpp` |
| DAG worker | `T1_DAG` | 原始采样 3，稳态可回收到 0 | compaction、DDL、storage 等 DAG task worker。执行具体 DAG 时会临时改线程名，完成后改回 `DAG`；空闲后由 scheduler 自适应回收。 | `src/observer/scheduler/ob_tenant_dag_scheduler.cpp` |
| DAG 调度 | `T1_DagScheduler` | 1，常驻 | DAG 队列调度和派发；当前即使无 DAG、worker 已回收到 0，调度线程仍然保留。 | `src/observer/scheduler/ob_tenant_dag_scheduler.cpp` |
| 定时器/时间源 | `T1_TimerSvr`、`T1_ClockGenerat`、`T1_DetectorTime`、`T1_TransTimeWhe` | 原始采样 4；当前 demo 在 mini mode 下只保留前 2 个固定线程 | 通用 timer、全局 clock、死锁检测 timer、事务 time wheel。两个 TimeWheel 当前由共享池 `DetectorTimer`、`TransTimeWheel` Source 推进。 | `deps/oblib/src/lib/task/ob_timer_service.cpp`、`deps/oblib/src/lib/time/ob_clock_generator.cpp`、`src/storage/deadlock`、`src/storage/tx/ob_trans_service.cpp` |
| PALF 核心 | `T1_LogLoop`、`T1_IOWorker`、`T1_LogIOCB0` | 原实现 2 个固定线程，外加 0~1 个动态 callback worker；当前 demo mini mode 下只保留 `T1_IOWorker` | PALF 状态维护、redo IO 和 IO 完成回调。状态维护循环由 `PALFLogLoop` Source 推进，IO 完成回调由 `LogIOCallback` Source 推进，redo IOWorker 继续隔离。 | `src/logservice/palf` |
| PALF append 回调 | `T1_ApplySrv0` | 原实现 1 个固定线程；当前 demo mini mode 为 0 | 按 PALF committed LSN 推进 append callback，完成事务/日志提交回调；当前由 `ApplyService` Source 消费原队列。 | `src/logservice/applyservice/ob_log_apply_service.cpp` |
| 进程/存储日志 | `T1_OB_PLOG`、`T1_OB_SLOG` | 原始为 3 个固定线程；当前 demo 在 mini mode 下只保留 1 个 PLOG | `seekdb.log` 异步写入，以及 server/tenant 两套存储元数据 redo。两套 SLOG 队列当前分别注册为 `SLOGServer`、`SLOGLocal` Source。 | `deps/oblib/src/lib/oblog`、`src/storage/slog` |
| IO 框架 | `T1_IO_GETEVENT0`、`T1_IO_HEALTH0`、`T1_IO_SYNC_CH0` | 原实现有 `IO_GETEVENT`、`IO_HEALTH` 2 个固定线程，外加按需扩缩的同步 IO worker；当前 demo mini mode 下通常只保留 `IO_GETEVENT` | async IO event 获取、IO health 检查和通用同步 IO；IO health 和同步 IO 分别由共享池 `IO_HEALTH`、`SyncIO` Source 推进。池完全饱和时允许临时 rescue worker。 | `src/share/io` |
| DDL | `T1_DDLQueueTh0`、`T1_DDLPQueueTh0`、`T1_DDLTransCtr`、`T1_DDLTaskExecu*`、`T1_DdlBuild` | 约 6 | DDL 队列、DDL transaction control、DDL task 执行和 build。 | `src/rootserver/ddl_task`、`src/share/ob_ddl_task_executor.cpp` |
| Compaction/存储维护 | `T1_MergeSchedul`、`T1_TbltTblUp0`、`T1_BFBuildTask`、`T1_MaBlkBFLoad`、`T1_FrzAsync` | 原始采样 5；当前共享池 demo 在 mini mode 下不再创建 `T1_MergeSchedul` | major merge 调度、Tablet 合并状态和 checksum 元数据同步、BloomFilter build/load、freeze async。 | `src/rootserver/freeze`、`src/storage/compaction`、`src/storage/blocksstable` |
| DBMS scheduler/job | `T1_DBMSSched`、`T1_DBMS_JOB_MAS` | 2 | DBMS scheduler 服务和 DBMS job master。 | `src/observer/dbms_scheduler`、`src/observer/dbms_job` |
| Change Stream | `T1_CSFetcher`、`T1_CSDispatcher` | 原始采样 2；共享池 demo 在没有异步向量索引时为 0 | change stream fetch/dispatch；demo 把 IDLE 维护迁入共享池，仅在存在异步索引时按需启动专用线程。 | `src/observer/change_stream` |
| 死锁/锁等待 | `T1_LockWaitMgr`、`T1_LCLSender` | 2 | lock wait 管理、本地死锁检测消息发送。 | `src/storage/memtable/ob_lock_wait_mgr.cpp`、`src/storage/deadlock/ob_lcl_scheme` |
| 临时文件 | `T1_TFSwap` | 原实现 1；当前 demo 在 mini mode 下为 0 | tmp file swap/write-buffer 后台处理；当前以共享池 `TFSwap` Source 串行执行。 | `src/storage/tmp_file/ob_tmp_file_thread_wrapper.cpp` |
| 时间戳服务 | `T1_TsMgr` | 1 | GTS/时间戳刷新与回调 drain。 | `src/storage/tx/ob_ts_mgr.cpp`、`src/storage/tx/ob_gts_source.cpp` |
| 事务提交 GTS 等待 | `T1_TxTsWaiter`、`T1_TxTsCb0` | 原实现有 1 个固定 waiter，callback worker 按需创建；当前 demo 在 mini mode 下不再创建 `T1_TxTsWaiter` | 等待全局事务版本越过 commit version，再异步完成事务提交回调；协调轮询迁入共享池，回调仍保留隔离的动态池。 | `src/storage/tx/ob_tx_timestamp_waiter.cpp` |
| 诊断/维护 | `T1_MemoryDump`、`T1_DiagnoseQueu`、`T1_EvtHisUpdTas`、`T1_MaintainDepI`、`T1_OptRefTask` | 原始采样 5；当前 demo 的 `MemoryDump` 和 `MaintainDepI` 已迁入共享池 | memory dump、诊断队列、事件历史更新、依赖信息维护、optimizer refresh。 | `deps/oblib/src/lib/alloc/memory_dump.cpp`、`src/share/ob_event_history_table_operator.cpp` 等 |
| 基础运行时 | `T1_qth_mgr`、`T1_SignalHandle`、`seekdb` 主线程 | 3 | 动态线程池管理、信号处理、主进程线程。 | `deps/oblib/src/lib/thread/ob_dynamic_thread_pool.cpp`、`src/observer/ob_signal_handle.cpp` |

## 关于 `T1_TxLoopWorker`

`T1_TxLoopWorker` 容易被误读。它不是普通业务队列 worker，而是 `ObTxLoopWorker` 注册到 `ObTimerService` 上的 repeat timer task。

启动链路：

```text
ObTxLoopWorker::start()
  -> timer_.init("TxLoopWorkerTimer", ...)
  -> timer_.schedule(*this, LOOP_INTERVAL, true)
  -> ObTxLoopWorker::runTimerTask()
```

每轮执行逻辑：

1. 每 `5s` 触发一次 `runTimerTask()`。
2. 遍历全部 LS：`scan_all_ls_()`。
3. 每个 LS 上做 keep-alive、weak read timestamp 推进、readonly tx cleanup。
4. 按周期 gate 执行 tx gc、retain ctx gc、start_working retry、log callback pool 调整。
5. 每轮还会更新 max commit ts。

多个 `T1_TxLoopWorker` 线程不等价于多个 `ObTxLoopWorker` 实例并发。原因是 `ObTimerService` 后面有 `TimerWK` worker pool，timer worker 执行 `ObTxLoopWorker::runTimerTask()` 时会被设置成 `TxLoopWorker` 线程名，执行后线程名可能残留。因此采样中看到多个 `T1_TxLoopWorker`，更像是多个 timer worker 曾经执行过该 timer task。

## 事务提交 GTS 等待线程

`T1_TxTsWaiter` 不是通用定时器线程，也不是执行全部事务提交逻辑的 worker。事务提交路径在 commit version 尚未被 GTS 越过时，通过 `ObTxCtx::wait_gts_elapse_commit_version_()` 把当前 `ObTxCtx` 放入 `ObTxTimestampWaiter`。原专用线程在队列为空时无限等待；队列非空时每 500us 获取一次 GTS，并把已经满足 `commit_version <= GTS` 的事务移交给 `TxTsCb` callback pool。

当前 demo 在 mini mode 下把这段协调循环注册为共享池 `TxTsWaiter` Source，使用 `HIGH/max=1`，每个 quantum 最多移交 64 个事务；GTS 尚未推进时通过 delayed readiness 在 500us 后继续，因此空载时既没有轮询，也没有 `T1_TxTsWaiter` 固定线程。`TxTsCb` 回调可能进入事务上下文、完成提交或发送响应，仍保留在原有按需创建、可回收的独立 callback pool 中。

## DDL 队列和后台线程

小规格空载采样中，DDL 相关线程共有 5 类、6 个：

| 线程名 | 小规格数量 | 创建和任务来源 | 空载行为 | 优化判断 |
|---|---:|---|---|---|
| `T1_DDLQueueTh0` | 1 | `ObSrvDeliver::init_queue_threads()` 固定创建。原用途是串行处理普通 RootService DDL RPC | 当前 lite 分支的 `OB_RPC` 投递入口直接返回 `OB_NOT_SUPPORTED`；仓库中 `ddl_queue_` 只有初始化和停止引用，没有入队路径 | 当前属于遗留空线程，优先考虑删除 |
| `T1_DDLPQueueTh0` | 1 | 与 `DDLQueueTh` 同时创建，原用途是处理并行 DDL RPC；普通模式线程数为 `CPU/2`、范围 `1~24`，小规格固定为 1 | 当前同样没有入队路径 | 当前属于遗留空线程，优先考虑删除 |
| `T1_DDLTaskExecu` | 2 | `ObDDLScheduler` 在小规格固定创建 2 个，普通模式创建 8 个。消费 `ObDDLTaskQueue`，执行建索引、表重定义、约束、split、物化视图等高层 DDL 状态机及其重试 | 队列为空时通过 idler 最长等待 30s，但线程不会自行退出 | 适合改成 lazy start 或最大并发仍为 2 的 `0~2` 动态池 |
| `T1_DdlBuild` | 1 | `ObDDLReplicaBuilder` 的异步队列 worker；小规格为 1，普通模式为 16。执行 index SSTable build、重定义 build、约束校验、自增序列更新等细粒度子任务 | 初始化时立即创建；空队列使用 1s 超时 pop，不会自行退出 | 适合按需启动并在持续空载后退出，保留最大并发 1 |
| `T1_DDLTransCtr` | 1 | `ObDDLTransController` 管理并行 DDL 的 schema version 顺序；任务完成时设置 `need_refresh_`，唤醒线程发布 schema 并广播 consensus version | 无任务时阻塞在 `wait_cond_`，不轮询，空载 CPU 成本很低 | 可以合入其他串行 executor，但只能减少 1 个线程且顺序语义敏感，优先级最低 |

当前 DDL 主执行链路可概括为：

```text
SQL DDL / RootService
  -> ObSysDDLSchedulerUtil::schedule_ddl_task()
  -> ObDDLScheduler::task_queue_
  -> DDLTaskExecutor（高层 DDL 状态机）
  -> ObDDLReplicaBuilder::push_task()
  -> DdlBuild（细粒度 build 子任务）

并行 DDL transaction 完成
  -> ObDDLTransController::remove_task()
  -> need_refresh_ = true
  -> DDLTransCtr 发布 schema / 广播 consensus version
```

`DDLQueueTh` 和 `DDLPQueueTh` 不在这条当前主链路上。`ObSrvDeliver::deliver()` 已明确删除 obcall RPC transport，收到 `OB_RPC` 会直接丢弃；`ob_rs_serial_call.h` 也说明原来由单线程 `ddl_queue_` 保证的串行语义已经改由进程级串行锁实现。因此，这两个线程不是“当前没有 DDL 所以暂时空闲”，而是当前 lite 代码中已经没有任务来源。

建议按以下顺序优化：

1. 删除 `DDLQueueTh` 和 `DDLPQueueTh` 的创建、停止和成员变量，先减少 2 个确定无任务来源的线程。
2. 把两个 `DDLTaskExecutor` 改为按需启动、持续空载后退出，保留峰值并发 2。
3. 对 `DdlBuild` 使用相同的 lazy start/idle exit 策略，保留峰值并发 1。
4. 暂时保留事件驱动的 `DDLTransCtr`，除非后续有统一的 RootService 串行 executor 可以安全复用。

保守完成前三项后，真正空载时 DDL 相关线程可以从 6 个降到 1 个，同时不降低有 DDL 任务时的现有小规格并发上限。删除前需要覆盖普通 DDL、并行 DDL、建索引、失败重试、leader 切换和进程退出场景，重点检查 lazy worker 启停之间是否存在 lost wakeup。

## Tablet 元数据同步线程

`T1_TbltTblUp0` 是 `ObTabletTableUpdater` 唯一任务队列在小规格模式下创建的 1 个 worker。这里的“元数据上报”不是把完整 Tablet 对象发送到外部服务，而是把本地 Tablet 最新的 major compaction 结果和校验信息异步同步到控制面元数据表。

任务来源包括 Tablet 创建或变更、major/DDL merge 完成、compaction 状态更新，以及 `ObTenantMetaChecker` 发现本地 Tablet 与元数据表不一致。提交方只把 `(ls_id, tablet_id)` 放入去重队列；worker 再从本地 `ObTablet` 和 major SSTable 读取实际状态。

当前 seekdb lite 实现虽然仍保留 `report_tablet_to_rs()` 这个历史函数名，但没有向远端 RootService 发送 RPC。`T1_TbltTblUp0` 直接通过 `GCTX.meta_db_pool_` 写入本进程的 SQLite 元数据库，并在同一事务中更新：

- `__all_tablet_meta_table`：记录 `tablet_id`、major compaction SCN、数据大小、所需空间、report SCN 和状态；
- `__all_tablet_replica_checksum`：记录 compaction SCN、行数、数据 checksum、列 checksum 和 checksum 类型。

这些数据主要由 major merge 进度判断、checksum validator、medium compaction、诊断和虚拟表读取。因此，更准确的线程职责描述是“异步更新本地 Tablet 合并状态及 checksum 元数据表”，而不是笼统的“Tablet 元数据上报”。

该线程不是周期 timer，也不在空载时轮询所有 Tablet；它消费事件驱动的 `ObUniqTaskQueue`。小规格固定创建 1 个 worker，空队列时执行最长 200ms 的条件变量等待，因此会周期醒来检查队列，但不会扫描 Tablet。后续评估空载线程优化时，可以先改成无超时等待并在 stop 时显式唤醒，以减少空载唤醒；若要减少线程数，再考虑 lazy start/idle exit，并保证首次提交与 worker 退出之间不会丢任务。

## Compaction/存储维护线程

此前空载线程表中归到 Compaction/存储维护的 5 个线程，实际包含 major merge 控制、Tablet 元数据同步、读路径 BloomFilter 缓存维护和异步冻结，并不都是在执行 Compaction。

| 线程名 | 创建和任务来源 | 空载行为 | 优化判断 |
|---|---|---|---|
| `T1_MergeSchedul` | 原实现由 `ObMajorMergeScheduler` 固定创建 1 个专用线程；当前 demo 在 mini mode 下注册为共享池 `MergeScheduler` Source，非 mini mode 保留原线程 | Source 为 `NORMAL/max=1`；无合并时 10s 后再次检查，合并进行中每 1s 推进一步；freeze info detector 仍可立即唤醒 | 已接入共享池，mini mode 确定性减少 1 个专用线程；实际 major compaction DAG 仍由 DAG worker 执行 |
| `T1_TbltTblUp0` | `ObTabletTableUpdater` 小规格固定 1 个 worker；任务来自 Tablet 创建/变更、DDL/major merge 完成和元数据校正 | 空队列时最长 200ms 条件等待，不扫描 Tablet | 先改无超时等待并在 stop 时显式唤醒；进一步考虑 lazy start/idle exit |
| `T1_BFBuildTask` | 查询产生的 BloomFilter 空读累计超过阈值，且目标 SSTable 没有持久化 macro-block BF 时，提交去重 build 任务 | 真正无查询时没有业务任务，但固定线程每 500ms 醒来检查队列并执行队列 GC | 高优先级 lazy start/idle exit 候选 |
| `T1_MaBlkBFLoad` | 查询产生的 BloomFilter 空读累计超过阈值，且 SSTable 已保存 macro-block BF 时，提交加载任务 | 真正无查询时没有业务任务；空队列最长等待 10s，新任务可提前唤醒 | 高优先级 lazy start/idle exit 候选 |
| `T1_FrzAsync` | 原实现由 `ObTenantFreezer` 固定创建 1 个 Occam worker；源码检查确认当前 lite 分支没有任务提交方 | 始终空队列并无限期条件等待 | 当前 demo 已直接删除该空线程；如果以后恢复异步 freeze 提交，需要重新评估隔离执行模型 |

两个 BloomFilter 线程由同一个读路径入口分流：

```text
查询空读次数超过 BloomFilter miss 阈值
  ├─ SSTable 已保存 macro-block BF -> MaBlkBFLoad 加载到缓存
  └─ SSTable 没有保存 BF          -> BFBuildTask 扫描宏块并构建
```

因此，在真正没有 SQL 请求的空载状态下，这两个线程都没有任务，只是初始化时被固定创建。最值得先做的线程数优化是让它们按需启动并在持续空载后退出，可以减少 2 个空载线程；之后再对 `TbltTblUp0` 做 lazy start/idle exit，可再减少 1 个。当前 demo 又把 mini mode 的 `MergeSchedul` 接入共享池，并删除了没有任务提交方的 `FrzAsync` 空线程；这 5 个原始常驻线程均已有对应优化路径。

### MergeScheduler 共享池迁移

`ObMajorMergeScheduler` 的职责是控制一轮 major merge 的状态推进，不负责执行具体 Tablet 合并。原专用线程的循环已在 mini mode 下拆成有界 quantum：

```text
freeze info detector / 10s 空载检查
  -> 通知共享池 MergeScheduler Source
  -> reload + 读取 global merge info
  -> 若有新 frozen SCN，初始化本轮进度检查
  -> 执行一次 check_progress/update_merge_status
  -> 未完成：1s 后再次通知
  -> 已完成：更新 last_merged_scn，恢复 10s 空载检查
```

每个 quantum 最多推进一次进度检查，不能在共享 worker 中 `sleep` 或独占线程等待；Source 总并发上限为 1，因此不会并发推进同一轮状态机。原有的失败退避、pause/resume、暂停 30 分钟后清理进度缓存和唤醒语义均保留。真正的 major merge 仍提交到 DAG scheduler，由 `T1_MAJOR_MERGE/*` 动态 DAG worker 执行。

2026-07-29 的 2G 小规格实测中，启动后以及 major freeze 完成后的 `/proc` 快照均不存在 `T1_MergeSchedul`。触发 `ALTER SYSTEM MAJOR FREEZE` 后：

- `frozen_scn/global_broadcast_scn/last_merged_scn` 从 `1/1/1` 推进到同一个新 SCN；
- `merge_status=0`、`is_merge_error=0`，测试表 1024 行保持可读；
- 日志中的调度步骤由 `T1_BGTask0` 约每 1s 执行一次，实际合并任务仍使用 `T1_MAJOR_MERGE/*`；
- 完成后线程快照为 51 个线程，其中共享 `T1_BGTask0` 为 3 个，没有新增专用调度线程。

这项迁移的确定性收益是 mini mode 减少 1 个常驻线程。总线程瞬时值仍会受 DAG、TimerService、IO callback 等动态线程影响，不能只用单点总数判断收益。

### TFSwap 共享池迁移

`TFSwap` 原来是一个固定专用线程，把临时文件 WBP shrink、clean page 淘汰、同步 SwapJob 和 flush 状态机串在同一个循环中。当前 demo 只在 mini mode 下将这个控制循环注册为共享池 Source，非 mini mode 保留原线程：

```text
普通 tmp file 写入完成
  -> 发布可合并的 LOW readiness
  -> 串行推进一次 shrink -> swap -> flush

WBP 分配失败，前台同步等待换页
  -> enqueue SwapJob 与发布 HIGH readiness 使用同一个生命周期门闩
  -> HIGH 优先推进 swap；clean page 不足时切 FAST flush
  -> 满足、超时或 stop 后唤醒前台
```

Source 总并发固定为 1，因此 HIGH/LOW 不会并发修改原状态机。dirty/write-back page、待淘汰 clean page、WBP shrink 或 flush 内部队列仍活跃时，继续按原 5ms/1s 节奏推进；完全空闲后改为 60s 维护检查，普通写入可立即重新唤醒 LOW。stop 会先关闭提交门闩、注销 Source，确认没有运行中的 quantum 后再以 `OB_IN_STOP_STATE` 唤醒残留 SwapJob，避免 enqueue 与 unregister 交错时永久等待。

2G 小规格外排压力验证构造了 524288 行、约 256MiB 数据，在 1% SQL work area 下执行预期触发外排的排序，约 18 秒完成且结果正确。启动和压力全程没有 `T1_TFSwap`；共享池由 2 扩到 3。压力结束后总线程降到 41，但第 3 个共享 worker 仍长期存在，因此 mini mode 的确定性收益是删除 1 个专用线程，后续还需单独改进“多个周期 Source 使所有 worker 都无法连续空闲 30 秒”的池级回收策略。

### IO_HEALTH 共享池迁移

`IO_HEALTH` 原来是 `ObIOFaultDetector` 内固定创建的单 worker `ObSimpleThreadPool`。任务不是周期性扫描产生，而是由实际 IO 事件触发：

- 慢 IO timing task；
- IO timeout；
- read failure。

当前 demo 在 mini mode 下把这三类 `RetryTask` 放入 detector 自己的有界队列，并向共享池发布 `HIGH` readiness。Source 的 `max_concurrency=1`，每个 quantum 取一个任务，继续调用原 `handle()` 完成 detect read、指数退避重试及 device warning 判定；因此同一设备上的故障探测仍保持串行。`ObIOManager` 比 server runtime 更早启动，故采用“先启动 detector/接收任务，后 attach executor”的生命周期，attach 时若队列已有任务会补发一次 readiness。非 mini mode 保留原来的专用线程。

这一版按“直接塞进共享池”实现，没有先把同步重试改成 5 秒定时器。正常空载时队列为空，不占用 worker；磁盘异常时，单个任务可能在默认约 5 秒的 warning tolerance 窗口内持续探测，期间占用一条共享 worker。由于 Source 总并发为 1，它不会同时扩出多条 worker，但这仍是共享池的已知隔离风险。后续若观察到其它 `HIGH` 任务被拖慢，可再将 `handle()` 拆成一次探测一个 quantum、通过 `next_ready_ts` 延迟重试的状态机。

2G 小规格实测在全新数据目录启动并空载 211 秒：终点共 45 个线程、2 个 `T1_BGTask0`，始终没有 `T1_IO_HEALTH0`；SQL 可正常执行，日志没有 Source 注册、容量或 quantum 错误，SIGTERM 后也能正常完成 detach 和退出。该测试验证了空队列和生命周期路径；真实磁盘故障下约 5 秒同步占用 worker 的行为仍需用故障注入单独验证。

## PALF、PLOG、SLOG 和同步 IO 线程

原始线程表把日志附近的线程统一写成了“LogService/PALF 约 7 个”，这个分类不准确。原实现固定可见 5 个，`LogIOCB0` 和 `IO_SYNC_CH0` 还会短暂出现；当前 demo 的 mini mode 已把 SLOG、LogLoop、LogIOCB 和 SyncIO 接入共享池，这组只固定保留 PALF `IOWorker`、PLOG 和异步 IO completion。

准确归属如下：

| 线程名 | 数量和生命周期 | 任务来源与职责 | 空载等待方式 |
|---|---:|---|---|
| `T1_LogLoop` | 原实现 1 个固定；当前 demo mini mode 为 0 | PALF 专用控制循环：切换状态、检查 freeze mode、按周期冻结最后一段日志、统计日志盘使用量 | 原线程默认每 100ms 执行一轮、period-freeze mode 缩短到 1ms；当前由同周期 `PALFLogLoop` Source 推进 |
| `T1_IOWorker` | 1，固定 | 消费 PALF `LogIOTask` 队列，执行 flush log/meta、truncate log、truncate prefix blocks 和 purge throttling | 队列 `pop` 超时为 100ms，空载也会周期醒来 |
| `T1_LogIOCB0` | 原实现 0~1，动态；当前 demo mini mode 稳态为 0 | PALF IO 完成后的 `after_consume()`：推进滑动窗口、执行持久化完成回调并释放 IO task | 当前由共享 `LogIOCallback HIGH/max=1` Source 非阻塞消费原队列；共享池饱和时最多临时启动 1 个 rescue worker |
| `T1_OB_PLOG` | 1，固定 | 进程诊断日志异步 writer，写 `seekdb.log` 等普通 INFO/WARN/ERROR 日志；不是数据库 redo | producer 提交日志时唤醒；无日志时最长等待 500ms |
| `T1_OB_SLOG` | 原实现 2 个固定；当前 demo mini mode 为 0 | 两套存储元数据 redo writer：`slog/server` 保存 tenant 创建/删除、tenant super block 和 unit 信息；`slog/sys` 保存 LS、Tablet 等租户存储元数据 | 原实现各自消费独立队列、空队列最长等待 1s；当前仍保留两条队列，但由两个共享 Source 事件驱动 |
| `T1_IO_SYNC_CH0` | 原实现 0~若干，动态；当前 demo mini mode 稳态为 0 | 通用 `ObSyncIOChannel` worker，执行同步 `pread/pwrite` 等请求。PALF 和数据存储共用本地设备，但该线程不专属于 PALF | 当前由共享 `SyncIO HIGH/max=1` Source 非阻塞消费原队列；共享池饱和时最多临时启动 1 个 rescue worker |

### 三条 PALF 线程

原实现的 `LogLoop` 不是 timer service 任务，也不消费队列，而是一个独立线程中的 sleep loop：

```text
LogLoopThread::log_loop_()
  -> check_and_switch_state()
  -> check_and_switch_freeze_mode()
  -> period_freeze_last_log()
  -> period_calc_disk_usage()
  -> ob_usleep(剩余周期)
```

默认周期在 `log_define.h` 中固定为 100ms。因此它在真正无业务请求时仍约每秒醒 10 次。本次约 4 分钟的 `/proc` 样本中，`LogLoop` 有 2474 次 voluntary context switch，与 100ms 周期基本吻合。

当前 demo 在 mini mode 下把一轮控制逻辑提取成 `PALFLogLoop` quantum：10ms 的状态切换 gate、1s 的 freeze-mode 检查、`period_freeze_last_log()` 和磁盘使用量统计都保持不变；默认通过 delayed readiness 在 100ms 后继续，进入 period-freeze mode 仍使用原 1ms 周期。redo `IOWorker` 和 IO callback 不在该 Source 中。

`IOWorker` 是 PALF 真正的 redo IO worker。当前 lite 无论配置如何都把 `real_log_writer_parallelism` 固定为 1，所以只创建一个。它不是周期性“每 100ms 刷一次日志”，而是一个单消费者、多生产者的串行 IO 队列：任务提交时会主动唤醒 worker，100ms 只是队列为空时 `pop()` 的最长等待时间，用于定期检查 stop 状态。

普通 redo 日志的主要提交链路为：

```text
事务或 Follower 收到日志
  -> PalfHandleImpl::submit_log()
  -> LogSlidingWindow::submit_log()
  -> LogSlidingWindow::handle_next_submit_log_()
  -> LogEngine::submit_flush_log_task()
  -> LogIOWorker::submit_io_task()
  -> T1_IOWorker 串行执行
```

进入 `IOWorker` 队列的任务共有 5 类：

| 任务类型 | 实际操作 | 主要来源和用途 |
|---|---|---|
| `FLUSH_LOG_TYPE` | 通过 `inner_append_log()`、`LogEngine::append_log()` 和 `LogStorage::writev()` 写 redo 日志 | 本机事务生成的日志，以及 Follower 接收或拉取补齐的日志；这是最常见的任务 |
| `FLUSH_META_TYPE` | 持久化 PALF 控制元数据 | Proposal/prepare、成员配置、访问模式、snapshot/base LSN、replica property 等状态变更 |
| `TRUNCATE_LOG_TYPE` | 从指定 LSN 截断日志尾部 | Reconfirm、回滚或恢复过程中删除不一致的尾部日志 |
| `TRUNCATE_PREFIX_TYPE` | 删除指定 LSN 之前的旧日志块 | Base LSN 推进、日志回收和 Rebuild |
| `PURGE_THROTTLING_TYPE` | 本身不写磁盘，在同一串行队列中充当顺序屏障 | 保证此前相关写任务经过后才能解除写限流，常见于 Reconfirm、配置检查和日志拉取状态切换 |

`IOWorker` 会把队列中已经连续积累的、属于同一个 PALF 日志流的 `FLUSH_LOG_TYPE` 聚合成 `BatchLogIOFlushLogTask`，再通过一次 `writev()` 批量写入。它不会为了凑批主动 sleep：队列中已有多个连续 flush task 就合并，只有一个就立即执行；遇到 meta、truncate 或 throttling task 就结束当前批次，以保持任务顺序。

IO 完成后，`IOWorker` 不直接执行完整的日志状态推进，而是把任务投递给 `LogIOCB0`：

```text
T1_IOWorker
  -> 完成 PALF 定义的物理写入
  -> LogIOCB0::handle()
  -> LogIOTask::after_consume()
  -> LogSlidingWindow::after_flush_log()
  -> 更新 max_flushed_lsn 和本副本 match LSN
  -> Leader 尝试推进 committed LSN，或 Follower 向 Leader 发送 flush ACK
  -> 触发后续日志处理和上层提交回调
```

两阶段拆分的目的，是让 `IOWorker` 专注于持久化顺序和批量写吞吐，避免滑动窗口推进、网络 ACK 和上层回调阻塞后续磁盘写。每个异步 IO task 还记录 `palf_id` 和 `palf_epoch`，执行 IO 和回调前都会校验当前 epoch；如果 PALF handle 已经重建或切换到新 epoch，旧任务会被忽略，防止过期异步任务修改新实例状态。

本次样本中 `IOWorker` 有 3619 次 voluntary context switch，除了空队列的 100ms 超时外，还包含少量后台 PALF IO。因为队列 `push()` 本身会发信号，新任务不需要等待下一个 100ms 周期，所以把等待时间延长不会增加正常写入的理论排队延迟；但如果 stop 没有显式唤醒，线程退出的最坏等待会相应延长。

较小改动是把 100ms 延长到 1s，可以把纯空载超时唤醒降低约 10 倍。更完整的方案是空队列时无限阻塞，并在新任务提交和 stop 时显式唤醒，同时保留退出前 drain 剩余队列的语义。这些修改只能降低空载 context switch，不会减少线程数。考虑到 `IOWorker` 位于持久化和顺序保证主链路，不建议做 idle exit，也不建议为了减少一条线程而与其他 IO executor 合并。

`LogIOCB0` 处理 `IOWorker` 完成磁盘 IO 之后的回调。原动态池虽然能回收到 0，但启动和写入突发仍会创建短生命周期 pthread。当前 demo 保留原 callback queue、task 所有权、Palf epoch 校验和 stop/drop 语义，只把队列设为 external-driver，由 `LogIOCallback HIGH/max=1` Source 每个 quantum 处理一个 callback。若 Source 通知失败，或共享池达到 8 条上限且没有 idle worker，原队列最多临时拉起 1 个 rescue worker，避免 PALF 持久化完成链路被共享任务饿死；队列清空后 rescue worker 按原动态池规则退出。

### `OB_PLOG` 和两条 `OB_SLOG`

`OB_PLOG` 容易被名字误导，它是进程日志 writer，不是 PALF/事务 redo。所有线程产生的普通诊断日志先提交到 2MB ring buffer，再由唯一的 `OB_PLOG` 线程批量写文件。空队列等待上限为 500ms，本次样本中有 510 次 voluntary context switch，约等于每秒 2 次。由于任意模块和异常路径都可能写日志，保留唯一专用 writer 更稳妥；可以把空队列等待改为无超时条件等待来降低唤醒，但不建议 lazy exit。

原实现的两个 `OB_SLOG` 不是因为任务堆积扩容，而是启动时明确创建的两套独立 writer：

```text
ObServerStorageMetaService
  -> slog/server
  -> tenant 创建/删除、super block、unit 等服务级元数据

ObTenantStorageMetaService
  -> slog/sys
  -> LS 创建/删除/更新、Tablet 更新/删除等租户存储元数据
```

二者使用不同目录和独立的 replay 顺序，写调用还会等待对应日志落盘，因此不能简单删掉一个。每条 writer 空队列最长等待 1s；本次两个线程的 voluntary context switch 分别为 260 和 1442。较低的一个接近纯 1s timeout，较高的一个说明空载期间仍有存储元数据维护写入。

当前 demo 已按“共享物理 worker、保持两套逻辑队列”的方式实现。`SLOGLocal` 和 `SLOGServer` 是两个独立 Source，各自 `max=1`，没有合并目录、文件游标、队列或 replay 顺序；共享池只调度哪条流下一次获得执行机会。每个 quantum 最多 flush 16 批，随后重新参与共享调度，写调用仍等待其所属日志项落盘完成。

server SLOG 在共享 executor 创建前就要参与 bootstrap/replay，因此启动阶段先使用原 base flush thread。runtime 初始化完成后，在存储日志构造互斥锁保护下停止并 join bootstrap consumer，再注册 `SLOGServer`，保证同一队列不存在两个并发消费者；local SLOG 创建较晚，直接注册 `SLOGLocal`。runtime 正常停止或初始化失败清理时，必须先 detach server SLOG、恢复 base flush consumer，再销毁共享 executor，否则后续 abort SLOG 会等待一个已经不存在的消费者。完整 `test_io_manager` 22 个用例覆盖了该启动失败和正常 teardown 路径。普通模式不变，仍使用原专用线程。

2G 实测覆盖了建表、插入、更新、显式事务、优雅退出和同数据目录重启；重启后 3 行、总值 75 的数据保持完整。约 9 分钟空载点为 32 个总线程、2 个共享 worker，没有 `T1_OB_SLOG`。这项迁移确定性删除 2 个 SLOG 专属线程，但总线程单点仍会被 IO callback、DAG 和 TimerWK 等动态线程影响。

### `IO_SYNC_CH0` 为什么原来会瞬时出现两个

`IO_SYNC_CH0` 来自通用 `ObSyncIOChannel`，不是 PALF callback。原实现中 `sync_io_thread_count=0` 表示最大线程数按 CPU 自动计算，但动态池最小值仍是 0；同步 IO 入队且没有 idle worker 时才扩容，持续空闲约 2s 后回收。因此，历史样本看到两个只说明采样瞬间有至少两个同步 IO worker 被拉起，不代表固定预留了两个线程。

当前 demo 继续使用同一 bounded queue 和同步请求完成协议，但在 mini mode 下由 `SyncIO HIGH/max=1` Source 调用原 `do_sync_io()`。磁盘异常时一次 `pread/pwrite` 可能阻塞，因此只允许占用一条共享 worker；共享池满载且无 idle worker时最多临时启动 1 个原生 SyncIO rescue worker，避免前台同步等待和共享池形成闭环。

当前线程命名还有一个干扰：`ObSimpleThreadPoolBase::Worker::run1()` 会把池的 `cur_worker_idx_` 写回当前 worker 的 `idx_`，可能覆盖创建 worker 时已经递增的计数。因此多个 worker 可能都显示成 `IO_SYNC_CH0`，不能仅根据相同后缀判断它们来自两个 channel。

### 优化优先级

这组线程的保守判断是：

1. `LogIOCB0`、`IO_SYNC_CH0` 原本就能回收到 0；当前 demo 进一步把正常消费交给共享池，只在共享池饱和时允许短暂 rescue。
2. `OB_SLOG` 已迁为两个事件驱动 Source；`IOWorker`、`OB_PLOG` 仍可把超时轮询改为可靠事件唤醒，以降低空载 context switch，但不会减少线程数。
3. `LogLoop` 控制循环已迁为 `PALFLogLoop` Source，但 1ms period-freeze 模式仍需做延迟和共享池干扰压测。
4. 两套 SLOG 已共享物理 executor 并保留两套文件、队列和流内顺序，在 mini mode 下确定性减少 2 个专属线程。

因此，这一组当前 demo 真正固定的是 2 个线程：`IOWorker` 和 `OB_PLOG`；`LogIOCB0`、`IO_SYNC_CH0` 正常路径不再创建专属 worker，两条 SLOG、PALF 控制循环和 callback/sync IO 由后台共享池按需执行。

### `ApplySrv` 共享池迁移

`T1_ApplySrv0` 消费的不是 redo 写盘任务，而是 PALF append 完成后的 callback token。每个 `ObApplyStatus` 内仍有 16 条按 SCN hash 分流的 callback 顺序队列；外层 bounded link queue 只保存当前哪些内部队列需要推进，task lease 保证同一 token 不并发执行。当前 demo 让这个原 link queue 进入 external-driver 模式：入队容量、lease、引用计数和 stop/drop 均不变，但不再自行创建 worker；`ApplyService` Source 每个 quantum pop 一个 token，继续使用原 100ms time slice，未完成任务按原逻辑重新入队。

验证先串行提交 200 个事务，再由 8 个连接并发提交 800 个事务，最终 1000 行、总值 3421500 全部可见；211 秒时没有 `T1_ApplySrv0`。提交压力后共享池为 3 个 worker，因此这项迁移只能确定“减少 1 个专属线程”，不能用该单点证明总线程净减 1。优雅退出和同目录重启正常，重启后继续提交、更新结果正确。

## MemoryDump 和两个 TimeWheel

`T1_MemoryDump` 原来固定等待信号或虚拟表触发的 dump/stat 请求，空载时几乎不工作。当前 demo 在 mini mode 下只保留原 pending bitmap 和任务对象，把唤醒出口接到 `MemoryDump` Source；一个 quantum 原子取走一批 pending 请求，执行期间到达的新请求留给下一轮。使用真实 `kill -62` 验证时，任务在 `T1_BGTask0` 上完成并生成 `log/memory_meta`，没有创建 `T1_MemoryDump`。

`T1_DetectorTime` 和 `T1_TransTimeWhe` 原来分别以 10ms、100ms 精度持续扫描 TimeWheel。它们不是普通 repeat timer：每个 `TimeWheelBase` 有 10000 个 bucket，task 的 schedule/cancel、task lock、运行引用和 callback 后释放顺序都由现有实现维护。当前 demo 没有替换这些数据结构，只把扫描循环拆成共享 Source quantum：

```text
DetectorTimer / TransTimeWheel Source
  -> 轮转各 TimeWheelBase
  -> 本轮总计最多扫描 64 步
  -> 有到期 backlog：立即续跑
  -> 没有到期 backlog：10ms / 100ms 后再次 ready
```

单测验证了 50ms task 到期执行、200ms task 取消后不执行以及 stop/unregister。同一个 2G 实例在 217 秒时为 27 个总线程、2 个 `T1_BGTask0`，在 550 秒时因 `IO_SYNC_CH0` 和 `LogIOCB0` 瞬时出现为 30 个；两个点都没有 `T1_DetectorTime`、`T1_TransTimeWhe`、SLOG 或 MemoryDump 专属线程。同数据目录重启后原 2 行、总值 30 的事务数据保持完整，1 秒行锁等待超时约 929ms 返回。

这仍是高风险实验项。10ms 空扫会持续发布 delayed readiness，可能把原专属线程成本转换为共享池调度和 TimerService token 成本；目前 1 秒采样没有看到常驻 TimerWK 增加，但还需要记录 TimerService 分配、共享 worker CPU和 callback 延迟分位。两会话互锁测试中，两个事务都在约 10.9 秒后超时，未观察到死锁 victim；没有 baseline A/B 前不能断定是迁移回归，也不能把死锁检测视为已验证通过。

## DAG 调度线程可优化点

复用已经完成 bootstrap 的数据目录重启，确认 `need_bootstrap=false` 后，连续空载观察约 5 分钟：

- 启动收尾阶段只产生 6 个 DAG：3 个 `MDS_MINI_MERGE` 和 3 个派生的 `MINOR_EXECUTE`；
- 最后一个 DAG 在启动约 7 秒时完成，此后没有新增 DAG；
- `T1_DAG` worker 从 4 个开始，约每分钟回收 1 个，最终回收到 0；
- worker 回收由 `ObTenantDagScheduler::try_reclaim_threads()` 和 `ObReclaimUtil::compute_expected_reclaim_worker_cnt()` 完成，因此 DAG worker 已具备按需扩缩能力，不是稳态空载线程优化重点；
- worker 全部回收后，`T1_DagScheduler` 仍保留 1 个线程，持续执行 DAG/DAG net 检查、调度和条件等待。

可优化方向是让 `T1_DagScheduler` 也具备 lazy start 和 idle exit：首次提交 DAG 或 DAG net 时启动调度线程；当 DAG 队列、DAG net、running task 和 worker 持续为空一段时间后退出；后续提交再重新启动。该方案最多减少每个 tenant 1 个常驻线程，收益有限，建议作为低优先级优化点。

实现时需要重点保证 scheduler 启停状态机正确，避免并发提交与退出之间发生 lost wakeup，同时覆盖 DAG net、tenant stop/destroy 和重复启动场景。若只降低调度循环的唤醒频率，只能减少空载唤醒，不能减少线程数。

## Change Stream 按需启动实验

原实现无论是否存在异步向量索引，都会固定启动 `T1_CSFetcher` 和 `T1_CSDispatcher`。源码和运行日志确认，普通实例中 Fetcher 长期处于 `IDLE`，Dispatcher 的事务 ring buffer 为空，但 Fetcher 仍承担两项不能直接删除的维护工作：

- 每 200ms 推进 change stream refresh SCN，供 `dbms_index_manager.refresh()`、fork table/database 等等待；
- 每 5 秒把 `change_stream_min_dep_lsn` 推进到 PALF 尾部，避免 checkpoint/CLOG 回收被旧值限制。

共享池 demo 在 mini mode 下增加 `CSIdleMaint` Source。没有 `sync_mode=async` 向量索引时，它在共享 worker 中复用上述维护逻辑，不创建两个专用线程；schema publish 会立即唤醒它检查最新 schema。发现异步索引后，它只负责启动原 `CSWorker`、`CSDispatcher` 和 `CSFetcher`，日志读取、事务切分、保序提交及向量索引写入仍由原专用执行模型处理。

验证结果：

- 无异步索引时，两个专用线程全程为 0，同时 `REFRESH_SCN` 和 `MIN_DEP_LSN` 都持续前进；
- 创建 async HNSW 索引后，在 250ms 采样粒度内观察到两个线程启动；
- 插入 3 行向量数据后，`dbms_index_manager.refresh()` 成功，近邻查询按预期返回 3 行；
- 已激活状态下优雅退出正常；
- 清理异步索引并重启后，3 分钟 181 次空载采样为：总线程 `min=37`、`max=47`、`avg=40.67`、`final=41`，两个 Change Stream 专用线程全程为 0。

对照上一轮同数据目录的 `min=39`、`max=49`、`avg=42.8`、`final=40`，最小值、最大值都确定性下降 2，平均下降约 2.13。终点线程数会受 `ReqWorker`、IO callback 等短生命周期线程影响，因此不能只比较单个终点。当前实现不在最后一个异步索引删除后动态停止专用组件；它们一旦激活便保留到进程退出，以避免第一阶段引入 LSN 重新定位和 in-flight transaction 停启状态机。

## 结论

空载 3 分钟后的线程总数主要来自常驻模块线程，而不是单一 timer 同相位触发导致的临时线程。

较大的 baseline 来源包括：

- SQL NIO 固定 8 个，本实验按约定没有处理；
- mini mode 不再保留独立 TimerWK；到期 callback 由 `TimerService HIGH/max=2` Source 消费，执行过事务 timer 后共享 worker 的线程名仍可能残留为 `TxLoopWorker`；
- DAG worker 在启动突发后仍按原模型自适应回收到 0，DAG scheduler 已迁为 Source；
- PALF `IOWorker`、PLOG、IO event、TimerSvr、ClockGenerator、请求 worker 和系统基础线程仍保留原执行模型；
- 已迁入的 29 个 Source 在最终空载实验中共用稳定的 5～6 个物理 worker，而不是每个 Source 预留线程；global/runtime `DiskCallback` 各占一个逻辑 Source，所以只剩 3 个静态 Source Slot。

TimeWheel 阶段在 217 秒点观察到 27 个线程，550 秒点因原 `IO_SYNC_CH0`、`LogIOCB0` 动态出现为 30 个；后续已将这两条动态 callback/sync IO 路径也接入共享池。不能拿跨阶段任意单点直接相减，但专属线程缺失可以确定：`OB_SLOG` 2 个、`MemoryDump`、`DetectorTime`、`TransTimeWheel`、`ApplySrv`、`LogLoop`、`LogIOCB` 和正常路径的 `IO_SYNC_CH` 均已消失，且对应基本功能已验证。

继续压线程数时，已经没有新的简单队列 worker。`TimerSvr` 是所有 delayed Source 的 deadline 依赖，`ClockGenerator` 是全局时间源，`OB_PLOG` 不能依赖会写日志的共享 executor，PALF `IOWorker` 和 IO event loop 又是共享任务同步等待的完成依赖；这些线程直接合池会引入自依赖、永久占用 worker 或池内死锁。下一步若继续实验，应优化这些线程内部的 timeout wakeup，或为持久化/阻塞路径建立独立物理 lane，而不是继续把它们直接塞入当前同一组 worker。

## Timer jitter 实验补充

后续做过一个最小实验：在 `ObTimerService::schedule_task()` 内部对普通 repeat timer 的首次触发时间加 stable jitter。

实验策略：

- 只影响 `repeate=true && immediate=false`；
- 一次性 timer 不变；
- immediate repeat timer 不变；
- 后续 repeat 周期仍然使用原始 `delay`；
- `delay < 1s` 不加 jitter；
- jitter 窗口为 `min(delay / 10, 1s)`。

同样小规格空载约 3 分钟后的结果：

- 总线程数：约 `62~63`
- `T1_TxLoopWorker`：约 `6`
- RSS：约 `236~239 MB`

对比原始版本：

- 原始版本 3 分钟点：总线程约 `67~68`，`T1_TxLoopWorker` 约 `9`
- jitter 版本 3 分钟点：总线程约 `62~63`，`T1_TxLoopWorker` 约 `6`

结论：jitter 可以轻微削平启动期/空载早期 timer 同相位造成的 worker 扩张，但对总线程数不是决定性优化。总线程基线仍主要由各模块常驻线程池决定。

## TimerWK 扩缩容归因实验

为了区分“线程名残留”和真实的 `TimerWK` worker 数量，后续在以下位置加入了临时诊断日志：

- `ObSimpleThreadPoolBase::push()`：记录无空闲 worker 时的扩容；
- `ObSimpleThreadPoolBase::run1()`：记录最后一个空闲 worker 取到任务后的补充扩容，以及空闲收缩；
- `ObTimerTaskThreadPool::handle()`：记录 `TaskToken` 类型、周期和执行耗时。

诊断日志只用于本次实验，采样结束后已经从源码中移除。原始日志保存在：

```text
/tmp/seekdb_timer_pool_diag_40102/log/seekdb.log
```

### 扩缩容结果

`TimerWK` 在启动后的约 30 秒内从 `0` 扩大到 `7` 个 worker，此后空载观察 3 分钟：

- 最大 worker 数：`7`
- 稳定 worker 数：`7`
- 扩容次数：`7`
- 收缩次数：`0`
- 同一毫秒内最多开始执行的任务数：`6`
- 3 分钟内执行 timer task：`12,957` 次
- 任务累计执行时间：`2.267s`
- 折算平均活跃 worker：`0.0126`

这说明 `7` 个 worker 不是实际计算量需要。绝大多数时间 worker 都在等待，线程数被高频唤醒和瞬时批量触发维持住了。

### 直接触发扩容的任务

“直接触发”是指某个任务取走最后一个空闲 worker，命中 `last_idle_popped` 扩容条件。它不等于该任务单独占满了线程池；同一时刻已经被其他 worker 取走的任务也共同构成了并发批次。

| 扩容阶段 | 直接触发任务 | 周期 | 功能 | 同批任务特征 |
|---|---|---:|---|---|
| `0 -> 2` | `ObLogCompressorTimerTask` | 5s，immediate | 扫描和压缩归档系统日志 | 第一个任务启动 worker；worker 取到任务后再补一个空闲 worker |
| `2 -> 4` | `ObKVGlobalCache::KVStoreWashTask`、`palf::BlockGCTimerTask` | 200ms、30ms | KVCache 淘汰/回收；PALF 日志块回收检查 | 两个高频任务在启动早期连续消耗空闲 worker |
| `4 -> 5` | `palf::BlockGCTimerTask` | 30ms | 调用 `PalfEnvImpl::try_recycle_blocks()` | 同一批还有 SQL plan monitor recycle、tenant freezer、index usage 配置刷新 |
| `5 -> 6` | `palf::BlockGCTimerTask` | 30ms | PALF 日志块回收检查 | 同一批还有 KVCache wash、tenant meta table GC、standby schema refresh |
| `6 -> 7` | `ObTabletGCService::ObTabletChangeTask` | 5s | 遍历 LS，检查 tablet persist/GC trigger | 同一时刻还有 standby schema refresh、SQL session GC、tenant meta table GC、checkpoint、mview MDS 任务，共 6 个任务在约 1ms 内开始 |

最后一次扩容最能说明问题：6 个任务在一个调度批次内同时到期，前 5 个 worker 分别取走任务，最后一个空闲 worker 取到 `ObTabletChangeTask` 后触发第 7 个备用 worker。任务本身并不重，其中这批任务多数只执行几十到一千微秒。

### 3 分钟内最高频的任务

| 任务 | 次数 | 周期 | 平均耗时 | 主要功能 |
|---|---:|---:|---:|---|
| `palf::BlockGCTimerTask` | 5,501 | 30ms | 93us | PALF block 回收检查 |
| `omt::ObMultiTenant` | 1,773 | 100ms | 84us | 调用 `tenant_->timeup()`，并按 gate 刷新系统租户配置 |
| `ObKVGlobalCache::KVMapReplaceTask` | 892 | 200ms | 100us | KVCache map 节点替换 |
| `ObKVGlobalCache::KVStoreWashTask` | 891 | 200ms | 284us | KVCache wash 和 hazard domain 回收 |
| `palf::LogUpdater` | 359 | 500ms | 73us | PALF 日志状态更新 |

这些亚秒级任务本身很轻，但会持续唤醒 worker。当前 worker 的收缩条件是连续发生队列 pop 超时：第一次约 1 秒超时只记录 `idle_since`，再空闲约 1 秒才尝试收缩。30ms、100ms、200ms 周期任务会让多个 worker 轮流取到任务并重置自己的空闲计时，因此本次 3 分钟样本没有发生一次收缩。

另外，`MIN_WORKER_THREAD_NUM = 4` 并不表示常驻下限是 4。`ObSimpleThreadPoolBase::init(4, ...)` 最初只把最大值设为 4，最小值仍为 0；随后 `set_thread_count(128)` 又把最大值改成 128，所以当前实际范围是 `0~128`。

### 对首次 jitter 的修正结论

本次诊断运行在“仅首次触发增加 jitter”的版本上，但仍然出现了最多 6 个任务在同一毫秒开始，原因包括：

- 30ms、100ms、200ms 等小于 1 秒的任务完全不加 jitter；
- `immediate=true` 的 repeat timer 不加 jitter，例如日志压缩任务；
- jitter 只影响第一次触发，后续按“本轮完成时间 + 原周期”重新调度，多个 timer 的相位会继续漂移并再次相遇。

因此，首次 jitter 只能削弱部分启动同相，不能解决高频任务让 worker 无法收缩的问题。

### 可合并的定时器

这里所说的“`TxLoopWorker` 导致线程数扩展”，准确含义是多个共享 `TimerWK` worker 执行过定时任务后残留了 `TxLoopWorker` 线程名，并不表示系统创建了多个 `ObTxLoopWorker` 实例。真正需要减少的是独立 timer task 的入队次数和同一时刻的到期批次。

`ObTxLoopWorker` 本身已经是一个合并定时器的正面例子：只注册一个 5s repeat timer，再在 `runTimerTask()` 内用时间 gate 分别执行 tx gc、retain ctx gc、start-working retry、log callback pool 调整、事务配置刷新和逐 LS 维护。可以把这种“一个模块一个 maintenance timer，内部按 elapsed time 分频”的方式继续应用到其他模块。

| 优先级 | 可合并对象 | 当前周期 | 合并方式 | 预期收益与注意点 |
|---|---|---:|---|---|
| 高 | `ObKVGlobalCache::KVMapReplaceTask` + `KVStoreWashTask` | 都是 200ms | 保留一个 `KVCacheMaintenanceTask`，一轮依次执行 `replace_map()`、`wash()` 和 hazard-domain 回收 | 同一 owner、同一周期、当前每 200ms 独立入队两次，是最小且风险最低的候选；可直接减少一半该模块的 timer 入队和唤醒 |
| 高 | `ObCheckPointService::ObCheckpointTask` + `ObTraversalFlushTask` | 都是 5s | 合成一个 checkpoint maintenance task，固定顺序执行 checkpoint 与 traversal flush | 同一 service、同一周期；需要确认二者串行后的最坏耗时和原有错误隔离语义 |
| 中 | `CheckClogDiskUsageTask`、`AdvanceCheckpointTask` 合入 checkpoint maintenance task | 2s、60s | 使用较短基础 tick，在回调内按 elapsed time 判断各子任务是否到期 | 用户场景允许少量延迟时可进一步减少独立 timer；但不要为了严格整除而提高无效检查频率 |
| 中 | `palf::BlockGCTimerTask` + `palf::LogUpdater` | 30ms、500ms | 由一个 PALF maintenance tick 驱动，`LogUpdater` 约每 500ms 过一次时间 gate | owner 接近且任务轻，但 30ms 路径频繁，合并后必须避免 `LogUpdater` 阻塞 block GC，因此优先级低于前两组 |

不建议跨 KVCache、PALF、checkpoint、schema、SQL session GC 等模块建立一个全局“大定时器”。这会把原本独立的生命周期、锁、超时和故障隔离耦合到一起。合并边界应遵循：同一 owner 或同一 service、允许相同执行上下文、回调总耗时有上限，并保留每个子任务独立的耗时/错误统计。

建议验证顺序：先合并 KVCache 的两个 200ms timer，再合并 checkpoint 的两个 5s timer；每一步都复测 3 分钟空载下的 `TimerWK` 最大/稳态线程数、`delay_in_thread_pool`、单任务最大耗时和收缩次数。定时器合并与小规格限制 `TimerWK` 最大线程数并不冲突：前者减少唤醒和突发，后者限制偶发突发带来的线程上限。

### 历史阶段：独立 TimerWK 上限实现与实测

共享 callback 迁移前的实验阶段曾实现以下最小改动：

- mini mode 的 TimerWK 使用队列压力驱动扩容，不再在最后一个 idle worker 取走任务时预先补一条备用线程；
- mini mode 最大 worker 数从 128 限制为 4；
- 普通模式继续使用原扩容策略和 128 上限；
- timer 注册、周期、取消、同一 timer 不并发执行等语义不变。

同一台机器、同为 2G 全新数据目录的前后两次空载快照：

| 版本 | 采样时间 | 总线程 | TimerWK/`TxLoopWorker` | `BGTask` | DAG |
|---|---:|---:|---:|---:|---:|
| 仅接入 `TxTsWaiter` | 205s | 41 | 10 | 2 | 0 |
| 再限制 mini TimerWK | 210s | 35 | 4 | 2 | 0 |

两个快照中的 `IO_SYNC_CH0=2`、`LogIOCB0=1`、`ReqWorker=2` 等动态线程数量也一致，因此本轮总线程减少 6 可以直接归因于 TimerWK 从 10 收敛到 4。限制版空载 3 分 30 秒内没有出现 `timer task too much delay in thread pool`（500ms）、priority queue delay 或 timer elapsed-time 告警；随后执行建表、插入、显式事务提交、`ALTER TABLE` 和更新，结果正确，TimerWK 仍不超过 4，也没有新增上述告警。现有 `test_timer` 9 个取消、重调度、跨 timer 隔离和启停用例全部通过。

这一历史结果说明 4 条独立 TimerWK 足以覆盖当时的空载和基本 DDL/事务场景，但它仍与后台共享池维护两组物理 worker。后续实验已由下一节的 `TimerService` Source 取代该方案。

### 当前阶段：Timer callback 迁入共享池

mini mode 现在只保留 `TimerSvr` 作为 deadline producer，全部到期 token 由 `TimerService HIGH/max=2` Source 消费；普通模式仍使用原 TimerWK。单并发实验在真正空载时也产生 130 条 500ms thread-pool delay 告警，最严重 callback backlog 约 2.5 秒，因此 `max=1` 已否决。并发 2 的同数据目录运行没有再出现这些告警。

线程名不能直接用于区分 TimerWK 和共享 worker：共享 worker 执行 `ObTxLoopWorker` 后也会残留 `T1_TxLoopWorker`。本轮按 TID 连续采样，并把 `T1_BGTask*` 与残留 `T1_TxLoopWorker` 合并统计为同一物理池。

另一个关键结论是不能只追求较低的瞬时线程数。保留 1 条 warm worker 时，周期 Source 会在 30 秒 idle shrink 后重新把线程扩回高水位；未强制 dispatch 让出的版本在 180 秒中为 `1～6` 条共享 worker，却出现 22 个不同 TID。当前 mini mode 因此采用“从 0 lazy 创建、保留观察到的 6 条高水位、最大 8 条”的策略：实例不会预创建 6 条，但启动期若确实扩到 6，就不再每 30 秒反复释放和申请 pthread/TLS/栈。

最终同数据目录连续采样 180 次、每秒一次：

| 指标 | 结果 |
|---|---:|
| 总线程 | `min=23`、`max=25` |
| 共享 worker | `min=5`、`max=6` |
| 共享 worker 不同 TID | `6` |
| DAG worker 最大值 | `0` |
| 重点迁移专属线程最大值 | `0` |
| Timer 500ms/1s delay 告警 | `0` |

这里的重点专属线程包括 TimerWK、LogIOCB、IO_SYNC_CH、DiskCB、SLOG、LogLoop、ApplySrv、MergeScheduler、TFSwap 和 IO_HEALTH。6 个共享 TID 在整个窗口内只创建一次、没有退出重建；最后一条是在运行中按需扩出，而不是启动时预创建。5 秒 `pidstat` 平均 CPU 为 3.0%，数据仍为 8201 行、`SUM(v)=155745504`，major compaction 保持 `IDLE` 且三个 SCN 相等。与“瞬时缩到 1～3、每 30 秒重建一批线程”相比，这个结果用稳定的 5～6 条物理 worker 承载 29 个逻辑 Source，更符合减少线程内存申请释放的实验目标。`test_background_task_executor` 14/14、完整 `test_io_manager` 22/22 均通过。
