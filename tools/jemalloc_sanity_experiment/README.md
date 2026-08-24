# jemalloc + Sanity experiment

This experiment routes jemalloc allocations through a dedicated arena whose
extent hook obtains memory from the address interval observed by Sanity. The
adapter keeps jemalloc metadata and redzones poisoned, and unpoisons only the
requested user bytes.

The deleted OBMalloc implementation's maximum candidate was
`[0x0c0000000000, 0x600000000000)`, an 84 TiB application interval with a
10.5 TiB shadow interval; occupied mappings made it retreat in 128 GiB steps.
The first jemalloc proof of concept used only 64 GiB. This version restores the
former capacity policy: it tries the old upper bounds and retreats in the same
increments. Reservations require working `MAP_FIXED_NOREPLACE` semantics, so
an existing mapping is never overwritten. Initialization performs a collision
probe and exits with status 127 when the running kernel does not support the
flag; there is no old-kernel address-hint fallback.

Run the focused checks after `./bazel.py deps init`:

```sh
tools/jemalloc_sanity_experiment/run.sh valid
tools/jemalloc_sanity_experiment/run.sh overflow
tools/jemalloc_sanity_experiment/run.sh uaf
tools/jemalloc_sanity_experiment/run.sh memcpy_overflow
tools/jemalloc_sanity_experiment/run.sh snprintf_overflow
tools/jemalloc_sanity_experiment/run.sh sprintf_overflow
tools/jemalloc_sanity_experiment/run.sh arena_valid
tools/jemalloc_sanity_experiment/run.sh arena_reuse_valid
tools/jemalloc_sanity_experiment/run.sh arena_typed_valid
tools/jemalloc_sanity_experiment/run.sh arena_typed_aligned_valid
tools/jemalloc_sanity_experiment/run.sh arena_typed_down_valid
tools/jemalloc_sanity_experiment/run.sh arena_typed_aligned_bf_valid
tools/jemalloc_sanity_experiment/run.sh arena_alignment_one_valid
tools/jemalloc_sanity_experiment/run.sh arena_large_alignment_valid
tools/jemalloc_sanity_experiment/run.sh arena_layout_valid
tools/jemalloc_sanity_experiment/run.sh arena_overflow
tools/jemalloc_sanity_experiment/run.sh arena_aligned_overflow
tools/jemalloc_sanity_experiment/run.sh arena_down_overflow
tools/jemalloc_sanity_experiment/run.sh arena_down_reuse_uaf
tools/jemalloc_sanity_experiment/run.sh arena_reuse_uaf
tools/jemalloc_sanity_experiment/run.sh arena_free_uaf
tools/jemalloc_sanity_experiment/run.sh arena_reset_remain_uaf
tools/jemalloc_sanity_experiment/run.sh arena_tracer_uaf
tools/jemalloc_sanity_experiment/run.sh arena_partial_free_uaf
tools/jemalloc_sanity_experiment/run.sh arena_partial_retrace_valid
tools/jemalloc_sanity_experiment/run.sh arena_aligned_bf_overflow
tools/jemalloc_sanity_experiment/run.sh arena_realloc_overflow
```

The `valid`, `arena_valid`, `arena_reuse_valid`, `arena_typed_valid`,
`arena_typed_aligned_valid`, `arena_typed_down_valid`,
`arena_typed_aligned_bf_valid`, `arena_alignment_one_valid`,
`arena_large_alignment_valid`, `arena_layout_valid`, and
`arena_partial_retrace_valid` commands must exit successfully. The remaining
commands must stop in
`memory_sanity_abort`.

This is intentionally an experiment. jemalloc background threads and tcache
are disabled in Sanity mode.

## Mapping from the former OBMalloc integration

The old implementation had several independent adaptation layers. They map to
the current experiment as follows:

| Former OBMalloc adaptation | jemalloc experiment |
| --- | --- |
| Reserve the application interval and its 1:8 shadow; allocate chunks with `sanity_mmap` | Reserve the same style of interval and give a dedicated jemalloc arena an extent hook backed by it |
| Poison `AObject` headers and allocation tails; unpoison only requested bytes; poison user bytes on free | Keep a private `AllocationHeader`, alignment padding, and redzone poisoned; unpoison only `requested_`; poison the full jemalloc allocation before release |
| Disable compiler range checks while allocator metadata is being manipulated | Build the allocator unity group without the Sanity pass and guard calls entering jemalloc |
| Add redzones inside `PageArena`, poison retained pages on reuse, and opt `MemoryContext` into that mode | The `ob_memory_sanity` facade owns sub-allocation layout/redzones while `PageArena` keeps raw allocation and lifetime policy; coverage includes `alloc_down`, best-fit aligned allocation, partial free, and tracer rollback, and `MemoryContext` opts in explicitly |
| Unpoison/poison cache macroblocks around their OBMalloc lifecycle | The jemalloc backend's cache-macroblock path already calls `jemalloc_malloc/free`, so it inherits the common adapter |
| Sanity-aware libc operations supplied by the Sanity runtime | Final-link `--wrap` checks avoid that runtime's first-use `dlsym` recursion |

The former direct `AChunk` mmap path is not copied into the normal jemalloc
allocation path because jemalloc replaces it there. Co-routine/thread-stack
chunks still use direct mappings and were intentionally excluded from shadow
allocation in the old code as well. The old SQL-operator datum checks were
proactive diagnostic checkpoints, not allocator correctness machinery, so
they are not restored by this experiment.
