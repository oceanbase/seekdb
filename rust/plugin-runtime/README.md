# seekdb Rust plugin runtime

This crate implements **host-side** plugin management; it is not a Rust example
plugin and is not the public plugin SDK. The existing C/C++ GIS, SQL extension
and reference DSOs keep using their C ABI.

## Current implementation

`ObPluginGeneration` delegates all lifecycle state, activation reservation,
execution reference counting and drain waits to this crate. Its C++ wrapper
owns one opaque handle. Existing registry and execution leases keep that wrapper
alive with `shared_ptr`; destroying it requires no live borrowers or waiters.
The registry lock precedes the Rust generation lock. Rust never calls back into
the registry or a plugin while holding its lock.

There is one authoritative generation state, not synchronized C++/Rust copies.
Publication reserves INITIALIZING before the durable catalog commit, then
promotes the prepared candidate. QUIESCING refuses new execution references;
existing references remain valid. STOPPED requires zero references. BLOCKED
cannot be stopped through ordinary state transitions; the C++ loader retains
the terminal-process-exit authority for the special bridge operation.

The bridge in `include/plugin_runtime.h` is internal to the host. It exposes
only opaque pointers, fixed-width integers and status values; do not install it
as part of the plugin SDK. C++ maps bridge errors to OceanBase error codes.
Host builds retain `panic=abort`. Poisoned runtime state terminates the host
rather than risking premature unload; plugin panic recovery is a separate SDK
contract, not a guarantee made by this crate.

## In-memory Extension declarations

`seekdb_runtime_package_from_source` accepts a host-owned, already selected
install/update source and returns the same deeply owned Package used by file
discovery. It validates identities, dependencies, namespaces, contiguous version
edges and bounded UTF-8 SQL without consulting a directory or concatenating SQL.
Zero scripts are valid for an explicit same-version update or native-source
fresh installation (`install_source = 'native'`). These are
source records, not authorization tokens or proof of durable catalog state.

The C++ `ExtensionScript::load_source` validates through this Rust entrypoint,
copies caller data before resetting old views, and uses the same real parser as
file-backed installation. The existing routine resolver/Root transaction path
retains authority over object creation, permissions, membership and publication.
The native module can now contribute additional installation declarations through
the optional public `<module_id>.catalog.install` service. The C++ loader pins
its service lease through installation/publication and validates copied fragments
through the Rust source constructor; `ExtensionScript` parses them separately
after the static SQL. The SDK supplies `catalog::Installer` and a borrowed,
same-thread `CatalogContext`. Emission only stages text and does not create an
object, run a query or grant transaction authority. The control remains required;
native-source packages require this service and its complete SQL object set, not
a placeholder base script. Ordinary SQL-source packages retain static files and
optional declarations. Updates use explicit migration scripts. This is not yet a
runtime-session catalog service; query callback DDL still needs transaction
integration. See the [full contract](../../docs/developer-guide/zh/plugin-catalog-install.md).

## Cooperative query status

The host SQL API's optional minor-1 suffix polls the actual execution context's
cancellation, timeout and additional status checks without issuing SQL. Its
remaining-deadline snapshot is not a reservation or asynchronous cancellation
token. Poll and SQL errors share the callback's sticky host state. Rust exposes
capability detection and a borrowed `Call::poll_query`; the scalar macro's optional
`query_control: true` requests the context while preserving legacy-v1 fallback.
Table SPI minor 2 adds a borrowed, poll-only context for streaming callbacks.
Rust `Rows` exposes it without retaining a query pointer in cursor state; planning
estimates are optional for control-only services. Older table services retain the
v1 context. On callback failure, the SQL host closes the cursor, releases its
leases, and keeps the original error until rescan/close. The real Rust text scalar
and word scanner poll within long computations. See
[the contract](../../docs/developer-guide/zh/plugin-query-control.md).

## Caller-session SQL in table callbacks

Table SPI minor 3 additionally exposes the existing caller-session SQL API in
open/next. The Rust SDK offers borrowed `QueryContext`/`Rows::execute_sql` and
explicit `WITH_SQL` services. SQL-dependent calls reject old contexts; minor 2
services keep their poll-only context. No query pointer is stored in a cursor,
and SQL rescan closes/reopens. The SQL series plugin exercises both phases.
See [the contract and test scope](../../docs/developer-guide/zh/plugin-table-sql.md).

## Transaction-built routine catalog

Catalog SPI 1.1 adds a build callback to the unchanged preparation prefix. The
loader binds installation identity and retains its module lease; the C++ Root
adapter supplies the existing transaction-local routine builder and the Rust
installation coordinator retains commit/rollback orchestration. The SDK provides
`TransactionalInstaller`, `TransactionContext` and lifetime-bound reserved IDs.
The control-only `rust_text_built` package exercises the real Rust DSO creating
two dependent routines. Public query SQL still cannot execute DDL; this is not
arbitrary catalog access or a second transaction manager. See
[the builder contract](../../docs/developer-guide/zh/plugin-catalog-builder.md).

