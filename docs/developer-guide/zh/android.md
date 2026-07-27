# 在 Android 上构建和运行 seekdb

在 macOS 上使用 NDK 工具链将 seekdb 交叉编译为 Android arm64-v8a 架构，然后在模拟器或真机上部署运行。

## 前置条件

- macOS 主机（本文档基于 macOS 环境编写）
- 已安装 Android NDK（**推荐 27.x**，与预构建依赖一致；其它主版本需自行验证。默认路径示例：`~/Library/Android/sdk/ndk/27.3.13750724`）
- 运行 arm64-v8a（API 28+）的 Android 模拟器，或物理设备
- 通过 [ob-deps](https://github.com/oceanbase/ob-deps/tree/android_arm64-v8a) 的 `ndk/build_all.sh` 构建依赖
- 已安装 `adb` 并加入 PATH
- 已安装 `mysql` 客户端（用于启动后连接）

若 NDK 不在默认路径，请设置 `ANDROID_NDK_HOME`：

```bash
export ANDROID_NDK_HOME=$HOME/Library/Android/sdk/ndk/27.3.13750724
```

> **说明**：配置阶段可能出现 `CMake Deprecation Warning`（来自 NDK 的 `flags.cmake` 中 `cmake_minimum_required(VERSION 3.6.0)`）。该警告可忽略，不影响构建。

## 构建

### 1. 初始化依赖

```bash
./build.sh release --android --init
```

该命令会以 Android 模式执行 `deps/init/dep_create.sh`，下载并解压预构建的 NDK 依赖 tarball 到 `deps/3rd/`。

### 2. 配置并构建

仅构建 observer 二进制：

```bash
cd build_android_release
make seekdb -j$(nproc)
```

### 构建 libseekdb（FFI 共享库）

在相同 Android 构建目录下编译 C API 共享库（CMake 目标名 `libseekdb`，产物为 `libseekdb.so`）：

```bash
cd build_android_release
make libseekdb -j$(nproc)
```

产物路径一般为仓库根目录下的 `build_android_release/src/include/libseekdb.so`，头文件为源码树中的 `src/include/seekdb.h`。

若需缩小体积，请使用 NDK 自带的 `llvm-strip` 处理 ELF（不要用 macOS 自带的 `strip`）。在 macOS / Linux 主机上，工具链位于 `toolchains/llvm/prebuilt/<宿主>/bin/`，例如：

```bash
NDK_STRIP=$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
$NDK_STRIP -o /tmp/libseekdb.stripped build_android_release/src/include/libseekdb.so
```

也可在仓库内使用 [`package/libseekdb/libseekdb-build.sh`](../../../package/libseekdb/libseekdb-build.sh) 打包 `seekdb.h` 与 `libseekdb.so` 为 **`libseekdb-android-arm64-v8a.zip`**（仅支持 **arm64-v8a**）。在 `package/libseekdb/` 下执行 `./libseekdb-build.sh --android`（会先按需构建），或 `./libseekdb-build.sh <path/to/build_android_*/src/include>` 仅打包已有产物；在 macOS 上仅含 NDK 产出的 `libseekdb.so` 时也会使用该命名，避免误用 `darwin-*`。

### 3. 构建单元测试（可选）

`all_tests` 会将所有单元测试合并到一个可执行文件中：

```bash
cd build_android_release
make all_tests
```

## 部署到模拟器

### 移除调试符号

```bash
NDK_STRIP=$(echo "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip)

$NDK_STRIP -o /tmp/seekdb build_android_release/src/observer/seekdb
```

macOS 自带的 `strip` 无法处理 ELF 二进制，必须使用 NDK 提供的 strip。

### 推送到模拟器

```bash
adb push /tmp/seekdb /data/local/tmp/seekdb
adb shell chmod +x /data/local/tmp/seekdb
```

## 启动 seekdb

### 启动服务

```bash
adb shell "mkdir -p /data/local/tmp/seekdb_data"
adb shell "/data/local/tmp/seekdb --nodaemon \
  --base-dir /data/local/tmp/seekdb_data \
  --parameter memory_limit=4G \
  --parameter datafile_size=2G \
  --parameter datafile_maxsize=4G \
  --parameter log_disk_size=2G \
  --log-level INFO"
```

`memory_limit` 及相关参数必须设置。若不设置，seekdb 会根据系统内存自动计算资源需求，可能超出 Android 内核限制，导致启动时报 `OB_RESOURCE_UNIT_VALUE_INVALID`。

### 端口转发

在另一个终端中执行：

```bash
adb forward tcp:2881 tcp:2881   # MySQL 协议
```

### 连接

```bash
mysql -h 127.0.0.1 -P 2881 -u root
```

```sql
SELECT 1;
```

### 查看日志

```bash
adb shell "tail -100 /data/local/tmp/seekdb_data/log/seekdb.log"
```

### 停止服务

```bash
adb shell "kill \$(pidof seekdb)"
```
