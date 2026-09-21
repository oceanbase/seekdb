# Rust Server-dev 候选构造示例

这是纯 Rust `cdylib`：manifest、注册、生命周期和候选策略均由 Rust 编写，
不再链接一个 C manifest 外壳或宿主 Rust runtime。依赖只包含 extension SDK。

后期选择示例在真实候选选择点提交 `candidate::CustomPath`，绑定自己的
spool service，调用 next 后选择新增项。宿主生成 `PLUGIN CUSTOM`，
缓冲与重放算法由 Rust 执行，不调用内置 MATERIAL。
**它故意强制选择新路径，用于验证扩展能力，不是生产优化策略。**
不要在普通生产实例中无条件启用：当前为单输入、保持原 SQL 列语义的
等价路径；v9 另有下述双输入整数 JOIN 策略，仍非通用生产优化器。
执行期已可通过 Context 的 input_schema/output_schema 在取行前读取
类型、编码、NULL 与 SQL 元数据；spool 会先验证输入/输出适合等价重放，
空输入也能验证。借用描述不能保留到回调之后；旧 v1 上下文不伪装为空
schema，而由 has_schema 明确区分并保留原字节路径。

从 v4 开始另外通过 `Hook::INSPECT` 读取规划图：候选根、子节点、已有谓词和
排序表达式及其参数拥有调用期 ID。构造和下游 hook 返回后检查新增
路径仍引用原输入。这些只读字段不是最终输出 schema，也不触发内核
表达式分配。详见[规划图与表达式身份](../../docs/developer-guide/zh/plugin-candidate-graph.md)。

## 后续扩展：SJD1 多输入参数化调度

SJD1 用显式拓扑顺序、逐输入绑定/独立重扫标记和输出映射，执行多层
嵌套读取。每个输入至多保存一行，所有 owned 行共享预算。它使用现有
v4 执行控制，不写任意宿主参数，不把 NULL 来源按等值键跳过。
乱序物理输入的链式依赖、多来源汇入、空中间输入、NULL/空值/70 KB
字符串、绑定重扫失败、恢复和关闭已通过完整 kernel 的物理夹具。

宿主规划与 codegen 已加入多 owner 参数映射；后续 `planner/multi.rs`
进一步自动识别满足条件的左深相关 INNER JOIN，构造 SJD1 候选，保留
完整右侧消费者及隐藏列。十八项插件单测及完整 kernel 通过：新增
三输入相关 SQL 在标量/batch=3 下分别运行原生与 Rust 路径，正常
codegen 产生 SJD1 三子输入 spec，五个参数、结果、取消清理及整体
重扫通过。SQL 用 LEADING/USE_NL 指定原生形状，不证明任意 JOIN
枚举子问题已开放。仍不能用“支持重扫”推断任意易变/外部输入可以
重复执行。
详见 [多输入参数化](../../docs/developer-guide/zh/plugin-multi-parameter.md)。

## v18：计划边界值与延后计算

upper-policy 使用 `Hook::VALUES` 和 v8 `value_info`，不再把关系 ID
包含关系当作表达式可用性证明。SDK 在 Rust 中拆解后续计算的依赖，
保留已产生的 GROUP/WINDOW 结果，在 DISTINCT／显式插件输出处检查
投影边界；未知节点不会伪装成可透传全部输入。

排序策略保持 SELECT 计算的原位置，只传递所需已有值；MAX、ROW_NUMBER
可作为已有排序键，`ABS(x)+10`、聚合加法及窗口加法不进入插件输入／
输出计算槽。排序算法仍是 SSO1，未新增聚合或窗口算法实现。

生产、SDK 全量回归／29 项文档测试、28 项插件单测及完整 kernel 通过。
kernel 在标量/batch=3 下完成 24 次实际 SORT 替代检查，保留既有
物理、Top-N 与 JOIN 矩阵。生产 v18 DSO 重建／审计、独立重建后的
30/30 CTest（115.12 秒）、生产契约下 28 项插件单测及 124 项 runtime
单测重新通过。详见
[计划边界值](../../docs/developer-guide/zh/plugin-plan-values.md)。

