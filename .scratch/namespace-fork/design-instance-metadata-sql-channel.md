# 实例元数据 SQL 通道：Rust 基建核查后的方案

状态：设计结论，2026-09-29；本地笔记，不提交。实例级、需要随 LS 复制的元数据继续使用 `InstanceMetaStore`。不增设实例 NamespaceRuntime，也不通过现有 Namespace SQL 访问这些表。实例私有、不复制的数据存储是另一议题。

## 接入形状

标准 MySQL 客户端使用保留的实例管理用户名后缀进入该通道，例如 `admin@instance`（实际名称待定义），仍连接现有 MySQL 端口。内部调用者使用同一个元数据 SQL 执行入口；入口持有轻量的会话事务状态，不创建 NamespaceRuntime。表名只在该通道的静态目录中解析，普通 Namespace 的 schema 和 fork 父链都不会装载这些定义。认证、授权和连接路由在 SQL 文本执行之前完成。

现有前端尚不能直接复用为这个入口：`ObMPConnect::process` 在验证用户身份前调用 `bind_session_namespace`；`load_privilege_info` 从已绑定 Namespace 的 SchemaService 取账号与系统变量；`ObMPBase::get_session` 要求 `ObSQLSessionInfo` 绑定完整 Runtime，`ObMPQuery` 直接使用该 Session 的 schema 和执行器。只在 COM_QUERY 分派处加条件会绕不过登录和后续 session 检查。

现有 Rust `sql-nio` 已处理 MySQL 握手、登录报文、TLS、压缩、命令分类和结果编码，并向 C++ 传递登录报文/命令。推荐在 `ObMPConnect` 识别保留用户名后缀，转入实例认证和轻量 session；在 `ObSrvXlator` 按已认证的连接模式把 `COM_QUERY` 等命令交给实例处理器。这样复用同一个 reactor、端口和协议实现，不引入 `opensrv-mysql`/Tokio，也不创建完整的 `ObSQLSessionInfo`/NamespaceRuntime。

这仍然是实质工程工作：实例账户及密码校验必须有明确持久化归属，不能从 Namespace 1 的 schema 偷取账号；轻量 session 要随连接关闭清理事务；结果发送要复用 `sql-nio` 的现有响应 C ABI。至少核查登录/认证切换、`COM_QUERY`、`PING`、`QUIT`、`INIT_DB`、`RESET_CONNECTION` 和不支持命令的错误。该模式不能直接复用 `ObMPBase::get_session` 的绑定检查，需为实例处理器建立独立的窄基类/响应包装，避免把 Namespace session 伪造出来。

若现有 callback/请求交付模型证明无法隔离实例连接，后备方案才是独立管理监听器；也应优先复用已有 `sql-nio`，而非新增第二套 MySQL 协议库。当前 `nio_start` 的本地 Unix socket 路径固定，直接再启动一个 reactor 会争用 `run/sql.sock`，必须先改为可配置路径或仅 TCP。这个后备方案会增加 reactor 线程和监听资源。

Rust 使用独立 crate 承载 SQL 解析、入口语法检查和受限执行器，通过现有 Rust 静态库进入 C++，由窄 FFI 访问 `InstanceMetaStore`。当前 `sql-nio` 已通过 CMake 链成一个 Rust staticlib；新的 Rust crate 可以作为其依赖，避免再链接第二个 Rust staticlib。这个构建安排需在实现时验证，不改变 `sql-nio` 的网络职责。

表声明是数据：名称、collection ID、字段名/类型、主键字段、JSON 或 BYTES 值映射、读写权限。现有 `InstanceMetaKeyCodec` 已有 key 字段描述，值格式已有 JSON/BYTES 标记；应扩展统一描述，避免给每张表写扫描和显示代码。声明只属于实例元数据目录，不进入 Namespace schema。第一批声明可选 `NAMESPACES`、`SNAPSHOT_PINS` 和一个普通 JSON 表，覆盖单键、复合键与事务。

SQL v1 只接受单表 `SELECT`、`INSERT VALUES`、`UPDATE`、`DELETE`、`BEGIN`、`COMMIT`、`ROLLBACK`、`SHOW TABLES`、`DESCRIBE`。过滤先支持列与常量的比较、`AND`，结果支持按主键升序与 `LIMIT`。主键等值/前缀映射到 `KeyRange`；其他过滤在扫描回调中计算。拒绝连接、子查询、DDL、触发器、二级索引和任意函数。`ORDER BY` 非主键及倒序要排序，v1 拒绝，避免无界内存。解析后立即在本通道校验语法形状，再做目录绑定和执行；不让现有 SQL 引擎接触这些语句。

