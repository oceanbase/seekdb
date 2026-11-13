# 使用 hipVS 加速 seekdb 的 HNSW 索引构建

> 赛题：[800509] 使用 hipVS 加速 HNSW 索引构建（AMD Radeon / ROCm）
> 实现位置：`tools/hipvs_bridge/`
> 本文记录设计取舍、可复现的验证方法，以及当前实际测得的结论。

## 1. 问题

seekdb 的 HNSW 索引由 VSAG 在 CPU 上自底向上逐点插入构建：每个新点都要在每一层做
一次贪心搜索再连边。这条路径的问题不是"算法慢"，而是内存访问模式差：

- 每插入一个点，需要随机访问图中若干条邻居的向量，cache miss 率高；
- 多线程插入时要在层内加锁，线程数上去以后锁竞争抵消掉并行收益；
- 构建阶段基本是"逐点 + 随机跳转"，而 GPU 最擅长的恰好是"整批 + 规整访问"。

因此本题的加速点不在查询（CPU HNSW 单查已经是亚毫秒级，GPU 的固定开销反而更大），
而在**构建**：把"逐点插入"换成"一次性批量构图"。

## 2. 方案

### 2.1 总体结构

```
 CREATE ... VECTOR INDEX i(c2) WITH (distance=l2, type=hnsw, lib=cuvs)
                    │
                    ▼
          obvsag 向量抽象层（按索引选择后端）
                    │
        ┌───────────┴────────────┐
        ▼                        ▼
  lib=vsag（默认）          lib=cuvs（opt-in）
  VSAG CPU HNSW             libseekdb_cuvs_bridge.so
  逐点插入                        │
                                  ▼
                           hipVS CAGRA 构图（GPU）
                                  │
                                  ▼
                    hnsw::from_cagra 投影为 HNSW 层级
                                  │
                    ┌─────────────┴─────────────┐
                    ▼                           ▼
            直接 GPU 检索（批量）        导出 hnswlib 兼容文件
                                          → CPU 侧继续提供服务
```

关键点：**GPU 只负责"构图"这个重活**。图建好之后既可以继续在 GPU 上批量检索，也可以
投影成 HNSW 落盘、回到 CPU 提供单点查询——这正是 seekdb 绝大多数读场景需要的东西。

### 2.2 为什么是 CAGRA，而不是"在 GPU 上实现 HNSW 插入"

CAGRA 是 hipVS 里为 GPU 设计的图索引：先用 NN-Descent / IVF-PQ 并行建一张稠密 kNN 图，
再剪枝成固定出度的图。它的构建复杂度对 GPU 友好（整批相似度计算 + 图剪枝都是规整并行），
而 HNSW 的"逐层贪心插入"本质上串行，强行搬到 GPU 只会得到一堆 kernel 启动开销。

hipVS 提供了 `cuvs::neighbors::hnsw::from_cagra()`：把 CAGRA 图投影成 HNSW 层级结构。
当 `hierarchy = CPU` 时，输出是 **hnswlib 兼容**的索引文件，可以被 CPU 侧的 HNSW 实现
直接打开。这条路径同时拿到了"GPU 建图快"和"HNSW 结构通用"两个好处。

### 2.3 逐点插入 vs 批量构图：收益从哪来

| 阶段 | CPU 逐点插入 | GPU 批量构图 |
|---|---|---|
| 距离计算 | 每次插入只算几十~几百个候选 | 整批矩阵乘式打分，Tensor/Matrix Core 打满 |
| 内存访问 | 随机跳转，cache miss 主导 | 连续分块，带宽主导 |
| 同步 | 层内锁 + 线程调度 | 无锁，kernel 内完成 |
| 固定开销 | 几乎为 0 | 每次构图一次（H2D + kernel 启动） |

这解释了为什么"点越多、维度越高，加速比越大"，也解释了为什么**小数据集上 GPU 会更慢**——
固定开销摊不掉。所以桥里保留了一个最小规模阈值（见 2.5）。

### 2.4 为什么用独立 .so + 纯 C ABI

| 备选 | 问题 |
|---|---|
| 直接把 hipVS 链进 seekdb | 全树引入 HIP/ROCm 头文件与链接依赖，无 GPU 机器无法编译 |
| `#ifdef` 编进主二进制 | 一份二进制只能对应一种 GPU 形态，分发和回归都变复杂 |
| **独立 .so + C ABI + dlopen**（采用） | 主工程零 ROCm 依赖；缺库/缺卡时回落；ABI 用纯 C，跨编译器稳定 |

`seekdb_hipvs_bridge.h` 里没有任何 HIP 类型：只传 `float*`、`int64_t`、错误码和
调用方提供的错误缓冲区。所有函数不抛异常，失败一律通过返回码表达。

### 2.5 回落策略（安全优先）

满足任一条即回落 VSAG/CPU，并且**不报 SQL 错误**，只在日志与遥测里留痕：

1. `libseekdb_cuvs_bridge.so` 不存在 / `dlopen` 失败 / `seekdb_hipvs_available()` 非 0；
2. 距离算法不是 L2（CAGRA 的 InnerProduct 仅部分构图算法支持，先不冒险）；
3. 向量行数低于阈值（默认 500）：此时 GPU 固定开销大于收益；
4. 环境变量 `SEEKDB_DISABLE_HIPVS=1`（排障开关）；
5. `seekdb_hipvs_build()` 返回任何非 0 错误码（含显存不足）。

回落后索引仍然是一个完整可用的 CPU HNSW，**不会出现"建了一半"的索引**。

### 2.6 参数映射

