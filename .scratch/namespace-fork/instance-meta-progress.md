# 实例 KV 实施交接（2026-09-29）

`a0c399100` 已推送事务目录接口：root/counter/GC 水位原子初始化，fork 的子记录/名称/pin/lineage 同事务提交，读 live、列 live、原子重命名。原生四阶段探针检查已提交 fork 跨重启恢复、名称索引；生产构建通过。`f50f32096` 已推送 `mark_deleting`/`list_deleting_owned`/`finish_drop` 独立 KV 事务入口，四阶段强制退出恢复通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_fnbmq34s`，注入移除、生产构建通过。这些还没有替换线上旧 SQL 目录；SQL+KV 同事务按用户决定留未来。

`c3062c749` 已推送 DDL KV 事务边界：版本读取、begin/finish change、begin/finish recovery、delta 发布。`publish_schema_delta` 仅在目录事务提交成功后返回物理待删除 tablet 列表。探针在创建阶段留下已提交 active DDL 标记并 SIGKILL；下一阶段确认标记恢复、delta 提交和恢复完成，再进入删除；完整四阶段通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_efae262x`。测试注入移除，生产构建通过；线上 DDL 仍未切换。

`71e9d7519` 已推送实体化补写版本围栏：调用者必须带已观察目录版本和当前原生 schema 版本，行锁下拒绝过期目录、仍有活动 DDL 或 native schema 落后 pending 的补写；Facade 的 `reconcile_owned` 在独立 KV 事务提交。四阶段探针以合成物理探针提交 owned，SIGKILL 后读回，再通过 delta 转 tombstone，证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_rlo6lxvm`。注入移除，生产构建通过。此测试没有真实物理 tablet，不能代替实体化业务门禁。

Goal `实现实例元数据事务型 KV` 仍 active。不能标记完整目标完成：当前只完成原生存储基础，未接入 Namespace 元数据业务。

2026-09-29 DDL 发布修复已推送 `8c33d9e43`：旧 live SQL 路径用 DDL 事务开始时 runtime refreshed version=1000 做 old guard，DROP TABLE 的旧表不存在，导致 owned 物理 tablet 不回收。改为从已提交目录读取基线；物理 drop 显式使用 PhysicalTabletMdsScope。旧二进制红证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_ddl_physical_drop_2npt5_a3`，新二进制定向绿证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_ddl_physical_drop_zpkx9f2v`，完整 direct 门禁 `.scratch/namespace-fork/gate-results/ddl-physical-drop/direct.log` 通过。后续又加行锁内基线校验与过期重算，typed KV 的 `stage_schema_delta` 同样拒绝过期基线，已推送 `ac66356f4`。原生四阶段 SIGKILL 恢复探针 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_4bhb7cdl`、EXCHANGE 定向 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_exchange_mapping_zi_g7mkg` 均通过；注入已移除、生产构建通过。仍未完成线上业务到 KV 的切换。

`aa6a890af` 已推送 KV exception loader：typed KV 记录可通过既有 `IExceptionLoader` 接口喂给 NamespaceControlState 缓存。生产构建通过；尚未替换线上 SQL loader，因此没有引入双源读取。下一步需要将 root/fork/drop/DDL/父链/实体化/GC/模板登记作为一套来源切换，不能只切一条读路径。

`bb97ea914` 已推送实例 KV 目录页 GC：`InstanceMetaStore::begin_directory_gc` 在一个事务生命周期内排斥普通 KV 事务，保证扫描快照覆盖之前已提交的目录根；`InstanceNamespaceMetadata::collect_unreachable_pages` 扫描 Namespace 与仍引用的 snapshot 根，遍历 B+tree 与关联对象页，一次最多删除 256 个孤儿页，失败由 caller 回滚。测试留在本地原生探针，检查双向互斥超时、Namespace/snapshot 可达页保留与孤儿删除。新建与三次 SIGKILL 恢复通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_3wlur8if`；测试注入移除后生产构建和 bootstrap 门禁 `.scratch/namespace-fork/gate-results/kv-directory-gc/bootstrap.log` 通过。GC 仍未替换旧 SQL 线上调用；真实业务切换未完成。

