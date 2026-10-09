# Namespace 原型 V8：多代 fork

分支 `codex/namespace-lineage-v8`，基于 V7 `44ff3a2e9`。

后续 [V9：元数据页回收与性能验证](namespace_metadata_gc_v9.md) 增加手动 GC、元数据读者保护和性能实验；本页记录 V8 当时的实现和证据。

验证 A→B→C：B 修改热表、保留未访问冷表，再 fork C；随后修改 B、删除 A/B、重启，C 仍保留自己的视图。fork 只捕获根，不能遍历/物化全部表。最后删除 C，快照链与相关存储释放。

## 两种输入必须同时成立

设 A→B 的快照为 S1，B→C 的快照为 S2。B 修改了热表，冷表一直没有打开：

| C 的表 | 首次访问时的输入 | 原因 |
| --- | --- | --- |
| 热表 | B 的私有 tablet @ S2 | 必须包含 B 在第二次 fork 前的修改，排除其后修改 |
| 冷表 | A 的原始 tablet @ S1 | B 自己看到的就是 A@S1，不能变成 A@S2 |

因此不能把所有叶子的快照都覆盖成 S2，也不能把所有输入都指向 B。目录叶子继续保存原始 local tablet ID 和可选的已绑定物理 tablet ID；读取时取路径上所有非零快照上限的最小值。B 自己物化后只改对应的 COW 路径，未改路径继续保留 S1。全冷的 B→C→D 连根引用上的旧上限也要保留。

## 内核实现

新增实验模式 `SEEKDB_NAMESPACE_FORK_PROTOTYPE=5`，复用模式 4 的 namespace 注册、B+ tree、存储入口、MDS、关闭排空和 GC。旧模式 4 的元数据表结构保留。

### 捕获与持久引用

`control_namespace()` 允许从任意 LIVE namespace fork。锁住来源根行，捕获 catalog/directory 根，并用 `min(已有非零上限, 新 S)` 设置根引用；不扫描目录、不创建目标 tablet、不重写元数据页。新 snapshot 与 namespace 发布、原生 acquired snapshot pin、新增的父引用在同一事务提交。

模式 5 的 canonical snapshot 行在既有五列后增加：

```text
catalog_cap, directory_cap, parent_ref, ref_count
```

引用数计算为：直接引用这个 snapshot 的 namespace 数量，加上以它为 parent_ref 的 snapshot 数量。每个新 snapshot 持有父 snapshot 的一个引用，父 S 严格小于子 S。删除 A/B 的 namespace 根后，C 仍通过持久 snapshot 链保护所需输入，不依赖 A/B 保持 LIVE。

`release_lineage()` 从新到旧锁 snapshot 行：还有其他持有者则减一后停止；最后一个持有者离开则删除对应 pin 和 snapshot 行，再释放该 snapshot 持有的父引用。整条级联释放和分支私有 tablet DELETE、映射删除、namespace 根清空属于同一事务。崩溃回滚后可以重试。

每个 snapshot 新增四个 8 字节标量，即 32 字节原始字段负载；这不包含 SQL 行、索引、缓存和分配器开销。没有新增常驻 lineage/绑定索引，查询链时使用临时容器。既有 schema holder 仍保持到进程退出。

### 首次物化与后台交接

`ensure_tablet()` 使用目录中的有效上限和真实输入：有继承绑定则取祖先的物理 tablet，否则取原始 local tablet。沿 snapshot 链找到该上限对应的 pin，并使用那个 snapshot 的 schema version 验证保护；A 的原生 schema 删除后，子 snapshot 的当前 schema version 可以不同于祖先 pin。

继续使用已有 type 3 物化和 tablet 创建事务。逻辑出生版本使用实际输入 S，与 fork info 的 S 保持一致；物理创建仍必须 ON_COMMIT 才可访问。提交后目录绑定改成子 namespace 私有 tablet，原始 local ID 不变，已有 tablet 仍走 V4 快路径。

