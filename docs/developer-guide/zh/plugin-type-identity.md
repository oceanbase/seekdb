# 插件持久类型：逻辑身份与运行时绑定分离

状态：基础实现已接线；数据库重启与并发回归待执行。不是完整数据库级 Extension 对象管理。

## 直接嵌套标量表达式的运行时身份

`PluginFunctionExpr` 的原始表达式推导会读取直接嵌套插件函数的逻辑返回类型，
不再只根据其物理 varchar 表示将参数归为 `core.type.bytes`。codegen 把对象 ID、
owner/generation、catalog epoch、返回类型和实际参数类型保存到
`PluginFunctionExtraInfo`；类型名由计划 allocator 持有，复制和反序列化都会独立
复制，执行按 C ABI 借用已带 NUL 结尾的参数类型名，不逐行复制这些字符串。

首次类型推导成功后，函数的隐藏名称参数替换成版本化 `PluginFunctionExtraInfo`
wire，固定对象、generation/epoch、结果类型、实参类型及稀疏 stored codec
binding。后续推导和 codegen 读取这份元数据，校验子表达式的类型、存储表示与
直接嵌套 binding 的 epoch，不再查询目录或调用动态结果类型推导。尚未绑定的
名称/参数探测仍可查目录，但不能直接进入 codegen。

wire 使用新建常量，不原地修改可能被共享的旧名称常量；完整长度最多 2 MiB，
参数和 codec 数量继续受既有边界约束。普通 raw copy 沿用查询 arena 约定，PL
跨 arena 深复制拥有独立正文；执行仅使用自有 plan extra-info，不逐行解析
wire 或执行隐藏元数据。未经过类型推导的用户二进制参数不能冒充编译器绑定。
绑定参数从逻辑值变成存储值、类型改变或 codec 身份变化时拒绝旧绑定，需要
显式构造新表达式。目录变更后，旧绑定是否仍可执行由既有 epoch/lease 准入
校验决定，读取旧 binding 本身不自动更新目录版本。

该数据是执行绑定，不是持久列元数据：执行仍通过 provider/loader 获取匹配的
generation lease，不能用零 generation 作为通配。嵌套推导观察到不一致 epoch、
codegen 返回物理布局改变或执行时内外逻辑类型不一致会报状态不匹配，而非静默
重新选择函数。非 NULL 标量结果须匹配绑定中已经确定的逻辑类型；结果回调失败不可被插件
返回 OK 或后续 emit 掩盖。内置类型按完整身份识别，保留既有 GIS 兼容别名；
任意以 `.int64`、`.geometry` 等结尾的自定义类型不会自动获得内置物理解释。

内核回归 `plugin_expression_fixture.h` 使用真实 raw expression、codegen、
extra-info 工厂/序列化和 `ObExpr::eval`，provider 为受控 fixture；它与实际 Rust
DSO 类型/转换测试互补，不等于已验证实库 SQL、优化器改写或完整
计划缓存依赖失效。完整类型身份传播仍需继续实现。

## 投影、派生表与引用表达式

`ObRawExpr` 新增可选 `PluginExprType` 指针；只在插件类型表达式上分配元数据及
字符串，不改变每行 `ObDatum`、`ObObj` 或通用 `ObRawExprResType` 布局。普通原始
表达式仍承担一个指针的编译期空间成本，不能称为完全零开销。字符串归属表达式
allocator，assign/deep-copy 不借用源表达式的 arena；重复推导相同类型不重复分配。
reset/复制普通表达式清除旧身份，表达式等价比较区分不同的逻辑类型和存储表示。

可选标记新增查询期 `catalog_epoch_`，复制和等价判断都包含它。非 stored 的
插件逻辑值必须带非零 epoch；持久 schema 导出的 stored 身份保留零值，直到
使用时绑定 codec。该字段不进入持久列元数据、不改变每行 datum 或公开 C ABI。
函数、cast 和 type-value 从各自固定 binding 设置 epoch；直接/两层派生列、
alias、exec-param 沿现有类型复制路径保留版本。相同逻辑 ID 但不同 epoch 不再
作为同一个表达式绑定处理。

函数选择及固定 binding 校验会检查参数标记的非零 epoch。cast 把源 epoch
传给 provider 的 expected epoch，随后再次校验源/结果标记与 wire；type-value
也要求已绑定源和目标 TYPE 版本一致。编码器在构造时固定 TYPE binding 并检查
已绑定输入的 epoch，后续推导/codegen 校验保存的输入、目标与 codec；普通列转换包装保留该版本，不被目标 schema 的
零值覆盖。stored 的零值不是可执行 generation 通配，仍须正常选择并验证 codec。

插件标量推导将确定的逻辑结果类型附到 raw expression。生成表列解析在复制物理
类型后复制该身份，别名引用与执行参数类型推导沿引用继续传播。后续插件函数绑定
读取该身份，不把派生列的 varchar 自动当作 bytes；codegen 将类型 ID 复制到既有
plan extra-info，执行不持有 raw expression 指针、不按行重新选择 overload。

