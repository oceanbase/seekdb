# 插件类型 ORDER BY：固定比较绑定与实际排序执行

状态：普通 SELECT 排序链路及 PX 归并比较入口已接入；不是完整 PX
调度/传输验证，也不是所有排序或存储路径均已完成。
验证日期：2026-09-09。总体目标仍见 [下一阶段设计](plugin-next-design.md)。

## 绑定与执行

显式 ORDER BY 在解析排序项后先推导类型，再调用
`PluginTypeValueExpr::prepare_ordering`。这一步必须在类型推导之后：CASE
等复合表达式的插件逻辑身份并非解析列名时就已建立。首次完整 SELECT
测试暴露了该时序问题，修复后 CASE 升降序均进入固定比较器。

自定义插件类型复用 MIN/MAX 的 `ORDERED` carrier，固定逻辑类型身份、
catalog epoch 和 comparator binding。存储值按原协议先解码；运行期值
直接包装。普通原生类型和原生兼容逻辑类型保留原生比较语义。

排序执行器通过排序字段位置找到对应表达式，非 NULL 的 ORDERED 值调用
Rust TYPE comparator。NULL 位置和升降序仍由 SQL sort collation 处理，
NULL 不送入插件比较器。每次插件比较前后检查查询状态，错误向上传播，
loader 继续负责固定绑定校验和调用期 lease。此改动不新增整段查询期 lease。

普通排序、Top-N、with-ties 比较、prefix 比较以及存储行归并的内部比较
入口共用该分派逻辑；这只是代码接线范围，不能代替每条路径的运行证据。
不改变通用 `ObSortCmpFunc` ABI，不在通用函数指针中编码插件地址。

## 优化路径与暂未开放的消费者

- 排序键携带插件身份时关闭原生编码排序键优化，显式 enable_newsort
  hint 也不能绕过检查。当前策略对原生兼容插件身份同样保守。
- 禁止为这类排序键生成仍按原生值比较的下推 Top-N runtime filter；
  Top-N 算子本身仍可使用插件比较器。
- 排序执行器拒绝 ORDERED 键配合原生编码、原生下推过滤，以及尚未接入
  插件 hash 语义的 partition key，不能静默回退到载体字节。
- `fill_sort_funcs` 的消费者须显式声明支持插件排序；当前普通 Sort、
  PX merge receive、merge coordinator、普通 RANGE 和 slave-map 分区 RANGE
  spec 已开放。递归排序等其余原生-only 消费者遇到 ORDERED 键仍报不支持。
  此限制是待继续接入的工作，不是最终设计目标。

## PX 归并比较接入

`compare_sort_datums` 移入公共排序实现，本地排序与 PX 使用同一套逻辑。
协调端、接收端（普通、local-order 初始化和 rescan）的归并堆传入
`all_exprs_` 与本算子的 `ObEvalCtx`；接收端识别局部有序段的比较也使用
同一分派。上下文和表达式由算子/spec 持有，不新增序列化字段或插件代码指针。

两类堆比较器检查行号、列号和比较函数数量，重新初始化清除旧错误。
`std::push_heap` / `std::pop_heap` 使用 `std::ref` 传递比较器：原来的
按值传递会将错误记录在临时副本里，调用者检查原对象时看不到失败。现在
比较错误和取消可以返回到接收/协调逻辑，失败的 pop 不发布输出行。

新增 `rust_px_merge_fixture.h` 使用真实 SQL 生成的排序元数据、真实 Rust
DSO、两类实际 PX 堆；输入通道和预计算行由 fixture 提供。scalar/3 行
batch 环境、升降序、插件/原生对照、3/9 通道、两类堆共 32 组归并，
覆盖 NULL、重复键、通道耗尽和初始空通道。原生对照故意不传逻辑表达式映射，
验证原有字节比较入口；插件用例验证固定 TYPE 绑定且不重新解析名称。

另有 8 组故障测试（两类堆 × 两方向 × 两种 frame）：比较函数数量不匹配、
损坏的 binding 元数据、真实 Rust 非法 UTF-8 错误、错误后的再次 pop、
首次比较后取消，以及 reset/reinit 后正常使用。错误后不继续调用插件，
关闭后 lease 归零。测试输入构造的数值缓冲区和原生顺序 oracle 已修正，
没有跳过失败检查；最终完整 kernel 回归通过。

这些证据验证归并部件，不等同于实际生成并执行完整 PX 计划。专用 spec
接线、local-order 分段、DFO/SQC 调度、DTL 传输和跨进程绑定恢复，还需要
端到端用例；不以堆测试替代这些工作。

## 普通 RANGE：采样、边界与分发一致

