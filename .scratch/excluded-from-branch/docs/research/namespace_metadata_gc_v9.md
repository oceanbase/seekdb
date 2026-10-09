# Namespace 原型 V9：元数据页回收与性能验证

分支 `codex/namespace-metadata-gc-v9`，基于 V8 `370738f73`。新增实验模式 6，复用模式 5 的持久元数据结构。

问题：重复 fork、首次物化、删除会留下不可达 B+ tree 页。怎样在真实引擎内回收这些页及其引用的 schema 数据块，同时保护旧根读者和尚未提交的目录更新？并测量 fork、冷表首读、热表查询随表数、分支数和链深度变化的成本。

## 实现范围

临时手动入口：

```sql
FORK DATABASE __gc__ TO __gc__;
```

它在执行命令的既有线程中运行全局元数据 GC，没有新增专用线程或定时任务。每次最多删除 256 个页/数据块；还有垃圾时可再次执行。标记过程仍遍历全部可达元数据，256 的限制不意味着整轮耗时有固定上限。

这次回收 `__fork_proto_meta.pages` 中的不可达记录，包括目录节点和序列化 schema 数据块。没有回收进程内 `SchemaHolder` / `DatabaseHolder`，没有复用 namespace 名称或 ID，也没有删除 namespace 墓碑。

## 读者与未提交更新

V7 的存储活动计数不足以保护元数据：解析表名、读取 catalog 等动作发生在存储准入之前。V9 为读取目录并使用其页的入口增加 `MetadataReadGuard`：

- 正常操作共享一个进程级读写锁，覆盖从取得根到读完节点和 schema 数据块的期间。返回的 schema 已复制到现有 holder，不依赖原页继续存活。
- 同线程嵌套调用只在最外层持有锁，避免 bootstrap 更新目录及后台基线查 schema 时重复加锁。
- GC 取得排他锁后才扫描。等待期间检查请求超时；超时不执行回收。GC 执行时，新的元数据读取/更新可能等待。
- 已物化 tablet 的存储快路径不额外取得这个锁；缓存 schema 的返回也无需延长页的生命周期。

仅有这个锁仍不够：原生 CREATE 的 `observe_schema()` 已经结束时，外层 DDL 事务可能还没提交。该事务可能引用一张此前不可达但被内容去重重新使用的页。

因此 GC 在同一事务里对全部 namespace 注册行执行 `FOR UPDATE NOWAIT`。目录更新本来就在对应根行锁下进行，这些锁保持到发布事务结束。若任何外层事务尚未结束，GC 退出并回滚，可稍后重试。不能在持有 GC 排他锁时无限等待根行，因为持有根行的事务可能还需要再次进入元数据操作。

原生 schema、内核实验元数据表自身的 SQL 访问不走虚拟 namespace 页读取入口，避免 GC 的内部 SQL 再进入同一排他锁。

常驻新增状态只有一个锁和每线程嵌套深度；没有新增按页、分支或读者增长的索引。单轮标记使用临时集合/栈，内存随可达元数据量增长，结束后释放。

## 标记与删除

1. 取得 GC 排他锁并成功锁住 namespace 行。
2. 收集 namespace 当前非零 catalog/directory 根，包括 DELETING 尚未清空的根；再收集所有仍有引用的 canonical snapshot 根。已删除祖先 namespace 不需要恢复为 LIVE。
3. 沿节点引用遍历，检查页校验和和节点结构；叶子中的 schema 对象也标记并检查存在性/校验和。发现错误则整轮失败，不执行 DELETE。
4. 扫描页 ID，选最多 256 个未标记对象，一条事务内 DELETE 删除；核对实际删除数量。
5. 提交事务，释放锁。提交前崩溃由引擎事务恢复回滚，重试重新计算可达集合，不持久化半成品标记表。

页内容没有改变，COW 写路径、快照上限、祖先输入选择以及 tablet 的既有 GC 保持原语义。手工篡改实验元数据表不属于支持的接口。

## 运行

```bash
# 正确性：生命周期、并发、提交前崩溃；真实实例顺序运行并在退出时归档
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_metadata_gc_prototype.py \
  --binary build_release/src/observer/seekdb

# GC 能力开启，但计时期间不主动运行 GC
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_fork_performance_prototype.py \
  --binary build_release/src/observer/seekdb --mode 6

# 并发发起手动 GC，专门测量它对查询的影响
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_fork_performance_prototype.py \
  --binary build_release/src/observer/seekdb --mode 6 --tables 10 --with-gc
```

