# seekdb：使用 AMD hipVS 加速 HNSW 索引构建

**2026-11-01 当前验收（4855acac7）**：最小镜像中29个真实RPM无系统安装完成初始化，28项Linux回归、完整GPU TGZ构建、真实CPack、27项SQL、六轮CPU/GPU、DML/过滤及隐藏GPU重启均通过；中位数24.9432/2.9940秒，加速8.3310倍。独立空缓存Rust安装最终完成（约61分钟，原包装器一小时超时单独保留），TGZ和默认Release真实入口通过初始化/配置检查。版本、缓存条件、日志轮转限制和原始证据见[本轮报告](COLD_ENTRY_AUDIT_20261101.md)。以下较早结果保留原版本归属；本地验收不能替代正式成绩。

**2026-11-01 原生入口排查**：在镜像默认 PATH 下复现原入口因缺 RPM 解包工具退出 5，原 Rust 初始化因缺 rustup 退出 127。新增精确 Rust 版本准备、失败阶段日志及冷缓存前提诊断；23 项 Linux 回归通过，真实依赖初始化及 GPU 配置通过，并刻意在主编译前停止。系统 RPM 解包工具仍须由构建环境提供。验证范围、缓存条件和官方根因未确认的边界见 [环境排查报告](ENTRY_ENVIRONMENT_AUDIT_20261101.md)。

**2026-11-01 最新完整验收**：已恢复 13 个上游打包原件，真实 TGZ 配置、完整 GPU 编译、CPack 两个运行包、解包启动、27 项 SQL、六轮 CPU/GPU 构建、增删改/过滤及隐藏 GPU 后重启均通过。本次 CPU/GPU 中位数 24.6685 / 3.1972 秒，加速 7.7157 倍，GPU Recall@10 为 1.000 / 0.999 / 0.999，重启后 1.000。编译提交 cfae4ba9、仅增时区数据的包提交 f18737e6 与后续文档提交分别追溯，见 [本轮完整报告](PACKAGING_AUDIT_20261101.md)及[原始证据](results/radeon-20261101-package/README.md)。下文旧日期的“完整 CPack 未通过”是当时状态；官方成绩仍以平台回执为准。

**2026-11-01 旧入口补齐**：Farm 编译入口已复用 GPU 构建校验，修复默认使用不支持的 debug 命令；obtest 部署入口补齐 GPU worker/bridge 和失败检查。编译及部署共 17 项针对性回归通过，范围及运行方式见 [旧入口修复说明](SECONDARY_ENTRY_FIX_20261101.md)。未声称已定位或修复官方 `failMsg=8`。

**2026-11-01 通用编译入口修复**：本 AMD 分支的 `.github/script/seekdb/compile.sh` 默认显式启用 CUVS/HIPVS，并核对配置及 GPU worker/bridge 产物，避免该入口静默生成 CPU 版本。8 项入口回归通过；未将其视作官方 `failMsg=8` 的已确认原因，也未声称本次完整重编译 GPU。用法、CPU 构建开关及验证范围见 [入口修复说明](COMPILE_ENTRY_FIX_20261101.md)。

**最新排查与安装修复**：普通 Linux Release 的 `server` 组件已补齐 `seekdb` 主程序安装，避免安装返回成功但只有 worker/bridge；另增加外部 Cargo 并行数配置，允许资源受限时将嵌套编译也限制为单任务。新增部署检查、复现记录和仍未通过的完整打包/Bazel 路径见 [再次排查报告](READINESS_AUDIT_20261031.md)。本轮新实例的完整 GPU 验证因 SSH 拒绝连接而中断，用户报告“内核意外死亡”；以下历史成绩不代表本轮重新测量。

**旧版独立复跑（2026-10-31）已完成**：新实例从已发布提交 `0e86b8c38afaf8fe3359041645c9b53c5a8524fe` 重新编译，CPU/GPU 构建中位数为 26.4873 / 3.3030 秒，加速 **8.0192 倍**。GPU 三轮 Recall@10 为 1.000 / 0.999 / 1.000；27 项 SQL、14 项回归、增删改/过滤和隐藏 GPU 后重启均通过。详见[独立复跑报告](RECHECK_20261031.md)及[原始结果](results/radeon-20261031-recheck/)。当前发布版本另含下节构建入口/部署修复及其验收，不将旧版复跑成绩记为修复版成绩。

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
export CARGO_BUILD_JOBS=1
./build.sh release --init -DOB_BUILD_HIPVS=ON -DSEEKDB_EXTERNAL_CARGO_JOBS=1 \
  -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm
make -C build_release -j1 seekdb
```

依赖缓存必须对应当前源码清单；不能仅凭镜像存在旧目录就跳过初始化。此前完整 GPU 验收复用了镜像的 `/opt/seekdb-deps-cache/.../3rd`，本次原生入口补验另按精确清单真实初始化私有缓存。冷缓存的 Ubuntu 环境需先安装 `rpm2cpio/cpio`，见环境排查报告。主程序为源码重新构建，未使用镜像预装的 seekdb 演示二进制。

Windows 导出源码时须保留 Linux LF 换行。镜像自带 Rust 1.97.1 低于仓库锁定版本；当前 native init 和通用编译入口会准备并核对 1.98.1。此前完整 GPU 验收使用手工准备的独立工具链，其官方包校验和解析后的锁文件仍按原始证据保留；不能据此宣称此前已验证自动初始化入口。

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

10 月 30 日构建使用 Git archive 加本地源码增量，该轮二进制内嵌 revision 为空，应联合核对其源码快照、源码/二进制哈希和日志。10 月 31 日有两组独立证据：构建入口修复版的完整重建见上节和 ENTRY_AUDIT；本次旧版对照复跑采用真实 Git 浅克隆同步，二进制报告 `1-0e86b8c38afaf8fe3359041645c9b53c5a8524fe`，16 个测量相关源码哈希与该提交一致。两组记录不混用。

## 证据与边界

- [本轮完整原始结果目录](results/radeon-20261030-cuvs/)：SQL、逐查询召回、真实 GPU 事件、内核 CSV、环境、源码哈希、构建与重启日志。
- [构建原理及限制](README.md)、[历史和本轮报告](REPORT.md)、[视频对应关系](VIDEO_ALIGNMENT.md)、[云实例创建步骤](RADEON_CLOUD_SETUP.md)。
- 仅支持 dense float32 L2 HNSW、无 extra-info、M ∈ {16,32,64}。微小索引明确回 CPU，大索引的 GPU 错误不静默降级。
- 本次是单一 gfx1100、固定合成数据的结果，未声称覆盖百万级数据、其他 GPU 或尚未公布的官方评分阈值。
- 复用的 seekdb、AMD hipVS 与 VSAG 代码遵循各自许可证；算法与参考来源见 README。配置不包含真实密码、API Key 或私钥。

提交材料包为同目录的 `seekdb-hipvs-submission.zip`，包含本说明、实现相关源码、测试、原始证据和相对赛事基线的补丁；完整 seekdb 工程由仓库提供，材料包不是独立的完整源码镜像。校验文件为 `seekdb-hipvs-submission.zip.sha256`。

测评平台填写本仓库地址、分支及最终推送的 commit hash，并上传与该版本对应的材料包。