普通 RANGE 的采样排序现在传入 `SortDef::exprs_`，使采样行使用与接收/
发送侧一致的固定 TYPE comparator。采样排序初始化显式指定关闭编码、
非 local-order、允许 rewind；修正旧调用中布尔参数位置与当前签名不一致。

`ObRangeSliceIdCalc` 的标量、批量边界查找均调用共享比较分派。lower_bound
按引用使用比较器，并检查比较器错误；无法比较时不产生有效的分发索引。
标量输出先设无效索引；批量先计算范围位置，全部成功并检查取消后才返回
索引指针、计算任务号及写入 DDL slice ID。跳过行保留无效索引，不比较其值。
拒绝零任务数、负数或超过 frame 容量的 batch；空 batch 不发布指针。
非空范围的批量分发先对各分发键调用 `eval_batch`，随后逐行读取缓存值
查找边界，避免首先调用 `eval` 将嵌套插件函数降为逐行 SQL 入口调用。

纯内存 datum 不再要求 LOB 读取上下文。`ObExecContext` 与 datum 公共协议
本已允许此上下文为空；外部 LOB 由实际读取接口检查。纵向测试暴露了旧
range 比较器的多余非空要求，修正后无需伪造 LOB 服务即可执行。

slave-map 分区 RANGE 的插件比较与批量路由现已接入，见下一节；普通
RANGE 的部件证据仍不能推广为持久 DDL、调度或恢复语义已经完成。

`rust_range_fixture.h` 使用实际 `ObDynamicSamplePieceMsgCtx` 的
init/split_range、排序器、`ObRangeSliceIdCalc` 和 Rust DSO。输入 sample
行由 fixture 提供，比较元数据来自正常 SQL 生成的排序 spec；协调器仅
提供执行上下文，没有运行调度或 DTL。

测试保留原有双键（token、ordinal）升降序，并加入单键 + 独立整数
DDL 输出 frame；覆盖直接 token、嵌套 identity 和原生 BINARY 对照的
scalar/3 行 frame，基础矩阵共 16 组采样与 448 次正常标量分发。分区
RANGE 的附加 SQL 用例也复用这套普通 RANGE 矩阵。两个具体分界点
由双键用例验证；单键独立预期处理重复值跨采样边界时必须同范围的语义。
原生对照来自正常 `CAST(token AS BINARY)` SQL 生成的 spec，并传递真实
表达式映射；采样/路由全程不增加 Rust 比较次数。

DDL 输出验证其编码保存 range count / 原始 range index，而不是取模后的
task index；覆盖成功、空范围、比较失败、比较后取消、跳过行，以及
1/2/3/5 个 task。失败或取消不得写入哨兵输出，跳过行保持原值。原有非法
UTF-8、重试、零任务、空 batch、越界检查仍保留。此处实测的是输出
expression frame，不代表持久 DDL 作业或恢复已得到验证。

嵌套 identity 的批量用例清除生成表达式链的缓存，直接填充真实输入列，
运行 14 个三行窗口；断言 SQL host 标量入口计数不变、identity 批量入口
计数增加，且路由和 DDL 结果正确。函数第 0 参数是隐藏绑定，测试沿第 1
参数到实际值；不能以填入预计算根值代替批量求值证据。该服务仍是 v2
动态服务，loader 内部可逐行 fallback；本测试不证明 v3 原生批处理或零拷贝。
所有用例运行期不重复解析绑定，结束后 module lease 归零。

生产构建与修正后的完整 kernel 回归通过。新增测试曾暴露定长数组未初始化、
单键重复值预期沿用双键次序、表达式链误入隐藏绑定三项 fixture 问题，均已
修正后重跑完整 kernel。尚需补齐完整 RANGE 计划生成、worker sample
消息收集/传输、跨进程绑定、持久 DDL 与恢复、采样落盘等端到端证据。

## 带分区映射的 RANGE

`PARTITION_RANGE` spec 显式允许 ORDERED 键；采样沿用共享 TYPE 比较，
`ObSlaveMapPkeyRangeIdxCalc` 的边界查找也调用相同分派，使用当前执行
context 与固定绑定。比较器按引用传递，每次查找重新初始化错误状态，
并校验比较元数据数量及 key 长度。缺少 SQC context 时返回未初始化错误。

标量路由先将输出置为无效，完成范围比较、通道映射和取消检查后才写入
DDL slice ID。原先在通道映射之前写 DDL 的顺序已修正；负通道编号显式
拒绝。DDL 保存原始范围编号，通道分配保留按连续范围均衡分组的规则，
与普通 RANGE 的取模分配不同。

