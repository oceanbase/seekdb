# fork Namespace 重构后的备库适配

日期：2026-10-03  
核对基线：`a11059085249f624c0a69ad7bce5543f954f62ab`  
状态：**实施中，2026-10-03 用户明确要求完成适配并跑通全部 `t/stanby` 用例，撤销此前 HOLD**。过程、失败证据和验收清单见 `standby-adaptation-progress.md`。本文及测试留在本地，不进入代码提交。

## 1. 目标与结论

继续使用 seekdb 唯一 LS 的物理基线复制和日志回放。Namespace、fork、物化的持久事实都进入该 LS；备库根据回放后的事实建立自己的 Namespace 运行状态。没有发现必须改为每 Namespace 一个 LS 或一条复制流的架构限制，但当前实现还不能据此宣称 fork Namespace 已支持备库。

本轮重构保留了两个有利条件：

1. 未物化孩子的读仍可引用父物理 tablet，受 fork SCN 上限约束。备库第一次读取孩子不必创建物理 tablet 或写入映射。
2. 主/LOB tablet 创建、tablet 映射及 InstanceMetaStore 中 owned 记录已共用一个原生内部事务。备库沿用原生事务/MDS 回放语义；在提交可见位置应同时看到这些结果。这个内部事务与用户 DML 事务仍分开。

工作集中在元数据装载、运行状态刷新、物理历史保留和角色切换。存储回放处理物理 tablet ID、事务与 SCN；Namespace 的解释放在 observer / Namespace 元数据模块。

## 2. 哪些复制，哪些本地重建

| 内容 | 处理方式 |
| --- | --- |
| 用户数据、各 Namespace 的 SQL 对象元数据和历史 | 同一 LS 的物理复制、事务回放 |
| InstanceMetaStore 内的 Namespace 名称/ID、父链、fork SCN、映射、删除状态、snapshot pin、schema 发布状态 | KV tablet 的物理复制、事务回放；无须逐 collection 编写同步程序 |
| 物理 tablet 创建事实、初始 fork 来源、MDS、事务提交状态 | 原生物理创建与事务回放，保留主库物理 ID |
| Namespace 注册表、SchemaService 的内存内容、映射/状态缓存、任务执行对象 | 从本机回放后的持久事实装载或刷新 |
| 活跃请求计数、持有的 tablet handle、DAG 内存状态 | 各节点独立维护 |
| 转储/合并结果、基线接管完成状态、本机 SSTable 地址和 slog | 各节点独立持久化；初始建备可复制物理基线，持续 LS 日志复制不等于复制每次本机合并结果 |
| 本机角色、复制源连接等本机私有状态 | 沿用本机存储，不因 Namespace 改造放进复制 KV |

SQL 对象的 table_id/schema_id 不增加 Namespace 编码。备库沿用主库已确定的物理 tablet ID，也不会为复制过来的 Namespace 重新分配 ID。

## 3. 已核实的代码缺口

以下位置均相对于上述基线，行号会随修改变化。

### 3.1 只读启动与写入初始化耦合

- `src/observer/ob_system_package_load_task.cpp:88` 的系统包加载流程负责恢复 Namespace 注册表并设置 `sys_package_ready_`；定时入口在不可写时跳过。
- `src/observer/namespace_template_registry.ipp:249` 的 `restore_namespace_registry()` 读取存活 Namespace 后，还会调用 `ensure_template_namespace()`。它也不承担持续删除/改名的核对。
- `src/observer/mysql/obsm_conn_callback.cpp:88` 的远程握手等待 `sys_package_ready_`。
- `src/standby/standby_module.cpp:463` 的备库 `wait_metadata_ready()` 只启动时区管理器。

需要将“从已提交数据装载本机运行状态”与“系统包安装、模板创建、持久状态修复”拆开。备库接入就绪以装载完成为条件，不能要求在备库执行这些初始化写入。

### 3.2 子 Namespace 的 schema 装载可能写入

