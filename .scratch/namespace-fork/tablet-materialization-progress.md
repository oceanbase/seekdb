# tablet 物化重构：实施记录（本地，不提交）

目标：实现评估中的方案 A，统一 Namespace tablet 访问准备，保留读继承、实际修改时物化。基线 a9bf8740a（事务共用已完成）。只提交并推送生产代码，不提 PR，不提交测试/文档，不跑完整 mysqltest/sysbench。

## 完成条件（全部完成）

- [x] 移除 Native/Fork 两套写物化策略。
- [x] 主/LOB 绑定从同一个固定版本 guard 收集，只保存一份物理 schema，移除写侧逻辑/物理重复拷贝和两处 DDL 的重复转换。
- [x] 读写、元数据修改、估算、范围及逻辑后台统一准备身份、来源和快照，保护持续到实际访问结束。
- [x] 将 DDL 物化移至实际元数据操作的准备阶段，移除建索引 helper 和截断分区的语法分支；保持锁顺序。
- [x] LOB/全文索引等直接调用方完成迁移后，删除 AccessService 的 Namespace 路由、物化和保护钩子；清掉存储参数中的 Namespace 访问策略传递。
- [x] 来源解析/GC 探测无写副作用；物理 replay/转储/合并不重新进入 Namespace 访问入口。
- [x] 冷读、主辅写、DDL、估算/范围、并发创建/删除/回收、故障及重启的聚焦回归和本地四件套通过，失败用例纳入门禁。
- [x] 最终生产差分审查、构建和验收证据对应同一源码；只提交并推送代码并核对远端 SHA。

## 早期阶段快照（截至 build-5；最新状态见续作及最终验收）

新增 rootserver/fork_table/namespace_tablet_access.{h,cpp}：TabletBinding 共用绑定准备；TabletAccess 持有访问保护，准备读来源/快照或本地可修改副本。EngineScan 的保护跨越 rescan，EngineWrite 在释放原生执行和写上下文后释放保护。估算和范围服务已迁入，尚待本轮构建/验证。

当前仍是分步迁移：AccessService 的 Namespace 路由、物化和保护钩子已从工作区移除，LOB/全文索引改由上层准备并持有保护；随后补迁了 DAS 聚合/全文索引直接扫描，并清理物理参数中的 Namespace 访问策略传递。这些最新改动尚未构建，不能认定迁移完成。两处 DDL 已复用绑定实现，但提前物化触发点仍存在；其他逻辑调用方及元数据操作入口尚待收敛。不能以阶段抽函数认定全目标完成。

## 构建与动态证据

