# PR: build seekdb HNSW indexes on an AMD GPU with hipVS (`lib=hipvs`)

## What

Adds an optional, opt-in AMD GPU backend for HNSW index construction:

```sql
CREATE VECTOR INDEX idx ON points(v)
  WITH (distance=l2, type=hnsw, lib=hipvs, m=16, ef_construction=200, ef_search=100);
```

`lib=vsag` (the default) keeps its current behaviour byte for byte.

## Why it is a build accelerator and not a "GPU search demo"

The GPU is used where the cost is: the k-NN graph. The pipeline inside the DSO is

```
cagra::build            (GPU: k-NN graph, degree 2*M, NN-descent)
  -> hnsw::from_cagra   (GPU base layer + HNSW hierarchy via hipVS' CPU routine)
  -> hnsw::serialize    (classic 64-bit hnswlib layout)
  -> label rewrite      (internal row -> seekdb VID, with full re-validation)
  -> fdeserialize       (import into the VSAG index seekdb would have built)
```

Queries, flushes, snapshot serialization and the incremental delta index are
untouched: the index that answers SQL is a normal VSAG HNSW index.

## Where it plugs in

| File | Change |
| --- | --- |
| `src/oblib/lib/vector/hipvs_build_api.h` | new: C ABI (`seekdb_hipvs_build_v1`, `..._probe_v1`, `..._abi_version_v1`) |
| `src/oblib/lib/vector/hipvs_backend/{CMakeLists.txt,hipvs_build.cpp,hipvs_snapshot_export.hpp}` | new: standalone hipcc/`find_package(cuvs)` project producing `libseekdb_hipvs.so` |
| `src/oblib/lib/vector/hipvs_backend/tools/*` | new: acceptance tools (stock-hnswlib read-back, VSAG import probe) - never part of the server build |
| `src/oblib/lib/vector/ob_vsag_adaptor.{h,cpp}` | `obvsag::build_hipvs_index()`: `dlopen(SEEKDB_HIPVS_LIBRARY)`, run the build on a dedicated OS thread, import the snapshot into VSAG. Fails hard when the backend/GPU is missing |
| `src/query/api/query/vector/ob_vector_index_util.h` | `ObVectorIndexAlgorithmLib::VIAL_HIPVS` |
| `src/observer/vector_index/ob_vector_index_util.cpp` | parse `LIB=HIPVS`; restrict it to plain FP32/HNSW/L2 without extra info; keep it across `ALTER`/rebuild |
| `src/query/api/query/vector/ob_vector_index_adaptor.h` | `hipvs_built_` flag, `finish_hipvs_snapshot()` |
| `src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp` | buffer the snapshot for `lib=hipvs` in `add_snap_index()`, build it once at the end of the scan in `finish_hipvs_snapshot()`, called from both the DDL pipeline and the async snapshot task |
| `unittest/vector_index/benchmark_hipvs.py`, `recall_sweep.py` | new: 3-round CPU/GPU benchmark with Recall@K, GPU evidence extraction and recall sweep |
| `docs/amd-hipvs/{README.md,REPORT.md}` | new: design, build/run steps, measured results |

## Impact / safety

* **No ROCm in the server link line.** The observer is built by the existing
  toolchain; HIP/cuVS live only in `libseekdb_hipvs.so`, loaded on demand. A node
  without the library or without a GPU still starts and serves `lib=vsag`
  normally.
* **No silent CPU fallback.** If the backend cannot be loaded or no AMD GPU is
  visible, `CREATE VECTOR INDEX ... lib=hipvs` fails with `OB_NOT_SUPPORTED` and a
  warning that names the reason; with the variable unset the log shows
  `hipvs backend unavailable, refusing to fall back to a CPU build`.
  `SEEKDB_HIPVS_LIBRARY` must be set before the observer starts (the backend is
  resolved once per process and kept, because the HIP runtime keeps thread-local
  state).
