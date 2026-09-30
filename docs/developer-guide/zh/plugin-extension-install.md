# 数据库级 Extension 安装与移除：实现进度

已实现安装身份与成员元数据，以及下述用户命令到事务适配器的接线。
`record_extension_install` 已由新的 catalog 安装协调器调用，并新增复用真实 routine
DDL 的具体 adapter。用户 SQL CREATE→包源→脚本 resolver→Root command→adapter
现已接线，尚未通过真实数据库回归，不能视为已完成完整 Extension 安装语义。
`record_extension_drop` 已由 Rust 移除协调器接入，routine 删除 adapter 已加入；
用户侧 DROP 命令与 Query/Root capability 已连接，真实删除事务尚未回归。
用户侧 `ALTER EXTENSION name UPDATE [TO 'version']` 已连接认证源版本读取、Rust
版本路径与顺序更新协调器；同版本 no-op 仍验证权限，完整实库更新行为尚未回归。
现有 `INSTALL PLUGIN` 继续保持进程级包/模块管理语义，没有被悄悄改成数据库级命令。

## Native-backed routine 包

顺序安装路径现在接受 control 的 `native_module`，将其原样绑定到
`ExtensionInstallSpec`，并与脚本和预检快照比对。不能在 Query→Root 过程中
丢失关联、把带模块的包当成纯 SQL 包，或替换成另一个模块。旧的仅返回 CREATE
参数数组的 `resolve()` 入口仍拒绝 native 包，因为它不承载安装身份与模块关联。

这里创建的是普通 FUNCTION/PROCEDURE schema 对象，可在 SQL body 中组合已加载
插件函数；不是新的 `LANGUAGE C`/任意符号绑定语法，也不是运行时 catalog builder。
类型、权限、只读、名称、owner、对象成员及事务路径继续复用现有 routine 机制。
`requires` 已接入同库已安装 Extension 的显式依赖；见下节。其他对象类别、
CASCADE 和用户事务加入仍未开放。

已有 catalog recording 在同一 schema 事务中锁定 native provider，检查 desired/
actual ACTIVE 且无未完成操作，再记录独立 Extension 身份、module ID 和成员。
检查失败由原 Rust 安装协调器回滚；这里没有添加第二次提交或自动加载模块。
模块的 RESTRICT 卸载查询数据库级 Extension 关联。该字段是显式声明依赖，
不是对所有 SQL body 中 native 调用的完整自动依赖收集。

更新仍锁定安装 ID/version/owner，控制文件必须保留原 native module ID。routine
updater 不再仅因关联了 native 模块而拒绝，SQL 更新不等于 native 热替换。
删除仍检查每个 routine 的权限和传入依赖；不再拒绝 native-backed 的 routine
成员集合，不要求重新加载模块。DROP 只移除 SQL 对象及关联，不停用共享动态库；
非 routine 成员、损坏记录和不支持的依赖仍会拒绝，不承诺修复任意损坏 Extension。

新增 [rust_text_ops SQL 包](../../../plugins/sql_packages/rust_text_ops/README.md)，
包括基础安装和 1.0→1.1 更新。CMake 仅交付 SQL/control 文件，不使可选动态库
参与 core 默认构建。kernel fixture 对安装后的包文件做真实 PL body 解析，
通过真实 Rust loader/registry 选择 native 函数；Query→Root 关联传递使用受控
持久化替身。以上不代表实库 CREATE/ALTER/DROP、鉴权、模块卸载竞争与回滚已验证。

## 安装脚本内的 native FUNCTION 授权

顺序安装 adapter 已接入 `GRANT`／`REVOKE`：例如先用 `LANGUAGE C` 创建 native
FUNCTION，再按签名对已有用户授权。每条 CREATE 先写入安装协调器的未提交事务，
后续授权使用同一事务、schema overlay 和 privilege overlay；没有独立 DCL 提交。
请求经过真实 resolver、普通权限检查、当前 actor/角色绑定和 Root 端再次准入。

支持安装数据库内 native FUNCTION 的 EXECUTE／ALTER ROUTINE、grant option
以及 REVOKE 的 RESTRICT／CASCADE。拒绝跨库目标、自动创建用户、密码修改（包括
显式空密码）和认证子句。DCL 不产生 extension member；安装失败由协调器统一回滚，
确认提交之后才发布 schema。不因使用内部 SQL 连接而跳过权限。

caller-routine SPI DCL 和用户活动事务中的扩展安装仍未开放。
目前通过构建、真实 GIS DSO/resolver 和受控 writer 回归。Root 与测试共同使用
`make_routine_extension_installer` 返回的真实 adapter；新增受控 SQL transport
用例将其与实际 routine/ACL writer、catalog binder 和 Rust 安装协调器串联，
验证 CREATE→GRANT→REVOKE→CREATE 私有权限变化、仅两个 CREATE 产生成员、
21 个写入位置逐一失败、4 个解析回调失败位置以及未知提交/回滚结果不重放。
这些用例提供已解析输入、受控版本分配和 SQL 行，不执行 parser 或模拟数据库
撤销数据。实库提交、回滚、并发与恢复仍待验收，不能据此宣称完整 PG 事务语义。

UPDATE 脚本也已接入上述 native FUNCTION DCL，但采用两阶段执行：准入时锁定
已发布对象的完整 ACL 基线，合并前序私有权限并预留版本；新建对象不尝试锁定尚未
写入的 SQL 行。整段准入成功后恢复初始视图，再按语句顺序写入并重新鉴权。发生
ACL 状态偏差时拒绝原计划，不另开事务或偷偷重算。DCL 不增加 extension member。

真实 Root UPDATE adapter／协调器回归覆盖新建函数和已发布成员，分别逐一注入
22／17 个 SQL 写入失败点，另覆盖解析失败、未知结束与 ACL 状态偏差。GIS fixture
验证真实升级包内的 GRANT／REVOKE、grant option 和 CASCADE 解析。这里只证明
受控传输和解析路径通过；实库事务验收仍待完成。

已补充新建／已发布对象的 GRANT→ALTER→REVOKE 和 GRANT→DROP→同名重建
回归，检查版本、ACL 与成员身份。扩展 DROP 不再在提交前发送独立的 PL-cache
刷新 SQL：请求记录在外部 DDL 事务中，提交前预留现有 Rust 队列容量，确认提交
后由 schema 版本门槛控制本地淘汰，确认回滚取消，淘汰失败保留重试。未知事务
结果只能触发保守的版本门槛淘汰，不宣称提交或重放 SQL。上述缓存 journal／queue
测试与 Root SQL fixture 均为离线验证，真实存储提交、并发、恢复仍待验收。

## Extension 组合依赖：requires

control 的 `requires = 'base_text, utility'` 声明当前 tenant/database 中必须已安装的
Extension。文件和内存源传递同一份声明，不会自动下载、安装或加载依赖，也不会授予
调用依赖对象所需的 SQL 权限。Rust 校验最多 64 个包名、字符/长度、重复和自依赖。