## v17：Rust 排序算法替代

upper-policy 通过 v7 SORT 语义读取实际键和方向，贡献 SSO1 片段：
保留的是原 SORT 的子输入，由 Rust 算法完成多整数键、四种方向／NULL
组合的排序，输出布局与键布局独立。不是仅在原生 SORT 上方缓冲结果。
插件负责 owned 行、稳定平局、有界资源、比较期取消和重扫；宿主负责
通常的表达式分配、codegen、类型转换与输入执行。GIS 仍使用 C++。

普通整数全排序、隐藏键、直接列载荷与已有键结果已接入；不提前求值
其他 SELECT 计算。特殊排序语义保留原生候选；通用阶段需求／已求值
结果协议、其他类型比较、spill、生产成本模型仍需继续实现。
这组策略仍是 additive/Around，不在贡献阶段强制选择。

生产重建、完整 kernel、对应 v17 DSO 重建与二进制审计通过。kernel
包括九组物理排序、十组新增 SQL 和既有十组上层／三十四组 JOIN SQL；
在标量/batch=3 下检查真正替代原生 SORT、结果、取消和重扫。
首次真实替代断言暴露并修复了 core 整数列的规划类型识别；CASE 各分支
的确定性检查也已补齐。独立重建后 30/30 CTest（102.34 秒）、生产
契约下 28 项插件单测和 124 项 runtime 单测通过。具体范围和限制见
[Rust 排序](../../docs/developer-guide/zh/plugin-rust-sort.md)。

## v15：上层关系贡献

新增 GROUP、WINDOW、DISTINCT、ORDERED 四个独立注册，使用同一个
Rust upper-policy 服务贡献保序 spool，不在贡献阶段选择赢家。策略
只处理本地串行候选；完整关系、JOIN 子问题及原选择策略保持独立。
五个服务、七个扩展须与 v15 manifest 一起部署。

这是上层调用点与组合协议的参考，不是聚合/窗口/排序替换算法。
生产构建及 SDK 回归通过；完整 kernel 的十组上层 SQL 已在标量与
batch=3 下通过正常 codegen、真实 Rust 执行和重扫，四阶段均实际
贡献，覆盖 MAX 分组、ROW_NUMBER、重复值去重、降序及 top-N。
SQL 夹具显式选择贡献的根验证执行，不证明生产成本优势。生产对应
DSO 重建/审计以及独立目标重建后的 30/30 CTest 通过，包含真实
四阶段 Rust 回调、构造失败、非本地串行回退及独立打包/宿主绑定。
详见 [上层关系候选](../../docs/developer-guide/zh/plugin-upper-paths.md)。

## v14：局部相关关系

子问题策略新增 SJC1/SJD1 参数交接：保留属于当前关系的列及全部
合法参数来源，省略明确属于其他关系的查询列。完整关系策略不变。
新增三表/四表 SQL 验证相关插件子计划与上层原生 JOIN 的组合，
包括嵌套插件输入、取消和重扫。二十一项插件单测及完整 kernel 通过：
三十四组 JOIN SQL，三表/四表在标量及 batch=3 下比较原生/Rust
结果；四表实际组合两个 SJC1 节点，分别管理两个和三个参数，位于
原生 HASH OUTER JOIN 下方。正常 codegen、取消、重扫和参数清理
通过，生产对应 v14 DSO 已重建并通过二进制审计。不是任意查询形状、
多插件或跨查询块/PX 验证。独立测试重建后 30/30 CTest 通过，包含
独立复制的 v14 插件构建/打包/宿主绑定；生产契约下二十一项插件
单测和 124 项 runtime 单测重新通过。

## v13：连接枚举子问题