* **Unsupported combinations are rejected at DDL time** rather than silently
  downgraded: quantized HNSW (`HNSW_SQ`/`HNSW_BQ`), `IVF*`, `COSINE`/`INNER_PRODUCT`
  distance, extra-info columns, and corpora with `N <= 2*M`.
* **Restart-safe.** A snapshot that comes back from the persisted VSAG stream is
  detected (`old_count > 0`) and is never rebuilt or replaced; `hipvs_built_` is
  reset by `free_memdata_resource()` so a retried snapshot does re-run.
* **One build at a time per process**, serialised on a mutex (the ROCm runtime is
  not documented as safe for concurrent builds in one process, and one build
  already fills the device).
* **Evidence is machine readable**: every GPU build appends one JSON record to
  `SEEKDB_HIPVS_TRACE_FILE` (or stderr) with `gpu_compute`, `cpu_fallback`,
  device identity (`arch=gfx1100`, name, HBM) and the per-phase timings.
* **Failures are not papered over**: the snapshot conversion re-validates degree
  bounds, label permutation, corpus bytes and the upper-layer extent, and only a
  fully validated file is published (temp + rename).

## Measured (3-round medians, same data/parameters, this machine)

| corpus | CPU `lib=vsag` | GPU `lib=hipvs` | speedup | CPU recall@10 | GPU recall@10 |
| --- | --- | --- | --- | --- | --- |
| clustered 100k x 768 | 35.261 s | 11.493 s | **3.07x** | 0.998 | 0.918 |
| clustered 20k x 768 | 6.278 s | 4.870 s | 1.29x | 1.000 | 0.960 |
| gaussian 20k x 768 | 14.408 s | 4.581 s | **3.15x** | 0.586 | 0.518 |
| gaussian 20k x 128 | 5.344 s | 3.651 s | 1.46x | 0.684 | 0.662 |

The headline row meets both targets at once (>=3x and both recalls >=0.90) and the
resulting index answers queries at the same speed as the CPU-built one
(0.49 ms vs 0.47 ms median), because it is the same VSAG HNSW read path.

Details, per-phase GPU timings, GPU evidence and the honest limitations are in
`docs/amd-hipvs/REPORT.md`; reproduction steps are in `docs/amd-hipvs/README.md`.

## How to test

```bash
# 1. build and install hipVS (with CAGRA HNSW wrapper), then:
cmake -S src/oblib/lib/vector/hipvs_backend -B build_hipvs -G Ninja \
      -DCMAKE_CXX_COMPILER=hipcc -DCMAKE_HIP_ARCHITECTURES=gfx1100 \
      -Dhip_DIR=/opt/rocm/lib/cmake/hip -DHIP_DIR=/opt/rocm/lib/cmake/hip \
      -DCMAKE_PREFIX_PATH="/opt/rocm;/opt/hipvs" -DCMAKE_BUILD_TYPE=Release
cmake --build build_hipvs -j && cmake --install build_hipvs

# 2. run the server with the backend available
export SEEKDB_HIPVS_LIBRARY=/opt/seekdb-hipvs/lib/libseekdb_hipvs.so
export SEEKDB_HIPVS_TRACE_FILE=/tmp/hipvs_build_records.log
./build.sh release --make && ./tools/deploy/obd.sh prepare -p /tmp/obtest \
  && ./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml

# 3. create a GPU index and compare with the CPU baseline
python3 unittest/vector_index/benchmark_hipvs.py --port <port> --database test \
    --gpu-log /tmp/hipvs_build_records.log --rows 20000 --dim 768 --rounds 3
```

Without the shared object the same script reports
`hipvs backend unavailable, refusing to fall back to a CPU build` for every GPU
round, which is the intended behaviour.

## Checklist

- [x] `lib=vsag` untouched
- [x] GPU evidence required for every reported GPU build
- [x] >= 3 rounds, medians
- [x] Recall@K for both libraries against exact ground truth
- [x] explicit error instead of CPU fallback
- [ ] PR opened from the fork (blocked: no GitHub credentials in this
      environment; patch is `seekdb-hipvs.patch` against `2026-competition`)