`d4ff82d32` 已推送 KV 原子重命名：锁定 Namespace 记录，核对预期旧名称，再在同一事务更新成员和唯一名称索引，为模板 Namespace 从构建名发布为正式名做准备。原生探针覆盖预期名称不匹配及新旧名称索引；四阶段新建/SIGKILL 恢复通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_0fin71fa`。测试注入移除，生产构建通过。线上模板登记仍走旧 SQL，尚未切换。

`02551c26c` 已推送 KV 父链读解析：typed 仓库读取本地 exception 与 Namespace parent/fork_cap，物理存在性由外部回调探测编码后的地址；本地 tombstone 阻断读取，owned 已提交而物理暂不可见时返回本地地址供 caller 等待，其余情况沿 parent 链取最近副本并累计最小快照上限，parent=0 终止，不对 ns=1 做特殊回退。三层探针覆盖祖父/父/本地副本、快照上限、墓碑和 owned 暂不可见；四阶段原生及 SIGKILL 恢复通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_1uifsi1p`。注入已移除、生产构建通过。旧 SQL resolver 仍是线上入口，待业务来源整体切换。

2026-09-29 物理孤儿回收补洞，已提交推送 `4a2167bf7`：旧 SQL GC 只枚举 kind=0 owned exception；如果原生物理 tablet 已提交但 owned 记录丢失，DROP NAMESPACE 后会留下 NORMAL tablet。旧二进制红证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_orphan_physical_gc_v0djvvr3`。增加按 DELETED Namespace 编码地址枚举 LS 物理 ID，保留 tombstone 直到物理 ID 消失。调试时发现 `ObTabletDrop` 虽写删除历史，但 Namespace MDS 路由默认跳过 DELETE_TABLET_NEW_MDS，日志表面 ret=0 而物理仍 NORMAL；用现有 `PhysicalTabletMdsScope` 显式绑定已编码物理 ID 的这次 GC 删除。中间失败分别为重复历史主键 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_orphan_physical_gc_0ed7hzl1` 和错误将删除历史当成物理完成 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_orphan_physical_gc_8__nxs4u`。最终最小绿证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_orphan_physical_gc_6olxzs7m`：缺失 owned 记录的物理 tablet 从 NORMAL 变 DELETED，再变空壳。本地 `orphan_physical_gc_probe.py` 已放入四件套 direct gate，完整 direct 通过 `.scratch/namespace-fork/gate-results/orphan-physical-gc/direct.log`。跨 SQL+实例 KV 联合事务按用户决定留未来，本期不实现。

2026-09-29 typed KV 墓碑裁剪接口已提交推送 `c97e8389c`：`prune_deleted_namespace` 在同一 KV 事务锁住 DELETED 记录，检查仍引用它的子 Namespace、owned exception、外部物理 tablet 存在性；三项都清空才删除 tombstone 与残余 exception。更新本地原生探针覆盖三类保留和最终裁剪。四阶段原生探针（新建、三次 SIGKILL 恢复）通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_d5doenhb`，临时注入已移除，生产构建通过。该接口尚未接入线上 DROP/GC，仍不能宣称实例 KV 替代旧 SQL 目录。

2026-09-29 后续进展：typed 业务仓库已提交 `11a1574ea` 并推送。当前工作又增加 `InstanceSnapshotLineageStore`、Namespace snapshot pin 和 KV GC 水位锁定协议（待提交）。本地原生探针在新建与强制退出恢复两轮通过，含完整 lineage fork/release、pin 校验与水位拒绝旧快照；证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_rdxo4737`。线上旧 SQL 路径尚未切换。

