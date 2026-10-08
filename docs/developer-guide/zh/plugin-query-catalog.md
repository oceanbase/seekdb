# 查询期 catalog：routine 查找与调用者事务内变更

这是 PG 式开放 catalog 设计的一步：插件可以在查询回调中查找当前数据库的独立
FUNCTION/PROCEDURE，而不必拼接系统表 SQL。另有实验性的单条 routine mutation
入口，贯通公开 C SPI、Rust SDK、正常 resolver 和调用者事务协调器。
**接口接通不等于完整实库事务语义已验证**；CREATE 的解析/writer 组合已有受控
fixture 成功证据，公开 SPI 成功写入与跨语句实库验证仍待完成。

## 接口与视图

SQL API minor 2 的 `seekdb_plugin_sql_api_v3_t` 保留 v2 前缀，追加
`lookup_routine(context, kind, name, name_size, result)`。kind 为 1（FUNCTION）或
2（PROCEDURE）；名称是 1–2048 字节 UTF-8 标识符内容，不带引号，不解释为 SQL 或
限定数据库路径，不接受内嵌 NUL。错误清空 object_id，保留原始 database_error；
成功时 object_id 为 0 表示不存在，不等同于无权限。

查找使用执行上下文已有的 schema guard 和 session 当前数据库；不另取最新 guard。
安装 builder 与查询回调复用 `catalog_routine_lookup.h`，共同执行名称校验、正常
SHOW 可见性检查和 guard 查找。若调用者视图已带 routine overlay，同样看到该视图
中的新增/替换与删除标记；这不表示查询回调已获得创建 overlay 或提交对象的权力。

宿主要求当前线程、当前执行上下文、匹配的 session/sql context、schema guard 和
物理计划，先检查取消/超时。不能在常量折叠、init/start、任意后台线程或 SQL 结果
consumer 中重入。执行中的重入错误不能被外层返回成功覆盖。

返回 ID 仅是当前视图的身份快照，不固定 schema version，不持有 module/object
lease，不授予 EXECUTE，也不自动生成对象依赖。后续实际使用必须经过正常 resolver
和执行权限检查；不能缓存该 ID 后跳过并发 DDL/schema invalidation。

## Rust SDK 与示例

`Call`、表函数 open 的 `QueryContext` 和 next 的 `Rows` 都提供：

```rust
use seekdb_extension::sql::RoutineKind;

if call.supports_catalog_lookup() {
    let id = call.lookup_routine(RoutineKind::Function, "my_function")?;
    // id: Option<u64>；快照，不是执行许可或提交证明。
}
```

示意代码所在函数应返回 `sql::Error` 兼容的 Result。已有 SQL 上下文即可提供该能力，
不另增标量/table service minor。SQL API 版本与 table context 的版本独立协商；旧
host 缺少后缀时能力探测返回 false，调用返回不支持，不能伪装成 None。

SDK 核对 API 大小、major/minor、全部保留字段、回调和结果布局/ID 范围。host 的
查找、SQL execute 与 poll 共用首错状态。Rust 表函数包装还会记住本地查找错误，
忽略失败后也不能继续 emit 成功行；close 仍可释放资源。普通业务代码应传播错误，
不能因为 capability probe 或一个整数返回值就假定有写权限。

Rust text 新增 `seekdb_rust_routine_id(bytes)`：查询当前库中的 FUNCTION，找到返回
ID，不存在或输入 NULL 返回 SQL NULL；权限错误/宿主错误直接失败。它依赖 schema
和调用者身份，不标记 immutable。模块当前为 `rust-text-stored-type-v11`，18 个
服务、21 个扩展对象，必须部署配套 manifest 和动态库。GIS 继续使用现有 C/C++。

## 实验性 routine mutation（SQL API minor 3）

