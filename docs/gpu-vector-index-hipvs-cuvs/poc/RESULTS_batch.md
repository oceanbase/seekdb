# 批量向量检索方向 —— 评估 + 批量算子 PoC(2026-08-18)

C3 定论:GPU 只在**批量**赢(~230×),单查 SQL 无收益。本篇重定向到批量方向:
(1) 评估 seekdb 是否有"多探针一次喂给向量索引"的入口;(2) PoC 一个批量算子。

## 1. 评估:seekdb 有相似度 JOIN,但执行是 nested-loop(逐探针一次 knn_search)

### 1.1 执行模型(源码)
- DAS 向量扫描 `src/sql/das/iter/ob_das_hnsw_scan_iter.{h,cpp}` 持有**单个** `search_vec_`;
  查询向量由 `origin_vec->eval()` 求值(**可以是外层列** → 支持相关子查询/LATERAL)。
- OB 有 batch NLJ(`can_batch_rescan_` / `can_use_batch_nlj_`,`ob_join_order.cpp`),
  但**向量扫描不参与**(`T_PSEUDO_GROUP_ID` 只在 `get_main_rowkey` 里被跳过,不做查询批量)。

### 1.2 实测(observer cuVS ON,tb=100k 压缩快照,t10k=10k)
- 相似度 JOIN **可用**(LATERAL 相关 ANN):
  \`\`\`sql
  select p.pid, n.c1 from probes p, lateral (
    select c1 from tb order by l2_distance(c2, p.pv) approximate limit K) n;
  \`\`\`
  每探针返回 top-K,最近邻=自身(正确)。
- **但是 nested-loop**:trace 显示 5 探针 → **5 次** `knn_simple`(同一 handle,a=128 dim,b=K),
  100 探针 → **100 次**。→ **没有任何批量到达 obvsag/向量索引**。
- nested-loop 基准延迟:
  - `t10k` 100 探针 top-10(cuVS 逐查服务)= **695 ms**(6.955 ms/探针;每查一次 cuVS
    调用 = RMM 分配+PCIe+启动+32MB pthread,单查开销主导,比 CPU 还慢——印证 C3)。
  - `tb` 100k 100 探针(VSAG 快照,cuVS 因 B1 缺口不服务快照)= ~0.2 ms/探针(快的 CPU HNSW)。

**结论**:批量入口在 SQL 层存在(LATERAL join),但执行逐条,GPU 批量红利(~230×)吃不到。
要兑现,需要一个**把多探针聚成一次 cuVS 调用**的批量算子。

## 2. 批量算子 PoC(桥接层,真实数据 base.f32 10k + query.f32 100 + 真值 gt_100x10.i32)

`bridge/batch_op_poc.c`(gcc 链 `libseekdb_cuvs_bridge.so`,GPU7):
建 CAGRA(10000×128)一次,对比逐探针 vs 批量。

| 路径 | 总时 | 每探针 | 吞吐 | recall@10 |
|---|---|---|---|---|
| 逐探针(100×nq=1) | 42.18 ms | 0.422 ms | 2,371 探针/s | **0.8690** |
| **批量(nq=100 一次调用)** | **0.787 ms** | **0.0079 ms** | **127,111 探针/s** | **0.8690** |
| **加速比** | | | **53.6×** | 召回**完全一致** |

**批量不改变结果,只改吞吐**(recall 两路完全相同 0.869)。

### 2.1 批量规模扫描(`bridge/batch_sweep.c`,10k CAGRA,tiled 真实查询)

| nq | 总时(ms) | 每探针(ms) | 探针/s | 相对单查 |
|---|---|---|---|---|
| 1    | 0.414 | 0.41428 | 2,414   | 1× |
| 10   | 0.414 | 0.04142 | 24,145  | 10× |
| 50   | 0.563 | 0.01126 | 88,840  | 37× |
| 100  | 0.646 | 0.00646 | 154,706 | 64× |
| 500  | 1.223 | 0.00245 | 408,960 | 169× |
| 1000 | 2.228 | 0.00223 | 448,868 | 186× |
| 2000 | 3.521 | 0.00176 | 568,072 | 235× |
| 5000 | 7.875 | 0.00157 | 634,930 | **263×** |

**关键洞察**:nq=10 和 nq=1 **总时相同**(0.414ms)——单查的每调用固定开销(RMM 分配+PCIe+
kernel 启动)主导;批量把它摊到多探针上,**额外探针近乎免费**,直到 GPU 算力在 nq≈500+ 饱和
(~635k 探针/s)。这就是批量红利的来源,也是单查永远追不上的原因。

## 3. seekdb 集成设计(把批量算子接进执行器)

批量探针可能的来源与集成方案(按可行性排序):

- **方案 A —— 向量扫描支持 batch rescan(让 LATERAL join 自动批量)**:OB 已有 batch NLJ;
  让 `ob_das_hnsw_scan_iter` 接收一组查询向量(来自 batch-rescan 分组),发一次 obvsag 批量调用,
  按 group_id 散射结果。需:(1) obvsag 批量 API `knn_search_batch`;(2) DAS 迭代器缓冲分组探针并调用;
  (3) 优化器对向量索引内表启用 batch rescan。**优点**:现有 LATERAL join 透明加速;**代价**:执行器改动深。
- **方案 B —— 新增批量 ANN 表函数(显式批量入口)**:`dbms_vector.batch_knn(index_tab, probe_tab, k)`
  返回 (probe_id, neighbor_id, dist)。内部读全部探针+复用快照索引,发一次批量调用。**优点**:干净、
  易路由到 cuVS 批量,契合"批量打分"场景;**代价**:新增 PL/表函数管线,需显式调用而非普通 join。
- **方案 C —— 桥接层批量服务(本 PoC 已证)**:`seekdb_cuvs_search(handle, q, nq, ...)` 已支持批量。
  **优点**:机制已验证;**代价**:未接入 SQL 规划器。

**建议 PoC 路径**:先在 **obvsag 适配层**加一个 `knn_search_batch` 入口(seekdb 向量抽象暴露批量→GPU
的确切缝),用 harness 在真实数据上验证;再按方案 A 或 B 接执行器。

## 4. 结论
- seekdb **有**相似度 JOIN(LATERAL 相关 ANN),但**逐条执行**,吃不到 GPU 批量。
- 批量算子 PoC:同一 CAGRA 索引,**nq=100 加速 53.6×、nq=5000 加速 263×,召回完全一致**。
- 批量是 GPU 在 seekdb 里唯一站得住的价值点;下一步在 obvsag 层落地批量缝 + 执行器接入。

## 5. seekdb 原生批量算子(obvsag 适配层 seam)—— 已落地并验证

在 `ob_vsag_adaptor.{cpp,h}` 新增 `obvsag::cuvs_knn_search_batch(key, queries, nq, topk, out_ids, out_dist)`:
复用 add_index 缓冲 + handle 注册表 + 32MB pthread,把 nq 条探针喂给**一次** `seekdb_cuvs_search`
(nq>1),再把 cuVS 行偏移映射回原始 vid。这是 seekdb 向量抽象暴露"批量→GPU"的确切缝,
未来批量算子(相似度 JOIN / 批量 ANN 表函数)即从此调用。增量重编 `BUILD_RC=0`(31.5s)。

harness `bench/batch_harness.cpp` 走**真实 obvsag 适配层**(create_index + add_index 10k + 查询):

| 路径 | 总时 | 每探针 | 吞吐 | recall@10 |
|---|---|---|---|---|
| 逐条 `knn_search`×100(cuVS-GPU) | 212.97 ms | 2.130 ms | 470 探针/s | 0.8730 |
| **批量 `cuvs_knn_search_batch`(nq=100)** | **1.826 ms** | **0.0183 ms** | **54,777 探针/s** | **0.8730** |
| **加速比** | | | **116.7×** | **逐条/批量 ids 完全一致 1000/1000** |

trace 佐证:逐条路径 101 次 `cuvs_serve`(每条一次 GPU 调用)vs 批量 **1 次 `cuvs_batch`**。
**批量返回与逐条完全相同的邻居(1000/1000 bit-identical),只是吞吐 116.7×。**
seekdb 层加速(116.7×)>桥接层(53.6×),因为批量还摊薄了每查 obvsag 开销(pthread 生成/后置过滤/分配)。

安全对照(cuVS OFF):逐条走 VSAG-CPU 0.217ms/探针(4598 探针/s,recall 0.926);批量 seam
`served=0` 安全回退(未开 GPU → add_index 不缓冲 → 注册表空)。完整对比:

| 路径 | 探针/s | recall | 结论 |
|---|---|---|---|
| VSAG-CPU 逐条 | 4,598 | 0.926 | 最快的单查 |
| cuVS-GPU 逐条 | 470 | 0.873 | 更慢(每调用开销,印证 C3) |
| **cuVS-GPU 批量** | **54,777** | 0.873 | **比最快 CPU 单查还快 12×,比 GPU 单查快 116×** |

**唯有批量,GPU 才决定性胜出(比最优 CPU 单查快 12×)。这就是 seekdb 里 GPU 的落地价值点。**

## 6. Option B 落地: dbms_vector.batch_knn (SQL 可调用批量算子) — 已实现并验证

把批量算子做成 **PL 系统包过程**, 真实 SQL 可调用:
`call dbms_vector.batch_knn(index_table, probe_table, topk, out_table)`。
内部: 读探针+索引向量(inner SQL, 向量按 LOB 原始 float 解码) -> 自建一次 CAGRA ->
**一次** GPU 批量检索(obvsag::cuvs_batch_knn, 32MB pthread) -> 邻居写入 out_table。

改动(mysql 模式无 PIPELINED/表函数, 故结果写输出表):
- obvsag: `cuvs_batch_knn(base,n,dim,query,nq,topk,out_ids,out_dist)` 一次性 build+batch-search+free。
- PL 5 处接线(照 rebuild_index 模板): 包 spec/body(PRAGMA INTERFACE)、ob_pl_interface_pragma.h、
  类头 DECLARE_FUNC、ob_dbms_vector_mysql.cpp 实现(inner SQL 读表 + 向量 LOB 解码 + 写 out_table)。
- *** 关键: 系统包 .sql 由 syspack_codegen 在 build 期嵌入 -> 需重编; 包在 bootstrap 期创建 -> 需全新 base-dir 引导。***

### 实测(observer cuVS ON, t10k=10000 索引, probes_q=100 探针)
- `call dbms_vector.batch_knn("t10k","probes_q",10,"bk_out")` -> **rc=0**, bk_out 得 1000 行(100x10)。
- **recall@10 = 0.8690**, 与桥接/harness 批量**完全一致**(0.869) -> SQL 路径正确。
- trace: **1 次 cuvs_raw_batch** -> 100 条探针一次 GPU 调用。
- 摊薄曲线(每次调用重建 CAGRA, build 为固定成本):

| nq | 总时 | 每探针 | 吞吐 |
|---|---|---|---|
| 100  | 332.5 ms | 3.325 ms | 301 探针/s |
| 1000 | 434.0 ms | 0.434 ms | 2,304 探针/s |
| 4000 | 661.7 ms | 0.165 ms | 6,045 探针/s |

固定成本(读 10k 索引向量 + 建 CAGRA)~324ms; **边际每探针 ~0.084ms**(批量检索+IO)。
nested-loop lateral join 基线(VSAG 快照, trace 实测): **0.187 ms/探针**。
=> batch_knn 越大批越省: build 摊薄后边际 0.084ms < VSAG 0.187ms < cuVS 单查 6.955ms。

### 结论
批量算子已成为**真实可调用的 SQL 能力**(`call` + `select`), 召回与桥接批量一致(0.869)。
GPU 批量检索本身 116x(harness, 预建索引; commit 28bddf14b)由此过程以 SQL 交付。
PoC 每次调用重建索引(自包含); 生产可缓存索引(如注册表 seam)以在任意批量规模拿满批量红利。
产物: poc/batch_knn_demo.sql, bench/bk_setup.sql; 复现需 cuVS ON + 全新 bootstrap(见 §6 注)。
