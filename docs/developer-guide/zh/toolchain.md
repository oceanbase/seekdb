# 安装构建工具链

当前维护的源码构建配置只有 Linux x86-64、Release、Unity 编译。

## 系统依赖

Fedora 系发行版：

```shell
yum install git wget curl make glibc-devel glibc-headers binutils m4 libtool libaio python3
```

Debian 系发行版：

```shell
apt-get install git wget curl make build-essential binutils m4 file python3 libaio1
```

如果发行版已将 `libaio1` 重命名，请安装兼容包，例如
`libaio1t64`。

## 初始化并构建

仓库依赖初始化脚本会准备编译器和第三方库。请安装 `.bazelversion`
记录的 Bazel 版本；仓库启动器只使用已安装的 Bazel，不下载 Bazel 或
Bazelisk。

```bash
source ~/.bashrc
./build.sh release --init
cd build_release
make -j"$(nproc)"
```

每个模块自己维护源码、头文件和 Unity 清单。跨模块编译输入来自显式声明的
语义 target，不再由 Clang depfile 生成全局兼容闭包。本地 C++ action 默认
使用 Bazel sandbox；需要远程执行时通过 `bazel.py` 显式启用。不得恢复
`//src/...` 全量可见的 header target，也不得用 `local` strategy 绕过隔离。

构建产物位于：

```text
build_release/src/observer/seekdb
```

macOS、Windows、Android、debug、覆盖率和非 Unity 源码构建目前均不维护。
旧入口的存在不能视为这些配置仍然可用。