`SELECT` 在扫描回调中解码和过滤，按有界批次交还客户端。`UPDATE` 先收集候选 key，再逐个 `get_for_update`，用锁后读到的值重新判断谓词并计算新值，最后 `put`；`DELETE` 同样在锁后重新判断。`INSERT` 使用原生 `insert`，让存储引擎原子检查主键冲突。写入均由调用者的 `InstanceMetaStore::Transaction` 包裹，错误回滚；显式事务复用该对象。当前没有跨普通 SQL 表和实例 KV 的同事务需求，这项能力留待将来单独设计。

## GlueSQL Core 核查结论

暂不接入 GlueSQL Core。它确实提供 parser、planner、执行器和可替换的 Store/Transaction trait，但和这里的存储边界有三处实质冲突：

1. `UPDATE` 在调用 `insert_data` 之前已经扫描、计算并收集新行。`InstanceMetaStore` 的 `get_for_update` 必须在重新计算前调用，才能保证并发的 `SET n=n+1` 基于锁后最新值。GlueSQL 的 Store trait 无法在该步骤插入锁后重算。
2. `INSERT`、`UPDATE` 最后都调用 `insert_data`。本地必须分别使用原生 `insert` 和 `put`；仅在存储 trait 实现中看不到语句种类，需额外执行上下文。INSERT 的预检查也不能代替原子插入，因为有并发窗口。
3. `scan_data` 是拉取式、无过滤条件的 RowIter；现有 `InstanceMetaStore::scan` 是不可重入的回调。GlueSQL 的 SELECT payload 又把结果收集成 `Vec<Vec<Value>>`。可做缓冲/重扫适配，但会增加内存、复制和桥接代码。

若为了用 GlueSQL 而增加全局 SQL 写锁、改其执行器或放宽更新语义，复杂度和维护成本会偏离这个通道的目标。因此不下载/编译 GlueSQL 作体积试验；先把更关键的语义接缝判定清楚。

## Rust 库选择与验证顺序

优先试 `sqlparser-rs`：复用成熟词法、SQL AST、MySQL 方言解析；在独立元数据通道的解析边界检查 AST 形状。它没有执行器，受限执行器按上述语义实现。若实际 AST 边界过宽、很难保证拒绝所有不支持的写法，再换 `pest` 写严格的最小语法；无需现在维护两套 parser。网络和结果编码由现有 `sql-nio` 继续承担，parser 的边界只需覆盖 SQL 文本。

验证顺序：先做离线 Rust parser/AST 边界与表声明演练；再连 C++ KV 事务，覆盖并发自增更新、冲突插入、回滚和扫描分页；并行做现有连接模式与响应 C ABI 的接入验证；最后用 MySQL CLI 测实例认证、查询和事务。运行时和二进制大小只有真实构建后才能报告。本机 Cargo 缓存目前没有 GlueSQL/sqlparser 的 crate，不为已被语义审计淘汰的 GlueSQL 下载依赖。

## 依据

- 本地 `src/storage/instance_meta/instance_meta_store.h/.cpp`：事务、`get_for_update`、`insert`、`put`、不可重入扫描和快照语义。
- 本地 `src/share/instance_meta/instance_meta_key_codec.h`、`instance_meta_value_codec.h`：key 描述和 JSON/BYTES 值标记。
- 本地 `src/observer/mysql/obmp_connect.cpp`、`obmp_base.cpp`、`obmp_query.cpp` 与 `src/observer/ob_srv_xlator.cpp`：登录、session 和命令分派耦合。
- 本地 `rust/sql-nio/src/{handshake,login,command,response_api,reactor}.rs` 与 `rust/sql-nio/include/nio.h`：现成的 MySQL 协议与响应 C ABI；`reactor.rs` 的本地 socket 路径固定。
- GlueSQL v0.20.0 [Store trait](https://github.com/gluesql/gluesql/blob/v0.20.0/core/src/store.rs)、[执行分派](https://github.com/gluesql/gluesql/blob/v0.20.0/core/src/executor/execute.rs)、[INSERT](https://github.com/gluesql/gluesql/blob/v0.20.0/core/src/executor/insert.rs)、[扫描](https://github.com/gluesql/gluesql/blob/v0.20.0/core/src/executor/fetch.rs)、[事务 trait](https://github.com/gluesql/gluesql/blob/v0.20.0/core/src/store/transaction.rs)。
- Apache [sqlparser-rs](https://github.com/apache/datafusion-sqlparser-rs) 提供解析，不提供执行。
- [`opensrv-mysql` 官方 API 文档](https://docs.rs/opensrv-mysql/latest/opensrv_mysql/)：备选异步协议 shim，目前无须引入。
