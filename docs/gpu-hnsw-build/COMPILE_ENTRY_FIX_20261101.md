# 2026-11-01 通用编译入口启用 AMD 后端

此前 `.github/script/seekdb/compile.sh` 调用 `build.sh release` 时未传入 GPU 选项，沿用 CMake 默认 OFF。如果测评通过该入口构建，可能生成 CPU 版本。

本次在 AMD 比赛分支修改该入口：

- 默认显式传入 `-DOB_BUILD_CUVS=ON -DOB_BUILD_HIPVS=ON`；不按依赖是否存在自动降级 CPU。课程默认依赖目录为 `/opt/hipvs`、`/opt/rocm`，可以使用 `HIPVS_ROOT`、`ROCM_ROOT` 指定其他路径。
- 配置结束核对 `CMakeCache.txt` 中两个开关的实际值，不符合要求立即失败。
- 编译成功后要求存在可执行 `build_release/src/observer/seekdb_hipvs_hnsw_worker` 和非空 `build_release/lib/libseekdb_hipvs_hnsw.so`；其他 PACKAGE_TYPE 使用相应 build 目录。
- 配置和编译的非零返回值继续向上传递；缺少根构建脚本不再跳过并报告成功。
- 明确需要 CPU 构建时设置 `SEEKDB_BUILD_HIPVS=OFF`，两个开关均显式 OFF，避免复用旧 GPU 缓存。其他值拒绝接受。
- 固定该 shell 文件为 LF，防止 Windows 检出后直接上传导致 Bash 语法错误。

通用入口运行方式（原有必填环境保持不变）：

```bash
GITHUB_WORKSPACE="$PWD" SEEKDB_TASK_DIR=/tmp/seekdb-build-task \
  bash .github/script/seekdb/compile.sh
```

预期日志包含 `AMD backend=ON`；真正编译完成并通过产物检查后，才会输出 `GPU configuration and worker/bridge artifacts verified`。缺少 hipVS 依赖应在 CMake 中失败，而不是生成 CPU 版本。

## 验证及边界

Linux/WSL 上 `bash -n` 通过；运行下列命令，8 项回归全部通过：

```bash
python3 unittest/sql/vector_index/test_hipvs_compile_entry.py -v
```

测试执行真实 `compile.sh`，用隔离构建工具夹具检查传参、配置核对、缺失产物、缺失脚本及错误码传递，包含路径带空格和自定义依赖路径。这些测试没有重新编译完整 seekdb，也不是实际 GPU 内核或 SQL 验收。既有真实 GPU 构建和运行证据仍按原提交记录。

此次修复只覆盖 `.github/script/seekdb/compile.sh` 入口。直接调用 `build.sh` 时仍须显式传入 GPU 开关；CMake 默认 OFF 保持不变。不能据此证明官方使用该脚本，也不能将 `failMsg=8` 解释为漏传开关。官方测评若使用外部脚本、Bazel 或其他部署方式，仍需对应日志和入口命令核实。

旧材料 ZIP 保留其原始源码版本及哈希；本次入口修复以仓库中包含本文的 Git 提交为准，不将旧 ZIP 称为包含本次修复的新包。
