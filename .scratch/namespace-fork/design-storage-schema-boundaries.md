# 物理 Schema 历史：晚物化与逻辑校验边界

日期：2026-10-09

核对基线：`44b6c98ab`，分支 `codex/namespace-worker-proxy-v20`。

状态：设计决定，尚未实现、编译或动态验证。本文取代旧方案中“每 tablet 保存完整 MDS 布局历史”、fork 初始时间未定及 freeze.schema_version 依赖未定的部分。旧方案的 DDL 入口收敛清单仍可用，但不是当前代码已经实现这些能力的证明。

修订：根据用户对 DDL 负担的质疑，撤销上一版新增 CatalogPublication 记录及为它扩大 Namespace 内元数据事务串行范围的决定。逻辑校验改用 G@F 已携带的表级 schema_version，复用现有单表历史定义读取；不构造整份目录在 F 的版本。

## 1. 已确定的整体结构

- SQL 目录继续归所属 Namespace；schema ID 不编码 Namespace。
- 一个专用内部元数据 tablet 复用 InstanceMetaStore、原生事务与 MVCC。一个物理表对象 G 一条布局 key，同一表的分区共享 G；G 是稳定对象身份，布局版本随 DDL 改变。
- DDL 在同一个原生事务中修改 SQL 目录、已有目录发布信息、受影响对象的完整 ObStorageSchema 和相应物理对象元数据。版本号标识定义，提交 SCN 决定可见性。不另加一份目录发布记录。
- 物理合并输入为物理对象与目标 F，通过 `read_layout(G, F)` 获得布局，随后固定到已有 medium_info 中。存储层不解析 Namespace，也不反查 SQL SchemaService。
- mini/minor 复用原生多版本保留能力；完整历史在磁盘，按需装载。已有 tablet storage_schema 的当前/基线描述仍需与新发布入口统一角色，不能继续作为第二个独立发布源。
- 不新增每轮 freeze 的 Namespace 版本向量、全实例版本发号器或常驻历史 schema 缓存。

下文接口、字段名均为拟议名称。

## 2. 问题一：必须分开的时间和身份

| 符号 | 含义 | 决定什么 |
| --- | --- | --- |
| S | 继承源数据的快照上限 | 子对象最多看到父数据的哪个时刻 |
| C | 这个物理 tablet 创建事务的真实提交 SCN | 这个物理对象能否参加目标 F 的合并 |
| F | 当前合并目标快照 | 合并输入的可见性、布局读取时刻 |
| G | 所属物理表的布局对象身份 | 到哪条 MVCC 记录读取布局 |
| `(tablet_id, create_transaction_id)` | 物理对象的 incarnation | 防止把同 ID 的后继对象当成旧对象 |

当前物化路径把 `logical_birth` 设置为源 cap；`PROTOTYPE_MATERIALIZE_TABLET::on_commit()` 保留该 create_commit_version_，只写 create_commit_scn_。后者是提交日志 SCN，不是事务可见性 SCN。create_transaction_id_ 是身份，也不是时间。

**决定：在原生创建/删除 MDS 数据中增加并持久化真实的 physical_create_version，所有创建路径都在 on_commit(commit_version, commit_scn) 中记录 commit_version。** 同步覆盖 assign/reset/serialize/checkpoint/恢复；保留原有逻辑可见性字段的含义。不得用日志 SCN、事务 ID 或继承 cap 代替 C。

依据：`src/storage/tablet/ob_tablet_create_delete_mds_user_data.cpp:114`；物化入参见 `src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp:1598`。

## 3. 合并成员资格：统一按 C 与 F 判断

同一规则适用于普通创建、fork 物化、隐藏表创建和物理重建，不按 Namespace 编号分支。

