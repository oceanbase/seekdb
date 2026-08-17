# Proposal: GPU vector-index backend for seekdb via hipVS / NVIDIA cuVS (AMD RDNA3, gfx1100)

Status: **design + loose-coupling PoC** (not yet a source-level backend).
Target hardware verified: AMD Radeon PRO W7900 (`gfx1100` / RDNA3), ROCm 7.2.4, Ubuntu 24.04.

## Summary

seekdb's ANN vector index is currently CPU-only (HNSW via VSAG, IVF via the OB
native library). This proposal adds an optional **GPU backend** that offloads
index build + k-NN search to **cuVS** (via **hipVS**, cuVS compiled for AMD
`gfx1100`). It plugs in behind the existing adaptor seam without changing the
SQL / storage layers' contracts.

## Current architecture (where the GPU backend hooks in)

- Vector-index engine bridge: [`src/oblib/lib/vector/ob_vsag_adaptor.h`](../../src/oblib/lib/vector/ob_vsag_adaptor.h)
  / [`.cpp`](../../src/oblib/lib/vector/ob_vsag_adaptor.cpp), namespace `obvsag`.
- The C-style interface is a clean seam:
  - `create_index(index_handler, index_type, dtype, metric, ...)`
  - `build_index(index_handler, float* vectors, int64_t* ids, dim, size, ...)`
  - `add_index(...)`, `knn_search(index_handler, query, dim, topk, ...)`
  - `serialize(...)` / `deserialize_bin(...)`
- Index libs today: HNSW → `lib='vsag'`; IVF/IVF-PQ → `lib='ob'`
  (see `pyseekdb` `HNSWConfiguration(lib='vsag')` / `IVFConfiguration(lib='ob')`).

## Proposed change

1. Add a new index lib value, e.g. `lib='cuvs'` (for HNSW/CAGRA) and for IVF-PQ.
2. Implement a `cuVS` backend (`ob_cuvs_adaptor`) exposing the **same `obvsag`
   interface**, routing `build_index` / `add_index` / `knn_search` to hipVS's
   `libcuvs_c`.
3. Natural algorithm mapping:
   - graph index: seekdb **HNSW ↔ cuVS CAGRA**
   - inverted index: seekdb **IVF-PQ ↔ cuVS IVF-PQ** (and IVF-Flat)
   - metrics: L2 / inner-product / cosine (normalize for cosine).
4. Surface it through the existing vector-index DDL (index type / `WITH (lib=cuvs)`).

## Build / dependency (hipVS)

hipVS = cuVS built for `gfx1100` (ROCm 7.2.4). It ships `libcuvs_c.so` + headers.
A self-contained runtime image is available; link seekdb's cuVS backend against
`-lcuvs_c` with `-I<hipvs>/include -I<hipvs>/include/cuvs`. cuVS C API used by the
PoC: `cuvsResourcesCreate`, `cuvsCagraIndexParamsCreate`, `cuvsCagraBuild`,
`cuvsCagraSearch` (+ DLPack tensors), `cuvsRMMAlloc/Free`.

## PoC evidence (loose coupling, same data + ground truth)

`poc/cuvs_bridge.c` builds a CAGRA index on GPU over the *same* vectors seekdb
indexes and searches top-k; `poc/m1_seekdb_baseline.py` measures the seekdb CPU
baseline; `poc/l1_util.py` exports vectors and scores recall against a shared
brute-force ground truth.

Dataset: N=10,000, dim=128, Q=100, K=10, L2-normalized random-Gaussian.

| Engine | recall@10 | per-query | notes |
| --- | --- | --- | --- |
| seekdb CPU (HNSW/VSAG, ef_search=200) | 0.648 | ~0.41 ms | async Change-Stream index build |
| hipVS GPU (CAGRA, default params) | **0.879** | **0.006 ms** (batch-100, ~161k qps) | AMD gfx1100 |

Takeaway: cuVS on the same data returns correct neighbors with **higher recall
and GPU-scale throughput** — validating the backend approach before the
source-level integration.

## Operational note discovered during the PoC

seekdb builds the vector index **asynchronously** (Change Stream). Querying
immediately after a bulk write returns near-random results until the delta/
snapshot HNSW is built (~seconds for 10k rows). Benchmarks must wait for the
index to be ready.

## Roadmap for the upstream PR

- [ ] `ob_cuvs_adaptor.{h,cpp}` implementing the `obvsag` interface over `libcuvs_c`.
- [ ] Config plumbing: `lib='cuvs'` for HNSW/CAGRA and IVF-PQ; device selection.
- [ ] DDL exposure + planner routing; serialize/deserialize of the GPU index.
- [ ] Recall/correctness regression vs the CPU backend; build-flag gating (ROCm).

---
Reproduced on AMD Radeon PRO W7900 (gfx1100) + hipVS (cuVS for gfx1100), ROCm 7.2.4.
