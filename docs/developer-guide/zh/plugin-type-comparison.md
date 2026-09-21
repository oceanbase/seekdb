# 自定义类型比较：C ABI、Rust SDK 与宿主绑定

状态：类型比较已接入 loader、宿主 provider、SQL 标量比较、BETWEEN、标量/元组列表 IN
与 simple CASE 匹配、平坦及嵌套元组比较；完整 kernel 回归已通过。
**ORDER BY、GROUP BY/DISTINCT、hash join 和索引尚未接入此回调**。
不能把类型提供了 comparator 当作所有 SQL 行为已经具有插件语义。

## 接口与语义

`execution_spi.h` 的 `seekdb_plugin_type_codec_service_v2_t` 保留完整 codec v1
前缀，追加 `compare` 和保留字段。codec SPI minor 1 表示这一可选能力，与 scalar
SQL-context minor 1、table service minor 1 的含义无关。旧 codec 不需要改写；
缺少大小/minor/回调的服务返回不支持，不在比较入口回退到物理字节排序。

插件对同一种逻辑类型的两个**已解码、非 NULL** 值返回 -1、0 或 1。该可选协议
定义稳定的全序：反对称、传递，0 表示该排序关系下相等；不依赖调用顺序、地址、
可变状态或 session collation。物理编码不必具有这个次序。不能把未定义排序的
类型强行纳入该协议，也不能由 comparator 的存在推断 hash/index 契约。

每个输入最多 16 MiB，同步借用；不提供 SQL、session、allocator 或 continuation。
NULL 三值逻辑和 NULL 排序由 SQL 消费方处理。函数必须线程安全，不能跨 FFI
unwind。宿主核对大小、类型身份、保留字段、输入范围和返回值；失败清空 ordering，
所以调用方必须先检查状态，不能把失败时的 0 当成“相等”。

## Rust 插件

实现 `type_comparison::Comparator`，用
`type_comparison::Service::<T>::with_codec(existing_codec)` 构造追加后缀的服务表，
并仍通过 TYPE 的 codec implementation 引用注册。SDK 保留 decode/encode 回调，
校验值的元数据、相同逻辑 ID，借用 byte slice，将 `Ordering` 转换为规范的三态结果。
`boundary` 转换错误及可展开 panic；不恢复 abort、内存破坏或所有 OOM。

实现仍须检查自身 instance 和生命周期。比较期间不能保留宿主输入。示例
`rust_text` 的 `rust_utf8` 采用 Unicode scalar 数量优先、精确 UTF-8 字节次序其次：
例如 `z` 排在 `aa` 前，明显不同于简单字节序。它不是自然语言排序；不做 locale、
Unicode 规范化或 grapheme segmentation。相等仍要求字节相同，包括内嵌 NUL。

原 `rust_utf8` 仍不声明持久类型；追加比较回调不改变它的持久列或索引语义。
新参考类型 `rust_stored_utf8` 使用独立 TYPE 与格式身份，详见下文。当前动态库与
manifest 的 build ID 为 `rust-text-stored-type-v11`，18 个服务、21 个对象。

### 独立持久类型与编码

`stored_text.rs` 通过 Rust SDK 注册 `org.seekdb.rust-text.stored-utf8`，SQL 名称
`rust_stored_utf8`；格式为 `org.seekdb.rust-text.stored-utf8.v1`、版本 1。
TYPE 声明 PERSISTENT/REQUIRES_CATALOG，模块另声明 persistent-data capability。
原 `rust_utf8` 的身份、flags、编解码与比较规则均不变。

运行时值是合法 UTF-8；存储为四字节 `52 55 54 01`（RUT + 版本 1）加每个输入
字节的按位取反。存储字节不能作为逻辑顺序；例如存储中的 `a` 排在 `z` 后，
但解码后的 Rust 比较器应判定 `a < z`。取反不是加密、校验和或压缩。
decoder 校验头、版本和还原后的 UTF-8，拒绝截断/未知版本，不做静默回退。
空串编码为仅有头部，NULL 继续由 SQL NULL 标记表达。

