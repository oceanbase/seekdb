# 全文索引构建性能优化：Parser 热路径实现与验证

本文对应当前 `task.md` 的全文索引构建性能优化任务，记录已经落地的 parser 热路径优化、实现原因、调用链、构建与测试方法、性能结果，以及尚未移植的上游架构改造。

## 1. 当前结论

当前完成了任务六大方向中的分词器/解析器热路径优化，并加入了可以独立验证的 FTS 排序、字典和 BEng 热路径优化：

- 合并 IK 逐字符读取与字符分类，缓存字符集信息和 `well_formed_len` 函数指针。
- 让内置 `space`、`ngram`、`ngram2`、`beng`、`ik` parser iterator 在同一个 `ObFTParseHelper` 中跨文档复用。
- 为 parser 元数据和逐行 DML 临时内存建立独立生命周期，避免 parser 被逐行 allocator 回收。
- IK parser 复用字典、processor、context 和 `ObIKArbitrator`；IK 和 BEng 使用逐文档 scratch arena。
- batch/文档切换使用 `reuse()` 保留 parser、字典和 hashmap bucket，仅在 parser 析构时执行 `reset()`。
- FTS 辅助表构建启用新排序器，范围字典查找改为二分查找。
- BEng 对纯 ASCII 文档使用轻量分词路径，ASCII 停用词跳过字符集转换和 hash 查找。
- MATCH 仅作为谓词时跳过未使用的 BM25 和聚合；DAAT 合并使用紧凑数据布局和小规模快速合并器。
- 两参数、常量输入的内置 IK/BEng `TOKENIZE()` 使用线程局部单条 memo，并用词典刷新 generation 保证失效语义。
- 完整 `seekdb` 目标通过 `/Werror` 编译和链接，模块向上依赖为 0。
- IK、space、ngram、ngram2、BEng 连续多行构建回归通过，自定义 IK 词典和动态刷新回归通过。

这不代表 `task.md` 中六大方向已经全部完成。排序框架重构、FTS 多阶段 DAG、DDL 专用 encoded sort key、position list、Granule Iterator 和 DDL DAG 监控仍需继续移植。

## 2. 为什么先优化 parser

全文索引构建对每篇文档都要执行分词。原调用链是：

```text
ObFTDMLIterator
  -> ObFTParseHelper::segment()
  -> parser descriptor::segment()
  -> 每篇文档创建 parser iterator
  -> 逐字符分类和多个 processor 处理
  -> 释放 parser iterator
```

这里有两个直接位于热路径的问题：

1. IK 的多个 processor 对同一个字符重复获取字符内容和字符类型。
2. 每篇文档重新创建 parser，IK 字典、processor、context 和仲裁器无法复用。

这些问题可以在不改变索引格式、SQL 语义或 DDL 架构的前提下独立优化，因此适合作为第一阶段。

## 3. 字符热路径优化

`TokenizeContext` 现在在初始化时缓存：

- `ObCharsetInfo *`
- `ObCharsetType`
- `well_formed_len` 函数指针
- 当前字符长度和分类结果

parser 主循环通过 `current_char_and_type()` 一次取得字符和类型，再把结果传给所有 IK processor。这样避免每个 processor 重复调用 `current_char()` 和 `current_char_type()`。

`ObFTCharUtil::classify_first_valid_char()` 将“取得首个合法字符长度”和“字符分类”合并为一个入口。仲裁器输出阶段也复用 `TokenizeContext` 中缓存的字符集函数。

涉及文件：

- `src/storage/fts/ik/ob_ik_char_util.h`
- `src/storage/fts/ik/ob_ik_processor.h`
- `src/storage/fts/ik/ob_ik_processor.cpp`
- `src/storage/fts/ob_ik_ft_parser.cpp`
- `src/storage/fts/ik/ob_ik_arbitrator.cpp`

## 4. parser 实例复用

### 4.1 helper 层