The optional build-context v2 suffix adds routine lookup with size negotiation.
The Rust SDK exposes capability detection and `Option<RoutineId>`; the C++ host
uses its normal routine visibility check and the same database-scoped schema
guard for base and newly staged objects. Lookup/create share sticky errors;
absence is not failure, and lookup never adopts objects or grants EXECUTE.

## Version-specific package control

The Rust source reader overlays optional `name--version.control` on the primary
control independently for each selected script destination. Explicit empty
`requires` removes inherited requirements; secondary `default_version` and
`directory` are rejected. Final dependencies and intermediate prerequisites
have separate owned arrays and internal C ABI fields, with a combined 64-name
limit. The C++ catalog transaction locks both sets before schema execution but
persists only final Extension edges. No-op/native zero-script sources cannot
carry intermediate prerequisites. Ordinary SQL object dependencies still apply.

Selected steps must share native module and namespace context; this is not
module hot replacement or relocation. CLI schema inspection validates all
secondary controls, including unselected versions. See
[package semantics](../../docs/developer-guide/zh/plugin-extension-package.md).

## Plugin-controlled table-function planning estimates

Table execution SPI minor 1 optionally appends an estimator to the unchanged
minor-0 service. The Rust SDK's `table_planning` module generates this boundary
and checks metadata/results; the C++ loader independently validates the service
shape and all numeric outputs. An omitted output starts as NaN and cannot pass
as a zero-cost plan. Callback failures, stale bindings and malformed results
fail planning without publishing partial output. Legacy minor-0 C table services
retain the previous 199-row/199-byte/cost-1 defaults.

The loader pins the table object and implementation at the bound registry epoch
and lends only object identity, declared post-coercion signature and column
count. It does not execute argument expressions/casts or open a cursor. Normal
`FunctionTablePath::estimate_cost` applies the returned cardinality/width to its
relation and total cost to its candidate. Normal path comparison and logical
property copying consume these values; there is no parallel plugin optimizer.

Rust words opts in with a sample prior of 8 rows/40 bytes/cost 4; these numbers
are deliberately illustrative, not measured accuracy or execution limits. This
metadata-only estimator does not yet consume constants, statistics or predicate
selectivity, nor provide custom paths or plan replacement. It is an additional
real planner control point on the way to the broader extension design.

## Executable around-planning hooks

The `build_contract` module additionally implements opt-in linked-host identity
admission for Server-dev manifests. It reads bounded ELF64 little-endian notes
from the Linux running executable, caches only successful reads, and rejects
mismatched contracts before lifecycle init. Public-only activation does not read
the host artifact. The `server_dev_contract` Cargo example generates a new C
header from the chosen final host executable without overwriting existing files.
This is linked identity, not a signature/checksum or complete Server-dev profile:
private headers/exports, deep planner objects and the Rust adapter are still to
be connected. See the [contract and limits](../../docs/developer-guide/zh/plugin-server-dev-contract.md).

The host now dispatches through `hook_v2.rs`, which distinguishes automatic
before/after observation, explicit around continuations, and replacement that
may skip the remaining chain. A mandatory host result validator runs only after
the entire successful chain, including wrapper post-processing. Downstream
errors remain sticky even for replacements that choose to call next. The old
host ABI converts entries on a bounded stack and uses the same engine.

This is an internal host bridge, not a new public plan API. The existing
`optimizer.plan.v1` adapter admits AROUND entries only and verifies the core
continuation ran. Actual replacement needs a separate permitted hook point,
version-bound plan access and a real plan result/ownership validator. Observer
AFTER reports the nested result, not final validation or database commit; trusted
native observers must not mutate operation state. See the
[mode contract](../../docs/developer-guide/zh/plugin-hook-modes.md).

The mode-aware engine composes synchronous `optimizer.plan.v1` callbacks around
`ObOptimizer::optimize`. Rust owns the per-invocation continuation protocol:
success calls the continuation once, pre-continuation failure vetoes planning,
and downstream database errors cannot be swallowed or replaced by a generic
plugin status. Missing/repeated continuations fail instead of manufacturing a
successful plan. The internal chain ABI accepts at most 64 callbacks; the C++
adapter additionally bounds nested dispatch to 16 per thread.

The C++ adapter resolves the ordered registry snapshot, pins every object and
implementation under one epoch and validates all service tables before entering
the first hook. Leases span the complete chain, including the core planner and
post-planning callbacks. No loader/registry lock spans a callback; C++ exceptions
are contained before returning across Rust. The public ABI is a separate
`optimizer_spi.h`, with borrowed metadata/continuation, not private plan layouts.
The SDK's `optimizer::Context::call_next` also enforces the protocol and wraps
unwind panics; host Rust still uses its existing abort policy.

