# AMD hipVS 端到端构建评估工具

本工具调用真正的 seekdb SQL 建索引及查询接口，不提供 GPU 模拟器，不把 CPU 回退或独立查询演示计作构建加速。GPU 实现与实测完成之前，本目录的存在不代表 AMD 题已通过。

## 运行环境

Python 3.12、`requirements.txt` 中的依赖；能够访问待测 seekdb 的 MySQL 协议端口。GPU 组的数据库进程须运行在正确配置的 AMD ROCm/hipVS 实例上。服务器镜像/驱动应以该实例实际 GPU 型号的支持矩阵为准。

在本目录创建虚拟环境并安装依赖：

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m unittest discover -v
```

连接由环境变量提供：`AMD_BENCH_HOST`（默认127.0.0.1）、`AMD_BENCH_PORT`（默认2881）、`AMD_BENCH_USER`（默认root）、`AMD_BENCH_PASSWORD`、`AMD_BENCH_DATABASE`（默认test）。不要把密码放入命令参数、报告或Git。账号需要在此专用数据库创建/删除表和向量索引的权限，以及读取 `oceanbase.__all_table`、`oceanbase.__all_database` 和本轮索引辅助表的权限，以验证实际存储快照。工具不会创建或删除整个数据库；每轮创建带随机run ID的新表，只清理自身成功创建的表。若进程被强制终止，可根据报告中的准确表名人工清理残留；不要按模糊前缀删除其他表。

## CPU基线检查

以下仅为小规模功能冒烟参数，**不是赛事评分数据规模或质量门槛**：

```bash
python benchmark.py --cpu-only --rows 200 --dim 16 --queries 20 --k 10 \
  --repeats 3 --min-recall 0.90 --output local-results/cpu-smoke.json