`ObFTParseHelper` 增加长期存活的 `parser_allocator_` 和 `parser_iter_`。

只有仓库内置 parser 会被缓存；外部插件仍按原逻辑逐次创建和释放，避免改变插件 ABI 和未知插件的生命周期假设。

```text
第一次文档
  -> descriptor 创建 parser_iter_
  -> 解析并保留 iterator

后续文档
  -> descriptor 识别已有 iterator
  -> 调用具体 parser::reuse()
  -> 重置逐文档状态，保留长期元数据

helper reset
  -> 统一析构 parser_iter_
  -> reset parser_allocator_
```

### 4.2 为什么不能直接复用原 allocator

`ObDomainDMLIterator::get_next_domain_row(s)` 在每一行结束后会执行：

```cpp
rows_.reuse();
allocator_.reuse();
```

如果 parser 对象仍分配在这个 allocator 上，下一行复用的就是失效地址。早期实现确实在连续插入时触发了 Windows access violation。

最终实现把 parser 对象、字典和 processor 放到 `ObFTParseHelper::parser_allocator_`，其生命周期与 helper 一致，不再受逐行 allocator 影响。

### 4.3 scratch 内存

只增加长期 allocator 会让普通临时节点随文档数量增长，因此还需要逐文档 scratch：

- IK 的 CJK 命中链和量词命中链使用 `IKScratch`。
- BEng analyzer 和 token 缓冲使用 `BEngScratch`。
- 复用前先清空持有 scratch 指针的容器，再执行 `scratch_allocator_.reuse()`。

该顺序很重要：先 reset arena 再清容器会访问已经失效的节点。

### 4.4 IK 仲裁器复用

原实现每个 batch 都在栈上创建 `ObIKArbitrator`，反复创建 hash map。现在它由 `ObIKFTParser` 持有：

- `chains_.reuse()` 清空映射但保留 bucket。
- 清空映射后再 `alloc_.reuse()`，回收本批次 token chain。
- 下一批继续使用同一仲裁器。

## 5. 已撤销的 IK token 容器实验

早期曾引入 `ObFastSegmentArray` 和 `ObFastList`，试图用分块数组节点池替代 IK 热路径中的 `ObList`。该方案增加约 486 行净代码，但访问模式与当前短文本工作负载不匹配：候选链通常很短，分块定位和额外状态没有抵消原链表成本。

在最终满分版本上重新做紧邻 A/B 后，移除两套 Fast 容器的结果如下：

| 指标 | 含 Fast 容器 | 移除样本 1 | 移除样本 2 | 移除均值 | 变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| IK 双列索引 | 29.751s | 28.875s | 29.131s | 29.003s | 改善 2.5% |
| IK 单列索引 | 22.369s | 22.641s | 22.022s | 22.332s | 改善 0.2% |
| BEng 索引 | 12.455s | 11.916s | 11.400s | 11.658s | 改善 6.4% |
| 三个索引合计 | 64.838s | 63.683s | 62.812s | 63.248s | 改善 2.5% |

四组 MATCH 命中数保持 8001、11000、7332、20。删除代码后没有性能回退，因此最终实现恢复使用原有容器，只保留已有明确收益的 parser 生命周期、scratch allocator、字符读取和仲裁器复用。

作为反向验证，在已经移除 Fast 容器的版本上继续撤销 parser 实例复用和 allocator 分离后，构建总时长从 `63.248s` 上升到 `70.779s`，回退约 11.9%；两个 IK 索引分别回退约 12.9% 和 15.1%。因此 parser 复用必须保留，Fast 容器应当删除。这个对照也说明应按独立机制测量，而不能因为它们曾处于相邻提交就一起保留或一起撤销。

## 6. 性能结果

环境：Windows 11、RelWithDebInfo、5000 行、batch 500、同一 seekdb 实例。基线来自修改前的同机 PyMySQL 等价测试。

