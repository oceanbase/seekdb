# Rust text plugin

This is a real Rust `cdylib` loaded through the same public C ABI as the C/C++
GIS plugin. It depends on the public `seekdb-extension` SDK, not the Rust host
runtime or private C++ headers. The host does not need a new expression factory
entry for these functions.

Version v15 requires the optional Host API v3 owned-memory SPI during init.
Words cursors keep their UTF-8 input in SDK `OwnedHostBuffer` and may migrate
serially between host workers. Byte tokens retain the memory account, while the
loader's separate cursor lease pins plugin code. Concat continues to use the
borrowed host buffer path; both kinds share the same module-generation quota.

- `seekdb_rust_char_count(TEXT)` counts Unicode scalar values (not bytes or
  grapheme clusters); NULL returns NULL and invalid UTF-8 is rejected.
- `seekdb_rust_concat3(left, middle, right)` concatenates three UTF-8 byte
  values and declares NULL propagation. The SQL host stops evaluating later
  arguments after the first NULL and does not invoke the function for that row.
- `seekdb_rust_concat3_called(left, middle, right)` shares that implementation
  but does not declare NULL propagation: all arguments are evaluated and the
  plugin is called even for NULL. It validates non-NULL inputs before returning
  NULL. Both functions have scalar and indexed batch entries, preserve empty
  strings and embedded NUL, and reject a result larger than 16 MiB. These are
  explicit three-byte-argument functions, not MySQL CONCAT coercion emulation.
- `seekdb_rust_sql_chars(TEXT)` uses parameterized host SQL
  `SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))`. It opts into
  execution context v2, propagates host errors, and has no native fallback.
- `seekdb_rust_text(TEXT)` validates UTF-8 and returns the custom `rust_utf8`
  logical type (`org.seekdb.rust-text.utf8`). Empty text and embedded NUL are
  preserved; malformed UTF-8 is rejected. NULL returns typed NULL.
- `seekdb_rust_char_count(rust_utf8)` is a separate typed overload with its own
  object identity, sharing the character-count implementation service.
- `seekdb_rust_identity(value)` uses a metadata-only Rust callback to resolve
  its result as the input logical type. It preserves bytes and typed NULLs;
  an unknown NULL is assigned `core.type.bytes` by this plugin's policy.
- `seekdb_rust_identity_bytes(value)` declares the bytes input signature and
  uses the same dynamic service. A `rust_utf8` input is implicitly converted
  first; result inference receives bytes too, not the original custom type.
- `seekdb_rust_words(rust_utf8)` streams whitespace-delimited `token` (custom
  `rust_utf8`) and one-based int64 `ordinal` columns. Unicode whitespace is used;
  this is a reference tokenizer, not a linguistic word-segmentation algorithm.
- `seekdb_rust_words_bytes(bytes)` returns the same columns but its separate Rust
  entry accepts only bytes. Passing `rust_utf8` exercises the host's implicit
  type-to-bytes cast before opening the stream.
- `seekdb_rust_words_or_null(bytes)` emits a `<NULL>` token for NULL input;
  `seekdb_rust_words_strict(bytes)` binds the same implementation but declares
  NULL-propagating behavior, so a NULL after conversion produces zero rows
  without invoking the table callback. Empty bytes remain an empty stream.
- `seekdb_rust_sql_series(bytes)` queries character length during open and uses
  parameterized SQL during next to return one `ordinal` column (1 through that
  length). NULL/empty inputs return no rows. This demonstrates SQL in both phases,
  not an efficient series generator; it is not marked immutable/deterministic.
- `seekdb_rust_routine_id(bytes)` looks up a FUNCTION in the caller's current
  database and authorized schema view; absent names and NULL return SQL NULL.
- `seekdb_rust_routine_ddl(bytes)` uses the experimental query catalog mutation
  API for one standalone routine CREATE, MySQL attribute ALTER or DROP. The
  affected ID is provisional in the caller's transaction, not proof of commit;
  absent DROP IF EXISTS and NULL input return SQL NULL. Normal permissions apply,
  errors propagate, and objects do not automatically become extension members.
  End-to-end live database transaction validation remains outstanding; see the
  [query catalog contract](../../docs/developer-guide/zh/plugin-query-catalog.md).

- `seekdb_rust_optimizer_calls()` returns the process-module planning-hook call
  count. The associated `optimizer.plan.v1` hook increments an atomic counter
  and calls the next planner stage. This observes planning, not query executions:
  cache hits do not increment it, and planning the counter's own SQL can do so.

Nine function descriptors, the custom type, a bytes-to-type explicit cast,
a type-to-bytes implicit cast, five table functions and one optimizer hook
are contributed during `init` through one registration transaction. The cast
cost is 1 in each direction; bytes-to-type is not eligible for implicit conversion.
`start` enables callbacks,
`stop` rejects new execution, and the
host owns leases and final unloading. There are no background threads or model
allocations in this example.