后续 `f33d99dda` 已推送：lineage/pin 业务接口。`edb286aef` 已推送：把 KV pin 加入 FreezeInfoMgr 快照列表，控制目录 bootstrap 初始化 KV GC 水位，背景水位写者先提交 KV 再单独更新 SQL。pin 刷新/删除探针新建和重启均通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_g0o_eqdf`；bootstrap 和 SQL 定向回归分别通过 `gate-results/instance-gc-coordination-rerun/bootstrap.log`、`gate-results/instance-gc-coordination-final/sql.log`。初始化缺失键时锁后插入的 -5024 已修，失败证据在 design 文档。线上业务尚未切到 KV；不要把此阶段称为完成。

`119c89de6` 已推送：增加 `InstanceNamespaceMetadata::fork_namespace`，在调用者持有的同一个 KV 事务里锁住源记录、取得快照、分配 ID，并写目标名称/成员、pin 和 lineage。调用者负责失败回滚、成功提交及提交后的 FreezeInfoMgr/Runtime 发布。新建和强制退出恢复探针均通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_psx04a65`；探针操作本身回滚，不能据此宣称线上 fork 路径已切换或 fork 记录已跨重启恢复。此前探针因生产 bootstrap 已初始化水位导致固定初值重复插入，已改成以实际水位选择测试快照，失败证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_vwygaw2b` 和 `namespace_fork_PROTOTYPE_instance_meta_native_66k520b7`。测试仅留 `.scratch`。

后续 typed KV 仓库加入 DDL dirty/pending 标记及恢复清理四个事务操作，使用同一 KV namespace 记录行锁和现有 active/pending 字段。原生探针覆盖并发 DDL 计数、脏状态拒绝 fork、待同步版本、重扫标记和过期版本拒绝；新建及 SIGKILL 重启通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_pbt4ljxs`。首次尝试 bootstrap 在探针执行前以 -4002 退出，证据 `namespace_fork_PROTOTYPE_instance_meta_native_b6b221qg`。本阶段只完成 typed 仓库，线上 DDL 仍走旧 SQL 标记，不能据此宣称切换完成。

`a2d677418` 已推送上述恢复标记，`bd8ff4420` 已推送后续 `stage_schema_delta`：把 owned/tombstone exception 与目录 schema 版本放在同一 KV 事务，物理存在性由上层回调显式提供；只有提交后才能删除返回的 owned tablet。探针覆盖新建物理副本、只继承的 tablet、EXCHANGE 式原 tablet 改表、删除 tombstone 和待删除 tablet 列表。生产构建通过；原生探针新建和 SIGKILL 恢复均通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_bvj30w_d`。探针仍是回滚事务，未验证业务提交后持久化，也未切换线上 DDL。

生命周期实码核对：`ObDDLSQLTransaction::end` 是先 native schema commit，再 `finish_namespace_schema_change` 写 pending 版本，最后 `publish_namespace_schema_change` 做 delta。故 `stage_schema_delta` 成功时还要把 `pending_schema_version<=published_version` 清零；恢复哨兵 `INT64_MAX` 必须保留到 `finish_schema_recovery`。已补逻辑及本地探针，完整原生新建与 SIGKILL 恢复通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_3tbzltgi`。仍未接线上。

Typed KV 新增 `insert_root_namespace`，在调用者的一个事务里新增 1 号 Namespace 与 ID counter；调用者还需在同一事务内初始化 GC 水位，之后才能向可服务状态发布。定向探针先独立提交 root/counter，SIGKILL 后第二次启动读取已提交 root，再分配子 ID，证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native__pxzwq8u` 的 `INSTANCE_RECORD_PROBE_PASS root_recovered=0` / `root_recovered=1`。生产构建通过。线上 bootstrap 仍旧 SQL，不能把探针视为业务切换。

`2c90615c7` 已推送上述首次建目录接口。Typed KV 又加入删除两阶段：`mark_namespace_deleting` 在 caller 已关闭新访问后写 DELETING；物理清理完成后 `finish_namespace_drop` 在同一 KV 事务中移除名称、写 DELETED、释放 lineage 与最后 pin。探针覆盖重复标记、删除后名称不可查、第三层 pin 删除及祖先引用递减；新建和 SIGKILL 两轮通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native__81owtz3`。测试里的删除记录位于回滚事务，尚未替代线上 DROP，也未验证真实物理 tablet 清理。

