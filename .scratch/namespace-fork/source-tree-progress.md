# Namespace 共享来源与安全回收：实施进度

目标依据：assessment-tablet-materialization-refactor.md 第 10、11 节。完整目标仍在进行中；本文件不缩小完成范围。

## 2026-10-08 第一批：批量 COW 与同事务发布基础

生产提交：78bcaea32（仅四个生产代码文件；已通过 gh 凭据推送到 codex/namespace-worker-proxy-v20，远端跟踪 SHA 与本地一致）。

生产改动：

- NamespaceCatalogTree::apply 接收有序的整批增删，在内存中构造最终受影响路径，再持久化；单条 put/remove 复用同一实现。这里是单次编辑的临时状态，不是暂缓的 DML 常驻缓存。
- 容量从固定 FANOUT=8 改为 16 KiB 编码页预算，key 最长 512 字节；拒绝过大条目和非法 cap。单个目录条目保存描述引用，不能把大 schema 直接塞进叶子。
- 分裂、删除后根收缩、多代 fork 的 cap 传播均由统一批量算法处理。失败不发布调用者的新根；页写失败由所属事务回滚。
- InstanceNamespaceMetadata::stage_catalog_delta 在调用方事务内锁定 Namespace、检查 schema 基线、暂存两棵树及 schema 版本；允许同 schema 版本的物化变更。
- 未切换 DML 路由、未接入 DDL end；当前 exception/父链及全局 pin 尚在。新增接口是后续接入基础，不能宣称已实现整体来源图。

验证（均为本地测试，不提交）：

- catalog_batch_probe.py：直接编译生产 catalog.cpp，C++17、严格警告、ASan/UBSan。8000 个 32 字节映射 value 生成 39 页、2 层；8000 个 4000 字节 value 生成 2007 页、3 层。验证多代 cap、共享路径仅写一次、批量删除/根收缩、200 批随机增删与完整参考模型、没有新写中间垃圾页、非法输入和读写失败。两版验证均通过；最终版用全量树遍历核对模型并检查每个叶子的首尾路由及采样查询，避免反复解码同一页导致的测试冗余。
- run_instance_meta_native_probe.py：新初始化及三次 SIGKILL 恢复通过。扩展 typed-record 探针验证 800 条映射同事务写入、旧根不变、过期 base 拒绝、回滚根/页一起消失；shared-transaction 探针验证 SQL 映射与双根版本共同提交/回滚；durable 探针验证提交后的来源根跨重启可读取。
- quick_startup_probe.py --fork --write：生产 binary 新初始化、fork、子读和子写通过。
- 正式 release 编译通过；测试注入已撤销，恢复正式 binary 的增量编译也已通过。注入只用于生成独立 native 测试 binary。
- 编译过程中修正了本地测试最初误用 C++14（现有 shared_mutex 需要 C++17）以及两个生产聚合初始化漏填 pending 字段的严格警告。最终回归按严格编译运行；无隐藏的生产测试钩子。
- catalog_batch_probe.py 已加入 run_four_gates.py 的 bootstrap-native-kv 步骤，原生探针扩展自动随同原生门禁运行。没有跑完整 mysqltest/sysbench 或全四件套。

证据文件：

- catalog-batch-result.log、catalog-batch-final-result.log
- source-tree-build.log、source-tree-native-build.log
- source-tree-startup-result.log
- source-tree-native-result.log
- native 实例目录：/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_instance_meta_native_5c65a9ew
- source-tree-artifacts/seekdb-production、source-tree-artifacts/seekdb-native

## 已定位的下一步入口

1. ObDDLSQLTransaction::end（src/rootserver/ob_ddl_service.cpp）目前在 SQL end 后调用 finish_namespace_schema_change / publish_namespace_schema_change。这里需要改为 SQL 提交前暂存来源变更，并让借用的 InstanceMetaStore::Transaction 保护保持到 SQL 提交/回滚后 detach。
2. ObSchemaService::get_increment_schema_operations 可在同一 ObISQLClient 上读取本事务 DDL 操作。ObSchemaServiceSQLImpl::get_full_table_schema_from_inner_table 可读取完整表及 aux 关系；删除要用能表达表不存在的底层 get_table_schema，不能把 ERR_NULL_VALUE 当其他错误吞掉。还需覆盖并行 DDL 与 bootstrap 的显式注册。
3. 已有 ObCreateTabletSchema（src/storage/ob_storage_schema.h）及序列化可复用；它含 storage schema、raw table ID、index status、truncate version。当前 ObTabletCreatorArg 只收 ObTableSchema，ObBatchCreateTabletHelper 在内部转成 ObCreateTabletSchema。应补齐直接从持久化物理描述创建的入口，避免后台重新装载 SQL Runtime。
4. 描述还需明确主/LOB/索引绑定和物理对象身份。大列数描述可能超过单条 KV 64 KiB/现有 save_page 60000 字节限制，不能随意限制正常表能力；描述分块及 GC 可达性必须同设计。
5. 物理 create_scn_ 与逻辑 create_commit_version 不同，物化时逻辑出生版本可能沿用 fork SCN；不能把后者直接当成物理代次。原子登记时的身份方案仍需核实原生创建/回放时序。

## 完成审计（本批后）

- 共享清单稳定路由、父 DROP/TRUNCATE/删除语义：未完成。
- fork 固定根开销、统一来源权威、描述按表版本共享：批量树基础完成，其余未完成。
- DDL/物化与来源图原子发布：底层暂存及借用事务验证完成，生产 DDL 接入未完成。
- 按物理来源及 SCN 批量安全 GC、替换全局 pin：未完成。
- 后台只读物化/接管与预算：未完成。
- 本方案相关主备/全量拷贝/升主与恢复：未完成；本次原生重启测试不能替代。
- 模板独立接管与初始化简化：未完成。
- DML 内存来源视图/节点缓存：按用户要求暂缓，不作为本轮前置条件。

证据清单：source-tree-evidence.json，记录源码/测试/二进制 SHA256、验证范围和已核实的分支推送。所有本批运行的编译/测试进程均已结束；测试实例已停止。

## 2026-10-08 第二批：物理描述与分块对象

- 新增 ObTabletCreator::add_create_tablet_batch，接收已有物理 schema 和绑定参数；实际物化创建路径已改用该入口，输入提前 reset 后仍须正确创建，验证其深拷贝所有权。
- 最小原生回归在修复前稳定失败 creation_descriptor_copy ret=-4016。根因是 ObStorageSchema::init(copy) 调用派生类 is_valid 时 ObCreateTabletSchema 的 table_id 尚未赋值；修为与 SQL init/deserialize 一样限定基类校验。修复后原生新启动及三次 SIGKILL 恢复通过。
- 正式实例共享事务回归通过，覆盖主/LOB 原子创建、并发创建、旧快照及重启；mixed-source 的普通表/二级索引/LOB 在父来源 GC 后强制退出恢复及续写通过。证据 physical-description-shared-fixed-result.log、physical-description-detached-fixed-result.log。
- save_object/read_object 将大描述分为 60000 字节块与一个 manifest；GC 沿描述引用保留所有分块，遇到缺失块先失败、不删任何候选页。180001 字节二进制对象、回滚和断引用后回收已在原生回归通过。
- TableCreationDescriptor 正在编译验证：复用 ObCreateTabletSchema、创建 extra 及 raw 主/LOB 关联，描述不包含分区列表；同一描述供各分区引用。生产物化已使用该描述；持久化读回的原生用例验证释放 SQL schema 后仍可构造创建 batch。DDL 来源根接入尚未开始，不宣称后台已脱离 SQL Runtime。
- 查询缓存优化仍暂缓。

### 非阻塞遗留：MDS 无限超时溢出

一次 mixed-source 回归在 bootstrap 失败4002，日志 scan_param.timeout=-9223372036854775808。定位为 ob_i_tablet_mds_interface.cpp::read_raw_data 的 timeout_us + ObClockGenerator::getClock() 在无限剩余超时下溢出上界；该表达式存在于本批之前。原有 mds::RetryParam 已使用饱和相加，但此处未复用。此问题不由描述复制引入；已记录，暂未修改。修复后的正式 mixed-source 回归已通过，仍不意味着该随机缺陷消失。失败证据 physical-description-detached-result.log 及实例 namespace_fork_PROTOTYPE_detached_restart_oywnkr1b 的日志；对照版本 source-tree-artifacts/seekdb-production 本次也通过。

### 工具状态

/home 磁盘一度耗尽，物理描述/source-tree 的本任务二进制副本已移到 /data/1/nijia.nj/test/namespace_fork_build_artifacts；原路径保留符号链接。失败的截断复制产物已删除，重新完整复制后才启动测试。未修改用户测试文件。

第二批最新验证：

- table-descriptor-native-result.log：最终 TableCreationDescriptor 的 KV 保存/读取、SQL schema 提前释放、batch 独立所有权、截断/尾随/错误标记拒绝；8000 分区增加前后编码完全相同（测试表描述167字节）。首次启动及三次强制退出恢复均通过；四次均有 INSTANCE_PERSISTED_CREATION_DESCRIPTOR_PASS 和 INSTANCE_DESCRIPTOR_PARTITIONS_PASS。
- table-descriptor-fault-result.log：最终描述入口下 rollback_create、rollback_owned、crash_create、crash_owned 四个场景均通过，退出前强制 native redo 落盘；中途读取仍看到继承数据，恢复后无半提交映射/owned，重试创建、LOB及自增序列正确。
- table-descriptor-preparation-result.log：正式最终描述版本32分区冷 DDL、首次/后续写、索引、类型修改、事务回滚、行外向量、重启通过。
- table-descriptor-catalog-result.log：生产 catalog.cpp 在 ASan/UBSan 下的批量树与 manifest 回归通过。
- table-descriptor-production-build.log：正式 release 编译通过，生产源码已撤销所有本轮测试注入。
- 四件套的原生阶段继续调用上述扩展探针；新增 gate_probe_injection.py 统一原生KV、shared-transaction、preparation三类注入，避免只开部分注入时误以为构建了完整四件套测试binary。本轮只运行所列定向用例，没有完整运行四件套/完整mysqltest/sysbench。

### 下一步实现的关键约束

- ObTabletMeta::create_scn_ 不是可直接用于提交前目录的创建代次：ObTabletCreateMdsHelper::register_process 在主库注册时传 SCN::invalid_scn，回放时才传 redo SCN。ObTabletCreateDeleteMdsUserData::create_commit_scn_ 在 on_commit 赋值，提交前同样未知。物化的 create_commit_version_ 则故意沿用fork SCN，不可用作代次。
- 候选实现：在原生 CREATE 的生命周期数据中保留创建事务ID（来自 MdsCtx 的 TRANSACTION writer），让目录在借用同一SQL事务时取得相同身份。需统一验证正常DDL、物化、回放、删除状态复制、MDS落盘/重启及空壳路径，不能先用逻辑出生版本冒充；该候选尚未实现。
- DDL接入要同时收掉 end 之后的发布权威，不能只提前更新roots.schema_version，导致现有 publish_schema_delta 将必要的源/异常更新当成已完成跳过。新初始化bootstrap需要在fork模板之前一次性登记所有所属物理表（含系统表）；发布事务仍需正确处理并行DDL的提交顺序。

