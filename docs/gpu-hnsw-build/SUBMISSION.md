# seekdb：使用 AMD hipVS 加速 HNSW 索引构建

本次提交在 seekdb 内实现完整 SQL 建索引链路：GPU 通过 hipVS 的 NN-descent/CAGRA 构建底层图，CPU 构建 HNSW 上层，将结果校验、映射到原始 VID 后导入原生 VSAG。后续查询、过滤、增删改和持久化沿用 seekdb 的原生路径。

视频中的 `LIB=CUVS` 与 `LIB=HIPVS` 均可选择 AMD 构建后端。这里测量的是 **HNSW 构建**，没有把视频中的批量查询加速数字作为构建成绩。

## 仓库与分支

- 仓库：<https://github.com/sevenmiles666/psfseekdb>
- 提交分支：`competition/amd-hipvs-build`。
- 赛事基线：`5bce9e06d1281347ad61344e4b01fab9158c58a2`，是当前分支的祖先。
- 本轮修改基于该分支已有实现 `c393a801db3b3ad1640b5445ed292799101d3e11`；完整源码以包含本文的提交为准。
- 本题按用户提供的新题面在 seekdb 中实现，原有 Qoder/PowerContext 项目未混入本次提交。

## 最新验收（2026-10-31）

本轮自行复现并修复了课程编译开关未启用 GPU、组件安装漏文件/布局不一致、部署复制不完整和新增头文件未登记等入口缺口。没有平台原始失败日志，不能据此断言历史失败原因。

在 b35b7d4c824d13c71737332b1c5ce333b5f1ff28 的独立检出中，仅传 `OB_BUILD_CUVS=ON`，从空目录完整编译 seekdb。9,248 个已跟踪文件构建前后哈希不变，二进制内嵌完整提交号。使用实际 GPU 子目录 `server` 组件安装脚本和新编译主程序，不设置 `LD_LIBRARY_PATH`，27 项 SQL 检查、三轮 CPU/GPU 对照、DML/过滤和重启全部通过。

50,000 × 128、M=32：CPU/GPU 中位数 **24.798575966 / 3.187198209 秒，加速 7.780682072 倍**；构建 Recall@10 **0.996 / 1.000**，重启后 **0.999**。另有五组输入、30 次 fresh 构建及重启检查，和最终 `copy.sh` 的真实 GPU 产物部署/配置失败检查。

最终发布中的 17 个实现/构建/验证文件与完整构建一致，`copy.sh` 的后续补充单独验过；后续文档和材料提交没有再次从空目录编译 C++。详见 [完整报告与限制](ENTRY_AUDIT_20261031.md)、[新证据及离线核验](results/radeon-20261031-entry/README.md)。完整 CPack 打包及 Bazel GPU 构建尚未通过，不能把上述组件运行验收等同于两者通过。

## 历史真实测量（2026-10-30，Radeon Cloud）

同一组 50,000 × 128 float32 向量、100 个独立查询，L2，M=32，ef_construction=200，ef_search=1000。CPU 与 GPU 各三轮，交替执行顺序；每轮重新建索引。计时包含 SQL 建索引至首条共同可用性查询完成，不包含数据生成、导入、精确真值计算和服务启动。

| 轮次 | CPU / VSAG（秒） | GPU / CUVS（秒） | CPU Recall@10 | GPU Recall@10 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 25.028745133 | 3.209281789 | 0.996 | 1.000 |
| 2 | 25.127499748 | 3.108347811 | 0.996 | 1.000 |
| 3 | 25.114934700 | 3.109182080 | 0.996 | 1.000 |
| 中位数 | **25.114934700** | **3.109182080** | | |

中位数之比为 **8.0776661036 倍**。GPU 构建日志包含 `complete gpu=true fallback=false`。构建之外单独执行的 profiler 记录 158 次 GPU 内核执行，其中 61 次为 NN-descent。

以下验证已通过：

