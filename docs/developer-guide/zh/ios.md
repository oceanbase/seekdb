# 在 iOS 上构建 seekdb

本文介绍如何在 Apple Silicon Mac 上将 seekdb 交叉编译为 `SeekDB.framework`。这是一个在 iOS App 进程内运行引擎的动态 framework，支持 arm64 真机和 arm64 模拟器。

## 前置条件

- Apple Silicon Mac
- 安装了 iOS SDK 的 Xcode，位于 `/Applications/Xcode.app`，或通过 `DEVELOPER_DIR` 指定
- 已将 `rustup` 加入 `PATH`
- 约 10 GB 可用磁盘空间，用于依赖和一个构建目录

## 构建

使用 iOS 构建入口，一次完成依赖初始化、配置和编译：

```bash
./build.sh release --ios --init --make -j8               # arm64 真机
./build.sh release --ios --simulator --init --make -j8   # arm64 模拟器
```

`--init` 与 Android 构建一样执行 `dep_create.sh` 和 `rustup toolchain install`，另外会用 `deps/ios-build` 中固定版本的源码编译 iOS 依赖库，安装到 `deps/ios/<sdk>/devel`；之后再执行时，仍然有效的依赖库会直接复用。依赖准备好后可以去掉 `--init`。不加 `--make` 时，`build.sh` 只生成构建规则。

framework 位于：

```text
build_ios_release/framework/SeekDB.framework             # 真机
build_ios_simulator_release/framework/SeekDB.framework   # 模拟器
```

CMake 选项写在 `--make` 之前：`-DCMAKE_BUILD_TYPE=Debug` 构建 Debug 版本，`-DDEP_DIR=/path` 使用其他 iOS 依赖目录。`./build.sh clean` 会删除这两个构建目录。

iOS CMake 构建不提供 `all_tests` 目标。单元测试修改应按照[编写与运行单元测试](unittest.md)中的流程，在受支持的 Linux 主机上验证。

## framework 内容

- `SeekDB`：引擎、Rust 库和第三方库链接成的一个 arm64 动态库，只依赖系统库
- `Headers/seekdb.h`：声明 `seekdb_open`、`seekdb_close` 和 `seekdb_connection_options`
- `Info.plist`、`build-manifest.json`（源码 revision、校验和、导出符号）、`rust-Cargo.lock` 和 `Licenses/`

真机和模拟器的 framework 不能混用。framework 没有用于分发的签名；在 Xcode 中以 **Embed & Sign** 方式加入 App 后，会随 App 一起签名。

## 在 App 中使用

framework 只负责启动和停止引擎。执行 SQL 时，使用任意 MySQL 兼容的客户端库，连接 `seekdb_connection_options` 返回的 Unix socket，用户为 `root`，密码为空：

```c
#include <stddef.h>
#include <SeekDB/seekdb.h>

SeekdbHandle db = NULL;
const char *parameters[] = {"memory_budget", "1G", NULL};
if (seekdb_open(absolute_data_dir, parameters, &db) == SEEKDB_SUCCESS) {
  SeekdbConnectionOptions options;
  seekdb_connection_options(db, &options);
  /* 用 MySQL 客户端以 options.user 连接 socket options.endpoint。 */
  /* 最后一次调用 seekdb_close 之前，先关闭客户端的连接。 */
  seekdb_close(db);
}
```

- 数据目录使用 App 容器内的绝对路径。新数据库默认 `memory_budget=1G`、`vector_memory_limit=128M`、`log_disk_size=2G`、`datafile_maxsize=20G`，创建数据库时可以用 key/value 参数覆盖。
- 不支持 TCP：传入非零 `port` 会返回 `SEEKDB_INVALID_ARGUMENT`。
- 再次打开同一目录会共享正在运行的引擎，打开其他目录会失败。每个进程只能启动一次引擎：最后一次 `seekdb_close` 之后，要重新打开数据库必须启动新的 App 进程。
- 引擎运行期间会改变进程工作目录，请使用绝对路径。framework 应一直保持加载，直到进程退出。