编码后最多 16 MiB，包含四字节头部；超限报错而非截断。普通显式 bytes-to-type
和隐式 type-to-bytes cast 不使用存储 envelope；只有存储 codec 负责这一层。
编码/解码结果在同步 callback 中由宿主复制，Rust buffer 不跨调用保留。
这是实验性持久类型参考，不表示完成了实库读写、重启、迁移或索引支持。

内核回归已验证 13 组 schema-derived 列表达式，以及 6 组正常列转换赋值。
读端使用原始 column metadata、真实 in-row LOB reader、Rust decoder，再进入
比较/cast/动态函数；写端经正常 `build_column_conv_expr` 调用 Rust encoder。
写出的存储数据再经实际 decoder 校验还原。测试检查 NULL、字符/空串/内嵌 NUL、
错误格式、结果所有权、回调次数与 lease 回收；explicit-only cast 不能用于
隐式赋值。行 buffer 仍由 fixture 提供，不是表扫描、落盘或恢复的证据。

## 宿主执行与后续 SQL 接线

`ObPluginLoader::check_bound_type_comparison` 只验证能力，不调用插件。
`compare_bound_type` 使用已解析、非零 generation/epoch 的 TYPE binding，同时固定
对象与 codec 实现的 lease 后调用。持久元数据中的 generation=0 不能直接执行；
旧 epoch、身份/格式变化或停用后的对象不能被换代实现静默替代。返回时释放 lease。
没有在 callback 内持有 loader/registry 互斥锁。

SQL 表达式的逻辑类型标记未必保存 SQL 名称，因此新增
`resolve_type_by_id`：直接按 TYPE object ID 从同一不可变 registry snapshot 取得
身份与 epoch，不通过 SQL 名称反查，也不接受函数 ID 或旧 GIS value alias。
可传入类型推导阶段的 expected epoch；不匹配时拒绝绑定，所有失败清空输出。
这一入口也可用于没有比较能力的类型；能力探测是独立步骤。

`ObIModuleProvider`、`ObServer` 和 `ObServerPluginRuntime` 已提供对应逻辑 ID
解析、比较能力探测和执行桥接。未支持该能力的 provider 明确返回不支持，不要求
所有独立 provider 同时实现；未初始化的 server runtime 返回未初始化。
解析仅复制元数据、不持有代码租约。实际探测/执行在联合取得 TYPE 与 codec lease
的同一 registry 临界区再次校验绑定 epoch，封闭“先校验、后获取”之间的竞态。
本轮没有改变公开 C ABI 或 Rust 示例的比较规则。

## SQL 标量比较接线

`PluginTypeComparisonExpr` 使用内部表达式编号 1936，Query/JIT 编号保持一致。
普通标量 `= / <> / < / <= / > / >= / <=>` 在物理类型降级前处理逻辑类型：

- 复用 Rust common-type/cast 选择。需要时先解码持久值，或插入声明允许的隐式
  cast；没有公共类型时返回类型错误。原生类型之间继续沿用普通 SQL 转换规则。
- 公共类型仍为插件 TYPE 时，按逻辑 ID 和 epoch 绑定 comparator，再将
  `a OP b` 降为 `compare(a, b, hidden_binding) OP 0`。保留原始关系运算符节点，
  不把它强制转换为不同的 raw-expression C++ 类。替换后刷新表达式属性。
- 若声明的隐式 cast 选择了原生公共类型，则先执行插件 cast，再用原生比较。
  因而 `rust_utf8('z')` 与另一个 `rust_utf8('aa')` 的次序，不一定等同于它
  与普通字符串 `'aa'` 的次序；这取决于类型与 cast 声明，不是载体自动转换。
- 普通比较遇到 NULL 返回 NULL；`<=>` 对两个 NULL 返回真，对单个 NULL 返回假。
  NULL 不传给 native comparator。参数求值、解码和比较失败作为错误传播，
  不能变成 NULL 或“相等”。调用前后检查查询状态。
- hidden binding 和 plan extra-info 按字段序列化，拥有 TYPE 身份、generation、
  epoch、格式及 NULL 模式，不保存插件函数指针。非持久 TYPE 同样适用。
  重复推导/复制使用已绑定元数据，不重新选择 comparator。