The Rust text plugin registers a real hook plus a SQL-visible invocation counter.
Native-loader tests check continuation execution, exact core failure, C++
exception containment, recursion limits and released references. The kernel
fixture drives the real optimizer entry with an intentionally incomplete context
and observes the Rust counter increment and original core error. That is
call-site evidence, not successful full-plan construction or a live SQL server
test. Planning cache hits and costing helper entry points are not instrumented.
Custom-path submission, plan replacement, executor and index protocols remain
part of the full design, not capabilities of this observation/around v1.

## Database SQL packages backed by native modules

The sequential routine adapter now carries a control file's `native_module`
through its bound installation spec into the existing catalog transaction.
Preflight checks the script/bound/request identities agree; it does not turn
that declaration into permission to load code. Catalog recording locks the
declared provider, requires stably ACTIVE state, and records the association
with SQL membership under the same Rust-coordinated install transaction.
Module RESTRICT removal already checks these installed-Extension associations.

SQL updates preserve the original module identity and the same member/owner
checks. Routine DROP removes SQL members and the association without unloading
the module. The adapter no longer rejects all native-backed packages, but still
rejects non-routine members and unsupported CASCADE. Bare CREATE-argument batch
resolution remains pure-SQL-only because it does not carry the module identity.

The `rust_text_ops` example delivers SQL wrappers and a versioned update, using
the existing Rust text module through normal expression resolution. Production
kernel tests resolve the actual package's PL bodies with a live Rust DSO and
exercise Query→Root identity transport with controlled schemas/publication.
This is not yet live database installation, module-removal concurrency or SQL
transaction rollback evidence. The declaration is one explicit dependency, not
automatic discovery of every module referenced inside a routine body. General
catalog builders and native-symbol DDL remain unfinished. Extension `requires`
now validates and locks already-installed providers in the same tenant/database
before schema application, and records stable-ID edges in the same transaction.
Provider removal is RESTRICT; consumer removal cleans outgoing edges. Versioned
updates can replace the declared set under a dedicated transaction-held graph
update fence. Rust validates the resulting stable-ID graph and rejects cycles
before schema changes; edges, members and version commit together. No-op updates
still require the same set (order is irrelevant). Automatic
installation, version constraints and CASCADE remain
unfinished. Controlled catalog transport tests are not live isolation/rollback
evidence; see the [installation guide](../../docs/developer-guide/zh/plugin-extension-install.md).

## Native module mapping

`native.rs` owns the native mapping handle and fixed ABI entry lookup. The C++
loader retains artifact verification, catalog coordination and plugin callback
adaptation; it no longer calls dlopen/dlsym/dlclose or LoadLibrary/GetProcAddress/
FreeLibrary. Unix uses NOW/LOCAL and Windows preserves the existing restricted
DLL search policy. Host allocation completes before native loading, and error
diagnostics use caller-owned bounded buffers.

Mapping operations require exclusive host ownership and run outside loader and
registry locks, since native constructors/destructors can execute. There is no
implicit Drop-based unload. Publishing a module permanently disallows failed-load
rollback from closing its mapping. Ordinary disable retains mappings; explicit
process-exit shutdown must first stop threads and drain all callback references.
The Rust flag enforces close phase, not proof of that host-side drain contract.

Close failure preserves handle ownership. Failed-load cleanup retains the
identity and artifact; terminal shutdown reports failure and can retry, without
claiming the runtime has completed shutdown. The OS failure path is unit-tested
with an injected close operation; tests never fabricate an invalid OS handle.

Execution context extensions are negotiated independently of mapping. Scalar
service tables at execution SPI minor 0 receive an exact v1 context, including
when the caller has a larger context. SQL-aware services opt into minor 1 and
still check the supplied context size. Existing GIS binaries with strict v1
size checks therefore need no algorithm or ABI-wrapper changes.

## Registration journal

`registration.rs` owns normalized contribution payloads, transaction tokens,
budgets and commit/abort/seal state. The C++ adapter validates the public ABI
and transfers a heap-owned metadata object only on a successful stage call.
Failed stage calls retain caller ownership. Drop calls the metadata's C++
destructor exactly once; these destructors may neither unwind/reenter nor
invoke plugin code. Execution tables remain owned by the mapped plugin, not
by the journal's metadata allocation.

All journal access is serialized by the existing host mutex, including
observation and destruction. There is no additional Rust lock, nor a second
C++ transaction state. After seal, immutable snapshots feed the existing
catalog/registry candidate protocol. Mixed services and SQL objects commit
as one vector move after capacity reservation; conflicts leave the entire
transaction intact for abort/retry.

Token allocations remain tombstones after commit/abort until journal
destruction. Unknown, foreign and ended tokens are compared by identity, never
blindly dereferenced. To bound tombstone memory, a module activation can issue
65536 tokens in total, including ended and empty transactions. The separate
limit of 4096 simultaneously open transactions remains unchanged. Payload
vectors are freed on commit/abort; tombstones retain no contribution buffers.