持久列初始化识别 v1/v2 schema 元数据，附加逻辑身份、owner、格式及版本，并标记
为 stored 表示；不复制历史 generation。已知插件标记但正文损坏会报错。这类列作为
插件标量函数参数时，现在会按下面的计划 binding / LOB / codec 路径解码，不再
一律返回 `OB_NOT_SUPPORTED`。写入编码已接入下述共享列转换与 INSERT VALUES
入口，并能在编码前插入已注册的 assignment cast；完整 SQL 存取支持尚未完成。

`plugin_projection_fixture.h` 使用真实 statement factory、生成表列 resolver、类型
推导和 raw-expression copier，覆盖两层派生表、alias/exec-param、跨 arena 复制、
身份清除及 v1/v2 持久列标记。provider 与输入行受控；它不是实库 SELECT/存储扫描。
UNION/CASE、任意 CAST/包装函数、标量子查询及所有优化器改写尚未完整接线；动态
typmod/shape、完整计划依赖失效和持久类型读写仍属于后续目标。

## 多分支表达式的公共逻辑类型：Rust 选择基础

Rust `seekdb_runtime_resolve_common_type` 和 registry `resolve_common_type` 已提供
统一的逻辑类型选择入口。registry 在锁内取得自有不可变快照及 epoch，锁外调用
Rust；返回独立拥有的类型 ID 和 epoch，不返回指向快照的字符串或可执行 lease。
候选尚未发布时不可见；移除或换代之后必须按新 epoch 重新绑定，即使类型 ID 不变。

当前规则只在输入中已有的已知类型间选择：未知 NULL 不约束结果，重复类型不增加
权重，只采用直接 implicit cast。每个不同源类型到候选类型按最低 cast 代价加 1
计分，同类型不需要 callback；总成本用 64 位累计。最低成本目标并列，或该目标
所需的最低成本 cast 有歧义时明确报错，不依赖分支/注册顺序。全未知或空输入返回
未找到，由 SQL 层决定默认类型；不搜索输入之外的公共父类型或多跳转换。

这是 seekdb 的逻辑 ID 选择策略，不是 PG 完整公共类型规则的复刻。内核数值提升、
字符集/collation、类型参数、shape 和结果物理布局仍需要 SQL 层协调。loader、server
runtime 和 provider 已暴露本入口，CASE 结果分支与 UNION 集合查询类型合并已按下节接线，
不能据此宣称完整多分支类型系统已经完成。各分支使用同 epoch 的隐式转换/存储
解码 binding，后续还需让结果身份经过所有
复制、子查询与计划依赖路径传播；相同 varchar carrier 不等于相同逻辑类型。

loader/server/provider 的 cast 解析接口新增可选 expected epoch，非零时要求选定
cast 的快照版本与之相同。目录改变后即使仍能找到同名转换，也返回状态不匹配并
清空 binding；未找到/非法参数等原有失败仍保留为失败。零值保留原有独立 cast
解析行为。执行时仍原子校验 epoch 并取得对象/实现 lease，不能因为解析阶段验证
过版本就省略执行准入。该扩展只影响 host 内部 C++ 接口，插件公开 C ABI 不变。

`PluginExprType` 已携带查询 epoch，插件函数及直接 cast/type-value 的 wire 与
标记会相互校验，已有投影/别名路径保留版本。但任意内置包装、全部优化器改写、
UNION/CASE 的完整优化路径、子查询与计划失效仍需继续接线和验证；不能只检查新增 cast 的
expected epoch 就宣称所有查询始终使用同一版本。编码器的 TYPE 也已在表达式
构造时固化；这不代表表函数等其他入口或全部查询计划依赖已完成版本统一。

输入上限为 1024 个类型、4096 个 cast，工作向量使用可失败分配，全部元数据都
校验后才返回结果。测试穷举三类型的 4096 种转换图及六种输入排列，并覆盖最大
代价、数量边界、NULL、重复分支、歧义、发布隔离和换代。它们是选择器与 registry
的证据，不替代真实 CASE/UNION、持久存储或完整 SQL 类型系统验证。

### CASE 结果分支接线

`PluginBranchType::prepare_case` 在原有 CASE 物理类型推导之前协调 THEN/ELSE 的逻辑类型。
存在自定义类型或 stored 值时，经 provider 调用 Rust 公共类型选择器；所有
已知输入 epoch 必须与选择快照一致。不同类型使用直接 implicit cast，同类型
stored 值先经绑定的 type-value decoder 转为逻辑值；NULL 不虚构转换 callback。
全部转换/解码准备成功后才替换分支，再由原 CASE 算子推导物理类型与 collation。
结果标记保留目标逻辑 ID 和 epoch，供外层插件函数识别。

只有内置逻辑类型时不使用插件 cast 图取代数值提升规则。能够通过现有插件值
接口表示的原生结果保留其类型/epoch；超出该接口的纯原生结果继续走内核规则，
不伪造 bytes 类型标记。这不是任意内核类型已经拥有完整插件类型描述的声明。

