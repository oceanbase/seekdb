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

The vector index adapter keeps two in-memory graphs and one visibility view:

| Member | Owner | Contents |
| --- | --- | --- |
| `snap_data_` | `ObPluginVectorIndexAdaptor` | HNSW graph deserialized from the snapshot table |
| `incr_data_` | `ObPluginVectorIndexAdaptor` | HNSW graph built from rows appended after the snapshot |
| `vbitmap_data_` | `ObPluginVectorIndexAdaptor` | committed visibility baseline for those appended rows |

`ObPluginVectorIndexAdaptor::serialize()`
(`src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp:3156`) writes only
`snap_data_->index_` through `ObVectorIndexSerializer`. Nothing else is persisted:

- `incr_data_->index_` — the incremental graph itself
- `incr_data_->bitmap_->insert_bitmap_` — membership of the incremental graph, that
  is, which VIDs the graph is supposed to contain. `delete_bitmap_` is never
  allocated for `incr_data_`: `init_mem_data(VIRT_INC)` leaves it `nullptr`
  (`ob_plugin_vector_index_adaptor.cpp:809`), and a delete is recorded by removing
  the VID from `insert_bitmap_` (`insert_rows()`, `:1539`)
- `incr_data_->vid_bound_` — the VID range covered by the graph
- `incr_data_->last_dml_scn_`, `last_read_scn_` — the watermarks that decide which
  rows a subsequent scan may re-read, and whether the index-id scan may be skipped
  at all (`is_pruned_read_index_id()`, `:1126`)
- `vbitmap_data_->bitmap_->insert_bitmap_`, `vbitmap_data_->bitmap_->delete_bitmap_`
  and `vbitmap_data_->scn_` — the committed visibility view: which appended rows are
  visible, which are tombstoned, and the SCN that view is valid at.
  `init_mem_data(VIRT_BITMAP)` creates both bitmaps (`:830-835`). Unlike the
  incremental graph, this view is not advanced by the insert path; it is written as
  a side effect of replay in `complete_index_mem_data_incremental()`
  (`:2944-2951`). Queries consult it to decide whether the index-id scan can be
  skipped entirely (`check_index_id_table_readnext_status()`, `:2359`)

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
HNSW parameters only. `inherit_index_id_watermarks_from()` (`:1966`) carries
`last_dml_scn_` and `last_read_scn_` to the new adapter, which is why the new
adapter's first query can skip forward correctly, but the graph and the visibility
baseline are still not transferred: neither `incr_data_`'s bitmaps nor
`vbitmap_data_`'s bitmaps and `scn_` move with the watermarks. The incremental graph
and its baseline are therefore rebuilt from the index-id table on the first query.

### What the current write path does not give a checkpoint

`insert_rows()` (`ob_plugin_vector_index_adaptor.cpp:1361`) mutates two objects, in
this order:

1. `obvectorutil::add_index(incr_data_->index_, ...)` — the graph itself
   (`:1512`, `:1522`)
2. take `incr_data_->bitmap_rwlock_`, update `insert_bitmap_`, then
   `set_vid_bound()` (`:1533-1543`)

Two consequences matter, and neither is visible from the insert path alone:

- `insert_rows()` never takes `incr_data_->mem_data_rwlock_`. A checkpoint that
  takes `mem_data_rwlock_` for read plus `bitmap_rwlock_` for read therefore does
  not exclude this writer. The graph pointer stays reachable and `add_index()`
  keeps mutating it while the checkpoint believes it holds a stable view.
- Copying `roaring64_bitmap_t *` handles out of `bitmap_` copies pointers, not
  contents. Both bitmaps remain mutable after the lock is released, so a later
  pass over those handles is not reading the state that was captured.

`ObVectorIndexSerializer::serialize()` offers no snapshot hook to fall back on: it
takes a raw `void *index`
(`src/query/api/query/vector/ob_vector_index_serialize.h:198`) and hands it
straight to `obvectorutil::fserialize()`
(`src/observer/vector_index/ob_plugin_vector_index_serialize.cpp:196`). That is
acceptable for `snap_data_->index_`, because the snapshot index is immutable after
load and is replaced, not mutated, by `renew_single_snap_index()`
(`ob_plugin_vector_index_adaptor.cpp:3176`). `incr_data_->index_` is mutated in
place, so the same call on the same object is not a snapshot.