当前按需绑定的比较表达式使用已有 state-function 标记，不提前在解析阶段执行
插件。范围推导看到的是“插件比较结果对整数 0”，不是原始列的载体比较；这不
赋予原始列原生 range/hash/index 语义，也没有新增 operator family 或索引协议。

内核 fixture 另用有效表 schema，经 schema guard 和完整 SELECT 表名/列名解析，
再调用实际 `ObPreRangeGraph::preliminary_extract_query_range`。11 组用例覆盖
七种比较、反向比较、AND/OR 和原生整数等值对照；插件列保留持久逻辑身份，
对应谓词得到全范围且未被消费为 storage range expression，整数对照仍得到
精确等值范围。解析、重复推导及范围提取不执行插件函数/cast/codec/comparator，
绑定后不再次查找入口。这不是完整 optimizer 成功或真实索引扫描的证明：
schema 来自内存 fixture，没有表数据、统计或物理存储。后续仍需实现插件索引
支持函数和代价协议，而不是以全范围过滤替代目标中的深层索引扩展能力。

含自定义类型的平坦及嵌套元组比较使用下述逐叶子 lowering；原生元组保持原行为。
集合子查询比较、排序/去重、实际索引与
完整预处理参数场景需要继续单独接线、验证，不能从本次标量表达式测试推导完成。

参考插件的示例（SQL 表达式执行已在 kernel fixture 验证）：

```sql
-- 两边均为 rust_utf8：字符数优先，结果为 1。
SELECT seekdb_rust_text('z') < seekdb_rust_text('aa');
-- 混合原生字符串：声明的隐式 cast 选 bytes，结果为 0。
SELECT seekdb_rust_text('z') < 'aa';
-- NULL-safe equality，结果为 1。
SELECT CAST(NULL AS rust_utf8) <=> CAST(NULL AS rust_utf8);
```

### BETWEEN / NOT BETWEEN

存在自定义类型时，值、下界、上界统一经过 Rust common-type/cast 选择；公共
类型仍为插件 TYPE 时使用 `PluginTypeBetweenExpr`（内部编号 1937）。其输入
为三个逻辑值和隐藏的 TYPE binding，返回 0/1/NULL；原始 BETWEEN 节点成为
`result BETWEEN 1 AND 1`，NOT BETWEEN 成为 `result NOT BETWEEN 1 AND 1`。
保留原有 raw-expression 类和反向语义，不把三个载体值交给原生 comparator。
公共类型是原生类型时则只插入声明允许的 cast，沿用原生 BETWEEN。

值只求值一次。值为 NULL 时立即返回 NULL、不求值边界；否则先依次求值两个
边界，再调用比较。边界表达式的错误不能因为下界比较为假而被吞掉。NULL
不进入 comparator：任何已知越界条件得到假，否则存在 NULL 边界时结果未知。
执行前、操作数求值后和每次比较前后检查查询状态；过期绑定和 Rust 错误传播。

实验插件构建中，解析器不再提前复制插件值、含插件值的表达式或尚未定型的
列/子查询来展开 BETWEEN。后续类型推导才决定是否使用插件执行器，避免提前
变成两次函数调用或两次独立类型选择。原生列继续使用现有 BETWEEN 范围提取。
新表达式复用比较 extra-info 的字段序列化，并要求 `null_safe_ == 0`；公开
C ABI、Rust 服务与持久格式不变。不承诺全部计划转换或跨进程恢复已验证。

```sql
-- Rust 按字符数优先比较，结果为 1；按普通字节比较则不成立。
SELECT seekdb_rust_text('z') BETWEEN CAST('a' AS rust_utf8) AND CAST('aa' AS rust_utf8);
-- 相同类型规则，结果为 0。
SELECT seekdb_rust_text('z') NOT BETWEEN CAST('a' AS rust_utf8) AND CAST('aa' AS rust_utf8);
```

完整 kernel 已覆盖 42 组表达式（包含原生/混合类型对照）、6 组持久列读取和
新增 6 组表列范围用例。后者包含两个原生整数 BETWEEN 对照，累计 17 组；
自定义谓词仍不被消费为载体范围。另核对重复推导/复制、单次求值、回调次数、
无公共类型、过期绑定、非法 UTF-8、取消和 lease 回收。这不是物理表扫描、
完整 optimizer 或插件索引支持函数的验证，后续仍需接入真正的索引扩展协议。

