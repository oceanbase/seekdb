# 与 8 月 19 日 AMD 视频的对应关系

参考：用户提供的 `8月19日-母老师.mp4`，约 22 分 30 秒；对应[课程页面](https://open.oceanbase.com/course/detail/16684)。已提取关键画面并完成全部音轨的本地机器转写；技术名称、版本与数值以可读课件交叉核对。原视频、截帧及完整转写没有加入代码仓库。

| 视频时间 | 课程内容 | 本题如何采用 |
| --- | --- | --- |
| 01:35–03:30 | hipVS 面向 AMD ROCm，沿用 cuVS 兼容 API | 使用 hipVS 的 `cuvsCagraBuild`、`cuvsHnswFromCagra` 等 C 接口，不把 cuVS 名称误认为必须使用 NVIDIA |
| 04:03–06:33 | `seekdb SQL → obvsag C-API → C bridge → hipVS libcuvs → ROCm`，SQL 使用 `lib=cuvs` | 两条参数解析路径均增加 `LIB=CUVS` 到 `VIAL_HIPVS` 的映射；保留 `LIB=HIPVS` |
| 约 04:30 / 06:00 | 薄 C 接口隔离 C++ ABI，按索引选 CPU/GPU，示例的过滤、删除、非 L2 回 CPU | 默认关闭 GPU 构建；独立 worker 隔离库符号；原生索引仍由 CPU 查询、过滤与更新。不支持的 GPU **构建声明明确报错**，不伪装成 GPU 加速 |
| 06:33–08:39 | 单查询 CPU 8.17 ms、GPU 12.57 ms；批量查询 N=4000 时峰值约 12 倍，另展示 Recall@10 | 这是**查询**演示，不是 HNSW **构建**加速，也不是已公布的评分门槛。本题另测完整 SQL 建索引至首条可用查询的时间和独立近邻召回率 |
| 09:00–15:30 | Radeon Cloud 注册、算力、先配 SSH 公钥再启动实例、seekdb-hipvs Notebook | 见 [实例创建与连接](RADEON_CLOUD_SETUP.md)，不在 Windows 上模拟 AMD GPU 性能 |
| 17:00–19:32 | Notebook 检查 AMD Radeon / gfx1100、ROCm、seekdb 与 C bridge，并比较批量查询 | 实测前核对软件版本、实际 GPU 与源码；镜像预装演示二进制不能替代本项目修改后编译的 seekdb |
| 19:32–21:30 | Profile 中保存公钥，再通过实例 SSH 入口连接开发工具 | 先保存 `.pub` 再创建实例，不自动删除已有实例，不索取密码、私钥 |

课件中的环境显示 gfx1100 / RDNA3、约 49,136 MB 显存、ROCm 7.2.4；不能采用机器转写中识别错误的显存数字。视频介绍的活动算力时长是录制时说明，当前额度以本人账号与活动规则为准。

## SQL 名称与实际行为

```sql
CREATE VECTOR INDEX ann ON documents(embedding)
  WITH (distance=l2, type=hnsw, lib=cuvs,
        m=32, ef_construction=200, ef_search=1000);
```

本分支中 `LIB=CUVS` 是 **AMD hipVS 构建**的别名，不是 CUDA 后端。DDL 检查归一为 HIPVS，运行时也映射为相同枚举；类型、距离、M 与 `OB_BUILD_HIPVS` 检查仍然生效。输出参数可能规范化为 HIPVS。

视频的 `CALL dbms_vector.batch_knn(...)` 属于批量查询演示，本次未实现或声称实现这个过程。题面要求 HNSW **构建**加速，因此保留 CAGRA 构图 → CPU hierarchy → 原生 VSAG HNSW 导入的构建链路。

## 新版本如何验证

编译并启动本项目的新 seekdb 二进制后，使用 README 中的基准命令，增加 `--gpu-lib cuvs` 并选择新的输出路径。例如 `--database hipvs_course_benchmark --output /workspace/hipvs-course-results.json`。基准会重建专用库中的 `corpus` 表，不能指向业务库。`--gpu-lib hipvs` 可对照原拼写，两者应产生相同后端的真实 GPU 构建日志。

新增 mysqltest 声明回归检查 `lib=cuvs` 的非 L2、量化类型、不支持的 M，要求返回 HIPVS 能力错误，而不是未知库错误。

新 CUVS 别名已完成完整 seekdb 构建、27 项真实 SQL 检查和 Radeon GPU 实测，当前构建加速为 **8.0777 倍**，GPU Recall@10 为 100%；隐藏 GPU 后重启查询也通过。14 项 Python 回归以及新、旧证据的一致性检查均通过。云镜像未提供 mysqltest runner，本次执行的是可复现的 `check_hipvs_sql.py`，不将其表述为 mysqltest 已运行。详见[本次提交说明](SUBMISSION.md)。8.208 倍历史结果仍单独保留。