`seekdb_plugin_sql_api_v4_t` 保留完整 v3 前缀，追加
`mutate_routine(context, sql, size, result)`；不改变既有 execute、poll 和 lookup。
输入是非空、最多 4 MiB、无 NUL 的单条 UTF-8 SQL，支持独立 FUNCTION/PROCEDURE
的 CREATE、MySQL 属性 ALTER、DROP（含 IF EXISTS）。多语句、其他对象类别、
CREATE OR REPLACE、CREATE IF NOT EXISTS 在操作前拒绝。普通 execute 仍不接受 DDL。

`Call`、`QueryContext`、`Rows` 提供独立的 `supports_catalog_mutation()` 探测和
`mutate_routine(&str) -> Result<Option<u64>, sql::Error>`。例如，在返回
`sql::Error` 兼容 Result 的查询回调中：

```rust
let provisional_id = call.mutate_routine("DROP FUNCTION IF EXISTS obsolete_model_fn")?;
// Some(id)：本次操作影响的对象；None：IF EXISTS 未命中。不是提交证明。
```

宿主继承当前身份、数据库与事务，执行正常授权、definer、依赖及 DDL 准入检查，
不另开安装事务，不隐式提交，也不自动建立 Extension 成员关系。输入错误同样进入
宿主首错状态；SDK 不在本地提前吞掉非法 SQL。表函数还记录本地错误，忽略失败也
不能继续发出成功行。context 仅当前同步回调使用，不可跨线程、重入或异步保存。

C 结果区分 NOT_STARTED、APPLIED、ROLLED_BACK、REQUIRES_ABORT，并分别保留主错误、
close、身份检查、数据回滚、视图回滚和 poison 错误。APPLIED 仅表示外层事务内
临时成功；ROLLED_BACK 仅表示本次操作撤回，不承诺整个查询或事务仍能继续。
REQUIRES_ABORT 由宿主撤销可用视图并禁止提交。失败不返回对象 ID；Rust 安全接口
传播主错误，不把清理结果误当成可提交对象，详细清理字段保留在 C/sys 层。

参考插件新增 `seekdb_rust_routine_ddl(bytes)`，使用上述 SDK，返回 provisional ID，
输入 NULL 或无命中返回 SQL NULL。它有 catalog 副作用，不标记 deterministic/immutable。
在完成下述实库验收前，不应把此参考函数作为已验证的生产管理接口。

## 与事务内创建的关系

当前安装 builder 的 Root DDL 事务不属于查询的调用者事务，且拒绝已有事务。
因此不能从本查询回调直接调用安装入口，也不能删掉普通 SQL SPI 的 DDL 禁止分支。

宿主现已新增 [routine/权限视图保存点](plugin-catalog-savepoints.md) 和 Rust 事务
视图日志。session 惰性持有参与者和同一对 schema/权限视图，公共 SQL 执行上下文
初始化绑定该视图，并在数据保存点回滚及同步/异步事务结束路径中处理它。
公开 mutator 现已连接这条路径，但不能仅凭接线宣称用户事务内的对象创建、后续
调用与回滚在真实服务器中已经成立。

宿主内部现有 `ExtensionRoutineResolver::mutate`：单条 UTF-8 routine SQL 通过
正常 resolver/ACL，然后由 Rust 协调器和调用者 frame 进行 DDL admission、实际
ID/version 预留、writer 写入及 schema/ACL staging。输入没有 Extension 包身份，
不会自动成为 member。MySQL ALTER 从同一事务读取原依赖，DROP 保留成员保护。
只有操作 APPLIED 才返回临时对象 ID；DROP IF EXISTS 未命中返回 0。这里没有
独立 commit/publish，提交仍属于外层事务。

上述公开 SPI/SDK 复用这条内部接线，仍未证明真实数据库中的后续 SQL
可见性、保存点与外层失败回滚、并发 DDL 和提交失败恢复；这些继续属于完整目标。

## 调用者事务 SQL 适配层

`CallerCatalogTransaction` 是宿主内部的 catalog SQL transport，不是新增的插件
API。它为已有接受 `ObMySQLTransaction &` 的 routine 写入与版本预留代码提供借用
适配，避免为了复用这些代码而额外开启 Root DDL 事务。Query routine mutation
经调用者 frame 使用它；transport 本身不分配对象、不检查对象 ACL，也不发布 schema，
DDL 锁和 epoch 由显式的 `admit_ddl` 阶段处理。

