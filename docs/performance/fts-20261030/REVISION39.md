# Document-batched streaming FTS index construction

This candidate has no official score. Control: 5c73c72d02dc2d098dbbe256a50eb7c8ca859193, previously reported at 42.795535/50. A subsequent user report after revision33 delivery gave 28.656890/50, dominated by IK content-index time 4.727 -> 7.430 seconds. That report does not identify its submitted SHA; see platform-reported-28points-20261101.json. The local checked-in scoring baseline does not reproduce platform scores, so no official full-mark prediction is made.

## Implementation

- Resolve four generated FTS output expressions once per operator, preserving their immutable spec lifetime across rescans.
- Enable document-scoped term batches for plain and row/block/DDL-sample scans with four batch-compatible outputs and no filters. Actual sampling retains its existing raw scan path. A batch never advances to another document, preserving the source doc ID and term-buffer lifetime.
- Initialize result-slot pointers with locate_datums_for_update before numeric storage-datum copies. The rejected prototype used the read accessor and crashed; it is not the implementation in this candidate.
- Keep one document input copy until its terms have been consumed, including temporary out-of-row LOB input.
- Allocate FtsDocumentRowState only for FTS DDL. Retain the current document term map and materialize one reusable ObDatumRow per call. The consumer copies the row before requesting another. This removes per-term row-object/array allocation; ordinary scans retain only a null state pointer. Bucket sizing, iteration order, token frequencies and document lengths follow the original path. The generic DML generate_fulltext_word_rows implementation is unchanged.

## Validation

Complete observer build passed. All 16 regression groups passed on a fresh, exclusively owned instance against results recorded by immutable revision28. Coverage includes all ASCII bytes and Unicode boundary/random cases, null/empty and long LOB documents, composite keys, both auxiliary-index layouts, DML/update/delete/rebuild, projected relevance and a document with 2048 unique terms crossing batch boundaries. Live INFO trace confirms batch_size=16, output_count=4, batch_mask=15 and multiple terms in one actual batch; see revision39-fresh-checks/document-batch-proof.txt. Independent exhaustive UTF-16 coverage is not claimed.

All 43 changed C++ sources/headers matched the VM build by normalized SHA256. The VM git HEAD remains the base with dirty source, so the manifest, rather than that base HEAD, binds these measurements to the candidate. Original benchmark SQL, data generation, rounds, score rules and baseline files are unchanged. All hit counts remained 8001/11000/7332/20.

## Complete local comparison

Four fresh temporary instances ran control/candidate/candidate/control, each with CPU=2, memory=3G, datafile=2G, log_disk=2G and the same 20-second startup readiness wait outside measurement. Official parameters remained rows=20000, batch=500, rounds=3000, query_rounds=200, samples=3, warmup=30, SKIP_LOAD=0. Compilation, GDB and profiling did not overlap timing. All temporary instances were stopped and removed afterward.

| Metric | Control | Candidate | Elapsed-time reduction |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 13.914000 | 12.791500 | 8.07% |
| build_ik_content_sec | 10.253500 | 9.471500 | 7.63% |
| build_beng_en_sec | 5.488500 | 4.793000 | 12.67% |
| tokenize_ik_avg_ms | 0.482000 | 0.475750 | 1.30% |
| tokenize_beng_avg_ms | 0.410700 | 0.385100 | 6.23% |
| query_cn_avg_ms | 1.896850 | 2.001000 | -5.49% |
| query_beng_avg_ms | 2.585200 | 2.554500 | 1.19% |
| query_mixed_avg_ms | 2.134900 | 2.050950 | 3.93% |
| query_limit_avg_ms | 0.759200 | 0.692300 | 8.81% |
| select1_avg_ms | 0.328550 | 0.349000 | -6.22% |

Category means: build 9.4553%, tokenization 3.7650%, query 2.1103%. Overall additional local elapsed-time reduction: **5.1102%**. Chinese query elapsed time regressed 5.49%; SELECT 1 regressed 6.22%. These regressions are retained rather than omitted. Local improvements cannot be added directly to official points or treated as proof of full marks.

## Experiments not published as submission candidates

Revision34 passed correctness but had only 2.8641% overall local improvement, with IK-all construction and some queries slower. Revision38 passed all 16 fresh regressions and live batching, but its full local benchmark regressed 1.6022% overall despite 2.3173% build improvement. Their positive and negative raw results are retained. Earlier disabled-path prototypes were not benchmarked as successful optimizations. The revision37 crash was reproduced under GDB and fixed before revision38. A reused fixture also stalled at a transaction barrier; subsequent correctness and measurements used fresh isolated instances. Historical revision33 results and limitations remain in REVISION33.md.
