# seekdb-extension: public Rust plugin SDK

This initial SDK wraps a subset of seekdb's public C plugin ABI. It is an
independent Cargo workspace, not part of the host's `panic=abort` workspace.
It has no external crate dependencies. A plugin should produce a `cdylib` and
use an unwind-enabled profile if it wants to convert Rust panics into status
errors. See [rust_text](../../plugins/rust_text/README.md) for a complete example.

## Available interfaces

- `scalar_function!`: a module-level fixed-result function declaration generating
  `DEFINITION`, `SERVICE`, and `provide(version, capabilities)`. It wraps the raw
  callback with instance admission, context validation and the SDK panic/error
  boundary. The handler receives an opaque instance and a borrowed `Call`; SQL
  capability, service versions, volatility flags and lifecycle checks stay explicit.
- `sys`: `#[repr(C)]` layouts and callback types for the manifest, services,
  registration, scalar/table execution and SQL execution APIs. This is not a complete
  binding for every public descriptor yet.
- `Registration`: init/start-only registration transaction. Uncommitted and
  failed-commit transactions are automatically aborted. Function/type names and
  argument type names may be generated dynamically: the host deep-copies
  descriptors before registration returns. Implementation version ranges and
  capabilities are explicit plugin choices, not hardcoded SDK policy.
- `sys::IMPLEMENTATION_ONLY`: scalar registration without a global SQL name,
  intended for database-local `AS 'module-id', 'implementation-id' LANGUAGE C`
  declarations. Set this in a function definition's `flags`; `sql_name` is then
  only an optional diagnostic label (use `c""` for an unnamed definition).
  Exact owner/object-ID binding and lifecycle inventory remain available.
  `schema::scalar_wrapper` rejects these definitions because a `RETURN name(...)`
  wrapper cannot resolve an implementation that has no published SQL name.
- `TypeDefinition`, `CastDefinition`, `ImplementationReference`, `CastContext`:
  byte-oriented type/codec registration and explicit, assignment or implicit
  conversion descriptors with plugin-selected cost, flags and service versions.
- `DynamicFunctionDefinition`, `TypeResolution`: functions with a metadata-only
  result type resolver, with optional typed/variadic signatures. `Call::argument_type`
  exposes the logical type for generic implementations, including typed NULLs.
- `Call`: callback-scoped borrowed values, nullable UTF-8 arguments, one int64
  result emission, and typed, parameterized host SQL with synchronous row
  consumers. `query_i64` remains a one-parameter scalar convenience wrapper.
  An emit failure cannot trigger a second emit from the same wrapper.
  `bytes(index, type_id)` and `emit_bytes(type_id, value)` additionally support
  plugin logical types and binary payloads without forcing UTF-8 or int64.
- `sql::{Value, Row, Outcome, Error}`: NULL, signed/unsigned int64, float64,
  UTF-8 text and binary parameters/results; affected/returned row counts;
  original database errors alongside host and consumer status codes.
- `table::{Definition, Column, Cursor, Service, Arguments, Rows, Cell}`: typed
  table registration and owned synchronous cursor execution. The generic service
  supplies open/next/rescan/close FFI boundaries and tracks EOF/poisoned cursors.
- `optimizer::{Definition, Hook, Context, Service}`: synchronous around-planning
  registration and callbacks, with explicit continuation and veto. This is not
  a custom-path or plan-replacement API.
- `table_planning::{Context, Estimate, Planner, Service}`: optional table-function
  cardinality, width and cost estimation, consumed by real planner candidates.
- `type_comparison::{Comparator, Service}`: optional codec suffix defining a
  deterministic total ordering over decoded, non-NULL values. The host loader
  validates/pins its invocation; scalar SQL comparisons and typed
  `BETWEEN`/`NOT BETWEEN`, scalar/row list `IN`/`NOT IN`, simple CASE matching
  and flat/nested row-constructor comparisons consume the callback. Nested rows
  require matching shapes and share converted selector leaves across IN candidates;
  this is not a persistent composite/record type ABI. Real SQL batch-frame tests
  cover skip masks, cached rows, changing batch sizes, lazy CASE callbacks and
  cancellation. Type comparisons still call the per-value ABI; ordinary functions
  can additionally use the indexed row-batch service described below.
  Plugin execution nodes do not inherit constant status merely from literal
  arguments; volatility-aware constant optimization remains separate work, as do
  sorting, hashing and indexes. See the [comparison contract](../../docs/developer-guide/zh/plugin-type-comparison.md).
- `batch::{Batch, Handler, Service}`: optional v3 function-service suffix with one
  handler invocation for a compact batch, indexed outputs, sticky host/cancellation
  errors and an adapter for existing scalar handlers. Old scalar services remain
  supported. This is typed row batching, not a columnar or asynchronous ABI. See
  [the scalar batch contract](../../docs/developer-guide/zh/plugin-scalar-batches.md)
  for limits, SQL integration and current verification scope.
- `catalog::{CatalogContext, Installer, Service}`: optional installation-time SQL
  declarations, copied by the host and admitted through the normal Extension
  resolver and Root transaction. This context does not execute SQL itself.
