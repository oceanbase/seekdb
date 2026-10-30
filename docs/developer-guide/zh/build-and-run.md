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

`init` 准备 Rust 和平台依赖，首次编译、依赖定义或 `rust-toolchain.toml` 变化后执行。后续增量编译只需重复 `build` 命令。产物位于 `build/bin/src/observer/seekdb`。

所有参数都放在命令后，包括 `--batch`、`--bazelrc` 等 Bazel 启动参数。`build.py` 每次读取当前 Bazel 的参数定义，在内部将启动参数移到命令前。参数名称保留 Bazel 原生拼写，多词参数用下划线连接。

Windows 使用 `python build.py`，命令参数相同。C++ 测试目标参见[编写与运行单元测试](unittest.md)。修改 Rust 依赖时，一并提交更新后的 `Cargo.lock`。

Rust 的 CXX 接口由 Bazel 自动生成，头文件路径为 `rust/<Bazel target name>.h`，例如 `#include "rust/local.h"`。C++ 编译只等待所需头文件，不等待 Rust 静态库。只想查看接口时执行 `./build.py build //rust:generated_headers`，文件位于 `build/bin/rust/`；该目标不编译 Rust 业务库。配置 YAML 由 Python 生成配置源码和 `config_checkers.h`，生成文件无需入库。

Rust workspace 的 `Cargo.toml`、`Cargo.lock` 和 `rust-toolchain.toml` 位于仓库根目录。各 crate 的 `Cargo.toml` 负责社区依赖及其版本；crate_universe 直接读取真实 workspace，在构建的仓库解析阶段生成这些依赖的 Bazel 规则。没有自定义 metadata 转换或临时 manifest。

新增 Rust crate 时，在它的目录添加 `BUILD.bazel`，使用标准 `rust_library` 声明源码、本地依赖和启用的 features。社区依赖通过 `all_crate_deps`、`aliases`、`crate_edition` 从 Cargo 声明读取，无需重复写版本。内部 path 依赖在 Cargo 和 BUILD 中各声明一次；新增 build.rs 时显式使用 `cargo_build_script` 并声明其输入。可参考 `rust/logger/BUILD.bazel`；生成文件的接入可参考 `rust/config/BUILD.bazel`。

需要进入最终静态库的模块，在 `rust/seekdb-rust` 的 Cargo.toml、BUILD.bazel 和 src/lib.rs 中添加依赖及引用。workspace 内的 path 依赖会自动成为成员，不需要修改 MODULE.bazel、SDK 初始化脚本或 C++ 最终链接清单。普通 build 检查 manifest 和锁文件变化，Rust 依赖变化不需要重新 init。

向 C++ 提供接口的模块，在 `rust/modules.bzl` 的模块表中登记一次。`bridge` 和 `headers` 路径相对 crate 目录，crate 名中的 `-` 转成目标名中的 `_`。在 crate 的 BUILD 中将桥接输入文件导出给 `//rust:__pkg__`。声明会自动创建头文件和桥接目标，并加入 `generated_headers` 和最终桥接链接集合。只需要编译接口的调用方依赖 `//rust:<name>_headers`；需要同时链接 Rust 实现的目标依赖 `//rust:<name>`。

CXX 的 `features` 与 Rust 编译启用的 features 必须一致。需要共享时使用模块内的常量文件，例如 `rust/sql-nio/features.bzl`，由 BUILD 和接口表同时引用。`exclude_platforms` 继续控制 CXX 桥接的平台可用性。仅供 Rust 调用的 crate 不登记 CXX 接口。

原生目标使用固定版本的 stable Rust SDK。Wasm 使用固定版本的 nightly，以及 `init --wasm` 准备的带补丁标准库。Cargo 的目标依赖声明选择各平台的社区依赖；sql-nio 共享的 feature 表让 Rust 和 CXX 一致选择原生网络或 Wasm 内存传输。

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

`.bazelrc` 默认让 C++ 编译优先使用远程执行；未配置远程执行器时，使用本地执行策略。指定 `--remote_executor` 即可，无需再加 `--config=remote`。Rust 编译、生成器和链接仍在本机运行。Linux 和 macOS 的默认本地策略顺序为 `worker,sandboxed,local`；Windows 使用 `worker,local`，因为 Bazel 在 Windows 上不支持沙箱。命令行显式指定的执行策略可覆盖默认值。如果服务需要 SSH 隧道，先单独建立隧道。

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

在 Linux 和 macOS 上，使用仓库中的实例管理脚本启动编译产物：

```bash
python3 tools/deploy/sdb.py start \
  --binary build/bin/src/observer/seekdb \
  --base-dir "$PWD/build/dev-instance" --port 2881
python3 tools/deploy/sdb.py wait-ready \
  --client deps/3rd/u01/obclient/bin/obclient \
  --base-dir "$PWD/build/dev-instance" --port 2881
```

脚本直接启动 seekdb，新数据目录会自动初始化。需要调整服务端参数时，向 `start` 传入一个或多个 `--parameter key=value`。`start` 在创建进程后返回；`wait-ready` 检查受管理的进程，并等待 `SELECT 1` 执行成功。

使用以下任一客户端连接：

```bash
mysql -h127.0.0.1 -P2881 -uroot
./deps/3rd/u01/obclient/bin/obclient -h127.0.0.1 -P2881 -uroot -Doceanbase -A
```

需要前台运行时，包括在 Windows 上，可以直接执行二进制，传入 `--nodaemon --base-dir=<目录> --port=2881`。Python 实例管理脚本目前支持 Linux 和 macOS。

## 停止或清理本地实例

停止实例并保留数据：

```bash
python3 tools/deploy/sdb.py stop --base-dir "$PWD/build/dev-instance"
```

再次执行相同的 `start` 和 `wait-ready` 命令即可重启。需要停止实例并删除本例创建的数据时，执行：

```bash
python3 tools/deploy/sdb.py destroy --base-dir "$PWD/build/dev-instance"
```

`destroy` 会删除整个受管理的实例目录，只能用于无需保留的数据。SQL 回归测试使用独立的临时实例，见[运行 mysqltest](mysqltest.md)。