```

报告状态只能为`CPU_ONLY`，不会给出GPU成绩。公开参数默认CPU索引为`distance=l2,type=hnsw,lib=vsag,m=16,ef_construction=200,ef_search=200`。SQL语法依据同仓库`tools/deploy/mysql_test/test_suite/vector_index/t/`测试，查询使用`INDEX(t index_name)`提示及`APPROXIMATE LIMIT`，并检查EXPLAIN确实使用指定向量索引。

## CPU/GPU对比

`--gpu-with`必须填写**已经实现并验证的GPU路径**所用WITH参数，工具不猜测`lib=cuvs`等尚未接入的语法。CPU/GPU的`distance/type/m/ef_construction/ef_search`必须显式相同；本版本仅支持FP32、L2、HNSW。`--rows/--dim/--queries/--k/--min-recall/--target-speedup`应采用赛事统一评测配置；未知目标时省略`--target-speedup`，不会声称达到赛事目标。

环境文件`--environment-json`记录服务器的`cpu_model`、`cpu_threads`、`cpu_affinity`、`gpu_model`、`gpu_architecture`、`rocm_version`、`hipvs_version`、`seekdb_commit`。这些是服务器信息，不能使用客户端笔记本配置替代。字段缺失时对比结论是`UNVERIFIED`。不要在环境文件保存账户密码、SSH地址或令牌。

运行至少三轮。每轮CPU/GPU顺序交替。脚本记录以下独立阶段：

1. 固定随机种子生成FP32向量、查询与非连续BIGINT主键，保存SHA256数据指纹；精确近邻真值用同一FP32数据的FP64距离累加计算。
2. 每组新建无向量索引的独立表，导入完全相同数据。数据生成和导入不计入构建加速比。
3. 验证全部原始ID及不存在旧向量索引，从`CREATE VECTOR INDEX`开始计时，至首次指定索引ANN查询完成结束。此区间包含扫描、GPU传输/同步、格式转换、DDL以及首次查询可能触发的延迟构建。
4. 使用相同查询集校验Recall@K，拒绝未知ID、重复ID、ID精度丢失、非法距离、结果数量不完整、未使用向量索引等结果。随后以每个输入向量做自身最近邻查询，保存全部输入的 ANN 可达性诊断；近似查询未命中不直接等同于存储遗漏。
5. 对CPU和GPU每轮实际 SQL 索引读取独立的快照完整性证据：根据当前数据库和准确表名查出所属辅助表，读取完整 VID→主键映射及同一版本的连续快照块。解析本仓库固定 VSAG 原生格式，验证全部节点、全部主键一一对应、每条 FP32 向量逐字节一致，以及各层邻接数量和节点编号合法。缺少证据、歧义、格式不符或任何不一致均失败，不回退到行数或 ANN 抽样。
6. 分别取CPU和GPU构建耗时中位数，**不是各轮加速比的平均数**。每一轮均须同时满足正式Recall门槛、指定索引查询计划和实际存储完整性条件。

JSON保存每轮耗时、SQL参数、查询计划、Recall、自身ANN命中、数据指纹及服务器软件/硬件信息，同名Markdown便于阅读。`storage_integrity` 保存本轮 SQL 快照和映射的 SHA256、完整行数、维度、M、源表/索引及辅助表元数据；`storage_verification_sec` 单独记录完整性检查耗时。不能把本工具随机资料换成更少数据/更低精度来宣称满足统一评测。

读取辅助索引表时临时启用会话变量 `ob_enable_index_direct_select`，结束或异常时均恢复原值并回读确认；这一步位于构建计时和正式 ANN 查询之后，不改全局配置或表数据。辅助列名从 `oceanbase.__all_column` 按已核对所属关系的精确 `table_id` 读取，不能使用不支持索引表解析的 `SHOW COLUMNS`。错误报告保留安全的缺表/缺列标识和文件名、函数、行号，不保存连接凭据、任意 SQL 文本或堆栈局部变量。

每轮显式设置并回读会话变量 `ob_hnsw_ef_search`，因为会话默认值 64 会覆盖索引声明中的搜索宽度。正式 Recall@K 和首查询使用双方共同配置的值，不自适应提高召回分数。全部 ID 自检先使用同一搜索宽度；只有未命中的自身向量才提高诊断搜索宽度重试。

seekdb 的 VSAG 适配层还将实际搜索宽度限制为 `min(ef_search, max(1000, 10*K))`，所以仅把会话变量设为 10000、仍查询 `LIMIT 1`，实际宽度只有 1000。重试设置 `ef=min(160000,max(配置值,rows))`，并使用 `K=min(rows,max(1,ceil(ef/10)))` 取回足够候选。只有首个返回项为自身准确 ID 且距离为 0 才计为自身命中，同时校验所有候选 ID、距离、排序及查询计划。报告分别记录请求宽度、适配层生效宽度、重试 K、重试数量和最多 20 个未命中样本。两条路径使用同一规则；这项可达性诊断不计入构建耗时，也不代替配置宽度下的 Recall@K。

固定版本 CPU VSAG 的原生图实验已经出现“全部 10000 个输入主键和 FP32 向量都在快照中，但仍有节点从 ANN 入口不可达”的情形。把自身 ANN 命中 100% 当作存储完整性门槛会误判这个基线。因此自身命中数继续原样报告，实际存储完整性由两条路径相同的直接快照检查决定；正式 Recall 阈值保持不变。独立原生实验不能替代本轮 SQL 证明，历史报告没有 `storage_integrity` 时不能获得有效加速成绩。当前解析器只支持本仓库固定依赖的旧原生 HNSW 格式以及本工具新建的非分区、单快照索引；权限不足或其他格式直接失败。

基线在 DDL 刚结束时可能尚未读取到新的快照 LOB。首查询只针对 4016/4138 重置事务快照后有限重试，所有重试时间均计入构建耗时，两条路径使用同一规则。最终仍必须取得真实 ANN 结果并通过质量检查。

## GPU证据接口

当前桥接库会把真实 GPU 完成记录写入 seekdb 的标准错误流。专用测试实例可使用
`--server-stderr /absolute/path/to/seekdb.log` 自动读取每次建索引开始前到首查询完成后
新追加的记录。运行期间必须没有其他索引构建；缺失、重复、行数或维度不匹配以及日志轮转均报错。
报告保存原始记录、字节范围与该区间 SHA256，并关联本轮唯一表名和索引名。
日志区间使用 64 KiB 分块流式读取及增量 SHA256，记录真实字节数；内存不随日志总量增长。
单次区间最多 512 MiB、单行最多 1 MiB；跨分块记录会完整匹配，短读继续读取，提前 EOF、
文件轮转、重复记录或超限均明确失败，不截取部分日志充作完整证据。
seekdb 通常把 stderr 重定向到数据目录的 `log/seekdb.log`；以实际进程的 `/proc/PID/fd/2` 为准。
读取日志发生在构建计时结束后，校验结果不会替代 SQL 查询和实际索引快照的全量 ID/向量检查。

`--evidence-jsonl`读取**由待测seekdb构建后端实际产生/导出的**逐索引JSONL记录；不可手写成功记录补成绩。每轮随机`table_name/index_name`必须匹配唯一完成记录，同时包含：

|字段|要求|
|---|---|
|producer|`seekdb-hipvs-build`|
|backend|`hipvs`|
|device_vendor|`AMD`|
|gpu_compute|布尔true，表明GPU参与构建核心计算|
|cpu_fallback|布尔false|
|completed|布尔true|
|rows/dim|与完整本轮数据一致的整数|
|gpu_name|实际GPU型号|
|table_name/index_name|本轮准确标识|

这是对接约定，不代表当前seekdb分支已实现该日志接口。接入层若采用不同格式，应先从真实原始记录做明确转换并保留原始证据，而非伪造该JSONL。脚本保存输入证据文件SHA256便于核对；JSONL本身并非防篡改签名，还需要原始日志/性能分析记录作为归属证据。缺少证据、出现CPU回退、记录不完整或硬件元数据缺失时，报告`UNVERIFIED`，有效加速比为null，即使SQL成功也不计算GPU成绩。

## 原生图兼容性检查

在配置好 ROCm、hipVS 和本仓库 VSAG 依赖的 AMD 实例运行：

```bash
bash unittest/amd_hipvs/test_gpu_export.sh /workspace/amd-build
```

该检查实际生成两份 GPU 图，再用 seekdb 相同版本的 VSAG 加载；验证全部输入 ID、
精确 L2 的 Recall@10、流序列化恢复及恢复后的新增向量。0.95 是该检查自定的功能阈值，
不是赛事评分门槛。该程序不测量 SQL 端到端构建加速比。

## 独立实例完整复现

普通构建自动交付桥接库、无路径覆盖的真实 AMD 验证报告见
[DELIVERY_RESULTS_20261101.md](DELIVERY_RESULTS_20261101.md)。原始手工桥接部署的历史报告仍保留，二者不混用。

在赛事 `seekdb-hipvs` 工作区使用本提交构建数据库。新启动的模板可能没有 RPM 解包工具，Ubuntu 中先安装：

```bash
apt-get update
apt-get install -y --no-install-recommends rpm2cpio cpio wget make g++ python3-venv
bash build.sh release --init -DSEEKDB_WITH_HIPVS=ON --make -j8
python3 -m venv --system-site-packages /workspace/amd-validation-python
/workspace/amd-validation-python/bin/python -m pip install -r unittest/amd_hipvs/requirements.txt
/workspace/amd-validation-python/bin/python unittest/amd_hipvs/run_cloud_validation.py \
  --workspace /workspace --output /workspace/amd-validation-results --port 2883
