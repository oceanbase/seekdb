# Install the toolchain

seekdb uses CMake, Rust, and a repository-managed compiler and dependency set. Install Rust and the host tools below, then let `./build.sh release --init` prepare the pinned C/C++ dependencies under `deps/3rd`.

## Rust

Install Rust with [rustup](https://rustup.rs/). On Linux, macOS, or WSL, run the official installer and load Cargo's environment into the current shell:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

On Windows, download and run `rustup-init.exe` from the [official Rust installation page](https://www.rust-lang.org/tools/install), then open a new PowerShell window so `%USERPROFILE%\.cargo\bin` is available in `PATH`.

The repository pins Rust `1.97.1`, includes the `clippy` component, and uses these targets:

```text
x86_64-unknown-linux-gnu
x86_64-pc-windows-gnu
```

The CMake build invokes Cargo from the Rust workspace when compiling the `sql-nio` library. Rustup then reads `rust/rust-toolchain.toml` and installs the pinned toolchain, components, and targets automatically. The first build may also download the locked Cargo dependencies; configure a crates.io mirror if the host cannot reach the default registry.

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
./build.sh release --init
```

Continue with [Build and run seekdb](build-and-run.md).
