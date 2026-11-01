# Fixed-size owning document-ID copies

This candidate retains revision41's score-free posting-list union and revision42's resource-bounded parallel auxiliary-index construction. See `REVISION42.md` for their evidence and limitations. The rejected revision43 dictionary hint is not included.

Query profiling had identified document-ID materialization and memory copying among the posting-list costs. Native document IDs are 16 bytes, but `ObDocIdExt::from_datum` previously always used a generic variable-size copy. Specialize only non-null 16-byte datums to a compile-time fixed-size owning copy, retaining all datum metadata and resetting the pointer to the destination's own storage. Null and other-length IDs use the original deep-copy routine. This still owns the bytes: borrowing expression buffers would be invalid when another query dimension refills its batch.

The full observer build and all 16 fresh differential regression suites passed, including scored/Boolean retrieval, filters, offsets, function lookup, prepared execution, Unicode, parser properties, LOBs, composite keys, DML/rebuilds, and multi-batch documents. Live DDL batch proof passed. All 45 changed C++ files matched the VM source manifest. No benchmark SQL, input, iterations, timing code, scoring rules or baseline files were changed.

## Complete local ABBA comparison against revision42

Fresh instances used revision42/revision44/revision44/revision42, two CPUs, 3GiB memory budget, 2GiB datafile/redo budgets, and 20 seconds of readiness settling outside timing. Original parameters: rows=20000, batch=500, rounds=3000, query_rounds=200, samples=3, warmup=30, SKIP_LOAD=0. No compiler, profiler or archive compression ran during timing.

| Metric | Revision42 | Revision44 | Time reduction |
| --- | ---: | ---: | ---: |
| IK title/content build, seconds | 6.5155 | 6.8935 | -5.80% |
| IK content build, seconds | 5.2515 | 5.4470 | -3.72% |
| BENG build, seconds | 2.7205 | 2.7660 | -1.67% |
| IK tokenize, ms | 0.54655 | 0.46795 | 14.38% |
| BENG tokenize, ms | 0.41005 | 0.37685 | 8.10% |
| Chinese query, ms | 1.60155 | 1.59000 | 0.72% |
| English query, ms | 2.02785 | 1.82110 | 10.20% |
| Mixed query, ms | 1.71265 | 1.58040 | 7.72% |
| LIMIT query, ms | 0.94435 | 0.67180 | 28.86% |

Category reductions: build **-3.7323%**, tokenize **11.2388%**, query **11.8749%**; overall additional local reduction **6.4605%**. SELECT 1 improved 0.3097% and is not scored. Counts stayed 8001/11000/7332/20. The copy specialization does not execute during TOKENIZE, so the observed tokenization improvement cannot be attributed to it; it reflects local-run variation. Construction regressions are retained. The full comparison, including unmodified categories, is reported rather than selecting only favorable query numbers.

No new official score is available. Local timing differences cannot be added to official points or used to assert official full marks. Raw reports and source manifests are retained, including failed experiments.