- `/tmp/seekdb-tablet-access-build-1.log`：第一组生产构建通过。
- `/tmp/seekdb-tablet-access-smoke-1.log`：fresh bootstrap 失败，核心 tablet 已提交但工厂因无 owned 而查询尚未创建的 Namespace 记录，stage=roots，-4018。实例日志：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_shared_transaction_78goe16b/log/seekdb.log`。没有恢复 Native/Fork 分支；改为已提交物理对象直接可用，保留逻辑墓碑检查，登记由创建它的 bootstrap/DDL 发布负责。
- `/tmp/seekdb-tablet-access-build-2.log`：修正后生产构建通过。
- `/tmp/seekdb-tablet-access-smoke-2.log`：bootstrap、LOB 物化/更新、父子隔离、自增及并发 UPDATE 通过；并发 INSERT 在读取内部表 1024 时命中另一个请求未提交 CREATE，返回 -4023。实例日志：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_shared_transaction_b_edjze2/log/seekdb.log`。原有测试中已经记录继承扫描会请求重试，此次仍把它保留为失败，不能用重跑抹去。
- 历史探测已增加未提交 CREATE 的判断：物化的逻辑出生版本可以已设为 fork SCN，但不能用它证明 CREATE 已提交；未提交创建应视为未进入已提交视图，读取继续沿继承链。已提交 owned 仍选择本地对象，由原生读取等待 MDS 提交发布。后续证据显示仍未解决所有并发窗口，见下。
- `/tmp/seekdb-tablet-access-build-3.log` 构建通过，`/tmp/seekdb-tablet-access-smoke-3.log` 聚焦物化、并发及重启通过。这一次通过不是并发缺陷关闭的充分证据。
- `/tmp/seekdb-tablet-access-build-4.log` 构建失败：移除 AccessService 解析输出参数后，rescan/scan_block_stat 调用参数未同步；修正后的 `/tmp/seekdb-tablet-access-build-5.log` 构建通过。
- `/tmp/seekdb-tablet-access-smoke-4.log` 在并发 INSERT 时再次出现 `4023 / Try again`，此前成功的创建、LOB、序列检查不能覆盖这个失败。日志指向内部表 1024 扫描与创建状态；来源探测/MDS 提交发布的具体竞争尚未确认。实例目录为 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_shared_transaction_wu7qc80b`。保留失败，继续定位，不以重跑消除记录。
- DAS 直接扫描准备和物理参数清理发生在 build-5 后，当前二进制不对应最新工作区。尚未执行最新代码的四件套验收。

启动与并发失败均来自四件套已有入口 shared_transaction_probe.py；后续增加 CREATE 暂停时另一读者仍能读取继承数据的确定性断言。

## 续作：调用方收敛及并发修复

- DAS 聚合/全文索引直接扫描、FTS DML、LOB、IVF 探测、表级 fork helper、重定义序列同步和 CREATE MDS 来源准备已迁入公共 TabletAccess。外部 resolve_read_tablet 仅剩公共模块一处；物理存储参数不再传递 Namespace 访问模式。
- 删除建索引 materialize_index_source_tablets 和 truncate-partition 提前物化分支。已有主 tablet 的隐藏/LOB 绑定、SYNC_TRUNCATE_INFO、DDL 本地构建从实际操作入口准备；schema 来自明确 Namespace 的 guard。
- 工厂仅保留带完整绑定 schema 的 ensure_tablet，删除可空 schema 包装和 ensure_tablet_impl；此最后清理正在 build-11 验证。
- build-6 并发最小复现首轮失败：`/tmp/seekdb-tablet-access-concurrency-red.log`。build-7 临时诊断确认 CREATE 的 create_commit_scn_ 为 INVALID（UINT64_MAX），原先 is_max() 判断不成立。改为根据 MDS 事务状态和 CREATE 类型识别未提交对象，继续读取继承源。诊断插桩已移除。
- build-8 `/tmp/seekdb-tablet-access-concurrency-fixed.log` 在第三轮再次失败：owned 已随事务可见，原生 CREATE MDS 回调仍未完成。ObLSTabletService::get_tablet_with_timeout 在原有超时内重试 EAGAIN；只读零超时探测不等待。历史探测若二次观察已提交，则校验最新状态，避免返回旧 EAGAIN。底层未增加 Namespace 判断。
- build-9 `/tmp/seekdb-tablet-access-concurrency-fixed-2.log`：8 轮双连接同时首次 INSERT 无客户端重试全部通过。`/tmp/seekdb-tablet-access-smoke-5.log`：原有主/LOB 物化、隔离、自增、并发与重启通过。
- `/tmp/seekdb-tablet-access-reads-1.log`：冷聚合、分区、FTS/LOB 读取及继承全文索引 UPDATE/DELETE 通过；只读没有创建被检查的主/索引 tablet。
- build-10 后 `/tmp/seekdb-tablet-access-truncate-1.log`：继承全局索引截断、强制索引查询、父子隔离与重启通过。`/tmp/seekdb-tablet-access-redefinition-1.log`：本地/继承全文索引堆表离线 INT→VARCHAR 重定义、后续写入及重启通过。
- 上述并发最小复现、冷读、截断及重定义用例已纳入本地 run_four_gates.py。故障组的 CREATE 暂停时继承读断言尚待最新插桩二进制验证；完整四件套尚未完成。所有失败日志保留。

## 最终验收（进行中）

- build-11 生产编译成功；移除本地故障/原生探针后再次生产编译成功：`/tmp/seekdb-tablet-access-production-restore-build.log`。恢复编译与正在接受门禁的生产二进制 SHA-256 完全一致：`78465b649570ef9948102412e228873efce81e280728a8a55f10b11252dc9b4b`。
- 本地探针构建成功：`/tmp/seekdb-tablet-access-probe-build.log`。探针和生产来自相同业务源码。探针只添加可禁用的本地测试块，已从全部生产文件移除。
- `gate-results/tablet-access-final/` 中 bootstrap、sql、tls 已通过；direct 和 bootstrap-native-kv 仍在执行。
- 新增 `tablet_access_index_probe.py`，纳入四件套 direct。正在运行的 direct 已加载旧命令列表，因此此例单独补跑、共同组成验收证据。`/tmp/seekdb-tablet-access-index-gate-2.log` 通过：冷继承普通索引、堆表全文索引、2048 维 IVF 向量索引构建，父 fork 后更新隔离和重启恢复。首轮 `/tmp/seekdb-tablet-access-index-gate.log` 的 `lists` 参数为测试拼写错误，按项目现有语法改成 `nlist` 后通过；生产代码未因此改动。
- 表级 FORK 自有及继承来源、自增序列、重启已在最终 direct 中通过。新增 CREATE 暂停时另一连接读取原数据和 LOB 的确定性断言，也已在非强制 redo 故障组通过。
- 生命周期审查：EngineScan/DAS/FTS 先释放原生迭代器，再解除访问保护；EngineWrite 先释放 execution/context。元数据调用保护覆盖实际注册、绑定操作；DDL 本地构建在上层准备已提交的本地 tablet 后交给既有物理 DAG，DAG 的读取继续使用原生 tablet/read-table handle 和既有 snapshot 保护。未向物理 DAG、replay、转储或合并传入 Namespace 策略。

## 最终 direct 发现并修正的批次缺陷

- 最终 direct 在 FTS 表 DROP COLUMN + ADD COLUMN 返回 -4187。原日志保留 `gate-results/tablet-access-final/direct.log`；实例 `namespace_fork_PROTOTYPE_inprocess_direct_vutsdjr0`。不算验收通过。
- 独立 `fts_column_redefinition_probe.py` 首轮即可复现：`/tmp/seekdb-tablet-access-fts-columns-red.log`。已加入四件套 direct。
- 日志显示 CREATE 批次第一组创建隐藏主 tablet，第二组为它创建 LOB；路由仅在第二组内部查新对象，把同批第一组的新主 tablet 误当作已存在对象，进而查询尚未提交的映射。修正为路由前一次收集整个批次的新逻辑 tablet ID，只有批次外的既有绑定对象才进入修改准备。用哈希集合避免大批次平方扫描，不添加 DDL 语法或 Namespace 特例。
- build-12 正在编译该修正；此前二进制的 direct 失败不得由其他通过用例替代。后续需重跑最小复现、原 direct 末段及受此批次修改影响的门禁。
- 对照 `a9bf8740a` 对应旧探针二进制（未启用探针环境变量），相同最小用例 5 轮通过：`/tmp/seekdb-tablet-access-fts-columns-baseline.log`。确认是此次新准备逻辑引入，而非遗留问题。
- build-12 修正后最小用例 5 轮通过：`/tmp/seekdb-tablet-access-fts-columns-fixed.log`。最终 v2 生产 SHA-256 为 `0eb0c67977b86a244f260392a39c78567a50a0c47d2e99aa502f06f55fcab555`。完整生产门禁已从头重跑至 `gate-results/tablet-access-final-v2/`；未删掉失败场景或改为客户端重试。
- v2 原生 KV 四次启动恢复已通过；随后事务故障组 fresh bootstrap 命中已记录的 MDS 超时溢出，尚未进入业务测试。实例 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_shared_transaction_v_q_d169/log/seekdb.log:5810`，原生 `read_raw_data` 将近 INT64_MAX 的相对时间加当前时钟，扫描绝对超时溢出为负。该文件与 HEAD 相同；同一问题已有独立 TODO，不属于此次批次修正。保留 v2 原始失败日志，从 shared_transaction_probe.py 起续跑至 `gate-results/tablet-access-final-v2-resume/`，不将续跑成功视为旧溢出缺陷已修复。