- `boundary`: `Result`/unwind panic to C status conversion, including panic
  payload destruction that itself panics. It does not recover abort, memory
  corruption or all allocation failures, and does not repair poisoned shared
  state. Native plugins are trusted code, not a sandbox.

`Registration::begin`, `Call::from_raw` and `TypeResolution::from_raw` are unsafe entry points because the
host supplies raw pointers. Their contracts require valid allocations, advertised
sizes, pointer lifetimes, correct callback thread and no foreign unwinding.
After construction, safe wrappers are neither `Send` nor `Sync`. Query data
must not escape to an asynchronous task. Results are synchronously copied by
the host; neither side frees memory owned by the other.

Calling `commit` stages contributions in the loader's activation candidate. It
does not expose provisional objects to queries or implement a database DDL
transaction. Registration is currently restricted to init/start. This SDK does
not yet provide runtime-session catalog builders, native-symbol/type DDL,
schema attribute macros, prepared SQL cursors, columnar batches or deep type/index/planner
adapters. Those remain part of the broader design. The independent
[`cargo-seekdb`](../cargo-seekdb/README.md) tool creates an independent Rust
plugin with `new`, generates routine SQL/control with `schema`, and packages it
through its audited CMake target with `package`. Live server test orchestration
is not yet implemented.

## Installation-time catalog declarations

Implement `catalog::Installer` and advertise `catalog::Service::<YourInstaller>::V1`
under `<native_module_id>.catalog.install`, business major 1. The borrowed context
exposes the installing package/version/database/owner and `declare_sql(&str)`.
Check the instance's lifecycle in your implementation. See the real
[`rust_text` installer](../../plugins/rust_text/src/catalog.rs) and the
[full host contract](../../docs/developer-guide/zh/plugin-catalog-install.md).

Declarations are synchronously copied, validated and parsed as separate inputs.
Errors remain sticky even if an installer ignores a failed emission; each FFI
entry catches unwind panics. The host keeps the module lease through subsequent
object admission, installation and publication. The context is not Send/Sync,
must not escape the callback and provides no SQL execution or transaction API.
Do not launch background work or perform external side effects during preparation.
Current installs require a control file. `install_source = 'native'` requires
this service to provide all initial SQL, without a base SQL file; the default
SQL-source mode retains static SQL plus optional declarations. Updates use explicit
migration scripts, and unsupported DDL is rejected by normal object admission.

## Around-planning hooks

Register an `optimizer::Definition` using `Registration::optimizer_hook` in the
same activation transaction as your other objects. The hook point is
`optimizer.plan.v1`; the definition supplies identity, priority, flags and an
explicit implementation service/version reference. Expose `Service::<YourHook>::ABI`
in the manifest and implement `Hook::validate_instance` and `Hook::invoke`.
See [`rust_text::planning`](../../plugins/rust_text/src/planning.rs).

The borrowed `Context` exposes statement kind, database/user IDs and
`call_next()`. Observe before/after that call, or return an error beforehand to
veto planning. Success requires exactly one continuation; a skipped or repeated
call fails. Downstream errors are sticky even if your handler ignores them;
`database_error()` retains the exact host error after the continuation. Contexts
and continuations cannot escape, cross threads, or enter asynchronous tasks.
Panics use the same unwind boundary as scalar callbacks. Shared plugin state
still needs its own synchronization and failure policy.

The host pins all hook objects and implementations before entering the ordered
chain (descending priority, then object ID), with at most 64 hooks and 16 nested
planning dispatches per thread. It does not hold loader/registry locks through
callbacks. The call point wraps `ObOptimizer::optimize`, not cached-plan
execution or every optimizer costing helper. Metadata excludes SQL text and
private C++ query/plan pointers. This v1 observation/around contract leaves
custom path submission and explicit replacement protocols as separate work;
it must not be used to report success without a core plan.

## Declaring scalar functions

Use `scalar_function!` at module scope for a fixed-result SQL function. Supply a
`FunctionDefinition`, `sql: false` (native context) or `sql: true` (SQL context),
an instance validator `fn(*mut sys::Handle) -> Result<()>`, and a handler
`fn(*mut sys::Handle, &mut Call<'_>) -> Result<()>`. The validator must check
your module's actual instance and lifecycle state; it runs even for NULL input.
Neither the macro nor a raw pointer comparison supplies synchronization or
permission checks for arbitrary module data.

See the compiled example in [`scalar.rs`](src/scalar.rs) and the real
[`rust_text` declarations](../../plugins/rust_text/src/lib.rs). The generated
module imports parent-scope names for its declaration/handler paths. Refer to
an existing parent constant as `super::DEFINITION` if it has the same name as
the generated member. This is a declarative macro, not a procedural attribute
that parses arbitrary Rust functions or infers SQL signatures.

For a declaration named `text_count`:

```rust,ignore
// During the existing init/start registration transaction:
registration.function(&text_count::DEFINITION)?;
// In the static native service-provides array:
text_count::provide(sys::Version { major: 1, minor: 0, patch: 0 }, sys::THREAD_SAFE)
```