`7c4a6c476` 已推送删除事务接口。本地原生探针现将 root/counter 和一个完整 KV fork 真正提交；后续三次 SIGKILL 重启依次验证已提交 fork、持久化 DELETING、完成删除后的 DELETED/名称消失。四阶段均通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_j7epqndw`，标记 `INSTANCE_RECORD_DURABLE phase=created/marked/finished/verified`。这覆盖 KV 业务提交与恢复，但 fixture 无真实物理子 tablet，不能替代线上 DROP/物理清理门禁。

针对“物理 tablet 创建已提交，KV owned exception 未提交”的跨事务窗口，typed KV 新增 `reconcile_owned_tablets`：输入必须来自所属 Namespace 的当前 schema tablet→table 关系；物理探测使用同一 Namespace 编码地址。仅在本地物理副本存在时补写/修正 owned，物理缺失时保留继承语义，不推进 schema 版本、不清 pending DDL。原生探针在回滚事务中覆盖补写、改 table ID、缺失副本及 pending 保留，完整四阶段恢复再跑通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_k4ve88ma`。尚未接入线上 schema 恢复或实体化路径；DROP 后无 schema 的物理孤儿仍需要按 Namespace 物理地址扫描回收。

**撤回记录（2026-09-29）**：曾假定 Namespace 1 的 `__all_tablet_to_table` 包含所有子空间物理 tablet，按此将 owned 读者与 GC 改读该映射，并删除 kind=0 exception 写入。最小 EXCHANGE PARTITION 用例红证据 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_exchange_mapping_1l11htbo`：该 delta 涉及子空间本地 tablet `200007`、`200009`，Namespace 1 映射均未命中（临时诊断 `[DEBUG-exchange-map]`，见 `namespace_fork_PROTOTYPE_exchange_mapping_fuqh84xb`）。原因是子空间原生 DDL 走其自身映射，只有部分实体化 tablet 走目录代理；Namespace 1 映射不是全部物理 ownership 的权威。`57f1d62f6` 至 `f955a09c0` 六个基于此前提的提交已经用 `2387dda60` 至 `60d532853` 六个 revert 提交撤回并推送；生产树等价 `119c89de6`。临时诊断已从生产源码移除，测试文件留本地。后续不能再用 Namespace 1 映射替代全部 kind=0 记录。需要以本地 Schema+物理 tablet 的可恢复重建或其他明确来源，设计 SQL 物理事务与实例 KV 的分段提交协议。最小交换用例 `exchange_mapping_probe.py` 已串入本地四件套的 direct gate；撤回后的最小用例和完整 direct 均通过 `gate-results/revert-mapping-assumption-rerun/direct.log`。首次串接时 bootstrap 以 -4002 退出、未进入用例，保留 `gate-results/revert-mapping-assumption/direct.log`。

对 EXCHANGE 用例增加了新建表后及交换后的 exception table_id 与子空间 `__all_tablet_to_table` 对照，旧实现两阶段均通过 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_exchange_mapping_9qq5trsz`。曾误读 `collect_directory_tablets` 的 `emplace` 第二个参数；重新核对工作树和 HEAD，实际是 `table_id`，没有对应缺陷，也未做这方面生产改动。

## 约束

只推送分支 `codex/namespace-worker-proxy-v20`，不提 PR；只提交生产代码，文档/测试留 `.scratch`。不跑完整 mysqltest/sysbench。只支持 KV 自己创建的事务，本期不支持或预留 SQL+KV 混合事务。不能引入新 Namespace Runtime/SchemaService/线程组。不考虑升级兼容。

## 原生基础

- 新文件 `src/storage/instance_meta/instance_meta_store.{h,cpp}`；AccessService 拥有实例，构造注入物理 AccessService 与原生事务服务。
- 固定三列 `(collection_id:uint64,key:VARBINARY,value:VARBINARY)`，联合主键前两列。K512，V65536。表定义不注册 SQL catalog。
- 新 LS 内部 tablet49404，LS 初始化/回收列表由3变4；普通行 MemTable，mini/minor，不做 major。用 `has_internal_memtable()` 区分旧三个专用内存表。
- get/scan 固定事务快照且看到自身最新写入；get_for_update 原生行锁后采新快照。insert/put/erase、跨集合 begin/commit/rollback，RAII 清理。scan callback 不可重入同一事务。
- 原生事务不在 SQL session 活跃快照采集中，所以 store 维护在途快照链表；实例 tablet 合并保留这些快照。
- 已有物理 table/DML plan 不再无条件取 Namespace SchemaService；需要检查版本时仍按原逻辑解析。
- AccessService 内原有 Namespace hooks 尚未上移；不要声称分层改造已完成。

