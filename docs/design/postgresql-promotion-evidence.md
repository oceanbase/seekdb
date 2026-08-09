# PostgreSQL REL_18_4 单向在线 promotion 源码证据

## 调查范围与版本

- PostgreSQL 源码：本地检出的官方 `REL_18_4` 源码树
- 固定版本：tag `REL_18_4`，commit `f5cc81719e6da4cbdb1f797c48b693e91018153a`
- 本文的源码行号均以该 commit 为准。源码链接指向 PostgreSQL 官方 GitHub 镜像中的同一 commit。
- 只回答两个问题：Hot Standby 的已有连接如何在线完成 promotion；运行中的 primary 是否有反向进入 standby 的路径，以及旧主应如何成为新 standby。

## 结论

1. **Hot Standby -> Primary 是 postmaster 和已有 backend 不重启的单向在线状态转换。** Promotion 只通知专门执行 WAL recovery 的 startup process 结束恢复；startup process 正常退出后，原 postmaster 从 `PM_HOT_STANDBY` 进入 `PM_RUN`。已有 backend 不在被终止的路径上，并通过共享内存看到 recovery 已结束。
2. **已有 SQL 会话在 promotion 后仍保持连接，但可写性的切换以事务为边界。** recovery 结束时共享状态变为 `RECOVERY_STATE_DONE`；同一 backend 下一次调用 `RecoveryInProgress()` 会看到该状态。下一次 `StartTransaction()` 因而按普通模式设置 `XactReadOnly = DefaultXactReadOnly`，默认即可写。已经在 Hot Standby 中开始的事务不会因 promotion 自动改写自己的 `XactReadOnly`；最明确的使用方式是结束该事务后在同一连接上开始新事务。
3. **PostgreSQL 18.4 不存在运行中 Primary -> Standby 的在线反向状态转换（demotion）。** `RecoveryInProgress()` 的设计明确假定一旦离开 recovery 就不会重新进入；`standby.signal` 也只在一次性的 `StartupXLOG()` 启动序列中读取。`pg_ctl` 有 `promote`，没有 `demote`。
4. **旧主成为 standby 的官方路径是先隔离并停止旧主，再重同步并以 standby 配置重新启动。** 时间线已分叉时首选 `pg_rewind --target-pgdata=OLD_PRIMARY --source-server=NEW_PRIMARY -R`；不能 rewind 时，从新主执行 `pg_basebackup -R` 重建。新主可在线作为 source，但作为 target 的旧主必须离线，所以这不是运行中反向 promotion。

## 证据一：Hot Standby promotion 不重启 postmaster 或已有 backend

### 1. 官方定义本身包含“连接保持”

