# Server-dev 自定义执行服务：协议与资源基础

状态：通用执行服务、Rust SDK 与 loader 游标已接入。本轮新增真实的
自定义候选、逻辑节点、物理算子与 SQL codegen，最终完整 SQL kernel
验收已通过。不是“所有 PG Custom Scan 能力已完成”的声明；原
[MATERIAL 构造](plugin-candidate-selection.md) 仍为独立的内置路径。

后续进展：物理适配已支持独立多输入；v3 计划片段请求进一步分开目标
关系与实际输入，接入逐输入依赖和本地单并发的逻辑属性处理。最新范围
见[计划片段](plugin-plan-fragments.md)，后文“本轮”保留相应历史检查点。

## 本轮接入：从 Rust 候选到 PLUGIN CUSTOM

`candidate::Context::custom(CustomPath)` 经现有 build 回调提交 kind=2，
携带输入索引、服务版本、计划参数、算子代价及排序/阻塞属性。宿主
生成 `LogPluginCustom`，读取子计划属性并参与同一候选集合；只有最终
选中项才提交 child.parent 变化。服务缺失、负数/非有限代价、非法
flags/reserved 或超限参数保留错误，不退化为 MATERIAL。

v1 请求表示一个单输入的等价实现：保持输入的 SQL 列身份与关系结果，
但可由插件决定执行算法；只有声明 preserves_order 才继承排序属性。
这是首个纵向路径，不是将多输入或新 schema 永久排除在设计之外。

代码生成创建独立的 `PluginCustomSpec/PluginCustomOp`，保存实现绑定、
参数及实际输入列的逻辑类型 ID/NULL 属性。运行时通过现有 module
provider 重新获取同一实现，Rust 拥有 spool 的行缓存和算法。
宿主适配实际 child operator 取行；使用核心已有的 row/batch 转接，
不传递 ObDatum/C++ 布局。当前桥接本身仍是逐行的。

传输目前覆盖 SQL 有符号/无符号整数、float/double、字符串/字节（含文本 LOB）、NULL
与这些表示上的插件类型。持久类型通过计划绑定的 codec 转换（已通过
kernel 回归）；delta LOB、decimal/时间等尚未接通的
表示明确拒绝。比较/NULL 属性不能被当作
单纯字节载体绕过。
插件输出先复制到临时行，next 与出口取消检查全部成功后才写入表达式
Datum；错误不能交付半行。失败 next/发布/rescan 必须成功重扫后才能
继续。ExecContext 的 destroy 路径也释放游标，不能依赖未调用的析构。

计划不保存函数地址；绑定目前包含进程 runtime incarnation，不能把
序列化字段的存在当成跨进程/PX 兼容的证明。远端实现重绑定、计划缓存
失效、完整 schema/属性协议、批量性能与 query memory accounting 仍
需继续验证和完善。Rust 参考 spool 的内存上限不是宿主资源预算。

### 取行前可见的执行 schema

`custom_context_v2` 在原 v1 前缀后提供输入 schema 数组和独立的输出
schema，数量分别由 input_count 和 output_column_count 核对。每列
包含完整逻辑 type ID、ABI encoding、nullable/stored 属性以及当前
宿主构建的 SQL 类型、collation、precision/scale。字符串的 precision
槽位在内核中是 length-semantics union，不能当成数值精度传给插件；
当前对此使用 -1。这些字段不描述 C++ 内存布局，也不暴露 LOB locator。

schema 在任何 next_input 之前可读，包括输入完全没有行时。零列是
已知 schema，不代表“不支持描述”；旧 v1 上下文才表示 schema 不可用。
这是版本绑定的 Server-dev 上下文扩展，保留 v1 布局及使用路径，不是
承诺旧插件二进制跨宿主构建兼容。

元数据由宿主拥有，一次 next 回调内不可变。SQL 适配器从最终物理
表达式、逻辑 ID、NULL 属性和 codec 绑定构建列描述，随执行状态保存；
不在取行过程中从样本推断类型。Rust `Context::has_schema`、
`input_schema(index)`、`output_schema()` 返回借用的 Schema/Column。
字段查询不会取行、执行 SQL 或调用宿主回调；非法索引是普通查询错误。
需要保存到游标状态或异步任务的元数据必须复制，借用视图不能逃逸或
跨线程。对 v1 查询 schema 返回 UNSUPPORTED_ABI，不伪装成空 schema。