## 调试与验证

确定根因：固定 schema 原误用 `set_binary()`（ObCharType），scan range 是 `set_varbinary()`（ObVarcharType），`ObMemtableKey::encode` 返回-4016。统一 VARBINARY 修复。失败 `/tmp/seekdb-instance-meta-native-probe6.log`，首次全绿 `/tmp/seekdb-instance-meta-native-probe7.log`。

进一步 `/tmp/seekdb-instance-meta-native-probe9.log`：新建和 SIGKILL 恢复均通过，跨集合提交/回滚、固定快照、锁后最新值、重复insert、60000字节含NUL值、512字节K、65536字节V、空值、二进制键/范围、超限、只读拒写、erase回滚。四次mini后SSTable数降为2，旧快照仍可读；两个原生事务争用一行返回-6005。

生产源码已清除所有 `LOCAL_INSTANCE_META_PROBE` 与 `DEBUG-meta-read`。生产构建 `/tmp/seekdb-instance-meta-build14-production.log` 成功。测试二进制保存在 `/data/1/nijia.nj/test/instance_meta_native_artifacts_md7bm_ye/seekdb`（对应相同生产代码，额外本地探针）。

本地工具：

- `native_probe_injection.py enable/disable`：构造/移除临时编译注入，依赖 ensure_control_schema 入口；入口移动时需更新脚本。注入状态不能提交。
- `instance_meta_native_probe.ipp` 与 `run_instance_meta_native_probe.py`：原生测试与一次crash恢复。测试集合10001..10003。
- `run_four_gates.py --binary ... --native-probe-binary ...`：原生回归作为bootstrap阶段，然后原四件套bootstrap/sql/direct/tls。可用 `--gate` 重跑失败/待跑步骤。

四件套首轮原生、bootstrap、sql已通过，direct在cache_cycle_3关闭客户端后立即DROP遇到4179活动连接。同脚本前面的source_drop已有有界等待，这处漏了。已提取 `drop_after_client_close` 给两处用，仅重试4179且含active connections，最多3秒。修改只在本地测试脚本。首轮日志 `.scratch/namespace-fork/gate-results/instance-meta-foundation`；direct/tls重跑日志 `instance-meta-foundation-rerun`。direct 和 tls 重跑均通过，四件套全部通过。生产代码已提交 `eab127ce6` 并通过 gh 凭据推送到 `origin/codex/namespace-worker-proxy-v20`；远端跟踪引用与 HEAD 一致。工作区仅 `.scratch/` 未跟踪。

## 后续工作

1. 原生基础已评审、编译、跑四件套并推送 `eab127ce6`（仅17个生产代码文件，不包含测试/文档）。目标仍active，下一步从第2项继续。
2. Namespace admission、父链解析、写入实体化上移到上层适配：EngineScan/EngineWrite 是主路径，但要审计向量、LOB、FTS/block stat等直接调用 AccessService 的辅助路径，不能只改两个主入口。保护必须覆盖迭代器/上下文生命周期。ObStoreCtxGuard 的 prototype_access_ 随之清理。
3. KV业务记录/helper，替换 `namespace_fork_kernel_prototype.cpp` 中 SQL adapter：pages、roots/namespaces/name mapping、snapshots/lineage、exceptions、counters、template registry。
4. pin/登记水位/GC读写必须闭合，保持fork+pin+lineage同事务。注意 ObSnapshotTableProxy 还被 DDL SQL事务调用，本期不允许凭此引入SQL+KV混合事务接口；先核实所有调用与协议，不能拆开关键原子性或留两份权威。
5. 移除旧 __fork_proto_meta 创建、名称过滤和SQL回退；验证SQL目录不可见、多层fork不继承、提交/删除中断恢复、pin与GC竞争。

