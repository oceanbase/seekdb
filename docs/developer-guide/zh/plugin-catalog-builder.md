# 事务内 Catalog routine builder：C ABI 与 Rust 插件

通用对象构建接口已从宿主桥接接到实验性插件 C ABI 与 Rust SDK。支持安装事务内的
FUNCTION/PROCEDURE 构建，不等于查询期 DDL 已开放。原来的安装前 SQL 声明接口
继续存在；不能将普通 SQL SPI 的 DDL 禁止分支直接移除，或在查询回调中另行提交 DDL。

## 插件接口

`catalog_spi.h` 新增 `seekdb_plugin_catalog_service_v2_t`：以原 v1 为前缀，
spi_major=1、spi_minor=1，新增 build 回调。原 v1 prepare 上下文和布局不变；新宿主
仍接受旧服务，旧宿主会拒绝新 SPI minor，不会忽略 build 而假装完整安装成功。
新版服务仍执行 prepare，随后在安装事务内执行 build；两阶段可以有任一阶段不产生
对象，但整个安装最终必须有对象。更新仍使用显式脚本，不重新运行 build。

Rust 插件实现 `TransactionalInstaller: Installer`，导出 `Service::<T>::V2`。
build 得到 `TransactionContext<'txn>`，可调用 `create_routine(sql)`，得到
`RoutineId<'txn>`。上下文不能 Send/Sync，ID 生命周期绑定构建视图；原始数值可读取，
但不是提交证明，不能在失败后当成有效持久身份。

`rust_text_built` 示例只交付 control 文件，关联 `org.seekdb.rust-text`。Rust build
先创建 `rust_built_length`，拿到其 ID 后创建引用它的 `rust_built_nonempty`，并核对
两个预留身份不同。没有占位基础 SQL，也不在 init/start 阶段写 catalog。
模块仍须事先安装且 ACTIVE，SQL 端在目标数据库执行 `CREATE EXTENSION rust_text_built`。
样例当前只有 1.0，没有宣称提供其他版本或升级脚本。

loader 在 prepare 前取得 service lease，并保留到整个安装和 schema 发布结束。
事务构建程序与 tenant/database/owner、包名/版本和 native module 身份绑定；宿主
preflight 不能换用另一份身份。build 最多执行一次；C++ 与 Rust 两侧都检查错误，
宿主的原始创建错误优先保留，不因插件返回成功而被吞掉。

## 构建语义

### 事务视图内查找

build context 新增可选 v2 后缀 `lookup_routine(kind, name, id)`，保留 v1 前缀和
SPI 1.1 的回调签名。调用方先检查 `struct_size`；Rust SDK 提供 `supports_lookup()`
和 `lookup_routine(RoutineKind, name) -> Result<Option<RoutineId<'txn>>>`。旧宿主
不提供后缀时仍可 create；调用不支持的 lookup 会失败，不会把“不支持”伪装成不存在。

名称是未加引号的 UTF-8 标识符内容，最多 2048 字节，不是 SQL 片段，也不解析限定
数据库路径。查找固定使用安装目标库，区分 FUNCTION/PROCEDURE，并由 schema guard
执行正常名称比较。会话身份必须仍为安装 owner，使用正常 routine SHOW 可见性检查，
再从同一 guard 查询；因此既能看见已有 routine，也能看见本事务刚 stage 的对象。
不可见返回权限错误；可见性允许但不存在返回 `None`，不污染后续构建。

查找不建立 Extension 成员、不增加持久依赖，也不授予 EXECUTE。对象被后续函数体
使用时，仍由普通 PL resolver 生成依赖和 schema-version fence。所有 ID 都绑定本次
视图，刚创建的 ID 仍是预留身份，不能当成提交证明。查找和创建共享首错状态：无效
输入、权限或宿主错误之后，即使插件返回成功，安装也失败；正常的不存在不是错误。

`rust_text_built` 先确认函数不存在，创建后用大写名称查回相同 ID，并确认同名过程
不存在，再创建引用该函数的对象。模块 build ID 已更新；既有服务版本与准备接口不变。

### 创建与提交

