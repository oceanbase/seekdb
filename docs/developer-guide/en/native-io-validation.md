# Native I/O migration validation

On November 13, 2026, the native Rust worker-read path used 6.3% less process CPU per GiB than the frozen original C++ implementation. Throughput ranges overlapped. This result meets the migration's approximately 20% CPU-overhead allowance for this workload; it is not a general SQL performance claim.

## Measured result

Each implementation ran five alternating trials. Each trial read 16 GiB through the public device and manager APIs, using a freshly initialized 1 GiB image, `O_DIRECT`, random 16 KiB reads, application depth 32, four workers, and device depth 512. Application-buffer copying and every read comparison were inside the measured interval. Each trial completed 1,048,576 requests with no submission, completion, short-I/O, or data errors and passed final whole-file verification.

| Metric | Original C++ median [range] | Native Rust median [range] |
| --- | --- | --- |
| Process CPU seconds/GiB | 1.739 [1.705, 1.752] | 1.629 [1.586, 1.670] |
| Throughput, MiB/s | 335.6 [322.8, 343.7] | 339.0 [325.0, 356.3] |
| Observed p99, ms | 4.511 [4.423, 4.553] | 4.558 [4.415, 4.793] |

CPU time sums user and system CPU across the process's threads. Latency measures application observation, not kernel completion. Peak RSS includes a fixed 1 GiB reference image and cannot establish the I/O component's resident memory cost.

Separate profiling runs found a device-wait-sensitive workload. During their I/O intervals, NVMe utilization medians were 95–97%, read await medians were 0.12–0.15 ms, and process CPU usage was about one core on a host with 16 available CPUs. Copying and comparison accounted for approximately 41% of sampled C++ user cycles and 44% of sampled Rust user cycles. Rust request submission and publication were additional visible costs. These observations do not establish a language-wide overhead.

The profiler sampled user cycles and produced DWARF decoding warnings. Leaf symbols resolved, but stack details should be treated as approximate. Its timing is excluded from the measured comparison. The perf clock used the host's raw clock; interval selection applied the measured clock offset and trimmed one second from each boundary. Other users' processes were left running, with host snapshots recorded for each trial.

## Correctness and lifecycle evidence

- Both final production targets, `//src/observer:_liboceanbase_link` and `//src/observer:seekdb`, built successfully with the repository's optimized Rust configuration.
- All 68 focused Rust Service and Request tests passed with CXX enabled. Deterministic tests cover late completion after executor shutdown and the drain-to-close interleaving. Cancellation retains payload bytes until a retained completion is discarded, and retained results continue to consume record capacity. The existing engine test separately controls physical retirement and file-claim release.
- The native and worker read/write paths each completed 128 public-API operations with byte verification and no errors. Separate syscall traces observed 128 `io_submit` calls for native reads and 128 for native writes, confirming that those cases used native AIO.
- An isolated database stored 32,768 rows containing 64 MiB of payload. A completed 32,768-row MINI_MERGE established SST persistence. A restart read reproduced the full row count, key sum, length sum, and payload checksum; the process's read-byte counter increased during that query. An update followed by another restart reproduced the updated checksum. Normal shutdowns returned zero using `SIGUSR1`.
- The initial validation script used `SIGTERM`, which this repository maps to `SIGKILL`. Recovery after that forced exit also reproduced the original checksum. Subsequent checks used the correct graceful signal. A fixed five-second wait was replaced by observed MINI_MERGE completion before claiming an SST cold read.
- No C++ unit-test fixture was added or run. The existing C++ public-path performance driver remains the cross-language benchmark.

Independent ownership reviews found no remaining proven Major or Blocker issue in the reviewed migration. The discovered shutdown enqueue race was fixed before the final production build. A worker-delay-based Rust assertion was replaced with deterministic retained-completion assertions, avoiding an unproven assumption about whether a worker had started.

## Provenance and reproduction

The production build identifies revision `a8290225565b8c21001d8ad3d35b70b11beaa818`. Subsequent changes only added documentation and revised `cfg(test)` Rust coverage. The original C++ tree is `6ff9b040095ec810f4b14bf7edc022483d85e39b`.

Both drivers used source SHA-256 `d4a1023fc1b640a40726c74da68db6fc4d8f45e85a167efe10482daee0433ea3`. Frozen library hashes, per-run metrics, byte/error counts, configuration, and SQL fingerprints are recorded in [the measurement receipt](../../../unittest/share/io/results/native-request-20261113.json). Use the [benchmark commands](../../../unittest/share/io/README.md) to reproduce the comparison with the same built revisions.

Local raw evidence is under `.audit/native-io-20261113/`, including `product-build-final.log`, `final-service-tests.log`, `four-path-validation/`, `native-route-trace/`, `sql-final/`, `profile-worker-read/`, and `measure-worker-read/`. The isolated instance at `/data/1/nijia.nj/test/native-io-20261113-final` is stopped and its data is retained.
