# Numeric and boolean expression programs

The goal is a reusable Rust execution responsibility for complete filter/project graphs. A query borrows a prepared program from its immutable physical plan and owns a separate execution state, including shared intermediate values and demand-sensitive caches. Scalar execution uses the same interface with one row. C++ retains SQL resolution, type deduction, optimization, relational operators and datum frames.

## Current execution flow

At baseline `16cd4f958f5ca52e160d54fd5abe125f9c1edbc4`, static expression code generation assigns typed evaluator functions and frame layouts. `ObOperator::get_next_row` and `get_next_batch` apply ordered filters and publish projections. Downstream consumers can also demand scalar outputs lazily. `ObExpr::eval` recognizes child-projected materialized results. Batch evaluated flags distinguish rows previously requested through different branches. Operator cleanup commonly uses explicit `destroy`, so ordinary C++ destructors alone cannot release Rust owners.

These facts require an operator-level program boundary, stopping at materialized child outputs. Arbitrary C++ expression leaves cannot be evaluated eagerly. A computed scalar child output without a structural availability guarantee makes the whole operator ineligible. The compiler must not expand that boundary into hidden base columns: materializing operators can restore the computed datum while those base-column frames are stale. The implementation selects a whole supported operator filter/project graph before importing inputs, and never retries failed Rust execution through C++.

The first adapter admits table scans without operator filters, subplan scans, sorts and materialization operators. These producers leave input columns available and do not change the semantics of internal expression nodes. Subplan scans materialize their projector destination columns before entering the generic filter/project stage. Sort and materialization operators restore their child outputs. Other operator kinds remain on the C++ path until their own produced expressions and available inputs have an explicit contract. For example, ROLLUP publishes a grand-total NULL for a computed grouping key; recursively recomputing that key would lose the grouping semantics. Child-output boundaries alone cannot establish this producer contract.

Table scans with operator filters also remain entirely on the C++ path. Their final-limit implementation can consume those filters before the generic operator stage. Migrating that stage requires moving filter execution ahead of its limit accounting and removing the later duplicate call. Table-scan projections without operator filters can use Rust. Filters above a materialized subplan boundary exercise the complete Rust filter/project path.

## Alternatives and synthesis

Two independent architectural passes compared a native DAG and an Arrow/DataFusion physical-expression executor. Neither dependency is present in the local Cargo lockfile or source cache. No new large dependency was downloaded.