## SQL overload resolution

`resolution.rs` is the single implementation of overload selection. C++
captures an immutable registry snapshot and its epoch, filters by name/kind,
and passes borrowed metadata to Rust without holding the registry mutex.
Signature spans are built once with each immutable entry and shared across
snapshot copies. Rust retains no pointers, invokes no callbacks, and allocates
no memory during matching. Executable acquisition still validates the chosen
generation separately; resolving a name does not grant an execution lease.

Compatible typed overloads precede legacy untyped descriptors; within the typed
set, exact matches precede direct implicit casts by cost. Equal best costs are
ambiguous once any argument type is known. All-unknown name/arity probes retain
the existing stable object-ID ordering until child typing. Explicit/assignment
casts do not silently become implicit, and multi-hop casts are not synthesized.

The fallback priority is structural, not a magic numeric cost: a legal
`UINT32_MAX` conversion cost is accepted and cannot cause an untyped function
to steal a typed match. Costs accumulate in 64 bits. Malformed argument type
IDs are rejected even if no candidate matches the SQL name.

`seekdb_runtime_resolve_cast` also selects direct conversions for explicit,
assignment and implicit SQL contexts. A declared context must be at least as
permissive as the requested context. Minimum cost wins; equal minimum costs
return ambiguity rather than depending on object names or registration order.
Source/target must be known type IDs; identity and unknown-NULL coercions are
left to the SQL caller. The entire bounded snapshot is validated, including
unrelated entries after a matching cast.

The C++ registry captures one owned immutable snapshot and uses this Rust
selector outside its lock. Existing implicit function-argument execution uses
the same selector. `ObPluginLoader::resolve_sql_cast` produces a pointer-free
binding with source/target, requested/declared context, object/owner/generation
and catalog epoch. `execute_bound_cast` acquires exactly that object and its
implementation, checking the epoch under the same registry mutex. A catalog
change requires explicit rebinding; an already acquired call keeps its leases.
No cast selection or fallback occurs inside bound execution. Synchronous output
validation/copying belongs to the caller's sink; SQL context suffixes still
follow the existing function-service version negotiation.

The server/provider bridge exposes these operations to SQL adapters. Assignment
expressions can insert a registered assignment cast before type encoding, and
explicit SQL casts can bind a named plugin type. This does not make arbitrary
bytes assignable to persistent plugin columns.
Rust unit tests, C++ registry tests and actual Rust text DSO calls cover context
eligibility, ambiguity, byte/NULL conversion, binding validation, stale epochs,
generation replacement and leases; they are not SQL-server assignment tests.

### Fixed scalar expression bindings

The SQL adapter now freezes a scalar function binding at its first successful
raw-expression type deduction. A new binary hidden constant replaces the original
SQL-name constant and owns versioned `PluginFunctionExtraInfo` wire: object,
generation/epoch, result/argument identities and sparse stored-argument codecs.
The original name constant is not mutated. Wire is bounded to 2 MiB; existing
argument/codec limits still apply. Untyped binary input is not accepted as a
compiler-created binding, and codegen refuses an unbound name-only expression.

Repeated deduction and codegen validate the fixed wire and child identities,
direct nested binding epochs and stored-format requirements without re-selecting
functions or codecs. A child rewrite that changes type or stored representation
requires an explicitly new binding. Cross-arena PL copies own their constants;
execution uses owned plan extra-info and never parses or evaluates hidden wire
per row. Reading an old binding does not grant execution: the loader's existing
epoch/lease checks still reject invalidated plans. Unbound discovery probes may
continue to query the registry.

Optional `PluginExprType` annotations now carry a query epoch through existing
projection/alias/exec-param copies. Non-stored runtime identities require a
nonzero epoch; stored schema identities can remain zero until codec binding.
Function, cast and type-value metadata validate those epochs; cast selection
passes the source epoch to the provider fence. Encoders and column-conversion
wrappers preserve known input epochs. Encoder TYPE selection is now frozen by
the builder; subsequent inference/codegen only validate its owned binding.
This does not persist a module generation in columns or change the public ABI.
CASE and UNION branch type composition are wired as described below. Arbitrary
wrappers and full optimizer/plan invalidation remain unfinished.

### Fixed encoder binding

`PluginTypeEncodeExpr::build` selects the persistent TYPE codec, checks the
logical object/owner/format/version and input epoch, then stores a bounded
binary binding constant next to the value argument. Assignment casts must use
the same epoch. The private operator now has two arguments; public SQL/DSO ABI
and persisted column formats are unchanged. Unknown NULL also receives the
selected TYPE epoch, while execution still skips its codec callback. Same-format
stored copies do not double-encode.

