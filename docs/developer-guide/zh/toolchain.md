# 安装工具链

在编译 OceanBase seekdb 源码之前，需要先在开发环境中安装 C++ 工具链。本文档介绍如何在不同操作系统上安装所需的工具链。

## 概述

seekdb 是一个 C++ 项目，需要特定的编译工具链。请根据你的操作系统选择对应的安装方法。

## 相关文档

- [编译与运行](build-and-run.md) - 编译和运行 seekdb
- [IDE 配置](ide-settings.md) - 配置开发环境

## 支持的操作系统

OceanBase seekdb 并不支持所有的操作系统，特别是 Windows 和 Mac OS X。

这是当前兼容的操作系统列表：

| 操作系统             | 版本        | 架构   | 兼容性 | 安装包部署 | 二进制部署 | MySQLTest 测试 |
| ------------------- | ----------- | ------ | ------ | ---------- | ---------- | -------------- |
| Alibaba Cloud Linux | 2.1903      | x86_64 | ✅     | ✅          | ✅          | ✅              |
| CentOS              | 8.3         | x86_64 | ✅     | ✅          | ✅          | ✅              |
| Debian              | 9.8 / 10.9  | x86_64 | ✅     | ✅          | ✅          | ✅              |
| Fedora              | 33          | x86_64 | ✅     | ✅          | ✅          | ✅              |
| openSUSE            | 15.2        | x86_64 | ✅     | ✅          | ✅          | ✅              |
| OpenAnolis          | 8.2         | x86_64 | ✅     | ✅          | ✅          | ✅              |
| StreamOS            | 3.4.8       | x86_64 | ❓     | ✅          | ✅          | ❓              |
| SUSE                | 15.2        | x86_64 | ✅     | ✅          | ✅          | ✅              |
| Ubuntu              | 20.04/22.04 | x86_64 | ✅     | ✅          | ✅          | ✅              |

> **注意**：表格中的 `x86_84` 应为 `x86_64`。

> **注意**:
>
> 其它的 Linux 发行版可能也可以工作。如果你验证了 OceanBase 可以在除了上面列出的发行版之外的发行版上编译和部署，请随时提交一个拉取请求来添加它。

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
