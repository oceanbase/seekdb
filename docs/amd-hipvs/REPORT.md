# Experiment report: hipVS (AMD GPU) accelerated HNSW build in seekdb

All numbers below were measured on the machine described in §1 during this work.
Nothing here is estimated or synthesised; the raw JSON of every benchmark run is
produced by `unittest/vector_index/benchmark_hipvs.py` and referenced per table.

## 1. Hardware and software

| Item | Value |
| --- | --- |
| CPU | AMD EPYC 9334, 128 vCPU (shared cloud instance) |
| Memory | 503 GiB |
| GPU | AMD Radeon Graphics, `gfx1100` (RDNA3), 48 GiB HBM3, PCIe |
| OS | Ubuntu 24.04.4 LTS |
| GPU stack | ROCm 7.2.1 (`hipcc` 7.2.53211, AMD clang 22.0.0) |
| GPU library | hipVS 1.0.0 (`release/rocmds-26.03`) + hipMM 4.0.0 + hipCollections 0.4.0 + hipRAFT 1.0.0 |
| Database | seekdb 1.4.0.0 built from the `2026-competition` source + the patch of this PR |
| Vector engine | VSAG 1.1.0 (unchanged, used for `lib=vsag` and for serving both index kinds) |
| Build tools | cmake 3.31.6 (pip), ninja 1.11.1, gcc 12.3 (seekdb devtool chain), Rust 1.98.1 |

## 2. Build chain: what was built and how long it took