普通/分区 RANGE 都只为活动行定位可写 DDL datum，不再使用会重设全部
datum 指针的批量辅助函数。跳过行指向借用缓冲区时，也必须保留其指针
和值；普通范围、空范围及分区范围都有外部哨兵缓冲区断言。

新增 `SM_REPART_RANGE` 批量分派与能力声明。先批量求 tablet ID 和分发键，
暂存各行通道及 DDL 值，整批成功并检查状态后才发布指针和 DDL 输出。
缺失 tablet、坏通道、比较失败或取消均不发布整批输出；跳过行保持无效
通道与原 DDL 值，支持空 batch 并拒绝负数/超 frame 大小。

`rust_partition_range_fixture.h` 保留正常 SQL 的 `ordinal + 1000` 对照，
通过生产 builder 增加 PDML tablet ID 与 DDL slice ID 两个内部伪列，
再经正常优化/表达式生成器分配独立 frame；生成的 ordinal frame 输入
受控 tablet ID。沿用真实 sample split
结果，初始化实际 SQC handler，调用 `set_partition_ranges` 深拷贝范围，
再运行真实路由器的 init、模板标量/批量入口及 destroy/init。

8 个附加 SQL 用例覆盖直接 token 升降序、嵌套 identity、原生 BINARY，
分别使用 scalar/3 行 frame；正常路由覆盖 168 次标量与 84 行批量。
映射包含 3 个范围对 2/4 个通道、空范围对 1 个通道，验证多 tablet 的
非连续通道编号。故障覆盖损坏绑定（无 Rust 回调）、非法 UTF-8、首次
比较后取消、非法/缺失 tablet、负通道、未初始化/重复初始化、元数据
数量不匹配、重试、全跳过与 batch 容量；输出失败原子性与 lease 释放均
有断言。identity 批量窗口从输入列重算，并检查 host 标量入口不增加、
批量入口增加；不把 v2 loader 内部 fallback 说成原生 v3 批计算。

生产构建与完整 kernel 通过。**证据边界：** 尚未实际生成完整分区 RANGE
交换计划，未运行 worker 调度、sample 消息收集/传输、真实分区表达式
计算/单侧分区重映射、真实 DTL 传输、持久 DDL 或恢复。SQC 的容器和
深拷贝是实测行为，不是跨进程证明。携带 tablet ID 的批量发送见下节。

## 批量发送携带每行 tablet ID

计算器新增批量 tablet ID 能力查询及 `get_previous_batch_tablet_ids`。
只有成功完成批量路由、产出索引并具备 ID 能力时，才记录可读取的批量
大小；失败、空 batch、下一次标量/批量计算、直接 tablet 求值以及
destroy/init 会使此前记录失效。读取时检查大小与能力，失败返回空指针。
这是查询局部的借用数组，不是可跨调用持有的不可变快照。跳过行 ID
显式置无效，`get_tablet_ids` 更新上一行状态时直接读取自己拥有的数组，
不再依赖调用者传入的输出指针已非空。

`ObPxTransmitOp::send_rows_in_batch` 不再仅因存在 `tablet_id_expr_` 就
退回标量：同时支持批量路由和批量 ID 的计算器提供每行 ID，发送端一次
取得数组，按当前行写入 PDML 伪列。其他计算器继续走已有标量路径。
现有 affinity、partition-random 与新的 partition-RANGE 均有覆盖。

`rust_tablet_transmit_fixture.h` 运行实际发送循环、路由分派、PDML 输出
写入及逐 batch EOF 分支；以预取三行作为输入，以 capture channel 替换
传输。4 个 batch SQL 环境下共 20 次发送调用，核对 44 条捕获数据行的
通道、tablet ID 和 DDL 值：正常 RANGE、末行缺失 tablet（零数据行）、
跳过坏行、affinity 和单通道/partition 的 random 对照。成功后仍能读取
批量 ID 的断言可检测意外标量回退。嵌套 identity 从输入列重算，断言
host 标量入口不增加、批量入口增加。旧批量结果在大小不符、失败、空
batch、标量调用和销毁后不可读取。

测试初次编译修正了借用 physical plan 的 const 赋值；首次执行发现普通
计算表达式作为 DDL 输出会在发送时重新求值，已改用生产 DDL 伪列，保留
原 SQL 计算列对照。生产构建和完整 kernel 通过。此证据不包含网络、
接收端解码、异步任务 EOF/背压、真实 PDML/storage 作业，也尚未证明
完整分区交换计划生成。分区表达式的进一步验证见下节。Rust C ABI 与插件格式不变。

## 真实分区表达式与单侧一级分区映射

