---
title: Debug
---

# Debug seekdb

Use the Bazel `O1` configuration for source-level debugging. It keeps optimization lower than the default `O2` build while retaining the repository's normal toolchain and link layout.

## Build for debugging

```bash
source ~/.bashrc
./bazel.py deps init
./bazel.py build --config=O1 //src/observer:seekdb
```

The binary is `build_bazel/bin/src/observer/seekdb`. Record `seekdb -V` with any core dump or debug report so the binary revision and build flags can be matched.

## Attach GDB or LLDB

Find the process and attach GDB on Linux:

```bash
pidof seekdb
gdb build_bazel/bin/src/observer/seekdb -p <pid>
```

On macOS, use LLDB:

```bash
lldb -p <pid>
```

For a core dump, use the exact binary that produced it:

```bash
gdb /path/to/seekdb /path/to/core
```

Set breakpoints, inspect variables, and obtain a backtrace with the usual debugger commands. Optimized code may still inline functions or report variables as optimized out; use logs or a narrower Bazel test when necessary.

## Use RPM debuginfo

The package installs the runtime binary as `/usr/bin/seekdb`. Install the matching debuginfo package when the configured repository provides one, or extract it without installing:

```bash
rpm2cpio seekdb-debuginfo-<version>.<arch>.rpm | cpio -idmv
find usr/lib/debug -type f -name '*seekdb*.debug'
```

Load the discovered file in GDB:

```gdb
symbol-file /absolute/path/to/seekdb.debug
```

The runtime binary and debuginfo package must have the same revision and architecture. Do not rely on a fixed package path; use the file found in the extracted package.

## Debug with logs

Logging is usually more effective than stopping a concurrent server in a debugger. Add structured fields with `K()` and rebuild the affected target:

```cpp
LOG_DEBUG("insert sql generated", K(insert_sql), K(lbt()));
```

Use the configured base directory to locate logs. For a systemd installation, inspect both service output and the server log:

```bash
journalctl -u seekdb --since today
tail -F /var/lib/oceanbase/log/seekdb.log
```

If `base-dir` was changed in `/etc/seekdb/seekdb.cnf`, use the corresponding log directory. Search a request by its trace ID:

```sql
SELECT last_trace_id();
```

The trace ID appears in the log line; use `rg` or `grep` to find all related entries. The [logging guide](logging.md) documents log fields, levels, rotation, and rate limiting.

## Adjust logging while debugging

The following settings are dynamically effective cluster parameters:

```sql
ALTER SYSTEM SET syslog_level = 'DEBUG';
ALTER SYSTEM SET syslog_io_bandwidth_limit = '50MB';
ALTER SYSTEM SET diag_syslog_per_error_limit = 1000;
ALTER SYSTEM SET enable_async_syslog = false;
```

Restore the original values after debugging. Increasing log volume or disabling asynchronous logging can affect performance and disk usage.

## Print and resolve a call stack

Include `lbt()` in a structured log when a source-level backtrace is useful:

```cpp
LOG_DEBUG("state before retry", K(state), K(lbt()));
```

Resolve addresses with the same binary that produced the log. For example:

```bash
addr2line -pCfe build_bazel/bin/src/observer/seekdb <address> ...
```

Use a binary with matching debug information; otherwise the output may contain only `??` frames.

## SQL execution trace

Enable the session trace, run the statement, and inspect the recorded operations:

```sql
SET ob_enable_show_trace = 1;
-- run the statement to investigate
SHOW TRACE;
```

Disable the setting when it is no longer needed. The trace is intended for focused diagnosis and can add overhead.

## Debug Sync

Debug Sync pauses a selected server thread at an existing `DEBUG_SYNC` point without stopping the entire process. It is useful when attaching a debugger would interfere with heartbeats or concurrent activity.

Enable the facility, configure a point, and signal it from another session:

```sql
ALTER SYSTEM SET debug_sync_timeout = '100000s';
SET ob_global_debug_sync = 'BEFORE_UNIT_MANAGER_LOAD wait_for signal_name execute 10000';
SET ob_global_debug_sync = 'now signal signal_name';
```

Clear the point and disable Debug Sync when finished:

```sql
SET ob_global_debug_sync = 'BEFORE_UNIT_MANAGER_LOAD clear';
ALTER SYSTEM SET debug_sync_timeout = 0;
```

The point name must exist in the code path being tested. Adding a new point requires adding `DEBUG_SYNC(...)` in source code and rebuilding the affected target.
