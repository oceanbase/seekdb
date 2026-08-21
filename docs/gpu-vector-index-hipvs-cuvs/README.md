# hipVS/cuVS GPU vector backend

Status: **Phase 1 implemented and review-gated** on AMD `gfx1100` with ROCm
7.2.4. This directory documents the current integration and its reproducible
validation assets. Historical feasibility experiments remain available in Git
history rather than in the product-facing tree.

## Scope

The backend adds optional hipVS/cuVS acceleration behind seekdb's existing
vector adaptor. It does not replace VSAG globally.

- GPU support is compiled only with `OB_BUILD_CUVS=ON`; the default is `OFF`.
- A dense L2 HNSW/HGRAPH index opts in with `WITH (lib=cuvs)`.
- `lib=vsag` indexes keep their existing CPU behavior.
- Cosine and inner-product metrics are rejected for `lib=cuvs` at DDL time.
- Filters, deletes, stale GPU indexes, and unavailable GPUs fall back to VSAG.
- `dbms_vector.batch_knn` exposes one-call batched GPU ANN for explicit batch
  workloads.

The product integration lives in:

- [`ob_vsag_adaptor.cpp`](../../src/oblib/lib/vector/ob_vsag_adaptor.cpp):
  per-index marking, buffered vectors, lazy CAGRA build/search, freshness and
  safety fallback, trace points, and batch entry points.
- [`ob_plugin_vector_index_adaptor.cpp`](../../src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp):
  maps declarative `lib=cuvs` indexes to the adaptor handle.
- [`ob_vector_index_util.cpp`](../../src/observer/vector_index/ob_vector_index_util.cpp):
  validates the L2-only DDL contract.
- [`ob_dbms_vector_mysql.cpp`](../../src/pl/sys_package/ob_dbms_vector_mysql.cpp):
  implements `dbms_vector.batch_knn`.

## Bridge

seekdb links a small C ABI bridge instead of importing hipVS/cuVS C++ types into
the server. The maintained bridge source and build entry point are under
[`tools/hipvs_bridge`](../../tools/hipvs_bridge/):

```bash
tools/hipvs_bridge/build.sh /work/bridge/libseekdb_cuvs_bridge.so
```

The bridge exports:

```text
seekdb_cuvs_build
seekdb_cuvs_search
seekdb_cuvs_free
seekdb_cuvs_cagra_knn
```

Build seekdb with the resulting library:

```bash
./build.sh release \
  -DOB_BUILD_CUVS=ON \
  -DOB_BUILD_CUVS_TRACE=OFF \
  -DCUVS_BRIDGE_LIB=/work/bridge/libseekdb_cuvs_bridge.so \
  --make -j64
```

For a CPU-only build, leave the defaults or pass:

```bash
./build.sh release \
  -DOB_BUILD_CUVS=OFF \
  -DOB_BUILD_CUVS_TRACE=OFF \
  --make -j64
```

## SQL usage

Per-index GPU opt-in:

```sql
CREATE TABLE items(
  id BIGINT PRIMARY KEY,
  embedding VECTOR(128),
  VECTOR INDEX idx_embedding(embedding)
    WITH (distance=l2, type=hnsw, lib=cuvs)
);
```

Explicit batch ANN writes results to a caller-created output table:

```sql
CALL dbms_vector.batch_knn(
  "items",
  "query_vectors",
  10,
  "batch_results"
);
```

See [`examples/batch_knn.sql`](examples/batch_knn.sql) for the table contract.

## Validation

The self-contained GPU smoke runner generates deterministic vectors and ground
truth; it does not use private `/work/datasets` or `/work/bench` assets:

```bash
LD_LIBRARY_PATH=/work/bridge:/opt/hipvs/lib:/opt/rocm/lib \
  docs/gpu-vector-index-hipvs-cuvs/validation/run_gpu_smoke.sh \
    --observer /path/to/trace-build/seekdb \
    --bridge /work/bridge/libseekdb_cuvs_bridge.so \
    --base-dir /tmp/seekdb-cuvs-smoke \
    --port 2981 \
    --render /dev/dri/renderD135 \
    --evidence /tmp/seekdb-cuvs-evidence
```

The runner validates:

- cuVS L2 build/serve and same-process VSAG isolation;
- non-L2 DDL rejection;
- filter, delete, and freshness fallback;
- threshold-triggered CAGRA rebuild;
- one-call `batch_knn`, row cardinality, and recall;
- GPU device file descriptors and hidden-GPU fallback;
- bounded observer cleanup and closed ports.

The recorded review-gate environment and results are in
[`validation/RESULTS_review_gate.md`](validation/RESULTS_review_gate.md).
The no-GPU DDL contract remains covered by
[`vector_index_cuvs_ddl.test`](../../tools/deploy/mysql_test/test_suite/vector_index/t/vector_index_cuvs_ddl.test).

## Current limitations

- Phase 1 supports dense float32 L2 indexes only.
- GPU indexes are process-local and rebuilt lazily; persistence and cache reuse
  are follow-up work.
- The explicit batch API rebuilds its CAGRA index per call.
- GPU validation is manual because upstream CI has no compatible GPU runner.
- Multi-GPU, long-duration, and snapshot-reload coverage are out of scope for
  this phase.
