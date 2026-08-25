# Build and run seekdb on Android

Cross-compile seekdb for Android arm64-v8a on macOS, then deploy it to an emulator or physical device.

## Prerequisites

- macOS host
- Android NDK 27.x, with `ANDROID_NDK_HOME` set when it is not installed at the default SDK path
- An arm64-v8a device or emulator running API 28 or later
- `adb` and a MySQL-compatible client on `PATH`

## Build

Use the supported Android entry point. It initializes Android dependencies, configures the build, and builds seekdb:

```bash
./build.sh release --android --init --make -j16
```

The binary is generated at:

```text
build_android_release/src/observer/seekdb
```

The Android CMake build does not provide an `all_tests` target. Validate affected unit tests on a supported Linux host by following [Write and run unit tests](unittest.md).

## Strip and deploy

Use the NDK strip tool because the macOS `strip` command cannot process Android ELF binaries:

```bash
NDK_STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
"$NDK_STRIP" -o /tmp/seekdb build_android_release/src/observer/seekdb
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