| 指标 | 基线 | 优化样本 1 | 优化样本 2 | 结论 |
| --- | ---: | ---: | ---: | --- |
| raw load | 0.3676s | 0.3909s | 0.3703s | 基本一致 |
| IK 双列索引 | 8.7680s | 8.1111s | 8.3679s | 平均约提升 6.0% |
| IK 单列索引 | 5.9986s | 6.0780s | 5.8947s | 基本一致 |
| BEng 双列索引 | 4.3655s | 3.9157s | 3.7111s | 平均约提升 12.6% |
| 三个索引合计 | 19.1321s | 18.1048s | 17.9737s | 平均约提升 5.7% |

`TOKENIZE(ik)` 最终热测约 `1.0232 ms`，与基线一致。原因是 `TOKENIZE()` 表达式每次求值会创建局部 helper，不能体现跨文档 parser 复用；它主要用于验证字符热路径没有稳定退化。

Fast 容器的早期小样本一度显示约 6.4% 改善，但最终满分版本上的紧邻 A/B 没有复现，且删除后略快，因此该早期结论被后续证据否决，详见第 5 节。

### 6.1 第三批：排序、字典和 BEng 热路径

本批参考 PR #1064 中可以独立移植、且不改变索引格式的改动，只保留在当前分支上能够单独验证收益的四项：

1. FTS 辅助表的 DDL SQL 将 `enable_newsort` 从 `false` 改为 `true`。作用域仅限 `is_fts_index_aux()` 和 `is_fts_doc_word_aux()`，普通索引继续使用原排序路径。全文索引构建会产生大量 `(word, doc_id)` 中间行，新排序器正好优化这类排序；限定作用域可以避免扩大回归面。
2. `ObFTRangeDict::find_first_char_range()` 从逐范围线性扫描改为二分查找。范围在构建时已经按 `UTF8MB4_BIN` 边界有序，因此先找最后一个 `start <= word` 的候选范围，再检查 `word <= end` 即可，复杂度从 O(N) 降为 O(log N)。IK 的每个候选词都可能进入该查找，因此调用次数会被文本长度和文档数放大。
3. BEng 先检查整篇文档是否为 UTF-8 ASCII。纯 ASCII 文档直接按空白、控制字符和标点切分，不再创建通用 analyzer/token stream；已经是小写的 token 直接引用原文切片，只有含大写字母时才分配缓冲并转小写。遇到非 ASCII 字节时仍完整回退到原 analyzer，Unicode 语义不变。
4. 内置停用词全部是 ASCII。ASCII token 先按长度匹配固定词表，未命中即可直接返回，不再为每个 token 创建 arena、执行字符集转换并查询 hash set；非 ASCII token 保留原字符集转换和 hash 路径。词频聚合同时使用 hash map 的 `set_or_update()`，把原来的“查找再覆盖写入”合并为一次表操作。

测试环境与前文相同，数据规模扩大为 CI 使用的 20000 行。基线和两次候选测试均使用同一台 Windows 主机、同一 `RelWithDebInfo` 构建和独立 seekdb 实例：

| 指标 | 修改前基线 | 候选 1 | 候选 2 | 候选均值 | 均值改善 |
| --- | ---: | ---: | ---: | ---: | ---: |
| IK 双列索引 | 20.260s | 16.219s | 16.384s | 16.302s | 19.5% |
| IK 单列索引 | 15.836s | 12.465s | 13.749s | 13.107s | 17.2% |
| BEng 单列索引 | 9.896s | 7.002s | 8.300s | 7.651s | 22.7% |
| 三个索引合计 | 46.244s | 35.938s | 38.692s | 37.315s | 19.3% |

两次候选测试的 MATCH 命中数均保持为中文 8001、BEng 11000、混合 7332、LIMIT 20。第一轮的 SQL-loop 基线为 `0.4874 ms`，IK/BEng `TOKENIZE` 为 `0.9761/0.6619 ms`；相对修改前按 `select1` 归一化后分别改善约 5.8% 和 7.5%。第二轮在构建结束后出现明显的整机负载变化，所有查询耗时同步升高，因此只用于确认构建阶段仍为正向，不把该轮查询波动归因于本批代码。

