# Install the toolchain

seekdb uses Bazel, Rust, and a repository-managed compiler and dependency set. Install the host tools below, then run `./build.py init` to prepare Rust and the pinned C/C++ dependencies under `deps/3rd`.

## Rust

`./build.py init` installs rustup if needed, then prepares the toolchain, components, and targets declared in `rust-toolchain.toml`. The repository pins Rust `1.98.1`. Run `init` before the first build and after changing the toolchain file. Downloads and installation logs are shown in the terminal; `build` reports a missing toolchain and asks you to run `init`.

Native init requires only the host Rust standard library and clippy. The Android, iOS device, and iOS simulator init options install their Rust targets on demand. CI installs the Windows GNU target explicitly before its cross-platform check. Host and cross targets use the same pinned toolchain.

Rustup downloads use [RsProxy](https://rsproxy.cn/) by default. Set `RUSTUP_DIST_SERVER` or `RUSTUP_UPDATE_ROOT` before invoking `build.py` to use another source. `RUSTUP_INIT_URL` can override the rustup installer URL. These defaults apply only to the build process and its children; the script does not edit shell profiles or change the default Rust toolchain.

Existing `CARGO_HOME` and `RUSTUP_HOME` settings are preserved. Without overrides, rustup uses its standard user directories. `build.py` adds the Cargo binary directory to its own `PATH`; to run Cargo directly in another shell, add that directory to your shell's `PATH`.

The build script downloads locked Cargo dependencies through the RsProxy sparse registry, overriding the user's crates.io source selection for this step. Downloads reuse the existing Cargo cache (`CARGO_HOME`, or `~/.cargo` by default).

## Host detection and supported architectures

The current dependency initialization script recognizes the following host families. Recognition means that a dependency profile can be selected; it does not by itself guarantee that every generated package is certified for production on that host.

| Architecture | Recognized Linux families |
| --- | --- |
| x86_64 | RHEL, CentOS, AlmaLinux, Rocky Linux, Alibaba Cloud Linux/AliOS, Anolis OS, TencentOS, Ubuntu, Debian, Fedora, Kylin, openEuler, openSUSE Leap, SLES, and UOS |
| aarch64 | RHEL, CentOS, AlmaLinux, Rocky Linux, Alibaba Cloud Linux/AliOS, Anolis OS, Ubuntu, Debian, Kylin, and openEuler |

Do not infer aarch64 support for Fedora, openSUSE/SLES, UOS, or TencentOS from the x86_64 list; those hosts are not selected by the current aarch64 dependency branch.

The compatibility build also recognizes macOS 13 or later on both `arm64` and `x86_64`. Apple Silicon is the primary tested development platform; accepting an Intel host in the build scripts does not constitute a production-support guarantee.

Windows 11 x64 uses `python build.py`; `init` selects the Windows dependency manifest.

## Linux host packages

### RHEL-compatible systems

```bash
sudo yum install git wget curl rpm-build rpm2cpio cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian-compatible systems

```bash
sudo apt-get update
sudo apt-get install git wget curl rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

Ubuntu 24.04 and Debian 13 use the time64 libaio package:

```bash
sudo apt-get install libaio1t64
```

### SUSE-compatible systems

```bash
sudo zypper install git wget curl rpm cpio make glibc-devel binutils m4 python3
```

## macOS host packages

```bash
brew install git pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

See [Homebrew optimization](homebrew.md) if a mirror is required.

## Windows host packages

Install Python 3 and Visual Studio 2022 Build Tools with the **Desktop development with C++** workload. The project dependency SDK includes LLVM 18.1.8; no separate LLVM installation is needed.

Open **x64 Native Tools Command Prompt for VS 2022** in the repository root and initialize the parser tools and dependency SDK:

```bat
python build.py init
python build.py build seekdb
```

`build.py` selects clang-cl from `deps\3rd\tools\llvm18`. The executable is `build\bin\src\observer\seekdb\seekdb.exe`. The build retains `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` to bypass the compiler version check in newer MSVC standard library headers, for both project C++ and Cargo's C++ bridges.

The DLLs required by the executable are emitted in the same directory, including the MSVC runtime. Packaging consumes this build output; it does not search the dependency SDK for server DLLs.

.NET 8 and WiX v4 are optional and are required only for the configurator and MSI packaging path.

## Verify the setup

Return to the repository root and run:

```bash
./build.py init
./build.py version
```

Continue with [Build and run seekdb](build-and-run.md).
