# 获取代码、编译并运行 seekdb

## 前置条件

先按照[安装工具链](toolchain.md)安装支持的编译器和依赖。完整源码编译需要较多的磁盘空间和内存。

## 获取代码

```bash
git clone https://github.com/oceanbase/seekdb.git
cd seekdb
```

## 编译生产二进制

仓库的 `build.py` 下载 `.bazelversion` 指定版本的 Bazel，并执行构建命令。默认使用 `-O2` 优化，保留调试信息。

```bash
./build.py init
./build.py build seekdb
```

`init` 准备平台依赖，首次编译或依赖定义变化后执行。后续增量编译只需重复 `build` 命令。产物位于 `build/bin/src/observer/seekdb`。

所有参数都放在命令后，包括 `--batch`、`--bazelrc` 等 Bazel 启动参数。`build.py` 每次读取当前 Bazel 的参数定义，在内部将启动参数移到命令前。参数名称保留 Bazel 原生拼写，多词参数用下划线连接。

Windows 使用 `python build.py`，命令参数相同。C++ 测试目标参见[编写与运行单元测试](unittest.md)。修改 Rust 依赖时，一并提交更新后的 `rust/Cargo.lock`。

## 设置编译并发

Bazel 默认按本机可用 CPU 数量设置并发。显式指定本地编译并发时，`build.py` 同时设置任务数量上限和本地 CPU 调度额度：

```bash
./build.py build -j32 seekdb
```

`-j32`、`-j 32` 与 `--jobs=32` 等价。只同步命令行中显式指定的数字；不指定时保留 Bazel 自动值。显式 `--local_resources=cpu=N` 可覆盖该额度，内存配置不受影响。

向其他工具传递同名参数时，使用 `--test_arg=-j32` 等带等号的形式。`run --` 后的程序参数原样转发。

## 使用远程 C++ 编译

指定 REAPI 服务地址和并发数：

```bash
./build.py build --remote_executor=grpc://HOST:PORT --jobs=100 seekdb
```

`.bazelrc` 默认让 C++ 编译优先使用远程执行；未配置远程执行器时，使用本地执行策略。指定 `--remote_executor` 即可，无需再加 `--config=remote`。Cargo、生成器和链接仍在本机运行。默认本地策略顺序为 `worker,sandboxed,local`，保留沙箱优先级。命令行显式指定的执行策略可覆盖默认值。如果服务需要 SSH 隧道，先单独建立隧道。

命令行指定 `--config=remote` 或非空 `--remote_executor` 时，`--jobs` 只控制总并发，本地 CPU 额度仍由 Bazel 自动识别或通过 `--local_resources=cpu=N` 单独设置。

## 编译 Sanity 二进制

Linux 上通过以下命令启用 Sanity 插桩：

```bash
./build.py build --sanity seekdb
```

Sanity 默认关闭；`--sanity` 等价于 `--sanity=true`，显式关闭用 `--sanity=false`。

如需保留独立的 Sanity 构建，指定构建目录：

```bash
./build.py build -o build_sanity --sanity seekdb
```

二进制通过 `build_sanity/bin/src/observer/seekdb` 访问。

## 构建目录

Bazel 的实际工作目录默认是项目内的 `build/`，中间文件和最终产物都放在这里。`build/bin`、`build/out` 等链接指向这个目录内的对应位置；Linux/macOS 的二进制通过 `build/bin/src/observer/seekdb` 访问。

在命令后指定 `-o PATH` 或 `--output_base=PATH`，`build.py` 会同时将产物链接前缀设为 `PATH/`。例如 `./build.py build -o build3 seekdb` 的实际工作目录是 `build3/`，二进制入口是 `build3/bin/src/observer/seekdb`。相对路径以项目根目录为基准，也可指定其他磁盘上的绝对路径。不同目录的构建状态和链接互相独立，可以并行构建；同一个目录的命令由 Bazel 排队执行。

