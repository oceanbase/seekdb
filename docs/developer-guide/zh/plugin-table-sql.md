# 表函数的同步服务端 SQL

表函数不再只能消费传入的字节：Rust/native 插件可以在 open 和 next 中执行参数化
SQL，组合数据库中的数据，再返回自定义表行。该能力复用标量函数的 SQL SPI 和宿主
执行路径，不增加第二套权限或事务管理器。

## C ABI 与协商

表函数服务 minor 3 仍使用完整的 `seekdb_plugin_table_function_service_v2_t`，
估算回调可选。open/next 接收 `seekdb_plugin_table_execution_context_v3_t`：
保留 poll-only v2 前缀，追加 `sql_api`，以 `v2.query_context` 作为 SQL handle。
SQL 和 poll 使用同一个本次回调的 `PluginSqlContext`，共享首错状态。

loader 向 minor 0/1 服务传递精确的 v1 context，向 minor 2 服务传递最多 v2，
不会把新 SQL 后缀直接交给旧服务。minor 3 的调用者仍可能只提供旧分配，因此插件
必须先检查 struct_size 再读取后缀。SDK 的 `WITH_SQL` 明确拒绝缺少 SQL 的 open
和 next，不把 SQL 依赖静默降级成本地计算。服务版本、context 版本与业务版本不同，
不能由此承诺旧宿主一定接受新插件二进制。

## Rust 接口

- `table::Service::<C>::WITH_SQL`：无需实现 Planner，使用宿主默认估算。
- `table_planning::Service::<C>::WITH_SQL`：同时保留自定义估算回调。
- `Cursor::open_with_context(instance, arguments, query)`：打开时访问
  `QueryContext::supports_sql/execute_sql/supports_query_control/poll_query`。
  默认实现调用原 `Cursor::open`，旧插件不必修改。
- `Rows::supports_sql/execute_sql`：在 next 内使用相同的类型化 SQL 参数/结果协议。

`execute_sql(statement, parameters, max_rows, consumer)` 的 SQL 使用位置参数 `?`。
consumer 接收借用的 `sql::Row`，所有 SQL 行在函数返回前消费完毕；需要跨 next
保留的结果必须复制成 cursor 自有数据。它不是可跨回调使用的 prepared cursor。
查询上下文不能 Send/Sync，也不能在 consumer 中通过安全 Rust 重新借用同一上下文。

`QueryContext` 也让原 minor 2 插件可以在 open 内 poll，不再仅限 next 的 Rows。
这不会为 minor 2 授予 SQL 能力。

## 生命周期与错误

SQL consumer/宿主错误记录为本次回调首错，保留 ABI 状态、数据库错误及 consumer
状态。之后的 SQL/poll 不再进入宿主，next 不得继续 emit。即使插件忽略错误并返回
成功，SDK 也返回失败；open 创建出的自有 cursor 状态会被销毁，不发布 raw handle。

真实 SQL 表函数宿主还检查自己的首错。open/next 失败会关闭 cursor、释放模块及
转换引用、保存原始数据库错误；重复 fetch 不重开游标，不把错误当 EOF。
显式 SQL rescan 关闭旧游标，在下一次 fetch 用新的借用上下文重新打开。

原 C ABI rescan 没有 query context，仍调用 Rust `Cursor::open`。依赖 SQL 才能
重建的 cursor 应在该入口明确返回不可用，要求调用者 close/open；不要保存 open
传来的裸查询指针来绕过这个限制。close/Drop 不获得 SQL 执行上下文，资源释放不能
依赖关闭时仍然能查询数据库。

## 权限、事务和重扫副作用

SQL 复用当前用户、session 和事务；复用宿主既有的嵌套执行、权限检查、外层 SELECT
savepoint 及结果集关闭处理。支持集合仍是 SELECT/INSERT/UPDATE/DELETE/REPLACE，
不开放 DDL、显式事务控制、init/start 中 SQL、后台 session 或任意线程访问。
SQL/参数/结果字节、行数与嵌套限制沿用现有 SQL SPI。不能用表函数绕过安装期
catalog builder 的对象管理路径。

该接线不等于实库事务原子性已经得到验证。仍需用真实会话检查写入后外层失败、
关闭/取消、权限不足、嵌套与并发时的提交/回滚行为。

插件必须按真实 SQL/外部副作用声明函数属性，不应把依赖数据库状态的表函数标记为
immutable。优化器可能多次调用、重扫或提前结束表函数；在 open/next 写入数据应
明确定义重复执行语义。数据库回滚也不能撤销外部请求。不要把一次 next 的成功
误解成外层语句已经提交。

## 示例与验证边界

新增 `seekdb_rust_sql_series(bytes)`：open 通过 SQL 查询输入字符数，next 通过
参数化 SQL 逐个生成 1 到字符数的 ordinal；NULL/空输入为空表。游标只保存两个
整数，没有查询指针、SQL 结果引用或后台线程。注册没有 immutable/deterministic
标记，使用宿主默认估算。

```sql
SELECT ordinal FROM TABLE(seekdb_rust_sql_series('A中🙂'));
-- 预期三行：1、2、3。
```

这是展示 open 与 next 均可调用 SQL 的参考例，不是高效序列生成算法；真实数据源
应采用有界批量查询，避免逐行嵌套 SQL。示例不接收用户提供的任意 SQL 文本。
模块当前 build ID 为 `rust-text-query-catalog-v8`，manifest 与动态库必须成套部署。
SQL-series 自身仍为 minor 3；分词服务新增的 minor 4 投影不改变此函数的 SQL 契约。

SDK 回归覆盖原 context 短分配/能力缺失、非法保留字段、SQL 成功、被忽略的 open/
next 错误、Drop、失败后禁止重入/emit、后续 context 降级及 raw rescan 拒绝。
C/Rust ABI 测试对照 v3 的大小/对齐/偏移。实际 Rust DSO 测试覆盖 open/next SQL
往返、批次行数、EOF、NULL、两个阶段的失败及 lease 释放，但 SQL transport 是
受控替身，不是实际数据库查询结果的证据。

内核回归通过真实 parser/resolver/codegen、SQL table fetch、loader、Rust DSO、
宿主 SQL 状态检查，验证打开期错误传播、错误保持和 rescan 重新准入。SQL 连接
之前注入失败，不能据此声称真实 SQL 查询、授权、回滚或性能已经通过。
最新执行结果见 [实施进度](plugin-implementation-status.md)。
