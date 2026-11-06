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

Execution construction initializes input validity to NULL. The backing must already contain initialized u64 values, as required by the native slice interface. The CXX allocation adapter supplies that initialization. Batch binding clears only the current rows of each node's flags; inaccessible capacity rows are reset when a later batch exposes them. Values remain unusable until the corresponding flags report evaluation.

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
- CXX checks cover real input transport, output consumption, allocation failure, release, cancellation and concurrent isolated runners sharing one immutable program.
- Production SQL checks must compare baseline and candidate, including scalar and batch execution, prepared parameters and unsupported warnings.
- `information_schema.GLOBAL_STATUS` counters distinguish prepares, successful program calls, input rows, batch calls and rows reaching actual Rust filter programs. A plan alone is insufficient evidence of execution.
- The remote sysbench comparison includes input conversion, root consumption and compilation/binding. Standard OLTP and custom expression workloads are reported separately, with identical work and errors checked.

No C++ arithmetic family can be deleted yet because unsupported types and non-operator expression consumers still use it. Family deletion requires migrating those callers and all supported signatures. This cohort removes their execution only for selected complete operator graphs.

## Running focused checks

Native tests and lint use the repository registry configuration:

```bash
source ~/.bashrc
cargo test --config bazel/rust_cargo_config.toml --offline -p expression-program --lib
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
