---
title: Debug
---

# Debug seekdb

The default Bazel build is optimized with `-O2`. For source-level debugging, use the repository's `O1` configuration, which keeps optimization lower and makes stepping through code more practical.

## Build for debugging

```bash
source ~/.bashrc
./bazel.py deps init
./bazel.py build --config=O1 //src/observer:seekdb
```

The binary is `build_bazel/bin/src/observer/seekdb`. Optimized code may still inline functions or report variables as optimized out; use logs or a narrower Bazel test when stepping through optimized code is impractical.

## Attach a debugger

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
