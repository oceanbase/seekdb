# Namespace 原型 V7：最后一个分支删除后的存储回收

分支 `codex/namespace-snapshot-reclaim-v7`，基于 V6 `2a27ca62c`。沿用实验模式 4。

后续 [V8：多代 fork](namespace_lineage_v8.md) 增加 B→C 捕获、祖先快照引用链与最后持有者的级联释放；本页记录 V7 当时的单代实现和证据。

## 本轮问题

A→B、C 后删除 A、B，C 的冷表仍能读取自己的 S；再删除 C，快照引用和版本保护解除，源输入与分支私有 tablet 能通过既有 GC 成为空壳。删除须处理在途读写和提交前崩溃，不提前物化冷表。B+ tree 元数据页回收另做。

## 实现

继续使用 V6 的 `LIVE → DELETING → DELETED` 持久状态，临时控制入口扩展到分支：

```sql
FORK DATABASE a TO __drop__;
FORK DATABASE b TO __drop__;
FORK DATABASE c TO __drop__;
```

1. 锁注册行并提交 DELETING。此前进行的冷表物化和此次关闭竞争同一根行锁：已经提交的绑定进入删除目录；尚未取得锁的物化在关闭后被拒绝。
2. 等待已经进入的存储扫描、DML 操作、后台基线 DAG 退出。等待发生在目录锁和 DDL 锁之前，防止“物化等根锁、删除等物化”的环。
3. A 继续使用原生数据库 DROP。B、C 没有原生表目录行，扫描其当前 directory，只收集 `bound != 0` 的私有 tablet，使用既有 `ObLockAloneTabletRequest` 的事务内排他锁等待写事务结束，再交给 `ObTabletDrop` 删除映射、提交 DELETE MDS。继承但未访问的条目不物化、不删除来源输入。
4. 同一删除事务清空 namespace 根和 `snapshot_ref`，设置 DELETED；锁住对应的持久 snapshot 行，确认没有 namespace 再引用它，删除其版本保护与 snapshot 行。版本保护使用既有批量删除接口，精确匹配类型、S、schema version、tablet 0；没有按类型清除其他快照。
5. 提交后刷新既有快照管理器。V6 的 empty-shell GC 按仍有引用的 snapshot directory 保留来源输入；引用全部消失后，既有 GC 正常把已删除的来源及私有 tablet 换成空壳。读引用失败或启动时元数据未就绪仍暂停破坏性 GC。

B、C 当前各自有一个 GTS 标识的 snapshot 和 pin。删除 B 只释放 B 的 snapshot；C 即使从未打开表，仍通过自己的 snapshot directory 保护源 tablet。这不是把 B、C 的根指针手工改成共用一个 snapshot ID。

提交前失败/崩溃会回滚私有 tablet DELETE、映射删除、根清空、引用与 pin 释放；此前已提交的 DELETING 保留，入口仍关闭，重复删除命令续做。完成后的重复删除成功返回，名称和 ID 暂不复用。

## 读写持有期与内存

`ObStoreCtxGuard` 多一个布尔标记：实际存储入口先登记活动操作，再读取持久 namespace 状态。扫描保持登记直到迭代器释放，DML 操作直到存储上下文释放；写事务在操作返回后仍由原生 tablet 锁保护到事务结束。`ObTabletForkCtx` 同样覆盖从取得源输入前到异步 DAG 清理完成的期间。缓存计划、已选中的默认数据库以及已有物化 tablet 均不能绕过状态检查。

原型使用一个全局原子计数，不新增按 namespace/tablet 增长的引用或绑定缓存。代价是删除可能等待其他 namespace 的长查询、DAG；持续负载也可能使删除超时。失败时保持 DELETING 和快照保护，可重试。正式实现应结合 namespace worker 的关闭/排空机制解决此隔离问题。

这不是“完全不增加内存”：有一个全局计数和每个活动上下文的布尔标记，结构体对齐可能增加空间；现有 schema holder 仍保留到进程结束。本轮没有清除这些缓存，也没有宣称解决全部内存回收。

## 一条命令验收

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_snapshot_reclaim_prototype.py \
  --binary build_release/src/observer/seekdb
```

默认依次运行 `reclaim` 和 `crash` 两个独占真实实例，也可用 `--case` 分别执行。Python 准备实验元数据表、发 SQL、设置已有同步点并核对结果；引用释放、准入、排空、锁、MDS 和 GC 均在内核执行。退出停止实例并归档数据。本机使用 `/tmp`，不下载或复制二进制。

## 验收证据

二进制 SHA-256：`5a2023c6c085961051c9d1cb5059a333506f3a4cf7edf4607a098e99c54cb535`。

[最终编译日志](/data/1/tmp/namespace-v7-build-drain-sync.log)：既有 release 目录，`source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功。

[V7 完整验收日志](/data/1/tmp/namespace-v7-reclaim-final.log)两个用例均 PASS：

