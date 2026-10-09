# 8000 分区 COUNT/SUM 慢查询定位（2026-10-09）

## 当前状态

已删除扫描适配层的完整 schema 复制、全分区 tablet ID 改写和逐行中转；直接使用 SQL 执行计划已有的列描述及原生扫描迭代器。实施、性能复测与回归结果见 [扫描入口重构](native-scan-results.md)。以下保留修复前的定位证据，源码行号对应诊断时版本。

## 诊断时结论

已定位，尚未修改生产代码。全分区查询的主要开销在 Namespace 进程内扫描适配层：每打开一个 tablet 扫描，都重新深拷贝两份完整 `ObTableSchema`，遍历整张表的分区定义，生成及检查逻辑、物理 tablet 列表。访问 S 个分区、表共有 P 个分区时，这部分准备工作是 O(S × P)；全分区扫描成为 O(P²)。复制的是表和分区描述，不是用户数据。

父、子 Namespace 均经过这段代码。该现象不能归为孩子首次物化、数据行太多或目录 KV 查询占主导。

## 环境与复现

- 源码 HEAD：`7a5f822c70e9af4fdde80535690fac2c4be87eed`，生产代码同 `ff66963b2`。
- 正式二进制 SHA-256：`b36993b04c989664b8d8ff39f49352c6625bf9dc36fbde77ca846e1039d61bd1`，本轮重新核对一致；未加生产 hook。
- 一张两列整数表，8000 HASH 分区、8000 行、每行 v=10；没有二级索引或 LOB。`cpu_count=4`、`memory_budget=8G`、后台维护开启、合并间隔 5 分钟。
- 从 ns1 fork `scan_child`；父子都执行 `SELECT COUNT(*),SUM(v) FROM scan_diag.t`，结果均为 `(8000,80000)`。本次未执行前次服务延迟测试中的单行 UPDATE，因此 SUM 与前次 80001 不同。
- 独占本测试实例，无其他测试 SQL 并行；共享宿主机，未隔离 CPU。

去掉采样及符号分析后的完整查询实测：

| Namespace | 全分区耗时 |
| --- | ---: |
| 父 ns1 | 38.124 秒 |
| 子 scan_child | 45.444 秒 |

最初带采样的父查询为 69.891 秒，期间 perf 符号分析也在运行，故不作为无干扰基准。子查询带采样为 45.156 秒。性能现象与前次孩子 43.59—50.80 秒一致。

## 控制变量：只读 256 个分区、256 行

另外建立 256、1024 分区表，并 fork 一个包含这些表的新孩子。三张表均扫描 p0…p255，返回 `(256,2560)`，只有完整表定义的分区数不同。每组重复三次，中位数如下：

| 全表分区数 | 父查询 | 子查询 |
| --- | ---: | ---: |
| 256 | 95.6 ms | 96.0 ms |
| 1024 | 203.5 ms | 217.4 ms |
| 8000 | 1542.1 ms | 2566.7 ms |

读取的分区数和行数不变，父、子分别放大 16.1、26.7 倍。再次执行带放大倍数断言的短反馈循环，父 18.53 倍、子 20.42 倍，预期失败退出 1：

```bash
# fixture 保持运行时，复用已准备的三张表。
python3 .scratch/namespace-fork/fullscan-diagnosis/scaling.py \
  --reuse --max-amplification 8
```

这里的 8 倍是定位用的宽松断言，不是产品 SLA。原始失败日志保留为 `fullscan-diagnosis/scaling-red.log`，不能因正确性检查通过而把性能缺陷标为修复。

## CPU 采样与源码对应

使用本机 perf、99 Hz、用户态 cpu-clock，父采样 20 秒（2046 samples），子采样 15 秒；无外部下载或上传。以下是包含子调用的 CPU 样本比例，不是可以相加的墙钟时间比例。

| 函数 | 父 | 子 |
| --- | ---: | ---: |
| `EngineScan::open` | 85.39% | 84.92% |
| `copy_scan_schema` | 72.09% | 72.65% |
| `ObTableSchema::assign`，两处合计 | 59.92% | 58.94% |
| `resolve_read_tablet`，含 KV 和状态探测 | 4.79% | 4.31% |
| `find_tablet_source` | 2.79% | 2.35% |

