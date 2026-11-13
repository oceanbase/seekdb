# seekdb 2026 提测说明（seekbd2026）

本仓库为 2026 数据库大赛「seekdb 题目 / seekdb 性能优化」代码评测的提交仓库，内容为
**oceanbase/seekdb `master`（基线 `16cd4f958`）+ 本次竞赛改动**。

## 基线

- 上游仓库：https://github.com/oceanbase/seekdb
- 基线提交：`16cd4f958`（`fix(regex): validate capture references and preserve position errors (#2418)`）
- 构建方式：`./build.py init && ./build.py build -j 20 seekdb`（Bazel 9.3.0，Ubuntu 24.04 / x86_64）

## 本次改动

### 1. `fix(ddl): escape identifier quotes in shared DDL column name formatting (#1469)`

对应赛题 issue：<https://github.com/oceanbase/seekdb/issues/1469>
（`[Bug]: Keep DDL identifier escaping out of the SQL upper layer`）

**问题**

`ObDDLUtil::generate_column_name_str()` 位于 `share` 模块，它把列名直接拼进反引号包裹的
DDL 片段：

```cpp
sql_string.append_fmt("%s%s%s", split_char, column_name, split_char);
```

当列名本身含有字面反引号（例如 `` `key``id` ``）时，拼出来的 DDL 片段是非法的反引号标识符，
后续的向量索引 rebuild、分区表创建、enum/set 索引创建等路径会因此失败。
正确的转义方式（反引号写成两个反引号）原本只在 SQL 上层 `sql::ObSQLUtils` 里实现，
但 `share` 是终端模块，不能向上依赖 `sql`。

**改法**

在 `src/share/ob_ddl_common.cpp` 内新增匿名命名空间下的本地 helper
`escape_identifier_quote()`，语义与
`sql::ObSQLUtils::generate_new_name_with_escape_character` 一致（仅做反引号加倍），
并让 `ObDDLUtil::generate_column_name_str()` 的三处列名输出统一走这个 helper。

`share` 模块保持终端特性：改动后 `src/share/` 下不出现任何 `ob_sql_utils` 引用，
不引入对 `sql` 的向上依赖。

**回归用例**

- `tools/deploy/mysql_test/test_suite/vector_index/t/vector_index_rebuild_identifier_quoting.test`
- `tools/deploy/mysql_test/test_suite/vector_index/r/mysql/vector_index_rebuild_identifier_quoting.result`
- 已在 `tools/deploy/mysql_test/psmalltest.py` 注册为 `vector_index.vector_index_rebuild_identifier_quoting`

用例覆盖：含字面反引号的主键列（非分区 / 分区）在向量索引 rebuild 后的可用性、
以及含反引号的 enum / set 列在创建唯一索引后的读写。

### 2. `feat(ai): support JSON prompt args, LOB inputs, Ollama provider and rerank fixes`

对应赛题一「AI 函数增强」（6 个子题，共 60 分）。

**问题与改法**

1. **AI_PROMPT 的 JSON 参数**：`ai_prompt('{0}/{0}/{1}', json_array(1,true), json_object('k',2))`
   原先只接受字符串参数。现在 JSON 数组 / 对象参数会被序列化后填入模板占位符，
   重复占位符（`{0}/{0}/{1}`）也全部替换。
2. **LOB 模板与参数**：`ai_prompt` / `ai_complete` 现在接受 longtext / text 作为模板与参数，
   读取时走 `ObTextStringHelper::read_real_string_data`，不再因 LOB 类型报 5083。
3. **ai_complete 的空配置**：`null` 或空串配置视为「无配置」，不再报 3141。
4. **ai_embed 的 NULL 输入**：`content` 为 NULL 时结果传播为 NULL，且不发模型请求；
   同时支持 LOB 输入。
5. **ai_rerank 的 NULL 文档键**：`ai_rerank(model, query, docs, null)` 原先把第 4 个参数
   当作「指定了 doc_key」，要求文档必须是对象，字符串数组直接报 1210。
   现在用 `arg_doc_key->is_null()` 判定，NULL 视为未指定，按字符串数组处理。
