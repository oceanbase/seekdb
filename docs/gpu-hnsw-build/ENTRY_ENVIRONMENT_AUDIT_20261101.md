# 2026-11-01 原生编译入口与依赖环境排查

本轮复现了此前手工 GPU 验收未覆盖的入口阻塞：原镜像的依赖缓存对应旧清单，当前源码初始化缓存未命中后需要 `rpm2cpio/cpio`，但两者缺失；继续执行原 Rust 初始化命令，又会因缺少 `rustup` 退出。此前使用手工准备的 Rust 和镜像依赖目录完成二进制验收，不能证明默认入口能够自行准备环境。以上都是本机实际复现，尚无官方执行日志证明它们就是历史测评失败原因。

## 复现结果

在同一 Radeon 实例的独立目录，从 `bb488f7cfcdf102d8d11c5f44ac7f2fd02d86cec` 导出源码，使用原始 PATH `/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`。没有将上轮手工 Rust 加入 PATH，没有改上轮已验收源码或二进制。

| 检查 | 实际结果 | 含义 |
| --- | --- | --- |
| 原 `.github/script/seekdb/compile.sh` | 0.715 秒退出 5，`rpm2cpio: command not found`、`cpio: command not found` | 在编译前即失败 |
| 原 `cd rust && rustup toolchain install --no-self-update` | 退出 127，`rustup: command not found` | 镜像的独立 Rust 安装不能满足原初始化脚本 |
| 原 Rust/Cargo | 1.97.1；源码 pin 为 1.98.1 | 不能直接跳过版本准备 |
| 精确依赖初始化 | 159.326 秒，退出 0 | 补齐解包工具后，当前清单可真实下载并解包 |
| 修复后的实际入口 | init、GPU configure 通过，主编译 guard 刻意退出 42 | 证明到达真实主编译阶段，不是完整构建成功 |
| Linux 回归 | 11 项入口 + 6 项 Rust + 6 项部署，23 项全部通过 | 覆盖工具链选择、失败码、入口和部署合约 |

源码依赖清单 MD5 为 `6cbf427016d9c5328b3f45ad0bb925b5`，镜像旧缓存为 `ac032efc6b707fc0273e6b553db53011`。源码清单与赛事基线相同，这不是本轮引入的新依赖。没有把旧缓存改名成新清单，也没有伪造 `DONE`；本轮在任务私有目录真实初始化精确清单，镜像共享缓存保持原样。

## 修复行为

新增 `deps/init/setup_rust.sh`，由通用编译入口和 native `build.sh init` 共用：从源码读取精确 pin；Rust/Cargo 同时匹配时允许复用独立安装；不匹配且缺 rustup 时，在任务目录准备私有工具链，随后验证两个版本都精确匹配。编译入口在父 shell 中导出工具链，保证 init 与 configure 使用一致环境。cross init 保留原 manifest 的 targets/components 安装行为。

默认从官方静态二进制地址下载 rustup-init；本次 `sh.rustup.rs` 返回 403，不能假设安装脚本域名可达。支持显式 `RUSTUP_INIT_URL` 脚本镜像，以及 rustup 原生的 `RUSTUP_DIST_SERVER/RUSTUP_UPDATE_ROOT`。下载、安装、版本检查错误会非零退出，入口记录最终阶段及状态，不继续用旧编译器构建。

冷缓存缺少 RPM 提取工具时，`dep_create.sh` 在下载前列出缺失工具和安装命令。**脚本不会自动安装系统包**；本验证实例已实际安装官方 Ubuntu 的 `rpm2cpio/cpio` 及其依赖。评分机如果也缺少这些工具，需要先满足此前提，或提供对应精确清单的完整依赖缓存。

Ubuntu 构建环境准备示例（由环境维护者执行）：

```bash
apt-get update
apt-get install -y rpm2cpio cpio
```

真实入口示例：

```bash
GITHUB_WORKSPACE="$PWD" SEEKDB_TASK_DIR=/tmp/seekdb-amd-task \
  MAKE_ARGS=-j4 bash .github/script/seekdb/compile.sh
```

工具链默认位于 `$SEEKDB_TASK_DIR/rust`，可设置 `SEEKDB_RUST_INSTALL_ROOT` 复用私有安装。已存在的用户 rustup 仍按其原生配置使用；原镜像无 rustup 的路径不会覆盖系统 Rust，也不修改 shell 启动文件。网络隔离环境需预先准备精确工具链和下载缓存。

## 验证范围与证据

实际 rustup-init 从官方地址下载并运行，SHA256 为 `5be191e8c70a5007074254cb74d78dbfdd25cd5a61c27de7b8e5410122ec24f4`。为避免重复下载 122 MB 已校验文件，三个官方 cargo/rustc/rust-std 包预置到 rustup 标准下载缓存，逐个对照官方 manifest 核对 SHA256；工具链仍由实际 helper/rustup 安装，未手工铺设。不能将这次测试描述为完全无缓存的全网下载验证。

真实 compile.sh 使用原镜像 PATH，helper 自行选用私有 Rust 1.98.1，随后执行真实 `build.sh init` 和 release GPU CMake 配置。`OB_BUILD_CUVS/OB_BUILD_HIPVS` 均为 ON，最终到达 `make -j2 seekdb`。guard 允许 CMake 编译器试编译，仅在主构建目录刻意退出 42；入口正确记录 `final phase=build exit=42`。第一次 guard 误拦试编译、一次驱动 cwd 错误均保留为驱动错误，不计为产品缺陷。

本轮改动不涉及 C++、GPU、CMake 源码，没有再次执行完整 C++ 编译或 GPU SQL 验收。上轮完整 TGZ 编译、实际 CPack、解包运行、27 项 SQL、六轮 CPU/GPU、DML/过滤及隐藏 GPU 重启结果仍按 [PACKAGING_AUDIT_20261101.md](PACKAGING_AUDIT_20261101.md) 的原始提交分别追溯。

[本轮原始证据](results/radeon-20261101-environment/README.md)包含成功与失败日志、真实驱动、23 项测试输出、官方包清单和候选文件哈希；远端被验证脚本与本次候选文件逐一匹配。Shell 语法和 `git diff --check` 通过。

官方历史任务 47236537 仅返回 `failMsg=8`、`errorMessage=null`、空用例详情，公开记录页也没有该枚举的解码器。没有足够证据把 8 解释为某个具体编译或算法错误；修复后的官方成绩仍须读取平台回执。
