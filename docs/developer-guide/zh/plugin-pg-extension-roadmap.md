# SeekDB：以 PG SQL Extension 为正式扩展模型

状态：包源、原生声明解析、按实现 ID 绑定、routine 持久字段和原生 UDF 执行桥已实现；
原生 CREATE 的权限与签名准入、SQL 包逐条解析、catalog 名称解析与执行、
PL 调用者的依赖分析已验证；实库创建、PL 字节码执行及安装闭环未完成。
**GIS 已切换为实验性 SQL 包交付，公共函数不再由模块全局发布；实库安装闭环仍未验证**。
不能把本文件中的目标 SQL 当作已验证的生产操作步骤。

**最新闭环检查：GIS 不仅缺实库验收，算法迁移本身也未完成。**
首批已复用现有 Boost.Geometry 算法，修复此前 4 项拓扑／距离语义错误；
二维面积、长度、质心和 buffer 也已复用；clipping、SRS 与一般集合分派等仍未完成迁移。
下文的“代码及交付已切换”只表示调用／注册边界迁移，不代表 GIS 语义等价或可发布。
详见“GIS 完整算法后端缺口”。

最新 GIS 进展见“C. GIS 切换”：106 条声明已交付，真实 DSO 的公共函数全部为
implementation-only；类型／cast 支撑注册仍属模块。下文保留历史验证记录。

最新权限进展：native CREATE 已在同一事务内写入 owner 对象 ACL，slot 0
也已改为对象权限检查；SQL GRANT／REVOKE 已接到独立的 Root 批量事务入口，
但尚无实库提交证据。同名原生重载的 Root slot 分配及持久化准入已接通，最新状态
见“原生重载持久化准入”；下文保留各阶段验证记录，不能将旧阶段限制视为最新状态。

## 设计结论

面向数据库用户的扩展采用 PG/PostGIS 的控制文件、SQL 安装/升级脚本与原生动态库
分层，而不是以加载动态库作为发布所有 SQL 名字的动作。SQL 是声明与组合对象的
入口；C++、Rust 是实现算法和回调的语言。纯 SQL 包不要求动态库。

