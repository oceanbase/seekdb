# Namespace fork 最小原型：实现与验证结果

2026-09-14，分支 `codex/namespace-fork-prototype`，基于 `dbe8fcdbb2ed1ea97a12d781ff0ffad8638e01b8`。

本文保留 V1 的实现与原始验收记录。后续已完成 [内核 V2](namespace_fork_kernel_v2.md)：持久 COW B+ 树、继承 catalog、普通 SQL 在存储入口自动物化及崩溃重启验证。

**核心假设通过有限夹具验证：捕获时可以省去逐表物化，稍后按同一个旧快照创建可写目标表。** 首次访问仍执行目标建表、事务提交、schema 发布和基线读取。独立向量实验也通过，但明确观察到了首次查询补增量、反序列化持久图；没有消除这部分成本，也没有测延迟。

这是一次性、单进程、来源保留的真实引擎实验。完整范围与取舍见 [最小方案](namespace_fork_minimal_prototype.md)。

## 复现

在当前工作区运行，使用本次构建的二进制和本机已有的 PyMySQL：

```bash
python3 tools/obtest/namespace_fork_prototype.py --binary build_release/src/observer/seekdb
```

默认顺序运行 2 表、20 表、关闭原型开关的对照，每项独占新实例。独立向量实验：

```bash
python3 tools/obtest/namespace_fork_prototype.py --binary build_release/src/observer/seekdb --vector-only
```

两条命令均已退出 0。脚本负责开关、空闲端口、启动、断言和停止，只操作自己创建的 `namespace_fork_PROTOTYPE_*` 目录。每个实例配置 2 GB 内存预算、2 GB 日志盘，数据文件初始 256 MB。结束后保留 SQL/断言日志和引擎日志，数据压入 `data.tar.gz`；这些归档不代表可恢复的分支。

构建使用本地依赖缓存和 `CARGO_NET_OFFLINE=true make -j80 seekdb`，没有下载外部依赖。工作盘空间不足时，只将本次生成的 `build_release` 移到了 `/tmp/namespace-fork-build-aik1qbg1/build_release`，当前工作区保留软链接。二进制 SHA-256：

```text
80b8ff513edfa41c79f27391a99f41c800469cd5401d50856a21dc25ebd940db
```

## 实现落点

| 路径 | 本次新增行为 |
| --- | --- |
| `src/rootserver/fork_table/ob_fork_database_service.cpp` | 实验捕获：同一元信息事务创建空目标库、获取 S 并登记 `tablet_id=0` 的快照保护；提交后经正常 reload 和 schema 发布流程，再发布运行时句柄 |
| `src/rootserver/fork_table/namespace_fork_prototype.h`、`ob_fork_table_util.cpp` | 最多 8 条运行时记录；校验来源/目标身份、固定 schema、原保护和存储保留边界；记录实际 helper 调用计数 |
| `src/rootserver/fork_table/ob_fork_table_service.cpp` | 实验目标使用已登记 S，复用 `fork_single_table_in_trans_`，不为每张表重新取快照 |
| `src/storage/compaction/ob_freeze_info_mgr.h` | 添加实验用正常 reload 入口；未改变快照保留算法 |
| `tools/obtest/namespace_fork_prototype.py` | 真实实例夹具、显式 `ensure_table`、SQL 和后台任务状态断言、原始证据留存 |

进程环境变量 `SEEKDB_NAMESPACE_FORK_PROTOTYPE=1`、目标库名前缀 `__fork_proto_b` 同时满足才进入实验路径。来源库名前缀须为 `__fork_proto_a`。关闭开关时，现有 `FORK DATABASE` 仍立即创建全部目标表，对照已通过。

捕获入口只校验 database，不枚举其用户表。首次物化时只接受两列整数 `id`、`v`、单列主键 `id`、非分区、无附属对象的固定结构；来源 schema 晚于捕获版本会被拒绝。实验驱动串行准备目标表，业务事务在准备完成后开始。任意 SQL 的自动按需解析没有实现。

## 普通表结果

| 验证点 | 2 表夹具 | 20 表夹具 |
| --- | --- | --- |
| 捕获时目标用户表 / fork 任务 | 0 / 0 | 0 / 0 |
| 捕获前后单表物化 / tablet 收集 helper 计数 | 0 / 0 | 0 / 0 |
| 首次只访问 t1 | 仅建立 t1 | 仅建立 t1 |
| 捕获时尚未提交的跨 t1/t2 事务 | 两表都不带入晚提交 | 两表都不带入晚提交 |
| A.t2 后续增删改，GC 新快照水位超过 S 后再准备 B.t2 | 仍读原 S | 仍读原 S |
| B 独立增删改、事务回滚、重复准备同一表 | 通过 | 通过 |
| 捕获后新建来源表再尝试物化 | 拒绝，错误 1235 | 拒绝，错误 1235 |
| 访问完成后目标用户表 | 2 | 2，其余 18 表未物化 |

