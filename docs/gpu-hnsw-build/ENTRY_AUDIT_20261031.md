# 2026-10-31 构建入口、部署与完整重建验收

本轮在没有平台失败日志的条件下，自行复现并修复构建/部署缺口，再从空目录完整编译并执行真实 SQL。没有将这些缺口直接认定为历史平台失败原因，也没有据本地实测预报官方分数。本轮未修改 GPU 构图或 HNSW 导入算法。

## 修复及验证来源

- `aa2833bffa999576591756415c2b4203c41427cf`：GPU worker 与桥接库归入 `server` 安装组件，位置与主程序查找方式一致；补齐相对运行库搜索路径和 `copy.sh` 支持文件。
- `b35b7d4c824d13c71737332b1c5ce333b5f1ff28`：课程开关 `OB_BUILD_CUVS=ON` 等价启用 `OB_BUILD_HIPVS`；同时指定冲突值时明确失败；登记两个新增私有头文件。
- `a29bfc4db`：`copy.sh` 按当前 CMake 配置判断 GPU 开关，忽略切回 CPU 后的残留 worker；GPU 配置缺 worker/bridge 或缺主程序时返回非零。

完整 C++ 编译与安装后 SQL 验收针对 **b35b7d4**。此后只修改部署 shell 脚本、文档和证据。最终材料的 17 个实现/构建/验证文件与完整构建一致，另一个 `copy.sh` 单独执行真实部署及配置/失败分支检查；18 个发布文件均有 SHA-256，见 [源码对应关系](results/radeon-20261031-entry/release-source-provenance.json) 和 [发布源码哈希](results/radeon-20261031-entry/release-source-sha256.json)。未声称最后的文档提交再次从空目录编译过。

## 已复现的入口缺口

原实现只认 `OB_BUILD_HIPVS`。传课程参考所用的 `OB_BUILD_CUVS=ON` 时 CMake 虽然配置成功，却没有 `OB_BUILD_HIPVS` 编译宏和 worker 目标，并提示变量未使用。修复后实际运行六组 CMake 配置：课程 ON/OFF、原生 ON、两者 ON、冲突值及均未设置，全部符合预期。默认仍为 OFF，没有自动开启 GPU。

原 worker/bridge 属于 `Unspecified` 安装组件，CPack 选择的 `server;libs` 不包含它们；worker 原安装位置 `bin` 也不与主程序的 `usr/bin` 对齐。原 `copy.sh` 仅复制主程序。修复后 worker 位于 `usr/bin`，bridge 位于 `usr/libexec/seekdb/lib`，均归入 `server`；`copy.sh` 使用 `bin`/`lib` 布局。

两个新增私有头文件原先未登记到 `oblib_header_inventory.bzl`。本次登记增量与赛事基线到本实现的头文件生成器输出增量一致。严格模块边界检查无新增违反。**完整 oblib terminal 检查未通过，Bazel GPU 构建也未验收**：基线另有六个既存头文件遗漏及 Rust 依赖约束问题，本机 Bazel 还受 Xcode license 阻塞，远端未安装 Bazel。

## 本次完整构建与 SQL

Linux x86-64、gfx1100、ROCm 7.2.4、16 CPU 配额、55 GiB 内存。使用独立 Git 检出和空的 `build_release`：

```bash
./build.sh release -DOB_BUILD_CUVS=ON -DCARGO=/path/to/rust-1.98.1/bin/cargo
make -C build_release -j8 seekdb
```

本次没有传 `OB_BUILD_HIPVS=ON`。实际编译宏含 `OB_BUILD_HIPVS`；9,248 个已跟踪文件构建前后哈希完全一致，二进制 `--version` 含完整 b35b7d4 提交号。原始主程序 SHA-256 为 `cf20664c40464ddb204bb36f64f3e1a3e1308eb211bad5b200a9beca1167c04e`。

执行 GPU 子目录实际生成的 `server` 组件安装脚本，将本次新编译的主程序放入同一 `usr/bin`，**不设置 `LD_LIBRARY_PATH`**，使用新数据库和端口 2895：

- 27 项真实 SQL 合约检查全部通过。
- 50,000 × 128、M=32、100 条查询，CPU/GPU 各三次 fresh 建索引；包含共同的首次可用性查询。
- CPU/GPU 中位数 **24.798575966 / 3.187198209 秒**，比值 **7.780682072 倍**。
- CPU/GPU 构建 Recall@10 为 **0.996 / 1.000**，独立精确近邻和精确 SQL 交叉检查均通过。
- 更新、删除、重插和过滤通过；正常重启后 Recall@10 **0.999**，达到本地 0.99 门槛。
- 测试数据库正常停止。本轮重启没有隐藏 GPU；此前 0e86 的隐藏 GPU 重启证据单独保留，不混写为本次步骤。

本次安装验证使用实际 GPU 子目录组件脚本，不等同于整个 `server` 组件或完整 CPack 包安装验收。

## 额外输入与部署检查

五组输入使用已通过完整验收的 0e86 主程序和前一轮修复的安装布局。每组 CPU/GPU 各三轮，共 30 次 fresh 构建，并检查 DML、过滤和重启：

| 用例 | 行数 × 维度 | M | 数值缩放 | 重启 Recall@10 |
| --- | ---: | ---: | ---: | ---: |
| 奇数维 | 2048 × 3 | 16 | 1 | 1.0 |
| 小数据、高 M | 257 × 7 | 64 | 1 | 1.0 |
| 高维 | 16384 × 768 | 32 | 1 | 1.0 |
| 大数值 | 4096 × 128 | 16 | 1e5 | 1.0 |
| 小数值 | 4096 × 128 | 16 | 1e-8 | 1.0 |

全部通过。高维 CPU 某轮 Recall 为 0.995，GPU 为 1.0。没有在这些输入上复现算法或数值范围错误，因此没有基于 half 预处理的推测改写算法。这些结果扩大正确性覆盖，不代表所有规模均加速；此前 4096 × 256、M=64 的实测比值只有 0.8788 倍。

最终 `copy.sh` 另执行一次真实 b35b7d4 GPU 产物部署，版本号核对通过，动态链接器在不设置 `LD_LIBRARY_PATH` 时解析到部署目录的 bridge。配置/错误分支用隔离文件夹验证：GPU 缺 worker、缺 bridge、缺主程序均返回 1；CPU 的干净及残留 worker 两种配置均返回 0 且不复制 GPU 文件。分支检查中的主程序使用小型文件夹样本，不将它们表述为额外 CPU 二进制编译或 SQL 验收。

## 材料与尚未通过的范围

本轮 [原始结果目录](results/radeon-20261031-entry/) 保留完整构建状态、编译日志、SQL 合约/性能 JSON、输入矩阵、部署检查和源码/二进制哈希。历史材料未重写；核验 2026-10-30 结果必须使用其原始 `--source-ref 0e86b8c38afaf8fe3359041645c9b53c5a8524fe`。

完整 TGZ/CPack 配置仍被赛事基线缺失的 `tools/systemd/profile/telemetry.sh.template` 阻塞，依赖缓存也没有 obshell。本轮没有制造占位文件绕过，**完整 CPack 打包未通过**。GPU 运行仍依赖配置的 hipVS/ROCm 库，源码材料 ZIP 不包含这些第三方运行库。

本地加速与正确性通过不能证明未知隐藏评测全部通过。赛方未提供可用首错，实际评测构建和部署入口仍未知；上述缺口是自行复现的确定问题，历史失败原因仍未证实。