## 业务切换前的事务边界核对

- `control_namespace` 当前把名字/ID、pin、snapshot lineage 放在同一个 SQL 事务中。KV 切换时仍须在同一个 **KV** 事务里提交这四种记录；提交后刷新 FreezeInfoMgr，再发布登录名。`acquire_storage_snapshot` 只取得快照时钟，不承载该元数据事务。
- 以上曾记录的“用 Namespace 1 mapping 全面替代 kind=0”的假设、实施与测试结论均已撤回。此前 SQL/direct/source_drop 通过，未覆盖子空间原生 DDL 的真实映射来源；单看绿门禁不能证明这个全域不变量。后续要把 exchange 等失败过的用例保持在本地四件套。
- `publish_schema_delta` 先于物理删除提交目录元数据，已经是分段协议；KV 改造仍需保存 tombstone 在先、物理 drop 在后、失败可恢复。
- `finish_namespace_drop` 现在嵌在物理 drop 的 SQL 事务中；KV 切换时先持久化 DELETING，排干新访问，再删物理 tablet，最后 KV 标记 DELETED 并 release lineage/pin。进程崩溃后的 DELETING 重试必须幂等。
- 本期不提供 SQL+KV 联合事务接口。未来若确有 SQL 表和实例 KV 原子更新需求再设计；不能以此为当前切换捷径。

仍需补验证：未提交KV写入中途crash、业务提交结果不确定的恢复、64KiB值持久化边界（当前64KiB测试在回滚事务，60000字节已验证刷盘/合并/重启）。

## 2026-09-29 实例 KV 业务切换

用户确认 SQL 表与实例 KV 共用同一事务留待未来；本次只保证实例目录内部事务及与物理 tablet 分段恢复。

当前生产差异已将 bootstrap/fork/template/DDL delta/读路径/实体化/DROP/GC 切到 InstanceMetaStore。删除了所有生产 __fork_proto_meta SQL 读写和该控制库的 SchemaService/权限/扫描过滤。物理 tablet DDL 与共享快照时钟仍显式使用 Namespace 1 proxy。实体化先提交物理事务，再提交 KV owned；若中间崩溃，已有物理 tablet 的后续带 schema 访问补齐 owned。DROP 先 KV DELETING、排空访问，再 KV DELETED；物理 tablet 由共享后台 GC 删除，物理事务提交后才清 KV owned。被活子空间继承的物理 tablet 会被保留。

调试红样本：
- quick_startup_probe.py --fork --write 首次报 SQL 表 __fork_proto_meta.namespaces 不存在，定位实体化仍走旧 SQL。切 KV 后通过。
- 子空间 CREATE DATABASE 首次超时，日志 /data/1/nijia.nj/test/namespace_fork_PROTOTYPE_ddl_physical_drop_kzr7wwsx/log/seekdb.log 显示实体化在 roots 阶段反复 OB_EAGAIN(-4023)；错误地阻止 active_schema_changes。去掉此限制后 DDL 物理删表专项通过。
- 原生四阶段探针首次第四阶段断言失败：GC 已裁剪完成删除的墓碑；本地探针更新后四阶段通过。

验证：
- make -C build_release -j80 生产构建通过，临时注入已清除。
- fork 继承读、子空间更新、DROP 后父空间读、私有物理 tablet 变 empty shell：/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kv_startup_30s_c71h6tn2 PASS。
- 子空间 CREATE DATABASE/TABLE/INSERT/DROP TABLE 后物理 tablet 变 empty shell：/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_ddl_physical_drop_09_f0dbt PASS。
- 原生 KV 跨重启四阶段：/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_oxle8745 PASS。
- 本地四件套 bootstrap/sql/direct/tls 分别在 gate-results/kv-cutover-{bootstrap,sql,direct,tls} PASS；direct 包含上述失败复现和 exchange 行为。完整 mysqltest/sysbench 未运行。

测试与文档只在 .scratch/，不提交。当前生产差异尚待最终提交和推送。
