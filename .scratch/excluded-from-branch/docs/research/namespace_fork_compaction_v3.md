# Namespace fork 原型 V3：正常转储和合并

日期：2026-09-14。分支：`codex/namespace-fork-compaction-v3`，基于 V2 `f736de6fd`。状态：真实转储、minor/major 合并和崩溃恢复验收通过。

后续进展：[V4 复用现有 tablet 状态跳过已物化表的目录事务](namespace_fork_fastpath_v4.md)，不新增物化绑定缓存。下文保留 V3 原始实现和验收记录。

## 本轮问题

V2 的目标 tablet 尚未完成 fork 基线构建，会从来源 tablet 读取 A@S。本轮检查来源和目标正常转储、minor/major 合并后，已物化目标的修改及尚未访问表的旧快照是否仍然正确，并再次检查崩溃恢复。

保持两列整数普通表、单代 fork、来源存活、全局快照保护和 V2 元信息表；没有新增 namespace 进程、索引、来源删除或引用 GC。

## 实际发现与最小修补

未修改内核时，来源两张表和已物化目标均完成四轮 MINI_MERGE，以及一次包含四个输入文件的 MINOR_MERGE，目标行正确。之后来源两张表完成 MAJOR_MERGE，目标没有自己的 major 基线，180 秒内始终不能完成 major 合并。这是 V2 的真实缺口。

补上基线后，后台合并又将继承表判为 `OB_TABLE_IS_DELETED`：它使用 `get_tablet_to_table_history` 查询原生表历史，V2 没有逐表写入该历史。因此在这个共享入口接入已提交 directory 的 tablet→table 绑定，同时检查请求 schema 版本不早于捕获版本；后续表定义查询继续使用 V2 的固定 catalog。合并和 tablet 统计等调用方共同受益。

修补在现有后台 tablet 调度中检查原型目标的持久 `fork_info`：

1. 只处理已经物化、仍未完成基线交接的目标 tablet，不扫描或创建未访问的继承表。
2. 从已提交的 directory 绑定恢复来源、目标、S 和固定 schema；校验其与 tablet 元信息一致。
3. 复用现有 fork 就绪条件。对原型目标，就绪检查只等待来源正常落盘，禁止该检查主动调用来源 freeze。
4. 就绪后调度既有 `ObTabletForkDag`，复用 SSTable 共享/按 S 裁剪重写和目标 table-store 发布逻辑，保留目标自有增量。
5. 既有 table-store 更新将基线和 `fork_info.complete` 一起持久化。后台扫描会重新发现未完成状态；重启不依赖进程内任务列表。

这是存储后台 DAG，不是 rootserver 的逐表 FORK TABLE DDL 任务。fork 捕获时仍只共享目录根；正常落盘后的基线构建成本在后台发生。重启后的自动重试设计复用既有持久 tablet 状态，本轮不等于完成所有交接提交阶段的故障注入。

## 一条命令验收

```bash
python3 tools/obtest/namespace_fork_compaction_prototype.py \
  --binary build_release/src/observer/seekdb
```

独占测试实例中将既有 `ob_compaction_schedule_interval` 从默认 120 秒设为 3 秒，以缩短等待；没有修改生产默认值。测试使用正常 `ALTER SYSTEM MINOR FREEZE TABLET_ID=...` 和 `ALTER SYSTEM MAJOR FREEZE`，不使用 DEBUG_SYNC 暂停。

检查步骤：

- A.t1/A.t2 初始均为 `(1,10),(2,20),(3,30)`，fork 后只物化 B.t1。
- B.t1 更新 id=1、删除 id=2、插入 id=4；给后台调度运行机会，确认它没有提前转储来源。
- A 两张表更新 id=1、删除 id=3、插入 id=5；来源和 B.t1 经四轮正常转储及真实 minor 合并。
- 来源和 B.t1 完成真实 major 合并；此时 B.t2 仍没有物理 tablet。
- 首次访问 B.t2 必须返回原始三行；B.t1 必须保留自己的更新、删除和新增，且保留来源后来删除的 id=3。
- 杀进程并重启，核对目录根、两张目标表、基线存在及事务回滚。

完成条件来自本地 `__all_virtual_table_mgr` 的实际存储文件和 `__all_virtual_tablet_compaction_history` 的已完成任务。不能用发出合并命令或看到空 major 表代替完成证据。

## 2026-09-14 实测

最终实例 `namespace_fork_PROTOTYPE_compaction_v3_ri0cm8l5` 返回 PASS，已停止并归档。

| 检查 | 结果 |
| --- | --- |
| fork 捕获 | 页数不增加，目标物理 tablet 数为 0；S=`1789369581328035009` |
| 后台等待来源正常转储 | 等待期间来源数据仍在 active memtable，目标没有 major 基线；日志没有 fork 工具主动 freeze |
| 实际转储 | A.t1、A.t2、B.t1 各完成四轮 MINI_MERGE |
| 实际 minor 合并 | 三个 tablet 均有已完成 MINOR_MERGE，输入至少三个 SSTable |
| 实际 major 合并 | 三个 tablet 均完成 MAJOR_MERGE，版本为 `1789369603563125002`，晚于 S |
| 已物化目标 | B.t1 始终为 `(1,100),(3,30),(4,414)`，保留自己的更新/删除/新增 |
| 未访问目标 | major 合并前后物理目标均只有 B.t1；首次打开 B.t2 返回 `(1,10),(2,20),(3,30)` |
| 来源 | A 两张表均为 `(1,14),(2,20),(5,50)`，与目标隔离 |
| 崩溃恢复 | 目录根不变；B.t1 的 major 基线及数据恢复；B.t2 在崩溃前尚无 SSTable，重启后后台完成基线构建；事务回滚通过 |
| DDL 任务 | 整个过程没有 rootserver FORK TABLE DDL 任务 |

证据：

- [完整验收输出](/data/1/tmp/namespace-v3-compaction-mapping.log) 与 [结构化事件](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_compaction_v3_ri0cm8l5/experiment.jsonl)。
- [引擎日志](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_compaction_v3_ri0cm8l5/log/seekdb.log) 与 [归档数据](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_compaction_v3_ri0cm8l5/data.tar.gz)。
- [V2 缺少基线的失败记录](/data/1/tmp/namespace-v3-compaction-cases.log)、[补基线后缺少历史目录映射的失败记录](/data/1/tmp/namespace-v3-compaction-handoff.log)。
- [最终编译输出](/data/1/tmp/namespace-v3-build-mapping.log)，既有 release 目录内 `source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功，无外部下载。

本次二进制 SHA-256：`a0623ac1abba72cc6800393a1a8c39cff1ed9a81712f3e521cbfc4d0b34376e0`。

同一二进制的回归也已通过：

- [V2 40 表验收](/data/1/tmp/namespace-v3-v2-regression.log)：根共享、真实 COW、8 路并发首次访问、业务事务回滚、旧快照、目录恢复和 DDL 边界。
- [V1 与关闭开关的对照](/data/1/tmp/namespace-v3-v1-regression.log)：`lazy_fixed_snapshot` 和 `flag_off_keeps_eager_database_fork` 均为 PASS。
- 新脚本 Python 语法检查、`git diff --check` 通过。实验实例均已停止，数据已归档。

## 边界

全局快照保护仍然保留，为未访问的继承表保护来源旧版本；没有证明移除该保护或删除来源后的正确性。尚未优化已物化表的目录访问和锁根开销。这里的后台基线构建是复用现有 fork 流程的原型落点，不代表完整 namespace 的精确对象引用和回收协议已实现。
