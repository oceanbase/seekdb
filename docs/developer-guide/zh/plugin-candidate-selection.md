# Server-dev：Rust 参与真实候选计划选择与构造

本页描述后期选择阶段。2026-09-10 新增独立的
[关系候选贡献阶段](plugin-relation-paths.md)，在上层排序/聚合前保留
原生候选并追加实现，不隐式重复执行本页的选择 hook。

当前接入 `ObLogPlan::get_minimal_cost_candidate`，让通过宿主绑定准入的
插件决定本次候选集合中的选择结果。新增的 spi_minor=1 还允许请求宿主
构造 materialization 候选并参与同一集合的选择。后续新增 kind=2 的
Rust 自定义等价路径，详见 [自定义 SQL 执行](plugin-custom-executor.md)。
这仍不是完整 join/access path、多输入或任意结果 schema 的开放接口。
新增 spi_minor=2 的[只读规划图](plugin-candidate-graph.md) 提供候选根、
子节点、已有谓词/排序/join 表达式和调用期表达式身份；不调用表达式
生成流程，也不将这些字段称为最终输出 schema。

## 调用链与权限

候选比较点 → 模块 provider → loader 固定完整 hook 链 → Rust hook v2
调度器 → Rust SDK callback → C++ 候选读取/选择桥接 → 最终结果检查。

hook point 为 `optimizer.candidate.select.v1`。仍使用已有 optimizer
descriptor 与统一 registry 的优先级排序、epoch 与对象/实现 lease。
调用前检查完整链，不持 registry/module 锁调用插件；实现代码的所属模块
必须已经通过 Server-dev 宿主绑定准入。仅声明 hook point 的 Public
插件不能得到执行权。准入事实保存在宿主模块记录中，不在每次回调时
重新相信插件可修改的 manifest capability。

每次输入由已有规划器提供。选择索引不改变候选的语义、属性或所有权，
也不跨越此次候选集合；它替代的是这个位置原有的默认选择规则。它不是
对完整查询所有 join/access paths 的统一访问接口。

## Rust 接口

`seekdb_extension::candidate` 提供 `Hook`、`Mode`、`Service` 与
`Registration::candidate_hook`。后者通过现有事务式 descriptor 注册
路径发布，不为深度插件另建 registry。

回调的 `Context<'_>` 可以：

- `count()` 获取当前候选数量。
- `get(index)` 读取 cost、rows、width 与当前宿主构建的 operator type。
- `select(index)` 将某个候选设为结果；最后一次有效选择生效。
- `call_next()` 调用后续策略/默认选择，并通过 `database_error()`
  保留精确的下游数据库错误。

Around 成功时必须恰好调用一次 next；Replace 可以不调用 next，但整条
链成功时必须已有合法选择。允许 around 在 next 成功之后改变选择。
如果先 select 再 next，后续策略可以覆盖它；默认选择会清除先前的临时
选择再按原规则计算，避免把前序选择误当作额外的最低代价候选。

SDK 不把 C++ `ObLogPlan`、`CandidatePlan` 或逻辑算子布局交给 Rust。
数值信息按 C ABI 复制，查询数据留在宿主；Context 同步借用、非 Send，
不能放到异步任务或保存到回调结束后。这是版本绑定的深层 API，不属于
Public optimizer v1 的兼容承诺。

## 结果与错误规则

宿主的最终检查在所有 around 后处理结束后执行，验证选择确实来自本次
候选集合。越界 get/select 或错误的输出结构大小会记录失败；即使原生
插件随后进行了有效选择，也不能恢复成成功。失败时清空结果指针，不能
让调用者误用部分完成的选择。

SDK 同样保留首个操作错误，阻止插件吞掉错误后返回 OK。Rust 调度器
保留下游精确错误，重复 next 不会再次进入默认选择。成功回调没有选择
不是合法的 replacement。内核对象不会因选择而获得插件 vtable 或新的
插件私有生命周期，因此当前桥接不需要在执行阶段延长这些选择回调的
lease；原 SQL 对象/函数的执行绑定仍按现有机制固定。

## 验证方式

`plugin_candidate_rust` 实际编译 Rust SDK callback，连同 C manifest
组成 DSO，对最终 loader executable 生成宿主绑定。Rust 自己通过 SDK
注册 hook。九个变体覆盖 replacement/around 改变默认结果、无选择、
越界读取/选择、around 缺少 next、重复 next、下游错误与 Public 代码
错误接入。各次调用后检查 lease 归零，退出后对象与服务释放。

策略故意选择代价更高的候选，目的是证明宿主采用 Rust 决定，而不是
默默再次选择默认最小值；这不是建议用于生产的优化策略。

SDK 测试还检查 C/Rust 字段布局、非 Send、非法上下文、宿主错误返回和
错误信息的保留。测试中 catalog/产物验证器为明确的替身，不是实库
权限或签名验证证据。