安装协调器在 schema adapter 创建对象之前，按名称排序锁定 provider 安装行。
native-backed 包先锁定 native provider，再锁 Extension provider，保持统一锁序。
缺失依赖、损坏身份和解析到同一 provider ID 的重复声明均拒绝。记录接口再次检查，
将稳定 ID 依赖边与安装身份、成员写入同一 DDL 事务，不产生第二次提交。

新增 `__all_extension_dependency`（开发表号 1152），主键为
`tenant_id / database_id / required_extension_id / extension_id`。持久依赖引用安装 ID，
不引用某次装载的 module generation。删除 provider 时，在解除成员保护和删除对象
之前检查传入边并执行 RESTRICT；直接 catalog record-drop 也重复检查。删除 consumer
清理其出边；整库删除在同一数据库 DDL 事务中清理本库依赖，不卸载共享动态库。

版本发生变化的 UPDATE 现在可以增加、替换或移除 `requires`。目标集合来自当前包源
的有效版本 control/内存声明，必须有明确的版本更新路径；没有自动推断迁移。
次级 control 按字段覆盖主 control；所选中间步骤额外需要的 provider 作为独立
`prerequisites_` 传递，执行前与最终 provider 一起锁定，但不写入最终 Extension
依赖边。两组名字合计最多 64 个。缺失临时 provider 同样在 schema apply 前失败。
同版本 no-op 仍要求集合不变（顺序可以变化），不能靠编辑 control 静默
改变已安装版本的依赖。

更新协调器在锁定安装实例之前，在 `__all_plugin_sequence` 中取得专用行
`sql-extension-dependency-update` 的事务写锁。它只用于序列化依赖图更新，不分配 ID，
不复用 `sql-extension-instance` 分配序列，以避免与“provider→ID 分配”的安装锁序
相互等待。当前该锁在实例范围共享，跨库版本更新也会串行化；它不进入查询热路径。
读取/锁定同库 provider 和完整依赖边后，Rust 按稳定 ID 构造替换后的图，复用非递归
依赖排序器拒绝循环。图上限为 65,536 个身份、1,048,576 条边；不开放任意大小图。

上述检查在成员 detach/schema apply 之前完成。原依赖删除、新依赖插入、完整成员
集合及目标版本仍在同一 DDL 事务中提交，任何前置或记录失败都回滚；提交结果未知
不自动重试。原生模块关联仍不能随 SQL 更新替换。普通 routine 的真实对象依赖继续
有效：从 requires 移除声明不意味着可以删除仍被 SQL body 引用的 provider。
CASCADE、跨库依赖、依赖版本约束与自动安装尚未实现，也不宣称完整 PG 通用依赖语义。

受控 SQL transport 测试覆盖实际 catalog 查询/写入、Rust 协调器及提交/回滚选择；
不模拟数据库隔离或实际撤销数据。实库并发安装/删除、事务回滚、重启恢复及新增
系统表的 bootstrap/升级仍需验证；缺表不能当成无依赖继续执行。

组合示例 [text_composed](../../../plugins/sql_packages/text_composed/README.md) 依赖
`text_ops`，以普通 SQL body 调用 provider 的字符数/字节数函数；无需新增 native
service 或内核 factory。1.0 提供 ASCII 判断，1.1 更新新增额外字节数函数。kernel
用例从实际 CMake 安装产物解析 provider/consumer，检查 PL 编译得到的 routine-ID
依赖与 schema-version fence。独立实库脚本增加函数结果、依赖 ID、跨库拒绝、更新
保留边、RESTRICT 与整库清理检查；其执行结果不包含在模型或 kernel 回归中。

## 独立身份

新增系统表 `__all_extension_instance`（开发分支表号 1150）：以
`tenant_id / database_id / extension_name` 为主键，保存独立 `extension_id`、
`owner_id`、SQL extension 版本和可选的 native module 逻辑 ID。
没有 module generation、函数地址或进程 incarnation 字段。

新增 `__all_extension_member`（开发分支表号 1151）：以
`tenant_id / database_id / object_class / object_id` 为主键，指向所属
`extension_id`。同一数据库中的同一 schema 对象不能属于两个 Extension。
不同数据库可以拥有同名安装；object class 由 schema 层定义，不能把当前插件
descriptor 字符串或运行时 generation 当作 schema object ID 填入。

SQL 包版本与原生模块业务/ABI 版本分别管理。没有 native module 的记录可表示
纯 SQL 包的安装元数据。现已有 Rust control/安装 SQL 读取器及纯 SQL 包源文件
交付，并已连接 CREATE 的脚本执行入口；详见 [包源读取](plugin-extension-package.md)。
表号仍需按项目发布流程做跨分支分配核对，本轮只验证当前工作树没有冲突。

## Rust 与 C++ 的职责

`rust/plugin-runtime/src/extension_install.rs` 负责安装请求的身份、长度、UTF-8、
成员重复及配额校验。不对 SQL 名称做擅自大小写转换，不启动数据库事务，也不
保存调用者指针。名称应由 schema namespace 层先按 seekdb 规则规范化。

`ObPluginCatalog::record_extension_install` 是 host 内部接口：

1. 调用者先验证目标数据库/owner 的权限、包可信性，并在同一写事务中创建或
   验证所有实际 schema 对象。
2. 记录接口先调用 Rust 验证完整请求；原生模块场景锁定 provider 包行并确认
   它处于稳定 ACTIVE 状态。
3. 使用独立的 `sql-extension-instance` SQL sequence，在调用者事务中加锁分配
   稳定安装 ID，再写安装行和全部成员行。sequence 不复用模块操作/generation。
4. 成功返回的 ID 在调用者提交前仍是 provisional；任何失败都要求调用者回滚
   **整个安装事务**，不能把部分写入提交。接口不自行 commit/rollback。

这不是一个允许插件直接写系统表的公开入口。新的协调器已调用它，但权限、对象
存在性和具体 DDL 的事务能力仍需 schema adapter 证明，不能用 Rust 输入校验替代。

## 已接入的自动提交安装协调路径

`ObPluginCatalog::install_extension` 接收核心侧 `IExtensionSchemaInstaller`，调用
Rust `seekdb_runtime_extension_install_run` 驱动以下步骤：

1. preflight：检查输入并由 adapter 校验权限、包可信性和支持的 DDL 集合，不写入。
2. begin：创建一个本次安装独占的事务；schema adapter 可提供未开始的内核 DDL
   事务及已核验的 schema version，由协调器调用其虚函数开始/结束。
3. apply：adapter 在该连接/事务内创建或验证实际 schema 对象，返回其成员 ID。
4. record：在同一事务内写入安装身份和成员归属，重复成员等错误仍会触发回滚。
5. commit：仅全部步骤成功后提交；此前任一步失败进入统一清理。

驱动明确区分未开始、已回滚、已提交、提交结果未知、回滚结果未知，保留原始错误
和清理错误。开始事务失败也尝试清理。提交报错后不盲目回滚或自动重试；需要在新
连接中按 tenant/database/name 核验持久状态。诊断字符串分配失败不能把“事务结果
未知”降格为普通内存错误。若 adapter 意外结束事务，不能把后续空 rollback 当成
成功撤销已发生写入。