`read_binding`, `calc_result_type2` and codegen validate the complete wire,
target annotation, source representation and epoch without selecting again.
Unannotated binary input cannot impersonate a compiler-built encoder. Execution
uses plan-owned extra-info and never evaluates the hidden constant; existing
bound-codec epoch/lease admission still applies. Retaining a readable old
binding is not permission to execute a disabled or replaced module.

Controlled codec tests cover new-binding epoch conflicts, fixed binding when
the provider changes or is unavailable, truncation/output clearing, changed
target/source metadata, forged unannotated input and PL copies surviving source
arena destruction. Full constructor/assignment-cast/decode → encode → physical
column conversion trees check no additional registry lookups during repeated
inference, codegen or evaluation. These tests use the existing controlled codec
provider, not end-to-end Rust persistent SQL write/read/recovery.

### Fixed table-function bindings

The SQL adapter freezes table-function object/generation/epoch, actual argument
identities and all column names/types/nullability into an owned, versioned binary
constant during initial deduction. Repeated deduction, column discovery and
codegen validate that binding without selecting or describing the object again.
The wire is bounded to 2 MiB with existing argument/column limits. Result and
argument annotations must match the selected identities and epoch; codegen also
checks the result's physical carrier. PL expression copies own their metadata.

Plugin table columns participate in ordinary name resolution through the fixed
column list, not the PL collection/type lookup path. Output column expressions
retain logical identities for nested scalar plugin consumers. Stored inputs are
decoded through a fixed type-value expression before opening the cursor. The
cursor uses plan-owned argument identities and the bound loader API; there is
no SQL-name lookup at first fetch. Existing runtime admission/leases still apply.

The row sink checks declared identities and nullability, preserving an emit
failure even if the plugin callback subsequently returns success. Rescan closes
the cursor and reopens the same binding on demand; close is idempotent. NULL
arguments are passed without reading payload bytes; the loader, not this SQL
adapter, applies the descriptor's strictness to converted inputs. SQL EOF is
cached until rescan, including strict calls which created no plugin cursor.

The kernel fixture exercises full SELECT resolution and generated expression/
cursor execution with a controlled provider, including a typed table output
consumed by a scalar plugin function. A separate stored-input fixture supplies
an in-row LOB column datum and checks decoding before cursor open. These are not
proof of a complete physical FunctionTable operator plan, an actual Rust table
DSO, persistent SQL storage/recovery or concurrent server execution.

A separate real Rust DSO fixture now registers and runs `seekdb_rust_words`
using the public Rust table SDK. It parses complete SELECTs, generates expression
frames and routes table describe/open through the production loader, registry,
leases and Rust service. Custom token values feed the typed character-count
overload; Unicode whitespace, ordinals, empty text, typed NULL, rescan and fixed
lookup counts are checked. These new cases are separate from the controlled
stored-type provider above. They do not prove automatic optimizer plan selection,
live-server catalog transactions or multi-hop implicit cast search.
The final fixture drives `ObFunctionTableOp` itself (open/get_next_row/rescan/
close), including EOF, timeout and repeated close; it manually assembles the spec
from generated expressions instead of exercising optimizer-to-spec generation.

Bound table open now uses the same prepared direct implicit-cast path as scalar
execution. Rust still owns selection policy; C++ acquires object/implementation
leases at the binding epoch, validates the entire argument set, and executes
casts into host-owned buffers before table open. The implementation lease, not
the SQL object's owner, supplies the callback instance. Conversion output shares
the scalar 16 MiB aggregate budget. Unknown NULL takes the declared target type;
typed NULL goes through any required cast. Table SPI v1 supplies no SQL suffix.

The cursor retains cast leases and instances, not a borrowed loader pointer.
Direct rescan reuses that selection and rejects original type/count changes;
next is disabled after rescan/conversion errors until successful rescan. Close
releases table and conversion leases. A changed catalog epoch rejects stale
column descriptions and new opens, while already admitted cursors may finish
with their pinned code. The SQL operator's close/reopen rescan needs fresh
admission; it does not silently choose a replacement binding.

The real Rust bytes-only table entry is separate from the custom-text entry and
strictly checks its input ID. Native regression covers conversion on open and
rescan, caller-buffer ownership, malformed arguments/bindings, NULL, failed-cast
recovery, stale epochs and exact lease release. Complete SQL cases also include
custom-to-bytes input and typed output consumption in the physical operator.

### Table NULL policy

`NULL_PROPAGATING` now gates table callbacks after prepared implicit casts, not
before them. A cast may independently transform NULL to non-NULL or vice versa.
Initial strict NULL open returns `OB_ITER_END` with no cursor; all temporary
leases are released. For a live cursor, strict NULL rescan sets a dormant-empty
state without invoking plugin rescan/next, while keeping the old cursor and its
leases until a non-NULL rescan or close. Failure still disables next; successful
rescan restores it. No query context is captured to manufacture a new cursor.

