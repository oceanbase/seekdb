# 关系候选贡献：在上层算子生成前扩展计划

后续进展：已接入 v5 语义／依赖接口和 v9 Rust JOIN 策略，详见
[规划语义与 Rust 策略](plugin-planner-semantics.md)；以下验证记录对应
本阶段引入时的检查点，不覆盖后续新增用例。

日期：2026-09-10。状态：生产构建、104 项 SDK 测试与 25 项 doc-test
通过；完整 kernel 和 30/30 独立 CTest 通过，包括真实 Rust/loader
的七个新增贡献阶段变体。

## 为什么需要新阶段

双输入 Rust 等值连接的首轮正常 SQL 用例失败在 EXPLAIN 断言，而非
Rust 结果比较。诊断重跑确认：现有 `optimizer.candidate.select.v1`
在该查询中收到的是 `SORT → HASH JOIN → FUNCTION_TABLE × 2`。
只匹配候选根 JOIN 的策略无法贡献连接实现。移除 ORDER BY 或声称
连接天然保持排序，都不能解决这个规划入口缺失的问题。

新阶段 `optimizer.relation.paths.v1` 在 join/access paths 转换为
logical trees、加入特殊 filter/startup 后运行，早于聚合、排序和
LIMIT 等上层算子的生成。由 `init_candidate_plans()` 的明确调用点
触发，不放入会被各阶段反复调用的数组初始化重载。

## 契约

| 项目 | 关系候选贡献 | 后期候选选择 |
| --- | --- | --- |
| hook point | `optimizer.relation.paths.v1` | `optimizer.candidate.select.v1` |
| 输入 | 当前查询块的初始关系候选 | 宿主提供的等价候选组 |
| build | 追加关系等价的实现，可使用 fragment v3 | 追加候选 |
| select | 禁止，记录不可忽略的错误 | 可以选择宿主候选 |
| next | 调用后续贡献者；叶节点不选赢家 | 后续选择链／宿主最小代价选择 |
| 成功输出 | 原始候选和全部新增候选 | 最终选中的候选 |

两个阶段使用同一版本绑定的 context/service、graph 和 build 协议，
但独立注册、独立路由。现有选择插件不会隐式多执行一次。贡献阶段
要求 Server-dev admission、service minor ≥ 1 和 AROUND 模式；
成功必须恰好调用一次 next，不允许截断其余贡献者。后期选择仍可
使用已有 replacement 模式，二者并非同一个可替换的操作。

所有原生和自定义候选都交给正常的上层算子分配、属性处理、剪枝和
代价比较；不会因为构造了自定义算子便提前抛弃有用的原生排序路径。
贡献者负责关系／表达式的 SQL 语义，宿主仍检查实际支持的数据流、
类型、构造边界和位置契约。fragment 目前只接受本地单并发。

## 发布与共享子树

新建候选可以共享原生候选的输入子树。构造期间恢复暂改的父指针，
贡献阶段只发布候选数组，不把某个共享输入绑定到尚未胜出的父节点。
宿主 `adjust_final_plan_info` 在最终计划确定后设置实际树的父子关系。
上层候选规划中不能依赖未胜出替代树的唯一父指针。

整个贡献链成功后才复制结果；失败时结果数组为空，原始候选数组
保持不变。build/get/graph/select 的 sticky error 在最终出口再次
检查，即使受控 module provider 吞掉错误也不能发布部分结果。
输入和输出数组不能别名，以免先清理结果而破坏待处理候选。

## Rust 接口

`Registration::relation_paths_hook(&optimizer::Definition)` 注册独立
hook point。服务实现复用 `candidate::Hook`，启用 BUILDERS，使用
Around；根据需要启用 INSPECT / QUERY_TARGETS。

回调可遍历候选图，调用 `custom_fragment` 贡献算法，然后调用
`call_next()` 并返回成功；不调用 `select()`。PlanId/ExpressionId
只在当前回调链中有效，执行计划中只保存宿主解析后的表达式与插件
owned 参数，不保存这些句柄。

本次真实 Rust SDK/DSO 的独立回归贡献 MATERIAL 以验证服务接入，
正常 JOIN SQL 的策略仍为受控 C++ graph 回调，执行算法为 Rust
SJE1。不要将两种证据合称“已有通用 Rust join 规划器”。

## 验证与剩余范围

新增用例验证保留原始候选、追加两层候选、默认代价仍能选择原生
路径、父指针不变、超时及被吞掉的 select/build 错误不发布结果。
独立 loader/Rust 用例区分两个 hook point，并覆盖错误 mode、旧
service、未获 Server-dev admission、遗漏 next 和精确错误传播。
正常 SQL 保留 ORDER BY，让低代价 SJE1 关系候选经过真实排序规划、
codegen 和 Rust DSO 执行，检查重复键、NULL、取消及重扫。

上述本轮检查已通过：正常 SQL 包含标量/batch=3 各两种键语义，
EXPLAIN 为 SORT → PLUGIN CUSTOM → 两个 FUNCTION_TABLE。
独立 candidate/loader 现有 20 个变体全部通过，包含新阶段的成功
贡献、错误 mode/service 版本、Public 实现拒绝、遗漏 next、被吞掉
的构造失败及非法 select；调用结束后检查 lease 归零。

这是查询块初始关系的贡献阶段，不是 PG 全部路径扩展协议的完成：
还没有每个 join 枚举子问题、全部 upper-relation 阶段、跨查询块
替换或分布式/PX 重绑定。真实 Rust 语义策略、类型/索引/恢复、
实库 catalog 事务与 AI 资源／轻量化仍属于完整目标。

具体测试结果见[实施进度](plugin-implementation-status.md)。
另见[计划片段](plugin-plan-fragments.md)、[Rust 双输入连接](plugin-rust-join.md)。