此路径适用于**独占事务的自动提交管理操作**，不加入调用者已有的用户事务。
后续 SQL executor 必须明确选择事务语义；它不是 `BEGIN; CREATE EXTENSION …;
ROLLBACK;` 的已完成实现。`record_extension_install` 仍保留显式传入事务的低层
入口，供后续 schema/user-transaction 集成使用。

adapter 禁止独立提交、隐式提交 DDL、提前发布 hook 或启动外部任务。不能把普通
SQL 脚本直接循环执行后声称它满足这些约束。后台任务启动尚未接入，不包含在目前
的“提交成功”驱动测试中。

## 首个具体 schema adapter：一组 SQL routines

`ObPLDDLService::install_routines_extension` 接收 1–4096 个经过正常 resolver/definer
检查的 `ObCreateRoutineArg`，或绑定主机会话的顺序脚本回调与空参数数组；同时接收
真实会话权限和启用角色，以及数据库 Extension 身份。两种输入互斥。
原单 routine 入口委托同一批量实现，不另保留单对象事务路径。它是
核心 C++ 桥接入口，不接收第三方插件传来的会话凭据，也不自行解释 SQL 命令。

- 预检限定当前 seekdb tenant 1、纯 SQL routine、新建而非替换/忽略冲突；核对 owner、
  database 与回收站状态，按普通 CREATE ROUTINE 的权限规则检查调用者和角色。
- 所有输入在首次 schema 写入前完成预检，检查空参数、每个对象的权限、并行 DDL
  冲突、已发布对象重名，以及本批次内部重名。内部重名沿用 `ObSchemaNameComparator`
  的比较规则，不用字节比较或 ASCII lower-case；FUNCTION/PROCEDURE 保持不同命名
  空间。顺序脚本使用 Root 持有的私有 schema overlay 检查前面已暂存的对象。
- Query 先预检完整脚本支持集合，再释放旧 schema guard。Root 在协调器已开始的
  事务内逐条解析、检查权限和 schema version，预留最终 routine ID/version（含参数
  身份）并暂存，之后才解析下一条。临时 EXECUTE/ALTER 权限由 Root 按
  automatic_sp_privileges 显式记录；只拥有临时 schema 或 owner 相同不构成授权。
  Query 回调拥有每条参数快照到整个命令结束，失败不继续解析后续语句。
- 使用非并行 `ObDDLSQLTransaction`；Rust 安装驱动通过虚函数调用带 schema version
  的 start 和 DDL end，保留 schema 顺序锁、epoch 校验、DDL 标记及 schema watermark。
  **只把 schema 记录写进普通 MySQL catalog 事务是不够的。**
- `create_routine` 新增外部事务模式，沿用已有 ID/version 分配、routine/参数写入、
  依赖、错误信息与 automatic_sp_privileges 授权；此模式不自行开始、提交或发布。
  安装路径消费上述预留 token，不重新分配身份；所有语句准入后才开始写入 routine
  schema。原有普通 CREATE ROUTINE 调用仍保持自行开始/结束事务和发布的行为。
- 实际 routine ID 以 `ROUTINE_SCHEMA` 成员类型交给安装协调器，同一事务写 Extension
  身份与全部成员归属。按输入顺序创建，成员 vector 在写入前 reserve；任何一个
  routine 创建失败都停止后续创建，统一由 Rust 驱动请求整个 DDL 事务回滚，而不是
  为每个 routine 新开事务。记录失败、重复安装等错误也走同一个回滚路径。
- 提交成功后调用正常 `publish_schema()`。返回值分为安装结果和 `publication_status`；
  后者失败不抹掉已提交的 extension ID，也不把安装变成可重试错误。后续 SQL executor
  需要呈现“已提交、发布待恢复”的状态，而不能重新执行安装。

此 adapter 目前覆盖一组 SQL routines；已连接文件 resolver 的核心安装方法与用户
SQL 执行端，也已接入逐条 CREATE 的 provisional 名称解析。尚不覆盖 native
函数、其他类型 DDL、shell/前向声明及完整循环引用协议。
已新增 [普通 routine resolver 桥接](plugin-extension-package.md)，它会从语法树调用
真实 resolver 和统一权限检查，保留每条语句独立的上下文与参数数组；新增 Query
Root command 在 Root 内串行调用本 adapter，尚未验证真实会话的完整成功路径。
即使文件已解析成语法树，也不能推断各函数已完成权限或互相引用的语义解析。
正常 resolver/definer 检查不可由
adapter 的 owner 检查替代。routine 成员的单独 DROP 已接入下述保护，纯 SQL
routine 的整包移除也已接线；其他对象类别与完整 Extension 语义仍待
统一 object manager 接入，不能把当前支持集合描述为完整 PG 安装能力。
上述是真实内核调用路径的接线，不是数据库安装/回滚/发布行为已经通过回归的声明。

### 核心安装 capability 与所有权

`extension_install.h` 的 `IExtensionCatalogInstaller` 仅暴露安装能力；具体
`ObPluginCatalog` 实现它，Observer 把同一个实例注入 LocalManagementService。
使用同一控制块的 shared_ptr 保持 catalog 存活，Root 调用持有该引用直至结束。
撤销注入后新调用无法再取得 capability；已有调用、SQL proxy 和服务析构仍依赖
正常停机排空，不能依靠引用计数替代。Root 方法已内部持有串行锁，禁止重复加锁。

## 更新：Rust 事务协调与 catalog capability

新增核心 `ExtensionUpdateRequest`、`IExtensionSchemaUpdater` 和
`IExtensionCatalogUpdater`。`ObPluginCatalog::update_extension` 实现后者；这些是
host 内部接口，不是公开插件 SDK 的系统表写入能力。已有下述 routine schema
adapter、Root/Observer capability 接线、已安装版本读取及查询结果与脚本的拥有式
计划绑定；更新语义 resolver 和 `ALTER EXTENSION UPDATE` 用户命令仍未完成。

请求必须包含 tenant/database/name、预期安装 ID、原版本和目标版本。与 DROP 的
按名称查找不同，更新不能省略预期 ID：包源可能在锁定之前选择过版本路径，期间
同名包可能被重建或被别的更新推进。锁定安装行及完整成员集后，严格比较 ID 与
原版本，再交给 adapter 做 owner、权限、对象与依赖准入，之后才解除成员保护。

Rust 更新入口复用 DROP 的锁定实例事务驱动：

1. PREFLIGHT：验证身份/版本请求及 adapter 的支持范围，不写入。
2. BEGIN：开始调用者提供的未启动 DDL 事务；不加入已有用户事务。
3. LOCK_SNAPSHOT：锁定完整快照、核对原版本与固定 ID，再调用 adapter 准入。
4. DETACH：仅在同一事务内解除这份快照的成员归属，要求删除行数精确匹配。
5. APPLY：schema updater 在同一事务中执行对象变更，返回完整的最终成员集。
6. RECORD：校验全部成员，写回归属；按安装 ID、owner、原版本和模块身份条件
   更新版本，要求恰好一行。不分配新安装 ID，不改变 owner、模块或创建时间。
7. COMMIT：提交全部更改；此前失败统一回滚。未知提交不能自动回滚或重试。

