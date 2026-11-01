# FTS 全文索引调优交接文档

分支：`feat/fts-tuning`（基于 `2026-competition` @ `5bce9e0`）
仓库：`Funnyvalentine00/seekdb1`
对应 issue：`#6`

## 1. 题目与验收标准

在 `src/storage/fts/**` 等允许范围内优化全文索引的构建/分词/查询性能，同时保持
`tokenize(text,'ik'|'beng')` 与 `MATCH(...) AGAINST(...)` 语义不变。

评分口径（`tools/benchmark/fts_large_bench_score.py`）：

- 9 个指标分三类，类内先平均，再按 3 类等权平均：
  - build：`build_ik_all_sec`、`build_ik_content_sec`、`build_beng_en_sec`
  - tokenize：`tokenize_ik_avg_ms`、`tokenize_beng_avg_ms`
  - query：`query_cn_avg_ms`、`query_beng_avg_ms`、`query_mixed_avg_ms`、`query_limit_avg_ms`
- `score = clamp(mean_category_improvement, 0, 0.5) / 0.5 * 100`，基线固定为 CI 五轮平均值。

计数契约（必须与基线一致）：`query_cn_hits=8001`、`query_beng_hits=11000`、
`query_mixed_hits=7332`、`query_limit_hits=20`。

验收命令：

```bash
# 正确性
cd tools/deploy && ./obd.sh mysqltest -n <cluster> --suite fts_index
# 性能（改动前后各一次）
LABEL=after ROWS=20000 BATCH=500 ROUNDS=3000 QUERY_ROUNDS=200 SAMPLES=3 WARMUP=30 \
  SKIP_LOAD=0 OUTPUT=/tmp/fts-after.txt bash tools/benchmark/fts_large_bench.sh
python3 tools/benchmark/fts_large_bench_score.py /tmp/fts-after.txt
```

本次结果：官方 `fts_index` 15 例中 **14 例通过**，唯一失败 `partitioned_simple_query`
已用优化前二进制复现为 pre-existing（见 5.1）；性能上分词指标稳定提升约 9%，
建索引/查询指标在噪声范围内（见 5.2）。

## 2. 接口契约

本次改动**不涉及任何外部接口**（SQL 语法、函数签名、parser 名称与属性、错误码、
`SHOW CREATE TABLE` 输出均不变），只改 `ObAddWord::groupby_word` 的内部哈希更新方式。

| 场景 | 优化前 | 优化后 |
| --- | --- | --- |
| 首次出现（groupby） | `get_refactored` 返回 `OB_HASH_NOT_EXIST`，再 `set_refactored(word, 1)` | `set_or_update(word, 1, updater)` 直接插入 1 |
| 重复出现（groupby） | `get_refactored` 读出旧值 v，再 `set_refactored(word, v + word_freq, overwrite)` | 同一桶锁内回调 `entry.second += word_freq`，等价于 v + word_freq |
| 非 groupby | `set_refactored(word, 1)` | 不变 |
| 哈希键 | 首次写入的拼写保留，后续同词不覆盖 key | 不变（`set_or_update` 仅在值命中时回调，不替换 key） |
| 并发 | 读-改-写跨两次加锁，可能被其他线程覆盖 | 单次桶锁内完成，读改写原子 |

不变式：词频累计值、去重后的词表内容、最终索引内容与查询结果均与优化前一致；
`verify.log` 与 `verify-baseline.log` 在重复词负载上逐字节相同。

## 3. 改动文件清单

源码（相对 `5bce9e0`，仅 1 个文件）：

- `src/storage/fts/ob_fts_stop_word.cpp`（+21 / -8）
  - 新增匿名命名空间回调 `ObAddWordCountUpdater`
  - `ObAddWord::groupby_word` 由 `get_refactored` + `set_refactored` 改为 `set_or_update`

交付物：