`share/plugin/catalog_builder.h` 定义宿主内部 `ICatalogBuildProgram` 和
`ICatalogRoutineBuilder`。程序绑定到 `ExtensionRoutineResolver::install`；绑定者
负责让程序及其 module lease 存活到安装、发布结束。preflight 可重复且只检查声明，
build 最多运行一次，在静态安装脚本之后执行。

Root 的现有安装 adapter 先检查事务、provider、身份和权限，建立 routine schema /
privilege overlay，然后解析、admit 和 stage 静态对象，最后运行构建程序。
`create_routine(sql, object_id, error)` 每次接受一条新建 FUNCTION 或 PROCEDURE：

1. 复用 Rust 内存包源校验和内核 parser；多语句、非 CREATE routine 及非法内容失败。
2. 在当前事务内 schema view 下执行正常 resolver 和权限检查，产生完整拥有的参数。
3. Root 检查目标库、owner、名称冲突、只读限制等，预留真实对象 ID 和 schema version。
4. 对象和自动权限进入本次安装的 overlay，才向构建程序返回 ID。

后续构建可根据该 ID 决定下一对象，并在 SQL body 中引用已构建对象。普通 routine
依赖和 schema-version fence 继续由 PL/resolver 生成，不能只把 ID 放进另一份 registry。
返回的 ID 是**尚未提交的身份**；不能据此宣称对象已经全局可见或在失败后继续可用。

所有对象构建结束后，Root 才通过原 DDL operator 写入 schema。成员、Extension 身份
和依赖仍由原 Rust 安装协调器驱动同一 DDL 事务提交；不新增事务、提交或提前发布。
零静态脚本只在 native source 加上构建程序时进入该入口，构建后对象集合仍必须
非空。普通静态安装与更新 API 保持原语义，更新目前不执行构建程序。

## 错误与生命周期

- 构建 capability 只在同步 build 调用内、同一线程可用，不得保留或交给后台线程。
- SQL 总字节计入静态文件及原安装声明，总上限 4 MiB；静态/动态对象总数最多 4096；
  已解析的完整 DDL wire 参数总上限沿用 64 MiB。
- 第一次构建失败后，后续 create 返回同一错误，输出 ID 清零。程序返回成功也不能
  吞掉这个错误。返回零/非法 ID 的宿主 stage 被视为失败。
- 成功、失败或异常之后均不能重跑同一个构建程序。参数由 resolver 持有到 Root
  持久化结束，不引用临时 parser arena。回调不得执行外部副作用。
- 未知提交和 schema 发布失败仍沿用现有恢复规则；构建程序不拥有提交/回滚权限。

## 验证边界与后续工作

新增 kernel fixture 使用真实 Rust source/parser、PL resolver、ID/version reservation
和 schema overlay，覆盖第二对象引用第一对象、稳定 ID、版本 fence、参数生命周期、
权限失败、非法/多语句/超限输入、stage 失败、回调吞错、异常和禁止重跑。
该 fixture 的 schema/事务传输仍受控，不证明完整 Root 实库安装、隔离或回滚。
最新运行结果见 [实施进度](plugin-implementation-status.md)。

ABI 布局测试对照 C/Rust 的大小、对齐和字段 offset；旧/新服务与短布局/未知版本
拒绝有宿主测试。真实 Rust DSO 的 build 回调已纳入 kernel runner，使用实际 parser、
PL、ID/version reservation 和 schema view，检查对象引用和 build 期间 module lease。
2026-09-08 的完整 kernel runner 已通过这些路径，22 项独立 CTest 同时通过。
后续同日的 lookup 回归也已通过：15 个宿主构建场景涵盖 base procedure、overlay
function、大写名称、函数/过程命名空间、六类查找失败及跨 lookup/create 的首错传播；
真实 Rust DSO 同时执行查找和构建。SDK 对短上下文、可选后缀、非法 ID 和借用身份
生命周期的测试通过。
这里验证的是对象构建和语义解析，不是新函数的实库 SQL 返回值或 Root 事务行为；
最新完整记录见实施进度，不能把受控 fixture 等同于实库已验证。

接下来需要更广的对象查找/构建 API，以及真正的查询期 catalog 事务参与。更广的类型、
operator、index 对象仍需继续扩展内核能力；不能将当前 routine builder 称为完整
PG 式 catalog 自由度。