第二批已提交并推送：ed722ba2a17a33adf37a0ed18afa73178ed1c112。远端跟踪SHA相同：True。仅11个生产文件；未提交文档/测试，未创建PR。最终 mixed-source 脱离父源回归见 table-descriptor-detached-result.log，同样通过。源码、binary、用例和日志校验值见 table-descriptor-evidence.json。上述原生探针、故障注入、正式分区及混合来源回归进程均已结束，实例已停止。完整目标仍未完成，继续按上列顺序接入。

## 2026-10-08 第三批：创建身份及 DDL 原子来源发布

### 生产实现

- 原生 CREATE MDS 保留创建事务 ID。主库注册与备库回放均从 MdsCtx 的 TRANSACTION writer 取值；删除状态复制和序列化保留该身份，不使用物化时沿用 fork SCN 的逻辑出生版本冒充物理代次。ObTablet 的持久状态增加 8 字节；8000 个物理 tablet 对应约 64 KB 增量。
- 来源叶子改为明确的 48 字节记录：raw table ID、物理 tablet ID、创建事务 ID、逻辑主/LOB 绑定三元组；快照 cap 继续属于树路径。定义叶子保持物理描述引用。元数据页 GC 区分两类根，只从定义叶子追踪描述 manifest/分块。
- NamespaceSchemaPublication 接入 ObDDLSQLTransaction::end，在 native SQL end 之前借用同一事务、读取 schema operation 集、展开受影响主/LOB 组、生成描述和来源变化、批量更新双根。SQL commit/rollback 后再 detach。正常 DDL 不再依靠提交后差异发布更新来源权威。
- 新初始化在模板 fork 之前登记已有系统表和物理来源。首次安装失败仍直接失败，不增加半成品补建逻辑。
- 物化在已有 CREATE/MDS/映射/owned 共用事务中一并发布新来源叶子，以本次 native 创建事务 ID 为代次，新叶子 cap=0。
- **仍是阶段性实现**：读取与物化前的来源解析仍使用旧 exception/父链；为保持过渡过程可运行，新 DDL 在同一事务中同时更新旧消费者。旧持久化 active/pending/recovery 登记、全局 pin 和旧 schema 同步入口尚未删除。最终目标要求统一权威，不能将这些过渡逻辑当作交付终态。

### 已发现并修复的问题

1. 第一条 CREATE TABLE 失败 4020：存在性查询 ReadResult.close() 仅关闭 cursor，handler 仍持有内部 SQL 连接。复用该连接前 reset 释放 handler。失败 ddl-catalog-startup-result.log，修复后 ddl-catalog-startup-fixed-result.log 通过。
2. 并发 DDL 失败 5627：前一 DDL 已提交的来源版本领先于内存 SchemaService。发布不能从内存 guard 获取基线，改为在 SQL 所有者事务中按明确 schema 版本读取历史表定义；显式检查历史 is_deleted/当前存在性，避免 get_table_schema 返回已删除对象的最后历史记录。失败 ddl-catalog-schema-result.log，修复后的分区/并发集合见 ddl-catalog-schema-final-result.log。

### 已执行的定向验收

- creation-identity-result.log：创建前登记的事务 ID、回滚不留记录、强制 redo 后 SIGKILL/恢复及删除状态编码；成功及四类回滚/崩溃通过。
- catalog-source-codec-result.log：严格警告、ASan/UBSan，来源编码非法长度/代次拒绝、主/LOB 绑定、共享旧根不变及物化解除叶子 cap；原有 8000 条目、随机批量树、manifest 回归同次通过。
- ddl-catalog-final-{initial,child}-{rollback,abort_crash,commit_crash}-result.log：最终按历史版本读取的实现，初始/孩子两类 Namespace 各三种故障，六个均通过。直接观察 SQL 当前 tablet 和 KV 根/来源；提交后退出前两者已共同可见；恢复后在 owner 首次登录前核对持久根，后代继续读取并更新旧表。故障用例要求 native redo 提交。
- ddl-catalog-partitioned-result.log：32 分区、冷建索引、主/LOB 物化、类型修改、用户事务回滚、行外向量及重启通过（本项运行于更改并发基线读取前）。
- ddl-catalog-schema-final-result.log：最终历史读取版本；8 分区主/LOB、冷 comment/加列、增删索引、TRUNCATE、DROP、旧后代视图、4 个并发建表及 DROP DATABASE。每一步将 SQL 表/分区映射与真实来源树逐条核对，并验证绑定组完整。
- ddl-catalog-materialization-retry-result.log：最终历史读取版本，成功、CREATE 后回滚、来源登记后回滚、两种未提交崩溃均通过；显式检查来源树仍指向父或原子改为孩子，主/LOB 三个身份相同且均有创建代次；强制 redo 后重启验证。初次运行在 bootstrap 命中既有无限超时溢出（ddl-catalog-materialization-result.log，scan timeout 负溢出）；本轮未修复该独立问题。

### 并发验证的明确限制

- 原有 begin_schema_change 用独立 KV 事务写 active 计数，四连接并发会返回 6005。失败证据 ddl-catalog-schema-fixed-result.log；日志定位在 ObDDLSQLTransaction::start 的登记阶段，早于新发布逻辑。同样在修复 5627 前的日志存在。
- schema probe 仅对 6005 且确认表尚未创建的情况进行有界客户端重试并记录 registration_lock_retry；**不接受/重试新发布路径的 5627**。因此“并发通过”不表示已消除旧登记的锁冲突。
- 后续移除双提交恢复状态时，一并收敛 DDL/fork 的发布协调；不要为这套即将退役的登记再加永久旁路。

### 本地四件套

- 增加 ddl_catalog_atomic_probe 六场故障；替换旧 fork_ddl_publish_recovery_probe 对“SQL 已提交、来源未发布”窗口的过时断言。
- 增加 ddl_catalog_schema_probe；quick startup 也执行首次写；shared_transaction_probe 逐条检查新来源图，并增加 --creation-identities 开关。
- gate_probe_injection.py 统一管理 native、shared transaction、preparation、creation identity 和 DDL commit 五类钩子。本批保存的 ddl-catalog-artifacts/seekdb-native 只启用了 identity/shared/DDL 三类，不能拿它宣称全四件套原生构建完整。
- 文档、测试、故障钩子脚本均保持本地；生产源中的本批钩子已撤除。未运行全量 mysqltest/sysbench。

### 后续仍必须完成

1. 统一根/读视图持有，并切换 fork、读取、物化、删除来源解析，最终删掉 exception/父链权威及提交后 schema 补发布状态。
2. 按真实来源图及本机 fork 边生成 GC 计划，补齐并发截点、活跃读者和原生对象代次保护，再取消全局 Namespace pin。
3. 后台按预算物化/接管只读孩子；从持久化物理描述创建，避免加载完整 SQL Runtime。
4. 模板 fork 后独立接管、显式禁止登录、删除安装补救链；本方案的主备/全量拷贝/升主验收。
5. DML 查询缓存仍暂缓。当前阶段不声称总体目标完成。

第三批最终状态：正式 release 构建及撤除钩子后的 ddl-catalog-production-schema-result.log、ddl-catalog-production-shared-result.log 均通过；后者覆盖并发首次创建和重启。16 个生产代码文件提交 faf305a4433b6ca41a9c86be65a88b5b6a7624ea，远端跟踪一致：True。未提交文档/测试，未创建 PR。证据清单 ddl-catalog-evidence.json。完整目标保持进行中。

## 2026-10-08 第四批：从持久化来源与创建描述物化

### 实现

- 物化的加锁路径从当前来源树读取主/LOB 绑定、源物理身份及快照，从定义树读取按表版本共享的 TableCreationDescriptor。持有 Namespace 行锁期间复查来源；主/LOB 创建、映射及来源叶子仍在同一个原生事务提交。
- 对源物理 tablet 核对创建事务 ID 和历史可读状态，拒绝空壳及错误创建代次。不得用同 ID 的新物理对象替代来源。
- 删除 TabletBinding 和 MetadataTabletPreparation，以及调用方的 SQL tablet→table 查询、SchemaService guard 和全表分区枚举。普通 DML 自身所需的 SQL schema guard 继续由请求持有。
- 新增 read_table_definition，核对定义键、table ID 和对象引用后读取持久化描述；不增加来源缓存或后台任务权威。
- 目前只切换物化慢路径；普通读取、存在对象的墓碑检查、GC 仍有旧消费者。不能称为完成统一来源权威或精确回收。

### 本批验证与失败记录

- source-materialization-cold-result.log：孩子首次登录之前直接调用实际物化接口，主/LOB 来源原子改为孩子，所有 Runtime 服务槽仍为空。错误创建代次返回 SNAPSHOT_DISCARDED，不产生目标物理对象；修复测试输入后物化和登录读写成功。
- source-materialization-preparation-result.log：32 分区冷建索引读取持久化描述，后续 owned UPDATE 不再读取描述；普通写、索引、类型 DDL、回滚、行外向量及重启通过。
- source-materialization-fault-result.log：原生 redo 落盘下 CREATE/来源更新后的回滚与崩溃、提交边界观察、主/LOB 绑定、自增序列、并发首次创建和重启均通过。每次直接检查来源树与物理对象共同可见或共同回滚。
- source-materialization-detached-result.log：混合来源的普通表、索引及 LOB 接管，父物理源回收后强制退出恢复，孩子读写通过。
- source-materialization-native-result.log：旧原生测试夹具在 bootstrap 前插入 version=5 的空 Namespace 根，使模板继承空来源树，启动物化返回 TABLET_NOT_EXIST；已中断该任务拥有的进程。仅在本地夹具补齐真实初始化目录，不在生产增加安装补救分支。
- source-materialization-native-fixed-result.log：修正夹具后首次启动及三次 SIGKILL 恢复通过。8000 条持久化来源在释放 SQL schema 后逐条可查；物理描述仍为表级共享。完整原生 KV/事务/目录/描述/持久化阶段通过。
- 第一次 source-materialization-build.log 构建失败：build_ddl_local_batch 残留旧 MetadataTabletPreparation 调用；已经迁入公共持久化描述入口，后续 native 构建通过。

### 本地四件套

- gate_probe_injection.py 增加 cold materialization 钩子；run_four_gates.py 增加首次登录前物化及错误代次拒绝用例。
- 原生 8000 分区用例改为直接验证持久化来源和共享描述，不再测试已删除的分区绑定缓存。
- preparation 用例验证持久化描述调用以及 owned 后续写不重复准备；保留所有原生事务故障用例。
- 本批仅执行上述定向用例，未运行完整 mysqltest/sysbench。测试、文档和钩子只留本地。

### 后续切换读路径的约束

