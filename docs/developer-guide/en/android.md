# Build and run seekdb on Android

Cross-compile seekdb for Android arm64-v8a on macOS, then deploy it to an emulator or physical device.

## Prerequisites

- macOS host
- Android NDK 27.x, with `ANDROID_NDK_HOME` set to its installation directory
- An arm64-v8a device or emulator running API 28 or later
- `adb` and a MySQL-compatible client on `PATH`

## Build

Set the installed NDK path, initialize Android dependencies, then build with Bazel:

```bash
export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/27.3.13750724"
python3 build.py init --android
python3 build.py build --config=android seekdb
```

`init --android` prepares host tools in `deps/3rd` and Android dependencies in
`deps/android`, and installs the Android target for the pinned Rust toolchain.
Native and Android builds can share one checkout.

The binary is generated at:

```text
build/bin/src/observer/seekdb
```

This configuration produces an API 28 ARM64 PIE with 16 KiB page alignment and a statically linked C++ runtime. Validate affected unit tests on a supported Linux host by following [Write and run unit tests](unittest.md).

## Allocator

Android builds use the bundled, statically linked jemalloc through SeekDB's
explicit allocator interfaces. The build sets jemalloc's page size to 16 KiB
and disables initial-exec TLS so the allocator can also be used in a loaded
library. Host libc allocations keep their own allocation and release APIs.
Background workers are enabled after server initialization reaches the logger,
rather than during allocator bootstrap.

Build the allocator test with the same Android toolchain, then run the resulting
binary on the device:

```bash
python3 build.py build --config=android //unittest/oblib:test_jemalloc_backend
adb push build/bin/unittest/oblib/test_jemalloc_backend /data/local/tmp/
adb shell chmod +x /data/local/tmp/test_jemalloc_backend
adb shell /data/local/tmp/test_jemalloc_backend
```

Validate on both 4 KiB and 16 KiB devices (`adb shell getconf PAGE_SIZE`). The
test covers realloc, alignment, cross-thread release, background reclamation
and worker restart. ELF alignment and allocator page size must both support the
device page size.

## Strip and deploy

Use the NDK strip tool because the macOS `strip` command cannot process Android ELF binaries:

```bash
NDK_STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
"$NDK_STRIP" -o /tmp/seekdb build/bin/src/observer/seekdb
adb push /tmp/seekdb /data/local/tmp/seekdb
adb shell chmod +x /data/local/tmp/seekdb
```

For an Apple Silicon NDK installation, use the actual prebuilt host directory present under `$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/`.

## Start and connect

Choose resource values that fit the device:

```bash
adb shell "mkdir -p /data/local/tmp/seekdb_data"
adb shell "/data/local/tmp/seekdb --nodaemon \
  --base-dir /data/local/tmp/seekdb_data \
  --parameter memory_budget=4G \
  --parameter datafile_size=2G \
  --parameter datafile_maxsize=4G \
  --parameter log_disk_size=2G \
  --log-level INFO"
```

Forward the SQL port and connect:

```bash
adb forward tcp:2881 tcp:2881
mysql -h127.0.0.1 -P2881 -uroot
```

Inspect logs and stop the process with:

```bash
adb shell "tail -100 /data/local/tmp/seekdb_data/log/seekdb.log"
adb shell "kill \$(pidof seekdb)"
```