A checkpoint that stages bytes from the live graph and the live bitmaps can
therefore mix two generations. The footer will still verify and the payload will
still parse, while the bitmap claims a VID the graph never received, or omits one
it did.

**The footer checksum proves byte integrity only. It says nothing about logical
consistency.** Consistency comes from the capture protocol in "Checkpoint snapshot
contract", not from the CRC.

## Proposed format

A checkpoint is a self-describing envelope stored as one row in a dedicated inner
table, keyed by the index identity and the checkpoint generation:

```
key:   <tablet_id>_<index_id>_<generation>
value: header | snap_binding | graph | membership | visibility | footer
```

Header fields:

| Field | Purpose |
| --- | --- |
| `magic`, `format_version` | reject unknown payloads instead of interpreting them |
| `generation` | monotonic counter, incremented on every checkpoint and on every snapshot replacement |
| `snap_scn` | SCN of the full snapshot this checkpoint extends |
| `snap_checksum` | checksum of the full snapshot the checkpoint was built against |
| `coverage_scn` | the single boundary of this payload: rows at or below this SCN are represented by the checkpoint. Duplicated inside `visibility` so a torn capture is detectable |
| `graph_dml_scn` | `incr_data_->last_dml_scn_` at capture; must not exceed `coverage_scn` |
| `vid_lower`, `vid_upper` | VID range covered by the graph |
| `graph_len`, `membership_len`, `visibility_len`, `footer_len` | section offsets, so a partial write is detectable |

Sections:

- `graph` — the serialized `incr_data_->index_`, produced by the existing
  `ObVectorIndexSerializer`. The payload is opaque to this design; the serializer
  already supports both flat and SQ variants.
- `membership` — `incr_data_->bitmap_->insert_bitmap_`, serialized with
  `roaring64_bitmap_portable_serialize()`. The incremental delete bitmap does not
  exist (`init_mem_data(VIRT_INC)` leaves it `nullptr` at `:809`), so the section
  records that explicitly as an empty marker instead of serializing a null pointer.
  A reader that finds a non-empty delete section in `membership` rejects the row.
- `visibility` — `vbitmap_data_->bitmap_->insert_bitmap_`,
  `vbitmap_data_->bitmap_->delete_bitmap_` and `vbitmap_data_->scn_`, serialized
  the same way. The SCN is written here as well as in the header; the two copies
  must agree.
- `footer` — CRC32 of every preceding byte plus the total length. A row whose
  footer does not verify is ignored, not partially applied.

The bundled roaring version exposes the portable serialization API
(`roaring64_bitmap_portable_serialize()`), so both bitmap sections are
endian-stable and forward-compatible.

### Coverage boundary

`coverage_scn` is the boundary the whole payload is stated against: every committed
row with commit SCN at or below `coverage_scn` must be present in `graph`, present
in `membership`, and resolved as visible or tombstoned by `visibility`. Replay
starts strictly above it.

One boundary has to cover three sections because three different code paths
advance them:

| State | Advanced by | Lock held |
| --- | --- | --- |
| `incr_data_->index_` | `insert_rows()` (`:1512`, `:1522`), `write_into_delta_mem()` (`:2160`, `:2173`) | bitmap lock only, and taken afterwards |
| `incr_data_->bitmap_->insert_bitmap_` | `insert_rows()` (`:1533`), `update_incr_bitmap()` (`:3011`), `write_into_delta_mem()` (`:2179`) | `bitmap_rwlock_` |
| `vbitmap_data_->bitmap_`, `vbitmap_data_->scn_` | replay in `complete_index_mem_data_incremental()` (`:2849-2856`, `:2944-2951`) and query paths | `vbitmap_data_`'s own lock pair |

`vbitmap_data_->scn_` is advanced by replay, not by `insert_rows()`, so no single
existing critical section advances both views. The capture protocol therefore
*establishes* the boundary instead of inheriting it:

