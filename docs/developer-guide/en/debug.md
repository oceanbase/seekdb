---
title: Debug
---

# Debug seekdb

The supported Release build uses `RelWithDebInfo`, so it retains debug information while compiling with production optimization. There is no `build.sh debug` mode.

## Build for debugging

```bash
source ~/.bashrc
./build.sh release --init --make
```

The binary is `build_release/src/observer/seekdb`. Optimized code may inline functions or report variables as optimized out; use logs or a narrower Bazel test when stepping through optimized code is impractical.

## Attach a debugger

Find the process and attach GDB on Linux:

```bash
pidof seekdb
gdb build_release/src/observer/seekdb -p <pid>
```

On macOS, use LLDB:

```bash
lldb -p <pid>
```

For a core dump, use the exact binary that produced it:

```bash
gdb /path/to/seekdb /path/to/core
```

Record `seekdb -V` with the dump so the revision and build flags can be matched.

## Debug an installed RPM

The package installs the runtime binary as `/usr/bin/seekdb`. Install the matching debuginfo package when the configured repository provides one, or extract it without installing:

```bash
rpm2cpio seekdb-debuginfo-<version>.<arch>.rpm | cpio -idmv
find usr/lib/debug -type f -name '*seekdb*.debug'
```

Load the discovered file in GDB rather than relying on an old hard-coded package path:

```gdb
symbol-file /absolute/path/to/seekdb.debug
```

The runtime binary and debuginfo package must have the same revision and architecture.

## Debug with logs

Logging is usually more effective for concurrent server behavior:

```cpp
LOG_DEBUG("insert sql generated", K(insert_sql));
```

Use `K(variable)` to print both the variable name and value. See [Logging system](logging.md) for log levels, modules, rate limiting, and runtime configuration.

For systemd installations, inspect service and server logs with:

```bash
journalctl -u seekdb --since today
tail -F /var/lib/oceanbase/log/seekdb.log
```

If `base-dir` was changed in `/etc/seekdb/seekdb.cnf`, use the corresponding log directory.