后台基线可能需要处理已删除但被 C 引用的 B tablet。`bound_value()` 先查 LIVE 所有者；所有者已关闭则从 LIVE 后代的 snapshot 引用沿父链查不可变目录。`check_baseline_access()` 在查找前登记 V7 的活动计数，保证最终后代关闭后不能再进入新的来源读取。已经进入的 DAG 由删除流程排空。实际输入与 S 从 tablet 的 fork info 取得，继续复用原生基线 DAG、转储和合并。

分支 DROP 只删除自己拥有的绑定，跳过继承的祖先物理绑定。empty-shell GC 检查仍有引用的全部 canonical snapshot 目录，按 local ID 查条目后匹配真实物理输入；存在引用则继续保留，最后引用释放后按既有 GC 回收。即使基线已经完成，目前也保守保留祖先快照链，直到后代释放。

## 一条命令验收

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_lineage_prototype.py \
  --binary build_release/src/observer/seekdb
```

默认顺序运行 `lineage`、`crash` 两个真实独占实例，也可用 `--case lineage` 或 `--case crash` 单独执行。Python 仅准备实验元数据表、发 SQL、操纵既有同步点并核对结果；捕获、物化、引用维护、恢复和 GC 都在内核执行。退出停止实例并归档数据。

临时控制语法保持如下；`b`、`c` 是逻辑 namespace 名，SQL 表地址仍为 `__fork_ns_<id>__db.table`：

```sql
FORK DATABASE a TO b;
FORK DATABASE b TO c;
FORK DATABASE a TO __drop__;
FORK DATABASE b TO __drop__;
```

## 验收证据

二进制 SHA-256：`4fe863dd1c6d00948194ffd7fa5e68de4a88c9c1199b619a2904509a0e7314ae`。

[编译日志](/data/1/tmp/namespace-v8-build.log)：在既有 release 目录，`source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功。没有下载依赖。

[主验收日志](/data/1/tmp/namespace-v8-lineage.log)中的 `multi_generation_namespace_fork` PASS：

| 检查 | 实测结果 |
| --- | --- |
| 混合目录 | 两个数据库共 10 张表，B+ tree 高度 2。B 热表已修改，另 9 张表未物化；C 捕获后热表上限 S2=`1789378742126414004`，其余为 S1=`1789378742055191008` |
| fork 无逐表工作 | B→C 前后全部 76 个不可变页内容完全相同；目标物理 tablet 集合不变，仅有 B 热表。B→sibling 的捕获也保持全部 78 页和 tablet 集合不变 |
| 热表隔离 | C 保留 `(1,90),(2,20)`，不受随后 B 更新为 100、删除 id=2、插入 id=3 影响 |
| 冷表隔离 | A 后来改成 11/12、B 后来物化并改成 77；删除 A/B 并重启后，C 冷表首次读取仍是 `(1,10),(2,20)` |
| 所有权 | 删除未物化 sibling 不删除继承的 B tablet；删除 B 后，不被 C 引用的 B 冷表私有 tablet 回收，B 热表和原始 A 输入保持 DELETED、已提交、非空 |
| 后台交接 | 真实源 MINI_MERGE、已删除 B 热表的基线、C 热/冷表基线完成；C 完成 S2 之后的 MAJOR_MERGE。重启仍保留 C 自己的 UPDATE/DELETE/INSERT，剩余 8 张冷表首读正确 |
| 最终释放 | 删除 C 后 snapshot 链和 pins 均空，相关私有/来源 tablet 成为空壳或已被正常 GC 移除，映射消失；再重启仍成立 |

主实例：[事件](/tmp/namespace_fork_PROTOTYPE_lineage_v8_lineage_7cyt8qyh/experiment.jsonl)、[数据归档](/tmp/namespace_fork_PROTOTYPE_lineage_v8_lineage_7cyt8qyh/data.tar.gz)。

随后加强 `crash` 用例，先删除 A，再进行 B→C；[最终崩溃验收日志](/data/1/tmp/namespace-v8-crash-source-gone.log)的 `lineage_capture_and_cascade_crash_retry` PASS：