- `handoff/fts-tuning/README.md`（本文）
- `handoff/fts-tuning/changes.patch`
- `handoff/fts-tuning/benchmark-before.json`、`benchmark-after.json`（3 对运行的指标与中位数）
- `handoff/fts-tuning/ab-summary.json`（逐指标 before/after 中位数与变化率、hits 明细）
- `handoff/fts-tuning/logs/{build,verify,verify-baseline,verify-diff,test,notes,score}.log` 与 `logs/verify.sql`

未改动 `tools/benchmark/**`、基线 json、计时逻辑、测试 SQL、数据量与金样。

## 4. 构建与运行命令

```bash
# 构建
cd /opt/codex-seekdb-20261030/build_release && make -j8

# 启动独立实例（2881，评测与回归专用）
cd /opt/seekdb-fts-20261101
nohup ./bin/seekdb --port 2881 --data-dir ./store --base-dir . \
  --parameter memory_budget=4G --parameter datafile_size=4G --parameter datafile_maxsize=8G \
  --parameter log_disk_size=2G --parameter cpu_count=4 > ./log/console.log 2>&1 &

# 测试账号与依赖
obclient -h127.0.0.1 -P2881 -uroot -e "create user if not exists 'admin' identified by 'admin';"
obclient -h127.0.0.1 -P2881 -uroot -e "grant all on *.* to 'admin' with grant option;"
obclient -h127.0.0.1 -P2881 -uroot -e "set global recyclebin=ON;"

# 正确性回归
cd tools/deploy
for t in mysql_test/test_suite/fts_index/t/*.test; do ... mysqltest --test-file=$drv ...; done

# 性能
LABEL=before ROWS=20000 BATCH=500 ROUNDS=3000 QUERY_ROUNDS=200 SAMPLES=3 WARMUP=30 \
  SKIP_LOAD=0 OUTPUT=/tmp/fts-ab/before-1.txt bash tools/benchmark/fts_large_bench.sh
python3 tools/benchmark/fts_large_bench_score.py /tmp/fts-ab/before-1.txt
```

每次换二进制都要先删 `store` 与 `etc/seekdb.data_version.bin` 再启动，避免版本校验失败。

## 5. 官方用例与性能评测

### 5.1 正确性

`mysqltest --suite fts_index`：15 例，通过 14 例。

| 用例 | 结果 |
| --- | --- |
| basic_dml / basic_dml_sequel / cn_word / create_fts_index_afterward | PASS |
| create_table_with_fts_index / domain_id_merge_with_compare_null / drop_index | PASS |
| index_view / load_data / offline_ddl_with_fts_index / online_ddl_on_table_with_fts_index | PASS |
| parser_properties / simple_query / tokenize_function | PASS |
| partitioned_simple_query | FAIL（pre-existing） |

`partitioned_simple_query` 的差异仅为 EXPLAIN 中下推参数编号：金样 `nl_params_([t_dim_table.seq(:0)])`，
本机输出 `(:1)`，其余文本一致。用优化前二进制（`final-before`，sha256 `a918d84e…`）复跑得到
同一处差异、同一失败点，因此判定为基线与金样之间的既有差异，与本次改动无关
（见 `logs/test.log` 第 [2] 段）。

重复词语义等价校验（`logs/verify.log` vs `logs/verify-baseline.log`）：24 行英文 +
12 行中文重复词数据集，比对 `MATCH` 分数（保留 6 位小数）、命中数与 `tokenize` 输出，
`diff_exit=0`，逐字节一致（见 `logs/verify-diff.log`）。

### 5.2 性能评测

参数与 CI 基线完全一致：`ROWS=20000 BATCH=500 ROUNDS=3000 QUERY_ROUNDS=200 SAMPLES=3 WARMUP=30 SKIP_LOAD=0`。
before/after 交替执行 3 对，每对独立重启实例并重建数据；下表为 3 次运行的中位数。