### 标量列表 IN / NOT IN

`PluginTypeInExpr`（内部编号 1938）将左值和列表候选统一交给 Rust common-type /
cast 选择。公共类型仍为插件 TYPE 时绑定同一 comparator，按列表顺序寻找相等
项，返回 0/1/NULL；原始 IN/NOT IN 节点分别变为 `result = 1` / `result <> 1`。
两种节点同属 `ObOpRawExpr`，切换种类时释放已缓存的旧 IN operator。公共类型
为原生类型时只插入声明允许的 cast，保留原生 IN 路径。

左值只求值一次；NULL 左值跳过整个列表。找到相等项立即返回，不求值后面的
候选；没有匹配时，列表中存在 NULL 则返回 NULL，否则返回假。NOT IN 保留
相同三值逻辑。NULL 不交给 comparator，已求值候选的错误正常传播，不变为
“不匹配”。操作数求值及比较前后检查取消状态，复用 TYPE/codec 的绑定与租约。

左值的物化字节使用独立临时内存，不能被后续表达式的临时区重置。候选物化
内存逐项复用，不为每个列表元素永久保留一份 LOB 副本；这不限制表达式 frame
本身的所有结果内存，也不构成零拷贝承诺。当前为线性比较，不以原生 byte hash
或字节相等替代 Rust 类型语义。

实验插件解析器保留涉及插件或未定型列/子查询的单元素 IN，避免提前改写为
普通二元比较而改变 NULL 左值的短路行为。支持的自定义列表最多 1023 个候选
（与左值合计受 `SEEKDB_PLUGIN_MAX_ARGUMENTS = 1024` 限制）；此限制不施加于
纯原生列表。该上限是当前实现约束，不是完整扩展性目标的终点。

```sql
SELECT seekdb_rust_text('z') IN (CAST('a' AS rust_utf8), CAST('z' AS rust_utf8));
-- 返回 NULL，而不是 1。
SELECT seekdb_rust_text('z') NOT IN (CAST('a' AS rust_utf8), NULL);
```

集合子查询比较仍需独立接线，当前明确拒绝；检查同时覆盖信息
提取后移除了 ANY/ALL 包装的 `SQ_*` 运算符，不能漏检后退回载体比较。
原生元组/子查询继续走原有路径。标量列表的支持不代表子查询 IN、hash/index
或完整优化器变换已经完成。

完整 kernel 已通过新增 54 组 IN/NOT IN 表达式、7 组持久列读取、6 组表列范围
和 5 组 SELECT 解析用例；覆盖复制/重复推导、NULL/短路、函数/cast/comparator
次数、非法 UTF-8、过期 epoch、取消、lease 回收以及长值/候选数量边界。表列
范围累计 23 组。原生单元素 IN 保留 IN 节点，按范围生成器定义不是 precise get；
测试确认其候选数量和闭区间边界，并以整数等值对照确认 precise-get 路径仍有效。
这些是实际内核/真实 Rust DSO 的受控 fixture 证据，不是实库事务或索引扫描。

### Simple CASE 匹配

`CASE value WHEN candidate THEN result ... ELSE fallback END` 的匹配输入含
自定义逻辑类型时，在按载体类型计算 CASE collation 之前，绑定每个
`value = candidate`。每对输入独立使用 Rust common-type / implicit-cast 规则：
不同 WHEN 可以使用不同的公共类型，不要求整个候选集合都具有同一种公共类型。
例如自定义值与自定义 WHEN 使用 Rust comparator，与原生字符串 WHEN 则可以
使用插件声明的 type-to-bytes cast 和原生比较。

所有比较共享同一个选择值表达式节点；持久选择值先建立一个共享 decoder 节点。
比较、表达式复制和执行 frame 保留这个 DAG 共享关系，不为每个 WHEN 复制
一次选择值函数。每对输入所需的 cast、WHEN 表达式仍由执行到的条件按需求值。
这区别于把整个 CASE 固定为一套字节 collation，也区别于逐个重新执行选择值。

