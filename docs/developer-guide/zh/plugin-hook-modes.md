# 插件 hook：观察、包装与替换

状态：Rust 宿主运行时已实现模式调度；公开 planner 的深度对象接口仍未完成。

## 目标与当前接入

[下一阶段设计](plugin-next-design.md) 要求观察、包装和替换三种 hook，
不能把“成功必须调用 previous/next”作为所有扩展的统一条件。本轮将
这一要求实现为 Rust 的 `seekdb_runtime_hook_run_v2`。它是 C++ 宿主与
Rust runtime 之间的内部 C 接口，不是向独立插件开放的计划构造接口。

`ObPluginLoader::run_optimizer_hooks` 已使用新调度器。公开
`optimizer.plan.v1` 的所有条目仍按 AROUND 注册，因为它只提供元数据
和 continuation，没有创建、替换或验证私有计划对象的能力。公开 ABI、
Rust SDK 的 `optimizer::Hook` 和 Rust text 插件的 build ID 均未改变。
旧宿主 `seekdb_runtime_hook_run` 通过有界栈数组适配到同一套 Rust 引擎，
保留旧契约，不维护第二套调度算法。

## 三种模式

| 模式 | 谁推进后续执行 | 成功条件 | 典型用途 |
| --- | --- | --- | --- |
| OBSERVE | Rust 宿主自动推进 | 观察回调没有 next，也不返回状态 | 前后观测、计数与耗时记录 |
| AROUND | 插件显式调用 next | 必须恰好一次；调用前可返回错误否决 | 包装、策略检查、前后处理 |
| REPLACE | 插件决定是否调用 next | 可不调用；最终状态必须通过宿主验证 | 在允许替换的扩展点接管执行 |

REPLACE 不调用 next 时，后续条目和原始操作均不执行。外层 AROUND 的
后处理及外层 OBSERVE 的 AFTER 仍执行。因此排序不仅决定优先级，也
决定哪些观察者位于替换范围之外。安全检查不能仅作为一个可能被跳过的
下游 hook；宿主必须在调度前完成必要的权限/准入检查。

OBSERVE 采用不同的 void 回调签名，传入 BEFORE/AFTER 与嵌套操作结果。
BEFORE 的结果字段为 0；AFTER 表示内部链的结果，不表示最终验证、事务
提交或持久化成功。观察回调不得修改操作状态；native 代码仍是受信任
代码，少传一个 continuation 并不构成内存沙箱。

## 错误与结果校验

next 最多调用一次；重复调用使整个调度返回协议错误，而且不会再次运行
下游。无论 AROUND 还是 REPLACE，只要调用了 next，就不能吞掉或替换
它返回的非零数据库错误。允许后续接口提供显式恢复协议，但本接口不会
把“回调返回成功”当作数据库操作已经恢复。

v2 强制宿主提供 `validate_result`。只有整条链成功、协议也有效时，才
在所有后处理返回后调用一次；其错误原样交给宿主，不重跑原始操作。
这保证 replacement 不能靠跳过 next 同时跳过结果检查，也能检查包装
回调在 next 之后改变的结果。协议错误或业务失败不再调用该验证器。

验证器检查什么由扩展点的宿主 adapter 决定。当前 planner v1 adapter
只需额外确认核心 continuation 已执行；这是 around-only 的约束，不是
自定义计划合法性验证。未来 planner replacement 必须另行检查计划与
查询的归属、输出形状、分配生命周期、依赖和可执行性。Rust 调度器没有
这些内核知识，不能替宿主证明它们。

## 边界与资源

宿主先获取有序 registry 快照，并在同一 epoch 下固定全部对象与代码。
调度前验证完整表，非法的后续条目也必须阻止第一个回调执行。v2 表为
固定 stride，struct_size 必须精确匹配；未知模式、保留字段非零、
回调与模式不符等均拒绝。最多 64 个条目，旧入口适配不做堆分配。

回调和 continuation 同步、同线程、不能逃逸。宿主在整条链及最终验证
期间保留代码/资源引用，不持有 loader/registry 锁进入插件。C++ adapter
在跨 Rust 边界前处理异常；host Rust 继续使用既有 panic=abort 策略。
独立链的重入由 adapter 限制，现有 planner 保留每线程 16 层限制。

失败时由宿主管理 provisional 结果及资源清理。本调度器只返回错误，
不会自动撤销 native 回调已经做出的外部副作用，也不承担 catalog 提交。

## 验证范围与后续工作

Rust 单测枚举三个位置的全部 27 种模式组合，并验证替换链的错误保留与
后处理后的最终检查。`plugin_hook_modes` 从 C++ 经实际 Rust 宿主静态库
验证替换/跳过、前后顺序、非法整表预检、64/65 上限、重复 next、否决、
缺少验证器和无结果的假成功。该测试的结果对象是受控测试状态，不是真实
SQL 计划；现有 loader/kernel 回归用于验证公开 planner v1 路径不退化。

本轮生产构建、完整 kernel 回归、115 项 Rust runtime 单测、24 项
插件 CTest、12 项 build gate、源码边界与格式检查均通过。该证据覆盖
模式调度与既有 planner 的 around 路径，不证明新插件已能生成替换计划。

接下来需要给深层扩展增加明确的准入和版本绑定宿主接口，再接入真实
candidate path、计划生成/执行/EXPLAIN 与结果验证器，配套 Rust adapter
和插件样例。内部调度支持 REPLACE 不等于这些接口已经提供，也不能代替
type/index 扩展、catalog 事务与轻量化/AI 目标。完整进度见
[实施进度](plugin-implementation-status.md)。