| 对象状态 | 对 F 这一轮的处理 |
| --- | --- |
| 尚未创建物理 tablet | 不属于物理候选集合；不为本轮强制物化 |
| 创建尚未提交、状态不可判定 | 水位尚未到F时重试；水位已到F且确认仍未提交的创建不属于本轮。读取状态失败不能当作未提交或对象不存在 |
| 已提交，C > F | 明确“不属于本轮”，不查 G@F，也不等待接管 |
| 已提交，C <= F，继承基线不完整 | 本轮待完成，继续既有接管调度；不能用临时跳过报告完成 |
| 已提交，C <= F，基线完整，但 major 尚未到 F | 取 G@F 并调度合并 |
| 已提交，C <= F，major 已达到本轮要求 | 按现有合并结果与校验状态报告；不要求同时删除旧版本 |
| 对象已提交删除 | 根据明确的物理删除状态退出后续调度，已存在任务按原生取消/收尾规则处理；数据 GC 仍服从 fork、读者和文件引用 |

“不属于本轮”须是显式结果，不能伪造 snapshot_version=F、major SSTable 或 checksum。调度器、SQLite tablet 进度报告、rootserver 汇总和 checksum 输入筛选必须采用同一 incarnation/C 判定。正常轮次只把 F 之后的目标用于 C>F 的对象。选取后续目标时，也要跳过冻结记录中所有小于 C 的旧 F，不能因继承基线为 S 而再次选中它们。

### 3.1 枚举与并发创建

1. 先确认原生提交/回放的可读水位已到 F。
2. 再构造物理 tablet 候选迭代器并检查创建状态。
3. 不能复用水位尚未到 F 时已构造的候选快照来宣布完成；需重新枚举。
4. 水位到 F 后才发生的新创建，其真实 C 必须大于 F，不会向本轮补入成员。
5. 已提交删除按 incarnation 确认；“取不到一个对象”不能无条件作为完成证据。

复用原生可读水位和按轮次的临时候选列表，不新增持久化的“每轮全部 tablet 清单”。当前迭代器及完成统计没有完整实现这条协议，需要一起修改。

### 3.2 晚物化示例

```text
80：孩子继承父数据，S=80
120：产生 freeze F=120，孩子的物理 Q 尚不存在
150：创建并提交孩子的 Q，C=150
200：后续 freeze
```

- Q 不参加 120；所以不存在“必须查询一个到 150 才创建的 G 在 120 的版本”的要求。
- Q 可以在 150 后正常读写，继续通过父数据@80与本地增量工作。
- Q 参加 200；若接管未完成则等待，完成后用子对象 G@200 合并。
- 父物理对象独立按其身份参与维护。源数据@80仍由已有持久 source 引用保护，排除 Q 的旧轮次不会解除这个引用。

物化和接管仍然分开；不为确定 C 或本轮资格而同步接管全部父数据。

## 4. G 的创建、发布与 fork 布局

1. 普通表的初始 G、完整布局与首批物理 tablet 同事务创建。
2. fork 仍共享不可变目录根/定义，不在 fork 时枚举所有表创建 G，也不创建 8000 个物理 tablet。
3. 子表第一次需要独立发布布局（DDL 或物化）时，分配这个子物理分支的 G。初值来自子目录当时的确定定义，不读取父 Namespace 的“当前最新定义”。
4. G 的首次写入、对子表的绑定及需要创建的 Q 处于同一个原生事务。该表后续分区物化复用 G，不重新覆盖已有 G 的布局。
5. 若孩子在物化前已经 DDL，则 G 可以早于 Q 存在；物化在受序列化的目录事务中使用当前已发布定义，并保持 G 一致。父亲后续 DDL 仅发布父 G。
6. 每次布局更新使用其真实提交 SCN；不把初始记录伪写成 S/F。对参与合并的 Q，必须满足 G 的有效创建时间不晚于 C，且 C<=F。
7. 接管用源 SSTable 自身描述读取输入，用目标分支已确定的布局描述安装/生成目标基线。父、子 G 的局部版本号不做 MAX/MIN；现有基线安装中按裸 schema_version 取新旧的路径须核对并改为明确的布局归属。
8. G 绑定对同一物理 incarnation 稳定。需要更换物理对象的重建创建新 incarnation，并明确新 G/绑定；普通加列不改 8000 个 tablet 的 G。