绑定成功后保持 `ObCaseOpRawExpr` 类，将 ARG_CASE 改为普通 searched CASE，
清除选择值字段、换入条件并释放旧 operator 缓存。复用已经接线的
`PluginTypeComparisonExpr`、普通 CASE 惰性执行器及 pointer-free binding，
不增加 ABI 或 Rust 服务种类。原生-only 匹配保持原有 ARG_CASE 重写路径。
THEN/ELSE 继续独立选择输出公共类型，可以返回插件值、原生值或 NULL。

匹配使用普通 `=` 而非 NULL-safe equality，因此 NULL 不与 NULL 匹配。与列表
IN 的 NULL 左值短路不同，此路径依次求值 WHEN 比较；比较不成立时跳过 THEN，
首个成立时只执行该 THEN，不再求值后续 WHEN；没有匹配时执行 ELSE，省略
ELSE 则返回 NULL。已执行的选择值、转换、WHEN 或结果表达式错误正常传播，
不能因另一个分支未使用而吞掉。比较继承现有的取消与绑定租约检查。

```sql
-- 第一个 WHEN 使用声明的 type-to-bytes cast，第二个使用 Rust comparator。
SELECT CASE seekdb_rust_text('z')
         WHEN 'a' THEN 11
         WHEN CAST('z' AS rust_utf8) THEN 22
         ELSE 33
       END;
```

最终完整 kernel 覆盖 27 组表达式、6 组持久列读取、2 组表列范围和 3 组 SELECT
解析用例。检查原始/复制 DAG 的共享节点、真实 Rust 回调次数、混合匹配类型、
插件输出类型、NULL、UTF-8/类型错误、第二个比较绑定过期、取消与 lease 回收。
表达式经过实际 `ObTransformPreProcess::transform_expr` 后代码生成、执行，
原生-only CASE 也执行其原有转换；未把测试替身的比较函数作为结果依据。

共享求值的当前实现依赖宿主表达式 DAG/frame 语义；完整优化器变换、并行执行、
PL 专用复制与跨进程计划恢复仍需继续验证，不能仅凭局部 lowering 声称全部完成。

### 平坦元组比较

含插件逻辑值的 `(a, b, ...) OP (x, y, ...)` 支持
`= / <> / < / <= / > / >= / <=>`。左右元组须具有相同列数，每对同位置元素
独立选择公共类型和允许的 cast，持久元素通过已有 codec 解码。不同列可以
采用不同的插件类型或原生类型；没有为整行发明单一载体类型或字节比较器。

相等是逐列普通相等的 AND，不等是逐列不等的 OR，`<=>` 是逐列 NULL-safe
equality 的 AND。AND/OR 保留 SQL 三值逻辑：某列未知不代表整个行必然未知，
例如 `(NULL, 'a') = (NULL, 'b')` 为假。遇到能够确定整体结果的列时停止求值。

大小关系按字典序执行：只有当前列确定相等才进入下一列；首个不等或含 NULL
的比较决定结果，后续列不执行。使用平坦 searched CASE 表示这一选择过程，
不随列数构造一条嵌套 CASE 链。每列的自定义 comparator 结果由“是否相等”和
“对应大小关系”共享，不重复调用 comparator 或 decoder；返回错误立即终止，
不能把失败当作相等以继续下一列。

```sql
-- Rust 的字符数优先次序：z 排在 aa 前，第二列不影响结果。
SELECT (seekdb_rust_text('z'), 9) < (seekdb_rust_text('aa'), 1);
-- 第一列未知，但第二列确定不等，结果为 0。
SELECT (CAST(NULL AS rust_utf8), 1) = (CAST(NULL AS rust_utf8), 2);
```

绑定成功后原始 row 运算符变成布尔结果与 1 的比较，清除缓存的旧 row operator。
复用现有 scalar comparator ABI、extra-info、普通逻辑/CASE 节点，不新增 Rust
服务、表达式编号或行排序协议。原生-only 元组不进入该 lowering。
元组 BETWEEN 和集合子查询比较仍需进一步接线；元组 IN 与嵌套支持见后文，此处不提供 row hash/index。