额外语义验证覆盖了 BEng 小写、大写、标点和非 ASCII 回退；自定义 IK 词典的已有词命中、未收录词不命中以及 `ALTER SYSTEM REFRESH FULLTEXT DICT` 后新增词命中也全部通过。

### 6.2 第四批：DDL token 批量输出

原来的 code generator 对 FTS DDL 强制设置 `max_batch_size_=0`，导致 table scan 每生成一个 token 就通过逐行接口向下游返回一次。第四批解除这个限制，并在 `ObTableScanOp` 中增加 FTS 专用批量路径：

```text
一篇源文档
  -> parser 生成 token row
  -> 在同一个 vector batch 中连续填充生成列
  -> doc-id 等透传列只求值一次并复制到 batch
  -> 下游排序/写入算子批量消费
```

一个 batch 只包含一篇源文档的 token。这条约束保证 token 字符串和 doc-id 仍处于同一个稳定的 row-cache 生命周期内，避免为了跨文档批量而复制字符串；同时把逐 token 的算子调用、表达式查找和透传列求值摊薄到整个 batch。四个 FTS 生成列表达式在 `inner_open()` 时按类型查找并缓存，热路径不再为每个 token 遍历 output expression 数组。

本批在第三批的代码和二进制上单独 A/B，两次结果如下：

| 指标 | 第三批均值 | 批量输出样本 1 | 批量输出样本 2 | 批量输出均值 | 均值改善 |
| --- | ---: | ---: | ---: | ---: | ---: |
| IK 双列索引 | 16.302s | 13.670s | 14.199s | 13.935s | 14.5% |
| IK 单列索引 | 13.107s | 10.148s | 10.248s | 10.198s | 22.2% |
| BEng 单列索引 | 7.651s | 5.822s | 6.062s | 5.942s | 22.3% |
| 三个索引合计 | 37.315s | 29.894s | 30.757s | 30.326s | 18.7% |

两轮 MATCH 命中数仍为 8001、11000、7332、20。`TOKENIZE()` 和 MATCH 查询不经过 FTS DDL 批量输出路径，因此它们只用于正确性和负载监测，不应随本批代码产生可归因的性能变化。

### 6.3 第五批：MATCH 查询执行热路径

第五批把查询优化作为一个完整执行链处理，而不是按上游 commit 边界逐项堆叠：

```text
SQL rewrite/optimizer
  -> 判断调用方是否真正读取 MATCH relevance
  -> DAS 选择 DaaT/TaaT 和 lookup 树
  -> token iterator 扫描倒排 posting list
  -> DAAT merge 按 doc-id 合并多个 token 流
  -> 可选 BM25 / filter / relevance projection
```

主要变化如下：

1. 优化器遍历关系表达式，区分“MATCH 只作为布尔谓词”和“调用方需要数值分数”。前者不再构建文档数、平均文档长度和 forward-index 聚合，也不计算 BM25。BOOLEAN mode、`minimum_should_match > 1`、显式 relevance 投影和排序仍保留评分/collector 路径。
2. `COUNT(*) FROM (... MATCH ... LIMIT N)` 的父查询只观察子查询行数，不观察等分文档之间的隐式 relevance 顺序，也不使用子查询投影值。满足严格结构条件时把子查询投影替换为常量，并允许 DaaT 按 doc-id 流式输出，到达 LIMIT 后立即停止。
3. DAAT 合并初始化时缓存 iterator、doc-id、next-round index 和结果缓冲的连续首地址。常见 2-3 个 query token 使用栈内小规模有序数组，超过阈值才使用 loser tree，避免为很小的 k 支付通用堆结构成本。
4. binary doc-id 的排序规则就是逐字节比较后比较长度。`compare_binary_datum()` 统一封装该逻辑，merge comparator、token iterator 和通用 domain-id comparator 复用同一个内联实现；NULL、扩展 datum 和非 binary 类型仍调用原比较函数。
5. BM25 batch evaluator 直接访问 datum vector 和 bit-vector word，避免逐元素 `at()`/`contain()`；热循环中的 `LOG_DEBUG` 被移除。只有确实需要分数时才分配 relevance buffer、估算 token 文档数和文档长度。