删除不能立刻删除 G：当前/历史物理对象、已固定任务、仍使用的基线描述及 fork 创建描述的生命周期需覆盖其引用。已保存的完整创建描述允许未来冷物化初始化子 G，不要求永久保留一个已经删除的父 G 的所有 MVCC 版本。

## 5. 问题二：freeze schema_version 的代码审计

检索覆盖 ObFreezeInfo、SchemaVersionInfo 的引用、schema_version 字段读取，以及相关 helper 的调用方。以下为当前源码证据，不表示这些路径都已动态执行。

| 路径 | 当前用途 | 目标处理 |
| --- | --- | --- |
| `ObMajorMergeInfoManager::set_freeze_info/get_schema_version`，cpp:129/239 | 从绑定的根 SchemaService 取得一个版本，写入 freeze | 完成消费者切换后 freeze 只发布 F、数据格式版本等实例事实，不再生成全局 SQL schema 版本 |
| `ObMediumCompactionScheduleFunc::find_valid_freeze_info/choose_major_snapshot`，cpp:93/172 | 比较 freeze 版本与上个 SSTable 版本；查询 Namespace 表定义；失败时尝试其他 freeze | 用 incarnation/C 确定资格，再取 G@F；删除跨对象版本比较、MIN 截断及 schema 缺失时的最新版本替代 |
| 同文件 `prepare_medium_info/get_table_schema_to_merge`，cpp:852/926 | 非全局 medium 也共用 SQL schema 入口 | 对该任务自己的目标 B 取布局；B 必须不早于 C 且受保护，不照搬全局 F |
| `ObBasicTabletMergeCtx::get_meta_compaction_info`，cpp:1237 | meta major 间接调用同一 SQL schema helper | 使用被整理基线的确定布局；保留/引用其已持久化完整描述，不能把最新 G 布局静默标成旧基线布局 |
| `ObMajorMergeProgressChecker::check_schema_version`，cpp:396 | 等根 schema 缓存达到 freeze.version；随后按根逻辑目录枚举和检查 | 物理完成统计改用共同的 incarnation/C/F 规则；逻辑校验按每个G@F自带的表级版本读取其所属目录的单表定义 |
| `ObTableCkmItems::check_schema_change_after_major_freeze`，cpp:566 | checksum 不一致后用 freeze.version 查询历史目录，判断索引/分区是否变化 | 主表、索引分别用自身G@F的版本取得定义与绑定；校验必须先固定可比对象，不能用分区数量变化概括所有变化 |
| `ObMajorMergeScheduler::check_namespace_progress`，cpp:403 | 根 checker 之外遍历非根 Namespace，加载 runtime，仅检查 owned tablet snapshot | 删除根/子两套进度规则；不把该检查当作子空间已经做了完整索引校验 |
| `ObTabletScheduler::get_min_dependent_schema_version`，cpp:1091 | 暴露全局 schema 回收版本 | 仓内仅声明/定义、未发现调用方；删除旧接口，不能声称现有 schema GC 已消费它 |
| `ObFreezeInfoProxy::get_freeze_schema_info`，cpp:370 | 以 F 查询唯一 schema_version | 仓内仅声明/定义、未发现调用方；删除，而非新增隐式 ns1 版本 |
| `ObFreezeInfo`、proxy、内表 schema | 字段有效性、序列化、SQL读写 | 与最终消费者删除同步清理；本版本不做升级兼容，不能用常量1占位假装有效 |

其他 ObFreezeInfo 使用者读取的是 F、data_version、快照保留或诊断信息，不因此改成 Namespace 服务。`ObITable::get_frozen_schema_version()` 返回各表自身描述版本，不等同于全局 freeze 字段，不能按名字全删。

## 6. 逻辑校验复用表级布局版本，不新增目录发布记录

ObStorageSchema 不包含完整主表/索引关系、索引状态、逻辑列身份和分区绑定。继续使用已有 Namespace SQL 目录的历史读取能力，不把这些逻辑关系塞入存储层，也不另写一套 checksum schema 引擎。

### 6.1 已有信息足够定位单表历史

