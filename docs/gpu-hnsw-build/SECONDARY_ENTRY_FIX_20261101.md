# 2026-11-01 补齐旧 Farm 编译及 obtest 部署入口

## 问题与修复

旧 `.github/script/seekdb/scripts/farm_compile.sh` 默认执行 `build.sh debug --init --make`。当前根构建命令已不接受 debug；隔离运行原脚本可复现退出码 2，且该脚本未传 GPU 开关。

现在 Farm 入口将 `WORKSPACE` 作为 `GITHUB_WORKSPACE`，复用 `.github/script/seekdb/compile.sh`，默认 release。旧调用者显式指定 `PACKAGE_TYPE=debug` 时，输出说明并映射到 release（RelWithDebInfo，带调试信息，不是原先 Debug 优化等级）。这样统一启用 GPU、配置核对、构建产物检查及错误码传递。`SEEKDB_BUILD_HIPVS=OFF` 仍可显式选择 CPU，`MAKE`、`MAKE_ARGS`、依赖目录选项由通用入口处理。

```bash
WORKSPACE="$PWD" SEEKDB_TASK_DIR=/tmp/seekdb-farm-task \
  bash .github/script/seekdb/scripts/farm_compile.sh
```

旧 `tools/obtest/copy.sh` 仅部署主程序为 `bin/observer`，没有复制本实现所需的 worker 和 bridge；构建目录还按当前工作目录选择，未遵从传入的源码目录，并优先选择过时的 build_debug。

现在按源码目录查找构建目录，优先 release、sanity；可以用 `SEEKDB_BUILD_DIR` 明确指定。读取实际 CMake 的 HIPVS/CUVS 开关，GPU 模式部署：

- `src/observer/seekdb` → `bin/observer`
- `src/observer/seekdb_hipvs_hnsw_worker` → `bin/seekdb_hipvs_hnsw_worker`
- `lib/libseekdb_hipvs_hnsw.so` → `lib/libseekdb_hipvs_hnsw.so`

主程序和 worker 保持可执行，目录布局符合 worker 基于主程序路径的查找及相对 RPATH。缺少主程序、GPU worker 或 bridge 时返回失败，复制错误也传递给调用方。CPU 配置不会部署旧构建残留的 GPU 文件。第三方 hipVS/ROCm 运行库仍须在运行环境中提供。

脚本的 `-l` 参数表示只做本地部署，可以跳过旧脚本的内网下载：

```bash
# 在目标部署目录执行，源码路径和构建路径请换为实际位置。
SEEKDB_BUILD_DIR=/path/to/source/build_release \
  bash /path/to/source/tools/obtest/copy.sh -l /path/to/source
```

## 验证

两个 shell 文件的 `bash -n` 通过。Linux/WSL 下共 17 项检查通过：

```bash
python3 -m unittest discover -s unittest/sql/vector_index -p test_hipvs_compile_entry.py -v
python3 -m unittest discover -s unittest/sql/vector_index -p test_hipvs_obtest_copy.py -v
```

11 项编译入口检查执行真实通用/Farm 脚本，以隔离构建工具夹具验证默认 GPU、旧 debug 兼容、CPU 选择、配置和产物校验、非零退出码传递。6 项部署检查执行真实 obtest 脚本的 `-l` 路径，以本地文件和 libtool 夹具验证三类产物复制、CUVS 别名、带空格路径、明确构建目录、CPU 残留文件、缺失主程序/桥接库和复制失败。测试未访问内网下载地址。

这是入口和部署回归，不是完整 seekdb GPU 重编译、真实 ELF 动态链接或 SQL 验收。未知官方构建入口、完整 CPack/Bazel GPU、评测环境的依赖兼容问题仍未确认；本修复不能作为 `failMsg=8` 的原因证明。旧材料 ZIP 保留各自记录的源码版本，本次代码以 Git 提交为准。