使用顺序为对象级解析/权限预检、调用者语句及操作保存点、打开适配层，再显式
DDL admission，最后核对对象状态并执行 writer。
查询期 `open` 要求当前执行帧、物理计划和实际活动的可写事务，绑定 transaction ID、sequence
base、数据库、权限主体及线程。不接受无事务、反序列化 session、正在提交或只读
事务；关闭时恢复嵌套 SQL 的 session 状态，不改变数据事务所有权。普通插件 SQL
继续走原来检查调用者权限的 SQL SPI；此内部连接关闭通用 SQL 权限检查，只能用于
已经过宿主对象级授权的生成 catalog SQL，不能向插件暴露任意 SQL 通道。

`BorrowedSQLTransaction` 不设置基类的 `in_trans_`，因此基类析构不会隐式提交。
它拒绝 start、end 和 acquire_connection，保留第一个错误；执行前后校验调用者
身份，失败时清空结果或 affected rows。底层 SQL 错误优先于执行后发现的上下文
失效，C++ 分配异常和其他异常转换为数据库错误。版本预留 token 与结果集必须先于
适配层销毁；整个使用期独占，不跨线程或异步边界，不能越过调用者 session/执行帧。

### 提交阶段入口（2026-09-08）

查询期 transport 现在还需显式调用 `admit_ddl(journal, service, fresh_reader,
refreshed_schema_version, absolute_deadline)`：首次取得 DDL 串行锁并在锁后验证
schema，再将捕获的 epoch/root barrier 存入 Rust；后续连接复用同一事务仍有效的
准入。准入前拒绝 catalog DML，logger 也拒绝缺少准入的写入；查询期读取/取连接
仍可服务于准入本身。保存点回滚可撤销准入，不允许在已有写入后补录 epoch。
详细顺序与 fresh-reader 契约见 [DDL 准入](plugin-catalog-savepoints.md)。

`open_for_commit(session, journal, transaction_id, sequence_base, absolute_deadline)`
不依赖物理计划，允许显式 COMMIT 当前帧为空或没有 physical plan。但它并不自己
开始 Rust 准备阶段：journal 必须是该 session 已拥有、ID/base 相同且处于 Preparing
的日志；宿主先持有 session 独占锁和 DDL 锁/捕获的 epoch。每次操作先检查真实活动
数据事务、只读状态、数据库/权限主体、线程及原执行帧，再核对日志归属和 Rust 状态。
这样 session reset 后不会继续解引用旧 journal；end-sign 后仍可继续 MDS/watermark。

新增数据层 `tx_desc_is_active`，复用原事务实现的活动状态判断。`is_in_tx` 的含义
较宽，包含部分已经结束的状态，不能把 `is_in_tx && !is_committing` 等同于可写。
查询/提交 transport，以及 session 视图/版本记录和准备提交的入口均改用活动判断。

提交 transport 使用有明确生命周期的 `ObTimeoutCtx`，取提交绝对截止时间与外层
上下文截止时间的较小者，不延长已有 deadline；同时检查 worker 超时和 session
取消。实际内部 SQL 沿用已有每次执行的 worker/session timeout guard。缺少普通
statement bookkeeping 时，仅临时建立嵌套保存所需的语句状态，不启动数据事务。
close 恢复嵌套状态、释放连接、撤销自己建立的 statement bookkeeping，再退出 TLS
timeout frame；结果集必须已关闭。恢复错误不能掩盖先前 SQL/状态错误。

提交模式的 DDL logger 使用 Rust `record_end_sign` 固定槽，不制造 query savepoint
或继续调用普通 schema operation 记录入口。调用方必须在 close 完成后，才将准备
结果交给 `complete_prepare` 并尝试真正数据提交。该入口现已接入 session 的
实际提交协调路径，替换无条件拒绝；仍需实库成功连接、错误恢复、取消、
身份变化、DDL 并发锁/epoch 和 publication 证据；拒绝路径测试不是这些证据的替代。

