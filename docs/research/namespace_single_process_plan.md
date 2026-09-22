# 单进程化实施方案（namespace 进程内对象化）

状态：方案已定，待开工。配套文档：`namespace_single_process_audit.md`（可行性审计，343 处调用点实测）、`namespace_worker_functional_delivery.md` 末节（方向修订与 GC/pin 语义）。

## 目标与不变式

- 目标：`oltp_point_select` 追平单体（0.13ms/query 量级）；fork→可用保持秒级并向 100ms 收敛；通用负载不退化。
- 不变式 1：**存储引擎 ns-blind**——只认编码 id，ns 语义由 id 编码承载、翻译层解释（审计 §1 实测：存储读路径 0 处感知）。
- 不变式 2：**无 ambient 上下文**——ns 只能经由 session/任务显式传入，不引入 thread_local 当前 ns / scoped guard（OB MTL 的老路，漏一次 = 跨 ns 静默读错）。
- 不变式 3：**SQL 执行线程与 NIO 全局共享**（session 驱动，session 携带 ns）；**后台服务 per-ns 实例化**（构造即绑定 ns，模块内部保持单 ns 逻辑零感知）。多 ns 由"多个实例"承载，不由"实例感知多 ns"承载。
- 不变式 4：模块边界靠机制——bazel visibility + layering_check 做架构门禁，不靠评审自觉。

## 核心设计抉择

已定：

1. 命名三件套：`Namespace`（身份/血缘/存储根）/ `NamespaceRuntime`（per-ns 运行时数据：schema 版本状态、刷新状态）/ `NamespaceRegistry`（唯一新全局）。
2. 注入点两处：登录 session 绑定（`root@ns`）、后台任务派发。
3. 空 ns = fork 内置 `__template__` ns（两条用户路径一套机制）。
4. MVCC pin：fork 时全局 pin 不动；子物化后渐进下放为 per-tablet pin（记录，后做）。

**已定（用户拍板）：per-ns 服务组 + serverless 休眠**，即"worker 进程对象化"。

- 每个 NamespaceRuntime 持有一组 per-ns 服务实例（schema service + 刷新调度、stats、plan cache 等），构造时绑定 ns。模块内部代码保持单 ns 逻辑，零 ns 感知——这是相对"全局队列 + ns_id 列"方案的关键优势：后者要改动每个模块的枚举与队列结构，太复杂，已否决。
- 代价：活跃 ns 数 × 服务线程数的资源占用。**优化方向是 serverless 式休眠**：冷 ns 停止服务、退出/挂起线程、释放缓存，只保留 `Namespace` 元数据；首次访问唤醒（等价于现 worker 的 respawn，但进程内完成，毫秒级）。fork 后懒激活与之一致：fork 只建元数据，runtime 首次登录才拉起。
- 审计中的备选（单例服务 + ns 维度、plan cache key 加维度）降级为备选方案保留在 `namespace_single_process_audit.md`，不再推荐。

## 模块归属：Runtime vs 共享（Phase 1 划法，已定）

划分原则（用户拍板）：**默认无脑 per-ns 进 Runtime；明显可公用且改造简单的直接公用；共享线程池优化留待后续阶段**。worker prototype bootstrap（`namespace_sql_worker_prototype.ipp` 的 WORKER_STEP 序列）已经为每个 worker 进程单独实例化过下面 Runtime 列的全部模块——这就是 per-ns 可行性的现成证据，Phase 1 照搬即可。

### Runtime 持有（per-ns，构造绑 ns，内部零感知）

| 模块 | 自带线程 | 备注 |
| --- | --- | --- |
| `ObMultiVersionSchemaService` + schema_status_proxy + 刷新 | 是（refresh 定时器） | 1.3 spike 验证第二实例共存 |
| `ObPlanCache` / `ObPsCache` | 否 | 失效语义天然按本 ns 版本，不动 key |
| `ObSQLSessionMgr` | 是（清理线程） | session 生命周期归本 ns |
| `ObPxPools` | 是（动态伸缩） | 见下方 PX 专项 |
| `dtl::ObDfc` / DTL / `ObDTLIntermResultManager` | 是（dfc 刷新） | PX 数据通道状态 |
| `ObSqlMemoryManager` / `ObOptStatMonitorManager` | 否 | |
| `ObDataAccessService` / `ObLobManager`（SQL 侧） | 否 | |
| `ObDDLServiceLauncher` / `ObDDLScheduler` | 是 | 长事务 DDL 按 ns 归口 |
| `ObSharedTimer` | 是 | per-ns 定时器；后续可合并为全局一张 |
| vt_data_service / srs_service / autoincrement | 否 | autoincrement 现为 `get_instance` 单例，需改 per-ns |
| 统计信息（stats 收集/监控） | 是 | per-table 语义，schema 耦合，天然 per-ns |

### 共享（Phase 1 白名单）

