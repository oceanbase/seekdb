# 安装工具链

seekdb 使用 CMake、Rust 以及仓库管理的编译器和依赖集合。先安装 Rust 和少量宿主机工具，再由 `./build.sh release --init` 将固定版本的 C/C++ 构建依赖准备到 `deps/3rd`。

## Rust

通过 [rustup](https://rustup.rs/) 安装 Rust。在 Linux、macOS 或 WSL 上，执行官方安装脚本，并让 Cargo 环境在当前 shell 中生效：

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

> **中国大陆网络：** 如果官方源下载缓慢，可以在当前终端使用[清华大学 TUNA rustup 镜像](https://mirrors.tuna.tsinghua.edu.cn/help/rustup/)，然后执行安装命令：

```bash
export RUSTUP_DIST_SERVER=https://mirrors.tuna.tsinghua.edu.cn/rustup
export RUSTUP_UPDATE_ROOT=https://mirrors.tuna.tsinghua.edu.cn/rustup/rustup
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
```

上述环境变量也会让同一终端中后续的 rustup 工具链下载使用 TUNA 镜像。首次构建还需要下载 Cargo 依赖；如果 crates.io 访问缓慢，可以在 `$HOME/.cargo/config.toml` 中配置 [TUNA crates.io 稀疏索引](https://mirrors.tuna.tsinghua.edu.cn/help/crates.io-index/)：

```toml
[source.crates-io]
replace-with = "tuna"

[source.tuna]
registry = "sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/"

[registries.tuna]
index = "sparse+https://mirrors.tuna.tsinghua.edu.cn/crates.io-index/"
```

在 Windows 上，从 [Rust 官方安装页面](https://www.rust-lang.org/tools/install) 下载并运行 `rustup-init.exe`，然后重新打开 PowerShell，使 `%USERPROFILE%\.cargo\bin` 出现在 `PATH` 中。

仓库固定使用 Rust `1.97.1`，需要 `clippy` 组件，并声明以下目标：

```text
x86_64-unknown-linux-gnu
x86_64-pc-windows-gnu
```

CMake 编译 `sql-nio` 库时会从 Rust 工作区调用 Cargo，rustup 随后会自动读取 `rust/rust-toolchain.toml`，并安装固定版本的工具链、组件和目标。首次构建还可能下载 Cargo 的锁定依赖；如果主机无法访问默认 registry，请配置 crates.io 镜像。

## 宿主机检测与架构

当前依赖初始化脚本可以识别以下宿主机系列。能够选择依赖配置不代表该平台生成的所有软件包都已通过生产认证。

| 架构 | 可识别的 Linux 系列 |
| --- | --- |
| x86_64 | RHEL、CentOS、AlmaLinux、Rocky Linux、Alibaba Cloud Linux/AliOS、Anolis OS、TencentOS、Ubuntu、Debian、Fedora、Kylin、openEuler、openSUSE Leap、SLES 和 UOS |
| aarch64 | RHEL、CentOS、AlmaLinux、Rocky Linux、Alibaba Cloud Linux/AliOS、Anolis OS、Ubuntu、Debian、Kylin 和 openEuler |

不能根据 x86_64 列表推断 Fedora、openSUSE/SLES、UOS 或 TencentOS 的 aarch64 支持；当前 aarch64 依赖分支不会选择这些宿主机。

兼容构建还可以识别 macOS 13 或更高版本的 `arm64` 和 `x86_64`。Apple Silicon 是主要测试的开发平台；构建脚本接受 Intel 主机并不等同于生产支持承诺。

Windows 11 x64 使用 `build.ps1` 和独立的依赖流程。

## Linux 宿主机依赖

### RHEL 兼容系统

```bash
sudo yum install git wget curl rpm-build rpm2cpio cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian 兼容系统

```bash
sudo apt-get update
sudo apt-get install git wget curl rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

Ubuntu 24.04 和 Debian 13 使用 time64 版本的 libaio：

```bash
sudo apt-get install libaio1t64
```

### SUSE 兼容系统

```bash
sudo zypper install git wget curl rpm cpio make glibc-devel binutils m4 python3
```

## macOS 宿主机依赖

```bash
brew install git cmake pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

需要使用镜像时，参见 [Homebrew 优化配置](homebrew.md)。

## Windows 宿主机依赖

安装 Python 3 和 Visual Studio 2022 Build Tools，并选择 **使用 C++ 的桌面开发** 工作负载。随后初始化仓库管理的 CMake、Ninja、LLVM、win_flex_bison、OpenSSL 和第三方库：

```powershell
.\build.ps1 init
```

.NET 8 和 WiX v4 是可选依赖，仅配置器和 MSI 打包流程需要。

## 验证环境

回到仓库根目录执行：

```bash
./build.sh release --init
```

然后继续阅读[获取代码、编译并运行 seekdb](build-and-run.md)。
