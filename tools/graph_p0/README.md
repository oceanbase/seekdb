# seekdb Graph P0 原型

本目录实现 P0 的关系转换与单跳访问验证工具，不是对外图查询入口。语法目标是 Oracle / DuckPGQ SQL/PGQ，seekdb 尚不能执行文档中的 `CREATE PROPERTY GRAPH` / `GRAPH_TABLE`。

## 构成与边界

- `graph.h`：已绑定固定模式 IR 到普通 SQL 的转换；保留复合键、变量关联、平行边重数及显式边不重复约束。另有单语句批量访问候选和有界分页的 `SingleHop` 控制器。
- `main.cpp`：输出转换 SQL；真实服务器访问适配器通过 MySQL 协议调用 seekdb，实际数据来自既有 SQL/DAS/存储链路。
- `test.cpp`：协议级故障注入、取消、恢复、分页、预算、身份与关联测试。
- `run.py`：真实 seekdb 执行、独立 Python 穷举结果、随机差分、事务、取消、EXPLAIN 和性能测量。

两种单跳原型分别回答不同问题：

1. `sql batch_out` / `batch_in` 在**一个 SQL 语句**里组织端点去重、读边、目标点检查和绑定恢复，直接沿用服务端语句快照。CTE 表示可共享的关系结构，不保证优化器物化或只读一次。
2. `expand` 实际运行 C++ 分页控制器。一个调用内使用同一连接的 RR 一致性事务，不在批次之间提交。它验证协议和真实数据访问，**不是**进程内 `ObDASIter` 适配器，不能据此宣称已验证内部语句快照传递、Attach 或内核查询内存归属。

原型只使用测试夹具的两列 BIGINT 键和固定映射；它不是任意图元数据的 Resolver。复杂属性类型、任意复合键和无向边语法在契约中列出，不能由这些测试推导为已实现。

## 构建与单测

需要 C++17、CMake >= 3.18；真实服务器适配器还需要 MySQL/MariaDB 客户端开发库。

```sh
cmake -S tools/graph_p0 -B /tmp/graph-p0-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graph-p0-build --parallel 2
ctest --test-dir /tmp/graph-p0-build --output-on-failure
/tmp/graph-p0-build/graph_p0 sql chain
/tmp/graph-p0-build/graph_p0 sql batch_out
```

只有协议测试时，可以传 `-DGRAPH_P0_MYSQL=OFF`。GitHub `Graph P0 protocol tests` 工作流执行 ASan/UBSan 单测；它不替代真实实例测试。

## 真实实例验证

使用独立实例，用户需要有创建、删除测试库和取消自身查询的权限。不要在生产实例运行。测试只清理本次成功创建的 UUID 测试库。

```sh
python3 -m pip install PyMySQL
export GRAPH_P0_HOST=127.0.0.1
export GRAPH_P0_PORT=18991
export GRAPH_P0_USER=root
# 有密码时通过 GRAPH_P0_PASSWORD 环境变量提供，不放在命令行。
python3 tools/graph_p0/run.py \
  --binary /tmp/graph-p0-build/graph_p0 \
  --report /tmp/graph-p0-report.json --nodes 1000 --runs 3
```

`--server-binary /absolute/path/to/seekdb` 记录服务器二进制版本与 SHA256；`--server-pid PID` 采集独立服务端进程 CPU 和 RSS。用户必须确保这两者对应实际连接的实例。

默认规模数据为 1000 点 / 10000 边，随机种子 20260909，均匀、热点、汇合三类；64 个绑定对应 16 个不同源点。可通过 `--nodes` 扩大规模。查询没有 LIMIT 截断结果。

失败时工具非零退出，报告记为 failed；C++ 流式输出可能已有部分行，调用者必须检查退出状态，不能将部分 stdout 当作成功结果。

## mysqltest

`tools/deploy/mysql_test/test_suite/graph_p0` 中两个用例复用 `fixture.sql`，已注册到 `mysqltest_config.yaml`。与 CI 一致，从 `tools/deploy` 执行，连接已创建的独立空测试库：

```sh
mysqltest --host=127.0.0.1 --port=18991 --user=root --database=graph_p0_mysqltest \
  --test-file=mysql_test/test_suite/graph_p0/t/semantics.test \
  --result-file=mysql_test/test_suite/graph_p0/r/mysql/semantics.result
mysqltest --host=127.0.0.1 --port=18991 --user=root --database=graph_p0_mysqltest \
  --test-file=mysql_test/test_suite/graph_p0/t/transaction.test \
  --result-file=mysql_test/test_suite/graph_p0/r/mysql/transaction.result
```

不要直接用 `--record` 接受差异；预期结果来自小图人工枚举，`run.py` 的随机验证也不读取生成 SQL 的逻辑来生成预期结果。

## 指标含义

- `edge_rows_returned`：分页 SQL 返回到控制器的边行数，不是存储检查行数。
- `target_keys_requested`：各页请求的不同目标键数之和；同一目标在不同页可能重复读取。
- `sql_statements`：连接配置、事务开启、绑定读取和访问查询总数，不是 RPC 或物理 IO 次数。
- `controller_budget_charge`：控制器根据绑定数和页容量预先收取的保守额度，不是实际堆峰值，也不包含客户端库与服务端内存。
- `server_cpu_ms`：服务端进程 CPU 增量，包含后台任务；`server_rss_peak_sampled_bytes` 是 10ms 采样峰值，可能漏掉瞬时峰值。
- 延迟包含结果传输；`expand` 还包括进程启动、建连和事务开销。不能把它与内核 Expand 算子成本等同。

语义与后续工作见 [P0 契约](../../docs/design/graph/p0-contract.md)、[架构决策与验收](../../docs/design/graph/p0-architecture.md)、[实际验证报告](../../docs/design/graph/p0-validation.md)。