1. Capture both views under the mutation-exclusion scheme described in "Checkpoint
   snapshot contract", and read `vbitmap_data_->scn_` and
   `incr_data_->last_dml_scn_` inside the same critical section.
2. Set `coverage_scn = vbitmap_data_->scn_`. The visibility baseline is what was
   actually replayed, so it is the boundary the payload can honestly claim.
3. Require `incr_data_->last_dml_scn_ <= coverage_scn`, and require the captured
   membership set to be a subset of the union of
   `vbitmap_data_->bitmap_->insert_bitmap_` and
   `vbitmap_data_->bitmap_->delete_bitmap_`.
   Membership is added only after the graph insert (`:1512` then `:1533`), so
   membership is always a subset of graph content, never the other way round. The
   reverse check — membership against the visibility view — is what detects a
   graph that ran ahead of its baseline.
4. If any check fails, do not publish. Record the attempt as deferred with its
   reason and leave `coverage_scn` where it was. An under-eager boundary only costs
   replay; an over-eager one is a correctness bug.

The relation in step 3 is not new: it is the same `base_scn >= last_dml_scn_`
condition `complete_index_mem_data_incremental()` already uses to decide whether the
index-id scan can be omitted (`:2866-2869`). The running system already relies on the
consequence of that condition — that the incremental graph has been filled up to
`last_dml_scn_` — because the skip path also skips the delta check that would
otherwise repair a missing vertex. A checkpoint may therefore only claim a boundary at
which the existing replay would itself skip the prefix; claiming a larger boundary
would assert coverage the replay code does not consider covered.

### Why an envelope and not a second serializer call

Extending `serialize()` to also write `incr_data_->index_` would produce a stream
with no binding between the graph and the snapshot it extends. After a snapshot
replacement the reader would have no way to tell which `incr_data_` belongs to which
`snap_data_`, and no way to detect a checkpoint written against a superseded
generation. The explicit binding and checksum cost a few dozen bytes per checkpoint
and remove that whole class of failure.

## Checkpoint snapshot contract

A checkpoint is only meaningful if its payload is one generation. Because the
serializer reads whatever the pointer it is handed currently contains, the
generation must exist before the serializer is called. Two designs satisfy that;
phase 1 uses the first.

### Option 1 — exclusion until the bytes exist (phase 1)

One mutation gate on the adapter, held for write by the checkpoint task from
capture until the graph bytes and the bitmap copies are complete. Every mutator
takes the same gate for read, including `insert_rows()`, which today holds neither
`mem_data_rwlock_` nor anything else around its `add_index()` call.

Writer participation is therefore part of the contract, not an optimisation:

- `insert_rows()` (`ob_plugin_vector_index_adaptor.cpp:1361`) must hold the gate for
  read across its whole mutation, `:1509-1544`. Today it takes only
  `bitmap_rwlock_`, and only at `:1533`, after the graph has already changed.
- `write_into_delta_mem()` (`:2130`) must hold it across `:2145-2186`.
- `update_incr_bitmap()` (`:3001`) must hold it across `:3011-3015`.
- The replay writer in `complete_index_mem_data_incremental()` must hold it across
  its `incr_data_` mutation as well as `vbitmap_data_`'s lock pair at `:2944-2951`.

Writers that arrive while the checkpoint holds the gate **block**; they are neither
rejected nor moved to another generation. No row is lost and none is applied twice: a
writer serialized before the capture has its mutation inside the payload, and a writer
serialized after it has its row above the boundary, where the tail replay picks it up.
The gate is also what prevents the half-applied case — a graph vertex without its
membership bit, or vice versa — because neither can be observed between the graph
mutation and the bitmap update. Nothing is queued, so there is no second graph to
serve queries from.

Blocking is bounded by admission. The hold time is the graph serialize plus the
bitmap copies, which can reach hundreds of milliseconds on a large incremental
graph, so the task must not start at all when the projected hold exceeds the
configured stall budget. Refusing to start and recording a deferred attempt is the
degraded behaviour; starting and stalling every insert for an unbounded time is
not.

### Option 2 — immutable generation (measured alternative)

If measurements show the exclusion hold is unacceptable, the generation has to be
frozen instead: the checkpoint serializes a generation that no writer can reach.

