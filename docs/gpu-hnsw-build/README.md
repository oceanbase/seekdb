# GPU construction of native HNSW indexes

Latest submission: [Chinese instructions](SUBMISSION.md) and
[2026-10-31 build/deployment acceptance](ENTRY_AUDIT_20261031.md).
The fully rebuilt course-flag binary measured **7.7807×** construction speedup;
raw results are in [radeon-20261031-entry](results/radeon-20261031-entry).
The separate historical 8.0777× campaign remains in
[radeon-20261030-cuvs](results/radeon-20261030-cuvs).

`LIB=HIPVS` uses AMD hipVS to construct a CAGRA base graph on the GPU, converts
it into a full mutable HNSW graph with a CPU hierarchy, and imports that graph
into VSAG. The resulting index uses seekdb's normal query, filter, delta-update,
and snapshot persistence paths. `LIB=VSAG` retains CPU construction.

The course spelling `LIB=CUVS` is also accepted as an alias of this AMD
construction backend. See [video alignment and validation status](VIDEO_ALIGNMENT.md)
and [Radeon Cloud setup](RADEON_CLOUD_SETUP.md). This is not a CUDA backend or
the course's separate batch-query procedure.

## Build and run

The optional feature defaults to **OFF**. An enabled build requires Linux
x86-64, glibc 2.34 or later, the seekdb VSAG dependency, ROCm, and an installed
hipVS C library exposing CAGRA construction and HNSW conversion/serialization.
The tested Radeon image provides hipVS under `/opt/hipvs` and ROCm under
`/opt/rocm`. See [the measured report](REPORT.md) for exact versions.

The course build flag `-DOB_BUILD_CUVS=ON` is accepted as an alias of
`-DOB_BUILD_HIPVS=ON`. Both enable this AMD HNSW construction implementation;
if both flags are supplied, their values must agree. Leaving both flags unset
keeps the default CPU build.

```bash
# Initialize normal seekdb build dependencies first, if needed.
./build.sh release --init -DOB_BUILD_HIPVS=ON \
  -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm
make -C build_release -j8 seekdb

# The child worker needs the installed hipVS/ROCm libraries.
export LD_LIBRARY_PATH="/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
python3 .github/script/seekdb/sdb.py start \
  --binary "$PWD/build_release/src/observer/seekdb" \
  --base-dir /var/tmp/hipvs-test --port 2881 \
  --parameter cpu_count=16
```

Deploy `seekdb_hipvs_hnsw_worker` beside the seekdb executable and
`libseekdb_hipvs_hnsw.so` in its library directory. CMake builds the worker
when building `seekdb`. The `server` install component places the worker in
`usr/bin` beside seekdb and the bridge in `usr/libexec/seekdb/lib`; the worker's
relative runtime search path locates that bridge. The bridge retains the
configured hipVS/ROCm library paths. `tools/deploy/copy.sh` also copies the
worker and bridge to its `bin`/`lib` layout for GPU builds. Keep the normal CPU
deployment by leaving both GPU flags disabled. The deployment script reads the
current CMake configuration, ignores stale GPU artifacts in CPU builds, and
fails when an enabled build is missing its worker or bridge.

Use a local filesystem for the live database. The tested cloud's persistent
`/workspace` is NFS and rejects seekdb's `fallocate`; retain source, binaries,
and evidence there, but run the database on local storage. See the recorded
[failed startup log](results/radeon-20261030-cuvs/failed-nfs-startup.log).

```sql
CREATE TABLE documents(id BIGINT PRIMARY KEY, embedding VECTOR(128));
-- Import the original float32 vectors before creating the index.
CREATE VECTOR INDEX ann ON documents(embedding)
  WITH (distance=l2, type=hnsw, lib=hipvs,
        m=32, ef_construction=200, ef_search=1000);
SET ob_hnsw_ef_search=1000;
SELECT id FROM documents
  ORDER BY l2_distance(embedding, '[...]') APPROXIMATE LIMIT 10;
```

Supported GPU construction is dense float32 L2 HNSW, no extra-info payload,
and `M` equal to 16, 32, or 64. Unsupported combinations and use of `LIB=HIPVS`
in a disabled build return an error. Small snapshots with at most
`max(128, 4*M)` rows explicitly use CPU construction and log
`gpu=false fallback=true`. Larger GPU failures propagate without CPU fallback.
Existing native snapshots and CPU delta indexes do not need a GPU to query.

## Construction and isolation

Snapshot insertion batches collect vectors and arbitrary 64-bit seekdb VIDs.
Finalization before serialization or a read creates private anonymous files,
writes the complete float32 dataset, and uses `posix_spawn` to launch the
worker. It closes inherited database descriptors other than the two data files
and standard streams. The parent waits for completion, with a 30-minute limit.

The worker uses NN-descent CAGRA construction, graph degree `2*M`, intermediate
degree 256 for M=16/32 or 512 for M=64 (capped for small inputs), and uncompressed vectors. It synchronizes
GPU work, builds the HNSW hierarchy with eight CPU threads and the requested
`ef_construction`, then serializes it. The parent checks the 64-bit HNSW layout,
every ordinal, every stored float32 vector, and edge bounds before mapping
ordinals to the original VIDs and importing the graph as VSAG `hnsw_data`.
This format is checked explicitly; another VSAG/hipVS serialization version
may require an updated importer.

hipVS and VSAG both export hnswlib symbols. They must live in separate
processes: direct linking causes symbol collisions, and loader namespaces
alone do not isolate seekdb's allocation hooks. Only the worker loads hipVS;
no GPU library is linked into seekdb. Worker startup, transfers, synchronization,
conversion, validation, import, persistence, and first-query loading all remain
inside the measured SQL construction interval.

