# Server-dev 查询目标与显式计算输出

状态：宿主与 Rust 接口已接入，生产构建、SDK 与完整 kernel 验证通过。
此文讨论当前查询块的逻辑 SELECT 目标，不是任意计划替换
已经完成的声明。

## 为什么需要独立目标接口

v3 的 filter/startup/ordering/join condition/join filter 只表示节点已有
字段。`SELECT ABS(ordinal) ... ORDER BY token, ordinal` 中的 ABS 不必
出现在这些字段中。插件若只能从排序或过滤中取 ID，就无法声明自己计算
该 SELECT 值。

v4 增加 query 与 target 两个只读回调，candidate service minor=3。
查询块以当前 ObLogPlan 的已重写 statement 为准，而不是最外层原始 SQL
文本；嵌套查询在各自规划调用中有自己的 statement。这里不提供递归
枚举嵌套 AST 的接口。

## C ABI 与 Rust SDK

`query_info_v1` 包含 struct_size、匹配构建的 statement_type、flags、
target_count 与零保留字段。SELECT_LIST 表示完整逻辑 SELECT 列表可用，
SET_OPERATION 表示这是集合结果的目标，而非某个分支的输入 schema。
非 SELECT 的 flags/count 为零，和有列表但零项的 SELECT 明确区分。
缺少 statement 是错误，不作为“查询没有目标”。

Rust hook 声明 `QUERY_TARGETS = true`，同时获得 graph 与 builders。

```rust,ignore
let query = context.query()?;
if let Some(count) = query.target_count {
    for ordinal in 0..count {
        let id = context.target(ordinal)?;
        let expression = context.describe_expression(id)?;
        // 判断该表达式的算法适用性及所需输入；不会自动选择或构造计划。
    }
}
```

target 保留 SELECT 项的顺序与重复项。相同表达式指针与 v3 graph 共享
同一个 invocation ID；不同表达式即便文本相同，也不额外合并。`SELECT *`
已经由 resolver 展开。不返回名字/alias、常量值或绑定参数数据。

目标 ID 可以传入 `custom_with_layout`，但“可以识别目标”不代表任意子
计划都能计算它。聚合、窗口、集合、子查询等仍有合法的执行位置与依赖；
插件负责算法和关系语义，宿主继续执行显式布局和表达式分配校验。

## 所有权、错误与兼容

读取不会调用 get_op_exprs、不分配内核表达式、不改写 SELECT 列表。
只有被读取的表达式被登记到调用期身份表，沿用最多 16384 个不同表达式
的限制。列表项数和不同表达式数不是同一概念，重复项不额外消耗身份。

query 的失败输出清零，target 的失败输出为 UINT32_MAX。失败进入同一
sticky database error，忽略状态不能继续构造成功计划；最终校验仍拒绝
发布。Rust 的 ExpressionId 不跨查询生命周期或线程。

loader 在回调前验证完整 v4 后缀，再按每项 service minor 提供精确的
v1/v2/v3/v4 前缀。旧插件不被迫解释新字段，新插件也不能在旧宿主上把
“没有目标接口”伪装成“目标列表为空”。这是版本绑定 Server-dev 接口，
仍需匹配宿主构建，不增加 PG 二进制兼容承诺。

## 参考插件与验证边界

Rust candidate v6 实际枚举 SELECT 目标并读取元数据，在构造与 next
之后重新核对数量及首项身份。它仍选择全列 spool，不声称自动推导完整
输入依赖、替换任意 SQL 函数或处理多输入计划。

SDK 验证 C/Rust 字段布局、可用/空/非 SELECT/集合信息、顺序/重复身份、
与 graph 的相同身份、错误状态及非法后缀，并用编译失败测试限制目标
句柄逃逸。宿主图夹具包含不在节点角色中的目标、重复项、集合标记、
非 SELECT 和七种非法访问，检查读操作没有修改节点输出与父子关系。

正常 SQL 夹具新增只在 SELECT 中出现 ABS(ordinal) 的用例。C++ 测试
策略从 v4 target 获取它，通过公开 build 请求令实际 Rust spool 产出
对应值；正 ordinal 上的 identity 合法，但不是通用 ABS 实现。测试
仍使用部分 schema/session 替身，不能证明实库事务、扫描或跨进程 PX。

最新结果见[实施进度](plugin-implementation-status.md)，相关协议见
[规划图](plugin-candidate-graph.md)与[显式布局](plugin-custom-layout.md)。