`rust_partition_expression_fixture.h` 使用内存中拥有真实 partition/subpartition
对象的二级 RANGE schema。SQL fixture 通过生产 `build_calc_tablet_id_expr`
和正常 optimizer/codegen 生成 `CALC_IGNORE_SUB_PART` 表达式，不手填求值
函数、extra info 或返回 tablet ID。查询输入的整数分区键经过真实
`calc_partition_level_two`、DAS tablet mapper、schema 范围查找，得到
一级 partition ID，再通过 `ONE_SIDE_ONE_LEVEL_FIRST` 转为实际 tablet。
样本切分、SQC 范围存储、Rust 比较器与发送循环沿用已有真实调用路径。

测试使用独立的 SQC 范围集，不混入旧负例的 tablet 44。分区 ID 为
101/102/103，tablet ID 为 11/22/33，故混淆两种 ID 会直接使断言失败。
输入包括 0/9、10/19、20/29 和无匹配的 30，检查 RANGE 上界不包含。
升序、降序、嵌套 identity 与原生 BINARY 环境均运行；同一计算器
destroy/init 后重复检查，destroy 连续调用也须成功。批量发送验证
三行不同 tablet、末行无匹配时零发送且不发布 DDL/批量 ID、跳过该行
后前两行正常发送。新增这组范围键使用已计算 datum；已有 fixture
继续独立证明 identity 的 host 批量重算。

修复 `ObRepartSliceIdxCalc::destroy()` 未销毁 `part2tablet_id_map_` 的
问题：即使 channel map 清理失败也尝试清理此表，保留首个错误。
否则单侧一级分区路由第二次 init 会遇到已创建的映射表。
首次完整 kernel 在初始化处发现测试范围集混入了旧负例的 tablet 44；
改为独立、匹配的 SQC 后完整重跑通过，未放宽断言。新增 8 个 SQL
环境共 672 次正常标量路由，4 个 batch 环境共 24 次发送调用、40 条
数据行。生产构建、23 项 CTest、源码边界、12 项 build gate 通过。

这些测试不是 Rust 类型本身充当持久分区键的证据：分区键是原生整数，
Rust 类型用于 tablet 内分发范围的逻辑顺序。另一侧和完整两级计算的
后续验证见下节；这组测试尚未覆盖完整交换计划、真实 worker
采样与 DTL 网络、接收解码、背压、存储写入及恢复。schema 由 fixture
持有，不等于实库 CREATE TABLE/PDML 作业。C ABI、Rust build ID 与
持久格式均不变，GIS 仍为 C++。

## 子分区上下文与完整两级 RANGE 计算

新增一个 3×3 二级 RANGE schema，分别生成 `CALC_IGNORE_FIRST_PART`
与 `CALC_NORMAL`，两个输入是独立整数 frame。仅重算子分区时使用
实际表达式 context 保存一级 partition ID；完整两级模式由输入逐行
计算一级、再计算子分区。二者均走实际 DAS/schema 查找和 Rust 范围
比较，再调用真实批量发送循环。分区键仍是原生整数。

本轮修复两个通用内核边界：

- `get_part_id_by_one_level_sub_ch_map` 校验所有目标 tablet 都属于同一
  一级分区。混合父分区返回 `OB_INVALID_ARGUMENT`，不根据哈希表首项
  任意选择，也不覆盖旧表达式 context；测试覆盖两种插入顺序。
- `ObSysFunRawExpr::inner_same_as` 对 partition ID、tablet ID、两者组合
  三种表达式都比较计算模式。此前只检查第一种，实际内核测试已复现
  不同 tablet 计算模式被判等。新增三类表达式、三种模式两两组合的
  正反向断言，相同模式仍可判等，不同模式不可合并。

同一个 SUB 表达式与计算器在 destroy/init 后依次绑定 101→103→102。
输入的一级键故意设为越界值 999，证明使用绑定 context。完整模式覆盖
全部九个 tablet 与两个层级的上下界；批量输入同时改变一级和子分区。
一级或子分区无匹配时，整批零发送且不发布 DDL/批量 ID；跳过该行后
其余两行正常。原生 BINARY 不调用 Rust 比较器，插件路径实际调用比较
且不重复解析绑定，最终 module lease 归零。新范围键使用预计算 datum，
旧 fixture 保留独立的 Rust identity 批量重算验证。

完整 kernel 重跑通过：8 个 SQL 环境新增 4,032 次正常标量路由，4 个
batch 环境新增 96 次发送调用、144 条数据行；原有 FIRST 路径及负例
矩阵保留。生产构建、23 项 CTest、源码边界与 12 项 build gate 通过。
上述证据不涵盖完整交换计划、真实网络/worker
采样/接收解码/背压、存储写入和恢复，也不证明 Rust 类型可充当持久
分区键。Rust C ABI、build ID 和持久格式未改。

## DTL datum 块搬迁与接收