当前 DDL 已在原生事务结束前更新 `normal_schema_version`，并通过 `NamespaceSchemaPublication::stage()` 更新 KV Namespace 记录的 `roots.schema_version`。再写一条同义发布记录会增加日志、行锁和历史保留负担。直接读取已有整个目录记录@F也不是免费替代：还要保护该记录所在的通用 InstanceMetaStore tablet 的历史。

本问题无需整份目录在 F 的版本。`ObStorageSchema::init` 已从输入 ObTableSchema 保存该表自身的 schema_version（`src/storage/ob_storage_schema.cpp:1085`）。统一物理发布入口应保留这个来源，禁止把它改成全Namespace的END_SIGN版本或无关的局部计数。

```text
主表：read_layout(G_data, F)  -> 主表版本 V_data
索引：read_layout(G_index, F) -> 索引版本 V_index

read_table_definition(owner_catalog, data_table_id, V_data)
read_table_definition(owner_catalog, index_table_id, V_index)
```

单表历史读取已有基础：`ObSchemaServiceSQLImpl::get_table_schema/get_not_core_table_schema` 接收 table_id/schema_version，按版本读取表、列、分区；`namespace_schema_publication.cpp::publication_schema` 已经使用这个接口。需要让 checksum 消费单表定义，替换要求一个全目录版本的 guard 使用方式；不是把 V_data 当作整个目录版本继续传给所有表。

owner_catalog 来自上层已确定的物理对象归属，存储只读取G。主表和索引的局部版本可以不同，不相互比较，不用一个表的版本查询另一个表。相关索引/辅助对象从适用的物理G集合及各自历史定义中的归属关系构建，不能按主表V批量推定全部辅助表的状态。

### 6.2 一致性来自每个对象的同事务发布

- 同一个对象的目录定义版本 V 与该次完整布局原子提交；取得 G@F 后，V就是这个对象在F时刻可见定义的身份。
- 同事务改变主表和索引时，它们的布局及目录一起可见；不同对象的事务可以并行，不需要把它们压成一个目录前缀版本。
- 保留已有同对象DDL并发控制和并行任务提交顺序；不为此次读取增加整Namespace的串行锁。实施必须核对接口收敛后，同对象一个版本确实对应完整、不可变的表定义。
- 所有影响该物理表逻辑定义/校验关系的表版本变化都经过统一发布入口。布局字节未变但表版本改变时仍发布该对象版本，不能按内容去重漏掉逻辑变化。无物理对象的用户/权限等DDL不因这个方案额外发布布局。
- G 尚未存在/物理对象C>F时不捏造旧布局，按第3、7节处理。正常读到布局但所指的单表历史缺失应报错/重试，不能缩小V或改读最新定义。

这样不需要新增 CatalogPublication，不扩大 Namespace 内元数据事务串行范围，也不在 freeze 时枚举Namespace收集版本。上一版宣称必须增加这些措施，是把“校验几张确定表”扩大成“还原整份目录快照”造成的多余设计。

### 6.3 逻辑历史的保留

表版本 V 的 SQL `_history` 行是显式逻辑历史行；保护 G 的 MVCC 版本不会自动保护这些逻辑行。目录历史 DELETE 必须保留未结束校验/活跃读者引用的各对象 V，以及重建该对象V所需的列、分区等前驱版本。可从尚需保留的布局/已固定任务确定引用；不能从一个全局版本号推测所有Namespace的边界。

本次源码检索未发现 `get_min_dependent_schema_version` 被回收器调用，不能把一个未接线的 getter 当作现有保护。实施时将此约束放在统一目录历史删除入口；没有历史删除路径时保持这些行，不为本任务新造后台回收线程。

物理 merge 和进度枚举不再需要 Namespace runtime。**逻辑索引校验仍实际需要目录定义，复用 SchemaService 时可能加载所属 runtime。** 这项成本与其逻辑归属明确保留；不承诺整个后台永不加载 Namespace，也不为了此次改造复制一套轻量 SchemaService。

## 7. checksum 的对象集合和结果语义

