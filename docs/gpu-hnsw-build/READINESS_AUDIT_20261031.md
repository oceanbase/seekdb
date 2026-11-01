# 2026-10-31 再次排查：Release 安装缺主程序

本轮发现并修复一个可以复现的部署缺口：普通 Linux Release 构建的 `server` 组件只安装 GPU worker 和桥接库，没有安装 `seekdb` 主程序。它是确定的仓库问题，但没有平台日志，不能认定它就是官方测评“执行失败”的原因。

## 修复及复现

在新 Radeon 实例检出 `4e2fc5b5e59bda3b21d1fe31f8dcc448b5f86a66`，使用课程开关 `OB_BUILD_CUVS=ON` 配置并开始从空目录构建。worker 已编译后执行：

```bash
cmake --install build_release --component server --prefix /workspace/install-before
```

命令返回 0，但目录只有以下 GPU 文件，缺少 `usr/bin/seekdb`：

```text
usr/bin/seekdb_hipvs_hnsw_worker
usr/libexec/seekdb/lib/libseekdb_hipvs_hnsw.so
```

生成的 `src/observer/cmake_install.cmake` 也没有主程序安装规则。因此不是仅仅“主程序尚未编译完”：原安装计划本身就没有该文件。新增的 `check_hipvs_install.py` 对实际安装目录返回非零，并报告 `server component is incomplete: usr/bin/seekdb`。

之前的组件验收手动复制过主程序，未覆盖这个缺口；不改写这些历史验收记录。本轮已在普通 Linux Release 构建中注册主程序的 `server` 安装规则，使用 `cmake/InstallReleaseServer.cmake` 统一定义。RPM/DEB/TGZ/WIX 包配置仍由 `Pack.cmake` 注册，不重复安装。Windows 和 macOS 的原安装行为不变。

完整结果与本地测试记录见 [本轮记录](results/radeon-20261031-readiness/README.md)。本地使用实际 CMake 和 C++ 编译器，验证生产安装规则的三个行为：`server` 组件包含可执行的主程序、未编译主程序时安装不能假成功、包配置不重复注册这条 Release 规则。这是安装规则测试，没有把小型测试程序当成完整 seekdb 二进制或 GPU 验收。

## 限制嵌套构建并行数

用户随后报告云端“内核意外死亡”，Jupyter Terminal 也无法打开，SSH 端口拒绝连接。目前没有取得 cgroup 的 `memory.events` 或内核 OOM 日志，不能确认崩溃一定由内存不足引起，也不能将它直接等同于官方测评的失败原因。

排查中确认，外部 jemalloc 工作区的 Cargo 启动命令原先写死 `--jobs 4`，因此仅设置顶层 `make -j1` 或 `CARGO_BUILD_JOBS=1` 不能把所有构建都限制到单任务。新增 CMake 参数 `SEEKDB_EXTERNAL_CARGO_JOBS`，默认仍为 4，允许显式设为 1，并拒绝零、负数和非数字。三项新增测试实际执行生产模块生成的 CMake 启动器，记录子进程收到的参数，验证默认值、单任务配置和非法值拒绝；没有把记录参数的测试替身当成真实 Rust/jemalloc 编译。

在确认新实例状态稳定后，资源保守的编译方式为：

```bash
export CARGO_BUILD_JOBS=1
./build.sh release -DOB_BUILD_CUVS=ON \
  -DSEEKDB_EXTERNAL_CARGO_JOBS=1 \
  -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm
make -C build_release -j1 seekdb
```

单任务能降低并行编译的内存需求，但不保证任何内存配额都足以完成 Unity 编译；启动前应读取实际 cgroup 限额并保留日志。本轮没有重新启动云实例来声称该方案已通过完整构建。

## 运行新增部署检查

在具备依赖、Rust 1.98.1、hipVS 和 ROCm 的 Linux 环境，从完整仓库执行：