That requires a genuine copy-on-write or frozen read-only copy of the graph, not a
copied handle and not a second pointer to the live object. Writers then participate
as follows: the mutation that would have landed in the frozen generation is applied
to the *next* generation, which starts at `coverage_scn` with the frozen generation
still serving queries; queries merge both until the frozen generation is retired.
`merge_and_generate_bitmap()` (`:3214`) already merges candidate sets from two
mem-data objects, so the serving half of this is not new machinery, but it does add
a third candidate source and is out of scope for phase 1. Any writer that cannot be
redirected to the next generation must wait for the snapshot to complete, which
makes this option at least as strict as Option 1.

### The footer does not substitute for either option

The footer CRC32 catches truncation and bit rot in the staged bytes. It cannot
catch a payload assembled from two generations, because such a payload is well
formed. Do not treat a verifying checksum as evidence of a consistent checkpoint.

## Write path

A checkpoint is produced by a background task, never by the foreground insert path.
Under the Option 1 contract the sequence is:

1. Take the mutation gate for write, then `incr_data_->bitmap_rwlock_` for write,
   then `vbitmap_data_`'s lock pair.
2. Under those locks, read `incr_data_->last_dml_scn_` and `vbitmap_data_->scn_` and
   run the boundary checks in "Coverage boundary". If a check fails, release,
   record a deferred attempt with its reason, and publish nothing.
3. Still under the gate, copy the membership bitmap and both visibility bitmaps with
   `roaring64_bitmap_copy()`. The copies are required: the handles are not a snapshot,
   and the bitmaps keep mutating.
4. Still under the gate, serialize `incr_data_->index_` into the staging buffer,
   then append `vid_bound_` and the watermarks.
5. Release the gate. Compute the footer over the staged bytes and publish.

Steps 1-4 are inside one critical section, so the graph, the membership bitmap and
the visibility bitmaps in the payload all come from the same generation. The hold is
the serialize plus the bitmap copies; that is the price of the contract, and it is
why admission refuses the attempt instead of letting it start and then stall every
insert.

Concurrent inserts are not "allowed to land above `coverage_scn`" as an unstated
side effect — they are *required* to, because the gate is what makes `coverage_scn`
the boundary. A mutation that acquired the gate before the checkpoint is inside the
boundary and is in the payload; one that acquires it afterwards is above it. There
is no window in between.

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
   length, then fall back to replay. This is the one failure the CRC does catch, and
   it never results in a partial apply.
3. Deserialize the graph into `incr_data_->index_`, restore `membership` into
   `incr_data_->bitmap_->insert_bitmap_`, restore `vid_bound_` and the watermarks,
   then restore `vbitmap_data_->bitmap_->insert_bitmap_`,
   `vbitmap_data_->bitmap_->delete_bitmap_` and `vbitmap_data_->scn_` from
   `visibility`.
4. Run the restore validation below. On failure, drop the whole checkpoint rather
   than the failing section, and fall back to replay.
5. Set the replay start to `coverage_scn + 1` and let
   `complete_index_mem_data_incremental()` handle only the tail.

The replay entry point does not change. What changes is its input: instead of
starting from the snapshot SCN and rebuilding every appended row, it starts from the
restored boundary. Rows at or below `coverage_scn` are already in the graph, in the
membership bitmap and in the visibility bitmap.

### Why the visibility baseline is not optional

The incremental graph does not carry visibility. `init_mem_data(VIRT_INC)` leaves
`incr_data_->bitmap_->delete_bitmap_` at `nullptr`
(`ob_plugin_vector_index_adaptor.cpp:809`), and the commit/tombstone view for
appended rows lives in `vbitmap_data_`. That view is produced only as a side effect
of replay: `complete_index_mem_data_incremental()` copies it out at `:2849-2856` and
writes the replayed rows back into it at `:2944-2951`.

A restart that restores `incr_data_` and jumps the replay cursor to
`coverage_scn + 1` never runs that scan for the prefix, so `vbitmap_data_` stays
empty. There are exactly two ways to handle that, and only one of them is fast:

