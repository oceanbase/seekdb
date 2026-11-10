# 在 Android 上构建和运行 seekdb

本文介绍如何在 macOS 上将 seekdb 交叉编译为 Android arm64-v8a 二进制，并部署到模拟器或真机。

## 前置条件

- macOS 主机
- Android NDK 27.x，设置 `ANDROID_NDK_HOME` 指向实际安装目录
- API 28 或更高版本的 arm64-v8a 真机或模拟器
- 已将 `adb` 和 MySQL 兼容客户端加入 `PATH`

## 构建

设置实际安装的 NDK 路径，准备 Android 依赖后用 Bazel 编译：

```bash
export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/27.3.13750724"
rustup target add aarch64-linux-android
python3 build.py init --android
python3 build.py build --config=android seekdb
```

二进制文件位于：

```text
build/bin/src/observer/seekdb
```

该配置构建 API 28、支持 16 KiB 内存页的 ARM64 PIE，静态链接 C++ 运行库。单元测试修改应按照[编写与运行单元测试](unittest.md)中的流程，在受支持的 Linux 主机上验证。

## 移除符号并部署

macOS 自带的 `strip` 无法处理 Android ELF 二进制，必须使用 NDK 提供的工具：

```bash
NDK_STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
"$NDK_STRIP" -o /tmp/seekdb build/bin/src/observer/seekdb
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
