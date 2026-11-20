# Resolver IN-list duplicate regression (issue #1512)

This patch deduplicates immutable integer membership values before the existing
large-IN-to-VALUES decision. It preserves first-occurrence order and one literal
NULL. It applies only to root WHERE IN/NOT IN predicates with a scalar column
left operand. The column is deliberately not required to be bound at this parser
stage: process_column_ref_node has only created a qualified-name placeholder.

Supported RHS forms are homogeneous signed integers, homogeneous unsigned
integers, or ordinary automatically parameterized integers of identical runtime
meta, parameter meta and accuracy. A duplicate direct literal must also have
identical original text length, conservatively preserving precision distinctions
such as 1 versus 0001. This is not a general SQL constant-folding operation.
Strings, decimals, floats, expressions, functions, rows, mixed bind/literal values,
mixed signed/unsigned values, currently NULL binds, unchecked types, batch binds,
PREPARE statements, PL and dynamic SQL retain the original path. Other unaffected
forms retain their existing runtime behavior.

Each removed automatic parameter creates a production ObPCParamEqualInfo guard.
That guard must match before a cached plan can be reused: equal values changing
to unequal or NULL values cannot reuse the plan that removed them. No parameter
indexes are renumbered. If the result still meets the session or OPT_PARAM
inlist_rewrite_threshold, the helper publishes neither AST changes nor guards;
ACCESS_PARAM can continue to read the original contiguous parameter range.

Work is bounded to 16,384 input children, 128 retained values and 1,048,576
comparisons. Larger or mostly distinct lists fall back to the original path.
No result or score is hardcoded.

## Production-linked unit test

The Bazel target links the same liboceanbase production runtime as observer:

    python unittest/sql/resolver/run_inlist_dedup_test.py \
      --workspace-root /home/dataset-local/chy/cwk \
      --cargo-home /home/dataset-local/chy/cwk/.cargo \
      --rustup-home /home/dataset-local/chy/cwk/.rustup \
      --jobs 2 --memory-mb 10240 --dry-run

Remove --dry-run only under the designated build owner's resource budget.
Target: //unittest/sql/resolver:test_inlist_dedup. The runner creates only local
outputs, limits Bazel CPU/memory/JVM/download concurrency, and records command,
HEAD, source hashes, elapsed time, actual exit code and complete Bazel log.
Existing initialized personal Rust homes should be supplied explicitly.

The unit tests cover the published 3,072/3-distinct shape; a real ObParser fast
parse + full parse + ObSqlParameterization::transform_syntax_tree ordinary-SQL
pipeline (including actual need_to_check_type/meta/accuracy assertions); precise
cache guards; threshold and contiguous-access fallback; NULL IN/NOT IN; integer
boundaries and precision; unsupported forms/scopes; bounded work. A test using
the existing TestPlanSet friend name calls production match_constraint to check
equal -> unequal -> equal -> NULL invalidation.
That focused unit manually installs the generated guards: it does not claim to
exercise set_params_info -> init_new_set -> match_params_info. init_new_set
requires a live plan-cache/PCVSet memory-context fixture in addition to SQL
execution/session state. Actual SQL below is the integration evidence for that
complete production path; a successful helper or guard unit alone is insufficient.

## SQL integration and timing

The runner requires personal PyMySQL and NumPy. It never installs dependencies,
launches a database, changes GLOBAL settings, reuses a database or deletes data.
Use only the resource owner's isolated, explicitly reserved localhost instance.
It creates a new UUID database, keeps all data for inspection and writes evidence
under .inlist-dedup-validation/sql. Passwords come from SEEKDB_TEST_PASSWORD (or
the named environment variable); credentials are not recorded in evidence.

    python unittest/sql/resolver/run_inlist_dedup_sql.py \
      --owned-isolated-instance --port <reserved-port> \
      --workspace-root /home/dataset-local/chy/cwk \
      --mode fixed --source-sha <full-40-character-build-commit> \
      --server-binary <personal-observer-path> --profile diagnostic

