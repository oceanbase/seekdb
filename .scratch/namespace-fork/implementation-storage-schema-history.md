# 物理布局历史改造进度

目标以 [design-storage-schema-boundaries.md](design-storage-schema-boundaries.md) 为准。
本文件记录实现和验证证据，不缩减已确认范围。2026-10-10 开始实施。

## 必须完成

- [ ] 真实物理创建提交版本 C：所有创建、复制、持久化和恢复路径；统一合并资格判断。
- [x] 专用布局元数据 tablet：复用 InstanceMetaStore、原生事务和 MVCC；只做 mini/minor。
- [ ] 稳定布局身份 G：上层分配和绑定，同表分区共用，存储不解析 SQL 表或 Namespace。
- [ ] DDL 与完整布局同事务发布；bootstrap、普通创建、fork 首次 DDL/物化均接入。
- [ ] 合并按 G@F 读取并固定完整布局；普通 medium/meta major 适配；保留现有本地副本。
- [ ] freeze 准备等待既有后台物化/接管；最终锁后新快照复核；超时结束请求。
- [ ] 物理进度统一使用 C/incarnation/F，并在提交/回放水位达到 F 后重新枚举。
- [ ] Namespace 上层逻辑 checksum：各表自身历史定义、完整输入、缺项不通过。
- [ ] MVCC 与 SQL 历史保留、重启顺序、备库本机接管及提升主库。
- [ ] 删除 freeze.schema_version 和根/子两套旧路径，不保留隐式回退。
- [ ] 编译、针对性动态验证、8000 分区成本验证；已执行及失败用例加入四件套。
- [ ] 提交并推送当前分支；不提 PR。

## 当前证据

- 创建/删除 MDS 数据独立保存 physical_create_version_；物化仍保留原来的逻辑可见性。基础编译、物化回滚与崩溃恢复验证已通过（见后文），消费者未接线。
- 已确认 InstanceMetaStore 可按 tablet ID 创建独立存储实例，支持借用 SQL 原生事务；布局存储复用该基础。
- 当前 KV 单值上限 64 KiB，完整布局最大尺寸需要处理，不能默认为任意表定义都能放入一行。
- 新增 `StorageSchemaHistory`：按 `(G, chunk)` 存储完整布局，首块保存大小和来源版本，小布局一行；大布局按固定位置分块，发布/缩小和读取均在同一原生事务快照中完成。拒绝简化 schema 和同 G 的倒退/重复版本，首次创建不会覆盖现有布局。
- 专用 LS 内部 tablet 已接入创建、删除、mini/minor 和活跃读者 MVCC 保留。持久 freeze 保留尚未接入，不能据此宣称长周期合并历史已受保护。
- `ObCreateTabletSchema` 与 `ObTabletMeta` 已增加稳定 G，并覆盖复制及持久化；上层分配/绑定正在本轮接入（见下节），合并仍走旧读取路径。这是实施中的接口基础，不是最终可交付状态。

## 后续接线线索

- G 的分配可复用已有 `ObCommonIDUtils::gen_unique_id` / 原生全局 ID 分配能力，无需另造持久计数器或每次 DDL 锁住全局发号行；G 只作身份，不作版本。
- 普通创建的共同入口是 `ObTabletCreator::execute/execute_impl`，bootstrap 的 core 表也走 `ObTableCreator`。必须在 CREATE MDS 注册前选定 G，不能等当前 DDL end 钩子才给物理元数据补值。
- `NamespaceSchemaPublication::stage()` 在同原生事务内读取最终定义；首次物化在 `ensure_tablet()` 已用同事务锁定 Namespace 根并读取冻结创建描述。接入布局归属时必须核对这两条路径的锁顺序，避免新增绑定锁与 Namespace 根锁反转。
- G 整体记录的删除必须等待旧物理 incarnation / 任务 / fork 描述引用满足要求；不能只保护活跃读者，也不能永久遗留无主 G。

## 验证记录

### 持久布局历史保留（进行中）

