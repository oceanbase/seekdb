# 在 iOS 上构建 seekdb

本文介绍如何在 Apple Silicon Mac 上将 seekdb 交叉编译为 `SeekDB.framework`。这是一个在 iOS App 进程内运行引擎的动态 framework，支持 arm64 真机和 arm64 模拟器。iOS App 不能启动其他进程，所以 App 中要嵌入 framework。

## 前置条件

- Apple Silicon Mac
- 安装了 iOS SDK 的 Xcode，位于 `/Applications/Xcode.app`，或通过 `DEVELOPER_DIR` 指定
- 已将 `rustup`、Autoconf、Automake 1.16.3 及以上、Libtool、Gettext、pkg-config 加入 `PATH`（`brew install autoconf automake libtool gettext pkgconf`）
- 约 10 GB 可用磁盘空间，用于依赖和一个构建目录

## 构建

初始化依赖，再用 Bazel 编译 arm64 真机 framework：

```bash
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
./build.py init --ios
./build.py build --config=ios seekdb
```

`init --ios` 准备宿主工具、Rust iOS target，并编译基础依赖库，安装到 `deps/ios/iphoneos/devel`。其余固定版本源码依赖由 Bazel 的 `//bazel/sdk_sources:source_sdk` 构建。后续编译直接运行 `build`；再次初始化时会复用仍然有效的依赖库。Bazel 版本由项目的 `.bazelversion` 固定，首次使用时由 `build.py` 下载。

arm64 模拟器使用独立的依赖目录和配置：

```bash
./build.py init --ios --simulator
./build.py build --config=ios-simulator seekdb
```

最低部署版本为 iOS 18.0。两种配置均通过 Apple C++ 工具链直接编译引擎，并关闭物理备库功能。

framework 位于：

```text
build/bin/src/observer/framework/SeekDB.framework
```

如需保留独立的真机和模拟器构建，分别指定构建目录：

```bash
./build.py build -o build_ios --config=ios seekdb
./build.py build -o build_ios_simulator --config=ios-simulator seekdb
```

单元测试修改应按照[编写与运行单元测试](unittest.md)中的流程，在受支持的 Linux 主机上验证。

## framework 内容

- `SeekDB`：引擎、Rust 库和第三方库链接成的一个 arm64 动态库，只依赖系统库
- `Headers/seekdb.h`：声明 `seekdb_open`、`seekdb_last_open_error`、`seekdb_close` 和 `seekdb_connection_options`，与桌面版 `seekdb.h` 中的声明相同
- `Info.plist`：源码 revision、平台和最低系统版本
- `build-manifest.json`、`rust-Cargo.lock`、`Licenses.tar`：链接归档哈希、依赖来源记录和许可证

真机和模拟器的 framework 不能混用。framework 没有用于分发的签名；在 Xcode 中以 **Embed & Sign** 方式加入 App 后，会随 App 一起签名。

## 在 App 中使用

framework 只负责启动和停止引擎。执行 SQL 时，使用任意 MySQL 兼容的客户端库，连接 `seekdb_connection_options` 返回的 Unix socket，用户为 `root`，密码为空：

```c
#include <stdio.h>
#include <SeekDB/seekdb.h>

SeekdbHandle db = NULL;
const char *parameters[] = {"memory_budget", "1G", NULL};
if (seekdb_open(absolute_data_dir, parameters, &db) == SEEKDB_SUCCESS) {
  SeekdbConnectionOptions options;
  seekdb_connection_options(db, &options);
  /* 用 MySQL 客户端以 options.user 连接 socket options.unix_socket。 */
  /* seekdb_close 只释放 handle，引擎会一直运行到 App 进程退出。 */
  seekdb_close(db);
} else {
  fprintf(stderr, "%s\n", seekdb_last_open_error());
}
```

- 数据目录使用 App 容器内的绝对路径。新数据库默认 `memory_budget=1G`、`vector_memory_limit=128M`、`log_disk_size=2G`、`datafile_maxsize=20G`，创建数据库时可以用 key/value 参数覆盖。
- 不支持 TCP：传入非零 `port` 会返回 `SEEKDB_INVALID_ARGUMENT`。
- 第一次调用 `seekdb_open` 时启动引擎，之后引擎一直运行到 App 进程退出；`seekdb_close` 只释放 handle。再次打开同一目录会复用正在运行的引擎，所有 handle 都关闭之后也一样。打开其他目录会失败，引擎启动失败之后再打开也会失败。
- 引擎会把进程工作目录设为它的数据目录，并且不再改回，请使用绝对路径。framework 应一直保持加载，直到进程退出。