Without the flag, the plugin receives typed NULL and may return rows. Rust
words-or-null emits `<NULL>` and its strict SQL alias shares the same service;
the SQL C generate-series sample explicitly declares NULL propagation. Native
tests cover typed/unknown NULL, either argument NULL, NULL/non-NULL rescan and
lease release. A controlled cast test covers source/result nullness independently
and strict/non-strict gating; it supplements rather than replaces actual Rust
DSO/SQL tests. The raw C service alone does not enforce catalog descriptor flags.

### Common logical type selection

`resolution/common.rs` supplies `seekdb_runtime_resolve_common_type`, called by
`ObPluginServiceRegistry::resolve_common_type` over an owned immutable snapshot
outside the registry mutex. This is the selection foundation for expression
composition. The loader/server runtime and `ObIModuleProvider` now expose it to
SQL adapters; CASE results and set-query projection type merging now call it.

Candidates are the known input identities, not arbitrary third-party supertypes.
Unknown NULLs do not constrain selection; repeated identities do not add votes.
Only direct implicit casts participate. Each distinct non-identity source adds
`1 + minimum cast cost`; the lowest total wins. Equal best targets or ambiguous
minimum casts needed by the winning target return ambiguity. All-unknown/empty
input returns NOT_FOUND for the SQL caller to apply its native default. Identity
requires no callback. No plugin code runs during this selection.

This is an explicit seekdb logical-ID policy, not a reproduction of the complete
PostgreSQL or native seekdb common-type rules. Built-in numeric promotion,
collation, typmod, shape and physical result layout remain SQL-layer concerns.
When wiring expression composition, native rules must be reconciled with this selector;
using a shared varchar carrier alone cannot establish a common logical type.

Unlike allocation-free overload/direct-cast matching, this operation uses
fallibly reserved scratch vectors, bounded by 1024 arguments and 4096 casts.
It sorts input identities and cast edges and uses 64-bit totals, including legal
UINT32_MAX costs. Every input and cast is validated, even unrelated metadata.
The C bridge clears the selected index to UINT32_MAX on failure when the output
pointer is valid. C++ returns an owned identity and snapshot epoch, or clears
both outputs on failure; the result is not an executable lease. Subsequent
conversion bindings must match that epoch rather than silently mixing snapshots.
`resolve_sql_cast` and the server/provider forwarding methods accept an optional
expected epoch (zero preserves standalone cast selection). A successful cast
selection from a different snapshot returns STATE_NOT_MATCH with an empty binding;
it never silently substitutes a new conversion. Normal lookup/validation failures
also remain failures. Bound execution still checks its epoch and leases atomically,
so the metadata fence does not replace execution-time admission checks. These
methods are internal C++ host APIs, not new public DSO ABI structures.

Tests compare all 4096 three-type conversion graphs against an independent
reference for six input permutations, and cover NULLs, duplicate branches,
ambiguous casts, maximum bounds/costs, provisional publication, removal and
generation replacement. They prove selector/registry behavior, not CASE/UNION
SQL support or complete plan invalidation.

`PluginBranchType::prepare_case` reconciles custom/stored THEN/ELSE branches before native CASE
physical inference: select with Rust, check epochs, prepare direct implicit casts
or same-type stored decoders, then publish all replacement branches together.
NULL needs no invented callback. Native-only branches retain native promotion;
native results outside the plugin value API are not mislabeled as bytes. The
result annotation carries the chosen identity/epoch. Repeated inference reuses
the prepared branches, and the original CASE executor retains short-circuiting.

The real Rust DSO fixture now has 26 SQL cases, including CASE branch selection,
NULL/omitted ELSE, nested functions, implicit conversion to bytes, native numeric
promotion and a missing-conversion bind error. Successful binding also checks
no additional provider lookups during repeated inference/codegen. A controlled
stored-column fixture verifies decoder insertion and no partial replacement on
epoch failure; it does not prove persistent CASE execution. Custom
comparison/operator semantics and complete optimizer propagation remain open.

`prepare_set` uses the same Rust selection policy before the optimizer's
`try_add_cast_to_set_child_list` and `gen_set_target_list` physical type merging.
It stages logical conversions across all projected columns, then publishes the
replacements together. It retains SQL's existing incremental set-group order,
native numeric promotion and collation. Native cast wrappers and set outputs
carry the selected identity/epoch; formerly unknown NULLs acquire the target
identity after coercion so that later groups do not reinterpret them as bytes.
Unchanged built-in callbacks retain their original binding type IDs.

Custom-result UNION ALL type merging is enabled. Native-result DISTINCT uses
native operators; custom-result comparison/deduplication and plugin recursive
CTE anchor coercion explicitly remain unsupported pending their actual semantic
interfaces. No opaque-byte comparison is substituted. The real Rust fixture
exercises set binding, nested output identity/copying, converted-projection
codegen/evaluation, NULL order, conversion to bytes, native numeric promotion,
missing casts and custom DISTINCT rejection. A controlled stored fixture checks
decoder insertion, epochs and no partial publication after a later column fails.
These are not UNION physical-operator, persistent scan or recovery tests.
Complete optimizer/VALUES/subquery propagation remains open.