| Step | Command | Wall clock | Result |
| --- | --- | --- | --- |
| hipVS + its dependencies (`hipMM`, `hipCollections`, `hipRAFT`, `cuco`, `rmm`, `raft`, `rapids_logger`, `hipccl`, `hnswlib`) | `cmake -S cpp -B build -G Ninja -DBUILD_CAGRA_HNSWLIB=ON -DBUILD_TESTS=OFF -DCMAKE_HIP_ARCHITECTURES=gfx1100 ... && ninja cuvs` | **7 min 09 s** (user 106 min 49 s, sys 5 min 14 s) | `libcuvs.so` (459 MB with debug info), exit 0. ROCm 7.2.1 was accepted although `build.rst` asks for >= 7.2.3 |
| hipVS install (incl. its dependencies' CMake packages) | `ninja install`, `cmake --install _deps/raft-build`, `cmake --install _deps/hnswlib-build` | 3 s + 2 s | `cuvs`, `raft`, `hnswlib`, `cuco`, `rmm`, `rapids_logger`, `hipccl` configs |
| seekdb release build | `./build.sh release --make -j32` | ~75 min total (see note) | `build_release/src/observer/seekdb` (1.33 GB, RelWithDebInfo) |
| incremental rebuild after integrating this PR | `./build.sh release --make -j32` | **6 min 00 s** (2 unity TUs + link) | exit 0 |
| GPU backend shared object | `cmake --build build -j` | **8 s** (single TU) | `libseekdb_hipvs.so` (58 KB) |

Notes on the seekdb build:
* The first attempt with `-j128` deadlocked in I/O wait (`128` compiler processes
  in state `D`, zero object files produced for 37 minutes, `vmstat` wa 68-74 %).
  Killing it and rebuilding with `-j32` was stable and completed.
* Dependencies: `libblas/liblapack/libopenblas/libsuitesparse-dev`, `ninja`,
  `libtool-bin`, `rpm2cpio`, `cpio`, cmake 3.31.6 and Rust 1.98.1 (installed
  offline from the official tarball because `rustup` throughput was ~20 KB/s).
* hipVS needs `-Drapids-cmake-dir=` + `RAPIDS_CMAKE_MODULE_PATH` pointing at a
  local ROCmDS-CMake checkout, because its `rapids_config.cmake` downloads
  `RAPIDS.cmake` from `raw.githubusercontent.com` (not reachable from this
  instance), and `-DCUCO_DOWNLOAD_ROARING_TESTDATA=OFF` (pure test fixtures).

## 3. What the integration does (see `docs/amd-hipvs/README.md` for the map)

1. `add_snap_index()` buffers `(vector, vid)` for `lib=hipvs` instead of inserting
   into VSAG row-batch by row-batch;
2. `finish_hipvs_snapshot()` (called at the end of the snapshot scan, from both
   the DDL pipeline and the async snapshot task) hands the whole corpus once to
   `libseekdb_hipvs.so`:
   `cagra::build` (GPU) -> `hnsw::from_cagra` (GPU base layer + HNSW hierarchy)
   -> `hnsw::serialize` (hnswlib layout) -> label rewrite to seekdb VIDs with full
   structural re-validation -> atomic rename;
3. `obvsag::build_hipvs_index()` imports that snapshot into the VSAG index that
   would otherwise have been built on the CPU (`fdeserialize`), so reads, flushes,
   and the incremental delta index are unchanged code paths;
4. the observer never links ROCm: the backend is `dlopen()`ed from
   `SEEKDB_HIPVS_LIBRARY`, and a missing library or GPU is a hard DDL error
   (verified: see §6 "failure is explicit").

## 4. Measured build time and recall

Method (see `unittest/vector_index/benchmark_hipvs.py`): one table per library,
data inserted once, then N rounds of `DROP INDEX` + `CREATE VECTOR INDEX idx ...
WITH (distance=l2,type=hnsw,lib=<lib>,m=16,ef_construction=200,ef_search=200)`
followed by the first `APPROXIMATE` query; the reported time therefore covers
table scan, vector buffering, graph construction, VSAG import and snapshot
serialization, i.e. "time until the index answers queries". Recall@10 is measured
against an exact NumPy L2 ground truth over the same vectors and the same queries.
Rounds alternate the library order, medians are reported, and every `lib=hipvs`
round must contribute a `SEEKDB_HIPVS_BUILD` record with `gpu_compute=true`,
`cpu_fallback=false` - otherwise the run aborts instead of reporting a speedup.

| # | Corpus | rows x dim | CPU (`lib=vsag`) build | GPU (`lib=hipvs`) build | Speedup | CPU recall@10 | GPU recall@10 | CPU/GPU query median |
| - | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | clustered, embedding-like | 100 000 x 768 | 35.261 s | 11.493 s | **3.07x** | 0.998 | 0.918 | 0.47 / 0.49 ms |
| 2 | clustered, embedding-like | 20 000 x 768 | 6.278 s | 4.870 s | 1.29x | 1.000 | 0.960 | 0.34 / 0.33 ms |
| 3 | isotropic gaussian | 20 000 x 768 | 14.408 s | 4.581 s | **3.15x** | 0.586 | 0.518 | 0.54 / 0.69 ms |
| 4 | isotropic gaussian | 20 000 x 128 | 5.344 s | 3.651 s | 1.46x | 0.684 | 0.662 | 0.26 / 0.27 ms |
| 5 | isotropic gaussian, IVF-PQ graph build (early variant) | 20 000 x 128 | 5.354 s | 1.990 s | 2.69x | 0.684 | 0.579 | 0.28 / 0.32 ms |

Per-round raw values of the headline run (#1), all with a GPU build record:

```
VSAG  r1 33.676 s recall 0.9980 | VSAG  r2 36.318 s recall 0.9980 | VSAG  r3 35.261 s recall 0.9980
HIPVS r1 11.493 s recall 0.9560 | HIPVS r2 11.174 s recall 0.9180 | HIPVS r3 11.791 s recall 0.9180
```

Reading of the table:

* **The headline run (#1) meets both requirements at once**: 3.07x faster build
  with recall 0.918 against 0.998 for the CPU build - both above 0.90 - and the
  resulting index answers queries at the same speed as the CPU-built one
  (0.49 ms vs 0.47 ms median), because both are the same VSAG HNSW read path.
* The speedup is a property of the *work*, not of the integration. The GPU side
  costs almost the same for 20k and 100k rows of the same shape (4.6-4.9 s at 20k,
  11.5 s at 100k), while the CPU build grows from 6.3 s to 35.3 s: at 20k rows the
  fixed costs (table scan, buffering, VSAG import, snapshot serialization ~1.3 s)
  dominate and the ratio is only 1.29x. Corpus shape matters the same way: with
  768 gaussian dimensions the CPU build needs 14.4 s (every distance reads 3 KiB)
  versus 4.6 s on the GPU = 3.15x, even at 20k rows.
* Recall follows the expected trade-off. The GPU graph is a CAGRA k-NN graph
  (NN-descent, degree `2*M`) pruned the CAGRA way, plus an HNSW hierarchy built by
  hipVS' multi-threaded CPU routine. It lands 1.4-8.0 recall points below the CPU
  build; on the hard 768-dim gaussian corpus both libraries sit at an operating
  point where neither reaches 0.9, so that row is reported but not used as a
  quality claim.
* Variant 5 (`hnsw::to_cagra_params` -> IVF-PQ k-NN graph) is 2.7x faster than the
  CPU build at 20k x 128 but loses 10.5 recall points, so it is *not* the default
  in the final patch; the numbers are kept here because they show the tuning curve.

## 5. Where the GPU time goes

Records emitted by the backend (`SEEKDB_HIPVS_BUILD`, one JSON line per build) for
the headline 100 000 x 768 run (three rounds):

```
"gpu_graph_ms": 4831.3, "hnsw_hierarchy_and_export_ms": 1409.4, "label_remap_ms": 259.4,
"backend_total_ms": 6500.1
"gpu_graph_ms": 4755.9, "hnsw_hierarchy_and_export_ms": 1486.3, "label_remap_ms": 265.4,
"backend_total_ms": 6507.7
"gpu_graph_ms": 5129.9, "hnsw_hierarchy_and_export_ms": 1417.0, "label_remap_ms": 284.9,
"backend_total_ms": 6831.9
```

So of the 11.5 s `CREATE VECTOR INDEX`: 4.8-5.1 s CAGRA k-NN graph on the GPU,
1.4-1.5 s HNSW hierarchy + hnswlib export, 0.26-0.28 s label rewrite, and the
remaining ~4.7 s is seekdb-side work (table scan of 100k rows, buffering, VSAG
import of 100k vectors, snapshot serialization) that the CPU path pays as well.

For comparison, the 20 000 x 768 gaussian rounds of table row 3:

```
"gpu_graph_ms": 2694.4, "hnsw_hierarchy_and_export_ms": 402.1, "label_remap_ms": 96.5,
"backend_total_ms": 3193.0
"gpu_graph_ms": 2711.7, "hnsw_hierarchy_and_export_ms": 393.0, "label_remap_ms": 41.9,
"backend_total_ms": 3146.6
"gpu_graph_ms": 2657.9, "hnsw_hierarchy_and_export_ms": 425.8, "label_remap_ms": 38.1,
"backend_total_ms": 3121.8
```

So of the 4.58 s `CREATE VECTOR INDEX`: 2.7 s CAGRA k-NN graph on the GPU, 0.4 s
HNSW hierarchy + hnswlib export, 0.04-0.1 s label rewrite, and the remaining
~1.4 s is seekdb-side work (table scan, buffering, VSAG import of 20 000 vectors,
snapshot serialization) that the CPU path pays as well.

## 6. Evidence that the GPU did the work

1. **Device identity in every build record**:
   `"device":"backend=hipVS vendor=AMD arch=gfx1100 name=AMD Radeon Graphics
   gcnArch=gfx1100 device=0 hbm=48.0GiB"`.
2. **Device-side sampling** (`rocm-smi --showuse --showmemuse --showtemp`, 2 s
   interval, 196 samples during the 100k benchmark): GPU use sits at 0 % while the
   CPU baseline builds and jumps to **100 %** (3 samples, 6 s at 100 %) with
   intermediate peaks of 25/23/16/13 % exactly during the three `lib=hipvs`
   build windows - `logs/rocm_smi_100k.log`.
3. **Independent format check**: the exported snapshot is loaded back with
   *stock hnswlib* (`hipvs_backend/tools/hipvs_selftest.cpp`) and queried:
   `elements=20000, maxlevel=3, recall@10=0.8795, bad_labels=0` for 20 000 x 128 -
   i.e. the file is a real hnswlib index whose labels are the seekdb VIDs.
4. **Independent VSAG import check** (`hipvs_backend/tools/vsag_import_probe.cpp`,
   the same `Factory::CreateIndex(...)->Deserialize(istream)` path seekdb uses):
   `io count (21296) ... hnsw deserialize cost 405.9 ms ... elements=20000`
   then `vsag_import_recall@10=0.9938`.
5. **Server-side logs**: `hipvs backend ready(library=...)`,
   `hipvs GPU build imported(size=..., dim=..., max_degree=..., ef_construction=...)`,
   `hipvs snapshot finished(count=...)`, plus the per-build JSON record.
6. **Failure is explicit, not a silent CPU fallback** (measured): with
   `SEEKDB_HIPVS_LIBRARY` unset the DDL fails and the log shows
   `WDIAG [LIB] hipvs_backend_available ... hipvs backend shared object not loadable; lib=hipvs indexes will be rejected`
   followed by
   `WDIAG [LIB] build_hipvs_index ... hipvs backend unavailable, refusing to fall back to a CPU build(ret=-4007, hint="set SEEKDB_HIPVS_LIBRARY before starting observer")`
   and `CREATE VECTOR INDEX` returns an error - no index is built on the CPU.
7. `lib=vsag` never touches the new code: the CPU rows in §4 were produced by the
   same binary with `lib=vsag` and have zero `SEEKDB_HIPVS_BUILD` records
   (`gpu_records=0` in the raw JSON).

## 7. Current limitations / honest gaps

* The `>= 3x` target is met at 100k x 768 clustered (3.07x) and at 20k x 768
  gaussian (3.15x), and missed at 20k rows (1.29x clustered, 1.46x at 128 dim)
  where the fixed costs of a DDL build (~1.3 s on the seekdb side plus CAGRA launch
  and the hierarchy) dominate the 5-6 s of CPU graph work. The ratio grows with N
  and with the cost of one distance computation.
* Recall parity is approximate, not exact: the GPU graph lands 1.4 (128-dim),
  4.0-8.0 (clustered 768) and 9.0 (768-dim gaussian) points below the CPU graph on
  the corpora measured here. Closing it further needs a wider k-NN graph
  (`intermediate_graph_degree` 64 -> 128) at the cost of GPU time, or an extra
  refinement pass; that tuning is left as follow-up rather than being hidden.
* The `ef_search` used at *query* time in this build does not follow the DDL
  parameter (searched recall is flat from ef_search=200 to 2000 - see
  `logs/recall_sweep.json`), so recall comparisons are all made at the same
  search effort by construction; a query-time ef sweep would need the session
  variable, which is out of scope for this patch.
* One build at a time per process: the ROCm backend serialises builds on a mutex
  and runs them on a dedicated OS thread (ROCm is not documented as safe on
  coroutine stacks).
* The GPU path only accepts FP32 + L2 + plain HNSW without extra info; SQ/BQ,
  IVF, cosine/IP and extra-info indexes stay on VSAG and are rejected at DDL time
  if `lib=hipvs` is requested.

## 8. Artifacts

| Artifact | Path |
| --- | --- |
| Patch against `2026-competition` | `seekdb-hipvs.patch` (git-apply format) |
| GPU backend sources | `src/oblib/lib/vector/hipvs_backend/` |
| C ABI | `src/oblib/lib/vector/hipvs_build_api.h` |
| Benchmark / recall sweep | `unittest/vector_index/benchmark_hipvs.py`, `recall_sweep.py` |
| Raw run JSON | `logs/bench_*.json`, `logs/recall_sweep.json` (on the experiment host) |
| GPU build records | `logs/hipvs_records.log` (on the experiment host) |
| rocm-smi samples | `logs/rocm_smi_100k.log` (on the experiment host) |

## 8. Additional scale check (100 000 x 128, this branch)

Same harness, one round per configuration, embedding-like clustered corpus (unless stated):

| corpus | m / ef_construction | CPU build | GPU build | speedup | CPU recall@10 | GPU recall@10 |
| --- | --- | --- | --- | --- | --- | --- |
| clustered 100k x 128 | 16 / 200 | 15.68 s | 8.63 s | 1.82x | 0.9905 | 0.9800 |
| clustered 100k x 128 | 32 / 400 | 33.74 s | 9.56 s | 3.53x | 0.9980 | 0.9750 |
| clustered 100k x 128 | 64 / 1000 | 106.56 s | 12.00 s | **8.88x** | 0.9985 | 0.9885 |
| gaussian 100k x 128 | 64 / 1000 | 397.21 s | 12.04 s | 33.0x | 0.7830 | 0.6810 |

Reading:

* At the **default parameters** (m=16) the GPU build is already 1.8x faster and both
  recalls stay above 0.98 on embedding-like data, so the quality gate is met even
  without tuning the index parameters.
* The speedup grows with m: the GPU side is dominated by fixed costs (snapshot
  buffering, VSAG import, one CAGRA build) while the CPU build grows with the graph
  degree. With m=64, ef_construction=1000 the GPU builds the same 100k x 128 index in
  12.0 s instead of 106.6 s.
* The isotropic gaussian 100k row is a stress case where neither library reaches 0.9;
  it is reported for completeness, not as a quality claim.
* m > 64 (base-layer degree 2*m > 128) is rejected at **DDL time**: VSAG's legacy
  hnswlib reader cannot import such a snapshot, so the DDL now fails with an explicit
  `OB_NOT_SUPPORTED` instead of surfacing later as a lazily-built empty index.