loader 和 SDK 均验证上下文后缀、列数、固定长度 ID 的终止、保留字段、
flags、encoding，以及 builtin/GIS 已知 ID 与编码的一致性；校验失败
不进入插件算法。带描述的行还需满足对应输入/输出的列数、逻辑 ID、
NULL 和编码宽度。宿主仍执行 SQL 物理范围、codec 和整行字节预算检查。
错误不可通过插件忽略回调返回值变成成功，恢复仍要求成功 rescan。

参考 Rust spool 在第一次取行前比较输入/输出 schema，确认适合自己的
单输入等价重放算法；未知 v1 上下文继续使用旧字节传输路径。通用 C ABI
可以描述多个输入和独立输出；默认 v1 策略仍保持原列身份，新增 v2
请求可声明独立表达式布局。物理适配现已支持独立的多个 child，见
[多输入执行](plugin-multi-input.md)；逻辑构造进展见计划片段，多输入 SQL
纵向验证、任意新关系与跨进程/PX 重绑定仍未完成。每次回调按当次 schema 验行，不新增跨回调 schema
版本协议；SQL 执行依靠现有固定计划和执行状态提供一致的描述。

规划阶段独立接入：当前候选选择发生在 ALLOC_EXPR/PROJECT_PRUNING
之前，不能把当时尚未分配的 output_exprs 当成最终 schema。后续需将
逻辑表达式身份、候选关系语义与最终列分配关联。显式列表的实现见
[规划布局](plugin-custom-layout.md)，执行期元数据不能替代规划期协议。

### 独立输入与输出布局、Rust 投影

`PluginCustomSpec` 的 `columns_/type_ids_/nullable_/codecs_` 表示输出；
`explicit_input_` 为 true 时，由对应的 `input_*` 数组独立描述子输入。
false 保留既有共享布局，但拒绝同时填入 input 数组，避免静默丢弃
请求。显式空输入列与共享布局不同；输入和输出各自校验列数、类型、
数值表示及 codec 绑定，执行状态分别拥有 schema 和转换状态。

输入数量决定取行缓冲和数值槽位，输出数量决定待发布行和表达式槽位。
decode 仅依据输入绑定，encode 仅依据输出绑定，因此投影删列、重复
列、重排列序，以及持久态与非持久态之间传递逻辑值，不再要求两端
使用同一列数组或同一 codec。内存限额和错误/取消/重扫语义继续有效。
新字段随 spec 序列化；这本身不是计划缓存失效或跨进程 PX 已验证的声明。

多输入扩展使用 input_offsets_ 将 input_* 扁平数组分成各 child 的
列范围，每段最多 1024 列，最多 64 个 child；offsets 为空保留原有
单 child 语义。输入行 scratch 按最宽输入复用，不能跨 next_input
保存借用，即使下一次读取的是其他 child。

Rust candidate v5 的 spool 接受私有 `SPJ1` 投影计划；格式与限制见
[示例说明](../../../plugins/rust_candidate/README.md)。它在首行前检查
全部输出映射，允许 NULL 约束放宽、允许存储标记不同，但不将投影视为
SQL 类型转换。丢弃的输入列仍由宿主/SDK 校验；零列输出保留行数。

执行布局保留原候选的关系等价语义。v1 请求/default Rust hook 仍提交
全列 spool；新增 custom_path_request_v2 已关联规划期表达式 ID、
输入依赖分配、输出生产者和投影裁剪，codegen 可从该请求生成独立
布局。独立物理 spec 与正常 SQL 规划的验证分别报告，见
[显式表达式布局](plugin-custom-layout.md)。这不是多输入关系替换已完成。

