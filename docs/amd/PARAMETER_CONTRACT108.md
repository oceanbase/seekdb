# AMD108 parameter contract (read-only audit)

2026-10-28. This note describes the existing candidate; no source modification,
recompilation or cloud operation was performed for this audit.

## Actual parameter flow

| Setting | GPU-created HNSW | Ordinary VSAG / CPU fallback |
|---|---|---|
| Vector values, count, dimension | Supplied by the actual index buffer to `build(base,n,dim)` | Supplied by the ordinary adaptor |
| M | `SEEKDB_HIPVS_HNSW_M`; graph degree must equal `2*M` | Actual SQL index metadata |
| ef_construction | `SEEKDB_HIPVS_EF_CONSTRUCTION` | Actual SQL index metadata |
| Query ef | Handle's saved `search_ef`, initialized from `SEEKDB_HIPVS_SEARCH_EF` | Actual query parameter, subject to existing VSAG cap |
| Restored query ef | Restored from snapshot header offset48, independent of current environment | Ordinary restored index/query contract |

The six preserved legacy ABI functions have no configuration argument.
`mark_cuvs_index(void *key)` also receives no SQL parameters. The GPU fast return
in `knn_search` occurs before the ordinary CPU `ef_search` processing. Therefore
DDL M/ef_construction/ef_search and session `ob_hnsw_ef_search` are **not forwarded**
to a GPU handle. No existing check rejects or reconciles a mismatch.

For topK10, the existing CPU adaptor clamps requested ef above1000 to1000.
Exactly1000 is unchanged (`if (ef_search > 1000)`). Consequently a GPU env4096
versus SQL/session4096 would actually compare GPU4096 against CPU1000.

## Precise published scope for the current measurement

> This optional backend builds a full HNSW index from actual dense float32 L2
> vectors via GPU CAGRA, and queries the resulting HNSW on the CPU. The measured
> deployment uses M64, ef_construction400 and effective query ef1000 for both
> backends. Its SQL DDL/session settings and process environment are explicitly
> synchronized; GPU graph/intermediate degrees are128/256 and NN-descent
> iterations20. Fresh-index logs, full input readback and complete query recall
> evidence identify the actual run. The current legacy bridge uses one process
> configuration profile; SQL parameters do not automatically reconfigure each
> GPU index or an already-built/restored GPU handle.

The measurement's quality gate applies to the complete recorded queries at the
recorded N/D/K and synchronized profile. Source accepts other N/D/M/configuration
values subject to validation; those configurations require their own full
quality validation and matching SQL/environment. A fixed dataset, a fixed
dimension, or a saved benchmark answer is not embedded in the bridge.

Ordinary `lib=vsag` behavior remains its original parameter path. The optional
GPU route is selected only for eligible dense L2 HNSW indexes without extra
information. Stateful continuation uses ordinary VSAG; unsupported query/filter
conditions can use CPU fallback. These routing checks do not enforce parameter
equality. Do not document this version as transparently honoring arbitrary
per-index or per-query GPU settings.

## Existing checks and the remaining generic interface gap

The bridge validates integer bounds, positive shape, graph/intermediate sizes
against N, graph_degree=`2*M`, explicit full hierarchy, output IDs/distances,
snapshot format/shape/length/checksum, and the six-symbol ABI. Caller eligibility
checks cover index type/metric/extra information. None compares SQL M/efc/ef to
the bridge's process settings.

A subsequent generic interface revision should carry immutable index build
parameters and per-query search ef through an additional versioned ABI, retaining
the legacy six functions for compatibility. A narrower alternative is explicit
parameter matching before opt-in, with a logged ordinary-CPU fallback on a
mismatch. Either requires new correctness/recall controls; neither is implemented
or claimed by the current running candidate. Do not mutate this candidate while
its three formal rounds are in progress.

## Reviewed source locations

- `validation/amd-build107/candidate/src/oblib/lib/vector/ob_cuvs_hooks.h`:
  marking accepts only the index pointer.
- Same candidate `ob_vsag_adaptor.cpp`: dynamic ABI declarations near48;
  build job near170; GPU search dispatch near249/1753; CPU ef cap near1770.
- Same candidate `ob_plugin_vector_index_adaptor.cpp`: GPU marking near1367,
  checking lib/type/metric/extra-info eligibility, with no parameter equality.
- `runs/amd107/hnsw_bridge/seekdb_cuvs_bridge.c`: env configuration near168;
  query handle ef near234; snapshot writes near386 and restores near448.

These locations are file-system-relative to `D:/ob2026_plan`, not the product
checkout; the unchanged performance product remains separate.
