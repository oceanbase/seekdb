# CPU HNSW 自身近邻未命中诊断

本诊断不使用 GPU、不计 SQL 构建加速成绩，也不修改数据库或评测门槛。它区分近似搜索未命中与存储向量遗漏。

## 运行

使用已编译 seekdb 的同版 VSAG 头文件和库，Python 依赖见本目录 requirements.txt：

```bash
bash unittest/amd_hipvs/native_cpu_diagnostic.sh /workspace/native-cpu-diagnostic
```

可通过 VSAG_DEVEL_PREFIX 指定依赖安装目录，通过 PYTHON 指定 Python 解释器。输出目录必须是新目录。脚本记录 VSAG 库 SHA256，生成固定 10000×128 FP32 数据、100 个查询和大于 2^53 的唯一 BIGINT ID；参数与 benchmark.py 的默认数据相同。索引按主键排序后的顺序每批 500 行 Add，M=16、ef_construction=200。正式 Recall@10 使用 ef_search=200。

自检先对每条输入向量查询自身，再对未命中的向量提高搜索宽度。VSAG ef_search 上限受 K 影响：ef=10000 时使用 K=1000，不能仍用 K=1。每个阶段打印真实未命中数量。程序另检查所有 ID 的 CheckIdExist，保存 Serialize() 中的原始 HNSW_DATA。

Python 分析器逐条比较原生图中的全部唯一标签和 FP32 原始字节，检查图边界并计算从入口出发的有向可达节点数。报告明确区分底层可达性、合并所有层边后的可达性和完整存储；层间合并图仅用于诊断，不代表实际 HNSW 搜索的访问规则。

## 已观察到的结果

2026-11-01 的本机 VSAG 012a0f2 诊断，数据指纹为 `b35caa9beffe605b248289d7b21a6a29a5822673ccc8919ad4bf9d41e0aecae9`，与云端测试一致。M16/ef_construction200 的 CPU 图有 10000 个唯一正确标签，每个 FP32 向量逐字节相同；Recall@10=0.953，ef1000 自检未命中 19 个，ef10000/K1000 后未命中 17 个。底层入口可达 9983 节点，合并所有层边后可达 9984 节点，16 个节点入度为零。

因此，“ANN 自身查询 100% 命中”不是“所有输入向量均存储完整”的等价判据。这里的完整存储证据来自真实序列化产物和逐条比对，而不是放宽 Recall 门槛。最终仍须独立验证实际 SQL 索引及 CPU/GPU 的相同查询集和检索质量；本诊断不能替代这些检查。
