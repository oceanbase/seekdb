# ObTxCtx lifecycle cache experiment

This change removes `ObServerObjectPool` and its last consumer, `ObTxCtx`.
It starts from official `upstream/master` commit
`f432e19740265cdc558c73bf517efd2f8a106e3f`.

Related work: [PR #1428](https://github.com/oceanbase/seekdb/pull/1428)
explores transaction-context compression and uncached on-demand allocation.
This PR is independent of that branch and keeps the uncompressed master context,
using lifecycle-bound reuse to remove server-wide idle retention.
[PR #1398](https://github.com/oceanbase/seekdb/pull/1398) already moved table-scan
iterators out of the server object pool into SQL sessions.

## Ownership

- User SQL sessions create a cache lazily and retain at most one free context.
  Internal sessions and work without a user session allocate and delete directly.
- Checked-out contexts retain the cache control block independently of the
  session. Closing the session deletes free contexts immediately; a later
  transaction cleanup deletes its context and drops the remaining reference.
- Replay has a per-LS cache of at most 64 free contexts. It closes when all
  committed log has been submitted and pending tasks reach zero, including on
  continuous standby replay. Local append handoff and LS offline/reset are
  additional close points. It does not identify the last transaction.
- Tx-table recovery has a cache of at most 64 free contexts, closed at the end
  of the SSTable scan. Restored contexts may outlive that scan safely.
- Allocation uses nonthrowing `operator new`, with explicit 64-byte alignment
  and matching release of the original allocation. No cleanup timer is added.

On this build, `sizeof(ObTxCtx) = 10944`, `alignof(ObTxCtx) = 64`, and
`sizeof(TxCtxCache) = 64`. Each session adds one pointer. The object allocation
also includes a 24-byte header and up to 63 bytes of alignment padding.

## Initial prototype validation on 2026-10-10

The following short A/B table was measured for commit `d4c7e7d4f172`, before
the replay-idle callback was added. It is not formal performance evidence for
the current implementation. See the follow-up record below.

Both release binaries were built with `CARGO_NET_OFFLINE=true ob-make seekdb`
from their `build_release` directories, without a `-j` option. `git diff --check`
passed. The standalone test exercised direct allocation, alignment, reuse,
cache overflow, 160,000 parallel borrow/return operations, and 200 rounds of
eight simultaneous late returns after the owner was closed.

The A/B test used one identical crash snapshot with about 60 MB of replayable
log, including 20,000 sysbench write transactions. Every restart preserved the
committed sentinel row and excluded the uncommitted row. This primarily covers
log replay; it does not establish coverage of nonempty tx-context SSTable recovery.

The local shared development host has 80 logical CPUs. Each server was pinned
to CPUs 48-55, with `cpu_count=8` and the legacy `memory_limit=4G` option.
That option is ignored by current memory sizing, so this test used automatic
memory budgeting; it must not be described as a measured 4 GiB limit.
Sysbench 1.0.20 used four
10,000-row tables, eight client threads, uniform keys, fixed seed 20261010,
text protocol, a five-second warmup and 20 seconds per measured workload.
Startup used INFO logging to capture replay boundaries; measured foreground
workloads used ERROR logging. The earlier hotspot-distribution pilot had lock
timeouts in both variants and is excluded from the table below.

| Round | Variant | Replay seconds | SQL-ready seconds | write_only TPS | read_write TPS |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | master | 0.869079 | 3.429 | 927.53 | 517.78 |
| 1 | experiment | 0.737562 | 3.414 | 1010.64 | 541.55 |
| 2 | experiment | 0.875077 | 4.204 | 1195.90 | 563.25 |
| 3 | master | 0.829249 | 3.099 | 1433.59 | 599.67 |
| 4 | master | 0.710689 | 3.289 | 1602.59 | 720.43 |
| 5 | experiment | 0.667707 | 2.981 | 1646.57 | 712.32 |

All 12 measured workloads had zero ignored SQL errors and zero reconnects.
Replay median: master 0.829249 seconds, experiment 0.737562 seconds. The replay
cache had 20,413-20,414 hits and 26-27 misses per restart (about 99.87% hits).
No replay slowdown was observed in this workload. This is not evidence of a
replay speedup: the shared environment and short recovery window introduce noise.

Foreground throughput drifted strongly over time in both variants. These
samples cannot establish that foreground performance has no regression.
A stable benchmark host, longer steady workloads and higher concurrency remain
necessary before treating this prototype as ready to merge.

Runtime inspection after four user connections each performed 100 updates:

| State | Live session objects | Active ObTxCtx | Replay cache pointer | Recover cache pointer |
| --- | ---: | ---: | --- | --- |
| Connections open | 4 | 0 | null | null |
| Two seconds after disconnect | 0 | 0 | null | null |

Together with the lifetime tests and user-session-only creation rule, this
confirms that this primary-instance idle scenario has no lifecycle-owned
cached ObTxCtx remaining. Allocator-retained free pages and overall RSS are
different measurements and were not used as a zero-cache criterion.

Raw artifacts from this run are retained locally:
`uniform_results.json`, `uniform-*.txt`, restart logs, and
`connections-open.txt` / `connections-closed.txt`.

## Follow-up: restart and standby replay

The replay-idle callback closes storage caches once the committed log tail has
been submitted, the pending buffer count is zero, and the minimum unreplayed
LSN reaches that tail. Pre-barrier buffer accounting can reach zero before all
barrier queue entries have been retired, so those entries must be popped before
an idle callback is permitted. It is
checked by both the submitter
and the last replay worker, since task completion can race cursor publication.
Batch epochs suppress repeated idle callbacks and permit new batches to notify
independently. The owner still protects concurrent context allocation and late
returns. No periodic cleanup is introduced.

Both binaries passed crash restart and clean restart checks on an identical
checkpoint snapshot containing four uncommitted transactions. Tablet 49401's
SSTable contained four serialized contexts. The candidate's recovery-close log
recorded four misses, proving that the nonempty tx-context SSTable path ran.
Four 10,000-row sysbench tables were compared row by row using SHA-256. All
hashes matched the original snapshot. The committed sentinel table retained
128 rows with sum(v)=56896; all 8,000 uncommitted rows remained invisible.
SQL-ready times on the final recheck were 2.149/2.141 seconds for
master/candidate crash recovery and 2.141/2.155 seconds for clean restart.
These are short functional-test timings,
not statistical evidence of a performance improvement.

Both versions retained four active recovered transaction contexts in this
deliberately unresolved-transaction fixture. The candidate's replay and recover
cache roots were null. Live transaction state and empty reusable caches must
not be conflated; this fixture is not an all-transaction-objects-zero assertion.

Each version also passed a real local primary/standby test with 9,000 confirmed
commits across streaming replay, standby downtime/backlog catch-up, standby
crash restart, primary crash restart, and continued replication afterward.
The sysbench table hashes and acknowledged-transaction counters matched at all
seven comparison points, including online tablet creation and schema DDL.
The candidate standby's replay/recover roots and active
transaction count were zero after streaming and after both restarts. Source
SSTables were frozen and major compaction settled before standby bootstrap;
the gRPC service was enabled only for these isolated instances. The test used
the actual `memory_budget=4G` setting. Catch-up/readable-SCN delays include
periodic timestamp refresh and must not be used as pure replay throughput.

The current build and the standalone lifetime test passed. The formal Jenkins
master/candidate/master sysbench comparison also completed; the results are
recorded below. The earlier shared-host short runs remain preliminary evidence,
not the basis for the foreground performance conclusion.

Follow-up artifacts are retained locally:
`restart-final/restart-results.json`, `restart3/checkpoint-active-contexts.json`,
`standby-final/standby-results.json`, and per-instance logs/debugger snapshots.

Reproduce using `validate_recovery.py restart` or `standby`, passing `--root`,
`--snapshot`, `--baseline`, and `--candidate`. The snapshot is the immutable
crash snapshot generated by `run_ab.py prepare`. `inspect` reopens the completed
restart fixtures and compares active transaction counts between both binaries.

## Formal sysbench gate

Use the authorized `lite_perf_guard_new` Jenkins workflow with the two exact
locally built binaries, without changing its shared baseline. The current job
was checked live: six workloads, 550 clients, 30 tables with 100,000 rows each,
and sysbench `1.1.0-3ceba0b`. Set `sysbench=true`, all other workload flags false,
`refresh_base=false`, `sysbench_warmup_time=60`, and `sysbench_runtime=300`.
Run master/candidate/master with identical arguments; repeat if per-workload
differences or drift are material. Preserve binary SHA-256, build source,
job numbers, actual runtime configuration, raw workload logs and machine load.
Require complete workload coverage, zero ignored errors/reconnects, and compare
TPS, average latency and P99. Jenkins SUCCESS alone is insufficient. The
latest historical job's one-second CPU-diagnostic run is not a usable baseline.

### Completed Jenkins comparison on 2026-10-10

- Jenkins master runs #267 and #269:
  `f432e19740265cdc558c73bf517efd2f8a106e3f`.
- Jenkins candidate run #268:
  `f95f2c496e78eaecbcef36ef5d1aa7598777d953`.
- All 18 workloads completed with zero ignored errors and zero reconnects.
  Each measured interval lasted at least 300 seconds after a 60-second warmup.
- The pressure target used 22 CPUs, affinity `0-21`, PERF logging, disabled
  adaptive compaction and disabled defensive checks. Its effective memory budget
  was automatic (`memory_budget=0M`), not 174 GiB: the configured legacy
  `memory_limit=174G` is ignored. Master and candidate used the same settings.
- Jenkins applies `strip -g` again during deployment. All allocated ELF sections
  of each installed executable matched its selected uploaded binary exactly;
  changes in stripped section-table layout explain the whole-file hash change.
- TPS and latency changes below compare the candidate with the arithmetic mean
  of the two master runs. Lower latency is better.

| Workload | Master #267 TPS | Candidate #268 TPS | Master #269 TPS | TPS change | Avg latency change | P99 change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| point_select | 247196.85 | 250894.77 | 248738.73 | +1.18% | -1.13% | -1.79% |
| read_only | 10299.47 | 10324.47 | 10327.09 | +0.11% | -0.08% | 0.00% |
| read_write | 5395.31 | 5426.61 | 5414.76 | +0.40% | -0.38% | -0.90% |
| insert | 49969.10 | 49768.96 | 50007.67 | -0.44% | +0.41% | +2.73% |
| update_non_index | 50754.38 | 50270.52 | 50201.18 | -0.41% | +0.37% | +0.89% |
| write_only | 18164.11 | 18101.40 | 17929.99 | +0.30% | -0.28% | -8.61% |

The geometric mean of the six normalized TPS ratios was +0.19%. Master-to-master
TPS drift ranged from -1.29% to +0.62%. Overall throughput was approximately
unchanged in this A/B/A run; insert showed a small measured slowdown and higher
P99. One candidate run does not prove that these small differences are stable
or that every metric has no regression. No sustained CPU profile was collected,
and these foreground workloads do not establish standalone replay throughput.

Verified artifact identities (internal download addresses omitted):

- Master: 165932112 bytes, SHA-256
  `e8b0a5abe9c33064075ba37368b14994ac0b8748fc0cd5f403611c24bd6f369c`.
- Candidate: 165932720 bytes, SHA-256
  `194dcf8d317fa547f29652eb06674dcc8dfef6c75f708437e6164404cefaf712`.

Raw records under the follow-up artifact directory include
`formal-sysbench-results.json`, `formal-sysbench-manifest.json`,
`jenkins-{267,268,269}-console.log`, and the corresponding deployment identities.
Internal endpoints and host-specific artifact paths are not published here.

## Reproduce

First build both checkouts. Run the lifetime test from the experiment checkout:

```sh
python3 tools/tx_ctx_cache_experiment/run_cache_test.py build_release
```

The A/B driver requires PyMySQL, sysbench Lua scripts and an unused TCP port.
Use a fresh, adequately sized artifact directory. It starts only its own server
processes, kills the seed process to make the crash snapshot, and stops every
comparison instance. The `inspect` phase attaches a read-only debugger to its
own instance and requires host ptrace access. Pass absolute binary paths:

```sh
python3 tools/tx_ctx_cache_experiment/run_ab.py prepare \
  --root /tmp/txctx-cache-ab --baseline /path/to/master/seekdb \
  --candidate /path/to/experiment/seekdb --cpus 0-7
python3 tools/tx_ctx_cache_experiment/run_ab.py compare \
  --root /tmp/txctx-cache-ab --baseline /path/to/master/seekdb \
  --candidate /path/to/experiment/seekdb --cpus 0-7
python3 tools/tx_ctx_cache_experiment/run_ab.py inspect \
  --root /tmp/txctx-cache-ab --baseline /path/to/master/seekdb \
  --candidate /path/to/experiment/seekdb --cpus 0-7
```

Adjust `--sysbench-lua`, `--port`, `--threads` and `--seconds` as needed. Do not
reuse an existing comparison root for a second `compare` run; each round
requires a fresh clone target. Review `valid_sysbench` and the raw workload
output instead of treating driver exit status as proof of clean performance.
