# Namespace fork 原型 V4：复用已有 tablet 状态

日期：2026-09-14。分支：`codex/namespace-fork-fastpath-v4`，基于 V3 `41888648f`。状态：快路径、并发物化、超时回滚和提交前崩溃恢复验收通过。

后续的跨数据库 namespace 身份原型见 [V5](namespace_identity_v5.md)。本文保留 V4 的实现和原始验收记录。

## 本轮问题

V3 每次访问已物化的 fork tablet，仍会在 `ensure_tablet` 中开启内部事务、锁住目录根、读取 B+ tree 确认绑定。本轮验证：能否直接使用 tablet 现有的已提交状态跳过这些操作，不增加独立缓存、缓存条目或每个 tablet 的字段。

## 实现

`ensure_tablet` 先通过既有 `ObTabletCreateDeleteHelper::check_and_get_tablet`，以 `READ_READABLE_COMMITED` 模式获取 tablet。该路径已经使用 `ObTablet::tablet_status_cache_`；缓存只在 MDS 状态为 `ON_COMMIT` 且 tablet 为 `NORMAL` 时填充，现有状态变更会重置它。

- 确认已提交：直接返回，随后读写继续执行原有业务快照、表锁及存储检查。
- 不存在或物化尚未提交：释放临时 handle，再执行原有目录事务和根锁，串行处理首次物化。
- 其他错误：向上返回，不把内存不足、状态读取失败等误判为需要创建。

正确性的前提是 V2 已经将 CREATE_TABLET MDS、tablet→table 映射、目录绑定放在同一个引擎事务中；当前原型的目标绑定固定、目标 ID 不复用。已提交的目标 tablet 因而可以作为已提交物化绑定的证据。

新增的检查不再使用单独的 set、哈希表或位图，没有新增常驻缓存，也没有修改 C++ 对象布局。局部 handle 使用现有引用管理；tablet 不在内存时仍可能通过现有元数据管理器加载。这里不声称所有访问都没有内存分配。

为保持写入加锁顺序，本轮保留 `ObAccessService` 的调用位置。快路径有一次提前的 tablet 获取和状态检查，后续读写仍正常获取 handle；本轮优化的是重复目录事务，不是消除所有 tablet 查找、schema 目录查询或后台目录读取。

## 必须修正的提交判断

原型的逻辑创建版本 S 在物理创建事务提交前就已写入，不能沿用“创建版本有效即代表创建已提交”的判断。

在共享的 `check_read_snapshot_for_normal` 中，对 `PROTOTYPE_MATERIALIZE_TABLET` 明确要求 `ON_COMMIT`；否则返回 `OB_EAGAIN`，让原型访问退回目录根锁等待。既有缓存仍只记录已提交状态。

`READ_ALL_COMMITED` 的共享锁冲突回退路径也补上同样的区分，避免它误用提前填写的 S。普通 CREATE_TABLET 的条件保持原样。

## 一条命令验收

```bash
python3 tools/obtest/namespace_fork_fastpath_prototype.py \
  --binary build_release/src/observer/seekdb
```

脚本创建并关闭独占实例，保留日志并归档数据。使用已有 `AFTER_UPDATE_TABLET_TO_LS` 调试点，在原型写完映射与目录、提交前暂停；未增加新的同步点枚举或测试缓存。正常执行没有暂停。提交前还检查 SQL worker 状态，已超时请求回滚内部物化事务。

验收条件：

1. 另一连接持有目标目录根的行锁时，已物化表完成 20 次 SELECT 和 20 次 UPDATE/ROLLBACK，`PROTOTYPE_V4_DIRECTORY_SLOW_PATH` 日志不增加。
2. 首次创建停在提交前，物理 tablet 已存在而已提交目录根尚未改变；并发 reader/writer 都退回慢路径等待。放行后只产生一个已提交 tablet。
3. 创建暂停时使真实 SQL worker 超时，放行后内部事务回滚，目录根不变、未提交 tablet 被移除；重试重新物化，失败 INSERT 的行不可见。
4. 在另一张表的创建提交前杀进程，重启核对目录和物理 tablet 均恢复到提交前；原已物化表第一次访问即可在目录根被锁住时读写；未提交和从未访问的表按需物化。