使用 `./build.py info output_base` 查看实际工作目录，`./build.py clean --expunge` 清理它。自定义目录时，使用 `./build.py info -o build3 output_base` 或 `./build.py clean -o build3 --expunge`。搜索源码时可排除构建目录，例如 `find . -path './build' -prune -o -type f -name '*.cpp' -print`。

## 复用构建缓存

Bazel 二进制和默认的构建结果缓存可供不同代码目录共用。它们的根目录在 Linux 上是 `~/.cache/bazel`，macOS 上是 `~/Library/Caches/bazel`，Windows 上是 `%LOCALAPPDATA%/bazel`。Linux 支持 `XDG_CACHE_HOME`。依赖下载缓存由 Bazel 单独管理，也可跨代码目录复用。

使用 `--launcher_cache_dir PATH` 指定 Bazel 二进制缓存目录，`--disk_cache=PATH` 指定构建结果缓存目录，`--repository_cache=PATH` 指定依赖下载缓存目录。这些参数都放在命令后。例如：

```bash
./build.py build --launcher_cache_dir=/data/bazel --disk_cache=/data/bazel-actions seekdb
```

如需更改依赖下载设置，在命令后使用 Bazel 的 `--registry=URL` 或 `--downloader_config=PATH`。

## 为其他平台编译

各平台统一使用 `seekdb` 目标，通过 `--config=android`、`--config=ios`、`--config=ios-simulator` 或 `--config=wasm` 选择平台。不指定时构建本机程序。依赖准备方式见 [Android](android.md)、[iOS](ios.md) 和 [WebAssembly](webassembly.md) 指南。Android 生成服务端程序，iOS 生成 framework，WebAssembly 生成引擎和 SQL shell；别名不改变各平台的产物路径。

## 维护构建声明

根据需要修改的内容选择文件：

| 文件 | 职责 |
| --- | --- |
| `build.py` | 启动指定版本的 Bazel，通过 `init` 准备平台依赖。 |
| `.bazelrc` | Bazel 默认参数和各平台的命名配置。 |
| `MODULE.bazel` | Bazel 模块依赖，以及仓库和工具链注册。 |
| `bazel/defs.bzl` | 供 `BUILD.bazel` 使用的公共 C++ 和链接规则入口。 |
| `bazel/compile_config.bzl` | 公共编译参数和 Sanity 插桩配置。 |
| `bazel/build_settings.bzl`、`bazel/BUILD.bazel` | 自定义配置项类型和公共开关声明。 |
| 各目录的 `BUILD.bazel` | 目标、头文件、显式依赖和可见性。 |

C++ 库从 `//bazel:defs.bzl` 加载 `cc_library`、`unity_cc_library` 或 `grouped_unity_cc_library`。`cc_library` 分别编译每个源文件；`unity_cc_library` 将有序的 `unity_members` 列表合为一个编译单元；`grouped_unity_cc_library` 为 `unity_groups` 中的每个命名分组生成一个编译单元。

- 普通源码加入所属目标的 `srcs`。加入已有 Unity 分组时，在该模块 `*_source_inventory.bzl` 对应分组的 `srcs` 中添加仓库相对路径，保留成员顺序。使用 `unity_members` 的目标直接在 `BUILD.bazel` 中列出包内相对路径。
- 不应与其他源码合编的文件，加入所属目标使用的独立源码列表。列表直接填写路径，C 和 C++ 分开，由规则调用指定语言。新增列表还需要在 `BUILD.bazel` 中接入目标。
- 公共头文件加入 `hdrs` 或模块的公共头文件列表；私有头文件放入 `srcs` 或模块内部头文件目标。使用头文件 `glob()` 的模块会自动纳入匹配的新文件。Unity 辅助规则对应的属性名是 `public_hdrs` 和 `private_hdrs`。
- 公共头文件需要的依赖放入 `deps`，只有实现文件需要的依赖放入 `implementation_deps`。用 `visibility` 限定使用方。依赖由开发者显式维护，没有扫描 include 并自动生成依赖的脚本。

通过 `./build.py build //package:target` 编译受影响的目标。现有模块依赖方向和头文件分层检查随构建执行。

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
