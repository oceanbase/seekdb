# 计划边界值：让 Rust 插件组合聚合、窗口与延后计算

日期：2026-09-10。v8 规划上下文／service minor 7 已接入；生产、SDK
与完整 kernel 回归通过。不是任意计划替换已经完成的声明。

## 问题与契约

关系 ID 只表示表达式依赖哪些表。`MAX(x)` 已经由 GROUP 产生之后，
不能要求保留原始 x 来重算 MAX；反过来，SELECT 的 `MAX(x) + 10`
又不应该为了插件布局而提前计算。最终 output_exprs 在贡献阶段通常
尚未分配，也不能把全查询 column inventory 当作某个子树的输出。

新增 `value_info(plan, expression)` 使用已有调用期 ID，返回两项事实：

| 字段 | 含义 | 不承诺什么 |
| --- | --- | --- |
| AVAILABLE | 此表达式身份可作为保留子树的结果请求，保留其已有计算阶段 | 不是最终物理槽，也不是任意跨算子的等价变换证明 |
| SCALAR_ARGUMENTS | 普通标量求值使用图中参数，可拆解其数据依赖 | 不代表纯函数、确定性或可提前求值 |

两项可以同时存在；已有值优先。没有标志表示尚未证明，不表示表达式
永远不可用。常量不申请可写的逐行输出。返回和上下文使用 struct_size、
零保留字段检查；失败清空输出并进入原有 sticky error。

宿主只读取既有元数据，不调用可能分配／改写计划的 get_op_exprs：

- GROUP 暴露分组、rollup 与 aggregate 结果，阻断向未分组列追溯。
- DISTINCT 暴露去重表达式，不能随意恢复它下面已经不可用的值。
- WINDOW 增加窗口结果，同时保留输入已有值。
- 显式插件布局以 result_exprs 为边界；非显式的等价单输入布局保留输入。
- SORT、MATERIAL、LIMIT 沿保留输入查询已有值，不因此提前执行新计算。
- 原生扫描／表函数要求当前查询块内相同身份的已解析列。JOIN 依连接
  语义保留合法输入；外连接只对直接列传播，不推断复杂表达式的 NULL 等价性。
- 未接入的算子不伪装为通用透传；非法子树、环与遍历超限返回错误。

这是一项可扩充的语义查询，不是固定算法白名单或最终输出 schema。
最终表达式分配、参数拥有权、布局闭包和 codegen 校验仍然执行。

## Rust SDK 与策略

`Hook::VALUES = true` 隐含此前 graph/query/semantics/bindings/sort 能力。
loader 接受 v8，按每个服务声明的 minor 提供精确前缀；旧服务不解释
新字段，新服务也不能将旧宿主缺少该能力解释成空依赖。

```rust,ignore
let root = context.root(candidate_index)?;
let input = context.child(root, 0)?;
let target = context.target(0)?;
let required = context.required_values(input, target)?;
// Some(values): 保留这些值，由宿主在原位置计算 target。
// None: 未证明，不构造一个假装可用的结果。
```

required_values 在 Rust 中做显式 DFS：遇到已有值就停止下探；常量不
进入行布局；其他标量递归处理参数。用 active/finished 状态区分环与
共享 DAG，去重依赖；句柄保持调用期生命周期，不可缓存到查询外或跨线程。
节点与待处理队列有界，分配失败显式返回，宿主错误不会变成“正常跳过”。

v18 upper-policy 用这项接口生成 SSO1 的输入／输出映射。现有排序键
仍在原 SORT 的子输入上求值，包含已经完成的 aggregate/window 值。
SELECT 的其他计算只传递依赖值，不把其表达式声明为插件计算结果。
这开放了 GROUP/WINDOW 后的排序和延后投影；不是 Rust 已替代聚合／
窗口算法。已有 SSO1 执行算法、资源与取消协议不变，GIS 继续使用 C++。

## 验证与后续

- 生产首轮（36525）因旧容器不支持范围循环失败，改用索引访问后生产
  重建通过（35578）。未通过触碰时间戳掩盖源码变更。
- SDK 全量回归通过（11817），包括布局、依赖共享／常量／环、损坏
  元数据、缺失能力与 29 项 doc-test；插件 28 项单测通过（67788）。
- 完整 kernel（81350）通过：新增图夹具覆盖 GROUP/WINDOW/DISTINCT/
  custom 边界、输出不被改写、错误清零；SQL 要求 MAX、ROW_NUMBER 后
  的外层排序确实由 Rust 替代，并增加延后 ABS、聚合加法、窗口加法。
  最终物理输入与输出槽均不包含这些延后 SELECT 计算。八种排序专项
  用例 × 两种 batch 配置，加上四种既有上层用例 × 两种配置，共有
  24 次实际替代检查；原有 Top-N 回退、九组物理与三十四组 JOIN SQL
  继续通过。窗口内部排序保持原生，不把它与被替代的最终 ORDER BY 混淆。
- 生产对应 v18 DSO 重建／二进制审计通过（83819）；独立目标重建
  （16682）后 30/30 CTest 通过（24221，115.12 秒），包含独立插件
  构建、打包、宿主绑定和新后缀准入／旧服务前缀验证。
- 生产宿主契约下 28 项插件单测重新通过（28778）；十二项构建门禁、
  Rust 格式和 diff 检查通过（78523），源码边界检查通过（50683）。
  最终 runtime 124 项单测重新通过（50080）。

后续仍需扩展各类计划边界、跨查询块和参数消费者、PX／缓存、成本与
spill／资源协议；值查询不能代替 GROUP/WINDOW/索引的算法构造协议。
实库 catalog 事务、权限／保存点／并发、类型／索引恢复、AI／轻量化、
PL／Bazel／平台目标均保持不变。详见 [实施进度](plugin-implementation-status.md)。