完整 kernel 已通过 16 组 LOB/codec 投影组合及五组数值/零列布局。
前者分别验证无 codec、仅输入 decode、仅输出 encode、双向 codec，
后者使用独立数值输入/输出槽，并在 Rust 取行前独立核对 ABI 字节。
覆盖 NULL、空值、大文本、数值极值、负零/无穷、重扫、零列行数，
以及空输入时越界映射或 NULL 约束收紧在取行前失败。临时 LOB fixture
提供必需的读取服务，同时断言未发生后端存储读取。这些是适配器与
真实 Rust 算法的验证，不是实库扫描或规划期新 schema 的证明。

### 数值的逻辑类型与物理表示

逻辑 builtin ID 决定 ABI 的字节宽度，不能因为 SQL Datum 使用 64 位整数
槽位，就把 bool/int32/uint32 全部传成 8 字节。普通 SQL 窄整数提升为
`core.type.int64` 或 `core.type.uint64`；float 提升为 `core.type.float64`。
带插件类型元数据的列保留其逻辑 ID。

| 逻辑 ID | 非 NULL ABI 表示 |
| --- | --- |
| `core.type.bool` | 1 字节，只接受 0 或 1 |
| `core.type.int32` / `core.type.uint32` | 4 字节有符号/无符号整数 |
| `core.type.int64` / `core.type.uint64` | 8 字节有符号/无符号整数 |
| `core.type.float64` | 8 字节 C double |

现有 `org.seekdb.gis.scalar.{bool,int32,uint32,int64,uint64,float64}` 是
明确支持的兼容别名，遵守相同宽度但保留原逻辑 ID；不是任意后缀匹配。
函数/表函数/cast 等共享类型推导将 uint64（包括 GIS 别名）映射为 SQL
无符号 BIGINT，避免只保存位模式却按有符号类型比较或拒绝自定义路径。

这是同进程、本机字节序接口，不是持久格式或跨架构网络编码。宿主使用
memcpy、Rust 使用 `from_ne_bytes`，不要求插件缓冲区对齐。NULL 仍必须
没有 payload，与值为零不同。用户自定义 ID 不因为以 `.bool` 等后缀
结尾而自动继承 builtin 表示；当前仍按其 SQL 物理表示及 codec 处理。

宿主接纳全部 8/16/24/32/64 位 SQL 整数和有/无符号 float/double。
输入不能在缩成逻辑 int32/uint32 时静默截断；输出同时校验 ABI 宽度和
目标 SQL 物理范围，例如 128 不能写入 TINYINT。有限 double 超过 float
范围及无符号浮点的负值明确失败；允许的 float 转换有正常精度舍入。
数值校验不写 Datum，不先发布某些列再发现后续数值越界。
失败保持 poison 状态，只有成功重扫才能恢复。

SDK `table::Cell::number()` 返回 `Option<table::Number>`，保留 bool、
i32/u32、i64/u64、f64 的区别；已识别数值类型的 NULL 为 None。
未知 ID 或错误宽度返回 INVALID。`Number::recognizes` 只认完整 builtin
ID 及上述 GIS 兼容别名；Rust spool 使用该接口实际读取数值后再缓存字节，自定义类型仍走
自己的逻辑表示。这个便利接口不扩大 catalog 对象支持集合。

新增验证分别覆盖真实 SQL frame → loader → Rust spool 的数值矩阵，
以及正常 SELECT 的 FLOAT 投影/排序。前者独立检查进入 Rust 前的字节，
不是仅靠原样往返判断 ABI 正确；输出故障由明确的测试适配器注入。
后者要求实际自定义 spec 包含 FLOAT 列及 float64 ID，避免最终投影停
在节点上方而没有验证传输。最终执行结果与限制见实施进度；这些不是
实库 IO、跨进程 PX、decimal/时间类型或原生批量自定义执行的证明。

### 文本 LOB 传输

宿主使用现有 `ObTextStringIter` 解出内容；Rust 看到的是逻辑字节，
不是内存/磁盘 locator 或持久编码。临时、行内和持久行外文本使用
同一内容协议。行外读取前检查声明的 payload 长度与整行剩余限额，
读取后核对实际长度；各列合计仍不得超过 16 MiB。读取使用当前执行
上下文的 LOB 服务，保留原始错误和取消语义。