`rust_dtl_wire_fixture.h` 在两级分区发送矩阵中增加第三个输出：实际
Rust 逻辑类型值（或原生 BINARY 对照）。Capture channel 调用生产
`ObDtlDatumMsgWriter::write` 编码，并使用真正的 DFC 内存管理器分配
和归还缓冲区。编码块复制到两个与发送缓冲区地址不同的分配中，然后
覆写并释放发送缓冲区；接收端使用独立 exec/eval frame，分别运行
`ObReceiveRowReader` 的标量与批量读取。比较文本字节、Unicode、NULL、
tablet ID、DDL 值和 evaluated/projected 标志，不借用发送端 datum。

4 个 batch SQL 环境共 120 次发送调用、216 条数据行，分别作两次读取。
其中增加 24 次三行同通道发送，批量读取必须按 2+1 返回；其他用例保留
不同 tablet 和整批失败/跳过坏行。缓冲区被 reader 接管、读尽、重复
reset，测试结束检查每个 channel memory manager 的分配/归还计数相等。
这不是内存池 RSS 降为零的承诺。

修正 `ObReceiveRowReader::get_next_batch` 仅在中间结果分支检查容量的
问题：统一在入口拒绝非正数、超过 eval frame 容量的请求及空输出数组，
并先将 read_rows 置零。实际 datum-buffer 分支验证拒绝请求不会消费行
或写入行指针数组，后续合法读取仍成功。

首次测试重复调用 unswizzling 导致段错误：datum writer 的 write 已经
生成偏移，生产 `switch_buffer` 明确跳过其 serialize。已按生产顺序
修正测试，原搬迁/覆写断言保留，完整 kernel 与补充的 2+1 矩阵重跑
通过。生产构建、23 项 CTest、源码边界和 build gate 通过。
可选 `--debug-on-crash` 只在失败后尝试 gdb，保留
失败结果；当前环境禁止 ptrace，未获得栈，定位依据是实际源码与修正
后的运行结果。

此处验证的是 DTL datum 块，不是完整 RPC 封包或跨进程传输。收发端
共享只读计划描述，不证明计划序列化、worker 调度、网络/背压、异步
任务 EOF 或完整 receive operator 生命周期。LOB/outrow 与存储型
Rust 值、多个 DTL 块串接、恢复仍需单独覆盖。C ABI、Rust build ID 与
持久格式未变。

## 多块接收与批量行结构校验

每条带类型值的发送同时走原有单块 writer 和逐行封块 writer。两者均
使用生产 datum 编码与真正的 DFC 分配器，复制到不同地址、覆写并释放
发送块。接收端把多个块交给真实 reader，标量读尽和批量跨块读取都须
得到同样的值；保留单块 2+1 对照，不用拼接后的模拟行数组替代 reader。
另覆盖读取第一行后提前 reset，验证未读队列与已迭代块一起释放。
同通道 burst 从独立的范围 oracle 中挑选三个不同值，落在共用通道的
范围 0/1；包含不同字节长度、适用排序中的 NULL 和不同 DDL 范围编号，
逐行比较原发送顺序，避免重复相同值掩盖偏移或对应关系错误。

批量 attach 现在先检查 read_rows 容量、每个行指针与列数、目标表达式
指针和动态常量约束，再写入任何 datum。标量路径已有列数检查，旧
批量路径则可能静默忽略多余列，或读取不足列数的数组。负例分别使用
最后一行少一列，以及完整三列块配两列表达式；检查报错时三列表达式
值/求值标志不变，且 get_next_batch 返回零个有效行。空行指针与直接
attach 的非法行数也有断言。

结构错误与非法批量请求的消费语义不同：容量请求在读取前拒绝、不
消费输入；结构错误是在取出行后发现，不承诺回放或恢复读取位置，
调用者应终止并清理该接收操作。此处也不承诺动态常量复制的分配失败
能回滚全部 frame，更不是对任意畸形网络块的完整验证。

多块、提前 reset、结构错误矩阵和不同值 burst 的完整 kernel 均通过；
生产构建、23 项 CTest、源码边界和 12 项 build gate 已通过。
尚未覆盖 RPC/worker/背压、完整 receive operator、
计划传输、outrow 与存储型 Rust 值。插件化整体目标保持不变。

## FIFO 接收算子的受控传输交接

DTL fixture 进一步将编码后的独立缓冲区交给真实 `ObPxFifoReceiveOp`，
调用公开的 `init/open/get_next_batch/get_next_row/rescan/close`，析构时
执行派生类资源清理。spec 使用真实生成的 tablet、DDL 和类型值表达式，
接收 frame 与发送端分离；不是用模拟算子重写读取逻辑。

