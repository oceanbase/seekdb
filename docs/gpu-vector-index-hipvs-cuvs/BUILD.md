# Building seekdb from source inside the hipVS (ROCm/gfx1100) image

To add a GPU (cuVS/hipVS) vector-index backend, seekdb must be built from source
alongside hipVS. This is a **verified** recipe for building seekdb on Ubuntu
24.04 inside the hipVS runtime image (which bundles ROCm 7.2.4 + hipVS/libcuvs_c).
Result: `build_release/src/observer/seekdb` (~1.3 GB), runs `OceanBase seekdb 1.4.0.0`.

## Environment
- Base image: hipVS runtime (Ubuntu 24.04.4, ROCm 7.2.4, /opt/hipvs = libcuvs_c + headers).
- seekdb build uses its OWN bundled toolchain (obdevtools clang-17 / gcc-12.3), fetched by `./build.sh init`.

## Steps (run as root in the container, repo at /work/seekdb)

1. System build prerequisites (Ubuntu):
   ```bash
   apt-get update
   apt-get install -y --no-install-recommends rpm cpio bison flex libaio1t64 libaio-dev
   ```
   - `rpm`+`cpio`: init downloads el8/el9 **RPM** deps and unpacks with rpm2cpio|cpio.
   - `libaio1t64`/`libaio-dev`: Ubuntu 24.04 time_t64 transition; the observer links `-l:libaio.so.1t64`.

2. Dependency init (`./build.sh init`) fetches the toolchain + libs (gcc 12.3, llvm 17, **VSAG 1.1.0**, boost, gtest, grpc, s2geometry, jemalloc, ...) from `mirrors.oceanbase.com` (~7 GB into `deps/3rd/`).
   - **Environment-specific workaround**: this environment cannot reach the internal host
     `ob-yum.oceanbase-dev.com` (obshell/ob-deploy) and some client RPMs 404 on the mirror.
     Those are client/deploy tools, NOT build deps. They are commented out (`#L2SKIP`) in
     `deps/init/oceanbase.el9.x86_64.deps` (obshell, ob-deploy, obclient, libobclient).
     Drop this change for an environment that can reach the internal repos.
   ```bash
   ./build.sh init
   ```

3. Modern Rust (the `sql-nio` component pulls a crate requiring Rust **edition2024**;
   distro cargo 1.75 is too old). Install via rustup and make the CMake-cached cargo path valid:
   ```bash
   curl -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal
   . "$HOME/.cargo/env"
   ln -sf "$HOME/.cargo/bin/cargo" /bin/cargo   # CMakeCache caches CARGO=/bin/cargo
   ```

4. Configure + build the server target:
   ```bash
   ./build.sh release --make -j64
   ```

## Output
- `build_release/src/observer/seekdb` — the server binary (ELF, ~1.3 GB).
- Verify: `build_release/src/observer/seekdb --help` prints `OceanBase seekdb 1.4.0.0`.

## Notes
- Compiler: bundled clang-17 (`deps/3rd/usr/local/oceanbase/devtools/bin/clang++-17`), C++20.
- VSAG headers (seekdb's CPU vector engine): `deps/3rd/usr/local/oceanbase/deps/devel/include/vsag/`.
- Next: link `libcuvs_c` and add a cuVS backend behind `src/oblib/lib/vector/ob_vsag_adaptor` (see README.md).