新增独立 `optimizer.join.paths.v1` 注册和 Rust 子问题策略。它按
部分关系作用域保留所需列，可以贡献局部整数 INNER JOIN，目标是
由上层原生 JOIN 继续组合。完整关系策略、相关 SJC1 和多层 SJD1
保留。新阶段不隐式替代原 relation.paths/candidate.select。

生产重建和完整 kernel 已通过：三表 SQL 的内层 PLUGIN CUSTOM
实际参与上层原生 HASH OUTER JOIN，标量/batch=3 下比较原生结果，
并检查 EXPLAIN、取消及重扫。整组发布和路径生命周期夹具也通过。
这不是任意连接分解、局部参数拥有权、PX 或生产成本模型的证明。参见
[连接子问题](../../docs/developer-guide/zh/plugin-join-subproblems.md)。

## v12：参数化片段与相关 JOIN

join-policy 通过 `custom_bound_fragment` 承接满足条件的原生 NestedLoop
参数列表，保留左右子树，贡献 SJC1；执行服务 minor=2 通过宿主持有的
来源快照 `bind_rescan_input`，由 Rust 驱动左行/绑定/右输入循环。
根节点须无剩余待处理谓词，参数源属于左输入；不开放任意参数 frame。

完整 kernel 已验证相同相关 SQL 的原生和 Rust 路径、两参数、
结果及重扫；新增第一/第二次绑定后取消和清理用例随再次完整 kernel
通过，47 个原生执行协议变体随 30/30 CTest 通过。独立输入 SJE1
和显式流式 SJR1 保留，详细范围见
[参数化执行](../../docs/developer-guide/zh/plugin-parameter-execution.md)。

后续两组参数快照物理用例已通过完整 kernel，覆盖 NULL、空值、
含 NUL/UTF-8、70 KB 的 VARCHAR/in-row LOB；绑定前改写来源 buffer
仍获得正确参数和 Rust 输出，重扫失败/提前关闭不改动无关参数。
宿主已进一步开放来源输入的独立重扫，撤销旧快照并要求重新读取与
绑定；生产构建与新增路径的完整 kernel 均通过。默认 Rust 策略
不会自动增加来源扫描次数；这项控制能力留给明确需要它的算法。

宿主后续已将输入就绪与传递失效迁入 Rust runtime，实际相关 JOIN
通过新的状态桥接执行，生产及完整 kernel 回归通过。参考插件使用的
v4 契约不变；通用状态图并未放开任意参数拥有权或改变默认策略。
见 [Rust 输入状态](../../docs/developer-guide/zh/plugin-input-state.md)。

## v11：逐输入重扫与显式流式参考算法（历史检查点）

执行服务通过 minor=1 理解 custom_context_v3，SJR1 可驱动右输入逐行
重扫，保留至多一行左/右输入。其键/输出字段与 SJE1 一致，整数、NULL
及输出 schema 校验保留；两行共享 owned 数据预算，取消和失败后只能
由整个算子重扫恢复。该接口不设置参数，也不保证重复执行得到相同行。

默认 JOIN 规划策略继续使用 SJE1：在开放输入易变性及重复执行语义
之前，不能把所有输入自动切换成重扫。新流式算法的四组物理用例随
完整 kernel 通过，原有十六组 JOIN SQL 保留通过；重建独立测试后
30/30 CTest 通过，详见[输入控制](../../docs/developer-guide/zh/plugin-input-control.md)。

## v10：读取参数绑定，保留相关输入语义（历史检查点）

join-policy 使用 candidate v6/minor=5，通过 PARAMETERS 读取 JOIN
的 NestedLoop/LeftPushDown/RightPushDown 绑定。当前执行算法把
两个输入当作独立流，因此遇到这些绑定时不贡献候选。接口还允许
深度插件枚举参数与来源表达式，不只提供一个“不支持”标记。
生产构建与审计、SDK/插件单测及完整 kernel 通过；十六组 JOIN SQL
包含八组 Rust 候选和八组内核回退。新增相关表函数在标量/batch=3
验证参数来源、逻辑类型、结果和重扫；独立测试重新构建后 30 项
CTest 通过，包括 v6 loader 准入及旧服务 ABI 前缀兼容。
真正由插件接管参数绑定和子计划逐行重扫仍需后续执行协议。

