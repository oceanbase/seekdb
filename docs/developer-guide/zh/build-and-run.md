# 获取代码、编译并运行 seekdb

## 前置条件

先按照[安装工具链](toolchain.md)安装支持的编译器和依赖。完整源码编译需要较多的磁盘空间和内存。

## 获取代码

```bash
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

## 编译生产二进制

当前构建支持 Release 模式（`RelWithDebInfo`、`-O2`），该模式在保留调试信息的同时启用生产优化。`build.sh` 不支持 Debug 模式。

```bash
./build.sh release --init --make
```

`--init` 用于准备当前平台所需的依赖，首次编译或依赖定义变化后通常需要执行。编译产物位于：

```text
build_release/src/observer/seekdb
```

完成初始化后，可以使用以下命令进行增量编译：

```bash
./build.sh release --make
```

在 Linux 上，同一套 CMake 构建还提供模块单元测试目标和聚合目标 `pretest`，具体用法参见[编写与运行单元测试](unittest.md)。

## 编译 Sanity 二进制

在 Linux 上，可以使用独立的 Bazel `sanity` 模式编译经过 Sanity 插桩的 seekdb 二进制：

```bash
./build.sh sanity --init
```

`--init` 通常只在首次编译或依赖定义变化后使用。后续编译可以省略该参数；其余参数会传递给 Bazel 构建命令：

```bash
./build.sh sanity --jobs=32
```

编译产物位于：

```text
build_bazel/bin/src/observer/seekdb
```

## 运行本地实例

使用仓库中的 `obd.sh` 包装脚本准备隔离环境并启动 seekdb：

```bash
./tools/deploy/obd.sh prepare -p /tmp/obtest
./tools/deploy/obd.sh deploy -c ./tools/deploy/single.yaml
```

从 `tools/deploy/single.yaml` 读取 `mysql_port`。如果生成的端口是 `10000`，可以使用以下任一客户端连接：

```bash
mysql -h127.0.0.1 -P10000 -uroot
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P10000 -uroot -Doceanbase -A
```

如果生成了其他端口，应以文件中的实际值为准。

## 停止并清理本地实例

```bash
./tools/deploy/obd.sh destroy --rm -n single
```

该命令会停止实例并删除此示例创建的部署数据。不要对需要保留的数据执行该命令。
