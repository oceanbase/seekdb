# libseekdb package

Portable C library build of libseekdb for Linux (x64/arm64), macOS (arm64), and Windows (x64). Output is a zip containing `seekdb.h` and `libseekdb.so` (Linux), `libseekdb.dylib` (macOS), or `seekdb.dll` / `seekdb.lib` (Windows), suitable for standalone use.

## Build

```bash
./libseekdb-build.sh
```

On Windows (after configuring and building target `libseekdb`, e.g. `.\build.ps1 release --ninja --target libseekdb -DBUILD_EMBED_MODE=ON`):

```powershell
cd package\libseekdb
.\libseekdb-build.ps1
```

Output: `libseekdb-<os>-<arch>.zip` is created in this directory. Arch is `x64` (for x86_64) or `arm64`, e.g. `libseekdb-linux-x64.zip`, `libseekdb-linux-arm64.zip`, `libseekdb-darwin-arm64.zip`, `libseekdb-windows-x64.zip`.

### Reference build environments (CI)

The supported systems and environments are defined by the GitHub Actions workflow [`.github/workflows/build-libseekdb.yml`](../../.github/workflows/build-libseekdb.yml). The workflow builds on push/PR and optionally uploads zips to S3 when **DESTINATION_TARGET_PATH** (e.g. `s3://bucket/libseekdb/<sha>`) or **AWS_S3_BUCKET** and AWS credentials are configured.

| Platform    | Zip name                   | Runner / container                                   | Deps profile              |
| ----------- | -------------------------- | ---------------------------------------------------- | ------------------------- |
| Linux x64   | libseekdb-linux-x64.zip    | ubuntu-22.04 + quay.io/pypa/manylinux2014_x86_64     | oceanbase.el7.x86_64.deps |
| Linux arm64 | libseekdb-linux-arm64.zip  | ubuntu-22.04-arm + quay.io/pypa/manylinux2014_aarch64 | oceanbase.el7.aarch64.deps |
| macOS arm64 | libseekdb-darwin-arm64.zip | macos-14 (native)                                    | oceanbase.macos.arm64.deps |
| Windows x64 | libseekdb-windows-x64.zip  | windows-2022 (native)                                | oceanbase.windows.x86_64.deps |

Use these systems and deps as the standard when building or consuming libseekdb.

### Linux glibc compatibility

CI Linux builds use **pypa/manylinux2014** (CentOS 7–based), which ships **glibc 2.17**. The prebuilt `libseekdb.so` therefore requires **GLIBC_2.17** or newer on the target system, **including CentOS 7**.

- **Supported (glibc ≥ 2.17)**: CentOS 7 / RHEL 7, CentOS 8 / RHEL 8, AlmaLinux 7/8, Rocky Linux 8/9, Ubuntu 18.04+, Debian 10+, Fedora 25+, and most distros from about 2014 onward. This covers current and legacy Linux environments, including CentOS 7.
- **Not supported (glibc &lt; 2.17)**: CentOS 6 (2.12) and older distros. For these, build libseekdb locally on the target system.

To check your system: `ldd --version` or `getconf GNU_LIBC_VERSION`.

### macOS compatibility

CI macOS builds use **macOS 14** runners and set **CMAKE_OSX_DEPLOYMENT_TARGET=11.0**, so the prebuilt `libseekdb.dylib` runs on **macOS 11 (Big Sur) and later** (12, 13, 14, 15). Setting the deployment target to 11.0 allows use on most current and recent macOS versions.

### CI validation (packed zip smoke)

After `libseekdb-build.sh`, CI runs `test-packed-artifact-smoke.sh` on the zip. It:

1. Unpacks the zip into a temp directory (`libseekdb.dylib` + `libs/` on macOS).
2. Ad-hoc signs dylibs (same as seekdb-js `build:package`).
3. Builds `seekdb.node` **into that directory** with `@loader_path` (`smoke-loader/binding.gyp`).
4. Runs `smoke-loader/smoke-vsag.js` (VECTOR INDEX `lib=vsag` + `DBMS_HYBRID_SEARCH.SEARCH`).

Layout matches **seekdb-js** (`pkgs/js-bindings/seekdb.node` + `@loader_path/libseekdb.dylib` + `libs/`).  
The old smoke used `nodejs_napi` linked to `build_release` via `@rpath`, which could pass while seekdb-js failed.

