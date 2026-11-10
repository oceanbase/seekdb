# MiniVectorDB 公开自测题包

适用于 `2026-competition-round2`。本包共 **20 个公开样例、95 条 SQL**，统一放在 `cases/` 目录，按清单顺序从简单操作逐步验证持久化、索引和事务能力。

## 公开用例

| 用例 | 检查目标 |
| --- | --- |
| sql_crud | 一条记录的插入、查询、更新与删除 |
| sql_comparisons | 整数比较和 AND 条件 |
| sql_ordering | 无并列项的升降序与 LIMIT |
| sql_empty | 空表查询、空表删除 |
| sql_primary_key | 拒绝重复主键，保留原记录 |
| sql_unicode | 中文字符串写入与读取，不混入长度边界和字符排序 |
| sql_bigint | 超过 JavaScript 安全整数范围的 BIGINT 精确读写，不混入溢出 |
| sql_unsupported | 对题目不支持的 ALTER TABLE 返回错误 |
| storage_restart | 一条自动提交记录在正常重启后保留 |
| vector_euclidean | 单条二维向量的欧氏距离，不混入更新、过滤或索引 |
| transaction_commit | 普通表事务修改提交后可见，不依赖向量或重启 |
| transaction_rollback | 普通表事务修改回滚后恢复，不依赖向量或重启 |
| storage_mutations | 更新、删除和向量修改在重启后保持一致 |
| storage_multiple_tables | 多表定义和记录的持久化 |
| vector_ties_and_filter | 先过滤后 TopK，距离并列按主键排序 |
| vector_float32 | 存储与距离计算的 float32 语义 |
| vector_index_maintenance | 建索引后增删改、重启及模式切换 |
| transaction_failed_insert | 事务中多行插入失败的原子性及后续提交 |
| transaction_autocommit_atomic | 自动提交语句失败不能留下部分数据 |
| transaction_shutdown_rollback | 正常关闭时回滚未提交事务 |

## 格式与执行约定

这是 **mysqltest 风格的 SQL/预期断言题包**，不是 MySQL 官方 mysqltest 的原生可执行格式，不要求 MySQL 网络服务。

- `manifest.json`：每个样例的 id、类别及 test/result 相对路径。
- `cases/*.test`：UTF-8，每个非空行是一条完整 SQL 或驱动指令。
- 同名 `.result`：JSON 数组，只与 SQL 按顺序一一对应，驱动指令不占断言位置。

每个用例从独立的空数据库和数据目录开始，不能把全部文件拼成一次执行。用例内 `--restart` 必须复用原数据目录；不要因重启清空数据。

| 驱动指令 | 含义 |
| --- | --- |
| `--restart` | 正常关闭并等待退出，再用相同数据目录启动；关闭须回滚未提交事务 |
| `--mode ann` | 后续向量请求采用 ann 模式；每个用例最初为 exact |

指令不是 SQL。本包没有强杀指令。ANN 小数据样例按对应 result 的明确结果检查，不等于正式随机 ANN 的完整召回率验证。进程通信采用标准输入/输出 JSONL 协议；详见根目录 STDIO_PROTOCOL.md。配套公开自测驱动位于 protocol_check/run.py。

断言规则：

- `{}`：SQL 必须成功，不额外要求特定结果字段；不表示可以忽略错误。
- `error`：SQL 必须失败，值是错误语义名，如 `duplicate_key`。当前未冻结错误码映射的配置至少要求非空错误码；正式映射以题包为准。预期错误后继续执行后续语句。
- `affected_rows`：受影响记录数，精确比较。
- `columns`：查询列名及其顺序，精确比较。
- `rows`：按顺序比较查询结果；`unordered: true` 时改用多重集合比较，重复行不可丢失。

整数、字符串精确匹配；浮点比较绝对/相对容差均为 `1e-5`。向量先存为 float32，L2_DISTANCE 返回欧氏距离而非距离平方，例如 `[3,4]` 与 `[0,0]` 的距离为 5。

本包不提供数据库实现。请实现正常执行与错误处理，而非预存样例答案。基础样例已简化，不能用评测仓库中的同名正式用例替代它们，须读取本目录中的实际文件。

## 正式验收要求仍然公开

公开样例通过不代表完整功能准入通过。正式用例共 29 个，全部通过才进入性能与 Agent 评分；组织方另保留 9 个用例的具体输入、操作序列、数据生成参数和预期结果。正式评测并非只运行保留的 9 个。

以下功能仍属于正式要求，不能因本包没有相应完整用例而省略：

- 拒绝 NaN、Infinity、维度错误、float32 溢出等非法向量。
- 失败语句不能部分生效；显式事务之前的成功修改仍可提交或回滚。
- 已确认提交在进程强杀后保留，未提交修改不部分生效；恢复途中再次退出仍能恢复。
- 能处理超过内存限额的数据，不能只依赖全量常驻内存。
- ANN 必须满足过滤、有效 ID、无重复及题包规定的召回率，索引更新不能破坏事务语义。

隐藏的是测试数据，而非功能要求。正式题目的完整语法、资源预算和冻结配置以统一题包为准。


## JSON 通信协议与自测入口

请先阅读 [STDIO_PROTOCOL.md](../STDIO_PROTOCOL.md) 和 [协议自测说明](../protocol_check/README.md)。
容器必须主动输出一行 `{"type":"ready"}`，随后逐行读取 execute/shutdown JSON 请求，
逐行返回含相同 id 和布尔 ok 的响应。每条输出须换行并 flush；日志写 stderr。
纯文本 READY、直接输入 SQL、文本表格均不符合正式协议。

本目录 .test 是待执行 SQL/.result 是预期断言，不是实际通信报文；驱动负责封装请求。
公开断言中的 `{}` 也要求成功响应含 id、ok:true。

在仓库根目录先运行握手检查，再运行公开用例（替换镜像名）：

```bash
export VECTORDB_IMAGE='你的镜像名:标签'
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" --handshake-only --output "protocol-results/handshake-$(date +%Y%m%d-%H%M%S)"
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" --output "protocol-results/public-$(date +%Y%m%d-%H%M%S)"
```

每次使用新的产物目录。握手通过不代表SQL正确；公开用例通过不等于正式功能准入通过。
example_stdio.py仅提供通信骨架，SQL执行尚未实现，不可作为数据库实现直接提交。