Snapshot memory renewal also preserves the reload flag and honors it even
when another adapter still holds the same snapshot prefix. This prevents a
query from observing a freshly renewed empty index after successful DDL.
Targets created solely for deserialization use native VSAG directly.

## Reproduce the comparison

Install NumPy in the chosen Python environment and provide the `mysql` CLI.
Use a dedicated database: the benchmark recreates its `corpus` table.

```bash
OPENBLAS_NUM_THREADS=8 OMP_NUM_THREADS=8 python3 \
  docs/gpu-hnsw-build/benchmark.py \
  --rows 50000 --dim 128 --queries 100 --rounds 3 \
  --m 32 --ef-construction 200 --ef-search 1000 --min-recall 0.99 \
  --database hipvs_build_benchmark \
  --server-log /var/tmp/hipvs-test/log/seekdb.log \
  --output /workspace/hipvs-results.json
```

Both backends construct a fresh index in alternating order on the same imported
dataset. The timer runs from `CREATE VECTOR INDEX` through completion of the
same first ANN availability query; it includes snapshot reload and returns
only when ten distinct IDs with valid distances are available. Generation,
import, exact-ground-truth computation, and server startup are reported
separately. Do not run profiling or another benchmark concurrently.

The fixed seed is 20261030. Vectors are float32, document IDs are shuffled and
noncontiguous, and exact nearest neighbors are computed independently with
float64 NumPy distances. Three exact SQL queries cross-check that ground truth
on every run. The tool records all raw times, per-query Recall@10, medians,
speedup, server settings, query plans, dataset SHA256, and actual GPU-path logs.
The local 99% quality floor is a reproducibility choice; the competition's
official quality and speedup thresholds have not been supplied here.

After compiling the current source, use `--gpu-lib cuvs` to exercise the
course's SQL spelling, or `--gpu-lib hipvs` for the original spelling. The
chosen spelling is recorded in the benchmark configuration. Use a new output
path for new measurements; the archived results predate this alias change.

The baseline caps effective `ef_search` at `max(1000, 10*topk)`; increasing the
session value beyond 1000 does not improve top-10 search. M=16 measured 97.5%
CPU recall on this dataset, so the reported comparison uses M=32 for both
backends. GPU performance results containing fallback or GPU errors fail.
Failures are preserved in the JSON, and do not produce a completed report.

After all rounds, the tool tests update, delete, reinsert, and filtering and
leaves a newly built GPU-produced native index. Restart the same database,
then repeat the exact command with `--verify-existing` and the same output
path to verify persistence and retrieval quality. Existing snapshots can also
be checked with `HIP_VISIBLE_DEVICES=-1` in the server environment.

The mysqltest case
`vector_index/t/vector_index_hipvs_validation.test` covers unsupported GPU
declarations, the CPU route, and twelve fresh build/query/drop cycles. It runs
without GPU resources and in builds where the optional feature is disabled.

On an enabled build, this checker also exercises both GPU spellings and mixed
case, including the explicit tiny CPU path:

```bash
python3 unittest/sql/vector_index/check_hipvs_sql.py \
  --database hipvs_contract_check --output /workspace/hipvs-sql-contract.json
```

It passed 27 actual SQL checks in the new cloud run. It uses the `mysql` CLI;
it does not claim to be an execution of the mysqltest runner.

## CPU-only regression and archived-evidence checks

The benchmark now rejects filtered results that contain invalid IDs or fall
below `--min-recall`, checks that the reinserted vector is searchable, and
sets `completed=false` on failure. Restart verification records a separate
`running`/`passed`/`failed` state and retains `build_completed` so a failed
restart can be retried without reusing a stale successful verification.
Reports are read and written explicitly as UTF-8, including on Windows.

Run the focused checks without NumPy, SQL, ROCm, or an AMD GPU:

```bash
python3 -m unittest discover -s unittest/sql/vector_index -p 'test_hipvs_*.py' -v
python3 docs/gpu-hnsw-build/verify_evidence.py \
  --source-ref c393a801db3b3ad1640b5445ed292799101d3e11
```

The second command recomputes medians and speedup from the six archived runs,
validates per-query recall and filter/restart results, checks the measured
source hashes against the specified commit, and counts the raw GPU kernel
dispatch records. It performs **consistency checks only**, not a fresh SQL
build or GPU execution. A failure exits nonzero. Tests also reject changed
speedups, missing rounds, wrong-dataset GPU events, nonfinite recall, and
stale restart success.

The historical measured source is the original commit above. The subsequent
benchmark fixes and CUVS alias were separately built and executed on Radeon
Cloud; use the new campaign below to verify the current source.
Do not edit the historical source manifest to make it match later edits.
Omitting `--source-ref` checks the current working tree instead (normalizing
Windows CRLF); it will correctly report that the benchmark has changed since
the archived measurements. New GPU runs should use a new output path and
record their own source manifest and environment.

```bash
python3 docs/gpu-hnsw-build/verify_evidence.py \
  --results-dir docs/gpu-hnsw-build/results/radeon-20261030-cuvs \
  --source-ref 0e86b8c38afaf8fe3359041645c9b53c5a8524fe
```

This verifies 16 measured source files at the recorded commit, six timed runs, quality,
persistence results, and the new kernel trace. It remains an offline evidence
check, not a fresh GPU benchmark execution.

## Attribution

The official [AMD hipVS project](https://github.com/AMD-Ecosystem/hipVS) provides
the CAGRA and HNSW algorithms. The seekdb hipVS integration discussion and
[PR #1314](https://github.com/oceanbase/seekdb/pull/1314) informed the environment
and C API investigation. Their batch-query performance is not used as this
implementation's construction result. This implementation constructs a native
HNSW snapshot and measures its complete SQL construction path.