```

构建另需仓库要求的 Rust 工具链；网络受限时可使用对应工具链及公开 Cargo 缓存离线构建。完整控制器在随机新数据目录运行本仓库二进制，固定 8 个 CPU、8 GiB 内存预算和 4 GiB 日志盘；拒绝占用中的端口和已有输出目录。它验证真实 GPU 图、SQL 创建索引、进程重启后的结果一致性、增量插入/删除，以及 200×16 和 10000×128 各三轮 CPU/GPU 构建。样例门槛 0.90、规模及参数为本地复现配置，非赛事统一评分配置。

`status.json` 保存阶段、进程、源码/脚本/二进制哈希，`driver.log` 保存进度，逐轮 JSON/Markdown 保留原始测量。发生错误仍保留报告并返回非零退出码；进程退出后数据目录留存。运行完成后应把报告与环境记录下载到本地，云端实例工作盘的保留策略需以平台为准。

控制器对全部 CPU/GPU 轮次统一使用 `--log-level WARN`，避免全量自身 ANN 诊断产生大量 INFO 日志并触发轮转。该配置写入 `status.json`；GPU 构建完成证据由桥接库直接写入 stderr，仍必须逐轮验证。不能把不同日志配置的旧轮次混合计算加速比。

## 退出状态与限制

- 退出0：`CPU_ONLY`基线完成、`MEASURED_TARGET_UNSPECIFIED`已测但目标未提供、或`PASS`达到操作者明确输入的门槛。请同时阅读状态，不把退出0直接当作赛事通过。
- 退出2：不完整、运行错误、质量/功能失败、证据缺失或未达到性能目标。
- 测量包含客户端往返开销；CPU/GPU应使用同一连接位置和固定CPU配额，不在并行负载下运行。
- 本脚本不验证数据库进程重启恢复、不代替内核泄漏检查或GPU性能分析；这些证据需另行补充。
- 本工具不修改全局数据库配置，不以平台的查询加速参考结果代替构建实测。
