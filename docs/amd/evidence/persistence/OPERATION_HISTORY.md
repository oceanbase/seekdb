# Recorded recovery and persistence replay

This is a same-cloud-instance observer recovery using the existing database at
`/workspace/amd107/candidate-quality64`. The original formal process PID was
14001; the recovered observer PID is 33736. It uses the same saved argv,
explicit saved settings, observer SHA256 and bridge SHA256. The recovery receipt
records no active old threads, no port2882 listener or connection, and SQL ready.

The operator reported this actual sequence:

1. The original guarded `restart_persisted.py` sent SIGTERM to its verified owned
   observer, then refused a replacement while port2882 remained occupied.
   The initial refusal diagnostic is not in this export, so that first step is
   operator history rather than an independently checked original diagnostic.
2. The separate `recover_persisted.py` observed the old process fully stopped
   (zombie leader only) and port2882 free. At 2026-10-28T10:36:54.324084+00:00 it
   started the saved executable/argv against the existing database path. It sent
   no signals and performed no cloud restart, database deletion/creation or DDL.
3. `replay_existing.py` ran at 10:36:56.519507–10:37:25.002870 UTC against the
   original final CPU and GPU round2 indexes (`cpu_r02`/`ix_cpu_r02`,
   `gpu_r02`/`ix_gpu_r02`). Both tables retained100000 rows, IDs0–99999, and both
   plans used their original VECTOR INDEX SCAN. Each complete100-query replay
   achieved1000/1000 Recall@10 against the unchanged original100k formal GT.

The read-only independent review checked every one of the871 exported originals
by SHA/size and all205 replay SQL/event/session/row receipts. Raw SQL contains
SELECT/EXPLAIN only (the connection init sets ef_search1000); it contains no
index rebuild or data writes. Reconstructed query-vector text has the original
GT-manifest SHA256, and all200 prediction lists match their raw SQL row outputs.

## Exact limits of the receipt

- Saved settings were overlaid on the current shell environment. The complete
  original process environment was not captured/reused and is not claimed.
- Existing directory identity was checked during recovery. Historical inode
  continuity from before the original shutdown is not proven or claimed.
- The replay script alone does not prove a restart. Its successful raw report
  retains `restart_verified_by_this_script:false`; this directory separately
  includes the actual launch/recovery receipt and the independent combined review.
- No new construction timing is inferred from this replay. The formal build
  measurements remain the12 fresh pre-recovery rounds in evidence/formal.
- The complete formal bulk archive remains externally downloaded/verified by
  the operator separately; this persistence review does not verify that archive.

`originals/` retains all837 replay originals plus the original and recovered
launch receipts and recovered stdout:840 selected original files. Retaining all
205 SQL/session/rows/stderr groups permits full query/GT audit without relying
only on aggregate recall; unrelated exploratory smoke receipts are not copied.
The complete871-entry export manifest is retained separately. `small-raw-index.json`
records every copied original; `persistence-review108.json` records the independent
outcome and `review_persistence108.py` records its read-only verification method.