参考 [PG Extension](https://www.postgresql.org/docs/18/extend-extensions.html) 和
[PostGIS 安装](https://postgis.net/docs/postgis_installation.html)。对齐的是对象、安装、
调用和依赖模型，不是承诺加载现成 PostGIS 二进制或运行全部 PG SQL。
SeekDB 的 MySQL 类型/命名空间、存储事务和稳定 C ABI 仍须有明确适配。

## 对齐目标与当前差距

| 维度 | 目标 | 当前证据与差距 |
| --- | --- | --- |
| 包交付 | 平铺 `.control`、`name--version.sql`、更新脚本 | 本阶段修改读取器与 CMake 安装；兼容旧子目录，拒绝同名歧义 |
| 原生实现 | 库提供实现入口，SQL 声明绑定入口 | 独立 AST、精确实现绑定、routine 持久字段、CREATE 准入及 UDF 标量/批量执行桥已接通；实库安装链路尚未闭环 |
| 数据库作用域 | 每个数据库独立安装扩展及其 SQL 对象 | GIS 公共函数已改为数据库 routine；受控 catalog 的 SQL 隔离回归不代替实库安装提交 |
| 对象身份 | 稳定 catalog ID、类型签名、owner、权限、依赖 | 不能用运行时注册 ID 或动态库地址替代持久身份 |
| CREATE/UPDATE/DROP | 一致的事务、成员与依赖处理 | 已有 routine 专用接线；不等于通用对象安装或 PG 事务语义 |
| SQL 自由度 | 支持内核已实现、可事务协调的 DDL/DML，而非函数白名单 | 当前执行器主要接纳 routine，需扩展对象适配器与安全的脚本执行上下文 |
| 扩展种类 | 函数、类型、转换、操作符、聚合、表函数、索引支持、执行钩子 | 现有 SPI 能力不能直接等同于这些对象已具备完整 SQL catalog 生命周期 |
| 生命周期 | 数据库扩展卸载与共享代码卸载分离 | 保留代码租约机制，需把数据库对象依赖接到卸载检查 |
| 备份恢复 | 安装声明与配置数据可恢复，成员不重复创建 | 待实现与服务器恢复验证，不以包文件测试替代 |

## 交付、安装、调用是三个独立阶段

1. 管理员发布可信不可变的 `.so`、manifest、control 和 SQL 文件。
   包根目录不可由普通 SQL 用户写入。CMake/打包工具负责交付，不执行安装 SQL。
2. 在选定数据库执行 `CREATE EXTENSION gis`，读取包、检查依赖与权限，执行脚本，
   在同一事务中登记对象和扩展成员；成功提交后发布可见性并失效相关缓存。
3. SQL resolver 先解析数据库对象和权限，再根据其原生绑定调用通用执行器。
   模块存在但数据库未安装扩展时，不能解析到该扩展的 SQL 名字。

GIS 交付布局（`plugins/gis/sql/` 由 CMake 安装至平铺 SQL 包目录；升级脚本按需增加）：

```text
plugins/gis/
  plugin.toml
  seekdb_gis.so
share/seekdb/extension/
  gis.control
  gis--1.0.sql
  gis--1.0--1.1.sql     # 仅在确有升级内容时提供
```

保留 `plugin.toml` 管理 SeekDB 稳定 ABI、实现入口、代码依赖与能力信息；control
管理 SQL 包版本、依赖、安装位置。两者版本不必相等，不允许用 SQL UPDATE 偷换
不兼容 ABI。原生模块的加载策略由管理员控制，不能让用户 SQL 指定任意文件加载。
最终可由 CREATE EXTENSION 根据受控映射确保实现可用；不应要求普通用户手工
执行 INSTALL PLUGIN。过渡期现有已加载模块要求仍然保留。

## 原生函数直接绑定：下一阶段的关键工作

不采用 `CREATE FUNCTION ... RETURN 内部插件函数(...)` 转发来伪装原生绑定。
目标是引入独立的原生语言函数声明，类似 PG 的 `AS module, symbol LANGUAGE C`。
SeekDB 的具体语法必须由真实 parser 支持并完整往返，不能用注释、函数体字符串
前缀或私有 SQL 名称隐藏绑定元数据。

持久绑定至少包括：数据库/命名空间、函数 ID、owner、参数和结果类型 ID、
原生模块逻辑 ID、稳定实现 ID、调用 ABI 版本、NULL/易变性等执行属性。
SQL 函数名和实现 ID 分离：多个 SQL 名字可以共享同一实现，库升级不能改变
已安装 SQL 对象的身份。SQL 签名与实现签名必须在安装及重新绑定时检查。

解析入口必须统一覆盖普通调用、限定数据库名、PL、预处理语句和 POINT 等特殊
语法节点，不能仅给 `ST_Area` 加分支。名称解析记录 routine/类型/扩展依赖，
计划缓存绑定稳定身份与版本；执行时持有对象及代码租约。对象删除、SQL 包更新、
模块重新激活都必须触发相应失效，不能让旧计划按名字调用到另一个实现。

最先用一个标量函数验证完整闭环，再迁移 GIS 全量声明：参数个数、默认/可变参数、
别名、几何类型、NULL 和精确数值转换均须保持测试覆盖。转换不能通过统一 TEXT
包装丢失类型，也不能因通用调用破坏向量化/批量执行或引入不必要的逐行复制。

## 安装事务、权限与对象管理

- 安装脚本以明确的安装身份执行；不沿用内部连接的隐式权限绕过。
  `trusted`、owner 替换与权限提升未完成前继续明确拒绝相关选项。
- CREATE、UPDATE 的对象变更、成员记录、依赖、版本推进须具有同一提交结果。
  普通 DDL 的隐式提交不能穿透脚本事务。失败不得留下半个扩展或已发布对象。
- PG 能在调用者事务内安装扩展；现有 SeekDB 安装拒绝活动事务。应设计显式的
  caller-transaction 参与协议后再开放，不能只删除拒绝条件。
- DROP 根据成员和依赖图删除，不依赖外部卸载脚本。RESTRICT/CASCADE 需要统一
  对象依赖遍历；单独删除扩展成员要受保护。不要通过清空依赖表绕过限制。
- 多数据库可以分别安装不同 SQL 包版本；代码版本是否共存必须显式约束。
  仍有依赖对象、运行中查询或计划租约时不能卸载共享实现。
- 表、视图、类型、转换、操作符、聚合、索引等按对象类别接入事务 schema adapter。
  安装包支持范围以实际可回滚和可恢复的对象为准，不把可解析当成可安装。
- 生成列、索引表达式需要确定性声明、持久依赖及失效策略。先验证这些约束，
  再解除现有通用插件表达式的生成列限制。

### control 安装权限策略

已接入 PG 风格 `superuser`：默认 true，false 只使用调用者权限，不提供 trusted
提权。Rust 包读取、内存 source、版本路径、C++ resolver 与 Root 安装／更新准入
传递并核对同一个策略，多段脚本取所有实际执行版本的最严格要求。
SDK 主／版本 control 生成器支持显式选项；纯 SQL 和 SQL wrapper 示例加入 false，
GIS／native_math 继续要求管理员。原生 C 声明即使处于 false 包中仍检查 SUPER。
详见 [包权限策略及默认行为变化](plugin-extension-package.md#安装权限策略)。

实库 runner 增加只有 CREATE ROUTINE 的新用户安装、更新、卸载 text_ops 的路径，
检查 extension owner 仍为该用户；这条路径尚无实库通过证据。
本轮完整 seekdb 构建、Root writer、真实 GIS DSO 和 kernel 综合回归通过；
33 项 Rust 包测试、12 项 SDK schema 测试、17 项 runner 自测、6 项相关 CTest
以及 runtime／SDK 库的 Clippy 检查通过。Root 夹具验证包策略在事务开始前拒绝，
真实 resolver 验证 false 包仍拒绝无 SUPER 的 C 声明，并拒绝 source／请求策略
不一致。以上不证明实际存储提交、并发、取消或重启恢复已经完成。

### SQL 包内 DCL 的事务接线（安装／UPDATE 已接入，待实库验收）

独立 SQL GRANT／REVOKE 已有 Root 自有事务入口，但不能由扩展脚本直接调用：
这会把授权提交与扩展安装提交分开。包内 DCL 必须使用协调器已有的同一个
`ObDDLSQLTransaction`、同一 schema/privilege overlay，以及同一最终发布结果。

安装器现在逐条 reserve/stage/write CREATE，再解析后续语句。native ACL writer
需要锁定已存在的 routine/ACL 行，因此 `CREATE f; GRANT ... ON f;` 中的 CREATE
必须先写入同一未提交事务，不能只暂存在 schema overlay。GRANT／REVOKE 借用
该事务和匹配的 schema/privilege overlay，不启动、结束或发布独立事务。
UPDATE 保留写库前的完整依赖图校验；现已接入下述两阶段 DCL 计划，不能让 DCL
在准入阶段提前改变已提交权限。

有序操作载体 `ExtensionRoutineUpdateOperation` 包含 GRANT／REVOKE
及互斥 payload；`ExtensionRoutineUpdateBatch` 通过完整 RPC codec 深拷贝目标
routine/版本、actor/角色、接收者、grant-option/CASCADE、版本栅栏及审计文本，
支持混合顺序、自引用复制和统一总字节限额。诊断仍只打印种类，不打印 SQL、
用户名、密码或参数指针。载体不是执行权限，也不触发任何 SQL。
caller-routine 写入入口仍显式拒绝非 schema 操作。安装与 UPDATE 分别处理
GRANT／REVOKE，不能将其当成 CREATE 解引用空指针。

安装脚本已接入真实 DCL resolver 和普通权限检查，再由 `NativeRoutineDclRequest`
提取请求、绑定当前 actor/启用角色、检查全部接收者，并深拷贝到有序载体。
REVOKE 先绑定 actor，再规范化接收者集合。仅支持安装数据库内已有或先前创建的
native FUNCTION 的 EXECUTE／ALTER ROUTINE 授权，支持 grant option 和原生
REVOKE 的 RESTRICT／CASCADE。不自动创建用户，拒绝密码修改（包括显式空密码）
及认证子句。包内 GRANT 的密码遮罩使用当前解析的脚本文本，不借用外层 SQL。

Root 再次校验 actor/角色、目标数据库、对象版本及权限，并调用已有 native ACL
writer。DCL 不加入 extension member；整个脚本的 schema、ACL、成员和依赖写入
由 Rust 安装协调器统一提交或回滚，只有确认提交后才发布 schema。

验证：完整 `seekdb` 构建、GIS 真实 DSO 回归、`kernel_script.py` 综合回归、
116 个 routine writer 场景、6 项相关 CTest 和 `git diff --check` 通过。
GIS fixture 覆盖混合 DDL/DCL wire 顺序、源释放、自拷贝、112 种 payload 形状、
失败清空、总字节限额和 malformed target，以及安装 preflight／逐条 resolver
的 GRANT／REVOKE 正向语义解析和非法接收者、密码修改、跨库目标的拒绝。
另外验证 CREATE→GRANT→REVOKE 顺序解析绑定先前 CREATE 的私有对象 ID/版本，
脚本释放后全部操作仍有效；该用例提供受控 schema，不执行 ACL SQL 或提交。
离线 resolver/writer 回归不等价于完整 Root 安装事务和实库提交验收；没有操作测试服务器。

后续增加了真实安装 adapter 的串联回归（`native_function_declaration.py --writer-only`）：
Root 入口与用例使用同一个 `make_routine_extension_installer`，注入受控 DDL 事务，
调用真实 routine/ACL SQL writer、catalog binder 和 Rust 协调器。28 个场景涵盖
成功安装、21 个写入位置失败、4 个解析回调失败及两种事务结束结果未知。
成功路径确认 GRANT 后可执行、REVOKE 后拒绝、两个 CREATE 产生两个成员；失败
路径没有后续写入或重放，只有一次提交/回滚选择。这不是实库隔离或实际撤销数据
证据，也未执行 Root 外层的锁获取、发布及恢复流程；输入声明和查询行由用例提供。

UPDATE 的 DCL 规划基础已增加 `NativeRoutineGrantPlan`，与既有纯
`NativeRoutineRevokePlan` 配套。它按完整目标 ACL 和已选择的授权者计算有序
before/after：重复接收者/授权者请求合并，grant option 逐权限保留，未请求组和
列级权限不受影响，请求的 no-op 组仍输出。它不做鉴权、SQL、版本分配或视图发布，
不是可直接执行的权限凭证。当前 GRANT writer 已复用它，实际逐组写入仍重新鉴权，
并核对前后状态；状态不符则返回错误，由调用者回滚 SQL，不发布私有权限前缀。

验证通过 324 组权限/option 组合、18 类非法输入、16,384 组边界及 45 个 GRANT
writer 场景（包括真实变化和意外 no-op 两类计划状态偏差），安装 adapter 的
28 个串联场景仍通过；完整构建、综合内核回归和 6 项相关 CTest 通过。
该纯计划阶段尚未接入 UPDATE：当时仍需将完整 ACL 快照、对象版本
预留和逐条私有权限计划接到准入阶段，并在执行阶段按序恢复视图、重新校验后应用。
不能将最终准入视图当作每一条中间语句的执行视图。

完整私有 ACL 投影已加入 `RoutinePrivilegeOverlay::merge_object_snapshot`：由完整
基线和已准入的私有 after-image 生成独立持有的 `(grantee, grantor, column)` 有序
快照。零权限仅删除对应授权者的函数级组，保留其他授权来源及列级组；不将权限
按用户 OR 合并后丢失委派来源。支持输入/输出为同一数组、失败清空、退休与过期
对象拒绝，并在删除/新增全部完成后检查最终 16,384 组上限。

串联纯 GRANT／REVOKE planner 的回归确认：前面的私有委派会阻止 RESTRICT，
CASCADE 生成相应下游撤权计划；应用私有 after-image 后再 GRANT 能看到已经撤权
的状态；保存点恢复原快照，已返回的旧快照保持独立。11 类非法/过期输入及边界
用例通过，既有 catalog/writer 和安装串联回归仍通过。这仍是主机内部规划基础，
没有 SQL、自动鉴权或事务提交；UPDATE 入口接线见下文。

ACL 版本预留已增加宿主内部的 `NativeRoutineAclVersionReservation`：准入阶段
分配真实版本，绑定同一 schema service、SQL service、活动事务、完整目标、actor／
roles 和有序逐组 before/after。消费是一次性的，任何匹配失败都会清空凭证；
执行阶段不重新分配版本。GRANT／REVOKE writer 可消费外部预留，也保留内部预留
路径，并继续重新鉴权、核对实际 ACL；凭证不是授权许可，也不负责提交或发布缓存。
38 个预留场景、47 个 GRANT 和 66 个 REVOKE writer 场景已通过，包括后续操作
先分配更高版本后仍消费原版本，以及外部计划不匹配时不写 SQL。

UPDATE 的原生 CREATE 也接入 owner 自动授权预留：在 routine 版本之后、下一条
脚本操作之前，预留 owner 的 EXECUTE／ALTER 对象 ACL 版本。真实 writer 检查
目标和 automatic_sp_privileges，再消费该版本；新身份的空 ACL 和 owner 检查
仍从同一事务执行。关闭自动授权的 CREATE 不预留该权限操作。这样隐式授权不再
在 UPDATE 执行阶段临时分配版本；不是把系统表写入交给插件或另开事务。

本阶段完整构建、catalog 和 writer 回归通过：新增 10 个 owner 预留专项场景，
writer 共 136 个场景（slot 0／7 各 68 个），验证中途分配更高版本后 SQL 历史和
事务记录仍使用原预留版本；目标／策略／空凭证不匹配在 CREATE 写入前失败，
5 个自动授权 SQL 失败点均保留调用者事务且不重复分配版本。安装 adapter 的
28 个串联场景、GIS 真实 DSO SQL／LOB／batch、综合内核回归和 6 项相关 CTest
仍通过；`git diff --check` 通过。SQL transport 和模块 provider 受控，
这些证据不代表实库事务提交、回滚或恢复验证。

UPDATE 脚本现已接入显式 native FUNCTION GRANT／REVOKE。正式 Root 入口与回归
共用 `make_routine_extension_updater`，并继续由 Rust 协调器持有唯一事务：

1. 准入按原始语句顺序解析、校验 actor／角色／目标／所有接收者。已发布对象锁读
   原始 schema 对应的完整 ACL 基线；本事务新建对象从空基线与 CREATE 私有策略
   开始。ALTER 后规划仍锁原发布版本，不锁尚未写入的未来 routine 版本。
2. 合并前序私有 ACL，选择真实授权来源，计算 GRANT 或 RESTRICT／CASCADE
   REVOKE 计划，逐组预留版本并记录私有 after-image。保留直接 no-op 组，不丢失
   grantor 来源。此阶段不写 routine／ACL SQL；协调器的依赖图栅栏不属于 DCL。
3. 整段依赖准入成功后回滚规划视图到初始保存点。执行时按语句重放 schema／ACL
   前缀，再次校验目标、接收者、当前权限和锁定 ACL，消费原有一次性版本凭证。
   变化时拒绝旧计划，不隐式重算并覆盖。任一执行失败回滚私有执行视图；SQL 回滚
   由协调器统一处理，未知结束结果不重放。
4. DCL 不创建成员，也不会收编普通外部函数。返回完整的保留／新建成员集合。
   单个权限计划最多 16,384 组；整段 UPDATE 保留的基线行与预留组共用 262,144
   上限，避免每条语句复制一份不受限制的完整 ACL 图。

验证通过：真实 Root adapter＋catalog binder＋Rust UPDATE 协调器串联
CREATE→GRANT→REVOKE→CREATE，22 个 SQL 写入失败位置、4 个解析失败位置、
未知提交／回滚；已发布原生成员上的 GRANT→REVOKE→CREATE，17 个写入失败
位置、3 个解析失败位置及准入后 SQL ACL 状态偏差。检查规划期无 routine／ACL
写入、执行前清除未来视图、原成员保留与 DCL 不产生成员。GIS fixture 新增四个
真实升级包，验证普通／多接收者 GRANT、REVOKE、GRANT OPTION 和 CASCADE 的
UPDATE preflight 与真实 resolver。完整构建、catalog/writer、GIS、综合内核、
6 项 CTest 和 `git diff --check` 均通过。

上述 Root 集成仍使用受控 SQL transport／已解析输入；GIS 包用例验证真实解析
而非实库写入。随后补齐下述 ALTER／DROP 基本混合矩阵，但委派链、重载及资源
边界的完整组合，以及实库提交、并发与恢复仍待验证，不能推定全部 PG 事务语义已实现。

#### ALTER／DROP 混合脚本与提交后缓存失效

参考包新增 `native_math--1.1--1.2.sql`，将 ALTER 与 DROP／同名 CREATE 放入
同一次 UPDATE；control 的默认安装版本仍为 1.0，旧版本脚本不变。
`native_increment` 重建后默认实参从 41 改为 99，使默认调用结果从 42 变为
100，能检测缓存仍使用旧默认值，而不仅检查 SQL 名字是否还存在。
`native_extension_server.py` 的可选实库流程增加以下断言：新对象使用新 ID，
其他成员 ID 保持不变，旧成员／依赖／对象 ACL 清除，旧授权不能继承到新对象，
跨连接 SHOW CREATE 可见 ALTER 属性，预处理调用重新绑定且重新检查权限；
为新对象授权后可执行，另一数据库的 1.0 安装不受影响。
脚本遇到未知 UPDATE 结果立即停止并保留自身夹具，不重试、不自动重置实例。
离线脚本测试只验证断言与失败停止行为，不代表这些实库语义已通过。
本轮 16 项脚本自测（含 33 个 SQL／通信位置逐一失败注入）、6 项相关 CTest、
真实 C++／Rust package reader、平铺安装以及 kernel 综合解析／执行回归通过。
再次探测确认 Unix socket 可创建但 bind 被拒绝，TCP 创建也被拒绝；没有启动
或重置用户实例，1.2 实库升级尚未执行。

真实 UPDATE adapter 新增四条路径：新建／已发布对象分别执行
GRANT→ALTER→REVOKE→CREATE，或 GRANT→DROP→同名 CREATE。ALTER 后权限仍
按原对象 ID 保留，后续 REVOKE 绑定 ALTER 的新版本；DROP 锁读并删除同一事务
中的对象 ACL，同名重建获得新 ID，旧对象和旧成员不会复活。四条路径分别逐一
注入 30／25／27／22 个 SQL 写入失败点，另覆盖全部解析位置和未知提交结果。

检查发现并修复了扩展 DROP 的缓存边界：原来借用外部 DDL 事务时仍调用
`flush_pl_cache_by_sql`，会在提交前走另一条 SQL 连接。现在通过
`ObDDLSQLTransaction::record_routine_invalidation` 记录待失效的数据库／routine
身份，`RoutineDdlInvalidation` 复用 Rust journal 和现有 plan-cache 队列：

- 记录真实 DDL epoch 和 schema 操作版本；宿主私有 scope/barrier 标签不是
  物理事务 ID，也不用于伪造版本或赋予 DROP 权限。
- 正常 Root end-sign／MDS／watermark 准备成功后、数据提交前预留队列容量；
  无容量、队列关闭或版本不匹配仍可由同一事务回滚。
- 确认提交才发布正常缓存请求；确认回滚取消。结束结果未知不报告已提交，沿用
  队列的保守版本门槛淘汰，不重放 SQL，也不凭队列记录发布 schema。
- plan-cache worker 在 schema 已刷新到要求的版本后执行本地淘汰；失败保留请求
  重试。失效请求不依赖已经退出的事务对象或旧 SQL 连接。

8 个 journal＋真实 Rust 队列场景验证提交前不可交付、确认回滚取消、版本门槛、
未知结果、预留／seal 失败、关闭队列、重复身份的独立幂等票据与淘汰失败重试。
混合 Root SQL fixture 检查 DROP 调用外部事务的记录接口，不再依赖全局 SQL proxy；
其事务结束和 SQL 行仍受控，不能代替真实 `ObDDLSQLTransaction::end` 的存储验证。
完整构建、catalog/writer、GIS、综合内核回归、6 项相关 CTest 和
`git diff --check` 通过；没有部署、重启或重置用户实例。

仍须补齐：

1. 扩展 UPDATE 混合 ALTER／DROP 的委派链、跨重载与资源边界串联矩阵；
   后续语句必须看到此前的私有 ACL 变化。
2. 安装事务的端到端失败注入与实库验证：失败统一回滚 schema、ACL、成员、依赖
   和版本；提交结果未知不得重放。
3. 验证 CREATE→GRANT→后续语句、REVOKE 后续拒绝、同名不同重载、整批接收者
   失败、保存点／整体回滚及实库提交；载体 codec 和离线测试不能替代这些证据。

## 迁移顺序与验收

### A. 包源与交付（本阶段）

实现平铺发现/安装、受控脚本目录、按版本 `MODULE_PATHNAME` 替换，沿用已有
版本图、依赖选择和独立脚本边界。测试旧布局、新布局、冲突、符号链接逃逸、
失败输出清空、替换前后大小上限及真实 C++/Rust ABI。替换只准备文本，不代表
已实现 LANGUAGE C 或加载许可。现有 GIS SQL 行为不变。

### B. 原生函数对象闭环（进行中，已接通 SQL 创建入口）

已实现的基础：

- PL parser 接受 `CREATE FUNCTION ... RETURNS ... AS '模块逻辑 ID', '实现 ID' LANGUAGE C`，
  使用独立 `T_SF_NATIVE_BODY` 节点，保留源文用于诊断；不改写为 RETURN wrapper。
  `NativeFunctionDeclaration` 拷贝拥有模块/实现 ID，拒绝路径、空值、超长值、
  非规范 ID 和不支持的语言。LANGUAGE C 指稳定 C ABI，不限定实现源码只能是 C。
- Registry/Loader 的 `resolve_native_function` 精确匹配模块所有者和实现对象 ID，
  在同一不可变 snapshot 上进行参数/转换匹配，返回当前 generation/epoch 的绑定。
  这不是按 SQL 别名重新选择重载，也不会自动加载库或授予数据库权限。
- scalar descriptor 新增 `IMPLEMENTATION_ONLY` 标志，将实现注册与 SQL 名字发布
  分开。带此标志的对象不进入 SQL 名称枚举／重载候选，也不占用同名 SQL 签名；
  仍按模块＋对象 ID 检查唯一性、精确绑定、记录依赖并参与生命周期清单。可省略
  SQL 名称或仅保留诊断标签。未知标志和非 scalar 的误用会被拒绝，旧 host 不会
  把新标志静默解释为 SQL 可见。此标志不是隐藏代码或绕过权限的机制。
  C 参考插件已有有标签／无标签两个实现，Rust SDK 暴露同一标志并拒绝为它生成
  SQL RETURN wrapper；GIS 全量切换将在 SQL 包与参数语义准备好之后进行。
- 标量／批量执行不再按 `binding.sql_name` 重新枚举实现，改为按对象 ID、模块
  所有者、generation 与 catalog epoch 原子取得对象和服务租约；租约内再检查
  flags、参数范围和静态结果类型。无 SQL 标签的实现也可正常调用和序列化，
  普通 SQL 可见函数仍须有名称。执行错误或失效绑定均释放租约。
- 执行复用 `execute_bound_function` 的对象/代码租约及类型转换路径。
  元数据绑定不持有永久代码租约，停用或换代后旧绑定须重新验证。
- `ObRoutineInfo` 明确保存 `native_module_id`、`native_implementation_id` 和
  `native_abi_version`，不把绑定藏在函数体或注释里。复制与反序列化拥有 ID 字符串；
  standalone FUNCTION 才能持有当前 ABI 1 绑定。参数/返回签名、owner、身份、
  数据库和 schema version 沿用 routine 字段。
- `__all_routine` 及其 history 的生成定义、读写路径已接入三个字段。普通 SQL/PL
  DDL 不引用新列，因此未更新系统表的实例仍可执行原来的 routine 写入；历史记录
  缺少全部新字段时按普通 routine 读取，部分绑定、未知 ABI 或读取错误不会吞掉。
  native→SQL 替换显式清空三个字段。该字段实现**不是系统表升级方案**；旧实例仍
  需要后续的原生功能准入/系统表升级协调，不应手工向系统表写入原生记录。
- `ObIModuleProvider` → `ObServerPluginRuntime` → loader 已提供精确实现绑定入口；
  未初始化或未启用插件的失败路径清空输出，不会按 SQL 名字回退或自动加载库。
- 普通 `T_FUN_UDF` 的代码生成可根据 routine ID 读取原生绑定，校验 schema version、
  参数/结果物理类型及声明确定性，并保存拥有字符串的可序列化绑定。不增加隐藏名字
  参数，也不通过 `RETURN 内部插件函数(...)` 转发。PL 局部函数和构造器保持原路径。
- 原生 UDF 标量与批量执行均复用通用插件执行器，调用前检查 routine 版本与当前
  `EXECUTE` 权限；NULL 不绕过权限。参数先以调用者身份求值，再切换 routine 的
  数据库、执行环境和 invoker/definer 身份。成功/错误返回均恢复上下文；不会额外
  开启、提交或回滚事务。批量求值跳过已完成行，错误会清除结果有效标记。
- SQL/PL 的现有 routine 名称解析按数据库查找 catalog 签名、记录函数 ID/version
  依赖，可直接供上述原生执行桥使用，不另造名字分发器。已验证限定名、当前数据库
  裸名称、未安装数据库不可见，以及错误参数个数。PL 函数体引用原生 routine 时
  也能解析并记录依赖；这不等于该 PL 调用者已通过字节码执行测试。
- SQL parser 的 `POINT`／集合构造关键字节点在模块 SQL 名称不存在时，也转入
  普通数据库 routine 解析，不因它们是关键字就绕过 catalog。`GEOMETRYCOLLECTION`
  与 `GEOMCOLLECTION` 保留不同的 SQL 名称；共享算法实现不等于共享 routine ID
  或权限。内核 GIS 打开的构建仍走原来的内建路径。这是 GIS 停止自动发布 SQL
  名字的前置接线，尚不等于已将现有 GIS 模块切换为 implementation-only。
  完整主程序构建及真实 GIS DSO fixture 已通过：受控 provider 隐藏模块 SQL
  名字后，8 种构造名字按各自 routine ID 解析；未安装数据库不可见，限定数据库
  名可访问，嵌套 `ST_Length(LINESTRING(POINT(0,0),POINT(3,4)))` 返回 5。
  已生成表达式在撤销嵌套构造函数的 EXECUTE 后拒绝执行；删除一个集合别名
  不影响另一个，删除 POINT 后也不回退为内建实现。catalog 与权限由 fixture
  提供，此证据不等于 GIS 模块已经切换注册方式或实库安装／卸载已通过。
  同轮原生声明解析及 `kernel_script.py` 综合回归通过。
- 原生 SQL 形参采用内核物理类型时，已知内建/GIS 类型在完成 SQL 参数转换后
  按形参类型封送。例如 `ST_GeomFromText` 的 GIS geometry 可以传给声明为
  `GEOMETRY` 的原生 routine。只允许已知命名空间，不按 `.geometry` / `.int64`
  后缀猜测任意自定义类型；未解码的存储类型必须经过 codec，不能直接当作内建值。
- 原生函数已接通 `DEFAULT` 的数值、布尔及 NULL 字面量。默认值属于 SQL routine
  参数，不属于共享实现；省略参数插入实际表达式，经普通 SQL 参数转换后求值，
  不再使用仅供 PL 字节码补值的 NULL 占位。显式 NULL 不触发默认值。
  遵循 [PG 默认参数的尾随规则](https://www.postgresql.org/docs/18/sql-createfunction.html)：
  一个输入参数有默认值后，后续输入参数也必须有默认值。CREATE 及原生实现绑定
  都检查默认值内容／顺序；数字不能作为 GEOMETRY 默认值，NULL 可以。
  wire 使用现有参数 `default_value` 字段，SHOW CREATE 输出并可重新解析。
  普通 MySQL PL 函数／过程仍拒绝新增默认参数，不改变其执行语义。
  **尚非完整默认表达式支持**：函数／对象引用需要定义期绑定与持久依赖，字符串
还需固定定义期字符集／转义语义，当前明确拒绝；原生 SQL 重载选择进展见下文。
- 原生重载的事务私有视图基础已接入：同一函数名可容纳不同输入签名的独立
  ID／slot，按 slot 查找和按完整候选集合合并分开。输入身份不包含参数名、
  默认值、返回类型或实现 ID；保留二进制／字符 SQL 类型及数组形参的差别，
  DECIMAL 的普通和 decimal-int 内部表示归一化。这个字符串只用于内存比较，
  不是持久格式、散列 ID 或 SQL 对象 ID。
  视图拒绝重复输入签名、原地改变某个 ID 的输入身份、未删除就覆盖已有身份，
  以及将普通 MySQL PL routine 混入原生重载集合。独立删除和 savepoint 回滚
  不回收仍被引用的旧 schema。新 API 只合并调用者传入的 base 候选与 overlay，
  **Root slot 分配和对象级权限尚未接通**；
  Root 写入端仍拒绝非零 slot，不开放半成品重载 DDL。
  完整构建、catalog fixture、原有单函数 overlay 回归及真实 GIS DSO fixture
  已通过；106 条声明在同一拥有数据的视图中按名字形成重载集合。测试覆盖
  重复签名拒绝、基础快照合并、错误覆盖拒绝、独立删除／替换、savepoint 回滚、
  失效视图拒绝及旧指针生命周期；不把这些测试计为持久 catalog 或授权验证。
  38 项受控 routine writer 场景和 4 项相关 CTest 也通过。最终综合回归首次
  在构建辅助 Rust candidate DSO 时遇到 LLD 自身崩溃（尚未执行综合断言）；
  未改工具链配置，使用原命令重跑后通过。以上均不代表实库安装／恢复已验证。
- schema 快照中的同名候选索引已实现：`ObRoutineMgr` 维护按数据库、package、
  类型、名称和 slot 排序的次级指针索引，以 `O(log N + K)` 枚举 standalone
  function 家族，不扫描数据库内所有 routine，也不依赖 slot 从 0／1 连续排列。
  增删、同 ID 改名／换 slot、浅复制、深复制、reset 和索引重建同步维护它；
  同一名称／slot 的不同 ID 冲突在修改前拒绝，索引失效时候选查询返回错误。
  新的 `ObSchemaGetterGuard::get_standalone_function_infos` 加载基础快照的完整
  schema，校验 ID／名称／slot／版本，再与事务视图合并。不能先使用单 ID 的
  overlay 查询替换基础对象，否则会掩盖输入身份冲突或删除记录。
  这只是持久 schema 的**内存查询索引及 guard API**，没有修改持久格式、
  分配新 slot；该阶段尚未接入 SQL 重载选择。该阶段完整构建、catalog manager／guard
  测试、106 条 GIS 声明的同名候选核对、GIS 实现回归、38 项受控 routine writer、
  独立 overlay 回归、完整 `kernel_script.py` 及 4 项相关 CTest 均通过。
  manager／guard 测试覆盖稀疏 slot、数据库／package／函数与过程命名空间隔离、
  大小写比较、改名／换 slot／删除、冲突拒绝、浅／深复制、分配失败不破坏旧值、
  旧指针生命周期、基础 schema 版本不匹配拒绝及 overlay 删除／保存点回滚。
  持久 catalog 加载、SQL 语句重载选择和实库安装不在这些测试的证明范围内。
- 原生 SQL 重载选择已接入：普通 SQL／PL 外部函数解析先取得 guard 的完整候选，
  `NativeRoutineOverload` 按实参类型选出 catalog 对象，而不是按 ID、slot 或注册
  顺序取第一个。原有普通 PL／包函数继续走原匹配器，未扩大其重载权限。
  规则参考 [PG 函数类型解析](https://www.postgresql.org/docs/18/typeconv-func.html)：
  精确类型优先，同一有效输入签名的固定参数优先于展开式可变参数；同名默认参数
  前缀冲突报歧义，不用“默认参数少”来猜。输入精确度只计算已提供的实参，区分
  字符／二进制类型，DECIMAL 的两种内部表示归一化。NULL／未知参数不算精确匹配，
  从剩余候选推断类型类别；仍有多个同等候选时返回歧义。
  **SeekDB 适配而非 PG 类型兼容层**：可转换性使用现有 `cast_supported`，没有
  复制 `pg_cast` 的 implicit 标记；数字类别优选 DOUBLE，文本／字节类别分别
  优选 LONGTEXT／LONGBLOB。名称仍按当前或显式数据库解析，不引入 `search_path`。
  选择过程不执行默认值、不做授权、不加载模块；后续普通 UDF 路径保留对象 ID、
  schema version、代码绑定和执行前权限检查。该阶段授权仍是 MySQL 名称级，
  **Root 非零 slot 写入仍关闭**，不能据此声称已支持按重载授权或持久安装。
  完整构建、选择器/catalog fixture、实际 SQL 的同名 GIS 选择与真实 DSO 执行、
  独立删除后重新选择、默认参数歧义、38 项 writer、综合回归和 4 项 CTest 通过。
  预编译 SQL 解析补充测试也通过：`over_geo(?,?)` 报歧义，
  `over_geo(?,POINT(3,4))` 从已知几何参数选出固定双几何重载；使用受控参数表，
  **未验证客户端预编译协议／缓存执行或实库安装**。
- 原生函数的按输入签名 ALTER 已接入 parser／resolver：
  `ALTER FUNCTION db.f(DOUBLE, DOUBLE) COMMENT '...'` 与
  `ALTER FUNCTION db.f(GEOMETRY[]) COMMENT '...'` 精确指定声明的输入类型。
  DDL 不使用调用匹配器，不做隐式转换、默认参数补齐或可变参数展开；省略整个
  签名时仅允许唯一候选，多个候选报歧义。显式 `()` 表示零输入参数，不等于
  省略签名。当前仅支持原生函数的内建输入类型及末尾数组，不支持参数名／模式、
  通用自定义类型，也没有扩展普通 PL 函数的按签名 DDL。
  resolver 将目标 routine ID、slot、schema version 和完整签名保存在现有
  ALTER 参数及 DDL 依赖中；Root 属性修改与调用者事务适配路径按 ID 复核目标，
  不再重新选择 slot 0。Root 还检查名称、数据库、owner、版本及原生绑定签名。
  **非零 slot 的持久写入仍明确拒绝**：直接 writer 和扩展批处理入口均有检查，
  等对象权限与完整生命周期接通后再开放。该阶段不是可持久执行的重载 ALTER。
  测试覆盖真实 parser／resolver 的类型、数组、元数与歧义匹配、目标 ID／版本
  依赖及参数序列化往返；writer 测试验证非零 slot 在 catalog／模块操作前拒绝，
  且失败后仍保持一次性 writer 语义。受控 schema 夹具没有执行 Root 属性修改
  事务，不将 resolver／wire 通过当作提交、授权或实库回归通过。
  本轮完整 seekdb 构建、原生声明及 catalog fixture、GIS SQL／真实 DSO 回归、
  38 项 writer 场景、综合 `kernel_script.py` 和 4 项脚本／包布局 CTest 全部通过；
  新头文件 license 与 `git diff --check` 通过。未部署或操作用户的测试实例。
  按签名 DROP 的后续进展见下；CREATE slot 分配与对象级 ACL 仍未接通，
  GIS SQL 包尚未加入正式安装。
- 按签名 DROP 已接入实现：共享 ALTER 的精确输入签名解析，支持
  内建类型和末尾数组；省略签名且存在多个候选时报告歧义，`IF EXISTS` 不消除
  歧义。原生目标作为拥有数据的 routine 快照保存在 DROP 请求中，单独序列化
  schema version，并在解码后深拷贝；不依赖声明格式中缺失的 schema version
  或输入缓冲区的生命周期。命中目标还携带现有 DDL 版本依赖。
  Root／调用者事务按 routine ID 读取并复核数据库、名称、slot、owner、版本
  和原生绑定签名；已解析的 `IF EXISTS` 未命中保留为 no-op，不改删同名的
  其他签名。扩展更新临时对象表的 key 增加 slot，事务私有视图删除也传入
  精确 slot。**这不开放非零 slot 的持久删除**：routine 授权写入入口仍按名字保存，writer
  在 catalog／依赖／ACL 修改前拒绝，以免撤销另一个重载的权限。
  同时修正原生 ALTER 的跨 RPC 版本复核：从已序列化的 DDL 对象依赖读取版本，
  不假设 routine 声明格式会携带版本。新增请求往返测试先发现签名复核会把
  `ObDataType` 格式不携带的声明辅助字段当作身份差异；比较已改为传输的类型
  metadata（含 collation）、accuracy 和 zerofill，加上参数 flags、名称、默认值
  及原生绑定。测试验证真正的类型、collation、精度、对象 ID、slot、owner、版本
  或实现绑定变化仍拒绝；不是通过跳过签名检查解决往返失败。
  GIS fixture 已验证精确签名、数组、默认参数不补齐、零参数、`IF EXISTS`、
  大小写名称、请求／扩展批处理复制及破坏原缓冲区后的目标持有。它没有执行
  Root 删除事务或验证完整对象权限；完整重载创建、对象级授权、GIS 安装／
  更新／卸载和恢复仍待接通与实库验证。
  修复后完整 seekdb 构建、原生声明、catalog fixture、GIS SQL／真实 DSO、
  38 项 writer 场景、综合 `kernel_script.py` 及 4 项相关 CTest 全部通过；
  `git diff --check` 通过。新增的非零 slot 删除检查已验证在 catalog、依赖、
  模块和授权变更之前返回，并保留一次性 writer 语义。没有部署、重启或重置
  用户实例，也没有将这些受控测试当作 GIS 扩展的实库安装／卸载证据。
- 对象级权限第一阶段：复用已有 `__all_objauth`／`__all_objauth_history`、
  `ObObjPriv` 和权限缓存，权限键是函数对象 ID、对象类别、对象层级、grantor
  与 grantee；不把 SQL 名字或 overload slot 当作授权身份。新增 guard 检查
  先核对当前 routine 的 ID、数据库、owner、slot、版本和绑定，再合并精确对象
  上不同 grantor 的授权；不会退回名称级 routine grant。
  权限收集读取当前 user/role schema，只沿当前仍授予用户的已启用角色向下
  遍历；嵌套角色去重、检测循环并限制数量，不相信 session 中过期的权限位。
  保留 SeekDB 用户级／数据库级权限；GRANT OPTION 按 EXECUTE／ALTER 各自
  检查，不把某一项的转授权能力扩散到其他权限。
  非零 native slot 的通用执行入口已使用该检查。**slot 0 暂时保留旧授权通道**，
  不将阶段性接线当作全量原生 ACL 迁移。SQL GRANT／REVOKE 写入和 Root 自动
  授权尚未接入 ID 授权；私有视图、DROP 清理和签名解析的后续进度见下文。因此非零 slot
  的持久 CREATE／ALTER／DROP 仍不开放。现有对象授权表和缓存可复用，不表示
  这些 routine 生命周期入口已经完成或经过实库验证。
  catalog fixture 已验证同名对象／对象类别／column 隔离、多个 grantor 合并、
  每项权限独立的 GRANT OPTION、已启用且仍获授的嵌套角色、循环去重、过期
  session 权限位、旧 routine 版本，以及私有 schema 删除／保存点回滚／retire。
  GIS fixture 使用正常 SQL 解析和真实 DSO：已有名称授权及另一个重载的对象授权
  均不能调用，加入精确 ID 授权后成功，撤销后复用同一表达式再次执行被拒绝。
  这是受控 ACL 缓存变更，不是 SQL GRANT／并发撤销提交／真实缓存刷新的证据。
  现有对象授权写入器的当前表、历史表和操作日志 SQL，以及精确对象／grantor／
  grantee 的撤销条件和历史写入失败传播也已验证。测试发现未限定的对象授权表名，
  已将相关 DML 明确限定为 `oceanbase` 内部表，为借用调用者连接做准备；没有
  新增独立授权表或让名称级 routine 授权兼作对象 ID 授权。
  最终完整 seekdb 构建、catalog、GIS、38 项 writer、综合 `kernel_script.py`、
  4 项相关 CTest 和 `git diff --check` 均通过。未操作用户测试实例；下一步仍需
  将 SQL 授权请求、事务私有授权及 routine 生命周期接到这些对象 ID 机制上。
- SQL 授权入口的对象固定阶段：`GRANT/REVOKE ... ON FUNCTION db.name` 对 native
  函数先读取完整候选族；多个重载时拒绝歧义，不从 slot 0 或最小 slot 中任选。
  唯一候选的 ID、owner、数据库、slot、签名、绑定和版本独立拷贝到 DCL 请求，
  经序列化后仍拥有自己的参数和字符串。routine 声明 codec 不保存 schema version，
  所以该版本随目标单独传输；重复解码会清除前一次目标。
  Root GRANT 在自动创建用户及授权写入之前校验目标，REVOKE 在进入旧写入服务前
  校验；请求中的外层 object ID／类型／名称也必须与目标一致。目标删除、替换、
  版本或绑定变化以及候选族新增重载均不能静默转为另一个授权对象。
  **这一步没有开放非零重载的 SQL 授权写入**：校验后仍返回 NOT_SUPPORTED，
  不能落入旧名称级权限表。slot 0 继续兼容旧权限写入，既未宣称该路径已改为
  对象 ID 写入，也未宣称并发 DDL 与授权之间已实现事务级序列化。
  grantor 选择及权限依赖仍需实现；签名形式的 GRANT/REVOKE、精确对象权限写入、
  调用者私有 ACL 保存点／回滚和原生 DROP 清理的后续进度见下文。
  已验证完整 seekdb 构建、catalog、38 项 writer、GIS SQL／真实 DSO、综合
  `kernel_script.py` 和 4 项相关 CTest。新增 GIS fixture 直接运行 GRANT／REVOKE 的 parser 和 resolver，
  覆盖名称歧义、非零唯一候选、大小写、请求往返后覆盖传输缓冲区、重复解码、
  外层 ID／类型不匹配、目标版本／slot／owner／绑定变化以及非零写入拒绝。
  这些是解析、传输与准入检查证据，不是已提交的 SQL 授权或实库生命周期证据。
- GRANT／REVOKE 的精确声明签名：主 SQL parser 接受
  `GRANT EXECUTE ON FUNCTION db.f(DOUBLE, DOUBLE) TO ...` 和对应 REVOKE，
  可指定空参数列表及末尾 `GEOMETRY[]`。复用已有 SQL 类型／数组语法，将列表
  包装适配到 ALTER／DROP 使用的 `NativeRoutineDdl`，统一生成输入签名并查找
  同名候选族。不做调用转换、默认参数省略或可变参数展开；参数名、表达式和
  默认值不是授权签名。非数组 collection／嵌套数组以及非 FUNCTION 的类型
  签名明确拒绝。
  `NativeRoutinePrivilegeTarget` 携带拥有所有权的 routine 快照和“显式签名”
  标记，均经过请求序列化；复用 decoder 会清除旧标记和旧目标。Root 仍先复核
  数据库、ID、版本、slot、owner 与绑定；带签名时只要求该输入签名唯一，不因
  存在其他输入签名而误判名称歧义。不带签名时继续要求整个候选族唯一。
  **当前仅完成解析、目标传递和准入，尚未开放带签名的授权写入**。所有带签名
  请求（包括 slot 0）在准入末端仍返回 NOT_SUPPORTED，防止进入名称级写入服务，
  把授予某个重载的权限扩散给同名重载。解除此限制需要完成对象 ACL 写入、
  grantor／转授权校验、自动授权和事务／缓存发布的整体接线。
  主 SQL parser 已重新生成且无语法冲突。GIS fixture 通过真实 GRANT／REVOKE
  parser 和 resolver 验证精确类型、默认参数不能省略、末尾数组、大小写、
  无匹配签名、非 FUNCTION／非法签名拒绝，以及拥有所有权的请求往返和 decoder
  复用。受控私有视图验证重复签名拒绝、删除重建后旧请求不转向新对象、保存点
  回滚后恢复原目标；不把这些检查当作实库锁竞争或授权提交证据。
  完整及最终增量构建、GIS 真实 DSO、catalog、48 项 writer、综合
  `kernel_script.py`、4 项相关 CTest 与 `git diff --check` 均通过。
- 原生 DCL 的对象权限检查：带签名或非零 slot 的 GRANT／REVOKE 在权限提取时
  生成带 `(routine_id, schema_version)` 的 `ObNeedPriv`，而不是共享名称授权。
  普通赋值和 `deep_copy` 都保留该身份；guard 先验证对象、数据库、名称和版本，
  再复用 native 对象权限检查。缺失对象、旧版本、错误权限级别或不完整身份不
  回退到名称授权。后续自动授权改造已将未带签名的 slot 0 一并转向对象 ACL；
  所有 native SQL DCL 写入口仍关闭，见下文“原生 CREATE 自动对象授权”。
  转授权要求每项请求权限各自带 GRANT OPTION，使用当前 user／role schema、
  对象 ACL 及事务私有覆盖，不凭 session 缓存的广泛权限或 EXECUTE 普通权限
  放行；若需要创建被授权用户，还单独提取 CREATE USER 权限需求。
  GRANT 和 routine REVOKE 执行器从当前 `priv_user_id` 对应的 user schema 填写
  请求的 `grantor_id`、用户名及 host；原生请求找不到该主体时明确失败，不再
  带着默认／空授权人继续执行。**这不是角色授权来源选择、授权依赖撤销或宿主
  事务内最终授权校验的替代品；SQL 对象权限写入与自动授权仍未接通。**
  GIS fixture 从真实解析的 GRANT／REVOKE 生成权限需求并深拷贝后执行检查，
  覆盖普通授权不能转授、EXECUTE／ALTER 的 GRANT OPTION 彼此独立、旧 session
  广泛权限与名称级授权不能替代对象权限、私有撤销和保存点恢复，以及重复检查
  时 ACL 撤销、错误数据库／权限级别／不完整身份／过期版本的拒绝。
  完整构建、GIS 真实 DSO、catalog、48 项 writer、综合 `kernel_script.py`、
  4 项相关 CTest 与 `git diff --check` 均通过；未操作用户测试实例。
- 写入前的事务对象权限复核：`change_native_routine_privileges_authorized` 接收
  宿主已确定的 actor、已启用角色和选定 grantor，复用完整权限读取器锁定并复核
  routine，读取同一事务内该 FUNCTION 的全部对象 ACL。该快照是完整替换视图：
  缺少记录就是缺少权限，不从缓存或独立的私有 after-image 补齐。
  guard 继续沿当前 user／role schema 验证已启用、仍授予且可达的角色，并把
  本次委托权限限定在选定 grantor 自身；来自不同主体的 EXECUTE／ALTER 权限
  不能拼成某一个主体的授权来源。校验通过后才进入实际差异 SQL 写入，再发布
  私有视图。角色保留为权限记录的 grantor，而私有视图仍按经过检查的 actor
  限定所属用户；发布失败也返回错误，要求调用者回滚已执行的 SQL。
  **这里的事务权威读取覆盖对象 ACL，不替代宿主对 user／role／database 元数据
  的串行准入和当前 guard 保证。** 该包装器不选择 grantor、不处理授权依赖级联、
  不拥有事务或发布全局缓存；Root SQL GRANT／REVOKE 尚未改为调用此链路，
  自动授权及持久非零重载限制仍待继续推进。
  Catalog fixture 运行该包装器和真实差异 SQL 服务，验证空／已撤销事务快照
  覆盖旧缓存与私有授权、普通权限不能转授、选定用户／角色与嵌套角色来源、
  未启用／未授予角色拒绝、不同主体的权限不能冒充同一个 grantor、收件主体
  缺失和目标过期，以及读取／关闭／三处写入失败。另覆盖带转授权的 GRANT、
  REVOKE、仅撤销 GRANT OPTION、no-op 仍复核资格、角色写入调用用户私有视图、
  视图主体不匹配后的失败传播及保存点回滚。以上是受控 SQL 传输和元数据视图
  证据，不是实库锁竞争、Root SQL 授权提交或恢复证据。
  完整及最终增量构建、catalog、48 项 writer、GIS 真实 DSO、综合
  `kernel_script.py`、4 项相关 CTest 与 `git diff --check` 均通过。
- 授权主体上下文与来源选择：原生 GRANT／routine REVOKE 执行器把当前用户 ID
  和已启用角色随拥有独立内存的目标请求传递，不传 session 权限缓存或授权令牌。
  角色 ID 先验证，再排序去重；请求解码在读取角色前限制数量，拒绝无用户却有
  角色、重复／逆序／非法 ID，并清理失败 payload 中的目标上下文。
  `select_native_routine_grantors` 复用当前 user／role 图遍历和完整事务对象 ACL，
  分别选择 EXECUTE／ALTER 的来源：owner／当前 superuser 的来源规则见下文；
  其他情况优先有资格的当前用户，否则选有资格的最小角色 ID。
  每个主体必须自身满足所委托权限及 GRANT OPTION；不能拼接不同主体
  的普通权限和转授权位。任一请求权限没有来源时，清空整个结果，不返回部分计划。
  **选择结果不是授权凭证，也不写 SQL。** 宿主仍须按来源分组、为不同 grantor
  预留不同版本，调用事务授权写入器再次复核，并处理授权依赖；Root SQL 接线、
  自动授权和非零 slot 的持久化准入限制尚未完成。
  GIS fixture 验证角色去重及自引用绑定、请求往返后的内存独立性、actor 与外层
  grantor 一致性、payload 失败／复用清理、有界且严格有序的角色 wire，以及
  目标重新赋值后不残留主体。Catalog fixture 验证权限分属不同来源、角色顺序
  无关、用户优先于更小 ID 的角色、空快照和未启用／未授予角色拒绝、广泛权限
  不能跨主体拼接、后续角色图错误清空已有选择，以及旧选择在权限撤销后不能
  绕过写入器复核。完整内核构建、最终 catalog、48 项 writer、GIS 真实 DSO、
  综合 `kernel_script.py` 和 4 项相关 CTest 均通过；未操作用户测试实例，仍不
  宣称 Root SQL 提交、实库并发或恢复验证已完成。
- 宿主原生 GRANT 批量写入：`NativeRoutineGrantWriter` 在同一个已启动的调用者
  事务中接收已固定对象及用户／角色上下文，预检并去重全部收件人，读取完整
  对象 ACL，选择 EXECUTE／ALTER 的 grantor，再按 `(grantee, grantor)` 分组。
  每组由真实 schema service 独立预留递增版本；全部版本预留成功后才写 SQL，
  同一主体同时提供两项权限时合并为一组。每组写入前再次进行事务权限复核。
  SQL 全部成功后才发布事务私有视图；发布失败由视图保存点撤销本批所有变更，
  SQL 仍必须由调用者回滚。已附加私有视图的 guard 不能省略对应发布视图。
  返回值仅携带真正发生 SQL 变化的最大版本，失败或纯 no-op 返回零；对象一次
  尝试后即不可复用。**这不是 SQL 入口准入或事务提交器**：不创建用户、不拥有
  SQL 事务、不发布全局缓存，也不处理 REVOKE 及其授权依赖。Root SQL 入口和
  非零重载的现有限制仍保留，不能把此写入器的测试当作 GRANT 已可在实库使用。
  Catalog fixture 的 43 个宿主批量场景通过：覆盖收件人去重／全量预检、独立
  grantor 的实际 SQL 键和历史版本、同来源合并、GRANT OPTION、预留失败／异常／
  非递增版本、后续权限失效、no-op 及最大实际变更版本、无私有视图的基础 guard、
  私有视图遗漏／不匹配／退休，以及 SQL 成功前不发布前缀、发布失败恢复原视图、
  调用者保存点、一次性消费和全部 12 个 SQL 失败位置。完整及最终增量构建、
  最终 catalog、既有 48 项 writer、GIS 真实 DSO、综合 `kernel_script.py`、
  4 项相关 CTest 和 `git diff --check` 均通过；未操作用户测试实例。
- 原生 REVOKE 授权依赖计划：`NativeRoutineRevokePlan` 以完整事务对象 ACL 和
  宿主确认的授权根为输入，按 EXECUTE／ALTER 分别计算 GRANT OPTION 可达性。
  直接撤销后仍由独立路径支持的下游授权保留；失去授权根的环、自授权和长链
  不能自行维持资格。仅撤销 GRANT OPTION 保留被直接指定的普通权限，但其
  无独立来源的下游授权仍属于依赖撤销。RESTRICT 拒绝额外的依赖撤销，CASCADE
  输出全部必要差异；重复及重叠直接请求按单调移除合并，不依赖输入顺序。
  规则参考 [PG REVOKE](https://www.postgresql.org/docs/18/sql-revoke.html)。
  算法使用有界邻接表和非递归队列，不按依赖深度使用调用栈；结果按
  `(grantee, grantor)` 排序，仅返回发生变化的完整 before／after 权限。
  错误、RESTRICT 拒绝或分配失败均清空输出；已有无授权根的 ACL 明确拒绝，
  不借一次普通撤销静默修复历史不一致。其他 column 组不能提供 FUNCTION
  级转授权，也不会被当成该函数权限撤销的目标。
  `collect_roots` 复核固定 routine 后，从当前 guard 的 user／role 和目标数据库
  广泛权限提取独立授权根；对象 ACL 本身不是根，角色权限也不冒充其成员的
  独立权限。owner 固有授权根及 superuser 映射见下文；明确授予的 SeekDB 用户／
  数据库级权限仍可提供额外授权根，**并非 PG 的纯对象 ACL／角色模型**。
  宿主仍负责元数据串行
  准入、对象锁定、请求者与选定 grantor 的授权检查，不能直接信任外部输入的根。
  该计划本身不会预留版本、写 SQL、发布权限视图或提交事务；宿主写入接线见
  下文。**Root DCL 入口仍未接通**，不能把单独的计划计算当作 SQL REVOKE 可用。
  用户／角色删除、成员关系及广泛权限变更仍需维护相应授权依赖；当前遇到
  缺失主体或无根 ACL 返回错误，不会擅自把这些状态解释成可清理的授权。
  Catalog 回归已通过：包括直接及仅转授权撤销、RESTRICT、独立来源、分离的
  EXECUTE／ALTER、混合撤销、循环／自授权、重复请求和坏数据；另有 256 组
  Floyd–Warshall 独立对照图、16,384 条边的长链及输入上限测试。授权根回归
  覆盖当前用户／角色广泛权限、目标数据库隔离、普通权限与角色成员关系不构成
  根（owner 身份除外）、对象 ACL 不构成根、元数据变化后的重新提取、过期对象及缺失主体时清空
  输出。最终构建、catalog、既有 43 项 GRANT 批量／48 项 writer、GIS 真实 DSO、
  综合 `kernel_script.py`、4 项 CTest 与 `git diff --check` 均通过；这些不是 SQL
  REVOKE、实际锁竞争、提交或恢复证据，未操作用户测试实例。
- 宿主 REVOKE 计划写入：`NativeRoutineRevokeWriter` 在调用者已启动的事务内，
  自行预检收件人、确定直接 grantor、读取完整 ACL、提取当前授权根并建立
  RESTRICT／CASCADE 计划，不接受外部提供的计划或授权根。版本全部预留后，
  再次读取完整锁定 ACL，复核授权根和直接 grantor 的当前资格；任何新授权路径、
  权限位或来源资格变化都会在执行差异 SQL 前拒绝旧计划。级联 grantor 不要求
  是当前用户的角色，其撤销依据来自已获授权并整体核实的依赖计划。
  `apply_native_routine_privilege_reduction` 每次锁定精确 FUNCTION／grantor／
  grantee，比较完整 before-image 后才能减少权限；不允许新增权限、提高转授权
  或带孤立 option 位。一次差异可同时删除某项权限和降级另一项 GRANT OPTION，
  仍只使用该键的一个历史／操作日志版本；不一致时不执行该键的写入。
  所有 SQL 成功后才发布私有视图，发布失败由保存点撤销本批视图变更，SQL 仍由
  调用者回滚。未变化的直接请求也核对并刷新私有视图，包括不存在的授权键，
  避免把陈旧私有授权留在一次“成功 no-op”之后。返回最大实际 SQL 变更版本，
  失败或纯 no-op 为零；不自行提交事务或发布全局缓存。**Root SQL REVOKE
  入口、自动授权及用户／角色生命周期接线仍未完成，现有重载准入限制未解除。**
  受控 catalog 回归已通过：81 种精确 before／after 状态转换中，仅允许 36 种
  合法减少或 no-op；检查实际 SQL 主键／权限值、陈旧 before-image、无效权限位
  以及混合减少的全部 5 个 SQL 失败位置。另有 62 项宿主 REVOKE 场景，覆盖
  完整快照／授权根／角色资格重新检查、逐键旧值冲突、独立授权路径、私有视图
  剩余权限、无变化请求及全部 20 个批量 SQL 失败位置；不作为实际提交证据。
- owner／superuser 的对象授权来源：native guard 在复核对象身份／版本后读取
  当前 owner，缺失或非法 owner 拒绝执行／授权检查。owner 始终具有 EXECUTE／
  ALTER ROUTINE 的转授权能力，包括对象 ACL 为空、已撤销 owner 自身 ACL 的
  情况；ALTER 保留为所有权操作，但**普通 EXECUTE 仍必须有实际权限**，不能
  仅因是 owner 就绕过已撤销的执行权限。拥有者角色必须当前仍可达且已启用，
  陈旧 enabled-role 列表不能保留所有权能力；选择 grantor 时拥有者角色优先。
  当前用户 schema 上的 SUPER 可绕过普通权限检查，但 GRANT／REVOKE 的来源
  固定映射到对象 owner；不能写成 superuser 自己，也不能根据过期 session
  权限或被继承角色上的 SUPER 产生这种身份。选择和逐键写入复核使用同一规则。
  这参照 [PG 的 owner 权限](https://www.postgresql.org/docs/18/ddl-priv.html) 与
  [superuser DCL 身份](https://www.postgresql.org/docs/18/sql-revoke.html)。
  `collect_roots` 即使面对空 ACL 也加入当前 owner；SUPER 不再是额外的持久
  授权根。撤销 owner 的自身 ACL 不会导致它发出的授权失去来源。SeekDB 现有
  用户／数据库级显式 right + GRANT 权限仍作为额外来源保留，ALTER ROUTINE
  也仍是 SeekDB 权限项，不能将这些差异描述为 PG 权限系统的完全复刻。
  当前已完成 guard、来源选择、授权根和宿主写入器语义；自动对象授权的后续
  接线见下文。**owner 转移、用户／角色删除及 SQL DCL 批量事务入口仍需接通**，原有持久化／
  SQL 准入限制未解除。普通非 owner 的旧授权回归使用明确的非 owner 测试身份，
  保留空事务 ACL 拒绝、权限丢失拒绝及全部 SQL 失败注入覆盖。
  Catalog 回归已通过 owner 自身 EXECUTE 的授予／私有撤销／保存点恢复、固有
  grant option、拥有者角色的启用与当前成员关系、来源优先级、SUPER 到 owner
  的固定映射、SUPER 撤除、角色 SUPER 不继承、坏 ACL／缺失 owner／过期对象
  拒绝，以及空 ACL 的 owner 根和 owner 自撤权不级联掉其发出授权。
  43 项宿主 GRANT 中的 SUPER 场景现检查实际 SQL grantor 为另一位 owner；
  64 项宿主 REVOKE 包含 SUPER 以 owner 身份执行完整级联，以及预留后失去
  SUPER 时零 SQL 写入。仍不作为真实服务器提交或 owner 变更事务的证据。
  完整及最终增量 `seekdb` 构建、最终 catalog、48 项 writer、GIS 真实 DSO、
  综合 `kernel_script.py`、4 项 CTest 与 `git diff --check` 均通过；未操作用户
  测试实例。下一步仍是 SQL DCL 的统一批量事务入口和自动对象授权接线。
- 原生 CREATE 自动对象授权：`RoutineCatalogWriter::create` 在写入 routine 和
  模块依赖的同一借用事务中检查当前 owner，锁读新对象的完整 ACL。即使关闭
  `automatic_sp_privileges`，新 ID 若已有权限记录也拒绝继续，避免继承遗留
  授权。开启时，以 `(routine_id, owner, owner)` 写入 EXECUTE／ALTER ROUTINE
  普通权限，使用独立分配、严格晚于 routine 创建版本的真实 ACL schema 版本；
  当前表、历史表和操作日志失败均向调用者传播，由调用者回滚整个事务。
  native CREATE／DROP 不再写旧的名称级 routine ACL，普通 PL 函数保持原行为。
  私有 CREATE 视图按精确对象 ID 隔离同名重载，开启自动授权时给 owner 初始
  普通权限，关闭时记录明确的零权限覆盖。此处使用 routine 创建版本作为
  后续私有 ACL 事件的排序下界，**不是 SQL ACL 写入版本或提交凭证**；实际
  SQL 写入另行分配版本，整段宿主操作失败必须同时撤销 SQL 和私有视图。
  对象删除／保存点回滚联动 schema 可见性，不能通过同名对象或 ID 重用继承权限。
  执行入口现在对所有 native slot（包括 0）读取对象 ACL；按名称进行的
  FUNCTION 执行／修改权限检查也先识别当前候选族。原生候选不唯一时拒绝，
  不回落到共享名称权限。**所有 native SQL GRANT／REVOKE 暂时明确拒绝，
  包括未带签名的 slot 0**，待 Root 统一批量事务入口接通后开放。
  native 与普通 PL 之间的 CREATE OR REPLACE、native 隐式 owner 变化会在
  catalog／模块写入前拒绝；尚未实现两套权限模型的转换及 owner 依赖迁移。
  **已有实验性 native 名称 ACL 未自动迁移，不支持直接原地部署后宣称权限
  等价**。持久非零 slot 准入、用户／角色生命周期和 GIS 完整包安装仍未闭环。
  回归增加独立重载初始授权／零覆盖、私有变更及保存点、DROP 和 ID 重用拒绝，
  writer 覆盖扩展至 58 项，检查自动授权精确 SQL 键／权限／历史版本、遗留 ACL
  拒绝，以及全部五个写入失败位置。真实 DSO 测试明确验证旧名称授权不能使
  native 函数执行成功；这些仍是受控内核测试，不是实库提交或恢复证据。
  最终增量 `seekdb` 构建、catalog、58 项 writer、GIS 真实 DSO、综合
  `kernel_script.py`、4 项相关 CTest 及 `git diff --check` 均通过。综合回归的
  普通 PL fixture 已补充不可变完整 base routine 快照，以覆盖权限分流前的
  候选族检查；普通名称授权和事务回滚断言保持不变。未操作用户测试实例。
- 原生 SQL DCL 批量入口：`ObDDLService::grant` 对已解析 native 目标分流至
  `grant_native_routine`；`ObLocalManagementService::revoke_routine` 对已解析
  native 目标分流至 `revoke_native_routine`。普通函数继续走名称 ACL 路径，旧
  名称写入器的 `admit` 仍拒绝 native 对象，不能因为分流遗漏而扩大权限范围。
  新入口通过独立 `revalidate` 检查数据库、对象 ID／版本、绑定和精确输入签名
  或名称唯一性；身份检查不是授权，当前 actor／role 和转授权资格仍由事务内
  宿主 writer 检查。未绑定 actor 的请求不能进入新入口。
  REVOKE executor 将所有接收者放入一份有界、排序去重的请求并只调用一次 Root；
  batch 模式的旧 `user_id` 必须无效，不能退化为只撤销第一个用户。序列化保留
  整批 ID，解码在分配前检查数量上限，拒绝重复、乱序、零／非法 ID 和冲突的
  单用户表示；失败清空批次，旧请求复用 decoder 不继承前一批用户。
  GRANT 使用已有整组用户／host 列表，Root 在写入前解析全部接收者。native
  对象授权不自动创建用户、不顺带修改密码；缺失用户或带密码的请求在任何 ACL
  写入前失败，普通 MySQL 授权的用户管理行为不受此分流修改。
  两条路径共用 `execute_native_routine_privilege_transaction`，生产调用提供
  独占 `ObDDLSQLTransaction`：开启时锁定 DDL 操作流并检查当前 schema 版本，
  一个宿主 writer 处理整批权限／依赖变化，完成后只结束一次事务。只有成功且
  产生真实 ACL 变更的提交才调用 `publish_schema`；写入失败整体回滚，提交失败
  或结果不明确不自动重放、不提前发布，刷新失败也向用户返回错误。
  REVOKE 默认采用 RESTRICT，显式 CASCADE 使用已有完整授权图计划；GRANT 的
  WITH GRANT OPTION 映射为每项对象权限的 option，而不是名称级 GRANT 权限。
  新增 14 项事务边界回归，覆盖开启前后失败、写入／回滚失败、提交失败、异常、
  no-op 和发布失败，验证调用次数与顺序。这使用受控 transaction，不代表真实
  存储提交、锁竞争、恢复或未知提交结果已经验证。SQL 包内 DCL 的调用者事务
  适配、持久非零重载准入、owner／role 生命周期与 GIS 安装闭环仍需继续推进。
  GIS 回归通过 12 组原生 REVOKE 类型签名／option-only／行为组合的整批请求
  往返，新增 72 个非法批次以及超限数量／decoder 复用／自赋值检查。真实
  `ObRevokeExecutor` 在受控 Root 服务上验证两个用户只发出一次调用，成功和
  失败都不会拆分或重放，且调用持有 Root 串行锁；这不是 Root 存储提交测试。
  最终增量 `seekdb` 构建、14 项事务边界、58 项 routine writer、catalog（含
  43 项 GRANT／64 项 REVOKE writer）、GIS 真实 DSO、综合 `kernel_script.py`、
  4 项相关 CTest 与 `git diff --check` 均通过。未部署或操作用户测试实例。
- 原生重载持久化准入：新增 `NativeRoutineCreateSlot`，Root 在当前 schema／
  事务私有候选族中按输入类型签名判断重复，再选择当前最大 slot 的下一项；空族
  使用 slot 0。SQL 请求或插件提供的 slot 不作为分配依据；slot 只是存放位置，
  不是对象身份，不代替独立的 routine ID。拒绝重复输入签名、混入普通 PL 函数、
  损坏的重复 ID／slot、越界 slot 以及超出候选族上限的请求。
  直接 CREATE、Extension 安装及 UPDATE 中的 CREATE 共用该分配器，先分配
  slot，再预留 ID／schema 版本并进入私有视图。前面创建／删除的对象立即影响
  后续语句的候选族；保存点恢复后的分配也重新依据恢复后的可见状态。
  `RoutineIdReservation` 和 `RoutineVersionReservation` 对 native 函数同时绑定
  slot、原生种类及输入签名，reserve／take 之间改动这些字段会失败并消费凭证，
  不会隐式分配另一个 ID／版本。普通 PL 仍只支持 slot 0；native ALTER 的旧／
  新输入身份必须一致，返回类型不是重载身份，不允许借 ALTER 改写输入身份。
  移除 native writer 及 Extension UPDATE 的非零 slot 禁用；CREATE 的自动
  授权和 DROP 清理继续只操作对应对象 ID。UPDATE 的 native ALTER／DROP 权限
  检查携带确切 ID／版本，查找旧对象也不回落到 slot 0。
  普通函数 CREATE 判重改为检查整个候选族，不能在 slot 0 删除后混入一个普通
  PL 函数；旧的非原生 DROP 请求遇到原生候选要求重新解析，不能选中任一重载。
  新增 slot 分配／坏候选／事务视图回滚及原生预留凭证测试；58 项 writer 用例
  分别在 native slot 0 和 7 执行，共 116 项。GIS 声明测试改为让实际 Root
  分配器逐条放置全部 106 条声明，而不是由测试指定任意 slot。
  **这些接线与受控测试不是 GIS 实库安装成功的证据**。GIS 包交付、安装／更新／
  卸载及跨会话验证，SQL 包内 DCL 的调用者事务适配、owner／role 生命周期仍待完成。
  最终增量 `seekdb` 构建、新增 placement／reservation 测试、116 项 writer、
  catalog、GIS 106 条声明的 Root slot 分配及真实 DSO、综合 `kernel_script.py`、
  4 项相关 CTest 与 `git diff --check` 均通过。未操作用户测试实例。
  下一阶段 GIS 必须从全局 SQL 名称注册转为 implementation-only 描述符，由
  数据库中的 SQL Extension 对象发布名称；仅交付 control／SQL 文件不能实现
  “该数据库未安装扩展就不能解析这些 SQL 名称”的隔离要求。
- SQL REVOKE 选项传递：新增 `REVOKE GRANT OPTION FOR ... ON FUNCTION ...`
  及尾部 `RESTRICT`／`CASCADE`，支持现有精确函数类型签名。请求分别保存
  option-only 和默认／显式 RESTRICT／CASCADE，不把“撤销转授权”混同为
  `REVOKE GRANT OPTION` 的传统权限集合；省略行为时，后续 native 写入应使用
  RESTRICT。参照 [PostgreSQL REVOKE](https://www.postgresql.org/docs/18/sql-revoke.html)，
  option-only 保留普通权限，级联依赖计算由上述宿主写入器承担。
  新选项仅允许已解析的 native FUNCTION 和 EXECUTE／ALTER ROUTINE，其他对象
  不能忽略选项后继续执行；带新选项的未限定 slot 0 也提取精确对象 ACL 要求。
  序列化保留选项、对象身份及 actor／role；无效行为值或目标组合拒绝解码并清空
  native 请求状态，同时使请求本身无效，不能退化成合法的旧请求。旧请求复用
  解码器时不会继承之前的选项。解析选项使用独立子节点，避免被语句层的问号
  参数计数覆盖；问号检查仍保留。
  **目前仅完成解析、权限要求和请求传递，尚未开放 SQL 撤权。** Root 准入会先
  复核对象，再拒绝任何带新选项的请求进入旧按名称写入器；类型／非零 slot 的
  原有限制也保留。多收件人必须在接入宿主批量写入器后统一提交，不能沿用当前
  executor 逐收件人的旧调用循环来声称具备原子 CASCADE。
  GIS／真实内核回归已验证 12 组 option-only／行为／类型签名组合、普通与混合
  权限解析、精确对象权限要求、完整请求往返、84 个无效载荷、解码器复用、
  错误语法及原有表／过程／角色／ALL 撤权语法；Root admission 检查验证拒绝
  新选项落入旧写入路径。这些是解析／请求／准入证据，**不是 SQL REVOKE 提交
  或多收件人事务原子性证据**。
  最终重新生成 parser 并完成 `build_release` 的 `seekdb` 构建后，catalog、48 项
  writer、GIS 真实 DSO、综合 `kernel_script.py`、4 项相关 CTest 及
  `git diff --check` 均通过。未部署、重启、清理或重置用户测试实例；SQL DCL
  批量事务入口、owner／superuser 语义及 GIS 包的持久安装闭环仍待完成。
- 对象 ACL 差异写入器：`ObPrivSqlService::change_native_routine_privileges` 接收
  已获宿主授权的目标、grantor／grantee、权限动作及已预留版本。要求调用者事务
  已启动，在同一连接通过完整 `(routine_id, package_id)` 键锁定并复核当前
  routine 的数据库、owner、slot、版本、名称和 native 绑定，再锁读精确对象／
  grantor／grantee 的权限记录。宿主仍负责主体存在性与锁定、转授权资格、权限
  依赖以及版本预留；这个 SQL 服务不是权限绕过入口或事务协调器。
  EXECUTE／ALTER 的普通授权、带 GRANT OPTION 授权、撤销权限、仅撤销转授权
  各自做差异计算：普通 GRANT 不降低已有转授权，缺失权限的 REVOKE 为 no-op，
  未变化的权限不覆盖、不补历史。同一动作可以修改两项权限，但只生成一次
  schema 操作日志；当前表、历史表与日志均使用 `oceanbase` 限定名。
  before／after 权限位仅在全部成功后返回，读行／关闭结果／任一写入失败均
  向调用者传播，部分写入必须由调用者回滚；函数不自行提交、回滚或发布缓存。
  **该写入器尚未接到 SQL GRANT／REVOKE；native routine 自动授权已接通**，非零 slot 的
  入口限制仍保留；私有 ACL 日志／保存点及独立 DROP 清理的接线见下文，不能把
  受控 SQL 传输测试当作真实锁竞争、提交或恢复证据。
  受控 catalog 测试已验证 108 种权限组合及实际 SQL 主键／权限值、无变化时
  零写入、多权限变更仅一条操作日志、缺失／过期／重复 routine 行、重复／未知
  权限行、错误 option、两次读取各自的行读取／关闭错误，以及全部五个写入
  位置的失败传播。完整构建、GIS、38 项既有 writer、综合 `kernel_script.py`
  和 4 项 CTest 已通过；未部署、重启或重置用户实例。
- 私有对象 ACL 与执行入口：`RoutinePrivilegeOverlay` 增加按
  `(routine_id, grantee_id, grantor_id)` 索引的完整权限 after-image 和有界撤销日志。
  零权限是明确的覆盖记录，不能回退到该 grantor 的旧缓存；其他 grantor、其他
  重载以及当前有效的已启用角色仍分别贡献权限。记录校验 routine 的数据库、
  owner、slot 和版本，并核对连续变更的 before-image 与递增 ACL 版本；不接受
  孤立 GRANT OPTION 位、其他对象权限或过期事件。
  schema guard 在存在私有变更时按 grantor 读取缓存记录并合并覆盖；无私有变更
  保留原聚合快路径。所有 native slot 的执行检查均使用该视图，不再通过 slot 0
  的名称级授权通道。两个 guard 只有显式共享私有视图才会看到这些变更，不会发布
  到全局权限缓存。
  `RoutineCatalogSavepoint` 同时标记 schema、名称级权限和对象级权限日志；
  回滚无分配，废弃分支的后代标记不可恢复，退休状态也不会被回滚撤销。
  ACL SQL 写入器可接受该私有视图，只在当前表、历史表和操作日志均成功后记录
  after-image。视图记录失败同样向宿主传播，并要求宿主回滚已执行的 SQL；
  写入器不自行提交，也不会把局部成功当作提交。SQL no-op 可以保留事务读到的
  权限快照，但不因此写历史或操作日志。
  **SQL GRANT／REVOKE 尚未调用这条完整链路**；自动授权见上文，DROP 的独立清理见下文。这不是
  实库提交／跨会话隔离或恢复已经验证的声明，相关生命周期限制仍需后续解除。
  catalog fixture 已验证按 grantor 覆盖、角色授权撤销、独立 guard 隔离、before
  不匹配／版本重复／非法权限位拒绝、嵌套／废弃分支／析构回滚、schema 删除
  联动，以及写入器到私有视图的成功、no-op、失败和退休路径。GIS fixture 通过
  真实 DSO 复用同一 native SQL 表达式，验证私有撤销后拒绝、回滚后恢复，以及
  私有授权回滚后再次拒绝；这些是受控视图和表达式执行证据，不是 SQL 提交证据。
  完整 seekdb 构建、catalog（含 108 种写入组合）、GIS、38 项 writer、综合
  `kernel_script.py`、4 项相关 CTest 和 `git diff --check` 均通过，未操作用户实例。
- 原生 DROP 的事务内对象权限清理：`ObPLDDLOperator::drop_routine` 在删除
  native routine 元数据之前，锁定并复核精确 routine 行，再从同一事务读取该
  FUNCTION 对象 ID 下的全部 grantor／grantee／column 权限组，不依赖已提交缓存。
  因而事务内刚写入、尚未发布到缓存的授权也在清理范围内；`automatic_sp_privileges`
  关闭不会跳过对象权限清理。各组独立分配 schema 版本，写当前表删除、历史记录
  和对象权限删除操作日志，借用调用者事务，不自行提交。此前预留的函数删除版本
  可以小于权限删除版本，事务记录保留最大版本用于提交后的刷新。
  完整收集成功前不删除权限；缺失／过期 routine、无序或重复权限行、非法字段、
  结果关闭失败均拒绝继续。删除时找不到刚锁读的权限行也向调用者报错，不作为
  缓存过期忽略。任意部分写入失败仍须由调用者回滚 SQL 与私有视图。
  普通 PL 删除路径保留原流程，但对象类型改用 `get_object_type()`，不再把
  routine 类型枚举当作对象类型枚举。**这一步不开放非零 slot 的持久化生命周期，
  也不等于 SQL GRANT／REVOKE 或实库并发／恢复已验证。**
  Catalog fixture 验证完整权限分组、结果拥有权、16384 组上限、坏行和两次读取
  的失败路径；writer 扩展到 48 个场景，验证缓存中不存在的授权仍被删除、权限
  清理先于 routine 删除、版本分配／当前表／历史表失败停止后续删除，以及事务
  记录保留较高权限版本。完整构建、catalog、writer、GIS 真实 DSO、综合
  `kernel_script.py`、4 项相关 CTest 和 `git diff --check` 已通过；未部署、重启
  或重置用户实例。
- 原生可变参数接线采用 `VARIADIC points GEOMETRY[]`，保留“一个末尾数组形参”
  的 catalog 身份，以参数 flag 标识数组语义、`param_type` 保存元素类型；不会为
  每种调用参数数目创建或改写一个 routine。SQL 匹配及代码生成对实际展开实参
  重复应用元素类型，host 按稳定 C ABI 的参数向量传给实现，不传 PG 内部数组地址。
  CREATE／Root 验证整个实现参数范围中的元素签名，防止只检查第一个元素后
  错绑不同类型的尾参数；验证期间 generation／epoch 必须一致。
  这阶段接入普通展开调用（可带固定前缀），要求至少一个可变元素，总实参不超过
  现有 SPI 的 1024 个上限，并受具体实现的 arity 范围约束（例如当前 GIS
  LineString 实现最多 64 个元素）。只允许末尾一个 IN
  可变参数；普通 PL、可变参数默认值、在其前面使用默认参数均暂不接纳。
  参考 [PG 可变参数规则](https://www.postgresql.org/docs/18/xfunc-sql.html#XFUNC-SQL-VARIADIC-FUNCTIONS)。
  **不等于完整 PG 数组语义**：直接传数组的 `f(VARIADIC array_value)`、空数组、
  命名数组参数及通用数组类型／codec 仍未接通；不把已有 C ABI 的 VARIADIC
  descriptor 直接宣称为这些 SQL 能力。该语法在 PL parser 中将 `VARIADIC`
  作为关键字，原来以此命名的未加引号标识符需要加反引号。
  完整 `seekdb` 编译、原生声明和 catalog fixture 已通过；真实 GIS DSO 的
  标量展开调用已验证 2／3／64 个点、固定前缀、NULL、空实参和超出实现上限的
  拒绝路径，以及 wire／SHOW CREATE 往返。批量调用另测返回 LineString 的精确
  坐标字节、跳过行、NULL 行与缓存行不重复求值；这些是受控 schema fixture，
  不是真实数据库安装回归。
- 插件构建中的 CREATE resolver 已接通原生声明，要求 `SUPER` 和原有
  `CREATE ROUTINE` 权限，按声明签名预检已加载模块；不会进入 PL 函数体编译，
  `route_sql` 为空。Root / caller catalog 写入端也检查原生创建权限。
  非插件构建仍明确返回不支持；解析成功不代表已经持久化创建成功。
- 新增原生 routine → 模块实现的持久依赖入口，消费者是稳定的 `routine.<id>`，
  不使用 SQL 别名或临时 runtime generation 充当函数身份。新增依赖先锁定模块包，
  检查 ACTIVE 状态、持久 generation、scalar implementation 和模块变更屏障；
  复用现有 `RESTRICT` 阻断记录。
- routine writer 的创建、重新绑定、删除，以及绕过该 writer 的 `DROP DATABASE`
  清理路径，均将依赖操作放进原有 schema 事务，不开启/提交额外事务。相同绑定的
  属性修改保留原依赖；换绑定先登记新目标，再删除旧目标。删除按持久边上的代次
  清理，不要求模块仍 ACTIVE，也不加载代码。
- 上述依赖 SQL 明确使用 `oceanbase.*` 系统表，不能依赖借用会话的当前数据库。
  模块状态/代次读取失败、依赖损坏或登记失败均返回给外层事务，不吞掉错误。
  Root writer 在 routine DML 前通过真实 SQL 检查当前表和历史表的新列是否存在，
  重新检查原生签名，再将验证代次与持久模块行锁下的 generation 对比；模块在验证
  后换代时拒绝写入。相同绑定但参数、结果或确定性改变时也须重新验证，不能当成
  注释等属性修改跳过。该准入不替代系统表升级或模块升级的兼容协议。

本段验证由 `native_function_declaration.py --build-dir <已配置并有基础产物的目录>`
重编真实 parser/lexer、关键字表与 routine resolver（包括所属 unity object），并链接
已有支持库；测试解析、标识所有权、拒绝路径、普通 PL/SQL 语法及 CREATE guard。
这是有界代码 fixture，不是真实数据库安装回归。
修改 `ObRoutineInfo` 布局后必须先完整构建 `seekdb`，不能把新头文件的测试对象与
旧 schema 库混合链接。上述 runner 的 `--catalog-only` 模式另测真实 schema
复制/旧 wire 读取/事务 overlay 生命周期、schema reader、当前表与历史表 DML
及生成的表列；SQL transport 是受控 fixture，并不证明数据库事务和恢复已通过。
`plugin_registry_resolution` / `plugin_loader_registration_gis` 另验精确 ID/owner、
重载不改绑、类型/元数拒绝、停用/换代，以及真实 GIS DSO 的通用调用。

2026-09-22 本阶段验证：`build_plugin_overlay_verify` 的完整 `seekdb` 构建通过；
原生声明与 catalog 两种 fixture 模式通过；6 项 registry/loader、20 项 catalog
服务器测试脚本的单元测试及 7 项生成 schema 清单测试通过，依赖边界检查通过。
20 项是**测试脚本本身**的受控单元测试，不是运行 `query_catalog_server.py` 的实库回归。

同日原生执行桥验证：完整 `seekdb` 构建及上述声明/catalog fixture 再次通过；
`gis_sql.py` 在真实代码生成器、执行器和 GIS DSO 上验证了原生 `area_alias` routine
的 scalar/batch × invoker/definer 四种组合，包括返回 12、NULL、授权/撤销、参数
求值身份、跨库上下文恢复、错误传播、schema 版本拒绝、绑定序列化与深拷贝。
既有 GIS SQL/LOB 表达式回归也通过。
首轮由 fixture 提供 routine schema 和已定型 UDF 表达式；SQL 名称解析及 PL
调用者依赖分析的覆盖在下面的后续验证中补充，不能据此宣称 GIS 已完成安装迁移。

后续同日验证已扩展 `gis_sql.py`：现在正向 UDF 来自真实 `SELECT` 解析，而非手工
定型；未经参数替换的 `SELECT native_db.area_alias(ST_GeomFromText(...))` 已经过
代码生成和实际 GIS DSO 执行，结果为 12。另保留受控列输入检查批量/权限上下文。
PL router 的调用者函数体分析与函数依赖记录也通过。该测试发现并修复了原生形参
使用 `core.type.geometry`、嵌套 GIS 函数返回 `org.seekdb.gis.geometry` 时错误拒绝
的衔接问题，同时补测任意自定义类型和存储类型不得被冒充为内建参数。
完整 `seekdb` 构建、GIS fixture、8 项 registry/loader/清单/脚本测试入口通过。
**catalog schema、授权与输入仍由 fixture 提供；这轮验证尚未覆盖原生 CREATE、
PL 字节码执行、实库安装或恢复。** 后续 CREATE 准入和 SQL 包解析证据见下文；
模块版本变更兼容检查及实库安装仍待闭环，不应手工向系统表写记录。

同日持久依赖接入验证：完整主程序构建、catalog fixture、GIS fixture 和上述 8 项
回归入口通过。catalog fixture 检查 ACTIVE/代次/实现身份、错误传递、停用后的
边清理及 `RESTRICT` 阻断，并用真实 SQL parser 检查该事务路径的系统表引用均
限定在 `oceanbase` 数据库。
新增 `--writer-only` 复用综合测试中的 routine writer fixture，独立通过 30 个场景
（其中 11 个新增原生场景），覆盖创建/删除/换绑定、相同绑定保留、失败返回、
事务借用及回滚通知。独立入口不构建无关辅助 DSO，不替代完整综合测试；
SQL transport、模块依赖响应与版本分配均受控，**不证明实库事务隔离、删库或恢复**。
完整 `kernel_script.py` 首轮在辅助 Rust candidate DSO 的链接阶段遇到链接器
SIGSEGV，未运行到综合断言；未修改工具链配置、使用原命令复跑后通过。独立
writer 测试与综合测试复用相同场景，不把绕过辅助构建等同于综合测试通过。

同日继续接通原生 CREATE 与包声明路径：writer 场景扩展为 38 个，补充当前表／
历史表缺列、无效代次、结果类型不匹配、相同实现的签名／确定性变化，以及验证后
模块换代的失败路径。依赖登记允许成功 INSERT 的零影响行结果作为幂等重验，但
不会吞掉数据库返回的唯一键冲突或其他写入错误。

GIS fixture 的 routine 现在来自真实 `CREATE FUNCTION` resolver：拒绝非 SUPER
和错误返回类型，正确声明通过且没有 PL 转发体。另通过 SQL 包逐条 resolver 生成
拥有字符串的 wire operation；销毁脚本源后仍可暂存到受控 schema、解析 SQL 名称、
记录独立 routine ID 并调用真实 GIS DSO，面积结果为 12。失败的包解析序列不允许
提升权限后原地重试。`SHOW CREATE FUNCTION` 走真实 schema printer 并重新解析，
输出采用 catalog 的绑定而非可能陈旧的源文；native/PL 声明不一致返回错误。
这些测试仍使用受控 catalog 和授权，**不证明 CREATE EXTENSION 已提交成功、
安装事务已回滚或重启恢复通过**。GIS 公共 SQL 名字仍由模块级注册发布，尚未提供
`gis.control` / `gis--1.0.sql` 的正式交付；后续迁移源进展见下文，不能把测试中的
`packaged_area` 当成已交付 GIS 包。
本轮完整 `seekdb` 构建、原生声明/catalog fixture、GIS fixture、综合
`kernel_script.py` 和上述 8 项 registry/loader/清单/脚本入口全部通过；新文件
license header 与 `git diff --check` 也通过。没有部署、重启或重置实库。

随后补充实现注册与 SQL 发布分离：`IMPLEMENTATION_ONLY` 的 registry 测试覆盖
SQL 名称不可见、同名签名不冲突、对象 ID 仍唯一、生命周期清单保留、停用与换代；
C 参考 DSO 的直接／snapshot 注册测试验证有标签与无标签实现的标量／批量调用
均返回 42，过期 epoch 拒绝调用且释放租约。通用表达式额外信息测试覆盖空标签的
序列化往返，普通 SQL 函数仍拒绝空名称。完整主程序构建、GIS fixture、综合
kernel fixture 和上述 8 项专项测试通过。
SDK 默认 LLD 的两次全量运行分别在 `query_mutation`、`custom_executor` 链接时
崩溃；默认配置的 11 项 schema 单测通过。仅在临时输出目录切换 BFD 后，SDK 全量
166 项测试通过，格式检查通过；没有修改项目工具链配置，也不把 BFD 通过写成
默认 LLD 问题已解决。以上仍不等于 GIS SQL 包的实库安装／恢复验证。

随后交付 `plugins/sql_packages/native_math/` 参考包，包含 control、1.0 安装脚本
和 1.0 → 1.1 更新脚本。实际 CMake plugins 安装组件将三份文件平铺交付；真实
package reader、更新路径及 kernel parser 测试通过。两个数据库级 SQL 函数
分别绑定有标签／无标签的 implementation-only C 实现，更新增加共享实现的新名字
并保留旧 routine ID，不使用 RETURN 转发体。此包用于验证公共机制，**不是 GIS 包**。

新增 `native_extension_server.py`，准备验证安装／更新／卸载、数据库隔离、
routine ID 与 Extension membership、原生实现依赖、NULL 与 EXECUTE 授权、
SHOW CREATE、预处理调用以及删库清理。11 项离线自测验证脚本的错误码断言、
身份检查、连接参数、失败保留和凭据不输出；它们不代替实库回归。
补测同时发现参考 C 回调未处理无 NULL 自动传播标志的输入，已修复；真实 DSO
direct／snapshot 两种 loader 测试先复现失败，修复后标量及批量 NULL 检查通过。

本轮独立实例完成初始化但未能启动 SQL 监听：当前沙箱禁止 TCP socket 创建及
Unix socket bind，进程退出。因此 **CREATE EXTENSION native_math 的实库测试
仍未执行成功**，不能宣称事务提交、恢复或安装闭环通过。没有重启或重置用户的
`/data/wangzelin.wzl/test` 实例；独立尝试的诊断日志与测试数据保留。

原生默认值阶段已通过完整 seekdb 构建、声明 grammar/admission fixture、综合
kernel fixture，以及真实 GIS DSO 执行测试。测试创建 `default_point(x DOUBLE
DEFAULT -3.5, y DOUBLE DEFAULT 4e0)`，经过参数 wire、拥有数据的 schema overlay、
SHOW CREATE、正常 SELECT resolver 与 codegen，验证传入零／一个实参、显式覆盖、
显式 NULL 和错误参数个数；拒绝错误默认顺序、表达式引用、数字 GEOMETRY 默认值
及普通 PL 默认参数。wire 不包含最终 schema version，夹具与 Root 的实际职责一致，
在 admission 后单独分配受控版本，不将缺失版本视为可执行 catalog。
参考包 `native_increment` 同时加入 `DEFAULT 41`，实库脚本检查空参结果 42 与
显式 NULL 的区别。脚本离线自测增至 12 项；这仍不是持久安装或恢复测试。

GIS 全量声明源现位于 `plugins/gis/sql/`：control 与 1.0 SQL 覆盖当前模块的
82 个 SQL 名字、106 条原生声明。可选实参通过同名重载表达，集合使用展开式
VARIADIC；不会用 DEFAULT NULL 改变“省略”和“显式 NULL”的区别。SQL 别名
绑定规范实现 ID，而不是再创建一个转发函数或依赖 `.alias.*` 的代码对象。
真实 control 读取／替换、SQL parser、逐条 CREATE resolver、GIS DSO 精确签名
校验及 routine wire 已通过；8 项离线清单检查覆盖缺少／重复元数、元素类型、
遗漏名字及错误实现绑定。随后增加了 106 条声明在同一事务私有 schema 视图中
按 82 个名字合并候选的验证；仍**不是在持久 catalog 中同时安装重载**。
在该迁移源阶段，这些文件尚未加入 CMake 安装，`CREATE EXTENSION gis` 不可用。发布前须接通
standalone 重载身份、按签名查找／DDL、对象级权限和安装事务，不能仅将同名
声明加入文件后就当作安装闭环完成。其后的持久化准入及 C 阶段已继续推进；
新增或修改 GIS descriptor 必须同步 SQL 源。

```sh
cmake --build build_plugin_overlay_verify --target seekdb -j12
python3 rust/plugin-runtime/tests/native_function_declaration.py --build-dir build_plugin_overlay_verify
python3 rust/plugin-runtime/tests/native_function_declaration.py --build-dir build_plugin_overlay_verify --catalog-only
python3 rust/plugin-runtime/tests/native_function_declaration.py --build-dir build_plugin_overlay_verify --writer-only
python3 rust/plugin-runtime/tests/gis_sql.py --build-dir build_plugin_overlay_verify
```

完整构建同时修正了原生 AST 与 `FLUSH PRIVILEGES` 的编号冲突；native AST 使用
独立编号 4927。catalog 测试直接包含 schema reader 时暴露的头文件函数重复定义
也已修复。测试未部署/重启服务器，也未删除或重置已有数据。

仍需完成：

完成 parser、持久 schema、权限、Root DDL、依赖、通用调用及缓存失效，以一个
示例包验证：未安装不可见、安装后可用、其他数据库不可见、授权检查、重启恢复、
卸载后旧计划不能调用。此阶段不以 SQL wrapper 替代直接绑定。

### C. GIS 切换（代码及交付已切换，实库闭环待验证）

GIS 函数描述符统一带 `IMPLEMENTATION_ONLY`，SQL 名称不再从模块注册表全局
解析。`gis.control` 与 `gis--1.0.sql` 通过 CMake `plugins` component 平铺交付，
不会因此把可选 GIS DSO 拉进 core 的默认构建。类型／cast 的 ABI 支撑注册暂仍
属于模块；本次切换的是 82 个公共函数名及其 106 条 routine 声明。

GIS fixture 读取真实包，逐条执行 CREATE resolver、精确 DSO 准入及 Root slot
分配，再用受控 ID、schema overlay 和对象 EXECUTE 授权运行真实 SELECT／原生
UDF。原来依赖全局名字的距离、面积、WKT/WKB、LOB、大字符串、别名和批量用例
均迁到此路径；原生调用 fixture 的辅助 POINT 等也来自数据库声明，删除测试专用
的“隐藏模块名称”开关。空数据库拒绝未限定名，显式限定已安装数据库则可调用。
NULL 回归区分参数求值与算法调用：`ST_Distance(NULL, POINT(...))` 可以求值
POINT，但不得执行 distance 算法。

本阶段验证通过：完整 `seekdb` 构建；基于新构建链接的 `gis_sql.py`（包括
106 条 implementation-only 绑定、真实 SQL/LOB 及 1,031 行 batch）；
`kernel_script.py` 综合回归；GIS loader、query/native runner、声明清单和
package layout 共 5 项 CTest；`git diff --check`。这些测试没有启动服务器，
数据库状态和授权由 fixture 提供，不代表实库事务提交／恢复通过。

随后补充 `gis_extension_server.py` 实库验收入口：管理员在已配置的隔离实例
上显式确认后运行，脚本不负责部署或重启。覆盖全部 106 个 routine 的实现绑定、
新安装 slot、成员、依赖，两个数据库独立安装和跨会话可见性；2D／3D 重载独立
EXECUTE、预处理语句撤权、DROP EXTENSION／重装的新身份和旧授权失效，及
DROP DATABASE 的 routine／成员／依赖／live 对象 ACL 清理。失败保留已确认
与尝试创建的名字，不自动重试或清理未知结果；成功只删除本轮随机命名的资源。
`test_gis_extension_server.py` 是脚本保护机制与 SQL 顺序的离线测试，不能当作
上述实库行为的证据。当前环境对 AF_INET 与 AF_UNIX socket 创建均返回
`EPERM`，未启动测试服务器、未连接用户实例；实库验收仍然待执行。
本轮脚本 10 项离线自检（含 `python -O`）、6 项相关 CTest 和
`git diff --check` 通过；没有 C++／Rust 生产代码改动，不重复声称实库通过。

这一源码切换先在实验分支完成，不等于已有服务器可以直接替换 DSO。发布门槛
仍是 B/C 实库回归：安装／提交、预处理及缓存、卸载、失败回滚与重启恢复。
生成列与空间索引组合也不能由标量／批量用例推定支持；不得悄悄回退旧通道。

#### GIS 转换数值检查

闭环检查在真实 DSO 路径复现了 `ST_Transform` 的数值缺陷：4326 的点 (2,49)
投影至 3857，旧实现 Y 约 5279943.671 m，正确值约 6274861.394 m。插件中的
短阶正切／指数近似已替换为标准 Web Mercator 公式；正向使用对称的
asinh(tan(latitude))，反向指数参数保持非正，避免巨大 northing 导致指数溢出。
不再把高纬度静默截到瓦片边界，正向两极／越界纬度和非有限结果拒绝输出。
未实现的非同 SRID 转换现在报错，不再只改 SRID 标签伪装转换成功。

17 个真实 DSO 控制点／错误用例通过，覆盖双向独立参考值、南北半球、高纬度、
PointZ、极点、溢出和未知转换；SQL／LOB 回归覆盖点、polygon ring、集合递归、
NULL 和错误传播，6 项相关 CTest 通过。生产 GIS DSO 构建和边界审计通过，
没有引入内核 GIS 调用或新的第三方算法库。当前只实现 4326↔3857 与同 SRID
恒等转换，不代表任意 EPSG、一般投影数据库或实库安装验证完成。

#### GIS 完整算法后端缺口

原 `gis_topology_probe.cpp` 直接编译 `geometry_engine.cpp`，没有另造算法模型，
迁移前运行退出码为 1，四项都失败；方案 2 首批修复后四项均通过：

| 用例 | 迁移前结果 | 修复后结果 |
| --- | --- | --- |
| (0,0)→(2,2) 与 (0,2)→(2,0) 两条线是否相等 | true | false |
| (0,0)→(2,2) 与 (0,1)→(1,2) 两条线是否相交 | true | false |
| (1,1) 到线段 (0,0)→(2,0) 的距离 | √2 | 1 |
| [0,1]×[0,1] 与 [2,3]×[0,1] 两个正方形的并集面积 | 3 | 2 |

原因为 relation_result 比较包围盒、geometry_distance 只比较顶点、
combine_rectangles 的 UNION 返回外包矩形；这三条近似路径现已移除。
buffer 的扩大包围盒路径也已替换为原 Boost 策略算法。
这些不是数值容差问题，不能靠修几个控制点或把错误期望写进测试解决。
探针已转为 `plugin_gis_topology` CTest，并增加孔洞、边界、非矩形、Multi 和
C ABI 错误／emit 检查；SQL／LOB 测试也覆盖原四例。不能解读为完整 GIS 迁移完成。

原内核 `src/share/geo` 已有 Boost.Geometry 的完整类型／算法分派。下一步存在
两条会影响交付与兼容性的路线；用户已选择第一条（方案 2）：

- 抽取原 Boost.Geometry 算法到插件私有后端，解耦内核 allocator、SRS 和执行上下文，
  优先保持现有 SeekDB 语义；不能仅把私有内核调用搬到 DSO 中。
- 以 GEOS 的可重入 C API 提供拓扑后端，另以 PROJ 支撑一般投影，均作为 GIS
  插件依赖而非 core 必需依赖。更接近 PostGIS 的算法组合，但新增依赖、打包、
  坐标维度／地理坐标兼容与回归工作。当前环境未检测到 GEOS 开发包。

参考 [GEOS C API](https://libgeos.org/usage/c_api/) 的稳定 ABI、显式资源释放与
线程上下文约定。两条路线都必须覆盖孔洞、退化／空几何、集合、SRID、Z、
错误传播、取消和内存生命周期，并重新完成 SQL 包与真实存储验收。
首批通过 `include/seekdb/geo/cartesian_algorithms.hpp` 抽取原无显式策略的
equals/intersects/distance/union 调用；原内核适配器和插件共用该入口。
插件私有适配器使用项目自带 Boost 头文件构造拥有内存的几何模型，不链接内核
GIS 库，也不回调 ObExpr。SQL 包、Rust runtime 和 C ABI 不需要更换。

当前支持 2D、SRID 0/3857 的点线面及对应 Multi 关系／距离，以及面／Multi 面
并集、差集和对称差。孔洞按 ring 位置保留，绕序规范化。未知／地理 SRID、Z、
GeometryCollection、混合维度叠加、无效几何和空几何距离暂时报错，不返回近似值。
这不是完整兼容契约；需要继续抽取原 SRS、集合、空值与错误语义。
异常在插件 C 边界转为状态码，准入失败不 emit，结果回调失败不重试。
没有引入 GEOS/PROJ 或 Rust GIS 依赖，也没有部署、重启或修改测试实例。

第二批复用原 area/length/centroid/buffer 的无显式地理策略算法入口。
质心按原 `ob_geo_gc_split` 的维度分组，优先面再线再点，使用面积／长度权重，
空质心发出 SQL NULL；面积扣除孔洞而不依赖输入环绕序。Buffer 采用原默认参数
及 8 种策略组合，支持负距离收缩、收缩至空集合、混合集合缓冲后的并集。
`ST_Buffer_Strategy` 不再产生无效占位数据，而使用原 12 字节 type/value 编码，
重复类别和不适用于输入类型的策略会报错。PG 文本／quad-segment 参数尚未迁移。

第三批抽取原 `ObGeoInteriorPointVisitor` 的扫描线和候选顶点算法到
`include/seekdb/geo/interior_point.hpp`，内核和插件共用，分别保留各自的内存容器。
`_ST_PointOnSurface` 不再借用质心：面选择避开孔洞的最宽内部区间，线优先内部顶点，
多点选择真实成员；混合集合按最高非空维度处理。退化面保留原首顶点回退，
空输入返回空几何集合（区别于 Centroid 的 SQL NULL）。仍限 2D、SRID 0/3857；
非有限质心／距离计算报错，不能伪装为空结果。
内部点拓扑回归、4 项 GIS CTest、生产 DSO 边界审计、真实 SQL/LOB fixture
（新增 8 条内部点 SQL）通过；原内核 visitor 使用生产编译参数完成语法检查。
这些验证没有启动服务器，也不构成实库安装或原实现全面等价的证据。

第四批复用 `ObGeoFuncIsValid` 的 Cartesian 拓扑谓词和 `ObGeoFuncCorrect` 的
规范化入口，替换原来只检查点数／闭环的有效性判断。遵循原默认 build 流程，
检查前规范化环绕序并补闭环，不修改调用方数据。自交、外部孔洞、孔洞重叠、
退化线面和 MultiPolygon 成员重叠返回 false；GeometryCollection 逐成员检查，
不能错误地要求其成员互不重叠。编码结构错误、暂不支持的 SRS／维度仍报错。
此阶段仍限 2D、SRID 0/3857，不会再因为闭环而把自交结果冒充有效几何。
已通过拓扑/C ABI 回归、4 项 GIS CTest 和真实 SQL/LOB fixture（新增 11 条
有效性 SQL）；原内核 IsValid/Correct 通过生产参数语法检查，生产插件通过
二进制边界审计。集合中的无效前缀不会掩盖后续成员的结构错误。

第五批通过 `include/seekdb/geo/polygon_repair.hpp` 抽取原单面 dissolution 和
`make_valid_polygon_inner` 的孔洞分类／叠加流程，供内核及插件共同使用。
`_ST_MakeValid` 不再只补闭环：自交外壳按原逆序环对称差分解；相交孔洞先合并后
与外壳做对称差，外部孔洞作为独立面合并；无效 MultiPolygon 逐面修复再求并集。
内核临时面使用 arena 生命周期，插件使用拥有内存的模型，公共辅助函数不依赖
SQL、私有内核类型或 allocator；结果保留 SRID，不修改调用者输入。

这里保留的是 SeekDB 原语义，不是擅自换成 PostGIS 的修复契约：有效集合可直接
返回，无效集合不递归进行多边形分解；完全退化面分解为空时保留规范化输入，
因此不能承诺 MakeValid 后 IsValid 总为 true。非多边形无法纠正时按原路径报错。
一般 SRS/Z 和原内核／插件差分验收仍未完成。
修复阶段已通过拓扑/C ABI 回归、4 项 GIS CTest、真实 SQL/LOB fixture（新增
15 条修复 SQL，含 NULL 与预期错误）；原 dissolution／GeoExprUtils 使用生产
参数完成语法检查，生产 DSO 通过二进制边界审计。没有部署或重启测试实例。

第六批将原 `ObGeoBoxClipVisitor` 的线段求交、沿矩形顺时针连接片段、孔洞
归属流程抽到 `include/seekdb/geo/box_clip.hpp`。内核通过 arena factory、插件
通过拥有内存的 Boost/STL factory 共用算法，删除插件按包围盒返回矩形的替代实现。
`_ST_ClipByBox2D` 保留线／面形状和原结果类型简化规则；空裁剪范围返回 NULL，
不相交返回空集合。原“全部包含”快速路径保留边界几何；局部相交 visitor
排除只落在边上的点／线段。这两个分支的区别需要保留，不能统一为 clamp。
第二参数提供坐标范围而非 CRS 转换，结果保留第一参数 SRID；仍限 2D、0/3857。
Box clip 阶段已通过拓扑/C ABI 回归（含 36 组独立 overlay 面积对照、孔洞归属
失败传播）、4 项 GIS CTest 和真实 SQL/LOB fixture 的 17 条裁剪 SQL；原 visitor
通过生产参数语法检查，生产插件通过边界审计。未启动或修改测试实例，也尚无
完整原实现差分或一般 SRS 的验收结论。

第七批接入 `_ST_AsMVTGeom` 的 Cartesian 流水线：选择最高维几何、瓦片仿射
变换（翻转 Y）、rint 网格吸附、相邻点去重、零容差共线简化、真实矩形裁剪、
多边形修复、二次网格吸附（面用 floor）。仿射／网格／零容差简化抽到
`include/seekdb/geo/tile_grid.hpp`，原内核 visitor 与插件共用；不再逐点 clamp。
保留小线预过滤、空／退化结果为 SQL NULL、原 SRID、Multi 单元素简化。
MVT descriptor 不再整体 NULL 传播：第一参数 NULL 返回 NULL，bounds NULL
报错，可选参数 NULL 取 4096／256／true。extent／buffer 按原 32 位整数值域
校验，clip 按有符号 8 位校验；当前 SQL 包仍以 DOUBLE 声明可选控制参数，
原解析器的类型限制／字符串转换／错误码完全对齐尚待审计，不能宣称完全等价。
一般 SRS、原实现差分与实库验收仍未完成。
本批验证：拓扑／C ABI 回归和 4 项 GIS CTest 全部通过；真实 SQL/LOB fixture
通过 28 条 MVT 正常、NULL 与预期错误用例，含 NULL 输入仍校验非法控制参数。
覆盖正反环、真实线段求交、孔洞裁开、凹面分裂、rint／floor、退化结果和回调
错误传播。三个原内核 visitor 通过生产参数语法检查，生产插件通过构建和边界
审计。测试仅使用受控 catalog／storage fixture，未部署、重启或修改实库实例。

第八批迁移 `_ST_GeoHash`：将原 `calc_precision`／`calc_geohash` 抽到
`include/seekdb/geo/geohash.hpp`，内核表达式与插件共用。改为 PG Box 模式的
包围盒中心编码（多边形不计内环），去掉代表点、固定默认 12 位、精度最多
32 位及坐标 clamp。省略／NULL／非正精度自动选择；点为 20 位，跨首级分割
范围可返回非 NULL 空字符串；空几何返回 SQL NULL。保留 32 位有符号精度
范围，插件另有 16 MiB 输出分配预算，超预算报 NO_MEMORY，不截断结果。
精度 ABI 改为 `core.type.int64`，SQL 仍声明 BIGINT；NULL 策略由插件按参数处理。
替换 DSO 后需要重新建立实验性 SQL 绑定，不能沿用旧无符号精度载体缓存。
原 GeoHash 以 NULL SRS 构造 geometry，因此这里取原始 XY、忽略 Z，不变换
坐标或交换轴。一般 SRS 存在性查询尚未接入，当前已知 ID 为 0／4326／3857。
含空成员的非空集合在原 Box 路径存在未初始化／空迭代器行为，本批明确拒绝，
不伪造原点或跳过成员；解析器转换、错误码和原实库差分仍待验收。
本批验证：1,200 组独立整数分箱编码对照、120 组自动精度公共前缀对照均无
差异；拓扑／C ABI 回归和 4 项 GIS CTest 全部通过。真实 SQL/LOB fixture
通过 27 条 GeoHash 正常、NULL、负精度、3D 和预期错误用例；原内核表达式
通过生产参数语法检查，生产 DSO 通过构建及边界审计。未部署或重启测试实例，
以上仍不是完整原实库差分验收。

第九批迁移 `_ST_BestSRID`：将原 `ObGeoBoxUtil` 的单位球三维包围盒、线段
大圆极值、极点扩展、范围中心／角宽高以及 `get_box_bestsrid` 的投影选择
抽到 `include/seekdb/geo/geographic_box.hpp`，内核与插件共用。删除插件的
SRID 回显／不匹配就返回 3857 的占位逻辑；保留原 polar Lambert／UTM／LAEA／
world Mercator 选择顺序，返回原 999xxx 私有 PG 投影 ID，而非直接返回 EPSG。
这些投影还需要接到通用 transform／SRS 服务，不能以 ID 选择成功宣称转换完成。
抽取时同步修正内核的 Z 点积项、反向同点判断、覆盖 X 却未初始化 Y、忽略
对跖线段错误等缺陷；不复制未定义行为。角度计算限制浮点误差的反三角函数
输入域，并明确 XY 轴退化角点的结果。两参数都为空返回 999000，一个为空
使用另一个的范围，同时修复原表达式只根据最后参数判断空几何的问题。
当前非空输入限已知地理 SRS 4326 的原始 XY 度，允许 Z 但按原流程忽略；
投影坐标、未知 SRS、越界坐标、对跖线段及未定义的嵌套空 Box 路径报错，
不发布部分结果。一般 SRS／轴序／单位、SQL 转换与错误码、原实库差分仍待验收。
本批验证：200 条线段的 102,600 个独立大圆采样点均在计算包围盒内，端点反向
及极值紧致度对照通过；解析极值、日期变更线、投影选择阈值和 C ABI 失败
传播回归通过。4 项 GIS CTest、真实 SQL/LOB fixture 的 22 条 BestSRID 用例
通过；相关内核工具／Box dispatcher／表达式通过生产参数语法检查，插件通过
构建及边界审计。修复后的共用算法已有上述证据，但没有实库全量 GIS 差分或
通用 SRS 验收结论；未部署、重启或修改测试实例。

第十批先修正空间索引的标量边界：原 `ObExprSpatialCellid` 和
`ObExprSpatialMbr` 本来就是不计算参数、只返回 NULL 的内部占位表达式。
真正的 cell 由 `ObDASDomainUtils::generate_spatial_index_rows` 通过
`ObS2Adapter` 为一个几何生成多行；查询范围还需要祖先去重、子 cell 范围、
查询专用 covering 和 MBR 过滤。因此删除插件的代表点／Morton 编码和普通
32 字节包围盒输出，恢复内部表达式与 SQL 包兼容入口的 NULL 语义。
SQL 包入口仍遵循普通 native routine 的参数求值规则，不承诺跳过嵌套函数；
内部占位表达式则不求值参数，也不依赖插件加载。NULL 占位不表示空间索引可用。
这两个非限定名仍保留为内部索引生成列的内置名称；验证 SQL 包中的兼容 routine
需要使用数据库限定名，并检查实际 DSO 调用计数，防止只测到内置占位入口。
本批验证：42 项 C ABI 检查覆盖 NULL 类型／空载荷、结构与参数校验、回调错误
以及异常隔离；真实 SQL/LOB fixture 的 10 条相关 SQL（限定 routine 和非限定
内置入口）及新旧引擎内部占位测试通过，4 项 GIS CTest 全部通过。插件构建、
二进制与源码边界检查通过，内核适配对象已重新编译并完成静态库及可执行文件
链接。没有空间索引写入／扫描的实库验收结论；未部署或重启测试实例。

S2 迁移必须作为独立索引服务贯通写入、查询和过滤，不把一个几何压缩为
单个标量 cell：复用原 region coverer 参数、投影坐标到 S2 face 的映射、
超边界 sentinel 与修正路径、地理经度环绕区间，以及原 point/non-point MBR
布局；跨 C ABI 返回受生命周期保护的 cell／ancestor 集合和 MBR，内核只负责
行展开与范围拼装。原 `SEEKDB_ENABLE_CORE_GIS=OFF` 的 DAS 写入、范围生成、
MBR 扫描过滤仍保持显式不支持，不能只打开其中一个入口就宣称索引迁移完成。

第十一批完成独立 S2 covering 后端的第一段：新增
`org.seekdb.gis.index.cover` 租约服务与 `spatial_index_spi.h` 字节协议，
输入为几何及已解析的 SRS 元数据，输出为覆盖 cell、可选去重祖先／原顶点
cell 集合和 MBR。不把该服务注册成 SQL 标量函数，也不冒充完整索引 AM。
投影边界由宿主提供，不硬编码 SRID；地理输入要求宿主已完成度单位／轴序
归一化。Z 按原索引流程忽略，地理缓冲使用宿主换算后的弧度，投影缓冲须先做。
结果有界、由宿主在回调中复制，不跨 ABI 暴露 S2/STL 对象或插件指针。

原 visitor 的逐 region covering/union、4/50-cell 参数、full-range 回退、
祖先／顶点集合、MBR 扩张和 face-0 ST/UV 映射抽到 `s2_covering.hpp`，
内核与插件共用；插件保留 1% 内缩边界、原 envelope 相交后重试和 UINT64_MAX
sentinel。查询窗口显式选择 50 cells，写入、covered-by 和地理距离缓冲使用
4 cells，与原三个 adapter 构造分支一致；不是所有查询都统一为 50。
返回原几何而非裁后几何的投影 MBR。Getter 改为复制后缓冲，
避免重复读取累加扩张；修复原 reset 析构 bounder 后未重建的问题。
插件适配投影方向时归一化为 S2 要求的单位向量，闭环 WKB 去掉 S2Loop 不需要
的重复结束点；无效 S2 几何显式报错，不触发库内致命断言。这些表示／准入
修正仍需扩大原实现差分，不能直接宣称已有索引兼容。

构建复用现有准备好的 S2、Abseil、OpenSSL BIGNUM 静态库，不引入新算法库，
不链接内核；每条第三方依赖边都通过私有 target 校验。仅放行确切 Unix 系统
库叶子，拒绝原始 vendor 库路径／参数、链接搜索目录、生成表达式及同名 target
冒充，并在配置结束时复查后加的依赖。
验证通过：200 组原生 coverer 独立对照、真实租约 DSO 覆盖／祖先／越界重试／
日期线／缓冲／Z／ABI 错误回归、6 项 GIS 与构建边界 CTest、12 个 SDK 头的
独立 C/C++ 安装编译、生产插件构建及二进制审计、原 SQL/LOB fixture。
内核 visitor 通过开启 core GIS 的生产参数语法检查。仍未接通 DAS 写入、
范围生成和 MBR 扫描过滤，也没有实库空间索引验收；未部署或重启测试实例。

第十二批接通宿主 MBR 容器的过滤桥接：新增
`org.seekdb.gis.index.filter`，复用原 Cartesian Contains/Intersects 与 S2
地理区间谓词，包括日期线环绕和地理点对 ApproxEquals 容差。返回值表示
“丢弃候选行”，不是精确几何关系。core-GIS-off 的 `ObSpatialMBR::filter`
通过现有 provider/registry 租约调用插件；core-GIS-on 使用同一份
`s2_mbr.hpp`。不支持的关系和非法区间先报错，不再让点对快捷路径误接受不支持操作。

宿主保留原 MBR 行字节编解码，抽到无 S2 依赖的 `spatial_mbr.hpp`：点为
16 字节 `[xmin,ymin]`，其他几何为 32 字节 `[ymin,ymax,xmin,xmax]`，保留
原 native-endian 布局。新增精确长度／有限数检查，memcpy 处理未对齐输入，
失败不覆盖输出；两个 profile 均初始化 MBR 默认标志。宿主只做协议校验和
复制，算法仍在插件；生产宿主对象无 S2/Boost 未定义符号。超时、内存错误、
零次／重复回调、非法布尔值／类型／保留字段均不能发布部分过滤结果。

本批验证：192 组原生 S2 谓词独立对照、真实 DSO 租约 ABI／错误回归、
生产对象 SQL/LOB fixture 中的宿主 MBR codec/过滤桥及坏 producer 注入通过；
新宿主对象已编译、归档并链接进 seekdb，core-GIS-on MBR 源文件通过生产参数
语法检查。仍未打开 DAS 扫描入口、范围生成和写入的准入，不冒充完整空间索引
或实库扫描验收。测试实例未部署、未重启。

第十三批接通 core-GIS-off 的 `ObS2Adapter` 覆盖桥接。cover 服务升级为
1.1：显式 `ALL_VIEWS` 请求返回 v2 结果，一次租约调用取得 cover cell、
唯一祖先、vertex cell 与原查询使用的交错 cell/祖先序列；旧请求仍返回
原 v1 格式，execution SPI 本身不变。宿主复制并缓存结果，不保留插件或
输入 WKB 指针，不通过多次调用拼接不同 provider 代际的覆盖视图。
保留原 4/50 cell 构造选项、地理角度缓冲和原 MBR 语义。

新增 `org.seekdb.gis.index.cells` 有界批量服务，复用原 S2 后代范围及祖先
算法，非法 ID 先报错；越界 sentinel 仅对应自身键且无祖先。宿主范围接口
返回状态，范围生成调用方检查错误，不再失败时伪造零范围。初始化失败可重试，
成功后重复初始化报错；无输出／重复输出、类型／长度／保留字段错误及输出后
失败均不发布部分状态。宿主仅负责协议校验、复制和错误传播，不引入 S2 算法。

本批不开放空间索引准入：权威 SRS、DAS 写入／范围／扫描链路、持久索引的
插件依赖和代际固定以及实库验收仍待完成。持有覆盖结果副本不等于持有完整
索引生命周期依赖；不得将局部桥接测试解释为存储链路已完成。

本批验证通过：6 个 face／31 个 level 共 186 组原 S2 cell 范围／祖先对照，
真实 DSO 的 v1/v2 视图等价与批量 metadata 回归；生产对象 SQL/LOB fixture
覆盖结果所有权、失败重试、4/50 选项、空／越界／缓冲及 9 类坏 producer。
6 项 GIS／构建边界 CTest 和 12 个安装 SDK 头 C/C++ 编译通过；宿主对象已
重编译、归档并重链接，core-GIS-on adapter／范围代码通过生产参数语法检查。
以上属于局部构建与离线 fixture 证据，不是 clean 全量构建或实库空间索引验收。

第十四批开始一般 SRS 迁移：将原 Boost.Spirit WKT1 grammar 抽到
`srs_wkt_grammar.hpp`，内核解析适配器与插件私有 `srs_parser.cpp` 共用语法。
插件使用自有字符串、variant 和参数数组，保留名称／authority 字符串、
轴向、单位、datum 七参数和参数顺序，不通过 SRID 硬编码猜测元数据。
保留原地理 AXIS 必填、投影 AXIS 可选、方括号／圆括号及关键字大小写语义。
修复全空白输入读取越界、可选 authority／axis 未初始化、字符串／参数容器
分配失败未正确传播的问题，移除原每个字符串单独创建 arena 的辅助类型。

插件解析回归覆盖内置 5,151 条定义：483 条地理、4,668 条投影、39 种投影
authority。`gis_srs_parser.py --build-dir build_release` 用生产编译／链接参数
编译实际内核适配器与插件解析器，逐字段比较所有定义，并验证空白／分配失败。
这是共享语法下的模型一致性证据，不是独立投影算法差分，也不是实库 SRS 服务
验收。解析器已作为插件私有组件编译，但尚未暴露 SRS 服务；core-GIS-off 的
catalog/SRS 桥、原投影工厂及椭球／轴序／单位策略仍需迁移。不能据此放开
现有 SQL SRID 限制或空间索引准入，也没有将 STL／SRS 对象放进公共 C ABI。
生产插件构建／二进制审计、上述模型对照、7 项 GIS／构建边界 CTest、原
SQL/LOB fixture 通过；内核 parser 与 SRS 工厂源码通过 core-GIS-on 生产参数
语法检查。这些仍是组件级证据，不是 clean 全量构建或实库 SRS／存储验收。

第十五批增加实际可调用的 `org.seekdb.gis.srs.describe` 1.0 服务。通过现有
registry 租约接收有界 WKT1 和明确 SRID，返回纯 C POD 元数据：基础地理椭球、
角度／线性单位、本初子午线、外层／基础轴向、WGS84／七参数标志及有序必需
投影参数。catalog 查询、权威 bounds 和 proj4text 仍由宿主负责；服务不按
SRID 猜定义，不执行坐标变换，未知工厂方法保留原零类型回退而非宣称可投影。

39 种原工厂参数表抽到 `srs_projection_parameters.hpp`；WGS84 判别、轴向／
单位／本初子午线转换及短半轴计算抽到 `srs_semantics.hpp`，内核和插件复用。
修正 Cassini-Soldner 对象误报 Mercator 类型的问题（应为 EPSG 9806），保留
其原参数表；内核必需参数注册改为检查并传播分配失败。authority 数字解析
保留原前缀／符号／整数窄化语义，没有借迁移静默更改可接受定义。

扩展 `gis_srs_parser.py`，对所有 5,151 条定义比较实际内核工厂元数据和单位
转换；真实 DSO 回归覆盖新服务、WGS84／七参数、Cassini／横轴墨卡托必需参数、
重复参数后值优先、未知方法、坏请求和回调失败。39 份参数表有原始注册列表
摘要校验，数字解析另与原二进制字符集转换比较，参数注册加入分配失败注入。
宿主 SRS cache 桥、一般投影变换和完整空间索引实库验收仍未完成，SQL SRID／
索引准入没有开放；不会将局部服务等同于方案 2 全部迁移。
本批生产 DSO 构建／导出审计与租约调用、实际内核工厂／归一化对照、7 项
GIS／构建边界 CTest、13 个安装 SDK 头独立 C/C++ 编译及原 SQL/LOB fixture
均通过；测试实例没有部署或重启。

第十六批接上 core-GIS-off 的 `ObSrsWktParser::parse_srs_wkt`：通过已有
registry 租约调用 describe，校验完整响应，再分配宿主 arena 中的 `PluginSrs`。
对象只保存数值元数据、bounds 和宿主字符串，不持有插件 vtable、STL 资源或
长期租约，适配原 cache 整体释放 arena 的方式；错误、重复回调和分配失败
不发布半成品。轴序／单位辅助接口和原 244 个 PG 保留 SRID 的 proj4 文本生成
在两种构建配置中复用，不把 Boost/S2 链回宿主。地理 proj4 格式化同时修正
球体错误的 `+b=0`，改为 `+b=长半轴`。

新增回归覆盖真实 DSO 桥接、响应故障、宿主对象所有权及实际
`ObSrsCacheSnapShot` 保留条目在 DSO 关闭后的读取；不等同于 catalog SQL
刷新验收。单次调用租约不能保证整批 cache 刷新来自同一插件代际，快照代际
绑定／失效机制仍待实现，一般投影和实库索引链路也未完成。没有据此开放
SQL SRID／索引准入，也没有部署或重启测试实例。
本批生产对象编译／seekdb 重链接、core-GIS-on 语法检查、完整 SQL/LOB fixture、
5,151 条原内核工厂对照、7 项 GIS／边界 CTest、真实生产 DSO 加载及 13 个
安装 SDK 头的独立 C/C++ 编译通过；这不是 clean 全量构建或实库验收。

第十七批复用原 `boost::geometry::srs::transformation<>`，通过共享
`projection.hpp` 供原内核和插件私有 `Projection` 使用。原内核补查 Boost
点／范围变换的失败返回；插件 4326↔3857 SQL 路径也改用该后端，不再维护
专用 Mercator 公式。增加带 registry 租约的 `org.seekdb.gis.srs.transform`：
接收几何、显式源／目标 proj4 和结果 SRID，一次调用复用一个变换器，全部
顶点成功后才输出；三维高度参与基准转换，不只是透传 Z。

该底层服务要求地理 XY 为相对定义本初子午线的经纬度弧度，+pm 由 Boost 应用
一次，调用者不可重复偏移；投影 XY 使用定义的线性单位，Z 使用 proj4 垂直
单位（未指定时 Boost 使用该定义的线性单位）。
定义而非 SRID 数字决定变换，相同 ID 也不能跳过定义检查。无网格数据能力，
明确拒绝网格定义／NAD27、init 文件扩展、地心坐标及非 ENU proj4 轴向。
补充 10 种定义共 1,000 点与原 Boost 范围调用的适配对照和逆变换检查，以及
真实 DSO 的 UTM、单位、本初子午线、3D 基准、空输入、坏请求／参数与尾部
顶点失败测试；不是独立测地算法或实库 SQL 的等价性证明。
对照发现原 Boost Cassini 在本样本集上的最大往返角误差约 4.94359e-8 弧度；
插件逆变换与原直接逆变换逐点误差小于 1e-12 弧度。记录原算法精度限制，
不把“与原实现一致”当成更高精度保证；其余九种定义保留 1e-8 弧度的往返检查。

一般 SRID 的权威 catalog 查询、SQL 轴序／单位归一化与错误策略尚未接到此
服务，缓存代际绑定和完整索引验收也仍未完成；没有把通用底层投影服务等同于
完整 `ST_Transform` SQL 支持，暂有限制及索引准入保持关闭。
本批生产插件构建／导出审计、真实 DSO 加载、8 项 GIS／边界 CTest、完整
SQL/LOB fixture、core-GIS-on transform 语法检查及 13 个 SDK 头独立 C/C++
编译均通过；没有 clean 全量内核构建或实库 catalog／存储验收声明。

第十八批增加宿主 SQL API v5 的追加接口 `lookup_srs`，一次查询最多 16 个
非零 SRID，在同一 SRS cache guard 下取得全部记录后才回调一次。返回原始
catalog WKT、proj4 和 bounds 的只读 C POD，字符串只在回调内有效；保留
旧 v1–v4 前缀，Rust 端同步 FFI 布局。不向插件暴露宿主 C++ 对象，也不授予
catalog 写权限；这一快照不是数据事务快照或持久 catalog epoch。

实际 `ObSrsCacheSnapShot` 深拷贝普通 catalog 行和 PG 保留条目的原始定义，
为后续由当前插件代际自行解析提供输入，不能据此宣称所有旧解析缓存的代际
问题已解决。修正目录字段错误被后续读取覆盖、bounds getter 错误被一律当成
NULL 的问题。增加生产对象回归，覆盖实际行提取／缓存所有权、批次中途切换
provider 快照、缺失记录／坏请求、回调失败、重入、线程和超时检查；目录 SQL
传输仍为受控 fixture，不代表真实刷新或并发 DDL 验收。

**普通 SQL `ST_Transform` 尚未接入新查询接口**，一般坐标系的 SQL 轴序／
单位及错误语义还需继续对齐；SQL SRID 限制和索引准入本批没有放开。
本批受影响生产对象编译／重链接、core-GIS-on 语法检查、完整 SQL/LOB fixture、
8 项 GIS／边界 CTest、真实 DSO 加载、SDK 安装头检查及 Rust SDK 测试／Clippy
通过，包含新记录和接口的 C/Rust 布局比对。完整 `kernel_script.py` 生产对象
回归亦通过，验证实际 Rust text DSO 及旧 SQL／query catalog 前缀调用。
未部署或重启服务器，完整缓存
刷新生命周期和实库存储验收仍待完成。

第十九批将 `ST_Transform`／`_ST_Transform` 接到 SQL 上下文和同快照
`lookup_srs`，移除硬编码的 4326↔3857 分支。插件复制原始定义，在当前租约
下解析；地理 SRS 按原椭球／WGS84／七参数元数据生成 proj4，投影 SRS 使用
catalog proj4。内部 WKB 固定 X／经度在前，不按 WKT 的纬度优先声明再次
交换；复用原方向、角度单位和本初子午线换算，避免 +pm 重复施加。

默认 3857 条目中的 `+nadgrids=@null` 是明确无位移网格，允许该精确标记而不
开放文件访问；真实网格／NAD27、init、地心及非 ENU proj4 轴向仍拒绝。
非零同 SRID 和空几何也必须查到并验证定义；0 仅支持 0→0。缺少目录接口
或记录不允许回退为重标 SRID。计算前、每 1024 个顶点及结束后检查取消，
所有点／环／集合成员成功才输出一次。旧宿主缺少 SQL API v5 时明确拒绝。

回归使用仓库原始 4326／3857／32631 定义，覆盖真实 DSO 和 SQL/LOB 路径的
Mercator／UTM 正反向，以及 grad、非零本初子午线、南／西向轴、投影千米、
3D 基准偏移、坏回调、未知同 SRID 和 2048 点中途取消。组合 SQL 验证
`_ST_BestSRID`→原 PG 保留 UTM 999031→`ST_Transform`，保留定义由原宿主
构造器生成。SQL 明确拒绝没有已知 datum／显式七参数／无位移网格约定的
proj4，防止 Boost 静默跳过基准转换；显式底层服务保留原局部投影策略。
SQL 使用实际宿主缓存和 SPI，但
目录传输仍受控，不是实库刷新验收。原始 SQL 错误码、构造器轴序可选项、
缓存刷新生命周期、一般 SRS 下其他算法和完整索引仍需继续完成。
本批最终生产 DSO 构建／导出审计及加载、完整 SQL/LOB fixture（含保留 UTM
组合调用）、8 项 GIS／边界 CTest 和源码依赖边界检查均通过。没有部署或
重启测试实例，不代表实库 catalog／存储／索引验收通过。

第二十批修复宿主 SRS 快照发布／回收：旧快照的引用数只读取一次，入退休队列
后不再直接释放，回收时先移除队列项；队列分配失败保留当前快照并销毁未发布
候选。刷新前原子领取失效标记，避免覆盖刷新过程中其他线程的新通知；失败
重新置为待刷新，保留读取／解析的原始错误，仅空目录映射为 SRS_EMPTY。
两条目录查询均限定 `oceanbase`。销毁路径使用 allocator reset，支持幂等销毁
和重新初始化；仍要求先排空调用者及 guard，不允许 guard 跨越服务销毁。

生产对象回归通过实际 `ObSrsService` 和受控 SQL 代理验证旧读者保留、PG 保留
定义生成、读取／部分行／未完成导入失败后的重试、刷新时跨线程失效，以及
销毁／重建。代理报告已完成导入的计数，但仅返回一条代表性目录行，不是完整
实库导入验收。另用生产发布辅助函数确定性触发“入队期间最后一个读者释放”
并注入队列分配失败，检查恰好一次回收。受影响生产对象编译及重链接、core-GIS-on
语法检查、完整 GIS SQL/LOB、8 项 GIS／边界 CTest 和源码边界检查通过。
补编译两个受影响的 observer Unity 对象并最终重链接后，GIS 回归与使用实际
Rust text DSO 的完整 `kernel_script.py` 回归再次通过。
全缓存的插件代际绑定、实库刷新／存储及关停并发仍未验收，未部署或重启实例。

第二十一批迁移普通 WKT/WKB 的目录和轴序语义。四个普通构造器增加第三个
`axis-order` 参数，四个序列化入口增加第二个参数，SQL 包从 106 增至 114 个
声明，仍为 82 个名称。非零 SRID 通过 SQL API v5 查询原始定义，由当前插件
解析；不再只给坐标贴 SRID。默认／空白选项使用 SRS 定义顺序，另支持
`long-lat`／`lat-long`，大小写和 ASCII 空白规则与原解析器对照。EPSG:4326
默认输入 `POINT(49 2)` 对应内部 X=2、Y=49，投影／SRID 0 不交换坐标。

I/O 按原角度单位检查范围，但不变换 datum、本初子午线、方向符号或 Z；
无需仅投影转换才需要的 towgs84。补上原实现支持的空 GeometryCollection
两种文本形式，空集合也必须验证非零 SRID。普通 WKB 拒绝 EWKB 标志（包括
嵌套），避免内嵌 SRID 覆盖实参；PG EWKT/EWKB／geography 仍是待完成的独立
入口。WKT 数字解析使用有终止符的私有副本，并限制集合递归深度。非零 SRID
遍历前后及每 1024 顶点检查取消，失败不输出部分结果。实际 DSO 测试和 SQL
fixture 覆盖独立轴序控制值、两种字节序、Z、别名、NULL、本地 datum、grad、
坏回调和取消；测试从原源码提取 option parser 函数体作语法对照。原 SQL
精确错误码、完整 WKT／3D／M／各类空几何及真实服务器验收仍未完成。
本批最终生产 DSO 构建／导出审计及加载、含 114 项声明的完整 SQL/LOB、8 项
GIS／边界 CTest 和源码边界检查通过。隔离服务器验收脚本已增加轴序用例，
但本轮没有在服务器上运行该脚本，没有部署、重启或实库存储验收。

第二十二批迁移 EWKT 输入、EWKB 输入／输出及 geography 文本输入，替换原先
借用普通 WKT/WKB 的实现。EWKT 从 `SRID=...;` 前缀取 SRID，默认经度在前；
EWKB 从根头取 SRID/Z，第二参数改为原实现的轴序字符串，NULL 使用默认顺序，
保留原入口仅小端、拒绝 M 的策略。输出非零根 SRID 和 Z 标志，子几何沿用
原规范 WKB 表示；解码检查子 SRID、一致多几何的类型和维度。geography 默认
4326（包括显式 SRID=0），要求地理 SRS。原经纬度折返函数抽入共享头文件，
宿主 visitor 和插件共同调用，保留 1e-10 容差、度数策略以及原 2D／3D 边界
处理差异。删除 EWKT/geography 三个错误的额外数值参数重载后，当前包为
111 个声明、82 个名称；上批 114 是中间阶段数值。`_ST_AsEWKT` 文本输出和
精度参数仍待独立迁移，精确 SQL 错误码、完整语法／维度与实库验收也未完成。
本批最终生产 DSO 构建／导出审计和实际加载、共享折返桥的 core-GIS-on 语法
检查、含 111 项声明的完整 SQL/LOB、8 项 GIS／边界 CTest 及源码依赖检查通过。
覆盖独立 EWKB 十六进制控制值、SRID/Z、NULL 轴序、空集合和原 geography 边界。
服务器验收脚本增加了对应控制值，但仅运行离线脚本测试，没有部署、重启或
实库 catalog／存储验收。

第二十三批接入 `_ST_AsEWKT` 输出与可选精度，SQL 包增加至 112 个声明、82 个
名称。输出原始 SRID（包括未知 ID 和原 `SRID=NULL` 哨兵），不查询 SRS 目录、
不交换坐标。默认精度 15，0／负数／>=25 不舍入；保留原 3D 忽略精度和固定
25 字节数值缓冲区规则。多几何保留 Z，集合保留子类型，显式 NULL 精度返回 NULL。

将原 oblib dtoa 抽到私有共享源码，内核和插件分别编译，未引入宿主算法回调或
新的公共 ABI。插件侧以十进制字符串舍入替代 ObNumber 依赖，测试从原源码
提取精度格式化函数，与真实内核 ObNumber/dtoa 对照，373,705 组输入通过。
插件 dtoa 堆分配按调用作用域管理，失败时回收并由插件异常边界转换状态。
独立数值和分配控制值的 ASan/UBSan 通过；环境不支持 LeakSanitizer，未宣称
泄漏检测通过。生产 DSO／导出审计、实际加载、SQL/LOB、9 项 GIS／边界 CTest
和源码边界检查通过。完整原 geometry visitor 差分仍未完成，尤其各类空几何、
混合维度、原错误码以及 2D 不舍入时缓冲区容量相关的格式选择仍需兼容性检查。
没有部署、重启或实库验收。

第二十四批统一普通 `ST_AsText`／`ST_AsWKT` 与 EWKT 的插件内序列化器，
普通输出改用原 dtoa，修正二维空格规则、三维类型标记与多几何 Z 坐标保留。
按原 visitor 拒绝根与子几何维度不一致的集合，失败不输出部分结果。
二维不舍入时模拟原缓冲区的 512 字节初始容量、倍增、预估和坐标剩余宽度，
替换上一批固定 256 字节的近似。逻辑容量与实际字符串分配分离，不按原内环
数量平方的估算分配内存；成功结果移动交付，避免额外全文复制。

新增 `gis_wkt.py`，离线编译原二维／三维 visitor 和二进制几何类，与实际
GIS 动态库对照；1,290 项覆盖七类几何、嵌套／空集合、SRID、精度、混合维度
拒绝及缓冲区扩容边界。普通 SQL 用例增加短浮点表示、Z 多几何和三维文本
往返。其余空类型、M、畸形输入／精确 SQL 错误以及实库验收仍未完成。
最终生产 DSO 构建／导出审计、实际加载、完整 SQL/LOB、9 项 GIS／边界 CTest、
源码依赖和新增文件许可证检查通过。未部署或重启实例。

第二十五批将原 WKT 解析器的全局维度推断、连写／分写 Z、MultiPoint 括号一致性、
线至少两点及环首尾 XY 位级闭合规则移入插件；保留仅集合接受 EMPTY／空括号的
原规则。数值输入复用有界 strtod，失败不修改输出。原解析器准入和原始 WKB
逐字节差分新增 218 项，与已有 1,290 项输出差分均通过；SQL/LOB、9 项 GIS／
边界 CTest 通过，数值跨度测试的 ASan/UBSan 通过（禁用环境不支持的泄漏检测）。
这不是完整二进制语法、SQL 错误码或实库目录／存储验收；未部署或重启实例。

第二十六批修正 EWKB 的根头转换和二进制准入：只转换根部 Z／SRID 标志，
子几何必须是普通 WKB，不能递归携带 EWKB 标志或另设 SRID。保留原根类型低位
取模规则，由 Z 标志决定维度，不能将 ISO 偏移直接当作 EWKB Z。二维沿用统一
小端、非空多几何／多边形和环 XY 位级闭合；三维保留不同的空类型／闭合规则。
转换及临时缓冲区由插件负责，没有新增宿主算法回调或公共 ABI。

差分测试提取原 EWKB 根头／转换函数，编译原二维／三维校验器，与真实 DSO
对照 1,619 项全部通过，覆盖根标志／偏移、七类几何、截断／尾部、多层标志、
空类型、闭合、混合维度及二维混合字节序。WKT 218／1,290 项、完整 SQL/LOB、
生产 DSO 加载、9 项 GIS／边界 CTest 和源码边界检查通过。非有限坐标、所有畸形
三维子类型／字节序组合、精确 SQL 错误码及实库索引验收仍待完成；未部署重启。

第二十七批接通 DAS 索引行生成函数 `generate_spatial_index_rows`：移除轻量
内核的无条件拒绝，通过已有租约式 S2 桥获取覆盖单元与 MBR；宿主只负责行投影
和存储布局。补齐插件 geometry envelope v1 的 SRID 读取，短输入／无效版本
拒绝，完整几何仍由插件校验，不增加旧 SWKB 0x41 格式转换或新的公共 ABI。
校验投影索引和 SRS 条目，分配／reshape／追加失败时撤回本次已追加的行，保留
调用前输出。

真实生产对象和 GIS DSO 测试覆盖平面／地理点、线、空集合、越界哨兵、cell／
MBR／复合主键布局、不存在的 SRS、无效投影、服务缺失和九类插件错误。SRS
快照通过生产 catalog 行解析流程构建；17 列迫使 datum 行越过内联容量，对每个
分配位置注入失败，包含已生成部分行之后的回退。完整 SQL/LOB、9 项 GIS／边界
CTest、源码边界及许可证检查通过；DAS 和 SRID 头对象已重编译归档，seekdb
重新链接，core-GIS-on 的 DAS Unity 生产参数语法检查通过。
这是行生成证据，不是存储写入／索引 DDL 回填／范围／扫描验收；范围与扫描
入口、持久索引模块依赖及实库验收仍待完成，没有部署或重启实例。

第二十八批接通 DAS 空间扫描：轻量内核可构建原扫描／排序去重／回表迭代树，
标量扫描通过已有租约式 MBR SPI 过滤；新增批量路径，修复继承普通扫描导致
批量读取绕过过滤的问题。多查询范围保持 OR 语义，压紧所有输出（包括事务
元数据），有保留行后不再跨批读取，避免借用数据失效。末批保留行与 ITER_END
同时返回，插件／存储异常不发布成功的部分批次。重新绑定参数时重置全范围
标志；MBR 按 VARCHAR 底层类型检查，接受二进制 VARBINARY，而非错误使用
排除二进制排序规则的 `is_varchar()`。

新增 `gis_das_scan_fixture.h` 使用真实生产扫描迭代器和 GIS DSO，存储行源
由夹具控制，检查标量／批量、多范围、重绑、有无事务元数据、全拒绝批次、
压紧／容量限制／末批和故障隔离。它不是实库 tablet 扫描或整棵排序／回表树
的验收；范围生成、回填、持久索引模块依赖和实库存储验收仍待完成。
本批完整 SQL/LOB（包含新增扫描夹具）、9 项 GIS／边界 CTest、源码依赖边界、
新增文件许可证和 diff 空白检查通过。生产 DAS Unity 对象重新编译归档、seekdb
重新链接，core-GIS-on 同一 Unity 单元的生产参数语法检查通过。未部署或重启实例。

第二十九批接通表扫描空间索引回填入口。轻量内核中的
`get_cellid_mbr_from_geom` 通过已有租约式 S2 适配器取得原覆盖算法结果，宿主
只组装 cell 与存储 MBR；失败保留调用前的 cell 前缀和 MBR 缓冲区内容，不增加
公共 ABI 或宿主几何算法。表扫描跳过 NULL／空集合直至产生真实索引行，拒绝
非 NULL 的零字节几何，防止返回上一行的生成列。补齐 SRS 空结果检查，错误时
清除部分 covering，投影成功后才推进输出位置。初始化验证唯一的几何／cell／
MBR 表达式，全部分配成功才发布缓存，保留内存不足错误并允许失败后重试。

`gis_backfill_fixture.h` 使用真实表扫描算子和实际 GIS DSO，底层 DAS 行源受控；
不覆盖 tablet IO、DDL checksum 上报或提交后的建索引流程。原单源行 32-cell
回填上限暂时保留，超过时明确失败，不截断结果。优化器空间范围生成、持久索引
模块依赖和实库索引端到端验收仍未完成。
本批完整 SQL/LOB 回归通过，包含 NULL／空集合、多 cell／主键对应关系、平面／
地理／越界哨兵、raw／行内 LOB、缺失 SRS／服务、九种插件错误、畸形几何、
存储错误和三处缓存分配失败后重试。9 项 GIS／边界 CTest、源码边界及新增文件
许可证检查通过。表扫描 Unity 和 SPI 桥对象重新编译归档并重链 seekdb；
core-GIS-on 的同一表扫描 Unity 生产参数语法检查通过，未部署或重启实例。

第三十批接通轻量内核 `ObRangeGenerator` 的五处空间范围入口。宿主组装索引键
和 MBR，覆盖、祖先和子 cell 仍调用插件内原 S2 算法。平面距离查询通过内部
`org.seekdb.gis.index.planar_buffer` 服务复用现有 buffer；宿主先查验 SRS，插件
仅适配投影 SRID 标签，不变换坐标、不接纳未知／地理 SRS，也不扩大原算法维度
支持。地理距离保留原 S2 角度 buffer 路径。该服务不增加 SQL 函数声明。

保留 SRS 查询错误，拒绝非有限距离并传播子 cell 服务失败。空几何／NULL 的
保守全范围回退不再被无需合并分支跳过，不发布空／NaN MBR。失败时撤销新追加
的范围／MBR 并恢复单值范围标记；测试从空输出数组开始，不宣称对任意已有范围
列表的原地合并具有完整回滚保证。`gis_range_fixture.h` 通过公开 API 构建范围图，
调用实际生成器和 GIS DSO。

**第三十批的 SQL 优化器接线仍未完成**：当时 `ObRawExpr::is_spatial_expr()` 和 domain-op
映射仅识别内置表达式类型，不能把这批范围图测试当作 native SQL 函数已能选中
空间索引的证明。下一步需接通基于真实实现绑定的 planner 支持，并验证实际 SQL
提取路径；持久索引模块依赖、tablet IO 和实库建索引／查询也仍是未完成项。

本批完整生产对象 SQL/LOB 回归通过，新增点范围／祖先对照、相交／包含／被包含、
平面／地理距离、32631 投影 SRID、无约束列 SRID、越界哨兵、空／NULL、畸形头和
OR 分支失败、缺失 SRS／服务、超时及九种生产者错误测试。buffer 失败保留输出，
服务类型和保留字段校验也已覆盖。9 项 GIS／边界 CTest、源码边界、新文件许可证
与 diff 检查通过；SQL range/common 与 share 桥对象重编译归档并重链 seekdb，
range Unity 通过 core-GIS-on 生产参数语法检查。仅为增量构建和离线验证，未部署、
重启实例或运行实库验收。

第三十一批接通 SQL 扩展包 native 函数的空间谓词识别及范围提取路径。实现描述符新增
四个互斥索引策略标志：相交、包含、被包含和距离；固定 C ABI 布局不变，旧宿主
拒绝未知标志。注册只允许定型、固定元数、implementation-only、deterministic、
immutable、NULL-propagating 的标量实现声明这些策略。GIS 插件自行标注实现，
宿主按精确 owner／object-ID 绑定取得元数据，不按 SQL 函数名判断。

原始 UDF 表达式复用现有 64-bit extra 槽保存已验证描述符标志，不增加表达式布局
或悬空指针；复制、比较和 hash 保留这些信息。代码生成复核标志及 native 身份，
避免解析后的实现变更沿用旧策略。范围转换保留原 native 函数作为残余过滤，
策略只提供候选集合，不宣称精确结果。`POINT(...)` 等已验证 immutable 的 native
构造函数保留为范围输入表达式；不解除通用常量折叠对普通 PL UDF 的执行限制。

新增真实 SQL 表列解析及范围提取夹具，包含正反参数方向、距离、SQL 别名、
自定义名绑定相交实现，以及同名 `st_intersects` 绑定距离实现的反例。完整 SQL/LOB
回归通过：从实际 SQL 提取范围图，再通过 native `POINT` 和 GIS DSO 生成索引
范围及 MBR。提取图时不执行 native 构造函数；去掉 immutable 证明则安全回退，
复制保留策略标志，代码生成时绑定策略变化被拒绝。10 项 GIS／注册／边界 CTest、
Rust/C ABI 对照、源码边界和 diff 检查通过；重编译归档并重链 seekdb，三处 SQL
Unity 的 core-GIS-on 语法检查通过。

本批之后仍不能宣称完整优化器迁移或实库验收完成：当时 simple-filter-column
收集器仍枚举内置表达式类型（下一批已接通），成本驱动的空间索引选择及其他消费者还需接通／验证。持久索引模块
依赖、tablet IO、实际建索引查询和剩余 GIS 语义覆盖仍未完成。本批未部署或重启实例。

第三十二批接通 simple-filter-column 收集器：按已验证 native 空间策略接纳 UDF，
保留普通 UDF 排除、参数元数检查和表关系检查；距离策略接受三参数，其余空间策略
接受两参数。真实 SQL 夹具覆盖正反参数、包别名、自定义名字、同名非空间实现、
同表列对列、无关表 ID、常量参数、AND／OR 去重及普通比较。收集过程不调用插件。
新增受控空间索引元数据，通过实际 schema-backed 索引候选匹配入口验证几何列
可以命中空间索引，而不是只检查收集数组。该夹具没有实际建索引或执行 tablet IO。

完整 SQL/LOB 回归、10 项 GIS／注册／边界 CTest、源码边界和 diff 检查通过；
重编译 rewrite Unity、归档并重链 seekdb，修改单元 core-GIS-on 语法检查通过。
成本驱动的空间访问路径选择、持久索引依赖和实库验收仍未完成，不能用候选匹配
成功替代。本批同样未部署或重启实例。

第三十三批补充完整逻辑优化入口的证据：在受控表／空间索引 schema、本机地址、
tablet 标识和默认统计信息下，调用实际 `ObOptimizer::optimize`，不加 INDEX hint。
相交、包含的两个参数方向、被包含、距离、包别名和自定义函数名共 7 个查询选择
空间索引；同名 `st_intersects` 绑定距离实现的反例选择基表扫描。逐个检查选中的
扫描算子、回表标志、非全范围图及保留的原始精确谓词，而不是只看优化入口成功。
内部会话用于避免依赖统计 SQL 后端；这不代表生产统计质量或性能已经验收。

完整 SQL/LOB 回归和 10 项 GIS／注册／边界 CTest 通过。定位时的临时诊断输出
已从生产源码移除，并重编译、归档、重链；本批没有保留新的优化器生产修改。
离线脚本新增可选 `--keep-artifacts` 保留隔离的二进制和日志，默认仍自动清理。
该证据仍限于逻辑计划；物理计划生成、真实回表／tablet IO、持久索引依赖、
实库验收与剩余 GIS 语义仍需继续。本批没有部署或重启实例。

完成方案 2 的剩余验收清单（不能以当前绿灯替代）：

- SRS 服务与原投影／椭球策略，去掉仅 0/3857 的临时准入限制；完整检查轴序、单位、Z/M。
- 一般 GeometryCollection 关系／距离与混合维度叠加、空几何结果和原 SQL 错误语义。
- MVT 可选参数的 SQL 类型／转换／错误语义对齐；MVT、box clip、内部点、有效性和修复仍需一般 SRS 与差分验收。
- 原 S2 空间索引完整链路迁移；代表点／Morton 近似已删除，但恢复占位语义不代表实现了索引。最佳 SRID／GeoHash 仍需一般 SRS 与完整差分验收。
- 审计全部已发布函数的 WKT/WKB/EWKB/GeoJSON、可选参数、声明和数值语义，补齐迁移遗漏。
- 原内核／插件差分回归、实库 SQL 包安装和完整 GIS 回归、资源／并发／取消／性能验证。

内部点的局部算法迁移不代表方案 2 全部完成；原 SRS／地理、可选参数及其余近似函数
仍需继续实现，不能以局部 fixture 代替实库验收。

### D. 全面对象与运维语义（未完成）

扩展脚本对象类别、调用者事务安装、通用 CASCADE、权限元数据、SQL 名字重定位、
备份恢复和升级协调。必须增加并发 DDL、提交失败/未知结果、取消/断连、重启恢复
及性能回归。纯 Rust 状态模型、包源读取和 SQL fixture 均不能代替这些证据。

GIS README 提供隔离实例上的候选验证步骤，明确标注未取得实库通过证据；
本阶段没有部署、重启或修改用户测试实例。
