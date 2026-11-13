# seekdb ↔ hipVS bridge

GPU 加速的 HNSW 索引构建桥接层，供 seekdb 在 AMD Radeon（ROCm / gfx11xx）上使用
[hipVS](https://github.com/ROCm-DS/hipVS) 完成向量图构建。

本目录是**独立构建**的：不参与 seekdb 根工程构建，seekdb 侧只依赖一个纯 C ABI
头文件（`seekdb_hipvs_bridge.h`）并在运行时 `dlopen("libseekdb_cuvs_bridge.so")`。
因此没有任何 ROCm 的机器照样能编译、启动 seekdb。

## 为什么需要这层桥

| 约束 | 处理方式 |
|---|---|
| seekdb 全树不能引入 HIP 头文件 | 只有本目录包含 `<cuvs/...>`、`<raft/...>` |
| 无 GPU 环境必须零影响 | 缺库 / 缺设备 / 调用失败都返回错误码，调用方回落到 VSAG(CPU) |
| 构建是大头，查询是长尾 | 桥暴露批量 `build` + 批量 `search`，查询走一次 GPU 调用摊薄固定开销 |
| GPU 建图结果要能被 CPU 侧复用 | `seekdb_hipvs_export_hnsw(hierarchy=CPU)` 输出 hnswlib 兼容文件 |

## 构建

```bash
# 有 ROCm + hipVS 的机器（Radeon Cloud 的 seekdb-hipvs 模板已预装）
cmake -S tools/hipvs_bridge -B build/hipvs_bridge \
      -DCMAKE_PREFIX_PATH="/opt/rocm;/opt/hipvs"
cmake --build build/hipvs_bridge -j

# 只有 CPU 的机器：构建 stub，用于确认 ABI 与调用方回落逻辑
cmake -S tools/hipvs_bridge -B build/hipvs_bridge_stub \
      -DSEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS=OFF
cmake --build build/hipvs_bridge_stub -j
```

产物固定命名为 `libseekdb_cuvs_bridge.so`（与 Radeon Cloud 模板中 seekdb 期望的名字一致）。

## 自检

```bash
./build/hipvs_bridge/seekdb_hipvs_selfcheck 50000 128 10
```

输出示例（**在目标机器上实测，不要引用别处的数字**）：

```
bridge : seekdb-hipvs-bridge 1.0.0 (hipVS)
device : AMD Radeon PRO W7900 (gfx1100)
build  : <秒> s  (<kvec/s> kvec/s, 50000 x 128, degree=32)
search : <ms> ms for 64 queries (<ms>/query)
recall@10 : <r>
export : seekdb_hipvs_selfcheck.hnsw
```

无 GPU 时返回码为 `2` 并打印 `SKIP`，CI 可直接当作「跳过」而非失败。

## 基准

```bash
python3 tools/hipvs_bridge/bench/bench_build.py \
        --n 10000 50000 100000 --dim 128 --k 10 --repeat 3 \
        --csv results/build_speedup.csv
```

脚本同时跑 CPU（hnswlib，`m`/`ef_construction` 与 seekdb DDL 同名参数）与 hipVS
CAGRA，并用精确暴力检索算 recall@k。指标全部现场测量、无内置数字；缺少某个后端时
该列显示 `n/a`。

## C ABI 摘要

完整定义见 `seekdb_hipvs_bridge.h`。

```c
int  seekdb_hipvs_available(int32_t device_id);
int  seekdb_hipvs_device_name(int32_t device_id, char *buf, size_t len);
int  seekdb_hipvs_build(const float *vectors, const int64_t *ids, int64_t count,
                        const seekdb_hipvs_build_params *params,
                        seekdb_hipvs_index **out, char *err, size_t errlen);
int  seekdb_hipvs_search(seekdb_hipvs_index *index, const float *queries,
                         int64_t n_queries, int32_t topk, int32_t ef_search,
                         int64_t *out_ids, float *out_dist, char *err, size_t errlen);
int  seekdb_hipvs_save(seekdb_hipvs_index *index, const char *path, char *err, size_t errlen);
seekdb_hipvs_index *seekdb_hipvs_load(const char *path, char *err, size_t errlen);
int  seekdb_hipvs_export_hnsw(seekdb_hipvs_index *index, const char *path,
                              int32_t hierarchy, int32_t ef_construction,
                              char *err, size_t errlen);
int64_t seekdb_hipvs_count(const seekdb_hipvs_index *index);
int64_t seekdb_hipvs_dim(const seekdb_hipvs_index *index);
void seekdb_hipvs_free(seekdb_hipvs_index *index);
```

错误码：`0` OK、`1` 不支持（未编入 hipVS / 算法指标不支持）、`2` 参数非法、
`3` 无设备、`4` 内存不足、`5` 运行时失败。所有函数不抛异常，错误信息写入调用方缓冲区。

## 数据结构与参数映射

| seekdb DDL | bridge 参数 | hipVS 对应 |
|---|---|---|
| `type=hnsw` | — | CAGRA 图 + `hnsw::from_cagra` 投影 |
| `m` | `graph_degree` | `cagra::index_params::graph_degree` |
| — | `intermediate_graph_degree` | `graph_degree * 2`（默认） |
| `ef_construction` | `ef_construction` | CAGRA 建图候选表 / HNSW 层级构建 |
| `ef_search` | `ef_search` | `cagra::search_params::itopk_size` |
| `distance=l2` | `SEEKDB_HIPVS_METRIC_L2` | `DistanceType::L2Expanded` |
| `distance=inner_product` | `SEEKDB_HIPVS_METRIC_INNER_PRODUCT` | `DistanceType::InnerProduct` |

CAGRA 内部用行号 `[0, count)` 寻址，桥里保存 `ids` 映射表并在 `search` 出口翻译回
调用方的 `int64` 主键，因此 seekdb 无需假设行号连续。
