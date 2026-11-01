# 2026-11-01 原目录、单文件修复验证

针对用户提供的原始 `seekdb_hipvs_batch_demo_executed.ipynb` 修复，保持文件名及 `/workspace/seekdb_hipvs_notebook` 工作目录，数据库仍位于 `/workspace/run/obs1`。采用[原目录单文件修复版](../../original-file/seekdb_hipvs_batch_demo_executed.ipynb)，不依赖此前的 `seekdb-fixed-*` 解压目录，不要求替换磁盘上的原 `lib/seekdb_demo.py`。

## 实际验证

- 全新内核顺序执行 11 个代码单元，执行编号 1–11，无异常输出。
- 最后打印 JSON 与 `summary.json` 完全相同，`completed` 和 `sql_results_validated` 均为 true。
- 原 helper 的执行前后 SHA-256 相同，均为 `adfcc0f0a44c075327716497218beeb9c95fccd63bf10eeab44ba73f547497cb`。
- 再次调用启动函数，进程 PID 前后均为 375，复用已启动实例，没有删除原目录或创建重复 observer。

[已执行 Notebook](executed.ipynb) · [JSON](summary.json) · [CSV](summary.csv) · [图表](seekdb_hipvs_speedup.png) · [执行与源码哈希](execution.json) · [启动复用检查](idempotence.json) · [日志](notebook-execution.log)

| 探针数 | CPU ms | GPU ms | 查询加速比 |
| ---: | ---: | ---: | ---: |
| 100 | 318.7 | 1170.2 | 0.27× |
| 500 | 1488.9 | 975.0 | 1.53× |
| 1000 | 2947.6 | 983.3 | 3.00× |
| 2000 | 5882.8 | 1120.2 | 5.25× |
| 4000 | 11802.0 | 1265.0 | 9.33× |

CPU/GPU 单查为 7.7549/13.2206 ms，CPU/GPU 批量 Recall@10 为 0.640/0.882，GPU 单查为 0.866，GPU 单查与批量 top-10 集合完全相同为 14/100。未把原课程 12.7× 输出或此前其他轮次结果作为本次成绩。

验证程序从临时输入文件读取修复版，以原目录为工作目录执行，输出写入本 results 目录；没有覆盖用户正在 Jupyter 编辑的云端 Notebook。用户须按[替换步骤](../../original-file/README.md)上传本机同名修复文件，才能让原来的交互标签页使用修复代码。

这里证明原目录中的 Notebook 修复可运行，不证明比赛平台必须修改这个目录，也不说明此前提测失败的根本原因。指标仍是课程预装二进制的批量查询结果，不作为 HNSW 索引构建加速成绩。Docker 状态与 2026-10-31 报告分开，本轮没有执行 Docker 部署。