覆盖单块与逐行多块两种流：批量 2+1、公开逐行 API 的向量转行适配器、
重复读取 EOF、读尽后重扫、仅读一行后重扫再从新缓冲区读尽，以及提前
close。每一行仍比较 tablet/DDL、NULL 和字节内容；重扫必须丢弃 reader
及行适配器的未读状态。所有缓冲区转交后清空源指针，并延续 DFC
分配/归还计数检查。公共 batch wrapper 会清除非 table-scan 算子的
all-active 优化提示；有效行依据实际 skip 位图断言，不要求该提示为真。

唯一覆写的执行入口是 `try_link_channel`：fixture 不建立活动通道，
使用 `all_eof(0)` 让算子读取已接收的数据后结束。因此这覆盖真实算子的
本地生命周期，但不是 SQC 通道建立、网络消息循环、异步 EOF、背压或
真实通道关闭验证。仍共享只读计划元数据，不证明交换计划生成/序列化；
outrow、存储型 Rust 值和恢复仍待推进。新增算子与公共 wrapper 源文件
已纳入 kernel runner 的生产构建新鲜度检查。

完整 kernel 已通过。初次编译修正 fixture 对私有 batch setter 的调用；
首次运行修正对 all-active 提示的错误假设，保留全部逐行 skip/值断言。
新增中途重扫后再完整运行通过；23 项 CTest、12 项 build gate、源码边界
和 diff 检查通过。本轮无生产逻辑变更，复用通过新鲜度检查的生产构建。

## 实际通道消息循环与 EOF

新增通道级 fixture，保留前述预交接生命周期对照。两条真实
`ObDtlLocalChannel` 注册到接收 DFC 和 FIFO 算子的消息循环，缓冲区经
`feedup/attach` 入队并通知 watcher，再由生产 `process_any/process1`
调用 `ObPxReceiveRowP`，不再直接调用 reader.add_buffer。

先处理一个无数据通道的空 EOF，断言 EOF 计数为 1、all_eof 为 false，
另一个通道尚无消息时返回 WAIT_EAGAIN。随后输入 Rust 类型数据，分别
覆盖最后一个数据块携带 EOF，以及由 writer 生成的独立空 EOF。调用
公共 batch/row API 后检查全部行、重复结束、两个通道各自的 EOF、
已接收/已处理计数，以及通道和聚合 DFC 的队列字节/缓冲区归零。

另有提前关闭和不支持的 payload 类型标签错误。错误必须传播到公共
batch API，不发布有效行，也不把未消费的 EOF 当成已完成。清理路径
分别覆盖 reader 已接管的块、通道尚未处理的队列、失败时留在
process_buffer 中的块，并检查分配/归还相等。

SQC 发现和全局 DTL channel map 仍由 fixture 外置管理；正常消息已经
在读取前入队，不证明真实异步调度、RPC、跨进程发送或背压唤醒。
流控验证当前覆盖低于阻塞阈值时的实际增减账，不把计数归零当作阻塞/
解除阻塞协议已验证。该通道样例不重扫，重扫仍由前述预交接样例覆盖；
不证明带活动通道的完整重扫。接收算子 spec 使用真实生成的表达式，
但仍是手工组装的 exchange spec，不是交换计划生成/传输证明。

完整 kernel 已通过。首次链接发现 fixture 调用了仅声明而未实现的
unset_msg_watcher，改用显式通知链脱离并在 watcher 存活时销毁通道；
强制移除是必要的，因为丢弃队列不会递增 processed_buffer_cnt，不能
用 has_msg 为 false 作为脱链前提。修正后实际通道矩阵完整运行通过；
23 项 CTest、12 项 build gate、源码边界与 diff 检查通过。本轮未改变
生产逻辑，相关通道/流控/消息处理源码加入生产构建新鲜度检查。

## 全局通道注册与实际 send/flush

保留缓冲区搬迁和双通道交接对照，新增 `LinkedExchange`。它在真实
transmit 发送回调中取得原始 `ObPxNewRow` 与 sender eval，使用生产
channel-group 生成配对 ID，在全局 DTL 中分别创建发送/接收通道及
DFC。原始输入直接调用 channel.send/flush，经实际 writer、队列、
send_message、全局 get_channel/release_channel、local feedup 和
消息循环到达 FIFO 算子；该样例不直接注入接收缓冲区，也不根据预期
值数组重新构造发送数据。

接收端使用独立 exec/frame。单块流在真实 EOF 发送后以 2+1 读取；
逐行封块流同步交替发送/读取，每次最多保留一块未处理数据，结束时
再处理独立空 EOF。逐项比较原发送顺序中的 tablet、DDL、NULL、变长
Unicode 字节。检查发送块数、接收/处理块数、重复 EOF、DFC 计数和
channel pin：读尽时仅剩 registry pin，移除后为零，随后注销 DFC、
删除通道，并验证原 ID 返回 HASH_NOT_EXIST。测试域结束时全局通道
表为空、聚合 channel count/队列计数归零、内存分配/归还相等。