重复推导复用已协调的类型与分支 binding，不重新选择公共类型/转换；epoch
冲突、缺少隐式转换或已绑定分支发生不相容改变时明确失败。CASE 执行仍由原
算子完成，只求值选中分支。自定义比较/运算符、任意 typmod/shape、全部优化器
改写仍未完整覆盖；集合查询接线见下一节。

真实 Rust DSO 的 SQL 用例覆盖同类型分支、隐式转回 bytes、嵌套函数、NULL、
省略 ELSE、未选中分支不执行、原生数值提升及缺少转换的绑定期错误。所有成功
绑定用例还检查重复推导/codegen 不增加 provider 查询。受控 stored-column
fixture 验证解码节点插入、版本冲突不发布部分替换，以及重复推导不重查目录；
该 stored 测试是类型/绑定证据，不是实际持久列的 CASE 存储执行或恢复验证。

### UNION 集合查询类型组合

`PluginBranchType::prepare_set` 复用同一 Rust 选择器，按投影列协调查询组的
逻辑类型。在 `try_add_cast_to_set_child_list` 与 `gen_set_target_list` 原有
物理类型合并之前执行。不同类型插入已注册的直接 implicit cast，同类型
stored 值插入 decoder；所有列准备成功后再发布这些逻辑转换。

保留 seekdb 原有左右查询组逐步合并的顺序，不把整条 SQL 重写成全局无序
类型选择。原生数值提升和 collation 仍归内核；原生 cast 包装保留适当的
逻辑身份/epoch，集合输出同样记录选定身份。尤其对原先未知的 NULL 分支，
在原生转换后标记其目标类型，避免下一层 UNION 将 carrier 误当成普通 bytes。
未改变的内置插件函数仍保留自身 callback binding 使用的类型 ID。

当前接通 UNION ALL 类型合并。最终选为普通内置类型的 DISTINCT 继续走
原生处理；最终仍为自定义类型的去重、INTERSECT/EXCEPT 等比较操作明确
返回不支持，直到 compare/hash 协议接线，不能用 opaque carrier 字节比较
冒充类型语义。插件类型递归 CTE 的 anchor 转换尚待专门实现。纯常量 UNION
转 VALUES 的 resolver 快速路径不接受函数或命名 CAST 节点；其他 VALUES、
优化器重写、递归及所有子查询路径不因本轮接线而自动获得完整保证。

新增真实 Rust DSO fixture 通过真实集合查询 binder 构造输出，并对转换后的
投影进行 codegen/求值，覆盖 NULL 顺序、同类型、转 bytes、内置数值提升、
嵌套集合输出、无转换错误和自定义去重拒绝。集合表达式复制检查独立拥有的
类型元数据；其原生 `same_as` 使用指针身份，不把深拷贝当成同一个集合节点。
受控 stored fixture 检查版本冲突、后续列失败时不发布前列转换，以及解码
节点插入。这些是绑定与投影执行证据；另有下节的完整 SELECT 解析测试。
两者都不是 UNION 物理算子、持久列扫描或恢复的端到端证据。

### 标量子查询与完整 SELECT 解析

`ObRawExprDeduceType::visit(ObQueryRefRawExpr&)` 在标量子查询的物理类型确定
后，从其唯一输出表达式自有复制 `PluginExprType`，保留逻辑 ID、stored
表示元数据和查询 epoch。此前仅复制 `column_types_[0]`，会使
`SELECT (SELECT seekdb_rust_text('hello'))` 丢失自定义逻辑身份。

这条传播不执行插件回调、不重新选择类型或目录版本。已有函数的固定
binding 继续校验子查询参数 ID/epoch，版本冲突仍拒绝。物理列元数据缺失、
输出形状不匹配，或已带插件标记却丢失引用语句时明确失败；改成普通原生
输出时清除旧标记。多列/集合引用不是一个逻辑 datum，不沿用标量标记。

真实 Rust DSO fixture 新增完整 SQL parser → `ObSelectResolver` 用例，
覆盖插件 SELECT、派生表 UNION 后外层消费、嵌套标量子查询、typed NULL、
CASE/UNION/带 LIMIT 子查询组合、参数决定返回类型、普通子查询、EXISTS
以及缺少 implicit cast 的绑定错误。对外层函数还直接检查固定 binding 的
实参逻辑 ID，并检查重复类型推导不增加 provider 查询或执行回调。

测试使用现有受控空 schema manager/guard 满足常量 LIMIT 的解析前置条件；
不装载业务表，不证明真实 schema service 或身份认证。受控表达式测试另外
覆盖自有复制、陈旧 epoch、物理类型冲突、引用缺失和标量/集合转换清理。
这仍是完整 SELECT 解析和类型绑定证据，不是 optimizer → SubPlanFilter/
UNION 执行的端到端证明；相关子查询、行值子查询、全部改写、零行/多行
基数行为、持久列扫描与计划失效仍需要后续验证。

## 参数决定的返回类型