- Restore `vbitmap_data_->scn_ = coverage_scn` with empty bitmaps. Then
  `check_index_id_table_readnext_status()` takes its fast path, because it trusts
  `vbitmap_data_->scn_ <= query_scn && is_pruned_read_index_id()`
  (`:2359`). The empty view is treated as authoritative, so the checkpointed
  inserts are filtered out as invisible and the pre-checkpoint tombstones are never
  applied. Silent wrong answers.
- Leave `vbitmap_data_->scn_` at its initial value. Then the fast path is not taken
  and the prefix is rescanned, because `complete_index_mem_data_incremental()` only
  skips the index-id scan when `base_scn >= last_dml_scn_` (`:2866-2869`), and
  `base_scn` comes from `vbitmap_data_->scn_`. Correct, but it rebuilds the whole
  prefix and the checkpoint buys nothing.

Persisting the baseline is what makes the tail-only replay claim true. There is no
third option in which the checkpoint omits it and the restart is both correct and
fast.

### Restore validation

Three checks run before the restored state is used. All are cheap and none of them
reads the index-id table:

| Check | Failure means |
| --- | --- |
| `header.coverage_scn == visibility.scn` | the payload mixes two captures |
| `coverage_scn >= header.graph_dml_scn` | the graph watermark ran past the visibility baseline |
| `membership` is a subset of `visibility.insert` plus `visibility.delete` | the graph claims a VID the visibility view never resolved |

The subset test is the important one. Membership is only advanced after
`obvectorutil::add_index()` (`insert_rows()` `:1512` then `:1533`), so a well-formed
generation always has membership inside the union of the visibility views. A payload
that fails the test is a checkpoint of a graph that ran ahead of its baseline — the
state an unlocked capture produces whenever it overlaps the replay path.

On failure nothing is partially applied: the graph, the membership bitmap and both
visibility bitmaps are all discarded, and replay runs from the snapshot SCN. The
degraded path also has to clear `incr_data_->last_read_scn_` and
`incr_data_->last_dml_scn_`, because `is_pruned_read_index_id()` (`:1126`) is true
whenever `last_read_scn_ > last_dml_scn_`, and that flag combined with an empty
`vbitmap_data_` is precisely the silent-wrong-answer case above.

A checkpoint is therefore never a partial commit of state. It is either applied
whole after passing all three checks, or ignored whole.

Instrumentation for the acceptance criteria is a single log line per load:

```
[vec index] checkpoint restored: generation=<n> coverage_scn=<scn> graph_rows=<n> visible_rows=<n> tail_rows=<n> restored_bytes=<n> checks=<ok|fallback:reason>
```

`tail_rows` is the count the replay actually consumed. A restart after a completed
checkpoint must show `tail_rows` equal to the rows written after the checkpoint, not
the total row count. `checks` reports whether the restore validation passed; a
`fallback` reason here is how a rejected payload is distinguished from a checkpoint
that was never written.

## Online replacement

Replacement today builds a new adapter, copies metadata, and drops the incremental
graph. Two options were considered.

**Option A — catch up in the background before the switch.** The old adapter keeps
serving. A new adapter is built from the full snapshot, then fed the incremental rows
in `(coverage_scn, switch_scn]` by a background task. The switch happens when the new
adapter's coverage reaches the switch SCN. Queries during the catch-up are served by
the old adapter and are unaffected. This requires a way to enumerate index-id rows
in SCN order, which the index-id table already provides.

The catch-up has to fill the visibility baseline, not just membership.
`try_sync_vbitmap_memdata()` (`src/observer/vector_index/ob_plugin_vector_index_utils.cpp:603`)
already drives `check_index_id_table_readnext_status()` against a given adapter and
therefore populates `vbitmap_data_`; by its own comment it does not yet sync the
vectors into `incr_data_`. A catch-up that fills the graph but skips the baseline
produces the silent-invisible-rows case described in "Why the visibility baseline is
not optional".

**Option B — generation handoff.** Freeze the current incremental generation at the
rebuild boundary, write later inserts into a new generation, and let the new adapter
adopt the frozen generation's checkpoint plus its own tail. Adopting a checkpoint
means restoring every section, `graph`, `membership` and `visibility`, together with
its SCNs; a handoff that carries the graph but not the baseline lands the new adapter
in the same broken state. This avoids re-reading the index-id table twice but
requires two live incremental graphs for the duration of the rebuild.

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

