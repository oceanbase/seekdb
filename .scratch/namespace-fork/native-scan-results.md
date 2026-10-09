# Namespace 扫描入口重构与 8000 分区复测（2026-10-09）

## 改了什么

普通扫描、聚合、全文索引扫描统一经过 `NamespaceScanIterator`，然后直接调用既有 `ObITabletScan`。SQL 的 `ObDASScanCtDef::table_param_` 已在计划生成时准备列描述；扫描继续使用它及原有表达式、过滤器、批量输出和参数，不再自行准备一套表定义。

新链路：

```text
SQL 执行计划已有的 ObTableParam / 表达式 / scan param
  → NamespaceScanIterator
      当前 tablet 物理路由、fork SCN、访问保护、原生事务指针接入
  → 原生 ObAccessService / ObTableScanIterator
  → 既有 MemTable、SSTable 多版本读取与父子数据合并
```

- 删除 `ScanSchema`、`copy_scan_schema`、`EngineScan`、`ReadScans`、`ScanBatch` 及扫描句柄的 open/fetch/rescan/close 桥接；生产代码 9 个文件，增加 148 行、删除 708 行，净删 560 行。
- 不再每个 tablet 深拷贝两份完整 schema、改写全表分区 tablet ID、枚举全表 tablet、计算旧 RPC 消息大小或重新执行 `ObTableParam::convert`。
- 删除行结果经 `ObObj` 复制后再转换成 SQL datum 的中转，原生迭代器直接输出；过滤、投影、聚合沿用原生实现。
- 删除 DAS 层聚合/全文扫描的单独入口，以及按虚拟表编号列举的分派规则。虚拟表使用会话所属 Namespace 的虚拟表服务，普通表使用同一个原生扫描入口。
- 表定义保留逻辑 ID，实际物理 tablet 和 schema 所属 tablet 地址放在请求参数中。没有增加全局缓存、跨请求 schema 副本或 Namespace 1 回退。
- 创建、DDL、DML 的专用准备不属于本次扫描修改；并未宣称全工程所有 `make_storage_schema` 调用均已删除。

## 事务、快照和生命周期

1. SQL 计划和 DAS 请求拥有列描述、表达式和 scan param，原有接口要求 scan param 比扫描迭代器存活更久。新包装持有引用，不建立另一套 schema 生命周期。
2. 每次打开或切换 tablet，通过已有 `TabletAccess` 解析实际来源、fork 快照上限，并持有目录读视图和访问保护。物理地址与列描述各司其职。
3. 切换分区前恢复请求原始的快照上限，避免上一个继承分区的 fork SCN 影响下一个已经物化分区的当前写入。原生迭代器切换完成后再释放旧访问保护；失败时先关闭原生迭代器。
4. SQL 侧事务描述符是 shadow，存储打开/重扫时临时接入所属会话的原生事务描述符，调用后恢复原指针。不另开事务，不提交事务，也不改变读己之写或备库只读行为。
5. 父子 MemTable/SSTable 的多版本合并是现有存储能力，本次未修改合并算法，也未让读取触发孩子物化。

## 8000 分区：与原基准相同的五轮服务计时

与 [原基准](fork-service-latency-8000.md) 使用同一个脚本、配置和执行顺序：一张 8000 HASH 分区表，8000 行两列整数，无二级索引/LOB；每轮 fork 新孩子，首次登录、点查、单行 UPDATE 提交后再执行全分区 COUNT/SUM。结果均为 8000 行、SUM=80001，父表值保持不变。

| 阶段 | 修复前中位数 | 修复后中位数 | 修复后最小—最大 |
| --- | ---: | ---: | ---: |
| FORK 返回 | 3.8 ms | 4.3 ms | 2.6—22.4 ms |
| 首次连接/登录 | 217.3 ms | 218.1 ms | 178.1—288.7 ms |
| 首次主键点查 | 78.6 ms | 72.9 ms | 56.6—79.6 ms |
| 首次 UPDATE 并提交 | 21.4 ms | 13.9 ms | 10.6—25.6 ms |
| 发起 fork → 首次点查成功 | 309.6 ms | 302.0 ms | 238.7—369.9 ms |
| 发起 fork → 首次写入提交 | 325.3 ms | 315.9 ms | 249.3—386.1 ms |
| 全分区 COUNT/SUM | 44.48 s | 2.663 s | 1.861—3.446 s |

全扫描中位数约快 **16.7 倍**。首次服务时间没有同等比例变化：这部分还包含 Namespace 懒装载、schema 装载等准备，不能用全扫描的收益概括。

- 正式二进制，无测试 hook；SHA-256：`b2dde8679339bc0e59359d033435fd69dee73b90ea3c81b91fa50aebe192afdf`。
- 计时时基线 HEAD 为 `90184bcc4`，同时包含本次未提交的生产改动；JSON 中的 `source_head` 单独不代表被测二进制。完整生产 diff 的 SHA-256 另存 `native-scan-verification.json`。
- `cpu_count=4`，`memory_budget=8G`，后台任务开启，合并调度间隔 5 分钟。性能测量期间没有并行运行本轮其他测试；宿主机仍共享，5 轮样本不代表 P95/P99 或产品 SLA。
- 数据和全部分区物理 tablet 已准备完毕才开始 fork 计时；没有重启 OS 或清空缓存。
- 原始结果：`native-scan-latency-results.json`；日志：`native-scan-latency-8000.log`。