PostgreSQL 18 的 [Hot Standby 官方文档](https://www.postgresql.org/docs/18/hot-standby.html)把 Hot Standby 定义为既能在 recovery 中接受只读查询，也能在用户继续查询或保持连接时从 recovery 转为 normal operation。它还明确说明：failover/switchover 后 session 保持连接，Hot Standby 结束后，即使 session 始于 Hot Standby，也可以发起 read-write transaction。对应 SGML 原文在：

- [`doc/src/sgml/high-availability.sgml:1515-1524`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/high-availability.sgml#L1515-L1524)
- [`doc/src/sgml/high-availability.sgml:1723-1731`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/high-availability.sgml#L1723-L1731)

这不是通过 postmaster restart 实现的连接重建，而是同一 postmaster 下的进程状态转换。

### 2. promotion 请求只被转发给 startup process

SQL 路径 `pg_promote()` 的实现为：确认当前仍在 recovery，创建 `promote` signal file，向 postmaster 发送 `SIGUSR1`；若 `wait=true`，**调用该函数的现有 backend 自己继续存活并循环检查 `RecoveryInProgress()`，直到它变为 false**：

- `pg_promote()`：[`src/backend/access/transam/xlogfuncs.c:663-724`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogfuncs.c#L663-L724)
- 官方 SQL 函数说明：[`doc/src/sgml/func.sgml:29345-29368`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/func.sgml#L29345-L29368)，在线版见 [`pg_promote`](https://www.postgresql.org/docs/18/functions-admin.html#FUNCTIONS-RECOVERY-CONTROL)

`pg_ctl promote` 走相同协议：`do_promote()` 要求 control file 处于 `DB_IN_ARCHIVE_RECOVERY`，创建 `promote` 文件并向 postmaster 发 `SIGUSR1`，而不是 stop/start postmaster：

- `do_promote()`：[`src/bin/pg_ctl/pg_ctl.c:1181-1249`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_ctl/pg_ctl.c#L1181-L1249)

postmaster 的 `process_pm_pmsignal()` 检查 promote file 后，只向 `StartupPMChild` 发送 `SIGUSR2`。代码没有在这里向普通 backend 广播终止信号：

- `process_pm_pmsignal()`：[`src/backend/postmaster/postmaster.c:3865-3877`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/postmaster.c#L3865-L3877)

startup process 的 `StartupProcTriggerHandler()` 只设置 `promote_signaled` 并唤醒 recovery latch；随后 recovery loop 的 `CheckForStandbyTrigger()` 消费 signal file 并设置共享的 promotion 状态：

- `StartupProcTriggerHandler()`：[`src/backend/postmaster/startup.c:91-97`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/startup.c#L91-L97)
- `CheckForStandbyTrigger()`：[`src/backend/access/transam/xlogrecovery.c:4471-4489`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L4471-L4489)
- `WaitForWALToBecomeAvailable()`：promotion 会先尽量重放 archive/`pg_wal` 中可用 WAL，再关闭 walreceiver 并结束等待，[`src/backend/access/transam/xlogrecovery.c:3640-3655`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L3640-L3655)

因此，promotion 的控制对象是 recovery/startup process，不是 postmaster 本身，也不是承载 SQL session 的普通 backend。

### 3. recovery 结束先开放共享写状态，再让 startup process 正常退出

`StartupXLOG()` 完成 end-of-recovery 动作后，在 `ControlFileLock` 下同时完成两个关键更新：

1. `ControlFile->state = DB_IN_PRODUCTION`；
2. `XLogCtl->SharedRecoveryState = RECOVERY_STATE_DONE`，源码注释直接说明该字段控制 backend 是否可以写 WAL。

证据：[`src/backend/access/transam/xlog.c:6189-6212`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6189-L6212)，函数 `StartupXLOG()`。

`StartupProcessMain()` 在 `StartupXLOG()` 返回后以 exit code 0 正常退出；注释明确说 0 表示 recovery 成功完成：

- [`src/backend/postmaster/startup.c:215-264`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/startup.c#L215-L264)

postmaster 的 child reaper 只回收这个 startup child，然后把自身状态更新为 `PM_RUN`、继续允许连接并启动 normal-operation workers；它没有退出：

- [`src/backend/postmaster/postmaster.c:2247-2328`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/postmaster.c#L2247-L2328)

postmaster 状态机顶部的设计注释也完整描述了 `PM_HOT_STANDBY -> PM_RUN`：archive recovery 完成时 startup process exit 0，postmaster 切到 `PM_RUN`；普通 backend 在 `PM_HOT_STANDBY` 和 `PM_RUN` 两种状态都可存在：

- [`src/backend/postmaster/postmaster.c:292-315`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/postmaster.c#L292-L315)

这就是“不需要 postmaster restart”的直接进程级证据。

### 4. 同一 backend 如何从只读进入可写

每个 backend 的 `RecoveryInProgress()` 都保留本地缓存，但只在看到 recovery 已结束后才永久缓存 false；此前每次都会重新读取共享的 `XLogCtl->SharedRecoveryState`。所以 promotion 后，原 backend 无需重建即可观察到 `RECOVERY_STATE_DONE`：

- `RecoveryInProgress()`：[`src/backend/access/transam/xlog.c:6365-6398`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6365-L6398)

事务开始时，`StartTransaction()` 调用 `RecoveryInProgress()`：

- recovery 仍在进行：`startedInRecovery = true` 且 `XactReadOnly = true`；
- recovery 已结束：`startedInRecovery = false` 且 `XactReadOnly = DefaultXactReadOnly`。

证据：[`src/backend/access/transam/xact.c:2114-2131`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xact.c#L2114-L2131)，函数 `StartTransaction()`。

写查询在执行入口根据 `XactReadOnly` 被拒绝，见 `standard_ExecutorStart()` 和 `ExecCheckXactReadOnly()`：

- [`src/backend/executor/execMain.c:153-170`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/executor/execMain.c#L153-L170)
- [`src/backend/executor/execMain.c:792-825`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/executor/execMain.c#L792-L825)

因此实际边界是：

```text
同一个 TCP/SQL session
  Hot Standby 中的事务: XactReadOnly = true
  promotion 完成:       backend 仍在，SharedRecoveryState = DONE
  下一次事务开始:       RecoveryInProgress() = false
                       XactReadOnly = DefaultXactReadOnly（通常为 false）
  后续 DML:             可写
```

重要限定：promotion 更新的是全局 recovery 状态，不会异步覆写一个已开始事务的 `XactReadOnly`。源码甚至专门保留 `startedInRecovery`，因为事务存续期间 recovery 可能已经结束：[`src/backend/access/transam/xact.c:1034-1045`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xact.c#L1034-L1045)。所以“已有连接变为可写”不等于“已有只读事务被原地改成可写”；可靠语义是同一连接在 promotion 后开启新事务。

## 证据二：不存在运行中 Primary -> Standby 反向路径

### 1. recovery 状态在运行进程中是不可逆的

`RecoveryInProgress()` 的实现注释明确写道：一旦离开 recovery，系统“can't re-enter recovery”，因此 backend 看过 `RECOVERY_STATE_DONE` 后永远返回 false：

- [`src/backend/access/transam/xlog.c:6365-6398`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6365-L6398)

standby 模式由 `standby.signal` 决定，但该文件是在启动阶段读取的：

1. `StartupXLOG()` 的注释规定它在 postmaster/standalone-backend startup 中只调用一次；[`src/backend/access/transam/xlog.c:5463-5468`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L5463-L5468)
2. `StartupXLOG()` 调用 `InitWalRecovery()`；[`src/backend/access/transam/xlog.c:5597-5607`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L5597-L5607)
3. `InitWalRecovery()` 才调用 `readRecoverySignalFile()`；[`src/backend/access/transam/xlogrecovery.c:500-548`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L500-L548)
4. `readRecoverySignalFile()` 发现 `standby.signal` 后设置 `StandbyModeRequested` 和 `ArchiveRecoveryRequested`；[`src/backend/access/transam/xlogrecovery.c:1038-1125`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L1038-L1125)

官方 [Standby Server Operation](https://www.postgresql.org/docs/18/warm-standby.html#STANDBY-SERVER-OPERATION) 同样表述为：只有服务器**启动时**数据目录中存在 `standby.signal`，服务器才进入 standby mode。对应 SGML 为 [`doc/src/sgml/high-availability.sgml:620-625`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/high-availability.sgml#L620-L625)。

工具接口也与此一致。`pg_ctl` 的命令列表和 parser 有 `promote`，没有 `demote` 或“在线进入 standby”的命令：

- help 命令表：[`src/bin/pg_ctl/pg_ctl.c:1972-1987`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_ctl/pg_ctl.c#L1972-L1987)
- command parser：[`src/bin/pg_ctl/pg_ctl.c:2363-2382`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_ctl/pg_ctl.c#L2363-L2382)

这些证据共同排除了运行中 primary 原地切换为 recovery/standby 的官方路径。仅创建 `standby.signal`、修改 `primary_conninfo` 或 reload 配置都不会反转当前进程的 recovery 状态；必须停止并重新启动目标实例。

### 2. failover 后旧主回归的官方流程

PostgreSQL 18 的 [Failover 官方文档](https://www.postgresql.org/docs/18/warm-standby-failover.html)给出两个关键要求：

- 新主产生后，必须阻止旧主再次以 primary 身份运行，避免双主和数据丢失；
- 要恢复冗余，需要在旧主或第三台机器上重新创建 standby，`pg_rewind` 可加速该过程。

对应 SGML：

- fence/STONITH：[`doc/src/sgml/high-availability.sgml:1437-1443`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/high-availability.sgml#L1437-L1443)
- 重建 standby/使用 `pg_rewind`：[`doc/src/sgml/high-availability.sgml:1464-1478`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/high-availability.sgml#L1464-L1478)

典型的官方工具路径是：

```bash
# 先通过集群管理/fencing 保证旧主不再对外写入，然后停止旧主
pg_ctl -D "$OLD_PRIMARY_PGDATA" stop -m fast

# 旧主是离线 target；新主可作为在线 source
pg_rewind \
  --target-pgdata="$OLD_PRIMARY_PGDATA" \
  --source-server="$NEW_PRIMARY_CONNINFO" \
  --write-recovery-conf

# 重新启动后，startup sequence 读取 standby.signal，旧主才成为 standby
pg_ctl -D "$OLD_PRIMARY_PGDATA" start
```

`pg_rewind` 官方文档直接把“failover 后让 old primary 作为 standby 跟随 new primary”列为典型场景，并规定 target 必须 shut down；`--source-server` 允许 source 在线，`-R/--write-recovery-conf` 创建 `standby.signal` 并写入连接配置：

- 在线文档：[`pg_rewind`](https://www.postgresql.org/docs/18/app-pgrewind.html)
- 场景定义：[`doc/src/sgml/ref/pg_rewind.sgml:40-48`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/ref/pg_rewind.sgml#L40-L48)
- target/source 约束：[`doc/src/sgml/ref/pg_rewind.sgml:143-178`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/ref/pg_rewind.sgml#L143-L178)
- `-R` 行为：[`doc/src/sgml/ref/pg_rewind.sgml:182-193`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/ref/pg_rewind.sgml#L182-L193)

源码把这条路径落实为：

- `sanityChecks()` 要求 target control state 为 `DB_SHUTDOWNED` 或 `DB_SHUTDOWNED_IN_RECOVERY`；[`src/bin/pg_rewind/pg_rewind.c:735-780`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_rewind/pg_rewind.c#L735-L780)
- `perform_rewind()` 把 target control state 设为 `DB_IN_ARCHIVE_RECOVERY`；[`src/bin/pg_rewind/pg_rewind.c:726-731`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_rewind/pg_rewind.c#L726-L731)
- `main()` 在 rewind 后调用 `WriteRecoveryConfig()`；即使发现两边未分叉、无需复制数据，指定 `-R` 仍会写 standby 配置；[`src/bin/pg_rewind/pg_rewind.c:449-456`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_rewind/pg_rewind.c#L449-L456)、[`src/bin/pg_rewind/pg_rewind.c:523-533`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_rewind/pg_rewind.c#L523-L533)
- 共用前端函数 `WriteRecoveryConfig()` 把 `primary_conninfo` 追加到 `postgresql.auto.conf` 并创建 `standby.signal`；[`src/fe_utils/recovery_gen.c:117-156`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/fe_utils/recovery_gen.c#L117-L156)

PostgreSQL 18.4 的 `pg_rewind` 可以在 target 非 clean shutdown 时用 single-user mode 补做 crash recovery，但这不代表可对运行中的旧主 rewind。官方选项说明仍要求 target shut down；源码先检查 control file、必要时运行 `ensureCleanShutdown()`，最终 `sanityChecks()` 仍要求 shutdown state：

- [`src/bin/pg_rewind/pg_rewind.c:321-350`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_rewind/pg_rewind.c#L321-L350)
- [`doc/src/sgml/ref/pg_rewind.sgml:272-285`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/ref/pg_rewind.sgml#L272-L285)

### 3. `pg_rewind` 不适用时的完整重建路径

`pg_rewind` 需要旧集群已启用 data checksums 或 `wal_log_hints`，并需要从分叉点开始的必要 WAL；条件不满足或 rewind 失败时，官方文档建议取 fresh backup。可清空/替换旧主数据目录后，从新主运行：

```bash
pg_basebackup \
  -D "$OLD_PRIMARY_PGDATA" \
  -d "$NEW_PRIMARY_CONNINFO" \
  -R \
  -X stream

pg_ctl -D "$OLD_PRIMARY_PGDATA" start
```

[`pg_basebackup` 官方文档](https://www.postgresql.org/docs/18/app-pgbasebackup.html)说明 `-R/--write-recovery-conf` 会创建 `standby.signal` 并把连接设置追加到 `postgresql.auto.conf`；对应 SGML 为 [`doc/src/sgml/ref/pg_basebackup.sgml:227-250`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/doc/src/sgml/ref/pg_basebackup.sgml#L227-L250)。源码通过 recovery injector 注入配置和空的 `standby.signal`：

- [`src/bin/pg_basebackup/pg_basebackup.c:1238-1248`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_basebackup/pg_basebackup.c#L1238-L1248)
- [`src/bin/pg_basebackup/astreamer_inject.c:50-78`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_basebackup/astreamer_inject.c#L50-L78)
- [`src/bin/pg_basebackup/astreamer_inject.c:157-181`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/bin/pg_basebackup/astreamer_inject.c#L157-L181)

## 证据三：promotion 是一个单调、耐崩溃的 recovery 提交

PostgreSQL 并非收到 promote signal 后立刻把 `RecoveryInProgress()` 改成 false。signal 只是令 WAL recovery loop 在耗尽当前可用 WAL 后退出：standby 没收到 trigger 时会持续等待，收到 trigger 后才返回并进入 `StartupXLOG()` 的 end-of-recovery 收尾：

- recovery loop 的单向退出条件：[`src/backend/access/transam/xlogrecovery.c:3268-3287`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L3268-L3287)
- promote 状态一旦观察为 true 就不再反转：[`src/backend/access/transam/xlogrecovery.c:4429-4489`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlogrecovery.c#L4429-L4489)

### 1. 先建立新的 WAL 历史，再允许普通 backend 写

archive recovery 结束时，`StartupXLOG()` 总是选择一个新的 timeline，初始化新 timeline 的 WAL segment，删除 `standby.signal`，并写入 timeline history file：

- 新 timeline 的选择与原因：[`src/backend/access/transam/xlog.c:5972-5999`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L5972-L5999)
- 删除 recovery signal 并持久化 history：[`src/backend/access/transam/xlog.c:6000-6031`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6000-L6031)

这里 timeline 不是一个展示字段。它把 promotion 后产生的 WAL 与旧历史区分开，并记录分叉点，供下游和 `pg_rewind` 判断历史关系。

### 2. startup process 可以先写，普通 backend 仍然不能写

随后 PostgreSQL 完成一组必须位于发布写能力之前的恢复动作：

1. 初始化 WAL insertion 位置；
2. 修整 CLOG 和 MultiXact；
3. 恢复 prepared transactions；
4. 停止 WAL recovery reader；
5. 只给 startup process 自己开放 WAL insertion；
6. 写 full-page-write 参数变化；
7. 完成 commit timestamp 初始化。

对应源码集中在 [`src/backend/access/transam/xlog.c:6033-6187`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6033-L6187)。其中 `LocalSetXLogInsertAllowed()` 只让 startup process 写 WAL，并没有提前放开其他 backend。

promotion 快速路径不会等待完整 checkpoint，而是写入并 flush 一个轻量的 `XLOG_END_OF_RECOVERY` 记录，再异步请求 online checkpoint：

- promotion 快速路径：[`src/backend/access/transam/xlog.c:6320-6363`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6320-L6363)
- end-of-recovery 记录及其强制 flush：[`src/backend/access/transam/xlog.c:7406-7451`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L7406-L7451)
- promotion 完成后请求 online checkpoint：[`src/backend/access/transam/xlog.c:6225-6240`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6225-L6240)

### 3. 共享 recovery 状态是最后的写能力发布点

只有上述动作全部完成后，PostgreSQL 才在 `ControlFileLock` 内同时持久化 `DB_IN_PRODUCTION` 并发布 `RECOVERY_STATE_DONE`。源码注释明确说明 `SharedRecoveryState` 控制普通 backend 是否能够写 WAL：

- 最终发布点：[`src/backend/access/transam/xlog.c:6189-6212`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6189-L6212)
- backend 的 WAL write gate：[`src/backend/access/transam/xlog.c:6419-6448`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/access/transam/xlog.c#L6419-L6448)

因此 PG 的提交顺序可以概括为：

```text
promote intent
  -> 停止等待更多上游 WAL
  -> 建立新 timeline
  -> 完成所有 recovery 收尾
  -> flush XLOG_END_OF_RECOVERY
  -> 持久化 DB_IN_PRODUCTION
  -> 发布 RECOVERY_STATE_DONE
  -> 普通 backend 在下一事务获得写能力
```

从删除 `standby.signal`、持久化新 timeline 到发布 `RECOVERY_STATE_DONE` 的顺序可以进一步推导出：promotion 一旦越过持久化意图，就不会尝试在线回滚为 standby；中途崩溃由下一次 startup/crash recovery 继续收敛到 normal operation。这是对源码顺序的推论，不是一个额外的 PG API 保证。

## 证据四：SQL 连接连续，但依赖进程按各自语义收敛

PG 保留的是 SQL 服务连续性，不是承诺所有类型的连接和后台进程原封不动。

### 1. SQL backend 保留，primary-only worker 在 promotion 后启动

postmaster 明确允许 normal backend 同时存在于 `PM_HOT_STANDBY` 和 `PM_RUN`。checkpointer/bgwriter 两种状态都运行；WAL writer 和 autovacuum launcher 只在 `PM_RUN` 启动：

- SQL backend 可存在的状态：[`src/backend/postmaster/postmaster.c:292-315`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/postmaster.c#L292-L315)
- worker 生命周期条件：[`src/backend/postmaster/postmaster.c:3283-3330`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/postmaster/postmaster.c#L3283-L3330)

这说明 PG 也存在 promotion 后激活 primary-only capability，但激活由 postmaster 的一个生命周期边界负责，不要求普通 SQL backend 重启。

### 2. 上游 receiver 退出，物理级联 sender 在线跨越 promotion

walreceiver 只允许在 recovery 中运行，看到 recovery 已结束就退出：[`src/backend/replication/walreceiver.c:410-429`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/replication/walreceiver.c#L410-L429)。

与之相反，给下游发送物理 WAL 的 cascading walsender 会在线观察 `RecoveryInProgress()` 变为 false，切换到新 insertion timeline，并利用 timeline history 确定旧 timeline 的发送终点：

- 检测 promotion：[`src/backend/replication/walsender.c:3150-3188`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/replication/walsender.c#L3150-L3188)
- 确定旧 timeline 的有效终点：[`src/backend/replication/walsender.c:3188-3208`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/replication/walsender.c#L3188-L3208)

所以 PG 的级联连续性不是“LSN 数值看起来连续就继续发”，而是依赖 timeline history 对旧流和新流做显式衔接。

### 3. 特殊连接可以选择断开重连

逻辑级联 walsender 不承担在线切换恢复环境的复杂度。源码明确选择在 promotion 后主动断开，要求客户端重连：[`src/backend/replication/walsender.c:1459-1469`](https://github.com/postgres/postgres/blob/f5cc81719e6da4cbdb1f797c48b693e91018153a/src/backend/replication/walsender.c#L1459-L1469)。

因此，从 PG 得到的准确产品语义是：普通 Hot Standby SQL 连接保持，当前事务语义不变；物理级联可以由专门实现在线衔接；不值得承担切换复杂度的特殊连接允许重连。

## 对 SeekDB 设计讨论的含义

PostgreSQL 的“单向在线 promotion”依赖四个边界清晰的机制：

1. recovery 由独立 startup process 承担，promotion 正常结束该子进程，不结束 postmaster/backend；
2. 可写性由所有 backend 可见的单调共享状态控制，并在事务开始时重新派生事务只读属性；
3. recovery 收尾有一个严格顺序，所有持久化和运行时不变量满足后才发布写能力；
4. 状态只允许 `RECOVERY -> DONE`，不提供 `DONE -> RECOVERY`。反向角色变更被刻意放在进程生命周期之外，由 fencing、停机、数据重同步、写入 standby 启动配置和重启组成。

因此，不能把 PostgreSQL promotion 的在线性推导成双向在线角色切换能力。它提供的是 **standby 到 primary 的在线、单调状态转换**；旧 primary 回归 standby 是 **target 离线、source 可在线的重建/rewind 工作流**。

同样不能从 PG 推导出“SeekDB 只需要切三个布尔开关”。REL_18_4 的 backend 源码中，`RecoveryInProgress()` 出现在 38 个文件、120 个调用点；PG 依靠 `StartupXLOG()` 集中协调 timeline、WAL、事务恢复、后台进程和级联状态。SeekDB 应借鉴的是单调状态、事务边界和单一 recovery owner，而不是复制这种分散查询 recovery 状态的实现风格。

对 SeekDB 更稳妥的落地约束是：

1. 只提供 `STANDBY -> PRIMARY` 的进程内 promotion，不提供在线 demotion；
2. promotion 由 `StandbyModule` 内的单一深接口拥有，observer 和业务模块不能编排内部步骤；
3. write gate 最后打开，promotion 前开始的只读事务保持只读，下一事务才派生新的写能力；
4. promotion 失败不在线反转日志模式；保持 fenced，并由持久化 intent 在重启时完成收敛；
5. 在编码前列出 SeekDB 对应于 PG end-of-recovery 的完整不变量，尤其是本地日志 append、事务 ID、timestamp、prepared transaction、后台任务和级联发送；
6. 如果继续不引入 timeline/generation，必须明确由现有日志身份、严格 fencing 和“failover 旧主不得复用”的运维契约分别承担历史分叉、级联 source 切换和旧主回归的约束，不能仅凭 SCN/LSN 数值连续判定安全。
