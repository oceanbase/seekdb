# 在 Android 上构建和运行 seekdb

本文介绍如何在 macOS 上将 seekdb 交叉编译为 Android arm64-v8a 二进制，并部署到模拟器或真机。

## 前置条件

- macOS 主机
- Android NDK 27.x；如果不在 SDK 默认路径，需设置 `ANDROID_NDK_HOME`
- API 28 或更高版本的 arm64-v8a 真机或模拟器
- 已将 `adb` 和 MySQL 兼容客户端加入 `PATH`

## 构建

使用受支持的 Android 构建入口，一次完成依赖初始化、配置和编译：

```bash
./build.sh release --android --init --make -j16
```

二进制文件位于：

```text
build_android_release/src/observer/seekdb
```

Android CMake 构建不提供 `all_tests` 目标。单元测试修改应按照[编写与运行单元测试](unittest.md)中的流程，在受支持的 Linux 主机上验证。

## 移除符号并部署

macOS 自带的 `strip` 无法处理 Android ELF 二进制，必须使用 NDK 提供的工具：

```bash
NDK_STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
"$NDK_STRIP" -o /tmp/seekdb build_android_release/src/observer/seekdb
adb push /tmp/seekdb /data/local/tmp/seekdb
adb shell chmod +x /data/local/tmp/seekdb
```

Apple Silicon 上应以 `$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/` 下实际存在的主机目录为准。

## 启动并连接

根据设备资源选择合适的参数：

```bash
adb shell "mkdir -p /data/local/tmp/seekdb_data"
adb shell "/data/local/tmp/seekdb --nodaemon \
  --base-dir /data/local/tmp/seekdb_data \
  --parameter memory_budget=4G \
  --parameter datafile_size=2G \
  --parameter datafile_maxsize=4G \
  --parameter log_disk_size=2G \
  --log-level INFO"
```

转发 SQL 端口并连接：

```bash
adb forward tcp:2881 tcp:2881
mysql -h127.0.0.1 -P2881 -uroot
```

查看日志和停止进程：

```bash
adb shell "tail -100 /data/local/tmp/seekdb_data/log/seekdb.log"
adb shell "kill \$(pidof seekdb)"
```