schema updater 返回的不是增量列表：未修改成员也必须保留；成员删除/替换与依赖
检查由具体 adapter 负责。catalog 只能验证身份、数量、重复和成员唯一约束，不能
靠一组 ID 证明真实 schema 变更已正确完成。adapter 不得自行修改 Extension 元数据、
开始/结束事务、提前发布 schema/hook 或启动外部任务。metadata RECORD 是私有方法。

原版本等于目标版本时，仍执行 BEGIN、锁定/准入与 COMMIT，但跳过 DETACH/APPLY/
RECORD。空 SQL 更新边若版本不同则不跳过：即使没有对象变化，也需要写回成员与
版本。no-op 与空更新不混淆，也不能绕过安装身份、当前版本及权限检查。

仅确认提交后返回非零 extension ID；`changed=false` 表示已确认的同版本 no-op，
`changed=true` 表示版本更新提交。未知结果的两个输出均保持初始值，返回
`OB_TRANS_UNKNOWN` 并保留错误诊断；调用方需按 ID、原版本和目标版本核验结果。
schema 发布仍由后续 schema/Root adapter 在提交后完成，发布失败不能撤销已提交状态。

验证：Rust 新增 7 项更新测试并回归共用 DROP 驱动，覆盖固定身份、旧版本拒绝、
每阶段失败、no-op、空更新和未知结果。独立 C++→Rust 测试验证阶段 ABI 与事务模型。
最新完整生产构建、77 项 Rust 单测、严格 Clippy、16 项独立 CTest 和真实内核回归
通过；内核新增用例仅验证未初始化 catalog 不调用 updater、清空输出。实际 catalog
SQL、schema 成员增删改、权限/依赖、事务回滚及恢复尚未由真实数据库验证。

### 有序 routine 更新参数与外部 ALTER 事务

新增 core-only `ExtensionRoutineUpdateOperation`，显式区分 CREATE、DROP、ALTER，
使用普通 `ObCreateRoutineArg`（包括 ALTER）/`ObDropRoutineArg`，没有另造一套
删减过的 DDL 参数协议。它是借用视图，不是插件权限凭据。需要保持完整脚本顺序：
`DROP f; CREATE f` 与 `CREATE f; DROP f` 不能去重或整理为按名称索引的最终 map。

`ExtensionRoutineUpdateBatch` 使用现有完整 DDL wire codec 深复制所有参数，
保留基类 schema-version fence、依赖、编译诊断、IF EXISTS 和 DDL 文本；每个
已解码对象的 wire 后备存储与它一同存活。支持空更新、自引用重新赋值；任意失败
清空整个结果，不暴露半份计划。操作上限为 4096，合计 wire 字节上限为 64 MiB，
这是 wire 后备存储的独立配额，不代表含解码对象和自引用复制峰值在内的总堆内存
上限，也不替代包源 SQL 的 4 MiB 上限。现有安装 batch
仍要求非空。此层只校验命令形状和持有数据，不将“拷贝成功”解释为权限/语义通过。

普通 `ObPLDDLService::alter_routine` 新增外部 DDL 事务模式，与已有 CREATE/DROP
内部接口一致：要求事务已开始且非 parallel，自身不 start/end/publish；MySQL
ALTER 转 routine replacement 的分支也传递相同事务。未传事务时保留原独立命令
行为。它仍按已发布 schema 解析对象，不支持以旧 guard 解析本次更新刚创建的对象。

这些接口现已被下面的具体 update adapter 使用，**没有把 UPDATE 缩减为只能增加 routine**。
Root 命令已接线；仍须将 SQL 语义解析连接到按顺序可见的 provisional 对象集合，
再接 SQL `ALTER EXTENSION UPDATE`。
新增 kernel 回归覆盖真实 wire codec、顺序、资源所有权/限制与未启动事务拒绝；
生产构建和回归结果记录在 [实施进度](plugin-implementation-status.md)，不是实际 schema
更新、成功事务参与或 rollback 的数据库证明。

### 具体 routine schema updater

`ObPLDDLService::update_routines_extension` 接收核心已解析、拥有完整参数的有序
CREATE/DROP/ALTER 列表，取得新的 schema guard/version，将一个未开始的非并行
DDL 事务交给已有 C++ catalog / Rust 更新协调器。调用者必须持有 Root DDL 串行化，
并提供经过普通 resolver/definer 检查的参数及真实会话权限；不是 native 插件入口。

锁定安装快照后，adapter 校验 owner/SUPER、库状态/只读、普通 CREATE/ALTER ROUTINE
权限、解析时 schema fence 与支持的对象类别。所有原成员先加入名称视图，使用
内核 `ObSchemaNameComparator`，FUNCTION/PROCEDURE 分属不同名称空间。视图保留
删除 tombstone，后续不会从旧 guard 重新装回同名对象；CREATE、DROP、单次已发布
routine 的 MySQL ALTER 按脚本顺序生成实际 DDL 步骤。CREATE 的结果成为成员，
未改动成员保留；修改普通外部 routine 不自动收编，删除其他 Extension 的成员
在 detach 前拒绝。最后返回完整存活成员集合，而不是只有新增项。

对原对象删除，锁定查询已记录的 typed incoming dependency：计划中一并删除的
dependent 可通过，存活 dependent 会拒绝；新 routine 的已解析依赖也不能仍指向
本次删除的原 ID。同名重建不是旧依赖身份的自动重绑定。此检查尚未支持“通过前序
重新定义消除依赖”的完整图演化，且不替代暂未实现的 provisional resolver。

执行复用普通 routine DDL 的外部事务模式，schema、依赖、自动授权和成员/版本写入
共享同一事务，不提前发布。确认提交后仅有版本变化时发布 schema；no-op 不额外
发布，发布失败保留已提交安装 ID，不转为可重试错误。

复用 DDL 时修正了一处必要的事务可见性：外部事务模式的自动 routine grant/revoke
选择从同一事务读取并锁定 `__all_routine_privilege`，而不是使用未包含前序写入的
schema cache。保留正常 privilege SQL service 的历史记录与 schema operation 写入；
空结果表示无权限，读取/关闭失败、未知权限位及多个大小写候选为错误，不静默合并。
普通独立 grant/revoke 的默认读取路径不变。准入视图同时识别本事务 CREATE 按
`automatic_sp_privileges` 获得的 owner ALTER 权限，不能把任意缺少权限当作可忽略。

当前仍拒绝新建/已 ALTER 对象的再次 ALTER，以及尚未解析的对象 ID 重绑定：完整
routine RPC 是解析后的 schema，不包含足以安全重放的原始 ALTER 子句 mask；直接
复用旧 guard 生成的整对象会覆盖前一步属性。接入事务感知 SQL resolver 后必须
解除这个限制，而不是把它作为最终自由度设计。其他对象类别、native-backed 包、
CREATE IF NOT EXISTS/OR REPLACE、完整 CASCADE 和更新用户命令仍待完成。

验证边界：生产编译与新增 kernel 准入/故障传输检查不证明 adapter 的成功 SQL/权限/依赖、
同名重建、完整成员集、回滚和恢复。本轮结果见实施进度；这些场景仍需真实数据库
或能执行实际 schema SQL 的集成测试，不能由事务阶段模型替代。