GC 脚本的 Python 树遍历只用于独立验收：从持久根计算应保留的节点/schema 对象，逐字节核对存活数据，并检查每轮删除数。删除决定和执行都在内核。性能脚本每个配置采样 20 次，输出完整原始毫秒值、中位数和最近秩 P95；可用 `--samples` 调整。

两列整数、每表两行；本地客户端、真实 2G 内存/4 CPU 实验实例。计时包括 SQL 往返，源建表、页检查和分支删除不计入 fork/查询耗时。冷查询包含首次物化，热查询紧接同一张已物化表的首读。依次改变表数 10/100、额外存活兄弟分支 16/64、父分支深度 8/32，不做笛卡尔积压测。表行数固定，因此这些数据不能代表大数据扫描或生产容量。

## 已验证的正确性

二进制 SHA-256：`eceaaee620df3f82beaa41c373def074bac687ce0634e7f9b12d6294a750958f`。

[最终编译日志](/data/1/tmp/namespace-v9-build-native-sync.log)：`source ~/.bashrc` 后在已有 release 目录执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功。

[最终 GC 验收日志](/data/1/tmp/namespace-v9-gc-final.log)三个用例 PASS：

| 用例 | 结果 |
| --- | --- |
| 生命周期 | A→B→C 混合热/冷表，删除 A/B 后 GC、重启，C 保留正确视图。20 轮 fork→访问两表→删除→GC 后，存活页集合逐字节相同，稳定在 28 页；最后删除 C 后为 0，重启仍为 0 |
| 旧根读者 | 在读取 catalog 根后暂停 SHOW CREATE，删除 B 使旧根失去持久所有者；GC 等待且页不变。读者放行后可回收；后续 schema 查找正确返回已删除表不存在，没有越过存储关闭状态 |
| 外层 DDL | 原生建表已写目录且读取保护已释放、DDL 尚未提交时，GC 返回 1205，页不变。建表提交后 GC 可执行，新 fork 可看到该表 |
| GC 提交窗口 | GC DELETE 尚未提交时，新的 fork 和冷表查找等待；外部仍看到原页集合。放行后两个请求成功，后续 GC 和重启数据正确 |
| 分批与崩溃 | 50 张源表产生超过 256 个不可达对象。第一批 DELETE 提交前强杀，重启后原页集合及引用全部恢复；重试分两批清理到 97 个可达对象，重启后首读正确 |

旧版本第一次 GC 验收中的生命周期和旧根读者已通过，但原生 CREATE 没有命中既有外层同步点，因此整个并发用例未通过；见[首次日志](/data/1/tmp/namespace-v9-gc.log)。现已在元数据读取保护释放后复用该同步点，准确覆盖事务提交间隙，并重跑全部用例。没有放宽并发断言。

## 性能证据

[V8 原始基线](/data/1/tmp/namespace-v9-baseline.log)先于代码修改运行，10/100 表的 fork 中位数分别为 6.22/6.96 ms，冷查询为 10.36/11.80 ms。64 个兄弟分支配置有 61.67 ms 的 fork P95；不能凭一次尾延迟就归因于快照刷新。

[V9 模式 6 复测](/data/1/tmp/namespace-v9-performance.log)和[阶段计时](/data/1/tmp/namespace-v9-stage-times.json)已经完成。阶段计时显示：10 表实例所有 193 次非 bootstrap 捕获的发布部分中位数 4.09 ms，快照刷新 0.75 ms；冷表祖先查找经过 2/9/33 个快照行时，中位数分别为 56/262/872 μs。链深度成本存在，但在这组规模下未成为首读的主要耗时。

使用同一产物和同一脚本设置的[模式 5 对照](/data/1/tmp/namespace-v9-performance-control.log)与模式 6 结果如下，单位 ms；每格为“中位数 / P95”。模式 6 此表计时期间没有主动执行 GC。

| 配置 | 模式 5 fork | 模式 6 fork | 模式 6 冷查询 | 模式 6 热查询 |
| --- | --- | --- | --- | --- |
| 10 表、父深度 1 | 5.05 / 6.24 | 5.63 / 10.04 | 10.24 / 15.68 | 0.57 / 0.87 |
| 100 表、父深度 1 | 9.44 / 24.53 | 6.02 / 10.18 | 10.81 / 18.51 | 0.57 / 0.86 |
| 10 表、16 个额外兄弟 | 5.14 / 6.48 | 5.38 / 7.56 | 9.87 / 12.44 | 0.51 / 1.15 |
| 10 表、64 个额外兄弟 | 5.12 / 8.80 | 6.71 / 12.67 | 12.33 / 27.49 | 0.75 / 2.00 |
| 10 表、父深度 8 | 4.52 / 5.71 | 4.76 / 5.88 | 8.97 / 12.06 | 2.81 / 3.76 |
| 10 表、父深度 32 | 5.73 / 7.79 | 4.91 / 6.12 | 9.34 / 16.20 | 2.88 / 4.29 |

