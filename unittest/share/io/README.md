# Local I/O request-path benchmark

This Linux benchmark compares the same public `ObLocalDevice` / `ObIOManager`
request path against two built seekdb trees. It is an opt-in performance fixture,
not a C++ unit-test target. It adds no product hooks and does not run in ordinary
correctness CI.

The driver is the same source used to investigate the Rust I/O migration CPU
increase. Compile that one source against both versions. The compiler freezes
each library and records source, executable and library hashes. The runner checks
those hashes and the absolute dynamic-library dependency before running.

## Build

First build `//src/observer:_liboceanbase_link` in both release worktrees using
the repository build environment. Run the following from the tree containing
this benchmark. Replace the example roots and hashes with the selected builds.
Output directories must not already exist.

```sh
source ~/.bashrc
python3 unittest/share/io/compile.py --root /path/to/before --side before \
  --library-sha256 BEFORE_LIBRARY_SHA256 --output /path/to/results/before
python3 unittest/share/io/compile.py --root /path/to/after --side after \
  --library-sha256 AFTER_LIBRARY_SHA256 --output /path/to/results/after
```

`compile.py` uses the selected tree's SDK compiler, generated includes and built
shared library. Both trees must expose the public device/manager interfaces used
by `io_path.cpp`. It records each tree's commit and tracked diff hash; select
clean revisions when attributing a change to a commit.

## Run

Inspect host load and competing processes first. Run a pilot to select operation
counts long enough for useful intervals, then freeze those counts for both sides.
The default reads total 16 GiB and writes total 32 GiB per trial. Each trial uses
a freshly initialized 1 GiB private file. Successfully verified data files are
removed; result JSON, commands, hashes and logs remain. At least 1 GiB disk space
and memory for the 1 GiB reference image plus the I/O runtime are required.

```sh
python3 unittest/share/io/run-paired.py --before /path/to/results/before \
  --after /path/to/results/after --case worker-read --mode pilot \
  --output /path/to/results/pilot
python3 unittest/share/io/run-paired.py --before /path/to/results/before \
  --after /path/to/results/after --case worker-read --mode measure \
  --output /path/to/results/measured
```

Repeat `--case` to select `native-read`, `native-write`, `worker-read` or
`worker-write`; omitting it selects all four. `plan`, the default mode, only
writes the five-pair schedule. `measure` runs five pairs with AB/BA order.
`profile` runs a separate pair with pidstat and iostat; `--perf` also records
user CPU stacks. Missing profiler output must not be interpreted as evidence.
Pilot and profile timings never become measured results. The summary marks
intervals shorter than ten seconds. There is no automatic performance pass.

For a small correctness check without performance output, invoke a compiled
driver directly with a fresh file path:

```sh
/path/to/results/after/io_path /path/to/results/check.bin worker read 16 1024 validate
```

The direct invocation retains its private file. The driver uses exclusive create
and refuses an existing path. Native selects the native-preferred backend; use a
separate syscall trace to confirm the actual route on a new platform.

## Measurement contract

Both versions use `O_DIRECT`, application depth 32, device depth 512, four worker
threads, one native channel and unlimited rate admission. Reads are deterministic
aligned random 16 KiB requests; writes are aligned sequential 256 KiB requests.
A write run must cover the entire file, whose initial bytes differ from the
expected final bytes. There is no separate warmup pass.

Initialization, its fdatasync and final whole-file verification are outside the
I/O interval. Submission, completion drain and read-byte comparisons are inside.
Every operation must complete with the requested byte count. Submission,
completion, short-I/O or data errors fail the run. Final write fdatasync duration
is reported separately.

`cpu_seconds_per_gib` is the process's accumulated user plus system CPU time
inside the I/O interval divided by completed logical GiB. It sums work across
all threads and can exceed elapsed wall time. It includes request bookkeeping,
copying, data comparisons and synchronization, not only pread/pwrite. Latencies
are application slot-observation times, not kernel completion timestamps.
Peak RSS includes the fixed reference image and setup allocations.

Use repeated unprofiled results and their ranges before declaring a regression.
Pair this microbenchmark with a representative SQL test. This fixture alone
cannot determine an end-to-end impact or assign overhead to a specific stage.