## 交付结论

方案 A 完成并提交推送 `a73ae2b0c066963a43bfdd6fb76ab7c89f0a8a5d` 至 `origin/codex/namespace-worker-proxy-v20`；gh API 核对远端 SHA 与本地相同。未创建 PR，提交仅包含 45 个生产文件，文档/测试/插桩不提交。

最终 v2 的 bootstrap、sql、tls、direct 全部通过。bootstrap-native-kv 原生单测通过，后续 fresh bootstrap 的既有溢出失败保留；事务故障与 redo 故障两组续跑均通过。原始失败和成功日志哈希见 `tablet-access-acceptance.json`。本轮新引入的 CREATE 跨分组判断缺陷已经修复并通过原始综合门禁；既有超时溢出仍作为独立 TODO。

所有预定迁移与删除均完成：来源解析及物化的外部调用各集中一处，Native/Fork 策略及两处提前物化代码删除，物理 AccessService 不再携带 Namespace 路由、物化或保护钩子，读仍不持久化。生产差分新增 662 行、删除 630 行，净增 32 行；不能宣称净减少代码。简化来自实现收敛及反向依赖移除，额外代码覆盖之前缺失的访问保护、直接调用路径及已复现的并发窗口。

生产源码最终审查完成；没有剩余未处理的 Blocker/Major。生产恢复构建与门禁生产二进制 SHA 相同，已跟踪工作区干净，仅 `.scratch/` 保持本地。
