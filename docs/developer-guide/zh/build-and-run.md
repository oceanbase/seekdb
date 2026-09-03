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

在 Linux 上，可以使用独立的 CMake `sanity` 模式配置经过 Sanity 插桩的 seekdb 构建：

```bash
./build.sh sanity --init
```

`--init` 通常只在首次编译或依赖定义变化后使用。与 Release 模式相同，增加 `--make` 可以在配置后直接编译：

```bash
./build.sh sanity --init --make -j32
```

后续增量编译可以使用：

```bash
./build.sh sanity --make -j32
```

编译产物位于：

```text
build_sanity/src/observer/seekdb
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

仓库还提供了一个无依赖的 Python 命令行客户端（`python3 tools/seekdb-cli -h 127.0.0.1 -P10000 -uroot`），也可以通过本地 socket 连接嵌入式数据库，用法见 `tools/README.md`。

## 停止并清理本地实例

```bash
./tools/deploy/obd.sh destroy --rm -n single
```

该命令会停止实例并删除此示例创建的部署数据。不要对需要保留的数据执行该命令。
