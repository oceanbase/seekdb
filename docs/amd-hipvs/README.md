# Accelerating seekdb HNSW index build with hipVS (AMD GPU)

This directory documents the AMD ROCm/hipVS backend for seekdb HNSW index
construction: what it does, where it plugs into seekdb, how to build it, and how
to reproduce the measurements.

## 1. What the change does

`CREATE VECTOR INDEX ... WITH (lib=hipvs)` builds the graph of an HNSW index on
an AMD GPU instead of on the CPU:

1. the DDL snapshot scan buffers the `(vector, vid)` pairs of the snapshot table
   (`ObPluginVectorIndexAdaptor::add_snap_index`) -- for `lib=hipvs` nothing is
   inserted into VSAG at this point;
2. when the snapshot scan finishes (`check_snap_hnswsq_index` ->
   `ObPluginVectorIndexAdaptor::finish_hipvs_snapshot`) the whole corpus is handed
   once to the GPU backend, which builds a CAGRA k-NN graph with hipVS on the
   device, converts it into a real HNSW graph (base layer from the GPU graph,
   hierarchy from hipVS' multi-threaded CPU routine) and exports it as a classic
   hnswlib stream whose labels are the seekdb VIDs;
3. seekdb imports that stream into the VSAG index it would otherwise have built
   itself (`fdeserialize`), so *queries are unchanged*: same VSAG HNSW read path,
   same snapshot serialization, same incremental delta index for later DML.

`lib=vsag` (the default) is untouched: same code path, same behaviour.

## 2. Where it plugs in

| File | Role |
| --- | --- |
| `src/oblib/lib/vector/hipvs_build_api.h` | C ABI between seekdb and the GPU backend |
| `src/oblib/lib/vector/ob_vsag_adaptor.{h,cpp}` | `obvsag::build_hipvs_index()`: `dlopen()` the backend, run the build on a real OS thread, import the snapshot into VSAG. Fails hard (`OB_NOT_SUPPORTED`) when the backend or the GPU is missing -- no silent CPU fallback |
| `src/query/api/query/vector/ob_vector_index_util.h` | new `ObVectorIndexAlgorithmLib::VIAL_HIPVS` |
| `src/observer/vector_index/ob_vector_index_util.cpp` | parses `LIB=HIPVS` and restricts it to plain FP32/HNSW/L2 without extra info |
| `src/query/api/query/vector/ob_vector_index_adaptor.h` | `hipvs_built_` flag + `finish_hipvs_snapshot()` |
| `src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp` | buffers the snapshot for `lib=hipvs` and builds it once at the end of the scan |
| `src/oblib/lib/vector/hipvs_backend/` | the GPU backend itself: standalone CMake project (hipcc + `find_package(cuvs)`) producing `libseekdb_hipvs.so` |
| `unittest/vector_index/benchmark_hipvs.py` | 3-round CPU/GPU benchmark with Recall@K and GPU evidence extraction |

ROCm is deliberately *not* linked into the server: the observer binary is built
by the normal seekdb toolchain and the HIP/cuVS stack lives only inside
`libseekdb_hipvs.so`, which is loaded on demand with
`SEEKDB_HIPVS_LIBRARY=/path/to/libseekdb_hipvs.so`. The variable must be visible
to the observer process, i.e. it has to be set *before* the server starts.

## 3. Build the GPU backend

Prerequisites: ROCm >= 7.2 (7.2.1 works), cmake >= 3.31, ninja, hipVS installed
with `-DBUILD_CAGRA_HNSWLIB=ON` (this provides `cuvs::cuvs` and
`hnsw::from_cagra`/`hnsw::serialize`), plus the CMake packages hipVS installs for
its own dependencies (`raft`, `hnswlib`, `cuco`, `rmm`, `rapids_logger`,
`hipccl`).

```bash
cmake -S src/oblib/lib/vector/hipvs_backend -B build_hipvs -G Ninja \
      -DCMAKE_CXX_COMPILER=hipcc \
      -DCMAKE_HIP_ARCHITECTURES=gfx1100 \
      -DCMAKE_BUILD_TYPE=Release \
      -Dhip_DIR=/opt/rocm-7.2.1/lib/cmake/hip \
      -DHIP_DIR=/opt/rocm-7.2.1/lib/cmake/hip \
      -DCMAKE_PREFIX_PATH="/opt/rocm-7.2.1;/opt/hipvs" \
      -DCMAKE_INSTALL_PREFIX=/opt/seekdb-hipvs
cmake --build build_hipvs -j
cmake --build build_hipvs --target install        # -> /opt/seekdb-hipvs/lib/libseekdb_hipvs.so
```

Optional self-checks (never part of the server build):

```bash
cmake --build build_hipvs --target seekdb_hipvs_selftest    # GPU build + stock hnswlib read-back
cmake -S ... -DVSAG_ROOT=<seekdb deps devel dir>            # enables vsag_import_probe
cmake --build build_hipvs --target vsag_import_probe        # VSAG imports the snapshot and answers queries
```

## 4. Run seekdb with the backend

```bash
export SEEKDB_HIPVS_LIBRARY=/opt/seekdb-hipvs/lib/libseekdb_hipvs.so
./tools/deploy/obd.sh prepare -p /tmp/obtest
./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml
```

Then, in SQL:

```sql
CREATE TABLE points (id BIGINT PRIMARY KEY, v VECTOR(128));
-- ... INSERT ...
CREATE VECTOR INDEX idx ON points(v)
  WITH (distance=l2, type=hnsw, lib=hipvs, m=16, ef_construction=200, ef_search=100);
SELECT id FROM points ORDER BY l2_distance(v, '[...]') APPROXIMATE LIMIT 10;
```

`lib=vsag` with the same data/parameters is the CPU baseline.

Failures are explicit by design:

* no `SEEKDB_HIPVS_LIBRARY` / no AMD GPU -> `CREATE VECTOR INDEX` fails with
  `OB_NOT_SUPPORTED` and a warning naming the reason; the index is never built on
  the CPU behind the user's back;
* quantized types (`HNSW_SQ`/`HNSW_BQ`), non-L2 metrics, extra-info columns and
  `lib=hipvs` on `IVF*` are rejected at DDL time.

## 5. Reproduce the measurements

```bash
python3 unittest/vector_index/benchmark_hipvs.py \
    --port <mysql_port> --database test \
    --gpu-log <observer stderr log> \
    --rows 20000 --dim 128 --rounds 5 --m 16 --ef-construction 200 --ef-search 100
```

The script times `CREATE VECTOR INDEX` (plus the first `APPROXIMATE` query, so a
build only counts when the index is usable), computes Recall@K against a NumPy
brute-force ground truth, requires a `SEEKDB_HIPVS_BUILD` record with
`gpu_compute=true` in the server log for every GPU round, and reports medians of
at least three rounds.

See `REPORT.md` for the measured numbers on the development machine.