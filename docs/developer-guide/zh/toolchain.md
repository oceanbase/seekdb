# 安装工具链

在编译 OceanBase seekdb 源码之前，需要先在开发环境中安装 C++ 工具链。本文档介绍如何在不同操作系统上安装所需的工具链。

## 概述

seekdb 是一个 C++ 项目，需要特定的编译工具链。请根据你的操作系统选择对应的安装方法。

## 相关文档

- [编译与运行](build-and-run.md) - 编译和运行 seekdb
- [IDE 配置](ide-settings.md) - 配置开发环境

## 支持的操作系统

OceanBase seekdb 并不支持所有的操作系统。

这是当前兼容的操作系统列表：

### Linux

| 操作系统             | 版本                  | 架构             | 是否兼容 | 安装包是否可部署 | 编译的二进制文件是否可部署 | 是否测试过 MYSQLTEST |
| ------------------- | --------------------- | ---------------- | -------- | ---------------- | -------------------------- | -------------------- |
| Alibaba Cloud Linux | 3                     | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| CentOS              | 7 / 8 / 9             | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| Debian              | 11 / 12 / 13          | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| Fedora              | 33                    | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| Kylin               | V10                   | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| openSUSE            | 15.2                  | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| OpenAnolis          | 8 / 23                | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| OpenEuler           | 22.03 / 24.03         | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| Rocky Linux         | 8 / 9                 | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| StreamOS            | 3.4.8                 | x86_64 / aarch64 | ❓     | ✅          | ✅          | ❓              |
| SUSE                | 15.2                  | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| Ubuntu              | 20.04 / 22.04 / 24.04 | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |
| UOS                 | 20                    | x86_64 / aarch64 | ✅     | ✅          | ✅          | ✅              |

### macOS

| 操作系统 | 版本 | 架构                      | 支持 |
| ------- | ---- | ------------------------- | ---- |
| macOS   | 13+  | Apple Silicon (M 系列芯片) | ✅   |

> **注意**：
>
> - macOS 仅支持 **macOS 13 (Ventura) 及以上版本**，且仅支持 **Apple Silicon (M1/M2/M3/M4) 芯片**。不支持 Intel 芯片的 Mac。

### Windows

| 操作系统 | 版本 | 架构 | 支持 |
| ------- | ---- | ---- | ---- |
| Windows | 11   | x64  | ✅   |

> **注意**：
>
> - Windows 平台的编译器、构建工具及第三方库均由 `build.ps1 init` 自动下载到 `deps/3rd`，无需手工安装。
> - 用户仍需自行准备 Python 3.x 以及 Visual Studio 2022 Build Tools（详见下方安装步骤）。

> **注意**:
>
> 其它的 Linux 发行版可能也可以工作。如果你验证了 OceanBase seekdb 可以在除了上面列出的发行版之外的发行版上编译和部署，请随时提交一个拉取请求来添加它。

## 安装步骤

根据你的操作系统，选择对应的安装方法：

### Fedora 系列系统

适用于：CentOS、Fedora、OpenAnolis、RedHat、UOS 等使用 `yum` 包管理器的系统。

```shell
yum install git wget rpm* cpio make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

> **注意**：如果没有权限执行 `yum`，请使用 `sudo yum ...`。

### Debian 系列系统

适用于：Debian、Ubuntu 等使用 `apt-get` 包管理器的系统。

```shell
apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4 python3
```

> **注意**：如果没有权限执行 `apt-get`，请使用 `sudo apt-get ...`。

### SUSE 系列系统

适用于：SUSE、openSUSE 等使用 `zypper` 包管理器的系统。

```shell
zypper install git wget rpm cpio make glibc-devel binutils m4 python3
```

> **注意**：如果没有权限执行 `zypper`，请使用 `sudo zypper ...`。

### macOS (Apple Silicon)

> **注意**：仅支持 macOS 13+ 且搭载 M 系列芯片 (M1/M2/M3/M4) 的 Mac。

```shell
brew install git cmake pkg-config openssl@3 ncurses googletest
brew install zstd lz4 utf8proc thrift re2 brotli
```

> **提示**：如果 Homebrew 下载速度较慢，请参阅 [Homebrew 优化配置](homebrew.md) 设置国内镜像加速。

### Windows

适用于：Windows 11 x64。

**必备依赖**：

- **Python 3.x**：从 [python.org](https://www.python.org/downloads/windows/) 下载安装，安装时勾选 "Add Python to PATH"。
- **Visual Studio 2022 Build Tools**：从 [Visual Studio 下载页](https://visualstudio.microsoft.com/zh-hans/downloads/) 获取 Build Tools，安装时勾选 **"使用 C++ 的桌面开发"** 工作负载。该负载会一并安装 Windows 11 SDK，提供 `windows.h`、系统导入库以及 `signtool.exe`，是 Clang/LLD 编译 Windows 原生二进制所必需的。

**可选依赖（仅打包时需要）**：

- **.NET 8 SDK**：用于构建 seekdb Configurator 安装向导（WPF）。缺失时 `package` 流程会跳过向导。
- **WiX v4**：用于生成 MSI 安装包，缺失时会回退到 ZIP 格式。
  ```powershell
  dotnet tool install --global wix
  ```

**自动下载（无需手工安装）**：

CMake、Ninja、LLVM 18、win_flex_bison、OpenSSL 以及全部第三方依赖会在执行下面命令时自动下载到 `deps/3rd`：

```powershell
.\build.ps1 init
```

## 验证安装

安装完成后，可以通过以下命令验证工具链是否正确安装：

```shell
# 检查编译器
gcc --version
g++ --version

# 检查构建工具
make --version
```

## 下一步

工具链安装完成后，可以继续：

- [编译与运行](build-and-run.md) - 编译 seekdb 项目
- [IDE 配置](ide-settings.md) - 配置开发环境以便更好地阅读代码