函数 descriptor 不填 `static_result_type_id` 时，loader 在选定 overload 后调用
`seekdb_plugin_function_service_v2_t::resolve_result`，要求完整 v1 前缀、SPI major 1
和 result-type minor 2。v1 固定类型服务不变；缺少推导接口的动态函数不能绑定，
不再把未知结果静默解释为 bytes。

推导仅接受逻辑类型数组。对已声明类型的签名，输入是隐式转换后的目标类型；
对 untyped arity envelope，输入是调用方原始类型，未知 NULL 保留为空指针。
此时不执行 cast，不读取参数值，不提供 SQL/session/transaction context。
插件必须按输入类型和 generation 内不可变定义作确定性、线程安全、无副作用推导。

host 在回调期间持有对象/实现双 lease，取得真实实例后释放 loader 锁再调用，
前后核对 catalog epoch；验证返回结构大小、保留字段和完整有界类型 ID。
成功结果写入正常 SQL binding，继续进入上一节的计划信息与执行校验，不逐行推导。
Rust SDK 的 `DynamicFunctionDefinition`、`TypeResolution`、`Call::argument_type`
提供对应入口；`finish` 将可动态构造的字符串复制到 host buffer。

Rust text 样例新增 identity 与 typed-bytes identity，实际 DSO 测试覆盖原样逻辑类型、
未知 NULL 策略、隐式转换后的返回类型及执行。SDK 布局/借用/错误测试与 native
非法服务/非法输出测试补充协议验证。尚不支持 typmod、collation、shape、基于常量值
的类型推导或 PG 完整多态签名约束，也没有以这些测试替代真实 SQL server 验证。

## SQL-facing codec 桥接

server provider 新增 `decode_bound_plugin_type` / `encode_bound_plugin_type`，
经 `ObServerPluginRuntime` 转给 loader 的 bound-type 接口。调用者使用正常 TYPE
解析得到的无指针 binding，不需要把内部 registry 对象或函数地址放入 SQL 计划。
该路径校验结构、有限长度的名称/身份、非零 generation、格式及版本和保留字段，
并核对当前对象的 owner、generation、格式和 flags；随后以精确身份重新获取对象
与实现双 lease，在回调结束前固定模块生命周期。查找与回调之间发生停用/换代不能
静默替换实现。无关 registry epoch 不构成物理格式变化，历史零 generation 仍不能执行。

decode 输入是已经 materialize 的存储字节，空字节表示非 NULL 空值；encode 输入
是有逻辑类型的运行时值，可以是 NULL。codec 仍仅收到 v1 execution context，
不提供 SQL suffix；输出在回调中由调用者检查和复制。公开 C ABI/Rust SDK 不变。

实际 Rust DSO 的 bound 接口回归覆盖 UTF-8/空串/含 NUL 字节往返、NULL encode、
非法绑定/格式/generation、结果 sink 错误、关闭后拒绝，以及回调内双 lease 和可
重入状态读取。它验证 loader 桥接，不是存储执行证据。

## 标量函数读取持久类型参数

raw binding 解析 stored annotation 对应的 TYPE，校验 persistent 属性、稳定对象
身份、owner 和物理格式/版本，要求与函数绑定来自同一 catalog epoch。历史列
generation 不进入执行；实际 codec generation 来自当前解析。

codegen 在 `PluginFunctionExtraInfo` 中保存按参数位置排序的稀疏 codec binding，
普通参数不分配此列表内容。每条 binding 仅含有界值与自有字符数组，逐字段 UNIS
序列化，不 memcpy C 结构体布局；计划复制和反序列化不借用源内存。extra-info
版本升为 2，本实验实现不承诺复用旧版执行计划。

执行先经现有 `ObTextStringHelper` 读取 LOB 内容，再使用已编译 binding 调用
provider decode。输出复制到查询临时 allocator；单次标量调用的解码输出总量
上限 16 MiB。输入 LOB materialize 本身仍使用现有读取器，不能把这个输出上限
视为整个查询的内存上限。输出类型、大小、NULL、保留字段和单次 emit 受检查；
emit 错误保持粘性，缺失输出/重复输出/忽略错误不会进入目标函数。存储 NULL
不交给字节解码器，保留独立 NULL 语义。函数继续接收逻辑类型值，既有隐式 cast
与 lease 验证不被绕过，运行时不重新选择函数 overload。

该路径覆盖插件标量消费 stored 参数。独立列的协议输出、跨逻辑类型赋值转换、
表函数、比较/索引语义和完整实库持久类型读写仍未完成。

内核 fixture 已验证真实 in-row LOB 头读取、`E:hello` 解码为 `hello` 后执行、回调
缓冲区销毁后的内容所有权、NULL、codec 错误、计划复制/序列化与截断拒绝。LOB
service fixture 不提供 out-row I/O，provider 也受控；这不替代真实数据库存储回归。

## SQL-facing cast 选择与执行绑定

Rust runtime 新增直接 cast 选择器，经 registry 的 `resolve_cast` 使用同一不可变
快照及 epoch，释放 registry 锁后进行选择。声明为 implicit 的转换可用于 assignment
和 explicit；assignment 不参与隐式函数参数转换，explicit 不自动参与赋值。匹配
源/目标逻辑类型和允许场景后选择最低代价，同代价歧义明确报错，不按对象名称
或注册顺序决定行为。不自动拼接多跳转换；同类型和未知 NULL 由 SQL 调用方处理。