For a pure scalar smoke test, add --scalar-only. For the larger published-shape
workload, use --profile issue1512-shaped. It fixes 200,000 rows, 128 dimensions,
8 query vectors, 4 warmup pairs, 12 balanced AB/BA measured pairs, 4 exact control
repetitions, EF=16 and a 3,072-member list shuffled by random.Random(1024).
Vectors use explicitly coded tenant correlation 0.85 and NumPy seeds 20261002 /
20261003. The issue does not publish complete RNG call order or tenant-vector
construction code; this profile must not be reported as byte-identical official
reproduction or expected to produce exactly 119,582 eligible rows. Dataset,
membership, sources and binary hashes are recorded.

Run the identical runner/profile against an independently built clean db4bb5
server using --mode baseline, then against the patch using --mode fixed. Keep
conditions and ownership/resource limits equal and run serially. The baseline
mode records old plans without demanding the new fixed plan. The fixed mode
requires compact and expanded ANN plans to retain VECTOR INDEX and avoid the
VALUES/subplan expansion; both modes check scalar truth, complete eligible sets,
exact Top-K equality, result eligibility, uniqueness and recall against exact
search. ANN-vs-ANN IDs are deliberately not required to match. Recall is always
recorded; --minimum-recall can impose an explicit user-selected floor.

By default every failed assertion stops either mode immediately. To collect
later timing and recall observations after a known baseline defect, explicitly
add --continue-baseline-diagnostics together with --mode baseline. This option
is rejected in fixed mode. Failed checks remain failed, the first failure is
retained in evidence.first_failure, the final status remains failed and the
process exits nonzero. Unexpected execution errors still stop the run. These
diagnostic observations do not constitute a successful baseline validation.

Cache cases include automatic literal SQL with small and 3,072-element lists:
the last position removed from an initial all-equal integer AST changes to an
unequal integer, equal/unequal/fractional decimal, equal/unequal/fractional string,
uint64 maximum, NULL, then back to the original equal integers. Both signed and
unsigned table columns have explicit expected result IDs. The unsigned column
has a uint64-maximum row, making an ignored removed uint64 position observable.
NOT IN equal -> unequal -> equal -> NULL cases require the NULL result to become
empty. These are result assertions, not merely comparisons with rewritten SQL.
Further cases use true server
PREPARE binds (conservative fallback), and a deterministic ANN geometry where
category=2 is nearer than every category=1 row. A missing equality guard would
hide that second category. Threshold=3 cases check below-threshold compaction,
at-threshold original VALUES fallback and correct original contiguous binds.

For each positive-IN transition, the runner reads the supported
oceanbase.`V$OB_PLAN_CACHE_PLAN_STAT` view before and after execution. PLAN_ID,
SQL_ID, HIT_COUNT, EXECUTIONS, STATEMENT, QUERY_SQL and PARAM_INFOS are verified
against src/share/inner_table/ob_inner_table_schema_def.py:7183-7199, whose hash is
included in evidence. It records exact counter deltas and verifies reuse of the
initial equal plan when returning to equal inputs; on a fixed binary that plan
must not receive hits for changed values or types. Any explicitly observed hit
of that initial plan by a changed value fails, even if other return-to-equal
observations are incomplete. A unique table name and SQL
prefix avoid other workloads and the observation query itself. Long cache texts
are stored once by SHA-256. No unavailable/unauthorized view or missing counter
delta is reported as a cache pass: cache status remains unverified, and the run
status becomes passed_with_unverified_cache_observation if functional checks pass.
Inspect plan_cache.status separately; exit zero alone does not prove cache reuse.

The evidence contains the complete sorted eligible ID set and its hash, every
exact ordered Top-K result, each query vector and compact/expanded SQL with
SHA-256, and measured/warmup ANN ordered IDs with recall. This makes both
baseline and fixed datasets, query identities and observed results comparable.

