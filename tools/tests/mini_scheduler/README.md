# MINI scheduler selection regression tests

Run from the repository root with Python 3 and a C++17 compiler supporting
AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
python3 tools/tests/mini_scheduler/run.py
```

The runner extracts `pop_task_from_ready_list_` and
`record_ready_task_dispatch_` verbatim from the current scheduler source,
then compiles them with mock DAG, list and dispatch adapters. It does not
reimplement the selection policy. Temporary outputs are removed on exit;
use `--output /path/to/results` to retain source hashes, binaries and logs,
or `--compiler clang++` to select the compiler.

The 22 cases cover large MINI selection behind a long small-task queue,
continued small-task progress, MINI/MDS alternation with one worker,
transaction-table progress, diagnostic counter resets, successful versus
failed dispatch accounting, repeated size preference across forced FIFO
turns, emergency work, dependencies, retries, cancellation and cleanup.
Both optimized and ASAN/UBSAN builds run the same cases. Restoring the
FIFO-turn size-bit toggle makes the repeated mixed-workload case fail.

These are extracted-function tests: real kernel locks, concurrent DAG
lifetimes, the outer dispatch caller, recovery and physical memory
reclamation are outside their coverage. The printed scan timing uses
mock objects and is not a production contention benchmark. A kernel build
and database regressions remain separate checks.