loader 的 `resolve_sql_cast` 返回无指针、自有字符数组的
`seekdb_plugin_sql_cast_binding_v1_t`，记录源/目标类型、使用/声明场景、对象、owner、
generation 与 catalog epoch。失败清空输出。`execute_bound_cast` 原子检查 epoch
并取得该对象及实现的双 lease，再检查声明/类型，不重新查找最低代价转换或换用
新 generation。已取得引用的调用可完成；之后的目录变更要求后续调用重新绑定。

调用通过 server runtime/provider 暴露给 SQL 层，已有函数参数隐式 cast 执行也
改用同一个 Rust 选择器。实际转换仍使用现有 function service，并按其版本协商
SQL context suffix；结果由调用方同步校验和复制。新增的是 host SQL binding，
不改变现有 C/C++ GIS 入口或 Rust 插件回调 ABI。

测试覆盖 3×3 场景矩阵、最大代价、歧义、非法元数据、C++→Rust 快照选择、原子
epoch/lease 校验，以及真实 Rust text DSO 的 UTF-8/空串/内嵌 NUL/NULL、回调错误、
损坏 binding、真实目录变更后旧绑定拒绝和显式重新绑定。这不是实际数据库赋值
回归；loader 层与内核表达式层的验证范围应分别记录。

内部 `PluginCastExpr` 使用值参数和隐藏的版本化 binding 常量。构建时按请求场景
选择一次转换，重新推导类型和 codegen 读取同一 binding；计划 extra-info 使用
逐字段 UNIS 序列化及固定大小自有字段，不直接序列化 C ABI 结构体布局。普通
优化器复制沿用同一查询 arena 的常量共享约定；跨 arena 的 PL 深复制拥有常量
正文。表达式标为 state function，避免在尚未接通 volatility 契约时预计算具有
SQL 副作用或依赖目录状态的 cast。

执行读取计划 binding，经 v2 SQL context 调用 bound cast。stored 源值先通过
真实 LOB reader 获取字节，再调用绑定的 TYPE decoder，最后才把逻辑值交给 cast。
编译时核对 decoder/cast 的 catalog epoch；编码器和直接消费 cast 的插件函数也
核对绑定 epoch。转换结果使用 16 MiB 有界、粘性错误 sink，同步校验类型、NULL、
保留字段和单次 emit，并在插件回调返回前复制数据。未知类型 NULL 不进行 cast
选择；已知源类型的运行时 NULL 则交给选定的 cast，NULL 行不调用存储字节 decoder。

当前内部入口支持整数、浮点、字符串/LOB、geometry 及相应逻辑类型；尚未实现
任意内核类型的 coercion。命名目标的 SQL 接线见下文。不能把 byte
carrier 接通等同于完整 PG 类型系统、比较/索引语义或真实持久列读写完成。

### 插件值到内置 SQL 类型的显式转换

普通 SQL `CAST` / `CONVERT(value, type)` 在 postorder 类型推导阶段，已经完成
源列类型解析后，调用 `PluginCastExpr::coerce_sql_cast`。插件逻辑类型先按
explicit 场景选择到目标 core 类型的已注册转换，再让原有 SQL CAST 执行长度、
字符集等物理转换和约束。stored 源值的 decoder 在插件 cast 之前执行。没有合适
注册转换时返回类型错误，不再把插件 carrier 的 varchar/LOB 表示直接当成普通
文本或数字解释。date/decimal 等尚未建立 wire 类型契约的目标明确返回不支持；
继续扩展这些目标仍属于完整 SQL 类型支持任务。

此入口仅处理显式转换，不拦截编码器外层的隐式 varchar-to-LOB 表示转换。已是
内置逻辑类型的非 stored 值保留正常内核转换；插入 cast 后重复推导也因此不再
重选 binding。不按后缀把 `org.test.int64` 这类用户类型认作内置 int64。

命名插件目标使用下述独立 parser 分支及 typed-value 延迟绑定路径，支持同类型
值和 typed NULL；不能在初步名字解析阶段对尚未确定类型的列调用立即绑定
builder。后续优化器类型传播也仍未完成。

内核回归使用真实 CAST/CONVERT 文本解析、解析后列类型接线、formalize、代码生成
和 frame allocation，对 stored/非 stored 插件值验证 BINARY(5)、BINARY(3) 截断、
SIGNED、UNSIGNED(UINT64_MAX)、DOUBLE 及运行时 NULL。检查 explicit 场景、单次
选择、decoder/cast 调用次数和无转换不回退。源列绑定及 provider 仍为受控 fixture，
不等同于真实 schema service / SQL server / Rust DSO 与存储的端到端验证。

## 命名目标转换的 typed-value 基础