### 完整 routine 写入效果

`RoutineCatalogWriter` 将正常 PL DDL 的完整写入效果与 Root 的事务所有权分开。
传入真实、已准入的 `ObMySQLTransaction`、当前 guard 和宿主 schema/proxy 后，
CREATE/replace 共用 schema/dependency/error/automatic privilege 路径；reserved
CREATE 清除旧名称权限，reserved DROP 同样不因 automatic_sp_privileges 关闭而
继承旧 grants。Root 的原 owned/external 路径也复用此 writer，不另行复制一套
查询 catalog 系统表写入。writer 仅一次调用，失败后由事务所有者回滚。

这不是权限入口：调用者仍需普通 resolver/definer/ACL、当前 guard 与 DDL 准入，
保留实际 ID/version token、配对视图/data barrier，并在所有写入成功后 staging。
MySQL 属性 ALTER 必须用 replacement 并保留原 dependencies，不能把空数组解释为
“依赖不变”。writer 的 alter 方法只对应原错误状态/重新编译分支。

原 DROP operator 中的 `flush_pl_cache_by_sql` 使用全局连接，不能默认跟随借用
事务执行。writer 的 DROP 现在要求 `IRoutineCacheInvalidation` 宿主 sink；Root
明确选择原 flush 行为，查询事务必须选择事务内记录、已知提交后处理，回滚丢弃。
该 sink 不是插件 hook，不赋予 SQL 或事务控制权限。Rust journal 现已提供按数据
barrier 记录/回滚、已知提交后 peek/ack 的请求协议；查询 transport 提供校验事务
归属的借用 sink。session 在 Preparing 时预留计划缓存拥有的 Rust 队列快照，已知
提交无分配交接，后台复用缓存清理定时器，在 schema 版本就绪后驱逐并确认，失败
保留重试；未知结果保守驱逐但不发布 schema。详见 [队列协议](plugin-catalog-savepoints.md)。
公开查询 DROP 已接线，但真实事务/跨 session/旧 schema 并发验证仍未完成，不能把队列接线
视为查询 catalog DDL 全部完成。

### SQL 预检与剩余闭环

SQL 预检使用当前 session 模式的真实 parser：读只接受单条 SELECT，写只接受单条
INSERT/UPDATE/DELETE；先拆分语句，避免带分号的多语句绕过。DDL、事务控制、CALL、
SET 等在进入内部执行前拒绝。这是生成 SQL 的形状检查，不是解析任意用户 SQL 的
授权机制，也不意味着 SELECT/DML 内部没有函数调用或其他副作用。

嵌套 session 的保存路径也调整为先预留数据库名 buffer、完成可能失败的快照复制
和表数组容量预留，再清空调用者语句状态；tx result 的复制提前到基本语句保存
之前。SQL 状态保存中的数据库名赋值复用预留容量，独立调用该方法时也先复制名称
再修改审计和 inner flag。这样打开连接的保存阶段
失败不会先丢失调用者的 query/plan。恢复仍可能报错，必须传播给唯一事务协调者，
不能因连接释放成功而掩盖失败。该改动不承诺所有 autonomous transaction 保存路径
均已具备失败原子性。

剩余写入闭环包括公开 SPI 成功 CREATE 的完整服务接线及真实数据库验证：检查现有 resolver、
ID/version、writer、overlay、DDL 结束信号和提交后发布在整条调用链上的行为，
证明跨语句可见性、保存点、最终回滚、提交失败及并发 DDL。不能将内部接线或
受控 fixture 当作这些语义已经成立的证据，也不能复用独立事务安装器来冒充查询创建。