- 不能直接用最新根替换普通读路由。固定快照事务可能在 DDL 之后才首次打开旧表，需要在取得快照时即保护可选择的来源视图。
- 物化发生在事务快照之后时，读取还要看到该事务写入孩子的行；不能机械地始终路由至快照根中的父来源。
- InstanceMetaStore 普通目录事务参与 GC 排空，不得通过让每个用户事务长期持有它实现根保护，否则无关 Namespace 的长事务会阻塞目录 GC。统一视图持有需要独立生命周期。

第四批正式验证：source-materialization-production-build.log 构建通过；撤除全部测试钩子后的 source-materialization-production-schema-result.log 与 source-materialization-production-shared-retry-result.log 通过。后者首次启动发生既有 MDS 无限超时相加溢出，scan timeout=-9223372036854775806，失败证据 source-materialization-production-shared-final-result.log，实例 shared_transaction_uwzisoii；本批未修复此独立缺陷。另一次 source-materialization-production-shared-result.log 为复制二进制尚未结束导致 Text file busy，属于测试启动顺序错误，后续已等待复制结束。生产差分9文件，新增98/删除245，净减147行。

第四批已提交并推送 bf1fe91500673386c7b458fed77090cf21b10a04，远端跟踪一致：True。证据 source-materialization-evidence.json。未提交文档/测试，未创建 PR；本批构建和回归进程均已结束。完整目标继续进行，普通来源路由/根持有、旧状态删除、精确 GC、后台预算接管、模板和主备完整验收仍未完成。

## 2026-10-08 第五批进行中：独立的读根持有

- NamespaceCatalogViews 由现有 NamespaceRegistry 持有，Handle 只保存 Namespace、读 SCN 和两个不可变根，不保存 tablet 映射缓存。复制 Handle 共享保护；最后释放后移除注册；注册数据可以安全跨越 Registry 对象的析构顺序。
- InstanceMetaStore::begin_read 在目录 GC 排他之前登记短期 KV 事务及未决 MVCC 保护，再调用上层快照获取函数，在同一 SCN 读取 Namespace 根。获取根与登记 Handle 在短期事务结束前完成，不让用户事务长期持有 ordinary_transactions 计数。
- Kernel acquire_read_view 同时持有短期物理发布保护。目录页 GC 纳入持有的根；当前物理 GC 候选过滤也纳入这些根及未完成物理 fork 边，保护尚未打开的旧来源。
- 普通 SQL 事务尚未接入，旧来源路由尚未替换。该批只建立并验证根选择/持有/GC 接口；最终还必须把 RC 语句与 RR 事务生命周期、PX/嵌套扫描及物化后的自身写入接通。GC 的历史 SCN 归并仍是后续整体保留计划职责。
- 核实 ObTransService::start_tx（ob_tx_api.cpp）只重置 snapshot_version，首次 get_read_snapshot 才取得读快照，因此 RR 的首个根选择可接在已存在的事务适配入口，不需在 BEGIN 提前人为选择数据快照。
- 原生回归增加“取 S 后提交新根，随后仍读到 S 对应旧根”“删除当前 Namespace 后持有旧根，GC 能进入并保留旧页/物理边”“释放后旧页回收”“快照获取失败不泄漏目录保护”。四件套沿用原生 gate 执行此新用例。
- catalog-view-unit-result.log：ASan/UBSan 下共享句柄寿命、并发注册/收集/释放及 registry 析构顺序通过；原有批量 COW 树回归同次通过。

第五批接入调查（尚未实施）：

- 读快照位于 namespace_worker_gateway_prototype.ipp::call_in_process_tx_read_snapshot；真实 ObTxDesc 由 EngineWrites 管理，C/R/U/N/release/reset 是事务视图释放候选。EngineScan 当前只持有 TabletAccess；RC 的并行/嵌套扫描必须另外持有自己的句柄，不能只在 EngineWrites 留一个可被下一条语句覆盖的根。
- ObSqlTransControl::start_stmt/end_stmt 与 ObDASCtx 管理语句快照；弱读走 get_weak_read_snapshot_version，plain insert 有不预先取得普通快照的优化。不能只拦截 get_read_snapshot 就宣称覆盖全部读入口。
- NamespaceRegistry::mark_ready 当前调用在 standby metadata-ready 流程；不可假设它能统一表示主库目录 bootstrap 已完成。bootstrap 应由已有 NamespaceSchemaLifecycle 的显式 bootstrap 状态选定，不能根查询失败后回退。
- 物化后自身写入：视图中的来源可能仍是父对象，而孩子物理对象已在内部事务创建。后续选择本地可读对象还需保证同一逻辑对象、创建代次及历史可读性；不能始终强制读旧根中的父物理对象。此项必须用 RR 首写/写后读、另一个事务物化、回滚和 DROP 并发分别验证。
- 旧执行计划或 DDL 任务持有的 schema 版本也须与根持有协调。仅保护最终 SQL 快照，不能自动证明计划生成后、首次打开前的 DDL 变化均已覆盖；应检查已有 schema/MDL/计划失效约束后接入，不能凭猜测增加全局历史缓存。

第五批验证完成：

- catalog-view-native-result.log：原生 KV/目录/共用事务探针首次启动和三次 SIGKILL 恢复通过，四次均检查 INSTANCE_CATALOG_VIEW_PASS。新用例的根切换及页 GC 运行在真实 InstanceMetaStore 上；物理候选过滤使用合成的物理身份和未完成 fork 边，尚不是普通 SQL 老事务的端到端验收。
- catalog-view-production-build.log：撤除所有本批测试钩子后的正式 release 构建通过。
- catalog-view-production-startup-result.log：30 秒内初始化、fork、读取及首次写通过。
- catalog-view-production-detached-result.log：真实普通表/索引/LOB 的混合来源接管，父物理对象实际回收后重启及孩子读写通过。
- 两次构建失败已修复并保留日志：catalog-view-native-build.log 为本地探针使用未完整声明的 ObTransService；改用已有 acquire_storage_snapshot。catalog-view-native-fixed-build.log 为新公开声明缺少 share::SCN 前置声明；已补齐。catalog-view-native-final-build.log 为最终原生成功构建。
- 未运行完整 mysqltest/sysbench。测试和文档留在本地，用户已有测试改动未提交。证据 catalog-view-evidence.json；完整目标未完成，SQL 生命周期及来源路由接入仍是下一步。

第五批提交并推送 6f9e7f62e4ddecc3b0e4d2845e9a9bbe8261136d，远端跟踪一致：True。构建与测试任务已结束；无 PR，完整目标保持进行中。


## 2026-10-08 第六批进行中：SQL 快照与来源根生命周期

- RC 语句取得快照时选择并持有来源根；RR/SERIAL 同一事务复用已持有的根。短期 KV 事务结束后由独立 Handle 保护，不长期占用目录 GC 的普通事务计数。并行工作线程只共享仍被持有的 Namespace/SCN 视图，不凭裸旧 SCN 重新引入历史根。
- 普通来源路由验证物理创建代次和历史可读性；RR 选择父来源后若孩子物化，仅在本地 fork 来源/快照完全一致时采用孩子对象，从而读取自身后续写入。
- 首轮 SQL 回归的 RR/RC/PX、延迟首次打开表、其他连接物化、自身写入/回滚、父 TRUNCATE 与 LOB 均通过。初次脚本末尾调用了不支持的 FORK NAMESPACE __gc__ FROM __gc__，已删除该错误命令；真实 DROP/后台 GC 回归 sql-read-view-gc-result.log 已通过。
- 临时路由探针最初误读 process.out；数据库将 stderr 转入 seekdb.log。修正采集路径后，首轮 64 次用户表扫描全部取得视图。随后扩展外键/REPLACE/upsert 场景，捕获一条缺失视图的外键扫描（sql-read-view-delayed-missing-result.log）。延迟取快照入口已接入，待新二进制验证。
- 普通 SQL/直接 DAS 扫描改为显式 lookup 并传入已保护视图；非 bootstrap 的缺失视图返回错误，不再由空 Handle 静默选择旧路由。优化器/后台等尚未迁移的 prepare_read 调用仍是后续工作，不声称全体来源消费者已经统一。
- 子 Namespace CREATE PROCEDURE 暴露已有错误，上一批正式二进制也复现超时。DDL 日志中 OB_DDL_GRANT_ROUTINE_PRIV 复用了 table_id_=1；NamespaceSchemaPublication 错将它当作表变更，删除核心表来源并导致 schema 刷新反复 TABLET_NOT_EXIST。已按已有 TABLE_OPERATION 范围筛选实际表操作，待复测。sql_read_view_routine_probe.py 纳入本地四件套，基线失败日志保留。
- 来源缓存继续暂缓。精确物理/历史 SCN GC、旧权威及 active/pending 状态移除、后台预算接管、模板和完整主备验收尚未完成。此节不表示本批已提交或完整目标已完成。


### 第六批验证结果与范围

- 临时探针版 sql-read-view-strict-result.log：71 次用户表扫描，缺失视图为 0；RR 晚开表/并发物化/自身写入与回滚、RC 刷新、PX、父 TRUNCATE、LOB、外键延迟快照、upsert、REPLACE、过程内嵌套 SQL 均通过。
- sql-read-view-native-result.log：真实 KV 根选择、记录删除后的同事务视图复用、目录页 GC 不被长期 SQL 事务计数阻塞、根释放后回收及回调失败清理通过。实例日志含 INSTANCE_CATALOG_VIEW_PASS。物理候选边仍是合成对象，不能代替完整物理 SCN 保留计划。
- sql-read-view-unit-result.log：ASan/UBSan 的共享 Handle/并发 lookup/list/最后释放不复活/Registry 析构，以及 8000 映射、批量 COW 等原有用例通过。
- sql-read-view-preparation-result.log：32 分区冷索引创建、更新、类型 DDL、索引读取、LOB、行外向量、回滚及重启通过。
- sql-read-view-ddl-result.log：表/LOB/分区/索引来源图和 SQL schema 对照，多代 fork 保留旧视图、4 个并发 DDL、DROP DATABASE 通过。
- sql-read-view-fts-result.log：冷聚合与全文索引读取不物化、继承全文索引更新/删除通过。
- sql-read-view-detached-result.log：混合来源普通表/索引/LOB 完成接管，父物理来源实际删除后 SIGKILL、重启和续写通过（旧 pin 尚保留）。
- 撤除全部 SQL_VIEW_PROBE/SQL_VIEW_NATIVE_PROBE 注入后，sql-read-view-production-build.log 正式 release 构建通过。正式 binary 的 sql-read-view-production-result.log 与 sql-read-view-production-shared-result.log 通过，后者含首次物化并发创建、主/LOB 原子性及强制退出恢复。
- 主备首次运行保留两项失败：namespace_fork_local 在 bootstrap 注册 MDS 返回 4002、尚未执行用例；错误入口证据 sql-read-view-standby-failure/bootstrap-register-4002.log，未找到无限超时溢出的证据，不能与此前已定位的 4002 合并归因。publication_child 的旧脚本等待 pending_schema_version>0，当前同事务发布已经消除此窗口，属于过时断言。
- 更新 standby_publication_failover_probe.py：等待备库见到已推进根且 active/pending 清零，立即确认新 SQL 列可读，然后 SIGKILL 主库、升主并读写及再次 fork。sql-read-view-standby-updated-results/results.json 两项通过；namespace_fork_local 覆盖冷继承读取、物化及 DDL 回放、父 TRUNCATE/DROP、动态 Namespace、升主写入；publication_child 覆盖原子发布后丢主。先前 bootstrap 4002 保留为未定位问题，成功重跑不表示它已修复。
- 本地四件套 direct gate 加入 sql_read_view_probe.py 与 sql_read_view_routine_probe.py，原生 gate 继续包含扩展的 catalog_read_view_probe.ipp。测试、文档与失败日志不提交；未运行完整 mysqltest/sysbench。