1. 一个校验组固定 F，各对象分别固定 `(catalog_id, table_id, G, V)`；它们的V均来自自身G@F。主表/索引的列和分区取自身版本，不要求各对象V相等。缓存 key 至少包含 catalog_id，不能跨 Namespace 只用 table_id。
2. 每个输入 checksum 必须匹配 `(physical tablet incarnation, F, layout identity)`。物化或重建后同 ID 的新对象不能补旧对象的 checksum；需要把 incarnation/layout 身份接入现有本机 checksum/进度数据或查询核对。
3. 先确认组完整、身份匹配，再做 local 分区配对或 global 求和；不能只比 tablet 数量/分区数。数量相同的分区替换也属于绑定变化。
4. F 之后创建/才变为可读的索引不加入 F 的校验组。F 后已提交的 DROP/TRUNCATE/物理替换如使旧组无法继续取得输入，记录为“对象已被 DDL 替换，本轮不再适用”，不能记录 CHECKSUM_PASS，也不能仅因某个版本不同而吞掉已成立的 checksum 错误。
5. 对属于本轮、仍存在且可比的完整组，checksum 不一致必须报错；缺失的必需 checksum 是待完成/错误，不能当删除处理。

### 7.1 尚有继承分区的孩子

父 P@S 的数据不等于 P@F；不能拿父 F 的 checksum 填孩子的缺项。更不能只求和孩子已物化的少数分区，就声称整张表/全局索引已校验。

本方案不为 checksum 强制物化全部冷分区。只有本轮具备完整本地物理输入的组执行完整跨表比较；仍含继承输入、或 C>F 的组记录明确的“本轮不具备完整比较集合”。该状态允许物理合并轮次收尾，但不计为逻辑校验通过，诊断中保留覆盖情况。后续物化并在适用轮次形成完整输入后再比较。

这是明确的校验覆盖限制；如果未来要求冷 fork 的每个分区也每轮完整校验，需要单独增加源 S 的 checksum/按 S 数据校验能力，不能靠物理布局历史宣称已经做到。磁盘块校验、实际执行合并的校验和已有源引用保护继续执行。

## 8. 保留、恢复与无循环等待

- 新 F 的保护必须在回收可能越过 F 前建立。生成 F 的短窗口由原生读快照注册覆盖，freeze 记录提交后交接给持久任务状态推导出的保护，交接无空隙。
- 同一个 schema tablet 的保留输入包含：未结束 freeze/逻辑校验需要的 F、正在准备布局的读者、未固定布局的其他合并目标。对已持久化完整布局的 medium 任务，执行和恢复复用该布局，不再次依赖旧 SQL schema。
- 重启先恢复 tablet/SSTable/日志，禁止推进回收边界；读取 freeze 记录与持久合并完成状态，恢复最早仍需要的 F，再开放推进。多轮积压取最早尚需的目标，不只取最新广播。
- 当前 freeze 记录在 seekdb `__all_freeze_info`，合并状态在本机 SQLite `__all_merge_info`。此次复用它们；不借此把实例私有存储改造一起纳入。
- 已完成物理合并但逻辑校验尚需 G@F及各对象V 时，不提前释放所需历史。全局结束状态持久化后才解除这一轮要求；其余读者继续保护自己的快照。
- 新 schema tablet 沿用内部元数据 tablet 的转储/minor 维护，不参加用户 tablet 的全局 major 完成集合；保留历史不要求停止它自己的转储和文件整理。
- 非 freeze 的 medium 在准备时选择不早于 C、且仍受保护的 B；保护后读取并固定布局再提交任务。读取已回收的任意旧 B 必须失败，不使用最新布局替代。
- meta major 复用被整理基线的已固定完整布局；完整布局必须随基线持久保存/引用到基线退休，不能仅保留一个将来可能已回收的版本号。具体沿用 tablet 当前持久化描述的改造纳入合并入口切换验收。

## 9. 实施拆分和删除范围

