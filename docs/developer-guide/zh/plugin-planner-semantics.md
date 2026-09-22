# 规划语义与 Rust 自主关系策略

日期：2026-09-10。最新 v6/v10：生产宿主与动态库构建/审计、109 项
SDK 测试与 25 项 doc-test、十一项插件单测通过；本轮完整 kernel
通过，独立测试重新构建后 30/30 CTest 通过（100.87 秒）。v6 包含
十六组 JOIN SQL，其中八组
Rust 候选、八组内核回退。下文十四组 SQL 与 30 项 CTest 通过记录
属于前一 v5/v9 检查点；此处 v6 结果来自重新执行，不沿用旧结果。

## v6：参数绑定也是计划语义的一部分

`candidate_context_v6` / service minor=5 增加 binding_count/binding，
Rust 通过 `Hook::PARAMETERS` 启用（隐含前代能力）。按 NestedLoop、
LeftPushDown、RightPushDown 三种角色读取 JOIN 自身持有/携带的执行
参数列表。每个 binding 返回参数表达式与来源表达式两个调用期 ID，
与原 graph/target/column 共用身份；可继续读取来源的类型和依赖。

这里不是 SQL 绑定参数的当前值，也不开放设置执行 frame、跨线程
借用或自动跨查询块读取。非 JOIN 返回对应角色计数为零，不等于
整个子树没有外部依赖；本接口不递归推断依赖。没有 PARAMETERS
能力时 SDK 返回 UNSUPPORTED_ABI，而不是伪造“没有参数”。

旧服务仍收到自己的精确前缀；旧版 plan_semantics flags 没有变化。
宿主检查 plan/role/index、空绑定/来源、输出别名与内部错误；两个
输出在失败时均初始化为无效 ID，不发布半个绑定。读取只对现有
表达式建立调用期 ID，不调用可能改写内核状态的 get_op_exprs。

Rust v10 的独立输入 JOIN 策略在贡献前查询这三类绑定。只要根节点
带有绑定，就保留内核候选；否则删除绑定 owner 可能使右侧相关
输入失去逐行绑定/重扫语义。这不是把参数化能力排除在完整设计外：
后续需要显式 bind-before-rescan、参数作用域/恢复及子计划资源
协议，才能让插件真正接管相关执行，而不是只提供观察接口。

新增 SDK 测试覆盖共享表达式身份、七组返回/宿主错误、六种不完整
上下文和缺少能力；Rust 策略单测覆盖三类绑定的独立回退。kernel
新增三类真实 ObLogJoin 列表及五种错误检查均通过；标量/batch=3
相关表函数 SQL 验证保留真实绑定 owner、结果与 rescan，完整 kernel
通过。相关 SQL 使用左表的插件逻辑类型 token 驱动右表函数，并在
右侧过滤器使用左表 ordinal，覆盖两个实际执行参数而非合成空列表。

这条路径也修正了原生优化器执行参数创建时丢失插件逻辑类型的问题：
两种 create_new_exec_param helper 在设置引用时复制逻辑类型，缓存
列表入口复用完整创建流程，不依赖后续 formalize 才恢复类型信息。
普通/插件/存储类型、三个创建入口及缓存复用的拥有权测试通过。
手动执行夹具另按正常执行上下文预留物理计划参数槽；它此前只有
动态表达式 frame，没有为原生 NLJ 初始化 ObObj 参数 store。

## 新增桥接信息

`candidate_context_v5` / service minor=4 新增五个只读回调。Rust
`Hook::SEMANTICS` 启用这些能力，并隐含 query、graph 和 builder。
旧服务仍收到自己声明的精确前缀，不把新增字段强塞给旧插件。

| 接口 | 含义 | 不代表什么 |
| --- | --- | --- |
| plan_semantics | 规范化连接类别、本地单并发条件 | 不是 C++ 枚举编号，也不是分布式支持承诺 |
| expression_semantics | 普通／NULL-safe 等式、builtin 整数类别、标量确定性条件 | 不证明任意插件算法纯粹或数学正确 |
| scope | 当前查询块中，表达式的非空关系依赖是否包含于指定计划 | 不证明跨任意算子的可移动性或最终物理可用性 |
| column_count / column | 重写后查询的已解析列引用清单 | 不是最小投影、子计划输出 schema 或跨查询块遍历 |

scope 区分 Independent、Outside、Contained；不能把无依赖的常量
当作左右输入中的任意一路。不同查询块的计划报告 Outside。句柄
仍是本次调用的身份，与 query targets/graph 共用，不进入持久计划。

标量确定性检查遍历已有表达式节点，不调用会分配表达式的
get_op_exprs。排除聚合、窗口、子查询、执行参数、UDF、已知状态／
非纯函数等；使用宿主表达式的确定性契约。这不是表达式移动的一般
正确性证明，NULL 扩展、上下文和算子语义仍需策略自己判断。