### 自动目录页回收之前必须闭合的主备边界

本机 Handle 能让本机目录 GC 保留旧根，但主库看不到备库单独持有的旧根。后续主库删除不可达页并复制后，备库旧事务必须按其受保护 SCN 读取页面历史版本，同时明确实例 KV 的 MVCC 保留租约；不能继续对旧根使用最新 KV 快照。当前普通来源解析仍以短期最新 KV 事务读取不可变页，需要在自动页 GC 启用前修正，并增加“备库 RR 已取快照但未开表→主库更新根并回收旧页→备库晚开表”的定向测试。现有主备通过项未覆盖这个交错。当前 collect_metadata 仅由旧 __gc__ 控制入口触发，普通 FORK SQL 拒绝该特殊名字；不能把这当作已完成自动页 GC。

后续继续：闭合跨实例旧页/MVCC 保护；迁移剩余 prepare_read/物理判断消费者并移除旧 exception/父链及 active/pending 恢复协议；构建物理身份+SCN 保留计划、并发 GC 截点；后台预算接管；模板初始化；完整主备/全量复制/升主验收。完整目标未完成。

第六批已提交并推送 0f90666f1e4253ef9969ea2a24455e7b63cfb0f6，远端跟踪一致。仅 20 个生产代码文件，+252/-75；未提交文档和测试，未创建 PR。证据 sql-read-view-evidence.json。本批构建和定向回归已结束；原有用户测试改动保留。完整目标继续进行，上述主备旧页/MVCC 边界和未定位 bootstrap 4002 仍未解决。


## 2026-10-08 第七批：旧目录页的跨实例 MVCC 保护

- 已复现旧缺口：仅持有 NamespaceCatalogViews 不会降低 InstanceMetaStore 合并保留水位。catalog-mvcc-red-result.log 对应原生探针报告 CATALOG_VIEW_MVCC_GAP；真实 held SCN 小于 min_retained_snapshot。选择根的短事务结束后，注册根本身不足以保护主库 DELETE 回放后的页面历史。
- InstanceMetaStore 增加独立 SnapshotHandle，先在仍存活的选根事务下交接保留，再结束短事务；不长期占用原生事务或目录 GC 的普通准入计数。持有快照的注册表以共享寿命对象管理，句柄释放不依赖 store 析构次序。
- NamespaceCatalogViews::Entry 持有不透明的存储保留句柄。GC 复制 Entry 后即使 SQL 最后一个 View 释放，工作集仍保留所需 KV 历史。普通 SQL 的来源页读取、物理候选 GC 对旧视图的读取均改为该视图的 SCN。
- 目录页 GC 允许逻辑删除仅被活跃旧读者持有的页；本机 MVCC 留住历史版本，备库也采用同一规则。这里只解决元数据页历史，不等于完整物理来源/SCN GC 已完成。当前自动目录页 GC 尚未启用。
- 新弱读可能取得比强读更早的原生快照，因此 min_retained_snapshot 同时受原生弱读水位约束。catalog-mvcc-horizon-native-result.log 通过，实例日志包含 INSTANCE_CATALOG_VIEW_PASS，涵盖历史页、根工作集交接、最后释放及 weak_horizon。
- 主备首轮 catalog_read_view 已确认主库旧根逻辑删除回放，以及备库 RR 晚开表读到旧值；提交后的自动提交 RR 查询超时，完整用例判失败。失败日志保留在 catalog-mvcc-standby-failure，不将部分断言成功作为通过。
- 进一步最小复现 standby_rr_admission_probe.py 不执行任何 GC：RC 与显式 RR 成功，提交后的自动提交 RR 超时。日志显示尝试申请日志支持的事务 ID 被备库拒绝；纯读 get_read_snapshot 可绕过 start_tx，漏设 WRITE_FENCED。修正原生纯读准入后正在复测，保留 standby-rr-admission-red.log。
- 四件套原生 gate 已加入主备 catalog_read_view；该用例还包含提交后的 RR/SERIAL 自动提交读取，能够检出本轮新发现的失败。所有探针和测试均保持本地；来源查询缓存继续暂缓。


### 第七批验证结果

- catalog-mvcc-standby-fixed-results/results.json：主库旧页 DELETE 已回放，备库 RR 首次打开另一张表读取旧值，提交后 RR/SERIAL 自动提交读取新值，全流程通过。standby-rr-admission-green.log 的无 GC 最小复现同时通过 RC、显式 RR、自动提交 RR/SERIAL 四个断言。
- catalog-mvcc-production-build.log：撤除本批 SQL_VIEW/原生探针/GC 触发钩子后的正式 release 构建通过。catalog-mvcc-production-sql-result.log：RR 延迟打开、并发物化、自身写入与回滚、RC、PX、父 TRUNCATE/LOB、外键/upsert/REPLACE 通过。
- catalog-mvcc-production-standby-results/results.json：子 Namespace 原子 DDL/来源发布后强杀主库、备库升主、读取/续写及再次 fork 通过。
- catalog-mvcc-unit-result.log 的 ASan/UBSan 根工作集寿命及现有 8000 条目/COW 回归通过。本次原生探针初次构建暴露缺少完整 ObTransService 声明及命名空间限定，已修正本地注入脚本；生产代码不依赖该测试声明。
- 主备删除测试实际执行了逻辑页删除并读历史，但未强制触发 KV minor compaction。另有原生 min_retained_snapshot 断言验证合并保留水位；不能把两者扩大成已验证全部 KV 合并交错。
- 四件套保留所有本次失败用例。来源缓存继续暂缓；完整目标仍缺剩余旧路由移除、自动预算页 GC、物理身份/SCN 保留计划、后台接管、模板及完整主备恢复。此前 bootstrap MDS 4002 尚未定位。
- 证据及源文件/二进制 SHA256：catalog-mvcc-evidence.json。文档、测试和用户已有测试改动均不提交。

四件套维护补充：共用事务原生探针原先断言 SQL end 前后 min_retained_snapshot 完全相等；加入弱读水位后，该水位可以独立前进。断言改为不倒退且始终不超过仍 attached 的快照，目录 GC 仍须在 detach 前超时。完整多 fixture 原生 gate 本批未重跑，本批实际执行的是独立 catalog 探针、主备和正式 SQL/升主用例；不声称整套四件套通过。

第七批已提交并推送 ded01f924327a8dd351283be0160415fe99be57d，远端跟踪一致。仅 7 个生产代码文件，+108/-29；无 PR，无测试/文档提交，用户原有测试改动保留。定向测试进程全部结束。整体目标保持进行中，下一步继续旧来源消费者迁移与自动 GC 协议，不能将本批旧页保护验证等同于完整物理 GC 完成。


## 2026-10-08 第八批进行中：剩余来源读取入口

- TabletAccess::prepare_read 不再默认接受缺失视图；语句/事务读取显式传递已保护的根。prepare_current_read 明确选择新提交视图，原生读时间戳在 KV 保护登记后取得，不使用 GTS 缓存作为已提交事务快照的替代。
- 优化器行数/块数估算、范围划分、IVF 后台 tablet 检查、DDL fork 来源解析、自增序列迁移以及 ForkTableHelper 改为显式 current 访问。不会因为来源缺失改查 Namespace 1，也不新增来源缓存。
- 全文 DML doc-word 读取根据已有 info.snapshot 查找语句/事务持有的视图并交给 TabletAccess，不自行取得一个更新的来源根。
- Kernel 普通读取删除无视图时调用 exception/父链的回退。只有生命周期明确处于 bootstrap 时允许读取尚未登记初始目录的本地 tablet；该状态来自初始化参数，普通 Namespace 空视图报错。
- 编译取消默认参数后发现 ForkTableHelper、自增序列迁移两个遗漏入口，已接入。source-consumers-build.log 保留初次编译失败；定向回归尚在准备，不能声称已通过。
- 物理 GC 的候选过滤及持久化 ownership/接管等仍有旧 exception 消费者，后续继续迁移。本批不声称旧权威已全部删除。

### 第八批验证完成

- source-consumers-final-build.log：正式 release 构建通过，无测试钩子。
- reads/index/preparation/fts-ddl/ivf/ivf-pq/fork-table 定向回归全部通过，覆盖冷继承、全文 DML、32 分区 DDL、LOB、行外向量、IVF 后台缓存、FORK TABLE 自增与重启。
- source-consumers-standby-result.log：namespace_fork_local 主备回归通过，耗时 105.89 秒。
- 本地四件套 direct gate 已加入修正后的 inherited IVF flat/PQ 用例；失败编译日志保留。未运行完整 mysqltest/sysbench，未提交测试与文档。
- 证据 source-consumers-evidence.json。物理 GC、ownership 等旧消费者尚需迁移，完整目标未完成。

第八批提交并推送 252b46a0f0abbcc7b20f5a2558193a1eb8df8fa0，远端跟踪一致。仅 12 个生产代码文件，+47/-20。无 PR，未提交测试/文档。

## 第九批进行中：物理 GC 改用来源树

- filter_unreferenced_tablets 批量标记 live/deleting 根和受保护旧读根，再沿本机 incomplete 物理 fork 边标记；不再按 Namespace 父链探测地址。当前快照内共享目录页只扫描一次。
- 删除 InstanceNamespaceMetadata/Directory 的旧 resolve_read_tablet 接口。旧根仍按其保留 SCN 读页面历史；物理依赖环或坏页导致整轮失败，不提交部分删除。
- 这是对象存在性的过滤迁移，尚未替代全局历史 pin，尚未完成按身份/SCN 的保留计划与短截点协议。
- 原生用例增加无父链 Namespace 的显式来源、异逻辑 ID 物理依赖及环拒绝；旧父链模型测试改为不可变来源快照测试。四件套原生 runner 同时修正过时的 marker 全串匹配。正在验证。

第九批验证完成：source-gc-native-build.log / source-gc-production-build.log 均构建成功。原生 INSTANCE_CATALOG_VIEW_PASS 增加 source_graph_gc=1，历史页保护及损坏物理环拒绝通过。正式 SQL read-view、detached 普通与 mixed-source 两类回归通过；主备 namespace_fork_local 通过（99.04 秒）。source-gc-read-view-invalid-args.log 是首次 runner 误传 --gc 的参数错误，改正参数后通过；未把参数错误当成产品缺陷。证据 source-gc-evidence.json。完整四件套未重跑，更新的 durable/metadata 原生 fixture 本批尚未执行；本批实际执行的原生 fixture 为 catalog_read_view_probe。

