# Build and run seekdb on Android

Cross-compile seekdb for Android arm64-v8a on macOS, then deploy it to an emulator or physical device.

## Prerequisites

- macOS host (this guide is written for macOS)
- Android NDK installed (**27.x is recommended** to match pre-built dependencies; other major versions are untested). Default path example: `~/Library/Android/sdk/ndk/27.3.13750724`
- Android emulator running arm64-v8a (API 28+), or a physical device
- Dependencies built via [ob-deps](https://github.com/oceanbase/ob-deps/tree/android_arm64-v8a) `ndk/build_all.sh`
- `adb` available on PATH
- `mysql` client (for connecting after launch)

Set `ANDROID_NDK_HOME` if your NDK is in a non-default location:

```bash
export ANDROID_NDK_HOME=$HOME/Library/Android/sdk/ndk/27.3.13750724
```

> **Note**: You may see a `CMake Deprecation Warning` during configuration (from NDK's `flags.cmake` using `cmake_minimum_required(VERSION 3.6.0)`). This warning can be ignored and does not affect the build.

## Build

Use the supported Android entry point. It initializes Android dependencies, configures the build, and builds seekdb:

```bash
./build.sh release --android --init --make -j16
```

The binary is generated at:

### 2. Configure and build

To build only the observer binary:

```bash
cd build_android_release
make seekdb -j$(nproc)
```

### Build libseekdb (FFI shared library)

In the same Android build directory, build the C API shared library (CMake target `libseekdb`, output `libseekdb.so`):

```bash
cd build_android_release
make libseekdb -j$(nproc)
```

The artifact is usually `build_android_release/src/include/libseekdb.so` (relative to the repo root). The public header is `src/include/seekdb.h` in the source tree.

To reduce size, strip ELF with the NDK `llvm-strip` (not the host `strip`). On macOS or Linux hosts the toolchain lives under `toolchains/llvm/prebuilt/<host>/bin/`, for example:

```bash
NDK_STRIP=$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
$NDK_STRIP -o /tmp/libseekdb.stripped build_android_release/src/include/libseekdb.so
```

You can also pack `seekdb.h` and `libseekdb.so` into **`libseekdb-android-arm64-v8a.zip`** with [`package/libseekdb/libseekdb-build.sh`](../../../package/libseekdb/libseekdb-build.sh) (**arm64-v8a only**). From `package/libseekdb/` run `./libseekdb-build.sh --android` (builds if needed), or `./libseekdb-build.sh <path/to/build_android_*/src/include>` to pack an existing tree. On macOS, a tree that only contains the NDK-built `libseekdb.so` still gets that naming (not `darwin-*`).

The Android CMake build does not provide an `all_tests` target. Validate affected unit tests on a supported Linux host by following [Write and run unit tests](unittest.md).

## Deploy to Emulator

### Strip debug symbols

```bash
NDK_STRIP=$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip)

$NDK_STRIP -o /tmp/seekdb build_android_release/src/observer/seekdb
```

macOS `strip` cannot process ELF binaries -- you must use the NDK strip.

### Push to emulator

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
