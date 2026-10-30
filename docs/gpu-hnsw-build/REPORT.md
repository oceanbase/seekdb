# AMD hipVS HNSW construction measurements

The original 8.208× campaign below is historical. A new full build with the
course's `LIB=CUVS` alias measured **8.0776661036×**, with CPU/GPU medians
25.114934700 / 3.109182080 seconds and Recall@10 0.996 / 1.000. All DML,
filter, hidden-GPU restart, and 27 SQL contract checks passed. See the
[current submission](SUBMISSION.md) and
[new raw campaign](results/radeon-20261030-cuvs/sql-50000x128.json).

Measured on 2026-10-30 (Asia/Shanghai) against the frozen competition source
`5bce9e06d1281347ad61344e4b01fab9158c58a2` plus the implementation in this PR.
The exact tested source files are hashed in
[source-sha256.json](results/source-sha256.json). The compiled binary's version
string reports the base revision because the implementation was uncommitted
when compiled; its hash and the source manifest identify the tested artifact.

## Result

For 50,000 float32 vectors with 128 dimensions, native SQL HNSW construction
was **8.208× faster by the ratio of three-run medians**. CPU Recall@10 was
99.6%; GPU-produced indexes measured 100%, 100%, and 100% on the same 100
independent queries. Both exceed the local 99% floor used in this experiment.
These are local measurements, not an official competition score or a claim
that an unpublished official target has been met.

| Round | CPU end-to-end seconds | GPU end-to-end seconds | CPU Recall@10 | GPU Recall@10 |
|---|---:|---:|---:|---:|
| 1 | 25.588729635 | 3.101683253 | 0.996 | 1.000 |
| 2 | 25.458483383 | 3.096226860 | 0.996 | 1.000 |
| 3 | 24.607142027 | 3.207336940 | 0.996 | 1.000 |
| Median | **25.458483383** | **3.101683253** | | |

Full precision timings, all per-query recall values, actual GPU build events,
query plans, server settings, DML checks, and restart verification are in
[sql-50000x128.json](results/sql-50000x128.json).

## Fixed configuration and timing

| Setting | Value |
|---|---|
| GPU | AMD Radeon Graphics, gfx1100, 51,522,830,336 bytes VRAM |
| Effective CPU allocation | 16 cores, cgroup `cpu.max=1600000 100000` |
| Effective RAM allocation | 59,055,800,320 bytes (55 GiB) |
| seekdb CPU setting | `cpu_count=16` |
| OpenMP / OpenBLAS environment | 8 threads each |
| hipVS HNSW hierarchy threads | 8 |
| ROCm / HIP runtime | 7.2.4 / 70253211 |
| hipVS installed header version | 1.0.0; exact library hashes recorded |
| VSAG dependency | Installed seekdb VSAG 1.1 package |
| Compiler / CMake / Rust | clang 17.0.6 / 3.30.3 / 1.98.1 |
| Build | CMake RelWithDebInfo, Unity, `OB_BUILD_HIPVS=ON` |
| NumPy | 2.4.6 |
| Dataset / queries | 50,000 × 128 float32 / 100 × 128 float32 |
| Generator | `numpy.random.default_rng(20261030).standard_normal(..., dtype=float32)` |
| Distance / HNSW parameters | L2, M=32, ef_construction=200, ef_search=1000 |
| Snapshot memory saving | Default enabled, including first-query reload |
| Compression | No configured CAGRA compression; native vectors retain all float32 bits |

`nproc` reports 128 host CPUs, but the experiment uses the 16-core cgroup
allocation above. The deprecated `memory_limit=16G` setting is accepted by this
base revision but does not determine its actual memory budget; the finite
cgroup memory limit is recorded instead. See
[final-environment.json](results/final-environment.json) for versions and hashes.

Document IDs are `1000000007 + 17*permutation(50000)`, deliberately shuffled
and noncontiguous. The SHA256 of input vectors, IDs, and queries together is
`05f4d49fc4b4bdfdf86aa9d07e1a7632a9b6b7cc2788deba4342f6a820339476`.

The timer starts before `CREATE VECTOR INDEX` and ends after the first common
availability query returns ten distinct IDs with valid distances. It includes
the original table scan, input buffering, worker launch, transfers, GPU
synchronization, CPU hierarchy, format validation, VID mapping, VSAG import,
snapshot persistence, and any first-query reload. It is not a kernel-only
speedup. Each backend constructs a fresh index; backend order alternates.
Profiling and other validation workloads run separately from these final
timed runs.

Original data generation/import and server startup are outside the interval.
The final run imported data in 4.646024 seconds and computed independent exact
ground truth in 1.942961 seconds. Ground truth uses float64 distances over
the original float32 values. Three exact SQL queries cross-check it after each
build. Every query connection explicitly sets the same session search value.

## GPU execution evidence

Each timed GPU build logged exactly one successful 50,000-row GPU construction
event, with `gpu=true fallback=false`; no fallback or build error occurred.
CAGRA stage wall times were 1,662.072, 1,662.032, and 1,724.168 ms. These
substage measurements are diagnostic and are not used as SQL construction time.