`src/observer/namespace_worker_inprocess_prototype.ipp:966` 的初次 schema 装载调用 `begin_schema_recovery()`，必要时发布 schema delta、调用 `finish_schema_recovery()`，随后恢复 DDL 任务。

需要提供纯装载流程，供主备共同使用；持久状态修复与可写 DDL 任务恢复由主库恢复阶段显式调用。不能在备库登录时把尚在主库执行的 DDL 当作遗留事务清理。

### 3.3 持续刷新和升主重置只覆盖初始 Namespace

`src/observer/ob_server.cpp:157` 的 standby host 只重置 Namespace 1 的 max-ID cache；`:165` 的 schema refresh 只走 home schema/proxy。现有刷新定时器可以复用，但其宿主适配必须覆盖所属 Namespace。

此外，当前 Namespace 状态缓存、exception 缓存和 tablet→table 缓存有本地操作后的更新/失效逻辑。物理回放不会自动执行 SQL FORK、DDL、物化代码里的进程内回调。因此“日志已回放”并不自动意味着这些缓存已更新。

### 3.4 历史保留已有钩子，备库时序未闭合

- `src/observer/omt/ob_server_runtime_controller.cpp:1534` 已为 FreezeInfoMgr 注入 KV snapshot pin loader。
- `src/storage/compaction/ob_freeze_info_mgr.cpp:489` 依次读取 SQL freeze/GC 水位、SQL snapshot 和 KV pin，目前不是一次显式绑定快照的读取。
- `src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp:719` 已根据本机未完成 fork 的 source 链保留物理 tablet；回收还会排除本机正在访问的物理对象。

需要核对备库启动、持续回放及本机合并进度不同情况下的完整时序。现有本机互斥锁不自动约束回放中的元数据变化，不能直接把主库调用路径上的锁当成备库保护证明。

## 4. 目标流程

```text
主库：Namespace SQL / fork / DDL / 物化
                  │
        同一 LS 的原生事务日志
                  │
备库：物理恢复 + 日志回放
                  │
        事务与 MDS 已安全可读的位置
                  │
        Namespace 元数据装载 / 核对
          ├─ 注册表及 Namespace 状态
          ├─ 所属 schema 与物理映射
          └─ 历史依赖和 snapshot pin
                  │
        开放相应 Namespace 的只读请求
```

LS 提供安全可读进度；Namespace 元数据模块负责把进度转换为可服务状态。沿用 Registry 和各 Namespace 已有服务，不创建额外的实例管理 NamespaceRuntime，也不按所有表/分区常驻复制 schema。

### 4.1 首次建备及重启

1. 物理恢复或本地恢复 LS，然后回放到启动要求的位置。
2. 确认存在事务安全的可读快照；读取 InstanceMetaStore，恢复 Namespace 清单及历史依赖。
3. 安装历史保留约束后，允许相应回收/压缩水位推进。
4. 以只读模式装载所需 Namespace schema，核对其已发布版本与映射相容。
5. 发布 Namespace 接入就绪。完整服务和 schema 仍按需激活；不能仅因为全局“日志就绪”就允许尚未装载的 Namespace 被使用。

当前 `ObStandbyTimestampProvider` 从 LS weak-read snapshot 获取时间戳，可作为复用基础。不能将“收到日志的最高 SCN”直接当作安全可读位置。

### 4.2 在线追随 Namespace 和 schema 变化

对新增、fork、删除、改名、DDL 和物化后的映射变化，都要将持久事实反映到本地运行状态：

- 新增：注册可发现的 Namespace，按需装载服务。
- 删除：禁止新请求进入对应 Namespace；本机在途请求与物理依赖按已有引用规则释放。
- 改名：更新名称索引，身份仍是原 Namespace ID。
- DDL：刷新所属 schema，核对发布状态后使相关计划/映射失效。
- 物化：即使 schema 版本未变，也必须使旧的继承映射失效。