| 模块 | 理由 |
| --- | --- |
| 网络/NIO（net_frame、mysql 监听、rpc、sql nio） | session 携带 ns，入口一次绑定后热路径无感知；跨 ns 公用是唯一合理形态 |
| 存储引擎族：ObLSService / local_storage_meta / ObAccessService / memstore / freezer / checkpoint / tablet GC / compaction 全家 / dag_scheduler / io_service / tmp_file / shared_macro_block_mgr / tablet_runtime_meta_updater | 不变式 1：ns-blind，全部按 tablet/LS 编码 id 组织（审计 §1 实测读路径 0 感知；tablet_runtime_meta_updater 实测 tablet_id 键） |
| 事务/日志族：trans_service / log_service / timestamp / trans_id / unique_id / table_lock / lock_wait / deadlock | 全局键组织，无 ns 状态；worker 模式下本来也是共享进程独占 |
| config / tz_info / GMEMCONF / sql factories / expr static | 纯全局常量态，worker bootstrap 中本就是 reload/init 幂等 |

### PX 专项（回答"PX 共享改动大吗"）

`ObPxPool` 本体是 ns-blind 的任务执行器：`submit(RunFuncT)` 只收闭包，任务闭包自带 `ObPxInitTaskArgs`（内含 `ObDesExecContext` → session → ns），池子不读任何 ns 状态。现状 `ObPxPools` 已按 `group_id` lazy 分池，线程动态伸缩 + 空闲回收。

- Phase 1（per-ns）：worker prototype 已经 per-worker new 了 `mods_px_pools_`，照搬，**零新增改动**。
- 未来共享：因为 ns 全程走任务上下文、不走路由，共享只需把 `get_or_create(group_id)` 的 key 归一成单池，**改动极小**。PX 是线程池共享化最理想的候选，但第一阶段不做。

### freeze 家族专项：执行层共享、调度层也全局（已定，用户拍板）

原则：**Runtime 是纯计算层，不承载调度**。调度（merge/freeze 的发起与进度追踪）是全局协调者，允许知道 registry；执行层 ns-blind。

实测拆分：

- **执行层（共享，ns-blind）**：memstore freezer、checkpoint、compaction 执行、dag、tablet GC——全部 tablet 键组织，不读 schema。
- **调度/进度层（schema 耦合，是唯一的 ns 感知点）**：`ObMajorMergeScheduler` init 绑定单个 `ObMultiVersionSchemaService&`（`ob_major_merge_scheduler.h:64`），`ObMajorMergeProgressChecker` 经 `schema_guard.get_simple_table_schema` 逐表枚举 tablet、按 table_id 做 checksum 校验（`ob_major_merge_progress_checker.cpp:250`），`ObDailyMajorFreezeLauncher` 经 sql_proxy 写 freeze info 内部表。单进程化后没有"覆盖所有 ns 的单一 schema service"，这个引用拿谁的就是问题。

**已定做法：调度器全局单例 + registry 遍历**。关键实测便利点：`ObMajorMergeProgressChecker` 的各方法本来就把 `ObSchemaGetterGuard&` 当参数传（`ob_major_merge_progress_checker.cpp:170/221`），不持有 schema_service。因此只需把 scheduler 的"持有一个 schema_service 引用"改为"持有 `NamespaceRegistry&`，每轮调度遍历所有活跃 ns、逐个取 guard 传入"——checker 内部零改动，合并语义保持全局一轮（broadcast scn / freeze info 不变）。这正是不变式 2 的形态：ns 由调度任务显式传入，模块内部无感知。

已定细节：`__all_freeze_info` 等全局调度元数据由系统 ns 承载（`__template__`，internal 标记保证永久存在、拒绝登录/DDL/drop），调度器与存储层的相关 inner SQL 统一定向该系统 ns。

### inner SQL 规则（已定）：永远有宿主 ns，共享层不自带 SQL 能力

- Runtime 内发起：默认本 ns，零改动。
- 共享层发起（存储引擎、全局调度器）：显式指定目标 ns，交给该 ns 的 Runtime 执行——进程内函数调用，worker 模式的跨进程 inner SQL 重定向机械全部作废。全局调度元数据一律指定系统 ns。
- 载体：`ControlSqlNamespaceScope` 从 prototype hack 升格为正式机制——inner SQL 入口强制带目标 ns，共享层调用点显式给出（不变式 2）。
- 层级：存储→SQL 上向调用 vanilla 单体本已存在（tablet_scheduler 读 freeze info），单进程化只是恢复该形态，增量仅是"在哪个 ns 的命名空间执行"。

已否决：

- 调度层 per-ns 进 Runtime：违反"Runtime 纯计算层"定位（用户拍板）。
- 保留覆盖全 ns 的全局 schema 视图专供调度：内存 double，已否决。

### 待定/后续阶段再定

- 共享线程池化（合并 per-ns timer/px/ddl 线程）：待 ns 数上来后按实测资源占用决定，休眠机制（Phase 4）优先于池合并。

