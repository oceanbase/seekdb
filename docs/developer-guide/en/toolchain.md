# Install toolchain

To build OceanBase seekdb from source code, you need to install the C++ toolchain in your development environment first. If the C++ toolchain is not installed yet, you can follow the instructions in this document for installation.

## Supported OS

OceanBase makes strong assumption on the underlying operating systems. Not all the operating systems are supported; especially, Windows and Mac OS X are not supported yet.

Below is the OS compatibility list:

 | Alibaba Cloud Linux | 3                     | x86_64 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | CentOS              | 7 / 8 / 9             | x86_64 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | Debian              | 11 / 12 / 13          | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | Fedora              | 33                    | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | Kylin               | V10                   | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes
 | openSUSE            | 15.2                  | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | OpenAnolis          | 8  / 23               | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | OpenEuler           | 22.03  / 24.03        | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | Rocky Linux         | 8  / 9                | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | StreamOS            | 3.4.8                 | x86_84 / aarch64 | Unknown    | Yes                | Yes                        | Unknown          |
 | SUSE                | 15.2                  | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | Ubuntu              | 20.04 / 22.04 / 24.04 | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |
 | UOS                 | 20                    | x86_84 / aarch64 | Yes        | Yes                | Yes                        | Yes              |

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
