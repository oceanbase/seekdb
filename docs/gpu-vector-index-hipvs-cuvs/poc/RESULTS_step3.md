# L2-B Step3 — runtime end-to-end at the seekdb vector-adaptor layer

Harness `bench/seekdb_cuvs_harness.cpp` drives seekdb's REAL adaptor
`oceanbase::common::obvsag` (create_index -> build_index -> knn_search ->
delete_index), linked against the actual `liboblib.a` via the observer's own
`link.txt` recipe. Same dataset (10000x128, 100 queries, topk=10), same code,
at this pre-hardening stage, the backend was toggled by the now-removed
`OB_VSAG_USE_CUVS` environment variable. Current builds use `lib=cuvs` per-index marking.

| run | backend | GPU | recall@10 |
|-----|---------|-----|-----------|
| flag OFF          | VSAG HNSW (CPU)      | no  | 0.9260 |
| flag ON + GPU     | cuVS CAGRA via hipVS | yes | 0.8790 |
| flag ON, GPU hidden (HIP_VISIBLE_DEVICES=-1) | cuVS build fails -> falls back to VSAG | no | 0.9260 |

GPU proof (rocm-smi, GPU7/renderD135, sampled during looped cuVS runs):
- VRAM Used: baseline 29 MB -> 829 MB / 587 MB during CAGRA build+search, back to 29 MB after.
- GPU use: 0% idle -> 18% / 22% / 12% / 9% during runs.
- HIP_VISIBLE_DEVICES=-1: `[seekdb_cuvs] build failed` on stderr, recall reverts to the VSAG number 0.9260.

Interpretation: seekdb's real vector-index adaptor executes vector search on the
AMD gfx1100 GPU through hipVS libcuvs_c when enabled, exercised through the exact
C-API the SQL/storage layer calls, with correct recall and graceful CPU fallback.
Not covered here: bootstrapping a full OceanBase observer + SQL parser/storage
(the layer that *calls* obvsag) — a separate, larger effort.