- 已接入 schema tablet 的 mini/minor 保留输入：SQL snapshot_gc_scn 栅栏、未结束 freeze、本机待完成广播，以及既有活跃读者/弱读水位。生成 F 前注册短期原生读者，freeze SQL 提交后结束；复用既有 snapshot_gc 行锁和先读 GC 栅栏再读 freeze 列表的顺序，不新增持久 pin 表。
- 启动未恢复完整视图时只允许沿用 tablet 已有 MVCC 边界，不推进；新增 `StorageSchemaHistory::read_at` 在注册读者后核对持久保护和本机可读水位。合并消费者尚未切换，不能据此声称合并/校验已经完成。
- 第一轮生产构建通过（`/tmp/seekdb-layout-retention-build-1.log`）。专项测试版构建通过（`/tmp/seekdb-layout-retention-native-build-1.log`）。
- 首个 `run_layout_retention_probe.py` 通过：没有 F 之前的活跃读者，SQL GC 栅栏及原生弱读水位均大于 F，旧 V=10 仍可跨强制转储/minor读取，最新为 V=11；kill/restart 后仍可读，测试输入标记轮次完成后推进 MVS 并拒绝旧 F。实例 `namespace_fork_PROTOTYPE_layout_retention_2wd3x_2c`，日志 `/tmp/seekdb-layout-retention-test-1.log`。该测试控制已完成进度输入，不验证 checksum 完成判断，也不是实际主备测试。
- 补充暂停恢复用例后复现失败：本机持久广播 F 尚未结束，移除 freeze 行后，`get_schema_history_retention` 返回了大于 F 的边界。内存调度器因暂停尚未启动广播，不能作为恢复依据。实例 `namespace_fork_PROTOTYPE_layout_retention_6mscmser`，`seekdb.log` 中 `LAYOUT_RETENTION_FAIL line=200 ... retained == frozen`；确认失败后终止该测试实例，驱动记录 exit -9，日志 `/tmp/seekdb-layout-retention-test-2.log`。失败场景已在四件套用例中保留。
- 已改为既有 freeze reload 同时读取本机持久合并进度，计算一个 SCN 边界并与视图一起安装；新增的是可重建的 8 字节派生边界，无持久 pin、名单或独立恢复任务。修正后的专项构建通过（`/tmp/seekdb-layout-retention-native-build-3.log`），完整暂停恢复用例通过：删除 freeze 行、本机暂停未装载广播、kill/restart、mini/minor 中保留旧 F、完成后释放。实例 `namespace_fork_PROTOTYPE_layout_retention_ppewqiq4`，日志 `/tmp/seekdb-layout-retention-test-3.log`。完整主备验证仍待后续接线。
- 移除注入后的生产构建通过（`/tmp/seekdb-layout-retention-production-final-build.log`），已有 `primary_major_namespace_probe.py` 通过真实 `ALTER SYSTEM MAJOR FREEZE` 和进度完成/物理 ID 断言，F=1791572487823549012，实例 `namespace_fork_PROTOTYPE_major_progress_r_3agyp0`，日志 `/tmp/seekdb-layout-retention-production-major-test.log`。合并布局消费者此时仍是旧路径，此结果只覆盖本次保留接入对真实发布入口的影响。
- 保留交接的取样顺序已修正：回收器先读持久边界，再检查活跃读者；读者先注册，再检查持久边界。反过来会在“读者进入、轮次完成”交错时同时漏掉两种保护。专项用例在真实 `get_multi_version_start` 两次取样之间确定性插入读者与完成进度变更，断言输出不越过 F、读者仍读到 V=10，再释放并确认 MVS 可推进。测试构建 `/tmp/seekdb-layout-retention-handoff-build.log` 通过，用例 `/tmp/seekdb-layout-retention-test-4.log` 通过，实例 `namespace_fork_PROTOTYPE_layout_retention_7t9vdj26`。包括暂停且 freeze 行消失后的重启保护，全部已纳入四件套。
- 本轮全部测试注入已从生产代码移除，最终生产构建 `/tmp/seekdb-layout-retention-production-final-build-2.log` 通过；没有运行完整 mysqltest/sysbench。完整目标仍未完成：合并布局消费、资格/进度/checksum、新 freeze 准备、布局对象 GC、非全局任务保护及实际主备并发仍需接线验证。
- SQL 逻辑历史静态核对：`ObTableSqlService::delete_from_all_table_history/delete_from_all_column_history` 实际插入 `is_deleted=1` 的新版记录；`ObDropIncPartHelper::drop_partition_info` 同样写分区/子分区历史 tombstone。当前在 `src` 的实际 DELETE/exec_delete/splice_delete_sql 调用中未找到这些 schema 历史表的物理行回收入口，因此本轮保持历史行，不新造回收线程；此证据不替代后续历史定义读取的动态验证。