- 27 项真实 SQL 检查：HIPVS/CUVS/混合大小写、能力限制、微小索引 CPU 路径及 12 次 CPU 重建。
- 六轮建索引均通过独立 NumPy 精确近邻、逐查询召回和精确 SQL 交叉检查。
- 更新、删除、重新插入、过滤检查，过滤 Recall@10 为 1.0。
- 关闭服务，以 `HIP_VISIBLE_DEVICES=-1` 启动同一数据库后，100 个查询 Recall@10 为 1.0。
- 隐藏 GPU 后直接执行构建 worker，返回明确 GPU 构建错误及非零状态，未输出成功索引。
- 14 项 Python 回归测试，以及新旧两组证据的一致性核验。

本轮未执行 mysqltest runner；云镜像没有该工具，使用仓库内可复现的 SQL 检查脚本。旧的约 8.208 倍数据单独保留在上一轮结果中，不与本次测量混合。

## 运行环境与构建

本次环境：Linux x86-64、AMD Radeon Graphics / gfx1100、51,522,830,336 字节显存，16 核 CPU 配额、55 GiB 内存配额，ROCm 7.2.4、hipVS C 库位于 `/opt/hipvs`，VSAG 1.1，clang 17.0.6、CMake 3.30.3、Rust/Cargo 1.98.1。完整版本、库及二进制 SHA-256 见 [final-environment.json](results/radeon-20261030-cuvs/final-environment.json)。

GPU 功能默认关闭；开启需要 glibc ≥ 2.34 及 hipVS 的 CAGRA/HNSW C API。先准备 seekdb 构建依赖和仓库锁定的 Rust 版本，再在仓库根目录执行：

课程参考中的 `-DOB_BUILD_CUVS=ON` 与下面的 `-DOB_BUILD_HIPVS=ON` 等价；二者都选择本项目的 AMD HNSW 构建实现。同时指定时值必须一致，避免配置成功但生成不符合预期的二进制。

```bash
./build.sh release --init -DOB_BUILD_HIPVS=ON \
  -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm
make -C build_release -j8 seekdb
```

已有依赖的课程镜像可按 seekdb 原构建说明复用依赖缓存，不必重复初始化。本次实际使用镜像的 `/opt/seekdb-deps-cache/.../3rd`。主程序为本次源码重新构建，未使用镜像预装的 seekdb 演示二进制。

Windows 导出源码时须保留 Linux LF 换行。镜像自带 Rust 1.97.1 低于仓库锁定版本，本次使用独立安装目录的 1.98.1，并核对官方安装包 SHA-256；清单见 [rust-packages.json](results/radeon-20261030-cuvs/rust-packages.json)。本次解析后的 Rust 依赖锁文件也已保存。

## 启动与验证样例

数据库目录必须位于支持 `fallocate` 的本地文件系统。本次云平台持久化 `/workspace` 为 NFS，首次启动报 `Operation not supported`，原始日志已保留；随后在实例本地磁盘启动成功。源码、二进制和证据保存到持久化目录。

在仓库根目录执行：

```bash
export LD_LIBRARY_PATH="$PWD/build_release/lib:/opt/hipvs/lib:/opt/rocm/lib:${LD_LIBRARY_PATH:-}"
export OMP_NUM_THREADS=8 OPENBLAS_NUM_THREADS=8
python3 .github/script/seekdb/sdb.py start \
  --binary "$PWD/build_release/src/observer/seekdb" \
  --base-dir /var/tmp/hipvs-test --port 2881 --parameter cpu_count=16
python3 .github/script/seekdb/sdb.py wait-ready \
  --client mysql --base-dir /var/tmp/hipvs-test --port 2881

python3 unittest/sql/vector_index/check_hipvs_sql.py \
  --database hipvs_contract_check --output /workspace/hipvs-sql-contract.json
```

预期 SQL 检查输出 `completed=true, checks=27`。部署时需让 `seekdb_hipvs_hnsw_worker` 与主程序同目录，并保留其桥接库及 hipVS/ROCm 运行依赖。

