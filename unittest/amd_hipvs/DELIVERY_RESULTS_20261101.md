# AMD ordinary-build delivery validation, 2026-11-01

The ordinary Linux build now builds and locates the hipVS bridge without a
`SEEKDB_HIPVS_BUILD_LIBRARY` launch override. This fixes a deployment gap in the
previous implementation, whose successful cloud tests built the bridge separately
and set that variable. The competition returned only “execution failed”, without
a build or runtime log, so this gap is not claimed as the sole platform failure.

## Tested version and environment

- Source: `d8c84b850e202123ce81e096de8ed308f5c3cf6e`; clean source checkout.
- AMD Radeon Graphics, gfx1100; AMD EPYC 9334; 8 fixed CPU IDs.
- ROCm 7.2.4 and the competition workspace's preinstalled hipVS C SDK.
- Full build: `bash build.sh release -DSEEKDB_WITH_HIPVS=ON --make -j8`.
- Build ran 10:14:21–10:23:01 UTC; bridge and seekdb targets completed.
- All bridge dependencies resolved in `ldd`; real GPU graph probes passed.
- Controller ran 10:23:06–10:24:12 UTC with `--min-recall 0.95`.
- Default `--bridge-mode installed`; `bridge_override_set` is false.
- SQL GPU CREATE, vector-index query plan, restart ID/distance consistency,
  incremental insertion, deletion, and graceful shutdown all passed.

The status is `PASS_LOCAL_VALIDATION_TARGET_UNSPECIFIED`. It is a real-hardware
functional validation, not an official competition grade. Subsequent report-only
commits do not change the tested source or controller.

## Measured results

CPU and GPU use identical FP32 input, L2, HNSW M=16, construction/search ef=200,
K=10, and three alternating trials. Timing covers CREATE VECTOR INDEX through
the first successful ANN query, including deferred construction and retries.
Input generation, import and quality checks are outside that interval.

| Dataset | CPU median | GPU median | CPU/GPU speedup |
| --- | ---: | ---: | ---: |
| 200 × 16 | 0.610905 s | 0.711539 s | 0.858568× |
| 10000 × 128 | 2.725209 s | 1.021798 s | 2.667073× |

For 10000 × 128, CPU Recall@10 was 0.953 in all three trials; GPU recall was
0.963, 0.964 and 0.968. Every CPU/GPU SQL index snapshot retained all 10000 IDs
and byte-identical FP32 vectors. Small inputs remain slower on GPU; this result
is retained. The first CPU large-data trial took 6.587991 s and is also retained;
the result uses the median, not selective removal of the slow trial.

## Evidence and reproduction

Raw reports are in [results/20261101-delivery](results/20261101-delivery/).
They include the controller status, environment, both complete benchmark JSONs,
GPU probe, controller log, full build log, bridge dependencies and final phase.
Per-build GPU completion records and their stderr interval hashes are retained
in the JSONs. `SHA256SUMS` covers the exact downloaded bytes, protected against
checkout newline conversion. This directory does not contain the complete
database stderr file; its additional cloud archive download timed out. No
missing log is reconstructed or presented as an original file.

The local archive `amd-delivery-evidence-20261101.tar.gz` has SHA256
`92034eba380518b4e4d0696ec7d73fb65dc1ababa2603f2906b0ddc59571aae7`.

Follow [README.md](README.md#独立实例完整复现) to build on the official AMD workspace,
then run (use a new output directory and a free port):

```bash
python3 unittest/amd_hipvs/run_cloud_validation.py \
  --workspace /workspace --output /workspace/amd-delivery-validation \
  --port 2883 --min-recall 0.95
```

The ordinary packaged install layout is additionally covered by the Linux CMake
and dlopen integration tests. This run uses the ordinary build-tree binary, not
an installed RPM. All 77 Python regression tests previously passed; the real
cloud run above supplies the full compiler/linker and GPU/SQL evidence those
fixture tests cannot provide. No official evaluation was submitted in this run.