行外内容放在一次输入行拥有的 arena 中，下次输入前 reset。插件需要
跨回调保存数据时仍须自行复制。输出经完整 emit/next 校验后，由
`ObTextStringHelper` 在表达式内存中重建临时 LOB，保留 NULL 与空内容
的区别，不把插件字节直接写成带头的 SQL 值。

Delta locator 的长度不能当作展开后大小；当前明确拒绝，待接入有界
重建。JSON/geometry 等其他物理类型仍需另行接入，更不代表所有
物理类型或实库存储事务均已验证。持久插件 codec 见下一节。

新增 kernel 验收已通过：真实 SQL frame、PluginCustomOp、loader 和
Rust spool 覆盖临时/行内/行外三种表示，测试 NULL、空值、内含 NUL、
Unicode、跨列移动的大值、重复执行与重扫；故障覆盖单列超限、整行
剩余额度不足、声明/实际长度不一致及存储超时，检查失败状态、恢复和
LOB 游标释放。行外数据由明确的存储 fixture 提供，不冒充实库 IO。
另外两组正常 SELECT（标量计划与 batch=3）生成重复长文本并排序，
断言自定义 spec 确有 LOB 列，完整内容往返及 rescan 正确；既有 SQL
与排序/PX 矩阵保留。这不是自定义算子原生批量接口或跨进程 PX 证明。

### 持久插件类型的 codec

自定义执行器的 SQL 列保留存储态身份，codegen 为每个需要转换的列
解析并核对 object ID、owner、持久格式与版本及已知 catalog epoch。
计划复用现有版本化存储类型绑定序列化，不保存 codec 函数指针；运行
时核对绑定数据完整性，解码和编码通过现有 loader 的精确绑定入口
执行，不按同名对象悄悄选择新版本。

输入先解开 LOB，再 decode 为相应逻辑类型的值。行外编码内容的临时
内存按列回收，已解码值随整行保留到下次输入回调，防止压缩型 codec
导致各列编码缓冲累计。单个编码输入最多 16 MiB；解码后的整行合计
最多 16 MiB，两者不是同一组字节。NULL 输入不调用 codec，解码产生
的 NULL 仍须符合列的 nullable 属性。

Rust 自定义算子处理并输出逻辑值。宿主在 Rust next 成功后完成所有
需要的 encode，再发布 SQL 表达式行；编码后的整行另有 16 MiB 限额。
任何 codec 失败、错类型、重复输出、非法 NULL 或超限结果都不能发布
部分 SQL 行。保留首个 sink 错误，不能通过吞掉 emit 失败后返回成功
来恢复；失败后仍须成功 rescan。无 codec 的查询不分配逐行编码元数据。

这不等于实现了实库持久读写、索引恢复或全类型 codec 兼容。计划缓存
失效与跨进程实现重绑定仍在完整目标中；实现变化导致绑定不再有效时
当前执行明确失败，而不是在一次 Rust spool 的前后半段换用另一 codec。

持久/普通混合列 × 三种 LOB 表示的六组 kernel 用例已通过，包含 NULL、
空内容、二进制/Unicode、大值、损坏绑定和持久格式、重复执行与恢复。
18 项故障 provider 用例单独验证宿主 sink 契约，不将故障替身称作真实
Rust codec；正向转换实际调用 Rust type DSO。SQL 计划测试使用正式
存储编码与排序绑定构造表达式，并断言自定义 spec 确有 codec 列，
不是只在节点上方计算最终投影。标量与 batch=3 的持久格式、逻辑排序、
rescan 和取消均通过；依然不是实库 INSERT/持久 IO 或跨进程 PX。

## 与内置 MATERIAL 的区别

MATERIAL 由宿主工厂创建，执行与状态也由宿主已有算子负责。自定义执行
则允许插件提供自己的算法、缓存和状态；宿主提供子算子取行、结果接收
和取消接口。不能仅把任意插件策略映射成几种内置算子，再称作通用扩展。