DataFusion supports real short circuit. Its current default `evaluate_selection` filters a RecordBatch, evaluates it and scatters to original row positions. Checked integer arithmetic also requires explicit overflow configuration. Reusing physical expressions would still require execution-local evaluated masks, shared subexpression caches, SQL error adaptation, accounting and cancellation. The sources consulted were upstream [PhysicalExpr selection](https://raw.githubusercontent.com/apache/datafusion/main/datafusion/physical-expr-common/src/physical_expr.rs) and [BinaryExpr arithmetic](https://raw.githubusercontent.com/apache/datafusion/main/datafusion/physical-expr/src/expressions/binary.rs).

The selected first cohort uses a validated native DAG. It supports signed integer inputs and constants, same-type parameters, signed add/subtract/multiply/DIV, comparisons including NULL-safe equality, NULL predicates, truth conversion, AND/OR/NOT, searched CASE and signed widening/identity casts. Unsigned, floating, decimal, string, volatile, external and assignment signatures remain in C++. Every node has the precise physical signature `Nullable<I64>`; boolean results are SQL integer values 0 or 1. This avoids introducing a framework while its core execution contract still needs replacement. Future type families extend the native operation/type model rather than translating C++ class hierarchies. This decision is architectural, not a measured performance claim.

`ObDatumVector` contains packed pointer-bearing descriptors and scalar/batch indexing. It is not an Arrow primitive array. A borrowed descriptor reader would require platform-aware unsafe layout handling. The first adapter instead gathers signed payloads and validity through C++ accessors into reusable Rust-owned execution storage. It scatters roots into C++ reserved frame storage. Both conversion costs belong in the measured query cost.

## Native ownership and usage

`Program::new(nodes, filters, projections, input_count)` is the native convenience constructor. `Program::from_instructions_in(spec, backing)` validates and compiles into caller-owned immutable storage. Both validate references, input indices, arities, node count and graph depth. Native callers can share a program reference without imposing a reference-counting allocator on adapters. No CXX type enters this interface.

The compact program uses five u64 words per node plus argument and root words, at most 47,104 bytes. The CXX plan host allocates it through the plan allocator before publication. The two fixed-size CXX Box handles use checked Rust system allocations and return allocation errors. `required_words(&program, capacity)` determines the complete execution region. `Execution::new(&program, capacity, backing)` owns a backing object implementing `AsRef<[u64]> + AsMut<[u64]>`. A native caller can supply a Vec. The CXX adapter supplies an exclusive allocation from the query allocator. Rust owns its layout, input planes, intermediate values, per-row validity/evaluated state and selection. Mutable state is never shared between executions.

The caller fills input values and validity, calls `begin_batch(rows, skip)`, then `filter_project(&program, control)`. Filters run in order and only surviving rows reach projections. AND, OR and searched CASE request only needed rows and branches. Shared nodes reuse individual evaluated rows. Failed batches cannot be retried without rebinding. A new batch invalidates constants and intermediate caches, so parameter rebind and rescan cannot retain stale values.

Public result, flag, and selection views are empty until execution completes successfully. Input mutation and a failed execution invalidate previous views. Invalid-program execution also poisons the batch until a new bind. Copied values remain independent of later binding or destruction.

Execution construction initializes input validity to NULL. The backing must already contain initialized u64 values, as required by the native slice interface. The CXX allocation adapter supplies that initialization. Batch binding clears only the current rows of each node's flags; inaccessible capacity rows are reset when a later batch exposes them. Values remain unusable until the corresponding flags report evaluation.

The adapter creates its runner with the first actual row count, clamped to one, instead of the plan maximum. Before a larger batch binds inputs, the CXX `reserve_rows` operation builds a fully initialized replacement execution in Rust and swaps it only after success. It preserves the runner handle, host and prepared-program identity. Allocation failure leaves the previous storage intact but fails the query without publishing old results or retrying in C++. All buffer borrows must end before reserve. Previously copied C++ results remain valid. Smaller batches retain capacity and rebind normally.

Growth chooses the smallest capacity that covers the incoming rows from the declared maximum's floor-halved sequence. Including the exact first allocation, total requested execution capacity remains below twice the declared maximum even when an arena retains released regions. This bound excludes allocator headers, page rounding and other buffers. Rows beyond the declared producer limit fail before allocation or input access. Native Rust callers can obtain the same transactional replacement by assigning a successful `Execution::new` result after ending prior buffer borrows; they need no CXX types.

The synchronous `Control` capability polls cancellation at entry and bounded row/node work intervals. Native errors carry node, row, operands and kind. Checked signed arithmetic reports overflow. Signed integer DIV uses NULL or an error on zero according to the resolved expression policy. This cohort does not invent division warnings; unsupported warning-producing conversions remain on the C++ path.

A native caller needs no CXX types. For example, a one-row projection can use the same path as a batch:

```rust
use expression_program::{Execution, Node, Operation, Program, required_words};

let program = Program::new(
    vec![
        Node { operation: Operation::Input(0) },
        Node { operation: Operation::Constant(Some(1)) },
        Node { operation: Operation::Add(0, 1) },
    ],
    vec![], vec![2], 1,
)?;
let words = required_words(&program, 1)?;
let mut execution = Execution::new(&program, 1, vec![0; words])?;
execution.input_values_mut()[0] = 41;
execution.input_validity_mut()[0] = 1;
execution.begin_batch(1, &[0])?;
execution.filter_project(&program, &mut ())?;
assert_eq!(execution.node_values(2), &[42]);
```

## Compatibility adapter

The existing CXX generator and single `seekdb-rust` archive expose `Prepared` and `Runner`. Flattened instructions are a private lowering representation. The bridge parses them into native operations. Runtime binding and execution use whole buffers, with no row/node callbacks. The only host capabilities allocate and release program/workspace regions and check cancellation.

The physical plan owns a catalog of immutable Prepared programs and input/root mappings. Operator specs borrow their compiled record. Normal code generation and unshared PX deserialization build the catalog after final expressions and operator trees exist, before publication. Shared PX tasks borrow the existing catalog. Runtime selection does not mutate cached plans or use a global lookup map. Literal values come from plan constant frames; parameters remain execution inputs.

Each operator execution owns an adapter, Runner and query-accounted workspace. It borrows the compiled record while the existing result-set plan guard keeps the physical plan alive. Explicit operator destroy drops Runner before its query host. Plan reset and destroy drop the catalog and Prepared before resetting frames or the plan arena; partial preparation failures use the same cleanup. Only Prepared permits concurrent immutable borrowing. Runners and masks are isolated per execution. No-evaluator roots need no flag writes, which avoids mutating shared literal-frame metadata.

C++ receives stable copies of computed projection roots, so no Rust result pointer escapes to datum frames. Filter results remain in Rust; the operator consumes their selection mask. A filter expression also used as an output is still exported through its projection root. The two fixed-size CXX handles use checked Rust system allocations. Variable-size metadata and compiler scratch use the plan arena; scratch capacity remains accounted until plan eviction. Their arrays grow geometrically in element units instead of rounding every small expansion to a normal arena block. Query skip storage reserves enough for the current rows before filling it and grows geometrically, bounded by the declared batch capacity. This avoids retaining a new arena allocation for every small increase in row count. These policies retain the existing allocators and failure paths; they do not change expression selection or arithmetic. Execution buffers and skip transport use the query allocator. Preparation is amortized by existing plan-cache reuse, and first-use or reprepare workloads must include it in their costs.

## Verification gates

- Native behavior tests cover shared nodes requested by different masks, SQL NULL truth tables, branch errors, all-skip, filter-before-project, overflow, division policy, batch rebind, snapshots and cancellation.
- A separate Rust integration test links the normal library and uses the public native API for scalar and batch filter/project execution, without CXX host objects. It checks NULLs, skipped rows, multiple roots and retained results after the program is dropped.
- CXX checks cover real input transport, output consumption, allocation failure, release, cancellation and concurrent isolated runners sharing one immutable program.
- Production SQL checks must compare baseline and candidate, including scalar and batch execution, prepared parameters and unsupported warnings.
- `information_schema.GLOBAL_STATUS` counters distinguish prepares, successful program calls, input rows, batch calls and rows reaching actual Rust filter programs. A plan alone is insufficient evidence of execution.
- The remote sysbench comparison includes input conversion, root consumption and compilation/binding. Standard OLTP and custom expression workloads are reported separately, with identical work and errors checked.

No C++ arithmetic family can be deleted yet because unsupported types and non-operator expression consumers still use it. Family deletion requires migrating those callers and all supported signatures. This cohort removes their execution only for selected complete operator graphs.

The [execution-owner and signature inventory](expression-execution-migration.md) identifies the remaining callers and the conditions for deleting C++ evaluators. It separates graph admission, removal of caller coordination, and whole-family retirement.

## Temporary row programs

`ObTempExpr::eval` is a second production owner of the same execution core. RANGE partition expressions and calculated query bounds use this entry outside the generic operator filter/project stage. The adapter admits a complete nullable-I64 graph and returns its single result. Generated-column wrappers with unsupported conversion or character metadata retain their complete C++ path.

The shared C++ lowerer accepts explicit roots, constant frames, and a producer input policy. Operators retain their existing child-materialization rules. Temporary programs accept declared row cells and ordinary same-type runtime parameters. Neither adapter invokes C++ leaf evaluators during input gathering. Temporary programs read row cells and parameter objects directly and validate their indices and types.

`gen_expr_with_row_desc` compiles eligible templates after code generation and input mapping. The template arena stores an opaque image and integer binding/node indices. It owns no Rust Box and needs no individual Rust destructor. Deep copy and UNIS serialization carry those values, never Rust addresses or execution state. Validation checks the image and its correspondence with the adapter's input count, node count, and single projection root.

Native callers can compile the same representation with `compile_image_in(spec, backing)`, inspect its immutable `ProgramImage::words`, and validate received words with `ProgramImage::parse`. `Program::from_image_in(&image, backing)` copies the validated payload into an independently owned program with a fresh identity. Images add six metadata words to the existing packed payload. Native construction, image construction, and parsing share the operation validator and payload layout. C++ does not interpret instruction words.

Each temporary execution context owns a restored Prepared and Runner through a query-allocator host. It retains no template pointer. Binding and diagnostic metadata are borrowed only during a synchronous call with the live template. Cached contexts check a local, nonserialized template incarnation to distinguish reused arena addresses. Copy and deserialization create fresh incarnations. Context creation, cache insertion failure, uncached errors, and destruction release initialized owners before their allocator. Native contexts allocate no legacy datum frames or expression-operator context store.

For admitted templates, Rust replaces recursive flag clearing, row-to-frame conversion, execution-context pointer swapping, and root `ObExpr::eval`. The unsupported branch still needs those C++ helpers. No entire evaluator family is deleted by this extension. A successful integer or NULL result is copied into an `ObObj`; failed import, execution, or conversion leaves the caller's prior output unchanged and returns an error. There is no fallback after native execution starts.

The execution cache is scoped to `ObExecContext`. Runtime traces show that the selected parameter-bound workload reconstructs contexts each SQL execution, while optimization-time partition evaluation can disable the cache entirely. Program restoration, validation, allocation, input conversion, and output consumption therefore belong in throughput measurements. The previous operator benchmark cannot establish the performance of this new owner.

Separate `information_schema.GLOBAL_STATUS` fields expose `Rust_temporary_expression_prepares`, `Rust_temporary_expression_state_builds`, and `Rust_temporary_expression_executions`. They count successful image compilation, successful owner construction, and successful copied results respectively. A state build can precede a later cache insertion failure. Existing `Rust_expression_*` meanings remain unchanged. A range query returning 128 rows still evaluates its temporary range-bound programs as scalar roots; it is not evidence of a new batched temporary-expression API.

The focused C++ suite covers whole-template execution without legacy frames, row and parameter rebinding, independent results, allocation failures, uncached error and cancellation cleanup, deep copy, serialization, reused template addresses, and inconsistent image/root metadata. `unittest/sql/temporary_expression_program.test` adds baseline-recorded SQL for partition boundaries, NULLs, prepared rebinding, lazy CASE errors, scalar execution, and unsupported generated-column wrappers.

## Retired logical execution family

The logical-control migration replaces all C++ scalar and batch execution definitions for AND, OR and NOT. Code generation now calls `prepare_logical_program` and binds the shared native-program compatibility entrypoints. The removed functions are `calc_and_exprN`, `calc_or_exprN`, `ObExprAnd::eval_and_batch_exprN`, `ObExprOr::eval_or_batch_exprN`, `ObExprNot::eval_not`, and `ObExprNot::eval_not_batch`, together with their private two-value combiners and declarations. Constructors, type deduction, and SQL resolution remain C++ responsibilities.

This boundary is independent of operator admission. Uncached scalar, batch, and scalar-from-batch computation for this family reaches a prepared fused logical program. Existing consumer snapshots can satisfy cached demands. Existing operator and temporary programs may still execute their own Rust logical instructions. No old C++ logical evaluator remains available as fallback. Resolver normalization preserves connected AND/OR/NOT nodes; truth conversions of nonboolean operands are explicit external input boundaries.

The native `logical` module exposes pointer-independent `ProgramImage`, `ProgramSpec`, image compilation, workspace sizing, and `Execution` without CXX types. Its iterative executor owns logical values, NULL/evaluated state, row demand masks and the traversal stack. Dynamic validated images cover graphs beyond the original numeric cohort's node and argument limits. Checked sizes and allocator failures remain resource errors. The CXX compiler stores opaque image words and integer parent/argument ordinals in `LogicalProgramInfo`. Existing ExtraInfo deep copy and UNIS transport reconstruct bindings against the current expression tree without serialized Rust addresses or execution handles.

A demanded root uses one lexical execution scope. `ObEvalCtx::TempAllocGuard` keeps its query-accounted backing alive across nested evaluations. Native masks and traversal storage remain private. C++ consumer frames retain materialized snapshots between calls; no persistent native sidecar cache needs a second reset protocol. Availability is initially imported without dereferencing datum payloads. `MATERIALIZED_PENDING` marks a snapshot that Rust requests only if demanded, so a materialized ancestor can hide invalid descendant datum pointers safely.

Nonlogical inputs are demanded for whole row masks. At this boundary the host publishes completed internal logical snapshots, invokes the existing external evaluator, reconciles invalidated or newly available consumer snapshots, then supplies the requested input values. `IN_PROGRESS` protects unfinished parent accumulators, including NULL prefixes, during reconciliation. This explicit handoff preserves shared-node reentry and external cache invalidation. The adapter omits the broad handoff only for the exact existing integer BOOL normalizer over a materialized integer column with no evaluators. It still executes that normalizer and populates its consumer cache. Custom scalar or batch functions retain the complete handoff. It is not a callback implementing AND/OR/NOT, and ordinary internal logical nodes do not cross CXX individually. Existing external operations retain their warning, error, conversion and side-effect policies. Failed evaluation propagates without replay; incomplete roots are not published. Interrupted reconciliation cannot republish stale snapshots.

The generic batch gateway skips nonbatch logical demand when every row is skipped, before any scalar warning or effect can occur. Successful logical results are copied into consumer-owned datum frames. The adapter gathers demanded packed datum descriptors and payloads into native row planes, converts masks for existing C++ inputs, and scatters completed logical values back. This boundary is not an Arrow column view or a zero-copy interface. End-to-end throughput includes those conversions and query-accounted scratch allocations. Cached/projected internal nodes, scalar broadcasts, row masks and batch indices retain their existing consumer lifetime contracts. Concurrent executions share only immutable image metadata.

`information_schema.GLOBAL_STATUS` exposes `Rust_logical_program_calls`, `Rust_logical_program_batches`, `Rust_logical_program_rows`, and `Rust_logical_program_nodes`. They count successful calls, batch calls, active requested root rows, and fused logical operations in successful calls. The node count is program size, not a count of every branch actually evaluated. These fields require the information-schema view; the separate `SHOW GLOBAL STATUS` implementation does not expose them. Input-frontier operations are excluded from the fused logical-node count.

Focused native tests cover truth tables, disjoint masks, materialization, reentry, invalidation, cancellation, independent concurrent scopes and large images. CXX tests also reproduce both previously failing shared-node cache sequences, stale child pointers, partial error publication, all-skip demand, copied/serialized metadata and query timeout. `unittest/sql/logical_program.test` uses baseline-recorded results for mixed string projections, scalar and parallel SQL, NULLs, parameter rebinding, warnings, overflow, aggregate arguments and nested external demand. A parallel hint and a metadata round trip are not alone proof of every deployed PX transport mode.

Arithmetic evaluators, nonlogical truth conversions, CASE, generic consumer frame/cache management and shared arithmetic/comparison helpers remain C++. Their retirement requires separate complete units. The logical deletion claim does not imply that all expression execution has migrated. Performance acceptance must use the final binary and real logical-program hit counts; historical prototype throughput does not accept this replacement.

## Running focused checks

Native tests and lint use the repository registry configuration:

```bash
source ~/.bashrc
cargo test --config bazel/rust_cargo_config.toml --offline -p expression-program
cargo clippy --config bazel/rust_cargo_config.toml --offline -p expression-program --all-targets -- -D warnings
./build.py test //unittest/sql:test_expression_program
```

The SQL corpus lives under `unittest/sql` to keep new test files out of `tools`. Run the established mysqltest client against a dedicated running instance and the committed baseline-generated results:

```bash
deps/3rd/u01/obclient/bin/mysqltest --host=127.0.0.1 --port=39082 --user=root \
  --database=expr_bench --test-file=unittest/sql/expression_program.test \
  --result-file=unittest/sql/expression_program.result
```

Execution coverage is observable through the actual status table:

```sql
SELECT VARIABLE_NAME, VARIABLE_VALUE
FROM information_schema.GLOBAL_STATUS
WHERE VARIABLE_NAME LIKE 'Rust_expression%'
ORDER BY VARIABLE_NAME;
```

`Rust_expression_programs` counts successful executions, `rows` counts their active input rows, `batches` counts calls through the batch route and `filter_rows` counts active rows reaching programs with filter roots. `prepares` counts programs compiled during physical-plan preparation. After warming a cached plan, repeated parameter rebinds should advance executions without preparing another program. Query the status table before and after a workload; preparation and EXPLAIN alone do not prove execution. `SHOW GLOBAL STATUS` currently follows a different status implementation and does not expose these fields.