An additional untimed profile of the same production worker, the same
50,000 × 128 input vectors, M=32, and ef_construction=200 captured **158 GPU
kernel dispatches, including 61 NN-descent dispatches**. The profile includes
NN-descent `preprocess_data_kernel`, `local_join_kernel`, reverse-edge kernels,
and CAGRA graph processing. The command, input/worker hashes, and success status
are in [kernel-trace command](results/kernel-trace-50000x128-command.json).
Raw [kernel dispatches](results/kernel-trace-50000x128/hipvs-build_kernel_trace.csv),
[kernel statistics](results/kernel-trace-50000x128/hipvs-build_kernel_stats.csv),
and memory-copy traces are preserved alongside it.

The installed hipVS NN-descent implementation uses an internal half-precision
preprocessing kernel. The input, serialized vectors, persisted native index,
and VSAG query distance calculations remain float32; the importer verifies
every serialized vector bit-for-bit. Graph approximation quality is measured
against independent exact neighbors, rather than inferred from input dtype.

## Correctness and stability checks

- All six fresh SQL builds and their 100-query quality checks passed, with
  exact SQL cross-checks on every run and a native vector-index query plan.
- A newly built GPU snapshot accepted UPDATE, DELETE, and reinsertion through
  the normal CPU delta path. Deleted IDs did not reappear. The filtered query
  returned only valid IDs and achieved Recall@10=1.0.
- The database was shut down cleanly and restarted with
  `HIP_VISIBLE_DEVICES=-1`. All 50,000 rows and the saved native index remained
  usable; restart Recall@10=1.0 with exact SQL cross-checks.
- A standalone diagnostic using the production builder checked all 4,096
  stored arbitrary 64-bit IDs and their exact self-distances, ANN self-recall
  of 1.0, native stream serialization/reload, and CPU insertion afterward.
  A 64-row case explicitly took the CPU path and passed the same checks.
- Hiding GPUs or removing the worker caused explicit failures (exit 1), with
  no silent CPU construction. Both returned control to the parent process.
- The mysqltest contract case passed, including unsupported declarations,
  ordinary CPU search, and twelve fresh CPU build/query/drop cycles. It also
  passed after the database restart with GPUs hidden.
- Default-OFF CMake configuration succeeded with deliberately nonexistent
  ROCm/hipVS prefixes and generated neither GPU compiler definitions nor a
  worker target. Both affected feature-gated C++ sources passed compilation
  syntax checks with `OB_BUILD_HIPVS` removed. A full separate CPU-only binary
  was not rebuilt; the enabled binary's unmodified `LIB=VSAG` construction
  route supplied the three timed CPU baselines.

Raw checks and commands are in [validation.json](results/validation.json).

## Limits and failed development attempts

The results cover this synthetic distribution, one Radeon architecture, one
data size, and this installed library ABI. They do not establish performance
or quality on million-row datasets or an unpublished official evaluation.
GPU construction is currently restricted to dense float32 L2 HNSW without
extra-info payload, M in {16,32,64}, and the checked 64-bit HNSW format.

Earlier in-process hipVS integration failed inside seekdb's allocation hooks;
the final implementation isolates GPU code in a separate process. Repeated
builds also exposed a snapshot renewal/reload issue, which is fixed and covered
by subsequent successful build and restart checks. An early evidence reader
missed events when the server log rotated; the final reader keeps the original
inode open and reads newly created log files.

M=16 yielded CPU Recall@10=0.975 on this dataset, below the chosen local floor.
Raising the requested ef_search beyond 1000 did not improve it because the
baseline caps top-10 searches at 1000. The final comparison uses M=32 on both
backends. An earlier M=32 candidate with intermediate degree 512 was slower;
the final candidate uses 256 and meets the same quality floor. Failed and
calibration runs are retained separately in the local evidence archive and
are not mixed into the final medians.

Reproduction commands, deployment details, and attribution are in
[README.md](README.md). The competition's exact target multiple, dataset, and
quality threshold must be applied separately when available.

## Follow-up validation changes (2026-10-30)

After cloning the existing `competition/amd-hipvs-build` branch at
`c393a801db3b3ad1640b5445ed292799101d3e11`, an additional Windows-side check
recomputed the recorded medians and 8.2079571971 speedup, matched all 14
measured source hashes against that commit, and counted the 158 kernel
dispatches and 61 NN-descent dispatches. This checks consistency of the
archived evidence; it is not an independent GPU reproduction or proof of
the original environment's provenance.

The benchmark was then corrected so filter recall and IDs actually gate
success, reinsertion is checked by a query, and failed restart verification
cannot retain an earlier successful status. Fourteen CPU-only regression
tests passed. The production hipVS bridge/builder and the historical raw
measurements were not changed. These newer benchmark checks were subsequently
executed in the separate Radeon Cloud campaign linked above. The original
performance table above describes the original measured commit only.

The supplied course link,
[AMD 赛题方向解析](https://open.oceanbase.com/course/detail/16684), was checked
as an anonymous visitor. The page requires login to watch the video and
did not expose a transcript; no additional thresholds have been inferred
from unseen video content.

The user subsequently supplied the 22.5-minute local recording. Its key
slides and complete machine transcript were checked together; see
[video alignment](VIDEO_ALIGNMENT.md). The follow-up SQL parser accepts the
course's `LIB=CUVS` spelling as an alias of the AMD `LIB=HIPVS` construction
backend, with identical capability checks. The benchmark can select it using
`--gpu-lib cuvs`, and mysqltest rejection cases were added. These C++/SQL
changes have now passed a full build and execution on the Radeon environment.
The course's 12x batch-query demonstration is not a construction result and
does not replace the independent construction measurements above.
