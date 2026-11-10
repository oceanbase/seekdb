# Install the toolchain

seekdb uses Bazel, Rust, and a repository-managed compiler and dependency set. Install Rust and the host tools below, then let `./build.py init` prepare the pinned C/C++ dependencies under `deps/3rd`.

## Rust

Install Rust with [rustup](https://rustup.rs/). On Linux, macOS, or WSL, run the official installer and load Cargo's environment into the current shell:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

On Windows, download and run `rustup-init.exe` from the [official Rust installation page](https://www.rust-lang.org/tools/install), then open a new PowerShell window so `%USERPROFILE%\.cargo\bin` is available in `PATH`.

The repository pins Rust `1.98.1` in `rust/rust-toolchain.toml`. Native builds use rustup to locate that version. Rustup installs it automatically if missing, unless `RUSTUP_AUTO_INSTALL=0` is set; no separate installation command is required.

The toolchain file also declares `clippy` and the following targets for Rust checks and cross-compilation. A native build does not require all of them:

```text
x86_64-unknown-linux-gnu
x86_64-pc-windows-gnu
aarch64-linux-android
aarch64-apple-ios
aarch64-apple-ios-sim
```

To preinstall the toolchain, components, and targets declared in that file, optionally run `rustup toolchain install --no-self-update` from the `rust/` directory. The `--no-self-update` flag only disables updating rustup itself.

The build script downloads locked Cargo dependencies through the [RsProxy sparse registry](https://rsproxy.cn/), overriding the user's crates.io source selection for this step. No global mirror configuration is needed; downloads still use the existing Cargo cache (`$CARGO_HOME`, or `~/.cargo` by default).

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