第九批提交并推送 056d0e7cb2a7a808c4e3b107b0b5e23010f34391。仅 3 个生产代码文件，+56/-134。首次推送遇到 SSL_ERROR_SYSCALL，重试推送成功，非权限问题。

## 第十批进行中：ownership 与物理接管身份

- 当前 ownership 与 tablet→当前 table 映射统一查来源树，主备同实现；移除 Kernel exception cache 加载、失效和写入及独立 tablet-table 内存缓存。
- 后台接管使用创建时的 table ID。原 ObCreateTabletSchema 已携带此参数，但 TabletMeta 没有保存；新增 create_table_id_，沿首次创建、复制、序列化与恢复保存。它不包含 namespace_id，不参与 Namespace 归属判断。每个常驻 tablet 增加 8 字节，8000 个约 64 KiB。
- DDL 可以改变当前 tablet→table 绑定，因此 create_table_id_ 仅作物理接管身份，不能替代来源树中的当前逻辑归属。大合并按 SCN/schema 取版本仍是独立 TODO，未在此解决。
- ensure_tablet 已存在物理对象的入口改查来源树；仅显式 bootstrap 可在初始根尚未发布时使用创建对象。
- 新增 retired_baseline_restart_probe.py 并加入四件套 direct gate：三代物化、删除中间 Namespace/源表、重启后接管与来源实际回收。尚待执行。
- 第一次编译触发 ObTablet 固定大小静态断言，已同步由 1368 改 1376；第二次发现两处残余 cache invalidation 调用，已删除。保留 tablet-identity-build.log / tablet-identity-fixed-build.log。

### 第十批验证完成

- tablet-identity-production-build.log 正式构建通过，全部本批测试注入已撤除。
- tablet-identity-preparation-result.log：32 分区、DDL、LOB、行外向量与重启通过。tablet-identity-standby-results 两场主备回放/升主通过（99.17 秒、93.94 秒）；这三项使用 MDS 修复前二进制。
- 新退休来源重启用例首轮 1h 参数超过 [3s,5m] 限制，修正为 5m 后第二轮复现 MDS 启动 4002：read_raw_data 将剩余 timeout 9221580585806369869 加当前时间溢出为 -9223372036854775806。证据 tablet-identity-mds-overflow-evidence.log。修复是通用饱和时间加法，不是 Namespace 特判。独立提交并推送 b6ef42a29（4 行新增、1 行删除）。此前第六批 bootstrap-register-4002.log 尚无同样负 timeout 证据，仍不直接宣称已根治那一项。
- tablet-identity-native-result.log：真实物理 tablet 的 MDS raw scan 绕开 cache，INT64_MAX、接近 INT64_MAX、普通时长三个输入均正常迭代结束；ObTabletMeta 创建身份复制及持久化 roundtrip 通过。历史根/MVCC/GC 原生断言亦通过。
- 修复后正式 binary 的 tablet-identity-retired-result.log：三代物化→删除中间 Namespace 与根表→SIGKILL→重启完成接管→源物理对象全部回收→孩子继续写入，通过。tablet-identity-read-view-result.log SQL 视图回归通过。
- 新用例进入本地四件套 direct gate，MDS/identity 原生断言经 catalog_read_view fixture 进入原生 gate。未跑完整 mysqltest/sysbench，也未声称整个四件套已重跑。证据 tablet-identity-evidence.json，含三份二进制 SHA256 和本批源文件 SHA256。
- 当前 ownership、正常读写和接管身份已经脱离 exception 读取；持久化 exception 仍服务删除整理/旧 DDL 协议，下一批继续移除。精确 SCN 保留计划、自动 GC、只读后台物化、模板等目标尚未完成。

第十批已提交并推送 9c2d3f312，远端跟踪一致。仅 4 个生产文件，+76/-186；先前独立 MDS 修复 b6ef42a29 已推送。所有本批测试进程结束，无 PR、无测试/文档提交。

## 第十一批进行中：删除旧 DDL 补发布协议

- 删除 active_schema_changes / pending_schema_version、begin/finish change、begin/finish recovery 与独立 SQL→KV 补发布函数。ObDDLSQLTransaction 直接在原生提交前 stage SQL/schema/来源根；重启装载已提交根与相同 MVCC 边界的 SQL 元数据。
- 删除 worker_commands_prototype.ipp 及其构建清单入口，移除已无消费者的 NamespaceControlState / IExceptionLoader / InstanceExceptionLoader。
- 仅删除旧目录发布补救；已有长时 DDL task 的原生恢复仍保留。持久化 exception 和删除整理仍待下一步迁移，不能声称所有旧权威已消除。
- 四件套的 typed/durable/DDL cleanup 原生 fixture 已适应无 marker 模型。原故障脚本 fork_ddl_publish_recovery_probe.py 改为调用真实共用事务 commit_crash 用例，主备 publication 用例明确断言旧 marker 字段不存在。正在构建与验证。

### 第十一批验证（2026-10-08）

- 正式版 table-family DDL：8 分区、LOB、索引、TRUNCATE/DROP、4 路并发 DDL、旧孩子快照均 PASS；SQL read-view RR/RC/PX/自身写入/父 TRUNCATE PASS。
- 正式版主库丢失后升主 publication_initial / publication_child 均 PASS，已提交字段不含 active/pending，升主前后孩子可读。
- 原生完整 fixture 初装及 created→marked→finished→verified 三次 SIGKILL 恢复 PASS，包括 typed metadata、目录树、pin、KV 自身写入/快照/冲突、共用事务、8000 分区描述、物理创建身份、MDS 极大 deadline。
- 初始/子 Namespace 各 rollback、abort_crash、commit_crash 六项全部 PASS。故障时直接观察 SQL 及目录根，再崩溃重启，验证不依赖先登录拥有者进行补发布。
- 首次原生编译失败是本地 DDL fixture 的 start(proxy, int) 重载歧义，已改为 int64_t；恢复了完整原生 fixture 中来源根变量重名。失败日志保留。未将这两项当作生产缺陷。
- Catalog batching ASan/UBSan PASS。测试 hook 已全部移除，正在完成最终正式重编译。证据索引 ddl-publication-cleanup-evidence.json；未跑完整 mysqltest/sysbench 或完整四件套。

第十一批最终正式重编译成功；74e9e3770 已提交并推送，仅生产代码 15 文件 +11/-875。

## 第十二批进行中：删除持久化 exception，物理 DROP 纳入 DDL 事务

已定位旧 DELETE MDS 在 Namespace 路由中被跳过，提交后独立物理清理是 exception 账本的剩余消费者。改为在统一 DDL 发布入口根据旧/新来源根确定本地被移除的物理身份，将 DELETE MDS 直接登记到 SQL 所有者事务。原 SQL mapping/history 删除已在原 DDL 事务执行；不再用另一 Namespace 的 SQL 事务补删。Namespace 删除后的物理扫描仍依靠原生物理对象枚举及当前/保留来源根过滤。

- 当前已删除持久化 ExceptionRecord、delta planner、读写/扫描 API、Namespace 删除时的 owned 清理列表以及 postcommit finish_schema_publication。物化仅发布来源树；Namespace drop 不再枚举无实际锁操作的 exception 清单。
- 物理 DELETE 在 stage 中校验来源物理身份及 create transaction ID，然后通过相同 SQL 连接注册 MDS；保留原 DDL 对 mapping/history 的 SQL 更新。继承地址不登记 DELETE。
- collections 中移除 EXCEPTIONS 声明，虚拟表按已声明 collection 枚举，跳过未声明编号；未引入升级迁移/退避路径。
- 四件套 atomic_ddl_catalog 新增对 DELETE MDS 提交前/回滚/提交后及重启状态的直接断言。父 TRUNCATE 及物化共用事务用例改为核对来源树，删除旧 exception 断言；typed fixture 保留删除准入、物理存在阻止 prune、来源根旧版本与版本冲突覆盖。
- 首次构建误删共享 key_id helper 导致编译错误，已恢复；失败日志 atomic-physical-drop-build.log 保留，尚未完成动态验证。

- 进一步审计确认旧物化还向目录 Namespace 的 __all_tablet_to_table 写 encoded ID 映射副本。现有 mapping 读者使用各 Namespace 的逻辑 tablet ID；物理接管已用 native create_table_id_，创建绑定走已编码的原生 MDS 参数。已删除此无消费者的写入，shared_transaction 回归改为核对 SQL 目录不存在此副本、source 树及物理 CREATE 原子一致。
- 将 ObTabletDrop 原有 MDS 注册段提取为 register_delete，普通逻辑 DDL 仍执行原 mapping/history SQL 后调用它；发布入口和 Namespace 物理 GC 使用同一个 MDS 注册方法。物理 GC 不再加载 SchemaService，也不再向初始 Namespace 插入物理地址的 SQL history。

### 第十二批动态证据进度

- 正式版六项 PASS：table-family DDL/并发、物理 DROP、已物化父 TRUNCATE 后孩子首次写、三代已删除中间来源重启后接管/实际回收、物化事务（无 encoded SQL mapping 副本）、RR/RC/PX/自身写入。
- 六项 DDL rollback/abort_crash/commit_crash（初始及子 Namespace）PASS，已直接断言物理 DELETE MDS 的未提交/回滚/提交及恢复状态。
- 原生物化四故障（CREATE 后及 source-root 发布后分别回滚/强制 redo 宕机）PASS，回滚无可读本地副本、重试后物理 incarnation/LOB/自增正确。第一次命令漏 --faults 导致用例筛选为空，是调用错误，原日志保留。
- 主备 publication_child PASS。namespace_fork_local 初次在主库首次登录时握手失败，尚未进入复制验证；随后部署覆盖了服务端日志，不能归因为旧 bootstrap 4002 或本次改动。已为本地 runner 增加失败日志尾部留存；原用例不改代码重跑 PASS（100.6s），覆盖父 TRUNCATE/DROP 历史来源、Namespace 删除回放、升主写入及再 fork。
- 完整原生 fixture 初次旧视图断言 -4016，初次无细分行号。增加诊断后初装+三次崩溃重启 PASS（所有四次保留水位与 weak 一致）；不能据此断言已定位第一次失败。
- 同时审查发现旧 fixture 错误要求释放自己视图后 retained >= min(strong,weak)：其他刚登记尚未取得 SCN 的真实 KV 事务会使 min_retained_snapshot 合法返回 MIN。新增真实 begin_read 获取 SCN 回调窗口的确定性断言，并改为验证本视图句柄已释放及 weak 上界，不错误排斥其他有效读者。正在编译并执行此补充。

