# Build seekdb for iOS

Cross-compile seekdb on an Apple Silicon Mac into `SeekDB.framework`, a dynamic framework that runs the engine inside an iOS app, for arm64 devices or the arm64 simulator. An iOS app cannot start another process, so apps embed the framework.

## Prerequisites

- Apple Silicon Mac
- Xcode with the iOS SDKs, at `/Applications/Xcode.app` or pointed to by `DEVELOPER_DIR`
- `rustup`, Autoconf, Automake 1.16.3 or later, Libtool, Gettext, and pkg-config on `PATH` (`brew install autoconf automake libtool gettext pkgconf`)
- About 10 GB of free disk space for the dependencies and one build directory

## Build

Initialize the dependencies, then build the arm64 device framework with Bazel:

```bash
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
./build.py init --ios
./build.py build --config=ios seekdb
```

`init --ios` prepares the host tools and Rust iOS target, and builds the base libraries into `deps/ios/iphoneos/devel`. Bazel builds the remaining pinned source dependencies through `//bazel/sdk_sources:source_sdk`. Use `build` directly for subsequent builds; initialization reuses dependencies that are still current. The project's `.bazelversion` pins Bazel, which `build.py` downloads on first use.

The arm64 simulator uses a separate dependency directory and configuration:

```bash
./build.py init --ios --simulator
./build.py build --config=ios-simulator seekdb
```

The minimum deployment version is iOS 18.0. Both configurations compile the engine directly with the Apple C++ toolchain and disable physical standby support.

The framework is generated at:

```text
build/bin/src/observer/framework/SeekDB.framework
```

To keep independent device and simulator builds, specify separate build directories:

```bash
./build.py build -o build_ios --config=ios seekdb
./build.py build -o build_ios_simulator --config=ios-simulator seekdb
```

Validate affected unit tests on a supported Linux host by following [Write and run unit tests](unittest.md).

## Framework contents

- `SeekDB`: the engine, its Rust libraries and third-party libraries linked into one arm64 dynamic library that depends only on system libraries
- `Headers/seekdb.h`: `seekdb_open`, `seekdb_last_open_error`, `seekdb_close` and `seekdb_connection_options`, with the same declarations as the desktop `seekdb.h`
- `Info.plist`: source revision, platform and minimum OS version
- `build-manifest.json`, `rust-Cargo.lock`, and `Licenses.tar`: linked archive hashes, dependency source records, and license notices

The device and simulator frameworks are not interchangeable. The framework is not signed for distribution; Xcode signs it with the app when it is added with **Embed & Sign**.

## Use it in an app

The framework only starts and stops the engine. Run SQL with a MySQL-compatible client library of your choice over the Unix socket that `seekdb_connection_options` returns, as user `root` with an empty password:

```c
#include <stdio.h>
#include <SeekDB/seekdb.h>

SeekdbHandle db = NULL;
const char *parameters[] = {"memory_budget", "1G", NULL};
if (seekdb_open(absolute_data_dir, parameters, &db) == SEEKDB_SUCCESS) {
  SeekdbConnectionOptions options;
  seekdb_connection_options(db, &options);
  /* Connect your MySQL client to the socket options.unix_socket as options.user. */
  /* seekdb_close releases the handle; the engine keeps running until the app exits. */
  seekdb_close(db);
} else {
  fprintf(stderr, "%s\n", seekdb_last_open_error());
}
```

- Pass an absolute directory inside the app container. A new database defaults to `memory_budget=1G`, `vector_memory_limit=128M`, `log_disk_size=2G` and `datafile_maxsize=20G`; key/value pairs override them when the database is created.
- TCP is not available: a non-zero `port` returns `SEEKDB_INVALID_ARGUMENT`.
- The first `seekdb_open` starts the engine, which then keeps running until the app process exits; `seekdb_close` only releases the handle. Opening the same directory again reuses the running engine, also after every handle has been closed. A different directory fails, and so does any open after the engine failed to start.
- The engine sets the process working directory to its data directory and leaves it there, so use absolute paths. Keep the framework loaded until the process exits.
