# 安装工具链

在编译 OceanBase SeekDB 源码之前，需要先在开发环境中安装C++工具链。如果你的开发环境中还没有安装C++工具链，可以按照本文档中的指导进行安装。

## 支持的操作系统

OceanBase SeekDB 并不支持所有的操作系统，特别是 Windows 和 Mac OS X。

这是当前兼容的操作系统列表：

| 操作系统                  | 版本               | 架构   | 是否兼容 | 安装包是否可部署 | 编译的二进制文件是否可部署 | 是否测试过 MYSQLTEST |
| ------------------- | --------------------- | ------ | ---------- | ------------------ | -------------------------- | ---------------- |
| Alibaba Cloud Linux | 2.1903                | x86_64 | Yes        | Yes                | Yes                        | Yes              |
| CentOS              | 8.3                   | x86_64 | Yes        | Yes                | Yes                        | Yes              |
| Debian              | 9.8 / 10.9            | x86_84 | Yes        | Yes                | Yes                        | Yes              |
| Fedora              | 33                    | x86_84 | Yes        | Yes                | Yes                        | Yes              |
| openSUSE            | 15.2                  | x86_84 | Yes        | Yes                | Yes                        | Yes              |
| OpenAnolis          | 8.2                   | x86_84 | Yes        | Yes                | Yes                        | Yes              |
| StreamOS            | 3.4.8                 | x86_84 | Unknown    | Yes                | Yes                        | Unknown          |
| SUSE                | 15.2                  | x86_84 | Yes        | Yes                | Yes                        | Yes              |
| Ubuntu              | 20.04 / 22.04         | x86_84 | Yes        | Yes                | Yes                        | Yes              |

> **注意**:
>
> 其它的 Linux 发行版可能也可以工作。如果你验证了 OceanBase 可以在除了上面列出的发行版之外的发行版上编译和部署，请随时提交一个拉取请求来添加它。

## 安装

这个安装指导因操作系统和包管理器的不同而有所不同。以下是一些流行环境的安装指导：

### Fedora 系统

包括 CentOS, Fedora, OpenAnolis, RedHat, UOS 等等。

```shell
yum install git wget rpm* cpio make glibc-devel glibc-headers binutils m4 libtool libaio
```

### Debian 系统

包括 Debian, Ubuntu 等等。

```shell
apt-get install git wget rpm rpm2cpio cpio make build-essential binutils m4
```

### SUSE 系统

包括 SUSE, openSUSE 等等。

```shell
zypper install git wget rpm cpio make glibc-devel binutils m4
```