确定性 reader_admission 用例 PASS：真实事务登记后、快照取得前 min_retained_snapshot 返回 MIN，同时本视图 retention weak_ptr 已 expired。该测试纳入四件套 catalog_read_view 原生 fixture。无生产保留逻辑改动；第一次无行号失败仍不宣称完全归因。当前所有 test hooks 已移除，最终正式重编译进行中；证据索引 atomic-physical-drop-evidence.json。

第十二批最终正式重编译成功，aa162807949e892e173f30c42e5b7cc8ea7b2fc0 已提交并推送，远端一致。仅 16 个生产文件 +81/-516，无 PR、无测试或文档提交。当前测试进程均已结束。

## 第十三批准备：精确 SCN 保留与主备截点

下一步不能仅把全局 pin 改成按 tablet 的 snapshot 列表：还须阻止尚未提交的 fork 在备库已清理的旧 SCN 上出现，及防止长遍历占有整个 metadata_mutex。将现有持久化 snapshot coordination 水位作为后续新 fork 的下界，物理计划截点不得越过可读边界及已复制的发布水位。全局 pin 暂不移除，先实现可验证的 cap/物理 incarnation 归并与完整计划协议。存储层只接收物理身份和 SCN，不增加 Namespace 解释。

### Phase 13：带快照上限的物理保留工作集（进行中）

`NamespaceCatalogTree::retain_sources` 按实际物理地址和 create transaction 身份归并来源，沿根、分支和叶子的 cap 取最小值。共享页只在遇到更早 cap 时重读；循环、身份冲突、坏页或预算超限使整个计划失败。当前接入物理存在性过滤，全局 SCN pin 尚未移除。

已跑本地 ASan/UBSan 单测：8000 个来源、共享页与不同 cap、incarnation 冲突、读错误、预算超限和循环，随后原有 200 轮随机 COW 更新全部 PASS。日志 retention-workset-unit.log。正式编译进行中。

后续截点协议的核对结果：

- 持久化 coordination 水位与 fork 登记共用 KV 行锁；看到水位 C 的一致快照必须同时看到所有已经获准在 S<=C 发布的 fork，之后登记这种 S 会失败。备库只采用已经回放可见的水位。
- 新弱读可能取得比当前强读更早的根。因此计划截点不能只取强读，也不能只列当前已登记的 SQL View；须纳入新弱读的可见边界和已有旧 View。
- 物理空壳检查目前只检查 DELETE 已提交，没有比较新读边界。计划增加通用的物理删除 SCN 检查，避免弱读尚可选择旧根时先清掉实体；存储层不解释 Namespace。
- 对象删除与 MVCC 版本回收是两种结果。不可把来源存在性集合直接当成已完成的版本保留协议。
- 当前 262144 条预算超限会拒绝本次回收，保证安全但不是大图最终解法。仍需接入可丢弃的本地临时文件/外排序，不能宣称重复重试即可解决超过预算的图。

Phase 13 正式编译 PASS；定向 SQL 回归全部 PASS：物理 DROP 12.12s、三代来源删除后重启接管并实际回收 38.83s、RR/RC/PX/自写/截断读视图 13.07s。日志 retention-workset-build.log、retention-workset-*-probe.log，结果 retention-workset-formal.json。8000 来源工作集及既有 COW 随机回归已经包含在四件套使用的 catalog_batch_probe.py 中。此批仅生产代码提交，不宣称全局 pin 已去除。

### Phase 14：新弱读的来源与空壳回收（进行中）

已在原生 KV 上确定性复现：没有 NamespaceCatalogViews holder，KV 历史仍可读；目录已删除，模拟本机弱读边界尚在删除之前。旧 filter_unreferenced_tablets 错误返回 freed=1/retry=0（weak-source-gc-red.log）。

修复分两层：
1. InstanceMetaStore::begin_weak_read 显式选择本机新弱读的可读 SCN，保持真实事务和 KV 历史保留；目录回收同时读取这个历史目录和最新目录，再合并已登记读视图。
2. 原生空壳检查要求本机新读边界 >= DELETE 的 commit version，不能仅凭 ON_COMMIT 清空。需要稍后重试的 tablet 不阻塞本轮其他候选。存储层只比较物理状态和 SCN。

weak-source-gc-green.log PASS：相同旧根未回收，边界前进后释放。empty-shell-horizon.log PASS：真实 SQL DROP 后 tablet 保持 DELETED/committed/nonempty 至少 12 秒，边界前进后转 empty shell。所有测试 hook 已通过独立脚本清除；四件套 bootstrap-native-kv 新增上述两个用例，相关注入脚本统一由 gate_probe_injection 管理。正式重编译、主备和退休中间副本接管回归继续执行。

### 后续物理 SCN 计划的具体协议（待实现，非完成状态）

- 计划截点 R 使用本机新弱读边界；短 publication fence 下取得 R、KV 事务及已有 Catalog View 副本，然后释放 publication fence，遍历期间仅保持物理 GC 串行权和快照 lease。新 SQL/DDL 不等全图遍历。
- 在 R 上读取已复制的 coordination 水位 C，计划的无特别依赖时保留下界为 min(C,R)。看到 C 的一致视图包含所有获准在 S<=C 发布的 fork；之后这种登记会被相同行锁协议拒绝。
- 遍历 R 的 live/closing 源根和截点已有读视图，各根的 cap 传递到叶子并归并物理 incarnation。沿真实本地 tablet 的未完成 fork 边继续保留 source@min(读取需求, fork SCN)。完成状态只看本机 native 状态，主备各建本机计划。
- 截点后的 Namespace fork 要么使用 S>C，要么沿此前已保护的固定 cap 继承；物化新副本的旧数据来源已经在旧根中受保护。普通新物理对象不能凭旧快照重新引入此前无引用的来源。
- 存储查询接收物理 ID、创建事务身份、SCN；未匹配地址只受计划下界限制，身份冲突不能当成不需保留。计划整体发布，错误保留前一份安全计划，不能发布部分结果。
- freeze reload 与计划整体交换；使用截点版本防止较早构建的计划覆盖较新计划。去掉 fork 完成后的同步全图 reload，保持 fork 元数据成本与 tablet 数无关。
- 仍需落地预算外临时工作集及索引查询，不能以永久超预算停止 GC 作为最终完成。全局 pin 要在上述机制完整验证后移除。

Phase 14 完成验证：去除钩子的正式二进制物理 DROP 11.83s、RR/RC/PX 读视图 10.97s PASS；多代中间副本删除后重启完成接管并实际回收 PASS；主备 namespace_fork_local 99.7s PASS（历史来源、父 TRUNCATE/DROP、Namespace drop admission、升主写入和新 fork）。证据 weak-source-gc-evidence.json。全局 pin、完整物理 SCN 计划及预算外工作集仍在后续范围。

### Phase 15：物理 SCN 计划与合并接入（进行中，尚未提交）

- 新增 storage/physical_snapshot_retention.h，仅含物理 ID、创建事务身份、SCN、截点和后续来源下界。Namespace 的 transient PhysicalRetention 复用该字段类型；存储层无 Namespace 解析。
- load_physical_retention 短 publication fence 内固定 weak SCN 和读视图副本，随即释放 publication fence；保留物理 GC 串行权遍历 weak 目录/旧读根及本机未完成 fork 边。完成对象依赖不再传播到其旧源；失败不覆盖调用方旧计划。
- 动态截点测试 PASS：父来源 A 与已删除中间副本 P 分别保留在两次 fork 的不同 SCN；暂停在截点后，冷孩子 SELECT + UPDATE 物化耗时 0.0266 秒；本次计划仍读旧根且不包含截点后新物化 C。证据 physical-retention-cut-empty-bootstrap.log，二进制 physical-retention-artifacts/seekdb-cut-native。
- 本轮失败及处理：初版测试命令使用保留前缀 __，在 resolver 1235，改用普通名称的本地注入入口；随后真实 4018，诊断明确发生在 watermark 读取，weak 截点早于首次目录初始化事务。只有该截点全目录为空时才接受 floor=1 的保守计划；若任何 Namespace 记录存在而 coordination 不存在则失败。没有初始化修复/补写。保留原始 physical-retention-cut-native.log、*-native-retry.log、*-diagnostic.log。
- ASan/UBSan 来源/计划查询及原有 200 轮随机 COW PASS，physical-retention-plan-unit.log。四件套新增 physical_retention_cut_probe；相关临时 hook 已移除。
- FreezeInfoMgr 已改成装载完整 PhysicalSnapshotRetention，按物理 ID 与创建身份查询，整体交换且拒绝更早截点覆盖较新计划。fork/drop 不同步重载全图；物化以原生 multi_version_start 校验实际可用历史，移除对 pin 行和冻结缓存的读取。
- 已移除把 KV Namespace pin 装入 tablet_id=0 全局 snapshot 列表的回调。持久化 pin/lineage 行本身尚未清理，仍用于 fork admission/lineage；不要宣称相关代码均已删除。
- 接入构建最初漏了 ob_partition_merge_policy 的宏调用参数，已补齐；physical-retention-integration-build.log 保留失败，*-retry-build.log PASS。正式集成二进制 physical-retention-artifacts/seekdb-integrated，开始 undo_retention=0 实际合并选择性保留与多代退休来源接管测试。
- 仍未完成：262144 内存预算外 spill；物理存在性 GC 自身仍用旧全遍历 fence（本批缩短的是新 SCN 计划构建）；只读后台物化、模板接管/nonlogin、pin/lineage 清理和主备完整验证。

Phase 15 已通过实际选择性合并：undo_retention=0，来源 MVS 精确停在 fork SCN 1791459180807662040，无关新表 MVS 推进至 1791459218070481000；冷孩子仍读到 fork 值。三代来源删除/重启/接管/实际回收通过；主备 96.33s 通过；最终正式 SQL 读视图 12.79s、父 TRUNCATE 后孩子写 11.11s、物理 DROP 12.29s 均通过。正式无 hook 编译通过。全量原生 fixture 发现既有测试长期发布虚构 tablet 82（身份 71），新收集器正确以 OB_TABLET_NOT_EXIST 拒绝；已将其改为真实继承条目的 COW 发布和跨重启物理身份验证，重新编译中。所有失败日志保留于 physical-retention-evidence.json。

Phase 15 收尾：physical-retention-native-lookup-retry.log 全部通过，created/marked/finished/verified，三次 SIGKILL 恢复。诊断确认 kept=0 是初始化阶段合法保留边界，错误创建身份返回 OB_STATE_NOT_MATCH；原先测试传 INT64_MAX 非法 SCN、要求 kept>0 均已修正并保留失败日志。生产无 hook 编译通过。98b9c1527 仅 14 个生产文件 +266/-83；推送执行中。

### Phase 16：删除 snapshot/pin 引用链（进行中）

source-roots-only-red.log 在 Phase 15 正式二进制确定性失败：fork 仍写 collection 3/8。改为仅插入带 cap 的 Namespace 根，直接持有 coordination 行锁至提交，拒绝 S<=watermark；删除 Namespace 清空自身根，不递归释放祖先 snapshot。页 GC 仅从 Namespace 当前根出发，旧读者仍由独立 KV snapshot lease 保护。移除 snapshot_ref/parent_ref/ref_count、两个 collection 描述和 lineage adapter/算法。对应 native typed record / 三次崩溃恢复测试改验 capped roots、watermark fence；新增 source_roots_only_probe 纳入四件套。测试与文档只留本地。

