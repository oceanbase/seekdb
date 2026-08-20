---
title: 调试
---

# 调试 seekdb

Bazel 默认使用 `-O2` 优化。进行源码级调试时，使用仓库提供的 `O1` 配置，降低优化程度以便单步跟踪。

## 构建可调试二进制

```bash
source ~/.bashrc
./bazel.py deps init
./bazel.py build --config=O1 //src/observer:seekdb
```

二进制文件位于 `build_bazel/bin/src/observer/seekdb`。优化构建仍可能内联函数或将变量显示为 optimized out；遇到这类情况时，日志或范围更小的 Bazel 测试通常更有效。

## 附加调试器

在 Linux 上查找进程并附加 GDB：

```bash
pidof seekdb
gdb build_bazel/bin/src/observer/seekdb -p <pid>
```

在 macOS 上使用 LLDB：

```bash
lldb -p <pid>
```

分析 core dump 时必须使用生成该文件的同一二进制：

```bash
gdb /path/to/seekdb /path/to/core
```

收集 dump 时一并保存 `seekdb -V` 输出，以便匹配 revision 和构建参数。

## 调试 RPM 安装的 seekdb

软件包将运行时二进制安装到 `/usr/bin/seekdb`。如果仓库提供匹配的 debuginfo 包，可以直接安装；也可以在不安装的情况下提取：

```bash
rpm2cpio seekdb-debuginfo-<version>.<arch>.rpm | cpio -idmv
find usr/lib/debug -type f -name '*seekdb*.debug'
```

在 GDB 中加载实际找到的文件，不要依赖旧版本中的固定路径：

```gdb
symbol-file /absolute/path/to/seekdb.debug
```

运行时二进制和 debuginfo 包的 revision、架构必须一致。

## 使用日志调试

对于并发服务行为，日志通常比单步调试更有效：

```cpp
LOG_DEBUG("insert sql generated", K(insert_sql));
```

`K(variable)` 会同时打印变量名和值。日志级别、模块、限流和运行时配置请参考[日志系统](logging.md)。

systemd 安装可以使用以下命令检查服务和运行日志：

```bash
journalctl -u seekdb --since today
tail -F /var/lib/oceanbase/log/seekdb.log
```

如果在 `/etc/seekdb/seekdb.cnf` 中修改了 `base-dir`，应使用对应的日志目录。