| 指标 | before | after | 变化（正 = 更快） |
| --- | --- | --- | --- |
| `build_ik_all_sec` | 5.555 s | 5.563 s | -0.14% |
| `build_ik_content_sec` | 4.132 s | 4.032 s | +2.42% |
| `build_beng_en_sec` | 2.326 s | 2.325 s | +0.04% |
| `tokenize_ik_avg_ms` | 0.2671 ms | 0.2422 ms | **+9.32%** |
| `tokenize_beng_avg_ms` | 0.2134 ms | 0.1946 ms | **+8.81%** |
| `query_cn_avg_ms` | 1.2112 ms | 1.2114 ms | -0.02% |
| `query_beng_avg_ms` | 1.7351 ms | 1.6951 ms | +2.31% |
| `query_mixed_avg_ms` | 1.3845 ms | 1.3746 ms | +0.72% |
| `query_limit_avg_ms` | 1.4999 ms | 1.5102 ms | -0.69% |
| `select1_avg_ms`（控制项） | 0.1846 ms | 0.1847 ms | -0.05% |

分类平均：build +0.77%、tokenize **+9.07%**、query +0.58%，三类等权 +3.47%。
控制项 `select1_avg_ms` 持平，说明主机无系统性漂移；hits 在 6 次运行中全部为
8001 / 11000 / 7332 / 20。

官方分数（`logs/score.log`）：before 三次 `mean_improvement` 为 77.82% / 78.24% / 78.27%，
after 为 78.77% / 78.99% / 79.65%；两者 score 均为 **100.00**。
原因是官方基线取自 CI（build_ik_all 35.28s 量级），本机绝对值远低基线，
超出 `full_score_improvement=0.5` 后即被 clamp 到满分，官方分数无法区分优化前后，
故以上表同机成对差值为有效性能证据。

## 6. 日志位置

- `logs/build.log`：`make -j8` 完整输出、退出码 0、重编后二进制 sha256
- `logs/verify.log`：优化后二进制在重复词负载上的查询输出
- `logs/verify-baseline.log`：同一负载在优化前二进制上的查询输出
- `logs/verify-diff.log`：两份输出的 `diff -u`（`diff_exit=0`）
- `logs/verify.sql`：等价性校验使用的 SQL
- `logs/test.log`：官方 15 例套件结果、pre-existing 失败的双二进制复现、等价性结论
- `logs/notes.log`：环境、二进制 sha256、命令、测量口径与已知风险
- `logs/score.log`：对 6 份报告逐一运行官方计分脚本的输出
- `benchmark-before.json` / `benchmark-after.json`：每指标的 3 次原始值与中位数
- `ab-summary.json`：逐指标变化率与 hits 明细
- `changes.patch`：本分支相对 `5bce9e0` 的源码补丁（仅 `src/storage/fts/ob_fts_stop_word.cpp`）

## 7. 已知风险

- `partitioned_simple_query` 为既有失败（基线二进制同样失败），并非本次引入；
  若队长要求全绿，需要另行修正金样或参数编号逻辑（不在本 issue 可改范围内）。
- 优化仅覆盖 `ObAddWord::groupby_word` 的哈希更新路径，收益集中在分词指标；
  建索引与查询指标处于本机噪声范围，不宜宣称提升。
- 单次改动规模小（1 文件 +21/-8），未改 issue 中列出的其他候选点
  （Beng 每 token 分配、`tokenize_fulltext` 每次新建 arena、字典缓存、每行 arena 等）。
- 性能数据来自单机 3 对交替测量（CPU 4 核、数据 20000 行），绝对值不可跨机比较；
  结论只基于同一实例上成对中位数。
- 官方计分在本机会饱和到 100（见 5.2），评审若需分数梯度，应使用同口径的 before/after 对比。
- 优化把读-改-写合并为单次桶锁内更新，语义等价，但同时消除了原先两段式更新可能出现的
  并发覆盖；并发场景下的最终值以原子更新结果为准（更严格但仍满足词频累加语义）。

## 8. 回滚方式

```bash
git checkout feat/fts-tuning
git revert <本次提交>          # 或 git reset --hard 5bce9e0
cd build_release && make -j8
rm -rf /opt/seekdb-fts-20261101/store /opt/seekdb-fts-20261101/etc/seekdb.data_version.bin
# 重新启动实例后回归 fts_index
```

回滚只影响 `ObAddWord::groupby_word` 的内部实现，无接口或数据格式变化，无需重建索引格式。