## 阶段拆解

### Phase 0 准备（不动语义，纯读代码 + 机械删除）

- 0.1 **343 处三分类**（能传 session/任务上下文；能推 编码 id；必须 ambient）。分类 3 的数量是成败指标：小于等于 10 走全显式；几十处则必须重新评估。审计 §7 卡点。
- 0.2 删 93 处恒真开关残留三元（审计 §8.2，机械零风险）。
- 0.3 bazel 门禁雏形：存储侧已有 `default_visibility=private` + 公私头文件划分；补 SQL 与存储的依赖方向规则，接入 CI 作为架构门禁（出货仍 CMake）。
- 出口：分类报告落文档；门禁绿。

### Phase 1 单进程骨架（目标：单进程双 ns，点查追平单体）

- 1.1 NamespaceRegistry + Namespace + NamespaceRuntime 骨架（懒创建，首次登录激活；Runtime 持有该 ns 的服务组）。
- 1.2 登录绑定：`root@ns` 用户名解析（复用现有路由代码），session 缓存 runtime 指针。
- 1.3 schema service per-ns 实例化：worker bootstrap 的 `init_schema` 序列移植为 Runtime 的服务组初始化；spike 最先验证 `ObMultiVersionSchemaService` 第二实例能否在进程内共存（隐藏全局依赖：refresh 定时器、inner SQL 连接池、publish signal）。
- 1.4 每语句版本 pin 从 `thread_local worker_request_schema_version` 改 session 级（约 6 处——合并后线程跨 ns 复用，thread_local 必串）。
- 1.5 plan cache per-ns 实例（Runtime 持有，失效语义按本 ns schema 版本，无需动 key 结构）。
- 1.6 热路径 4 入口改造（`obmp_query.cpp:104/515/850`、`ob_sql.cpp:1115`）。
- 出口验证：单进程 fork 出第二个 ns，`root@ns1` / `root@ns2` 各自读写隔离；fork 到可用小于 1s；**oltp_point_select 1t/8t 达到 vanilla_sysbench 基线**（整个转向的验收点）。

### Phase 2 全查询面成立（DDL/PL/虚表/后台）

- 2.1 152 处 DDL/rootserver 调用点显式 ns（DDL 任务上下文自带目标 ns）。
- 2.2 其余分类 1/2 调用点机械改造；分类 3 个位数逐个定点处理。
- 2.3 后台调度遍历 registry：schema refresh / stats / freeze / DDL scheduler——线程保持通用，任务对象带 ns。
- 2.4 inner SQL ns override（`ControlSqlNamespaceScope`）升格为正式机制：入口强制带目标 ns，共享层调用点显式指定（全局调度元数据 → 系统 ns）。
- 出口：四 prototype 套件适配单进程形态后 PASS；mysqltest 基础套件绿。

### Phase 3 删 IPC 层

- 3.1 删 worker 进程模式：main.cpp 分支、worker bootstrap、`namespace_worker_{scan,write,gateway,sql_worker}_prototype.ipp`、字节流代理、endpoints 表。
- 3.2 `check_sql_execution_role` / `worker_process` 判定删除（共享进程禁 SQL 的不变量作废）。
- 3.3 `INamespaceStorage` 接口沉淀（v20 协议语义到进程内接口）：可与 3.1 并行或后置，先直接调用再提炼接口也成立。
- 出口：二进制回到单体形态；删除过程本身即"无隐藏依赖"的验证。

### Phase 4 语义增强（后续独立排期）

- `CREATE NAMESPACE`（模板 ns fork）。
- MVCC pin 渐进松绑（全局先行 + 物化下放 per-tablet）。
- 父 ns 可 drop（source snapshot 保留，去掉 prototype 简化）。
- ns1 纳入编码（审计 §8，8 处依赖点，不动持久化格式）。
- fork 语法终态（FORK INSTANCE/TABLE 保留，FORK DATABASE 退役评估）。
- serverless 休眠/唤醒：冷 ns 停服务退线程留元数据，首访问毫秒级唤醒；休眠水位策略（按最近访问/内存压力）。

## 风险与回退

| 风险 | 兜法 |
| --- | --- |
| 分类 3（必须 ambient）数量超预期 | Phase 0 卡点，不进入 Phase 1 |
| schema service 多实例的隐藏全局依赖（refresh 定时器、inner SQL 连接池） | Phase 1 spike 最先验证 1.3 |
| 活跃 ns 数 × 服务线程数的资源占用 | serverless 休眠（Phase 4）；活跃数上限观测 |
| 无内存隔离：单 ns 膨胀拖全局 | 分配打 ns tag（只观测不隔离），virtual table 暴露 |
| 过渡期性能无门禁 | Phase 1 出口把 sysbench 对标单体设为硬门槛 |

回退策略：Phase 1-2 不删任何现有 worker 路径（并行新增），Phase 3 才删除；任一 Phase 失败停在原地，worker 模式仍可运行。