`PluginTypeValueExpr` 将命名 SQL 类型的结果统一为带逻辑身份的非 stored 值，
而不要求所有路径都有一个 native cast 对象。其 `PluginTypeValueExtraInfo`
区分三种模式：typed NULL、已有同类型值、同类型存储值解码。只有解码模式保存
真实 TYPE codec binding；NULL/identity 不伪造 cast ID 或 generation。不同源
类型仍通过已有 `PluginCastExpr` 和 Rust explicit 选择器，再包为相同结果形态。

builder 解析目标 TYPE 的身份与 catalog epoch。NULL/identity 允许没有持久格式
的执行期类型；stored 输入仍需匹配 owner、SQL 类型名、格式和版本，并持有有效
decoder binding。执行返回 query-owned 字节，不能借用随后可能被复用的输入
buffer。NULL 行不调用存储 decoder；解码输出使用已有有界、粘性错误 sink。

隐藏参数的 binary UNIS 元数据和计划 extra-info 保存逻辑 ID、epoch 与模式，
必要时附带 decoder binding。普通函数、native cast、编码器与嵌套 typed-value
读取该包装时核对编译期 epoch；codegen/执行不重新解析目标类型。此处尚不代表
计划缓存已经有完整 catalog 依赖失效协议。

`prepare` 只保存拥有式 SQL 类型名，不访问目录，也不推导尚未绑定的源列。
首次类型推导调用 builder 完成 lowering，并把两项参数的 calc metadata 更新为
新值/二进制绑定的类型；后续推导只读绑定。SQL parser 的
`CAST(value AS plugin_type)` / `CONVERT(value, plugin_type)` 已接到该入口，
支持普通和反引号类型名。目标是标识符而非值表达式，不加入源列待解析列表；
parser 和 recursive resolver 不提前查询目标 TYPE 或选择 cast。限定名、typmod
和数组目标尚未实现，现有内置类型与 `CONVERT(value USING charset)` 分支不变。
未知目标返回 `OB_ERR_INVALID_DATATYPE`，缺少适用显式转换返回
`OB_ERR_INVALID_TYPE_FOR_OP`。

内核回归验证直接/延迟入口的四种输入（NULL、同类型值、同类型存储值、跨类型
cast），经真实 formalize、codegen、frame allocation 和求值，检查不重复选择、
不调用多余回调、NULL、输出所有权、解码空串/内嵌 NUL 和七类输出错误；额外验证
PL 深复制、UNIS/全部截断、非法模式/深度、epoch 不一致和非持久类型的边界。
同一矩阵已扩展到真实 CAST/CONVERT 解析入口，另有完全从字面量/嵌套 SQL 构造
的转换表达式，包括 typed NULL/空串被普通插件函数消费，以及构造函数→同类型
转换→消费函数组合；断言函数/cast 回调次数与执行期不重查目录。这些测试已通过完整生产
构建后的 kernel 回归；provider 仍受控，并非实库命名 CAST 或完整查询计划
序列化验证，也没有证明全局目录更新后缓存计划的失效行为。

新增的 `rust_sql_expression_fixture.h` 另将真实 SQL 表达式接到生产 loader/
registry 和实际 Rust text DSO，而非上述受控类型 provider。16 组回归覆盖命名
转换、typed NULL、UTF-8/空串/NUL、转回 BINARY、构造/消费函数和动态返回类型
组合；非法 UTF-8 使下游函数不执行，同一模块随后可以执行合法输入。runner
每次构建并审计 DSO，实际安装到私有目录，运行完成后检查 shutdown 和注册清空。
该组合回归已经通过；授权/持久 catalog 提交仍为 fixture，未测试真实数据库
或列存储，也未改变 Rust text 类型尚未声明 PERSISTENT 的约束。

## 持久列的写入编码

内部 `PluginTypeEncodeExpr` 将目标逻辑类型的运行时值转换为存储字节。共享的
column-schema / column-reference 转换 builder 插入该表达式；INSERT VALUES
在逐行值入口插入，避免末端 values_desc 已表示目标存储列而跳过编码。原有
column-convert 保留，并在需要时插入 varchar-to-LOB 物理 cast，继续处理列的
表示和约束；外层表达式保留目标列的 stored annotation。

当前接受同一逻辑类型的运行时值及 SQL NULL。同对象、owner、格式和版本的
stored→stored 复制不重复编码；NULL 不调用字节 codec。裸 bytes 或另一逻辑类型
需要注册允许 assignment 场景的 cast，builder 将该转换插入编码器之前；无合适
转换时明确拒绝，不把任意 bytes 当成目标格式。stored 源类型转换则先解码再 cast。
这不是自动搜索多跳 cast：组合链中的每个转换都必须由明确的表达式构建步骤产生。

编码器在 builder 中解析 TYPE 并检查 persistent、对象、owner、格式及版本，
要求源值/赋值 cast 与该 TYPE 使用相同 epoch。私有表达式有两个参数：逻辑
值与最多 4096 字节的 binary binding 常量；不增加用户 SQL 参数或公开 ABI。
`calc_result_type2` 和 codegen 只读取完整 wire，校验 target annotation、源
表示与 epoch，以及直接 cast/type-value 的既有绑定。未携带目标标记的
binary 输入不能冒充编译器构造的 encoder。NULL 也绑定真实 TYPE 版本，但
求值不调用编码回调；同格式 stored→stored 仍不重复编码。