### Scalar subquery identity

The scalar `ObQueryRefRawExpr` type visitor now owns a copy of the selected
expression's plugin identity, storage representation and epoch alongside the
native physical column type. It performs no new registry selection or plugin
execution. Existing outer function bindings still reject changed argument
identities/epochs. Missing column metadata, mismatched output shape/carrier or
a plugin-annotated reference without its statement fail explicitly; native
replacement outputs and non-scalar set/row references clear stale annotations.

The actual Rust DSO fixture also runs complete SQL parsing and `ObSelectResolver`
for derived-table UNION consumers, nested scalar queries, typed NULL, CASE/set/
LIMIT composition, dynamic result types, native/EXISTS queries and missing-cast
errors. It checks the outer binding's logical argument ID, repeat-inference
lookup counts and absence of execution callbacks during binding. LIMIT uses
the existing controlled empty schema manager/guard fixture, not authenticated
production schema access. Separate raw-reference checks cover owned copies,
stale epochs, missing references, physical mismatch and scalar/set cleanup.
This extends the evidence to complete SELECT resolution, not full optimizer →
SubPlanFilter/UNION execution, correlated or row-valued subqueries, cardinality
behavior, persistent storage or complete plan invalidation.

## Dependency planning

`dependency.rs` supplies provider-before-consumer plans to the real catalog
startup preparation path. C++ reads one catalog writer snapshot, rejects missing
or disabled providers, filters archived consumer generations and maps sorted
plugin IDs to ordinals. Rust deduplicates ordering edges, builds compact adjacency
offsets, and selects the lowest currently ready ordinal. C++ no longer maintains
a second topological sorting implementation.

Startup explicitly ignores self-service edges; other callers can choose to
reject self edges as cycles. A cycle returns a blocked ordinal for diagnostics,
not a claim that the node itself is a cycle member. No partial ordering is copied
to the host on failure. The existing catalog transaction is rolled back on
planning failure; Rust neither commits SQL nor publishes modules.

Planning is iterative and bounded at 65,536 nodes and 1,048,576 input edges per
snapshot (before duplicate elimination). Larger graphs fail explicitly. All
working storage uses fallible reservation before scheduling; the ready queue
and result vector cannot grow beyond the reserved node count. This DAG planner
does not implement Extension install transactions or recursive SQL type/shell
object creation, which need their own object semantics.

## Linking

`seekdb-host` is the single Rust staticlib linked into C++. It aggregates
`sql-nio` and, with feature `plugins`, `seekdb-plugin-runtime` as rlibs. Do not
link another independently generated Rust staticlib into the same executable.
CMake's existing `sql_nio` target forwards to `seekdb_rust_host`. The server's
`SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS` option controls the Cargo plugin feature.
Bazel's existing `sql_nio_archive` label now supplies the aggregate archive.

## Verification

The production loader now routes existing host.alloc/free callbacks through
Rust generation-local memory accounts (`src/memory.rs`). Optional loader limits
cover requested host-allocation payload and live allocation count; defaults
remain unlimited. Runtime status reports current/peak usage, allocation failures
and invalid frees. Empty accounts allocate no tracking table and add no workers.
Deinit closes allocation admission; remaining blocks are retained until matching
free or exclusive terminal account destruction. This is not a query arena,
plugin-global allocator, GPU/tenant budget, RSS measurement or a native sandbox.
See `docs/developer-guide/zh/plugin-host-memory.md` for ownership and limitations.

Production startup accepts `--plugin-memory-limit=64MiB` and
`--plugin-allocation-limit=4096`. `memory_limit.rs` owns strict allocation-free
decimal/binary-suffix parsing; C++ only passes the resulting policy through the
server options and runtime bridge to the existing loader. Both default to
`unlimited`, zero denies nonempty allocations, and disabled-plugin builds reject
explicit flags. Limits apply independently to each module generation, not to
their sum. Effective startup limits are logged. The read-only
`oceanbase.__all_virtual_plugin_memory` table exposes the loader snapshot through
the ordinary C++ SQL adapter, with PROCESS plus ordinary object privileges.
It copies values once per scan, does not retain runtime/module pointers between
rows, and excludes library paths/error text. SHOW PLUGINS remains catalog state.
No background monitoring thread or catalog writes are added. Per-account Rust
counters are sampled together; different accounts/state/leases need not share an
instant. This is neither a transaction snapshot nor process/GPU heap accounting.

`plugin_host_memory` exercises the real C SQL plugin's cursor allocation through
the C++ host callbacks into Rust. Its activation catalog is still a test double.
`plugin_rust_host_memory` loads Rust text v14 and tests its SDK HostBuffer path:
zero-budget empty results, quota denial, release after scalar delivery failure,
per-row batch reuse, and no partial delivery after a later row exceeds quota.
Other Rust Vec/String and host result-staging bytes are not included in that
module budget. These fixtures are not live database or performance evidence.