统一复用元数据刷新入口，输入来自本地提交及回放后进度。第一步可复用现有刷新定时器核对持久状态；具体变更检测粒度在实施时确定，不能只比较 schema_version，也不能漏掉旧缓存的失效。延迟刷新的进程缓存不能独立作为删除权限或映射有效性的权威；请求使用缓存前须满足已发布进度/状态的校验约定。

实例级 Namespace 清单的核对由一个模块完成，这是清单本身的归属；各 Namespace 的 schema 装载、任务和缓存维护留在所属服务中。LS 回放线程不遍历 Namespace 执行业务任务。

### 4.3 元数据与 schema 的一致性

应以一个可固定的安全读快照为基础，读取 Namespace 控制记录，并装载与其发布状态相容的 SQL schema。逻辑版本属于各 Namespace，不要求不同 Namespace 的 schema_version 数字相等。

`InstanceMetaStore` 当前保存事务内读快照，但公开 `begin()` 尚不接受调用方指定 SCN。实施时需要补足共享读快照/指定快照的装载能力，或证明既有版本校验能达到相同一致性；不能简单连续读取两个“最新值”。

还必须处理 DDL 分阶段发布：当前 `active_schema_changes`、`pending_schema_version` 与最终目录/schema 发布构成恢复协议，并非所有 DDL 的 SQL 元数据与 KV 映射都已同事务提交。即使取相同 SCN，也可能落在发布中间阶段。

处理原则：

1. 常规追随时只发布相容视图；未完成发布时，继续使用仍有效且受保护的旧视图，或对受影响请求返回可重试结果。
2. 备库不主动清除正在回放的 DDL 发布标记。
3. 主库失效留下的发布中间态，在升主封定回放边界之后，由主库恢复流程收尾，再开放相关写入。

此项需要明确失败行为与进度观测，不能靠无限重试掩盖永久未发布状态，也不能让一个 Namespace 的 DDL 默认堵住全部 Namespace。

## 5. 物化、基线接管与回收

### 5.1 继承读与物化回放

例：主库 fork A→B，B 从未写入表 T；备库回放后可从 B 的逻辑 tablet 解析到备库本地 A 的物理 tablet，并按 fork cap 读取。这个读不向主库发送物化请求，也不在备库创建 B 的 tablet。

主库后来物化 B.T 时，备库回放同一个原生内部事务。提交可见后才将对应 owned/mapping 用于读取；没有 commit 的 redo 不应提前成为可用对象。物化已提交而用户 DML 未提交时，允许出现有自有物理 tablet、没有用户新行的状态。

当前 `ObLSTxService::activate()` 已在本地追加日志激活前回滚无最终决定的回放事务。备库适配要验证它只发生在恢复结束/升主边界，不在持续追随时取消尚等待主库 commit 的事务。

### 5.2 主备独立完成基线接管

`src/storage/tablet/ob_tablet.cpp:570` 在 fork table-store 安装时设置 `fork_info.complete`；`src/storage/ls/ob_ls_tablet_service.cpp:2857` 经本机存储元数据持久化及 CAS 发布。这种完成状态不能当作另一台机器已经建立了相同 SSTable 的证明。

例：

```text
主库 B 已接管 A 的基线，主库可以满足解除依赖的条件。
备库 B 尚未接管，读 B 仍会用到备库 A。
```

备库必须保留本机 A 的物理对象及必要快照数据，直到本机 B 完成接管，且其他持久引用和本机活跃引用也已解除。主备不要求同一时刻接管，也不要求 SSTable 文件一一对应。

备库允许对已有物理 tablet 做本机转储、合并和基线接管；这些操作不应创建新的逻辑归属或修改复制过来的控制记录。用于读取源数据的 fork cap 与 storage schema 要按现有物理规则保留。

### 5.3 分开保留“对象”和“数据版本”

