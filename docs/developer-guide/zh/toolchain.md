# 安装工具链

seekdb 使用 Bazel、Rust 以及仓库管理的编译器和依赖集合。先安装少量宿主机工具，再由 `./build.py init` 准备 Rust，并将固定版本的 C/C++ 构建依赖准备到 `deps/3rd`。

## Rust

`./build.py init` 会在缺少 rustup 时安装它，再准备 `rust-toolchain.toml` 声明的工具链、组件和目标。仓库固定使用 Rust `1.98.1`。首次编译及修改工具链配置后运行 `init`；下载和安装日志直接显示在终端中。`build` 阶段缺少工具链时会提示重新执行 `init`。

原生 init 只要求宿主平台的 Rust 标准库和 clippy。Android、iOS 真机和 iOS 模拟器目标分别由对应的 init 参数按需安装；Windows GNU 的交叉检查目标由 CI 检查步骤显式安装。交叉目标与宿主工具链使用同一份版本固定配置。

Rustup 下载默认使用 [RsProxy](https://rsproxy.cn/)。调用 `build.py` 前设置 `RUSTUP_DIST_SERVER` 或 `RUSTUP_UPDATE_ROOT` 可以覆盖默认源，`RUSTUP_INIT_URL` 可以指定 rustup 安装程序的 URL。这些默认值只作用于构建进程及其子进程，不修改 shell 配置，也不改变用户的默认 Rust 工具链。

已有的 `CARGO_HOME`、`RUSTUP_HOME` 设置会被保留；未设置时使用 rustup 的默认用户目录。`build.py` 会将 Cargo 可执行文件目录加入自己的 `PATH`。如需在其他终端直接运行 Cargo，将该目录加入对应 shell 的 `PATH`。

构建脚本通过 RsProxy 稀疏源下载锁定的 Cargo 依赖，在这一步覆盖用户的 crates.io 源选择。下载复用原有 Cargo 缓存（`CARGO_HOME`，默认 `~/.cargo`）。

## 宿主机检测与架构

当前依赖初始化脚本可以识别以下宿主机系列。能够选择依赖配置不代表该平台生成的所有软件包都已通过生产认证。

| 架构 | 可识别的 Linux 系列 |
| --- | --- |
| x86_64 | RHEL、CentOS、AlmaLinux、Rocky Linux、Alibaba Cloud Linux/AliOS、Anolis OS、TencentOS、Ubuntu、Debian、Fedora、Kylin、openEuler、openSUSE Leap、SLES 和 UOS |
| aarch64 | RHEL、CentOS、AlmaLinux、Rocky Linux、Alibaba Cloud Linux/AliOS、Anolis OS、Ubuntu、Debian、Kylin 和 openEuler |

不能根据 x86_64 列表推断 Fedora、openSUSE/SLES、UOS 或 TencentOS 的 aarch64 支持；当前 aarch64 依赖分支不会选择这些宿主机。

兼容构建还可以识别 macOS 13 或更高版本的 `arm64` 和 `x86_64`。Apple Silicon 是主要测试的开发平台；构建脚本接受 Intel 主机并不等同于生产支持承诺。

Windows 11 x64 使用 `python build.py`，`init` 会选择 Windows 依赖清单。

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
brew install git pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

需要使用镜像时，参见 [Homebrew 优化配置](homebrew.md)。

## Windows 宿主机依赖

安装 Python 3、Visual Studio 2022 Build Tools（选择 **使用 C++ 的桌面开发** 工作负载）。LLVM 18.1.8 随项目依赖 SDK 安装，无需单独安装 LLVM。

在仓库根目录打开 **x64 Native Tools Command Prompt for VS 2022**，初始化解析器工具和依赖 SDK：

```bat
python build.py init
python build.py build seekdb
```

`build.py` 固定选择 `deps\3rd\tools\llvm18` 中的 clang-cl，产物是 `build\bin\src\observer\seekdb\seekdb.exe`。构建沿用原有的 `_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH`，跳过新版 MSVC 标准库的编译器版本检查；该选项同时用于项目 C++ 和 Cargo 的 C++ 桥接代码。

可执行文件需要的 DLL（包括 MSVC 运行库）一起输出到同一目录。打包直接使用这份构建产物，无需再扫描依赖 SDK 查找服务端 DLL。

.NET 8 和 WiX v4 是可选依赖，仅配置器和 MSI 打包流程需要。

## 验证环境

回到仓库根目录执行：

```bash
./build.py init
./build.py version
```

然后继续阅读[获取代码、编译并运行 seekdb](build-and-run.md)。