The provided service ID comes from the declaration; its actual version and
capabilities remain caller choices and are checked against consumers by the host.
All ABI sizes and reserved fields in generated service tables are initialized.
The callback is not an additional public dynamic symbol; the normal manifest
entry and binary export audit remain unchanged. Service storage is static for
the module lifetime, while registration metadata still enters the normal host
registration transaction. The macro itself does not register or publish anything.

Handlers continue to validate their accepted arities and logical types and emit
the declared result. This permits a service to be reused by compatible overloads,
as the byte and `rust_utf8` count signatures do. No automatic NULL strictness or
cast is imposed. `sql: true` advertises the existing minor-1 scalar service and
requires an extended context; actual SQL operations still validate the host API
and session through `Call`. The loader limits native-only services to their
v1 context. This flag is not a database permission grant.

Admission and handler panics are contained by `boundary` only in unwind builds.
The plugin must still poison/repair affected shared state when necessary. The
macro does not change allocator ownership, context lifetimes, Send/Sync, host
panic policy, or the public C ABI. Dynamic results, types/codecs and casts may
continue using the lower-level SDK. Build-time schema generation is described below.

## Build-time SQL/schema generation

`schema::scalar_wrapper` consumes a `FunctionDefinition` (including a macro's
`DEFINITION`) and generates ordinary INVOKER SQL routine DDL. It validates arity,
duplicate parameter names, native type identities and distinct wrapper/native
names. Determinism comes from the native declaration; SQL data access is an
explicit author assertion. BIGINT maps to core int64, TEXT and LONGBLOB to core
bytes; selecting text semantics is explicit, not inferred from an opaque buffer.
Unsupported custom types fail generation rather than becoming plain bytes.

`schema::Package` combines these strings with handwritten base/update SQL and
emits default-version/native-module control metadata. SQL is preserved verbatim,
not split on semicolons, dependency-sorted or migrated automatically. Package
names, versions, duplicate files, default-version reachability, UTF-8/NUL and
host size limits are checked before writes; filesystem output is create-new.
`write_to` expects an existing staging directory and can leave partial files on
I/O failure. `cargo seekdb schema` owns that directory and its incomplete marker.

`render_with_source(InstallSource::Native)` and
`write_to_with_source(directory, InstallSource::Native)` generate an explicit
native-source package. They require `native_module`, reject base scripts, and
allow zero scripts or explicit updates only. Zero scripts emits control alone;
the module's installation callback supplies SQL later. The default version is
the advertised direct installation version, not inferred from update filenames.
Existing `render`/`write_to` retain SQL mode. The CLI's optional `--example NAME`
selects a generator; it validates control/default-version selection through the
actual Rust host source reader, without loading a plugin or authorizing SQL.

`render_with_options(PackageOptions { requires: &["text_ops"], ..Default::default() })`
and `write_to_with_options(directory, options)` add explicit Extension dependencies.
Options combine SQL/native source selection with at most 64 names, rejecting
invalid names, duplicates and self-dependency before writing. Declaration order
is preserved. This does not query a database or auto-install dependencies; the
host resolves same-database provider IDs under the installation transaction.
Existing render/write/source methods retain their previous empty-dependency output.
The Rust text project's `seekdb_composed_schema` example generates the pure-SQL
`text_composed` package (no native module association), sharing handwritten SQL
with the CMake-delivered files. The kernel runner compares all generated bytes
with the installed package before resolving its real provider/consumer routines.

`PackageOptions::version_controls` adds independently versioned metadata:

```rust
use seekdb_extension::schema::{PackageOptions, VersionControl};
let versions = [
    VersionControl { requires: Some(&["migration"]), ..VersionControl::new("middle") },
    VersionControl { requires: Some(&[]), ..VersionControl::new("2.0") },
];
let options = PackageOptions {
    requires: &["base"],
    version_controls: &versions,
    ..PackageOptions::default()
};
```

Each entry emits `name--version.control`. `requires: None` inherits primary
requirements, while `Some(&[])` emits an explicit empty override. Optional
`native_module`, `schema` and `relocatable` fields use the same host value rules;
schema quotes are doubled and control characters rejected. Omitted fields are
not copied into secondary files. Native/SQL source selection remains a primary
package option; secondary default-version/directory configuration is not exposed.
Files are sorted deterministically, declaration order is preserved, and duplicate
versions, invalid names, oversized filenames, schema conflicts and excessive
file counts fail before writes. Existing secondary files are never overwritten.

Metadata generation is not migration admission: the host still rejects selected
paths that change module/namespace context, checks the combined final/intermediate
64-provider limit, and resolves providers in the target database. The generator
does not reject a package just because unrelated versions use different providers.
`seekdb_versioned_schema` is an executable reference: version 1.0 inherits
`alpha,zulu`, the intermediate step needs `migration`, and 1.1 retains `gamma,zulu`.
The kernel runner generates this package through the CLI, then uses its actual
control/SQL files in the catalog/Rust update-driver tests. No provider is installed
by generation, and these controlled tests do not prove database isolation.