Both binaries also record end-to-end EXPLAIN planning controls under
USE_PLAN_CACHE(NONE) for a small list, duplicate3072 and mostly-distinct3072.
These are planning latency controls, not isolated resolver CPU time. They expose
compile-path regressions without calling warm cache time a compile measurement.
Do not use NO_REWRITE on the fixed binary as an independent baseline: this
resolver operation precedes that rewrite mechanism.

## Validation status

Actual controlled validation completed on November 1, 2026, against official
master db4bb5dc6f015bc3c54a06aea0c6f74fc50a9801:

- Independent baseline clean build: exit 0, 1,656.658685 seconds; binary SHA-256
  3d9269facff52034b74c27338ac97e3fca3b23bf8470ab5b7a9b7582ddcef9c6.
- Complete candidate production and unit targets: exit 0, 666.535246 seconds,
  using verified content-addressed cache reuse. This candidate build is not
  clean. Binary SHA-256
  069fcd1402f1d083962eea8c11aadd78ccdb95545a5f0eb254cea4a7a058f772.
- Production-linked unit execution: 15 tests, zero failures, exit 0,
  0.604158 seconds. Earlier fixture-bootstrap and build failures are retained.
- Fresh independent baseline/fixed instances completed issue1512-shaped
  validation in A/B/B/A order. Both fixed rounds passed 440/440 checks with
  observed plan-cache reuse and exit 0. Both baseline rounds completed
  diagnostics with 66 failed checks among 433 and exit 1; the first failure
  was ann_cache_guard_0. The aggregate controller preserves those failures
  and deliberately returns nonzero when baseline failures were collected.
- All four rounds used identical generated data (200,000 rows, 128 dimensions,
  119,780 eligible rows), complete eligible membership and exact ordered
  Top-K results. Each server used 4 configured CPUs, 4 GiB database memory,
  a 512 MiB vector budget and nice 10. Process identities, binary/source
  hashes, cache counters and vector-memory snapshots were checked. All eleven
  patch-source hashes stayed unchanged throughout; only README changed later.

Uncached end-to-end EXPLAIN planning medians, in milliseconds:

| Control | Baseline A1 / A2 | Fixed B1 / B2 | Ratio of mean round medians |
|---|---|---|---|
| duplicate3072 | 3.7057545 / 3.6825715 | 4.6426785 / 4.5571540 | 1.245185 |
| mostly-distinct3072 | 3.7764835 / 3.7442415 | 3.7799300 / 3.7288370 | 0.998410 |
| small list | 0.7335010 / 0.7315800 | 0.7830695 / 0.7837195 | 1.069421 |

The duplicate control still has about 24.52% (+0.906 ms) planning overhead.
These are shared-server measurements, not isolated resolver CPU timings.
The initial revision's quadratic equality-constraint pass caused a 22.366002x
planning regression. The current follow-up replaces it with arena-backed
hashed membership while keeping all guards. The hash normalizes symmetric
parameter indexes and includes comparison mode; first-occurrence order and
original orientation remain. Fallible work finishes before guard compaction.
Both negative and repaired measurements are retained.

Expanded ANN medians were 49.6707035 / 49.6551375 ms fixed and
2.2391765 / 2.4137030 ms baseline. The baseline drops eligible rows, so these
are not equal-result performance comparisons. Approximate recall remains low
on this dataset, including zero-recall queries. This patch does not claim a
general ANN recall fix, resolution of every aspect of issue #1512, an
end-to-end speedup over a correct baseline, or an official score.

The final four-round local record SHA-256 is
e10e185a3a0e4deffb1ea3305734b40fa7984ab4508dfb976c317bd51e44c064.
A prior follow-up stopped in its fourth round when retained schemas exhausted
the 512 MiB vector budget. That incomplete run and error 4013 remain separate;
no rounds were spliced into the fresh completed run. No existing official
test or benchmark asset was modified.