- 物理 tablet 的存在：由复制过来的继承关系、本机未完成 fork 来源链及本机活跃请求共同约束。
- 数据历史：由 snapshot pin、本机尚未完成的基线需求及本机有效读快照约束。
- InstanceMetaStore 自身的历史：还要满足父链解析和活动元数据事务的读取需求。

仅保留 tablet 空壳不能保住历史行；仅同步 snapshot pin 也不能保证源物理对象存在。GC/合并所用水位与依赖必须来自相容的回放状态；在依赖装载失败或未就绪时，不推进受影响的破坏性回收。

逻辑删除/持久控制记录清理由主库提交并复制；每台机器何时释放物理对象、SSTable 和宏块，按本机依赖决定。本机访问计数无需复制到主库，也不应为了等长查询而无限阻塞日志回放。

### 5.4 初始物理拷贝

`src/standby/ob_standby_grpc.cpp:333` 已按 LS 的物理 tablet 迭代器生成清单，使用 `READ_WITHOUT_CHECK`；接收方 `ObStandbySSTableCopier` 按物理 ID 恢复。这比按当前 SQL schema 的表清单导出更接近目标。

尚需专门核对：KV 内部 tablet、逻辑上已删除但被后代引用的源 tablet、多层 source 链、源数据的历史 SSTable/MemTable，以及复制期间接管/GC 并发。基线中若声明接管完成，对应数据必须确实完整；不能将新完成标记与旧的不完整数据拼接。清单枚举本身不构成整个复制会话的保留证明，需要覆盖从清单生成到数据读取结束的生命周期。

## 6. 任务角色与升主

角色能力在启动/切换时传入模块生命周期。Namespace 编号不决定角色，正常业务调用点不自行猜测上下文。

| 工作 | 只读备库 | 升主恢复完成后 |
| --- | --- | --- |
| Namespace/schema 只读装载 | 允许 | 允许 |
| 物理回放、本机转储/合并/接管 | 允许，遵守本机历史依赖 | 允许 |
| 用户 DML/DDL、fork、逻辑物化、目录修复写入 | 不执行 | 允许 |
| DDL 任务恢复、DBMS_SCHEDULER 的写任务、写入式索引维护、逻辑锁清理 | 不取得写执行权 | 恢复/核对后取得执行权 |

已有调度器角色通知应复用，但需要验证每个 Namespace 的实例都收到角色状态，尚未激活的服务在以后创建时也必须继承正确角色。

升主顺序：

1. 按现有协议封定日志边界：正常切换追到旧主封写位置；故障切换沿用异步复制允许的数据损失语义，并防止双主。
2. 停止继续导入日志，完成该边界内的回放和事务状态收尾。
3. 激活新主内部恢复所需的本地追加与事务能力；客户端和普通后台任务的写入口仍关闭。
4. 核对 Namespace 注册表、schema、映射与 pin；收尾未完成 DDL 发布；失效各 Namespace 的 ID/自增等相关缓存。
5. 准备 Namespace 写任务恢复，满足主库就绪条件后开放对外写入和相应后台任务。

现有 `complete_promotion()` / `finish_committed_promotion()` 将 `set_server_write_enabled(true)` 放在最后，是可复用的发布位置，但 Namespace 恢复必须纳入这个顺序。不能为了让内部修复事务可写而提前放开客户端写入。新增恢复步骤也要支持升主过程崩溃后的重复执行。

## 7. 实施拆分

| 顺序 | 工作包 | 完成标准 |
| --- | --- | --- |
| 1 | 纯装载与角色生命周期 | 备库完成注册表/必要 schema 装载后可登录；不执行系统包/模板/DDL 修复写入 |
| 2 | 元数据一致读取、在线核对、缓存失效 | 新建/fork/删改 Namespace、DDL、物化能在线追随；不会拼接不同发布阶段的状态 |
| 3 | 基线复制、历史依赖与本地回收 | 父 TRUNCATE/DROP、多层 fork、接管进度不同、慢回放及慢查询下数据仍正确；依赖释放后可回收 |
| 4 | 升主和任务执行权 | 所有 Namespace 在切换后正确恢复写入；中途崩溃可恢复；未提交物化/DDL 状态正确收尾 |