- A 已删除且原生 schema 不再存在时，B→C 提交前暂停。外部看不到 C，父引用数和 pins 保持原值，没有目标 tablet。强杀重启后发布及父引用增量回滚，重试成功。
- 全冷 B→C→D 只共享根，目录根上限始终为 S1=`1789378930614838001`；S2=`1789378947675040988`、S3=`1789378947675041013` 更大。子快照 schema version 已变化，仍正确使用祖先 pin。
- 删除 B/C 后只有 D 存活，三层 snapshot 各有一个引用。重启后 D 首读得到原始 10/20，没有读到 A 在 S1 后写的 123。
- 最后 D 删除在私有 DELETE 和三层引用/pin 级联释放写入后、事务提交前暂停并强杀。重启后 D 保持 DELETING，根和三层引用恢复、私有 tablet 恢复 NORMAL；重试删除后全链释放，再次重启确认回收。

最终崩溃实例：[事件](/tmp/namespace_fork_PROTOTYPE_lineage_v8_crash_uks8a_x9/experiment.jsonl)、[数据归档](/tmp/namespace_fork_PROTOTYPE_lineage_v8_crash_uks8a_x9/data.tar.gz)。主验收日志中较早的 crash 版本也通过，但上述最终版本额外覆盖了先删 A 再捕获的顺序。

同一二进制的旧模式回归：

- [V7 / 模式 4](/data/1/tmp/namespace-v8-regression-v7.log)两个用例 PASS：读者、写事务和后台 DAG 排空；冷兄弟分支存活；最后分支删除回收；提交前崩溃回滚及重试。旧模式 snapshot 表没有新增列。
- [V4 / 模式 2](/data/1/tmp/namespace-v8-regression-v4-retained.log) PASS：根锁竞争下热表读写、并发物化、超时回滚、物化提交前崩溃。重启前及重启后第一次热表访问各完成 20 次读取、20 次回滚写入，新增目录事务均为 0。
- [V5 / 模式 3](/data/1/tmp/namespace-v8-regression-v5.log) PASS：跨多个数据库及空数据库的 namespace 捕获，相同 local IDs 下独立物理 tablet 和锁，重启保持正确。
- Python 语法检查、`git diff --check` 通过。没有重复运行独立 V1/V2/V3/V6 套件；本轮实验实例均已停止并归档。

[V4 首次回归](/data/1/tmp/namespace-v8-regression-v4.log)在重启后的日志计数相等断言失败，SQL 本身完成。该实例日志记录 `log compressor cycles once`、`deleted_file_count=1`，历史慢路径记录已被删除：磁盘余量低时，即使日志数量未到 16 也会回收。短期 fastpath 实验现在显式设置 `max_syslog_file_count=0` 保留计数证据，并在失败时打印前后值；同一二进制重跑通过。这个设置仅用于会自动停止的实验实例，没有修改系统配置或放宽断言。失败不计入通过证据。

本机空间有限，已停止且已归档的 V8/V7 实例的引擎日志另以 gzip 压缩保留；上述 JSON 事件、验收日志和数据归档路径不变。

## 边界与下一步

- 表数无关的根捕获路径已经验证；没有做大规模延迟基准。提交后的实验用快照管理器刷新会扫描已获取 pins，不能据此宣称完整 fork 请求在任意分支规模下都是 O(1) 延迟。
- 祖先查找、后台绑定查找及最终级联释放的成本随链深度/存活引用增加，尚未做深链压测或链压缩。DROP 仍遍历私有目录并排空活动读写，不是快速删除。
- 使用 V7 全局活动计数，删除可能等待无关 namespace 的操作。未增加 namespace worker/IPC，也未完成完整复合对象键、用户/权限/config、通用 DDL；表结构仍限固定两列整数。
- 模式 5 是独立实验元数据格式，不支持与旧模式切换或升级/降级。
- empty-shell 和映射回收不等于全部共享宏块释放，更不等于预分配数据文件缩小。
- B+ tree 页和 schema holder 仍未回收。下一步建议先验证不可达元数据页回收：重复 fork、少量首次访问、删除后，元数据页不再只增不减。必须覆盖根/页读者持有期；现有存储活动计数不覆盖全部 schema/root 读取，不能直接在清空 namespace 根后删页。
