# PR #1314 GPU Review Gate Results

## Environment

- Runtime source tested: `8e6680bca4068fbc00230a874df4ff6bbdecfc99`
- Container: `zihao_seekdb_build`
- GPU: AMD Radeon Graphics, `gfx1100`, `/dev/dri/renderD135`
- ROCm: `7.2.4`
- Build: `OB_BUILD_CUVS=ON`, `OB_BUILD_CUVS_TRACE=ON`
- TRACE observer SHA-256: `35379b44b888968d89190f5e6b0ee13b51991d25a63f35c5abb28db3a14a18de`
- Bridge SHA-256: `d5d46ca0b03e19b9de9120beddbc143d48deb0de9824130eda6529dd47cf04f9`

The runner generates all vectors and ground truth from seed `1314`. It does not
use `/work/datasets`, `/work/bench`, or `OB_VSAG_USE_CUVS`.

## Command

```bash
LD_LIBRARY_PATH=/work/bridge:/opt/hipvs/lib:/opt/rocm/lib \
  docs/gpu-vector-index-hipvs-cuvs/validation/run_gpu_smoke.sh \
    --observer /work/review_gate/pr1314/build-on-trace/build_release/src/observer/seekdb \
    --bridge /work/bridge/libseekdb_cuvs_bridge.so \
    --base-dir /work/review_gate/pr1314/runtime/on-trace-final \
    --port 2981 \
    --render /dev/dri/renderD135 \
    --evidence /work/review_gate/pr1314/evidence/06-gpu-smoke/final/run-5
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
| `dbms_vector.batch_knn` | PASS | One `cuvs_raw_batch`, 100 probes x top-10 = 1,000 rows, recall@10 `0.9930`. |
| GPU file descriptors | PASS | Observer held `/dev/kfd` and `/dev/dri/renderD135`. |
| GPU unavailable fallback | PASS | With HIP/ROCR devices hidden, `cuvs_serve=0`; approximate matched exact and observer stayed alive. |
| Cleanup | PASS | Main and no-GPU observers stopped; the runner verified ports 2981 and 2991 closed. |

Aggregate main trace counts were `cuvs_build=2`, `cuvs_serve=2`, and
`cuvs_raw_batch=1`. The no-GPU trace contained zero cuVS markers.

## Evidence

Raw evidence is kept outside Git at:

```text
/work/review_gate/pr1314/evidence/06-gpu-smoke/
```

Key files are `final/run-5.log`, `final/run-5.status`, `final/run-5/summary.json`,
`final/run-5/cases.tsv`, `final/run-5/obvsag-trace.log`,
`final/run-5/no-gpu-trace.log`, `final/run-5/gpu-fds.txt`, and
`final/run-5/batch-output.tsv`.

This gate covers one `gfx1100` device and a fresh single-node seekdb instance.
It does not replace multi-GPU, long-duration, snapshot-reload, or upstream CI
coverage.