20 个样本不足以消除机器、后台任务和缓存状态的波动，不据此宣称 V9 更快、零开销或大规模 O(1) 延迟。热查询时间也包含 SQL 计划校验，阶段间的差别不能单独归因于祖先链。所有正常捕获均检查页数和目标 tablet 集合不增加。

[GC 运行期间的实验](/data/1/tmp/namespace-v9-performance-during-gc.log)在测试驱动中每次 GC 结束后等待 50 ms，再发起下一次；引擎没有新增线程。共完成 11 次 GC，无请求错误，GC 请求耗时 4.57–18.80 ms（包括等待锁及 SQL）：

| 同一实例 | fork | 冷查询 | 热查询 |
| --- | --- | --- | --- |
| GC 未运行 | 5.18 / 8.66 | 9.75 / 17.29 | 0.58 / 1.06 |
| 并发手动 GC | 4.66 / 9.01 | 8.64 / 15.02 | 0.44 / 0.69 |

样本中没有出现稳定的延迟退化，也不能推出 GC 不阻塞请求：并发同步点用例已经明确证明它会阻塞新的元数据读取。此测量包含 GC 与请求的实际调度重叠，而不是强制每个请求都撞上 GC 排他阶段。大目录需要另测。

本轮没有据此改写快照刷新或增加查找缓存：已量到它们的成本，但当前证据不足以证明这两处是主要瓶颈。

## 回归与归档

- [V8 / 模式 6 完整回归](/data/1/tmp/namespace-v9-regression-v8.log)两个用例通过：混合热/冷目录，先删除原生来源再 fork，已删除父 tablet 的真实基线与子分支 MAJOR_MERGE，最终存储回收，以及发布/级联释放提交前崩溃重试。
- [V4 / 模式 2 回归](/data/1/tmp/namespace-v9-regression-v4.log)通过：根锁下已有 tablet 快路径、并发首次物化、超时回滚和物化提交前强杀恢复。
- 模式 5 对照实验同时验证旧模式的捕获不逐表物化、冷热查询和分支删除仍成立。没有重复运行独立 V1/V2/V3/V5/V6/V7 套件。
- Python 语法检查、`git diff --check` 通过。所有本轮实例均已停止并归档。

最终 GC 实例：

- [生命周期事件](/tmp/namespace_fork_PROTOTYPE_metadata_gc_v9_lifecycle_vgiji31x/experiment.jsonl)
- [并发事件](/tmp/namespace_fork_PROTOTYPE_metadata_gc_v9_concurrent_zjaylp_g/experiment.jsonl)
- [崩溃事件](/tmp/namespace_fork_PROTOTYPE_metadata_gc_v9_crash_mqn4qsd0/experiment.jsonl)

本机磁盘余量有限，已停止且已归档的 GC/性能实例的引擎日志使用 gzip 压缩保留；`experiment.jsonl`、`data.tar.gz`、上述验收输出和阶段计时 JSON 均保留。短期 GC/性能实验设置 `max_syslog_file_count=0` 以避免磁盘压力下自动删除验收日志；不修改系统配置。

## 保留的边界

- GC 是人工触发的原型，标记阶段会阻塞需要读取元数据的请求；大目录尚未测量，不宣称在线回收没有延迟影响。
- 持续的元数据操作可能让 GC 等待到请求超时，失败后需重试；没有增加常驻读者注册表来解决全局排空的隔离和公平性。
- 原生来源首次登记仍按已有原型在准备阶段完成，没有扩展其与并发 DDL 的语义。
- 存储空间、schema holder、namespace 墓碑、快照管理器本身的内存各有独立生命周期；页记录删除不等于 RSS 降低或预分配数据文件缩小。
- 模式 6 不增加持久列，但仍不支持运行中切换实验模式或生产升级/降级。
- namespace worker/IPC、完整复合对象键、用户/权限/config、子分支通用 DDL 均未纳入本轮。

## 后续讨论

后续已转向 [V10：统一端口与跨平台 SQL worker 草案](namespace_sql_worker_v10_plan.md)。[流程原型与验收记录](namespace_sql_worker_v10.md)已跑通 Linux 上两个独立 SQL worker 共用引擎的只读与退出闭环；Mio 高并发和完整平台适配仍待实现，schema holder 的单独回收不作为这一阶段的优先工作。