这里验证的是同进程 local channel 的真实发送链，不是 RPC 或跨进程
传输。接收 spec 与通道发现仍由 fixture 组装，没有生产 SQC 建链握手；
EOF 由测试顺序调用发送，不覆盖生产 async EOF worker。逐行发送与读取
是有界的同步交替，不是并发背压测试；带活动通道的重扫、完整交换计划
生成/序列化和 outrow/存储型值仍需继续推进。

首次编译修正 fixture 对 get_peer_id 派生类接口的调用后，完整 kernel
通过；23 项 CTest、12 项 build gate、源码边界与 diff 检查通过。本轮
无生产逻辑变更，新增全局 DTL/channel-group 的生产构建新鲜度检查。

## 生产 EOF 批量发送器

LinkedExchange 的 EOF 发送改为实际 `ObTransmitEofAsynSender::asyn_send`，
不再在该 fixture 中单独实现 send/flush 顺序。Rust 类型数据、发送块数、
接收 EOF、registry pin 与资源归还的原有断言保留。

额外的三通道矩阵使用真实全局通道注册与 local send：全部对端存在，
以及第一/中间/最后一个对端缺失。派生测试类只记录 action 调用顺序，
实际 EOF action 仍调用生产实现。检查三个发送均被尝试，缺失对端的
错误由 asyn_send 返回，其余接收通道各收到一个空 EOF；失败发送的
缓冲区被释放，不残留额外 channel pin，移除后所有 ID 不可再查。
故障矩阵的 payload 是空 EOF；不把它描述为类型值损坏或 RPC 故障测试。

源码中的 asyn_send 在当前 local channel 实现里是“分批发起，再等待
完成”，没有另起 EOF worker。此前进度中的“async EOF worker 未覆盖”
应理解为尚未接入该生产发送器，不是存在一个已确认但未测试的专用
线程池。本节仍不证明并发 SQC/worker 调度、真实响应延迟、背压阻塞/
唤醒或跨进程传输；三通道矩阵也不覆盖配置阈值以上的多批次拆分。

首次 kernel 修正了“再次 wait_response 应重复失败”的 fixture 假设：
asyn_send 已收集响应，后续无待处理响应的 wait 是成功的空操作。
asyn_send 的失败码、其他通道 EOF 与清理断言保留，完整重跑通过。
23 项 CTest、12 项 build gate、源码边界与 diff 检查通过；本轮无生产
逻辑变更，dtl_utils 已加入生产构建新鲜度检查。

## 真实压力触发的阻塞与解除协议

使用首次非 NULL 的实际发送输入建立额外 LinkedExchange，重复发送其
原始表达式值，逐块 flush，但暂不读取。只将该发送通道的 buffer size
设为 1024 以限制实际分配；生产 DFC 的队列字节/块数/全局预算策略
不替换、不降低阈值，也不直接设置 block 标志。最多发送 65536 块，
必须在该上限内观察到真实接收 DFC 阻塞。

随后将发送 DFC 等待期限临时设为已过期，调用实际
wait_unblocking_if_blocked：阻塞响应必须转为发送 DFC 状态，在没有
解除消息时返回 OB_TIMEOUT，且没有新增数据发送。恢复等待期限后，
读取全部排队数据，生产 receive DFC 递减计数并发送真实 UNBLOCKING
控制消息。此时接收端已解除、发送端仍阻塞，控制消息已接收但未处理。
下一次真实 send/flush 消费该消息、解除发送 DFC，再发送并读取一行，
控制消息成功分派与控制块读尽分开检查：后续轮询观察 ITER_END 后归还
控制块。最后走生产 EOF 发送器。两端累计阻塞次数均须为 1；逐项检查所有
接收行的 tablet/DDL、字节内容，以及最终块数、pin、队列和内存归还。

该样例是单线程顺序推进的协议测试，证明真实压力触发、阻塞等待超时
及控制消息恢复，不证明两个 worker 并发等待/唤醒、取消竞争或线程
安全。恢复 DFC 等待期限仅用于继续检查低层协议，不宣称 SQL 查询超时
后可以继续执行。压力流重复一个非 NULL 输入，不代替原有不同值/NULL/
多块矩阵，也不是吞吐、RSS 或公平性基准。