The optional public memory SPI adds independent owned-byte tokens. Rust account
references now include the C++ root and each token; root destruction closes
admission, while token data remains valid until its final host release. Raw free
cannot consume token-owned data. No plugin callbacks run during this cleanup.
Tokens do not pin plugin code: Rust text v15 words cursors separately retain the
loader's execution lease and use OwnedHostBuffer for their persistent input.
`plugin_rust_host_memory` also checks cursor worker migration and a shared quota
across borrowed concat buffers and owned words buffers. Runtime tests cover
release after root destruction; the kernel runner compiles memory_spi.h against
the privately installed SDK. Neither proves background/model task lifecycles.

Run from the repository root:

```sh
cargo test --manifest-path rust/Cargo.toml -p seekdb-plugin-runtime --offline
cargo clippy --manifest-path rust/Cargo.toml -p seekdb-plugin-runtime --all-targets --offline -- -D warnings
cmake -S rust/plugin-runtime/tests -B build_release/plugin-runtime-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build_release/plugin-runtime-tests -j4
ctest --test-dir build_release/plugin-runtime-tests --output-on-failure
```

Use the workspace's pinned toolchain (running Cargo from `rust/` picks up its
`rust-toolchain.toml`). Offline Cargo requires dependencies in the local cache.
The standalone CMake test builds the production aggregate Rust archive and real
C++ registry without needing a running database. It checks C ABI state/status
values, candidate invisibility/promotion/abort, service lookup, moved leases,
quiesce and drain. It also calls sql-nio's ABI rejection path, proving network
and plugin symbols coexist in the archive without opening a socket.
The same bridge test covers dependency edge layout, startup self-edge handling,
duplicate requirements and atomic error output. Rust tests exhaust all 4,096
four-node directed graphs without self edges against a small reference scheduler,
and check a maximum-length reverse chain without recursive traversal. They do
not exercise persisted dependency rows or real startup transaction rollback.

Additional tests load the SQL extension DSO using both direct per-object
registration and legacy snapshots. They resolve and invoke `seekdb_add_one`,
exercise a rejected catalog commit without visible registrations, and check
descriptor deep copies, mixed service/object transactions, conflicts, abort,
closed admission and the shared registration quota. Catalog guards in these
tests are protocol doubles, not a running database.
The existing registration-conflict reference DSO is also loaded unchanged,
checking the historical open-transaction/service quotas, error statuses,
abort/retry behavior and acquisition of its dynamically registered services.
The registry-resolution test crosses the C++/Rust boundary for expensive casts,
fallbacks, ambiguity and unknown-type probes; it also checks snapshot-copy
signature lifetimes and rejection of an old binding after generation replacement.
Native mapping tests use real DSOs to check missing files/entries, publication
close restrictions, destructor timing and independent references to the same
library. The GIS integration test loads the existing C/C++ sources through the
production C++ loader and Rust mapping layer, calls ST_Point with both context
versions, then calls ST_Centroid through the C++ geometry engine. Its catalog
guards are still test doubles; it is not a SQL parser/transaction test.

These tests do not prove SQL installation, persisted catalog transactions,
GIS query results, complete server linking, or compatibility on other platforms.
Those remain separate acceptance requirements.

`tests/query_catalog_server.py` is the opt-in real-server acceptance path for
query-time routine mutation through the Rust plugin. It covers caller DML and
catalog commit visibility, provisional SQL/PL dependencies, repeated named
savepoints, late outer-expression failure, invoker/automatic routine privileges,
DROP/recreation cache behavior and non-membership of dynamic business objects.
It requires a disposable loopback server, the current `rust_text` plugin,
PyMySQL and `automatic_sp_privileges=1`; it never installs plugins or changes
global settings. Run with `--port 2881 --confirm-disposable-server`; supply the
administrator password through `SEEKDB_TEST_PASSWORD`. Failed runs retain their
randomly named database/users for diagnosis; successful runs remove only those
fixtures. See `docs/developer-guide/zh/plugin-query-catalog.md` for the matrix.

The separate `plugin_query_catalog_runner` CTest only runs dependency-free
helper/control-flow tests. It is NOT evidence of real catalog correctness.
The server matrix remains unexecuted here (socket creation is denied); it does
not cover concurrent DDL, injected commit uncertainty, restart or performance.

`plugin_type_identity` additionally exercises the actual C++ persistent type
metadata helpers: old v1 records and new generation-independent v2 records,
logical identity equality, numeric bounds and malformed fields. No database is
used by this test. `type_identity_server.py` provides a separate opt-in prepare /
external-restart / verify workflow for CREATE TABLE LIKE and DROP COLUMN/TABLE
after provider generation changes. See the developer guide's
`plugin-type-identity.md`; real restart and concurrent RESTRICT remain unverified.
