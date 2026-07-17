# Install toolchain

To build OceanBase seekdb from source code, you need to install the C++ toolchain in your development environment first. If the C++ toolchain is not installed yet, you can follow the instructions in this document for installation.

## Supported OS

OceanBase makes strong assumption on the underlying operating systems. Not all the operating systems are supported.

Below is the OS compatibility list:

### Linux

| OS                  | Version               | Arch             | Compilable | Package Deployable | Compiled Binary Deployable | MYSQLTEST Passed |
| ------------------- | --------------------- | ---------------- | ---------- | ------------------ | -------------------------- | ---------------- |
| Alibaba Cloud Linux | 3                     | x86_64 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| CentOS              | 7 / 8 / 9             | x86_64 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| Debian              | 11 / 12 / 13          | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| Fedora              | 33                    | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| Kylin               | V10                   | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| openSUSE            | 15.2                  | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| OpenAnolis          | 8  / 23               | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| OpenEuler           | 22.03  / 24.03        | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| Rocky Linux         | 8  / 9                | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| StreamOS            | 3.4.8                 | x86_84 / aarch64 | Unknown    | Yes                | Yes                        | Unknown          |
| SUSE                | 15.2                  | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| Ubuntu              | 20.04 / 22.04 / 24.04 | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
| UOS                 | 20                    | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |

### macOS

| OS      | Version | Architecture       | Supported |
| ------- | ------- | ------------------ | --------- |
| macOS   | 13+     | Apple Silicon (M-series) | Yes       |

> **Note**:
>
> - macOS support is limited to **macOS 13 (Ventura) or later** with **Apple Silicon (M1/M2/M3/M4) chips only**. Intel-based Macs are not supported.

### Windows

| OS      | Version | Architecture | Supported |
| ------- | ------- | ------------ | --------- |
| Windows | 11      | x64          | Yes       |

> **Note**:
>
> - The compiler, build tools, and third-party libraries are downloaded into `deps/3rd` automatically by `build.ps1 init`; no manual installation is needed.
> - You still need to install Python 3.x and Visual Studio 2022 Build Tools yourself (see the installation steps below).

> **Note**:
>
> Other Linux distributions _may_ work. If you verify that OceanBase seekdb can compile and deploy on a distribution except ones listed above, feel free to submit a pull request to add it.

## Installation

The installation instructions vary among the operating systems and package managers you develop with. Below are the instructions for some popular environments:

### Fedora based

This includes CentOS, Fedora, OpenAnolis, RedHat, UOS, etc.

```shell
yum install git wget rpm* cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian based

This includes Debian, Ubuntu, etc.

```shell
apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

> **Note**: If you are using Ubuntu 24.04 or later, or Debian 13 or later, you also need to install `libaio1t64`:
>
> ```shell
> apt-get install libaio1t64
> ```

### SUSE based

This includes SUSE, openSUSE, etc.

```shell
zypper install git wget rpm cpio make glibc-devel binutils m4 python3
```

### macOS (Apple Silicon)

> **Note**: Only macOS 13+ with M-series chips (M1/M2/M3/M4) is supported.

```shell
brew install git cmake pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

> **Tip**: If Homebrew downloads are slow, see [Homebrew Optimization](homebrew.md) for mirror configuration.

### Windows

Applies to: Windows 11 x64.

**Required**:

- **Python 3.x**: download the installer from [python.org](https://www.python.org/downloads/windows/) and tick "Add Python to PATH" during install.
- **Visual Studio 2022 Build Tools**: download from the [Visual Studio downloads page](https://visualstudio.microsoft.com/downloads/) and select the **"Desktop development with C++"** workload. The workload installs the Windows 11 SDK, which provides `windows.h`, system import libraries, and `signtool.exe` — all required for Clang/LLD to produce native Windows binaries.

**Optional (only needed for packaging)**:

- **.NET 8 SDK**: required to build the seekdb Configurator setup wizard (WPF). The `package` step skips the wizard if it is missing.
- **WiX v4**: required to generate the MSI installer; falls back to a ZIP package when missing.
  ```powershell
  dotnet tool install --global wix
  ```

**Auto-downloaded (no manual install needed)**:

CMake, Ninja, LLVM 18, win_flex_bison, OpenSSL, and all third-party libraries are downloaded into `deps/3rd` when you run:

```powershell
.\build.ps1 init
```
