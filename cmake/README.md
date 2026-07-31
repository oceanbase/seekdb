# CMake compatibility build

CMake is a temporary production-build compatibility path while seekdb adopts
Bazel. Bazel remains the only authoritative module graph: CMake does not define
or enforce architectural dependencies.

## Maintained boundary

| Dimension | Supported |
| --- | --- |
| Configuration | `RelWithDebInfo`, `-O2` |
| Compilation | Unity only |
| Product | `seekdb` (`observer` alias on Unix) |
| Pretest runtime | `liboceanbase.so` on Linux |
| Tests | Linux farm pretest: 10 module binaries, 74 CTest shards |
| Linux | x86_64 and aarch64 |
| macOS | x86_64 and arm64 |
| Windows | x64 |
| Android | arm64-v8a, API 28 |

Unit-test workflows other than the Linux farm pretest, packaging, Debug,
non-Unity, errsim, sanity, coverage, PGO/LTO, performance presets, and IDE-only
configurations are outside this boundary. Use Bazel for those workflows.

## Commands

On Linux and macOS:

```bash
./build.sh release --init
make -C build_release -j80 seekdb

# Build and run the complete CMake pretest graph on Linux.
make -C build_release -j16 pretest
build_release/unittest/run_tests.sh -j8 --output-on-failure

# Android arm64-v8a
ANDROID_NDK_HOME=/path/to/ndk ./build.sh release --android --init --make -j16
```

On Windows x64:

```powershell
.\build.ps1 release --init --ninja -j 16
```

Farm jobs may reuse a separately built shared runtime instead of recompiling
the production objects:

```bash
./build.sh release --init -DOB_SO_CACHE=ON
cp /downloaded/liboceanbase.so build_release/src/observer/liboceanbase.so
make -C build_release/unittest -j16
build_release/unittest/run_tests.sh -j8 --output-on-failure
```

The CMake pretest graph reads each module's target name, Unity batch size,
isolated-source list, and shard count from its Bazel `BUILD.bazel`. CMake then
generates the same ordered Unity translation units and registers the module
binaries as CTest shards. Runtime data is copied below `build_release/unittest`
so the archived farm artifact remains self-contained.

The CMake build consumes the data-only source inventories/build definitions
used by Bazel. `tools/cmake/emit_bazel_source_inventory.py` translates native
production source membership, exact production Unity groups, and pretest
module metadata at configure time. Adding a native module source therefore
changes the owning Bazel data file only; an unsupported or duplicate inventory
entry makes CMake configuration fail.
Inner-table and system-package outputs are emitted into the build directory.
SQL and PL parsers temporarily retain their historical generator contract,
including generated files below their source directories; this is a CMake
compatibility detail and is not part of the Bazel module graph.

The CMake target graph deliberately uses broad source-tree include paths and
object aggregation to preserve the historical production build. It must not be
used as evidence that a module dependency is allowed. `./bazel.py build ...`
continues to apply the centralized architecture policy and per-target
visibility rules.