### 已安装版本观察与更新计划起点

`IExtensionCatalogUpdater::read_update_source` 返回 `ExtensionVersionSnapshot`，只包含
安装 ID、owner、SQL 版本与可选 native module 逻辑 ID；它不是成员集合快照，也不
授权执行更新。单条无 `FOR UPDATE` 的 SELECT 从 `__all_extension_instance` 读取
这些字段，不启动/提交事务、不读取成员或 package 文件，不验证/装载 native provider。
catalog 绑定的 SQL client 若正处在事务中则拒绝，避免把未提交安装作为更新起点。

该读取与已有 DROP/UPDATE 锁定路径共用实例字段解码和 Rust 身份/文本校验。
锁定路径仍必须有事务、使用 `FOR UPDATE`、比对 expected ID，并继续锁完整成员；
没有因为新增规划查询而改为无锁修改。ID/owner 必须为正，版本非空，字符串有界且
为合法 UTF-8、无控制字符；版本仍是不透明 catalog 标签，不在这里强加文件名或
semver 规则。空结果为不存在，重复行/损坏字段为错误，读取/关闭错误透传；失败
清空输出，资源不足不误报为 catalog 数据损坏。返回的字符串独立持有，不借用行缓冲区。

`ObIRootCommandService::read_extension_update_source` 复用现有 updater capability，
在内部 Root 串行化边界核验真实会话、所选数据库和 owner/SUPER。调用方不要再包
Root 锁，并应在进入此边界前释放旧 schema guard，查询后为语义解析重新取得 guard；
不能带着会阻挡其他 DDL 发布的旧 guard 等待 Root 锁。拒绝已有事务/内部或嵌套会话，
出错不暴露 staged 结果。无新增独立 catalog、持久缓存或第四个 runtime 实例。

该观察在返回后即可过期：后续计划必须将安装 ID 和版本分别绑定到
`ExtensionUpdateRequest.expected_extension_id_` 与 `from_version_`，按此版本选择
更新脚本，执行时再锁定核对。不能用目标直接安装文件代替更新，也不能遇到并发
版本变化就静默改用新起点。下述计划绑定现已接入核心规划接口，SQL 前端仍待接入。

kernel 回归使用真实 catalog/row adapter/Rust 校验与受控结果集：验证完整读取和
返回值所有权、空结果、重复行、非法身份/UTF-8/长度/NULL 字段、读取/迭代/关闭失败、
已有事务拒绝，以及没有写入/start/end。Root 覆盖未初始化拒绝和输出清空。
受控结果集并不执行 SQL，也不证明数据库快照/锁语义、权限或并发；真实库回归仍需补齐。

### 更新身份与脚本的拥有式计划

核心 `ExtensionUpdatePlan` 将一次 `ExtensionVersionSnapshot` 与 Rust 选择的更新
路径、内核语法树和 `ExtensionUpdateRequest` 保存在同一对象中。它不是公开插件
catalog 写入口，也不自行执行 SQL、开始事务或激活模块。

- `prepare` 验证真实 host 会话上下文，拒绝权限绕过、已有事务、内部/嵌套会话及
  未选数据库；先释放旧 schema guard，再通过 Root 认证入口取得安装观察。
  调用方必须为后续语义解析重新取得 guard，即使 Root 查询失败也不能沿用旧 guard。
- 按观察到的原版本调用 Rust 更新源读取器；不重放旧安装脚本，也不以目标安装
  文件替代缺失更新边。显式目标或 control 默认目标都固化到 `to_version_`。
  同版本 no-op 与由空/注释脚本组成的版本更新均可形成计划，但不能绕过执行端锁校验。
- 请求固定 tenant/database/name、安装 ID 与原版本。观察的 owner/module 字符串、
  文件正文与语法树各自拥有存储，调用方释放或修改输入不改变计划。
  当前 SQL 版本更新保留 native module 身份；control 改成另一模块会被明确拒绝，
  不能借 SQL 更新暗中更换 provider。native 替换仍需独立的迁移/装载契约。
- Root 读取期间数据库或 SQL mode 改变时拒绝继续，不重新读取或自动换版本路径。
  读取、身份验证、路径选择和解析任一步失败都会清空整个旧计划与语句前缀。
- 低层 `load` 仅绑定 host 已取得的观察，不能证明调用者的权限或观察来源。
  `ready()` 仅表示输入和语法就绪，**不等于对象语义、依赖、固定 schema、权限或
  事务准入完成**。后续 resolver 仍须处理 control 约束与 provisional 对象视图；
  执行端必须在事务锁内重新比对安装 ID/原版本，并重新检查权限。

真实 kernel 回归已执行：固定身份与输入所有权、显式/默认目标、空更新和有序
DROP/CREATE、坏更新尾部/缺失路径清空、native 身份变化拒绝、非法 ID，以及
Root-observation fixture 下的前置拒绝、读取失败透传、会话变化拒绝和无静默重读。
fixture 只替代 Root 观察返回，不证明 Root 认证或真实数据库更新；包读取和语法解析
仍调用真实 Rust/C++ 实现。完整语义 resolver、用户 ALTER UPDATE 和实库事务验证待补齐。

### 顺序解析的 routine 临时 schema 视图（接线中）

普通 routine resolver 与 PL builder 都会直接查询 `ObSchemaGetterGuard`，因此不能
仅在 Extension resolver 内增加名称 map。新增核心 `RoutineSchemaOverlay`，并在
guard 的 routine 名称/ID 查询、存在性查询及 routine schema version 查询接入覆盖层。
这一步尚未由 Extension SQL executor 启用，不代表顺序解析已经完成。

- 未触及的名称/ID 返回 `handled=false`，继续走正常 base guard；已删除对象返回
  `handled=true` 且对象为空，不能回退读出旧 schema。函数、过程和数据库分别
  构成名称空间；名称比较沿用内核 `ObSchemaNameComparator`，不自行 ASCII 小写化。
- `stage` 深拷贝整个 `ObRoutineInfo`，包括参数、正文、环境等字段。重复修改
  保留所有旧版本，此前借用的 schema 指针直到整个 overlay 销毁才失效。
  新版本只切换名称/ID 索引，不覆写旧对象；失败不发布半份索引。
- 同名替换必须先记录旧 ID 的删除，再以新的真实 ID 创建。已删除 ID 不可复用，
  旧 ID 仍返回删除标记。overlay 不分配假 ID，也不证明所传 ID 已合法预留；
  后续 host 协调必须在编译前预留真正身份，并确保写入沿用同一身份。
- guard 只允许对已初始化的 runtime guard 附加一次拥有式引用，reset 随正常
  schema 生命周期释放；不修改全局 schema manager，不替代权限判断。
  当前覆盖点查询而非运行时批量枚举，不应作为全局/通用 schema guard 使用。
- `has_routine_overlay` 已接入 PL 共享缓存入口：临时视图查询返回 miss，发布为
  no-op，不调整 definer key、不读取 session 缓存参数、不触及共享节点或统计。
  miss 要求空输出 guard；已有对象引用时返回不会被 PL 调用方吞掉的
  `OB_ERR_UNEXPECTED`，保留原引用，不返回带旧对象的伪 miss。
  匿名块、独立 routine、package 和子程序编译产物也标记不可共享缓存。
