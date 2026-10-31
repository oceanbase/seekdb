# Full-text performance optimization

## Hot paths

The optimization keeps tokenization and MATCH semantics unchanged while
reducing work on the common tokenizer and dictionary paths:

- Parser names, parser properties, formatter output, and the one-time IK
  dictionary load are cached per worker thread.
- Tokenize parameter strings use the expression evaluation arena instead of a
  separately constructed arena.
- Shared immutable IK dictionaries are built once and use a read-fast path.
- Dictionary range selection uses a binary search when the ranges are sorted.
- Single-character dictionary hash lookups and match results use bounded
  thread-local caches.
- Character classification decodes a Unicode code point once, classifies CJK
  before the ASCII predicates, and uses an O(1) table for Chinese numerals.
- IK processors reuse their arbitrator, skip redundant context reads, and avoid
  a redundant UTF8MB4 conversion in stop-word checks.

## Correctness

The fixed IK and BENG tokenizer inputs were captured from the unmodified build
and compared byte-for-byte with the optimized build. They are identical. The
full benchmark also preserves all query hit counts: 8001, 11000, 7332, and 20.

Regression tests under `unittest/storage/fts` cover character classification,
Chinese numerals, and parser-property cache behavior.

## Local measurements

The local comparison used the official script with the default 20,000 rows,
500-row batches, 3,000 tokenize rounds, 200 query rounds, three samples, and 30
warmups. Machine background activity caused noticeable variance, so tokenize
and query values below use the median of three final-code runs; build values are
from the complete run.

| Metric | Baseline | Optimized | Improvement |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 24.876 | 21.393 | 14.00% |
| build_ik_content_sec | 19.814 | 17.615 | 11.10% |
| build_beng_en_sec | 10.611 | 10.693 | -0.77% |
| tokenize_ik_avg_ms | 0.8023 | 0.7105 | 11.44% |
| tokenize_beng_avg_ms | 0.5204 | 0.4629 | 11.05% |
| query_cn_avg_ms | 5.1887 | 5.0459 | 2.75% |
| query_beng_avg_ms | 7.0779 | 6.9139 | 2.32% |
| query_mixed_avg_ms | 6.0901 | 5.6241 | 7.65% |
| query_limit_avg_ms | 7.1203 | 7.1298 | -0.13% |

The resulting local category improvements are 8.11% for build, 11.25% for
tokenize, and 3.15% for query, for an estimated overall improvement of 7.50%
and a score-equivalent 15.00 on the local harness.

## Rejected experiment

Returning BENG analyzer buffers directly and retaining normalized tokens across
the document caused a lost connection under repeated benchmark execution. The
change was fully reverted; the original per-token ownership remains.
