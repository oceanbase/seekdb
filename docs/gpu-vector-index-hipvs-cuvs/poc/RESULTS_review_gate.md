# PR #1314 GPU Review Gate Results

## Environment

- Candidate tested: `45b71ea5a2d7c05635222171add37fb626e32fef`
- Container: `zihao_seekdb_build`
- GPU: AMD Radeon Graphics, `gfx1100`, `/dev/dri/renderD135`
- ROCm: `7.2.4`
- Build: `OB_BUILD_CUVS=ON`, `OB_BUILD_CUVS_TRACE=ON`
- TRACE observer SHA-256: `df23fb59529e57c04f2ac945457e28404ad9dc701d662e0b4e45699af3f4a07f`
- Bridge SHA-256: `d5d46ca0b03e19b9de9120beddbc143d48deb0de9824130eda6529dd47cf04f9`

The runner generates all vectors and ground truth from seed `1314`. It does not
use `/work/datasets`, `/work/bench`, or `OB_VSAG_USE_CUVS`.

## Command

```bash
LD_LIBRARY_PATH=/work/bridge:/opt/hipvs/lib:/opt/rocm/lib \
  docs/gpu-vector-index-hipvs-cuvs/poc/run_gpu_smoke.sh \
    --observer /work/review_gate/pr1314/build-on-trace/build_release/src/observer/seekdb \
    --bridge /work/bridge/libseekdb_cuvs_bridge.so \
    --base-dir /work/review_gate/pr1314/runtime/on-trace-run-4 \
    --port 2981 \
    --render /dev/dri/renderD135 \
    --evidence /work/review_gate/pr1314/evidence/06-gpu-smoke/run-4
```

Result: `runner_rc=0`.

## Cases

| Case | Result | Evidence |
| --- | --- | --- |
| `lib=cuvs` L2 route | PASS | First query emitted one `cuvs_build` and one `cuvs_serve`. |
| Same-process `lib=vsag` route | PASS | Query emitted zero cuVS trace markers. |
| cuVS cosine and inner product DDL | PASS | Both rejected with SQLSTATE `0A000` and `lib=cuvs only supports distance=l2`. |
| Freshness below rebuild threshold | PASS | At 400 rows, `cuvs_serve=0`; approximate result matched exact. |
| Freshness rebuild threshold | PASS | At 700 rows, one `cuvs_build` and one `cuvs_serve`. |
| Filter fallback | PASS | `cuvs_serve=0`; approximate result matched exact. |
| Delete fallback | PASS | `cuvs_serve=0`; approximate result matched exact and excluded the deleted row. |
| `dbms_vector.batch_knn` | PASS | One `cuvs_raw_batch`, 100 probes x top-10 = 1,000 rows, recall@10 `0.9910`. |
| GPU file descriptors | PASS | Observer held `/dev/kfd` and `/dev/dri/renderD135`. |
| GPU unavailable fallback | PASS | With HIP/ROCR devices hidden, `cuvs_serve=0`; approximate matched exact and observer stayed alive. |
| Cleanup | PASS | Main and no-GPU observers stopped; ports 2981 and 2991 closed. |

Aggregate main trace counts were `cuvs_build=2`, `cuvs_serve=2`, and
`cuvs_raw_batch=1`. The no-GPU trace contained zero cuVS markers.

## Evidence

Raw evidence is kept outside Git at:

```text
/work/review_gate/pr1314/evidence/06-gpu-smoke/
```

Key files are `run-4.log`, `run-4.status`, `run-4/summary.json`,
`run-4/cases.tsv`, `run-4/obvsag-trace.log`, `run-4/no-gpu-trace.log`,
`run-4/gpu-fds.txt`, and `run-4/batch-output.tsv`.

This gate covers one `gfx1100` device and a fresh single-node seekdb instance.
It does not replace multi-GPU, long-duration, snapshot-reload, or upstream CI
coverage.