完整 kernel 已覆盖 19 组输入乘七运算符，共 133 组表达式用例，另有 7 组持久
列读取、2 组表列范围和 2 组 SELECT 解析。真实 Rust DSO 的函数、cast、decoder
与 comparator 次数验证共享求值和逐列短路，覆盖 NULL、原生/混合类型、类型/
列数错误、已执行与未执行的非法 UTF-8 分支。复制/重复推导及正常表达式预处理
后再次执行，检查 lease 回收。该范围不包含完整优化器、PL 或实库扫描。

### 平坦元组 IN / NOT IN

`(a, b, ...) IN ((x, y, ...), ...)` 的左值或候选含自定义逻辑类型时，先检查
所有候选具有相同列数。按列收集左值与全部候选，自定义列统一使用 Rust
公共类型/隐式 cast 选择，原生列使用原生标量比较与转换规则。列之间相互独立，
不要求整个元组只有一个类型。所有转换节点准备成功后才发布改写结果。

转换后的左侧列节点由各候选行共享，包含持久列的 decoder 和需要的 cast。
每个左侧元素第一次被比较时才求值；已经由前列确定不匹配的候选，不会执行
该行后面的左值元素或候选表达式。同一个左值元素不随候选数量重复求值/解码。

各候选通过已有逐列行相等比较产生真/假/NULL，再由惰性 OR 组合。找到真即
停止；没有真但存在未知行结果则返回 NULL；只有所有行都确定不匹配才返回假。
NOT IN 对这个结果取反，NULL 保持 NULL。不能采用标量 IN 的“NULL 左值跳过
列表”规则，因为元组中另外一列仍可能决定该候选不匹配。

```sql
-- 第一列未知，但第二列不等，整个候选确定不匹配，结果为 0。
SELECT (CAST(NULL AS rust_utf8), 1) IN ((CAST('z' AS rust_utf8), 2));
-- 先遇到未知行，随后找到确定匹配，结果为 1。
SELECT (seekdb_rust_text('z'), 1)
       IN ((NULL, 1), (CAST('z' AS rust_utf8), 1));
```

该路径使用普通 OR、逐列比较和绑定元数据，不新增 C ABI 或 Rust 服务，也不
使用载体字节 hash。当前自定义列表最多 1023 个候选（与左值合计 1024）；
纯原生元组 IN 保持原有路径，不施加这个插件上限。集合子查询 IN、
元组 BETWEEN、完整优化器/PL/计划恢复与 row hash/index 仍需继续接线和验证。

单候选直接采用行相等结果，避免构造代码生成器不接受的单参数 OR；单列行
比较也直接采用列比较结果。完整 kernel 已覆盖 48 组正反 IN、8 组持久列
读取、3 组表列范围和 2 组 SELECT 解析。检查真实 Rust 回调次数、单次解码、
行/候选短路、NULL 与错误、混合类型、复制/重复推导、正常预处理及 lease 回收，
并验证自定义 1023/1024 候选边界与纯原生 1024 候选对照。不作为完整优化器、
PL、并行、实库事务或扫描证明。

### 嵌套元组

含插件类型的嵌套行构造也支持七种比较和 IN/NOT IN。例如：

```sql
SELECT ((seekdb_rust_text('z'), 1), 2)
       < ((seekdb_rust_text('aa'), 1), 2);
SELECT ((seekdb_rust_text('z'), 1), 2)
       IN (((NULL, 1), 2), ((CAST('z' AS rust_utf8), 1), 2));
```

先逐层验证两个操作数（或左值与每个候选）的形状：每个位置都必须同时为
标量或行，每层行的列数一致且非空。`((a,b),c)` 与 `(a,(b,c))` 虽有同样数量
的叶子也不能比较。校验后按深度优先顺序收集对应叶子，复用前述逐列转换和
比较协议；IN 对每个叶子位置跨候选选择公共类型，并共享转换后的左值。

普通相等/不等/<=> 分别组合叶子比较的 AND/OR/AND，大小关系按叶子顺序做
字典序判断。未知的前缀大小比较返回 NULL，不继续执行后续叶子；普通相等
则仍允许后面的确定不等覆盖前面的未知。候选短路与 decoder/comparator 的
单次求值规则保持不变，没有把整个嵌套行序列化成 BLOB 来比较。

