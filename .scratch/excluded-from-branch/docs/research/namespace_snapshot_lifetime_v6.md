# Namespace 原型 V6：快照独立持有与来源删除

后续：[V7：分支删除、最后引用释放与存储回收](namespace_snapshot_reclaim_v7.md)。本文保留 V6 当时的范围和证据。

日期：2026-09-14。分支：`codex/namespace-snapshot-lifetime-v6`，基于 V5 `bb21fa618`。

## 本轮验证的问题

A 有两个数据库，fork B 后只访问其中一张表；删除 A，再转储、重启，B 的已访问表及从未访问的表是否仍能读写。

V6 复用原生 DROP 真正删除 A 的数据库和表定义，源 tablet 的 DELETE MDS 正常提交。独立快照保护底层存储，B 按固定 S 读取这些输入。没有改名保存源表，也没有为删除提前物化 B 的所有表。

## 持久化对象

模式 4 新增 `__fork_proto_meta.snapshots`，每行记录 snapshot ID、catalog 根、directory 根、S 和 schema version。原型直接使用唯一 GTS 值 S 作为 snapshot ID，避免另建分配器。namespace 行新增 `snapshot_ref` 和 `state`。

fork 在同一个引擎事务中发布快照记录、既有 `__all_acquired_snapshot` 版本保护、B 的快照引用和共享根。B 的 `source_id` 为 0；之后的解析、首读物化和后台处理无需读取 A 的 namespace 行。快照目录中的物理 tablet ID 仍指向共享存储池里的原输入。

首次物化校验所引用的持久快照、S、schema version 和 catalog 根。目标目录的 COW 绑定仍独立更新；快照自身的目录根保持不变。

## 删除与准入

```text
LIVE --持久化关闭--> DELETING --一个 DDL 事务提交--> DELETED
                        |
                        +--失败或崩溃：保持关闭，重试同一命令
```

1. 锁住 A 的注册行并持久化 DELETING。fork 和新建源对象要求 LIVE，无法越过关闭状态。
2. `ObAccessService` 在实际读写入口检查源对象归属和 namespace 状态。旧连接、已选中的默认 database、缓存计划和 PREPARE/EXECUTE 同样经过检查；纯解析无需访问存储的命令不属于此准入检查。
3. 使用既有数据库 DROP 的 DDL 锁等待原有写事务结束。原型逐库枚举并执行原生删除；所有数据库/table schema 删除、tablet DELETE MDS 和 A 的根清空在同一 DDL 事务提交。
4. 删除完成后保留 state=DELETED 的小型注册墓碑，清空 A 的根。名称和 ID 暂不复用。B 的引用和快照版本保护继续存在。

删除事务通过 `NamespaceSourceDropGuard` 将它自身的 SQL client 指针登记为短时内部能力，六个原生表删除子路径和数据库删除路径按这个确切事务识别原型 DDL 例外。一个原子指针，不是全局放开源 DDL，也不使用 TLS current_namespace。作用域结束自动清除；原有 DDL 合法性检查继续执行。

来源访问的状态检查在本原型中会查询持久注册行；没有新增状态/绑定缓存。这有逐次检查开销，尚未做吞吐优化。系统和实验元数据不属于 A；内部 LOB/索引读取还需要按实际所属 tablet 识别，不能仅凭请求中的 table ID 判定，否则会阻断系统字典恢复。

## 数据为何不会被提前删除

源 tablet 的状态为 **DELETED、已提交、非空壳**。既有 MVCC 快照记录保留 S 所需版本；新增的 empty-shell GC 保护则防止整个源 tablet 的输入被清空。

GC 先收集已提交删除的候选，再读取仍被 namespace 引用的快照根，对候选 tablet 在不可变目录中查找。被引用的候选保留并安排重试。读取失败或启动时快照元数据尚未就绪，整轮破坏性回收暂停；不将“暂时读不到引用”解释为“没有引用”。关闭状态阻止从已删除来源再发布新快照。

这里保护的是快照目录，不要求 B 已经创建任何目标 tablet。因此 B 从未访问的冷表也受保护。使用临时数组扫描快照根，不新增常驻引用索引或绑定缓存；复杂度随候选数、快照数和树高增长。

本轮仍保守保留整个快照：即使 B 已完成基线交接，也不自动解除其输入保护。精确引用释放、快照删除、目标 namespace 删除和最终空间回收不在本轮范围。

## 一条命令验收

```bash
python3 tools/obtest/namespace_snapshot_lifecycle_prototype.py \
  --binary build_release/src/observer/seekdb
```

默认依次创建两个独占真实实例，验证正常删除与提交前崩溃。退出后停止进程、保留日志并压缩数据。可用 `--case lifecycle` 或 `--case crash` 单独运行。通过 `SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp` 指定实验根目录；本机 `/data` 空间紧张，本轮使用 `/tmp`，没有下载或复制二进制。

实验环境变量 `SEEKDB_NAMESPACE_FORK_PROTOTYPE=4`，由脚本设置。Python 只准备实验元数据表、发起 SQL 和核对结果；快照发布、准入、DROP、MDS、GC 保护和恢复均在 C++ 引擎中执行。

