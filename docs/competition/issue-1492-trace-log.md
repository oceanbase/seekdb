# Issue #1492 - per-transaction TraceLog retained storage

Related issue: https://github.com/oceanbase/seekdb/issues/1492

## Problem
ObTxCtx inherits ObTransCtx, which unconditionally embeds ObTransTraceLog
trace_log_ and a tlog_ pointer. In non-debug builds, setting
enable_record_trace_log=false nulls tlog_ but the recorder object is still
constructed, reset in default_init_(), and counted in every transaction
context size (~189 event slots plus a 1,200-byte info buffer).

## Proposed direction
1. Guard the recorder behind a compile-time / configuration switch so that
   disabled builds do not instantiate it.
2. Remove the stale reset call in ObTxCtx::default_init_().
3. Add a size assertion so future regressions are caught in CI.

## Validation plan
- RelWithDebInfo build with enable_record_trace_log=false.
- Compare sizeof(ObTxCtx) before/after via DWARF inspection.
- Run the existing transaction regression suite.

## Status
This PR records the analysis and proposal only; the code change will follow
on this same branch. Documentation-only, no build or runtime behaviour changed.