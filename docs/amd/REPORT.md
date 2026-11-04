# CPU/GPU HNSW construction report

The optional GPU construction path achieved **1.45× at 10,000 vectors** and
**13.13× at 100,000 vectors**, using three fresh rounds per backend and corpus.
All 12 rounds returned every query's exact top-10 target set: Recall@10=1.0000.
These are two measured scales; the 100k speedup is not a claim for every size.

| Corpus | CPU median seconds | GPU median seconds | Build speedup | Recall@10, every round |
|---|---:|---:|---:|---:|
| 10,000 × 128 | 6.174186 | 4.245433 | **1.4543×** | 1.0000 |
| 100,000 × 128 | 215.618203 | 16.423421 | **13.1287×** | 1.0000 |

## Measurement boundary and implementation

The same revision 108 observer compares ordinary lib=vsag with lib=cuvs on
separate new tables/indexes. Raw float32 vectors are ready in SQL and the target
index is absent before each timer starts. Timing ends only after DDL, the first
indexed query and successful completion proof. Transfer, synchronization, GPU
CAGRA construction, full HNSW conversion, self-contained snapshot serialization
and first-query lazy work are included. Proof/readback verification overhead is
also included in proven-ready time. DDL-only and DDL-to-first-query components
are retained in every raw report; they are not substituted for this boundary.
Import, generation and server startup occur outside the build timer.

GPU CAGRA builds the graph, then converts it to full HNSW. Queries use CPU HNSW.
No eager duplicate CPU graph is built on the selected GPU route. A fallback
round cannot qualify as GPU construction. This report makes no GPU query-speed
claim.

The measured runtime's SHA256 is
`84b7c08ae4c9d54f2c51eec91f8a847bda8501a5cecda1694fbce8a565465724`;
the bridge SHA256 is
`65c69d59001855d4a5c87ff037f38f66557020499e7d40b8dcd62f6e34b76f8d`.
Product source108 is preserved at
111c02a7b510199e1d1a69c46a9c2c6ef9b0312c. The separate public compatibility
commit8a4507da2b784fe3e39b5b615fd3d716daf01bfd only aligns an upstream Rust include,
relocates the hooks include and retains upstream's safe bitmap snprintf. It is
not a separately measured runtime; its GPU logic is unchanged.

## Environment and shared profile

The original environment/resource/launch receipts are under
evidence/formal/originals/. Hardware is AMD EPYC 9334 and gfx1100 AMD Radeon
Graphics, with ROCm 7.2.4 and installed hipVS/cuVS libraries. Both groups ran in
the same instance and observer. The process sees 128 logical CPUs and affinity
0–127; its cgroup CPU quota is 1600000/100000=16 CPU equivalents and cgroup memory
limit is 59055800320 bytes (55GiB). seekdb cpu_count=8 is a logical database
setting, not an OS affinity/quota. memory_budget=16G is a cache/buffer budget,
not an RSS cap. The retained resource receipt supplies the actual limits.

Both SQL backends use M64, ef_construction400 and effective session query ef1000.
GPU process settings use full GPU hierarchy, graph degree128, intermediate
degree256, NN-descent iterations20 and CPU search threads0 (automatic). Effective
seekdb logging is WARN for the accepted formal runs. The GPU legacy ABI reads
one process profile; SQL DDL/session parameters do not dynamically reconfigure
an existing/restored GPU handle. This run explicitly synchronizes them. See
PARAMETER_CONTRACT108.md; arbitrary per-index/per-query GPU settings are not
claimed.

## Data, exact GT and independent review

The 10k corpus comes from the official image's
/seekdb_workspace/datasets/{base,query}.f32. The separate100k corpus uses NumPy
PCG64 seed 20261028, normal vectors normalized to unit L2. Both have dimension 128,
100 complete queries and K10. Exact GT computes float64 squared L2 over every
actual float32 row, with deterministic ID tie ordering. Immutable input/gold
SHA256 values and source paths are in the two input manifests and results.json.

Independent local review recomputed recall from every prediction and gold file,
required all 100 query IDs and 10 distinct valid IDs each, and obtained 12000/12000
hits across 12 rounds. Every round's selected SQL/session/event/plan and full-N
proof was checked, including no fallback, synchronized conversion, caller-FD
snapshot, exact bridge hash loaded into the observer, and the retained GPU API
phase logs. 324 locally available proof-evidence references matched their
SHA256;24 bulk-readback references matched the export manifest's hashes.
The large omitted readbacks were not independently reparsed on this computer.
Review receipt: evidence/formal/formal-review108.json.

## Per-round construction results

### 10,000 vectors

Source: [quality64-formal10k/report.json](evidence/formal/originals/quality64-formal10k/report.json).