### 上层发布接线（进行中）

- 新增上层 `TableStorageLayouts`，在 schema tablet 的 `TABLE_STORAGE_LAYOUTS` 集合中保存不可变 `(namespace_id, table_id) -> G`。fork 不复制该集合；首次 DDL/物化用所属目录中的完整定义初始化独立 G，后续分区只复用身份。物理布局正文仍由不感知 Namespace/SQL 的 `StorageSchemaHistory` 管理。
- 普通/bootstrap CREATE 在注册创建 MDS 前选择 G；物化显式传入实际 Namespace 身份。DDL 的既有最终目录发布入口同步发布完整布局，即使只是表定义版本变化也发布。初始目录建立也借用一个 SQL 原生事务，布局与目录同时结束。
- 已有绑定读取不加锁。首次绑定使用原生非等待 INSERT，竞争失败要求整个所属事务回滚；不在持有 Namespace 根锁时等待另一个尚需根锁的创建者。布局更新沿用已有 DDL 发布协调；没有提前获取 Namespace 根锁或扩大全 Namespace 的串行范围。
- SQL 事务增加通用资源持有入口，KV 借用者保持到原生事务结束且连接释放之后；短生命周期的 tablet creator 返回不会提前撤掉快照/GC保护。
- G 整体 GC、冻结持久历史保护与合并消费仍未接线，不能把本段视作整体完成。当前绑定/布局的删除尚须与物理 incarnation 和任务引用一起落实。
- 专项用例 `run_table_storage_layout_probe.py` 已加入四件套并通过：原生首次绑定竞争、父子 G 独立、旧 seed 不覆盖新布局、回滚与参与者生命周期；真实分区/LOB/索引 DDL、晚物化和 kill 恢复后逐项核对 SQL V、布局 V、物理 G。
- 接线首轮编译失败：`ObArray<shared_ptr<void>>` 的虚拟打印要求元素提供 `to_string`。已改为不依赖该容器的事务资源链表，并重新构建全部包含公共事务头的目标。
- 专项探针编译失败：误写 `meta.table_id_`；该持久字段实际为 `create_table_id_`，已修正。
- 修正后测试版本编译通过（`/tmp/seekdb-layout-publication-build-3.log`）。`run_table_storage_layout_probe.py` 通过：358 个初始 SQL tablet 均有非零 G；重启逐项核对 7 个表对象、56 个物理 tablet 的绑定及布局 V；原生冲突立即返回、旧 seed 不覆盖新版、父子独立、回滚和参与者生命周期均通过。实例 `namespace_fork_PROTOTYPE_table_storage_layout__hwllbbt`，日志 `/tmp/seekdb-table-layout-test-1.log`。
- 两项 DDL 同事务崩溃检查通过：初始 Namespace 在提交前崩溃，目录与布局同时恢复旧版本；子 Namespace 在提交后崩溃，两者同时恢复新版本。命令为 `ddl_catalog_atomic_probe.py --layout-history` 分别配 `--owner initial --fault abort_crash`、`--owner child --fault commit_crash`；日志 `/tmp/seekdb-layout-abort-crash-test-1.log`、`/tmp/seekdb-layout-commit-crash-test-1.log`。两项已收入四件套。
- 所有本轮测试注入已移除，补充普通物理 tablet 创建必须携带有效 G 的入口校验（固定 schema 的 LS 内部 tablet 例外）后，最终生产构建通过（`/tmp/seekdb-layout-publication-production-final-build.log`）；专门 schema/instance tablet 不依赖自己的 G。
- 生产版 `ddl_catalog_schema_probe.py` 通过：分区与 LOB，索引创建/删除，表/分区 TRUNCATE，分区增删、库删除，4 路并发 DDL，旧 fork 视图保持。日志 `/tmp/seekdb-layout-production-ddl-test-1.log`，实例 `namespace_fork_PROTOTYPE_ddl_catalog_schema_hzkw5ofk`。
- 生产版 `large_partition_fork_probe.py`（8000 分区）通过：建表 12.438 秒，三次 fork 分别 2.493/4.034/3.128 毫秒，物理 tablet 增量均为0；孩子单分区写入与父子读回正确。日志 `/tmp/seekdb-layout-production-8000-test-1.log`，实例 `namespace_fork_PROTOTYPE_large_partition_fork_uljy562p`。这是本次单轮测量，未测尚未实现的 freeze 最终复核成本；8000 分区共 G 的逐物理对象检查仍待扩展原生审计覆盖。

