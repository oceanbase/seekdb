# Build seekdb for iOS

Cross-compile seekdb on an Apple Silicon Mac into `SeekDB.framework`, a dynamic framework that runs the engine inside an iOS app, for arm64 devices or the arm64 simulator.

## Prerequisites

- Apple Silicon Mac
- Xcode with the iOS SDKs, at `/Applications/Xcode.app` or pointed to by `DEVELOPER_DIR`
- `rustup` on `PATH`
- About 10 GB of free disk space for the dependencies and one build directory

## Build

Use the iOS entry point. It initializes the dependencies, configures the build, and builds the framework:

```bash
./build.sh release --ios --init --make -j8               # arm64 device
./build.sh release --ios --simulator --init --make -j8   # arm64 simulator
```

`--init` runs `dep_create.sh` and `rustup toolchain install` as the Android build does, and also builds the iOS libraries from the pinned sources in `deps/ios-build` into `deps/ios/<sdk>/devel`; later runs reuse the libraries that are still current. Leave out `--init` once the dependencies exist. Without `--make`, `build.sh` only configures the build.

The framework is generated at:

```text
build_ios_release/framework/SeekDB.framework             # device
build_ios_simulator_release/framework/SeekDB.framework   # simulator
```

Pass CMake options before `--make`: `-DCMAKE_BUILD_TYPE=Debug` builds a Debug framework, and `-DDEP_DIR=/path` uses another iOS dependency directory. `./build.sh clean` removes both build directories.

The iOS CMake build does not provide an `all_tests` target. Validate affected unit tests on a supported Linux host by following [Write and run unit tests](unittest.md).

## Framework contents

- `SeekDB`: the engine, its Rust libraries and third-party libraries linked into one arm64 dynamic library that depends only on system libraries
- `Headers/seekdb.h`: `seekdb_open`, `seekdb_close` and `seekdb_connection_options`
- `Info.plist`, `build-manifest.json` (source revision, checksums, exported symbols), `rust-Cargo.lock` and `Licenses/`

The device and simulator frameworks are not interchangeable. The framework is not signed for distribution; Xcode signs it with the app when it is added with **Embed & Sign**.

## Use it in an app

The framework only starts and stops the engine. Run SQL with a MySQL-compatible client library of your choice over the Unix socket that `seekdb_connection_options` returns, as user `root` with an empty password:

```c
#include <stddef.h>
#include <SeekDB/seekdb.h>

SeekdbHandle db = NULL;
const char *parameters[] = {"memory_budget", "1G", NULL};
if (seekdb_open(absolute_data_dir, parameters, &db) == SEEKDB_SUCCESS) {
  SeekdbConnectionOptions options;
  seekdb_connection_options(db, &options);
  /* Connect your MySQL client to the socket options.endpoint as options.user. */
  /* Close the client's connections before the last seekdb_close. */
  seekdb_close(db);
}
```

- Pass an absolute directory inside the app container. A new database defaults to `memory_budget=1G`, `vector_memory_limit=128M`, `log_disk_size=2G` and `datafile_maxsize=20G`; key/value pairs override them when the database is created.
- TCP is not available: a non-zero `port` returns `SEEKDB_INVALID_ARGUMENT`.
- Opening the same directory again shares the running engine; a different directory fails. The engine can start only once per process: after the last `seekdb_close`, reopening requires a new app process.
- The engine changes the process working directory while it runs, so use absolute paths. Keep the framework loaded until the process exits.