工作包 1 可以先产出只读链路，但不能仅凭“能登录查询”将备库适配标为完成。工作包 2、3 是正确性核心，均为完整交付条件。

## 8. 定向验收矩阵

过程用例纳入本地四件套，包括每个失败复现；不跑完整 mysqltest 或 sysbench，不提交测试/文档。

1. 主库先建多个 Namespace 再建备；备库重启后仍可分别登录，相同逻辑对象 ID 不串数据。
2. 备库在线期间 CREATE/FORK/RENAME/DROP Namespace，检查名称路由、旧会话、新连接及已激活/未激活服务。
3. 父子孙从未物化的只读查询，确认没有本地创建或映射写入；父 fork 之后继续写入，不越过孩子 fork cap。
4. 主库首次物化主/LOB tablet：redo 未 commit、commit 已到达、备库进程重启、升主，各阶段检查原子可见性；再区分物化提交与用户 DML 提交。
5. 备库已有继承映射缓存时，主库物化、TRUNCATE、DROP/重建同名表，确认缓存更新与 schema 发布相容。
6. DDL SQL 元数据已提交、KV 发布尚未完成时暂停/杀主；备库只读行为明确，升主后正确收尾。
7. 父 TRUNCATE/DROP 后孩子继续读取，交叉组合主备接管先后、多层 fork、回收、转储/合并、备库重启。
8. 暂停备库 pin/schema 装载而继续回放，确认相应回收不越过安全范围；恢复装载后能够继续推进。
9. 初始基线复制期间并发 fork、TRUNCATE、接管、GC，覆盖源数据仍在 MemTable 的情况。
10. 一个 Namespace 的长查询持有历史物理源，其他 Namespace 可独立删除；源依赖释放后可回收。
11. 正常切主、故障切主和升主中途重启后，对各 Namespace 执行 DML/DDL/再次 fork，检查 ID 分配与任务执行权。

## 9. 与既有 TODO 的关系及尚未证明的部分

- 大合并取 schema 的 Namespace 版本/冻结一致性仍是独立架构 TODO；本方案不会把它算作已解决。备库定向合并验收若触发该问题，应明确关联依赖。
- 实例共享内表归属、实例管理 SQL 入口及本机私有存储改造仍分别推进。备库无需等待这些表全部搬家才开始适配，但不能将 Namespace 1 的 schema/proxy 当作所有空间的通用上下文。
- 已确认物理数据、KV 控制事实位于同一 LS，以及上述启动/刷新缺口。初始拷贝全过程的保护、回放与 GC 的并发闭合、备库基线 DAG 的完整运行、角色切换下所有后台模块均未做动态证明。
- 变更检测机制与 schema 固定快照装载接口需要在实施工作包 2 时落实到代码；这是明确的实现工作，不以“加个缓存/定时器”替代一致性约定。

关联：[整体架构讨论](design-namespace-architecture-rebuild.md)、[tablet 物化评估](assessment-tablet-materialization-refactor.md)、[服务归属 TODO](todo-service-ownership-followup.md)。


## 10. 2026-10-03 实施结果

当前分支已落实纯只读注册/schema 装载、固定发布快照、在线清单/已激活 schema 核对、复制期间的所有权点查、升主缓存失效及未完成 DDL 发布收尾。8 个 t/stanby 原始用例与 5 个扩展 Namespace 用例通过，TLS 前自动生成并校验证书。实现及证据见 [实施记录](standby-adaptation-progress.md)、standby-results/final-results.json。

本轮未完成第 8 节整个架构验收矩阵；多层/慢回放/长查询/初始拷贝与 GC/接管交错、各后台任务异常路径，以及独立 schema 版本与全局冻结的长期方案，继续属于已有 TODO。
