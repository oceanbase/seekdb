# Expression execution migration boundaries

The migration goal is Rust ownership of prepared expression programs and their execution state, followed by removal of C++ evaluators whose callers have migrated. Numeric filter and projection graphs validate that boundary. Adding arithmetic opcodes alone does not close the remaining callers.

This inventory describes the source at `cf5e8fd7cf8b353057d20372984c7b9f5f8a8c1c`. It is a bounded source audit, not a workload-frequency ranking. The [current program contract](expression-program.md) describes the implemented cohort and its validation.

## Execution owners

An execution owner determines when inputs become available, which rows and roots are demanded, and when cached values expire. Each migrated owner needs a whole-program admission decision before execution. Unsupported graphs retain their C++ path. A failure after Rust execution starts must propagate without C++ replay.

| Owner | Production entry | Current boundary and remaining responsibility |
| --- | --- | --- |
| Generic operator | `ObOperator::get_next_row`, `get_next_batch` | Selected TableScan, SubPlanScan, Sort, and Material graphs use Rust. The adapter requires available inputs and supports complete nullable signed-integer graphs. Other producers retain C++ demand. |
| Scan before final LIMIT | `ObTableScanOp::get_next_row_with_das`, `get_next_batch_with_das` | Residual filtering can occur before the generic stage. Filtering, OFFSET consumption, and projection need one generation with deferred projection and retained caches. |
| Storage predicate group | `ObBlackFilterExecutor::filter`, `filter_batch` | Storage supplies decoded inputs and parent masks. A complete ordered predicate group could use Rust selection. Preparation, parent AND/OR demand, and decoded-input lifetimes belong in the integration. Storage white filters remain a separate optimized responsibility. |
| Temporary row program | `ObTempExpr::eval` | Query ranges, partition expressions, and generated-column projection bind rows or parameters. C++ currently allocates temporary frames, recursively clears flags, replaces execution-context pointers, and demands a root. |
| Constant and calculable roots | `ObPreCalcExprFrameInfo::eval`, `ObPlanCacheObject::pre_calculation` | Multi-root execution remains C++. Expected-error constraints intentionally continue after some errors and need a distinct outcome contract. |
| Join | Join key and residual demand | Pair generation, match state, and null padding determine expression inputs. Generic operator output coverage does not migrate these internal demands. |
| Aggregate | `ObAggrInfo::eval_aggr`, `eval_param_batch` | Argument programs can migrate separately from aggregation state. Different aggregates can demand different roots and retain their values. |
| Window | `ObWindowFunctionOp` argument and frame-bound evaluation | Restored rows and window frames determine cache generations. A single monotonically advancing operator batch is insufficient. |
| DML | `ObDMLService` row construction and constraints | Row dependencies, warnings, and stable values before writes remain. CHECK accepts NULL and converts evaluation errors to constraint failure. View checks reject NULL and propagate errors. |
| Storage materialization | Generated-column and retrieval-expression consumers | Storage NOP values, generated-column dependencies, relevance inputs, and datum conversion remain outside generic operator execution. |

Ordinary `WHERE` predicates can become storage pushdown filters in `ObTscCgService::generate_tsc_filter`. They do not necessarily enter TableScan's residual filter list. Widening TableScan admission therefore requires actual execution evidence before making a coverage claim.

The temporary-expression template has a different lifetime from an operator plan. `gen_expr_with_row_desc` placement-allocates `ObTempExpr` in caller arenas. The audited call paths do not establish explicit template destruction. Its deep copy and UNIS serialization also copy expression metadata. Adding an owning Rust Box to that template requires a proven teardown protocol or a representation that needs no individual destruction. The execution-context cache's explicit teardown alone does not solve immutable-template ownership.

## Signatures and value ownership

The current physical signature is `Nullable<I64>`, including boolean results represented as 0 or 1. The cohort includes literals, parameters, columns, checked arithmetic, comparisons, NULL tests, truth conversion, AND, OR, NOT, searched CASE, and signed widening or identity casts.

| Extension | Contract required beyond an opcode |
| --- | --- |
| Unsigned and mixed integers | Explicit signedness, comparison rules, overflow policy, and diagnostics. A u64 storage word currently contains an I64 bit pattern. |
| Floating point | NaN, infinity, range errors, conversion, and result representation. |
| Decimal and number | Precision, scale, multiword or variable storage, allocation, and diagnostic policy. |
| Strings and LOBs | Byte ownership through result consumption, collation, and LOB representation. String pass-through outputs currently make the complete operator graph ineligible. |
| Warning-producing casts | Warning order, truncation and NULL policy, session settings, and destination storage. The current native error interface has no warning stream. |
| Volatile or external operations | Exactly-once lazy demand, side-effect order, cancellation, and an explicit state or host-capability owner. |

Control-flow families are not closed merely because their results are boolean. AND and OR can demand unsupported conversions or functions. A NULL test can demand any child type. CASE must preserve the selected result's lifetime for every supported result type.

## C++ deletion conditions

Three milestones must remain distinct: a complete graph executes in Rust, its caller's duplicate execution coordination is removed, and a C++ evaluator family is retired. The first cohort reaches the first milestone for its admitted operator graphs. No whole evaluator family is retired.

A family can be removed after these dependencies are closed:

1. Every production owner that can demand the family uses the native program for the declared signatures. Any retained signatures and callers are named explicitly.
2. Code generation stops binding the removed scalar and batch functions. `ObStaticEngineExprCG::cg_expr_by_operator` currently requires scalar evaluators and installs a default batch fallback where needed.
3. Native admission no longer identifies operations by removed function pointers. The current NULL-test adapter checks the legacy evaluator pointer.
4. Copy, plan-cache, and PX paths reconstruct or borrow immutable programs safely and release owners before their allocators. Current function serialization uses pointer values, rather than a historical numbered evaluator registry.
5. Independent consumers of arithmetic helpers and datum comparators remain valid. Aggregate transitions, array utilities, string-length checks, and timestamp arithmetic use helpers from expression classes. Retiring SQL execution does not automatically retire those helpers or C++ type deduction.
6. Native, CXX, and production SQL checks cover demand, NULL, diagnostics, rebinding, lifetime, cancellation, and allocation failures. Same-host throughput comparisons include actual input conversion and result consumption.

## Selecting the next unit

The next unit must remove a complete execution responsibility and prove a reusable native capability. Two strong candidates expose different missing contracts:

- Staged filtering and projection preserve one generation while a relational caller applies LIMIT or OFFSET. This can reuse the existing physical-plan owner, but requires proof that a real residual-filter scan reaches the intended branch.
- An independent temporary row program separates compilation and execution from `ObOpSpec` and `ObOperator`. This can replace temporary frame and context management, but requires immutable-template ownership and real eligible consumers.

A storage black-filter group is another complete owner and may cover predicates that scan residual filtering misses. It requires its own preparation and storage-mask contract. Source occurrence counts do not determine which candidate is hotter.

Selection requires actual eligible SQL, a complete ownership design, and a named portion of C++ coordination to remove. Existing performance evidence applies only to the measured implementation and workload. Each new production entry needs fresh hit evidence and identical-work comparisons against the accepted 1% throughput-regression limit. Standard OLTP without native hits remains regression coverage.