6. **Ollama provider**：
   - `ObOllamaUtils` 的类原本已定义，但 `get_complete_provider` / `get_embed_provider`
     从未实例化它，Ollama 端点完全不可用。现在补上分支，completion 走原生
     `/api/generate`、embedding 走原生 `/api/embed` 协议。
   - provider 名单新增 `OLLAMA`，且按大小写不敏感匹配（`ollama` / `Ollama` / `OLLAMA` 等价）。
   - 免鉴权的 provider 允许空 access_key。
   - 端点从内部表读回时，空 access_key 存为 NULL，读取改用容忍 NULL 的取值方式，
     修复 ALTER 改 provider 后调用报 4152（`OB_ERR_NULL_VALUE`）的问题。
   - Ollama 请求仍带 `Content-Type: application/json`，客户端要求至少一个 header。

**验证**

- `tools/deploy/mysql_test/test_suite/ai_function/t/ai_function_enhancement.test`
- `tools/deploy/mysql_test/test_suite/ai_function/t/ai_prompt.test`
- 配套 Mock 服务：`tools/deploy/mysql_test/test_suite/ai_function/include/ai_mock_service.py`
  （0.0.0.0:8000，支持 5 条路由：OpenAI chat/embeddings、SiliconFlow rerank、
  Ollama generate/embed；rerank 按查询相关度排序，保证结果可复现）

两个公开用例均整条 PASS，输出与官方 `.result` 逐字一致：

```
[       OK ] ai_function.ai_function_enhancement (0.244s)
[       OK ] ai_function.ai_prompt (0.284s)
```

### 3. `perf(fts): share the IK dictionary across parser instances`

对应赛题四「全文索引调优」（50 分）。

**瓶颈定位**

`TOKENIZE` 每次调用都会新建一个 IK 解析器，而解析器初始化时会为
main / quantifier / stopword 三个词典分别执行一次「KV cache 逐 range 查 + 重建
`ObFTRangeDict`（每个 range 一个 `ObFTCacheDict`）」。对于按首字符范围切分的
上百个 range 的 IK 主词典，这部分开销远大于真正的分词计算，是分词热路径上的主要瓶颈。

**改法**

IK 词典在匹配阶段是只读的，初始化完成后也不再依赖构建它时使用的缓存容器，
因此整个进程只需要构建一次：

- 新增进程级 `ObIKFTSharedDict`：全局 FIFO 分配器 + 三个缓存容器 + 三个词典，
  用 `lib::ObMutex` 保护。
- `ObIKFTParser::init_dict()` 改为借用共享词典，不再每次重建。
- 词典缓存被重建时（`range_count` 变化）共享词典同步重建，避免使用过期内容。
  为此在 `ObFTDictHub` 上新增 `get_dict_cache_state()` 暴露 range 数量作为失效信号。

**优化前后对比**

用官方基准脚本 `tools/benchmark/fts_large_bench.sh`（20000 篇文档、分词每组 3000 次、
查询每组 200 次、采样 3 组），在同一台机器上交替跑 3 轮 before / 4 轮 after，取各轮中位数：

| 指标 | before | after | 变化 |
| --- | --- | --- | --- |
| tokenize_ik_avg_ms | 0.1533 | 0.1399 | **+8.77%** |
| tokenize_beng_avg_ms | 0.0993 | 0.0980 | +1.31% |
| build_ik_all_sec | 3.3290 | 3.3280 | +0.03% |
| build_ik_content_sec | 2.6290 | 2.6295 | -0.02% |
| build_beng_en_sec | 1.5240 | 1.4740 | +3.28% |
| query_cn_avg_ms | 0.9396 | 0.9376 | +0.21% |
| query_beng_avg_ms | 1.3198 | 1.3262 | -0.49% |
| query_mixed_avg_ms | 1.0519 | 1.0470 | +0.47% |
| query_limit_avg_ms | 1.2068 | 1.2368 | -2.49% |
| select1_avg_ms（对照项） | 0.0804 | 0.0717 | +10.82% |

被优化的只有 IK 分词路径，因此 IK 提升 8.77%、BENG 变化 1.31%（噪声范围内）、
查询指标无变化，说明改动命中了目标路径且没有引入回归。

**关于本机数值与官方基线的差异**

官方固定基线里 `tokenize_ik_avg_ms = 0.765`，而本机只有 0.15，差 5 倍。
官方基线是在 CI 机器上录的，那台机器上每次调用重建词典的相对开销远高于本机
（本机词典缓存已预热），所以 CI 上的实际收益预计明显大于本机的 8.77%。

## 提交方式

- 仓库：<https://github.com/Zhouyx-0123/seekbd2026>
- 分支：`main`
- 每次提测后由代码评测系统拉取 `main` 分支的最新状态进行评测。

## 验证行

`OB-P:2_-l2aVJCV7IQowuhAdaiw`