- 临时视图编译仍保留 AST/对象的依赖和当前调用的错误，但不独立写入普通
  error catalog 或依赖表。package/trigger 编译也遵循此规则，因为未修改的包
  仍可能依赖临时 routine。最终依赖必须由 Extension 的同一事务协调写入。
- SQL plan 的 text/prepared/PL get/add 入口也绕过共享缓存；底层拒绝发布，
  上层明确保持 `plan_added=false`，不吞掉非空 guard 的错误，不更新共享缓存
  访问统计或为缓存做参数化。batched 多语句退回非批处理，临时 CALL 不依赖
  未生成的快速解析 key。TEXT/PS/PL 模式的真实缓存入口回归及生产重建通过；
  仍不是完整 SQL 执行、并发命中/逐出或事务隔离验证。
- `PLPrepareCtx` 显式携带同步调用期间借用的父 guard；静态 SQL 准备使用传入的
  schema guard，动态 SQL 准备使用调用方 exec context 的 guard。在
  `ObSql::prepare_pl_sql` 获取新 runtime guard 后、解析/结果集初始化前，子
  guard 通过 `inherit_routine_overlay` 取得 overlay 的共享所有权。子 guard
  不借用父 guard 本身，不复制父级权限/锁或 base snapshot。无 overlay 时原路径
  不变；有 overlay 时校验双方状态、runtime 类型及相同 schema service，拒绝
  自继承和覆盖已附加的视图。共享的是临时对象视图，而不是新的不可变时间快照。
- SPI 结果对象在语句/游标打开时捕获一次拥有式 overlay（也固定空来源），不
  长期借用父 guard。`reset_member_for_retry` 重建执行上下文时保留这个 owner，
  每次 retry/fetch 新取 guard 后重新附加；完整 reset 则释放 owner 并允许下次
  打开重新捕获。重复捕获/重复附加被拒绝，不能静默切换为调用方后来改变的来源。
  package expression/变量/cursor/allocator 和直接 PL 子程序调用的新 guard
  同样继承调用方视图。捕获的是共享临时视图，不是冻结的语句级内容快照。
- 这不是完整上下文隔离：raw expression 的 query context、package guard 的
  复用与借用寿命还需顺序 resolver 保证；脚本跨语句保留游标时的视图演化、
  并发变更及真正 retry/fetch 的事务/权限行为仍需专门实现与实库验证。
  完整 resolver 也须处理真实 ID 预留、新对象依赖、DDL fence 和安装事务内权限。
  当前不会借此开放未经验证的 UPDATE。
- 最多保留 16384 条历史记录、累计 64 MiB schema convert-size；后者不包含全部
  map/vector/分配器开销，因此不是完整进程堆内存上限。容量或分配失败保留旧视图。

专项命令 `python3 rust/plugin-runtime/tests/kernel_script.py --build-dir build_release
--overlay-only` 已通过，使用真实 schema 类型的 wire 编码比较验证完整深拷贝，并
覆盖重复修改/删除后的借用有效性、名称/ID 删除标记、重建/冲突、跨数据库/函数过程
隔离、非 ASCII 名称、非法输入及记录/字节容量边界。该模式不调用 guard，也不
安装包或运行 SQL；不能当作 guard/PL 集成证据。guard 源文件按生产编译参数的
独立语法检查已通过。

guard 对象布局变化后，已在独立的 `build_plugin_overlay_verify` 完成全部生产对象
重构建与最终链接；同目录完整 kernel 回归通过。新增受控内存 schema manager
fixture 实际调用真实 guard 的点查询，验证 overlay 覆盖/删除不回退、未覆盖键
回退、独立 guard 保持旧版本、同名重建新 ID、错误附加拒绝和 reset 引用释放。
fixture 不初始化真实 SQL/schema 服务，不能证明生产 guard 获取、权限或事务行为。

PL 隔离接入后同目录生产目标重新构建/链接、插件边界检查及完整 kernel 复跑通过。
新增用例在真实 guard 上调用 PRCR/SFC/ANON/PKG/CALLSTMT 五类 PL 缓存入口，
验证空结果、无发布/引用变化、key 与对象统计不变、参数拒绝以及普通 guard 回退。
fixture 使用未初始化 cache 和独立分配对象来验证入口不触及缓存服务；不模拟正常
并发命中/逐出，也尚未覆盖非空输出 guard 的实际所有权拒绝路径。编译元数据写入
抑制通过生产编译，但不以此宣称真实 PL 编译、数据库回滚或事务可见性已验证。

嵌套准备继承接入后，`build_plugin_overlay_verify` 重新完成相关生产对象编译与
最终链接，插件边界及完整 kernel 回归通过。新增受控 guard 用例验证父 reset
后子查询仍有效、多级继承、删除/新 ID/版本和未覆盖对象回退、错误来源拒绝、
最后一个引用释放；缓存入口回归也改用继承后的子 guard。测试未启动实际 SQL
服务，不能把这些结果当作完整 prepare/execute、游标重试或事务行为的证明。

SPI 执行传播接入后的生产重构建/最终链接和 kernel 回归也通过。新增用例实际
构造 SPI result、三次重置重试成员并刷新/恢复 guard，最后完整 reset；验证父
guard 已释放时仍能查新对象/版本及删除标记，最后 owner 释放，普通空来源和
重复附加语义。ResultSet 的访问保护区通过计数 fixture 验证四次进入/退出匹配；
不是实际 server epoch、cache lookup 或游标执行模型。第一轮禁止保护区调用的
fixture 曾失败，修正为匹配真实构造/析构契约后通过；没有修改生产保护行为。

完整 kernel 使用 `python3 rust/plugin-runtime/tests/kernel_script.py --build-dir
build_plugin_overlay_verify`。runner 会拒绝缺失二进制或二进制早于已知 guard/PL 输入
的目录；已验证旧 `build_release` 被拒绝。时间检查仅排除已知陈旧布局，不能替代
完整依赖构建或构建进程终止证据。不得将新 guard 头文件与旧生产对象混合链接。
`--overlay-only` 仍只验证独立拥有式值类型，不包含 guard 集成。既有 UPDATE 的
新对象/重复 ALTER 限制仍需完整顺序 resolver 接入后解除。

### Root 更新命令与 Observer 生命周期

`ObIRootCommandService::update_extension_routines` 现由 `ObLocalManagementService`
实现。它接收完整有序参数和真实 `ObSQLSessionInfo`，先清空安装 ID、changed、
publication status 与诊断；拒绝服务未初始化、缺少 updater、已有用户事务、内部/
嵌套会话、错误 tenant/database 或缺少预期安装 ID。不会通过空操作列表或同版本
请求绕过后续 catalog 锁定/版本校验。

命令内部调用唯一的 `serialize_root_service_call`，在锁内检查 DDL 准入并重新
提取会话权限和启用角色，再进入 routine schema updater。调用者不得重复包一层
Root 串行锁，必须在入口前释放旧 schema guard，并保证已解析参数在同步调用内
存活。这个接口还不是 SQL parser/resolver，也不从文件名推断安装版本。

