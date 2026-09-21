# 插件协作式查询状态与 deadline

为长时间运行的 Rust/native 标量函数和表函数增加查询状态检查，不需要先发起一条嵌套 SQL。
这是 AI 计算和外部调用的执行契约基础，不是线程强制中断、异步调度或进程隔离。

## 接口与兼容性

`sql_spi.h` 新增 `seekdb_plugin_sql_api_v2_t`，保留原 SQL API v1 前缀，minor=1，
追加 `poll_query(context, query_status)`。现有 execution context v2 布局不变；
旧 SQL 客户端仍通过 v1 execute 回调执行 SQL。新插件检查 API struct_size、major、
minor 和回调后才读取后缀。旧 scalar minor=0 插件仍收到精确的 v1 execution context。

Rust `Call::supports_query_control()` 探测可选能力；`Call::poll_query()` 返回
`Result<Option<Duration>, sql::Error>`。`None` 表示没有配置 query deadline，
`Some(duration)` 是剩余微秒的即时快照，不是时间配额预留，也不保证下一操作能完成。
错误保留 ABI 状态和原始数据库错误码。上下文只在本次同步回调、同一线程有效。

`scalar_function!` 的 `query_control: true` 是可选的上下文请求，可以与 `sql: false`
同时使用；生成的服务请求扩展上下文，但 handler 仍接受旧版 v1，并由能力探测决定
是否使用 poll。原来的 `sql: true` 仍要求扩展上下文，不因本功能放宽为静默降级。
这些选项不是 SQL 权限授权；嵌套 SQL 仍使用调用者身份和原权限检查。

## 宿主语义

`PluginSqlContext` 复用真实 `ObExecContext::check_status()`，检查超时、会话终止、
执行中断、内存状态和已注册的附加状态检查。deadline 读取现有 physical plan 的
时间规则：ABI remaining_us=-1 表示无限制，非负值表示剩余时间；失败时该值为 0。

poll 和 execute 共用本次 invocation 的首错状态。宿主观察到失败后，后续 poll/SQL
不会继续工作；即使插件返回成功，SQL 表达式入口仍返回记录的数据库错误。不能通过
重置 deadline 或忽略错误把同一次 invocation 恢复为成功。错误不会跨新的独立调用
无条件继承，但宿主查询的终止状态仍按内核规则生效。

禁止在 SQL 行消费回调内递归使用同一个 context；错误会标记当前调用失败。跨线程
调用被拒绝且不改写原线程状态。无效输出缓冲区不会被越界写入。所有异常在 C++/Rust
回调内部转为错误，不跨 C ABI 传播。

## 表函数流式控制

表函数服务 minor=2 使用完整的 `seekdb_plugin_table_function_service_v2_t`，
接收按大小协商的 `seekdb_plugin_table_execution_context_v2_t`。该 context 保留
原 emit-row 前缀，只追加 query handle 和 poll 回调，不开放嵌套 SQL 执行。
minor=0/1 的旧服务仍收到精确的 v1 context；新服务也应允许旧调用者传入 v1。

Rust `Rows::supports_query_control()` / `Rows::poll_query()` 与标量接口返回相同
预算/错误语义。使用 `table::Service::<C>::WITH_QUERY_CONTROL` 无需实现 Planner；
此时 estimate 为空，宿主使用默认估算。需要自定义代价时使用
`table_planning::Service::<C>::WITH_QUERY_CONTROL`。原 minor=1 仍要求 estimate，
minor=2 才允许为空；旧宿主可能拒绝新服务，context 回退不等于服务版本普遍兼容。

`Rows` 仅在本次 next 回调中有效，不能存入拥有式 cursor 或交给其他线程。
poll 失败后 SDK 禁止继续 emit，并保留错误，即使 handler 忽略错误后返回成功。
SDK cursor 进入失败状态；SQL 表函数宿主同时关闭失败 cursor、释放模块及转换引用，
记录原始数据库错误。重复 fetch 不重新打开 cursor，也不把失败当作 EOF。
显式 rescan/close 重置流状态；rescan 后新调用仍要通过当前查询状态检查。

Rust `Cursor::open_with_context` 现在可以使用借用的 `QueryContext` 进行轮询，
默认仍调用只提供 Arguments 的原 `Cursor::open`。原 raw rescan 没有 query
context，SQL 执行器的 rescan 通过关闭并在下次 fetch 重新打开来执行。这些都不是
后台任务 token，也不允许保留任意一次 open/next 的宿主指针。

表函数 minor 3 在保留上述前缀的 v3 context 中追加同步 SQL API，minor 2 仍然
只获得 poll。新 SQL 入口与轮询共享首错状态，见 [表函数 SQL 契约](plugin-table-sql.md)。

## Rust 示例与使用边界

`seekdb_rust_char_count` 在支持该能力的宿主中，于计数前、每 4096 个字符及结束时
poll；旧宿主保持原计数路径。它没有新增 SQL 副作用，函数的结果和 immutable 声明
不变。分词示例同样在每轮扫描前、长文本扫描每 4096 字符及产生 token 前 poll，
避免单个长词或大段空白在一次 next 中完全跳过取消检查。当前模块 build ID 为
`rust-text-table-sql-v6`，加载验证器的预期身份同步更新。

模型或网络插件可以在工作块之间 poll，并将剩余预算用于其网络/推理调用的 timeout。
插件必须自己把调用拆成可返回的工作块，或使用底层库的取消机制；poll 无法中断一个
不返回的第三方同步调用。查询取消、回滚不能撤销已经发出的外部请求。
独立后台 session、跨线程/异步任务仍需相应的生命周期与取消接口，不能持有此裸 context。

## 验证范围

SDK 测试覆盖无限/有限预算、宿主错误保留、旧 context/API 的短分配、缺失能力和非法
输出；ABI 布局对照 C/Rust 的大小、对齐和字段偏移。内核 fixture 通过真实宿主控制
接口和 Rust DSO 检查正常计数、deadline、QUERY_KILLED、错误保持，以及在第三次
检查注入失败后停止计数且不产生结果。

表函数 SDK 额外验证可选控制、忽略 poll 错误时禁止输出、失败 cursor 的重扫与析构，
以及旧 context 回退；loader 白盒测试检查 minor=0/1/2 的 next context 范围与重复
close。实际内核表函数故障/资源释放回归的最新结果见实施进度，不以 SDK 测试替代。

实际 Rust DSO 表函数通过生成的 SQL 表达式与真实 function-table operator 执行。
测试绕过 operator 的外层状态检查，直接在 fetch 内由 Rust poll 观察注入的超时：
短输入首次检查失败，9000 字符单词第三次检查在扫描途中失败；随后验证 lease 为零、
重复 fetch 保持超时且不重新打开，以及显式 rescan 后从首行恢复。该内核回归已通过。

2026-09-08 完整生产构建、22 项独立 CTest、SDK/Clippy、边界检查和实际内核/DSO
的上述 5 个场景均通过，详见 [实施进度](plugin-implementation-status.md)。受控会话状态/错误注入
不等于真实客户端 KILL、网络取消、外部模型中断或取消延迟性能已经验证。