## v9：Rust 自主贡献 JOIN 候选（历史检查点）

新增独立 join-policy 服务，通过 v5 语义／依赖／查询列接口识别整数
内连接，在 Rust 中决定键方向、列映射、SJE1 参数与参考代价。正常
SQL 夹具已移除 C++ JOIN 策略；不适用的计划继续交给内核。生产 DSO
构建/审计与完整 kernel 回归通过：十四组 SQL 覆盖正向贡献、隐藏
排序列及不适用语义的原生回退。30 项独立 CTest 通过。十一项插件
单测通过，新增策略 ABI 用例进一步覆盖物理输入交换、首候选跳过、
隐藏列/去重映射、语义回退与宿主错误；不将 mock 图等同于 SQL。
详见[规划语义与 Rust 策略](../../docs/developer-guide/zh/plugin-planner-semantics.md)。

## v8：Rust 双输入整数等值连接（历史检查点）

执行 service 增加 SJE1 私有计划，声明两路键和输出来源。Rust 自己
复制输入、比较整数键并逐个输出匹配对，保留重复匹配、排除 NULL 键，
支持取消、重扫与共同预算。它是嵌套循环参考算法，不是默认生产策略。

正常 SQL 的双输入 fragment 夹具已接入。首轮验证发现后期 hook 只收到
SORT 根，已新增独立的提前关系候选贡献阶段，修正后完整 kernel 通过：
保留宿主 SORT，双输入 PLUGIN CUSTOM 使用 Rust 算法，标量/batch=3
覆盖完整结果对、NULL 和取消后重扫；
策略为 graph/build C++ 测试回调，算法是实际 Rust DSO。默认 Rust
hook 仍为全列 spool，不把独立 SDK 与执行测试合称通用 Rust 规划器。
详见[Rust 双输入连接](../../docs/developer-guide/zh/plugin-rust-join.md)。

## v6：读取当前查询块的完整 SELECT 目标

`Hook::QUERY_TARGETS` 要求 candidate service minor=3；宿主提供 v4
query/target。插件枚举完整 SELECT 列表、读取元数据，并在构造/next
之后核对数量及首项身份。目标包括不在排序/过滤角色中的 SELECT 表达式，
与 graph 共用调用期 ID，重复项保留 SELECT 顺序。

这不是最终子节点输出 schema，也不表示任意候选都能计算聚合/窗口等
表达式。默认策略仍提交空计划进行全列 spool，不擅自把查询目标当作
所有候选的输入布局。接口与受控显式输出用例的边界见
[查询目标](../../docs/developer-guide/zh/plugin-query-targets.md)。

## v7：多输入执行算法

执行 service 另支持 SMP1：每个输入具有独立列序号映射，轮询读取各
输入并在 Rust 中缓存投影后的 owned 行。所有分支的 schema 在任何
取行前校验；0 输入返回空关系，零列输入/输出仍保留实际行数。行数
和内存预算由全部输入共享，支持整体重扫和取消。

宿主物理适配现在支持最多 64 个 child 及独立输入 codec。**默认候选
hook 没有改为多输入构造**。后续 SDK 已增加 `custom_fragment`，将
目标结果与实际输入分开，并接入本地单并发的关系/依赖/属性处理；
两输入逻辑夹具及正常 SQL 单输入用例通过，多输入 SQL 联动仍待完成。
SMP1 是参考轮询合并算法，不是已实现的通用 join 或 SQL UNION
优化策略，详见[多输入执行](../../docs/developer-guide/zh/plugin-multi-input.md)
与[计划片段](../../docs/developer-guide/zh/plugin-plan-fragments.md)。

## v5：Rust 投影算法与独立执行布局