## 父子对照及控制变量

首轮修复后完整扫描：父 2.157 s、子 1.802 s，结果均为 `(8000,80000)`；另一个独立实例为父 1.603 s、子 2.418 s。

固定扫描 256 个分区、256 行，修复后第二次实例的五轮中位数：

| 表的总分区数 | 父查询 | 子查询 |
| --- | ---: | ---: |
| 256 | 55.85 ms | 59.67 ms |
| 1024 | 39.41 ms | 50.87 ms |
| 8000 | 161.23 ms | 241.36 ms |

放大倍数父 2.89、子 4.04，显式 `--max-amplification 8` 通过。**首轮三次测试曾出现父 8.005、子 5.34，父超出 8 倍阈值而退出 1**，日志保留为 `native-scan-scaling-1.log`；不能只保留后续通过值。该比值的分母只有约 28 ms，波动敏感。

最后从空实例重跑集成入口，显式同时指定 `--scaling --max-scan-seconds 8 --max-amplification 8`，退出 0：完整扫描父/子 3.215/3.398 s，固定扫描分区的放大父/子 7.647/5.932。日志 `native-scan-scaling-final.log`；两条性能断言均使用原定阈值。这也说明剩余准备成本和宿主机波动不能忽略。

进一步在第二个实例上重复 40 轮并进行 20 秒、99 Hz perf 采样：父/子放大中位数约 4.92/4.01。性能采样对应这一批，不能混入无采样计时。采样约 48% 在 `ObPlanMatchHelper::calc_table_locations` 所在的 SQL 计划匹配链路；其中 `ObDASTabletMapper::get_all_tablet_and_object_id` 累计 47.72%、自身 23.94%，其调用的 `get_tablet_and_object_id` 累计 23.73%、自身 23.38%。这些比例有嵌套，不能相加。

源码两次 `append_array_no_dup` 对完整分区列表做线性判重；该工具函数逐个插入、每次扫描已有数组，会形成 O(P²) 的准备。相关核心实现与 ns fork 前 `dbe8fcdb` 相同。扫描适配层复制已删除，但 SQL 原有计划匹配仍存在独立优化点，已记入 TODO；没有为此修改 SQL 计划缓存或增加常驻状态。

诊断中尝试读取 `oceanbase.gv$ob_sql_audit` 返回表不存在（1146），随后采用本机 perf；没有用缺失的视图数据作判断。原始 perf 文件保留于 `/data/1/nijia.nj/test/namespace_fork_native_scan_perf.data`，不提交二进制采样。

## 正确性验收

| 用例 | 结果及覆盖 |
| --- | --- |
| Release 编译 | 通过；使用现有 build_release、`source ~/.bashrc` 后编译 |
| `native_scan_probe.py` | 通过；继承/已物化分区切换、事务内更新/删除/插入、串行/PX、DECIMAL/CHAR/LOB、索引回表、强制 NLJ rescan、SQL PREPARE、系统目录和 ADD COLUMN |
| `sql_read_view_probe.py --procedures` | 通过；RR/RC、晚物化、读己之写/回滚、父 TRUNCATE 后读取和首次写、外键、嵌套过程 |
| `tablet_access_reads_probe.py` | 通过；冷继承 COUNT/SUM、全文索引、LOB，读取不物化，父子更新隔离 |
| `namespace_worker_bootstrap_prototype.py` | 通过；初始/孩子/新 Namespace、系统视图、SHOW、模板重启 |
| `namespace_worker_direct_prototype.py --case full` | 通过；DDL、分区、本地/全局索引、LOB、FTS、IVF/PQ/SQ8、空 HNSW、父源删除、重定义、缓存生命周期、重启与中断 DDL 恢复 |
| `run_standby_suite.py --case namespace_fork_local` | 通过；备库继承读、拒写、动态 Namespace 发现、物化事务与 LOB 回放、DDL 刷新、父 TRUNCATE/DROP、切主后写入及 fork |

新增用例首轮错误地将预期 SUM 写成 560；数据库返回 460，与逐行 `(0,0),(1,111),(3,30),(4,40),(5,50),(6,60),(7,70),(9,99)` 一致。修正测试预期为 460 后完整通过。保留 `native-scan-semantics-1.log` 和第二轮成功日志，不将这次断言错误记成产品缺陷。

新增语义用例已加入四件套 direct；原先失败的控制变量场景也纳入 `fullscan_diagnosis_probe.py --scaling`。默认校验数据并记录计时和比值，不设置共享宿主机上的硬时间门槛；可显式传 `--max-amplification 8` 检查放大倍数，或 `--max-scan-seconds 8` 检查完整扫描。原始修复前诊断断言失败为父 18.53 倍、子 20.42 倍；本次未把 8 调大来取得通过。

没有运行完整 mysqltest、sysbench 或全套四件套。所有本次启动的测试实例由脚本停止并归档；无用户实例被修改。

## 复现

```bash
python3 .scratch/namespace-fork/native_scan_probe.py --binary build_release/src/observer/seekdb
python3 .scratch/namespace-fork/fork_service_latency_probe.py \
  --binary build_release/src/observer/seekdb \
  --result .scratch/namespace-fork/native-scan-latency-results.json
python3 .scratch/namespace-fork/fullscan_diagnosis_probe.py \
  --binary build_release/src/observer/seekdb --scaling --max-scan-seconds 8
```
