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

- 已修改创建/删除 MDS 数据，独立保存 physical_create_version_；物化仍保留原来的逻辑可见性。尚未编译验证，消费者未接线。
- 已确认 InstanceMetaStore 可按 tablet ID 创建独立存储实例，支持借用 SQL 原生事务；布局存储复用该基础。
- 当前 KV 单值上限 64 KiB，完整布局最大尺寸需要处理，不能默认为任意表定义都能放入一行。
- 新增 `StorageSchemaHistory`：按 `(G, chunk)` 存储完整布局，首块保存大小和来源版本，小布局一行；大布局按固定位置分块，发布/缩小和读取均在同一原生事务快照中完成。拒绝简化 schema 和同 G 的倒退/重复版本，首次创建不会覆盖现有布局。
- 专用 LS 内部 tablet 已接入创建、删除、mini/minor 和活跃读者 MVCC 保留。持久 freeze 保留尚未接入，不能据此宣称长周期合并历史已受保护。
- `ObCreateTabletSchema` 与 `ObTabletMeta` 已增加稳定 G，并覆盖复制及持久化；上层还未分配/绑定 G，正常创建目前仍是 0，合并仍走旧读取路径。这是实施中的接口基础，不是最终可交付状态。

## 后续接线线索

- G 的分配可复用已有 `ObCommonIDUtils::gen_unique_id` / 原生全局 ID 分配能力，无需另造持久计数器或每次 DDL 锁住全局发号行；G 只作身份，不作版本。
- 普通创建的共同入口是 `ObTabletCreator::execute/execute_impl`，bootstrap 的 core 表也走 `ObTableCreator`。必须在 CREATE MDS 注册前选定 G，不能等当前 DDL end 钩子才给物理元数据补值。
- `NamespaceSchemaPublication::stage()` 在同原生事务内读取最终定义；首次物化在 `ensure_tablet()` 已用同事务锁定 Namespace 根并读取冻结创建描述。接入布局归属时必须核对这两条路径的锁顺序，避免新增绑定锁与 Namespace 根锁反转。
- G 整体记录的删除必须等待旧物理 incarnation / 任务 / fork 描述引用满足要求；不能只保护活跃读者，也不能永久遗留无主 G。

## 验证记录

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
