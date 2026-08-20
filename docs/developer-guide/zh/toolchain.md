# 安装工具链

seekdb 使用 Bazel、Rust 以及仓库管理的编译器和依赖集合。先安装 Bazel、Rust 和少量宿主机工具，再由 `./bazel.py deps init` 将固定版本的 C/C++ 构建依赖准备到 `deps/3rd`。

## Bazel 和 Rust

通过 [Bazelisk](https://github.com/bazelbuild/bazelisk) 安装 `bazel`，或者直接安装 Bazel。Bazelisk 会读取仓库中的 `.bazelversion`，自动选择所需的 Bazel `8.2.1` 版本。`bazel.py` 不会下载 Bazel，只会查找并校验 `PATH` 中的可执行文件（也可以通过 `--bazel` 指定路径）。

通过 [rustup](https://rustup.rs/) 安装 Rust。仓库固定使用 Rust `1.97.1`，需要 `clippy` 组件，并声明以下目标：

```text
x86_64-unknown-linux-gnu
x86_64-pc-windows-gnu
```

安装 rustup 后，先进入 Rust 工作区，让 rustup 读取 `rust/rust-toolchain.toml` 并安装固定版本的工具链和目标：

```bash
cd rust
rustup show active-toolchain
cargo --version
rustc --version
cd ..
```

Bazel 的 Rust 构建动作会直接调用 Cargo，因此 `cargo` 和 `rustc` 必须位于 `PATH` 中。首次构建还可能下载 Cargo 的锁定依赖；如果主机无法访问默认 registry，请配置 crates.io 镜像。

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
sudo yum install git wget rpm-build rpm2cpio cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

### Debian 兼容系统

```bash
sudo apt-get update
sudo apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4 file python3
```

Ubuntu 24.04 和 Debian 13 使用 time64 版本的 libaio：

```bash
sudo apt-get install libaio1t64
```

### SUSE 兼容系统

```bash
sudo zypper install git wget rpm cpio make glibc-devel binutils m4 python3
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
source ~/.bashrc
bazel --version
cd rust
rustup show active-toolchain
cargo --version
rustc --version
cd ..
./bazel.py deps init
```

`bazel --version` 必须与 `.bazelversion` 一致；版本不匹配时 `bazel.py` 会拒绝执行。

然后继续阅读[获取代码、编译并运行 seekdb](build-and-run.md)。