| 用例 | 实测结果 |
| --- | --- |
| B 删除 | 在途 UPDATE 持有事务时删除等待，旧连接、缓存 SELECT、PREPARE/EXECUTE、新 UPDATE 均无法越过关闭状态；提交旧事务后删除完成 |
| C 冷表存活 | A、B 删除后，B 私有 tablet 成为空壳且映射消失；C 保留自己的 pin，两个源输入仍为 `(DELETED, committed, nonempty)`。转储并重启后，C 冷表读到 S=`1789376784219273020` 时的 11，来源后来写入的 12 不泄漏 |
| 读者排空 | 暂停在已准入、尚未取得源输入的扫描处；删除 C 等待，pin 不变，放行后扫描返回正确的 11 |
| 后台排空 | 同时暂停一个已登记、尚未取得源输入的真实基线 DAG；扫描结束后删除仍等待，放行 DAG 后才提交 |
| 最终回收 | A/B/C 全为 DELETED、引用全 0、snapshots 和对应 acquired pins 均空。源 `200006/200007`、B 私有 `4611686027017522502`、C 私有 `4611686031312489799` 成为空壳或由后续原生 GC 移除，映射全部消失；重启无复活。22 个元数据页保留 |
| 全冷分支 | 另一用例先删除完全未访问的 `untouched` 分支，没有创建目标 tablet，释放其独有 pin，A/B 仍正常 |
| 提交前崩溃 | 最后一个 B 的私有 tablet 已写入未提交 DELETED，pin 释放尚未提交时强杀；重启后 B 仍 DELETING，私有 tablet 恢复 NORMAL、原根和 pin 完整恢复、来源输入仍受保护。重试删除完成最终回收，再重启仍成立 |

正常删除实例：[事件](/tmp/namespace_fork_PROTOTYPE_reclaim_v7_reclaim_q0ckar4d/experiment.jsonl)、[数据归档](/tmp/namespace_fork_PROTOTYPE_reclaim_v7_reclaim_q0ckar4d/data.tar.gz)。

崩溃实例：[事件](/tmp/namespace_fork_PROTOTYPE_reclaim_v7_crash_v6nmvbfb/experiment.jsonl)、[数据归档](/tmp/namespace_fork_PROTOTYPE_reclaim_v7_crash_v6nmvbfb/data.tar.gz)。最后一个 B 的 S=`1789376816122917008`，提交前私有 tablet 状态为 `(DELETED, uncommitted, nonempty)`。

复用现有 `AFTER_TABLE_SCAN`、`FORK_TABLE_BUILD_DATA`、`AFTER_UPDATE_TABLET_TO_LS` 同步点，未新增调试枚举。验收既接受观测到的已提交空壳，也接受有对应引擎成功移除日志的后续 GC；已经消失的 tablet 不能被误判为“尚未成空壳”。

排查记录保留：[普通锁表回查原生目录失败](/data/1/tmp/namespace-v7-reclaim.log)，改用独立 tablet 锁后通过；[读同步点未进入产物](/data/1/tmp/namespace-v7-reclaim-tablet-lock.log)，重新编译后进入；[验收误要求空壳永远留在物理 map](/data/1/tmp/namespace-v7-reclaim-drain.log)，核对原生 GC 移除日志后修正判据。这些失败不计为通过证据。

同一二进制的回归：

- [V6 / 模式 4](/data/1/tmp/namespace-v7-v6-regression.log)两个用例通过：原生来源删除后冷表首读、真实源转储/目标基线/MAJOR_MERGE，以及源删除提交前崩溃和重试。
- [V4 / 模式 2](/data/1/tmp/namespace-v7-v4-regression.log)通过：根行加锁时已有 tablet 的读写快路径、并发物化、超时回滚及提交前崩溃，没有新绑定缓存。
- [V1 / 模式 1 及关闭开关](/data/1/tmp/namespace-v7-v1-regression.log)通过：两表固定 S 的 lazy fork，以及关闭开关后保持既有 eager fork database 行为。
- Python 语法检查、`git diff --check` 通过。所有本轮实验实例均已停止、数据已归档。没有重复运行独立 V2、V3、V5 套件。

## 边界

- 单代 fork、固定两列整数表；未补齐完整复合对象键、用户/权限/config、namespace worker/IPC，以及通用 DDL。
- fork 捕获仍是共享根；删除需遍历 directory、等待活动操作及写事务，不是快速删除。
- 不回收 B+ tree 元数据页、不清除进程内 schema holder。它们另有读者持有期问题，不能仅凭 namespace 根清空就删除。
- 空壳转换和 tablet 映射消失证明存储生命周期释放；不等于所有共享宏块已释放，也不等于预分配的数据文件缩小。
- 不支持升级/降级或模式开关切换。已持久化模式 4 数据须由支持该模式的实验二进制恢复。