The Cargo example shares metadata through a plugin `rlib` while production still
loads only the audited `cdylib`; no registration or lifecycle callback runs during
generation. Generator/build scripts are trusted developer code, not sandboxed.
This tool does not execute or authorize SQL. Native-backed routine installation
requires the associated module already active; arbitrary SQL may still fail the
host installer's supported-DDL checks. See [CLI protocol and example](../cargo-seekdb/README.md).

## Cooperative query control

`Call::supports_query_control()` detects SQL API minor 1's optional poll suffix;
`poll_query()` returns a remaining `Option<Duration>` or a status with the original
database error. None means no configured query deadline. Poll between bounded
compute/I/O chunks; it does not interrupt a blocked thread or revoke external
effects. The callback context remains borrowed and same-thread.

`scalar_function!` accepts optional `query_control: true` alongside `sql: false`:
it requests the extended execution context while retaining legacy-v1 fallback.
`sql: true` still requires v2. Host-observed poll and SQL errors share sticky
invocation state. See [host contract](../../docs/developer-guide/zh/plugin-query-control.md).

Table cursors can opt in with `table::Service::<C>::WITH_QUERY_CONTROL`, or
`table_planning::Service::<C>::WITH_QUERY_CONTROL` when providing estimates.
Both use table SPI minor 2; the former needs no `Planner` implementation and
requests the host's default estimates. `Rows::supports_query_control()` and
`Rows::poll_query()` borrow the current next callback only, with the same result
type as `Call::poll_query()`. Poll errors block subsequent row emission even if
the cursor returns success. The SQL host closes a failed cursor and preserves its
database error until explicit rescan/close; repeat fetch cannot silently reopen it.
Old contexts remain usable without polling. This suffix has no SQL execution API;
`Cursor::open_with_context` also permits polling during open; its default calls
the original `Cursor::open`. Raw rescan has no query context.

## SQL inside table callbacks