具体还需承接 `ObDDLSQLTransaction::end()` 中提交前的 DDL epoch 校验、
`OB_DDL_END_SIGN`、`DDL_TRANS` 数据事务信号和 normal schema watermark 推进。
原 Root 代码通过线程局部 `TSILastOper` 记录最后 schema version；调用者事务跨
语句或执行线程交接时，不能直接把这个值当成事务完整的 schema 操作集合。借用
client 的 `log_operation` 路径现已改由 Rust 日志记录成功写入的最大版本和数量，
并随数据 barrier 回滚恢复，详见 [事务版本记录](plugin-catalog-savepoints.md)。
提交前仍需按既有并发 DDL 锁顺序完成上述步骤，watermark 不得在对象写入中途提前
推进。当前 session 使用 Rust Preparing 与宿主效果协调协议执行这些步骤，任一
错误禁止提交；不能据此宣称完整查询 catalog 写入已完成。

Root 现已将 end-sign、DDL_TRANS 和 watermark 抽成不拥有 commit/rollback 权力的
`CatalogCommitPreparation`，其正常 end 路径复用该组件。组件使用明确版本输入，
可与 Rust 日志和借用 client 配合；它不检查/获取调用者应先持有的 DDL 锁和 epoch，
也不负责提交后的发布。session 已接入：已确认提交在返回响应前推进共享的
已提交插件版本与会话 Read-After-DDL fence，并投递不等待的后台刷新。
新 MySQL 请求（包括独立连接）在取得语句 schema guard 前等待两个版本的最大值；
不在事务回调或内部刷新 SQL 中等待。入队失败保留共享屏障，详见
[提交前数据库步骤](plugin-catalog-savepoints.md)。

## 验证范围

`SHOW CREATE FUNCTION/PROCEDURE` 的虚拟表扫描有独立的已提交 schema guard，
首次读取时须绑定调用者的 catalog 视图，不能仅依赖解析阶段的绑定。
`show_routine_catalog_fixture.h` 覆盖真实 iterator/printer 的私有属性、DROP、
新身份与保存点回退，同时检查独立会话、查看权限及失效事务身份。它不替代
完整虚拟表扫描及实库事务验收。

事务私有 routine 快照必须拥有跨请求存储：`RoutineSchemaOverlay` 的每个
record 显式持有 arena，并用它构造 `ObRoutineInfo`。仅将外层对象放在堆上再
调用 `assign` 不足以保证参数与字符串存活，因为默认 `ObSchema` 在请求线程
上可能选择请求分配器。2026-09-21 增加带 request flag、请求 arena 释放/重用、
新 guard 查找以及依赖解析的受控回归，覆盖这一生命周期边界。

`caller_routine_mutation_fixture.h` 使用真实 ObSql/ObPL collaborator 执行 CREATE
预检及 writer，检查新 routine 的 ID/参数版本、owner、自动权限、第二个 routine
引用新对象所生成的依赖 SQL。Rust journal 接受模拟的已确认数据回退通知后，
验证 schema/ACL 和版本记录按 barrier 撤销。另覆盖写失败、同名冲突和缺失被调用
函数的严格解析拒绝。SQL 行、分配器、身份及 DDL 准入是受控输入；没有运行新
routine 的 SQL 调用或数据提交/回滚，也没有初始化完整服务器。本沙箱本地 socket
创建返回 EPERM；不能因此把这些用例当作实库验收通过。

mutation 的 SDK 用例覆盖旧 API 分配边界、各版本/保留字段、独立能力探测、临时 ID、
无命中、主错误与清理结果、畸形返回值、非法 SQL 交由宿主记录，以及表函数 open/next
忽略错误后的行发出限制。`query_mutation_fixture.h` 通过实际公开 host API 和 Rust
DSO 检查拒绝/prepare 失败路径；它明确没有数据事务服务，不是成功写入或提交证明。

新增 SDK 测试覆盖有值/不存在/数据库错误、旧布局与缺少能力、非法保留字段/名称/
结果 ID，以及表函数 open/next 吞错和析构。C/Rust ABI 测试对照大小、对齐和偏移。

