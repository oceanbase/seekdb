# Install the toolchain

seekdb uses Bazel, Rust, and a repository-managed compiler and dependency set. Install Bazel and Rust first, then install the host tools below and let `./bazel.py deps init` prepare the pinned C/C++ dependencies under `deps/3rd`.

## Bazel and Rust

Install a `bazel` executable through [Bazelisk](https://github.com/bazelbuild/bazelisk) or install Bazel directly. Bazelisk reads the repository's `.bazelversion` file and selects the required Bazel `8.2.1` release automatically. `bazel.py` does not download Bazel; it only locates and validates the executable in `PATH` (or the path passed with `--bazel`).

Install Rust with [rustup](https://rustup.rs/). The repository pins Rust `1.97.1`, includes the `clippy` component, and uses these targets:

```text
x86_64-unknown-linux-gnu
x86_64-pc-windows-gnu
```

After installing rustup, enter the Rust workspace once so rustup reads `rust/rust-toolchain.toml` and installs the pinned toolchain and targets:

```bash
cd rust
rustup show active-toolchain
cargo --version
rustc --version
cd ..
```

The Bazel Rust action invokes Cargo directly, so `cargo` and `rustc` must be available in `PATH`. The first build may also download the locked Cargo dependencies; configure a crates.io mirror if the host cannot reach the default registry.

## Host detection and supported architectures

The current dependency initialization script recognizes the following host families. Recognition means that a dependency profile can be selected; it does not by itself guarantee that every generated package is certified for production on that host.

| Architecture | Recognized Linux families |
| --- | --- |
| x86_64 | RHEL, CentOS, AlmaLinux, Rocky Linux, Alibaba Cloud Linux/AliOS, Anolis OS, TencentOS, Ubuntu, Debian, Fedora, Kylin, openEuler, openSUSE Leap, SLES, and UOS |
| aarch64 | RHEL, CentOS, AlmaLinux, Rocky Linux, Alibaba Cloud Linux/AliOS, Anolis OS, Ubuntu, Debian, Kylin, and openEuler |

Do not infer aarch64 support for Fedora, openSUSE/SLES, UOS, or TencentOS from the x86_64 list; those hosts are not selected by the current aarch64 dependency branch.

The compatibility build also recognizes macOS 13 or later on both `arm64` and `x86_64`. Apple Silicon is the primary tested development platform; accepting an Intel host in the build scripts does not constitute a production-support guarantee.

Windows 11 x64 uses `build.ps1` and a separate dependency flow.

## Linux host packages

### RHEL-compatible systems

```bash
sudo yum install git wget rpm-build rpm2cpio cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian-compatible systems

```bash
sudo apt-get update
sudo apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

Ubuntu 24.04 and Debian 13 use the time64 libaio package:

```bash
sudo apt-get install libaio1t64
```

### SUSE-compatible systems

```bash
sudo zypper install git wget rpm cpio make glibc-devel binutils m4 python3
```

## macOS host packages

```bash
brew install git cmake pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

See [Homebrew optimization](homebrew.md) if a mirror is required.

## Windows host packages

Install Python 3 and Visual Studio 2022 Build Tools with the **Desktop development with C++** workload. Then initialize the repository-managed CMake, Ninja, LLVM, win_flex_bison, OpenSSL, and third-party libraries:

```powershell
.\build.ps1 init
```

.NET 8 and WiX v4 are optional and are required only for the configurator and MSI packaging path.

## Verify the setup

Return to the repository root and run:

```bash
source ~/.bashrc
bazel --version
cd rust
rustup show active-toolchain
cargo --version
rustc --version
cd ..
./bazel.py deps init
```

`bazel --version` must match `.bazelversion`; `bazel.py` rejects a mismatched version.

Continue with [Build and run seekdb](build-and-run.md).
