# Incremental HNSW checkpointing

Status: design proposal for [#2206](https://github.com/oceanbase/seekdb/issues/2206).

This page describes how seekdb can persist the completed incremental HNSW graph so
that a restart does not rebuild every vector added since the last full snapshot, and
so that an online snapshot replacement does not leave the serving adapter without an
incremental graph.

The proposal is deliberately scoped to the storage format, the write path, the
recovery path, and the scheduling/observability surface. It does not change the
on-disk format of the full snapshot, and it does not change the SQL surface.

This revision is a design contract and acceptance checklist for a subsequent
kernel implementation. This change is documentation-only: it implements no
kernel code, and no database or runtime validation has been run. Every item in
"Validation plan" is a requirement for that implementation, not a reported
result.

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

Immediate-mode insertion is also speculative with respect to the transaction.
`ObLSTabletService::insert_rows_to_tablet()` calls `insert_vector_index_rows()`
before `insert_tablet_rows()`
(`src/storage/ls/ob_ls_tablet_service.cpp:2837-2839`), and `insert_rows()` adds
membership immediately (`ob_plugin_vector_index_adaptor.cpp:1533-1536`). An
`INSERT` followed by `ROLLBACK` on an already-loaded immediate-mode index can
therefore leave a physical graph vertex and its VID in adapter membership even
though the transaction leaves no committed index-id insert or tombstone. This
document calls that VID an **orphan membership entry**. Repeating visibility
catch-up cannot manufacture a committed record for it.

Absence from the index-id replay is not, by itself, proof of orphanhood. Until
`ObVectorIndexRefresher::do_refresh()` runs, a committed delta-buffer row exists
only in the delta-buffer table: `do_refresh()` copies its `ora_rowscn` into the
index-id table (`src/sql/engine/cmd/ob_vector_index_refresh.cpp:327-356`) and
skips that transfer entirely while
`domain_table_row_cnt < refresh_threshold_` (`:305-306`). The delta-buffer query
path has already put such a VID into the incremental graph and its membership
before that transfer happens: `check_delta_buffer_table_readnext_status()`
(`ob_plugin_vector_index_adaptor.cpp:2065`), `complete_delta_buffer_table_data()`
(`:2194`) and `write_into_delta_mem()` (`:2160`, `:2173`, `:2181`). A VID can
therefore be committed, materialized and queryable while the index-id table says
nothing about it. An **orphan membership entry** is a VID with no committed
record in *any* durable source (index-id table or delta-buffer table) at or
below the claimed boundary — never merely a VID missing from the index-id
replay.

Two locking consequences matter, and neither is visible from the insert path alone:

- `insert_rows()` never takes `incr_data_->mem_data_rwlock_`. A checkpoint that
  takes `mem_data_rwlock_` for read plus `bitmap_rwlock_` for read therefore does
  not exclude this writer. The graph pointer stays reachable and `add_index()`
  keeps mutating it while the checkpoint believes it holds a stable view.
- Copying `roaring64_bitmap_t *` handles out of `bitmap_` copies pointers, not
  contents. Both bitmaps remain mutable after the lock is released, so a later
  pass over those handles is not reading the state that was captured.

The asynchronous writer has the same exclusion problem at a higher level.
`ObCSAsyncIndexProcessor::write_to_vsag_()`
(`src/observer/change_stream/ob_cs_plugin_async_index.cpp`) advances
`last_dml_scn_`, directly calls `obvectorutil::add_index()`, and only then calls
`update_incr_bitmap()`. A gate inside the bitmap helper alone would protect the
last step while leaving the watermark and graph mutation outside the checkpoint
barrier.

There is also a cross-stage gap even when every individual mutation is gated.
`complete_index_mem_data_incremental()` can publish the visibility baseline and
watermark before the HNSW scan state machine reaches `QUERY_ROWKEY_VEC` and calls
`complete_delta_buffer_table_data()` to materialize the vectors. A capture in
that gap can therefore see live VIDs in `vbitmap_data_` but an empty or partial
incremental membership set. Mutation exclusion must be paired with the coverage
proof below; neither is sufficient by itself.

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
- `membership` — the committed checkpoint membership view derived from
  `incr_data_->bitmap_->insert_bitmap_` as specified in "Coverage boundary", not
  an unconditional copy of that raw adapter bitmap. It is serialized with
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

`coverage_scn` is the boundary the whole payload is stated against. Every VID that
is live in `visibility` at or below this boundary must have its vector
materialized in `graph` and its bit set in `membership`; every tombstoned VID
must be resolved by `visibility` and filtered even if an obsolete physical graph
vertex remains. Replay starts strictly above the boundary.

One boundary has to cover three sections because different code paths advance
them:

| State | Advanced by | Lock held today |
| --- | --- | --- |
| `incr_data_->index_` | `insert_rows()` (`:1512`, `:1522`), `write_into_delta_mem()` (`:2160`, `:2173`), and `ObCSAsyncIndexProcessor::write_to_vsag_()` | no common graph lock; the asynchronous caller invokes `add_index()` directly |
| `incr_data_->bitmap_->insert_bitmap_` | `insert_rows()` (`:1533`), `update_incr_bitmap()` (`:3011`), `write_into_delta_mem()` (`:2179`), and the asynchronous caller through that helper | `bitmap_rwlock_` |
| `vbitmap_data_->bitmap_`, `vbitmap_data_->scn_` | replay in `complete_index_mem_data_incremental()` (`:2849-2856`, `:2944-2951`) and query paths | `vbitmap_data_`'s own lock pair |

`vbitmap_data_->scn_` is advanced by replay, not by `insert_rows()`, so no single
existing critical section advances both views. The capture protocol therefore
*establishes* the boundary instead of inheriting it:

1. Run the background catch-up in "Write path" when delta-buffer reconciliation,
   visibility, or graph materialization is behind, then capture both views under
   the mutation-exclusion scheme described in "Checkpoint snapshot contract".
   Read `vbitmap_data_->scn_` and `incr_data_->last_dml_scn_` inside the same
   critical section.
2. Set `coverage_scn = vbitmap_data_->scn_`. The visibility baseline is what was
   actually replayed, so it is the boundary the payload can honestly claim.
3. Under the same gate, distinguish the raw adapter set from the committed set
   that will be persisted:
   - `adapter_membership = incr_data_->bitmap_->insert_bitmap_`;
   - `resolved = visibility.insert ∪ visibility.delete`;
   - `live = visibility.insert − visibility.delete`;
   - `checkpoint_membership = adapter_membership ∩ resolved`.

   Require `incr_data_->last_dml_scn_ <= coverage_scn`,
   `live ⊆ checkpoint_membership`, and
   `checkpoint_membership ⊆ resolved`. The latter is true by construction but is
   still checked against the staged copy before publication. The live-visibility
   relation rejects every committed live VID whose vector has not been
   materialized. Empty checkpoint membership is valid whenever live visibility
   is empty; resolved tombstones do not require membership bits.

   `live` must be computed; the raw `visibility.insert` is not the live set. A
   VID that was replayed and later deleted keeps its bit in
   `visibility.insert`: the delete removes the membership bit
   (`insert_rows()` `:1539`), while `complete_index_mem_data_incremental()`
   publishes by OR-ing the copied sets into the shared ones (`:2947-2948`), so
   the removal of `d_vids` from the copied insert set (`:2927-2928`) cannot
   clear a bit the shared bitmap already holds. Such a VID is in
   `visibility.insert` and in `visibility.delete` but not in
   `adapter_membership`. A containment check against the raw insert set demands
   a membership bit the delete path removed on purpose: capture and shutdown
   flush would then keep deferring forever after an ordinary delete even though
   every live vector is materialized.

   Adapter membership is added only after graph insertion (`:1512` then `:1533`),
   so under the mutation gate the live-visibility relation proves that no live
   VID in the claimed prefix is still waiting for `QUERY_ROWKEY_VEC`. Tombstones
   need no membership bit. A separate graph-completion watermark could provide
   the same proof, but phase 1 uses these containment checks and adds no second
   watermark.
4. Orphan membership is reconciled, not retried forever, and it is classified
   only after every committed durable record has been made visible to catch-up.
   Before `orphan_membership = adapter_membership − resolved` is computed,
   catch-up must reconcile the committed delta-buffer rows through a target SCN:
   run the refresh transfer that copies `ora_rowscn` into the index-id table
   (`ObVectorIndexRefresher::do_refresh()`,
   `ob_vector_index_refresh.cpp:327-356`) even when
   `domain_table_row_cnt < refresh_threshold_` would skip the user-facing
   refresh (`:305-306`). The same transfer deletes the copied delta-buffer rows
   (`:360-375`), and the subsequent index-id replay publishes them into the
   `vbitmap_data_` baseline that capture serializes. The `coverage_scn` read in
   step 2 must not exceed this reconciled target. If it does, or if any committed
   delta-buffer row at or below `coverage_scn` remains unreconciled, repeat the
   bounded catch-up or defer; never prune against the incomplete view.

   Updating only a temporary `resolved` set is not an equivalent reconcile. The
   same operations must reach the persisted visibility baseline; otherwise
   capture could retain a membership bit that is absent from the serialized
   `visibility.insert ∪ visibility.delete`, contradicting the restore checks.
   Only after the refresh transfer and index-id replay complete does
   `adapter_membership − resolved` mean "no committed record anywhere". A VID
   whose membership bit came from `write_into_delta_mem()` (`:2181`) while its
   index-id row was still waiting for the threshold transfer is *not* an orphan:
   stripping it would persist the graph vertex without its membership bit, and
   after restart the delta-buffer scan would rediscover the row and schedule
   foreground vector lookup through `prepare_delta_mem_data()` (`:3042`) while
   the load log reports `tail_rows=0`. If the reconcile cannot complete in
   bounded cycles, defer the capture.

   Once catch-up has established the committed visibility view at `coverage_scn`,
   phase 1 excludes `orphan_membership` from the checkpoint copy and persists only
   `checkpoint_membership`. It need not mutate the serving adapter's raw bitmap
   or physically delete an orphan vertex from the serialized graph: the graph
   may be a physical superset, but an orphan bit must never be restored into
   committed membership or made visible by membership/visibility filtering.
   Rebuilding the graph or reconciling the serving bitmap is an acceptable
   alternative only if it produces the same committed view. Repeating the old
   raw test `adapter_membership ⊆ resolved` after every catch-up is not an
   acceptable recovery strategy.
5. If any remaining check fails, do not publish. Record the attempt as deferred
   with its reason and leave the published `coverage_scn` where it was. Before
   retrying, run catch-up again rather than repeating validation against
   unchanged state. An under-eager boundary only costs replay; an over-eager one
   is a correctness bug.

The watermark relation in step 3 is not new: it is the same
`base_scn >= last_dml_scn_` condition `complete_index_mem_data_incremental()`
already uses to decide whether the index-id scan can be omitted (`:2866-2869`).
That condition is necessary but is not, by itself, proof of graph completion.
The running system relies on its graph-coverage consequence because the skip path
also skips the delta check that would otherwise repair a missing vertex. A
checkpoint may therefore only claim a boundary at which the existing replay would
itself skip the prefix; claiming a larger boundary would assert coverage the
replay code does not consider covered. The additional
`live ⊆ membership` relation closes the separate inter-stage window
that this watermark comparison cannot see. Consequently, when there are no rows
above `coverage_scn`, restore may report `tail_rows=0` only because the claimed
prefix is already query-ready, not because its first ANN query still owes graph
materialization.

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
- `update_incr_bitmap()` (`:3001`) must hold it across `:3011-3015` when entered
  without an outer mutation guard.
- `ObCSAsyncIndexProcessor::write_to_vsag_()` must hold the same adapter gate for
  read across its `last_dml_scn_` update, every direct `add_index()` call, and the
  subsequent `update_incr_bitmap()` call. It may instead route that entire
  sequence through one gated adapter method. Gating only the bitmap helper does
  not satisfy this requirement.
- The replay writer in `complete_index_mem_data_incremental()` must hold it across
  its `incr_data_` watermark mutation as well as `vbitmap_data_`'s publication and
  lock pair at `:2944-2951`. Vector materialization remains covered by the
  `write_into_delta_mem()` guard; the live-visibility containment check protects
  the gap between these stages.

The gate is acquired once at the outermost boundary of a logical mutation. A
callee participating in that guard must not independently release it or
recursively acquire a non-recursive lock. In particular, the asynchronous caller
and its bitmap helper share one outer acquisition unless the whole operation is
moved into the gated adapter method described above.

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
Capture and catch-up are separate phases.

### Background catch-up before capture

An already-loaded immediate-mode index can advance membership and
`last_dml_scn_` for a small committed batch while `vbitmap_data_` remains at its
last replay. Without an ANN query, age-based capture and shutdown flush would
otherwise retry the same failed coverage checks forever. Every capture therefore
starts by checking for stale visibility, incomplete graph materialization, or
committed delta-buffer rows whose `ora_rowscn` has not reached the index-id table
yet and, when needed, runs a bounded background catch-up to a target at least as
recent as the observed `last_dml_scn_`.

Catch-up uses one target SCN for its durable reads. An explicit flush uses its
requested flush boundary; a scheduled attempt uses a target at least as recent as
the observed `last_dml_scn_`. The eventual `coverage_scn` may be lower than that
target, but never higher. Catch-up must drive three stages of the existing
pipeline, in this order:

1. **Delta-buffer reconcile.** Run the refresh transfer for every committed
   delta-buffer row at or below the target SCN, so that the index-id view sees
   rows whose `ora_rowscn` has not been copied yet. The user-facing refresh skips
   that copy while `domain_table_row_cnt < refresh_threshold_`
   (`ObVectorIndexRefresher::do_refresh()`, `ob_vector_index_refresh.cpp:305-306`,
   transfer at `:327-356`); checkpoint catch-up must not inherit that skip for
   rows at or below its target. The transfer also removes the copied rows from
   the delta-buffer table (`:360-375`). No orphan classification may run before
   this reconcile completes (see "Coverage boundary", step 4).
2. **Index-id replay** through `complete_index_mem_data_incremental()` at the
   same target, publishing both visibility bitmaps and their SCN into the actual
   `vbitmap_data_` baseline that will be serialized, not just a temporary
   `resolved` copy.
3. **Vector materialization** — the `QUERY_ROWKEY_VEC` /
   `complete_delta_buffer_table_data()` work that materializes every live vector
   and its membership bit.

`refresh_bitmap_background()` currently discards the synthetic
query context after advancing `vbitmap_data_`, and `try_sync_vbitmap_memdata()`
also leaves vector synchronization undone; neither helper performs the
delta-buffer reconcile. None of them alone is checkpoint
catch-up. The implementation must retain and drain the materialization context,
without requiring an ANN query to do so.

The table scans, vector lookups, and materialization loops run **outside** the
exclusive serialization hold. Their individual mutations take the gate for read
as specified above, so writers and queries can continue between catch-up steps.
After catch-up returns, the capture task re-reads all watermarks and sets under
the write gate. New writes may require another bounded catch-up/defer cycle; no
cycle may weaken the containment checks just to make progress.

Under the Option 1 contract the complete sequence is:

1. Admit the attempt against its memory and stall budgets. Without holding the
   mutation gate for write, run the catch-up above — including the delta-buffer
   reconcile — when stale, and also after any previous deferral for stale
   visibility, incomplete membership, or incomplete delta-buffer transfer.
2. Take the mutation gate for write, then `incr_data_->bitmap_rwlock_` for write,
   then `vbitmap_data_`'s lock pair.
3. Under those locks, re-read `incr_data_->last_dml_scn_` and
   `vbitmap_data_->scn_`, derive
   `checkpoint_membership = adapter_membership ∩ resolved`, use
   `live = visibility.insert − visibility.delete` for containment, and run all
   boundary checks in "Coverage boundary". Require `coverage_scn` not to exceed
   the reconciled catch-up target. Orphan bits are excluded from that derived
   view only when the delta-buffer reconcile of step 1 has made every committed
   durable record at or below `coverage_scn` visible to the serialized
   `resolved`; if it has not, the attempt is stale and is deferred, never pruned
   against the incomplete view. If any other check fails, release, record a
   deferred attempt with its reason, and publish nothing. The next retry starts
   with catch-up, not another immediate capture.
4. Still under the gate, copy the raw adapter membership bitmap and both
   visibility bitmaps with `roaring64_bitmap_copy()`, then remove every orphan
   bit — classified only after the step-1 delta-buffer reconcile —
   from the membership copy to materialize `checkpoint_membership`. Copies
   are required: the handles are not a snapshot, and the bitmaps keep mutating.
5. Still under the gate, serialize `incr_data_->index_` into the staging buffer,
   then append `vid_bound_` and the watermarks. A physical orphan vertex may be
   present in this graph payload, but its VID is absent from the persisted
   committed membership view and remains invisible.
6. Release the gate. Compute the footer over the staged bytes and publish.

Steps 2-5 are inside one critical section, so the graph, the membership bitmap and
the visibility bitmaps in the payload all come from the same generation. Catch-up
is deliberately outside that section. The exclusive hold is only the serialize
plus the bitmap copies; that is the price of the contract, and it is why admission
refuses the attempt instead of letting it start and then stall every insert.

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
3. Deserialize the graph into `incr_data_->index_`, restore the committed
   `membership` view into `incr_data_->bitmap_->insert_bitmap_`, restore
   `vid_bound_` and the watermarks, then restore
   `vbitmap_data_->bitmap_->insert_bitmap_`,
   `vbitmap_data_->bitmap_->delete_bitmap_` and `vbitmap_data_->scn_` from
   `visibility`.
4. Run the restore validation below. On failure, drop the whole checkpoint rather
   than the failing section, and fall back to replay.
5. Set the replay start to `coverage_scn + 1`, mark the one-shot
   `initial_tail_scan_required` state, and complete the mandatory initial durable
   tail scan below before trusting either skip path.

### Mandatory initial durable tail scan

Restored watermarks describe the checkpoint prefix only. They do not prove that
no index-id row was committed after checkpoint publication. Restoring
`last_dml_scn_ <= coverage_scn` lets `complete_index_mem_data_incremental()` set
`can_omit_4th_table_scan` when `base_scn >= last_dml_scn_`
(`ob_plugin_vector_index_adaptor.cpp:2866-2874`). Independently, a restored
`last_read_scn_ > last_dml_scn_` makes `is_pruned_read_index_id()` true
(`:1126-1132`), allowing the synchronous bypass in
`check_index_id_table_readnext_status()` to set `is_skip_4th_index`
(`:2359-2370`). Either path can skip every durable tail row when no new DML
arrives after restart.

Phase 1 therefore requires one forced durable tail scan after every successful
checkpoint restore. While `initial_tail_scan_required` is set:

- the synchronous branch must not use `is_pruned_read_index_id()` to set
  `is_skip_4th_index`, even if `vbitmap_data_->scn_ <= query_scn`;
- `complete_index_mem_data_incremental()` must treat
  `can_omit_4th_table_scan` as false, regardless of the restored watermark
  comparison.

The scan starts strictly above `coverage_scn` and reads the durable index-id
table through the replay's `query_scn`. It must apply all committed tail inserts
and deletes to visibility and finish their vector materialization before the
restored graph is used for that query. Clear the one-shot state only after the
scan reaches iterator end and materialization succeeds; an error leaves it set
and must not silently fall back to either bypass. Subsequent replay may use the
normal skip decisions. Merely advancing the replay cursor, restoring different
watermark values, or observing that no post-restart DML arrived does not satisfy
this requirement.

The replay entry point and table-scan machinery do not change. What changes is
their input and the one-shot bypass state: instead of starting from the snapshot
SCN and rebuilding every appended row, replay starts from the restored boundary
and is guaranteed to consume the durable tail once. Every live VID at or below
`coverage_scn` is already materialized in the graph and committed membership
bitmap, and the visibility bitmaps resolve both live and tombstoned VIDs.

Tail replay is not a substitute for prefix materialization. If no rows exist above
`coverage_scn`, `tail_rows=0` must coincide with a query-ready restored graph:
the first ANN query adds no checkpointed VID to the graph or membership and does
not invoke delta completion for the claimed prefix. A payload that cannot prove
this with the restore checks below is rejected and rebuilt through full replay;
it is never restored with deferred prefix work hidden by an empty tail.

Delta-buffer rows above `coverage_scn` are outside the claimed prefix: they are
consumed by the normal delta-buffer scan on the first query and materialized
through `prepare_delta_mem_data()` (`:3042`), and they are not part of
`tail_rows`. A row at or below `coverage_scn` was copied into the index-id table
and deleted from the delta-buffer table by the capture-time refresh transfer
(`ob_vector_index_refresh.cpp:327-375`), so the normal delta-buffer scan cannot
rediscover it and index-id tail replay starts strictly above its covered SCN. If
such a row remains unreconciled at capture, capture must defer rather than
publish. Restored checkpoint watermarks alone do not suppress the delta-buffer
scan and are not a substitute for this reconcile.

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

Four checks run before the restored state is used. All are cheap and none of them
reads the index-id table:

| Check | Failure means |
| --- | --- |
| `header.coverage_scn == visibility.scn` | the payload mixes two captures |
| `coverage_scn >= header.graph_dml_scn` | the graph watermark ran past the visibility baseline |
| `membership ⊆ visibility.insert ∪ visibility.delete` | the graph claims a VID the visibility view never resolved |
| `visibility.insert − visibility.delete ⊆ membership` | a live VID in the claimed prefix still lacks graph membership and materialization |

The two subset tests protect opposite directions, and they apply to the persisted
committed membership view, not to the raw adapter bitmap. Raw adapter membership
is only advanced after `obvectorutil::add_index()` (`insert_rows()` `:1512` then
`:1533`), so the first test detects committed graph membership that is not
resolved by the baseline. The second detects a baseline that ran ahead of graph
materialization, including the interval before `QUERY_ROWKEY_VEC`; an empty
membership bitmap cannot validate a non-empty live visibility set. The live set
is `visibility.insert − visibility.delete`, never the raw insert set: publish
ORs the replayed copies into the shared bitmaps (`:2947-2948`) while a delete
removes the membership bit (`:1539`), so a tombstoned VID legitimately stays in
`visibility.insert` and would otherwise fail this check forever. Both tests are
mandatory at capture and restore. An orphan physical graph vertex does not fail
these checks because its orphan membership bit was excluded before publication;
restoring that bit is invalid.

Restore does not read the index-id table or the delta-buffer table, so it cannot
re-derive orphanhood. The guarantee that no committed delta-buffer row was
stripped of its membership bit is established at capture, by the reconcile in
"Coverage boundary", step 4; the below-refresh-threshold fixture in "Validation
plan" is what holds that guarantee accountable end to end.

On failure nothing is partially applied: the graph, the membership bitmap and both
visibility bitmaps are all discarded, and replay runs from the snapshot SCN. The
degraded path also has to clear `incr_data_->last_read_scn_` and
`incr_data_->last_dml_scn_`, because `is_pruned_read_index_id()` (`:1126`) is true
whenever `last_read_scn_ > last_dml_scn_`, and that flag combined with an empty
`vbitmap_data_` is precisely the silent-wrong-answer case above.

A checkpoint is therefore never a partial commit of state. It is either applied
whole after passing all four checks, or ignored whole. In particular, restore must
not accept `tail_rows=0` as evidence of completeness when the second subset test
would fail.

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

The catch-up has to fill both the visibility baseline and graph membership.
`try_sync_vbitmap_memdata()` (`src/observer/vector_index/ob_plugin_vector_index_utils.cpp:603`)
already drives `check_index_id_table_readnext_status()` against a given adapter and
therefore populates `vbitmap_data_`; by its own comment it does not yet sync the
vectors into `incr_data_`. A catch-up that fills the graph but skips the baseline
produces the silent-invisible-rows case described in "Why the visibility baseline is
not optional". A visibility-only catch-up can instead advance the baseline past an
incomplete graph. The switch therefore uses the same full materialization catch-up,
the same delta-buffer reconcile, and the same live-set containment check
(`visibility.insert − visibility.delete ⊆ membership`) as checkpoint capture.

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
attempt with its reason and returns without advancing the published checkpoint
coverage or generation. A stale-coverage deferral instead schedules background
catch-up before the next capture; catch-up may advance `vbitmap_data_->scn_`, but
that alone never publishes a checkpoint.

An explicit flush is exposed for planned shutdowns. It waits for an in-progress
checkpoint to finish, runs the same background delta-buffer reconcile, visibility
and vector materialization catch-up outside the exclusive hold, and then requests
capture if the pending set is non-empty. It reports whether the requested coverage was
reached. It is a best-effort operation: a shutdown that times out must still exit,
and the next start must fall back to replay. Flush must make progress for an
ingest-only immediate-mode workload without manufacturing an ANN query.

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

The mutation paths below do not share a single lock. The asynchronous caller and
its bitmap helper are listed separately because guarding only the helper leaves
the caller's watermark and graph insertion unprotected:

| Call site | Takes | Does not take |
| --- | --- | --- |
| `insert_rows()` (`ob_plugin_vector_index_adaptor.cpp:1361`) | `bitmap_rwlock_`, at `:1533`, after the graph changed | `mem_data_rwlock_` |
| `write_into_delta_mem()` (`:2130`) | `mem_data_rwlock_` at `:2145`, `bitmap_rwlock_` at `:2179` | `vbitmap_data_` locks |
| `update_incr_bitmap()` (`:3001`) | `bitmap_rwlock_` at `:3011` | `mem_data_rwlock_` |
| `ObCSAsyncIndexProcessor::write_to_vsag_()` | `bitmap_rwlock_` only inside the later bitmap helper | any lock across its watermark update and direct `add_index()` |
| replay writer in `complete_index_mem_data_incremental()` | `vbitmap_data_`'s pair at `:2944-2945` | anything on `incr_data_` |

Consequently "take `mem_data_rwlock_` for read and `bitmap_rwlock_` for read" excludes
`write_into_delta_mem()` and nothing else: not `insert_rows()`, which is the main
foreground writer, not `update_incr_bitmap()`, and not the direct asynchronous
`add_index()`. It also says nothing about the visibility view, which has its own
lock pair on `vbitmap_data_`.

The contract needs one gate that every mutator takes:

- **Phase 1 (proposed):** a dedicated `checkpoint_barrier_` rwlock on the adapter.
  Each mutator takes it for read for the duration of its mutation, and the checkpoint
  task takes it for write around capture *and* serialize (see "Checkpoint snapshot
  contract"). The relative order of the existing locks inside each read section does
  not change.
- **If the barrier is rejected** in favour of an existing lock, then
  `incr_data_->mem_data_rwlock_` must be added to `insert_rows()`,
  `update_incr_bitmap()`, and the full `write_to_vsag_()` watermark/graph/bitmap
  sequence. The checkpoint must hold it for write across the whole capture
  including the serialize, with `bitmap_rwlock_` and `vbitmap_data_`'s pair nested
  inside for the copies. Holding it for read, holding it only until the handles
  are copied, or taking it only inside the asynchronous bitmap helper does not
  satisfy the contract.

The order is stated once, in the adapter header, before checkpointing is enabled by
default. A comment that says "barrier, then graph, then bitmap" in one place is worth
more than an implicit convention spread across the mutation paths.

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
   checkpoint, and that the first query does not rebuild the checkpointed set. With
   no post-checkpoint rows, assert `tail_rows=0` and compare graph/membership state
   immediately before and after that query: no covered VID may be added. Compare
   first-query latency against the same fixture with checkpointing disabled.
2. **Age-based scheduling.** On an already-loaded immediate-mode index, commit a
   batch below the existing mem-sync and optimization thresholds and issue no ANN
   query. Let its age exceed `vector_index_graph_checkpoint_max_age`; assert that
   background catch-up advances both visibility and materialization, a checkpoint
   is published, and its generation advances without a full optimization or an
   endless sequence of stale-coverage deferrals.
3. **Resource deferral.** Constrain the budget so admission fails; assert the deferral
   is recorded with a reason and that a later attempt with a larger budget succeeds.
4. **Graceful shutdown.** Request a flush, shut down, restart, and verify that
   committed insert, update and delete visibility is preserved in both
   `sync_mode=immediate` and asynchronous modes. This is the case the visibility
   baseline exists for: a checkpoint that restores the graph but not
   `vbitmap_data_` passes every other test on this list and fails this one.
5. **Ingest-only flush and restart.** On an already-loaded immediate-mode index,
   insert a committed batch below the mem-sync and optimization thresholds, then run
   **insert → flush → restart with no intervening ANN query**. Assert that flush
   performs background catch-up outside the exclusive serialization hold, publishes
   a checkpoint, and restores with `tail_rows=0`. The first post-restart query must
   return the inserted rows without rebuilding or adding any covered graph vertex.
6. **Capture consistency and asynchronous writer participation.** Insert
   continuously through both the foreground path and
   `ObCSAsyncIndexProcessor::write_to_vsag_()` while checkpoints run. Include test
   suspension points after the asynchronous watermark update and after direct
   `add_index()` but before `update_incr_bitmap()`. No capture may serialize that
   partial mutation. For every published checkpoint assert that restore validation
   passes, no row committed at or below `coverage_scn` is missing, and no row above
   it is claimed as covered.
7. **Visibility/materialization inter-stage race.** Pause after
   `complete_index_mem_data_incremental()` publishes a non-empty live visibility
   baseline and watermark but before `QUERY_ROWKEY_VEC` materializes its vectors,
   with at least one live VID still absent from membership. Capture must not
   publish while `visibility.insert − visibility.delete ⊄ membership`; it must defer
   and use full catch-up before retrying. Resume materialization, capture, and
   restart with no tail rows: restore must pass both subset checks and the first
   query must not rebuild the claimed prefix.
8. **Degraded restore.** Corrupt one section of a published row, then restore.
   Separately construct a checksum-valid payload whose live visibility is not a
   subset of membership. Assert full fallback to replay in both cases, that both
   `incr_data_` and `vbitmap_data_` are left empty with `last_read_scn_` cleared,
   and that post-restart results still match the pre-restart results.
9. **Durable tail with no post-restart write.** Checkpoint a committed prefix,
   then commit at least one insert and one delete above `coverage_scn`. Restart
   and issue no further DML before the first ANN query. Assert that the one-shot
   initial scan reads the index-id table through that query's SCN, both skip
   paths remain disabled until it completes, and `tail_rows` equals the durable
   post-checkpoint row count. The inserted row must be visible, the deleted row
   must remain invisible, and results must match a full-replay control. Run the
   fixture in both `sync_mode=immediate` and asynchronous modes so each bypass
   is covered. Repeat with an empty tail and assert exactly one empty scan
   rather than a bypass.
10. **Rollback reconciliation and flush.** On an already-loaded immediate-mode
    index, run **BEGIN → INSERT → ROLLBACK → committed INSERT → flush**, with no
    intervening ANN query and with distinct VIDs for the rolled-back and committed
    rows. Flush must finish in bounded catch-up/defer cycles and publish a
    checkpoint rather than retrying raw membership containment forever. Assert
    that the rolled-back VID is absent from checkpoint membership, the new
    committed VID is present with a materialized vector, and any orphan physical
    vertex remains invisible. After restart, query results must contain only the
    committed row and must match the full-replay control.
11. **Replacement.** Write concurrently with a full optimization; assert the new
    adapter's first query does not reconstruct a graph that the old adapter had
    already built, that its visibility baseline is not empty, and that no transaction
    visibility is lost across the switch.
12. **Measurements.** Report first-query latency, online query latency distribution,
    catch-up duration, exclusive checkpoint duration, the length of the time writers
    are blocked on the mutation gate, and peak memory across at least two data sizes
    and two memory budgets. Show that table scans and vector lookups are outside the
    blocked-writer interval. The existing numbers in the issue (142.99 s first query
    after restart; 3.81 s and 2.80 s replacement stalls) are the baseline to beat.
    The blocked-writer figure decides whether Option 1 is acceptable or Option 2 has
    to be built.
13. **Replayed VID deleted between two checkpoints, background catch-up only.**
    On an already-loaded immediate-mode index, run **insert → checkpoint →
    DELETE → checkpoint** with no intervening ANN query, letting background
    catch-up drive both captures. The second capture must publish instead of
    deferring forever: the deleted VID stays in `visibility.insert` because
    publish ORs into the shared bitmap (`:2947-2948`), while its membership bit
    was removed by `insert_rows()` (`:1539`), so the live-set check
    `visibility.insert − visibility.delete ⊆ membership` must pass while the raw
    insert-set check would not. Assert that capture and restore validation use
    the live set rather than the raw `visibility.insert`, that after restart the
    deleted VID is invisible and the surviving row is returned, and that results
    match a full-replay control. Repeat with the delete committed while the
    index is below the mem-sync and optimization thresholds.
14. **Below-refresh-threshold delta-buffer commit.** Run two fixtures on an
    already-loaded immediate-mode index whose delta-buffer table holds fewer
    committed rows than `refresh_threshold_`:
    - *Pre-materialized orphan-classification race.* Commit a row, then use one
      preparatory ANN query to drive
      `check_delta_buffer_table_readnext_status()` →
      `complete_delta_buffer_table_data()` → `write_into_delta_mem()`. Before
      flush, assert that the VID is in graph membership, its committed row is
      still in the delta-buffer table, and no corresponding index-id row exists
      because `ObVectorIndexRefresher::do_refresh()` skipped the transfer
      (`ob_vector_index_refresh.cpp:305-306`). Then run **flush → restart with no
      further ANN query**. This preparatory query is setup for the pre-refresh
      state; it is not hidden work between flush and restart.
    - *Ingest-only flush.* Repeat from the same below-threshold initial state as
      **insert → flush → restart with no ANN query at all**. Flush itself must
      perform the reconcile, index-id replay, and vector materialization.

    In both fixtures, catch-up must run the refresh transfer despite the
    threshold and replay it into the serialized visibility baseline before any
    orphan classification, so the VID's membership bit is persisted together
    with its graph vertex. After restart the load log reports `tail_rows=0`, the
    normal delta-buffer scan does not rediscover the transferred row, the first
    query performs no foreground `prepare_delta_mem_data()` vector lookup for a
    covered VID, and results match a full-replay control. When the reconcile
    cannot complete, assert the attempt is deferred with a reason rather than
    published with the membership bit stripped.

Correctness must hold for runs where the checkpoint is interrupted at every stage:
before publish, after publish but before the reader observes it, and after the reader
observes it but before the previous generation is retired.

## Implementation phases

1. Envelope format, serialize and verify, the four restore checks, and the
   visibility baseline (`vbitmap_data_` bitmaps plus `scn_`) in both the payload and
   the restore path. Derive and persist committed checkpoint membership so that
   rollback orphans cannot block capture, and gate both restore-time bypasses behind
   the one-shot initial durable tail scan. Add the mutation gate to every writer,
   including the complete asynchronous `write_to_vsag_()` sequence, and add
   background catch-up through delta-buffer reconcile, index-id replay, and
   vector materialization. Checkpointing stays off by default; the only
   behaviour change is that a valid checkpoint is restored when present.
   Phase 1 is not shippable without every piece: the gate makes capture
   atomic, the committed-membership and live-set proofs make the claimed prefix
   complete, catch-up lets an ingest-only workload reach that state without a
   query, the delta-buffer reconcile keeps below-threshold committed rows out of
   the orphan classification, orphan reconciliation makes capture terminate after
   rollback, and the forced initial scan makes post-checkpoint durable rows
   visible after restart.
2. Scheduling on size and age, plus the observability surface and the flush entry
   point, all using the phase-1 catch-up before capture and stale retry.
3. Replacement catch-up (Option A), gated behind its own switch, carrying the
   visibility baseline alongside the graph and satisfying the same containment
   proof before the switch.
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
- Revision 3:
  - Extended mutation exclusion to the complete asynchronous
    `ObCSAsyncIndexProcessor::write_to_vsag_()` watermark, graph insertion, and
    membership sequence, and added its partial-mutation races to the acceptance
    plan.
  - Added `visibility.insert ⊆ membership` at capture and restore so a visibility
    baseline published before `QUERY_ROWKEY_VEC` cannot validate an incomplete
    graph or hide prefix rebuild behind `tail_rows=0`. (Revision 5 supersedes the
    raw insert set with the live set `visibility.insert − visibility.delete`.)
  - Required background visibility and vector-materialization catch-up outside
    the exclusive serialization hold before capture and every stale retry, with
    age-triggered and insert → flush → restart tests that use no intervening ANN
    query.
- Revision 4:
  - Required a one-shot initial durable tail scan after checkpoint restore, with
    both the synchronous `is_skip_4th_index` bypass and the
    `can_omit_4th_table_scan` path disabled until that scan and materialization
    complete. Added restart acceptance coverage with durable tail rows and no
    post-restart DML.
  - Replaced raw adapter membership persistence with a committed checkpoint
    membership view that reconciles rollback orphans instead of deferring
    forever, and added rollback → committed insert → flush acceptance coverage.
- Revision 5:
  - Normalized the capture and restore containment checks to the live visibility
    set `visibility.insert − visibility.delete`. Publish ORs the replayed copies
    into the shared bitmaps (`complete_index_mem_data_incremental()` `:2947-2948`)
    while `insert_rows()` `:1539` removes the membership bit on delete, so a
    tombstoned VID legitimately remains in `visibility.insert`; checking the raw
    insert set would demand the removed bit and defer capture and shutdown flush
    forever after an ordinary delete. Added insert → checkpoint → delete →
    checkpoint acceptance coverage driven by background catch-up with no ANN
    query.
  - Required committed delta-buffer rows to be reconciled before orphan
    membership is classified: checkpoint catch-up runs the refresh transfer past
    the `refresh_threshold_` skip (`ObVectorIndexRefresher::do_refresh()`,
    `ob_vector_index_refresh.cpp:305-306`, `:327-375`) and then replays the
    transferred rows into the serialized visibility baseline. The capture
    boundary cannot exceed the reconciled target, a temporary `resolved`-only
    merge is insufficient, and incomplete reconcile defers capture. Added
    below-refresh-threshold acceptance fixtures for both a pre-materialized
    orphan-classification race and an ingest-only, no-query flush/restart.