- 首轮编译：新增文件引用不存在的 `lib/allocator/ob_arena_allocator.h`，已改为已有的 `ob_allocator.h`；等待编译尾部完成后重新构建。
- 新增 `run_storage_schema_history_probe.py` 并加入四件套 bootstrap-native-kv，覆盖分块布局、缩小后历史读取、回滚、转储及重启；尚未执行。
- 原生物化 identity 探针补充真实提交 C 检查；原有创建描述探针补充 G 的复制/序列化检查；尚未执行。
- 旧 tx-data 转 MDS helper 没有有效创建提交版本的保证，仓内无生产调用；没有用其日志 SCN 推导 C。新 C 只由实际创建提交回调写入。
- 第二轮编译触发 ObTablet 内存大小静态断言：G 和缓存 MDS 中的 C 共增加 16 字节，ObTablet + ObRowkeyReadInfo 从 1376 到 1392 字节。已同步断言；8000 个常驻 tablet 仅这两个字段增加约 125 KiB，不计新元数据 tablet 自身开销。
- 第三轮测试版本编译通过。首个动态原生探针通过（140127 字节布局、缩小、回滚、旧快照、C codec），但 Python 驱动在错误的 process.out 中找标记而失败；实际标记在 seekdb.log。已修正并补充明确 minor 完成及 SQL 同事务提交/回滚检查，待重跑。失败实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_storage_schema_history_tlqh37eq`。
- 修正后 `run_storage_schema_history_probe.py` 完整通过：140127 字节布局跨 3 个 KV 行；缩小后旧快照跨 mini/minor 读取；大布局更新回滚；实际 SQL 元数据与布局同原生事务提交/回滚；kill 后恢复已提交大小布局；C 与继承 S、提交日志 SCN 分离的 codec 检查。实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_storage_schema_history_i53hw3f1`；驱动日志 `/tmp/seekdb-layout-history-native-test-2.log`。
- 移除布局测试注入后，生产版 `source ~/.bashrc && make -j80` 通过（`/tmp/seekdb-schema-history-production-build.log`）。上述成功不覆盖尚未实现的持久 freeze 历史保留或合并切换。
- 生产版 `quick_startup_probe.py --fork --write` 通过，父表数据、fork 继承读、孩子首次写及读回均正确。实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kv_startup_30s_15z3z5bv`；日志 `/tmp/seekdb-schema-history-smoke.log`。
- 物化故障探针首轮在恢复检查失败：它错误地要求 `get_latest` 的附加 node trans_version 总是有效；源码 `ob_i_tablet_mds_interface.ipp` 的持久缓存/SSTable 分支明确 reset 该输出。持久化 C 与崩溃前 on_commit 日志一致（例：tx=480，S=1791569247291981025，C=1791569249645304025）。修正探针，仅在 node version 有效时比较，并由 Python 始终用崩溃前真实提交日志逐项核对恢复 C。失败实例 `namespace_fork_PROTOTYPE_shared_transaction_bb5k5mvj` 已保留；该命令已纳入四件套。
- 第二次物化故障探针失败于独立证据收集：普通 INFO 日志不是每个事务都有，例 tx=497 缺少提交行。新增仅测试版本启用、同步输出的 `CREATION_PHYSICAL_COMMIT` 回调探针，直接记录 native commit 参数与 C，避免依赖普通日志采样。失败实例 `namespace_fork_PROTOTYPE_shared_transaction_grqzird2` 已保留。
- 第三次物化故障用例通过：`shared_transaction_probe.py --faults --creation-identities --case rollback_create --case crash_committed`。最后一次恢复逐项核对 370 个 tablet 的 C 等于崩溃前原生 commit_version，367 个走已落盘 MDS 分支；回滚和提交后 crash 恢复均通过。实例 `namespace_fork_PROTOTYPE_shared_transaction_zr4xor0k`，日志 `/tmp/seekdb-physical-birth-test-3.log`。
- 所有临时生产源码注入已移除，最终生产构建通过：`/tmp/seekdb-schema-history-production-final-build-3.log`。本轮用例与失败已收入四件套；未跑完整 mysqltest/sysbench。精简结果备份见 [验证记录](schema-history-validation.json)。
