# Allocator removal product A/B

`run_product_memory_ab.sh` is the version-controlled execution identity for the
allocator-removal product performance gate. It compares two real `seekdb`
binaries on the same host and CPU partition. It does not compile either binary.

The full run uses the approved workload identity:

- default and `memory_budget=2G` groups;
- five rounds per group and workload;
- 300 seconds of warmup followed by 600 seconds of measurement;
- an 80/20 point-read/update SQL mix;
- a deterministic 384 MiB KV working set with a 256 MiB KV-cache limit;
- 100,000 deterministic 128-dimensional vectors with an HNSW index;
- eight client threads by default.

Example:

```bash
BASE_SOURCE=<40-digit-base-sha> \
CANDIDATE_SOURCE=<40-digit-sha-plus-diff-sha256> \
SERVER_CPUS=0-7 CLIENT_CPUS=8-15 \
tools/jemalloc/allocator_removal/run_product_memory_ab.sh \
  /path/to/base/seekdb /path/to/candidate/seekdb \
  build_release/validation/product-memory-ab
```

Use `--smoke` only to validate the runner and SQL syntax. A smoke result is not
performance evidence and cannot pass the final performance gate. The analyzer
still runs and its untrusted short-run status is stored in
`smoke-analysis-exit-code.txt`, but only runner or workload failures make the
smoke command fail.

The output directory records binary hashes, source identities, host and CPU
identity, deterministic row counts and ID checksums, one-second RSS/PSS and
component samples, jemalloc shutdown statistics when supported, and one JSON
file per measured round. `summary.json` reports median throughput and P99,
coefficient of variation, runtime memory metrics, component maxima, and the
base/candidate deltas.

The analyzer returns 2 when throughput or P99 CV exceeds 3%; those groups must
be rerun. It returns 1 when throughput falls or P99 rises by more than 5%, or
when an A/B half is missing. Only a complete, non-noisy run with exit status 0
passes the performance gate.

The component virtual table is intentionally sampled as independent atomic
fields. Concurrent samples are useful for trends only; they are not a joint
snapshot. Strict quota invariants are covered separately at quiescent points by
the component and quota tests.