原生类型 visitor 会在消费比较前拒绝混合行/标量构造，因此嵌套消费者先用
同一个 visitor 推导叶子（保留会话与局部变量上下文），插件绑定完成后再走
正常标量校验。纯原生表达式未被转换，仍执行原有行结构检查。本能力不是
通用复合类型注册、可持久化 record datum、整行 NULL 或 PG record ABI。

新增内核用例包括 18 组输入乘七运算符（126 组）、12 组正反 IN（24 组），
另有 8 组持久列读取、3 组表列范围、3 组 SELECT 和 4 组批量表达式。检查
形状/类型错误、未知后再匹配、坏数据短路、共享 decoder/cast、精确 Rust
回调次数、复制/预处理、结果与 lease 回收。二元比较验证 63 层构造成功，
64 层返回 SIZE_OVERFLOW；遍历保留深度保护，不承诺任意深度。集合子查询、
完整规划、PL、跨进程计划、实库扫描/恢复与复合类型持久对象仍需继续实现。

### 批量 frame 与惰性执行

上述 SQL 消费者已经通过实际批量表达式 frame 验证。内核 fixture 使用正常
解析、类型推导、复制、表达式预处理和代码生成，输入为手供的真实编码 LOB，
回调经生产 loader 进入 Rust DSO。19 组表达式各执行 6→3→6 行，覆盖七种标量
比较、正反 BETWEEN/IN、simple CASE、平坦元组比较/IN，以及 CASE 中的函数与
持久类型输出。这里没有运行存储扫描或完整优化器。

每批先全部跳过，随后只启用部分行、重复求值、最后启用全部行。测试断言
全跳过时函数/cast/decoder/comparator 均不调用；已求值行不重算，新增行按
自己的三值逻辑与短路规则执行，批次大小改变后不读取上一批缓存。共享值的
decoder/comparator 与 CASE 函数调用次数逐行核对；覆盖输入 LOB 后输出仍有效。

该验证发现并修正了插件节点的常量分类错误：属性提取先计算常量继承，再恢复
状态标记，原本导致字面量插件 cast 被当作标量常量。原生批量执行的标量
dry-run 即使所有行被跳过也会试算，因而 CASE 的未选中分支可能调用插件。
现在插件执行节点显式不继承输入的常量属性，并携带状态标记；原生-only 表达式
不改变。未来按明确 volatility/副作用契约接入常量优化，不能仅凭输入是常量
就推断插件回调可提前执行，也不把当前分类当作完整 volatility 支持。

错误/取消用例还检查：坏版本行初始被跳过时成功，启用后返回实际 decoder
错误；首次回调前取消不执行回调；首个真实 comparator 返回后取消不继续进入
下一行。失败批次不消费部分结果，重置 frame 后可再次成功执行，lease 归零。

这是现有逐值 ABI 在 SQL 批量执行中的正确性验证，不是单次 Rust callback
处理整批，也不提供 columnar/零拷贝或内存开销有界的证明。真正的批量插件
接口、资源预算和性能测量仍在后续范围内。

后续仍需逐项完成：

1. 逻辑类型推导、显式/隐式 cast、存储 decoder 与 NULL 语义，不把 carrier 类型
   当成插件比较规则，也不让失败变为 NULL/相等。
2. 计划保存 pointer-free binding，复制/序列化保留 generation/epoch，执行时固定
   对象和代码引用。常量折叠与取消遵守同一套确定性和生命周期约定。
3. 优化器不能据此使用载体字节的范围扫描、等值传播或 hash 算法。排序、去重、
   hash、operator family、统计和索引需要分别接入一致的协议与验证。

完整自由度还包括不受全序限制的自定义 SQL operator、类型参数和统计等能力；
这个可选 comparator 是其中一个可组合协议，不是所有类型扩展的统一准入条件。

## 比较消费链的批量参数求值

比较、BETWEEN 与标量 IN 现在提供显式 SQL batch evaluator。它们把批量
求值传递给操作数中的 Rust 函数、cast 和 decoder；TYPE comparator 本身
仍使用逐值 ABI，不把物理字节比较、hash 或排序冒充插件语义。