慢路径日志和持有根锁时的实际完成共同证明访问不再开启该目录事务；不是 CPU、吞吐或延迟基准。

## 2026-09-14 实测

实例 `namespace_fork_PROTOTYPE_fastpath_v4_phvrpgfd` 返回 PASS，已停止并归档。

| 检查 | 结果 |
| --- | --- |
| 捕获快照 | S=`1789370590984529002`，初始目标物理 tablet 为 0 |
| 热访问 | 根行锁被另一连接持有期间，20 次 SELECT、20 次 UPDATE/ROLLBACK 全部完成，新增目录慢路径为 0 |
| 未提交创建 | B.t2 的物理 tablet 已存在、目录根未发布时，并发读写均返回 `OB_EAGAIN` 至原型入口，随后等待目录根锁；放行后均成功，只有一个目标 tablet |
| 提交前超时 | B.t3 的物化因真实 2 秒查询超时返回 4012；目录根不变、物理 tablet 被移除；再次读取重新物化并返回原始两行 |
| 提交前崩溃 | B.t4 在创建后、提交前被杀进程；重启只恢复此前已提交的三个目标 tablet，目录根未包含失败创建 |
| 重启后首次访问 | B.t1 未经 SQL 预热，再次在根行锁被持有时完成 20 次读取及 20 次更新回滚，新增目录慢路径仍为 0 |
| 未提交与未访问表 | B.t4 重试及 B.t5 首次访问均返回原始两行；最终五个物理 tablet，无 rootserver fork DDL 任务 |

整个验收共记录 9 次慢路径：五张表各一次成功物化，B.t2 两个并发等待请求，B.t3 一次超时尝试，以及 B.t4 一次提交前崩溃尝试。日志中的 `lookup_ret=-4023` 对应那两个未提交创建的并发访问。

证据：

- [完整验收输出](/data/1/tmp/namespace-v4-fastpath-sync.log)、[结构化事件](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_fastpath_v4_phvrpgfd/experiment.jsonl)。
- [引擎日志](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_fastpath_v4_phvrpgfd/log/seekdb.log)、[归档数据](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_fastpath_v4_phvrpgfd/data.tar.gz)。
- [编译输出](/data/1/tmp/namespace-v4-build-final.log)：既有 release 目录中 `source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功，无外部下载。

二进制 SHA-256：`c6a306f0b578829b89b50ea137074ceef7901a8cf35855bb53a56493cdc8a004`。

同一二进制的回归：

- [V2 40 表验收](/data/1/tmp/namespace-v4-v2-regression.log)通过：326 个旧页保持不变，catalog 高度 3、directory 高度 2；最终 8 个物化条目、32 个继承条目、6 个共享目录页。8 路并发、事务回滚及 GC 超过 S 后的未访问表读取均通过。
- [V3 转储合并验收](/data/1/tmp/namespace-v4-v3-regression.log)通过：来源两表及已物化目标完成实际 minor/major 合并，major 版本 `1789370682127984004` 晚于 S=`1789370659916393008`；未访问表保持旧快照，基线恢复及目标增量正确。
- [V1 与关闭开关对照](/data/1/tmp/namespace-v4-v1-regression.log)通过：`lazy_fixed_snapshot` 和 `flag_off_keeps_eager_database_fork` 均为 PASS。
- Python 语法检查与 `git diff --check` 通过；上述实验实例均已停止并归档。

首次脚本运行的调试变量名及 TIMEOUT 单位错误记录保留在 [诊断输出](/data/1/tmp/namespace-v4-fastpath.log)，不计为验收通过。修正为既有 `ob_global_debug_sync` 及微秒单位后，在同一二进制上完成上述验收。

## 边界

仍沿用 V3 的固定两列整数表、来源存活、单代 fork 和全局快照保护。目标 DROP、绑定替换、ID 复用与精确 GC 不在本轮范围。已有的进程生命周期 schema 缓存仍然存在，本轮只省去此前提议新增的物化绑定缓存。

本轮覆盖明确的物化暂停、超时回滚与提交前崩溃，不等于所有提交回调、元数据淘汰和故障组合均已完成验证。