`server_dev_executor.h` 定义 `custom_executor_v1`，四个回调为
open / next / rescan / close。它是一项正常注册的 service，不另建
registry，也不强行给每个执行实现创建一个可从 SQL 直接调用的函数。
未来 path 的对象/实现依赖仍需由计划绑定与 catalog 统一记录。

## 执行数据契约

- open 接收最多 64 KiB 的计划参数字节，插件在返回前复制所需数据。
  不向插件传递 C++ class、ObDatum 或执行计划指针。
- next 接收调用期 context，可按索引向任意一个输入请求下一行、产生
  一行结果、检查取消。支持零至 64 个输入，最多 1024 列；不是仅能
  包装一次 next 的观察 hook。当前 SQL 算子只开放单输入等价路径，
  并依据 codegen 保存的列信息校验；注册服务不等于自动改变 SQL 语义。
- 每个值带逻辑类型 ID、NULL 标记与借用字节。单行数据最多 16 MiB；
  NULL 使用空指针与零长度，非 NULL 的空字节与 NULL 区分。
- 输入行只借用到下一次宿主回调或本次 next 结束。输出在 emit 返回前
  由宿主校验并复制，不保存插件内存指针。只有整个 next 成功，输出才
  可交给上层；出错时宿主必须丢弃此前产生的临时输出。
- next 返回成功必须恰好 emit 一次；EOF 不得伴随输出。无输出成功、
  重复 emit、非法索引、坏行结构和超限数据都是协议错误。EOF 不是
  随意放在 database_error 中的可吞掉错误码。

这是首版逐行传输协议，不承诺零拷贝或批量性能。批量、直接 Datum
adapter、更广类型与 LOB 行为还需完善现有物理适配，不能用字节载体
绕过数据库的逻辑类型校验。

## 绑定与生命周期

`CustomExecutorBinding` 保存 service、owner、runtime incarnation、
generation 和精确 service 版本；不包含函数地址或查询指针。bind
返回已复制的逻辑身份，不长时间固定代码。open 重新获得相同身份的
执行 lease，不因 service 名字相同而悄悄选择另一模块或新版本。

bind 与 open 都检查实现代码所属模块已通过 Server-dev 准入，Public
插件不能仅声明相同 service 布局取得深层执行权。ObIModuleProvider →
ObServer → ObServerPluginRuntime → loader 的桥接已提供，core-only
默认返回不支持，不创建另一套运行时。

同一个 instance 可以同时服务不同查询的游标，包括 open/close。bind
与 open 都要求 service 自己声明 THREAD_SAFE；只在 module manifest
上设置该位不能代替服务声明，手工拼接 binding 也不能绕过检查。
这不要求插件算法无锁：插件可以同步自己的共享状态；宿主不使用一把
全局锁串行化所有查询。未声明并发契约的 service 当前不能绑定执行，
不是默默由调用方承担竞态风险。

成功 open 后，游标固定模块与 service 回调表直到 close 返回。查询
取消和输入/输出失败使游标进入失败状态，不能继续 next；只有成功
rescan 才恢复。宿主先重置子算子，插件只重置自己拥有的状态。

close 无论返回什么都消费游标；失败路径返回的非空游标也被 close。
宿主在 close 回调结束后释放 lease。逻辑 quiesce 阻止新 open，但不能
停止仍被执行游标引用的模块；原游标可以结束工作，再由宿主完成停机。
这不是允许不可信 native 代码安全热卸载的承诺。

## 错误与 Rust 所有权

宿主将每次输入、输出和取消回调包装为保留首个错误的调用帧。错误携带
精确数据库状态；插件即使忽略返回值并声称 OK/EOF，也不能消除已发生
错误。非法 database_error=EOF 被视作协议错误，不假装正常读尽。
C++ 异常在回到 Rust 前转换，Rust panic 在 FFI 入口内部处理。