| Round | Actual order | CPU proven-ready seconds | GPU proven-ready seconds | CPU / GPU hits | Proof |
|---|---|---:|---:|---|---|
| 0 | CPU → GPU | 6.187226526 | 4.245433154 | 1000/1000 each | PASS |
| 1 | GPU → CPU | 6.157944601 | 4.601356314 | 1000/1000 each | PASS |
| 2 | CPU → GPU | 6.174186289 | 4.113812426 | 1000/1000 each | PASS |
| Median | — | 6.174186289 | 4.245433154 | Recall 1.0000 | 1.454312450× |

### 100,000 vectors

Source: [quality64-formal100k-warn/report.json](evidence/formal/originals/quality64-formal100k-warn/report.json).

| Round | Actual order | CPU proven-ready seconds | GPU proven-ready seconds | CPU / GPU hits | Proof |
|---|---|---:|---:|---|---|
| 0 | CPU → GPU | 215.618203098 | 15.320032857 | 1000/1000 each | PASS |
| 1 | GPU → CPU | 217.130011935 | 16.423421180 | 1000/1000 each | PASS |
| 2 | CPU → GPU | 213.681720072 | 16.746494776 | 1000/1000 each | PASS |
| Median | — | 215.618203098 | 16.423421180 | Recall 1.0000 | 13.128702037× |

The median speedup is median(CPU proven-ready seconds) divided by median(GPU
proven-ready seconds), computed independently from each original three-round
pair. Round 0 and 2 run CPU then GPU; round 1 runs GPU then CPU.

## Retained diagnostics

The earlier M16/ef512 100k smoke returned CPU Recall 0.762 and GPU Recall 0.863.
It is retained at originals/candidate108-smoke100k/report.json and excluded from
the qualified result. The first INFO-log formal attempt ended with a failed
completion-proof command; its complete report is retained and the entire attempt
is excluded. Accepted 100k results use quality64-formal100k-warn; accepted 10k
results use quality64-formal10k. No rounds are selected from failed attempts.

## Evidence availability and remaining checks

The compact import initially retained 18,989 originals, while 2,676 large SQL
INSERT/readback files were listed by original size/SHA256. The complete archive
has now been downloaded and verified locally: 1,293,531,108 bytes, SHA256
`5995e9d5dc371cc7ed09cb958756751c47b2d84a8d03027f093ef17915840d82`.
All 21,665 original files (3,099,202,209 original bytes) and the exact embedded
manifest passed safe-path, byte-size and SHA256 validation. The 2,676 compact
omissions were restored and reread; no original remains missing locally.
See full-archive-review108.json and the refreshed formal-review108.json. The
initial compact-stage receipt is also retained explicitly as historical evidence.

This archive audit checks original bytes and hashes. It does not claim to rerun
the full raw-vector readback semantic calculation. The formal completion proof
retains the original bitwise-readback evidence; all query predictions and exact
GT intersections have been independently recomputed locally. The byte and
semantic verification scopes remain distinct in results.json.

The repository includes 449 selected raw review originals (9,508,101 bytes),
including reports, full predictions/GT/recall, phase logs, plans, selected
sessions, all SQL-event records and the complete original hash manifest.
small-raw-index.json maps every selected original. The full compact import stays
outside Git at D:/ob2026_plan/runs/amd107/cloud-final-formal for local audit.

## Existing-index recovery and read-only replay

The original formal observer PID14001 was fully stopped before a new observer
PID33736 was started at the same existing DB path, using the same saved argv,
explicit settings and runtime/bridge hashes. The separate recovery receipt
records the old process with no active threads, port2882 free, and the new
process SQL-ready. It records no new cloud instance, no database-directory
deletion/creation and no DDL. The original guarded restart refused replacement
while the port was occupied; that initial refusal is operator history, whose
original diagnostic is not in this packet. The subsequent recovery receipt and
read-only replay originals are retained and independently checked.

Both original final-round100k CPU/GPU tables retain100000 rows (IDs0–99999),
and their plans use the original `ix_cpu_r02`/`ix_gpu_r02` VECTOR INDEX SCAN.
Against unchanged original GT, both complete100-query replays achieve1000/1000
Recall@10, minimum per-query1.0. The independent review checks all871 exported
originals by SHA/size and all205 SELECT/EXPLAIN SQL/event/session/raw-row records;
reconstructed query-vector text matches its original GT-manifest SHA256.

Recovery reuses saved settings over the current shell environment; complete
original process-environment reuse is not claimed. Directory identity was
checked during recovery; historical inode continuity from before original
shutdown is not proven. The replay does not add a construction timing result.
See evidence/persistence/OPERATION_HISTORY.md, persistence-review108.json and
the840 selected originals. The original replay report retains
`restart_verified_by_this_script:false`; the actual launch is evidenced by the
separate recovery receipt. Full bulk archive local byte verification is complete, with its separate
audit receipt described above.

Source, tests, commands and operator-script scope are documented in
tools/benchmark/amd_hnsw/README.md and OPERATIONS.md. An upstream PR URL and
final submission commit will be added after the final material review.