采样调用链：

```text
ObGranuleIteratorOp::get_next_granule_task
  → ObTableScanOp::local_iter_rescan
  → ObDASScanIter::rescan
  → InProcessScanIterator::open
  → EngineScan::open
  → copy_scan_schema
      → logical->assign(source)                     // 完整深拷贝一次
      → logical->get_tablet_ids(...)                // 遍历全表分区
      → make_storage_schema(...)
          → storage_schema.assign(logical_schema)  // 再完整深拷贝一次
          → rewrite_tablet_ids(...)                // 改写全部分区的 tablet ID
      → routed->get_tablet_ids(...)                 // 再遍历全表分区
```

具体位置：

- `src/observer/namespace_inprocess_scan_engine.ipp:43`：每个 EngineScan 调用 `copy_scan_schema`。
- `src/observer/namespace_inprocess_scan_schema.ipp:107`：两份 schema、两份 tablet 列表。
- `src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp:1162`：`make_storage_schema` 完整 assign 后改写 tablet ID。
- `src/observer/namespace_inprocess_scan_engine.ipp:53`、`:76`：对完整 tablet 列表做成员检查；`:117`：进程内路径仍计算原 RPC 消息 schema 的序列化大小。
- `src/observer/namespace_inprocess_scan_service.ipp:171`：reuse 重置扫描；重开走上述准备。已有 NLJ 同 tablet rescan 的复用不能消除本次跨分区重开的开销。

因此，8000 次 tablet 扫描会反复处理 8000 项分区描述；仅两次完整复制就属于约 2 × 8000 × 8000 量级的分区对象复制工作。点查也会承担一次全表定义准备，但不会乘上 8000 次扫描。

`copy_scan_schema` 的主要代码在 `ead5e2da21` 已存在。本次 GC、后台维护归属和 DDL 发布修改没有引入它。此前 `a11059085` 的准备优化覆盖创建、DDL 和写路径，不能把它表述成扫描适配层也已消除整表重复复制。

## 诊断时修复方向

后续核实发现 SQL 计划已经持有需要的列描述，因此最终实现直接传递已有 scan param，没有再增加 schema guard、schema 副本或准备缓存。以下是诊断阶段提出的方向。

1. 扫描持有 schema guard，借用已经存在的固定版本逻辑 schema，保证其生命周期覆盖迭代器。
2. 路由当前 tablet 的物理 ID 与 fork SCN，放入扫描参数；不为了路由一个 tablet 复制、改写整张表的分区数组。
3. 删除进程内路径遗留的整表 RPC 序列化大小检查；复用已经准备的列描述。表、版本和请求绑定校验应由既有上层准备保证，避免每个 tablet 再枚举全表。
4. 跨分区切换时重建必要的 tablet 访问状态、保护和原生迭代器；复用本次扫描的表级准备。不新增全局 cache 或长期状态。
5. 修复验收除了上述计时，还须覆盖分区读、索引、LOB、同 tablet NLJ rescan、schema 生命周期及并发 DDL，防止去掉副本后出现悬空引用或复用旧 tablet 保护。

## 用例、证据与清理

- `fullscan_diagnosis_probe.py` 的父子全分区与部分分区结果检查已加入四件套 direct；记录性能，不设置共享宿主机上的硬时间门槛。可显式传 `--max-scan-seconds` 用于性能断言。
- 本轮运行了该场景、控制变量组和原始全扫描复测，没有运行完整四件套、mysqltest 或 sysbench。
- 原始 harness 完成全部查询后进入人工诊断等待；测量结束后中断该等待，由 finally 停止所属实例并归档。等待退出不是产品故障。
- `fullscan-diagnosis/results.json`、`scaling.jsonl`、`unprofiled.jsonl` 保存结构化结果；`repro.log`、`scaling-red.log` 保存原始记录。大 perf 文件路径记录在 results.json，不提交采样二进制。
- 临时实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_fullscan_diagnosis_j3xp7zqs`，已停止。没有改动生产源码或用户已有实例。
