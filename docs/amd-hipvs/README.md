# Experimental hipVS snapshot builder

Status on 2026-11-01: implementation prepared for external evaluation. Compilation,
GPU execution, VSAG import, restart and correctness/performance tests have **not**
been run, at the user's request. The available server has NVIDIA A100 GPUs and
does not have AMD/ROCm. This is not a measured speedup claim.

Use `LIB=HIPVS` with plain FP32 HNSW, L2 distance and no stored extra columns.
Default `LIB=VSAG`, incremental writes and queries continue through VSAG. A complete
snapshot is buffered across scan batches; the existing end-of-scan DDL and async
snapshot hooks finalize one graph. No per-batch GPU graphs are merged. An empty
snapshot creates an empty CPU index and does not claim GPU work. Nonempty snapshots
require `N > 2*M`; missing GPU/backend and unsupported profiles fail explicitly.

The separately built shared library runs CAGRA on AMD ROCm, synchronizes the GPU,
converts with `hierarchy=CPU`, and exports original hnswlib data. Conversion checks
header offsets, degrees, internal neighbors, source-label uniqueness and bitwise
label/vector correspondence, then replaces source row labels with seekdb VIDs.
It imports into a fresh VSAG handle and publishes only after import/count checks.
Subsequent serialization uses the existing VSAG writer. Upper hierarchy construction
and conversion are CPU work; they are included in timings. No CPU fallback is
reported as GPU acceleration.

## Build and enable

Build seekdb normally. CPU-only hosts do not need ROCm headers or libraries.
On the GPU host, first install AMD hipVS with its CAGRA/HNSW wrapper enabled
(`BUILD_CAGRA_HNSWLIB=ON`), plus its matching hipRAFT/hipRMM dependencies. hipVS
retains the `cuvs` C++ namespace and exported `cuvs::cuvs` CMake target.

```sh
cmake -S src/oblib/lib/vector/hipvs_backend -B build-hipvs \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/opt/rocm/bin/hipcc \
  -DCMAKE_PREFIX_PATH=/path/to/installed/hipvs-prefix \
  -DCMAKE_INSTALL_PREFIX="$PWD/install-hipvs"
cmake --build build-hipvs -j
cmake --install build-hipvs
export SEEKDB_HIPVS_LIBRARY="$PWD/install-hipvs/lib/libseekdb_hipvs.so"
```

Set this variable **before starting the seekdb server process**, not just the SQL
client. Set the dynamic loader path for the installed hipVS/ROCm libraries as
required by that installation. The backend is loaded once and retained for process
lifetime. It runs on a dedicated OS thread and GPU builds are serialized per server
process. Restart the server after changing the backend path. Preserve server stderr
to collect `SEEKDB_HIPVS` JSON records with real device architecture and stage times.

```sql
CREATE TABLE points (id BIGINT PRIMARY KEY, v VECTOR(128));
-- Insert the dataset BEFORE creating the index to exercise snapshot construction.
CREATE VECTOR INDEX idx ON points(v)
  WITH (distance=l2,type=hnsw,lib=hipvs,m=16,ef_construction=200,ef_search=100);
SELECT /*+ INDEX(points idx) */ id FROM points
  ORDER BY l2_distance(v,'[...128 components...]') APPROXIMATE LIMIT 10;
```

## Performance and quality collection

The reusable collector in `unittest/vector_index/benchmark_hipvs.py` connects to an
existing disposable database. It inserts the same seeded FP32 vectors and shuffled
nonconsecutive SQL IDs into fresh tables before timing `CREATE VECTOR INDEX` through
the first forced-index query. Each CPU/GPU run therefore includes database scan,
transfers, synchronization, upper hierarchy, conversion, VSAG import and index DDL.
It alternates CPU/GPU order over at least three rounds and records median speedup,
query median/p95, NumPy exact-L2 Recall@K, query plans, and matching GPU log records.
It deletes only the tables it created. Incomplete runs produce a failed report;
absent GPU evidence cannot produce a HIPVS speedup.

```sh
python -m pip install numpy pymysql
# Optional: export SEEKDB_PASSWORD in the shell; do not add it to arguments/files.
python unittest/vector_index/benchmark_hipvs.py \
  --database hipvs_benchmark --gpu-log /path/to/server-stderr.log \
  --rows 10000 --dim 128 --queries 100 --rounds 3 --output hipvs-results.json
```

The supplied `REPORT.json` is an explicit unmeasured status record. Do not replace
nulls with estimates. Synthetic normal vectors are a smoke workload, not a substitute
for the competition dataset; collect that dataset separately with the same timing
boundary, query set and parameters. Compare query plans to ensure the ANN index was
used. Also evaluate insert/update/delete after import, serialization/restart,
empty and small snapshots, GPU OOM, absent DSO/GPU, malformed export, and CPU defaults.

## Practical limits

Full-snapshot buffering consumes about `N*(4*D+8)` bytes before CAGRA/VSAG workspaces.
GPU/library-owned allocations and conversion buffers are not integrated into seekdb
tenant memory accounting. Construction blocks its finishing task and has no GPU
cancellation/timeout mechanism. Builds are single-process serialized, not scheduled
across multiple server processes. This experimental backend targets 64-bit
little-endian Linux and the original plain hnswlib layout. Imported compatibility
with the contest's packaged VSAG remains unverified; other layouts fail validation.
No support for SQ/BQ, cosine/IP, sparse vectors, text embedding indexes or stored
extra columns is claimed.

References: [hipVS HNSW C++ API](https://rocm.docs.amd.com/projects/hipVS/en/latest/reference/cpp_api/neighbors_hnsw.html),
[official hipVS source](https://github.com/ROCm/hipVS),
[pinned VSAG legacy HNSW reader](https://github.com/antgroup/vsag/blob/129b82c/src/algorithm/hnswlib/hnswalg.cpp).