`query_catalog_fixture.h` 验证真实 host lookup、schema guard、SHOW 检查、base/overlay
查找、删除标记、函数/过程命名空间、大小写、超时、错误上下文、跨线程和重入。
部分场景还通过实际 loader 调用 Rust DSO；验证结束后没有插件 lease 或新建的调用者
数据事务/保存点。schema storage 由内存 fixture 提供，不代表真实服务器的认证、
并发事务隔离或 catalog 写入回滚已经通过。最新执行结果见
[实施记录](plugin-implementation-status.md)。

借用适配层的 fixture 验证普通读写委托、start/end/acquire 拒绝、错误后禁止继续、
前后身份校验及异常转换，不连接数据库。内核用例另外覆盖真实 parser 的单语句
检查、无活动事务拒绝，以及 session 嵌套保存/恢复；两条定向分配失败通过替换
快照数组 allocator 检查调用者的 query、plan、原表列表、autocommit、审计和嵌套
状态未被消费。这些都不能替代真实活动事务里的系统表写入、锁和提交/回滚验证。

## 实库验收入口（2026-09-10）

新增 `rust/plugin-runtime/tests/query_catalog_server.py`，只连接显式指定端口的
一次性 loopback 实例。需要部署配套 `rust_text`、安装 PyMySQL，管理员具备创建
测试数据库/用户的权限，且 `automatic_sp_privileges=1`。脚本不安装插件、不修改
全局设置；密码只从 `SEEKDB_TEST_PASSWORD` 环境读取，不打印或放进命令行。

```sh
python3 rust/plugin-runtime/tests/query_catalog_server.py \
  --port 2881 --user root --confirm-disposable-server
```

脚本通过真实 Rust `seekdb_rust_routine_ddl` / `seekdb_rust_routine_id` 公开路径检查：

- table-free SELECT 的自动提交、返回 ID 与跨会话查找/调用；DROP / IF EXISTS。
- 调用者事务中先写业务行，再创建函数、依赖函数和过程；同事务后续 SQL/PL 可调用，
  观察者提交前不可见，提交后同时看到业务行和 routine，不允许隐式独立提交。
- ALTER 属性、DROP/同名重建、重复使用用户保存点；恢复原身份、定义和权限。
  提交重建后重复执行观察者已使用的 SQL，检查旧编译结果与名称权限没有被继承。
- 两行聚合调用 mutator 的成功对照，再令依赖该聚合的 EXP 溢出：错误发生在写入后，
  不依赖不同 SELECT 目标的求值顺序。自动/显式事务都撤销本语句 routine，保留较早
  调用者 DML；失败后同事务继续创建，再整体回滚，验证数据和私有视图一起撤销。
- 无 CREATE ROUTINE 用户拒绝写入；只有 CREATE ROUTINE 的用户在当前事务和提交后
  获得正常自动 EXECUTE/ALTER 权限；保存点恢复对象与 ACL，无关用户不能调用。
- duplicate CREATE 与只读事务按精确协议错误拒绝；动态业务对象不自动成为成员。

只在整个矩阵及清理成功后打印 PASS。超时、断连、语法或缺插件错误不能冒充预期
拒绝；CALL 会排空所有结果并传播后续结果错误。正确性检查不用 Python assert，
`python -O` 不会移除它们。成功只删除本次随机创建的数据库/用户；失败先回滚并关闭
客户端，保留已创建资源、打印名字供诊断，不重试未知提交，也不自动删除诊断状态。

离线 `test_query_catalog_server.py` / CTest `plugin_query_catalog_runner` 仅检查脚本的
结果形状、精确错误、确认参数、资源清理和报告行为，不使用 PyMySQL 或数据库。
它们通过不能作为上述服务器矩阵通过的证据。

Agent 沙箱 socket 创建返回 EPERM；后续由用户在宿主机启动实例并执行此脚本。
2026-09-10 的实测已确认插件安装及两个 NULL 探针成功，但首个自动提交写入
返回 6205。日志中的描述符有事务 ID、正 sequence base 和保存点，仍为 IDLE。

