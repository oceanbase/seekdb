# Tokio I/O performance measurement — 2026-11-14

The warm OLTP result is effectively unchanged at the resolution of this run. This
is not an equivalence proof or a cold-storage throughput measurement. Five trials
per binary were interleaved on a shared host; no trial was excluded.

| Binary | TPS median [min, max] | p99 ms median [min, max] | DB CPU ms/transaction median [min, max] | Sampled peak RSS median, MiB |
| --- | --- | --- | --- | --- |
| Historical C++ | 1136.830 [1116.330, 1141.960] | 13.700 [13.220, 14.210] | 4.363 [4.345, 4.375] | 753.87 |
| Rust before Tokio | 1131.300 [1075.130, 1152.110] | 13.950 [13.460, 14.210] | 4.372 [4.357, 4.398] | 763.98 |
| Rust with Tokio | 1136.160 [1070.350, 1153.060] | 13.950 [13.220, 14.730] | 4.381 [4.360, 4.414] | 761.58 |

Latest versus historical C++: TPS -0.06%, p99
+1.82%, CPU/transaction +0.39%.
Latest versus pre-Tokio Rust: TPS +0.43%, p99
+0.00%, CPU/transaction +0.19%.
Differences this small do not establish a causal speedup or regression. TPS ranges
overlap, and both Rust versions had a lower-throughput trial.

Sysbench 1.0.20 ran the existing oltp_read_write.lua workload with eight clients,
four tables of 100,000 rows, text protocol, a separate 10-second warmup, and
30 seconds of measurement. The seed contains 71.2 MB of actual string payload.
The historical seed directory no longer existed, so a fresh seed was prepared
using the C++ binary and restored before every trial. Database settings were
memory_budget=2G, datafile_size=1G, datafile_maxsize=2G, log_disk_size=2G, with no
durability override. The process affinity exposes 16 CPUs. Other existing
processes, including the user's port-2991 instance, were left running.

The 15 timed runs completed 507,797 transactions and
10,155,940 queries with zero errors and reconnects. Each timed
transaction counted exactly 14 reads, four writes, and BEGIN/COMMIT. All restored
seeds matched row aggregates and sampled hashes; post-run counts, ID sums, and
payload lengths remained correct, with sampled row contents changing on every
trial. All measured servers exited normally. CPU counters cover the whole client
invocation, including setup/drain; TPS and latency use sysbench's timing interval.
RSS is sampled, not a continuous exact maximum.

A separate 15-second profiled run is excluded from the performance table. It
shows 91.52% of sampled user cycles in ReqWorker, 0.33% in file-io-worker and 0.17%
in io-progress. The generator used less than one CPU in pidstat samples. Physical
reads totaled 24,576 bytes in that run. The workload
primarily exercises warm SQL/transaction processing rather than disk bandwidth.
The latest executable is stripped, so the profile does not resolve most SQL
functions; kernel and off-CPU costs are not represented by cycles:u. This evidence
does not establish the performance of cold reads or saturated callback decoding.

Binary SHA-256 values:

- Historical C++: `65f8e5c6c467049986bf124bbda50e508358a099423e5a88633174e9dc7abab8`
- Rust before Tokio: `c7b243ddabae060b84a4e49e244e2b1a70ef0df75fec87ff9a12a08194ef9b61`
- Rust with Tokio: `db5fbeb9c6db27df2e8d27b9204bb1eb717dda455f022820e53a2af4fe3731d4`

The historical C++ binary is from commit 6ff9b040095ec810f4b14bf7edc022483d85e39b;
pre-Tokio Rust is the previously validated 57785ba69 binary. The latest binary
includes the uncommitted Tokio adaptation. The historical comparison includes
the intervening migration changes and is not an isolated language-overhead test.

Raw commands, per-second output, process counters, hashes, row verification,
shutdown records and the separate profile are retained under
`.audit/io-tokio-performance/`. `results.json`, `summary.json`, and
`verification.json` contain the machine-readable results. The seed location is
recorded in `provenance.json`. No product source was changed for this measurement.
