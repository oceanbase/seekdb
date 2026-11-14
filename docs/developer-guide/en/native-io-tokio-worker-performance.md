# Tokio worker-read performance — 2026-11-14

The initial Tokio adaptation at `5eac57261` had a reproducible CPU-cost regression in the forced
worker-read path. The five measured CPU ranges do not overlap the pre-Tokio Rust
range. This is a different result from the nearly unchanged warm SQL workload.

| Version | CPU seconds/GiB median [min, max] | Throughput MiB/s median [min, max] | Observed p99 ms median [min, max] |
| --- | --- | --- | --- |
| Original C++ | 1.548 [1.518, 1.590] | 379.049 [366.968, 381.175] | 4.136 [4.088, 4.293] |
| Rust before Tokio | 1.507 [1.487, 1.544] | 378.934 [369.745, 402.112] | 4.193 [4.135, 4.286] |
| Rust with Tokio | 1.981 [1.973, 2.013] | 366.080 [363.049, 377.343] | 4.353 [4.325, 4.387] |

Compared with pre-Tokio Rust, that adaptation's CPU/GiB was +31.42%,
throughput -3.39%, and observed p99
+3.81%. Against original C++, the corresponding
changes are +27.93%, -3.42%,
and +5.23%. This CPU regression exceeds the earlier approximate
20% allowance for this workload. It is not evidence of inherent Rust language cost.

Each of five interleaved trials per version read 16 GiB: 1,048,576 random 16 KiB
reads from a freshly initialized 1 GiB O_DIRECT file, application depth 32,
backend depth 512, and four workers. Every read copied into an application buffer
and compared every byte inside the timed interval. Process user plus system CPU
includes submission, completion and drain. Initialization and final whole-file
verification are outside timing. There is no fixture warmup. Observed p99 is the
application ring's completion-observation latency, not a kernel timestamp.
All 15 trials completed 15,728,640 requests and 240 GiB with zero submission,
completion, short-read or data errors, and passed whole-file verification.
Peak RSS includes a fixed 1 GiB reference image, so it is not an I/O memory metric.

The historical generic device/manager APIs have been deleted. The old C++ fixture
was retained; its typed-owner adaptation uses the existing CXX NamedOwner and
ObIOHandle interfaces. Both Rust versions use byte-identical benchmark source.
The random pattern, slot finish/copy/compare code, timer boundaries and statistics
formula are retained from the old driver; API setup/submission differs as required
by the migration. Typed inventory registration and file initialization occur
outside timing. No production or C++ unit-test code was changed for this run.

A small trace independently verified 512 successful 16 KiB pread calls, and a
separate flag trace confirmed O_DIRECT on the typed read descriptor. The two
io_submit calls during setup belong to directory capability probing, not the
forced worker-read workload.

Separate 4 GiB C++ and Tokio profiles are excluded from reported timings: profiling
substantially increased measured CPU cost. During the trimmed I/O intervals, NVMe
utilization medians were 98.8% and 97.6%, respectively.
The user-cycle samples show buffer copy/comparison, request publication and
completion work. The Tokio profile additionally exposes task polling/destruction
and per-operation WorkerExit/CompletionWake work. These are investigation leads,
not a quantitative attribution of the whole regression. Kernel and off-CPU costs
are absent from cycles:u. Raw phase markers and monotonic-clock perf captures
permit excluding file setup and final verification.

The host is shared, with 16 CPUs available to the benchmark. Other user processes
were left running; all samples, including less favorable trials, are retained.
No optimizing edits were made between measurements. Normal SQL performance does
not substitute for this forced worker-read result; native AIO, writes and callback
saturation were not measured here.

Frozen artifacts (executable / shared-library SHA-256):

- Original C++: `ab34d8f81139b32bdc4764b012eb299c68d146e19f5a5303dc1041b83b0e0cb7` / `f5d730e029fb219ad86e3ef2d824771cb70ea1e2ad20fb721eeb8e5fd3917270`
- Rust before Tokio: `7d55f0b21da3cbf14fc47f30cce4582fcd134c560fbb04f826beb9afb22e416d` / `c37656f87fd7dd52315eb1a12bf1a700f842049f134d62ba5809daa78a30a0d1`
- Rust with Tokio: `127bdd3b9f586b583b89ed5964c397aebf9a6ce83571c55d5d749e8f8b2d7535` / `cc470d336b5d40bf97680b965fb0c3798e88d978d3a29e4c519177b75add10f4`

Raw source, compiler commands, pilot traces, phase logs, host snapshots, all trial
results and profile reports are in `.audit/io-tokio-worker-performance/`. Summary
and assertions are in `summary.json` and `verification.json`. Temporary measured
data files were removed after successful verification. Existing user database
instances were not restarted or modified.

## Task-exit notification fix

The worker backend previously ran `WorkerExit` once per persistent thread. The
Tokio adaptation ran it once per submitted task, including an unconditional
completion-port wake. Direct completion had already published the request result,
so this woke `io-progress` to scan otherwise idle state for each request.

The fix retains the mutex-protected task decrement and panic failure recording.
It emits the exit notification only when the task count reaches zero or the
current task panics. Normal completion notifications, stop notifications and
conditional service-progress notifications remain unchanged. No new pool,
queue or configuration is introduced.

A notification-only ablation retained Tokio and removed about 86% of the measured
CPU increase over pre-Tokio Rust. Separate thread CPU sampling showed
`io-progress` falling from 0.235 CPU seconds/GiB to below the observed tick
resolution. Evidence is in `.audit/io-tokio-cpu-diagnosis/diagnosis.md`.

The final built library was verified with five interleaved trials per version,
each reading 4 GiB with the same 16 KiB O_DIRECT workload, depths and four workers.
All 10 trials completed 2,621,440 requests and 40 GiB, with zero submission,
completion, short-read or data errors and successful whole-file verification.

| Version | CPU seconds/GiB median [min, max] | Throughput MiB/s median [min, max] |
| --- | --- | --- |
| Rust before Tokio | 1.506 [1.463, 1.549] | 377.603 [365.033, 401.010] |
| Fixed Tokio | 1.551 [1.543, 1.586] | 391.807 [372.617, 399.192] |

The CPU median gap is now 2.98%, with overlapping ranges. The throughput ranges
also overlap, so these runs do not establish a throughput improvement. The prior
disk profiles showed utilization near 98%; CPU consumption can improve much more
than throughput on this workload. No claim is made for native AIO or callback
saturation. Raw final measurements are in `.audit/io-tokio-exit-fix/`.

A deterministic test using a real single-worker Tokio backend fails before the
fix because an intermediate task changes the completion-port wake epoch. After
the fix it verifies both successful data delivery and the final-task wake. A
second test verifies that a panic still notifies while another task remains.
The existing Rust library suites pass: file-io 136, storage-io 171, with one
existing external-fixture test ignored. Both seekdb and liboceanbase.so build
successfully. Independent lifecycle review found no blocking issues. No C++
unit tests or unittest files were added.