临时 SQL 控制入口：

```sql
FORK DATABASE __empty__ TO a; -- 将原生用户目录注册为 namespace 1
FORK DATABASE a TO b;         -- 发布独立快照并创建 B
FORK DATABASE a TO __drop__;  -- 本原型借用的 namespace 删除入口
SELECT * FROM __fork_ns_2__db2.t;
```

这不是正式新增的 SQL 语法。DELETING 时重复删除命令恢复未完成操作；已 DELETED 时重复命令成功返回。

## 验收及证据

最终二进制 SHA-256：`38b1ddd3d2fb2d4ee233aa94f06fc24ecaa4dbec3bb839d9011956d2d945152e`。

[编译日志](/data/1/tmp/namespace-v6-build-drop-sync.log)：既有 release 目录中，`source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功。

[V6 完整验收](/data/1/tmp/namespace-v6-lifetime-final.log)包含两个 PASS：

| 用例 | 实测结果 |
| --- | --- |
| 正常删除 | B 的 S=`1789374189611974003`。A 的两个数据库和表定义从原生系统表消失，根清空；源 tablet `200006/200007` 均为 `(DELETED, committed, nonempty)` |
| 在途事务与旧入口 | A 的写事务持锁时 DROP 等待；关闭后旧连接、缓存查询、PREPARE/EXECUTE 和新 UPDATE 被拒绝；原写事务提交后删除完成，B 仍读 S |
| 冷表与 GC | DROP 和多轮 GC 后仍只有一个 B tablet；未打开的另一张表没有提前物化，快照记录不变 |
| 转储与恢复 | 删除后的源输入完成实际 MINI_MERGE；重启后 B 首次打开冷表读到 S；B 完成实际 MAJOR_MERGE，再次重启后增删改结果仍正确 |
| 提交前崩溃 | 两个源 tablet 已写入未提交 DELETED，schema/根删除尚未提交时强杀；重启后原生目录和 NORMAL tablet 一起恢复，A 仍为 DELETING，访问仍被拒绝 |
| 重试 | 重试删除完成；两个 B tablet 在整个失败/重试删除期间均未创建，此后首读两表返回正确的 S |

正常删除实例：[事件](/tmp/namespace_fork_PROTOTYPE_lifetime_v6_lifecycle_nap2rdj2/experiment.jsonl)、[目录快照](/tmp/namespace_fork_PROTOTYPE_lifetime_v6_lifecycle_nap2rdj2/directory_snapshot.json)、[数据归档](/tmp/namespace_fork_PROTOTYPE_lifetime_v6_lifecycle_nap2rdj2/data.tar.gz)。

崩溃实例：[事件](/tmp/namespace_fork_PROTOTYPE_lifetime_v6_crash_1z0ic53x/experiment.jsonl)、[数据归档](/tmp/namespace_fork_PROTOTYPE_lifetime_v6_crash_1z0ic53x/data.tar.gz)。复用现有 `AFTER_UPDATE_TABLET_TO_LS` 同步点暂停删除提交；没有新建调试枚举。

首次运行在删除和转储后恢复失败，原因是准入检查误拦系统字典 LOB 读取；修正后上述最终二进制完整通过。失败日志保留在 [诊断输出](/data/1/tmp/namespace-v6-lifetime.log)，不计为通过证据。

同一二进制的回归：

- [V5 / 模式 3](/data/1/tmp/namespace-v6-v5-regression.log)通过：三个 namespace、跨数据库目录、相同 local ID 的独立读写/锁及恢复。
- [V4 / 模式 2](/data/1/tmp/namespace-v6-v4-regression.log)通过：已有 tablet 快路径、并发物化、超时回滚和提交前崩溃恢复，无新增绑定缓存。
- [V1 / 模式 1 及关闭开关](/data/1/tmp/namespace-v6-v1-regression.log)通过：`lazy_fixed_snapshot` 和 `flag_off_keeps_eager_database_fork`。
- Python 语法检查和 `git diff --check` 通过；本轮创建的所有实验实例均已停止并归档。

## 明确边界

- 只允许删除原生 namespace 1；延续固定两列整数主键表、无辅助对象及单代 fork。B 的 DDL、用户权限/config、SQL worker 隔离及完整复合对象键仍未实现。
- 删除过程中允许已经开始的读完成，现有写事务通过 DDL 锁排空；本轮覆盖一个真实在途写事务、旧连接和缓存计划，不声称穷尽所有执行器/异步入口。
- fork 捕获仍不逐表枚举；**删除会逐库逐表执行原生 DDL**，会等待长事务，尚未实现快速删除。
- 快照 pin、页和被保护的源输入暂不释放；这一步验证来源逻辑删除后的存活与恢复，不证明空间已回收。共享数据目录不能随 A 一并物理删除。
- 已持久化模式 4 数据必须由支持该模式的实验二进制、以模式 4 恢复。没有升级/降级、磁盘损坏或断电保证。
