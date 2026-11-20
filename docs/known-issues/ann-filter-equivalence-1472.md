# ANN filter-equivalence latency difference

Issue: #1472 — *filtered full-table and materialized-subset searches cause up to 9.21x ANN latency difference*

Status: **reproduction documented; fix not included in this change.**

## 1. Reported behaviour

Two query formulations with identical Top-K semantics produce very different client-side latency:

| Formulation | ANN p50 | Recall@10 |
|---|---:|---:|
| Full table with `WHERE category5 = 1` | 6.9187 ms | 1.0000 |
| Materialized `category5 = 1` subset, no filter | 0.7514 ms | 0.4667 |

Equivalence property (from the issue): *searching the full table with* `category5 = 1` *is equivalent to searching an identical materialized subset without a filter*.

Two observations follow, and they should be investigated separately:

1. **Latency**: the filtered formulation pays a large constant cost that the unfiltered one does not.
2. **Recall**: Recall@10 collapsing from 1.0000 to 0.4667 on the *materialized* formulation means the two paths do not only differ in cost, they also disagree on results. A pure performance fix will not repair the recall difference.

## 2. How to reproduce

The reproduction needs a table with a vector column and an HNSW index, a filter column with enough selectivity,
and a ground-truth Top-K computed by an exact search. Follow the vector index documentation for the exact
DDL keywords of your build, then:

1. Create the base table with a vector column and a low-cardinality filter column (`category5` in the report).
2. Create a vector index on the column and load a deterministic vector set (fixed seed) so latency numbers are comparable across runs.
3. Run formulation A: Top-K ANN search over the full table with `WHERE category5 = 1`.
4. Materialize the subset into a second table (same rows, same vectors) and run formulation B: Top-K ANN search with no filter.
5. Compute ground truth with an exact (brute-force) distance scan restricted to the same subset.
6. Report client end-to-end p50 over at least 50 warm iterations, and Recall@10 against the ground truth.

Keeping the vector set and the query vectors fixed is essential: both the latency ratio and the recall number move
with data distribution and with the index build parameters.

## 3. What to measure when the fix lands

| Metric | Why |
|---|---|
| p50 / p95 client latency for A and B | the reported ratio (9.208x) is a latency claim |
| Recall@10 for A and B against the same ground truth | the reported recall divergence must go to zero |
| Rows examined / plan shape (`EXPLAIN`) | shows whether the optimiser still forces filter-then-search |

A fix that only equalises latency but leaves the two formulations returning different Top-K sets does not close this issue.

## 4. Where to look (to verify)

The reported symptom is plan selection between a filtered scan path and an unfiltered ANN path, so the relevant
logic is expected in the vector-index plan/rewrite layer rather than in the index implementation itself.
These pointers are **not** confirmed against a debug build and should be treated as a starting point:

- the rewrite/plan rules that decide whether a filter is pushed into the ANN scan or applied after it;
- the cost model entry used for filtered ANN scans;
- the recall contract of the ANN scan when a filter is present.

## 5. Environment of the original report

- seekdb 1.3.0.0, revision `100000092026051510-13e493b33b6c3ec22133e5fdf8694e8a4e9bff38`
- Ubuntu 24.04, x86_64, Linux 6.17.0-29-generic
- Docker container: 8 CPU cores, 32 GiB cgroup memory limit
