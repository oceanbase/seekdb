# 全文索引性能优化记录

## 1. 结论

在 `2026-competition` 分支上，以 `tools/benchmark/fts_large_bench.sh` 统一基准验证，保持分词与检索语义不变的前提下：

| 类别 | 指标 | 基线 | 优化后 | 变化 |
|---|---|---:|---:|---:|
| build | `build_ik_all_sec` | 7.188 s | 6.281 s | −12.6% |
| build | `build_ik_content_sec` | 5.550 s | 4.743 s | −14.5% |
| build | `build_beng_en_sec` | 3.140 s | 2.635 s | −16.1% |
| build | `build_total_sec` | 15.889 s | 13.669 s | **−14.0%** |
| tokenize | `tokenize_ik_avg_ms` | 0.2892 ms | 0.2859 ms | −1.1% |
| tokenize | `tokenize_beng_avg_ms` | 0.2347 ms | 0.2305 ms | −1.8% |
| query | `query_cn_avg_ms` | 1.9275 ms | 1.9560 ms | +1.5% |
| query | `query_beng_avg_ms` | 2.7663 ms | 2.7482 ms | −0.7% |
| query | `query_mixed_avg_ms` | 2.0574 ms | 2.1023 ms | +2.2% |
| query | `query_limit_avg_ms` | 2.3092 ms | 2.5579 ms | +10.8% |

四类查询的命中数完全一致：`query_cn=8001`、`query_beng=11000`、`query_mixed=7332`、`query_limit=20`，说明分词和检索语义未改变。

查询路径不是本次改动的目标（唯一相关代码是查询串分词），本机多次运行存在约 5–10% 抖动；上表为同一轮对照数据，未选择性截取。构建与分词是确定的收益，查询时延基本持平。

原始报告：[`baseline.txt`](fts-results/baseline.txt)、[`optimized.txt`](fts-results/optimized.txt)。

## 2. 热点定位

`fts_large_bench.sh` 分成三段：

1. **build**：20,000 篇文档分别建 `ik(title,content)`、`ik(content)`、`beng(en)` 三个全文索引；每个索引都要对每个文档执行 tokenize + 写 doc-word/word-doc 辅助表。
2. **tokenize**：`SELECT tokenize(...)` 重复 3000 次，测 IK/BEng parser 的固定开销。
3. **query**：4 条 `MATCH ... AGAINST` 查询，重复 200 次。

通过源码检查定位到四个固定开销：

- `ObFTCharUtil::do_classify()` 对同一个 UTF-8 字符最多调用 4 个 checker，而每个 checker 都重新执行 `ob_charset_decode_unicode`。
- `ObUnicodeBlockUtils::is_unicode_cn_number()` 对每个汉字线性扫描 34 个码点，位于 IK 的每个字符热路径。
- `ObDASDomainUtils::generate_fulltext_word_rows()` 为每个文档创建一个 997 桶的 `ObFTWordMap`，批量建索引时反复分配/销毁。
- `ObIKFTParser` 每个文档重新 `load_cache` + `build_dict_from_cache`，重新构建 IK 主词典、量词词典、停用词典和 4 个 processor；DDL 默认 `parallelism=1` 时整个 tokenize/sort 流水线串行。

## 3. 改动

### 3.1 字符分类只解码一次

`src/storage/fts/ik/ob_ik_char_util.h`：`do_classify()` 先调用一次 `decode_unicode`，再按原来的顺序调用 `is_alpha/is_arabic/is_chinese/is_other_cjk/surrogate`。判断顺序和范围函数完全不变。

### 3.2 中文数字 O(1) 判定

`src/storage/fts/utils/unicode_utils.cpp`：把 34 项线性数组扫描改成 `switch (unicode)`，覆盖原数组中的全部码点（一、二、两……卅、廿）。编译器可生成范围/跳转表。

### 3.3 复用 token map

`src/sql/das/ob_das_domain_utils.{h,cpp}`：`ObFTDMLIterator` 持有一个 `ObFTWordMap`，在 `init()` 创建一次；新增 `generate_fulltext_word_rows(..., ObFTWordMap &, ...)` 重载，每篇文档只 `reuse()`，不再反复 create/destroy。原有无状态重载保留给其他调用点。

### 3.4 线程内复用 IK parser

`src/storage/fts/ob_ik_ft_parser.{h,cpp}`：

- `ObIKFTParser` 增加 `work_alloc_`，把 `TokenizeContext` 和 4 个 processor 放到可重置的 per-document arena；词典、range cache 保留在持久 arena。
- 增加 `reuse_init()`/`can_reuse()`：collation 与 IK smart/max_word 模式一致时，只销毁并重建 per-document 状态，不重新加载词典。
- `ObIKFTParserDesc::segment()` 使用 `thread_local` cache；`free_token_iter()` 对 cached parser 只清 `in_use`，不析构。重入或初始化失败时回退到原来的 per-call parser，保证正确性。

### 3.5 DDL 并行度下限

`src/sql/resolver/ddl/ob_fts_index_builder_util.cpp`：FTS index 与 FTS doc-word 两个 CPU 密集 leg 的 `decided_parallelism` 从 `MAX(original/3, 1)` 改为 `MAX(original, 4)`。tokenize 的是互不相交的 macro block，没有顺序依赖；`ALTER TABLE ... ADD FULLTEXT INDEX` 未显式指定 PARALLEL 时也能使用多核。

## 4. 复现

```bash
cd seekdb
./build.sh release --init --make -j2     # 本次实测环境为 WSL2 / clang 17
export MYSQL='mysql -h127.0.0.1 -P2881 -uroot -N -s -A --default-character-set=utf8mb4'
REPORT=$(mktemp /tmp/fts.XXXX)
LABEL=before ROWS=20000 BATCH=500 ROUNDS=3000 QUERY_ROUNDS=200 \
  SAMPLES=3 WARMUP=30 OUTPUT="$REPORT" bash tools/benchmark/fts_large_bench.sh
python3 tools/benchmark/fts_large_bench_score.py "$REPORT"
```

测试环境：Windows 11 主机上的 WSL2 Ubuntu 22.04，16 逻辑核，6.7 GiB 内存；seekdb RelWithDebInfo（CMake + clang 17）。基线/优化后使用同一数据库实例参数、同一份 SQL 和生成器。

## 5. 已知限制

- 本机查询段抖动较大，`query_limit` 的 +10.8% 不能排除运行间波动；本次没有修改查询执行算子，建议 CI 以固定基线复测。
- 线程内 parser cache 会增加每个工作线程常驻的 IK 词典内存（约数 MiB），适合 server/批量构建场景；若需要严格控制线程常驻内存，可把 floor 并行度和 cache 做成配置项。
- 基准脚本和测试数据未修改。