计划 extra-info 持有有界、无指针 binding，逐字段序列化并支持深复制。执行
不求值隐藏 binding 常量，只调用现有 bound encode 桥接，
输出要求为 `core.type.bytes`，使用 16 MiB 有界、粘性错误 sink，并在回调返回前
复制结果。结果最终归属表达式 buffer；错误、重复/缺失 emit、无效类型或保留字段
不会被插件返回成功掩盖。公开 C ABI 和 Rust codec SDK 不需要改动。

内核回归覆盖两个 builder、INSERT VALUES、NULL、相同格式复制、计划所有权和
错误处理，并使用生产代码生成与 frame allocation 执行完整的 constructor →
encoder → physical cast → column-convert 树，以及 bytes → assignment cast →
encoder 的赋值链。第三条完整链从真实 in-row LOB column datum 出发，经 decode
→explicit cast→assignment cast→encode→column-convert。普通文本、空串、内嵌 NUL、
较大结果均校验原始字节、回调次数和不重复选择；独立检查 decoder 错误不进入
cast。另一项回归将实际编码结果解码后交给插件函数消费。provider 为受控测试
实现，不能据此宣称实际 Rust DSO 与持久列已完成端到端 SQL 读写。

固定绑定回归覆盖目录返回新版本时新建 encoder 拒绝、既有 encoder 重复推导
不重新选择、provider 不可用时仍能读取/codegen、每个截断长度拒绝并清空
输出、目标 epoch/源表示改变拒绝、伪造未标记 binary 拒绝，以及 PL 跨 arena
复制后源 arena 释放仍可读取和推导。完整赋值树继续检查推导、codegen、求值
不增加目录查询；执行期仍由原有 bound API 验证版本/lease，不把“可读取旧
binding”解释成“停用后的旧插件仍可执行”。

默认值、prepared 参数、trigger、批量写入、INSERT SELECT、独立列输出和实库
恢复仍需继续实现或验证。下述旧 `type_identity_server.py` 中的 raw-hex INSERT
不证明新编码路径，需要随构造函数/赋值 cast 的实库测试一并更新。

## 表函数的列身份与固定绑定

表函数首次推导保存对象、generation/epoch、实际参数逻辑 ID 和完整列描述
（名称、类型、nullable），用自有 binary 常量替换名称参数。wire 上限 2 MiB，
同时限制参数数与内核列数。之后推导、列名查询和 codegen 只校验已保存绑定；
不因目录变化重新选中另一个对象或另一组列。读取失败清空输出，PL 跨 arena
复制拥有自己的 wire，结果与输入 annotation 必须匹配逻辑身份和 epoch。

列名解析单独识别插件表函数，从固定描述取得名称，不再把它当作 PL collection
查询 UDT。生成列保留逻辑类型供外层插件函数消费；类型映射使用完整 builtin
身份，不因自定义 ID 恰好以 `.int64` 等后缀结尾而套用原生布局。

stored 输入在绑定阶段插入 type-value decoder，执行时先读取 LOB 并解码，
再将逻辑值交给 cursor。首次取行不按名称重查目录，执行和 rescan 使用同一
plan binding，仍受原有 bound API 的 epoch/lease 准入约束。输出 sink 检查
声明类型与 nullable，插件忽略 emit 错误也不能使该次取行成功；close 幂等，
rescan 关闭旧 cursor，下一次取行按固定绑定重新打开。

测试新增完整 SELECT 解析与生成表达式的取行/消费组合、截断、跨 arena 复制、
失配 annotation、列名检查和版本变化检查；另有 in-row LOB 输入解码执行链。
这些使用受控 provider/cursor，不代表真实 Rust table-function DSO 或完整
物理 FunctionTable 算子、持久列 SQL 扫描已验证。后续真实 Rust 验证见下节；
更多类型/批量协议需继续建设。

NULL 当前按表函数 descriptor 处理：未声明 NULL_PROPAGATING 时传给插件，
插件可返回行；声明时在 loader 完成隐式转换后检查，任一结果参数为 NULL
则不进入表回调。未知 NULL 补声明类型，typed NULL 仍可经过非 strict cast。
SQL adapter 不读取 NULL payload，并缓存 EOF，rescan 后重新准入。直接游标
rescan 到 strict NULL 时保留原 cursor/lease 但不调用其 rescan/next；再次
非 NULL rescan 可恢复，close 仍负责释放。不捕获旧 query context。

### 真实 Rust 表函数验证

Rust text 插件现通过 Public SDK 的 table 注册与泛型游标服务提供
`seekdb_rust_words(rust_utf8)`。输出 token 保持 custom 类型身份，ordinal
为 int64；内核回归用完整 SELECT 解析和表达式 codegen，检查外层
`seekdb_rust_char_count(token)` 绑定到 typed overload。生产 loader/registry
负责真实 Rust DSO 的装载、注册、绑定与 lease，不使用受控表函数 provider。