1. 原生 MDS 增加真实 C，统一资格判断，修正水位与枚举顺序及本机报告身份；先把晚创建的对象从旧 F 中明确排除。
2. 建立专用元数据 tablet、共享 G；与现有DDL原生事务共用，保持该对象布局与表级定义版本一致，不新增目录发布小记录或Namespace串行锁。
3. 接入 schema tablet MVCC 保留/启动恢复，补齐 SQL 目录历史删除约束；不把保护句柄内存 map 当作持久任务状态。
4. major/medium/meta 的完整布局来源收敛；复用 medium_info 的日志及恢复。检查基线安装不跨 G 比较局部版本。
5. 物理进度汇总与逻辑校验分清输入：删除根 checker + 子 Namespace 特供进度遍历；逻辑校验使用显式所属目录及每个对象从G@F取得的V。
6. 切换全部消费者后删除 freeze.schema_version、两个无调用接口及其结构/SQL读写依赖。全局 DDL/freeze 锁只有在所有消费者、同对象DDL原子发布及快照保护验证完成后才能解除；不以本次设计为由撤掉已有其他必要的DDL并发控制。

不增加全实例停写，不将 table/schema_id 编码 Namespace，不在正常读写中创建8000个布局历史副本，不新增长期全局缓存。本轮不处理所有实例内表归属或本机 SQLite 替换。

## 10. 验证矩阵（待实现后加入四件套）

本次只有代码静态核对和文档变更。以下均为待执行用例，不能记 PASS。

| 用例 | 必须断言 |
| --- | --- |
| S=80、F=120、C=150 | Q 明确不属于120；不读 G@120、不阻塞旧轮；后续轮使用子 G |
| C<F、C=F、C>F | 精确包含<=；未提交/回放未到F不误报完成 |
| 水位到F前构造列表、随后创建完成 | 水位到F后重新枚举，不能漏掉C<=F对象 |
| C<=F而接管未完成 | 本轮仍待完成，持续接管；不能当成C>F跳过 |
| 父DDL、子DDL、后物化、接管并发 | 父子G独立；初始化不覆盖已发布子G；不跨G比较版本号 |
| 8000分区 | fork不遍历发布G；布局正文按表发布；后续分区复用G；记录现有仍需枚举的操作 |
| 两Namespace相同table_id/版本 | 合并按G隔离；逻辑校验按catalog_id隔离 |
| 两表DDL版本分配与提交顺序交错 | 各自G@F选择正确的表级V；不能用一个对象V查询另一个对象；无需扩大Namespace串行范围 |
| freeze与DDL交错、记录交接时强制退出 | F前后的布局/目录原子可见，恢复保护先于回收推进 |
| 转储/minor、任务日志落盘、重启 | 旧F可读；已固定medium布局可独立恢复；缺失历史明确报错 |
| DROP/TRUNCATE/等数量分区替换 | 校验按实际绑定和incarnation判定，不能只比数量；被替换不记PASS |
| 部分分区物化/继承、local/global索引 | 不使用父F代替父S；部分求和不算完整校验；可比组错误仍报错 |
| Namespace创建/删除跨F | 物理对象C、上层归属及适用校验集合与F匹配；不复用旧catalog身份 |

沿用 `.scratch/namespace-fork/run_four_gates.py`，实际运行过的用例及曾失败的触发条件都加入；不运行完整 mysqltest/sysbench。代码、文档和测试可按用户后续授权一起提交作备份；只推分支、不创建 PR。

## 11. 本次闭合的结论及实际代价

两项设计决定已写明：晚物化按真实物理创建时间决定轮次；逻辑校验复用G@F已有的表级schema_version取得所属Namespace的单表历史定义。对应消费者、恢复要求、覆盖限制及实施入口都有明确归属。

实际新增成本为物理创建提交时间字段和已决定的每表分支布局历史；逻辑校验的版本定位不再额外增加每目录写入或Namespace串行锁。发布完整布局本身有序列化与日志成本，不能称为零成本，但不是每分区复制历史。逻辑校验依旧需要所属目录，冷继承对象不自动获得完整跨表checksum覆盖。以上不是“实现已完成”或“全部测试已通过”的声明；其他 DDL 入口与 mini/minor 布局兼容性仍按整体方案逐路径实施验收。