```bash
./build.sh release -DOB_BUILD_CUVS=ON \
  -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm
make -C build_release -j8 seekdb

python3 unittest/sql/vector_index/check_hipvs_install.py \
  --build-dir build_release \
  --prefix /workspace/hipvs-install-check \
  --output /workspace/hipvs-install-check.json
```

安装前目标目录必须不存在或为空，避免残留的旧主程序掩盖遗漏。检查会实际安装 `server` 组件，确认三项必需文件、主程序版本、worker 加载返回值及 `ldd` 结果；移除检查进程的 `LD_LIBRARY_PATH` 和 `LD_PRELOAD`，避免外部环境掩盖安装布局错误。该检查不声称执行了 GPU 构图。

安装完成后，使用安装目录中的主程序继续执行 [SUBMISSION.md](SUBMISSION.md) 的启动、SQL 合约、CPU/GPU 基准和重启步骤。例如启动命令的 `--binary` 应指向 `/workspace/hipvs-install-check/usr/bin/seekdb`，不能继续使用镜像预装程序。

## 仍存在的限制及缺项

1. **官方失败原因仍未知。** 仓库和分支存在，且当前分支的祖先 `5bce9e06d1281347ad61344e4b01fab9158c58a2` 与核对时官方 `2026-competition` 分支一致；没有证据说明平台到底在克隆、编译、安装还是 SQL 阶段失败。
2. **必须明确启用 GPU。** 普通默认构建仍为 CPU；需要 `OB_BUILD_CUVS=ON` 或 `OB_BUILD_HIPVS=ON`。两者均指定时值须一致。未启用时，即使机器安装了 hipVS，SQL 也不能使用本实现的 GPU 构建后端。
3. **镜像 Rust 版本低于源码要求。** 本轮实际检查到镜像 Rust/Cargo 为 1.97.1；仓库锁定 1.98.1。本轮使用独立安装的 1.98.1 开始构建，没有修改源码要求或全局工具链。
4. **完整 CPack 打包仍未通过。** 再次实际执行 `./build.sh tgz -DOB_BUILD_CUVS=ON ...`，配置因基线缺少 `tools/systemd/profile/telemetry.sh.template` 返回 1。`server` 组件安装修复不等于完整 RPM/DEB/TGZ 包通过，源代码材料 ZIP 也不是 CPack 运行包。
5. **Bazel GPU 路径没有接入。** GPU 开关、桥接库和 worker 目标目前在 CMake 中定义，不能声称 Bazel 构建已启用 GPU。此外 OBLib 架构检查仍报告基线已有的六个头文件未登记、生成清单过期及 Rust 头文件依赖规则冲突。未用本轮小修复重生成整套语义依赖图。
6. **Notebook 不能替代构建题验收。** 课程的 `dbms_vector.batch_knn` 来自镜像预装程序；本仓库的核心改动是 HNSW 构建。两种性能数据、二进制和验证入口分别保留。

## 本轮 GPU 验证状态

新实例从空目录配置、编译到 Rust 阶段后，SSH 端口开始拒绝 TCP 连接。**本轮完整 seekdb 构建、修复后的真实主程序安装、六组新增 SQL 边界用例、新 CPU/GPU 基准和重启尚未完成。** 已准备对应执行脚本，不把未执行完的计划写成通过，不复用历史 GPU 数字作为本轮成绩。

在后续另一个空白实例上准备离线依赖时，首轮缓存只覆盖主 Rust 工作区，遗漏了 `deps/external/Cargo.lock` 中锁定的 `seekdb-jemalloc-sys`，构建因此返回非零。这是本次测试缓存准备遗漏，不是源码缺少 jemalloc；补齐并校验两份锁文件共 123 个依赖后，恢复脚本尚未执行，实例便再次拒绝连接。该次已下载的配置/失败日志单独保存，不能将其描述为源码编译或新 GPU 验收通过。

本轮已完成的是旧安装缺口的云端复现和修复规则的 Linux 本地回归。此前不同提交的完整 GPU 记录仍见 [ENTRY_AUDIT](ENTRY_AUDIT_20261031.md) 和 [RECHECK](RECHECK_20261031.md)。