The scalar host-SQL helper now uses the SDK's general typed `execute_sql` path.
Plugins needing multiple parameters or rows can use that API directly; see the
[SQL SDK example](../../rust/extension-sdk/README.md#composing-sql-from-a-rust-plugin).
The original two function signatures remain available.

The native and host-SQL count functions now use the SDK's `scalar_function!`
declaration macro. It generates their fixed-result registration definition,
service table and FFI boundary; `provide` reuses the declared service ID in the
manifest. The plugin still explicitly checks `STARTED` and its instance handle,
selects service versions/capabilities, validates arity/types and computes results.
The typed overload continues sharing the native count service. Dynamic identity,
type codec and cast callbacks remain hand-written through the same public SDK;
the macro does not impose a new lifecycle or generate installation SQL.

## Type codec and conversion

The custom type's physical format `org.seekdb.rust-text.utf8.v1`, version 1,
contains validated UTF-8 bytes without an extra envelope. `decode` validates and
emits a custom-typed value; `encode` validates the logical input type and emits
`core.type.bytes`. Empty encoded input is a non-NULL empty value; nullness is a
separate value property, not a byte sequence. Both callbacks use the SDK byte
helpers and the same lifecycle instance checks as the functions.

The manifest now declares nineteen implementation services: routine mutation, routine lookup, character count, concat3,
host-SQL count, codec, bytes-to-type, type-to-bytes, dynamic identity, words and
words-bytes, words-or-null, SQL-series, planning, planning-count, catalog-install,
and the three stored-text codec/conversion services. Its build ID is `rust-text-owned-memory-v15` and data-format declaration
is 1. Deploy the matching
manifest and library together, not the previous two-service manifest with the
new binary. The original `rust_utf8` type remains nonpersistent; it is not
silently changed into the separate persistent type below.

The codec now has an optional minor-1 comparison suffix, implemented with the
Rust SDK's `type_comparison::Comparator`. It orders by Unicode scalar count,
then exact UTF-8 bytes (`z` precedes `aa`), without locale or normalization.
The loader, scalar SQL comparisons, typed `BETWEEN`/`NOT BETWEEN`, scalar/row
`IN`/`NOT IN`, simple CASE matching and flat/nested row comparisons invoke this under
bound type/codec leases. BETWEEN resolves one common type for all three operands
and evaluates its value once. IN similarly
resolves the value and candidates together, evaluates the value once, and stops
at the first match while retaining SQL NULL/error semantics. Flat-row IN shares
converted selector columns across candidate rows and combines three-valued row
equality; a NULL selector column does not automatically skip the list. Nested
row constructors validate matching shapes before comparing corresponding leaves,
including shared converted leaves across IN candidates. This is not a persistent
composite/record type ABI; set-subquery comparisons remain unsupported. Simple CASE resolves each
selector/WHEN pair independently and shares the selector (including its storage
decoder) across conditions; result branches retain independent type inference.
Row comparisons bind each column pair independently, preserving lexicographic
ordering and whole-row three-valued equality without native carrier shortcuts.
Real SQL batch-frame tests cover these consumers with skip masks, repeated
evaluation and changing batch sizes. Entirely skipped batches do not invoke
plugin callbacks, including literal-input functions/casts inside CASE branches;
the host no longer misclassifies those nodes as scalar constants. Error and
mid-batch cancellation checks retain actual Rust callback/lease evidence. This
uses per-value type comparison callbacks, not a columnar/zero-copy or storage-scan test.
Binary comparisons, BETWEEN and scalar IN now batch their operands, so nested
Rust functions need not fall back to the host's scalar-function entry. BETWEEN
masks NULL main values; IN advances one candidate at a time and excludes matched
rows while preserving UNKNOWN. Expanded 6/3/1031-row tests distinguish function
batch rows from scalar entry and keep per-value comparator/decoder counts.
MIN/MAX now retain a fixed comparator binding on their input carrier and use it
in row/batch aggregation and rollup state merging. The aggregate result retains
its logical type; native BINARY casts still use native byte ordering. Tests use
the real aggregate processor and Rust DSO with supplied frames/configuration.
Additional SELECT tests use top-level resolution, rewrite, optimizer and full
codegen to execute generated aggregate/table-function operators, with scalar
and 3-row batches, DISTINCT, NULL/empty inputs and rescan. Their schema and
inner-session environment remain fixtures, not a live server or storage scan.
Ordinary scalar aggregation no longer requires an unused temporary-file directory.
See the
[MIN/MAX contract](../../docs/developer-guide/zh/plugin-type-extrema.md).
Explicit SELECT ORDER BY now binds the same comparator after type deduction.
Generated sort and Top-N operators are tested in scalar/3-row batches, with
NULL CASE, nested identity, duplicate keys, LIMIT/OFFSET, rescan, 1,031-row
inputs and cancellation after the first comparison. Native BINARY is a control.
Native sort-key encoding and pushdown Top-N filters are disabled for plugin keys.
PX receive/coordinator heaps and local-order comparison now share the same
contextful dispatch. Component tests use both real PX heap implementations with
generated SQL metadata and the Rust DSO, supplied channel rows, empty channels,
NULL/duplicate keys, native controls, invalid values/bindings and cancellation.
Heap algorithms retain comparator errors through reference passing and reset
them on reinitialization. Full PX planning/scheduling/DTL/cross-process restore
still needs end-to-end evidence. Ordinary RANGE now uses fixed TYPE bindings in
sample sorting and scalar/batch boundary search. Component tests execute real
sample splitting and routing with supplied samples, NULL/duplicate keys, multiple
task counts, skipped invalid rows and cancellation. Comparison failures do not
publish valid routing results; in-memory values need no LOB read service.
RANGE evaluates distribution keys through the batch entry before probing cached
rows. Generated nested identity expressions are fed at their input column, not
precomputed at their root: host scalar-call counts stay unchanged while batch
calls increase. The v2 service can still fall back to scalar execution inside
the loader; this is not native v3 or zero-copy evidence. Single-key DDL output
tests retain the two-key controls and check raw range IDs, empty ranges, errors,
cancellation and skipped rows. Separately generated native BINARY specs exercise
sample sorting and routing without invoking the Rust comparator.
Slave-map partition RANGE now uses the shared TYPE comparator and has a staged
batch routing entry. Tests copy actual sample cuts into a real SQC handler and
exercise per-tablet channel maps, unequal range/channel counts, empty ranges,
invalid bindings/tablets/channels, cancellation, output atomicity and reinit.
Both RANGE paths prepare DDL output storage only for active rows, preserving
skipped datum pointers and values even when they reference borrowed buffers.
Nested identity keys are recomputed from their input column through the host
batch entry. Tablet IDs are controlled inputs; PDML and DDL outputs use production
pseudo-column builders and the normal expression generator. The actual transmit
batch loop now carries per-row tablet IDs for capable calculators instead of
falling back solely because a tablet-ID output is present. Captured-channel tests
cover RANGE, affinity and partition-random sends, skipped rows, zero data sends
on batch routing failure, output values and batch-ID lifetime invalidation.
An additional fixture generates `calc_tablet_id` with the production builder and
normal optimizer/codegen. An in-memory two-level RANGE schema feeds real DAS
partition lookup and one-sided first-level remapping, with deliberately distinct
partition and tablet IDs. The actual sender checks successful mixed-tablet
batches, no sends on a missing partition, and skipped invalid rows. Reinitializing
the same calculator exposed an uncleared part-to-tablet map; destroy now clears
both maps. The full kernel run passes after isolating these SQC ranges from the
older bad-tablet fixture. The partition key is native integer, not a persisted
Rust type; Rust comparison supplies the within-tablet distribution order.
A second 3-by-3 schema now exercises subpartition-only and full two-level
calculation through generated expressions, actual DAS/schema lookup and the
batch sender. Rebinding one SUB expression/calculator across first partitions
checks context reuse; mixed-parent channel metadata is rejected without
overwriting its context. Partition expression equality now distinguishes the
calculation mode for tablet-ID and combined-ID results as well as partition-ID
results. The kernel test first reproduced the unequal-mode equality defect,
then passed with same/different-mode controls and the full routing matrix.
Both-level missing-partition batches send no data; skipping the bad row preserves
the other rows. Native BINARY comparisons bypass Rust; plugin comparisons retain
their fixed bindings. Integer partition-input frames and range keys are supplied
by this fixture, while the expressions, schema lookup and send loop are real.
Full partition-exchange plan generation, scheduling, sample-message transport, persistent DDL/
recovery, spill and cross-process behavior still require verification. The
capture fixture does not exercise network transport, backpressure or
asynchronous task EOF. Calculators without batch-ID support retain scalar routing.
Selected two-level sends now include the actual typed value in a DTL datum block.
The production datum writer encodes it, then the buffer is copied to distinct
addresses and the sending allocation is overwritten and released. The real
receive reader restores tablet/DDL IDs, Unicode text and NULL into independent
evaluation frames using both scalar and batch reads, including a three-row
single-channel buffer read as 2+1. Invalid batch requests cannot consume rows;
all DFC buffer allocations must be returned. This covers datum blocks, not RPC
envelopes, plan serialization or the complete receive operator. Writer calls
follow the production switch-buffer path, which skips a second unswizzle for
datum blocks. Outrow LOBs and stored-type transport need separate coverage.
Both the original single-block stream and a row-per-block stream now pass through
the actual reader. Cross-block 2+1 reads and early reset exercise ownership of
iterated and unread buffers. Batch attach validates every row's width/pointer and
the destination expressions before touching frames, rejecting both missing and
extra columns. A trailing bad row cannot partially publish earlier rows, and
failed batch reads report zero valid rows. Shape failures may consume input;
they require terminating/resetting the receive operation, not retrying that batch.
The same-channel burst uses distinct values selected by the independent range
oracle, so byte lengths, NULL and DDL range IDs can differ while the channel stays
fixed. A further fixture feeds independent copies into the real FIFO receive
operator and calls its public open/batch/row/rescan/close APIs. It checks 2+1
batches, the vector-to-row adapter, repeated EOF, rescans after exhaustion and
after one row, and early close. Only channel linking is replaced: no live
channels are registered, so all_eof(0) represents a completed transport handoff.
This covers local operator lifecycle, not SQC linking, network message processing,
asynchronous EOF, backpressure, actual channel teardown or plan serialization.
A separate channel fixture now registers two real local receive channels and
receive-side DFC with the FIFO operator. Buffers enter through feedup/attach and
watcher notification; the production channel loop and row processor transfer
them to the reader. An empty channel finishes first without ending the other
channel. Data-bearing EOF, separate empty EOF, public batch/row reads, early
close and unsupported payload tags exercise actual dispatch and error propagation.
Queue and aggregate DFC bytes/buffer counts, channel ownership counts and memory
manager returns must balance. The fixture owns channel setup/teardown outside
the global DTL map and never rescans these live channels. SQC discovery, real
asynchronous scheduling, RPC, blocking/unblocking and exchange plan serialization
still need verification; low-pressure DFC accounting is not a backpressure test.
The linked-exchange fixture additionally uses the original transmit callback's
message/evaluation context with actual channel.send/flush. Paired channels are
created in the global DTL registry with real transmit/receive DFC; production
writer, peer lookup/release, local attach and FIFO reads deliver the values into
independent frames. Single-block 2+1 reads and synchronously interleaved row-block
sends check actual block counts, contents, EOF and registry pin ownership.
Removal must leave zero pins, subsequent lookup must fail, and teardown must
leave the global channel table/count empty with all buffers returned. This does
not cover SQC discovery/handshake, asynchronous EOF workers, concurrent backpressure,
active-channel rescan, cross-process transport or exchange plan serialization.
LinkedExchange now invokes the production ObTransmitEofAsynSender for EOF.
A separate three-channel fixture checks success and a missing peer at each
position using real local channels: all actions must be attempted, the aggregate
call must report failure, healthy peers must receive EOF, and failed buffers
and lookup pins must be released. Completion errors are collected by asyn_send;
a subsequent wait_response is a no-op, not a repeat of the old failure.
In the current local implementation, "async" means batched initiation followed
by completion waits; it does not start a dedicated EOF worker. This does not
prove concurrent scheduling, delayed responses, threshold-crossing batch splits
or backpressure. The missing-peer matrix sends empty EOF, not corrupt typed data.
A further pressure case repeats one original non-NULL typed message until the
unchanged production DFC policy blocks reception. A 1024-byte channel buffer
limits physical allocations; no block flag or policy threshold is injected.
The real transmit wait must time out without an unblock message. Draining the
receive queue then generates an actual unblock control message, which the next
send/flush consumes before transmitting another row. Message dispatch and control
buffer exhaustion are checked separately. Both sides must record one blocking
episode, with values, EOF, pins and allocations conserved. This is sequential
protocol testing, not concurrent worker wakeup, cancellation-race, fairness or
throughput evidence; restoring a low-level DFC deadline does not imply that a
timed-out SQL query may resume.
A rescan stream now reuses the same registered channel pair across fully drained
epochs. Actual fill_px_batch_info and public receive/transmit rescan calls reset
the batch ID and EOF state; the fixture no longer duplicates transmitter channel
reset operations. Each epoch checks its original tablet/DDL, NULL and byte value,
including differing inputs, cumulative buffer counts, pins and final cleanup.
The real intermediate-result manager holds empty old-batch and unrelated-batch
markers: receive rescan must erase only the former, and fixture cleanup leaves
the result map empty. These markers test key-scoped cleanup, not typed spill.
The auxiliary transmit operator only runs init/rescan on a borrowed channel;
its open/transmit, child execution and SQC scheduling are not exercised here.
This is not mid-stream queue rescan, concurrent cancellation, sampled-row recovery
or complete exchange plan generation/serialization. Both receive-only and final
receive/transmit rescan kernel runs passed, with build freshness checked against
the intermediate-result manager and exec-context implementation as well.
The temporary-file fixture rejects all file opens: this is not external spill,
storage or full PX scheduling evidence. See the [ordering contract](../../docs/developer-guide/zh/plugin-type-ordering.md).
**Grouping keys and indexes still require their own callback integration**. Old codec
prefixes and decode/encode behavior are unchanged. See the
[comparison contract](../../docs/developer-guide/zh/plugin-type-comparison.md).

### Scalar function batching

The existing `seekdb_rust_char_count` service now also provides a v3 batch
callback in `src/batch_count.rs`. One Rust handler reads the whole compact batch
and emits indexed results in reverse order; the host stages and orders them.
Its scalar entry, SQL object identity and stored data format remain unchanged.
The SQL adapter and old-service fallback are described in the
[scalar batch contract](../../docs/developer-guide/zh/plugin-scalar-batches.md).

Function arguments now propagate batch evaluation into nested Rust functions,
including through SQL cast/type carriers. Strict NULL rows are excluded before
later arguments run. Tests distinguish outer/inner batch calls from scalar
function entry, including a concat/count pipeline with an intervening cast.
New function bindings represent stored arguments as explicit cached decoder
expressions, so later nested functions can keep batching after decoding. Real
Rust stored-column tests check single decoding, NULL short-circuiting, malformed
storage and incremental evaluation. Conversions/codecs still use per-value
callbacks; legacy direct stored-argument metadata retains a deferred suffix,
as documented in the contract.

### Persistent reference type

`stored_text.rs` registers `rust_stored_utf8` with independent logical ID
`org.seekdb.rust-text.stored-utf8` and physical format
`org.seekdb.rust-text.stored-utf8.v1`, version 1. Its runtime value is UTF-8;
storage is four bytes `52 55 54 01` followed by the bitwise complement of each
UTF-8 byte. This is deliberately not a sortable encoding, encryption or a
checksum. The comparator consumes decoded text and uses scalar-count/byte order.

The type declares PERSISTENT and REQUIRES_CATALOG; the module also declares the
separate persistent-data capability. Explicit bytes-to-type and implicit
type-to-bytes casts operate on logical values, not storage envelopes. Encoding
adds the envelope; decoding validates the header/version and UTF-8. NULL remains
SQL metadata, while an empty non-NULL value has the four-byte envelope.
Encoded data is bounded to 16 MiB (including the header); oversize values fail
without truncation. Neither conversion nor decoding normalizes Unicode.

The persistent type is an experimental reference, not a promise of production
restart/migration/index support. The added kernel fixture uses schema-derived
column metadata and the real in-row LOB reader with the actual Rust DSO, but
supplies the row buffer itself; it does not exercise live table I/O or recovery.
Keep the new format identity/version stable if extending this example.

All four words table descriptors now use a minor-2 table execution service with
query control and a Rust planning estimator (`table_planning::Planner`). The example
returns a deliberately simple prior: 8 rows, 40 bytes per row, total cost 4 in
host optimizer cost units. This is not a measured tokenizer model; it does not
evaluate the argument to count actual tokens, and does not limit rows emitted
at execution. The callback validates the declared post-coercion argument type
and column count. Existing minor-0 C table functions retain the previous host
default estimates (199 rows/199 bytes/cost 1).

The scanner opts into `table_planning::Service::WITH_PROJECTION`. On supported
hosts it polls before scanning, every 4096 characters within a long token or
whitespace run, and before emitting a token. Old v1 contexts retain the original
scan path. This is cooperative cancellation, not interruption of blocked native
code. The SQL host closes a failed cursor and keeps the error until rescan/close;
query pointers never survive the current callback. The poll-only table context
does not grant SQL execution. See the [query-control contract](../../docs/developer-guide/zh/plugin-query-control.md).

Vectorized SQL table scans now pass their batch limit to the existing Rust
`Rows` interface: one native next may emit multiple rows. Projected SQL columns
retain descriptor ordinals, including scans that only consume row counts.
This still uses synchronous row emission, not a columnar ABI or a zero-copy
claim. See the [batch contract and verification limits](../../docs/developer-guide/zh/plugin-table-batches.md).

Planning pins the table object and implementation just like execution, but
does not create a cursor, run casts or give the callback SQL/session access.
The estimate enters the real `FunctionTablePath::estimate_cost`, the relation's
cardinality/width and the normal candidate comparison. It does not add a custom
path, new physical operator or plan replacement. See the SDK's
[planning interface](../../rust/extension-sdk/README.md#table-function-planning).

The SQL series uses table SPI minor 3 (`table::Service::WITH_SQL`) with default
estimates and requires SQL capability on every open/next. Its cursor owns two
integers, never a query pointer or borrowed SQL row. Raw rescan lacks SQL context
and is rejected; the SQL executor rescans by closing/reopening. The host SQL API
retains caller permissions/transactions and fails the invocation on errors.
See [SQL lifecycle and validation scope](../../docs/developer-guide/zh/plugin-table-sql.md).

The core loader now has `decode_type`, `encode_type` and `execute_cast` adapters
for previously resolved object identities. They acquire the object and service
leases together, resolve the owning instance, and invoke the validated callback
without holding the loader lock. Codec calls receive the exact v1 context, even
from extended callers. Wrong type/format, stale generation and terminally stopped
objects are rejected. Caller-provided result sinks must validate and copy output.
SQL-facing `decode_bound_type` and `encode_bound_type` additionally accept the
pointer-free TYPE binding returned by normal SQL resolution. They validate its
bounded identifiers, owner/generation, physical format/version and flags before
reacquiring the exact joint leases. A persisted generation-zero identity is not
an executable binding. These methods are exposed through the server provider;
they do not change the Rust plugin's public C ABI or require C++ registry objects
inside a SQL plan. Unrelated registry epochs do not alter a type's format identity.
These adapters do not themselves connect codecs to column storage or implement
new SQL CAST grammar, implicit-coercion policy, operator classes or index hooks.

Bound scalar function execution now applies the direct implicit conversions
already considered by the Rust overload resolver. For example, a host binding
of `seekdb_rust_sql_chars` with a `rust_utf8` argument selects the bytes signature,
then calls the Rust type-to-bytes conversion before the function. Cast selection
uses the existing cost/object-ID order. All selected object/service leases and
argument compatibility checks are prepared before any cast runs; they stay live
through the target function's result callback. Cast output is type-checked and
copied into host-owned argument storage, with a combined 16 MiB output budget.
Missing, malformed or repeated cast results fail instead of reaching the target.

Conversion-dependent bindings must retain the catalog epoch observed during
resolution until conversion preparation finishes. A changed epoch returns a
state-mismatch error; this is conservative invalidation, not automatic rebinding.
Once prepared, the pinned function/cast generations finish the call even if new
catalog changes occur. Unknown NULL is assigned the expected type; a typed NULL
requiring conversion is delivered to its cast rather than assuming strictness.
This loader path covers direct, one-hop scalar and table-function conversions,
not automatic cast chain search or complete prepared-plan dependencies.
The SQL adapter separately supports explicit `CAST(value AS rust_utf8)` and
`CONVERT(value, rust_utf8)`, including quoted target names. Nested SQL conversions
compose explicit expression steps; they do not enable implicit bytes-to-type casts.

Directly nested plugin scalar expressions now preserve the constructor's logical
result type when resolving the outer overload. Codegen stores that binding and
the actual argument type IDs in plan-owned extra information, including across
deep copy and serialization. Execution borrows those IDs rather than deriving
them again from the physical varchar representation. Custom type names are not
classified as built-ins by their suffix. A changed physical result layout at
codegen or inconsistent nested logical bindings is rejected.

The kernel suite retains the controlled-provider expression regressions and also
has a combined SQL-expression/real-Rust-DSO fixture. It parses literal and nested
SQL, binds against the production registry, runs normal type inference/codegen
and frame allocation, then evaluates through the actual loader and Rust callbacks.
The fixture checks UTF-8, empty text, embedded NUL, typed NULL, same-type and
cross-type conversions, typed overloads, dynamic identity and invalid UTF-8
followed by another valid call. Native authorization/catalog commit remain test
doubles; no running SQL server, persistent storage, permissions or transaction
rollback is simulated by this fixture. Complete type propagation through arbitrary
wrappers/optimizer rewrites and prepared-plan invalidation remain kernel work.

## Build and test

The dynamic functions require a host implementing the minor-2 scalar result
resolution suffix (`FunctionServiceV2`). Planning uses only logical type metadata,
not values or SQL/session state. The host pins object/implementation leases and
checks the catalog epoch before and after resolution. The callback must be pure
and generation-deterministic; native-code trust remains unchanged. An empty or
malformed result, callback error, or absent service suffix prevents binding.
The existing fixed-result functions and C/C++ v1 services remain supported.

The real Rust DSO regression checks bytes, int64, custom type, unknown NULL,
post-coercion result inference, invocation and shutdown. This is not a live SQL
server test or a complete PG polymorphic-type system (e.g. anycompatible/typmod).

From the repository root, with the normal seekdb dependencies configured:

```bash
cmake -S . -B build_release -DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=ON
cmake --build build_release --target seekdb_rust_text_plugin -j2
```

The output is
`build_release/plugins/rust_text/cargo-target/release/libseekdb_rust_text.so`
on Linux. CMake always runs the Cargo local-dependency audit and final binary
audit, including on incremental builds. Cargo performs the incremental compile.
An ordinary `cargo build` alone does not run these CMake boundary gates.

To collect the library and manifest into a new deployable directory, use
[`cargo-seekdb package`](../../rust/cargo-seekdb/README.md). It runs the same audited
CMake target and never overwrites an existing package or installs into a server.

The SDK/plugin use separate workspaces with `panic=unwind`; the host workspace
keeps its existing panic strategy. Use the repository's pinned Rust toolchain:

```bash
cd rust
cargo test --manifest-path extension-sdk/Cargo.toml --offline
cargo clippy --manifest-path extension-sdk/Cargo.toml --offline --all-targets -- -D warnings
cargo clippy --manifest-path ../plugins/rust_text/Cargo.toml --offline --all-targets -- -D warnings
```

Standalone host integration, from the repository root:

```bash
cmake -S rust/plugin-runtime/tests -B build_release/plugin-runtime-tests
cmake --build build_release/plugin-runtime-tests -j2
ctest --test-dir build_release/plugin-runtime-tests --output-on-failure
```

This loads actual Rust and C/C++ dynamic libraries using the production loader,
registry and Rust host. Tests exercise direct registration, failed publication
cleanup, UTF-8, NULL, v1/v2 contexts, host SQL errors and lifecycle shutdown.
The catalog and SQL executor are test doubles: a passing CTest does not establish
real database transaction, permission or cancellation behavior.

Combined kernel and Rust DSO regression, after completing the production build:

```bash
cmake --build build_release --target seekdb -j2
python3 rust/plugin-runtime/tests/kernel_script.py --build-dir build_release
```

The runner verifies that the production executable is current, uses its actual
compiler/linker flags, builds and audits `seekdb_rust_text_plugin`, and installs
a private copy of its library and manifest via the generated CMake install rules.
It explicitly installs the native plugin subdirectory because it is excluded
from the default top-level build/install. It then activates and finally shuts
down the real module. The `--overlay-only` mode does not build or load this DSO.

## Table streams

`seekdb_rust_routine_id(bytes)` uses the caller's query catalog capability to
look up a standalone FUNCTION in the current database, returning its ID or NULL
when absent/input NULL. Normal SHOW visibility applies; permission failures are
errors. It is not immutable, a schema lease, or permission to execute the routine.
See [query catalog](../../docs/developer-guide/zh/plugin-query-catalog.md).

The words services advertise table SPI minor 4 and consume optional projection
metadata on each next callback. Unrequested token/ordinal cells emit valid empty
bytes/zero placeholders, while scanning, row count and ordinal state continue
unchanged. Absent metadata requests both columns; an all-zero mask still produces
all rows. SQL-series remains minor 3 and receives an exact v3 context. This is not
columnar output and no speedup is claimed. See the
[projection contract](../../docs/developer-guide/zh/plugin-table-batches.md).

`words.rs` uses the public SDK's `table::Cursor` and generic FFI service. It
owns one UTF-8 input string, walks token boundaries incrementally and returns
borrowed token slices only during synchronous row emission. Cursor state is
released on close/rescan; there are no worker threads or token-list caches.
The `token` column retains `org.seekdb.rust-text.utf8` identity, so an outer
`seekdb_rust_char_count(token)` selects the typed overload. The SQL adapter passes
NULL without reading its physical payload. Non-strict functions receive it after
any required implicit conversion and choose their result; the original words
entries still choose an empty stream. Empty/whitespace-only input is non-NULL.

The bytes-only entry provides a conversion regression that cannot pass merely
by accepting the original custom input. The loader validates and pins the whole
argument conversion set before any plugin callback; the cursor retains those
object/code leases until close. Direct cursor rescan converts new values using
that same selection and rejects changes to the original argument type/count.
Conversion or cursor failure disables next until successful rescan. An unrelated
catalog publication rejects stale describe/new-open requests but does not abort
an already admitted cursor or reselect its casts. The SQL operator's rescan still
closes and later reopens, so it performs new admission on the fixed binding.
Table SPI v1 has no SQL context suffix; SQL-dependent casts cannot receive an
invented query context.

NULL-propagating tables are checked after implicit conversion, because a cast
can turn NULL into a value or a value into NULL. An initial strict empty open
returns host `OB_ITER_END` with no cursor and no table callback; the SQL adapter
caches EOF until rescan. Direct rescan of a live cursor to strict NULL keeps that
cursor dormant and retains its leases, without calling table rescan/next. A later
non-NULL rescan resets the original cursor; close always releases it. This needs
no saved query context, replacement cursor or ABI layout change. Raw SDK service
calls do not know the SQL descriptor flags; strictness belongs to host admission.

The kernel regression now parses complete SELECT statements with real Rust
table bindings and generated expressions, checking Unicode tokens/ordinals,
typed scalar consumers, empty/NULL inputs, fixed lookup counts and repeated
scans. Catalog activation still uses the controlled test guard; this does not
establish live-server authentication, installation transactions or recovery.
The final fixture drives the real `ObFunctionTableOp::open/get_next_row/rescan/
close` lifecycle, including query timeout and repeated close. Its spec is manually
assembled from generated expressions, so automatic optimizer-to-spec generation
remains a separate verification task.

## Server usage

For database-local SQL wrappers managed as Extension members, see the
[`rust_text_ops` package](../sql_packages/rust_text_ops/README.md). It declares
this already installed module as a dependency and supplies SQL installation/
update scripts; it neither loads a second runtime nor unloads this library when
its SQL objects are dropped. Its live-server transaction verification is still
outstanding; current tests cover real PL resolution through this Rust module.

Place the library and this directory's `plugin.toml` together under `rust_text/`
in the server's configured trusted plugin directory. Example SQL (requires a
server built with plugin support):

```sql
INSTALL PLUGIN rust_text SONAME 'rust_text/libseekdb_rust_text.so';
SELECT seekdb_rust_char_count('A中🙂');
SELECT seekdb_rust_sql_chars('A中🙂');
SELECT seekdb_rust_char_count(seekdb_rust_text('A中🙂'));
SELECT token, ordinal, seekdb_rust_char_count(token)
FROM TABLE(seekdb_rust_words(seekdb_rust_text('hello 中🙂 world')));
SELECT token, ordinal
FROM TABLE(seekdb_rust_words_bytes(seekdb_rust_text('hello 中🙂 world')));
SELECT token FROM TABLE(seekdb_rust_words_or_null(NULL)); -- one <NULL> row
SELECT token FROM TABLE(seekdb_rust_words_strict(NULL));  -- no rows
SELECT seekdb_rust_char_count(NULL), seekdb_rust_sql_chars(NULL);
UNINSTALL PLUGIN rust_text;
```

With a UTF-8 client/session the first two calls should return 3. This server
sequence has not yet been executed in the current sandbox. The checked-in
manifest names the Linux `.so`; other platforms need matching package metadata
and ABI/build validation. Bazel plugin packaging is not implemented yet.

The standalone Rust loader test additionally resolves the actual type and cast
metadata, invokes the Rust codec/cast/constructor/typed overload from the rebuilt
DSO, and checks UTF-8, empty/NUL-containing input, NULL, malformed bytes, host
emit failure, stale/changed identities and rejection after terminal shutdown.
It also verifies implicit type-to-bytes conversion before the SQL-aware function,
typed NULL conversion, rejection of an old conversion epoch, and no target SQL
call after invalid UTF-8 conversion. White-box tests check copied cast output,
wrong types, null pointers, invalid sizes, byte limits and sticky duplicate/error
emission. These complement, rather than replace, the real-DSO execution test.
The emit fixture checks live leases and reenters an administrative loader read
to detect callbacks invoked under the loader mutex. Catalog publication remains
a fixture: this is not real SQL parser/column persistence/coercion or concurrent
database transaction evidence.

See [the SDK](../../rust/extension-sdk/README.md) and
[the complete implementation scope](../../docs/developer-guide/zh/plugin-implementation-status.md).
## SQL package generation

From the checkout, `cargo run --offline --manifest-path rust/cargo-seekdb/Cargo.toml
-- schema --manifest-path plugins/rust_text/Cargo.toml --output /tmp/new-rust-text-ops`
executes the build-time `examples/seekdb_schema.rs`. It derives the `rust_text_length`
routine from `count_function::DEFINITION`, combines handwritten custom-type SQL
and the explicit 1.0→1.1 update, and emits the `rust_text_ops` package. No lifecycle
callback or server SQL executes during generation. The accompanying rlib only
lets this example share metadata; production still loads the audited cdylib.

The kernel runner compares all three generated files with the CMake-installed
package and resolves that same SQL against the actual Rust native module. The
checked-in SQL is a reviewable delivery snapshot; update it with the generator
when changing declarations. Generated wrappers use separate SQL names and
INVOKER security; this is not PG native-symbol DDL or arbitrary catalog writes.
See [tool protocol and limits](../../rust/cargo-seekdb/README.md).

The separate `seekdb_native_schema` Cargo example generates `rust_text_native`
with `InstallSource::Native`, containing control and its explicit update but no
base SQL. Select it using `cargo seekdb schema --example seekdb_native_schema`
with the same manifest/output options. Both generated packages are byte-compared
against installed artifacts by the kernel runner; no installation callback runs
during either generation command.

`seekdb_versioned_schema` generates a reference pure-SQL `consumer` package with
two migration steps and `VersionControl` declarations. Version 1.0 inherits
`alpha,zulu`; the intermediate version uses `migration`; target 1.1 overrides
the durable requirements with `gamma,zulu`. Select it with
`cargo seekdb schema --example seekdb_versioned_schema` and the same manifest/output
options. These provider names are illustrative, not packages automatically
installed by the command. The kernel runner uses the generated artifacts in its
actual package planner/catalog update tests with controlled provider rows.

## Installation-time catalog declarations

The service now advertises catalog SPI 1.1 with an unchanged prepare context and
an additional transaction-bound build callback. `rust_text_ops` and
`rust_text_native` retain their existing prepared declarations. The new
`rust_text_built` package supplies only control: its Rust build callback creates
`rust_built_length`, obtains the reserved ID, and creates `rust_built_nonempty`
referencing that function in the host's transaction-local schema view. The loader
keeps a module lease through installation/publication. This is not query-time
DDL, and callback return is not proof of commit. See the
[package example](../sql_packages/rust_text_built/README.md).

`src/catalog.rs` advertises `org.seekdb.rust-text.catalog.install` through the
public C ABI and implements the Rust SDK's `catalog::Installer`. When installing
`rust_text_ops`, it generates and submits `rust_runtime_length(TEXT)` from the
same native character-count definition. The native-source `rust_text_native`
package receives `rust_native_length` as its entire initial object set, without
a base SQL file. Other package names receive no extra
declarations. The two file-backed routines remain unchanged; the third routine
is created only by `CREATE EXTENSION`, not by `INSTALL PLUGIN` or the build-time
schema generator. It uses the installing database and owner, with INVOKER security.

The host copies each fragment, preserves parser boundaries and pins the module
until installation and schema publication finish. The callback has no SQL or
transaction API: successful `declare_sql` means staged text, not a created
object. Normal permissions, routine dependencies, membership and installation
transaction checks still apply. Updates continue using explicit SQL scripts.

See the [installation catalog contract](../../docs/developer-guide/zh/plugin-catalog-install.md)
for limits, lifetime rules and the distinction from future query-time builders.