Opt into `table::Service::<C>::WITH_SQL` (or the planning service's `WITH_SQL`)
for table SPI minor 3. Override `Cursor::open_with_context` to use its borrowed
`QueryContext::execute_sql`; next uses `Rows::execute_sql`. Both accept the same
typed parameters and synchronous row consumer as scalar `Call::execute_sql`.
SQL and poll share sticky errors; ignored open failures destroy the returned
cursor instead of publishing it. Missing SQL in open or any next is rejected.
Keep only owned data in cursor state, not the query context or borrowed SQL rows.
SQL-dependent raw rescan can reject `Cursor::open`; the SQL executor rescans by
closing/reopening with a fresh context. Close/Drop cannot execute SQL.
See [the table SQL contract](../../docs/developer-guide/zh/plugin-table-sql.md)
and the real [SQL series example](../../plugins/rust_text/src/sql_series.rs).

## Table projection metadata

Use `table::Service::<C>::WITH_PROJECTION` (or its planning equivalent) for
table SPI minor 4. `QueryContext` during open and `Rows` during next expose
`projection_column_count() -> Option<usize>` and `column_requested(index)`.
Choose `WITH_SQL_AND_PROJECTION` if SQL must also be available; projection alone
does not require SQL or polling. Older contexts conservatively request all columns.

The borrowed mask uses full declared ordinals, including columns needed only by
filters. Re-read it per callback; do not retain it across calls or threads. A
present all-zero mask requests cardinality only, unlike an absent mask. Emit the
full declared row shape even for unrequested columns: cheap placeholders must
still obey their type and nullability. Never change row count, cursor progression,
requested values or side effects based on projection. Known arity is checked before
host emission. Raw rescan has no projection context.

The host narrows contexts to exact v1/v2/v3 for older service minors. The
[Rust words example](../../plugins/rust_text/src/words.rs) uses valid empty/zero
placeholders while preserving scanning and ordinal state. This is advisory
computation pushdown, not columnar output or measured performance improvement.
See [the batch contract](../../docs/developer-guide/zh/plugin-table-batches.md).

## Query-time catalog lookup

Query callbacks also expose `supports_catalog_lookup()` and
`lookup_routine(sql::RoutineKind, name) -> Result<Option<u64>, sql::Error>` on
`Call`, table `QueryContext` and `Rows`, through the optional SQL API minor-2
suffix. It uses the current database and caller's schema/SHOW visibility; absence
is not permission denial. The ID is a snapshot only, not a lease or execute grant.
Lookup does not create objects, start transactions, install members or obtain a
new schema snapshot. See [query catalog](../../docs/developer-guide/zh/plugin-query-catalog.md).

## Query-time routine mutation (experimental)

`Call`, `QueryContext` and `Rows` also expose `supports_catalog_mutation()` and
`mutate_routine(statement) -> Result<Option<u64>, sql::Error>`. This negotiates
SQL API minor 3 independently of lookup, preserving older host API prefixes.
One standalone routine CREATE, MySQL attribute ALTER or DROP runs through normal
host parsing/authorization in the caller's transaction, without implicit commit
or automatic extension membership. A returned ID is provisional; `None` denotes
an absent DROP IF EXISTS. Propagate errors even when no ID is needed.

SQL must be nonempty UTF-8, at most 4 MiB, without NUL. Multiple statements,
other DDL, CREATE OR REPLACE and CREATE IF NOT EXISTS are unsupported. Input
validation is delegated to the host so ignored invalid SQL still fails the
invocation. Table wrappers additionally prevent emitting rows after local errors.
Contexts cannot cross threads or asynchronous boundaries. Detailed operation and
cleanup outcomes are available in the raw C/sys result, not a commit-capable token.
The public path is connected. A controlled kernel fixture now combines real PL
resolution, CREATE writer effects and Rust view-journal rollback, including a
second routine referencing the first. Public SPI success with full server/data
services and live transaction/savepoint/concurrency behavior remain unverified.
See the [contract and evidence limits](../../docs/developer-guide/zh/plugin-query-catalog.md).

## Transaction-bound catalog construction

`catalog::TransactionalInstaller: Installer` adds a `build` callback through
`Service::<T>::V2` (catalog SPI 1.1). `Service::<T>::V1` retains the unchanged
preparation-only ABI. New hosts accept both; old hosts reject the new SPI minor.
Preparation may emit static declarations first, or emit nothing for a build-only
native package. The host must see at least one object in the complete install.

`build` receives `TransactionContext<'txn>` with installing database/owner/package
metadata and `create_routine(sql)`. One new function or procedure is parsed,
authorized and staged per call; success returns a `RoutineId<'txn>` and makes the
object visible to subsequent construction in this transaction's schema view.
The ID is reserved, not committed. The context is not Send/Sync, IDs cannot safely
escape their lifetime, and a failed create remains sticky even if ignored.
Panics are contained in unwind builds, as with other SDK callbacks.

The size-negotiated `CatalogBuildContextV2` suffix adds optional lookup without
changing the SPI 1.1 callback. Check `supports_lookup()` for old hosts, then use
`lookup_routine(RoutineKind::Function, "name")` (or `Procedure`). It returns
`Option<RoutineId<'txn>>`: absent is not an error. Names are unquoted UTF-8
identifier contents, at most 2048 bytes; host routine visibility and collation
apply in the installing database. Existing and newly staged objects share this
view. Lookup does not adopt objects, grant EXECUTE or create dependencies.
Lookup and create share sticky errors; unsupported lookup is an error, not None.

This is installation-time construction, not query SQL, transaction ownership,
arbitrary system-table writes, or automatic support for new object classes.
The loader retains the module lease through build/installation/publication;
the original host transaction retains commit/rollback authority. No external
side effects or background work may start from build. Updates still use explicit
SQL scripts and do not run this callback.

The Rust text module's `rust_text_built` control-only package demonstrates two
dependent routines created by the actual Rust callback. See
[the builder design](../../docs/developer-guide/zh/plugin-catalog-builder.md).

## Table-function planning

Implement `table_planning::Planner` on your existing `table::Cursor` and expose
`table_planning::Service::<YourCursor>::ABI` instead of the minor-0 table service.
Registration and cursor execution are unchanged. The extended service embeds
the exact old v1 layout, advertises table SPI minor 1 and appends the estimate
callback. Scalar SPI minor 1's SQL context is unrelated: this interface grants
no SQL context or execution capabilities.

`Planner::estimate(instance, &Context)` returns `Estimate { rows, row_width,
total_cost }`. The host consumes it before candidate selection, and copies it
into ordinary logical-plan properties. Width is average bytes per output row;
cost uses the host's relative optimizer units, not milliseconds. All three
numbers must be finite and nonnegative (zero is permitted); invalid or omitted
outputs and callback errors fail planning, rather than silently reverting to
defaults. ABI validation and panic-to-status conversion precede publication.
The host also validates raw C callbacks independently of this SDK.

The context lends object identity, declared signature type IDs and output-column
count, allowing a shared implementation to distinguish registrations. It does
not expose input values, evaluate constants, execute casts, open a cursor or
lend a SQL session. Thus a parameter/expression with side effects is never
executed just to estimate cardinality. Keep estimation bounded and side-effect
free; neither the borrowed context nor its data may escape or cross threads.
The host pins the object and implementation under the binding epoch through
the call. This is planning metadata, not a row cap or an exact-cardinality claim.

Legacy minor-0 services remain valid and use the host's existing 199-row,
199-byte, cost-1 defaults. The Rust words example opts in with a fixed prior;
use a justified model for your own plugin rather than copying that sample prior
as a performance recommendation. Statistics/constant-value support, predicate
selectivity, custom paths and plan replacement remain separate future APIs.

## Declaring table functions

Use `Registration::table_function(&table::Definition)` in the same init/start
transaction as types and functions. The definition has a fixed typed argument
signature, explicit column names/types/nullability, flags and implementation
service reference. The host copies computed metadata before returning. Empty
argument lists are supported; at least one column is required. This wrapper
currently supports up to 1024 arguments and 4096 columns, subject to host limits.

Implement `table::Cursor: Send + 'static` with instance admission, `open` and
`next`. `open` receives borrowed `Arguments`; copy any data that must survive the
call into your cursor. `next` receives `Rows`, may emit up to `remaining()` rows,
and signals EOF by returning success without emitting rows. `Cell` carries a
logical type ID and optional byte slice (None is SQL NULL). Numeric bytes use
the public SPI representation, not Rust enum/String/Vec layouts. Row bytes are
copied synchronously by the host; the SDK bounds a row to 16 MiB aggregate data.
No query context or borrowed argument may escape to a background task.

The SQL host's vectorized table operator now forwards its requested batch limit
to this same interface. Results occupy separate batch datum slots; unreferenced
SQL columns retain their declared ordinal but need not be materialized. A failed
native next publishes no consumable partial batch and closes the cursor. This
is row-emission batching, not columnar exchange. See the
[batch execution contract](../../docs/developer-guide/zh/plugin-table-batches.md).

Expose a module-lifetime `table::Service::<YourCursor>::ABI` under the named
implementation service in the manifest. It supplies panic/status boundaries,
row counts, sticky emission errors, EOF tracking and close. A next/rescan error
or caught panic poisons the cursor; a successful rescan constructs replacement
owned state. Close drops the cursor even after the instance has stopped. Host
calls on a cursor must be exclusive, may move across threads, and must close
exactly once; the host's handle wrapper provides idempotent public close. A
closed raw pointer cannot be reused. Panicking shared state and abort/OOM still
require plugin policy; this is not isolation from native faults.

See [`rust_text/src/words.rs`](../../plugins/rust_text/src/words.rs) for an actual
plugin using these interfaces. It stores one owned UTF-8 input and streams token
slices with ordinals; it does not materialize all tokens or spawn a runtime.
The loader applies registered direct implicit casts to the declared signature
before calling open. Its cursor pins those conversions for direct rescan; changing
the original logical input types requires a new binding/open. Cast failures
disable cursor next until successful rescan, including errors before Rust entry.
The bytes-only words example exercises this with a custom text argument.
Without `sys::NULL_PROPAGATING`, the implementation receives NULL and defines
its result (including returning rows). With that flag, the host checks for NULL
after implicit casts and returns zero rows without entering the table callback.
Unknown NULL is assigned the signature's type; typed NULL still runs required
casts. The flag is a catalog/host contract, not an extra check in the raw generic
SDK service, which can be shared by strict and non-strict SQL objects. See the
words-or-null and words-strict examples sharing one Rust implementation.
Multi-hop cast search, asynchronous query context and columnar batch exchange remain
separate work, not implied by this wrapper.

## Registering custom types and conversions

`Registration::data_type` and `Registration::cast` use the same transaction as
`function`; they do not create a second catalog path. `TypeDefinition` supplies
the logical object ID, SQL name, physical format ID/version, flags, and a named
codec service reference. `CastDefinition` supplies source/target logical type IDs,
an explicit `CastContext`, cost, flags and a named implementation reference.
No implicit/assignment conversion policy or service version is chosen for you.

The codec service must be provided by the plugin's manifest and use
`sys::TypeCodecService` with valid `decode`/`encode` callbacks. Cast implementations
use the host's existing scalar execution service protocol. Service table pointers
remain module-lifetime data; copying registration descriptors does not copy code
or permit freeing the tables. Every callback must obey its raw-pointer contract
and contain errors/panics at its FFI boundary, just like scalar functions.

Metadata strings may be computed during init/start and discarded after staging:
the host deep-copies them synchronously. Types, casts and functions can be staged
in one transaction; failed commit is aborted exactly once by Drop. Commit only
stages a loader candidate. Actual names, capabilities, format declarations, type
dependencies and publication are still validated by the host catalog/registry.

For a scalar function operating on a registered custom type, the byte-oriented
callback body can use:

```rust
use seekdb_extension::{Call, Result};
fn identity(call: &mut Call<'_>) -> Result<()> {
    let payload = call.bytes(0, c"example.custom-type")?;
    call.emit_bytes(c"example.custom-type", payload)?;
    Ok(())
}
```

Non-NULL input must have the requested logical type ID. Output is copied by the
host before emission returns, and only one emission attempt is allowed across
both byte and int64 helpers, including after a host error. NULL and empty bytes
are distinct; byte access does not decode UTF-8. Input/output byte helpers reject
payloads larger than 16 MiB. Host result binding still decides whether the emitted
type is valid for that call; merely labelling bytes does not change a SQL type.

This exposes the current public type/codec and cast contracts to Rust, not the
full PG type system: richer comparison/hash, typmod, operator classes, statistics,
index integration and transaction-scoped runtime type creation remain separate
host work. Registration tests use a copying host fixture and layout tests compare
all added fields/enums with the C headers; this alone is not an end-to-end custom
type persistence, coercion, or index execution test.

The [Rust text reference](../../plugins/rust_text/README.md#type-codec-and-conversion)
now uses these APIs in a real loadable library: it contributes a UTF-8 type,
explicit conversion, constructor and typed function overload. Its loader test
executes codec and cast callbacks under joint object/service leases and checks
generation/format fences and terminal shutdown. This adds native execution
evidence, but the catalog remains a fixture and SQL column storage/coercion is
not established by that test.

## Metadata-dependent result types

Use `Registration::dynamic_function` with a `DynamicFunctionDefinition` and a
manifest-provided `sys::FunctionServiceV2`. Its complete v1 prefix uses the size
of the extended table, SPI major 1 and `sys::RESULT_TYPE_MINOR` (2); set both
execute and resolve_result callbacks and zero all reserved fields. Existing
`FunctionDefinition` and v1 services do not change. The new registration helper
sets static_result_type_id to NULL, using the existing descriptor contract.

`argument_types=None` declares an untyped arity envelope, not a new SQL type.
`Some(types)` declares target types; min/max and variadic follow the existing
host overload rules. After selection, the resolver receives effective argument
types following those same implicit conversions. No values, casts, SQL or other
query side effects are executed to infer the result. For an untyped function,
an unknown NULL is represented by None; the plugin must choose a concrete type
or return an error. Returning an empty ID is not a bytes fallback.

Inside the resolver's `boundary` closure:

```rust
use seekdb_extension::{Result, TypeResolution};
fn identity_type(call: TypeResolution<'_>) -> Result<()> {
    if call.argument_count() != 1 { return Err(seekdb_extension::sys::INVALID); }
    let id = call.argument(0)?.unwrap_or(c"core.type.bytes");
    call.finish(id)
}
```

Type IDs are borrowed from the call, and `finish` consumes the wrapper and copies
the result into the host's bounded output buffer. It accepts dynamically built
strings. The host rejects invalid IDs/size/reserved fields and callback failures;
Rust validates IDs and contains unwind panics at the same boundary as execution.
The wrapper is neither Send nor Sync. The callback may be invoked concurrently
and repeatedly; its result must depend only on argument metadata and immutable
generation-local definitions. Do not retain metadata, mutate catalog state,
execute SQL, initialize a model or access query-local state during resolution.

The host obtains joint object/code leases, calls without loader/registry locks,
and rejects an epoch change across resolution. A resolved ID is stored in the
normal SQL binding and plugin expression plan data; it does not require runtime
re-inference on each row. This implements logical type selection, not typmod,
collation/shape inference, or PG's full polymorphic signature/constraint system.

## Composing SQL from a Rust plugin

`Call::execute_sql(sql, parameters, max_rows, consumer)` exposes the existing
host SQL SPI without restricting plugins to one text parameter or one integer
cell. SELECT results are delivered row by row; DML returns affected rows without
requiring a result row. `max_rows=0` is useful for DML; for SELECT it rejects any
returned row. Exceeding the limit is an error, not a successful truncated result.

For example, inside an active SQL-enabled callback:

```rust
use seekdb_extension::{Call, sql::{self, Value}, sys};

fn combined_length(call: &mut Call<'_>, first: &str, second: &str)
    -> Result<i64, sql::Error>
{
    let mut total = 0i64;
    call.execute_sql(
        "SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4)) \
         UNION ALL SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))",
        &[Value::Text(first), Value::Text(second)],
        2,
        |row| {
            let length = match row.get(0)? {
                Value::I64(v) => v,
                Value::U64(v) => i64::try_from(v).map_err(|_| sys::INVALID)?,
                _ => return Err(sys::INVALID),
            };
            total = total.checked_add(length).ok_or(sys::INVALID)?;
            Ok(())
        },
    )?;
    Ok(total)
}
```

Parameters use positional `?`, never string interpolation. SQL cannot contain
NUL; text/binary parameter payloads can. SQL bytes, aggregate parameter bytes,
and aggregate delivered cell bytes each have a 16 MiB limit; parameters and
columns have a 1024-item limit. Unknown/malformed result kinds and invalid UTF-8
are rejected even if the consumer would ignore the corresponding column.

`Row::get` copies numbers and borrows text/binary cells only for that callback.
To retain data, copy it into plugin-owned storage; it must not reference a host
buffer reused for the next row. A higher-ranked callback prevents borrowed rows
or cells escaping; rows are neither Send nor Sync, and the mutable Call borrow
prevents recursive use of that same wrapper during consumption. Consumer errors
and unwind-mode panics are caught inside the C callback and retained even if a
faulty host reports success or attempts to deliver more rows. If the host maps
the consumer error to cancellation, `Error::consumer_status` preserves its cause.

Execution uses the caller's host session, permissions, and transaction context.
The host, not the SDK, rejects DDL/transaction control and manages statement
failure/rollback. A callback may already have copied rows or changed local plugin
state before a later SQL error; those plugin effects are not undone by Rust.
Propagate failures rather than treating partial results as success. This API
does not enable init/start SQL, background sessions, asynchronous I/O, standalone
prepared statements, or cursors that survive the call. These still need broader
host interfaces; no new C ABI or transaction coordinator was introduced here.

## Verification and build boundary

`memory::HostAllocator` explicitly borrows a module's public host alloc/free
pair. `zeroed`, `copy_from_slice` and `copy_from_slices` return initialized,
fixed-length `HostBuffer` bytes; Drop uses the original host, size and alignment.
Empty buffers make no allocation. Invalid layouts and missing ABI callbacks are
rejected, and host quota denial returns `NO_MEMORY`. This is not a global Rust
allocator: other Vec/String, allocator metadata and GPU memory remain separate.

The unsafe constructor requires a valid, live immutable host table/account and
execution authority. Safe buffers cannot outlive the borrowed allocator, and
neither wrapper is Send/Sync. They acquire no module lease and are not background
resource tokens. Rust text's concat3 scalar and batch callbacks use these bytes
under their existing host execution lease; synchronous emit copies the result
before Drop. See `tests/memory.rs` and the lifetime compile-fail examples.

The optional Host API v3 memory suffix additionally supports `OwnedHostBuffer`
via `owned_zeroed` and `owned_copy_from_slice`. These initialized bytes own a host
account token independently of the original allocator/HostContext. They support
Send/Sync and use a thread-safe host release callback, not a borrowed host.free.
They do not pin plugin code, carry query contexts, or replace task lifetimes.
Old hosts are explicitly rejected for owned requests, with no borrowed-pointer
fallback. Rust text v15 stores words cursor input in this form; its cursor still
relies on the loader's separate execution lease. See `tests/owned_memory.rs`.

Run commands from `rust/` to select the repository's pinned toolchain:

```bash
cargo test --manifest-path extension-sdk/Cargo.toml --offline
cargo clippy --manifest-path extension-sdk/Cargo.toml --offline --all-targets -- -D warnings
```

The ABI test compiles a C probe against the actual public headers and compares
size, alignment and every listed field offset with Rust. It currently requires
a native GNU-compatible C compiler (`CC`, or `cc`) and is not a cross-compilation
test. Real loader tests additionally exercise callback calling conventions.
Passing on Linux x86-64 is not evidence for Windows or other architectures.

The SQL SDK tests use a controlled host to check all six value kinds, multiple
rows/parameters, DML metadata, row/byte limits, malformed inputs, callback failure
and panic, and scalar compatibility. Compile-fail doctests enforce row lifetime,
thread and same-Call reentry constraints. The Rust text dynamic-library regression
also exercises the shared execution path through the production loader, but its
SQL host is a fixture. These tests do not establish real database SQL behavior,
permissions, transaction visibility, rollback or cancellation correctness.

Use `seekdb_add_rust_plugin` for the audited CMake build. Reachable local Cargo
dependencies must stay inside the plugin tree or this SDK tree; symlink-resolved
escapes and transitive dependencies on the host runtime are rejected. Registry
and git dependencies still require developer trust. The check does not sandbox
build scripts or prevent arbitrary source-level file inclusion. A separate
binary audit checks exported/imported symbols; Linux builds also require closed
linking with `-z defs`. No core host archive is linked into the plugin.

## Server-dev 规划图

`candidate::Hook::INSPECT = true` 请求 v3 规划上下文，并包含已有的
候选构造接口。`root`/`plan`/`child` 读取计划拓扑；`expression`/
`describe_expression`/`argument` 读取已有谓词、排序、join 表达式与
参数。`PlanId` 和 `ExpressionId` 是同一次 hook 链内稳定的借用身份，
不是 candidate index、C++ 指针或可持久化/PX 传递的计划绑定。

读取不会调用可能分配辅助表达式的 `get_op_exprs`。这些字段不等于
最终输入/输出 schema，也未提供常量值、任意改写或多输入物理算子。
See [planning graph](../../docs/developer-guide/zh/plugin-candidate-graph.md).

## 数值行读取

自定义执行 Context 另提供 `has_schema`、`input_schema(index)` 和
`output_schema()`：在取行前即可获得借用的 Schema/Column，包括空输入。
列描述有逻辑 ID、Encoding、nullable/stored 以及版本绑定的 SQL 类型、
collation、precision/scale。零列与不支持 schema 不同；旧 v1 上下文
返回 UNSUPPORTED_ABI。描述不可逃逸 next 回调；要持久保存需复制。
这是执行 schema，不等于已经具备规划期输出映射或多输入 SQL 算子。

`table::Cell::number()` 可用于自定义执行器输入等借用行，返回
`Result<Option<table::Number>>`。仅识别完整的 `core.type.bool`、
`int32`、`uint32`、`int64`、`uint64`、`float64` ID（后五项同样带
`core.type.` 前缀），以及对应的六个 `org.seekdb.gis.scalar.*` 兼容别名。
这些是明确的完整 ID，不是任意后缀匹配。数值类型的 NULL 返回 None，未知类型或非法表示
返回 INVALID；使用 `Number::recognizes(cell.type_id)` 判断是否采用
builtin 解码，自定义类型继续读取 bytes 或使用自己的 codec。

数值使用本机字节序，不要求缓冲区对齐。bool 仅接受一个字节的 0/1，
32 位整数为四字节，64 位整数和 double 为八字节；这不是跨平台持久
编码。解码保留整数有/无符号及宽度，不经 f64 中转而丢失大整数精度。