spool 新增插件私有的版本化计划格式：`SPJ1` 四字节、little-endian
u32 输出列数、相同数量的 u32 输入列序号。支持重排、删列、重复列和
零列输出；空计划字节仍表示旧的全列等价重放，不等于零列投影。
最多 1024 个输出列，拒绝截断、尾随字节、未知版本和越界序号。

显式投影必须有执行 schema。即使子输入为空，也会在取行前检查映射、
输入与输出的逻辑类型、编码及 SQL 元数据；不允许收紧 NULL 约束。
对于相同 SQL 类型和编码的 builtin 整数，precision 的显示元数据差异
不改变值表示；浮点/自定义类型及旧全列路径仍要求 precision 相同。
输入 decode 与输出 encode 可分别选择，Rust 只操作逻辑值，不解释
持久格式。跨回调保存的投影结果归 Rust 所有；重复列计入内存预算，
零列结果仍保留输入行数，rescan 保留计划并重新读取输入。

宿主 `PluginCustomSpec` 现在可保存独立输入表达式、类型、NULL 及 codec
绑定，输出继续使用自身列布局。**当前候选 hook 仍提交空计划**；使用
该默认策略的正常 SQL 仍走原列等价路径。投影执行有独立物理 spec 验证，
不能当作优化器已开放任意关系或多输入计划的证明。SDK 现已另提供
`custom_with_layout`，将规划图表达式身份接入输入依赖和输出生产者；
正常 SQL 的受控策略验证与本示例默认策略分开记录，见
[显式表达式布局](../../docs/developer-guide/zh/plugin-custom-layout.md)。

## 同源码树构建和打包

需要开启 `SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS` 的 Linux CMake 构建、
Python 3.11+、当前工程的 Rust 工具链与离线依赖。完成配置后执行：

```sh
cmake --build build_plugin_overlay_verify --target seekdb_rust_candidate_plugin -j8
cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml -- \
  package --build-dir build_plugin_overlay_verify \
  --target seekdb_rust_candidate_plugin --output /tmp/new-rust-candidate-package
```

输出目录必须不存在。命令构建、审计并复制动态库与 plugin.toml，不签名、
不发布，也不向运行中的服务器安装插件。构建失败保留 incomplete 标记，
不会复制此前残留的旧动态库。该示例为显式构建目标，core 不反向依赖它。

`plugin.toml` 声明 `api_profile = "server-dev"`。构建器等待最终 seekdb
链接，从该 ELF 生成 Rust `HOST_BUILD_ID`，通过 `SEEKDB_SERVER_DEV_CONTRACT`
将生成文件传给 Cargo。源码 `include!(env!(...))` 使用生成内容，SDK
`server_dev::bind` 包装普通 manifest，设置 manifest-only capability、
结构大小与零填充后缀。loader 在 init/start 前校验实际运行宿主。

每次构建都重新核对宿主，契约内容不变时不改其时间戳；换宿主时即使新
文件时间更早，也必须更新契约并重编译。build ID 是链接身份，不是签名、
完整性校验、完整 C++ ABI 指纹或不可信代码沙箱。

## 独立插件工程

可以将插件源码放在另一目录，Cargo 依赖指向相同源码版本的
`rust/extension-sdk`，项目根目录使用以下 CMake 入口：

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyCandidate LANGUAGES C)
include("/path/to/seekdb/cmake/RustPlugin.cmake")
seekdb_add_rust_plugin(my_candidate STANDALONE
  LIBRARY_NAME seekdb_rust_candidate
  MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
  SERVER_DEV_HOST "/path/to/matching/seekdb")
