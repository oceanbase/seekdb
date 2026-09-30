# Server-dev 规划图与表达式身份

本接口向 Rust/C 插件开放候选计划的已有结构，为后续输入关系与输出
映射提供调用期身份基础。它不是最终输出 schema，也尚未实现多输入
自定义物理计划或任意表达式改写。

## 入口和版本

`candidate_context_v3` 在 v2 后追加 root、plan、child、expression、
describe_expression、argument；candidate service 使用 spi_minor=2。
loader 按 service minor 提供精确的 v1/v2/v3/v4 大小，旧 hook 不会看到
未经约定的后缀，要求 v3 的插件不会静默退回 v2。

Rust `candidate::Hook::INSPECT = true` 启用 v3，同时包含 v2 构造能力。
Public 插件不能仅通过声明该 hook 绕过 Server-dev 宿主构建绑定。
v4 / minor=3 另开放当前查询块的完整 SELECT 列表，通过 QUERY_TARGETS
启用，复用同一表达式身份表；详见[查询目标](plugin-query-targets.md)。

```rust,ignore
let root = context.root(0)?;
let node = context.plan(root)?;
for index in 0..node.expression_count(candidate::Role::Filter) {
    let (expression, _) = context.expression(root, candidate::Role::Filter, index)?;
    let metadata = context.describe_expression(expression)?;
    for arg in 0..metadata.argument_count() {
        let argument = context.argument(expression, arg)?;
        let argument_metadata = context.describe_expression(argument)?;
        // 根据表达式种类、SQL 类型和插件逻辑身份判断算法是否适用。
    }
}
```

## 身份、所有权与只读边界

- candidate index 表示同一等价候选集合中的可选择项；PlanId 表示图中
  的一个节点。两者不是同一编号空间，不能互换。
- 一个 hook 链调用内，相同节点或表达式始终返回相同 ID；共享子树、
  重复参数及新路径引用原节点都保留身份。不同调用之间不能复用 ID，
  也不能把 ID 写入缓存计划/PX 消息当作持久绑定。
- 宿主持有对象与 ID 映射，Rust 只得到带生命周期的 PlanId、ExpressionId
  及复制的元数据。句柄不能从安全 SDK 逃逸调用期或跨线程。
- 初期最多登记 4096 个计划节点和 16384 个表达式；只在插件实际探测
  时创建映射，不为没有探测需求的查询预先遍历或复制完整计划树。
- 所有失败保留首个数据库错误，成功/忽略 Result 不能清除失败；后续
  构造与最终候选验证仍拒绝发布。失败输出为清零结构或 UINT32_MAX。

## 可读信息

计划信息包含算子种类、子节点数、join 种类（非 join 明确为空）、
cost/rows/width，以及已有 filter、startup、ordering、join condition、
join filter 数量。expression 根据角色和序号取得表达式身份，ordering
同时返回当前宿主排序枚举；其他角色没有排序值。

表达式信息包含种类、SQL 类型、collation、数值 precision/scale、
参数数目、列引用 table/column ID、常量/非空属性和可选插件逻辑类型。
普通 SQL 类型不伪装成某个插件类型；无插件元数据时 type_id 为空。
列引用是当前查询中的标识，不承诺直接等于持久表对象 ID。

这些枚举属于匹配的宿主构建，并不是长期冻结的公共枚举。字符串精度
union 不作为数值精度返回。不提供 C++ 类指针、ObDatum 布局或借用的
常量数据；常量值读取与参数绑定需另行设计。

## 为什么不直接调用 get_op_exprs

候选选择早于 ALLOC_EXPR/PROJECT_PRUNING。部分内核 get_op_exprs
实现会生成 table scan access expr 或 join partition expr，提前调用
可能改变规划状态。本接口直接读取已存在的字段，不调用这些生成过程。

五种表达式角色不是完整的算子依赖集，也不是输入/输出列集合。显式
输入依赖、输出生产者和表达式分配现已通过[显式布局](plugin-custom-layout.md)
接入；v4 查询目标补齐 SELECT 列表入口。后续仍需更完整的关系属性与
执行位置契约才能构造多输入等路径，不能用只读信息替代这些工作。

## 示例与验证边界

Rust candidate 从 v4 开始在构造 spool 前读取真实规划元数据，并在构造及
下游 hook 返回后检查自定义节点仍以原根节点为输入。它依然是单输入
等价 spool 示例，不是通用 join、GPU 或远端过滤算法。
v6 进一步读取完整 SELECT 目标，构造/next 后核对数量与首项身份。

SDK 测试涵盖 C/Rust 布局、句柄生命周期、结构/返回值验证和吞错。
kernel fixture 使用真实 C++ 逻辑节点与表达式验证共享身份、五种
角色和只读边界；真实 SQL 路径另经 loader 调用 Rust 插件。受控图
与 schema/session fixture 不构成实库扫描、权限或跨进程 PX 的证明。
最新执行结果见 [实施进度](plugin-implementation-status.md)。