安装 NumPy，使用专用测试库执行完整样例；脚本自动生成固定种子数据、导入初始向量、建立 CPU/GPU 索引并记录预期行为：

```bash
python3 docs/gpu-hnsw-build/benchmark.py \
  --gpu-lib cuvs --rows 50000 --dim 128 --queries 100 --rounds 3 \
  --m 32 --ef-construction 200 --ef-search 1000 --min-recall 0.99 \
  --database hipvs_judge_benchmark \
  --server-log /var/tmp/hipvs-test/log/seekdb.log \
  --output /workspace/hipvs-judge-results.json
```

预期：六轮通过，GPU 轮次具有真实构建日志，Recall@10 ≥ 0.99，更新、删除、重新插入和过滤均通过，JSON 中 `completed=true`。实际加速倍数由硬件及运行状态决定，8.0777 倍是上述固定环境的实测值。

验证持久化：

```bash
python3 .github/script/seekdb/sdb.py stop --base-dir /var/tmp/hipvs-test
HIP_VISIBLE_DEVICES=-1 python3 .github/script/seekdb/sdb.py start \
  --binary "$PWD/build_release/src/observer/seekdb" \
  --base-dir /var/tmp/hipvs-test --port 2881 --parameter cpu_count=16
python3 .github/script/seekdb/sdb.py wait-ready \
  --client mysql --base-dir /var/tmp/hipvs-test --port 2881
```

重复上面的基准命令，额外增加 `--verify-existing`，保持其他参数和输出文件相同。预期 `restart_verification.status=passed`。该模式复用已持久化的索引，不重建数据。

## 无 GPU 时核对材料

```bash
python3 -m unittest discover -s unittest/sql/vector_index -p 'test_hipvs_*.py' -v
python3 docs/gpu-hnsw-build/verify_evidence.py \
  --results-dir docs/gpu-hnsw-build/results/radeon-20261030-cuvs \
  --source-ref 0e86b8c38afaf8fe3359041645c9b53c5a8524fe
```

前者应通过 14 项回归；后者核对 2026-10-30 的 0e86 提交所对应的 16 个源码文件、六轮原始计时、质量、重启和内核 CSV，输出 `verified=true`。后续入口与部署修复不改写这组历史证据。它是离线一致性检查，不能代替重新执行 GPU 测试。

2026-10-30 这轮构建使用 Git archive 加本地源码增量，因此二进制内嵌 revision 为空。应联合核对该历史目录的 `source-snapshot.json`、`source-sha256.json`、构建日志及实际二进制哈希；不以空 revision 声称其他提交已被验证。16 个测量相关源码哈希对应该历史版本。2026-10-31 新构建的版本号和证据见上节。

## 证据与边界

- [本轮完整原始结果目录](results/radeon-20261030-cuvs/)：SQL、逐查询召回、真实 GPU 事件、内核 CSV、环境、源码哈希、构建与重启日志。
- [构建原理及限制](README.md)、[历史和本轮报告](REPORT.md)、[视频对应关系](VIDEO_ALIGNMENT.md)、[云实例创建步骤](RADEON_CLOUD_SETUP.md)。
- 仅支持 dense float32 L2 HNSW、无 extra-info、M ∈ {16,32,64}。微小索引明确回 CPU，大索引的 GPU 错误不静默降级。
- 本次是单一 gfx1100、固定合成数据的结果，未声称覆盖百万级数据、其他 GPU 或尚未公布的官方评分阈值。
- 复用的 seekdb、AMD hipVS 与 VSAG 代码遵循各自许可证；算法与参考来源见 README。配置不包含真实密码、API Key 或私钥。

提交材料包为同目录的 `seekdb-hipvs-submission.zip`，包含本说明、实现相关源码、测试、原始证据和相对赛事基线的补丁；完整 seekdb 工程由仓库提供，材料包不是独立的完整源码镜像。校验文件为 `seekdb-hipvs-submission.zip.sha256`。

测评平台填写本仓库地址、分支及最终推送的 commit hash，并上传与该版本对应的材料包。