初始数据确认在活跃 memtable 中。所有读写发生时，fork 后台任务一直停在 `FORK_TABLE_WAIT_FREEZE_END`，任务状态为 19；日志未出现 `fork table freeze stage done`。这证明本轮前台操作可在该后台阶段之前完成，没有验证解除暂停后的正常转储交接。

旧快照保护的直接证据：

| 夹具 | 捕获 S | 延迟物化 t2 时的 GC SCN | 存储保留计算结果 |
| --- | --- | --- | --- |
| 2 表 | 1789357461998797009 | 1789357470166215003 | `SNAPSHOT_FOR_MULTI_VERSION`，边界为 S |
| 20 表 | 1789357476945452009 | 1789357484951707003 | `SNAPSHOT_FOR_MULTI_VERSION`，边界为 S |

测试设置 `undo_retention=0`，并关闭额外的活跃事务保守水位 `_mvcc_gc_using_min_txn_snapshot`，避免初始零水位掩盖显式保护；全局 GC 水位实际前进。延迟入口校验系统表中的原保护，并调用正常存储保留计算。计算探针以 S 作为候选合并版本，避免传 0 选中未做 major 时的特殊零边界。

因此，证据覆盖了保护登记、生效计算、正常水位更新后的旧版本读取。它没有强制执行一次淘汰旧版本的物理合并，也没有证明长期 GC、日志回收和重启恢复完整正确。捕获仍有 GTS、元信息事务、reload 和 schema 发布成本；有限夹具及 helper 计数不能证明整个控制路径恒定耗时。

## 向量首次查询结果

向量采用独立的现有 `FORK TABLE` 路径，原型开关关闭；没有接入上面的延迟多表捕获。`SHOW CREATE TABLE` 确认 `SYNC_MODE=ASYNC`、`ORGANIZATION HEAP`，索引为 HNSW。两项均在 fork 后台主动 freeze 前执行首次目标查询，`EXPLAIN` 明确包含 `VECTOR INDEX SCAN`。

| 已确认的来源状态 | 首次目标 ANN | 来源删除最近点后目标 ANN | 目标删除最近点后的精确查询 |
| --- | --- | --- | --- |
| 仅增量，snapshot 辅助表 0 行 | id=4 | 仍为 id=4 | id=1 |
| 持久图加增量，snapshot 辅助表 1 行 | id=4 | 仍为 id=4 | id=1 |

持久图夹具通过 `dbms_vector.rebuild_index` 建立，轮询真实 snapshot 辅助表确认就绪，再添加增量。没有将后台等待结束当作夹具就绪。目标修改后的验证为精确查询，未据此宣称异步 ANN 维护全过程已覆盖。

驱动在首次 ANN 后读取 `last_trace_id()`，可直接关联 [向量装载日志](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_vector_kqeahq34/vector_loading.log)：

- 仅增量：`YB427F000001-00065B6954C8C69D-0-0` 出现 `SYCN_DELTA_prepare_data`、`SYCN_DELTA_complete_data`，内部 buffer count=8。
- 持久图加增量：`YB427F000001-00065B6954C8C6B2-0-0` 出现上述增量路径，内部 buffer count=2，并出现 `[OBVSAG] fdeserialize success`。

内部 buffer count 不是唯一文档数量。这里确认的是首次查询的实际工作来源，不是耗时测量。首次查询仍有补增量和装载图的工作，大图成本没有被本原型解决；共享内存图、增量分代和 D1 门禁方案均未实现。

## 原始证据与下一步边界

| 内容 | 文件 |
| --- | --- |
| 编译成功日志 | [namespace-prototype-build-pin.log](/data/1/tmp/namespace-prototype-build-pin.log) |
| 2/20 表和关闭开关对照的完整输出 | [namespace-prototype-final-cases.log](/data/1/tmp/namespace-prototype-final-cases.log) |
| 2 表 SQL/断言 | [experiment.jsonl](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_2_ztacmse8/experiment.jsonl) |
| 20 表 SQL/断言 | [experiment.jsonl](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_20_kof2jacg/experiment.jsonl) |
| 关闭开关对照 SQL/断言 | [experiment.jsonl](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_legacy_ipqu9guw/experiment.jsonl) |
| 向量查询、执行计划和 trace ID | [namespace-prototype-vector-evidence.log](/data/1/tmp/namespace-prototype-vector-evidence.log) |
| 向量 SQL/断言 | [experiment.jsonl](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_vector_kqeahq34/experiment.jsonl) |

本轮按最小范围结束。若继续产品化，普通表首先需要解决持久对象目录、精确保留和正常后台交接；向量若要求首次查询低延迟，需要另做大图装载成本测量，再决定共享图或预热策略。当前没有实现来源删除、多代分支、重启恢复、SQL worker/IPC、权限隔离或在线释放根引用。