```

`SERVER_DEV_HOST` 必须是明确的绝对文件路径，不能省略或用于 Public
profile。构建工具仍来自匹配的 seekdb 源码树；这不是一个已独立发布、
包含全部 server headers/adapter 的 Server-dev SDK 分发包。

Rust profile 通过版本绑定 C bridge 操作内核，不允许将 `server_headers`
直接作为任意 C++ 布局导入 Rust。需要新增 C++ adapter 时使用单独的受管
构建边界。额外 C 导出必须在 `exports` 中声明，否则最终二进制审计失败。
两档 profile 均保留 Cargo 本地依赖隔离、`-z defs` 与核心依赖方向检查。

## 生命周期与验证范围

manifest 与 service 是模块期不可变数据；运行状态使用 AtomicBool。
status 回调在 Rust FFI 边界内处理错误/panic；Context 不跨线程或逃逸。
SDK 注册仍进入同一个 registry 事务；loader 固定整个 hook 链的引用，
并在调用后释放。GIS 不受语言选择影响，继续由 C++ 实现。

`rust_server_dev_package` 以真实 loader 检查独立构建、打包、候选构造/
选择、宿主切换与恢复、非法宿主、未声明/显式声明导出、禁止覆盖及退出
清理。catalog/产物验证器和候选集合是明确的测试替身，不能当作实库权限、
事务或性能证明。完整 kernel runner 另外对最终 kernel executable
构建该示例，检查正常 SQL 生成的 PLUGIN CUSTOM、EXPLAIN、实际 Rust
服务调用、执行与 rescan；原混合 Rust/C MATERIAL 样例作为独立对照。

详见[候选构造协议](../../docs/developer-guide/zh/plugin-candidate-selection.md)
与[Server-dev 契约](../../docs/developer-guide/zh/plugin-server-dev-contract.md)。

## v3：Rust 自定义 SQL 执行路径

新增 `org.seekdb.rust-candidate.spool`。该 service 自己在 Rust 中保存
输入行并按原顺序重放，支持 rescan 与消费式 close，不调用宿主 MATERIAL。
样例限制为 65536 行和约 16 MiB 保留数据，不是完整 spill/内存预算引擎。

SDK 管理跨回调拥有的数据与调用期借用。当前候选 hook 提交独立的
自定义节点，声明保持顺序及阻塞行为，并提供示例性的每 worker 代价。
宿主复制实现绑定和计划参数，不把 plugin vtable 放进逻辑计划。
SQL 行桥接目前支持有符号/无符号 SQL 整数、float/double、字符串/字节
（含文本 LOB）和对应的插件类型；检查逻辑类型 ID、数值字节宽度、物理
范围和 NULL 属性。普通 SQL 窄整数/float 提升为 int64/uint64/float64；
逻辑 bool/int32/uint32 分别使用 1/4/4 字节。spool 经 SDK Cell::number
实际解码 builtin 数值及六个既有 GIS scalar 兼容别名后再缓存，不将
任意自定义 ID 的相同后缀当成 builtin。
LOB 由宿主读取内容并重建返回头，Rust 算法不解释 locator；行外读取前
检查大小。持久插件列通过计划绑定的 codec 解码后交给 Rust，输出由
宿主编码回 SQL 表示；Rust spool 不需要了解各类型的持久格式。Delta
LOB 及其他尚未接入的物理表示仍明确拒绝，而不是误解释。
当前逐行桥接不承诺批量性能、全类型或跨进程 PX。测试区分自定义执行
与 MATERIAL，最新验证状态见
[自定义执行协议](../../docs/developer-guide/zh/plugin-custom-executor.md)。

## v16：规范化 SORT 语义消费

upper-policy 使用候选服务 minor 6，读取真实 SORT 的键及可选表达式。
四阶段注册不变，保留五个服务／七个扩展；仍保留原生上层算子结果，
不是 Rust 排序算法已经完成。旧选择和 JOIN 服务继续声明自身版本。
详见 [排序语义](../../docs/developer-guide/zh/plugin-sort-semantics.md)。
生产构建、SDK 回归、完整受控 kernel、生产对应动态库审计及独立
CTest 30/30 通过；二十一项插件单测及 124 项 runtime 单测通过。
验证边界和记录见上述文档，不沿用历史检查点作为 v16 证明。
