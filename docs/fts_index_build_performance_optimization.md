# OceanBase 全文索引构建性能优化

## 1. 目标与边界

本次工作以 `tools/benchmark/fts_large_bench.sh` 为统一评价入口，优化目标包括：

1. 降低 `TOKENIZE(..., 'ik')` 的逐字符 CPU 开销；
2. 降低 `TOKENIZE(..., 'beng')` 的字符分类、大小写归一化和 token 内存开销；
3. 保持分词结果、词频统计和全文索引语义不变；
4. 不通过修改数据规模、查询语句或评分脚本规避真实性能问题。

基准固定参数为：`rows=20000`、`batch=500`、`rounds=3000`、`query_rounds=200`、`samples=3`、`warmup=30`。评分脚本先分别计算 build、tokenize、query 三类指标的平均提升，再对三类等权平均；平均提升达到 50% 即为满分。

## 2. 构建链路分析

全文索引构建的主要链路可以概括为：

`DDL 任务调度 -> 扫描原表 -> 文本解析/分词 -> 词项聚合 -> 辅助表写入 -> 排序/归并 -> 索引可用`

其中，分词器位于每行、每个全文列都会执行的热路径。单次调用中只有微秒级的冗余，在两万行、多列和多个全文索引上也会被成倍放大。因此，本次补丁优先处理可以同时改善独立 `TOKENIZE`、BENG 索引构建和 IK 索引构建的公共热点。

仓库中与题目其他方向相关的基础设施已经存在：

- SQL 排序算子位于 `src/sql/engine/sort/`；
- 存储层外排位于 `src/storage/ob_parallel_external_sort.h`；
- order-preserving sort key 编码位于 `src/storage/ob_order_perserving_encoder.*`；
- encoded sort key 的表达式、代码生成和优化器开关位于 `src/sql/engine/expr/ob_expr_encode_sortkey.*`、`src/sql/code_generator/ob_static_engine_cg.cpp` 和 `src/sql/optimizer/ob_log_sort.cpp`；
- FTS DDL 编排位于 `src/rootserver/ddl_task/ob_fts_index_build_task.*`。

这些模块影响范围大，若没有 profile 和完整回归环境，不应为了追求改动规模而盲目重构。本次选择能证明根因、能保持接口兼容、能由单测覆盖的热路径优化。

## 3. 实现内容

### 3.1 IK：消除每字符重复上下文读取

原实现中，`ObIKFTParser::process_next_batch()` 已经取得当前字符指针、字节长度和字符类型，但 `process_one_char()` 又调用每个处理器的 `process()`。每个 `process()` 再次通过 `TokenizeContext` 获取相同的字符信息。

IK 默认包含四个处理器，因此每处理一个字符都会产生多次重复函数调用、边界判断和状态读取。优化后，外层取得的 `(ch, char_len, type)` 直接传给各处理器的 `do_process()`，每个字符只读取一次上下文。

复杂度仍为 `O(字符数 × 处理器数)`，但显著降低常数项，不改变处理器顺序和状态机语义。

### 3.2 IK：处理器容器改为小数组

IK 处理器数量固定为四个。原实现使用链表保存处理器指针，遍历时存在节点跳转，初始化时还需要为链表节点分配内存。

优化后使用 `ObSEArray<ObIIKProcessor *, 4>`：

- 四个指针全部存放在对象内嵌空间；
- 遍历访问连续；
- 不改变处理器对象本身的分配、析构和执行顺序；
- 容量与当前处理器数量一致，不触发扩容。

### 3.3 BENG：ASCII 字符分类快路径

BENG 基于 whitespace tokenizer。英文文档中的绝大部分字节是 ASCII，但原实现对每个字符都调用通用字符集函数判断多字节长度、空白、控制字符和标点。

优化后：

- ASCII 字节的字符长度直接判定为 1；
- ASCII 分隔符使用字母数字判断完成；
- 非 ASCII 字节继续走原有字符集函数。

这样保留了多字节字符的兼容路径，同时缩短英文基准的最常见分支。

### 3.4 BENG：大小写归一化快路径

原实现对每个 token 都调用通用 `ObCharset::tolower()`。优化后先扫描 token：

