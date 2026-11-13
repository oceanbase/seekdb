# Incremental HNSW checkpointing

Status: design proposal for [#2206](https://github.com/oceanbase/seekdb/issues/2206).

This page describes how seekdb can persist the completed incremental HNSW graph so
that a restart does not rebuild every vector added since the last full snapshot, and
so that an online snapshot replacement does not leave the serving adapter without an
incremental graph.

The proposal is deliberately scoped to the storage format, the write path, the
recovery path, and the scheduling/observability surface. It does not change the
on-disk format of the full snapshot, and it does not change the SQL surface.

## Current behaviour

The vector index adapter keeps two in-memory graph instances:

| Member | Owner | Contents |
| --- | --- | --- |
| `snap_data_` | `ObPluginVectorIndexAdaptor` | HNSW graph deserialized from the snapshot table |
| `incr_data_` | `ObPluginVectorIndexAdaptor` | HNSW graph built from rows appended after the snapshot |

`ObPluginVectorIndexAdaptor::serialize()`
(`src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp:3156`) writes only
`snap_data_->index_` through `ObVectorIndexSerializer`. Nothing else is persisted:

- `incr_data_->index_` — the incremental graph itself
- `incr_data_->bitmap_->insert_bitmap_` / `delete_bitmap_` — roaring64 visibility state
- `incr_data_->vid_bound_` — the VID range covered by the graph
- `incr_data_->scn_`, `last_dml_scn_`, `last_read_scn_` — the watermarks that decide
  which rows a subsequent scan may re-read

Recovery therefore has to replay rows from the index-id table. The replay happens in
the first query that touches the adapter, through
`check_index_id_table_readnext_status()` /
`complete_index_mem_data_incremental()`
(`ob_plugin_vector_index_adaptor.cpp:2324` and `:2773`), which rebuilds the
incremental graph in the foreground. That is the stall reported in the issue.

Online replacement has a second gap. `try_sync_snapshot_memdata()`
(`src/observer/vector_index/ob_plugin_vector_index_utils.cpp:636`) constructs a new
adapter with `copy_meta_info()` and deserializes the full snapshot into it, but
`copy_meta_info()` (`ob_plugin_vector_index_adaptor.cpp:1927`) copies ids, types and
HNSW parameters only. The incremental graph is not transferred and is not rebuilt in
the background, so the new adapter rebuilds it from the index-id table on its first
query.

### The invariant a checkpoint must preserve

`insert_rows()` (`ob_plugin_vector_index_adaptor.cpp:1361`) already establishes the
invariant a checkpoint needs, in this order:

1. add the vectors to `incr_data_->index_`
2. take `incr_data_->bitmap_rwlock_` and add/remove the affected VIDs in the
   insert/delete roaring bitmaps
3. advance `vid_bound_`

A checkpoint taken between (2) and (3) is not a checkpoint. The design below takes
the graph and the bitmaps under one ordered pair of locks and records the watermark
inside the same critical section.

## Proposed format

A checkpoint is a self-describing envelope stored as one row in a dedicated inner
table, keyed by the index identity and the checkpoint generation:

```
key:   <tablet_id>_<index_id>_<generation>
value: header | snap_binding | graph | visibility | footer
```

Header fields:

| Field | Purpose |
| --- | --- |
| `magic`, `format_version` | reject unknown payloads instead of interpreting them |
| `generation` | monotonic counter, incremented on every checkpoint and on every snapshot replacement |
| `snap_scn` | SCN of the full snapshot this checkpoint extends |
| `snap_checksum` | checksum of the full snapshot the checkpoint was built against |
| `coverage_scn` | replay watermark: rows at or below this SCN are represented by the checkpoint |
| `vid_lower`, `vid_upper` | VID range covered by the graph |
| `graph_len`, `visible_len`, `footer_len` | section offsets, so a partial write is detectable |

Sections:

- `graph` — the serialized `incr_data_->index_`, produced by the existing
  `ObVectorIndexSerializer`. The payload is opaque to this design; the serializer
  already supports both flat and SQ variants.
- `visibility` — the insert and delete bitmaps, serialized with
  `roaring64_bitmap_portable_serialize()`. The bundled roaring version exposes the
  portable API (`deps/3rd/vcpkg/x64-windows/include/roaring/roaring64map.hh`), so the
  payload is endian-stable and forward-compatible.
- `footer` — CRC32 of every preceding byte plus the total length. A row whose footer
  does not verify is ignored, not partially applied.

`coverage_scn` is the field that makes replay safe: a checkpoint claims coverage up
to an SCN, and recovery replays strictly above it. Because `insert_rows()` advances
the bitmaps and `vid_bound_` inside one critical section, a checkpoint cannot claim
an SCN for a row whose bitmap entry was not written.

### Why an envelope and not a second serializer call

Extending `serialize()` to also write `incr_data_->index_` would produce a stream
with no binding between the graph and the snapshot it extends. After a snapshot
replacement the reader would have no way to tell which `incr_data_` belongs to which
`snap_data_`, and no way to detect a checkpoint written against a superseded
generation. The explicit binding and checksum cost a few dozen bytes per checkpoint
and remove that whole class of failure.

## Write path

A checkpoint is produced by a background task, never by the foreground insert path.
The sequence is:

1. Take `incr_data_->mem_data_rwlock_` for read and `bitmap_rwlock_` for read, in
   that order — the same order used by `insert_rows()`.
2. Snapshot `scn_`, `vid_bound_`, and the two bitmap handles.
3. Serialize the graph and the bitmaps into a staging buffer outside the locks.
   The graph serializer already reads under the caller's lock, so the buffer is
   built from a stable generation.
4. Compute the footer checksum over the staged bytes.
5. Publish the row.

Steps 1-3 must not hold the write lock across the serialize call: a full incremental
graph serialize can take hundreds of milliseconds, and holding `mem_data_rwlock_`
for that long would stall every insert. The lock is therefore held only long enough
to capture the watermarks and to guarantee that no concurrent mutation can advance
them while the copy is being taken. Concurrent inserts during step 3 are allowed and
land above `coverage_scn`, which is exactly what the tail replay is for.

Publication must be atomic with respect to readers. A reader must never observe a
checkpoint row whose payload is incomplete. The row is written through the existing
LOB/transaction path used by snapshot serialization; the reader treats a row as
present only when the footer verifies, and retention keeps the previous generation
until the new one is readable.

Retention rule: keep the current generation and one predecessor. A checkpoint that
was published but never observed can then still be superseded, and a reader that
started before the switch can still finish.

## Recovery path

On adapter load:

1. Read the newest checkpoint row whose `snap_scn` and `snap_checksum` match the
   full snapshot that was just deserialized. A mismatch means the checkpoint
   belongs to a different snapshot; skip it and fall back to replay.
2. Verify the footer. On failure, log at `WARN` with the generation and the expected
   length, then fall back to replay.
3. Deserialize the graph directly into `incr_data_->index_`, then deserialize the
   two bitmaps and restore `vid_bound_` and the watermarks.
4. Set the replay start to `coverage_scn + 1` and let
   `complete_index_mem_data_incremental()` handle only the tail.

The replay entry point does not change. What changes is its input: instead of
starting from the snapshot SCN and rebuilding every appended row, it starts from the
restored watermark. Rows at or below the watermark are already in the graph and in
the bitmaps.

Instrumentation for the acceptance criteria is a single log line per load:

```
[vec index] checkpoint restored: generation=<n> coverage_scn=<scn> graph_rows=<n> tail_rows=<n> restored_bytes=<n>
```

`tail_rows` is the count the replay actually consumed. A restart after a completed
checkpoint must show `tail_rows` equal to the rows written after the checkpoint, not
the total row count.

## Online replacement

Replacement today builds a new adapter, copies metadata, and drops the incremental
graph. Two options were considered.

**Option A — catch up in the background before the switch.** The old adapter keeps
serving. A new adapter is built from the full snapshot, then fed the incremental rows
in `(coverage_scn, switch_scn]` by a background task. The switch happens when the new
adapter's coverage reaches the switch SCN. Queries during the catch-up are served by
the old adapter and are unaffected. This requires a way to enumerate index-id rows
in SCN order, which the index-id table already provides.

**Option B — generation handoff.** Freeze the current incremental generation at the
rebuild boundary, write later inserts into a new generation, and let the new adapter
adopt the frozen generation's checkpoint plus its own tail. This avoids re-reading
the index-id table twice but requires two live incremental graphs for the duration of
the rebuild.

Option A is proposed first because it reuses the existing SCN-ordered scan and keeps
one serving graph. Option B remains available if measurement shows the catch-up scan
dominates.

Either way the switch must be coordinated: the new adapter publishes its checkpoint
before it starts serving, and the old adapter's generation is retired only after the
switch completes, so an interrupted replacement can be retried from the last good
checkpoint.

## Scheduling

Checkpoint scheduling is independent of the full-optimization ratio. A checkpoint is
attempted when either condition holds:

- pending rows since `coverage_scn` reach `vector_index_graph_checkpoint_min_rows`
  (default: a value large enough that the serialize cost is amortized — the exact
  default should be chosen from the measurements below, not guessed)
- the age of the oldest uncovered row reaches `vector_index_graph_checkpoint_max_age`

Both settings are session-variable-style parameters, so they can be tuned per
instance and observed through the existing parameter views. Setting both to zero
disables background checkpointing and preserves today's behaviour.

Resource admission: before starting, the task checks the memory that the old and new
graph will occupy simultaneously. The check must use the same admission path as
snapshot serialization; an underestimated check that lets the task start and then
fails mid-serialize is worse than deferring, because it leaves a partial row and
wastes the serialize cost.

Deferral is not failure. When admission is denied the task records a deferred
attempt with its reason and returns without advancing the watermark, so the next
scheduling pass retries.

An explicit flush is exposed for planned shutdowns. It waits for an in-progress
checkpoint to finish, requests one if the pending set is non-empty, and reports
whether the requested coverage was reached. It is a best-effort operation: a
shutdown that times out must still exit, and the next start must fall back to replay.

## Observability

The following are exported per index:

| Metric / column | Meaning |
| --- | --- |
| `checkpoint_generation` | last successfully published generation |
| `checkpoint_coverage_scn` | watermark carried by that generation |
| `checkpoint_pending_rows` | rows above the watermark |
| `checkpoint_oldest_pending_age` | age of the oldest uncovered row |
| `checkpoint_last_attempt_result` | success / deferred (with reason) / failed (with code) |
| `checkpoint_restored_tail_rows` | rows the last load had to rebuild |

A resource deferral or a failure must be visible here. Silently appearing up to date
is the failure mode the issue calls out, and it is the one that is hardest to notice
from the outside.

## Locking

`insert_rows()` advances the graph and then the bitmaps. Any checkpoint that reads
both must follow the same order, otherwise a checkpoint can observe a bitmap entry
for a row the graph does not contain, which produces a graph that is missing a
neighbour but claims the row is visible.

The existing code does not have a single documented order. `insert_rows()`
(`ob_plugin_vector_index_adaptor.cpp:1509-1543`) populates the graph and only then
takes the bitmap lock, while `write_into_delta_mem()`
(`:2142-2185`) takes `mem_data_rwlock_` around a block that also touches bitmap
state. This proposal does not require reordering those paths, but it does require
that both are audited and that the order is stated once, in the adapter header,
before checkpointing is enabled by default. A comment that says "graph, then bitmap"
in one place is worth more than an implicit convention spread over two call sites.

`query_lock_` is not part of the checkpoint contract. Immediate-mode DML records
visibility through the bitmap path without taking it, so a checkpoint that relied on
`query_lock_` for exclusion would miss exactly the concurrent-write case the issue
requires.

## Naming

`checkpoint` already means something else in this subtree:
`ObPluginVectorIndexLoadScheduler` implements `logservice::ObICheckpointSubHandler`
(`src/observer/vector_index/ob_plugin_vector_index_scheduler.h:219`) for the
log-service checkpoint of the scheduler's own state. To keep the two apart in code,
logs and parameters, the persisted incremental graph is called a **graph
checkpoint**, and the parameters are named `vector_index_graph_checkpoint_*`.

## Validation plan

The acceptance criteria in the issue are measurable, and the plan should map to them
one-to-one:

1. **Checkpoint reuse.** Insert N rows, complete one checkpoint, restart, run one ANN
   query. Assert the load log reports `tail_rows` equal to the rows written after the
   checkpoint, and that the first query does not rebuild the checkpointed set. Compare
   first-query latency against the same fixture with checkpointing disabled.
2. **Age-based scheduling.** Keep the pending set below the ratio threshold and above
   `vector_index_graph_checkpoint_max_age`; assert a checkpoint happens and that its
   generation advances without a full optimization.
3. **Resource deferral.** Constrain the budget so admission fails; assert the deferral
   is recorded with a reason and that a later attempt with a larger budget succeeds.
4. **Graceful shutdown.** Request a flush, shut down, restart, and verify that
   committed insert, update and delete visibility is preserved in both
   `sync_mode=immediate` and asynchronous modes.
5. **Replacement.** Write concurrently with a full optimization; assert the new
   adapter's first query does not reconstruct a graph that the old adapter had
   already built, and that no transaction visibility is lost across the switch.
6. **Measurements.** Report first-query latency, online query latency distribution,
   checkpoint duration, and peak memory across at least two data sizes and two memory
   budgets. The existing numbers in the issue (142.99 s first query after restart;
   3.81 s and 2.80 s replacement stalls) are the baseline to beat.

Correctness must hold for runs where the checkpoint is interrupted at every stage:
before publish, after publish but before the reader observes it, and after the reader
observes it but before the previous generation is retired.

## Implementation phases

1. Envelope format, serialize and verify, plus the restore path. Checkpointing stays
   off by default; the only behaviour change is that a valid checkpoint is restored
   when present.
2. Scheduling on size and age, plus the observability surface and the flush entry
   point.
3. Replacement catch-up (Option A), gated behind its own switch.
4. Enable by default after the measurements above are collected on the CI fixtures.

Phase 1 is the smallest change that demonstrates the win. Phases 2-3 are what make it
usable without operator intervention. The measurements in phase 4 are what decide
whether the default thresholds proposed in phase 2 were right.