98b9c1527 已通过 gh credential 推送，远端一致。Phase 16 ASan/UBSan 全部通过（source-roots-only-unit.log）：8000 来源、共享 cap、身份冲突、预算/循环失败、200 轮随机 COW 和 reader holder 生命周期。当前 Phase 16 原生测试编译进行中。

### Phase 17 后续核对（尚未实现）

物理存在性 GC 可复用 FreezeInfoMgr 发布的不可变物理计划，避免每次候选/DAG检查重新遍历全图。计划以 shared ownership 移交，不能复制大 map，也不能拿着 FreezeInfoMgr 的读锁执行 SQL/物理删除。候选必须校验当前 MDS 创建身份、提交状态及创建/删除 commit version 不晚于计划 read_snapshot；计划之后新建/删除的候选留待下一计划。再在短 publication fence 下检查 active resources 并执行实际回收。已删除 Namespace 最新名单可用于选择候选：截点仍 LIVE 的源被旧根计划保护，截点之后创建的物理对象被出生边界保护。未来新 fork 不能凭空重挂此前不可达旧物理来源，其继承 cap 已由截点根保护。错误/无计划不得回收。

注意 protect_snapshot_tablets 也用于退休中间副本 baseline admission；不能仅替换 empty-shell 一处而遗漏该全图调用。当前 InstanceNamespaceDirectory::filter_unreferenced_tablets 的本地 weak/view 测试需随实际调用入口调整，不能为删接口而悄悄丢弃既有失败回归。

Phase 16 完成：原生 KV 初始及三次 SIGKILL 恢复全部通过；主备 namespace_fork_local 95.54s PASS；无 hook 正式构建通过，shared roots 10.90s、SQL read views 12.72s、多代退休来源重启接管/实际回收 39.61s PASS。cbe17577a 仅 7 个生产文件 +23/-414，已推送。证据 source-roots-only-evidence.json。

Phase 17 出生边界补充核对：create_commit_version_ 对物化对象是继承的逻辑出生 SCN，不能作为物理提交边界；get_latest_tablet_status 的 trans_version 在读 MDS 磁盘分支被 reset，也不能直接使用。应使用持久化 create_commit_scn_（真实 CREATE 提交日志 SCN），并检查 DELETE 的真实提交 SCN/版本；seekdb 本地写事务正常提交版本来自提交日志 SCN（ob_tx_ctx.cpp:2595/2635），与本机 weak 截点可比较。不新增字段、不靠内存 MDS 版本回退。

### Phase 17：物理 GC 复用不可变计划（进行中）

FreezeInfoMgr 持有 shared_ptr<const PhysicalSnapshotRetention>，查询获得句柄后不持有 FreezeInfoMgr 锁；不复制 map。protect_snapshot_tablets 改为查询已发布物理计划并核对原生 MDS 身份和真实创建/删除日志 SCN，去掉每次全物理库存/全 Namespace 图遍历。不再按 ns 编码决定是否需要过滤；候选来自原生非 LS-inner 库存。物化 create_commit_version_ 是继承出生时间，因此使用 create_commit_scn_，MDS 已落盘时也存在。empty-shell 每次处理至多 64 个候选并轮转，避免一直被前缀保留对象阻塞；Namespace 删除库存扫描移出 publication fence，最后 prune 也释放 publication fence。逻辑 Namespace 删除写入仅主库执行。

首次 hook enable 因 release_publication 新增第二个调用导致旧注入脚本唯一锚点断言失败，所有 hook 自动清除；随后 shell 执行了正式无 hook 构建并通过（physical-gc-plan-native-build.log 实际为 production）。该产物已保存 physical-gc-plan-artifacts/seekdb-production 并开始定向回归。注入脚本锚点已精确到截点后的 metadata 构造，正在构建 native probe。新增 physical_gc_plan_probe 到四件套：父 SQL tablet 在截点前删除仍被根引用；冻结计划不更新后再物化 B、fork C、删除 B，验证旧逻辑出生不能冒充物理创建时间，旧源 A 不被清空；计划恢复且 C 删除后 A/B 实际回收。

尚未完成：原生新用例与正式回归结果；目录 filter_unreferenced_tablets 目前仍供原生弱读/view 回归使用，生产 GC 已改用计划；后续收敛测试入口后清理该冗余实现。bounded spill、只读后台推进、模板完整接管/nonlogin、主备本地完成/fullcopy 仍未完成。

Phase 17 验证完成：physical_gc_plan_probe 持续 12 秒/24 次查询，旧来源 A 保持 DELETED/committed/nonempty，新 B 保持 NORMAL/committed/nonempty；刷新恢复且 C 删除后 A/B 实际回收。正式物理 DROP 17.10s、多代退休来源重启接管与回收 45.52s、并发历史 fork/DDL 12.48s PASS；主备 99.48s PASS。无 hook 正式重编译通过。证据 physical-gc-plan-evidence.json。只提交 3 个生产文件，未提交四件套或文档。

### 用户范围纠正：避免过度设计

Phase 17 提交 `4dcb94cb5` 已推送。此前提到的 bounded spill/临时文件不是下一阶段必做项：用户明确否定现在扩展这套机制。尚未实现任何临时文件生产改动，仅查阅现有接口。保留内存计划和现有安全超限行为，记录超大图回收无法推进的限制；以后有实际规模证据再决定。接下来继续只读后台物化/接管、模板完整接管与本地复制恢复的正确性闭环，避免把可选资源优化作为架构前提。

### Phase 18：只读 Namespace 后台物化（实现/验证中，尚未提交）

- 不实现 spill、临时文件和持久化任务清单。复用现有 5 秒维护唤醒，在 Namespace 层轮转有效 fork Namespace；每个 Namespace 只记录一个可丢失的扫描位置，删除后移除。重启/升主重新扫描。
- 来源树增加有序续扫：每批最多 64 条，按最后 key 定位后续路径，不全量展开来源树。每次最多 16 个主 tablet/LOB 绑定单元，执行期限 2 秒；失败条目下一轮重试，避免一个错误阻塞整个清单。
- 创建复用用户写入的 admission + namespace row lock + native CREATE/KV 同事务；扫描后 DDL/删除的并发变化由该入口重新核对。只在可写主库创建，备库接收复制的来源/CREATE；已创建的物理 tablet 继续由现有原生调度器接管。
- 不调用孩子的 Runtime 激活或 SchemaService。background_materialization_probe 已加入四件套：完全不登录孩子，父表 DROP，主表/索引/LOB 物化、接管后实际回收父来源，再首次登录验证值并 SIGKILL 重启。该验收尚待运行。
- background-materialization-unit.log 已通过 ASan/UBSan：8000 条有序续扫、路径 cap、每批边界、空树、读故障清空部分结果，及既有 200 轮随机 COW、读视图保护等测试。

Phase 18 验证推进：只读孩子完全不登录，主表/索引/LOB 共四个物理来源自动接管并实际回收，141.59 秒；首次登录正确，SIGKILL 后正确。SQL 读视图、来源根、退休来源重启回归通过。原生 KV 锁交接、期限及初始+三次 SIGKILL 恢复通过（background-lock-wait-native-retry.log）。

主备暴露三处具体缺口：MDS 拷贝误用 data snapshot=0，已改用 native MDS 的 flush/end SCN，最小 fullcopy 通过；KV 行锁 6005 被直接返回给 DDL，已在原事务期限内重试 LOCK，原生用例通过；恢复流程用已提交可读过滤获取物理 tablet，把 CREATE 状态尚待回放的对象跳过，永久残留 restore EMPTY，正在统一改为原生 READ_WITHOUT_CHECK 并复测。另有快速 compaction 期间源 SSTable 消失 4735 尚未解决，不能宣称初始物理拷贝一致性已完成。所有失败日志与 fixture 修正详见 background-materialization-evidence.json。

Phase 18 收尾：恢复发送/接收端统一物理获取方式，空壳明确无 SSTable；主备 namespace_fork_local 91.54 秒 PASS，覆盖最初拷贝、孩子读取/DDL、父 TRUNCATE/DROP、升主后写入及 fork。拷贝前父已 DROP 的最小用例 PASS；MDS 物理拷贝 PASS。快速合并源文件消失 4735 仍 OPEN，已把 --compaction-interval 3s 回归加入四件套，未用等待/吞错误掩盖。正式无 hook 编译和 diff --check 通过。仅提交生产代码。

Phase 18 提交 `00e421a8f` 已通过 gh credential 推送。

### Phase 19：模板初始化与访问属性（进行中，尚未提交）

删除 __template_build__ 清洗用户对象/重命名/补建链。模板正常注册轻量身份，allow_login 随 Namespace KV 创建事务持久化；SQL 登录在装载服务前拒绝，registry session admission 同样检查。普通 fork 不继承源的禁止登录属性。新增 ASan/UBSan admission 用例已 PASS。

初装复用来源树分批枚举、现有 materialize_source admission/native CREATE、原生 baseline DAG；等待所有 main/index/LOB 接管完成才结束既有安装 job。该 job 属于本机，不能把备库升主的新本机 job 当作首次创建 Namespace；已有模板只验证全部来源/本机 complete，不补建。启动不存在 template 时不从用户空间清洗重建。正常已完成安装重启仅切换 schema bootstrap 生命周期并装载目录。

第一次实际初始化失败 4002：核心内表 1 的 storage_schema.schema_version=0 是有效值，ObTabletForkParam::is_valid 却要求 >0，第一份模板核心 tablet 无法接管。已改 >=0，与 ObStorageSchema 校验一致，不加 ns/root 特判。失败原件 template-baseline-probe.log 和 namespace_fork_PROTOTYPE_template_baseline_lpfll2tw 保留；该进程因已有确定失败被主动终止，避免无效等 600 秒。重编译中。新增 template_baseline_probe、template_initial_failure_probe 到四件套；后者使用本地 hook，验证初装失败与重启都不补建/开放 SQL。

Phase 19 验证完成：模板初装 15.59 秒，358 个来源全部属于模板且已完成接管；禁止登录（4179）且无 SQL Runtime 激活；CREATE NAMESPACE、SIGKILL 重启后再创建空空间 PASS。初装注入失败、重启均不重建/不开放 SQL PASS。原生 KV 初始与三次 SIGKILL 恢复 PASS，包含 allow_login=false 往返及普通 fork 不继承禁止登录属性。主备 namespace_fork_local 116.24 秒 PASS；快速合并 3s fullcopy 本次 PASS，但源 SSTable 跨 RPC 保留竞态仍 OPEN。正式无 hook 编译 PASS，test hook 全部移除。