- 二元比较批量计算两侧，包括 NULL 安全比较；普通比较不会因左侧 NULL
  而擅自跳过右侧，与既有插件比较的标量行为一致。
- BETWEEN 先计算主值，仅对非 NULL 行计算两个边界。下界为 NULL 不跳过
  上界，下界比较为假也不阻止已经约定必须求值的上界。NULL 主值跳过边界。
- IN 按候选项推进：先计算左值，NULL 左值直接为 NULL；每个候选只计算
  尚未匹配的行。候选 NULL 记录 UNKNOWN，但不阻止后续候选匹配；首次
  匹配后跳过后续项。未匹配且见过 NULL 则为 NULL，否则为假。NOT IN
  继续由外层原生布尔运算消费三值结果。
- IN 的左值独立持有一份字节副本，候选临时内存逐行复用，避免后续表达式
  重置 SQL scratch 后读取悬空缓冲区。mask 也不借用 SQL scratch。
- 调用者跳过和已有缓存共同决定待求值行；失败清除整个请求范围的有效
  标记，不能让先前缓存标记指向已被执行器清空的结果。比较前后检查取消。

参数阶段推进可能在首个 comparator 之前解码多个输入。错误/取消不撤销
已经执行的参数，不保证跨行副作用的固定顺序。IN 自有字节副本和子表达式
frame 的总内存尚未纳入统一查询预算，这不是零拷贝或资源用量有界的声明。

新增真实 Rust DSO 测试覆盖嵌套函数比较、NULL 安全相等、经 cast 的 v3
concat 函数、BETWEEN 三值逻辑、IN/NOT IN 的候选短路与 UNKNOWN，以及
匹配后跳过非法 UTF-8 候选。原有消费链一并扩展到 6/3/1031 行，检查全跳过、
增量缓存、精确 decoder/comparator 次数、函数批量入口及 lease。取消测试
区分已经完成的操作数解码和必须停止的后续比较，而不再假设二者逐行交错。

## 验证边界

SDK 用例覆盖三态结果、错误/panic 后结果清空、非法 metadata、不同逻辑 ID、NULL、
长度和保留字段。C/Rust 布局测试对照两种新增结构的大小、对齐和字段偏移。
实际 Rust DSO 的 loader 用例覆盖 Unicode/空值/内嵌 NUL 的完整比较矩阵、非法
UTF-8、短输入标记、旧 epoch、零 generation、格式变化，以及调用后 lease 回收。
宿主白盒用例还覆盖实际旧 v1 分配边界、畸形后缀/结果、异常、原始错误与
END_OF_STREAM 拒绝；比较不能用表函数的结束状态截断后续消费。旧 GIS DSO 不支持
比较入口，仍保留原 codec 和业务函数行为。
以上 loader/SDK 测试是比较入口的证据，不是 SQL 比较、排序、hash 或索引执行的证据。

逻辑 ID 解析测试还覆盖 prepared 对象不可见、SQL 名称/其它种类 ID 拒绝、失败
输出清空、预期 epoch、quiesce 后不可见、换代后新身份，以及独立发布使旧 epoch
租约获取失败。实际 Rust DSO 的逻辑 ID 绑定与 SQL 名称绑定核对身份、格式与
generation/epoch 一致。内核 fixture 通过模块 provider 调用同一入口和真实 DSO；
这一 provider 用例本身不是 SQL 运算符自动绑定的证明。

新增内核回归通过真实解析、类型推导、raw-expression 复制、代码生成及实际 Rust
DSO 执行验证七种标量运算符，共 84 组结果用例。用例包含三态次序、Unicode、空串/内嵌 NUL、typed
NULL、CASE 结果与显式/隐式 cast、原生对照、extra-info 序列化/截断、旧 epoch、
Rust 回调错误及取消，另覆盖正常 SELECT/WHERE 解析、无公共类型、失败 cast、
早期自定义 tuple 拒绝及原生 tuple 保持（后续支持见上文平坦元组章节）。完整生产构建、23 项 CTest 和完整 kernel
重新执行通过；不作为实库或全部优化器变换、持久列扫描、跨进程计划恢复的证明。