Resource admission has two dimensions. The first is memory: the staging buffer plus
the bitmap copies (the membership bitmap and both visibility bitmaps), and, only if
the immutable-generation option is ever enabled, the second graph. The check must use
the same admission path as snapshot serialization. The second is the stall budget:
under the phase-1 contract the mutation gate is held across the serialize, so an
underestimated memory check that lets the task start and then fails mid-serialize is
worse than deferring — it blocks every writer for the whole attempt and then
publishes nothing.

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
| `checkpoint_coverage_scn` | coverage boundary carried by that generation |
| `checkpoint_pending_rows` | rows above the watermark |
| `checkpoint_oldest_pending_age` | age of the oldest uncovered row |
| `checkpoint_last_attempt_result` | success / deferred (with reason) / failed (with code) |
| `checkpoint_restore_result` | applied / fallback (with the failed check) on the last load |
| `checkpoint_restored_tail_rows` | rows the last load had to rebuild |

A resource deferral or a failure must be visible here. Silently appearing up to date
is the failure mode the issue calls out, and it is the one that is hardest to notice
from the outside.

## Locking

Four call sites mutate the state a checkpoint has to read, and none of them shares
a single lock:

| Call site | Takes | Does not take |
| --- | --- | --- |
| `insert_rows()` (`ob_plugin_vector_index_adaptor.cpp:1361`) | `bitmap_rwlock_`, at `:1533`, after the graph changed | `mem_data_rwlock_` |
| `write_into_delta_mem()` (`:2130`) | `mem_data_rwlock_` at `:2145`, `bitmap_rwlock_` at `:2179` | `vbitmap_data_` locks |
| `update_incr_bitmap()` (`:3001`) | `bitmap_rwlock_` at `:3011` | `mem_data_rwlock_` |
| replay writer in `complete_index_mem_data_incremental()` | `vbitmap_data_`'s pair at `:2944-2945` | anything on `incr_data_` |

Consequently "take `mem_data_rwlock_` for read and `bitmap_rwlock_` for read" excludes
`write_into_delta_mem()` and nothing else: not `insert_rows()`, which is the main
foreground writer, and not `update_incr_bitmap()`. It also says nothing about the
visibility view, which has its own lock pair on `vbitmap_data_`.

The contract needs one gate that every mutator takes:

- **Phase 1 (proposed):** a dedicated `checkpoint_barrier_` rwlock on the adapter.
  Each mutator takes it for read for the duration of its mutation, and the checkpoint
  task takes it for write around capture *and* serialize (see "Checkpoint snapshot
  contract"). The relative order of the existing locks inside each read section does
  not change.
- **If the barrier is rejected** in favour of an existing lock, then
  `incr_data_->mem_data_rwlock_` must be added to `insert_rows()` and
  `update_incr_bitmap()`, and the checkpoint must hold it for write across the whole
  capture including the serialize, with `bitmap_rwlock_` and `vbitmap_data_`'s pair
  nested inside for the copies. Holding it for read, or holding it only until the
  handles are copied, does not satisfy the contract.

The order is stated once, in the adapter header, before checkpointing is enabled by
default. A comment that says "barrier, then graph, then bitmap" in one place is worth
more than an implicit convention spread over four call sites.

`query_lock_` is not part of the checkpoint contract. Immediate-mode DML records
visibility through the bitmap path without taking it, so a checkpoint that relied on
`query_lock_` for exclusion would miss exactly the concurrent-write case the issue
requires.

## Naming

`checkpoint` already means something else in this subtree:
`ObPluginVectorIndexLoadScheduler` implements `logservice::ObICheckpointSubHandler`
(`src/observer/vector_index/ob_plugin_vector_index_scheduler.h:188-191`) for the
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
   `sync_mode=immediate` and asynchronous modes. This is the case the visibility
   baseline exists for: a checkpoint that restores the graph but not
   `vbitmap_data_` passes every other test on this list and fails this one.
5. **Capture consistency.** Insert continuously while checkpoints run. For every
   published checkpoint assert that the restore validation passes and that no row
   committed at or below `coverage_scn` is missing from the restored view; assert
   that no row above `coverage_scn` is claimed as covered. This is the test an
   unlocked capture cannot pass, because it mixes the graph and the bitmaps of two
   generations while still producing a valid footer.
6. **Degraded restore.** Corrupt one section of a published row, then restore.
   Assert full fallback to replay, that both `incr_data_` and `vbitmap_data_` are
   left empty with `last_read_scn_` cleared, and that post-restart results still
   match the pre-restart results.
7. **Replacement.** Write concurrently with a full optimization; assert the new
   adapter's first query does not reconstruct a graph that the old adapter had
   already built, that its visibility baseline is not empty, and that no transaction
   visibility is lost across the switch.
8. **Measurements.** Report first-query latency, online query latency distribution,
   checkpoint duration, the length of the time writers are blocked on the mutation
   gate, and peak memory across at least two data sizes and two memory budgets. The
   existing numbers in the issue (142.99 s first query after restart; 3.81 s and
   2.80 s replacement stalls) are the baseline to beat. The blocked-writer figure is
   the one that decides whether Option 1 is acceptable or Option 2 has to be built.

Correctness must hold for runs where the checkpoint is interrupted at every stage:
before publish, after publish but before the reader observes it, and after the reader
observes it but before the previous generation is retired.

## Implementation phases

1. Envelope format, serialize and verify, the restore validation, and the visibility
   baseline (`vbitmap_data_` bitmaps plus `scn_`) in both the payload and the restore
   path. Checkpointing stays off by default; the only behaviour change is that a valid
   checkpoint is restored when present. Phase 1 is not shippable without the mutation
   gate: without it the payload does not represent one generation, and the restore
   checks will reject it under exactly the concurrent load the feature targets.
2. Scheduling on size and age, plus the observability surface and the flush entry
   point.
3. Replacement catch-up (Option A), gated behind its own switch, carrying the
   visibility baseline alongside the graph.
4. Enable by default after the measurements above are collected on the CI fixtures.

Phase 1 is the smallest change that demonstrates the win. Phases 2-3 are what make it
usable without operator intervention. The measurements in phase 4 are what decide
whether the default thresholds proposed in phase 2 were right, and whether the
mutation-gate hold justifies building the immutable-generation option.

## Change log

- Revision 1: initial proposal.
- Revision 2:
  - Replaced the "take `mem_data_rwlock_` and `bitmap_rwlock_` for read, then
    serialize the handles" write path. That sequence does not freeze anything:
    `ObVectorIndexSerializer::serialize()` is handed the live index, `insert_rows()`
    mutates the graph before it takes `bitmap_rwlock_` and never takes
    `mem_data_rwlock_`, and copying `roaring64_bitmap_t *` handles copies pointers.
    It is replaced by an explicit snapshot contract ("Checkpoint snapshot
    contract"): an exclusion gate held until the bytes exist, with every mutator
    enumerated, plus the immutable-generation alternative and its writer
    participation rule. The footer CRC is now documented as a byte-integrity check
    only.
  - Added the visibility baseline to the checkpoint state. The previous revision
    persisted only the incremental graph, its membership bitmap and its watermarks,
    which cannot produce a correct tail-only restart because `init_mem_data(VIRT_INC)`
    leaves the incremental delete bitmap `nullptr` and the committed view lives in
    `vbitmap_data_`. Added the `membership` and `visibility` sections, the
    `coverage_scn` boundary definition and its capture-time checks, the restore
    validation and its fallback, and the scheduling, replacement and validation text
    that depended on the old state.
  - Corrected the `ObPluginVectorIndexLoadScheduler` reference in "Naming" from
    `:219`, which is `checkpoint_handler()`, to the class declaration at `:188-191`.
  - Corrected two slips in the new text: the restore fallback discards one
    incremental membership bitmap, not two, and the roaring portability claim now
    names `roaring64_bitmap_portable_serialize()` instead of an untracked
    `deps/3rd/vcpkg/...` header path that cannot be checked from the repository.
