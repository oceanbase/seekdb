# 单进程化实施方案（namespace 进程内对象化）

状态：方案已定，待开工。配套文档：`namespace_single_process_audit.md`（可行性审计，343 处调用点实测）、`namespace_worker_functional_delivery.md` 末节（方向修订与 GC/pin 语义）。

## 目标与不变式

- 目标：`oltp_point_select` 追平单体（0.13ms/query 量级）；fork→可用保持秒级并向 100ms 收敛；通用负载不退化。
- 不变式 1：**存储引擎 ns-blind**——只认编码 id，ns 语义由 id 编码承载、翻译层解释（审计 §1 实测：存储读路径 0 处感知）。
- 不变式 2：**无 ambient 上下文**——ns 只能经由 session/任务显式传入，不引入 thread_local 当前 ns / scoped guard（OB MTL 的老路，漏一次 = 跨 ns 静默读错）。
- 不变式 3：**NamespaceRuntime 不绑线程/内存池/IO**——per-ns 只有数据对象；运行资源全共享（不要故障/资源隔离换来的红利）。
- 不变式 4：模块边界靠机制——bazel visibility + layering_check 做架构门禁，不靠评审自觉。

## 核心设计抉择

已定：

1. 命名三件套：`Namespace`（身份/血缘/存储根）/ `NamespaceRuntime`（per-ns 运行时数据：schema 版本状态、刷新状态）/ `NamespaceRegistry`（唯一新全局）。
2. 注入点两处：登录 session 绑定（`root@ns`）、后台任务派发。
3. 空 ns = fork 内置 `__template__` ns（两条用户路径一套机制）。
4. MVCC pin：fork 时全局 pin 不动；子物化后渐进下放为 per-tablet pin（记录，后做）。

**待拍板（审计与早期讨论的分歧点）**：schema service / plan cache 的形态。

- 方案甲（早期讨论）：per-ns 实例（NamespaceRuntime 持有 ObMultiVersionSchemaService 实例）。隐患：每实例自带刷新调度器 ≈ 每 ns 绑线程，违反不变式 3。
- 方案乙（审计推荐）：**单例服务 + ns 维度**。table_id 已编码 ns，schema 缓存天然按编码 id 免冲突；真正要加的只是"每 ns 的当前版本"存取（版本存储 + 3 个访问器，单文件）；plan cache key 加 1 个 fork-ns 字段（约 7 行，注意 `ObPlanCacheKey::namespace_` 是库缓存命名空间命名陷阱，别改错）。刷新调度器保持全局一套、遍历 registry。
- **建议方案乙**：改动量在数百行量级（审计判定小于 500 行），且与 ns-blind 原则同构——ns 以数据形式存在，不以对象复制形式存在。NamespaceRuntime 相应收窄为 { 版本状态、刷新游标、血缘 } 纯数据包。

## 阶段拆解

### Phase 0 准备（不动语义，纯读代码 + 机械删除）

- 0.1 **343 处三分类**（能传 session/任务上下文；能推 编码 id；必须 ambient）。分类 3 的数量是成败指标：小于等于 10 走全显式；几十处则必须重新评估。审计 §7 卡点。
- 0.2 删 93 处恒真开关残留三元（审计 §8.2，机械零风险）。
- 0.3 bazel 门禁雏形：存储侧已有 `default_visibility=private` + 公私头文件划分；补 SQL 与存储的依赖方向规则，接入 CI 作为架构门禁（出货仍 CMake）。
- 出口：分类报告落文档；门禁绿。

### Phase 1 单进程骨架（目标：单进程双 ns，点查追平单体）

- 1.1 NamespaceRegistry + Namespace + NamespaceRuntime 骨架（懒创建，首次登录建立）。
- 1.2 登录绑定：`root@ns` 用户名解析（复用现有路由代码），session 缓存 runtime 指针。
- 1.3 schema 版本 per-ns 存取：版本存储 + `get_runtime_schema_guard` / `get_runtime_refreshed` / `get_published` 三访问器加 ns 维度（ob_multi_version_schema_service 单文件）。
- 1.4 每语句版本 pin 从 `thread_local worker_request_schema_version` 改 session 级（约 6 处——合并后线程跨 ns 复用，thread_local 必串）。
- 1.5 plan cache key 加 fork-ns 维度（key 构造 + hash + 相等 + deep_copy）。
- 1.6 热路径 4 入口改造（`obmp_query.cpp:104/515/850`、`ob_sql.cpp:1115`）。
- 出口验证：单进程 fork 出第二个 ns，`root@ns1` / `root@ns2` 各自读写隔离；fork 到可用小于 1s；**oltp_point_select 1t/8t 达到 vanilla_sysbench 基线**（整个转向的验收点）。

### Phase 2 全查询面成立（DDL/PL/虚表/后台）

- 2.1 152 处 DDL/rootserver 调用点显式 ns（DDL 任务上下文自带目标 ns）。
- 2.2 其余分类 1/2 调用点机械改造；分类 3 个位数逐个定点处理。
- 2.3 后台调度遍历 registry：schema refresh / stats / freeze / DDL scheduler——线程保持通用，任务对象带 ns。
- 2.4 inner SQL ns override（`ControlSqlNamespaceScope`）收敛到 Phase 2 新机制。
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

## 风险与回退

| 风险 | 兜法 |
| --- | --- |
| 分类 3（必须 ambient）数量超预期 | Phase 0 卡点，不进入 Phase 1 |
| schema service 多 ns 的隐藏全局依赖（refresh 定时器、inner SQL 连接池） | Phase 1 spike 最先验证 1.3 |
| 无内存隔离：单 ns 膨胀拖全局 | 分配打 ns tag（只观测不隔离），virtual table 暴露 |
| 过渡期性能无门禁 | Phase 1 出口把 sysbench 对标单体设为硬门槛 |

回退策略：Phase 1-2 不删任何现有 worker 路径（并行新增），Phase 3 才删除；任一 Phase 失败停在原地，worker 模式仍可运行。