Observer 从同一个 `ObPluginCatalog` 分别提供 installer/dropper/updater 三类
capability。启动时注入，停止时在请求排空的生命周期边界撤销；每次 Root 命令
持有本地 shared ownership，直到更新事务与 schema 发布结束。撤销 capability
本身不等于取消或排空已准入请求，catalog 内部借用的 SQL client 仍须比这些请求
活得更久。没有新 registry、第二份 catalog 或额外 Rust runtime。

已提交 ID 与 changed 会穿过 Root 原样返回，publication failure 单独报告；异常
不能把已知提交变成可重试失败。未知提交仍无成功 ID，不能自动重试。禁用实验宏
时 runtime 的 updater getter 与其他 capability 一样返回空。

新增 kernel 测试覆盖真实 Query/Root 虚接口的未初始化拒绝/输出清空、三类接口
共用控制块与引用撤销，以及真实 Observer/catalog 的失败初始化、成功绑定、重复
初始化和销毁。初始化并不执行 schema bootstrap/恢复，测试传输未收到 SQL 请求；
所以这些证据不证明已初始化服务上的权限成功/拒绝、串行并发更新或数据库提交。

## 移除与 RESTRICT

### Rust 整包移除协调与 routine schema adapter

`ObPluginCatalog::drop_extension` 实现独立的 `IExtensionCatalogDropper`，不把整个
catalog/loader 暴露给插件。它要求调用方提供未开始的 DDL 事务及新取得的 schema
version，不会退回普通 autocommit SQL，也不会结束调用者已有事务。Observer 已把
同一 catalog 的此 capability 注入 Root，SQL `DROP EXTENSION` 通过该入口调用。

### SQL 与 Root 入口

新增 `DROP EXTENSION name [RESTRICT|CASCADE]`，未指定行为时使用 RESTRICT。
CASCADE 可解析，但当前 routine adapter 在写入前明确返回不支持。IF EXISTS、多包
一次删除、非 routine adapter 尚未实现；native-backed routine 支持见本文前节。
这不是完整 PG DROP 语义。

`DropExtensionResolver` 保存独立的名称/数据库字符串，拒绝 prepared、已有事务和
权限绕过。语句是写 DDL，但 parser 分类和 statement 均不触发隐式提交。
`DropExtensionExecutor` 再次检查真实 session、目标数据库、密码状态及顶层执行，
释放旧 schema guard 后调用 `ObIRootCommandService::drop_extension_routines`。

Root 内部串行化、取得真实会话权限/角色后刷新 schema，再进入以下 Rust/catalog
流程。不能在调用方重复持有同一 Root 串行锁。前端权限提取只验证请求/身份，
owner/SUPER 与逐成员 ALTER ROUTINE 在锁定快照后检查，不额外要求数据库级 DROP，
也不能把前端空权限列表误解为允许任意用户删除。只读与依赖检查同样留在 adapter。

移除命令不要求包源目录，也不读取安装文件或装载代码。确认提交才返回 affected
rows=1；后续 schema 发布失败返回警告，避免误导重试。未知提交不返回成功身份。
安装与删除 capability 使用同一个 catalog 控制块；服务停机撤销二者前仍须排空请求。

### 协调阶段与 schema 适配

Rust `extension_drop.rs` 驱动以下顺序：

1. PREFLIGHT：检查请求身份和 schema adapter 的准入范围，不写入。
2. BEGIN：开始本次移除独占的 DDL 事务。
3. LOCK_SNAPSHOT：先锁定安装行、再读取并锁定完整成员集；核对可选的预期安装
   ID，校验持久身份/成员，调用 adapter 的权限、对象存在性与外部依赖检查。
4. DETACH：只在当前事务内解除这份快照的成员归属，删除行数须精确匹配快照。
5. APPLY：通过普通 schema 删除接口移除实际对象，复用同一个活动 DDL 事务。
6. RECORD：带安装 ID/owner 条件删除安装元数据，要求精确删除一条安装记录。
7. COMMIT：提交全部更改；此前任一步失败统一回滚，恢复成员保护和 schema 状态。

只有锁快照阶段可以设置身份，其后阶段不得修改它。提交结果未知时不做自动
rollback/retry；回滚失败保留原错误、回滚错误及已锁定 ID，需按数据库/名称/ID
重新核验。C++ 只在已提交时返回非零 dropped_extension_id；schema 发布是后续
独立状态，发布失败不能把已提交删除报告成可重试失败。adapter 若意外结束事务，
后续空 rollback 不会被当作成功撤销。以上是实现契约，不是已验证的数据库事务结论。

`ObPLDDLService::drop_routines_extension` 是当前具体适配器：

- 调用者必须持有正常 Root DDL 串行化权限，并传入真实 session 的权限和启用角色；
  适配器取得新 schema guard，使用非并行 `ObDDLSQLTransaction`。
- 当前支持 tenant 1、SQL FUNCTION/PROCEDURE 成员和 RESTRICT，可关联 native module。安装 owner 或
  SUPER 才能申请；每个 routine 继续通过普通 ALTER ROUTINE 权限检查。检查目标库、
  回收站与只读状态，包括成员为空的安装。缺失/错库/不支持的对象不会被默默跳过。
- 从 `__all_dependency` 锁定读取匹配实际 routine ID 和 object type 的传入依赖。
  同包成员间引用不阻挡整包删除，包外依赖返回拒绝；全部检查在解除成员保护之前。
  Extension 显式依赖边另由 catalog 检查；CASCADE 尚未实现，不能把该检查称为完整 PG 依赖语义。
- `drop_routine` 的外部事务模式复用正常对象、参数、历史、依赖状态和自动授权清理，
  不自行开始、结束事务或发布 schema；普通 DROP routine 的原事务模式保留。
- 无需读取 control/SQL 文件或加载原生模块。这个 adapter 不支持其他对象类别；
  不应据此承诺任意损坏 native Extension 都可由它清理。

catalog 新增 checked row reader，损坏/NULL/转换失败不能降格为空模块、零 ID 或
无依赖；query 结果关闭失败也向上传递。锁定与 detach 帮助方法是 catalog 私有接口，
不能作为绕过 schema 检查的独立元数据删除入口；普通成员删除保护仍然生效。

验证分层：Rust 单测和 C++→Rust 事务模型检查阶段顺序、各阶段失败、未知提交、
回滚失败及身份保持；它们不执行真实 SQL。完整内核测试另检查未初始化 catalog
不调用 adapter、输出清空及 checked reader 的错误行为。新增 DROP grammar、
命令分类、深复制与 Root capability 所有权用例已在最新完整生产链接后执行通过；
实验宏关闭路径通过语法检查。正常授权删除、跨会话
可见性、DDL 回滚/发布、并发与真实依赖 RESTRICT 仍须通过数据库回归。