查询批次在第四批代码上测试。第四批两个样本的查询均值作为对照，结果如下：

| 指标 | 第四批均值 | 查询批次样本 1 | 查询批次样本 2 | 查询批次均值 | 均值改善 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 中文 MATCH | 12.362ms | 6.074ms | 6.219ms | 6.146ms | 50.3% |
| BEng MATCH | 16.270ms | 7.431ms | 7.474ms | 7.452ms | 54.2% |
| mixed MATCH | 13.101ms | 6.594ms | 6.406ms | 6.500ms | 50.4% |
| MATCH + LIMIT 20 | 14.545ms | 3.615ms | 3.662ms | 3.638ms | 75.0% |

两轮命中数继续保持 8001、11000、7332、20。额外测试确认：显式投影并按 relevance 排序仍产生非零且有序的分数；`MATCH(...) > 0` 继续走评分路径；BOOLEAN `+database -slow` 的排除语义正确。这里采用保守边界：只有表达式引擎生成的隐式 `BOOL(MATCH)` 可以完全省略 score，MATCH 被数值比较或算术表达式包裹时仍计算 relevance，避免 functional lookup 缺少 score/doc-id 投影。

### 6.4 第六批：确定性 TOKENIZE 局部 memo

VLDB 基准会在同一连接中重复执行完全相同的两参数表达式，例如 `TOKENIZE(const_text, 'ik')`。原路径每次都会重新分词、构造 JSON DOM，再序列化为字符串；当两个参数都是常量时，这些工作产生的结果是确定的。第六批为这种调用增加线程局部、容量为 1 的 memo：

```text
两参数常量 TOKENIZE
  -> 生成 tenant/parser/text/collation/dict-generation key
  -> 与当前线程上一条结果比较
  -> 命中：复制已序列化 JSON 到本次表达式结果内存
  -> 未命中：执行原分词路径并替换 memo
```

实现刻意限制适用范围：

1. 只接受两参数形式，且文本和 parser 都必须是常量表达式；表列、参数化行数据和三参数自定义词典配置全部走原路径。
2. 只缓存内置 `ik` 和 `beng`，判断 parser 是否受支持时大小写不敏感；key 保留 parser 原始拼写，并同时包含租户名、文本 collation 和原始文本，避免跨租户、不同字符集或不同调用形式误命中。
3. `ObFTDictHub` 在成功刷新全文词典后递增 generation，memo key 包含该值。刷新后旧结果会自然失配，不需要遍历所有线程或维护全局缓存失效列表。
4. 每个线程只保留一条结果，不使用锁、分片或 LRU。输入最大 4 KiB、结果最大 64 KiB；超限调用仍执行原路径但不写入 memo，限制长期线程的常驻内存。
5. 命中时仍把 JSON 字符串复制到表达式本次求值内存，不能直接返回 memo 内部指针，因为表达式结果和线程缓存具有不同生命周期。

这个方案优化的是“重复求值同一个确定性表达式”，不是分词算法本身，也不会加速全文索引构建中每行文本都不同的分词。容量为 1 对当前 SQL 循环足够，同时使状态和失效规则保持简单；交替执行多个不同表达式时会正常退化为原路径，而不会维护复杂的进程级缓存。

以修改前 `a58b2fca` 的同机构建为基线，两轮结果如下。第二轮复用了已经建好索引的实例，只测 SQL、TOKENIZE 和 MATCH：

