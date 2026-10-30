# Async vector visibility measurement

The goal is to reduce the delay between a committed write and approximate vector search visibility while keeping ASYNC indexing. The fetcher now wakes on a PALF log notification and retains its 200 ms maintenance timeout.

Five alternating baseline/fixed runs reduced the median per-run visibility P99 from **197.5 ms to 13.9 ms**. All 500 sparse writes per side were visible in primary-key and ANN queries. Each side also completed and validated 40,000 ordinary writes while an async index remained active.

| Metric | Baseline median [range] | Fixed median [range] |
| --- | --- | --- |
| Sparse ANN visibility P99, ms | 197.5 [197.1, 200.2] | 13.9 [11.0, 22.6] |
| CPU seconds during 10 idle seconds | 0.14 [0.11, 0.53] | 0.13 [0.12, 0.14] |
| Ordinary writes per second | 5,260 [4,178, 5,905] | 5,947 [4,283, 6,094] |
| CPU seconds for 8,000 ordinary writes | 3.00 [2.86, 3.29] | 3.03 [2.96, 3.12] |
| PALF callback-chain mean time, microseconds | 3.29 [3.25, 3.54] | 5.97 [5.28, 6.34] |

The visibility improvement exceeds the run-to-run variation. Idle CPU and ordinary-write CPU ranges overlap. Ordinary-write throughput is noisy, so these runs do not establish a throughput improvement or exclude small regressions. The PALF callback-chain median increases by about 2.7 microseconds. That measurement covers the whole callback chain. It comes from integer-microsecond, one-second diagnostic windows, not a dedicated measurement of the new callback alone.

## Workload and controls

The baseline is source `cdb859633f690c306f4faa24523cc7eb214322d7`. Both binaries use the repository's default native optimized build and the same instance parameters: `memory_budget=2G`, `vector_memory_limit=1G`, `cpu_count=4`, and `log_disk_size=4G`.

Both tables start with 56,010 equal, deterministic random-normal vectors of dimension 768. The heap table has an integer primary key. Its HNSW_SQ index uses L2 distance, M=16, ef_construction=200, and sync_mode=async. Both indexes were rebuilt before the scored series. Each run warms the index, measures ten idle seconds, then commits 100 individual vectors with random gaps up to 100 ms. The query uses `APPROXIMATE LIMIT 10`, an index hint, and session ef_search=256. EXPLAIN must contain VECTOR INDEX SCAN. Polling is every 2 ms, so the measured visibility includes query and polling cost.

The write-cost phase uses four autocommit clients to insert 8,000 rows into an ordinary heap table. Each payload is a serialized 768-dimensional vector. The async vector index remains active in another table, so these logs exercise the added PALF callback and fetcher notification without concurrent vector-index mutations. Row count, ID sum, and payload-byte sum are checked. A refresh barrier records catch-up time separately from insert-response time. This is a short write batch, not a sustained saturation benchmark.

The shared host exposes 16 CPUs and runs other jobs. Those jobs were not stopped. A/B order alternates, with the fixed version second in every pair, so changing background load remains a limitation. Each run has 100 sparse samples; its P99 is the second-highest sample. Per-run load and affinity are recorded in [benchmark_results.json](benchmark_results.json). The client used less than one CPU core during the timed writes. No profiler or tracer ran during scored measurements.

Separate traces show the baseline fetcher sleeping for 200 ms after iterator exhaustion. The fixed fetcher's 200 ms futex waits return early on writes. Its callback carries no cached LSN or SCN. The iterator still determines the readable boundary. The callback runs once per PALF log group and coalesces signals to at most one per armed read attempt.

## Reproduce

Install the Python packages `numpy`, `psutil`, and `pymysql` in the measurement environment. Start baseline and fixed servers with distinct directories and ports and the parameters above. Run the script once per server to create its dataset:

```bash
python3 unittest/observer/change_stream/benchmark_visibility.py \
  --port 2881 --pid "$baseline_pid" --rows 56000 --dim 768 \
  --samples 10 --output /tmp/baseline-setup.json
```

Read the generated `database` from each setup JSON. Reuse the corresponding database for five alternating runs, using the same seed on each side of a pair:

```bash
python3 unittest/observer/change_stream/benchmark_visibility.py \
  --port 2881 --pid "$baseline_pid" --database "$baseline_database" \
  --dim 768 --seed 1000 --samples 100 --gap-ms 100 --idle-seconds 10 \
  --throughput-kind plain --throughput-rows 8000 --clients 4 \
  --log-file "$baseline_log" --output /tmp/baseline-run-1.json
```

The script creates test databases and tables and retains them. Use isolated test instances. It records every failure and returns nonzero if validation fails. The committed JSON contains per-run summaries and binary hashes; full sample records remain in the script's output files.

## Verification and limits

The native server and unit target build successfully. All 12 tests in `//unittest/observer/change_stream:test_change_stream_fetcher` pass. The concurrent-notification regression executes the production wait path and callback wrapper. Reversing the key/arm order failed 20 of 20 mutation runs with approximately 200 ms waits; restoring it passed 20 of 20. This is stochastic stress coverage, not a deterministic proof of every interleaving.

Real-instance checks cover three async-index create/drop cycles, rollback exclusion, graceful primary shutdown during writes, primary restart, standby replication, standby write rejection, and standby restart. All 7,804 acknowledged writes survived the fixed primary's restart, with one additional transaction whose response was interrupted.

Two baseline failures remain separate from this fix:

- [Standby lifecycle failures, #2179](https://github.com/oceanbase/seekdb/issues/2179). A standby ANN check after primary restart times out in both versions. Promotion was not reached, so this is not a full standby lifecycle pass.
- [Concurrent vector ANN self-lookup misses, #2180](https://github.com/oceanbase/seekdb/issues/2180). An earlier baseline run inserted 4,000 vectors from four clients, but 36 ANN self-lookups still missed after refresh. That run was rejected. The write-cost results above concern ordinary SQL writes and do not establish concurrent vector-write throughput.