五组输入覆盖 Unicode token、不同空白、空串、全空白及 typed NULL；真实
`ObFunctionTableOp` 驱动取行、两次扫描、EOF、超时与重复 close，目录查询
次数在推导/codegen/执行期间保持不增加。该 spec 仍由测试从生成表达式
装配，不是 optimizer 自动生成计划的证据；catalog activation 仍是受控
guard。实库鉴权/事务/持久存储、NULL 非严格语义与隐式实参转换继续推进。

## 身份契约

列声明引用 SQL 类型的逻辑身份：SQL 名称、对象 ID、所属插件、物理格式 ID 和格式版本。一次装载的 generation 只属于运行时绑定，不应成为持久列的身份。

新列使用 `seekdb.plugin.type:v2` 标记。为保留七字段布局，第 4 个零基字段固定为字符串 `0`，不再保存当前 generation。旧 `seekdb.plugin.type:v1` 仍可读取，其 generation 必须符合旧格式的正整数约束，但解码后的逻辑 binding 不采用这个历史值。

两种格式解码后 `owner_generation=0`，只供 schema/catalog 依赖路径使用。执行函数、表函数和 registry lease 的 generation 校验没有放宽；零值不是执行通配符。比较列类型时，格式标记和历史 generation 不构成身份差异，其余逻辑字段仍需相同。

v2 元数据不能直接交给只识别 v1 的旧二进制。当前实现不承诺降级兼容，不会批量覆盖已有列元数据；需要降级的环境应在创建 v2 列前制定独立迁移方案。

## DDL 依赖修改

`ObServerPluginRuntime::mutate_type_dependency` 把逻辑身份交给 catalog 的事务绑定接口，而不是从持久列取出旧 generation 再拼装依赖边。

- 新增：在调用者已有 schema 写事务中锁定 provider 包行，按对象 ID、插件归属、物理格式及版本读取当前持久类型；要求 provider 为 ACTIVE、类型为 persistent，再用当前 generation 进入既有依赖校验/写入路径。
- 删除：按表/列 consumer、插件、格式和精确版本契约查找并锁定实际依赖边，使用边当前记录的 generation 删除。无需 provider 模块处于 ACTIVE，避免重启重绑或模块停止后无法删除列。
- 缺失、重复或不匹配的持久记录明确失败，不通过通配 generation 或忽略错误继续删除。

整个接口不另开事务、不独立提交、不调用 runtime registry，也不在外部事务中取得 catalog mutex。新增依赖与 DISABLE/UNINSTALL RESTRICT 使用相同 provider 行锁；RESTRICT 的依赖检查使用锁定读取。仅执行 BEGIN 并不等于所有 catalog 写入已串行化。

这些行锁已经接入代码，但实际数据库中的隔离级别、锁等待、死锁错误、并发 DDL 与 RESTRICT 仍需验证。不能把编译或元数据单元测试当作事务行为证明。

## 验证

Standalone CTest 的 `plugin_type_identity` 使用实际元数据 helper 和 `ObString`，覆盖 v1/v2 解码、跨 generation 身份比较、零 generation 规则、数值溢出、内嵌 NUL、长度和未知格式。它不连接数据库。

新增两阶段真实服务脚本：`rust/plugin-runtime/tests/type_identity_server.py`。需要一次性 loopback seekdb、已经安装的 SQL extension 插件、PyMySQL，以及读取系统 plugin catalog 的管理员权限。密码从 `SEEKDB_TEST_PASSWORD` 读取。

```sh
python3 rust/plugin-runtime/tests/type_identity_server.py prepare \
  --port 2881 --confirm-disposable-server
```

记录输出的唯一数据库名，然后在外部重启这台测试服务。脚本不会自行重启、修改配置或安装/卸载插件。

```sh
python3 rust/plugin-runtime/tests/type_identity_server.py verify \
  --database <prepare输出的数据库名> --port 2881 --confirm-disposable-server
```

验证阶段要求 catalog generation 确实增加，检查原数据和 SHOW CREATE TABLE，再执行 CREATE TABLE LIKE、DROP COLUMN、DROP TABLE；成功后删除该测试数据库。失败时保留现场，可用同参数的 `cleanup` 阶段清理；清理会先校验数据库名与内部 fixture 标记，只处理该测试数据库。

脚本可在旧 v1 构建上 prepare、新构建上 verify，用于验证旧列读取与恢复后的依赖删除，但这项跨构建验证尚未执行。当前仅完成脚本语法和命令行入口检查；此前本沙箱创建 loopback listener 返回 EPERM，不能将脚本存在或 standalone 通过记作数据库回归通过。

## 后续范围

仍需数据库/租户命名空间、数据库级 Extension 安装身份、统一对象创建和成员管理、SQL 安装/更新脚本与原子发布。当前类型对象 ID 仍是现有全局插件 catalog 的逻辑 ID；本次不声称已经实现 PG 式完整对象模型。
