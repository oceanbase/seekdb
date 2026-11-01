# 2026-11-01 完整构建、真实 TGZ 打包与解包运行

本轮修复了两个实际复现的打包缺口：缺少 `tools/systemd/profile` 原件导致 TGZ 配置失败，缺少 `tools/timezone_V1.log` 导致 CPack 安装阶段失败。全部 13 个文件从既有上游提交 `706253423cfbc7e907ebfe577109d74b152d6dbe` 原样恢复，逐文件 SHA256 与 Git blob 见 [来源记录](results/radeon-20261101-package/restored-package-provenance.json)。未修改 GPU 构图、查询或依赖版本。

以平台最新失败提交 `6dc14ba31001c7e6b1db2fdf7acba0f7c60a6f41` 为父版本，`cfae4ba92b14d3010f7c7aa6a87d064dfd7c0075` 恢复 12 个 profile 并完成从空目录开始的完整 GPU TGZ 构建；`f18737e699ad0b29e38759fa821fec79e3940545` 仅再恢复时区数据，用同一二进制完成实际 CPack 和解包运行。最终交付另合并远端 `5e35ceace74b4533ff88adfea84e58ecb2d0f42d` 的 Farm/obtest 入口修复，其 17 项 Linux 入口/部署回归单独执行，原始输出也收录在本轮证据。该合并只改变 shell 入口、对应测试和换行属性，没有改 C++、GPU 或 CMake 源码。文档、证据和材料包随后更新，未声称合并版再次全量编译。

## 真实验证结果

- 完整 `seekdb` GPU 构建、admin SQL 生成、`make all`、CPack TGZ 均通过。
- 实际生成 server/libs 两个 TGZ，解包检查主程序、obshell、worker、bridge、libaio、配置及 service 文件；两个可执行文件 `ldd` 无缺失项。
- 不设置 `LD_LIBRARY_PATH` 或 `LD_PRELOAD`，使用解包后的 `usr/bin/seekdb` 启动独立实例；27 项 SQL 合约通过。
- 50,000 × 128 float32、M=32、100 个独立查询、CPU/GPU 各三轮。每轮重建索引，计时包含 SQL 建索引至首次查询可用，逐查询校验使用独立精确真值。
- CPU 中位数 **24.668517491 秒**，GPU 中位数 **3.197188232 秒**，比值 **7.715691320**。CPU Recall@10 均为 **0.996**，GPU 三轮为 **1.000 / 0.999 / 0.999**，均超过 0.99 门槛。每轮 GPU 日志包含 `complete gpu=true fallback=false`。
- 增删改、过滤均通过；停止后以 `HIP_VISIBLE_DEVICES=-1` 重启同一数据库，100 个查询 Recall@10 **1.000**，精确 SQL 交叉检查通过。
- 9,408 个原始跟踪文件在完整构建及运行期间哈希不变；二进制内嵌 cfae 完整提交号。结束后停止专用数据库，端口 2897 已关闭。

主程序 SHA256：`37d0e7c44c0395c1353fc84578199af2c04b44eca4329114d806b0ff65f5c61d`。

真实包清单：

| 包 | 字节 | SHA256 |
| --- | ---: | --- |
| seekdb-1.4.0.0-1-linux-x86_64.tar.gz | 317319243 | 40f2aece778988bdcfa7ea759994d5bbe6783d36d039600310ba54ceb5718773 |
| seekdb-libs-1.4.0.0-1-linux-x86_64.tar.gz | 9857 | c08674085cdd40bebbaac0b5dfd2b6ed5b00c8740a589dfd2811cc42b4b51145 |

## 复现与日志

新 Radeon 实例使用 gfx1100、16 CPU 配额、55 GiB 内存、ROCm/hipVS 镜像依赖。使用官方 Rust 1.98.1 校验过的私有安装，主 Rust 锁文件来自已发布的 `radeon-20261031-recheck/rust-workspace-Cargo.lock`；两个锁文件共 123 个包逐一校验后离线编译。依赖缓存未被写入；obshell 从仓库指定的官方 RPM 校验提取到私有目录，来源见证据。没有安装系统服务或执行包安装钩子。

实际构建命令：

```bash
./build.sh tgz -DOB_BUILD_CUVS=ON -DSEEKDB_EXTERNAL_CARGO_JOBS=1 \
  -DLTO_JOBS=2 -DHIPVS_ROOT=/opt/hipvs -DROCM_ROOT=/opt/rocm \
  -DCARGO=/workspace/amd-acceptance-20261101-6dc14ba3/tooling/rust-1.98.1/bin/cargo
make -C build_tgz -j4 seekdb
make -C build_tgz -j2 generate_syspack_source
make -C build_tgz -j2
cpack --config build_tgz/CPackConfig.cmake -G TGZ -B /workspace/amd-acceptance-20261101-6dc14ba3/packages
```

构建起初 `-j2`，确认资源后用既有对象续接 `-j4`；嵌套 Cargo 始终单任务，ThinLTO 限制为两个任务。未修改系统 Rust 或共享镜像依赖。

TGZ 默认日志级别是 DBA_WARN，首次解包基准因未取得 INFO 级 GPU 成功标记而停止，未记为成功。最终验收启动显式增加 `--parameter syslog_level=INFO`，保留原失败日志并重新执行所有运行验证；没有删除路径证明要求。首次缺时区数据的 CPack 错误也保留在证据中。

[最终状态](results/radeon-20261101-package/package-status.json)、[完整 SQL 结果](results/radeon-20261101-package/package-sql.json)、[SQL 合约](results/radeon-20261101-package/package-contract.json)、[文件与二进制追溯](results/radeon-20261101-package/final-provenance.json)及[日志和实际脚本](results/radeon-20261101-package/README.md)。

## 官方测评边界

本轮只将实际完成的 TGZ 构建和运行记为通过；RPM/DEB、Bazel GPU 路径未在本轮验证。单一 gfx1100 和上述合成数据的加速结果不能替代官方成绩或其他输入规模的性能结论。

本轮开始时平台任务 47236537 执行 32 秒失败、`failMsg=8`、成绩 0，详情接口没有执行日志。尚无证据把上述实际打包缺口认定为该任务的历史根因；官方成绩以修复版提测后的平台回执为准。PR 的 CLA 仍由提交者本人处理。