完整 SQL kernel 回归已通过：先链接最终 kernel executable，再生成
宿主契约和 Rust 策略 DSO；真实 SELECT 的规划过程中断言该调用点确实
执行，随后继续 codegen 和原有执行断言。另在实际 `ObLogPlan` 上构造
两棵不同代价的逻辑算子候选，对比默认选择与 Rust 选择。此受控候选对
只验证选择结果，不作为可执行计划；真正执行的是前述正常生成的 SQL
计划。宿主侧故障注入覆盖非法 select/get、缺少选择和错误返回，均验证
结果被清空，不将局部选择留给调用者。

本轮最终生产构建、28 项完整 CTest、完整 kernel 回归、12 项 build
gate、源码边界与格式检查通过。首次 SDK 测试曾遇到链接器 SIGSEGV，
未修改链接参数，原样重跑和随后完整回归通过；不宣称已经修复链接器。
Rust runtime 总测试入口的 118 项单测也通过。DSO 专用 Rust fixture
位于 tests/fixtures，避免 Cargo 自动把它编译成 runtime 的独立测试，
不增加 runtime 对 SDK 的依赖。实际安装 plugin-sdk 后，新头文件通过
C11 编译检查。

## 尚未完成

单输入自定义路径已接入独立逻辑节点、物理算子、codegen、执行、取消
与 EXPLAIN，当前验证状态见自定义执行专题。还需扩大 schema、路径
属性/代价重估、缓存失效和跨进程执行协议，不能把首个等价路径样例
作为 PG 式自由度的终点。

Server-dev 的独立 SDK 分发与通用同构建 adapter 交付仍需整合。后续已
增加原生 Rust 打包入口，见文末；混合 Rust/C 错误矩阵不是完整开发工具链。PG 式自由度、
索引/类型/存储、实库事务与 AI 异步预算等完整目标保持不变。GIS 仍可用
C++ 实现。另见 [总体设计](plugin-next-design.md) 和
[Server-dev 构建 profile](plugin-server-dev-contract.md)。

## 后续进展：宿主拥有的候选构造

`candidate::Hook::BUILDERS = true` 声明使用 spi_minor=1。SDK
`Context::materialize(input_index)` 返回新候选的稳定索引，不自动选择。
`count/get/select` 与后续 hook、默认代价选择共享扩展后的集合。旧 minor=0
回调继续收到精确大小的 v1 view；每次进入旧回调前刷新候选数量。

当前构造协议的 kind=1 由既有 `allocate_material_as_top` 工厂创建真正的
逻辑 MATERIAL，属性推导与代价来自宿主，不接受插件伪造 cost。单次比较
最多追加 64 个候选，输入也可引用已追加的候选；索引不移动。按需复制
原集合，无插件或只选择时不创建扩展数组。

构造失败不返回半成品索引，越界、未知 kind、保留字段、容量上限和分配
异常均有错误路径。宿主记录首个错误并提供精确数据库错误值；插件忽略
失败再返回 OK 也不能改变结果。C++ 异常与 Rust panic 均不跨越 C ABI。

内置工厂设置 child 时会修改原候选的 parent。桥接用作用域恢复保护，
构造结束即恢复原 parent；仅最终选中新增路径时才连接该路径的新增
节点链。未选中和失败时不污染原候选。节点由原计划 allocator/factory
管理，不携带插件 vtable；未来自定义执行节点仍需独立的资源与 module
lease 契约。

该轮真实 Rust DSO 矩阵扩大到 13 种，验证默认候选集合扩展、选中新增项、
missing-next、越界构造与吞掉宿主错误。SDK 增加动态索引/数量、完整 v2
上下文、保留字段和错误回包检查。完整 28 项 CTest 已通过。

完整 SQL kernel 回归验证真实规划产生的 MATERIAL 出现在 EXPLAIN 和
物理计划中，ASC/DESC × scalar/batch 共四个构造样例完成执行与 rescan，
并继续原先排序/PX 基础矩阵。真实宿主故障注入检查未选中路径不改变
parent、构造后超时、未知 kind、65 次构造和非法保留字段；失败输出清空。
这四个新增样例不额外声称自定义 PX 算子、跨进程计划序列化或索引能力。

## 后续进展：纯 Rust 示例与开发入口

新增 `plugins/rust_candidate`，manifest、注册、生命周期与 hook 均为
Rust，不依赖 C manifest 外壳。它使用 SDK `server_dev::bind` 和构建器
生成的宿主 ID，沿用同一个 registry、loader 与候选协议。独立源码工程
可以通过 `seekdb_add_rust_plugin(... STANDALONE SERVER_DEV_HOST ...)`
构建，再使用现有 cargo-seekdb package 交付。

完整 kernel 现在对最终测试 executable 构建并打包这个未修改的示例，
加载包中的实际 DSO，重复前述四个 materialization 规划/执行样例。
纯 Rust 与混合 Rust/C 构造样例均通过，仍保留原始选择和排序/PX 矩阵。
这证明纯 Rust 交付路径进入真实执行链，不将模拟候选池测试代替 SQL。
打包、宿主绑定与独立 SDK 分发之间的边界见
[示例说明](../../../plugins/rust_candidate/README.md)。