`ObDDLSqlService::check_extension_member_drop` 属于常规 schema 层，不依赖可选
plugin runtime 或其编译开关。目前 `ObRoutineSqlService::drop_routine` 在删除
routine/参数/历史之前调用它，覆盖普通 DROP FUNCTION/PROCEDURE 及复用此底层
接口的删除路径。检查要求传入已开始的同一 MySQL/DDL 事务，并按 tenant 1、
database、schema class、真实 object ID 锁定读取 `__all_extension_member`：

- 无成员记录才允许继续；存在记录返回 `OB_OP_NOT_ALLOW`，日志包含所属 Extension ID。
- 缺表、读失败、损坏的 owner ID、结果关闭错误都不能被当成“无依赖”而继续删除。
- 不额外禁止保留 object ID 的 routine 替换。安装和删除仍须共享 schema 的 DDL
  顺序锁；`SELECT FOR UPDATE` 的空结果本身不能证明阻止了并发成员插入。
- 数据库整体删除通过下述专用路径，在同一事务中先移除该数据库的归属记录，
  再按正常 DDL 路径删除 routine；不绕过其他数据库成员的保护。

这个检查是正常 schema 不变量，关闭插件运行时也不绕过它。因此新系统表必须先
准备好，旧数据库缺表会明确失败；本轮尚未验证数据库升级或 bootstrap 回归。

### 整库删除、回收站与模块共享

`ObDDLOperator::drop_database` 在清理任何数据库成员之前调用常规 schema 服务
`delete_extensions_before_database_drop`。后者要求活动事务和合法 database ID，
只删除 `tenant_id=1 AND database_id=<目标 ID>` 的安装/成员/Extension 依赖行。先删除 instance
再删除 member 和 dependency，保持 instance→member→dependency 的锁顺序；不获取 native provider 行锁或 runtime
mutex，也不提交事务。后续任一 DDL 失败，调用者必须回滚整个事务，恢复归属记录。
成员缺失、空安装以及已损坏的 native 模块都不要求装载模块后才能整库清理。

这与单独删除 schema 对象不同：只有已通过正常权限检查、持有数据库/DDL 锁并
承担整个数据库删除的内核路径能调用该接口。它不是公开插件 API，也不是任意
对象解除归属的开关。缺表/SQL 写入错误向上传递，不能忽略。

进入回收站的路径没有调用此清理，database ID 和 Extension 归属继续保留；永久
PURGE 经已有 `purge_database_in_recyclebin → drop_database` 路径清理。数据库
名字改变不重写安装身份。没有触发 module unload，其他数据库可继续持有同一个
模块。跨库依赖仍由正常 DDL 规则处理，完整 Extension 依赖/CASCADE 管理尚未完成。
上述路径已接线，真实 DROP/PURGE/恢复、失败回滚及并发 RESTRICT 尚未执行回归。

`record_extension_drop` 同样只管理归属元数据，要求协调器先在同一事务中完成
成员对象删除或重新归属，再按 tenant/database/安装 ID/预期 owner 锁定并移除
成员和安装记录。它不实现 `DROP EXTENSION CASCADE`，也不自行删除 schema 对象。
它不要求原生模块可用，以免模块缺失时无法清理已坏的安装。

原有模块停用/卸载的 RESTRICT blocker 查询已接入新安装表。安装原生引用和
RESTRICT 使用同一 provider 行作为并发边界；RESTRICT 在该保护下使用锁定当前
读取。移除安装引用不等待 provider 行，避免 instance→provider 反向加锁。
数据库级安装引用不会随模块 generation 切换被视为过期。
RESTRICT 先读取/锁定数据库安装记录，再读取对象依赖，以匹配整库清理的先后
顺序，避免 teardown 持有 instance 等待 dependency、RESTRICT 反向等待的环。
返回 blocker 的展示仍保持对象依赖在前、数据库安装在后，与锁顺序分开。

## 尚未实现与验证的部分

### 原生 SQL 包的可选实库验收

`native_math` 参考包提供 1.0 安装、1.0 → 1.1 与 1.1 → 1.2 升级脚本。
后一次升级在同一脚本 ALTER 一个成员，再 DROP／同名重建另一个成员；
重建函数的默认调用结果从 42 改为 100，用于检测旧预处理计划残留。
默认安装版本保持 1.0，不改写已交付的旧版本 SQL。

在**可丢弃、使用当前 schema 的测试实例**上，先由管理员交付完整 SQL 包并
配置 `--extension-dir`，加载当前 `org.seekdb.sql_extension` 参考模块，再运行：

```bash
python3 rust/plugin-runtime/tests/native_extension_server.py \
  --port 2881 --user root --confirm-disposable-server
```

可以用 `--unix-socket /绝对路径/run/sql.sock` 替代 `--port 2881`。
密码通过 `SEEKDB_TEST_PASSWORD` 环境变量提供，不放进命令参数。
脚本验证安装、两次升级、卸载、数据库隔离、成员／依赖／ACL 清理和预处理
重新绑定；成功仅清理本次随机创建的库与用户，失败保留，不重试未知提交结果。
**当前沙箱不能绑定 SQL 监听地址，实库流程仍未通过验收**；离线脚本自测、
真实包读取／解析和受控事务回归不能替代真实提交、并发和恢复验证。

### 其余差距

- CREATE/ALTER UPDATE/DROP 的基本语法/statement/resolver/executor 已接线，仍缺真实
  数据库端到端验证、更多语法选项、routine 之外的多对象 DDL adapter 和用户事务接入。
- control/安装 SQL 源读取和解析/安装入口已接线；新安装可读取版本脚本路径，
  已有实例的 UPDATE 已接线，完整依赖验证仍待扩充。纯 SQL 样例可交付为文件，但实库行为未验证。
- routine 之外的 schema 成员创建/删除和单独 DROP 保护、完整 Extension 级联移除、
  跨库依赖及数据库 DROP/PURGE/恢复回归、依赖/权限与失效通知。
- 查询 resolver 的 tenant/database 上下文、数据库级名称查找及执行 binding。
- 跨会话安装失败不可见、重复安装竞争、成员冲突、RESTRICT 并发与恢复回归。
- 已有数据库补建新系统表的升级验证。生成代码只提供 schema 定义，不是升级
  成功的证据；新版本 RESTRICT 查询需要这些表，不能在缺表时忽略依赖检查。

当前证据包括 Rust 单测、C++→Rust ABI 调用、生成表结构和生产 C++ 语法检查。
安装驱动的 C++→Rust 测试使用明确的事务模型，覆盖部分 apply/record 写入后失败、
提交已持久化但回包失败、清理失败；这些不是数据库事务服务的回滚证据。
Standalone loader 的 catalog 仍是测试替身，没有执行以上真实数据库事务用例。
本地 TCP/Unix socket 监听受沙箱限制，真实服务测试尚未运行。

构建清单曾漏掉新增系统表及 LOB 辅表所在的三个分片，导致完整目标最终缺少
6 个 schema creator 符号。已补齐 CMake/Bazel 共用清单，并在 CMake 配置阶段
增加生成文件与编译清单的精确集合检查。新 shard 已编入真实生产对象；7 项
清单测试含真实表定义重新生成校验。最新完整目标仍待最终链接结果，不将文件
生成成功或对象编译成功当作完整服务可用的证明。