| DDL | bridge | hipVS |
|---|---|---|
| `m=16` | `graph_degree` | `cagra::index_params::graph_degree` |
| — | `intermediate_graph_degree` | `max(graph_degree*2, 64)` |
| `ef_construction=200` | `ef_construction` | CAGRA 候选表 / HNSW 层级构建 |
| `ef_search=64` | `ef_search` | `cagra::search_params::itopk_size` |
| `distance=l2` | `SEEKDB_HIPVS_METRIC_L2` | `DistanceType::L2Expanded` |

`guarantee_connectivity = true` 是刻意打开的：数据库索引一旦图不连通，召回会静默下降，
SQL 层无从察觉，这个代价不能省。

## 3. 实现清单

| 文件 | 作用 |
|---|---|
| `tools/hipvs_bridge/seekdb_hipvs_bridge.h` | 纯 C ABI，seekdb 侧唯一需要包含的头 |
| `tools/hipvs_bridge/seekdb_hipvs_bridge.cpp` | hipVS 实现；未编入 hipVS 时编译为 stub |
| `tools/hipvs_bridge/CMakeLists.txt` | 独立构建，产物 `libseekdb_cuvs_bridge.so` |
| `tools/hipvs_bridge/bench/selfcheck.cpp` | ABI 自检 + 构建耗时 + recall 冒烟 |
| `tools/hipvs_bridge/bench/bench_build.py` | CPU vs hipVS 构建基准，输出 CSV/Markdown |
| `tools/hipvs_bridge/README.md` | 构建、ABI、参数映射 |
| `src/oblib/lib/vector/ob_hipvs_bridge_loader.h` | 服务端侧的 `dlopen`/`dlsym` 加载器与能力探测 |

服务端不直接链接 hipVS：`ob_hipvs_bridge_loader.h` 在首次遇到 `lib=cuvs` 的索引时把
`libseekdb_cuvs_bridge.so` 打开，缓存整张函数表，并把设备探测结果记住（`usable()`）。
探测失败只意味着"继续走 CPU"，不会重试也不会拖慢其他索引。

## 4. 构建与运行

```bash
# 1) 桥接库
cmake -S tools/hipvs_bridge -B build/hipvs_bridge -DCMAKE_PREFIX_PATH="/opt/rocm;/opt/hipvs"
cmake --build build/hipvs_bridge -j

# 2) 冒烟：设备探测 + 建图 + 召回 + 导出
./build/hipvs_bridge/seekdb_hipvs_selfcheck 50000 128 10

# 3) 基准：多规模、多次取中位数
python3 tools/hipvs_bridge/bench/bench_build.py \
        --n 10000 50000 100000 --dim 128 --k 10 --repeat 3 \
        --csv results/build_speedup.csv
```

无 GPU 的机器上用 `-DSEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS=OFF` 构建 stub，可验证 ABI 与
回落路径；`selfcheck` 会以返回码 2 表示"跳过"。

## 5. 验证方法

### 5.1 正确性

- `recall@10`：以精确暴力检索为 ground truth，在同一份数据上比对 GPU 索引的 top-10；
- 回落验证：设 `SEEKDB_DISABLE_HIPVS=1` 建同一张表，两次查询结果应完全一致（都走 CPU）；
- 导出验证：`seekdb_hipvs_export_hnsw()` 产出的文件用 hnswlib 打开并查询，top-k 与 GPU
  侧结果一致。

### 5.2 性能

- 每组配置重复 3 次取中位数，冷/热各记一次（第一次含 H2D 与 kernel 首次加载）；
- 同时记录 `kvec/s`，避免只看绝对耗时被机器差异带偏；
- 规模梯度 10K / 50K / 100K × 128D，另加 50K × 256D 观察维度影响。

### 5.3 当前结论

> 本仓库**不内置任何跑分数字**。GPU 数据必须在目标机器（Radeon Cloud `seekdb-hipvs`
> 模板，gfx1100 / ROCm 7.x）上现场跑出来再填，命令见上。这样做的原因是：不同 ROCm
> 版本、不同显存带宽下加速比差异很大，引用别处的数字没有意义。

已经可以确定的定性结论（与 AMD 官方 demo notebook 一致）：

1. **单条查询 GPU 没有优势**——CPU HNSW 已亚毫秒，GPU 每次调用有固定开销；
2. **批量检索 GPU 决定性胜出**——一次调用处理 N 条探针，固定开销被摊薄；
3. **构建阶段随规模增长优势扩大**——这也是本题的目标场景。

## 6. 兼容性与风险

| 项目 | 结论 |
|---|---|
| 默认构建 | 完全不变：`lib=cuvs` 不出现时不触碰任何 GPU 代码 |
| 已有 `lib=vsag` 索引 | 不受影响，可读写 |
| 存储格式 | GPU 建图产物额外导出，不改变现有索引文件格式 |
| 无 ROCm 机器 | 构建 stub 即可；运行时回落 CPU |
| 风险：hipVS 版本漂移 | API 以 `cagra::build` / `hnsw::from_cagra` 为主干，版本差异集中在 `index_params` 字段，桥内已做参数收敛 |
| 风险：显存不足 | `build` 返回 `SEEKDB_HIPVS_ERR_OUT_OF_MEMORY`，回落 CPU，不影响 DDL 成功 |

## 7. 后续工作

1. 把 `lib=cuvs` 接入 DDL 解析层（`ob_vector_index_util.cpp` 的 `LIB` 分支），并把选择
   持久化进索引参数串；
2. 在 `obvectorutil::create_index/build_index` 增加 dispatch：`lib=cuvs` 且通过阈值检查时
   走桥，否则原路径；
3. 把构建耗时、回落原因写入 `telemetry.json`，便于赛后统计 GPU 实际生效比例；
4. `dbms_vector.batch_knn` 批量算子（一次把 N 条探针喂给 GPU），这是查询侧唯一有意义的
   加速入口。
