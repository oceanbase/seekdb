# AMD hipVS snapshot construction

`lib=hipvs` is an explicit, optional FP32/L2 HNSW snapshot builder. The default
`lib=vsag` path is unchanged. hipVS CAGRA builds the base graph on AMD hardware;
the CPU completes the upper HNSW hierarchy. The native export is validated,
its source-row labels are mapped to seekdb VIDs, and a fresh VSAG index imports
the completed graph. Queries, filters, incremental writes, and persistence then
use the existing VSAG implementation. There is no GPU query-time build cache.

Only `type=hnsw,distance=l2` without stored extra information is supported.
Nonempty batches must contain more than `2*M` vectors; an unsupported batch or
unavailable GPU/backend fails explicitly. No CPU fallback earns a GPU result.
An empty table has no GPU work and is not performance evidence.

## Build

Use the competition's Radeon `seekdb-hipvs` workspace. The tested bridge uses
ROCm 7.2.4, gfx1100, and its preinstalled `/opt/hipvs` C API. Both headers and
the library must provide `cuvsHnswFromCagraWithDataset` and `cuvsHnswSerialize`.
The AMD hipVS project retains `cuvs` symbol/header names.

The normal Linux CMake build now detects this SDK and builds the bridge next to
`build_release/src/observer/seekdb`. Packaging installs it under `usr/lib/seekdb`.
The loader resolves these locations relative to the executable, without requiring
a launch-time environment variable or searching the current directory.

```sh
bash build.sh release -DSEEKDB_WITH_HIPVS=ON --make -j8
```

`SEEKDB_WITH_HIPVS=AUTO` (default) leaves CPU-only hosts unchanged when the SDK is
absent. `ON` fails during configuration if dependencies are missing; use it for
an AMD validation build. `OFF` disables the bridge. Custom SDK locations use
`-DSEEKDB_HIPVS_PREFIX=/path/to/hipvs -DSEEKDB_ROCM_PATH=/path/to/rocm`.
The bridge link checks undefined symbols instead of deferring them to startup.

For a manual diagnostic installation, the explicit override remains available:

```sh
bash tools/hipvs_bridge_build/build.sh /workspace/amd-build
export SEEKDB_HIPVS_BUILD_LIBRARY=/workspace/amd-build/libseekdb_hipvs_build.so
export SEEKDB_HIPVS_BUILD_THREADS=8
export LD_LIBRARY_PATH=/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}
```

When using an override, these variables must be inherited by the **modified seekdb process**, before
its first HIPVS build. The Notebook's original binary does not implement
`lib=hipvs`. Build this checkout using the repository's normal Release workflow;
installing the bridge alone does not change the database executable.

After loading data into a table without a vector index:

```sql
CREATE VECTOR INDEX idx_gpu ON vectors(embedding)
WITH (distance=l2,type=hnsw,lib=hipvs,m=16,ef_construction=200,ef_search=200);
```

GPU export runs on a dedicated 32 MiB pthread stack and serializes GPU jobs to
avoid initializing ROCm on a seekdb coroutine stack. Temporary native files are
stored under private `mkdtemp` directories and removed after import. A process
crash may leave one private directory behind. The optional backend stays loaded
for the process lifetime because unloading live ROCm TLS/runtime state is unsafe.

## Validation status

On 2026-10-31 the bridge compiled on the supplied AMD instance and built 128x8
and 1000x32 FP32 graphs. Every source vector and row label survived export.
These are **bridge checks**, not SQL end-to-end performance measurements.

The local real-VSAG format test validates graph rejection, signed/non-contiguous
64-bit VIDs, L2 distances, filtering, stream persistence, and incremental Add
after restore. `unittest/amd_hipvs` also provides SQL end-to-end timing and quality
checks. Follow its README for at least three CPU/GPU trials and raw evidence.
The original SQL integration and performance results are recorded in
`unittest/amd_hipvs/RESULTS_20261101.md`. They used a manually built bridge and
explicit launch environment; they do not validate the new automatic delivery.
Run `run_cloud_validation.py` in its default `--bridge-mode installed` mode to
validate ordinary build output without the environment override. The legacy
manual mode is diagnostic only. Neither mode proves official competition acceptance.
