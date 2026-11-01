# 2026-10-31 全新内核复跑与课程声明核对

本轮在 Radeon Cloud 上真实执行。查询演示完成；指定 Docker 镜像的重新部署因运行环境缺少 Docker 且禁止用户命名空间而未完成。不能把本报告写成 Docker 全流程通过，也不是 HNSW 索引构建成绩。

## Notebook 从头执行

以用户下载文件作为输入，执行器清空旧输出，在全新内核按顺序执行 11 个代码单元；执行编号严格为 1–11，无异常输出，最后打印的 JSON 与保存的 `summary.json` 相同。本轮没有沿用用户下载文件里交错的旧执行状态。

[已执行 Notebook](executed.ipynb) · [JSON](summary.json) · [CSV](summary.csv) · [图表](seekdb_hipvs_speedup.png) · [执行日志](notebook-execution.log) · [源码/二进制/数据哈希](execution.json) · [文件核对](file-checks.json)

| 探针数 | CPU ms | GPU ms | 查询加速比 |
| ---: | ---: | ---: | ---: |
| 100 | 320.6 | 861.9 | 0.37× |
| 500 | 1481.2 | 1166.1 | 1.27× |
| 1000 | 2946.3 | 969.6 | 3.04× |
| 2000 | 5919.1 | 1113.5 | 5.32× |
| 4000 | 11703.1 | 1210.2 | 9.67× |

本轮单查 CPU/GPU 为 7.6795/12.3280 ms，CPU/GPU 批量 Recall@10 为 0.640/0.892。Notebook 中 GPU 单查/批量 top-10 集合完全相同为 21/100；GPU 单查 Recall@10 为 0.877。

`execution.json` 的输入 Notebook SHA-256 对应 [原样下载文件](../user-run-20261031/downloaded.ipynb)，不是根目录清空输出版的整文件哈希；两者代码单元一致。执行器只将新的执行结果写入本目录。`binary_version` 字段为空是该二进制 `--version` 的 stdout 为空；下方 SQL 核对记录中的 `SELECT version()` 返回 `5.7.25-OceanBase seekdb-v1.4.0.0`。

## 额外核对 CPU/GPU 路径及结果一致性

Notebook 完成后，另行执行 [verify_course_paths.py](../../verify_course_paths.py)，结果保存在 [path-checks.json](path-checks.json)，含两表 DDL、逐查询近邻 ID、召回和比较结果。该核对是后续单独运行，不能与上面的 Notebook 测量值混用。

确认两张表都是 10000 行，主键集合相同，逐向量 L2 差异为零。GPU 表声明 `lib=cuvs`，CPU 表声明 `lib=vsag`。本脚本未设置 GPU 选择环境变量，实测进程环境中也没有 HIP/ROCR/CUDA 可见设备变量。

| 对照 | top-10 集合完全相同的查询数 | 平均 top-10 重合比例 |
| --- | ---: | ---: |
| CPU 单查 vs GPU 单查 | 0/100 | 0.642 |
| CPU lateral vs GPU 批量 | 3/100 | 0.648 |
| CPU 单查 vs CPU lateral | 100/100 | 1.000 |
| GPU 单查 vs GPU 批量 | 18/100 | 0.866 |
| 两次 GPU 批量 | 21/100 | 0.857 |

还将 `t_vsag` 传给同一个 `dbms_vector.batch_knn` 过程：调用成功，但返回结果仅 2/100 与 CPU lateral 的 top-10 集合完全相同。**过程接受带 CPU 索引的表，不足以证明过程内部使用 CPU**；没有把这个现象写成“只改 lib 后同一个批量过程已纯 CPU 执行”。

因此，课程文字中的“结果一致”不能作为本轮已验证结论。返回数量、ID 范围和重复检查通过，但近似结果集合不同；不将这种不同改写为完全相同，也未进一步证明其内部算法原因。

## Docker 原命令的实际尝试

[environment.json](environment.json) 保存了原始 `docker run` 参数及失败信息：

- 当前云容器没有 `docker`、`podman` 和 `/var/run/docker.sock`，实际尝试返回 127。
- `/dev/kfd` 存在；实际 render 设备是 `/dev/dri/renderD128`，原命令写的 `renderD135` 不存在。
- `unshare -Ur true` 返回 `Operation not permitted`。没有通过安装客户端将本环境描述为具备容器运行权限。
- Windows 本地也未提供可运行该命令的条件：Docker Engine 未启动，WSL 中没有 `/dev/kfd`。

提供了 [reproduce_docker.sh](../../reproduce_docker.sh)，在具备 Docker Engine 和 ROCm 设备的 Linux GPU 主机上自动选择实际设备及组 ID，拉取原文指定镜像、记录镜像 ID、复制完整配套代码、启动容器并运行 Notebook 和额外检查。当前环境只执行到了该脚本的 Docker 缺失检查；镜像拉取、容器启动及容器内执行未完成。

补充尝试 `rocprofv3 --attach` 对现有 observer 采样，受到 `ptrace Operation not permitted` 限制；[状态与完整报错](profiling/profile-status.json)保留。没有采集到本轮 GPU 内核轨迹，不能将该次 attach 记为成功。

## 如何复现

已有课程镜像环境，在工程目录运行：

```bash
python run_demo.py --output results/new-run/executed.ipynb
cp results/summary.json results/summary.csv results/seekdb_hipvs_speedup.png results/new-run/
python verify_course_paths.py --output results/new-run/path-checks.json
```

全新 Docker 部署须在上述 Linux GPU 主机执行：

```bash
bash reproduce_docker.sh
```

脚本默认将结果写入被 Git 忽略的 `notebook_workspace/course-reproduction/results/docker-reproduction/`，拒绝覆盖已有工作区，容器名也不覆盖已有容器。Jupyter 绑定主机 `127.0.0.1:18890`；远程主机可通过 SSH 本地端口转发访问。GPU 后端仍由 SQL 参数选择；启动器自动补齐动态库路径，不要求人工设置 GPU 开关。

容器设备透传依据 [AMD ROCm Docker 文档](https://rocm.docs.amd.com/projects/install-on-linux/en/latest/how-to/docker.html)及 [Docker 运行文档](https://docs.docker.com/engine/containers/run/)。没有改变原文的镜像引用，也没有用本轮课程二进制的 SHA-256 冒充尚未拉取镜像的 digest。