修复将写入前的借用准入与真正 ACTIVE 状态分开：有身份和隐式保存点的 IDLE
可以建立/读取私有 catalog 视图，首次存储写入沿原路径激活同一事务。没有保存点、
身份改变、回滚中和终止状态仍被拒绝；记录已完成的 schema 写入及提交准备仍要求
ACTIVE，不由保存点或私有视图伪造提交资格。另在 table-free SELECT 成功建立
插件保存点后补齐 session 的 start-statement 登记，已有 result-set close 负责
配对结束；保存点创建失败不能留下无人清理的开始标记。

替换上述修复后，用户首个 CREATE 继续返回 4016。进一步补齐 Root DDL 锁适配：
外部 SPI 连接未调用 start_transaction，不能直接满足旧锁路径的连接事务标记。
对已准备的嵌套调用者，仅在事务内加锁期间临时借用该标记，正常及异常退出均恢复；
不独立开启/提交事务，仍拒绝只读及失效事务。阶段日志调整为默认日志级别可见。

后续实测通过加锁，但未限定库名的 catalog INSERT 被解析到业务库，返回 1146。
catalog 生成 SQL 现显式访问 `oceanbase` 下的 routine/权限/依赖/错误记录、DDL 日志、
成员表及 core-table，不通过切换调用者数据库或改用户身份解决名称解析问题。
受控回归增加实际生成 SQL 的 parser 关系节点检查，业务 SQL 默认库行为不变。

随后实测在 INSERT 结果关闭处返回 4016：catalog 内部 SQL 没有 plugin 嵌套标记，
`set_end_stmt` 将嵌套计数 1 当作外层语句的非法状态。catalog 专用连接现显式启用
嵌套执行上下文，并保留调用方事务类型；每次结果初始化验证借用状态，销毁清除标记。
没有父执行上下文的 COMMIT 准备仍依据显式 plugin 标记及嵌套会话设置层级，
不放宽外层语句结束断言，也不改变普通内部 SQL 的事务行为。新增受控回归直接
覆盖真实上下文连接及语句结束函数，不只验证 begin/end nested session。

最新实测通过写入后，在提交前的 DDL epoch 校验返回 1146：`ObGlobalStatProxy`
有直接查询 core-table 的路径，没有经过通用 `ObCoreTableProxy`。现已补齐该
代理全部直接 SQL 的系统库限定，并新增真实 epoch 管理器/SQL 生成路径的受控
检查，保留 epoch 不匹配等拒绝行为。此次日志确认数据事务回滚成功；后续
客户端清理报错来自服务端提交失败后主动断连，不应据此直接清库。
同一提交路径的 DDL MDS 信号登记也补齐了借用连接准入：只接受显式 catalog
模式下的嵌套可写 ACTIVE 原事务及 `DDL_TRANS` 类型，不授予连接事务所有权，
普通内部连接行为保持不变。受控检查验证准入，不代表实际 MDS 提交已通过。

9 月 16 日实测进一步通过 epoch 与 MDS，但 schema 水位 UPSERT 的重复键处理
在匿名保存点回退时触发 4109：冻结的 catalog journal 拒绝了正常的内部数据回退。
现将 Preparing 阶段、身份及嵌套上下文有效、严格位于当前子语句内部的根分支
回退视为 catalog 视图无变更；数据回退仍必须先成功。journal 保持冻结，普通
catalog/整条语句/旧保存点/跨分支回退不被放开。日志确认此次事务随后完整回滚，
服务端断连解释了脚本后续清理失败。受控回归覆盖边界，实库提交仍需复测。

这些修复仍需替换服务端后重跑上面的实库命令，不能将受控状态/内核回归等同实库
矩阵通过。并发 DDL、提交失败/未知结果注入、重启恢复、跨平台与性能还需独立场景；
该脚本没有声称覆盖这些目标。当前接口仍属实验性。

关联：[安装期 builder](plugin-catalog-builder.md)、[同步 SQL](plugin-table-sql.md)、
[完整设计](plugin-next-design.md)。