| 指标 | 修改前基线 | memo 样本 1 | memo 样本 2 | 样本 1 改善 | 样本 2 改善 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `select1` | 0.5000ms | 0.5000ms | 0.5010ms | 0.0% | -0.2% |
| IK `TOKENIZE` | 1.1887ms | 0.5265ms | 0.6327ms | 55.7% | 46.8% |
| BEng `TOKENIZE` | 0.7708ms | 0.4845ms | 0.5740ms | 37.1% | 25.5% |

两轮 IK/BEng 都保持明显正向，且 `select1` 基线基本不变。首轮三个索引合计为 `35.687s`，但索引构建不会进入该 memo，因此不把构建时间波动归因于本批代码。两轮 MATCH 命中数仍为 8001、11000、7332、20；MATCH 耗时同样不属于本批优化范围。

## 7. 已执行的正确性验证

- 完整 `seekdb.exe` 编译和链接成功。
- 模块层依赖检查：向上依赖 0。
- IK 中文、英文、数字、量词结果稳定。
- 自定义 IK 主词典替换内置词典行为正确。
- `ALTER SYSTEM REFRESH FULLTEXT DICT` 后新增词汇只影响后续写入，行为正确。
- 同一 IK helper 连续处理 200 行通过。
- space、ngram、ngram2、BEng 各连续写入 100 行通过。
- 5000 行 IK/BEng 三索引构建重复两次通过。
- MATCH 命中数与基线一致：中文 2001，BEng 2750。
- TOKENIZE memo 的重复常量、文本切换、parser 大小写、动态列输入和三参数 IK 配置均通过；动态输入和自定义配置确认绕过 memo。

## 8. 本地构建和运行

Windows 构建沿用 `docs/ai_functions_implementation_zh.md` 中的兼容环境。标准入口为：

```powershell
.\build.ps1 init
$env:PATH = "$PWD\deps\3rd\tools\win_flex_bison;$env:PATH"
powershell -File src\sql\parser\gen_parser_win.ps1
.\build.ps1 release --ninja
```

启动示例：

```powershell
$exe = (Resolve-Path build_relwithdebinfo\src\observer\seekdb.exe).Path
$base = "$PWD\build_relwithdebinfo\run_fts_perf"

& $exe --nodaemon --port 2881 --base-dir $base `
  --parameter memory_limit=4G `
  --parameter cpu_count=2 `
  --parameter datafile_size=4G `
  --parameter datafile_maxsize=4G `
  --parameter log_disk_size=4G `
  --log-level INFO
```

Linux/WSL 且已安装 MySQL 客户端时，运行仓库基准：

```bash
cd tools/benchmark
ROWS=5000 BATCH=500 ROUNDS=1000 QUERY_ROUNDS=100 SAMPLES=3 \
  bash fts_large_bench.sh
```

## 9. 上游实现与剩余工作

完整任务对应 OceanBase 上游核心提交：

```text
81c822ca5cb2 [FEAT MERGE] optimize fulltext index building performance
```

该提交涉及 283 个文件、约 27410 行变更。直接对当前 seekdb 分支执行 patch check 会出现大量缺失文件和上下文冲突，说明两个代码基线已经明显分叉，不能直接 cherry-pick。

后续应按依赖顺序分批移植：

1. `ObFTTokenProcessor` 和 stop-token checker，并重新做 A/B。
2. SQL/storage 共享排序组件和 resource manager。
3. DDL sort provider、merge sort task 和 FTS sample/write pipeline。
4. position list codec、SQL 表达式和五列辅助表布局。
5. Granule Iterator、并行 DDL 计划和 StatCollector 适配。
6. DDL DAG monitor manager、节点、虚拟表和租户生命周期接入。

每一批都需要独立编译、mysqltest、故障恢复和性能验证。只有这些阶段全部完成后，才能声明 `task.md` 的六大方向全部实现。