- 全 ASCII 且没有大写字母：直接引用原 token，不分配、不拷贝；
- 全 ASCII 且包含大写字母：按字节完成小写转换；
- 包含非 ASCII 字节：回退到原有字符集大小写转换。

该策略不会对非 ASCII 文本使用不完整的 ASCII 规则。

### 3.5 BENG：消除重复 token 拷贝

原链路先由 normalizer 生成 token，随后 `ObBEngFTParser::get_next_token()` 再次分配并复制。优化后解析器直接返回 token stream 的缓冲区。

零拷贝必须满足生命周期约束：返回的 token 在当前文档迭代完成前必须保持稳定。因此同时取消了 normalizer 在每次 `get_next()` 开头复用 arena 的行为；arena 只在开始分析下一篇文档时复用。其效果是：

- 小写 ASCII token 直接引用原文档；
- 大写或非 ASCII token 只保留一次归一化存储；
- 当前文档内已经返回的 token 不会被后续 token 覆盖；
- 下一篇文档开始时仍可整体复用内存。

## 4. 正确性约束

优化保持以下不变量：

1. IK 四个 processor 的顺序不变；
2. IK 每个 processor 接收到的字符指针、长度和类型与原实现相同；
3. BENG 对非 ASCII token 仍调用原字符集大小写转换；
4. BENG 返回 token 的有效期覆盖当前文档的完整迭代；
5. 分词器接口、词频、最小/最大词长过滤和停用词处理方式不变。

新增 `test_basic_english_token_lifetime`，连续获取两个需要分配小写缓冲区的 token，并在第二次获取后再次检查第一个 token，防止 arena 过早复用导致内容被覆盖。

## 5. 验证方法

### 5.1 Linux 构建与单测

```bash
bash build.sh debug --init --make
cd build_debug
ctest --output-on-failure -R test_text_analyzer
```

若项目提供按目标构建方式，应优先只构建并运行 `test_text_analyzer`，通过后再执行更大范围测试。

### 5.2 功能回归

至少检查：

- `TOKENIZE` 的 IK、BENG 输出与优化前一致；
- 三个全文索引均能成功构建；
- 四条 `MATCH` 查询的命中数分别保持为基准数据对应的值；
- 重复运行时无崩溃、悬空引用或 token 内容覆盖。

### 5.3 性能基准

```bash
cd tools/benchmark
LABEL=after OUTPUT=./fts_after.txt bash fts_large_bench.sh
python3 fts_large_bench_score.py ./fts_after.txt
```

正式对比应满足：

- 使用相同编译类型和编译参数；
- 使用相同机器、CPU 频率策略和数据库配置；
- 尽量避免其他负载；
- before/after 各运行至少三次完整基准；
- 除平均值外同时报告标准差，避免只选择最好的一次结果。

## 6. 题目示例结果复算

使用仓库内 `fts_large_bench_score.py` 对题目给出的示例数据复算：

- build 类平均提升：64.95%；
- tokenize 类平均提升：70.34%；
- query 类平均提升：76.27%；
- 三类等权平均提升：70.52%；
- 最终得分：100.00 / 100。

该结果说明题目示例已经超过 50% 的满分阈值。上述数字仅是对题目所给报告的复算，不应冒充本机实测结果；最终提交应附上修改后实际运行生成的完整报告。

## 7. 后续优化建议

若已有完整 Linux 构建、服务运行和 profile 环境，可按以下顺序继续：

1. 通过 `perf record`/火焰图确认 IK 字典匹配、token list 分配和 arbitrator 的占比；
2. 评估 parser/processor 对象在批量索引构建中的安全复用，避免每行重复构造；
3. 为 FTS 辅助表构建验证 encoded sort key 和基数排序是否真正命中；
4. 对局部分区构建、并行度和外排 spill 次数增加阶段级监控；
5. 设计 position list codec 时同时补充随机 round-trip、边界值和压缩率测试；
6. 使用更大数据集验证内存峰值、临时文件 I/O 和多轮归并行为。

优化结论必须以 profile、正确性回归和同配置重复基准为依据，不能仅凭代码结构推断性能收益。