本地 obtest 原有 reboot 在 template 接管仍进行时（固定等 5s）便执行初始 SQL，首次失败；尝试 set(reboot=...) 未覆盖 Java public field，第二次仍 5s。最终新建 namespace_setup_template_local.test，展开相同部署步骤给主库 30s 初始化时间，保留原来的 namespace_setup_local.test 不变。第三次部署成功但缺少新测试对应的空 .result，补齐后全流程通过。以上 fixture 失败日志全部留存，非生产放宽准入。证据 template-baseline-evidence.json。

Phase 19 提交 `fb12cfa8dc62a4f03d0cdc410685389f89542db8` 已通过 gh credential 推送，ls-remote 确认远端分支一致。文档和测试未提交。

用户再次明确不要过度设计：已同步修正方案 10.5 的旧外排序要求，11.3 的暂缓决定适用于全文。GC 临时文件、外排序未实现。备库快速合并期间源 SSTable 消失仍是已复现的正确性问题，不能用重试或单次通过宣布解决；目前仅调查既有物理对象保护接口，尚未新增拷贝会话、持久化任务或其他生产机制。

### Phase 20：目录旧页自动回收（进行中）

2026-10-09。新增本地 automatic_catalog_gc_probe.py 并接入四件套 direct 门禁。Phase 19 正式二进制先跑出预期失败：RR 读者持有旧来源根，另一连接物化和发布 DDL 后等待 60 秒，旧根仍在当前页集合中，页数从 1219 增至 1507；日志 automatic-catalog-gc-red.log。没有手工触发 GC。

生产接入只复用 ObEmptyShellTask 的现有周期与 collect_metadata：每次最多删除 256 页，事务期限 2 秒；只在可写且安装完成后执行。原有目录事务 guard 排除并发发布，历史读视图依靠已有 KV snapshot lease。尚待正式编译与绿色测试，不宣称完成。

Phase 20 验证完成：修正为孩子独有旧根的同一用例，旧版等 60 秒仍未回收（页数 1221→1509），新版采样 1221/1221/1221/965 后旧根消失，RR 旧值、当前新值、父 Namespace 删除、SIGKILL 重启全部 PASS。SQL 读视图与 namespace_fork_local 主备定向回归 PASS。正式编译和 diff --check PASS；无测试 hook。

测试修正留存：第一版选中的 fork 共享根仍被父当前视图持有，自动回收正确保留它而将总页数降到 741；现先物化孩子 anchor，再持有独有旧根，重新跑红绿两版。主备命令首次遗漏 --case 而启动默认套件，basic 在旧 5 秒初始化等待后握手失败；已停止该 runner 及其所属进程，改用明确 namespace_fork_local 和既有模板初始化 fixture 后通过。详见 automatic-catalog-gc-evidence.json。备库源 SSTable 跨 RPC 消失问题仍未解决。

Phase 20 提交 `e5d715edf` 已通过 gh credential 推送。

### Phase 21：备库物理拷贝视图（实现/验证中，未提交）

`standby_background_copy_probe.py --drop-during-copy --compaction-interval 3s` 使用仅本地的 `standby_copy_pause_injection.py` 在取得全部 SSTable 清单后、指定父 tablet 的宏块范围 RPC 前暂停。主库物化孩子、DROP 父表、完成接管并将父来源转换为空壳后解除暂停，确定性报 `OB_SSTABLE_NOT_EXIST (-4735)`，日志 standby-copy-retired-during-red.log；不再只依赖快速合并的随机触发。

实现方向：已有 LS-view RPC 捕获本次物理视图，持有原生 ObTabletHandle；捕获期间经 host callback 复用物理回收/publication fence，确保未完成孩子的父来源也在同一清单中。后续 tablet/SSTable/range/block RPC 显式使用同一视图，不重新查当前 tablet。清单发送 VIEW_END 后保持原 RPC 到拷贝结束，断开时释放；仅内存 weak-map，不增加持久化会话、任务清单或临时文件。数据 RPC 保留原超时；LS-view 连接寿命覆盖拷贝，不再由清单发送时间限制。

已用本地 protoc 3.19.5 更新生成文件并消除生成器行尾空格。首轮缺少 VIEW_END 生成结果及 to_cstring 接口，次轮清理函数 LOG_WARN 宏隐式依赖 ret，均已修正并保留失败日志。第三轮带单一 pause hook 编译通过，修复用例和 peer SIGKILL 释放用例运行中。pause hook 已从生产源移除，正式清洁构建进行中。新增两用例已写入本地四件套；当前证据不代表修复已验证。

Phase 21 验证完成：确定性暂停后主库将源 tablet 变为空壳，备库仍完整复制并读到孩子数据；备库 SIGKILL 后主库释放 721 个物理句柄。拷贝前父表已删除场景 PASS，正式 namespace_fork_local 114.17 秒 PASS，最终无 hook 构建及 3 秒快速合并 fullcopy PASS。空壳不要求旧 fork 来源闭包；出错关闭流先取消再 Finish。构建与 fixture 失败均保留于 standby-copy-view-evidence.json，新用例已纳入本地四件套。仅提交生产代码。

Phase 21 提交 `934bd884a` 已推送，远端分支确认一致。

### Phase 22：删除旧 GC 入口与验收补齐（进行中）

- 删除生产路径已经没有调用者的 InstanceNamespaceDirectory::filter_unreferenced_tablets，共 93 行。旧读视图和未来弱读测试改为真实 tablet 创建事务身份，直接运行 load_physical_retention；不再通过虚构 ID 验证已经退役的路径。COW 页历史、读视图租约移交和释放的断言保留。
- 真实 SQL 8000 分区表创建 14.35 秒，连续三个孩子 fork 3.17/3.60/4.68 毫秒，均共享目录/定义根。各增加 Namespace/name 两条记录；第一/第三次观察新增页和物理对象为 0；第二次观测窗口与后台批次重叠，增加 23 页、30 个物理对象，仍共享出生根。单分区 UPDATE 22.67 毫秒，只改变一个用户来源，增加 2 页、1 个物理对象。证据 large-partition-fork.log，用例已纳入四件套。
- 当前正式二进制普通/全文/2048 维向量索引及重启、继承 IVF-PQ 缓存/查询、分区表 ADD COLUMN/索引/LOB/截断/删除与并发 DDL 均 PASS，见 final-source-index.log、final-source-ivf-pq.log、final-source-partition-ddl.log。
- ASan/UBSan 的 8000 来源、200 轮随机 COW、共享页/cap/循环/故障/预算/读视图生命周期 PASS；真实未来弱读保护/释放 PASS（retention-api-cleanup-unit.log、retention-api-cleanup-weak.log）。
- 原生崩溃恢复初次失败：durable fixture 要求重启后第一个来源 cap 必为 fork SCN；日志明确显示后台已经物化 Namespace 2 的该绑定单元。改为校验真实物理归属：孩子已物化来源 cap=0，继承来源 cap=fork SCN，继续核对持久化创建身份。保留 retention-api-cleanup-native.log，修正后正在执行完整四阶段恢复。
- 增加仅本地的 baseline_progress_injection：暂停备库指定 tablet 的接管调度，通过原生 fork_info 观察本机进度。测试主库先完成并回收父源、备库仍 incomplete，双方 SIGKILL 恢复后继续读，解除暂停后备库自行接管/GC 并升主写入。新用例及全文/向量父删除后的冷后台接管用例正在运行，均已加入四件套。

Phase 22 验证完成：原生 KV 四阶段 created/marked/finished/verified、三次 SIGKILL 恢复 PASS；独立主备 complete/来源保留/两端崩溃恢复/各自 GC/升主写入 PASS；未登录 Namespace 的 main/index/LOB/FTS/vector 共 17 个物理 tablet 在 148.56 秒完成接管并实际回收父来源，首次登录及 SIGKILL 后查询均正确，后台未激活 SQL Runtime。无 hook 正式构建、diff --check PASS。仅删除 2 个生产文件中 93 行旧 GC API，测试及文档保持本地。

Phase 22 提交 `c3f18b084` 已通过 gh credential 推送。工作区无生产代码未提交修改；原有用户测试修改和所有本地四件套材料保持未提交。

## 本轮结项（2026-10-09）

以 assessment-tablet-materialization-refactor.md §§10–11 明确的范围为准，来源树、事务发布、精确保留/GC、冷后台接管、模板和相关主备恢复改造已经完成。各阶段历史文件中的“未完成”是当时状态；Phase 21 关闭 fullcopy -4735，Phase 22 关闭剩余旧 API 清理与 8000 分区、关联对象、主备独立进度验证。

| 验收目标 | 证据 |
| --- | --- |
| 固定来源根、代次、父 DROP/TRUNCATE/多代恢复 | atomic-physical-drop-evidence.json、physical-gc-plan-evidence.json、source-roots-only-evidence.json |
| 真正 8000 分区 fork 共享根、单分区 COW | large-partition-fork.log（3 次 fork；2 页/1 tablet 的单分区更新）；retention-api-cleanup-unit.log（8000 条、32 MB 大条目、多层页与随机批处理） |
| DDL/物化 SQL + KV + 原生 CREATE/DELETE 原子性 | atomic-physical-drop-evidence.json、source-materialization-evidence.json；retention-api-cleanup-native-retry.log（3 次崩溃恢复） |
| 弱读/旧读视图、目录页回收、并发物理 GC 截点 | physical-retention-evidence.json、physical-gc-plan-evidence.json、automatic-catalog-gc-evidence.json、retention-api-cleanup-weak.log |
| 无关表历史版本推进、去掉全局 pin/旧 exception/父链权威 | physical-retention-mvcc-source-compaction.log、source-roots-only-evidence.json；旧 filter_unreferenced_tablets 已删除 |
| 冷孩子 main/index/LOB/全文/向量接管，父来源最终回收 | final-source-family-takeover.log：17 个 tablet，148.56 秒；后台无 SQL Runtime，首次登录及崩溃恢复正确 |
| 主备各自 complete、保护/回收、崩溃恢复、升主 | standby-baseline-progress.log；standby-copy-view-formal/results.json |
| 全量拷贝期间源变为空壳；异常断连释放句柄 | standby-copy-retired-during-red.log → standby-copy-retired-during-green.log；standby-copy-view-peer-loss.log |
| 模板正常 fork 后完整接管、禁止登录、失败不修复 | template-baseline-evidence.json |
| 正式构建、无测试 hook、代码-only 推送 | retention-api-cleanup-clean-build.log，934bd884a + c3f18b084 |

所有新增用例及本轮遇到失败的回归已写入本地四件套。运行的是本轮所需的定向用例及原生故障/恢复门禁；没有运行完整 mysqltest/sysbench，也没有重新全跑四件套。没有 PR，测试和文档没有提交。

保留用户已同意暂缓的边界：来源 KV 热路径缓存、GC 超过 262144 项时的资源扩展、首次读物化策略、全局 freeze/schema 历史方案、其他实例内表/配置重构。只读孩子的后台接管会逐渐创建其物理 tablet；当前存在来源 KV 查询开销；超预算的 GC 计划会安全失败，可能不能推进回收。本轮没有增加临时文件、外排序或独立任务日志。