后续 Rust SORT 接入进一步补充：表函数列携带精确 `core.type.bool`、
`core.type.int32/uint32/int64/uint64` 逻辑身份时，只要是与当前 SQL
类型匹配的非存储编码整数值，也归为 builtin integer，而非一概 OTHER。
符号类别依据 SQL 物理类型，保留 uint32 映射为 SQL BIGINT 的现有语义。
任意自定义类型、存储编码及物理类型不一致的元数据不因此获得原生比较
承诺。CASE 是独立表达式类，检查覆盖 argument、每个 WHEN/THEN 和
default；任一子节点不满足确定性条件时，整体不具备该标志。对应当前
回归状态见[实施进度](plugin-implementation-status.md)。

## Rust v9 的真实 JOIN 策略

新增独立 service `org.seekdb.rust-candidate.join-policy`，通过 Rust
SDK 注册到 `optimizer.relation.paths.v1`。候选遍历、语义判断、键
方向、输入／输出映射、SJE1 参数和代价均在 `src/planner.rs` 中生成。
C++ SQL 夹具只负责路由到 loader、统计新增数，不再匹配或构造 JOIN。

当前参考策略处理本地单并发、一个普通等值条件、无额外目标 filter/
startup/join filter 的 INNER JOIN。两路键必须是相同 SQL 类型的
builtin 整数和标量确定性表达式；通过 scope 决定左右输入，允许等号
参数顺序或物理左右输入改变。NULL-safe、外连接、字符串、跨输入或
独立键等不适用情况返回不贡献，继续内核候选规划。协议错误、构造
失败和资源错误仍传播，不伪装为正常跳过。

插件传递查询列清单，而非只传 SELECT 列：ORDER BY、聚合等上层
可能引用未投影的列。输入每路先放键，再去重加入该路查询列；输出
去重后的查询列及其输入槽映射。计算仍交给实际 Rust SJE1 执行器，
宿主负责表达式计算、codec 和上层操作。最终布局与依赖检查保留。

每次贡献至多一个算法候选，保留全部原生候选。参考代价为输入行数
乘积的函数，体现嵌套循环复杂度，但尚未校准为生产成本模型。执行
仍有 owned 输入预算和取消，估计行数不是资源使用上限的保证。

原后期 CustomSpool 示例服务仍独立存在；它演示强制选择能力，不是
生产优化策略。JOIN SQL 回归停用这段无关的强制 spool，只让正常
上层规划／代价比较决定 JOIN 候选。实际 JOIN 策略来自 Rust DSO。

## 验证设计与剩余范围

SDK 回归覆盖新 C/Rust 布局、规范化元数据、查询列／目标身份一致、
13 组成功／损坏返回／宿主错误和九种不完整 context。C++ 图夹具
检查真实列依赖归属、确定性变化、执行位置和查询列清单。

正常 SQL 不再带 ORDERED/USE_HASH 提示。标量与 batch=3 计划分别
检查整数重复键、NULLIF 键、反向等式、非 SELECT 排序列；另检查
NULL-safe、外连接与字符串等式不贡献插件候选，结果仍正确。
正向用例保留 SORT，执行算法应为双输入 Rust SJE1，继续验证完整
结果对、取消和重扫。十四组 SQL 用例（七类、各两种 batch 配置）已
在完整 kernel 中通过：八组贡献 Rust 候选，六组回退内置计划。非
SELECT 排序列在插件输出中保留，上层 SORT 正常使用它。完整执行
矩阵还保留既有多输入、类型、LOB/codec 和 SQL 用例。schema/session
仍有夹具；这不是实库 catalog 事务或插件跨进程 PX 的证明。SQL
反向等式已覆盖，但不能据此声称物理左右输入交换已实际发生。

新增策略单测通过 SDK ABI 调用实际 IntegerJoin::invoke，只替换
宿主图回调和实例验证，不使用全局 ACTIVE。八组正向场景交叉覆盖
物理输入交换、等式参数反向、跳过首个非 JOIN 候选；逐字段检查
input plans/offsets、键、隐藏列、去重输出、SJE1 映射和参考代价。
十二类不适用语义只调用 next，既不构造也不 select；两类宿主失败
保留错误且不继续链。它补充策略分支证据，不代替 SQL 执行验证。

后续仍需扩展非等值／多条件关系、更多语义与表达式、各规划阶段、
跨查询块、并行／分布式和缓存重绑定；完善成本、spill 和资源协议。
实库 catalog 事务、类型／索引／恢复与 AI／轻量化仍是完整目标，
并未缩减为当前整数 JOIN 参考策略。

相关：[关系候选贡献](plugin-relation-paths.md)、[Rust 双输入执行](plugin-rust-join.md)、[实施进度](plugin-implementation-status.md)。
