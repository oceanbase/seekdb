# 插件类型的 MIN/MAX 聚合

## 接入方式

插件可以通过 TYPE comparator 定义自身的全序；`MIN` / `MAX` 消费这一
语义，不把编码字节或运行期字符串的内建排序当作插件顺序。没有 comparator
的自定义类型在绑定时返回不支持，不静默按字节处理。原生类型保持原有路径。

`PluginTypeValueExpr` 增加内部 `ORDERED` 模式：值的 carrier 仍是解码后的
逻辑值，额外携带固定 TYPE binding。绑定包含对象身份、owner generation、
catalog epoch、格式信息，不保存模块函数指针。比较元数据按需在计划 allocator
中分配，只有需要排序语义的 carrier 才拥有它；复制和序列化保留独立、可校验
的元数据。此模式是 SQL 内部表示，不是新增插件 C ABI 或存储格式。

MIN/MAX 类型推导时，存储值先取得显式 decoder，再包装排序语义绑定；动态
函数或 CASE 产生的运行期值直接包装。聚合结果保留逻辑类型身份，因此可继续
传入同类型消费链，而不是丢失身份变成普通字符串。重复类型推导不重新选择
已经绑定的 comparator。原生兼容类型不生成排序包装层。

## 执行与生命周期

聚合处理器在逐行更新、批量更新和 rollup 合并时，使用输入 carrier 中的
固定比较器。状态保存解码后的值；合并不重新解码。NULL 沿用 MIN/MAX 的
忽略规则，全 NULL 输入产生 NULL。批量参数可继续调用嵌套 Rust 函数的
批量入口；比较器自身仍逐值调用。

`MIN/MAX(DISTINCT custom_value)` 在类型推导时消除 DISTINCT：删除重复项
不会改变极值，无需走尚未接入插件 hash/equality 的物理 carrier 去重阶段。
这不是一般 `COUNT(DISTINCT)`、GROUP BY 或自定义等价类的支持声明。

比较前后检查查询状态，比较失败或取消不提交该次状态替换。模块 provider
继续经过真实 loader 校验 binding 并持有调用期 lease。状态本身没有可执行
指针；当前并未新增覆盖整个聚合生命周期的 module lease。执行中类型换代
可能导致旧绑定失败，不承诺运行中热换代透明成功。

## 验证范围

`rust_aggregate_fixture.h` 使用真实 SQL 表达式解析、类型推导、raw-expression
复制、代码生成、聚合处理器与已安装 Rust DSO。输入 schema/LOB/frame 和
`ObAggrInfo` 配置由 fixture 提供，不经过完整 SELECT 优化器/算子生成流程，
也不启动数据库服务。

基础场景覆盖 MIN、MAX、MIN(DISTINCT)、MAX(动态 Rust 函数) 与显式 BINARY
原生对照。样例包含 Unicode、内嵌 NUL 和 NULL，Rust 按字符数优先的顺序与
字节顺序不同；6/3/1031 行跨批聚合检查解码次数、函数入口和结果。
排序元数据有深拷贝、序列化及截断输入校验。

补充回归覆盖全跳过、部分跳过、全 NULL、逐行处理两组数据后 rollup 合并，
以及聚合比较器的非法 UTF-8、比较后取消、元数据 epoch 不一致和恢复使用。
失败的比较不替换既有状态，合并不再次解码，调用后 module lease 归零。
生产构建、首轮和补充后的完整 kernel、23 项 CTest 与边界/build gate
已通过；首轮 CTest 的 Rust 脚手架因 lld 段错误失败，完整重跑通过，未跳过。

### 完整 SELECT 计划与算子执行

`rust_aggregate_plan_fixture.h` 增加真实顶层 resolver、hint 分发、完整 rewrite、
optimizer、`ObCodeGenerator` 与生成算子树的执行。其输入来自 Rust 表函数
`seekdb_rust_words_bytes`；该名称中的 bytes 指输入，其 token 输出仍是
`rust_utf8`。测试不手供逻辑计划、聚合参数、输入 frame 或输出映射。

7 类 SQL 在逐行与 3 行 batch 下形成 14 个计划，每个计划执行后 rescan 再
执行一次。覆盖直接 MIN/MAX、DISTINCT、空输入、全/部分 NULL、嵌套 Rust
identity，以及 `CAST(token AS BINARY)` 的原生排序对照。对 `z aa 🙂 bbb`，
插件极值是 `z` / `bbb`，原生字节极值是 `aa` / `🙂`。除结果外，还检查实际
生成的 ORDERED 元数据、精确比较次数、无执行期重复解析，以及 close 后 lease
归零。cursor 在第一次读取时打开，不在算子 open 时提前打开。

执行验证发现标量聚合无条件申请临时目录并重复初始化 group。现在只有需要
临时目录的聚合或 DISTINCT 才申请；普通 MIN/MAX 仅初始化一次 group，能在
没有临时文件服务的该 fixture 中执行。需要落盘的聚合没有被降级为内存模式，
但本测试不提供 spill/I/O 的验证，也不声称已测得启动/RSS 的改善。

最终完整 kernel 与生产构建通过，原有处理器和 codec 测试仍保留。这个新增
证据使用内存 schema 和 inner session，没有数据库服务、鉴权、持久表扫描或
PX 调度；不能外推为任意 SELECT、存储下推、窗口/多阶段或跨进程计划已支持。

完整插件化目标仍包括：更复杂 SELECT 消费/存储下推与多阶段计划验证、分组/窗口
算子实际执行、排序/hash/operator family、用户自定义聚合状态与合并协议、
资源预算及跨进程计划恢复。MIN/MAX 只是已有聚合器对 TYPE 语义的一个消费点，
不替代以上范围，也不表示 ORDER BY 已接入。

Rust 示例模块的 C ABI、对象身份、build ID 和持久格式不变；GIS 继续使用 C++。