On **macOS CI**, when `SEEKDB_JS_ROOT` is set, the same script also runs `test-packed-artifact-smoke-js.sh` (checks out [seekdb-js](https://github.com/oceanbase/seekdb-js), rebuilds bindings, runs embedded vitest `sparseEmbedding`). That path reproduces `vsag::VsagException` on bad darwin zips; the N-API-only smoke can still pass.

Locally:

```bash
cd package/libseekdb
./test-packed-artifact-smoke.sh libseekdb-darwin-arm64.zip

# Full seekdb-js reproduction (requires pnpm install in seekdb-js once):
SEEKDB_JS_ROOT=../seekdb-js ./test-packed-artifact-smoke-js.sh libseekdb-darwin-arm64.zip
```

### Pack debugging (local vs CI / S3)

Each zip may include `pack-metadata.json` (uname, sha256, `otool -L`, CI env).

Compare two artifacts:

```bash
./diagnose-packed-artifact.sh \
  package/libseekdb/libseekdb-darwin-arm64.zip \
  'https://oceanbase-seekdb-builds.s3.ap-southeast-1.amazonaws.com/libseekdb/all_commits/<sha>/libseekdb-darwin-arm64.zip'
```

### macOS CI vs local dev builds (likely causes of divergent zips)

| Factor | GitHub Actions (macos-14) | Typical local Mac |
| ------ | ------------------------- | ----------------- |
| Runner / OS | `macos-14`, `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` | Host SDK, often no deployment target |
| Dependencies | `install-macos-brew-deps.sh` (`thrift@0.22`, …) | `brew install thrift` may pull 0.23+ |
| Compile | `-DOB_USE_CCACHE=ON`, cached `deps/3rd` | Local ccache / incremental `build.sh` |
| Pack input | Bundled dylib from CI `build_release` | Bundled from local `build_release` |
| Codesign | Optional Developer ID + entitlements (repo secrets) | Often ad-hoc only |

If `diagnose-packed-artifact.sh` shows a **different main library sha256** for the same source commit, the bug is in the **CI compile**, not dylibbundler. If only `libs/*` differ, check **dylibbundler / brew** versions. If smoke fails only on the CI zip, use metadata + diagnose output in the seekdb issue.

**ccache:** macOS/Linux CI ccache keys include `COMMIT_SHA` so object files from other commits are not restored into the same cache slot.

**seekdb-js:** [embedded CI](https://github.com/oceanbase/seekdb-js/actions) passes on **Linux** (`libseekdb-linux-*.zip`). A bad **darwin-arm64** S3 zip can fail macOS embedded tests while Linux stays green—use the smoke script on the macOS zip before publishing.

## Package contents and standalone distribution

Zip layout:

```
seekdb.h           # C API header
libseekdb.dylib    # Main library (macOS) or libseekdb.so (Linux)
seekdb.dll         # Main library (Windows)
seekdb.lib         # Import library for MSVC-style linking (Windows)
libs/              # Dependency dylibs (macOS only; collected by dylibbundler)
  *.dylib
```

- **Standalone distribution**: After extraction, the package can be used by other projects without this repo or the build environment.
- **macOS**: The main library and its dependencies use relative paths (`@loader_path/libs`). Unzip to any directory and keep the main library and `libs/` at the same level so they load correctly.
- **Linux**: Usually has no extra dependencies or relies on system libraries; unzip and use as-is.

### How to use (standalone)

1. Unzip to a target directory, e.g. `/opt/seekdb-sdk/`:
   ```
   /opt/seekdb-sdk/
     seekdb.h
     libseekdb.dylib
     libs/
       libfoo.dylib
       ...
   ```

2. Point your build at the header and library, e.g.:
   ```bash
   gcc -I/opt/seekdb-sdk -L/opt/seekdb-sdk -lseekdb ...
   ```
   At runtime on macOS, the main library loads dependencies from `libs/` in the same directory; you do not need to set `DYLD_LIBRARY_PATH`.

3. If the main library and the executable are in different directories (e.g. executable in `bin/`, library in `lib/`), ensure `libs/` exists next to the main library, or put the dependencies where the system can find them and set `DYLD_LIBRARY_PATH` (not recommended; prefer keeping the zip layout).

### Notes

- **OS and architecture**: The zip name reflects the build OS and CPU: `darwin-arm64`, `linux-x64`, `linux-arm64`, `windows-x64` (x64 = x86_64). Use the matching zip for the target environment. On Linux, the prebuilt .so requires glibc ≥ 2.17 (see [Linux glibc compatibility](#linux-glibc-compatibility)), including CentOS 7; on macOS, the prebuilt dylib is built on **macOS 14** with **minimum deployment target 11.0**, so it runs on **macOS 11 (Big Sur) and later** (12, 13, 14, 15). On Windows, CI builds on **windows-2022** with the MSVC-compatible Clang toolchain from deps (`deps/init/oceanbase.windows.x86_64.deps`).
- **Rebuilding**: After changing loader path or dependencies, run `libseekdb-build.sh` again to produce a new zip.
