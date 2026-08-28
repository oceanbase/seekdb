---
title: 调试
---

# 调试 seekdb

受支持的 Release 构建使用 `RelWithDebInfo`，在启用生产优化的同时保留调试信息。`build.sh` 不提供 Debug 模式。

## 构建可调试二进制

```bash
./build.sh release --init --make
```

二进制文件位于 `build_release/src/observer/seekdb`。分析 core dump 或提交调试信息时，应同时记录 `seekdb -V`，以便匹配二进制的 revision 和构建参数。优化构建可能内联函数或将变量显示为 optimized out；遇到这类情况时，日志或范围更小的单元测试通常更有效。

## 使用 GDB 或 LLDB

在 Linux 上查找进程并附加 GDB：

```bash
pidof seekdb
gdb build_release/src/observer/seekdb -p <pid>
```

在 macOS 上使用 LLDB：

```bash
lldb -p <pid>
```

分析 core dump 时，必须使用生成该文件的同一二进制：

```bash
gdb /path/to/seekdb /path/to/core
```

随后可以使用常规调试器命令设置断点、查看变量和获取 backtrace。

## 使用 RPM debuginfo

软件包将运行时二进制安装到 `/usr/bin/seekdb`。如果配置的软件源提供匹配的 debuginfo 包，可以直接安装；也可以在不安装的情况下提取：

```bash
rpm2cpio seekdb-debuginfo-<version>.<arch>.rpm | cpio -idmv
find usr/lib/debug -type f -name '*seekdb*.debug'
```

在 GDB 中加载实际找到的文件：

```gdb
symbol-file /absolute/path/to/seekdb.debug
```

运行时二进制和 debuginfo 包的 revision、架构必须一致。不要依赖固定的包内路径，应使用提取后实际找到的文件。

## 使用日志调试

对于并发服务，日志通常比暂停进程更有效。使用 `K()` 添加结构化字段，然后重新构建受影响的目标：

```cpp
LOG_DEBUG("insert sql generated", K(insert_sql), K(lbt()));
```

使用配置的 base-dir 定位日志。systemd 安装可以同时检查服务输出和服务器日志：

```bash
journalctl -u seekdb --since today
tail -F /var/lib/oceanbase/log/seekdb.log
```

如果在 `/etc/seekdb/seekdb.cnf` 中修改了 `base-dir`，应使用对应的日志目录。可以通过 trace ID 搜索一次请求的全部日志：

```sql
SELECT last_trace_id();
```

trace ID 会出现在日志行中，可以使用 `rg` 或 `grep` 搜索相关内容。日志字段、级别、轮转和限流请参考[日志系统](logging.md)。

## 调试时调整日志配置

以下配置项都是动态生效的集群级参数：

```sql
ALTER SYSTEM SET syslog_level = 'DEBUG';
ALTER SYSTEM SET syslog_io_bandwidth_limit = '50MB';
ALTER SYSTEM SET diag_syslog_per_error_limit = 1000;
ALTER SYSTEM SET enable_async_syslog = false;
```

调试结束后应恢复原值。提高日志量或关闭异步日志可能影响性能并增加磁盘占用。

## 打印并解析调用栈

需要源码级调用栈时，可以在结构化日志中加入 `lbt()`：

```cpp
LOG_DEBUG("state before retry", K(state), K(lbt()));
```

使用生成该日志的同一二进制解析地址，例如：

```bash
addr2line -pCfe build_release/src/observer/seekdb <address> ...
```

必须使用包含匹配调试信息的二进制，否则输出可能只有 `??` 栈帧。

## SQL 执行跟踪

开启会话级 trace，执行待分析语句，再查看记录的操作：

```sql
SET ob_enable_show_trace = 1;
-- 执行待分析的语句
SHOW TRACE;
```

不再需要时应关闭该设置。跟踪只适合定向诊断，可能带来额外开销。

## Debug Sync

Debug Sync 可以让指定服务线程在已有的 `DEBUG_SYNC` 点暂停，而不停止整个进程。它适合在 GDB 会影响心跳或并发行为时进行调试。

在一个会话中启用并配置同步点，在另一个会话中发送信号：

```sql
ALTER SYSTEM SET debug_sync_timeout = '100000s';
SET ob_global_debug_sync = 'BEFORE_UNIT_MANAGER_LOAD wait_for signal_name execute 10000';
SET ob_global_debug_sync = 'now signal signal_name';
```

调试结束后清理同步点并关闭 Debug Sync：

```sql
SET ob_global_debug_sync = 'BEFORE_UNIT_MANAGER_LOAD clear';
ALTER SYSTEM SET debug_sync_timeout = 0;
```

同步点名称必须存在于实际执行的代码路径中。新增同步点需要在源码中加入 `DEBUG_SYNC(...)` 并重新构建受影响目标。