首次完整 kernel 在恢复发送后的控制块计数断言失败；检查生产代码后
将解除状态与下一次轮询归还控制块分别断言，保留真实恢复与计数要求。
补充压力场景必达检查及接收 DFC 等待期限后完整重跑通过。23 项 CTest、
12 项 build gate、源码边界与 diff 检查通过；本轮无生产逻辑变更。

## 已读尽通道的收发重扫

保留同一对全局注册的 local channel，让各次实际发送输入分别完成
send/flush、FIFO 读取和 EOF，再通过生产 `fill_px_batch_info` 切换
执行上下文的 batch ID。调用接收算子的公开 `rescan()` 后，检查通道
EOF 状态和消息循环 EOF 计数均已清除，reader 为空且通道已绑定新 batch。
发送侧同样调用 `ObPxTransmitOp` 的公开 `rescan()`，不再在测试中复制
reset_state、set_channel_is_eof、set_batch_id 的生产复位逻辑。

每轮接收结果逐项与当次原始输入比较，包含 tablet/DDL、NULL 与文本
字节；不同值 burst 会跨轮改变数据，避免重复同一值掩盖旧数据残留。
单块和逐行块两种路径均检查累计收发/处理块数，EOF 后重复读取仍为空，
最终 channel pin、DFC 队列计数及内存分配/归还维持原有约束。测试域
结束时还必须确认确实发生过重扫，而不是只有单轮执行。

清理检查使用真实 `ObDTLIntermResultManager`，在同一 channel ID 下
插入旧 batch 和无关 batch 的空中间结果。生产 receive rescan 删除
旧键后，该键查询必须返回 HASH_NOT_EXIST；无关键仍可查询，随后由
fixture 清理。结束时遍历结果表必须为空。仅在初始化真实结果 map 时
为 standalone bootstrap 临时提供至少一个 CPU 的容量，立即恢复原值。
这里的空结果标记验证键选择与资源释放，不包含类型 payload 或磁盘 I/O。

边界：两端通道均已读尽后才重扫，不验证未消费的旧 recv queue 如何
处理。发送算子借用已注册通道，只调用其 init/rescan；open/transmit、
子算子执行和 SQC 调度未接入这一辅助算子。接收 spec、调度及 batch
参数集合仍由 fixture 构造，不等同于完整计划生成/序列化或跨进程 PX。
重扫样例不证明并发唤醒、取消竞争、采样状态恢复或存储型结果恢复。

接收重扫版本以及接入生产发送重扫后的完整 kernel 均通过；23 项
CTest、12 项 build gate 与源码边界检查通过。无生产逻辑/ABI/build
ID/格式变更；补充中间结果 manager 和 exec context 实现的生产构建
新鲜度检查，复用已有生产构建。

## 回归证据

`rust/plugin-runtime/tests/rust_sort_plan_fixture.h` 从顶层 resolver、完整
rewrite、optimizer 和 codegen 生成实际算子树，然后执行已安装 Rust DSO
提供的表函数和 SQL 排序。没有手工填写逻辑计划、排序 spec 或输入 frame。

10 类场景各运行 scalar 与 3 行 batch，共 20 个计划；每个计划执行并
rescan，共 40 次正常执行。18 个插件排序计划再验证首个 comparator
调用后取消，必须返回超时且不继续调用 comparator，close 后 lease 归零。

场景包含：升序、降序、多键和重复值、LIMIT、OFFSET、CASE 部分 NULL
升降序、嵌套 Rust identity、显式 BINARY 对照，以及 1,031 行重复键的
完整排序和 Top-N。断言实际生成 Top-N、禁用原生编码及下推过滤、运行期
不重复解析绑定。大输入结果使用独立指定的 key rank 与 ordinal 作为 oracle。

参考类型按 Unicode 字符数、再按 UTF-8 字节排序。例如 `z`、`🙂`、`aa`
的逻辑顺序与原生 BINARY 不同；对照用例验证没有以字节比较冒充插件语义。

生产构建、完整 kernel runner、23 项 CTest、源码边界、12 项 build gate
和 diff 检查通过。Rust ABI/build ID/持久格式不变，GIS 继续使用 C++。

## 仍需推进

测试使用内存 schema、inner session 和只分配临时目录编号的测试服务，
任何文件 open 都使测试失败。因此不是实库鉴权、持久表扫描、外部排序
落盘或存储 I/O 证据。prefix/with-ties、PX 调度/跨进程计划、窗口内排序、
set/group/hash/index、存储下推和恢复需继续分别接入及验证。当前 SQL
入口覆盖显式普通 ORDER BY，不能据此声称任意排序消费者都已开放。

插件化整体仍需完整类型与深度 planner/index 协议、实库 catalog 事务与
权限/保存点/并发、AI 异步与资源预算、轻量化指标及 Bazel/跨平台验证。