SDK `custom_executor::Executor` 的 State 必须为 Send + 'static。
宿主对单个游标串行调用，但允许在调用之间换线程。Context/Row 不是
Send，借用输入期间不能再次调用宿主；想跨回调保存数据必须拥有副本。
State: Send 不代表 plugin 全局状态自动线程安全，也不允许同时调用
同一个游标。跨游标共享的缓存、计数器和 instance 状态由插件同步。
正常算法返回 `Step::Row/End`，不能靠 Err(END_OF_STREAM) 绕过完成检查。

适配器在进入与离开 next 时检查取消；长时间循环还需要插件主动轮询。
这不是强制抢占，也不是异步 task、线程池或 AI 资源预算接口。Rust
panic=abort、内存损坏和不遵守回调契约的 native 代码不受此层隔离。

## 参考实现与验证边界

`plugins/rust_candidate` 的 v3 示例将 `org.seekdb.rust-candidate.spool`
接入自定义 SQL 路径。缓冲行与重放逻辑在 Rust 中实现，不调用宿主 MATERIAL 执行器。
它保留 NULL 与空字节，复制会被下次取行覆写的输入，设置行数与保留
数据的演示上限；没有实现 spill 或完整 query memory accounting。

loader 测试通过实际打包的 Rust DSO 调用该 service，证明第一行输出前
Rust 已拉完输入、重扫可以重建状态，游标固定模块直到 close。另有 19
个原始 C ABI 变体，独立于 SDK 检查无输出/重复输出/带输出 EOF、吞掉
子计划或 sink 错误、取消、分配异常、错误 open/rescan/close、Public
准入拒绝、接口版本错误、缺少 service 线程安全声明和清理平衡。

同一真实 Rust DSO 还接受八个并发游标测试：通过有超时的条件变量
会合确认八个 next 回调同时在途，而不是靠 sleep 猜测并发。此时
quiesce 必须等待所有执行 lease，阻止新的绑定而不调用 stop/deinit。
已有游标继续输出，在原工作线程、控制线程和第二批工作线程之间
独占移交；分别触发取消、输入及输出失败后用 rescan 恢复，其他游标
不受影响。保留最后一个游标仍阻止停机，其析构 close 才释放最后引用。
这是 loader/SDK 的多线程执行证据，不是完整并行 SQL/PX 调度证据。

loader 测试的行传输与 catalog/verifier 是明确的替身，不是 SQL 物理
算子的执行证据。本轮 kernel 样例另外要求实际 SQL 计划包含
PLUGIN CUSTOM、不能以 MATERIAL 替代，并检查 Rust 执行调用与 lease
清理；最终覆盖 ASC/DESC × 标量/批量计划四个样例、rescan 和取消，
以及非法代价/flags/服务/保留字段/参数长度的真实构造错误。catalog、
schema/session 和产物验证仍有明确替身，不是实库事务或完整 PX 证据。

宿主临时输出使用新行构造、全部复制成功后 swap 的方式。不保留每列
历史最大 vector 容量，防止宽数据在列之间移动时累计多列高水位；失败
复制也丢弃整行。这只约束桥接自有的临时行，不替代查询内存预算或
Rust 算法状态的资源管理。该收尾调整后的生产构建和完整 kernel
回归均通过。完整 30 项 CTest、118 项 Rust runtime 单测及源码边界
通过；两套构造路径的证据保持区分，不将测试替身当成实库验证。

## 后续：扩大真实自定义计划协议

执行期只读 schema、数值和文本 LOB/持久 codec 传输已经接通。在当前
单输入等价路径上，已有[只读规划图与表达式身份](plugin-candidate-graph.md)。
下一步需要将这些身份接入输入关系、依赖分配、多输入与显式输出映射，
并完善属性/代价重估。
不能将未分配的输出列当成已知规划 schema，也不能将运行期零列与
不支持 schema 混为一谈。更广的逻辑类型与 delta LOB 仍需继续接通。
EXPLAIN、计划缓存失效、序列化、PX/SQC、并发及恢复能力按实际支持情况
明确声明，不能在未接通的阶段把请求退化成 MATERIAL 并报告成功。

完整目标保持不变：PG 式灵活对象/执行协议、Rust runtime/SDK、索引/
类型/事务/AI 资源与轻量化。GIS 继续用 C++，不受框架语言选择限制。
