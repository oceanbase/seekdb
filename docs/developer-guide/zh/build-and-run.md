# 编译与运行

本文档介绍如何获取 OceanBase seekdb 源代码、编译项目以及运行 seekdb 实例。

## 前置条件

在开始之前，请确保：

1. **操作系统兼容性**：检查支持的操作系统列表（详见 [安装工具链](toolchain.md)）和 GLIBC 版本要求
2. **工具链安装**：按照 [安装工具链](toolchain.md) 文档安装 C++ 工具链
3. **基本要求**：确保系统有足够的磁盘空间和内存用于编译

## 相关文档

- [安装工具链](toolchain.md) - 安装编译所需的工具链
- [IDE 配置](ide-settings.md) - 配置开发环境
- [调试](debug.md) - 调试方法

## 获取源代码

将代码克隆到本地：

```shell
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

## 编译项目

seekdb 支持两种编译模式：Debug 和 Release。推荐开发时使用 Debug 模式，生产环境使用 Release 模式。

### Debug 模式

Debug 模式包含调试信息，便于开发和调试：

```shell
bash build.sh debug --init --make
```

编译完成后，二进制文件位于 `build_debug/bin/observer/seekdb`。

### Release 模式

Release 模式优化了性能，适合生产环境：

```shell
bash build.sh release --init --make
```

编译完成后，二进制文件位于 `build_release/bin/observer/seekdb`。

> **提示**：首次编译可能需要较长时间，请耐心等待。编译过程中如果遇到问题，请检查工具链是否正确安装。

## 运行实例

编译完成后，可以使用 `obd.sh` 工具部署一个 seekdb 实例。

### 部署步骤

1. **准备部署目录**：

```shell
./tools/deploy/obd.sh prepare -p /tmp/obtest
```

2. **部署实例**：

```shell
./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml
```

### 查看端口配置

部署完成后，可以通过查看 `./tools/deploy/single.yaml` 文件中的 `mysql_port` 配置项来确认监听端口。

> **默认端口**：如果使用 root 用户部署，seekdb 服务程序默认监听 10000 端口。下文示例基于此默认端口。

## 连接数据库

部署成功后，可以使用以下方式连接 seekdb：

### 使用 MySQL 客户端

```shell
mysql -uroot -h127.0.0.1 -P10000
```

### 使用 obclient

```shell
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P10000 -uroot -Doceanbase -A
```

> **提示**：如果连接失败，请确认：
> - 服务是否已成功启动
> - 端口号是否正确
> - 防火墙规则是否允许连接

## 停止服务

停止服务并清理部署：

```shell
./tools/deploy/obd.sh destroy --rm -n single
```

该命令会停止运行中的 seekdb 实例并清理相关资源。

## 下一步

- [编写单元测试](unittest.md) - 学习如何编写和运行单元测试
- [调试方法](debug.md) - 了解如何调试 seekdb
- [编程惯例](coding-convention.md) - 了解 seekdb 的编程规范
