# Building and Running SeekDB on Android

Cross-compile SeekDB for Android arm64-v8a using the NDK toolchain, then deploy and run on an emulator or device.

## Prerequisites

- Android NDK 26.x installed (default: `~/Library/Android/sdk/ndk/26.3.11579264`)
- Android emulator running arm64-v8a (API 28+), or a physical device
- Dependencies built via [ob-deps](https://github.com/nicai-inc/ob-deps) `ndk/build_all.sh`
- `adb` available on PATH
- `mysql` client (for connecting after launch)

Set `ANDROID_NDK_HOME` if your NDK is in a non-default location:

```bash
export ANDROID_NDK_HOME=$HOME/Library/Android/sdk/ndk/26.3.11579264
```

## Build

### 1. Initialize dependencies

```bash
./build.sh release --android --init
```

This runs `deps/init/android_deps_install.sh` which extracts pre-built NDK
dependency tarballs into `deps/3rd/`.

### 2. Configure and build
To build only the observer binary:

```bash
cd build_android_release
make seekdb -j$(nproc)
```

### 3. Build unit tests (optional)

A combined `all_tests` binary includes all unit tests in a single executable:

```bash
cd build_android_release
make all_tests
```

## Deploy to Emulator

### Strip debug symbols
```bash
NDK_STRIP=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip

$NDK_STRIP -o /tmp/seekdb build_android_release/src/observer/seekdb
```

macOS `strip` cannot process ELF binaries -- you must use the NDK strip.

### Push to emulator

```bash
adb push /tmp/seekdb /data/local/tmp/seekdb
adb shell chmod +x /data/local/tmp/seekdb
```

## Launch SeekDB

### Start the server

```bash
adb shell "mkdir -p /data/local/tmp/seekdb_data"
adb shell "/data/local/tmp/seekdb --nodaemon \
  --base-dir /data/local/tmp/seekdb_data \
  --parameter memory_limit=4G \
  --parameter datafile_size=2G \
  --parameter datafile_maxsize=4G \
  --parameter log_disk_size=2G \
  --parameter system_memory=1G \
  --log-level INFO"
```

The `memory_limit` and related parameters are required. Without them, SeekDB
auto-calculates resource requirements based on system memory, which can exceed
what the Android kernel allows and cause `OB_RESOURCE_UNIT_VALUE_INVALID` at
startup.

### Forward ports

In a separate terminal:

```bash
adb forward tcp:2881 tcp:2881   # MySQL protocol
adb forward tcp:2882 tcp:2882   # RPC
```

### Connect

```bash
mysql -h 127.0.0.1 -P 2881 -u root
```

```sql
SELECT 1;
```

### Check logs

```bash
adb shell "tail -100 /data/local/tmp/seekdb_data/log/seekdb.log"
```

### Stop the server

```bash
adb shell "kill \$(pidof seekdb)"
```