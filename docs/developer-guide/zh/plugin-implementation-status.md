# 插件灵活性与 Rust runtime：实施进度

目标保持为完整的 [下一阶段设计](plugin-next-design.md)：PG 式扩展对象与深度回调自由度，以及 Rust 插件框架实现。不是仅增加 Rust 示例插件，也不是只保留现有八种 descriptor 的封闭对象模型。

## 实库问题修复（2026-09-21，SHOW CREATE 未读取调用者私有 ALTER）

- 实库 fixture `query_catalog_a4b43120ec46d2f904bc` 已通过 autocommit 和
  调用者事务矩阵，在保存点测试的私有 ALTER 属性检查失败。`SHOW CREATE`
  虚拟表工厂重新取得已提交 schema guard，没有继承 resolver 的私有视图；
  routine 按 ID 读取及定义打印因此都可能使用旧版本。
- `ObShowCreateProcedure` 在首次读取前调用会话的 `bind_plugin_catalog_view`，
  让函数和过程的 SHOW 共用现有事务身份、journal 状态及视图绑定校验。
  不修改全局缓存，不跳过定义查看权限，也不在绑定失败时退回旧 schema。
- 新增 `show_routine_catalog_fixture.h`，使用真实 SHOW iterator、printer、
  session 与 Rust journal，受控提供已提交 schema 及事务 descriptor。
  覆盖函数/过程的私有 ALTER、独立会话隔离、无权限定义为 NULL、私有 DROP、
  新身份、保存点回退和事务身份变化拒绝。实库断言保留，仅增加实际定义诊断。
- 生产构建通过（45471），20 项脚本单测、相关 CTest 4/4，以及独立链接
  生产内核的 SHOW 定向回归（92451）通过；完整受控内核回归通过（82913），
  包含上述 SHOW 用例及真实 Rust DSO 回归。不宣称实库矩阵通过，没有清理
  诊断 fixture。9 月 21 日 15:53 二进制 SHA-256：
  `851f2ffb1dab58d07f295088549f434a736e2b6e3dd577ca9ab0119b10e514f8`。

## 实库问题修复（2026-09-21，跨语句 routine 参数悬空导致崩溃）

- 部署 SHA 为 `b9812e65…` 的实库已通过 `check_autocommit`，随后在调用者
  事务的后续 routine 依赖解析阶段断连。GDB 栈定位到
  `match_vacancy_parameters → ObRoutineInfo::get_routine_param → ObRoutineParam::to_string`
  打印无效字符串；不能将其当作普通网络超时或通过关闭日志绕过。
- `RoutineSchemaOverlay::stage` 原来用默认构造的 `ObRoutineInfo` 保存快照。
  虽然外层对象由 `unique_ptr` 持有且 `assign` 执行深拷贝，但真实请求中的
  `ObSchema::get_allocator()` 会选择 schema-stack/request allocator，参数及
  字符串的生命周期仍可能止于创建语句。离线 worker 没有 request flag 时会
  自动创建独立 arena，旧用例因而没有覆盖真实分配器选择。
- 每个 overlay record 现在持有独立 `ObArenaAllocator`，显式传给
  `ObRoutineInfo`；记录销毁时先销毁 routine 再销毁 arena。参数对象、字符串
  与参数指针数组均不借用请求内存，失败 staging 的存储随未发布 record 回收。
  既有 DROP/保存点回退保留历史存储、retire 拒绝新访问的规则不变。
- 新增 `routine_overlay_lifetime_fixture.h`：初始化带请求 allocator 的受控
  worker，设置真实 request flag，分别覆盖 schema-stack 和 worker allocator
  两条默认分配路径，验证新快照独立归属，释放并重用请求
  内存后用新 guard 读取 0/1/70 参数函数、默认值、扩展类型字符串并执行诊断
  打印，检查回退与 retire 后已借用存储仍有效。依赖解析 fixture 也在释放
  创建请求 arena 后运行真实 PL resolver。它们不是实际客户端/存储提交证明。
- 生产构建通过（25910），脚本单测 20 项与相关 CTest 4/4 通过（71508），
  完整受控内核回归通过（19743），包含上述两条分配路径、跨语句依赖解析
  和真实 Rust DSO 回归。不宣称实库完整矩阵通过；未清理诊断 fixture。
  9 月 21 日 15:31 二进制 SHA-256：
  `95ea106a509bd62c72e7fe1d29bbed4dd712c2d2b08947d2bc24760a213c71d3`。

## 实库问题修复（2026-09-16，autocommit 后独立连接对象不可见）

- 本次 fixture `query_catalog_d234d3d82e882e624812` 已通过 CREATE 和数据提交。
  写入 trace `YB427F000001-00065B91899E9E1C-0-0` 于 11:48:30.921945
  投递版本 `1789530510915792` 的后台刷新；测试连接 292 于 .923757 断开，
  而 routine `auto_value`（ID 310121）直到 .927363 才进入 schema 缓存。
  原实现仅设置 writer 的会话屏障，独立 observer 可在刷新完成前取得旧 guard。
- 提交完成现在先以原子 max 记录 schema service 的已确认插件版本，再投递
  刷新，最后才响应提交；队列错误保留版本且不伪装成事务失败。
  MySQL 普通查询与 prepare/execute 等入口在取得 guard 前等待共享版本与
  会话版本的最大值。等待只发生在客户端请求边界，不放入数据完成回调或内部
  schema refresh SQL；回滚、空 journal、身份失效和未知结果不发布版本。
- 新增 `catalog_visibility_fixture.h`：真实 session/Rust journal 完成路径、
  共享屏障及刷新入口，受控 schema 版本与未初始化的刷新后端。覆盖独立 reader
  拒绝旧版本、刷新失败保留屏障、原子单调性、原会话屏障、销毁重置及 7 类
  完成结果。它不证明真实后台刷新等待或持久提交成功。
  实库脚本保持单次即时断言，只补充 created/observed ID 诊断，不增加重试或 sleep。
- 最终生产构建通过（47939），脚本单测 20 项与相关 CTest 4/4 通过（40076），
  完整受控内核回归通过（91691），包含新增屏障用例和真实 Rust DSO 回归。
  没有清理诊断 fixture，不宣称实库矩阵通过。9 月 16 日 12:06 二进制
  SHA-256：`b9812e65653dd32ef8dc73becf16aa5dc36b0bf5145f33633cf83fe26803cc43`。

## 实库问题修复（2026-09-16，提交水位 UPSERT 返回 4109）

- 已核对部署文件与上次交付的二进制 SHA 一致。11:29:12 的 trace
  `YB427F000001-00065B9143BC9634-0-0` 显示 epoch 校验已通过，DDL MDS
  信号登记返回 0。随后 schema 水位 UPSERT 命中已有行，按正常执行路径
  回退 speculative insert 的匿名保存点，再转为 UPDATE。
- 首个 4109 来自 `rollback_plugin_catalog_view`：Rust catalog journal 已处于
  Preparing，拒绝普通 catalog 保存点回滚。此前把这种不修改视图的内部
  UPSERT 回退也送入冻结 journal，导致宿主主动 abort 原事务。不是主备角色
  变更；日志确认事务 1000043 最终 rollback 返回 0，随后服务端主动断连。
- 修复仅在身份一致、已激活且 statement-ready、非提交中的事务，当前
  plugin 子执行上下文与嵌套会话有效、journal 为 Preparing 时，允许严格晚于
  子语句起点的根分支保存点回退保留原冻结视图。数据回退仍须先成功；不解冻
  journal、不移除 end-sign、不接受整条语句/更早/跨分支/身份失效的视图回滚。
  依据是准备阶段仅执行宿主 SQL，不产生普通 catalog 视图标记；子语句内
  保存点不可能撤销之前已记录的 catalog 写入或 end-sign。
- 新增 `catalog_preparing_rollback_fixture.h`，调用真实 SQL 匿名保存点/
  回退控制、session 及 Rust journal，数据事务服务为受控替身。覆盖重复局部
  回退、数据回退失败、相同/更早/跨分支保存点、非 plugin/非嵌套上下文、无
  语句屏障、身份变化、已 seal 和终止事务；成功后 frozen/end-sign 不变，
  失败仍保留错误并按原逻辑撤销视图。不是实际 UPSERT/存储提交的验证。
- 最终生产构建通过（59959），19 项脚本测试与相关 CTest 4/4 通过（93009），
  完整受控内核回归通过（61393），包含上述 11 组边界用例及真实 Rust DSO
  回归。不宣称实库矩阵已通过，不清理用户诊断 fixture。
  9 月 16 日 11:37 二进制 SHA-256：
  `24efbd12c000bdd34ec1eba3a9234de0417cbd1dc82da34f210a936263fb52fa`。

## 实库问题修复（2026-09-10，提交前 DDL epoch 查询返回 1146）

- 16:35:28 的实库 trace `YB427F000001-00065B1CD8E1EA13-0-0` 已通过 catalog
  写入，失败于自动提交前 `ObDDLEpochMgr::check_and_lock_ddl_epoch`：
  `ObGlobalStatProxy::select_ddl_epoch_for_update` 直接生成未限定库名的
  `SELECT column_value FROM __all_core_table ... FOR UPDATE`，未经过前次
  修正的 `ObCoreTableProxy`，被解析到业务数据库并返回 1146。
- 修正 `ObGlobalStatProxy` 的全部七处直接 core-table SQL，显式限定
  `oceanbase`。不切换调用者数据库、不改变事务所有权，不跳过 epoch 校验；
  同类 SCN/LSN 读写保留原有锁和单调更新条件。公开插件 ABI/DSO 不变。
- 扩展提交准备回归，直接调用真实 epoch 管理器及 SQL 生成路径，借用事务和
  结果行为由受控夹具提供；真实 parser 检查系统库限定，覆盖 epoch 相等/
  不等、超时、无行、非法数值、NULL 和多行，另覆盖该代理其余直接读写 SQL。
  不再仅依赖覆盖 watermark 的夹具；这些检查仍不代表真实存储锁或提交成功。
- 日志确认事务 213 的 `rollback_tx` 返回 0，状态为 ROLLED_BACK；提交准备
  返回原始 1146 后，服务端按原有策略断开 session 1170。脚本 finally 再次
  rollback 已断开的连接，报告清理失败。保留此次 fixture，不自动清库；
  不把客户端断连误报为已确认存在未回滚数据，也不修改断连/失败保护逻辑。
- 顺查提交后续路径发现 DDL MDS 信号登记仍要求 owned-connection 事务标记。
  增加专用借用准入：只允许显式启用 catalog 模式的嵌套外部 SPI 连接，以
  ACTIVE、非提交中、可写的原事务登记 `DDL_TRANS`；拒绝其他 MDS 类型，
  不设置连接 owned 标记、不 BEGIN/COMMIT。普通 owned 连接保留原检查。
  新增真实入口拒绝测试及同一生产准入函数的成功检查；MDS 存储效果仍待实库。
- 首轮生产构建通过（18818），19 项脚本测试及相关 CTest 4/4 通过（40850）。
  首轮内核回归（29232）发现新测试将超时后的 borrowed adapter 状态误断言为
  成功；已改为要求保留超时，未改变生产错误语义。补齐 MDS 准入后的最终生产
  构建通过（12628），19 项脚本测试及相关 CTest 4/4 再次通过（68424）；完整
  受控内核回归通过（6828），包含新增 epoch SQL/MDS 准入和真实 Rust DSO 回归。
  实库仍待用户替换复测，不宣称提交或完整实库矩阵已通过。
  16:47 二进制 SHA-256：
  `e53fc6b09f064eba25ba523a5672625216eee81b9673a378eb24ed307c0b044a`。

## 实库问题修复（2026-09-10，catalog 子语句关闭返回 4016）

- 15:45:01 的实库 trace `YB427F000001-00065B1C24C063FC-0-0` 显示系统库
  INSERT 已进入存储写入；首个错误来自 `set_end_stmt`：要求嵌套计数为 0，
  实际为 1。普通 SQL executor 没有给 catalog 子执行上下文设置 plugin 标记，
  导致结果关闭误用外层语句结束逻辑。错误发生于 APPLY（3），操作级清理均成功。
- 增加 host-only 连接 opt-in：仅已准备事务中的嵌套外部 SPI 会话可启用；
  每个 catalog 结果（含重试）初始化时验证借用状态并标记 plugin SQL。
  保留调用方事务类型，不由内部 query 强制改为 SYS；销毁连接时清除 opt-in。
  普通内部 SQL、普通外部连接以及公开 C ABI 均不改变。
- COMMIT 准备可能没有父执行上下文：显式 plugin 标记加活跃嵌套会话仍按
  嵌套 SQL 处理，由 `LinkExecCtxGuard` 设置层级；不放宽 `set_end_stmt` 本身。
- 新增 `catalog_nested_sql_fixture.h`：真实连接准入、执行上下文连接/解除和
  `ObSqlTransControl::end_stmt`，事务服务为受控替身。先重现未标记的 4016，
  再覆盖有/无父上下文、SELECT/INSERT/UPDATE/DELETE、成功/回滚、会话及事务
  身份保留、过期嵌套帧拒绝和连接复用隔离；不声称已执行真实 SQL 或存储提交。
- 最终生产构建通过（95568）；19 项 Python 检查及相关 CTest 4/4 通过（30402）。
  完整受控内核回归通过（46029），包含上述新增嵌套关闭用例和真实 Rust DSO
  回归。首轮编译（94204）发现层级 setter 为私有，已由原有上下文连接器设置，
  未扩大执行上下文 setter 的可见性。实库矩阵仍需用户替换二进制后复测。
  16:07 二进制 SHA-256：
  `c2496d84924b8a7d0a869e59818c19d0ab76e9cea120fce236ddbf67f2c20e05`。

## 实库问题修复（2026-09-10，catalog 内表被解析到业务库）

- 15:18:46 的实库日志确认加锁已通过，失败阶段为 APPLY（3），语句为未限定
  库名的 `INSERT INTO __all_routine`，因此在 `query_catalog_17ac7108cb8b38dfa6dd`
  查表并返回 1146。操作级 close、身份检查、数据回滚和视图回滚均返回成功。
- catalog 写入显式使用 `oceanbase` 命名空间，不切换借用会话的数据库、用户、
  权限或事务。新增 host-only `CatalogDMLSqlHelper`，用于 routine/参数/历史、
  routine 权限、依赖和编译错误记录；并补齐原始 SQL 中的权限读取、依赖/
  错误记录、扩展成员检查、DDL 日志及 core-table 提交水位的限定库名。
  普通 `ObDMLExecHelper` 保持原行为，没有新的公开插件 ABI。
- 新增生成 SQL 的真实 parser 检查：所有关系节点必须显式属于系统库；接入
  CREATE/ALTER/DROP、依赖 routine 和提交准备的受控夹具。另验证 catalog
  DML 限定不改列/字面量，普通业务 DML 保持不限定库名。
- 最终生产构建通过（39219）；19 项脚本检查及相关 CTest 4/4 通过（40437）。
  完整受控内核回归通过（19451），包含新增 parser 命名空间检查、真实 catalog
  writer/调用者 mutation/提交准备夹具及 Rust DSO 回归。首轮（44111）的新夹具
  引号转义问题、第二轮（16133）的旧权限 SQL 前缀断言均已修正；未改变测试
  预期错误或跳过用例。实库写入矩阵仍需用户复测，不宣称通过验收。
  15:31 二进制 SHA-256：
  `cc35cd3988c199d7789e9df295452cf452d5828ebf52fd492d5140a48e9977d6`。
- 日志还记录了外层语句回滚将数据事务重置为 ID 0 后，旧视图身份检查拒绝并
  清除视图（6205）；客户端保留原始 1146。本次不放宽该身份保护，不将清理
  日志或受控 SQL 生成检查视为实库事务语义通过。

## 实库问题修复（2026-09-10，6205 后继续出现 4016）

- 用户替换 13:04 版本后再次测试，首个自动提交 CREATE 返回 4016；已核对
  部署二进制 SHA 与上次交付一致。14:05:00 的日志显示语句清理返回成功，
  事务 303 尚无存储写入，不能把上轮受控回归当成实库写入成功。
- 代码确认另一个必然拒绝点：Root DDL 加锁检查内部连接 `is_in_trans`，而
  外部会话 SPI 连接没有执行 `start_transaction`，该标记一直为 false。
  在 `ObInnerSQLConnectionAccess::lock_obj` 内补齐借用适配：仅允许已准备的
  嵌套外部会话执行事务内锁；只读、未准备及终止事务拒绝。在本次加锁调用
  内临时设置连接标记，正常返回和异常展开都恢复原值；不执行 BEGIN/COMMIT，
  不替换事务描述符，不修改原有 owned-connection 加锁行为。
- 新增真实内部连接初始化、嵌套会话和生产适配入口的受控回归，锁服务为
  显式替身：检查同一事务、成功/超时/异常的标记恢复、已有标记保留、非法
  状态拒绝及没有事务控制调用；不声称覆盖真实锁存储效果。
- 失败阶段日志改为默认可见的 INFO。此前 LOG_WARN 属于 WDIAG（级别 4），
  被默认 EDIAG（级别 3）过滤；诊断仍只输出阶段和错误码，不输出 SQL 正文。
- 生产构建通过（6271）；19 项脚本测试及相关 CTest 4/4 通过（47746）。
  完整受控内核回归通过（5022），含新增连接标记用例；实库矩阵仍待替换后复测。
  14:15 二进制 SHA-256：
  `a7ba5ac55ddd99566e49da4454e4adefb885d257462334c3860f0f9251c4f46d`。

## 实库问题修复（2026-09-10，首次 catalog 写入返回 6205）

- 用户宿主机已安装 `org.seekdb.rust-text`，两个 NULL 探针成功。实库脚本首个
  自动提交 CREATE 失败；对应日志 12:21:29 的事务 391 有正 sequence base、
  保存点但仍为 IDLE，先前将借用准入限定为 ACTIVE 导致提前拒绝。
- 增加只读 `tx_desc_is_statement_ready`：合法 ACTIVE/IMPLICIT_ACTIVE 或有
  身份、正 sequence base、隐式保存点的 IDLE。用于 catalog 预检身份固定、
  SQL transport 和私有视图准入；不修改已有 is_active 的含义，不调用 BEGIN，
  不直接改变事务状态，不删除提交/已完成 schema 写入的 ACTIVE 检查。
- table-free SELECT 的插件保存点成功后，补齐 session start-statement 登记。
  result-set 的既有结束路径配对清理；失败建立保存点时不留下开始标记。
  错误日志追加失败阶段及清理错误码，不输出 routine SQL 正文。
- 生产构建通过（59077），补齐语句登记后的最终构建通过（99019）。完整受控
  kernel 通过（14285），包含准备态 IDLE、缺失身份/保存点、回滚/终态拒绝、
  身份变化、视图绑定、首次写入状态衔接，以及生产 prepare_plugin_sql 在受控
  保存点服务下的首次/重复登记、已有语句和保存点失败路径。没有启动实库。
- 19 项 Python 脚本回归通过（88256），相关 CTest 4/4 通过（22120，1.05 秒）：
  query transaction、install coordinator、kernel build gate、query catalog runner。
  新二进制生成于 13:04，SHA-256 为
  `b5c2a0acf822a60b57d335ff8090da978144f8f66ade8d5d15a9e82e36e18208`。
- Rust text ABI/实现未变；当前二进制在 `build_plugin_overlay_verify/src/observer/seekdb`。
  仍需用户正常停库、替换服务端后重跑实库矩阵；不宣称实库事务已通过，也不删除
  用户失败现场或自动重启测试实例。

## 交付核对（2026-09-10）

用户要求明确收尾后，暂停新增扩展点。完整目标尚未完成，不再以不断追加小功能
代替验收。当前代码与原设计的对应关系、未通过的验收条件及下一步顺序见
[交付验收清单](plugin-delivery-audit.md)。这不是把完整目标改成阶段目标。

本次发现公开 `catalog_spi.h` 未加入 SDK 安装规则；已补齐。新增独立安装回归
先复现缺失失败，再验证 11 个公开头文件逐一以 C/C++ 编译（22 个编译单元）。
消费者只使用安装后的 `seekdb::plugin_sdk`，不借用源码 include 路径。
回归加入 CTest 为 `plugin_sdk_install`；此前完整 33/33 不包含该新增测试。
独立测试工程仅重新配置，新增 CTest 单项通过（88119，1/1，3.63 秒）；Python
语法与 diff 检查通过。未重新编译生产服务端，未将此单项结果宣称为完整 34/34。

## 当前推进（2026-09-10，SQL 实时内存观测）

- 新增只读 oceanbase.__all_virtual_plugin_memory（本地定义 ID 12565），接入
  schema 生成、Observer 源码清单、虚拟表工厂及 runtime 快照转发。原
  SHOW PLUGINS 继续读取持久化 catalog，未改成混合静态/动态信息。
- SQL 适配读取 Rust 账户已有计数，按 module generation 展示当前/峰值字节、
  存活/峰值分配数、拒绝/无效释放、额度及采样时间。无符号 64 位列保留
  UINT64_MAX，无新增监控线程、系统表写入或插件实现依赖。
- 要求 PROCESS 加正常对象权限；扫描拥有复制后的字符串/数值，不跨行保留
  runtime 指针或代码 lease。关闭/重开/reset 清理旧快照，采样异常转错误码，
  失败保持到重新打开，不交付部分行。
- 已新增真实 SQL 行适配回归：schema 类型、权限、投影、空快照、失败、扫描
  稳定性、重开，以及真实 Rust words 的 3 字节/额度拒绝/关闭归零。这里仍用
  显式 catalog 激活夹具和内存会话权限，不等于实库 SQL 授权/并发恢复验证。
- schema 生成清单 7 项检查通过（67794）。首轮生产全量重建通过（86508），
  补齐重开与异常路径后的最终生产重建通过（76408）。禁用插件的真实 CLI 与
  内存虚拟表均编译/符号检查通过，无插件 runtime 符号依赖。Runtime 136 项
  单测通过（9336）。内核首轮（81915）发现夹具回调的枚举推导返回类型不匹配
  int32 ABI，显式指定 seekdb_plugin_status_t 后完整 kernel 通过（57993）。
  新用例验证真实 Rust 游标持有 3 字节、第二次分配拒绝、关闭后新扫描归零、旧
  快照稳定、loader 销毁后字符串仍有效，以及权限、类型、重开、错误/异常清理。
  Rust 严格静态检查和格式通过（56668）。对应 DSO 重建/审计通过（38157），
  独立测试重建通过（90674），完整 CTest 33/33 通过（77972，105.54 秒）；
  最终日志结束于 Sep 10 11:54 CST，交付核对时确认含 33 项 Test Passed。
  Candidate 28 项（15646）、Rust text 4 项（71009）、12 项门禁（20678）
  及最终 diff 检查（27174）通过。后续按需加载、模型/任务、
  catalog/type/index/planner 等完整目标保持不变；GIS 仍使用 C++。

## 前一检查点（2026-09-10，管理员启动额度）

- Rust memory_limit 新增无分配严格解析器，通过 host C 桥接给真实命令行入口使用。
  两个启动参数分别设置每个 module generation 的 payload 字节和存活分配数，
  默认 unlimited，0 拒绝；字节支持精确 KiB/MiB/GiB/TiB。拒绝符号、空白、
  小数、模糊单位、尾部垃圾和溢出，不把无效值写回原选项。
- ObServerOptions → ObServerPluginRuntime → 原 loader 的 PluginMemoryLimits
  已接线。初始化日志记录生效额度；未增加线程、SQL SET 或持久化配置。
  SHOW PLUGINS 仍是 catalog 状态，不冒充实时 memory telemetry。
- 首轮生产构建通过（36086、78886），Rust 测试字面量中的歧义转义按 Clippy
  修正后，严格检查通过（97256），runtime 全量 136 项单测通过（13473）。
- 真实二进制参数检查（80962）发现 ob_main 没有继承 ob_server 的 PRIVATE
  插件编译开关，错误返回 OB_NOT_SUPPORTED；已补齐 ob_main 的编译定义和
  Rust host 依赖。完整 kernel 首轮（56196）也在真实 parser 新断言处失败，
  与二进制复现一致，没有削弱断言。
- 修复后生产重建通过（43086），完整 kernel 重新通过（1721）：实际二进制
  携带两个参数的 --help 成功；禁用插件的真实 CLI 目标文件编译/符号检查通过；
  原生 parser 错误矩阵、解析结果到 Observer runtime 初始化、SDK 安装检查及
  既有真实 DSO/SQL/表函数/排序/JOIN/codegen 回归全部保留。
- 生产对应 Rust candidate/text DSO 重建与二进制审计通过（90271）；独立测试
  重建通过（97645），完整 CTest 33/33 通过（98882，118.13 秒），含真实 C
  插件从 Rust 解析额度到分配拒绝/恢复、GIS、Rust owned/borrowed 内存和 SDK。
  Candidate 28 项单测（28610）、Rust text 4 项单测（51025）通过；12 项构建
  门禁、格式、源码边界及 diff 检查通过。禁用 CLI 对象检查不等于整个禁用插件
  版本构建，离线夹具不等于实库安装/恢复、并发事务、跨平台或性能证明。
- 管理员启动额度这一接线已验证；实时用量展示和更高层资源生命周期仍待实现。
  额度仅覆盖宿主 payload，不是插件自建堆、GPU、租户/进程总预算；按需加载、
  模型/后台任务、catalog/type/index/planner 等原目标继续推进。GIS 保持 C++。

## 前一检查点（2026-09-10，独立 owned 字节与 Rust 游标）

- 新增可选 Host API v3 memory 后缀及 owned-byte 描述符，v1/v2 前缀保持原布局。
  Rust token 独立保留内存账户；根销毁关闭新分配，最后一个根/token 引用释放才
  销毁账户。raw/owned 共用额度，但拒绝对 owned 数据使用普通 host.free。
- SDK OwnedHostBuffer 可离开分配器作用域并跨线程移动/只读共享；Drop 调用宿主
  release，不访问原 HostContext，不持有 query 指针或自动固定插件代码。Rust text
  v15 words 系列把 String 改为这类字节，游标既有 lease 继续负责代码生命周期。
- Runtime 9 项内存测试通过（41935），含根销毁后读取/跨线程释放、最终回收、
  引用回退、混用释放拒绝及共享额度。SDK 全量回归通过（57788），含新增 4 项
  owned 测试、C/Rust 布局与 32 项 doc-test；Rust text 4 项单测通过（27051）。
  Runtime 严格 Clippy 通过（2882）；SDK 对齐判断按 Clippy 提示调整后通过（94549）。
- 生产重建通过（1764），补充新头文件安装规则后再次检查通过（31833）。完整
  kernel 通过（87572），包含只使用私有安装 SDK 编译 memory_spi.h 的新检查及
  既有 SQL/表函数/排序/JOIN/codegen 矩阵。生产对应 DSO 重建/审计通过（44802）。
- 独立首轮构建（11104）发现旧白盒测试直接按 v2 字段访问扩展后的宿主表，改为
  读取保留的 v2 前缀、继续断言完整 v3 struct_size 后重建通过（52681）。增加实际
  HostContext 销毁后跨线程消费 release 的白盒验证。三项内存/白盒专项通过（20253）：
  真实 Rust words 跨回调/worker 关闭、raw/owned 共享额度、拒绝后恢复及字节/lease
  归零均通过。最终完整 CTest 33/33 通过（86671，122.10 秒）。
- 最终 runtime 133 项单测（16441）、生产宿主契约下 candidate 28 项单测（21229）
  通过；Rust text 严格 Clippy（3028）、runtime/SDK/插件格式、源码边界和 diff
  检查通过。SDK 全量回归及 32 项 doc-test 在最终 CTest 中重新通过。
- 上述仍是受控内核、真实 DSO 与离线夹具证据，不是实库并发事务、后台任务、
  进程/租户总预算、Bazel/跨平台或性能证明；独立 token 的泄漏也不会由根销毁强制回收。
- 这里只持有字节，不替代 code/task/model 生命周期。管理员配置、模型按需加载、
  高层资源/取消/租户计费，以及完整 catalog/type/index/planner 等原目标保持不变。
  GIS 继续 C++，本轮没有写语雀。详见 [Rust 宿主分配器](plugin-host-memory.md)。

## 前一检查点（2026-09-10，Rust SDK 宿主缓冲区）

- SDK 新增 HostAllocator/HostBuffer，显式借用既有 host.alloc/free；提供对齐清零、
  单片/多片直接复制、slice 访问及 Drop 配对释放。空缓冲区不消耗额度，不创建临时
  Vec。借用不能逃逸；wrapper 不支持 Send/Sync，也不自动持有 module lease。
- Rust text v14 的 concat3 标量和 batch 路径使用该缓冲区，在已有执行 lease 内通过
  高阶生命周期闭包借用宿主 API；deinit 在 drain 后清除发布的宿主指针。SQL 对象、
  NULL/UTF-8 语义和 16 MiB 算法上限不变，GIS 保持 C++。
- SDK 首轮全量回归通过（92036），含新增 6 项内存测试与总计 32 项 doc-test。
  生产目标检查（4896）、首轮完整 kernel（33827）及 CTest 33/33（78503）通过后，
  补齐空缓冲区指针保持请求对齐的语义和断言。SDK 严格检查指出测试中的冗余
  slice 指针判空，删除该无效断言后全 targets Clippy 重新通过（96222），没有屏蔽 lint。
- 最终完整受控 kernel 重新通过（43885），生产对应 Rust candidate/text DSO
  重建与二进制审计通过（53136），独立目标重建通过（11191）；最终 CTest 33/33
  通过（7450，120.57 秒），包含 SDK 全量回归、32 项 doc-test、GIS、构建门禁与新
  `plugin_rust_host_memory`。真实 Rust concat 的标量/批量在共享模块额度下验证了
  拒绝、空结果不分配、逐行释放、结果交付失败、后续行超额不交付前序暂存结果及重试；
  每次调用后存活字节/分配数和 lease 归零，无无效释放。
- 最终 Rust text 4 项单测（84193）、生产宿主契约下 candidate 28 项单测（18499）
  通过；插件全 targets 严格 Clippy（81007）、SDK/插件格式检查通过。Runtime 本轮
  未改动，其 130 项单测仍通过（12308）；源码边界和 diff 检查通过。
- 这些仍是受控内核和真实 DSO/离线夹具证据，不是实库 catalog 事务、完整租户
  预算、Bazel/跨平台或性能证明，不能将单行缓冲区峰值等同整个 batch 的总内存。
- 该封装覆盖同步临时字节，不禁止 native 插件使用 ABI 管理长期模块内存；长期/
  跨线程资源仍需独立的 owned token 和模块固定协议。管理员额度配置、完整资源核算、
  动态库/模型按需初始化，以及 catalog/type/index/planner 等原目标继续保留。
- 详见 [Rust 宿主分配器](plugin-host-memory.md)。本轮未更新语雀。

## 前一检查点（2026-09-10，Rust 模块宿主内存账户）

- 现有 host.alloc/free 改由 Rust MemoryAccount 处理，按 Module generation 独立
  校验所有权、分配/释放及共享额度。C ABI 签名不变；默认不施加新限额，宿主可在
  loader.init 显式配置每模块 payload 字节和存活分配数上限。
- Runtime 快照新增当前/峰值字节和分配数、失败/无效释放次数及额度。空账户不创建
  跟踪表、不启动线程；这不是零开销/RSS 结论。Rust Vec、第三方/GPU 内存及查询/
  租户开销不被该账户自动覆盖，SQL/命令行配置与 SDK buffer 包装仍需后续接入。
- 错误 owner/size/alignment 或未知地址不被释放、不扣额度。正常 deinit 后关闭
  新分配，仍保留/允许归还旧块；终止独占销毁时才回收余留字节。既有 lease/drain/
  不安全时保留整个 loader 域的规则不变，不把原生指针记账当成安全沙箱。
- 生产重建（96176）及完整受控 kernel（72245）通过，既有真实 Rust DSO、排序替代、
  JOIN、SQL 表达式与 codegen 矩阵保留通过。生产对应 Rust candidate/text 动态库
  重建与二进制审计通过（56585）。
- 独立目标首轮构建（19885）发现两个旧白盒夹具仍使用无参数 HostContext 构造，
  改为显式传入默认 PluginMemoryLimits 后重建通过（42465），未放宽测试断言。
  C 布局与真实 C SQL extension cursor 的内存专项通过（24259）：字节/数量拒绝、
  拒绝后已有 cursor 继续执行、释放后额度复用及 lease/存活内存归零。
  完整 CTest 32/32 通过（11707，124.45 秒），包括 GIS 与 Rust 插件既有检查。
- 最终 runtime 130 项单测通过（10444），含新增 6 项内存测试：八线程共享额度、
  布局/整数上限、关闭、错误释放及空句柄。Clippy 全 targets 严格检查通过（17082），
  十二项构建门禁（73876）、源码边界（78297）及 Rust 格式/diff 检查通过。
- 这些是生产构建、受控内核、真实 DSO 与离线夹具证据，不是实库 catalog 事务、
  租户资源预算、Bazel/跨平台或轻量化性能验证。本轮没有更新语雀文档。
- 详见 [Rust 宿主分配器](plugin-host-memory.md)。动态库按需加载、模型初始化、
  统一资源预算及完整 catalog/type/index/planner 等原目标保持不变，GIS 继续 C++。

## 前一检查点（2026-09-10，查询期 catalog 实库验收入口）

- 新增 `query_catalog_server.py`，经真实 Rust text 公开 lookup/mutation 入口
  组织自动提交、调用者 DML/routine 一起提交、跨语句 SQL/PL 依赖、属性 ALTER、
  DROP/重建、用户保存点、语句/整体回滚、自动及调用者 ACL、旧缓存与非成员语义。
  这是新增可执行验收矩阵，不是上述数据库行为已经通过的声明。
- 用成功的两行聚合写入作对照，再令依赖该聚合的浮点 EXP 溢出，避免把未确定的
  SELECT 目标求值次序当作“写入后报错”。精确错误码拒绝超时/断连等假阳性；
  CALL 排空全部结果；清理失败不报 PASS、不覆盖原始失败。
- 只使用随机 fixture 数据库/用户，强制 loopback、端口及一次性实例确认；
  不安装包、不改变全局配置。成功删除自己创建的资源，失败回滚/关闭后保留诊断对象。
- 18 项依赖无关的脚本检查在普通 Python 与 `python -O` 下通过（17756）；
  语法与 diff 检查通过（96498）。独立目标重建通过（18497），完整 CTest 31/31
  通过（18573，111.20 秒）。
  新 CTest `plugin_query_catalog_runner` 仅验证验收脚本，不是实库测试。
- 当前 socket 创建重新探测仍因 EPERM 失败（33123），没有启动数据库或执行实库矩阵。
  并发 DDL、提交失败/未知结果与恢复也尚未加入此脚本，不能借用离线测试证明它们。
  生产内核、Rust ABI/算法与 GIS 本轮未改动；完整实现目标不收缩为测试入口。
- 详见 [查询期 catalog](plugin-query-catalog.md)。本轮没有同步写入语雀。

## 前一检查点（2026-09-10，计划边界值与延后计算）

- v8 / service minor 7 增加 value_info；宿主根据实际算子边界区分
  已有值与可拆解的标量计算。Rust SDK 增加 VALUES、value 与
  required_values，保留聚合／窗口结果，不盲目下探原始输入列。
- v18 排序策略使用所需已有值构造布局，SELECT 计算保持原位置。
  新增正常 SQL 检查聚合／窗口后的排序替代及延后投影，保留此前矩阵。
- 首轮生产编译（36525）修正容器遍历后，重建通过（35578）；SDK
  全量回归／29 项 doc-test 通过（11817），28 项插件单测通过（67788）。
  完整 kernel（81350）通过：聚合／窗口结果、延后投影和已有排序共
  24 次真实替代检查通过，保留 Top-N、物理、JOIN 等既有矩阵。
  生产对应 v18 DSO 重建／审计通过（83819）；独立目标重建（16682）
  后 CTest 30/30 通过（24221，115.12 秒），生产宿主契约下 28 项
  插件单测重新通过（28778）。十二项构建门禁／格式／diff（78523）
  与源码边界检查（50683）通过。
  最终 runtime 124 项单测重新通过（50080）。
- 详见 [计划边界值](plugin-plan-values.md)。完整目标不缩减为这一项接口。

## 前一检查点（2026-09-10，Rust 排序算法替代）

- v17 upper-policy 开始用 Rust 策略和 SSO1 执行器替代支持的原生
  SORT，保留原 SORT 的子输入，不是再包一层 spool。多键方向／NULL、
  owned 载荷、可中断堆排序和有界资源已实现；其他上层算法保持原生。
- SELECT 布局保留直接列、常量及已有排序键，不提前执行其他计算。
  后续仍需阶段感知的需求／可用值协议，不把全查询列当作当前 schema。
- 二十八项插件单测（78627）与首轮生产目标检查（98910）通过；完整
  kernel（28737）九组物理用例通过，在上层普通降序的真实替代断言
  失败。已修正 core 整数逻辑标记被误判为 OTHER 的宿主语义，并补充
  CASE 递归语义及测试；生产重建（70068）通过。第二轮 kernel
  （70560）发现新增夹具漏填合法类型元数据，补齐后完整 kernel
  （1548）通过：九组物理、十组新增排序 SQL、十组既有上层 SQL、
  三十四组 JOIN SQL 全部通过，替代断言不放宽。
- 生产对应 v17 DSO 重建／审计通过（90167）；十二项构建门禁、
  Rust 格式与 diff 检查通过（16464），源码边界通过（37962）。
  独立目标重建（70753）后 CTest 30/30 通过（29151，102.34 秒）；
  生产宿主契约下 28 项插件单测（31112）、runtime 124 项单测
  （63906）重新通过。
- 详见 [Rust 排序](plugin-rust-sort.md)。完整目标保持不变，不将有界
  整数参考排序当作通用排序、spill、catalog 或 AI 能力已经完成。

## 前一检查点（2026-09-10，版本化排序语义）

- v7 规划上下文／service minor 6 接入 SORT 元数据与规范化排序键；
  宿主区分非 SORT、特殊排序与错误，读取不调用 get_op_exprs。
- Rust SDK 新增 Hook::SORTS、Sort/SortKey 及受调用期约束的表达式
  身份；loader 校验新后缀，同时给旧 service 精确旧前缀。
- v16 参考插件的 upper-policy 实际读取排序键与可选表达式。仍是
  保留原生结果的 spool，不是独立 Rust 排序算法已经完成。
- SDK 首轮回归通过（36876），补充缺失能力测试后全量重跑通过
  （13587，含 28 项文档测试）；生产构建通过（21458）。完整 kernel
  通过（69372），含新增 SORT 协议夹具、十组上层与三十四组 JOIN
  SQL 及既有矩阵。生产对应 v16 动态库重建／审计通过（90469），
  独立重建通过（59945）后 CTest 30/30 通过（72912，105.57 秒），
  包含真实 Rust 四阶段读取和旧 service 精确前缀。
- 生产契约下二十一项插件单测通过（44328），runtime 124 项单测
  通过（38742）；十二项构建门禁／格式检查通过（65417），源码边界
  及 diff 检查通过（91252）。无新增实库 catalog、Bazel／跨平台或
  性能证明，不将元数据接线等同于排序算法替代。
- 详见 [排序语义](plugin-sort-semantics.md)。下一步直接推进算法替换
  所需的策略、布局和执行器；完整 catalog、类型／索引、PX／缓存、
  资源、AI、轻量化、PL／Bazel／平台目标均保持不变。

## 前一检查点（2026-09-10，上层关系候选）

- 新增 GROUP、WINDOW、DISTINCT、ORDERED 四个独立贡献阶段；内部
  路由改用显式 phase，保留原有 relation/JOIN/select 分工。无注册
  快路径、整组贡献和 ORDERED 保序检查已接线。
- Rust SDK 新增 UpperStage 注册；v15 参考插件增加上层保序候选，
  不是聚合/窗口算法替换。新增 loader 与正常 SQL 的阶段验证用例。
- 二十一项既有插件单测通过（85460），生产构建（25409）及 SDK 全量
  回归/27 项文档测试（44003）通过。首轮 kernel（71325）在无聚合
  GROUP BY 用例的阶段计数断言失败；改为真实 MAX 分组并增加重复值
  DISTINCT 及阶段诊断后，完整 kernel（8999）通过：新增十组上层
  SQL 在标量/batch=3 下验证四阶段贡献、正常 codegen、真实 Rust
  执行、结果和重扫；保留三十四组 JOIN SQL 及既有矩阵。生产对应
  动态库重建/审计通过（9646），独立目标重建通过（71561），完整
  CTest 30/30 通过（28807，108.98 秒），包含真实四阶段 Rust 回调、
  错误/非串行回退和独立打包；格式/diff 及十二项门禁通过（60651）。
- 生产宿主契约下二十一项插件单测及 124 项 runtime 单测重新通过
  （63284），源码边界/最终 diff 通过（32262）。未新增 Bazel、跨平台、
  性能或实库 catalog 验证；通用上层算法还需语义及构造协议。
  详见 [上层关系候选](plugin-upper-paths.md)。完整目标不缩减。

## 前一检查点（2026-09-10，Rust 局部相关关系）

- Rust v14 在独立子问题回调中启用两输入及多拥有者相关策略，统一
  当前关系的 outside 列检查，保留完整绑定、隐藏列与原完整关系策略。
- 新增三表/四表 SQL，验证相关插件子计划与上层原生外连接组合；
  四表可以组合嵌套的插件实现，不把强制展平作为正确性的条件。
- 二十一项参考插件单测、生产目标检查（72974）通过。首轮 kernel
  在四表测试布局断言处失败：断言放错测试分支；三表相关执行及四表
  嵌套插件正常规划/codegen 已执行到。修正测试分支并增加参数槽
  唯一性检查后，第二轮完整 kernel（54630）通过：三十四组 JOIN SQL，
  新三表/四表相关子问题在标量及 batch=3 下通过正常 codegen、结果、
  取消和整体重扫；四表实际组合两个嵌套 SJC1，合计五个独立参数槽，
  上层保留原生 HASH OUTER JOIN。生产对应 v14 DSO 重建/审计通过
  （83338），独立测试目标重建（64954）后完整 CTest 30/30 通过
  （75904，116.18 秒）。生产宿主契约下二十一项插件单测及 124 项
  runtime 单测重新通过（19377）；十二项 kernel 门禁、Rust 格式、
  源码边界及 diff 检查通过（66507）。未新增 Bazel、跨平台或性能
  基线验证，也不将受控 schema/session 夹具当作实库 catalog 证明。
  详见 [连接子问题](plugin-join-subproblems.md)。完整目标继续保留。

## 前一检查点（2026-09-10，独立 JOIN 子问题路由与 Rust 策略）

- 新增连接枚举 hook、注册可用性门控、独立 provider/loader 路由；
  暂存全部 PluginPath 后发布，记录已处理原生路径，避免重复贡献。
  早期构树路径不再回收复用，并保护共享缓存子树的原父指针。
- Rust v13 注册独立子问题策略，按当前关系作用域省略其他关系列；
  保留原完整关系的相关/多层策略。新三表 SQL 验证目标是内层 Rust
  与上层原生 LEFT JOIN 组合，不是用整树替代冒充中间关系接入。
- SDK 回归与十九项插件单测、生产构建（93743）通过。首轮 kernel
  在参考插件加载处因测试元数据仍为 v12 失败，已同步为 v13 并修正
  注册数量。第二轮在局部插件关系的原生 LEADING 打印处失败，修正
  原生 JOIN 假设后生产重建（79760）、完整 kernel（17174）通过：
  二十六组 JOIN SQL，新增真实三表内层 Rust/外层原生 HASH OUTER
  JOIN 组合，标量/batch=3、结果、EXPLAIN、取消和重扫通过；既有
  完整关系策略保留。生产对应 DSO 构建/审计（60629）通过；独立
  重建（50151）后 30/30 CTest（52721，109.58 秒）通过，十二项
  门禁、格式和源码边界通过。未新增 Bazel/跨平台或性能基线验证。
  生产契约下十九项插件单测、124 项 runtime 单测也已重新通过（83314）。
  详见 [连接子问题](plugin-join-subproblems.md)。

## 前一检查点（2026-09-10，连接枚举的独立插件 Path）

- 新增 PluginPath 及 ObJoinOrder 发布入口，区分关系类型与实现类型；
  复制逻辑属性/参数约束、委托代价重估，避免错误的内置 Path 转换。
  原生谓词下推暂保留原生替代；不是 Rust 子问题回调已接入。
- 新增宿主夹具和 kernel 新鲜度输入。首次生产编译遇到 const ret
  与 OB_SUCC 写入宏冲突，修正后生产重建通过（39957），完整 kernel
  通过（30755），含路径身份/属性/参数约束、代价重估及错误、原生
  共存和树复用夹具；二十二组 JOIN SQL 与既有执行矩阵保留通过。
  后续重点包括早期逻辑树引用与 JoinPath 回收、独立阶段路由、关系
  作用域及真实 SQL 组合。详见 [连接子问题](plugin-join-subproblems.md)。
- 生产对应 Rust DSO 重建/二进制审计（69577）、十八项插件单测、
  独立目标重建后 30/30 CTest（11960，107.11 秒）、十二项门禁和
  源码边界通过；本轮没有新增 Bazel、跨平台或性能验收。

## 前一检查点（2026-09-10，Rust 自动多层相关候选）

- Rust 策略已能识别满足条件的左深相关 INNER JOIN，生成 SJD1
  多来源绑定/输出/调度请求；宿主增加交接与保留 owner 的参数槽
  别名检查。正常三输入 SQL 用例已加入，不以独立夹具替代端到端。
- 生产构建通过（15513），十八项插件单测、runtime 124 项、SDK
  119 项及 27 项 doc-test、十二项门禁及源码边界检查通过。
- 完整 kernel 首轮及诊断轮在新增规划夹具构造中段错误，原因是
  参数索引 setter 在来源表达式设置前读取来源类型。修正后完整
  kernel 通过（87369）：二十二组 JOIN SQL，新增正常三输入相关
  SQL 在标量/batch=3 下比较原生与真实 Rust 自动候选；正常 codegen、
  SJD1 三输入/五参数映射、结果、取消清理及整体重扫均通过。
  SQL 使用 LEADING/USE_NL 指定原生形状；仍非任意规划子问题或实库
  catalog/PX 验证。六组参数图及十八组多输入物理用例保留。
- 独立目标重建通过；首轮 CTest 28/30，两项在 LLVM DSO 链接阶段
  崩溃，kernel 结束后串行重跑 30/30 通过（43052，101.90 秒）。详见
  [多输入参数化](plugin-multi-parameter.md)。完整目标继续保留。
- 生产对应 Rust DSO 已重建并通过二进制审计（8380），格式检查通过。
  未新增 Bazel/跨平台验证；后续继续扩大参数拥有权和规划阶段。

## 前一检查点（2026-09-10，多 owner 参数化片段与 Rust 三输入调度）

- 参数请求不再固定来源 0、目标 1。被替代 JOIN 子图可交接多个完整
  NestedLoop 参数 owner，输入可乱序；每个 owner 的整个右子树须
  保留为一个目标输入，来源属于它的左子树。支持链式依赖与多来源
  汇入，不支持拆分消费者、共享参数多目标、above-pushdown 或跨块。
- 普通片段也检查已知参数 owner，避免绕过绑定请求丢失执行参数。
  规划检查不调用会修改计划的 get_op_exprs；失败不改动原父子链接。
  codegen/spec 保存来源输入、扁平槽与目标输入，不保存调用期图 ID。
- C++ 按来源独立保存 Datum 快照、按目标更新 setter，按 Rust 传递
  失效集合清理；所有存活来源快照共享 16 MiB payload 上限。Rust
  SJD1 按显式拓扑调度进行多输入读取、绑定、重扫与回溯，至多保存
  每路一行。SJE1/SJR1/SJC1 默认策略保留；SJD1 自动规划尚未接入。
- 首次生产构建因 ObIArray 不支持范围 for 失败（48162），索引遍历
  修正后重建通过（39525）。完整 kernel 通过（40809）：新增多个
  参数 owner 的规划夹具、六组三输入物理用例，保留十八组多输入
  物理及十八组 JOIN SQL。覆盖乱序链式/多来源、NULL/空值/70 KB、
  空中间输入、绑定重扫失败、恢复/关闭、无关参数和非法图准入。
- 新增多层规划与三输入物理执行是两组独立夹具，不是正常多层 SQL
  经策略/codegen/执行的端到端证明；既有两输入相关 SQL 的真实 Rust
  策略和正常 codegen 保持通过。实库 catalog/MVCC/PX 也未由此证明。
- runtime 124 项、SDK 119 项及 27 项 doc-test 通过；独立测试重建
  （1242）后 30/30 CTest 通过（16783，102.94 秒）。生产对应 Rust
  插件重建/审计通过（4433），十六项插件单测、十二项门禁、格式/
  源码边界通过。本轮没有运行 Bazel 或新增跨平台验证。
- 详见 [多输入参数化](plugin-multi-parameter.md)。下一步继续做普通
  多层相关 SQL 的 Rust 候选策略和端到端验证，并扩大参数拥有权。
  完整规划阶段、跨查询块/PX/缓存、实库 catalog、类型/索引、AI 与
  轻量化目标保持不变，不以三输入参考调度代替。

## 前一检查点（2026-09-10，Rust 输入依赖与传递失效）

- 新增 Rust 游标级输入状态机，管理当前行、有效参数环境、传递失效和
  READ/RESCAN/BIND 的开始/完成票据；支持 64 输入、4096 边记录的 DAG，
  多来源绑定要求全部当前来源就绪，无关分支保留。构造后不分配内存，
  不保存宿主值指针，也无 Rust 借用跨越子算子调用。
- C++ 自定义执行器已用 Rust 状态替代 source_ready/target_ready，仍
  管理 Datum 快照、setter、ParamStore 与子算子。错误/陈旧完成使
  状态失败，成功的整个算子重扫才能恢复；失效清理不改动无关参数。
  实际拥有权与值映射仍是根 NestedLoop JOIN 的 0 → 1，不把通用图
  测试描述成任意参数化子图已开放。
- Rust runtime 124 项测试通过（40288），含新增六项图状态测试；
  穷举四输入全部 4096 个无自边图，与独立 DFS 对照 543 个 DAG 的
  就绪及失效。C++ bridge 增加实际链接 archive 的布局、多来源、
  链式重扫、错误与票据测试；kernel 新鲜度门禁纳入新的 Rust 模块。
- 首次生产构建因 SQL 目标缺少裸内部头文件搜索路径失败（81145）；
  改用现有仓库根 include 路径后重建通过（80365）。完整 kernel
  通过（21797），保留十八组多输入物理用例与十八组 JOIN SQL，含
  相关 SQL 原生/Rust 结果、NULL/字节/LOB 参数快照、取消和来源重扫。
  schema/session 与部分输入仍有夹具，不证明实库 catalog 或 PX。
- SDK 119 项测试与 27 项 doc-test 通过；独立重建（20989）后 30/30
  CTest 通过（26462，108.73 秒）。生产对应 v12 插件重新构建/审计
  通过（22104），十四项插件单测、十二项门禁、格式/源码边界检查通过。
  本轮没有执行 Bazel 或新增跨平台验证。
- 详见 [Rust 输入状态](plugin-input-state.md)。后续继续扩大规划期
  参数拥有权与多输入物理映射，而非只放宽输入数。完整规划阶段、
  跨查询块/PX/缓存、实库 catalog、类型/索引、AI 与轻量化目标保留。

## 前一检查点（2026-09-10，参数化片段与 Rust 相关 JOIN）

- 后续快照回归已随完整 kernel 通过（91380）：新增 VARCHAR/in-row
  LOB 参数物理用例，含 NULL、空值、NUL/UTF-8 和 70 KB 数据；绑定
  前改写来源 buffer，验证 host-owned 快照、两个绑定及无关参数
  不变。目标重扫失败、整体重扫失败、提前 close/重开均验证清理。
  来源独立重扫已实现，生产重建（10949）和再次完整 kernel（23099）
  通过。EOF/重复重扫、来源失败、过早读取目标或重新绑定均验证；
  先撤销旧快照/参数，再要求新的来源行与绑定，不恢复陈旧环境。
  十八组多输入物理用例和十八组 JOIN SQL 保留通过。
- 来源重扫这轮的 SDK 119 项测试、27 项 doc-test 重新通过；独立
  测试重建后 30/30 CTest 通过（104.42 秒），含 47 个原生执行
  协议变体及 Rust loader、打包、脚手架和构建边界。
  生产宿主对应的 v12 插件构建/审计（60614）、十四项插件单测、
  118 项 runtime 单测、十二项构建门禁及格式/源码边界检查通过。

- custom_path_request_v4 承接根 JOIN 的完整 NestedLoop 参数拥有权；
  custom_context_v4 / minor=2 增加 bind_rescan_input。宿主复制来源
  Datum 快照、绑定并重扫右子算子，来源推进使旧绑定失效，整体重扫/
  关闭清理 owned 参数。初版保留原两子树，不支持 above-pushdown 或
  任意参数 frame 写入；SDK 与旧版本精确前缀保持明确能力边界。
- Rust v12 贡献 SJC1 相关 INNER JOIN，策略和执行循环都来自 Rust；
  保留子计划的相关函数/谓词，不调用内置 JOIN 算法。独立输入 SJE1
  和显式流式 SJR1 保留，不因有 rescan 就假定任意输入可重复执行。
- 生产宿主重建通过（16190），首轮完整 kernel 通过（40271）：
  十八组 JOIN SQL，十组 Rust、八组原生；相关 SQL 在标量/batch=3
  分别执行两策略，对比结果并验证两个参数、取消/重扫与参数清理。
- 后续 119 项 SDK 测试、27 项 doc-test 与十四项插件单测通过。
  首次 SDK 重跑遇到 LLVM 链接崩溃，独立构建结束后重跑通过。新增
  loader v4 负向变体随重建后的 30/30 CTest 通过（103.89 秒），
  原生执行协议矩阵共 47 个变体。绑定后取消 SQL 随完整 kernel
  再次通过（85190），第一/第二次成功绑定后取消、重扫清空参数并
  恢复完整结果；十八组 JOIN SQL 与全部既有执行矩阵保留通过。
- 118 项 Rust runtime 单测、十二项 kernel 构建门禁、格式/源码边界
  检查通过；生产宿主对应的 v12 DSO 构建/二进制审计通过（60351）。
- 详见[参数化执行](plugin-parameter-execution.md)。完整规划/参数
  依赖、分布式/PX/缓存、实库 catalog、类型/索引及 AI/轻量化目标保留。

## 前一检查点（2026-09-10，逐输入重扫与 Rust 流式执行）

- 新增 custom_context_v3 输入控制后缀；执行服务 minor=1 理解可选
  rescan_input，旧服务保留 v1/v2 前缀。宿主重扫单个实际子算子，
  Rust SDK 提供能力检查、借用约束和错误状态保持，不重置兄弟输入。
- Rust v11 新增 SJR1 显式流式整数 JOIN，保留至多一行左/右输入，
  为每个非 NULL 左键独立重扫右输入。默认策略仍使用 SJE1；易变性
  和重复执行语义不能由“支持 rescan”推导，不擅自替换现有 SQL 策略。
- 生产构建通过，113 项 SDK 测试和 26 项 doc-test 通过；首次 SDK
  构建遇到 LLVM 链接器崩溃，生产链接结束后重跑通过。kernel 新增
  四组流式物理用例随完整 kernel 通过（进程 50521），首行流式返回、
  重复/NULL 键、空输入/零列、独立重扫次数和失败/取消恢复均通过；
  十六组既有 JOIN SQL 保留通过。loader 新增十三种变体，独立测试
  已重新构建，30/30 CTest 通过（105.77 秒），包括全部 34 个 C 自定义
  执行变体、真实 Rust loader、SDK/打包及构建边界。十一项既有插件
  单测、118 项 Rust runtime 单测、十二项 kernel 构建门禁和格式/
  源码边界检查通过；针对最新生产宿主重新生成契约，生产 v11 DSO
  重新构建和二进制审计通过。
- 这一步提供实际子输入执行控制，尚未承接参数 owner/绑定/恢复。
  完整目标及实库 catalog、类型/索引、AI/轻量化验收继续保留。
  详见[逐输入重扫](plugin-input-control.md)。

## 前一检查点（2026-09-10，执行参数绑定检查）

- 新增 candidate_context_v6 / minor=5，公开 JOIN 的三类执行参数
  binding_count/binding，参数与来源表达式共用图身份。旧上下文及
  plan_semantics flags 保持原契约。Rust PARAMETERS 隐含前代能力。
- Rust candidate v10 在移除 JOIN 前检查其绑定，避免独立输入算法
  丢失相关参数的绑定/重扫语义；不等于已经支持插件参数化执行。
- 生产宿主、v10 Rust DSO 构建与审计通过；109 项 SDK 测试和 25 项
  doc-test、十一项插件单测通过。新增绑定身份、错误和能力测试，
  三类绑定策略回退；完整 kernel 与独立 loader 验证通过。
- 详见[规划语义与参数绑定](plugin-planner-semantics.md)。完整目标
  保留，包括真正的参数化执行、更多规划阶段、分布式、实库 catalog、
  类型/索引及 AI/轻量化，而非只防止误选现有参考算法。
- 首轮完整 kernel 在新增相关表函数的 codegen 报 OB_STATE_NOT_MATCH：
  真实 NESTED-LOOP JOIN 已保留 token/ordinal 两个绑定，图接口与
  三类列表测试通过；但优化器参数创建仅复制物理结果类型，丢失
  插件逻辑类型，后续插件 cast 的来源契约不匹配。已在两种创建
  helper 中复制逻辑类型，并让缓存列表入口复用完整创建路径。
  生产重新构建通过；增加普通/插件/存储类型及三入口的无 formalize
  类型拥有权测试，保留相关 SQL 原断言。
- codegen 修正后，相关 SQL 暴露手动执行夹具未预留参数槽的问题。
  gdb 因环境禁止 ptrace 未能取得回溯；依据正常执行上下文初始化
  路径补齐 reserve_param_space 后，完整 kernel 通过（进程 81194）。
  十六组 JOIN SQL 中八组使用 Rust SJE1，八组保留内核计划；新增
  两组相关表函数在标量/batch=3 下验证真实 token/ordinal 参数绑定、
  插件逻辑类型、结果和重扫。三类绑定图及错误检查、三入口类型拥有权
  和既有类型/LOB/SQL 矩阵同时通过。这里不声称 Rust 已接管相关 JOIN。
- 针对修正后的生产宿主重新生成构建契约，v10 DSO 重新构建与审计
  通过；独立 C++/loader 测试重新构建通过，30/30 CTest 通过
  （100.87 秒），覆盖 v6 准入、损坏上下文拒绝和旧服务精确前缀。
  CTest 内 SDK 重新验证通过；118 项 Rust runtime 单测、十二项
  kernel 构建门禁及源码边界检查通过。上述是本地受控验证，仍非
  实库 catalog 事务/权限/并发、完整参数化执行或跨进程 PX 的证明。

## 前一检查点（2026-09-10，Rust 自主 JOIN 规划策略）

- 新增 candidate_context_v5 / minor=4：规范化连接／等式／整数语义、
  表达式依赖范围、已解析查询列清单；Rust SEMANTICS 能力隐含旧版
  query/graph/builders。宿主保留旧服务精确前缀和 sticky error。
- Rust candidate v9 增加独立 join-policy 服务。实际 Rust 回调辨认
  两路键方向、保留上层需要的列、生成映射/计划/参考代价，C++ SQL
  夹具中原有 JOIN 匹配和构造策略已移除。无须猜测宿主枚举编号。
- 生产构建通过；106 项 SDK 测试与 25 项 doc-test 通过，包含新布局、
  13 组语义／错误用例和九种不完整 context。八项既有插件单测通过，
  v9 生产 DSO 构建与边界审计通过。完整 kernel/loader 后续验证结果
  见下文；未把通用规划或完整插件化目标记为完成。
- 详见[规划语义与 Rust 策略](plugin-planner-semantics.md)。
- 本轮 kernel 首次执行停在新图夹具的连接类别检查：直接构造的
  ObLogJoin 没有经过工厂设置 LOG_JOIN 标签。已补齐夹具前置条件，
  保留原断言并重跑完整回归；这次失败发生在新增 JOIN SQL 矩阵之前，
  不构成该矩阵通过或 Rust 策略失败的证据。
- 修正夹具后完整 kernel 通过：十四组无 ORDERED/USE_HASH 提示的
  SQL（七类、标量/batch=3）覆盖整数重复/NULL 键、反向等式、非
  SELECT 排序列及 NULL-safe/外连接/字符串键回退。八组使用双输入
  Rust SJE1，六组保留内置计划；结果对、取消、重扫与资源释放通过。
  既有多输入/类型/LOB/SQL 矩阵保留通过。独立测试重新构建后，
  30/30 CTest 通过（97.71 秒）；源码边界及十二项构建门禁通过。
- 新增实际 Rust 策略 ABI 单测，插件单测共 11 项通过。八组交叉
  场景检查物理左右输入交换、反向等式、跳过首候选、隐藏列及映射；
  十二种不适用情况不贡献候选且继续链；两种宿主错误不被吞掉。
  SQL 实例未发生物理左右交换，该分支证据来自这些单测，不混淆
  两种验证范围。新增代码只在 cfg(test) 下编译。
- 新增单测后再次构建生产 Rust candidate DSO 并审计通过；118 项
  Rust runtime 单测、两处 Rust 格式与 diff 检查通过。尚待完成通用
  规划阶段、参数化/跨查询块与分布式执行、生产成本/资源/spill，
  以及实库 catalog 事务、类型/索引/恢复和 AI/轻量化的完整验收。

## 前一检查点（2026-09-10，提前贡献关系候选）

- 双输入 SQL 首轮及诊断重跑均未通过。诊断确认后期选择 hook 收到
  SORT 根，无法匹配其下的 JOIN；十二组多输入物理夹具已执行通过，
  当时不能替代尚未通过的正常 SQL 链路；后续修正结果见下文。
- 新增独立 `optimizer.relation.paths.v1`，在上层排序/聚合等生成前
  追加候选，保留全部原生候选参加正常剪枝/代价比较，不强制选插件。
  loader 要求 Server-dev、builder service 和 Around；Rust SDK
  新增 relation_paths_hook。现有后期选择协议保持独立。
- 新增父指针、原始候选保留、连续贡献、错误不发布测试，以及真实
  Rust/loader 分阶段回归。生产构建通过；SDK 首轮遇到 LLVM 链接器
  SIGSEGV，生产构建结束后按同配置重跑，104 项测试和 25 项 doc-test
  通过，含独立 hook point 注册测试。生产 Rust DSO 构建与审计通过。
- 完整 kernel 修正后通过：EXPLAIN 保留 SORT，上层正常代价比较选中
  双输入 PLUGIN CUSTOM；四组正常 SQL 用例覆盖重复/NULL 键、标量与
  batch=3、完整左右结果对及取消后重扫。五种贡献/失败检查通过，
  十二组多输入物理夹具与既有排序/类型/SQL 矩阵保留通过。规划策略
  仍为受控 C++ 回调，SJE1 算法来自 Rust DSO；不据此宣称实库 MVCC
  或自定义跨进程 PX 完成。
- 独立测试重新构建通过；30/30 CTest 通过（86.80 秒），其中真实
  Rust candidate/loader 扩为 20 个变体，新增七个贡献阶段准入、
  独立路由和错误传播用例。118 项 Rust runtime 单测通过；源码边界、
  12 项 build gate、Rust 格式和 diff 检查通过。生产/kernel 验证期间
  保持对应源码不变，未删减失败用例。
- 详细契约及边界见[关系候选贡献](plugin-relation-paths.md)。未完成
  通用 Rust join 策略、全部规划阶段、分布式或完整插件化目标。

## 前一进展（2026-09-10，Rust 双输入连接与 SQL 联动）

- Rust candidate v8 新增 SJE1 整数等值内连接，读取两路 owned 数据，
  NULL 键不匹配、重复键产生全部结果对，输出按左右独立列映射，
  不调用宿主 join。逐对输出避免预先展开平方级结果，输入预算共用，
  读取/匹配检查取消，rescan 释放行缓存及容量。
- 八项插件单测通过（新增计划格式、边界与整数键三项）；生产 Rust
  动态库构建与二进制审计通过。正常 SQL 新夹具通过 v3 请求提交
  双输入依赖，拟覆盖取模重复键、NULLIF 键和取消后的重扫；完整 kernel
  失败于未生成 PLUGIN CUSTOM，原因及后续修正见上方最新进展。
- 规划策略仍是使用 graph/build 的受控 C++ 测试回调，执行算法为
  Rust DSO；默认 Rust hook 没有自动选择 join。完整目标继续包括
  通用 Rust 策略、跨查询块/分布式、缓存/PX、实库 catalog、类型/
  索引/恢复及 AI/轻量化。详见[Rust 双输入连接](plugin-rust-join.md)。

## 前一检查点（2026-09-09，计划片段与逐输入依赖）

- 新增 custom_path_request_v3：目标等价候选与实际输入 PlanId 分离，
  分区表达式依赖接入 LogPluginCustom、裁剪和 codegen；输出仍由
  插件节点产生。Rust SDK 增加 FragmentInput/custom_fragment。
- 目标关系的逻辑属性不再从首个输入推断，代价统计实际输入；当前
  显式支持本地单并发位置契约。构造检查可达、重叠与循环，父子关系
  提交改为先收集验证完整新建分支，再统一写入，不只处理单输入链。
- 生产构建、103 项 SDK 测试和 25 项 doc-test 通过，包含新 C/Rust
  布局与八组输入分区/边界/错误用例。首轮 kernel 编译暴露新夹具中
  PPDeps 缺少类名限定，补齐后按原命令重跑完整 kernel 通过，未修改
  生产实现。新增两输入逻辑夹具验证关系属性、成本、布局、十二类
  错误与父指针；正常 SQL 的 v3 单输入纵向链路覆盖标量/batch=3、
  offsets、派生输出和 rescan。原有十组多输入物理用例、21 组独立
  投影、26 种数值、六组 LOB/codec 和 SQL/排序/PX 矩阵保留通过。
  未宣称多输入 SQL 纵向验证或分布式/PX 构造已完成。
- 生产 Rust DSO 构建/审计、独立测试构建与完整 30 项 CTest 通过
  （88.25 秒），包括匹配新宿主的独立 Server-dev 插件打包、SDK 与
  loader 回归。生产/kernel 构建期间未修改待验证源码，未删减用例。
  最终 118 项 Rust runtime 单测、源码边界、12 项 build gate、Rust
  格式与 diff 检查通过。
- 继续完整多输入 SQL/Rust 策略、关系/执行位置、缓存/PX、实库
  catalog、类型/索引/恢复与 AI/轻量化。详见[计划片段](plugin-plan-fragments.md)。

## 前一检查点（2026-09-09，多输入物理执行与 Rust 轮询投影）

- PluginCustomSpec 新增扁平输入数组的 child offsets；PluginCustomOp
  接纳 0–64 个不同 child，每个输入有独立 schema/codec/数值表示，
  输入回调按 index 读取对应列范围。零 child、零列 child 与旧单输入
  布局分开；每个输入最多 1024 列，传输 scratch 按最宽输入复用。
- rescan 在进入基类 child 递归前进入失败状态，避免中途 child 失败
  后继续使用旧 Rust 缓存；row/batch 入口的失败检查也覆盖 EOF 快捷
  路径。只有所有 child 与 cursor 成功重扫后才恢复。
- Rust candidate v7 增加 SMP1 多路轮询投影。第一行前校验所有输入
  映射与共同输出，按输入编号轮询并复制 owned 行，所有输入共用行数
  和内存限制。默认 planner hook 仍是单输入空计划，未声明 join/
  UNION 规划器已经完成。
- 生产构建及五项 Rust 插件单测通过（其中两项为新增多输入格式检查）。
  首轮 kernel 编译发现新 Input 夹具漏实现基类纯虚 destroy，已补齐；
  未修改生产接口或关闭编译警告。补齐后完整 kernel 回归通过，覆盖
  真实物理适配、loader 与 Rust DSO 的十组正向/拒绝场景，以及
  重扫/输入/取消/最后分支映射失败。原有 21 组独立投影、26 种数值、
  六组 LOB/codec 和 SQL/排序/PX 回归仍保留通过；物理 spec 和子行
  由夹具构造，不将这些结果作为多输入 SQL 规划器已经完成的证据。
- 生产 Rust candidate DSO 构建与二进制边界审计、独立 runtime 测试
  构建通过。完整 CTest 30/30 通过（86.76 秒），包括使用新宿主
  契约独立打包的 Rust 插件、SDK 和 loader；118 项 Rust runtime
  单测、源码边界、12 项 build gate、Rust 格式与 diff 检查通过。
  生产/kernel 验证期间未修改待验证源码。
- 后续继续多 PlanId 的候选构造、关系/位置属性、分配/裁剪、缓存/PX，
  以及实库 catalog、类型/索引/恢复、AI 资源和轻量化。完整目标保持
  不变，接口边界见[多输入执行说明](plugin-multi-input.md)。

## 前一检查点（2026-09-09，查询块的完整 SELECT 目标）

- 新增 candidate_context_v4 / service minor=3，query 返回当前重写后
  查询块的语句种类、SELECT 列表可用性、集合查询标记与目标数量；target
  按 SELECT 序号返回与既有规划图共用的表达式身份，保留顺序与重复项。
  非 SELECT 明确无已支持列表，不能将其伪装为空 SELECT 或返回子节点输出。
- Rust SDK 提供 QUERY_TARGETS、query/target 和生命周期约束。loader
  为旧服务裁剪到原有精确前缀，缺失新接口或非法后缀在调用插件前拒绝。
  Rust candidate v6 实际读取目标元数据，并在构造/next 后核对目标身份；
  默认仍是全列等价 spool，不把完整目标枚举称为任意关系优化策略。
- 生产构建通过。SDK 首次在 ABI 测试链接阶段发生 LLVM ld SIGSEGV，
  未得到测试结果；生产链接结束后用相同配置重跑通过 102 项测试及
  25 项 doc-test，含 C/Rust 布局、14 组查询元数据/句柄用例、七种
  非法上下文及未声明能力拒绝。未更换工具链或降低测试要求。
- 完整 kernel 回归通过。新增受控正常 SQL 用例仅在 SELECT 中
  使用 ABS(ordinal)，从 target 取得该表达式并提交显式布局；要求它
  不出现在子节点输出和本节点重复计算列表，最终值与 rescan 正确；
  标量与 batch=3 都通过，显式布局正常 SQL 用例累计八组。查询图
  的重复/独立目标、集合/非 SELECT 与七种非法访问拒绝也通过，保留
  原有 21 组独立投影、26 种数值、六组 LOB/codec 与 SQL/排序/PX 矩阵。
  正 ordinal 上的 identity 仍是明确的测试策略，不是通用 ABS 算法。
- 生产 Rust DSO 构建/二进制审计与独立 runtime 测试构建通过。第一轮
  完整 CTest 的独立 Server-dev 打包在临时插件 DSO 链接时再次遇到
  LLVM ld SIGSEGV；这不是装载断言失败，且不能据此记为打包通过。
  该轮最终 29/30 通过；保持原配置重跑完整 CTest 后 30/30 全部通过
  （86.98 秒）。独立 loader 验证 v6 拒绝旧上下文及五种非法 v4，
  实际读取两个目标并跨构造/next 保持身份；旧服务仍收到精确旧前缀。
  独立打包还继续验证宿主切换/恢复、导出审计及 Rust 执行器单测。
- 最终 118 项 Rust runtime 单测、源码边界、12 项 build gate、Rust
  格式及 diff 检查通过。生产和 kernel 构建期间未修改其待验证源码，
  没有通过删减用例、更换链接参数或跳过失败检查获取通过结果。
- 后续继续多输入/子树替换、关系与执行位置属性、常量/参数值、批量
  与缓存/PX 重绑定、实库 catalog、类型/索引/恢复和 AI 资源/轻量化。
  本接口只覆盖当前 SELECT 查询块，不开放别名、完整嵌套 AST 或任意
  DML 输出协议。完整目标保持不变，详见[查询目标](plugin-query-targets.md)。

## 前一检查点（2026-09-09，规划期显式输入依赖与输出）

- 新增 custom_path_request_v2，插件通过当前规划图的表达式 ID 声明
  有序输入依赖和输出生产者。两份列表独立、最多各 1024 项，允许
  重复输入和零列，拒绝重复输出及常量/参数等非行级可写输出槽。
  Rust SDK 提供 custom_with_layout，旧 v1 请求保持原有等价路径。
- 逻辑节点拥有已解析的表达式引用，而非借用 ID 数组；输入接入
  ALLOC_EXPR 与 PROJECT_PRUNING，codegen 生成独立布局并标记插件
  输出为本节点产生的值。输出不隐式由 SQL 原表达式重新计算；缺少
  子输入依赖或依赖未声明输出时，拒绝生成使用残留 child frame 的计划。
- 生产构建通过。首次构建因 ObIArray 不支持范围遍历失败，已改为
  索引遍历；99 项 SDK 测试和 24 项 doc-test 通过，含新增 C/Rust
  请求布局与八组正确/错误数据流请求及不可吞掉宿主错误的用例。
- 第一轮完整 kernel 通过：受控策略从正常 SQL 候选图获取表达式 ID，
  通过 build 提交三列输入/两列输出，实际分配/裁剪/codegen 后由 Rust
  spool 执行；覆盖标量与 batch=3、插件类型和计算后的二进制输入。
  另验证 ID 数组被复制、输出表达式可不同于输入、缺失依赖及六种非法
  请求拒绝。原有数值、LOB/codec、独立投影和 SQL/排序/PX 矩阵保留。
- 后续表达式替换已同步接入两份列表，并拒绝改写合并输出身份造成
  同槽重复写入。第二轮 kernel 通过原有用例及替换检查，但新增的
  ABS 派生输出在取行时返回 -4002；保留该失败和用例，后续通过执行
  schema 诊断定位差异（见下项）。常量/参数类
  输出保护与不依赖子行的零参数函数覆盖已补齐并通过生产构建。
- 诊断重跑确认 ABS 失败仅因 BIGINT precision=-1/20 不同，其余字段
  相同。Rust 显式投影现将 builtin 整数的显示精度与编码/范围区分，
  类型、编码、scale、NULL 与物理范围检查不变；浮点/自定义类型/旧
  spool 不放宽。新增回归使参考插件三项单测通过，最终完整 kernel
  通过全部六组正常 SQL 显式布局用例（标量与 batch=3）。派生输出
  断言不在子计划 output 或本节点 calc_exprs，最终 SELECT/rescan
  值正确；图夹具八种非法请求、引用替换、输出合并拒绝和零参数本地
  计算覆盖通过。原有 21 组独立投影、26 种数值、六组 LOB/codec 与
  SQL/排序/PX 矩阵保留通过，未删除失败用例或改动测试构建参数。
- 本轮正常 SQL 策略明确为 C++ 测试回调，算法是实际 Rust DSO；SDK
  的 Rust 请求入口另有测试，不把两者合并描述成通用 Rust 优化策略。
  默认 Rust candidate hook 仍选择全列 spool。后续继续完整查询目标
  枚举、派生表达式/关系属性、多输入、常量/参数值、计划缓存/PX、
  实库 catalog、类型/索引/恢复与 AI 资源/轻量化；完整目标没有缩减。
  协议与验证边界见[显式表达式布局](plugin-custom-layout.md)。
- 后续补齐最终验证：完整 30 项 CTest（91.93 秒）、118 项 Rust
  runtime 单测、12 项 build gate 与格式/diff 检查通过。这些是在
  本次查询目标代码修改前完成的显式布局检查点，不混用为新接口证据。

## 前一检查点（2026-09-09，独立执行布局与 Rust 投影）

- 自定义物理 spec 新增显式输入布局，输入/输出分别保存表达式、逻辑
  ID、NULL 属性和 codec。读入缓冲与数值槽按输入数量分配，待发布行
  按输出数量分配；旧共享布局继续可用，混填 input 字段明确拒绝。
- Rust candidate v5 的 spool 新增私有 SPJ1 投影计划，支持重排列、
  删列、重复列和零列输出。显式投影在首行前验证 schema 与映射；
  输入 decode 与输出 encode 独立，Rust 只拥有和处理逻辑值。
- 生产构建已通过。初次编译暴露 C++ 基类/派生类条件引用不匹配，
  已改为显式返回。投影格式两项 Rust 单测已通过，独立打包 CTest
  也已接入这两项单测，使用其本次生成的宿主契约。
- 完整 kernel 的新夹具初次失败于 get_next_row；检查发现它未设置
  临时 LOB 读取路径也要求的 LOB service。补齐夹具服务后，同一完整
  kernel 已通过 16 组独立输入/输出 LOB/codec 映射，验证重扫、NULL、
  空值、二进制/Unicode、大值、输入仅 decode、输出仅 encode、双向
  codec，以及空输入的越界映射在取行前失败、失败状态与释放。
  临时 LOB 路径另断言未实际读取后端存储，不放宽生产校验。
- 第二次完整 kernel 通过新增五组独立数值/零列布局：三种数值类型
  重排、删列、重复列、零列输出及真正的零列输入，独立检查 ABI 字节、
  SQL 输出槽位、极值、NULL、负零、无穷与重扫行数；NULL 约束收紧在
  空输入取行前失败并保留失败状态。原有 26 种数值、六组 LOB/codec
  与 SQL/排序/PX 矩阵继续通过。
- 最终生产 Rust 插件构建/二进制审计、独立 runtime 构建、完整 30 项
  CTest（81.98 秒，含 98 项 SDK 测试与 24 项 doc-test）、118 项 Rust
  runtime 单测均通过。独立 Server-dev 打包测试使用复制后的插件源码
  执行投影解析测试，并继续验证宿主切换、恢复、非法宿主及导出边界；
  12 项 build gate、源码边界、格式和 diff 检查通过。
- 上述投影使用真实 SQL frame、适配器、loader 和 Rust DSO，但物理
  spec 与子数据仍是明确构造的夹具。普通 SQL hook/codegen 继续使用
  全列等价 spool；没有宣称规划期任意输出映射、多输入、计划缓存/PX
  重绑定、实库 catalog 或索引/恢复、AI 资源与轻量化目标已经完成。
  下一步仍需关联规划表达式身份、输入依赖分配与输出生产者。

## 前一检查点（2026-09-09，规划图与表达式身份）

- 增加 candidate_context_v3 / service minor=2，提供候选根、子节点、
  五类已有表达式角色、表达式元数据和参数访问；宿主用调用期身份表
  保留共享子树和共享表达式，不传递 C++ 指针或假设最终输出 schema。
- 不调用可能生成 access/partition 表达式的 get_op_exprs。只读探测
  不分配内核表达式、不修改父子关系；表达式分配与输出映射仍待接入。
- Rust SDK 新增 INSPECT、PlanId、ExpressionId 和类型化元数据接口。
  Rust candidate v4 使用真实规划图构造 spool，并检查新节点子输入
  的身份在构造和下游 hook 之后仍保持一致。
- 生产构建、98 项 SDK 测试和 24 项 doc-test（含新增 C/Rust 布局）
  已通过。新 kernel 图夹具首次因 ASC 名称歧义编译失败；完整诊断
  确认后已改为实际排序方向 NULLS_FIRST_ASC。随后夹具的非存储插件
  类型漏填 catalog_epoch，被现有契约拒绝；已补齐 epoch，不放宽
  生产校验。受控二元表达式随后暴露未预留参数空间的问题，已改用
  set_param_exprs，并补齐通常由逻辑工厂设置的节点类型。最终完整
  kernel 已通过，保留上述失败证据，未删减图身份和只读断言。
- kernel 覆盖共享子树/参数、五种角色、逻辑类型元数据、八种非法
  图访问及只读边界；实际 ObLogPlan 的失败探测阻止后续构造和候选
  发布。Rust v4 经真实 SQL 规划/执行，原有 26 种数值表示、六组
  LOB/codec 及排序/PX 测试矩阵继续通过。受控图与 schema/session
  夹具不代表实库扫描、权限或跨进程 PX 已完成。
- 最终生产 Rust DSO 构建/二进制审计、独立 runtime 测试构建、118
  项 Rust runtime 单测、完整 30 项 CTest（79.63 秒）、12 项 build
  gate、格式与 diff 检查通过。真实 loader 另验证 v3 插件拒绝 v1/v2
  上下文和八种非法 v3 后缀，调用前无选择/构造副作用；旧 minor 的
  Rust hook 继续通过原有测试。协议与边界见 [规划图说明](plugin-candidate-graph.md)。
- 下一步仍需把逻辑表达式身份接入输入依赖分配与显式输出映射，开放
  多输入物理计划、常量/参数读取、完整关系和属性契约，再继续计划
  缓存/PX 重绑定、实库 catalog、类型/索引/恢复和 AI 资源与轻量化。
  此检查点没有把完整目标缩减为只读探测或单输入 spool。

## 前一检查点（2026-09-09，自定义执行器只读 schema）

- 新增版本绑定的 custom_context_v2、schema/column C 布局，保留原
  v1 前缀。输入 schema 数组与输出 schema 独立描述；列包含逻辑 ID、
  ABI encoding、NULL/stored 属性、SQL 类型、collation 和数值精度。
  元数据在取行前可用，空输入及零列不等同于 schema 不可用。
- SQL 适配器从最终表达式与绑定构建描述，随执行状态拥有；字符串的
  precision union 不再被误读为数值精度。借用只在一次 next 内有效，
  不暴露 ObDatum、locator、表达式指针或内核类布局。
- loader 验证可选上下文后缀及全部 schema，并按对应输入/输出的
  描述校验每行的列数、逻辑 ID、NULL 和编码宽度。已知 core/GIS ID
  的 encoding 不能自相矛盾。错误不能被返回 OK/EOF 的插件吞掉，
  原有 poison/rescan/close 与执行 lease 保留。
- Rust SDK 增加 has_schema、input_schema、output_schema，以及
  借用的 Schema/Column/Encoding。纯元数据查询不触发取行或 SQL；
  旧 v1 明确返回 UNSUPPORTED_ABI。新增编译失败测试阻止 schema
  借用逃逸和跨线程。Rust spool 在首行前检查输入/输出适合等价重放，
  无 schema 的 v1 上下文继续使用既有路径。
- 生产构建、96 项 SDK 测试及 22 项 doc-test、C/Rust schema 布局
  核对、12 项 build gate/格式/diff 检查通过。21 个原始 C ABI 用例
  包含新增的吞掉输入/输出 schema 错误；真实 Rust DSO 的 18 组
  正向/错误元数据/行不匹配/重扫恢复也已通过定向 CTest（41.13 秒）。
  一次 SDK 链接发生 LLVM rust-lld 内部 realloc 崩溃，保留失败记录，
  原配置重跑通过，没有更换链接参数或放宽测试。
- 最终完整 kernel 已通过：26 种 SQL/逻辑数值表示与空输入 schema、
  六组文本 LOB/持久 codec 模式，均经实际 SQL 适配器、loader 和
  Rust DSO 验证，既有 SQL/排序/PX 测试矩阵继续通过。子输入和行外
  存储仍使用明确的 fixture，不是实库扫描、MVCC 或跨进程 PX 证明。
  故障 codec fixture 改变 NULL 约束时会重新打开游标，不在同一个
  活动执行状态里修改声明的 schema。
- 最终生产 Rust 插件构建及二进制审计、独立 runtime 测试构建、
  118 项 Rust runtime 单测、完整 30 项 CTest（75.58 秒），以及
  12 项 build gate、格式与 diff 检查均通过。本次续跑时旧 kernel
  会话句柄已不存在且最终输出缺失，没有据此推断通过；重新执行
  同一完整 kernel 命令并确认退出码为 0，作为本次 schema 验收依据。
- 这不是规划期输出 schema 或多输入 SQL 算子完成。当前候选选择
  早于 ALLOC_EXPR/PROJECT_PRUNING，尚未分配的输出列不能当成最终
  schema。后续仍需关联逻辑表达式身份、关系语义、列分配和自定义
  输出映射，并继续缓存/PX 重绑定、完整 catalog、索引与 AI 资源契约。

## 前一检查点（2026-09-09，自定义执行器数值与 Rust typed SDK）

- 修正逻辑 bool/int32/uint32 被按 SQL 整数槽位统一传成 8 字节的问题；
  现在按完整 builtin ID 使用 1/4/4 字节。普通 SQL 窄整数提升为
  int64/uint64、float 提升为 float64，codegen 保留实际逻辑身份。
- 物理桥接扩大到全部有/无符号 8/16/24/32/64 位整数和 float/double。
  输入缩窄检查、输出字节宽度和 SQL 物理范围检查分开，禁止静默截断、
  越界、非规范 bool 和已知 builtin ID 与物理表示不匹配。用户自定义
  ID 不按后缀推断 builtin 语义。明确接纳六个既有 GIS scalar 数值
  别名；共享函数/表函数/cast 类型推导将 core/GIS uint64 映射为 SQL
  无符号 BIGINT，不再误归为有符号类型。仍不支持 decimal/时间等所有
  物理类型。
- Rust SDK 增加 Cell::number 与有类型的 Number，安全读取本机字节序、
  未对齐的 ABI 数值；typed NULL 与错误表示分开，整数不经 double
  中转。Rust spool 实际调用该接口解码 builtin 值后缓存，自定义值
  仍按自身表示传递。无需 GIS 改写为 Rust。
- 完整 kernel 已通过 26 种 SQL/逻辑表示组合，独立断言进入 Rust 前
  的字节并经真实 Rust DSO 解码；覆盖极值、NULL、正负零/无穷、跨
  有/无符号表示、非 builtin 相同后缀、错误宽度、越界、未对齐输出、
  失败状态、重扫恢复和游标释放。输出故障使用明确的测试适配器，不
  把故障注入器称作 Rust 算法。子输入是 fixture，不冒充实库扫描。
  另用受控元数据 provider 验证真实 SQL 类型推导对 core/GIS uint64
  均产生无符号类型；不把该受控类型推导用例称为 Rust 函数执行证据。
- 正常 SELECT 的 FLOAT 投影/排序在标量及 batch=3 计划中通过；
  断言自定义 spec 确有 FLOAT 列和 float64 ID，避免只在节点上方投影。
  六组既有 LOB/持久 codec、故障 codec 与 SQL/排序/PX 矩阵继续通过。
- 生产构建、93 项 Rust SDK 测试及 20 项 doc-test、118 项 runtime
  单测、12 项 build gate、格式与 diff 检查通过。最终生产 Rust 插件
  构建及二进制审计、独立 runtime 测试构建、完整 30 项 CTest 均通过
  （80.63 秒）；这些结果包含最后补齐的 GIS 数值别名与 uint64 推导，
  不是复用补齐前的通过记录。
- 本轮首个 kernel 编译因测试循环拷贝警告被 -Werror 拒绝，已修正。
  随后 kernel 发现新数值校验对无存储区的默认 ObDatum 调用 setter
  导致 SIGSEGV；已改为不写 Datum 的纯校验，仅最终发布时写 SQL
  表达式槽位。修正后重建生产并重跑完整 kernel 通过，没有删减断言。
- schema 探测/多输入/输出 schema、缓存/PX 重绑定、delta LOB、更广
  类型、完整 Server-dev SDK、实库 catalog 事务与其余设计仍未完成。
  这是向完整灵活性与 Rust runtime 目标推进的检查点，不是完成声明。

## 前一检查点（2026-09-09，自定义执行器的持久类型 codec）

- 自定义 codegen 开始接纳存储态插件列，通过既有类型目录解析并校验
  owner/object/format/version/epoch，把现有存储绑定的版本化序列化
  结果放入 plan。运行时验证 blob 完整性并复制绑定，不保留函数地址。
- 输入 LOB 内容通过真实 decode 转为逻辑值，再交给 Rust spool；输出
  通过 encode 恢复 SQL 表示。所有编码完成后才发布表达式行。编码输入
  按列回收临时内存，逻辑输入与编码输出分别检查整行 16 MiB 限额。
- 增加临时/行内/行外 LOB 上的持久/普通混合列测试，检查真实 Rust
  codec 调用、完整持久字节、NULL/空值、损坏绑定和格式错误、重扫恢复。
  SQL 计划用例额外插入正常 DML 使用的规范编码表达式，再走真实
  optimizer/codegen/executor；不把这个 fixture 说成实库 INSERT。
- 完整 kernel 已通过六组持久/普通混合列与 LOB 格式往返、损坏绑定、
  实际 Rust decoder 格式错误、重扫恢复，以及 18 项故障 codec
  provider 的错误保留测试。故障 provider 是明确测试替身，正向 codec
  与自定义执行仍经真实 Rust DSO。坏类型、保留字段、非法 NULL、
  重复/缺少输出、超限、输出后失败及空 host 均不能交付 SQL 行。
- 两组存储态 SQL 计划（标量与 batch=3）确认实际自定义 spec 中存在
  codec 列，并验证持久格式、逻辑排序、rescan 和取消。测试最初使用
  未注册直接 cast、让投影停在自定义节点上方、遗漏注入表达式的排序
  绑定，均已修正；保留实际 codec 列与 ORDERED 元数据断言，没有
  通过删掉验证条件使测试通过。既有 SQL/排序/PX 矩阵继续通过。
- 生产构建、生产 Rust 示例构建/二进制审计、完整 30 项 CTest
  （78.25 秒）、118 项 Rust runtime 单测、12 项 build gate、格式与
  diff 检查通过。生产 Rust 示例打包首次遇到 LLVM 链接器内部 abort，
  保留失败记录，原配置重跑通过，未修改链接参数或放宽审计。
- schema/多输入、更多物理类型、delta LOB、计划缓存/PX 绑定和其余
  完整设计继续保留；这不是整体目标完成的声明。

## 前一检查点（2026-09-09，自定义执行器的文本 LOB 传输）

- 自定义 SQL 算子不再一概拒绝文本 LOB。通过现有 LOB iterator 获取
  内容，Rust spool 使用原有拥有型字节缓存；返回内容由宿主重建为
  表达式拥有的临时 LOB，不让 Rust 接触 locator 或 C++ 内存布局。
- 行外读取前检查 payload 声明长度与整行剩余的 16 MiB 限额，读取
  后核对实际长度；输入行 arena 在下一次输入前整体 reset。保留
  NULL/空内容差异、原始存储错误、失败状态和重扫恢复协议。
- 新增真实 PluginCustomOp + loader + Rust DSO 的 LOB 传输测试，
  子输入与行外存储明确使用 fixture；另新增正常 SQL 规划的长文本
  投影/排序用例，要求计划确实包含传输 LOB 的 PLUGIN CUSTOM。
- 完整 kernel 回归已通过：三种 LOB 表示的内容往返、NULL/空值、
  二进制与大值、重扫；行外单列与整行超限在读取前拒绝，声明/实际
  长度不一致及存储错误保留失败状态，重扫恢复和游标释放通过。正常
  SELECT 的长文本投影/排序在标量及 batch=3 计划中确认实际 LOB
  自定义节点，并验证完整内容与重扫。既有 SQL/排序/PX 矩阵仍通过。
- 生产 seekdb、绑定生产宿主的 Rust 示例构建/二进制审计、完整 30 项
  CTest（84.29 秒）、118 项 runtime 单测、12 项 build gate、格式
  和 diff 检查通过。初次生产编译的旧 tenant API 使用已修正；测试编译、
  固定数组初始化和手工算子的物理计划指针问题均已修正。一轮在构建
  测试 DSO 时遇到 LLVM 链接器内部 abort；保留失败结果，未改变配置
  重跑，最终完整 kernel 通过。
- Delta LOB 的有界重建、持久插件类型 codec、更多物理类型、输入
  schema/多输入、计划缓存/PX 绑定及完整设计中的其他工作仍未完成。
  GIS 继续使用 C++；此次未修改 GIS 算法或将其迁移为 Rust。

## 前一检查点（2026-09-09，Rust 自定义 SQL 路径闭环）

- `candidate::Context::custom(CustomPath)` 通过既有 build 回调提交
  kind=2，请求独立的插件执行路径，包含服务版本、计划参数、每 worker
  算子代价及排序/阻塞属性。当前为单输入、保持原 SQL 列身份和关系
  语义的等价路径；不是将插件策略翻译为某个内置 MATERIAL。
- 新增 `LogPluginCustom`、`PluginCustomSpec/PluginCustomOp`，接入
  逻辑/物理工厂、属性与代价计算、EXPLAIN 和正常 SQL codegen。
  候选构造保留原输入 parent，仅最终选中才连接新增节点。实现身份和
  参数复制为计划数据，执行时重新绑定相同 generation/incarnation。
  类型、服务和参数错误不静默回退或选择其他实现。
- SQL 桥接提供真实子算子取行、复制输出、取消、rescan、close 和
  ExecContext destroy 清理。当前支持 int64/uint64/double、普通
  字符串/字节、NULL 及这些表示上的非存储态插件类型；codegen 保存
  逻辑 ID 和 NULL 属性。LOB、持久插件类型及其他尚未接入的物理表示
  明确拒绝，不向 Rust 暴露 ObDatum 或 C++ 类布局。
- 输出先构造完整临时行，复制成功后替换；整个 Rust next 与退出取消
  检查成功后才写入表达式 Datum。临时行不累积各列历史最大 vector
  容量，失败复制不留下混合的新旧行。next、发布或 rescan 失败后只能
  在成功重扫后继续。此项不是完整 query memory accounting 或 spill。
- 纯 Rust 示例升级为 v3，候选 hook 真正绑定 Rust spool service；
  算法、缓存、重放和状态均在 Rust。SDK 新请求有 C/Rust 布局测试，
  新增字段传输、非法参数与吞掉宿主错误的测试。19 个原始 ABI 用例和
  八游标并发/线程移交/停机引用测试继续通过，不另建运行时。
- **真实 SQL 验证通过：** 最终 kernel 根据自身宿主标识构建实际 Rust
  包，正常 SELECT 规划生成 PLUGIN CUSTOM，断言没有以 MATERIAL 替代，
  并检查实际 Rust 执行调用。ASC/DESC × 标量/批量计划四个样例、重复
  执行/rescan、取消与最后 lease 清理通过；另保留混合 Rust/C 的四个
  MATERIAL 对照样例及既有 SQL/排序/PX 矩阵。真实构造故障测试覆盖
  负代价、非法 flags、缺失服务、保留字段和参数超限、未选中 parent
  不变。这不等于完整 PX/SQC、实库事务或所有数据类型都已验证。
- 生产 seekdb、绑定生产宿主的 Rust 示例、最终完整 30 项 CTest、
  118 项 Rust runtime 单测、12 项 build gate、源码边界、格式与 diff
  检查通过。不同宿主拒绝加载生产 Rust artifact。首轮编译中的头文件
  与局部变量问题已修正；首次 CTest 的两项源码清单数量检查随新增
  的两个真实核心桥接文件更新，仍检查精确归属、缺失和重复，而非
  放宽门槛。内存收尾改动后再次完成生产构建和完整 kernel 回归。
- **完整目标仍未完成：** 继续开放输入 schema 探测、多输入与输出
  schema、属性/代价重估、更广类型/LOB/codec、查询缓存失效和跨进程
  计划实现绑定。当前 spec 虽有序列化字段，runtime incarnation 仍
  属于进程，不宣称跨进程 PX 已可用。独立 Server-dev SDK/adapter、
  索引/类型/hash/window/存储恢复、实库 catalog 事务/权限/保存点/并发、
  AI 异步/预算/轻量化、PL、Bazel 与平台的完整目标保持不变；GIS
  继续用 C++。

详见 [自定义 SQL 执行协议](plugin-custom-executor.md) 与
[Rust 示例](../../../plugins/rust_candidate/README.md)。

### 前一检查点：Rust 自定义执行服务与并发契约

- 新增版本绑定 `custom_executor_v1`，插件拥有算法与状态，宿主提供
  子输入、同步复制输出和取消回调。open/next/rescan/close 全生命周期
  接入既有 loader/registry；ObIModuleProvider 与 server runtime 转发
  已提供，core-only 明确返回不支持。不是另一套插件运行时。
- `CustomExecutorBinding` 复制 owner、runtime incarnation、generation
  和精确服务版本；open 重新核对身份并取得执行 lease。游标直到 close
  返回才释放代码引用，失败 open 的非空游标也清理。非法行、重复输出、
  吞掉子输入/输出错误、取消与不合法 EOF 都不能被伪装为成功。
- Rust SDK 通过所有权和 FFI 边界管理游标；State: Send + 'static，
  查询 Context/Row 不可跨线程借用，保存输入必须复制。不同游标可以
  并发调用同一 instance；bind/open 都要求 service 自己声明线程安全，
  不能只靠 manifest 标志或手工构造 binding 绕过准入。插件自行同步
  共享状态，不把所有查询放进全局串行锁。
- `rust_candidate` v2 增加 Rust 自有 spool service，缓冲与重放不调用
  内置 MATERIAL。实际 DSO 测试覆盖输入缓冲区覆写、NULL/空值、EOF、
  重扫、失败恢复与代码引用。19 个原始 C ABI 变体独立于 SDK 检查
  错误协议。八游标测试通过条件变量会合，使八个 Rust next 同时在途，
  使用不同输入检查串扰，并验证线程移交、独立失败与最后引用释放。
- 生产 seekdb 与绑定生产宿主的 Rust 示例构建通过。最终完整 30 项
  CTest、118 项 Rust runtime 单测、12 项 build gate、源码边界、Rust
  格式与 diff 检查通过。完整 kernel 回归通过，覆盖既有 SQL 表达式、
  类型传播、Extension、MATERIAL 构造与排序/PX 路径；不把它算成新
  自定义执行服务的 SQL 接入证据。
- 保留首次失败记录：定向回归的 Rust Server-dev 包遇到链接器
  SIGSEGV，随后首次完整 CTest 的 SDK table_control 链接 abort，均
  出现 realloc invalid old size，尚未执行对应测试。未修改链接参数
  或断言，kernel 完成后完整原样重跑 30/30 通过；不宣称链接器问题
  已修复。这些测试仍使用明确的 catalog/verifier/行传输替身，不是
  实库事务、完整并行 SQL、性能或跨平台证据。
- **完整目标仍未完成：** 新服务尚未接到通用 logical node、physical
  spec/operator、SQL codegen 或自定义 path 构造。现有候选 hook 仍创建
  宿主 MATERIAL，其 SQL 测试不能算作新 Rust executor 的 SQL 证据。
  下一步应直接连接这些层并验证真实输入/输出 schema、属性/代价、
  EXPLAIN、缓存和资源契约。完整独立 Server-dev SDK/adapter 分发、
  索引/类型/hash/window/存储恢复、实库 catalog 事务/权限/保存点/并发、
  完整 PX/SQC/序列化、AI 异步/预算/轻量化、PL、Bazel 和平台目标不变。
  GIS 继续使用 C++。

详见 [自定义执行服务](plugin-custom-executor.md)。

### 前一检查点：候选构造与纯 Rust Server-dev 交付

- 候选服务新增 minor=1 构造上下文；Rust 可请求宿主创建 MATERIAL，
  读取动态候选集合并参与默认或显式选择。工厂、属性与代价计算来自
  真实规划器，不接受插件伪造代价；旧 minor=0 仍收到精确 v1 view。
  单次最多追加 64 项，错误保留精确数据库状态，失败清空输出。
- 构造使用的内置工厂会修改 child.parent，桥接以作用域保护恢复原值，
  仅在最终选中新增路径后连接新增节点链。未选中、构造失败和异常不会
  发布 parent 变化；新节点由宿主计划管理，不引入插件 vtable。
  真实 Rust/C DSO 错误矩阵扩大到 13 种，完整 kernel 覆盖 MATERIAL
  的 EXPLAIN、物理计划、执行与 rescan，以及非法构造/上限/错误恢复。
- SDK 新增 `sys::ServerDevManifest` 与 `server_dev::bind`；C/Rust
  布局和字段偏移、capability、有效/非法绑定都有测试。Rust 契约工具
  支持输出 Rust 常量；C++ 与 Rust 构建共用同一个宿主标识读取工具。
- `seekdb_add_rust_plugin` 支持显式 Server-dev profile 和独立工程的
  `SERVER_DEV_HOST`。每次构建核对宿主，内容不变时不更新时间戳；切换
  到时间更早的不同 ELF 仍更新契约并重编译。Cargo 本地依赖隔离、
  `-z defs`、导出清单与包输出不覆盖规则保持。Rust 不依赖 host runtime
  crate，不把 C++ 类布局直接交给 Rust；通用 adapter 交付仍需推进。
- 新增 `plugins/rust_candidate` 纯 Rust cdylib，manifest、生命周期、
  SDK 注册和候选构造均不用 C manifest 外壳。它故意选新增 MATERIAL
  来证明控制权，不是推荐的生产优化策略。独立工程通过现有
  cargo-seekdb 构建/审计/打包，检查实际加载、宿主切换/恢复、无覆盖、
  非法宿主与未声明/已声明导出。生成或审计失败不复制残留旧产物。
- 生产 seekdb 和新 Rust 示例构建通过；实际生产宿主绑定的 Rust DSO
  在另一个 loader executable 中被拒绝。完整 kernel 对最终测试宿主
  构建并打包未修改的 Rust 示例，加载包内 DSO 后通过四个构造规划/
  执行样例，和混合 Rust/C 四个样例及原排序/PX 矩阵一起通过。
- 最终完整 29 项 CTest、118 项 Rust runtime 单测、12 项 build gate、
  源码边界、Rust 格式与 diff 检查通过。首次完整 CTest 的 Rust
  Server-dev 打包、SDK doctest、脚手架三项曾遇到 linker SIGSEGV/abort，
  未改链接参数或测试断言，kernel 结束后原样完整重跑通过；不宣称
  链接器问题已修复。catalog/verifier 与部分 host 服务仍是明确替身，
  这些测试不是实库权限、事务可见性、性能或跨平台证据。
- **完整目标仍未完成：** MATERIAL 是宿主内置构造原语，不是任意
  自定义 path/物理算子。下一阶段仍需通用路径、属性与代价协议、
  自定义执行/EXPLAIN/计划缓存/取消资源协议，以及完整独立 Server-dev
  SDK 与 adapter 分发。索引/类型/hash/window/存储恢复、实库 catalog
  事务/权限/保存点/并发、完整 PX/SQC/序列化、AI 异步/预算/轻量化、
  PL、Bazel 与平台目标保持不变。GIS 继续为 C++。

详见 [候选构造](plugin-candidate-selection.md)、
[Server-dev 契约](plugin-server-dev-contract.md) 和
[纯 Rust 示例](../../../plugins/rust_candidate/README.md)。

### 前一检查点：Rust 选择真实 planner 候选

- 在 `ObLogPlan::get_minimal_cost_candidate` 接入版本绑定的候选 hook，
  C++ 提供当前集合的数值信息与选择接口，Rust SDK 可 around 包装默认
  选择，或 replacement 直接选择。仍由统一 Rust hook v2 执行顺序、
  next 次数、下游精确错误和最终检查；未建立另一套 registry/runtime。
- loader 固定完整链及实现 lease，并要求代码所属模块已经通过
  Server-dev 准入。宿主缓存准入事实，Public 插件不能仅靠 hook point
  名称取得执行权。最终验证选中项属于当前集合，失败清空输出，越界
  操作和已发生的下游错误不能被后续成功掩盖。
- Rust SDK 提供 `candidate::Context/Hook/Service` 与 descriptor 注册，
  不暴露 C++ class 布局。真实 Rust callback DSO 的九种模式/错误矩阵
  通过；C/Rust ABI 布局、非 Send、错误上下文与宿主操作失败亦有验证。
- 生产 seekdb 构建、最终完整 28 项 CTest、完整 SQL kernel 回归通过。
  kernel 按最终测试宿主重新生成绑定，断言 Rust 策略自然进入 SQL
  规划，并继续代码生成/执行；另用受控真实逻辑算子证明选中项与默认
  最低代价不同。受控候选对不是新物理算子的执行证据。12 项 build gate、
  源码边界和格式检查通过。首次 SDK 链接器 SIGSEGV 原样重跑后通过，
  未改链接参数，不宣称链接器问题已修复。
- Rust runtime 总入口的 118 项单测通过。专用于生成 DSO 的 Rust 源码
  放在 tests/fixtures 下，避免被 Cargo 当作独立 integration test；
  runtime 不因此反向依赖 extension SDK。新 C 头文件实际安装后通过
  C11 编译检查，这不等于完整 Server-dev SDK 打包已经交付。
- **完整目标仍未完成：** 当前只能选择已有候选，不能将它作为自由度
  的终点。下一步是新 path 构造/提交、属性与合法性检查、计划生成、
  自定义执行/EXPLAIN/缓存资源协议，以及 Rust Server-dev 打包工具。
  其余索引/类型/存储、实库事务、AI 异步/预算/轻量化、PL 和跨平台目标
  保持不变，GIS 继续用 C++。详见 [候选选择接口](plugin-candidate-selection.md)。

### 前一检查点：Server-dev CMake 开发 profile

- 新增显式 `api_profile/server_headers/exports` 声明，构建时按 profile
  区分直接私有头文件策略。配置结束后继承 `ob_sql` 的传递编译上下文，
  不链接核心目标或静态库；继续验证晚追加的编译、源码及链接依赖。
- Rust 契约工具新增 stdout 输出，CMake 等待最终宿主链接后自动生成
  绑定头文件。wrapper 将普通 v1 manifest 加上 Server-dev 后缀，隐藏
  实现入口和未声明符号；原有 `-z defs` 与二进制审计保留。
- `server_dev_reference` 使用真实 optimizer 头文件中的 inline 类型，
  已在生产编译配置下构建并通过二进制审计。它没有提供 candidate path
  或自定义执行计划，不能把“能包含头文件”当成深层能力全部开放。
- 6 项声明/源码单测、实际 CMake 编译/自动绑定/加载与拒绝矩阵通过；
  最终完整 27 项 CTest、118 项 Rust runtime 单测通过。实际生产示例
  在另一个 loader executable 中被拒绝。显式 profile 需要 Python
  3.11+；本机默认 python3 为 3.8，CMake 使用的 3.13 已验证。
  完整 SQL kernel 回归、12 项 build gate、源码边界、Rust 格式与 diff
  检查通过；本轮未宣称实库权限/事务或深层 planner 已完成。
- **完整目标仍未完成：** 当前是 Linux 同源码树 C++ profile，独立 SDK、
  Rust 深层 adapter、真实 planner/index 扩展协议、Bazel/跨平台仍需推进。
  实库 catalog/权限/保存点/并发、完整交换计划、type/hash/index/window/
  恢复、AI 异步/预算/轻量化和 PL 的剩余目标不变。GIS 继续使用 C++。
  详见 [Server-dev 契约](plugin-server-dev-contract.md)。

### 前一检查点：Server-dev 链接宿主准入

- 新增显式 manifest-only SERVER_DEV capability 与版本绑定后缀，保持
  公共 manifest 前缀布局和 ABI major/minor。loader 在 init/start 前
  检查桥接版本、结构、保留字段及实际运行宿主的 GNU build ID；不匹配
  走现有 activation abort，不发布对象/服务。公共插件不进入该校验。
- Rust 实现有界 ELF64 LE note 读取与成功缓存；Linux 读取
  /proc/self/exe 而非插件指定路径。生成工具根据最终宿主 artifact
  输出契约头文件且拒绝覆盖。build ID 是链接身份，不是文件完整性、
  签名或 SDK/header 来源证明；不支持的平台仅拒绝 Server-dev。
- 118 项 Rust runtime 单测、真实 loader 的七种动态库准入矩阵与
  12 项 build gate 通过。首轮 CTest 的 rust_plugin_scaffold 遇到
  链接器 SIGSEGV；未修改其代码/链接参数，单独重跑及随后完整 25 项
  CTest 均通过，保留首次失败记录，不声称链接器问题已修复。
  生产构建、完整 kernel、源码边界、格式与 diff 检查通过。
- 实际安装 plugin-sdk 组件后用已安装 header 编译契约 DSO。生产宿主
  契约与 readelf 结果相同；绑定生产宿主的 DSO 在另一个测试 executable
  下被拒绝，且未进入 init。这是链接身份与头文件交付证据，不是完整
  Server-dev profile 或实库深层 planner 插件的验收。
- **完整目标仍未完成：** 本轮是 Server-dev 加载准入基础，不是完整
  开发 profile。CMake 深层 header/export 边界、匹配构建 SDK 与 Rust
  adapter、candidate path/计划构造/执行/EXPLAIN 仍需接入。其余完整
  交换计划/并发、type/hash/index/window/恢复、实库 catalog 事务/
  权限/保存点/并发、AI 异步/预算/轻量化、PL、Bazel/跨平台目标不变，
  GIS 继续使用 C++。详见 [Server-dev 契约](plugin-server-dev-contract.md)。

### 前一检查点：Rust hook 三种调度模式

- 新增宿主内部 hook v2：OBSERVE 自动前后观察，AROUND 显式一次 next，
  REPLACE 可以跳过后续条目与原始操作。不再要求所有模式成功时必须
  调用 next。已调用 next 的下游错误仍原样保留，重复调用是协议错误。
- 成功链必须经过宿主最终结果验证，验证发生在全部包装后处理之后；
  非法整表在首次回调前拒绝。旧宿主入口用有界栈数组适配同一 Rust
  引擎，不复制调度算法。现有 C++ planner loader 已接到 v2 调度器，
  继续在快照 epoch 下固定全部对象/代码，不持锁调用插件。
- **公开 planner v1 仍只按 AROUND 准入。** 本轮没有提供任意计划对象
  给插件，也没有改变公开 C ABI、SDK 或 Rust text build ID。内部
  replacement 成功依赖宿主的结果检查，不等于真实自定义计划已可用。
- 115 项 Rust runtime 单测、重建后的 24 项插件 CTest、12 项 build
  gate、源码边界与格式检查通过，生产 seekdb 构建通过。Rust 用例包含
  三个位置的全部 27 种模式组合；新增 C++/Rust 跨语言测试覆盖替换与
  包装、否决、错误保留、非法表、上限和最终校验。完整 kernel 回归
  通过，真实 Rust DSO 的公开 planner v1 路径接入新引擎后行为保持。
  新鲜度门禁已加入 Rust 新旧 hook 实现及 optimizer 宿主入口。
- **完整目标仍未完成：** 后续需打通版本绑定的深层宿主准入、candidate
  path/计划构造与合法性检查、执行和 EXPLAIN，再开放对应 SDK 与真实
  插件样例；不能停在模式调度层。其余完整交换计划/并发、type/hash/
  index/window/恢复、实库 catalog 事务/权限/保存点/并发、AI 异步/
  预算/轻量化、PL、Bazel/跨平台目标保持。GIS 继续使用 C++。详见
  [hook 模式契约](plugin-hook-modes.md)。

### 前一检查点：已读尽通道的收发重扫

- Rust 原始发送值通过同一对全局注册 local channel 跨 batch 重用，
  用实际 fill_px_batch_info 切换执行上下文，再调用生产 receive 与
  transmit 的公开 rescan。发送端不再由 fixture 手动 reset_state /
  set_batch_id。每一轮检查 EOF 复位、新 batch ID、实际收发/处理累计
  块数、tablet/DDL、NULL 与字节值；不同值 burst 检查不串入上一轮结果。
- 绑定真实 ObDTLIntermResultManager，为旧 batch 和无关 batch 插入空
  结果标记。receive rescan 必须删除旧键、保留无关键；测试随后清理
  对照键，最终结果表、全局通道表及 DFC 计数为空，分配/归还相等。
  空结果标记只验证清理的键与生命周期，不是类型值落盘/恢复证据。
- 接收重扫版本及进一步接入发送重扫的完整 kernel 均通过；23 项
  CTest、12 项 build gate 和源码边界检查通过。补充中间结果 manager
  与 exec context 实现的生产构建新鲜度检查；无生产逻辑、ABI/build
  ID/格式变更，复用且核验已有生产构建，本轮未另做完整生产构建。
- **完整目标仍未完成：** 当前重扫只发生在队列已读尽、EOF 已收到后。
  通道、spec 与调度仍由 fixture 组装；发送算子只接入 rescan，不代表
  已执行完整 open/transmit/SQC 生命周期，也不证明未读队列中途重扫。
  SQC 发现/握手、并发唤醒/取消、完整交换计划生成/序列化、跨进程行为，
  以及 outrow/存储型值、恢复、type/hash/index/window、深度 planner、
  实库 catalog 事务/权限/保存点/并发、AI 异步/预算/轻量化、PL、
  Bazel/跨平台目标仍保持。GIS 仍为 C++，详见
  [排序契约](plugin-type-ordering.md)。

### 前一检查点：真实压力阻塞与解除协议

- 使用实际非 NULL 类型值重复发送，逐块 flush、暂不读取，直到生产
  DFC 在真实队列压力下阻塞。只缩小该通道 buffer 为 1024 字节以限制
  分配，不修改 DFC 阈值/策略或直接设置阻塞标志；测试设置有界块数，
  并在结束时确认压力场景确实执行。
- 将发送 DFC 等待期限临时设为已过期，实际等待必须超时且不新增发送；
  恢复期限后读取队列，真实 receive DFC 发出 UNBLOCKING 控制消息。
  下一次 send/flush 消费消息并恢复发送，两端累计阻塞次数均为 1。
  全部重复值、额外恢复行、EOF、pin、队列与内存归还均检查。
- 首次运行修正 fixture 对“控制消息分派即控制块读尽”的假设：解除
  状态先成立，后续轮询才归还控制块并增加 processed buffer 计数。
  保留恢复和资源断言后完整 kernel 通过。23 项 CTest、12 项 build
  gate、源码边界与 diff 检查通过。无生产逻辑/ABI/build ID/格式变更；
  补充控制块反序列化与 processor 的构建新鲜度检查。
- **完整目标仍未完成：** 本轮顺序驱动真实协议，不证明并发 worker
  唤醒、取消竞争、公平性或性能；低层等待期限恢复也不表示 SQL 超时
  可恢复执行。SQC 发现/握手、活动通道重扫、完整交换计划生成/序列化、
  跨进程行为及其余 outrow/存储型值、恢复、type/hash/index/window、
  深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/预算/
  轻量化、PL、Bazel/跨平台目标仍保持。GIS 仍为 C++，详见
  [排序契约](plugin-type-ordering.md)。

### 前一检查点：生产 EOF 批量发送器

- Rust LinkedExchange 改用实际 ObTransmitEofAsynSender::asyn_send，
  原始数据值、单块/逐行块数、重复 EOF、pin 与资源归还断言保留。
- 新增真实三通道 EOF 矩阵：全部成功，以及第一/中间/最后对端缺失。
  三个 action 均被尝试，错误返回给调用者，健康通道各收到一个空 EOF；
  失败发送无缓冲区或额外引用残留，移除后同 ID 不可查。
- 首次运行发现 fixture 错误预期重复 wait_response 会再次返回已收集
  的错误，按源码改为无待处理响应时成功；保留 asyn_send 的失败码及
  全部资源/EOF 断言。完整 kernel 重跑通过；23 项 CTest、12 项 build
  gate、源码边界、diff 检查通过。无生产逻辑、ABI/build ID/格式变更，
  新增 dtl_utils 的生产构建新鲜度检查。
- 当前 local asyn_send 是分批发起后等待响应，没有专用 EOF worker。
  因而此前“async EOF worker 未覆盖”的表述修正为发送器与并发调度
  应分别验证：前者本轮已接入，后者不能由本地同步路径证明。
- **完整目标仍未完成：** SQC 发现/握手、阈值以上 EOF 多批次、延迟
  响应、并发背压/唤醒、活动通道重扫、完整交换计划生成/序列化与
  跨进程行为仍待推进。其余 outrow/存储型值、恢复、type/hash/index/
  window、深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/
  预算/轻量化、PL、Bazel/跨平台目标不变，GIS 仍为 C++。详见
  [排序契约](plugin-type-ordering.md)。

### 前一检查点：全局通道与实际发送链

- 新增 LinkedExchange：在真实 transmit 回调拿到原始表达式输入后，
  调用生产 local channel.send/flush，经过 writer、发送队列、全局
  对端查找/引用归还、feedup、消息循环和 FIFO；不根据预期值重建输入，
  也不由该样例直接注入接收 buffer。旧搬迁/双通道矩阵保留。
- 单块带 EOF 后 2+1 读取；逐行封块同步交替发送/读取，最后发送独立
  空 EOF。独立接收 exec/frame 检查 tablet/DDL、NULL、Unicode 字节
  与顺序，同时断言实际发送/接收/处理块数和重复结束。
- 使用全局 DTL 注册表和实际收发 DFC；读尽时仅剩 registry pin，
  移除后 pin 归零，注销/删除后同 ID 不可查。测试域结束全局通道表
  为空、channel count 与队列计数归零、内存分配/归还相等。
- 首次编译修正 fixture 对派生类 get_peer_id 的调用；随后完整 kernel
  通过。23 项 CTest、12 项 build gate、源码边界与 diff 检查通过。
  本轮无生产逻辑、ABI/build ID/格式变更；新增全局 DTL/channel-group
  源码新鲜度检查，复用已有生产构建。
- **完整目标仍未完成：** 尚无生产 SQC 发现/建链握手、async EOF worker、
  并发背压阻塞/唤醒、活动通道重扫或完整交换计划生成/序列化。当前是
  同进程同步本地发送，不是跨进程/RPC 验证。其余 outrow/存储型 Rust
  值、恢复、type/hash/index/window、深度 planner、实库 catalog 事务/
  权限/保存点/并发、AI 异步/预算/轻量化、PL、Bazel/跨平台目标不变。
  GIS 仍为 C++，详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：通道消息循环与 EOF

- 新增两条真实 local channel 和 receive DFC，数据经 feedup/attach、
  watcher、channel loop、ObPxReceiveRowP 进入 FIFO reader；不是零通道
  的 all_eof(0)，也不由 fixture 直接调用 reader.add_buffer。
- 一个空通道先结束，断言 EOF 计数为 1 且另一路仍 WAIT_EAGAIN。
  另一通道分别用最后数据块 EOF 和独立空 EOF，验证公共 batch/row
  API、全部行的值/顺序、重复结束和两通道已接收/处理计数相等。
  保留单块/逐行多块和旧生命周期/结构错误矩阵。
- 提前 close 与不支持的 payload tag 错误覆盖未处理队列、失败的
  process_buffer 及 reader 已接管块。实际 receive DFC 清理后通道和
  聚合队列字节/缓冲区计数归零，通道及内存管理器分配/归还相等。
- 首次链接发现 fixture 调用的 unset_msg_watcher 仅声明未实现；改为
  强制移除通知链节点、在 watcher 存活时销毁通道。完整 kernel 重跑
  通过；23 项 CTest、12 项 build gate、源码边界、diff 检查通过。
  本轮无生产逻辑、C ABI/build ID/格式变更，复用并校验生产构建新鲜度。
- **完整目标仍未完成：** SQC 发现、全局 DTL map 建链、跨进程发送、
  真实异步调度、背压阻塞/唤醒、活动通道重扫及完整交换计划仍待推进。
  当前 DFC 只验证低压力增减账，不代表背压已验证。其余 outrow/存储型
  Rust 值、恢复、type/hash/index/window、深度 planner、实库 catalog
  事务/权限/保存点/并发、AI 异步/预算/轻量化、PL、Bazel/跨平台目标
  不变；GIS 仍为 C++。详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：FIFO 接收算子生命周期

- 在单块/多块 DTL 编码后，将独立缓冲区交给真实 FIFO 接收算子，
  执行公开 init/open、2+1 batch、逐行适配器、重复 EOF、rescan、
  close 与析构清理。读尽及读取一行后均重扫，再从新缓冲区读尽，
  验证 reader 和逐行适配器不残留旧数据/结束状态。
- 每行检查 tablet/DDL、NULL、变长字节与 skip 位图；保留现有行结构
  负例和 DFC buffer 分配/归还相等检查。新增接收算子及公共 wrapper
  源文件的构建新鲜度检查。本轮不修改生产逻辑、ABI、build ID 或格式。
- 初次 fixture 编译因访问私有 batch setter 失败，改用公开 scope guard。
  随后运行发现公共 batch wrapper 清除非 table-scan 的 all-active 提示，
  按实际契约检查提示为 false、逐行 skip 位为 false，未削弱数据断言。
  修正并增加中途重扫后，完整 kernel 通过；23 项 CTest、12 项 build
  gate、源码边界与 diff 检查通过。复用且核验已有生产构建的新鲜度，
  本轮未另行执行完整生产构建。
- **完整目标仍未完成：** 当前唯一替代入口为通道建立，零活动通道的
  all_eof(0) 表示传输已完成；不是 SQC 建链、消息循环、异步 EOF、真实
  通道清理、网络/背压或完整交换计划。outrow/存储型 Rust 值、恢复、
  type/hash/index/window、深度 planner、实库 catalog 事务/权限/保存点/
  并发、AI 异步/预算/轻量化、PL、Bazel/跨平台目标保持不变，GIS 仍为
  C++。下一步继续推进消息交接与完整交换执行链，详见
  [排序契约](plugin-type-ordering.md)。

### 前一检查点：多块接收与行结构检查

- 保留单块流，同时用生产 writer 为同通道数据逐行封块；真实 reader
  以标量和 2+1 批量方式跨块读取。三行 burst 改为同通道的不同值，
  验证变长字节、适用排序中的 NULL、DDL 范围编号与行顺序。收发缓冲区
  地址不同，发送块仍覆写并释放，接收 frame 独立。
- 新增只读一行后提前 reset，覆盖尚未消费和已迭代块的资源归还。
  单块/多块各有正常读尽、提前清理、最后一行缺列、目标表达式少列
  五种读取模式，继续检查 DFC 分配/归还计数。
- 修复批量 attach 缺少行结构校验：整批检查行指针/列数/容量及表达式
  约束后才写 frame，避免静默丢列或越界。结构错误返回零个有效行，
  保持表达式值与求值标志不变；不承诺恢复已消费输入或回滚复制分配
  失败。非法容量请求仍在读取前拒绝。
- 生产构建、两次完整 kernel（含不同值补充）、23 项 CTest、源码边界、
  12 项 build gate 与 diff 检查通过。C ABI/build ID/持久格式未改。
- **完整目标仍未完成：** 本轮仍为 DTL datum 块级路径，不是完整
  receive operator、交换计划、RPC/worker/sample/背压。outrow/存储型
  Rust 值、存储写入/恢复及其余 type/hash/index/window、深度 planner、
  实库 catalog 事务/权限/保存点/并发、AI 异步/预算/轻量化、PL、
  Bazel/跨平台目标不变。GIS 仍为 C++。
  详细边界见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：DTL datum 编码与接收

- 两级分区的实际发送输出加入 Rust 类型值，调用生产 DTL datum writer，
  复制到不同地址后覆写/释放发送缓冲区，再用独立 exec/eval frame 调用
  真正的 receive reader。逐项验证 tablet/DDL、文本字节、Unicode、NULL
  和投影状态；共享只读计划描述，不宣称跨进程计划传输。
- 4 个 batch 环境共 120 次发送调用、216 条数据行，分别用标量和批量
  读取；包含 24 次三行同通道发送，必须按 2+1 批量读取。保留分区失败
  零发送、跳过坏行与原生 BINARY 对照。测试结束检查 DFC buffer 分配/
  归还计数相等，不将内存池归还解释成 RSS 清零。
- 统一 `ObReceiveRowReader::get_next_batch` 的容量检查；此前只在中间
  结果分支检查。拒绝非正数/超容量请求及空数组，read_rows 先归零，
  验证失败不消费行、不写行指针，随后可继续合法读取。
- 首次 fixture 重复 unswizzle 导致段错误，按生产 switch-buffer 路径
  修正后完整 kernel 与多行补充矩阵通过。gdb 受 ptrace 限制未取得栈，
  保留失败记录；可选调试参数不会将原测试失败改成成功。生产构建、
  23 项 CTest、源码边界、12 项 build gate 和 diff 检查通过。
- **完整目标仍未完成：** 当前是 datum 块搬迁，不是 RPC/网络、完整
  receive operator 或交换计划；多块串接、outrow/存储型 Rust 值、背压、
  worker/sample、存储写入/恢复仍需推进。其余 type/hash/index/window、
  深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/预算/
  轻量化、PL、Bazel/跨平台目标不变。ABI/build ID/持久格式未改，GIS
  仍为 C++。详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：子分区上下文与两级分区计算

- 新增 `CALC_IGNORE_FIRST_PART` 与 `CALC_NORMAL` 的真实 builder /
  optimizer / codegen、DAS/schema 求值和发送链。3×3 二级 RANGE
  schema 覆盖所有 tablet，SUB 表达式与同一计算器依次切换一级分区
  101→103→102，验证 destroy/init 后无旧上下文误用。
- 校验 SUB 通道表的所有 tablet 属于同一父分区，拒绝混合父分区且
  不覆盖旧 context。修复表达式判等遗漏 tablet ID 与组合结果的计算
  模式；先用完整 kernel 复现不同模式被判等，再保留断言修复重跑。
  三种结果表达式的相同/不同模式都有正反向等价性对照。
- 新增 4,032 次正常标量路由、96 次批量发送调用与 144 条数据行。
  覆盖每级范围边界、各级无匹配、整批零发送/不发布结果、跳过坏行、
  上下文重绑、原生 BINARY 与实际 Rust 比较、固定 binding 和 lease
  归零。旧 FIRST、预设 tablet、排序/采样矩阵全部保留。
- 生产构建、完整 kernel、23 项插件 CTest、源码边界、12 项 build gate
  和 diff 检查通过。分区键为原生整数，Rust 类型用于 tablet 内范围顺序。
- **完整目标仍未完成：** 完整交换计划与网络/worker/sample/接收/背压、
  存储写入/恢复、Rust 持久分区键仍需推进。其余 type/hash/index/window、
  深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/预算/
  轻量化、PL、Bazel/跨平台目标不变。C ABI/build ID/持久格式未改，
  GIS 仍为 C++。详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：真实分区表达式与单侧映射

- 新增生产 builder / optimizer / codegen 生成的 `calc_tablet_id`，经
  实际二级 RANGE schema、DAS mapper 计算一级 partition ID，再由
  `ONE_SIDE_ONE_LEVEL_FIRST` 映射到 tablet，接入现有 Rust 比较、
  SQC 范围和实际发送循环。分区 ID 与 tablet ID 刻意不同，不能混用。
- 8 个 SQL 环境覆盖升序、降序、identity、原生 BINARY 与 scalar/batch；
  新增 672 次正常标量路由，验证上下界、无匹配、DDL 值和重复生命周期。
  4 个 batch 环境新增 24 次发送调用、40 条数据行，覆盖真实分区计算
  后的批量 ID、整批失败零发送、跳过无匹配行和有效结果的失效规则。
- 修复单侧一级分区映射表在 destroy 时未清理、阻止同一计算器重新
  初始化的问题；两个 map 均尝试清理并保留首个失败码。旧矩阵保留。
- 首次 kernel 发现新 schema 与旧负例 SQC 的 tablet 集不匹配，改用
  独立范围集后完整重跑通过。生产构建、23 项 CTest、源码边界、12 项
  build gate 通过。详细证据与边界见 [排序契约](plugin-type-ordering.md)。
- **完整目标仍未完成：** 分区键是原生整数，Rust 类型用于 tablet 内
  范围顺序；尚未证明 Rust 持久分区键、另一侧子分区计算、完整两级
  分区/交换计划、worker/sample/DTL 网络、存储与恢复。其余 type/hash/
  index/window、深度 planner、实库 catalog 事务/权限/保存点/并发、
  AI 异步/预算/轻量化、PL、Bazel/跨平台目标不变。ABI/build ID/持久
  格式未改，GIS 仍为 C++。

### 前一检查点：批量发送携带 tablet ID

- 计算器新增显式的批量 tablet ID 能力及借用结果查询；只在成功批量
  路由后记录有效大小，失败/空 batch/下一次计算/destroy/init 失效，
  跳过行 ID 置无效。修正内部上一行 ID 更新对调用者输出指针的依赖。
- 发送端针对支持该能力的计算器，按行使用批量 ID 写入 PDML 载体，
  不再仅因需要携带 tablet ID 就退回标量。覆盖 RANGE、affinity、
  partition-random；未具备批量能力的其他路径保持原有分发。
- 通过生产 builder 和正常表达式生成器建立 PDML/DDL 两个内部伪列；
  运行真实发送循环，以预取行作为输入、capture channel 作为传输边界。
  4 个 batch SQL 环境共 20 次发送调用、44 条数据行，覆盖成功、末行
  缺失 tablet（整批零发送）、跳过坏行和原生计算器对照；嵌套 Rust
  identity 从输入列重算并保持 host 批量入口。旧批量 ID 生命周期有断言。
- 修正 fixture 的 const 赋值，以及计算表达式替代 DDL 伪列导致发送期
  重算的问题后，生产构建、完整 kernel、23 项 CTest、源码边界、12 项
  build gate 与 diff 检查通过，原有排序/采样/路由测试保留。
  Rust ABI/build ID/持久格式未改，GIS 仍为 C++。
- **完整目标仍未完成：** 本轮不是网络/接收解码/异步 EOF/背压或实际
  PDML/storage 证据；真实分区表达式、单侧重映射、完整交换计划、
  worker/sample/DTL、持久 DDL/恢复等仍需推进。其余 type/hash/index/
  window、深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/
  预算/轻量化、PL、Bazel/跨平台目标不变。详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：分区 RANGE 的 Rust 比较与批量路由

- 开放 `PARTITION_RANGE` spec 的插件排序；slave-map 边界比较调用共享
  TYPE 分派，并传入执行 context。元数据数量校验、比较器错误重新初始化、
  缺少 SQC 的错误返回已补齐；标量输出先失效，通道映射与取消检查成功
  后才写 DDL，修正旧路径过早写入的问题。
- 普通/分区 RANGE 仅准备活动行的可写 DDL datum，避免批量辅助函数
  重设跳过行指针；新增借用缓冲区哨兵断言，保留跳过行的指针和值。
- 新增 `SM_REPART_RANGE` 的批量入口和分派：tablet ID/分发键批量求值，
  暂存通道和 DDL 值，整批成功才发布；任一行失败或取消不发布有效批量
  结果，跳过行保持无效通道和原 DDL 值。保留连续范围分组到通道的规则。
- 8 个附加真实 SQL 用例生成独立 tablet/DDL frame；sample split →
  实际 SQC 范围深拷贝 → init → 标量/批量路由 → destroy/init。
  正常覆盖 168 次标量、84 行批量，验证多 tablet、不同范围/通道数量、
  空范围、原生对照、坏绑定/值、取消、未初始化、坏映射和输出原子性。
  嵌套 identity 从输入列重算，证明 host 批量入口传播，不宣称 v3/零拷贝。
- 生产构建、完整 kernel、源码边界、12 项 build gate 与 diff 检查通过，
  源码新鲜度检查加入 SQC 与范围数据定义。首次 CTest 为 22/23，脚手架
  用例遇到 `lld` 段错误；保留失败记录并完整重跑后 23/23 通过，未放宽
  断言。Rust ABI/build ID/持久格式未改，GIS 保持 C++。
- **完整目标仍未完成：** 未验证完整分区交换计划生成、真实分区表达式/
  单侧重映射、worker 调度/sample 消息/DTL、持久 DDL/恢复；发送端携带
  tablet ID 时仍走标量。其余 type/hash/index/window、深度 planner、
  实库 catalog 事务/权限/保存点/并发、AI 异步/预算/轻量化、PL、
  Bazel/跨平台仍需继续推进。详见 [排序契约](plugin-type-ordering.md)。

### 前一检查点：RANGE 批量键求值与 DDL 输出验证

- RANGE 批量路由先调用分发键 `eval_batch`，再使用缓存值查找范围。
  实际生成的 `seekdb_rust_identity(token)` 表达式从输入列重新求值，
  断言经过 SQL host 批量入口、没有增加标量入口调用，结果与逻辑类型
  顺序一致。v2 服务内部 fallback 仍可逐行执行，不宣称 v3 或零拷贝。
- 保留双键范围测试，增加单键 + 独立 DDL 输出、真实 SQL 生成的原生
  BINARY spec。验证 DDL 保存原始范围编号而非 worker 取模编号，失败/
  取消不写入、跳过行不变、空范围正确；单键重复值使用独立预期。
- 16 组采样与 448 次正常标量路由，另覆盖批量窗口、嵌套函数的实际
  批量调用、原生路径无 Rust 比较、固定绑定及 lease 释放。修正测试
  数组初始化、重复值预期、隐藏绑定参数遍历后，生产构建、完整 kernel、
  23 项 CTest、源码边界、12 项 build gate 与 diff 格式检查通过。
  详见 [排序契约](plugin-type-ordering.md)。
- **完整目标仍未完成：** 这是生成排序元数据 + 手供 sample/channel
  的真实内核部件测试，不是完整 RANGE/PX 调度、消息传输或恢复；DDL
  验证只覆盖表达式输出。slave-map、spill、其余类型/hash/index/窗口、
  深度 planner、实库 catalog 事务/权限/保存点/并发、AI 异步/预算/
  轻量化、PL、Bazel/跨平台仍需继续实施与验证。

### 前一检查点：普通 RANGE 的 Rust 类型采样与分发

- 普通 RANGE spec、采样排序和标量/批量边界查找接入同一固定 TYPE
  comparator。修正采样排序初始化的布尔参数位置，并传递真实表达式映射。
  lower_bound 使用引用比较器并检查错误，禁止比较失败后继续发布分发结果。
- 批量范围位置先暂存，全部成功后才返回索引并写入 DDL slice ID；跳过
  行无有效索引。补齐任务数与 batch 容量检查。实际测试暴露 range 仍强制
  要求 LOB 服务，已按公共 datum 协议允许纯内存值不提供读取上下文。
- 实际 sampler.init/split_range → 排序 → 标量/批量 routing 的纵向
  部件测试：4 组采样、112 次标量分发、42 行批量分发；覆盖升降序、NULL、
  重复键、1/2/3/5 个任务、具体分界点、非法 UTF-8、跳过坏行、首次比较后
  取消、结果不发布、重试及空/越界输入。结束时绑定不重复解析、lease 归零。
- 修正后的生产构建、完整 kernel、23 项 CTest、源码边界和 12 项
  build gate 通过；原有普通排序与 PX 堆回归保持通过。Rust ABI/build ID/
  持久格式未变，GIS 仍是 C++。详见 [排序契约](plugin-type-ordering.md)。
- **完整目标仍未完成：** sample 行仍手供，协调器仅提供 context；未运行
  完整 RANGE/PX 计划生成、worker sample 收集、DFO/SQC/DTL 或跨进程恢复。
  slave-map 分区 range 的插件能力、实际 DDL slice ID 写入、采样落盘及
  原生独立对照待验证；其余类型/hash/index/窗口、深度 planner、实库 catalog
  事务/权限/保存点/并发、AI 异步/预算/轻量化、PL、Bazel/跨平台继续推进。

### 前一检查点：PX 归并比较接线与 Rust 部件回归

- 本地排序的固定 TYPE 比较分派移入公共排序实现，PX 接收端、协调端
  归并堆传递实际表达式映射和执行上下文；接收端 local-order 分段比较
  也已接入。普通 Sort/PX merge spec 开放 ORDERED 键；range distribution
  和其余原生-only 消费者仍保留显式拒绝，后续继续接入。
- 修正堆算法按值复制比较器导致错误状态丢失的问题，改为引用传递；
  补齐索引/列数检查与重新初始化的错误清理。共享 comparator 保留 NULL
  规则、比较前后取消检查及 loader 调用期 lease，不新增序列化字段。
- 真实 SQL 生成的绑定 + Rust DSO + 两类实际 PX 堆：32 组多路归并，
  覆盖 scalar/batch frame、升降序、NULL/重复键、3/9 通道与空通道、
  插件/原生顺序对照；8 组故障序列验证数量不匹配、坏绑定、非法 UTF-8、
  错误保持、比较后取消和 reset/reinit。测试输入仍手供，不是 DTL 数据。
- 生产构建、修正测试输入缓冲区和原生 oracle 后的最终完整 kernel、
  23 项 CTest、源码边界与 12 项 build gate 通过。原有 20 个普通排序
  计划/40 次执行与 18 次取消回归保持通过。Rust ABI/build ID/格式不变，
  GIS 仍是 C++。详见 [排序契约](plugin-type-ordering.md)。
- **完整目标仍未完成：** 本轮不证明完整 PX 计划生成、调度、传输及
  跨进程恢复，local-order 分段仍需实际执行用例。外部落盘、窗口/分组/
  hash/index、深度 planner、实库 catalog 事务/权限/保存点/并发、AI
  异步/资源预算/轻量化、PL 与 Bazel/跨平台仍在原目标中。

### 前一检查点：Rust 类型 ORDER BY 与 Top-N

- 显式 ORDER BY 在类型推导后绑定 ORDERED carrier，复用固定 TYPE
  comparator。完整测试发现并修正 CASE 推导前绑定导致比较语义遗漏的问题。
  排序执行器按字段位置找到固定表达式，NULL/方向沿用 SQL 规则，非 NULL
  值交给 Rust comparator，比较前后检查取消。
- 关闭原生编码排序键与原生下推 Top-N 过滤，不取消 Top-N 算子本身；
  原生-only 的 PX 等消费者遇到 ORDERED 键显式拒绝，防止错误的字节排序。
  普通排序、prefix、with-ties 和内部归并入口共用分派，但运行验证范围
  以实际用例为准，不把接线等同于 PX/落盘测试通过。
- 完整 resolver/rewrite/optimizer/codegen/算子执行新增 20 个计划、
  40 次正常执行/rescan，覆盖升降序、重复键、LIMIT/OFFSET、NULL CASE、
  嵌套 Rust 函数、BINARY 对照及 1,031 行跨批排序/Top-N；另验证 18 次
  首个比较后取消、无再次比较、close 后 lease 归零。fixture 仅提供内存
  schema/session 与临时目录编号，任何文件 open 都失败，不含存储 I/O。
- 修正后生产构建和完整 kernel 通过；23 项 CTest、源码边界、12 项
  build gate 通过，排序/优化器源码加入 kernel 构建时效检查。
  Rust ABI/build ID/格式未变，GIS 仍是 C++。详见 [排序契约](plugin-type-ordering.md)。
- **完整目标仍未完成：** PX、外部落盘、prefix/with-ties 运行验证、
  窗口排序、分组/hash/index/恢复，及实库 catalog 事务、权限与并发、
  AI 异步/资源预算/轻量化、深层 planner、PL 与 Bazel/跨平台均继续推进。

### 前一检查点：MIN/MAX 的完整 SELECT 计划与执行

- 新增 `rust_aggregate_plan_fixture.h`，从顶层 SQL resolver 进入正常 hint
  分发、完整 rewrite、optimizer、表达式/算子 codegen，再由生成的 spec 创建
  和执行算子树。不手工填写逻辑计划、`ObAggrInfo`、输出映射或输入 frame。
  输入来自已安装的真实 Rust 表函数，其 token 本身具有 `rust_utf8` 身份。
- 7 类 SQL 分别使用逐行和 3 行 batch，共 14 个计划、28 次含 rescan 的
  执行：MIN/MAX、DISTINCT、空输入、全 NULL、部分 NULL、嵌套 identity，
  以及显式 BINARY 原生对照。验证生成参数的固定比较器、精确比较次数、
  惰性 cursor open、结果、无重复名称解析及 close 后 module lease 归零。
- 执行测试暴露标量聚合无条件申请临时文件目录并重复初始化 group。已改为
  仅在聚合状态需要临时目录或存在 DISTINCT 时申请；普通 MIN/MAX 不依赖
  临时文件服务。保留可能落盘的聚合分支，不把此改动等同于 RSS/启动收益测量。
- 生产构建、最终完整 kernel、23 项 CTest、源码边界、12 项 build gate
  和差异检查通过。测试接线修正包括顶层 resolver 的 hint 初始化、正常
  factory 所有权、惰性游标及原生对照的显式 cast；临时生产诊断已移除。
  kernel 的构建时效检查补入聚合处理器和标量聚合源码。Rust ABI/build ID/
  对象/格式未变，GIS 仍是 C++；本轮是 Rust 类型协议的真实内核消费验证。
- **完整目标仍未完成：** 此证据覆盖内存 schema/inner session 下的表函数
  SELECT，不是实库鉴权、持久表扫描、存储下推或 PX 调度。后续继续验证聚合
  结果的更复杂消费、多阶段/窗口/分组计划、排序/hash/index、用户自定义聚合
  状态协议、实库 catalog 事务、AI 异步与资源预算/轻量化、planner 深度接口、
  PL/恢复和 Bazel/跨平台。详见 [MIN/MAX 契约](plugin-type-extrema.md)。

### 前一检查点：插件类型 MIN/MAX 与固定排序语义绑定

- `PluginTypeValueExpr` 增加内部 `ORDERED` carrier，按需持有独立的 TYPE
  comparator 元数据，保存固定身份/代次/epoch/格式，支持深拷贝、序列化与
  截断校验，不保存插件代码指针。存储参数先显式解码，运行期逻辑值直接包装。
- MIN/MAX 推导保留结果的插件逻辑类型，重复推导沿用绑定。原生兼容类型
  保持原路径，自定义类型必须提供 comparator；MIN/MAX(DISTINCT) 消除
  不影响极值的去重步骤，不调用尚未接入的 carrier hash/equality。
- 聚合处理器的逐行、批量和 rollup 合并均使用固定 comparator，状态保存
  解码值，合并不重复解码。比较前后检查取消，失败不提交当前状态替换；
  loader 继续校验绑定并持有调用期 lease，不新增整段聚合期 module lease。
- 新增实际 Rust 持久类型的 MIN、MAX、MIN(DISTINCT)、MAX(动态函数) 与
  BINARY 原生对照，验证字符数优先顺序与字节顺序的差异、Unicode/内嵌 NUL、
  6/3/1031 行跨批状态、全/部分跳过、全 NULL、逐行分组后 rollup、非法
  UTF-8 比较、取消、绑定元数据不一致及 lease 回收。
- 生产构建、首轮及补充后的完整 kernel、23 项 CTest、源码边界、12 项
  build gate 和差异检查通过。首轮 CTest 的 Rust 脚手架遇到 lld 段错误，
  完整重跑通过，未跳过或放宽断言。Rust ABI/build ID/对象/格式未变，GIS
  继续使用 C++。本轮仍是 Rust 类型语义与 SQL 内核之间的适配实现。
- **完整目标仍未完成：** fixture 手供 `ObAggrInfo` 和输入 frame，虽使用
  真实表达式解析/推导/CG、聚合处理器及 DSO，却不是完整 SELECT 优化器/
  算子生成/实库证据；需继续验证下推、多阶段、窗口和完整计划。ORDER BY、
  分组键/hash/索引、用户自定义聚合状态机、资源预算、实库 catalog 事务、
  AI 异步与轻量化、planner 深度接口、PL/并行/恢复及 Bazel/跨平台均保持
  在原目标内。详见 [MIN/MAX 契约](plugin-type-extrema.md)。

### 前一检查点：比较/BETWEEN/IN 的批量参数消费

- 二元插件比较、BETWEEN 和标量 IN 增加显式 batch evaluator，把批量求值
  传递给嵌套 Rust 函数、cast 和 decoder。TYPE comparator 仍逐值调用，
  不使用载体字节比较/hash 替代插件语义；Rust ABI/build ID/对象/格式不变。
- BETWEEN 仅对非 NULL 主值计算边界，下界 NULL 不跳过上界。IN 按候选
  项推进尚未匹配的行，保留 NULL/UNKNOWN 和首次匹配短路；左值独立持有
  一份字节副本，候选 scratch 逐行复用。内部表达式失败清除有效标记，
  比较前后检查取消，缓存命中不重复执行函数或 decoder。
- 实测发现原生 `<=>`、BETWEEN/NOT BETWEEN 包装层仍会逐行调用内部节点。
  codegen 仅为插件转换生成、其余参数为常量的包装形式接入批量源求值；
  不改原生-only 行为，不以最终结果正确替代批量入口证据。
- 增加 10 个真实 Rust 类型消费场景，原有 batch fixture 一并扩至
  6/3/1031 行。覆盖嵌套动态函数、经 cast 的 v3 concat、NULL 安全相等、
  BETWEEN 三值逻辑、IN/NOT IN 的候选短路和 UNKNOWN，以及匹配后跳过
  非法 UTF-8。函数标量入口不增加，batch 输入行数与预计函数求值次数一致。
- 比较、BETWEEN、IN 各自测试坏存储行先跳过后启用、调用前取消、首个比较
  后取消、有效标记清理及 lease 回收。参数先批量求值意味着首个比较之前
  可能已完成整批解码；取消计数明确区分解码和 comparator，不承诺跨行顺序。
- 生产构建/链接、修复包装层后的完整 kernel、补充故障场景后的最终完整
  kernel、首轮与最终 23 项 CTest、源码边界、12 项 build gate 和差异
  检查通过。前两轮 kernel 的标量入口断言暴露并定位上述包装层问题，
  修复执行路径后通过，没有放宽断言。GIS 仍使用 C++。
- **完整目标仍未完成：** 排序/分组/聚合/hash/索引、完整类型协议、表达式
  总资源预算、decoder 主动输出 NULL、实库 catalog 事务、AI 异步资源与
  轻量化、深层 planner/index、优化器/PL/并行/跨进程恢复及 Bazel/跨平台
  继续推进。这些 SQL frame/真实 DSO 测试不是实库扫描或副作用回滚证明。
  详见 [类型比较契约](plugin-type-comparison.md)。

### 前一检查点：持久参数显式解码与后续批量执行

- 新函数绑定把直接持久参数转为可缓存的 `PluginTypeValueExpr::DECODE`。
  解码结果进入 SQL frame，父函数按结果处理 NULL，再批量计算后续参数；
  行参数准备读取缓存，不再重复解码。替换节点和元数据构建完成并校验逻辑
  身份/epoch 后发布。复制和重复类型推导沿用固定绑定，不重选实现。
- 旧 sparse decoder 元数据的读取、复制、序列化和逐行兼容路径保留。
  两处手工 frame 测试显式构造旧绑定，新路径使用真实 schema/LOB reader、
  SQL codegen 与已安装 Rust DSO 验证，而不是让手工帧冒充新表达式树。
- 新增直接 count、严格/非严格 concat 三种持久列场景，按 6/3/1031 行
  检查全跳过、增量缓存、每个非 NULL 输入一次解码、后续嵌套函数批量入口、
  NULL 后非法参数的短路对照、非法存储头零后续函数调用及 lease 回收。
- 生产构建/链接、最终完整 kernel、首轮与最终 23 项 CTest、源码边界、
  12 项 build gate 和差异检查通过。前三轮 kernel 分别暴露旧手工帧两处
  绑定假设及新 fixture 漏接 LOB service；修正测试接线后完整重跑通过，
  没有跳过断言或回退生产功能。Rust ABI/build ID/对象与存储格式不变，GIS
  继续使用 C++；本轮改动属于 Rust 插件与 SQL 执行器的桥接层。
- **完整目标仍未完成：** codec 仍逐值调用，旧绑定不追溯改写。Rust 示例
  codec 对合法非 NULL 输入不产生 NULL，主动解码为 NULL 的分支仍需专门
  验证。比较/聚合等消费链、表达式总资源预算、实库 catalog 事务、AI 异步
  与轻量化、深层 planner/index、完整类型/优化器/PL/并行/恢复及 Bazel/
  跨平台仍在原目标内。这不是实库扫描或外部副作用顺序/回滚的证明。
  详见 [标量批量契约](plugin-scalar-batches.md)。

### 前一检查点：嵌套 Rust 函数参数的批量执行

- 普通插件函数按参数阶段批量求值子表达式，跳过调用者不需要及已缓存的行。
  严格函数在每个参数完成后排除 NULL 行，后续参数不计算这些行；全部排除
  后不触发常量 dry-run。mask 不借用会被子表达式重置的 SQL 临时 arena。
- cast、类型值与编码包装层批量计算源表达式，转换/codec 本身复用逐值协议。
  内外层 Rust 函数可通过类型转换组合批量调用，不新增 ABI 或改写 GIS。
  Rust 模块仍为 `rust-text-multiarg-v13`，19 服务/23 对象、数据格式不变。
- 直接存储参数的 decoder 尚在行参数准备阶段：预计算在第一个这种参数后
  停止，防止后续参数越过 decoder 的错误或 NULL 决策。两种 NULL profile
  均保留此边界；显式 decoder 表达式和其余消费链后续继续推进。
- 新增直接/经 cast 的两种嵌套 Rust 函数链，按 SQL 名区分父子批次，并检查
  原有严格/非严格嵌套第三参数。6/3/1031 行下父子都走批量入口，函数单行
  入口计数不增加；保留字节分块、缓存、全跳过/严格 NULL、错误和 lease 测试。
- 生产构建/链接、首轮及最终完整 kernel、23 项 CTest、源码边界、12 项
  build gate 与差异检查通过。首次 CTest 的脚手架 lld 遇到 invalid pointer/
  段错误，完整重跑通过；未跳过该测试或放宽断言。
- **完整目标仍未完成：** 旧直接存储参数后缀、比较/聚合等消费链、表达式树
  总资源预算、真实 SQL/网络/异步副作用、AI 调度/轻量化、实库 catalog
  事务、深层 planner/index、完整类型/优化器/PL/并行/恢复与 Bazel/跨平台
  均继续推进。本轮不承诺固定跨行副作用顺序、零拷贝或实库扫描性能。
  详见 [标量批量契约](plugin-scalar-batches.md)。

### 前一检查点：Rust 多参数函数与声明式 NULL 策略

- 增加 Rust `concat3` 服务及严格/非严格两个三参数 SQL 函数。两个对象共用
  服务，以 `NULL_PROPAGATING` 属性选择 SQL 层执行行为；提供单行与 v3
  批量入口。插件自行声明、注册和实现，不新增核心 factory 或 ABI。
- 拼接三段 UTF-8，保留空串/内嵌 NUL，检查合计结果上限并尝试可失败的容量
  预留；使用既有生命周期、取消、错误和索引结果协议。服务增至 19、对象增至
  23，manifest 与 DSO build ID 同步为 `rust-text-multiarg-v13`，数据格式不变。
- Rust 4 项单元测试、严格 Clippy、动态库构建及二进制审计、首轮和补充后的
  完整 kernel、23 项 CTest、源码边界、12 项 build gate 和差异检查通过。
  首次模块编译的相对 validator 路径已修正；首次 CTest 的脚手架 lld 出现
  内存错误/段错误，完整重跑通过，没有跳过测试或放宽断言。
- 新增 8 个标量场景和 4 个 6/3/1031 行批量场景，验证严格 NULL 左到右短路、
  非严格调用、后续非法 UTF-8 参数是否执行、共享列、结果缓存/所有权、精确
  回调次数和 lease 回收。整批严格 NULL 零回调；三参数重复列按全部载荷计费，
  9 行大输入形成 7+2 两批；拼接结果超限不重试且清除有效标记。
- **完整目标仍未完成：** 这些是手供 SQL frame 和真实 Rust DSO 的同步契约
  证据，不是实库写入/网络副作用/异步任务或完整 PG 求值规则证明。参数子树
  批量化、资源总预算、AI 调度/轻量化、实库 catalog 事务、深层 planner/
  index、完整类型/优化器/PL/并行/恢复和 Bazel/跨平台继续推进。GIS 保持 C++。
  详见 [标量批量契约](plugin-scalar-batches.md)。

### 前一检查点：SQL 到 Rust 批量的字节驱动分块

- SQL adapter 同时按实际输入行数与 64 MiB 载荷分块，不再因一组合法行的
  总字节数过大而直接拒绝。下一行只求值/转换/复制一次，当前块交付后移入
  下一块；既不重新执行参数，也不重试插件来猜测合适批量大小。
- 当前块持有独立载荷，逐行临时 allocator guard 不再横跨整块；若调用者
  已经持有外层 guard 则遵守外层生命周期。严格 NULL 短路行不计插件行数；
  NULL/空串仍可作为行传输；重复地址按重复载荷计费。单值/单行限制继续生效。
- 生产构建/链接、完整 kernel、23 项 CTest、源码边界、12 项 build gate
  及差异检查通过。新增 80 MiB 大输入与 NULL/单字节混合测试，精确验证
  4 行/64 MiB + 3 行/16 MiB+1 字节两次调用，覆盖 Rust 原生批量、显式
  逻辑类型 cast 及旧文本服务回退，包含结果、重复读取及绑定/回调次数。
- 后续字节块错误不会重试已执行插件，失败清空整批有效标记；超限单值在
  函数调用前拒绝。首轮测试错把列中的运行期 NULL 视为免 cast 的编译期
  NULL；诊断确认 7 行对应 7 次 cast，依据既有契约修正断言后完整回归通过。
- **完整目标仍未完成：** 传输字节预算不是 SQL 总内存预算，转换膨胀与输出
  超限还不能预先协商；参数子树批量化、多参数严格函数/副作用纵向测试、AI
  异步资源与轻量化、实库 catalog 事务、深层 planner/index、完整类型/
  优化器/PL/并行/恢复及 Bazel/跨平台继续推进。本轮不改 C ABI 或 Rust
  插件实现，GIS 仍使用 C++。详见 [标量批量契约](plugin-scalar-batches.md)。

### 前一检查点：Rust 标量批量 ABI 与 SQL 执行接线

- C ABI 增加可选 function service v3 后缀，Rust SDK 提供 `Batch/Handler/Service`，
  一个 handler 处理整批参数并按索引输出。旧单行服务不读取新后缀，仍可回退。
  SDK 校验输入/输出、类型、布局、数量、预算与取消，Rust panic 不穿越 C 边界。
- loader 在持有对象/实现/cast lease 时转换并执行，先校验和暂存整批结果；
  重复/缺失/越界/错误/取消失败不发布成功结果。调用者交付过程中出错时必须
  丢弃已收到的前缀。Rust 文本服务升级为 `rust-text-batch-v12`，对象身份、
  服务数量与持久数据格式不变，GIS 仍使用 C++。
- SQL `PluginFunctionExpr::evaluate_batch` 经 provider/server/runtime 接入该
  loader。跳过行和已缓存行不送入插件，严格 NULL/codec 共用标量参数准备；
  按最多 1024 候选行分块，最后才发布整次 SQL 批量的有效标记。失败时清理
  旧部分成功标记，避免引擎清空 datum 后错误复用缓存。当前复制输入载荷。
- SDK 全部测试/严格 Clippy、真实 DSO loader 与 C ABI 布局、生产构建/链接、
  首轮及补充后的完整 kernel、23 项 CTest、源码边界、12 项 build gate 和
  差异检查通过。首轮生产编译和随后 fixture 编译分别发现一个 API 名称和
  一个状态类型名称错误，已修正并重跑，没有放宽测试断言。
- 新增四种 SQL 函数表达式的 6/3/1031 行测试：批量分块、旧入口回退、逻辑
  类型、NULL/空串/Unicode/内嵌 NUL、跳过/缓存/重新启用、结果所有权、不重新
  绑定，以及后续分块错误、旧缓存失效、调用前和结果交付期间取消、lease 清零。
  详见 [标量批量契约](plugin-scalar-batches.md)。
- **完整目标仍未完成：** 参数子树尚未整体批量化，单块字节超限仍报错，需
  完善字节驱动分块、严格多参数/副作用测试和整体资源预算。这不是列式/零
  拷贝、异步模型或实库扫描证明。复合类型/子查询 IN/元组 BETWEEN、排序/
  hash/index、完整优化器/PL/并行/恢复、深层 planner、实库 catalog 事务、
  AI 异步资源/轻量化和 Bazel/跨平台均继续属于完整目标。

### 前一检查点：插件类型嵌套元组比较与 IN

- 含插件逻辑类型的嵌套行构造支持七种比较与 IN/NOT IN。逐层校验行/标量
  位置和列数，再按深度优先顺序收集叶子；叶子数量相同而嵌套形状不同仍拒绝。
  每对叶子或各候选对应位置使用现有 Rust 公共类型/cast/comparator，不新增 ABI。
- 类型 visitor 对嵌套比较先用同一个 visitor 推导叶子，避免原生 mixed-row
  检查在识别插件类型之前拒绝；成功绑定后复用已有布尔/CASE lowering。
  纯原生表达式保留原有行结构拒绝规则，未把原生嵌套行比较一并开放。
- IN 候选共享转换后的左侧叶子及 decoder，逐项按需求值；相等/不等/<=>
  使用逐叶子 AND/OR，字典序只在已知相等的前缀后继续。NULL、短路和错误
  不退回载体字节比较。这里是嵌套行构造表达式，不是持久复合类型或 record ABI。
- 生产构建/链接、首轮与扩展后的完整 kernel、23 项 CTest、源码边界、12 项
  build gate 和差异检查通过。新增 126 组比较、24 组 IN、8 组持久列、3 组
  范围、3 组 SELECT、4 组批量表达式；范围累计 33 组，批量累计 23 组表达式。
  测试覆盖形状/类型错误、NULL、实际回调次数、共享解码、坏数据短路、复制/
  预处理、lease 回收，以及二元比较 63 层成功/64 层溢出边界。
- **完整目标仍未完成：** 复合类型对象、集合子查询 IN、元组 BETWEEN、排序/
  hash/index、完整优化器/PL/并行与计划恢复、深层 planner、实库 catalog
  事务、AI 批量接口/资源/轻量化及 Bazel/跨平台继续推进。内存 schema 与手供
  frame 不代替实库扫描或恢复。Rust 类型协议和 C++ GIS 算法本轮不改写。

### 前一检查点：插件批量表达式与惰性执行修复

- 新增真实 Rust DSO 的批量表达式 fixture，覆盖七种标量比较、正反 BETWEEN/
  列表 IN、simple CASE、平坦元组比较/IN，以及 CASE 中的插件函数和持久类型
  输出。19 组表达式分别复用同一 frame 执行 6→3→6 行，共 57 组批次。
- 整批跳过测试发现 CASE 的常量 ELSE 仍执行一次 Rust cast。原因是属性提取
  先执行 `add_const()`，之后才恢复 `IS_STATE_FUNC`，插件节点因此错误继承
  常量属性并进入标量 dry-run。已在 `is_const_inherit_expr` 显式排除插件执行
  节点，并补齐普通插件函数/编码器的状态标记；不修改原生表达式 dry-run 规则。
- 测试检查原始/复制/预处理后的节点属性，整批跳过时零回调、部分行跳过、
  重复读取不重算、重新启用跳过行、大小批交替、精确函数/decoder/comparator
  次数，以及结果不借用输入 LOB。错误版本行被跳过时不解码，启用后报错；
  调用前取消和首个真实 comparator 返回后的取消均传播，错误后重置 frame
  再执行成功，模块 lease 回收到零。
- 最终生产构建/链接、完整 kernel、23 项 CTest、源码边界、12 项 build gate
  与差异检查通过。首次 CTest 在 Rust 脚手架链接阶段遇到 lld abort，完整重跑
  和最终回归通过；整批跳过失败通过修正生产分类解决，没有放宽测试断言。
- **完整目标仍未完成：** 当前是批量 SQL frame 调用逐值 Rust 回调，不是新
  批量 ABI、零拷贝或实库扫描证明。基于明确 volatility 的常量优化、深层
  planner、排序/hash/index、完整优化器/PL/计划恢复、实库 catalog 事务、AI
  资源与轻量化、Bazel/跨平台仍需推进。Rust 协议及 C++ GIS 算法本轮不改写。

### 前一检查点：插件类型平坦元组 IN / NOT IN

- 左值或候选含插件逻辑类型的平坦元组 IN/NOT IN 已接线。先检查列数和候选
  上限，再逐列收集左值与所有候选；自定义列使用 Rust 公共类型/隐式 cast，
  原生列沿用原生标量比较规则，不为整行定义一个载体类型。转换完成后发布。
- 转换后的左值列、cast 和持久 decoder 由各候选共享，每个元素首次被比较时
  才求值，不随候选数重复调用。各候选的逐列相等结果经惰性 OR 组合：真可覆盖
  此前的未知，没有真但有未知则结果 NULL，全假才为假；NOT IN 保持三值逻辑。
  不采用标量 NULL 左值跳过列表的规则，也不以载体字节 hash 替代插件语义。
- 单候选直接使用行比较结果，多候选才构造 OR；同时处理单列行比较的同类
  AND/OR 参数数量约束。首轮 kernel 在代码生成阶段发现单参数 OR 不合法，
  修正后重新完成生产构建和完整 kernel，而非修改预期错误以跳过该场景。
- 最新生产构建/链接、23 项 CTest、完整 kernel、源码边界、12 项 build gate
  与差异检查通过。新增 48 组 IN/NOT IN 表达式、8 组持久列读取、3 组表列范围
  及 2 组 SELECT 解析；已有元组拒绝用例改为验证成功结果及精确回调次数。
  表列范围累计 30 组，自定义谓词未被当成载体存储范围。
- 用例覆盖未知后再匹配、确定不匹配覆盖列 NULL、未执行的非法候选、已执行
  错误、左值函数/cast/decoder 次数、原生公共类型、Unicode/内嵌 NUL、列数/
  类型错误、复制/重复推导和 lease 回收。自定义 1023 候选成功，1024 候选拒绝，
  1024 候选的纯原生元组对照仍成功；这个插件参数上限不是完整扩展性的终点。
- **完整目标仍未完成：** 嵌套元组、集合子查询 IN、元组 BETWEEN、排序/hash/
  index、完整优化器/PL/并行/计划恢复、深层 planner、实库 catalog 事务、AI
  资源与轻量化、Bazel/跨平台继续推进。当前是内存 schema/受控行 frame 的
  内核证据，不是实库扫描、存储恢复或全部计划变换证明。未新增 C ABI/Rust
  服务；复用 Rust 类型/转换/比较协议，C++ GIS 算法保持不变。

### 前一检查点：插件类型平坦元组比较

- 含自定义逻辑类型的平坦元组支持 `= / <> / < / <= / > / >= / <=>`，同位置
  的列独立经过 Rust 公共类型/cast 选择和持久值解码；不同列无需采用同一种
  类型。原生-only 元组仍走原有执行路径，不通过插件框架重新定义原生比较。
- 相等、不等、NULL-safe equality 分别组合逐列 `=` 的 AND、`<>` 的 OR、
  `<=>` 的 AND。某列 NULL 时仍允许后续不等列决定整体结果；大小关系则只在
  当前列确定相等时继续，首个不等/未知列决定结果并跳过后续列。
- 字典序用平坦 CASE 表达，深度不随元组列数线性增长。共享每列已绑定的比较
  结果，不为“相等检测”和“大小关系”重复调用 Rust comparator/decoder。
  绑定后原始 row 节点转换为布尔结果比较、清理旧 operator 缓存；复用现有
  pointer-free extra-info、租约和逻辑/CASE 执行器，不新增 C ABI 或 Rust 服务。
- 最新生产构建/链接和完整 kernel 通过。新增 133 组七运算符表达式、7 组持久
  列读取、2 组表列范围及 2 组 SELECT 解析用例；检查 NULL、逐列短路、混合
  类型、回调次数、解码错误、形状/类型错误、复制/重复推导、正常预处理与 lease
  回收。表列范围累计 27 组；自定义谓词不被当作载体字节范围提取。
- 首轮 kernel 发现已有参数节点不能再次调用初始化用的 `set_param_exprs`，
  已改为替换参数槽并重新完成生产构建及 kernel。首轮 CTest 的 Rust 脚手架
  链接遇到 lld 段错误；随后完整 23 项 CTest 重跑通过，没有修改测试来掩盖失败。
  源码边界、12 项 build gate 与差异检查通过。
- **完整目标仍未完成：** 嵌套元组、元组 IN/BETWEEN、集合子查询比较、排序/
  hash/index、完整优化器/PL/并行/计划恢复、深层 planner、实库 catalog 事务、
  AI 资源与轻量化、Bazel/跨平台继续推进。当前仍是内存 schema/受控行 frame
  下的内核证据，不是实库扫描、完整规划或存储恢复证明。C++ GIS 算法不改写。

### 前一检查点：插件类型 simple CASE 匹配

- `PluginBranchType::prepare_case` 在载体类型聚合之前识别自定义匹配输入，逐个
  绑定 `selector = WHEN`。每对输入独立使用 Rust 公共类型/隐式 cast 规则，
  不要求所有 WHEN 具有一个公共类型；自定义比较调用 Rust comparator，声明
  允许转为原生类型的比较继续用原生规则。THEN/ELSE 输出类型独立推导。
- 选择值节点由全部比较共享，持久选择值的 decoder 也只构造一次。原始与复制
  后的 DAG 保持共享，执行中的函数/decoder 次数得到实际 Rust DSO 用例验证。
  成功绑定后将同一个 `ObCaseOpRawExpr` 从 ARG_CASE 改为 searched CASE，释放
  旧 operator 缓存、刷新表达式属性。原生-only 匹配继续原有重写路径。
- 复用现有比较 extra-info、租约和 CASE 惰性执行，不新增 ABI、表达式编号或
  Rust 服务。NULL 不与 NULL 匹配；按顺序求值 WHEN，只执行首个匹配的 THEN，
  否则执行 ELSE/返回 NULL。选择值或已执行 WHEN 的错误不能被吞掉，也不能
  因过期 binding 或取消而落入 ELSE。与列表 IN 不同，不对 NULL 选择值跳过 WHEN。
- 生产构建/链接、23 项 CTest、完整 kernel 两次运行、源码边界、12 项 build
  gate 与差异检查通过。最终 kernel 包含 27 组表达式、6 组持久列读取、2 组
  表列范围和 3 组 SELECT 解析用例；另检查复制/重复推导、正常表达式预处理、
  单次函数/decoder、精确 cast/comparator 次数、第二个比较绑定过期、取消和
  lease 回收。表列范围用例累计 25 组，自定义 CASE 谓词不提取为载体范围。
- 本轮扩展的是已由 Rust 实现的类型协议在 SQL 中的可组合性；C++ GIS 算法和
  Rust codec/comparator 不改写。**完整目标仍未完成：** simple CASE 在完整
  优化器、PL 专用复制、并行/跨进程计划中的行为还需验证；子查询/元组 IN、
  排序/hash/index、深层 planner、实库 catalog 事务、AI 资源与轻量化、Bazel/
  跨平台继续推进。受控 schema/行 frame 不是实库、持久恢复或完整优化器证明。

### 前一检查点：插件类型标量列表 IN / NOT IN

- 新增内部 `PluginTypeInExpr`（Query/JIT 编号 1938），左值和候选统一经过 Rust
  common-type/cast 选择。自定义公共类型使用已绑定 TYPE comparator 做线性匹配，
  不退回载体字节比较或原生 hash；原生公共类型继续使用声明的 cast 和原生 IN。
  复用 pointer-free extra-info、TYPE/codec 租约及 Rust 服务，不新增 C ABI。
- 左值只求值一次，NULL 左值跳过列表；首个匹配停止后续求值，无匹配但含 NULL
  返回 NULL。已求值候选的错误、过期 binding 和取消正常传播。左值物化内存独立
  保留，候选物化内存逐项复用；不声称整个 expression frame 内存因此有界。
  当前自定义列表最多 1023 个候选，纯原生列表不受这个插件参数上限限制。
- 解析器保留涉及插件或未定型列/子查询的单元素 IN，避免过早变为二元比较。
  子查询检查覆盖 ANY/ALL 包装被信息提取移除后的 `SQ_*` 形态；自定义集合
  子查询/元组比较暂时明确拒绝，原生对照保持可解析和执行。
- 重新确认完整生产构建/链接、23 项独立 CTest、完整 kernel、源码边界、12 项
  build gate 与差异检查通过。新增 54 组正反 IN 表达式（包含绑定失败用例）、
  7 组持久列读取、6 组表列范围和 5 组 SELECT 解析用例，另检查复制/重复推导、
  精确回调次数、1023/1024 候选边界、长值所有权、错误/取消与 lease 回收。
- 首次重跑失败于原生单元素 IN 的范围测试预期：范围生成器本就排除 IN 节点
  的 `is_precise_get` 标记。测试改为额外核对 IN 节点、精确候选数量、闭区间边界
  及没有多余分支，完整 kernel 再次通过；没有为通过测试修改范围生成器。
  自定义列仍得到全范围，原生 IN 仍提取受限范围，原生整数等值仍为 precise get。
- **完整目标仍未完成。** 集合子查询/元组 IN、simple CASE 匹配、排序/hash/index、
  完整参数化与计划恢复、深层 planner、实库 catalog 事务、AI 资源与轻量化、
  Bazel/跨平台继续推进。当前 kernel 使用内存 schema/受控行 frame，不是实库
  扫描、存储恢复或完整优化器行为的证明。C++ GIS 算法没有改写。

### 前一检查点：插件类型 BETWEEN / NOT BETWEEN

- 新增内部 `PluginTypeBetweenExpr`（Query/JIT 编号 1937），复用现有 TYPE
  comparator 和 pointer-free extra-info，不新增 C ABI 或 Rust 插件服务。
  三个操作数统一选择公共类型和允许的 cast；自定义公共类型经专用表达式计算
  0/1/NULL，再由原始 BETWEEN/NOT BETWEEN 节点处理结果。原生公共类型保持
  原生执行器路径，持久输入复用 decoder。
- 值仅求值一次；值为 NULL 时不求值边界，否则按下界、上界顺序求值，再比较。
  下界比较已能判假时可以不调用第二次 comparator，但不吞掉此前上界求值错误。
  NULL 不传给 Rust comparator；未越界且存在 NULL 边界时返回 NULL，确定越界
  时返回假。每次插件比较前后检查查询状态，错误/取消正常传播。
- 修正解析期的提前展开：插件值、包含插件值的表达式，以及类型尚未确定的
  列/子查询不再提前复制为两个比较。正常类型推导后再选择执行路径；原生列
  BETWEEN 的范围提取仍有效，并非关闭原生范围优化。该保护在实验插件构建生效。
- 完整生产构建/链接、23 项独立 CTest、完整 kernel、源码边界、12 项 build
  gate 和差异检查通过。新增 42 组正反 BETWEEN 表达式、6 组持久列读取和
  6 组表列范围用例，另覆盖无公共类型、过期 epoch、实际 Rust comparator
  错误、取消与 lease 回收。测试核对函数/cast/comparator 次数，不只检查结果。
- 本轮没有改写 C++ GIS 算法或 Rust codec/comparator；它将已开放的 Rust 类型
  协议接入更多 SQL 语义。**完整目标仍未完成：** IN、simple CASE 匹配、排序/
  hash/index、完整参数化与计划恢复、深层 planner、实库 catalog 事务、AI
  资源/轻量化、Bazel/跨平台继续推进。内存 schema 和手供行 frame 不代替实库。

### 前一检查点：Rust 持久列解析与范围提取验证

- 新增 11 组完整 SELECT 表列谓词用例，通过 schema guard、正常表名/列名解析
  和 `ObPreRangeGraph::preliminary_extract_query_range`，不再手工绑定列引用。
  测试表有有效整数主键和独立 Rust 持久 TYPE 列；schema 仍由内存 fixture 提供。
- 覆盖七种标量比较、反向比较、AND/OR 组合，以及原生整数等值对照。插件列
  保留 stored/logical ID 元数据，运行期 epoch 不进入持久身份；重复类型推导
  不重新解析 binding。解析和范围提取不执行 Rust 函数、cast、decoder 或 comparator。
- 上述自定义谓词得到全范围，且没有被消费为 storage range expression；原生
  整数对照仍产生精确等值范围。这证明当前比较 lowering 能通过这条实际范围
  提取路径，不是把所有原生范围优化关闭。**全范围不是插件索引功能的终点**，
  后续必须接入类型/索引支持函数与代价协议，才能安全利用自定义排序进行扫描。
- 生产目标构建检查、23 项独立 CTest、完整 kernel、源码边界、12 项 build
  gate 和差异检查通过。调试中补齐测试表主键、系统表检查器、数据库限定名、
  statement factory 和 SQL context；没有放宽生产校验或修改 Rust/C++ GIS 算法。
- **边界：** 这是受控 schema 下的正常解析与范围提取证据，不是完整
  `ObOptimizer::optimize` 成功、实库扫描、鉴权、并发、DML/恢复或全部重写的证明。
  混合列范围、IN/BETWEEN、排序/hash/index、深度 planner hooks、真实 catalog
  事务、AI 资源/轻量化和 Bazel/跨平台仍在完整目标内。

### 前一检查点：独立 Rust 持久类型与表达式读写验证

- 新增 Rust `rust_stored_utf8` TYPE、codec 与双向 cast，使用独立逻辑/格式身份。
  原 `rust_utf8` 继续非持久，原 codec/comparator 和 C++ GIS 算法保持不变。
  新格式为 `52 55 54 01` 加逐字节取反后的 UTF-8，编码有 16 MiB 上限；NULL
  不用字节序列表示。它刻意不保留逻辑排序，便于识别宿主是否遗漏解码。
- TYPE 声明 PERSISTENT/REQUIRES_CATALOG，模块另声明 persistent-data capability。
  SDK 补充对应公开常量，未改变 C ABI 布局。初次实际加载被已有持久能力检查
  拒绝，补齐 manifest 能力后加载与打包测试通过，没有放宽 loader 验证。
  新 build ID 为 `rust-text-stored-type-v11`，18 个服务、21 个对象。
- 新增 13 组 kernel 列表达式用例：正常 column schema 元数据进入原表达式，
  真实 in-row LOB reader 取数据，实际 Rust codec 解码，再进行比较、显式 cast
  或动态函数调用。覆盖 NULL、空串、内嵌 NUL、与物理次序相反的比较、格式版本/
  截断/UTF-8 错误、单次 decoder 调用、结果所有权和 lease 回收。
- 另有 6 组正常 SQL 列转换赋值用例，经 `build_column_conv_expr` 和真实 Rust
  encoder 输出持久编码，再由 decoder 还原。覆盖 Unicode、空串/内嵌 NUL、
  NULL，以及不把 explicit-only cast 当作 assignment cast 的拒绝路径。
- 生产目标构建检查、DSO 重新构建/审计、23 项 CTest、SDK/插件严格 Clippy、
  两项 Rust codec 单测、完整 kernel、源码边界、12 项 build gate、格式及差异
  检查通过；最终 kernel 包含上述列读取和赋值用例，也保留上一轮 84 组比较测试。
- **验证边界未扩大为实库。** 列的行数据由 fixture 填入 frame，LOB 服务只允许
  in-row 读取；没有执行真实表扫描、DML 落盘、事务提交、重启/迁移、并发或索引。
  本轮证明表达式/codec 链能复用到实际 Rust 持久 TYPE，不表示全部优化器变换
  已验证。排序/hash/index、深层 hook、实库事务、AI 资源与轻量化、Bazel/跨平台
  等完整目标继续推进。详见 [类型比较与持久参考类型](plugin-type-comparison.md)。

### 前一检查点：Rust 类型比较接入 SQL 标量表达式

- 新增 `PluginTypeComparisonExpr` 与字段序列化的 extra-info，Query/JIT 编号
  同步为 1936。标量 `= / <> / < / <= / > / >= / <=>` 在物理类型降级前处理
  插件逻辑类型；Rust 选择 common type 与允许的 cast，存储值复用已有 decoder。
- 自定义公共 TYPE 使用按 ID/epoch 绑定的比较回调，将 `a OP b` 转为内部比较
  结果 `OP 0`；原生公共类型则执行声明的 cast 后沿用原生 SQL 规则。替换后刷新
  表达式属性，优化器不再直接面对原始自定义值的载体关系比较。
- 普通比较保留 NULL 三值逻辑，`<=>` 处理双 NULL/单 NULL，NULL 不进入插件。
  比较前后检查查询状态，错误不降为 NULL 或相等；重复推导和 plan copy 保留
  binding，不重新选择 native 入口。extra-info 支持非持久 TYPE，不保存插件地址。
- 完整生产构建/链接、23 项 CTest、完整 kernel、源码边界、12 项 build gate
  与差异检查通过。kernel 新增 84 组七运算符结果测试，经实际解析/类型推导/
  复制/代码生成/真实 Rust DSO 执行，另验证序列化/截断、stale epoch、回调
  错误、取消、SELECT/WHERE 解析和无公共类型等失败路径。
- 构建过程修复了新类型前置声明、固定三参数推导分派、tuple 检查位置及替换后
  属性刷新；最终生产构建与 kernel 重新执行成功。首轮 SDK doctest 链接器异常
  退出后，完整 CTest 重跑全部通过。本轮没有修改 Rust SDK/插件或 C++ GIS 算法。
- **完整目标仍未完成。** 自定义 tuple 暂时明确拒绝；IN/BETWEEN、simple CASE
  匹配、ORDER BY、去重/hash/index 和完整参数化场景继续推进。实际持久列扫描、
  全部优化器变换及跨进程计划恢复没有由本次 fixture 证明。实库事务、其它深度
  hook、AI 资源与轻量化、Bazel/跨平台仍在完整范围内。
  详见 [类型比较](plugin-type-comparison.md)。

### 前一检查点：逻辑 TYPE 身份绑定与比较宿主桥接

- 新增按逻辑 TYPE object ID 的解析入口，从同一不可变 registry snapshot 的
  Rust object catalog 查找身份与 epoch；不依赖表达式保存 SQL 类型名称。
  支持 expected epoch，查询元数据不取得代码 lease，失败清空绑定。
- `ObIModuleProvider`、`ObServer`、`ObServerPluginRuntime` 已接入逻辑 ID 解析、
  comparator 能力探测与执行。独立 provider 可不支持新接口；未初始化路径与
  不支持路径均明确报错并清空输出。本轮不改变公开 C ABI、Rust comparator
  或 C++ GIS 实现。
- 比较的 epoch 校验进入 TYPE/codec 联合租约获取的同一临界区，避免在校验与
  acquisition 之间 registry 改变后继续执行旧绑定。测试覆盖独立发布导致旧
  epoch 失效、prepared/quiesced 对象不可见、换代身份、无租约查询及失败清空。
- 完整生产构建/链接、23 项 CTest、112 项 Rust runtime 单测、完整 kernel、
  源码边界、12 项 build gate 与差异检查通过。kernel 经实际 module provider
  调用真实 Rust DSO，验证按 ID 绑定后 `z` 排在 `aa` 前、过期绑定拒绝，以及
  默认 provider/未初始化 server runtime 的输出契约。最初新增测试的 C++
  initializer-list 类型推导错误已修正，再执行完整 CTest 通过。
- **SQL 比较运算符尚未自动调用 comparator。** 本轮完成其宿主绑定前置路径，
  下一步仍需表达式 lowering、NULL/coercion、计划序列化与优化器语义验证。
  ORDER BY、去重/hash/index、实库事务、其它深度 hook、AI 资源与轻量化、
  Bazel/跨平台均继续保留在完整目标中。详见 [类型比较](plugin-type-comparison.md)。

### 前一检查点：类型比较 C ABI / Rust SDK / loader

- codec SPI 1.1 追加可选 compare 回调，保留完整 v1 前缀。协议针对同一逻辑类型
  的已解码非 NULL 值，定义独立于物理编码的稳定全序；不向 callback 借出 SQL、
  session 或 allocator。未声明排序的旧类型无需实现，也不自动获得 hash/index 契约。
- 新 Rust `type_comparison::{Comparator, Service}` 校验元数据/类型身份、借用
  输入，将 `Ordering` 转为 -1/0/1，并处理错误及可展开 panic。`rust_text` 使用
  字符数优先、字节序其次的比较；build ID 为 `rust-text-type-comparison-v10`，
  服务/对象数量不变。原 decode/encode 与 C++ GIS 不改写。
- loader 按 generation/epoch、TYPE 身份及格式检查能力，固定对象与实现 lease
  后调用；失败清空 ordering，不退回物理字节。白盒测试覆盖真正的旧 v1 分配边界、
  畸形服务后缀/结果、主错误和 C++ 异常；END_OF_STREAM 作为非法比较结果拒绝，
  不能由后续 SQL 消费者误当成查询结束。
- 完整生产构建/链接、23 项 CTest、SDK/插件严格 Clippy、源码边界、12 项 build
  gate、SDK 格式/差异检查和完整 kernel 重新执行通过。实际 DSO 用例检查完整
  比较矩阵（Unicode、空串、内嵌 NUL）、非法 UTF-8、类型/长度/保留字段、过期
  epoch、零 generation、格式变化和 lease 回收；旧 GIS codec 仍拒绝新能力。
  首轮 SDK 测试遇到链接器 SIGSEGV，之后完整 CTest 中的 SDK 重跑通过；新宿主
  测试的状态枚举拼写、GIS 旧 alias 与 TYPE 身份的测试输入差异也已修正。
- **SQL 运算符自动绑定、ORDER BY、GROUP BY/DISTINCT、hash join 和索引尚未使用
  新 callback。** 这是可执行类型语义协议与 loader 接线，不是 SQL 比较全链路
  完成。下一步接真实表达式类型推导、coercion、NULL 与计划绑定，避免优化器将
  自定义比较误用为载体字节的范围/hash 语义。实库事务验证、其它深层 hook、AI
  资源与轻量化、Bazel/跨平台仍在完整目标中。详见 [类型比较](plugin-type-comparison.md)。

### 前一检查点：CREATE mutation 与 Rust 日志组合验证通过

- `caller_routine_mutation_fixture.h` 现在为实际 `ObSql` 绑定真实 `ObPL`，通过
  `CallerRoutineMutation::preflight/apply` 串联普通 PL 语义解析、保留 ID/version、
  `RoutineCatalogWriter`、schema/ACL 私有视图与 Rust journal。使用已有测试友元
  注入 PL collaborator，没有改生产接口、替换 resolver 或降低授权规则。
- 新增 CREATE 成功及第二个 routine 引用刚创建函数的用例，检查保留对象 ID、
  所有参数的 ID/version、owner、实际生成的依赖写入 SQL，以及无需发布/换 guard
  的后续 PL 解析。没有执行生成 routine 的 SQL 调用或 PL bytecode。
- 覆盖 automatic_sp_privileges 开/关：不仅核对私有 ACL，还确认自动授权产生的
  额外 schema version 进入 Rust 日志。模拟数据层已确认保存点回退后，真实 Rust
  journal 撤销第二次创建及版本记录，保留第一次对象/权限；继续退到首个 barrier
  后清除两个新对象及记录。数据回滚本身未执行。
- 写入失败不返回对象 ID、不发布视图；同名冲突保留冲突对象；缺失被调用函数在
  PL 预检阶段失败，不分配 ID/version、不写 SQL，不能降为延迟运行时报错后创建。
  正常 writer 的额外 ACL 版本、私有视图 lookup 要求传入当前 schema 对象、同名
  冲突应使用不同 ID，均已纳入正确的测试输入/断言。
- 本轮完整 kernel 重新编译、链接、执行通过，包含原实际 Rust DSO 用例；源码
  边界、12 项 build gate、差异检查通过。本轮只改变 fixture 和开发文档，未改变
  生产 C++/Rust。上一检查点的生产构建、112 项 Rust 单测、Clippy、23 项 CTest
  结果不冒充本轮重新执行。
- 本轮实际探测确认沙箱创建本地 socket 返回 `EPERM`，无法启动可连接的实库
  实例。上述行传输、版本分配、principal 和 DDL admission 仍是受控输入，ObSql
  只注入 PL collaborator，并非完整服务初始化。公开 SPI 成功写入、真实数据事务、
  SQL 后续调用、保存点/回滚/提交、并发和 shutdown 仍需实库证明；完整设计中的
  深层 planner/index/type、AI 资源预算/轻量化、Bazel/跨平台继续保留在目标内。

### 前一检查点：公开 Query mutation SPI 与 Rust SDK（局部验证通过）

- SQL API minor 3 以 `seekdb_plugin_sql_api_v4_t` 追加 `mutate_routine`，保持
  execute/poll/lookup 前缀；通过真正的 `CallerRoutineMutation` 和 Rust 单操作
  协调器执行，不走独立安装事务。C 结果保留操作 outcome、主错误及各清理错误，
  只在临时成功时返回对象 ID，不宣称已经提交。
- Rust SDK 的 `Call`、`QueryContext`、`Rows` 提供独立能力探测和 mutation 方法；
  检查 API/结果布局、保留字段及 ID，传播宿主首错。SQL 输入交由宿主检查并记录
  错误，表函数忽略错误后也不能继续发出成功行。新参考函数
  `seekdb_rust_routine_ddl` 不标记 immutable/deterministic，模块 build ID 为
  `rust-text-query-mutation-v9`，15 个服务、18 个对象；GIS 无需改写为 Rust。
- 真实 loader 测试发现测试校验器仍期待旧 build ID，导致新 DSO 被拒绝；已更新
  测试元数据，并给两处实际加载增加失败诊断，没有放宽生产 artifact 校验。
- 本检查点完整生产构建/链接、112 项 runtime 单测、runtime/SDK 严格 Clippy、
  23 项 CTest、源码边界、12 项 build gate、格式与差异检查通过。首轮 scaffold
  测试曾遇到链接器崩溃，降低 Cargo 并行度后完整 CTest 重跑通过。
- 完整 kernel 执行通过。新增 public mutation fixture 使用实际 C SPI、调用者
  frame 和 Rust DSO，覆盖无计划、缺少事务服务时 prepare 失败、空/非法输入、
  大小、UTF-8/NUL、多语句、权限禁用、超时、跨线程、重入、短结果区、首错保持和
  失败不 emit/不返回 ID。它没有数据事务服务，只证明拒绝及 prepare 失败清理。
  SDK 另外覆盖旧分配边界、缺少能力、结果/清理字段和表函数吞错后的关闭行为。
- **完整目标仍未完成。** CREATE 成功仍缺实际完整 Query/PL runtime 验证；实库
  跨语句可见性、保存点、鉴权、并发、提交/回滚及 shutdown 尚未证明。当前直接
  ALTER/DROP 成功是受控行/分配器 fixture，不能升级为真实数据库事务证据。
  深层 planner/index/type、AI 异步资源预算与轻量化测量、Bazel/跨平台继续推进。

### 前一检查点：具体 Query routine mutation（局部验证通过）

- 新增 `CallerRoutineMutation` 与 `ExtensionRoutineResolver::mutate`：真实 Query
  runtime 绑定普通 resolver 服务，解析/授权后，由既有 Rust 单操作协调器驱动
  调用者 frame；在同一 borrowed transaction 上执行实际 ID/version reservation、
  `RoutineCatalogWriter` 和 schema/ACL 私有视图更新，不创建 Extension 成员，
  不独立提交或发布 schema。只有 APPLIED 才向入口调用者返回 provisional ID。
- 新增不带包身份的单条 UTF-8 routine SQL 解析；拒绝多语句、非法字节和不支持的
  对象类别，并禁止将查询输入作为安装包或追加安装声明。复用普通 DDL 的依赖版本
  冲突检查，不构造未初始化的 Root DDL service。
- apply 再核对当前身份/数据库/SQL mode、同一配对视图、事务 recorder admission、
  数据库状态及正常权限。CREATE 使用真正的保留 ID/version，并戳记所有参数；
  MySQL 属性 ALTER 从同一事务读取原有依赖，不能用 resolver 空数组清空依赖；
  DROP 保留普通 writer 的 Extension 成员保护，失效请求写入调用者 journal。
- 新 kernel 用例发现查询 mutation 和既有 Root update admission 的权限定长数组
  在 push_back 前缺少 `reserve(1)`，会直接报 `OB_NOT_INIT`；两处均已修复。
  mutation 现在提供失败阶段诊断，保留原始错误码，诊断分配失败也不覆盖首错。
- 完整生产编译/链接、112 项 Rust 单测/严格 Clippy、23 项 CTest、源码边界与
  12 项 build gate 通过。完整 kernel 实际执行通过：新增直接 mutation 用例覆盖
  ALTER/DROP 成功、重复执行拒绝、授权变化、数据库切换、失败 journal、权限禁用、
  schema 版本冲突、写入超时、Extension 成员保护、DROP IF EXISTS 空操作，以及
  schema/权限视图与对象 ID 结果。单条解析覆盖 CREATE/ALTER/DROP、procedure 内部
  分号、非法 UTF-8/NUL/大小、多语句/错误尾部和查询输入不能作为安装包；实际
  frame 的缺失计划失败不返回对象 ID。原 SQL/PL/Rust DSO 用例保持通过。
  新 mutation 用例的数据库行、版本分配、principal 与 DDL admission 均是受控输入，
  不是实库提交/回滚证明；CREATE mutation 的成功执行仍需完整 runtime/PL 接线测试，
  不能以已有独立 CREATE resolver/writer 测试替代这一整条路径。
- 公开 SQL SPI/SDK 的 CREATE/ALTER/DROP 尚未接通。真实数据库跨语句事务、保存点、
  鉴权、并发和 shutdown 验证仍需完成；深层 planner/index/type、AI 资源与轻量化、
  Bazel/跨平台仍属于完整目标。

### 前一检查点：实际 Query catalog frame（局部验证通过）

- 新增 `run_caller_catalog_operation`，由实际 `ObExecContext` 构造宿主 frame，
  接入已验证的 Rust 单操作协调器。检查当前 session/SQL context/guard、正常权限
  模式、可写性、root branch 和查询状态；预检不能偷偷改变事务身份。
- prepare 调用真实 `prepare_plugin_sql` 承接外层 SELECT 的语句事务所有权，
  使用数据层匿名 barrier，登记 session 配对视图并绑定当前 guard，再打开
  `CallerCatalogTransaction`。apply 取得 guard base version，执行 DDL 锁/epoch
  admission 后将同一 borrowed transaction、配对视图和失效 sink 交给宿主 mutation。
  没有另开安装事务，也没有 commit/publish；mutation 忽略 SQL 错误也不能报告成功。
- 数据回滚直接使用事务服务，成功后才由 Rust 调度 journal rollback，避免调用
  合并包装器而提前/重复 undo。poison 先使捕获的 journal 失败并退休视图，再只对
  仍匹配 ID/base 的活动数据事务尝试 abort；不会清理换代后的参与者。
  新增 session `fail_plugin_catalog_transaction` 提供同样的无分配撤销接口。
- transport 的 `close(cleanup_result)` 分离 sticky 操作错误和实际恢复错误，兼容
  原 `close()` 返回值。恢复抛异常也转换为宿主错误并释放 owner；普通写入失败且
  恢复成功时，Rust 仍可尝试保存点回滚，不误判为上下文损坏。
- 112 项 Rust 单测/严格 Clippy、23 项 CTest、源码边界、12 项 build gate 和关闭
  插件宏的实际 session Unity 语法检查通过。完整生产构建、最终链接、测试入口
  语法检查和完整 kernel 实际执行通过。首次 kernel 暴露测试系统变量默认值初始化
  顺序依赖，已将公共初始化从单个 resolver fixture 移到 main，再次完整执行通过。
  新用例验证实际 frame 的缺失计划/权限禁用/超时拒绝、语义预检返回错误和异常
  不建事务、真实 prepare 因缺少事务服务失败后的空清理，以及 session fail 的
  ID/base 隔离、首错保留、旧视图退休且新参与者不被旧失败通知影响。原 SQL/PL/
  Rust DSO 用例保持通过。没有执行真实成功的 catalog 数据写入或数据 abort。
- **公开查询 CREATE/ALTER/DROP 仍未接通。** `ICallerCatalogMutation` 是可信 Query
  实现接口，不是插件注册回调；具体 routine resolver/普通授权、依赖及 ID/version
  reservation/writer 实现仍需接入，然后连接 SQL SPI/SDK。当前 frame 的新用例
  只验证真实预检拒绝与缺少事务服务时的 prepare 失败，没有假装完成真实数据写入。
  实库事务/并发、深层 planner/index/type、AI 资源/轻量化和 Bazel/跨平台仍在完整目标内。

### 前一检查点：Rust 查询期单操作协调（局部验证通过）

- 新增 `query_operation.rs` 和宿主 C ABI/C++ `run_catalog_operation`。Rust 管理
  单次 catalog 操作的 preflight、prepare、apply、close、身份核对及失败恢复顺序；
  不持有跨宿主 SQL 的 journal borrow、锁或动态分配状态，不提交、发布或另开安装事务。
  APPLIED 仅表示调用者事务内的临时成功；对象 ID 只能在这个结果下交给上层。
- prepare 即使部分失败也 close；close 或身份核对失败不使用不可信 context 回滚。
  数据回滚失败不撤销私有视图来伪装成功；只有确认数据回滚后才调用视图 undo。
  清理不确定则要求 poison，分别保留操作首错、close、身份、数据/视图回滚及
  poison 错误。poison 本身失败也不把结果降级为可继续提交。
- Rust journal 新增 `fail`：保持首个宿主错误，禁止新记录/写入/保存点回退/提交，
  但不执行 SQL、undo 或推断数据结果。已知 abort/析构仍清理旧 mark；宿主须同时
  退休配对视图，且只能作用于捕获的事务，不能操作换代后的 session 事务。
- 112 项 Rust 单测/严格 Clippy、23 项 CTest、完整生产构建/链接、源码边界与
  12 项 build gate、测试入口语法检查和完整 kernel 实际执行已通过。新增
  C++→Rust fixture 验证临时成功不封闭 journal、保存点只回退本次操作而保留
  此前 schema/ACL、prepare 部分失败、各阶段 bad_alloc 跨桥转换、close/身份/数据/
  视图清理失败不误报成功、首错与次错分别保留、poison 后禁止提交且已知 abort
  仍消费 mark，以及旧 schema 指针寿命。原 SQL/PL/Rust DSO 用例保持通过。
  fixture 使用真实配对视图与 Rust journal，SQL/data effects 是明确的受控输入，
  不证明真实数据事务、resolver/writer 生产接线或普通权限验证已经完成。
- **这是宿主协调入口，不是公开插件写 catalog 已接通。** 查询操作 frame 仍需将
  实际 resolver/普通 ACL、`prepare_plugin_sql`、真实 data barrier/session view、
  `CallerCatalogTransaction`、DDL 锁/epoch、ID/version reservation、writer 和失败
  poison/abort 接成一条生产调用链，再开放 SQL SPI/SDK CREATE/ALTER/DROP。
  真实客户端跨语句事务/并发、深层 planner/index/type、AI 资源与轻量化以及
  Bazel/跨平台验证继续属于完整目标，本检查点不取代这些要求。

### 前一检查点：session 配对视图归属、SQL 绑定与退休（局部验证通过）

- session 现在与 Rust journal 一起保留同一份 schema/privilege 配对视图。
  新增宿主 `prepare_plugin_catalog_view`：验证活动、可写、非反序列化事务，
  按真实 transaction ID/正 sequence base 惰性建立事务级视图，先记录实际数据
  barrier 对应的 Rust mark，再返回拥有的视图/日志。失败清空输出，不启动事务、
  不执行 SQL，也不授予 catalog 权限。既有显式 record 拒绝同事务替换为另一对视图。
- 新增 `bind_plugin_catalog_view`：核对当前事务和 Rust 未失败/未结束状态，
  对相同视图幂等，对 foreign overlay 拒绝。Preparing/Sealed 期间允许读冻结视图，
  普通新增 mark 仍由 Rust 拒绝。`ObSql::init_exec_context` 在计划上下文创建前
  接入绑定，覆盖使用该公共路径的 text/PS/PL 初始化，复用已有 provisional cache
  隔离；不复制 base guard 或跳过正常权限检查。
- 保存点回滚到首次使用之前仍保留 session 的空配对 owner；后续修改复用它。
  完成/reset/discard 先将配对视图单调退休，再清理 session 所有权。外部
  guard/host lease 可以保留 backing storage，但退休后 schema/ACL 查询、staging、
  新 savepoint、attach/capture/inherit 均拒绝；既有 mark 仍能撤销私有索引，
  不撤销退休状态。这避免未知结果下外部 journal 引用延迟析构时仍可读旧权限。
  MySQL 下一次取得 cached guard 时即使 base schema 版本不变也强制刷新退休视图；
  公共 SQL 初始化拒绝内部调用者传入的退休 guard，不静默回退到旧 schema。
  完成回调不立即 reset guard，避免释放结果清理仍借用的 schema。成员布局不受插件宏影响。
- 新增 kernel fixture 使用真实 session、transaction descriptor 的观察方法、
  Rust journal、schema guard 和实际 SQL result/context 初始化。descriptor 的
  活动/提交状态及授权输入受控，仅测试本地归属/绑定和清理，不声称实库提交。
  105 项 Rust 单测/严格 Clippy、23 项 CTest、源码边界、12 项 build gate 通过；
  上一版本的 const helper 签名错误已修复。本次完整生产重编译、最终链接和
  kernel 实际执行均通过；关闭插件宏的实际 session Unity 语法检查通过。
  新 fixture 使用 session 实际缓存 guard，验证事务 ID/base 换代、跨库/owner
  复用配对视图、公共 SQL 初始化绑定、保存点恢复、冻结视图可读但不可新增 mark、
  提交/reset/回滚/未知结果退休、外部 journal 延迟释放、旧指针寿命，以及退休后
  禁止新查询/staging/mark/绑定、旧 mark 清理不恢复访问。原 SQL/PL/Rust DSO
  回归保持通过。MySQL 实际跨语句刷新、实库提交与并发仍需端到端测试。
- 公开查询 CREATE/ALTER/DROP 仍未接入。普通对象授权、实际 writer/ID/version
  接线、真实 SQL 事务/保存点/并发、全部 PL/SPI 入口和缓存行为的端到端验证，
  深层 planner/index/type、AI 异步资源与轻量化指标及 Bazel/跨平台继续属于完整目标。

### 前一检查点：事务级多数据库/多 owner 权限视图

- `RoutinePrivilegeOverlay` 增加事务级构造方式，名称键显式包括 database ID；
  自动 EXECUTE/ALTER ROUTINE 权限属于每条记录的 routine owner，与正常
  `RoutineCatalogWriter` 的持久授权目标一致，不再要求整个视图仅对应一个创建者。
  现有安装/更新继续使用 `(database, principal)` 构造，保留原单库/单主体约束；
  两种模式共用索引、回滚和总预算，没有建立另一套权限实现。
- 查找同时检查传入当前 schema 的 database、ID、owner、版本、类别和名称；
  同 ID/name/owner/version 的跨数据库 schema 不能满足本库授权记录。无效 database
  参数清空输出并拒绝，显式 `(0, 0)` 仍是非法作用域，不解释为事务级模式。
  事务级记录不授予创建/definer/切库权力，也不从单纯 schema staging 推导授权；
  普通 resolver/ACL、实际写入和 host 授权记录仍是必要前提。
- 完整生产构建/最终链接、测试入口语法检查、105 项 Rust 单测/严格 Clippy、
  23 项 CTest、源码边界、12 项 build gate 和 diff 检查通过。完整 kernel 实际执行
  通过，新增 fixture 连接真实 guard/ACL、配对视图与 Rust journal，验证
  两库同名对象/不同 owner、函数与过程隔离、旧 user/role grants 遮蔽、跨库保存点
  回滚、借出 schema 寿命、非法作用域以及所有数据库共享 16384 条预算。
- **查询期 CREATE/ALTER/DROP 与 session 跨语句归属/绑定仍未接入**。本轮扩展的是
  可供该路径使用的权限视图，不代表真实 USE、definer 切换、鉴权、事务持久性或
  并发验证已通过。深层 planner/index/type、其它对象、AI 异步资源与轻量化指标、
  Bazel/跨平台继续保留在完整目标中。

### 前一检查点：真实缓存失败清理与跨目标布局修复

- `ObPlanCache::foreach_cache_evict` 在遍历失败时也取得已收集的节点列表，
  不执行批量删除，但逐一释放遍历期间取得的引用；部分删除失败后同样清理所有
  已收集引用，保留原错误。避免 Rust 后台队列重试时累积真实 PL 节点引用。
- 新增真实 map、PL 节点、collector、erase 与引用计数的 kernel fixture。第一次
  运行在 map create 返回 `OB_INIT_TWICE`，原因不是分配或测试初始化遗漏：
  `ObPlanCache` 的队列成员受 SQL target 的插件宏控制，而 main 等消费者未定义
  该宏，造成共享类布局不一致。修复为无条件 unique owner + 类外定义的 deleter；
  仅队列构造和操作受宏控制。未通过给测试补宏来掩盖跨目标布局错误。
- 关闭插件宏的实际 plan-cache translation unit 已编译成对象，`nm -uC` 检查
  不含 Rust runtime 或 RoutineInvalidationQueue 的未解析调用；kernel fixture
  使用原 main flags 的语法检查通过。105 项 Rust 单测/严格 Clippy、23 项 CTest、
  源码边界、12 项 build gate 和 diff 检查通过。
- 完整生产重编译、最终链接和修复后的 kernel 实际执行均通过。新增 fixture
  验证 16 轮在不同位置失败的遍历不删除节点、不积累引用；非空/空 map 的缺失
  collector 列表保留对应错误；注入 null map 值使实际删除路径在已删一个有效
  节点后失败，剩余所有遍历引用仍释放。真实 Rust queue 的首次消费在真实 map
  遍历中失败、请求保留，第二次调用正常 PL cache 按 database/routine 驱逐并
  确认；第三次消费不重复调用后端，空 map 重试保持幂等，最终仅剩 fixture 的
  外部引用。测试 main 不带 SQL 插件宏，直接检查启用插件的生产构造器所初始化
  的成员与 map，跨目标布局回归通过。原 SQL/PL/Rust DSO 等内核场景保持通过。
- 此证据覆盖真实节点引用计数和 map 删除，不是实库或完整缓存服务测试。schema
  readiness 与访问服务仍受控；外部引用阻止节点由 cache factory 最终析构。
  仍不覆盖实库事务/权限、旧 guard 并发、定时器调度/关闭、实际 PL bytecode
  的最终析构或跨平台完整构建。
- 完整目标继续包含公开查询 CREATE/ALTER/DROP、session 跨语句对象/权限视图、
  深层 planner/index/type 等协议、AI 异步资源和轻量化指标，未因本轮修复而完成。

### 前一检查点：提交前预留的 Rust 失效队列与后台接线

- 新增 Rust InvalidationQueue：一个 volatile plan cache 对应一个有界队列，支持
  并发生产者和单一串行消费者。Preparing 时复制纯标量请求快照并预留批次/请求
  容量，已知 commit 只将预留批次标记 ready，无新分配、SQL 或驱逐回调；已知
  abort 取消预留。session 私有 journal 销毁不再丢失已交接的请求，不向队列
  保留 session、视图、query context 或插件回调。
- session CommitHost 在准备 SQL 前通过现有 server_service<ObPlanCache> 预留；
  无队列、已关闭、容量或分配错误在数据提交前返回。每个 experimental plan cache
  默认允许 256 批/65536 请求槽，按实际 Vec capacity 整批计费；部分 ack 不提前
  归还仍占用的 backing allocation，最后请求释放才回收预算。
- 复用 ObPlanCacheEliminationTask，每轮最多处理 8 条，不新增线程。消费者在
  Rust 锁外检查 schema 版本门槛，再调用现有本地按 database/routine 驱逐路径，
  成功才 ack；版本不足仅请求异步刷新并保留。失败/异常保留原请求、轮换批次后
  下轮重试，不重试数据库提交。已知提交使用 end-sign 版本，避免在刷新前驱逐
  后又由旧 schema 重建缓存的明显窗口；旧 guard 并发仍需实库验证。
- 未解决的已预留 journal 被销毁时，队列保留冻结 schema-operation 版本并请求
  保守驱逐，不能推断 commit 或发布 provisional 对象。如果实际回滚且没有后续
  schema 推进，请求可能等待后续版本或 cache retirement。stop 仅关闭新预留，
  不撤销已接受承诺；destroy 先 join 定时器，在缓存不可访问后退休队列。Arc
  保持迟到 producer 的标量状态安全；这不是跨进程 durable outbox。
- 最终完整 seekdb 构建/链接、105 项 Rust 单测/严格 Clippy、23 项 CTest、源码/
  插件二进制边界、12 项 build gate、格式/diff 检查和完整内核回归通过。Rust
  新增预留/提交/abort/unknown、错误与重复 ack、轮换重试、关闭/退休后迟到提交、
  请求与批次预算/票据上限、32 并发生产者、部分确认不释放内存预算的验证；C ABI
  覆盖 queue 句柄与标量输出/版本门槛。内核新增 8 类宿主队列场景检查 journal
  销毁后交接、异常/超时保留、schema EAGAIN 不调用驱逐，以及后续成功重试；原
  writer DROP fixture 改为预留真实 Rust queue 后交接并消费，非初始化 plan cache
  预留拒绝也通过。首轮完整构建发现不存在的 GCTX.sql_engine_ 引用，改为已绑定
  的服务入口后重新完整构建通过，未添加新的全局 SQL engine 字段。
- **完整目标仍未完成**：公开查询 CREATE/ALTER/DROP、session 跨语句对象/权限
  视图和真实数据库事务/权限/并发/关闭验证继续推进。本轮驱逐效果为受控 fixture，
  尚不证明真实 PL cache 失败时引用清理、旧 schema 并发、定时器调度/恢复、所有
  内部入口的可见性 fence 或实库 durable outcome。深层 planner/index/type、其它
  对象、AI 异步资源与轻量化指标及 Bazel/跨平台仍属完整目标。

### 前一检查点：Rust routine 缓存失效日志与宿主接收端

- Rust query journal 增加 routine 失效记录，与 view/schema operation 共用 barrier
  和 16384 条预算；要求已有 DDL 准入且同一 barrier 成功记录 schema 写入。
  保存点回滚撤销请求并恢复 schema sequence，不回退高水位或 ticket；重复对象
  使用独立票据，溢出/容量错误不改变已有记录。日志本身不执行缓存失效或 SQL。
- known-commit finish 原地保留请求，继续按原逆序释放 view marks。新增宿主 C ABI
  和 RoutineCatalogTransaction 包装：count、提交后 FIFO peek、精确 ticket ack；
  重复 peek 支持接收失败后重试，错误/重复 ack 不消费后续对象。abort/unknown
  不授予投递权限。宿主必须保留未确认 owner；析构仍会丢弃私有请求，不是 durable
  outbox，也不提供崩溃恢复或后台成功保证。
- CallerCatalogTransaction 提供仅查询模式、已准入且上下文有效时可借用的 sink，
  每次调用重新检查原 session/实际事务/数据库/线程/barrier。commit transport
  不提供该接收端，close 结束借用。IRoutineCacheInvalidation 抽到独立头文件，
  避免把有环境性包含依赖的旧 PL operator 头传入查询连接实现；两套清单已登记。
- 最终完整 seekdb 构建/链接、99 项 Rust 单测与严格 Clippy、23 项 CTest、源码/
  插件二进制边界、12 项 build gate、格式/diff 检查和完整内核回归通过。C ABI
  覆盖输出清零、身份/阶段/参数、同 barrier、回滚与重登记、共享容量、已知提交
  FIFO/重复对象、错误/重复 ack、失败准备/abort/unknown；Rust 增补 ABI layout
  和 ticket 耗尽。原 19 类 writer 场景接入实际 journal，成功 DROP 分别验证回滚
  撤销和已知提交后读取/确认；宿主包装及无效 transport 的空 sink 也通过。
  首轮生产构建暴露旧 PL 头的独立包含错误，拆出上述小接口后重新完整构建通过。
- **尚未接通提交后 owner 移交与后台失效**：session completion 仍会 discard
  journal，公开查询 DROP 保持未接入。不能在此状态开放真实失效记录的用户入口，
  也不能用数据提交回调内全局 SQL flush 替代后台队列。接下来需完成无丢失接收、
  失败重试、关闭/恢复语义，再接公开查询写入和跨语句对象/权限视图。
  本轮 SQL/数据结果仍为受控 fixture，不证明实库权限、持久提交/回滚、异步并发
  或其它 session 可见性。深层 planner/index/type、其它对象、AI 异步资源/轻量化
  指标及 Bazel/跨平台仍是完整目标，不因本轮局部验证而视作完成。

### 前一检查点：routine 完整写入效果与事务所有权分离

- 新增宿主 RoutineCatalogWriter，接受已活动的 ObMySQLTransaction，而不是要求
  Root 专用事务。将正常 CREATE/replace、错误状态 ALTER、DROP 的 schema、依赖、
  编译错误、自动授权及 reserved identity 旧名称权限清理保留在一条共享路径；原
  Root 方法复用它并继续拥有自己的 start/end/publication。writer 一次性使用，
  不启动、结束或提交传入事务；权限身份/锁/当前 guard/reservation 仍由调用者准入。
- MySQL 属性 ALTER 仍通过 replacement 路径并保留原依赖；独立 alter 方法仅是
  原重新编译/错误状态分支，不能把它当成全部 ALTER 语义。外部事务模式的 ACL
  查询使用同一事务，不把 provisional/final overlay 当成已写入的权限记录。
- 发现 DROP 的原 operator 通过全局连接执行 PL cache flush。新增宿主
  IRoutineCacheInvalidation 交接点，writer 的 DROP 必须显式提供；Root 保留原
  flush 行为，调用者事务需要记录失效请求、已知 commit 后分发。当前尚未把该
  失效请求写入 Rust journal 或开放查询 DROP，不能宣称延期失效协议已经完成。
- 最终生产完整构建/链接、97 项 Rust 单测/严格 Clippy、23 项 CTest、源码/二进制
  边界及 12 项 build gate 通过。完整内核回归通过，新增 19 类 writer 场景覆盖
  CREATE/replace/错误状态 ALTER/DROP、两种 ACL 读取模式、自动授权开启/关闭、
  非活动事务/缺失 guard/backend、reservation 使用约束、SQL/ACL/失效接收错误、
  一次性使用、Rust 版本记录，以及不调用 start/end/watermark。两类非空旧 ACL
  场景验证 automatic grants 关闭时 reserved CREATE/DROP 仍清理 EXECUTE/ALTER/
  GRANT，删除 SQL 使用实际存储的大写名称，不误用调用者的小写名称。
  首轮新增 fixture 先后暴露缺少 runtime schema、重复添加默认 sysvar、缺少
  subprogram_id 的测试构造问题；补齐正常前置数据后保持原预期，17 场景通过，
  再补旧 ACL 后最终 19 场景通过。数据库行、版本分配及失效 sink 仍是受控效果，
  不证明实库授权、持久提交/回滚、跨 session 可见性或提交后缓存失效已完成。
- 查询期公开 CREATE/ALTER/DROP、session 跨语句 overlay/权限视图绑定、对象
  保存点与失败原子性、提交后对象缓存失效和真实数据库并发验证仍需继续接入。
  深层 planner/index/type、其它对象、AI 异步资源/轻量化指标、Bazel/跨平台继续
  保持在完整目标内，不以此次宿主层抽取替代这些能力。

### 前一检查点：session 提交准备与刷新交接

- session 不再无条件拒绝 surviving schema operation：真实事务身份检查后，
  由 Rust 日志提供原准入 epoch 和 surviving version，执行 freeze、提交借用连接、
  captured epoch 校验、end-sign/MDS/watermark、close、再次身份检查与 seal。
  宿主 ICatalogCommitHost 仅隔离数据库效果，Rust 状态/日志仍是唯一私有参与者；
  不添加第二套持久 catalog 或新的事务所有者，不跨 SQL 持有 Rust 可变借用。
- nested SQL 期间保留 journal shared owner，归属与生命期分别检查。所有准备
  错误先恢复连接再封存失败，保留首错。原事务控制路径只回滚仍匹配原 ID/base 的
  descriptor，避免空指针或误回滚替代事务。原同步/异步数据提交权力保持不变。
- 已知 commit 后读取最终版本、完成日志并单调推进 session Read-After-DDL fence；
  新 request_schema_refresh 仅投递原后台任务，不在数据回调中执行 SQL/等待刷新。
  队列失败保留 fence，不谎报数据库回滚；未知结果不猜测发布版本。其它 session
  立即可见、所有内部 SPI/PL 入口的 fence 和缓存失效尚未经过实库证明。
- 生产完整构建/链接、Rust 97 项单测/严格 Clippy、23 项 CTest、源码/插件二进制
  边界、12 项 build gate 和完整内核回归通过。新增 14 类宿主/Rust 协调场景检查
  准备与 close/最终身份校验顺序、异常、首错、缺失/错误 end-sign、view-only、
  无准入和禁止重试；session fence 单调递增且 data tx reset 后保留。刷新入口
  参数/未初始化拒绝也通过。原 DDL 准入/准备、Rust DSO、SQL/PL、builder、查询
  catalog 与表函数回归保持通过。SQL/MDS/身份变化仍为受控 fixture，不代表已验证
  实库持久提交、队列拥塞/后台刷新成功、异步线程并发或其他 session 可见性。
- 查询 CREATE/ALTER/DROP 的实际入口、真实权限/锁/事务/并发验证、深层 planner/
  index/type、更多对象、AI 异步资源/轻量化指标及 Bazel/跨平台仍属完整目标。
  本检查点不声称已实现用户查询中的可回滚 catalog DDL。

### 前一检查点：事务归属的 DDL 准入

- 新增宿主 CatalogDDLAdmission：一次性捕获已有 epoch、在借用事务取得 DDL 串行
  锁、锁后校验 guard version 与最新已提交 schema version。错误输出 epoch 为零，
  过期 schema 返回 EAGAIN；不提升 epoch、不启动/结束事务、不锁 watermark。新旧
  Root/caller 路径复用同一个 lock_transaction，保留 Root parallel 的 SHARE 模式，
  caller 使用 EXCLUSIVE 基线，避免另造不与 Root 互斥的锁协议。
- 元数据 freshness 使用宿主独立 current-read client，不能用用户 RR 旧 snapshot。
  这是只读检查，不是第二个 catalog 写事务。该 API 的 host-client 契约仍需实际
  调用者保证；并发等待/长用户事务持锁成本也尚未经过实库测量。
- Rust query journal 增加固定 ddl_epoch/ddl_sequence 槽，必须在任何 schema 写入前
  登记，不允许提交时补录；准入共享原序号高水位，回滚到/早于 barrier 时撤销，
  更晚的回滚保留，重新准入不能使用旧序号。check_ddl_write 检查 Open/首错/准入/
  写入序号；begin_prepare 拒绝有 surviving schema operation 却没有准入的日志。
- CallerCatalogTransaction 增加 admit_ddl，核对 session/journal 归属，首次调用
  上述宿主步骤后记录 Rust 准入，新连接复用尚有效的旧 epoch，不在提交阶段重新
  捕获覆盖。准入前拒绝 catalog DML/DDL logger，之后每次操作检查状态。SQL/data
  barrier 和对象 ACL 仍由真正调用者负责，内存准入记录不能代替实际数据库锁。
- 生产完整构建/链接、97 项 Rust 单测及严格 Clippy、23 项 CTest、源码/插件二进制
  边界、12 项 build gate 和 diff 检查通过。C ABI 新增准入身份/参数/重复/高水位/
  保存点撤销与保留/重新准入/禁止事后授权验证。完整内核回归通过，新增 12 类
  宿主准入步骤场景验证捕获/锁/新版本读取的顺序、失败短路、旧版本拒绝、身份
  变化、外层超时、异常与 TLS 恢复、单次尝试；底层锁接口检查超时/缺少连接时
  不执行 SQL。适用的提交准备 fixture 在 schema 写入前记录准入，原 Rust DSO、
  SQL/PL、builder、查询 catalog 和表函数回归同时通过。锁/SQL 效果仍为受控
  fixture，不证明真实锁冲突、持久写入/回滚和发布。
- session 提交协调器尚未接通这套协议，临时提交拒绝保留。仍需对捕获 epoch 的
  提交前校验、close/seal/真实数据结果及同步/异步 schema publication 接合；不能
  在异步数据回调里直接同步 refresh 并假定线程与锁语义正确。查询 CREATE/ALTER/
  DROP、真实事务/权限/并发验证、深层 planner/index/type、更多对象、AI 异步资源/
  轻量化指标、Bazel/跨平台仍属完整目标。详见 [DDL 准入](plugin-catalog-savepoints.md)。

### 前一检查点：提交阶段借用连接

- `CallerCatalogTransaction::open_for_commit` 接收真实 session、session 持有的
  Rust journal、预期 transaction ID/sequence base 和绝对 deadline。允许空执行
  帧或无 physical plan，但不允许伪造/独立日志代替同一 session 的 Preparing
  参与者。查询期 open 的物理计划要求保持不变，两者共用不拥有事务的 SQL adapter。
- 每次读写/连接/DDL recorder 使用前检查线程、原执行帧、实际活动可写事务、数据库/
  权限主体及 journal 归属，先比较 session ownership 再解引用日志，防止 reset 后
  访问旧 owner。新增 Rust check_preparing 只读接口，允许 end-sign 后继续数据库
  准备，但拒绝 Open/Sealed/Failed/finished，保留首错；提交模式记录固定 end-sign。
- 提交 SQL 的 TLS timeout frame 不延长外层 deadline；检查 worker 超时及 session
  取消，内部 SQL 继续使用原 timeout guard。没有 statement bookkeeping 时临时
  建立嵌套保存前置状态，close 按顺序恢复/释放并退出 timeout frame，不启动或结束
  数据事务。必须先 close，再 complete_prepare；恢复错误不覆盖原 SQL/状态错误。
- 新增数据层 `tx_desc_is_active`，复用私有实现原活动状态判断，不把已结束/中止/
  正在提交的 descriptor 因 is_in_tx 范围较宽而误当作活动事务。session 视图/版本
  记录、准备提交及两种 transport 均使用该检查，数据结果完成通知仍走原协议。
- 完整生产构建/链接、97 项 Rust 单测及严格 Clippy、最终 23 项 CTest、源码/插件
  二进制边界、12 项 build gate 和 diff 检查通过。首次 CTest 的 cargo-seekdb
  链接器 SIGSEGV，单独重跑通过；随后整套重跑的脚手架在 rust-lld 内 abort，停止
  生产编译后的最终整套重跑通过。未改变工具链或测试期望，崩溃根因未确认。
  完整 kernel 回归通过：新增 5 类无活动事务/非法参数/无 physical plan 场景，
  检查不创建事务、单次打开、session 嵌套与 TLS/worker timeout 保持原值；提交准备
  fixture 的 recorder 改用真实 Rust Preparing 检查。原 Rust DSO、SQL/PL、builder、
  查询 catalog 和表函数回归同时通过。
- 该入口尚未由 session 提交协调器调用。成功连接/真实 catalog 写入与回滚、提交
  超时/取消和错误恢复仍需实库证据；拒绝路径与独立状态机测试不替代这些验证。
  下一步必须完成事务归属的 DDL 锁/epoch、准备/close/seal/数据结果/publication
  接合，才能解除临时提交拒绝并开放查询期 CREATE/ALTER/DROP。深层 planner/index/
  type、其它 SQL 对象、AI 异步资源与轻量化指标、Bazel/跨平台仍属完整目标。详见
  [提交阶段 transport](plugin-query-catalog.md)。

### 前一检查点：Rust catalog 提交准备状态

- Rust query journal 增加 Open/Preparing/Sealed/Failed 状态。begin_prepare 在宿主
  执行数据库准备前冻结普通视图/schema 修改与保存点回滚，返回 surviving version/
  operation count；Rust 调用返回后再执行宿主 SQL，不跨回调持有 Rust 可变借用。
- 宿主 C ABI 和 RoutineCatalogTransaction 增加 begin_prepare、record_end_sign、
  complete_prepare。end-sign 只能成功记录一次且严格高于原最大值，使用无分配固定
  槽；普通日志已满也能记录最终版本，snapshot 计数包含这额外一条。Preparing 后
  只能完整 abort/discard，不为最终步骤伪造可局部回滚的保存点。
- complete 为一次性结果交接，须在宿主 SQL/MDS/watermark/transport 恢复全部结束
  后调用；成功校验最终版本再 seal，失败或版本矛盾永久拒绝提交。失败重试、查询
  提交输入和 abort 均保留原始宿主错误，undo 错误不能覆盖。旧 prepare_commit 只
  接受无 surviving schema operation 的视图事务，不再允许跳过完整准备协议。
- 最终生产构建/链接、97 项 Rust 单测及严格 Clippy、23 项 CTest、源码/插件二进制
  边界、12 项 build gate 和 diff 检查通过。C ABI 新增 9 类准备场景及满日志最终
  记录，覆盖冻结、身份/参数、重复/缺失/错误 end-sign、失败重试、首错、准备中
  abort/未知结果清理。完整 kernel 回归通过：原 15 类提交准备场景中适用 caller
  协议的路径已接入新状态机，验证最终版本、失败后禁止提交和原始错误；其它
  Root/no-end-sign/bootstrap 路径保持独立验证。原 Rust DSO、SQL/PL、builder、
  查询 catalog 和表函数回归同时通过。SQL/MDS 和多数 watermark 效果仍为受控
  fixture，不能作为实库提交、持久回滚、锁/epoch 或 publication 的证据。
- 这不是 session 提交阶段已完成：提交专用 session/connection、事务归属的 DDL
  锁/epoch 和提交结果/publication 尚未接通，临时提交拒绝仍保留。真实 catalog
  CREATE/ALTER/DROP、深层 planner/index/type、其它 SQL 对象、AI 异步资源和轻量化
  指标、实库及 Bazel/跨平台验证继续属于完整目标。详见
  [Rust 提交准备协议](plugin-catalog-savepoints.md)。

### 前一检查点：可复用的 catalog 提交前步骤

- 新增宿主 `CatalogCommitPreparation`，实现放在原 Root 生产 DDL 单元中，明确
  接收已有事务、surviving version、变更标记和 end-sign 策略。复用原版本分配器/
  DDL SQL logger、同一连接的 DDL_TRANS 登记和 ObGlobalStatProxy watermark，
  不自行读取 TSILastOper，不调用 start/end/commit/rollback 或发布 schema。
- Root `ObDDLSQLTransaction::end` 已接入该组件；epoch 检查仍在外层，准备成功
  才提交、失败回滚并保留首错。并行 DDL 顺序屏障及对象锁仍由原调用者负责。
  原独立 register_ddl_trans_signal 也委托相同底层方法，不另开连接。
- prepare 为一次性尝试，失败/无变更输出版本为零；新 end-sign 版本必须高于已
  写入最大值，异常转数据库错误；无 schema 变更跳过 end-sign/watermark，保留
  原事务信号行为。不要求旧 bootstrap 的零版本输入已是正数，但最终 end-sign
  必须为更高正版本。它不是提交确认，也不能用重新构造组件重试半完成的事务。
- 完整生产构建/链接和完整 kernel 回归通过。新增 15 类场景验证成功、无变更、
  不写 end-sign、无活动事务、非法/过期版本、分配错误/异常、SQL/记录/MDS/
  watermark 失败、旧零版本输入、缺失 schema service、一次性尝试和输出清空。
  还通过真实 ObGlobalStatProxy 路径由同一借用 client 读取系统表 FOR UPDATE，
  注入超时并验证传回准备阶段；Rust 日志中的 end-sign 版本可按 barrier 撤销。
- 最终 23 项 CTest、97 项 Rust runtime 单测及严格 Clippy、12 项 build gate、
  源码/插件二进制边界及 diff 检查通过。
  首次 CTest 的脚手架链接器发生 SIGSEGV；该项单独重跑及整套重跑通过，未修改
  测试期望或将其认定为业务逻辑缺陷。既有 Rust DSO、SQL/PL、builder、查询 catalog
  和表函数回归同时通过。
- SQL transport、MDS 和多数 watermark 效果仍由 fixture 控制；真实 watermark
  用例只验证读取失败，不证明实际系统表推进、持久回滚、epoch/并发锁或发布。
  session 提交阶段尚未调用此组件，临时提交拒绝保留；下一步需要提交阶段借用
  session/connection、DDL 锁/epoch 归属、prepare 后 seal 和提交结果/publication
  接入，再开放查询期 CREATE/ALTER/DROP。深层 planner/index/type、更广 SQL 对象、
  AI 异步资源与轻量化指标、实库及 Bazel/跨平台验证继续属于完整目标。详见
  [提交前数据库步骤](plugin-catalog-savepoints.md)。

### 前一检查点：Rust 事务归属的 schema version

- Rust query journal 增加 schema operation 项，与私有视图 mark 共用同一 barrier
  顺序、回滚栈和 16384 条预算。只接收已成功写入的正版本；维护 surviving 最大
  schema version 和 operation 数，支持预留版本乱序写入，rollback 恢复前值。
  snapshot 检查身份/首错，seal 后可读、finish 后拒绝；不推进全局 watermark。
- `ObDDLSqlService::log_operation` 按 client 的 `ICatalogOperationRecorder`
  能力区分路径。借用事务不再通过 SQL 生成器污染线程局部 TSILastOper；先检查
  recorder/正版本，写成功且 affected rows 为 1 才记录，SQL 首错不被回调覆盖。
  无 recorder 的借用 client 拒绝写入，不回退 TSI；原 Root/client 路径保持不变。
  直接使用 log_operation_dml 的批量生成路径尚未接入该协议。
- Caller transport 新增显式 data barrier 重载，session 必须已记录配对视图；
  成功后版本进入同一 Rust 参与者，而不是保存在短命连接或线程中。session 再次
  核对活动事务、ID/sequence base；不得并行/交错复用同一对象操作 barrier。
- DDL end-sign、MDS、watermark 和 publication 尚未闭环，因此 session prepare
  暂时拒绝提交 surviving schema operation，并沿原结束路径回滚。这个保护必须
  被完整提交前协议替换，不算查询期 CREATE/ALTER/DROP 实现，不改变完整目标。
- 完整生产构建/链接、完整 kernel 回归、97 项 Rust 单测及严格 Clippy、23 项
  CTest、源码/插件二进制边界、12 项 kernel build gate 和 diff 检查通过。C ABI
  新增混合 mark/版本、乱序最大值、保存点恢复、身份/参数/高水位、seal/finish、
  共享预算和 undo poison 后禁止提供提交输入的测试。
- 内核新增 7 类 logger 场景：缺少 recorder、拒绝上下文、SQL 失败、错误 affected
  rows、记录分配失败、成功及零版本。失败路径检查首错和未记录版本，成功路径
  检查最大版本/数量/回滚；独占顺序跨线程交接使用新借用 adapter，两个线程的
  TSI 均不变。原非借用路径仍更新 TSI。既有 Rust DSO、SQL/PL、安装 builder、
  表函数、查询 catalog 和 session 保存失败回归同时通过。
- 本轮测试使用真实 SQL 生成器、借用 adapter、Rust 日志与生产链接对象，SQL
  transport 仍是受控替身；不能由此证明实库写入/回滚/权限、异步 commit 交接。
  查询期 mutator、对象锁、提交发布、深层 planner/index/type、其它 SQL 对象、AI
  资源调度、轻量化指标及 Bazel/跨平台验证继续保留。详见
  [事务版本记录](plugin-catalog-savepoints.md) 与 [查询 catalog](plugin-query-catalog.md)。

### 前一检查点：调用者事务 SQL transport

- 增加宿主 `BorrowedSQLTransaction`：向既有 routine writer/version reservation
  提供同一个 `ObMySQLTransaction` 兼容对象，但不设置基类事务所有权。拒绝
  start/end/acquire，不因析构提交；执行前后检查真实调用者状态，保留首错并清空
  失败输出。底层 SQL 错误优先于后续上下文失效，C++ 异常转换为数据库错误。
- 增加 `CallerCatalogTransaction`，借用当前 session 的内部 SQL 连接，绑定实际
  transaction ID/sequence base、执行帧、线程、数据库和权限主体；不启动新事务。
  该连接仅供已由宿主完成对象权限/锁/保存点准备的生成 catalog DML，不是公开插件
  SQL 通道。真实 parser 预检拒绝多语句、DDL、事务控制、CALL/SET；关闭恢复嵌套
  session，结果集和预留 token 不得越过其生命周期。
- 嵌套保存先复制 tx result、query/table 快照并预留合并容量，再清空基本语句状态。
  继续修正 SQL session 层的数据库名复制：先 reserve，后续 assign 复用容量；独立
  save_sql_session 也在修改 audit/inner flag 前完成可能失败的复制。没有把这项
  保证扩大到全部 autonomous transaction 保存/恢复路径。
- SQL runtime 清单新增一个实际源文件：4 个 runtime sources，workspace 总归属
  1175；同步生成校验和测试期望，保留漏项/重复归属检查，不放宽依赖边界。
- 完整生产构建/链接及完整 kernel 回归通过，真实 Rust DSO、SQL/PL、builder、
  表函数和原查询 catalog 用例同时通过。新增测试验证借用委托/禁止事务控制/
  首错/异常，真实 SQL parser，以及缺少执行帧/活动事务拒绝。session 用例覆盖
  缺少 statement 前置条件、query/autocommit/数据库/inner flag 恢复；两条快照
  allocator 故障证明失败后原 query、plan、总表列表、当前表列表和审计状态未丢失。
- 97 项 Rust runtime 单测、严格 Clippy、23 项 CTest、8 项 SQL profile、12 项
  source inventory、12 项 kernel build gate、源码/插件二进制边界及 diff 检查通过。
  首次 kernel 失败于新用例未进入 statement 状态；补齐前置条件后，故障 allocator
  又缺少一个纯虚 alloc 重载导致测试编译失败；补齐后完整重跑通过，没有跳过断言。
- 当前 transport 尚未被查询期对象 mutator 调用，不构成真实活动事务写入验证。
  下一步仍需 normal routine writer、视图、预留 ID/version、依赖/权限写入、DDL
  epoch/end-sign、DDL_TRANS 信号、normal schema watermark 和 schema publication
  的同一调用者事务闭环。现有 TSILastOper 是线程局部状态，不能直接充当跨语句/
  线程交接事务的 schema 操作集合。查询期 CREATE/ALTER/DROP、实库权限/回滚、
  深层 planner/index/type、更广 SQL 对象、AI 异步资源、轻量化指标及 Bazel/跨平台
  验证仍属完整目标。详见 [查询期 catalog 与事务 transport](plugin-query-catalog.md)。

### 前一检查点：session 视图参与者

- Rust 日志新增提交前 seal：检查身份/首错，关闭后续 mutation/rollback，未 seal
  不接受 commit 通知，重复 prepare 拒绝。C ABI 与配对视图用例增加对应检查。
- `RoutineCatalogTransaction` 改为生产 share `.cpp`，头文件只保留声明，SQL/session
  不直接依赖 Rust 内部桥接头文件。CMake 只在插件开启时编译该实现；kernel runner
  不再追加 host include，重新完全复用生产 main 编译参数。
- session 惰性拥有日志，绑定真实 transaction ID/sequence base；无活动事务及
  反序列化 session 拒绝挂接。layout 成员不随 SQL 私有编译宏改变，避免跨 target
  布局不一致。reset/reset_tx_variable、事务换代清理私有视图。
- 显式具名、语句隐式及指定 barrier 回滚在数据成功后通知视图；视图失败尝试 abort
  同一事务，失败则保留 poison 阻止 commit。同步结束封存并按原始 data result
  完成/丢弃；异步路径独立传递数据结果、事务 ID 和响应错误，在现有 query-lock
  及交接完成屏障内处理，再重置描述符。失败交接的无响应清理路径同样处理日志。
- 97 项 Rust runtime 单测、严格 Clippy、完整生产编译/链接、23 项 CTest、完整
  kernel 回归、源码/插件二进制边界、12 项 build gate 与 diff 检查最终通过。
  kernel 实际链接生产 share adapter，验证 seal/拒绝未 seal commit、原视图回滚与
  session 惰性 baseline/无事务拒绝；原 Rust DSO、SQL/PL、builder 和表函数回归通过。
  这些不是真实 data transaction 或异步 commit 交接验证。
- 首次 23 项 CTest 中 22 项通过，脚手架生成插件的链接器进程发生 SIGSEGV；单独
  重跑及整套重跑均通过，未将其归因于本轮逻辑。关闭插件宏的定向检查起初抽出单个
  session 源文件，因原有 Unity 提供的 ObPieceCache 定义缺失而失败；改为原生产
  session Unity 单元、仅移除插件宏后语法检查通过。不是完整插件关闭构建的证明。
- 查询期 catalog CREATE/ALTER/DROP 仍未开放：预留 ID/version、系统表写入、当前
  guard 可见性和 schema 发布仍需真正加入调用者事务。原 Root 独立事务不能冒充
  caller transaction。真实事务/权限/异步交接、Bazel 与跨平台仍待验证；深层 planner/
  index/type、更广 SQL 对象、AI 资源/调度及轻量化指标继续保留为完整目标。

### 前一检查点：Rust 事务视图日志

- 新增 Rust `query_transaction.rs`：按调用者 transaction ID 与数据 barrier 管理
  视图 undo 所有权，倒序撤销 `sequence >= target`；保存点名称和嵌套/stash 规则仍由
  数据事务层解释。序号高水位不回退，同一 barrier 可有多条变更；最多同时持有
  16384 条标记，分配/校验失败不转移 payload，undo/release 均恰好消费一次。
- undo 原始宿主首错保持 sticky，继续清理但拒绝新记录和 commit；已确认 abort
  清理剩余记录。已确认 commit 仅释放视图标记，不提交数据或发布 schema。未知结果
  必须停止查询访问并销毁私有视图，不能把析构当成持久数据库回滚。
- 新增宿主 `RoutineCatalogTransaction`，以真实 `ObTxSEQ::get_seq()` 接入现有
  schema/privilege 成对保存点。当前只接受串行 root branch；这不是对并行分支的
  完整支持。该组件尚未挂入 session/异步提交生命周期，不构成查询期写入 API。
- 数据事务增加返回 resolved barrier 的具名 rollback 重载，选择、回滚与结果读取
  共用原事务锁。原入口委托到新实现；其他实现默认拒绝且不改变数据，后续协调者
  不能 fallback 或独立按名称推断目标。
- 97 项既有 Rust runtime 单测、严格 Clippy、23 项 CTest、源码边界、12 项 build gate
  和 diff 检查通过。新增 C ABI 测试覆盖倒序/同 barrier、重复 rollback、旧事务拒绝、
  packed 序号拒绝、无所有权转移失败、首错/poison/最终清理和 16384 条容量。
  完整生产编译/链接及新配对视图 kernel 回归通过：跨事务拒绝、同 barrier 多次变更、
  重复 rollback、早于首次插件使用的保存点、同名替换后权限/旧指针恢复、commit/
  abort/未知结果的私有视图清理，以及外来回滚破坏 ancestry 后的 poison 路径。
  原 Rust DSO、SQL/PL、安装 builder、查询 catalog、表函数回归同时通过。
- kernel 首次在编译测试入口时缺少 `plugin_runtime.h`：生产 main 不消费 Rust host
  的 include 接口。runner 现在先核对生产 registry 对象确有该 include 依赖，再
  给测试入口追加同一路径；不替换生产布局/宏或链接对象。完整重跑和 12 项 gate
  再次通过。测试中的数据 barrier 是构造输入，未验证真实用户事务的具名回滚、
  认证、提交或隔离；不能由这些结果推断完整 catalog durable rollback 已实现。
- 下一步必须接入 session 的唯一事务所有权、隐式/显式 rollback、同步/异步提交与
  reset/disconnect，再将预留身份、catalog 系统表写入及 schema 发布并入调用者事务。
  现有外部 session SPI 连接可复用，但 Root 的独立 `ObMySQLTransaction` 及绑定它的
  routine version reservation 不能直接当作调用者事务。查询期 CREATE/ALTER/DROP、
  深层 planner/index/type、其它 SQL 对象、实库权限/回滚、AI 异步资源和轻量化指标
  均仍属完整目标。详见 [保存点与事务日志](plugin-catalog-savepoints.md)。

### 前一检查点：routine/权限视图保存点

- 增加宿主 `RoutineCatalogSavepoint`，同时保有配对 schema/privilege overlay 的
  所有权并记录分支位置。构造/release 不复制映射；rollback 先校验两份分支祖先，
  再通过 undo 索引无分配恢复。旧分支标记不可向前恢复，标记一次性消费；release
  只放弃该标记，外层仍可回滚。不是数据事务、SQL SAVEPOINT 或 catalog 写入授权。
- schema 记录增加前驱及旧 name/ID 索引位置；权限记录增加独立 undo ancestry。
  回滚可恢复 base guard 回退，或先前的 live/tombstone/权限。所有旧 schema 和
  backing strings 保持存活，原 schema 字节和记录预算不因回滚减少；权限历史新增
  16384 条上限，不能通过不停回滚绕过容量限制。持久 ID/version 仍由协调者预留，
  view rollback 不允许重新分配已经使用过的持久身份。
- Root 安装的 schema/权限 staging 与 node 入队被保存点覆盖；更新也在 admission/
  staging 周围接入。中途错误/异常恢复成对视图，原命令队列、reservation 和实际
  持久事务仍按整体失败处理，不允许仅恢复视图就继续旧命令序列。
- 新增内核回归覆盖 base-only tombstone、嵌套和内层 release、重复消费、废弃分支、
  权限单独变化、错误配对、半份 staging 失败、ALTER 旧指针、累计 schema 字节和
  16384 次权限创建/回滚。真实 guard 权限用例证明新同名对象不能继承旧 grant，
  rollback 恢复原身份/指针和 EXECUTE/ALTER 权限。
- 完整生产编译/链接、22 项 CTest、独立 owned-overlay runner、完整 kernel runner、
  源码/插件二进制边界、12 项 build gate 与 diff 检查通过。原 Rust DSO、查询 catalog、
  安装 builder、SQL/PL 和表函数回归同时通过。Rust ABI/模块身份仍为 v8，不变更 GIS。
- 本轮曾误判原 schema 字节未累计；重新读完整 stage 后确认计数已有，已保留原逻辑。
  新增的是回滚情况下的容量回归，不是宣称修复原计数缺陷。
- 查询期 CREATE/ALTER/DROP 仍需把视图保存点、数据保存点、预留身份、系统表写入和
  schema 发布纳入调用者事务，并处理最终 commit/rollback/未知结果。当前受控 guard
  和 schema 回归不证明实库 durable rollback 或完整 Root 失败事务。深层 planner/
  index/类型、更广 SQL 对象、异步资源、实库权限及轻量化指标继续属于完整目标。
  详见 [catalog 视图保存点](plugin-catalog-savepoints.md)。

### 前一检查点：查询期 routine catalog 查找

- SQL API minor 2 / v3 追加 standalone routine lookup，供已有标量 SQL 上下文和
  table context v3/v4 使用。接受当前数据库中 FUNCTION/PROCEDURE 的 UTF-8 名称，
  按调用者已有 schema guard 与正常 SHOW 可见性返回身份快照；不存在与无权限区分。
  不重新获取最新 guard、不启动数据事务、不创建对象/依赖或授予 EXECUTE。
- 新增共用 `catalog_routine_lookup.h`，安装 builder 保留安装 owner 检查后复用该
  名称/权限/视图路径。query host 验证线程、当前执行上下文、session、guard 和
  物理计划，检查取消/超时；lookup、SQL execute、poll 共用首错，重入失败不被外层
  成功覆盖。结果 ID 不含 schema lease 或版本固定保证，不能替代正常 resolver。
- Rust `Call`、table `QueryContext`/`Rows` 新增 `supports_catalog_lookup` 和
  `lookup_routine(RoutineKind, name)`；保留旧 API 短布局，验证版本、保留字段、名称
  与结果身份。表函数忽略查找错误后仍不能 emit 成功，open 失败不发布 cursor。
- Rust text 增加非 immutable 的 `seekdb_rust_routine_id(bytes)`，返回当前库
  FUNCTION 的 ID 或 NULL；权限/宿主错误失败。当前为 14 服务、17 扩展对象，
  build ID 为 `rust-text-query-catalog-v8`。GIS 仍使用原 C/C++。
- 完整生产编译/链接、22 项 CTest、SDK/插件 Clippy、SDK 新增 3 项 query catalog
  测试、C/Rust ABI 与原 16 项文档测试通过。源码/插件二进制边界及 12 项 build gate
  通过。完整 kernel runner 最终编译/链接/运行通过：14 个 query catalog 宿主场景，
  其中 7 个实际 Rust DSO 调用；覆盖 base/overlay 替换/删除、大小写、函数/过程、
  不存在、权限、非法输入、超时、错误上下文、跨线程、重入与跨操作首错。
  调用后无插件 lease、新数据事务或 plugin SQL savepoint，原安装 builder 及
  SQL/PL/表函数回归同时通过。
- 首次 Rust 示例编译发现宏作用域/回调签名不匹配，SDK 测试也修正了变量遮蔽及
  Clippy 警告。首次 kernel 在 fixture 的 add_user 前失败：内存 schema manager
  缺少 server runtime schema；补齐初始化、加强错误码断言后完整重跑通过。
- **查询期 catalog 创建/修改/删除仍未实现**。Root 安装事务不属于查询调用者事务，
  不能直接复用或独立提交。仍需调用者事务/保存点参与、schema/privilege overlay
  可见性、系统表写入与提交发布/回滚协作。深层 planner/index/类型、其他 SQL 对象、
  异步资源、轻量化指标和实库事务/权限验证继续保留；本轮受控 schema fixture 不
  等同于真实服务器的认证/隔离/回滚验证。详见 [查询期 catalog](plugin-query-catalog.md)。

### 前一检查点：插件可见的表函数投影

- table SPI minor 4 / context v4 保留 v3 前缀，新增完整声明列数和借用的 0/1
  requested-columns 数组。SQL fetch 按完整 codegen 列槽每批生成一次，open/next
  都可读取；WHERE 专用列保留。缺少元数据按全部列处理，存在的全零数组仅消费行数。
- Rust SDK 的 `QueryContext`/`Rows` 提供投影列数与按序号查询；普通/Planner service
  都有 `WITH_PROJECTION` 和 `WITH_SQL_AND_PROJECTION`。后者仍严格要求 SQL，前者
  不要求 SQL/poll。SDK 检查列数上限、指针/列数组合、0/1 元素、保留字段和完整行
  arity；元数据不跨回调/线程，raw rescan 不保存 context。
- 分词插件采用 minor 4，未请求 token/ordinal 分别输出合法空字节/整数零，仍推进
  分词与序号状态。所有 cell 的类型、NULL 约束和完整列顺序不变，不跳过副作用。
  当前模块 build ID 为 `rust-text-table-projection-v7`，仍为 13 服务、16 扩展对象；
  SQL-series 自身仍为 minor 3。没有改变 GIS 的 C/C++ 实现语言。
- loader 给旧 minor 0/1、2、3 服务分别传递精确 v1、v2、v3。白盒覆盖 0–4 的 next
  大小；真实 Rust DSO 覆盖逐回调切换投影、全零掩码、旧上下文与重扫、非法掩码、
  失败保持和 lease 释放。SQL-series 用非法未知 v4 后缀验证 open/next 的 v3 裁剪。
- 完整生产编译/链接通过；SDK 单元/集成、C/Rust ABI 布局、16 项文档测试、SDK/插件
  Clippy、22 项独立 CTest 通过。完整 kernel runner 编译/链接/运行通过：六种真实
  引用整理/spec codegen 场景都在 open/next 断言 v4 掩码，再委托真实 Rust DSO；
  原有批量/EOF/重扫/故障清理、generator、SQL/PL 和 catalog 用例同时通过。
  源码边界、Rust 插件二进制审计、12 项 build gate 与 diff 检查通过。
- 这是可选的字段计算提示，不是列式/Arrow 接口或 filter pushdown；分词仍需扫描
  以保持行数，未宣称吞吐/RSS 改进。实库客户端/完整优化器、PL 集合矩阵、真实事务
  权限、查询期 catalog 写入、深层 planner/index/类型、异步资源和轻量化指标仍未
  完成，整体目标保持。详见 [表函数投影契约](plugin-table-batches.md)。

### 前一检查点：真实引用整理与表函数 spec codegen

- 用生产 `ObOperatorFactory → generate_spec` 替代批量 fixture 中手工构造的列映射，
  发现引用整理会强制引用所有表函数列，导致插件列裁剪不能真正生效。现仅对 native
  插件表函数取消这项强制保留，完整列声明仍在冻结 binding 中；内置/PL/JSON 等
  原规则不变。移除专有 codegen 中未使用的 physical plan allocator 引用。
- 内核六种场景通过真实 parser/resolver、重复引用整理、表达式帧、专有 spec codegen、
  执行器与 Rust DSO：双列、分别单列、零列物化、反转 SELECT 顺序、WHERE 专用列保留；
  各场景继续验证批量数据、EOF/重扫/失败清理。越界/低于起始序号/重复列槽在 codegen
  失败，不打开游标。通用 output/calc/filter 字段与逻辑算子仍由 fixture 装配，不能
  宣称完整优化器选路或 COUNT 聚合执行已经验证。
- 内置 generator 兼容用例发现其 COLUMN_VALUE 名称查询误入 PL UDT 查找；补上内置
  generator 的静态列名路径，不改真正 PL 集合处理。修正后 0/3 行、每批单行回退和
  重复 EOF 通过。只读实库脚本同步增加 generator 查询，但仍未在真实服务器运行。
- 最新完整生产编译/链接和完整 kernel runner 已通过；本轮 22 项独立 CTest、源码
  边界、12 项 build gate 与 diff 检查通过。前两次 codegen 失败暴露旧引用规则，
  后续 generator 解析失败暴露 UDT 分支问题；修正后的最终内核回归全部通过。
- Rust ABI/SDK 保持不变。PL 集合矩阵、完整客户端/优化器、实库事务权限、查询期
  catalog、深层 planner/index/类型、列式/异步资源以及轻量化指标仍未完成，整体
  目标保持。详见 [批量执行及验证边界](plugin-table-batches.md)。

### 前一检查点：表函数原生批量执行

- `ObFunctionTableOp` 接入向量化 get-next-batch，按请求/算子上限调用已有 native
  cursor.next；Rust `Rows` 在一次回调中产生多行，不需要新 ABI。非插件 PL/system
  路径保留单行回退。逐行读取仍使用原生命周期与请求一行的路径。
- SQL codegen 为插件表函数保持完整声明列序号，未引用列为空槽；fetch 跳过其结果
  物化，支持仅选择第二列和仅消费行数。结果按 batch index 写入独立 datum/变长
  缓冲区；检查实际 emit 数量与报告行数。回调失败时不发布部分批次，关闭游标并
  保持首错，显式 rescan 才重新准入。
- 完整生产编译/链接通过；22 项独立 CTest、源码边界、12 项 build gate、diff
  检查通过。内核新增真实 Rust DSO 批量用例通过：请求 2/4 分别返回 2/3 行，
  每批仅一次 native next；Unicode/长字符串、完整/部分/空投影、稀疏映射序列化、
  EOF 重读、重扫及第一行 emit 后第三次 poll 超时、零输出计数/lease 释放均验证。
- 第一次内核运行因测试仅配置算子 batch size、未配置 physical plan batch size，
  导致 skip bitmap 未分配而段错误；修正 fixture 使其与生产计划配置一致后，完整
  内核编译/链接/运行通过，原 SQL/PL、catalog、版本包及生命周期回归同时通过。
- 新增只读 opt-in `table_batches_server.py`，覆盖完整客户端投影/COUNT/过滤/类型
  消费/NULL/嵌套 SQL/LIMIT 重读，语法与 CLI 检查通过。本环境重新验证 socket 创建
  返回 EPERM，实库脚本尚未运行。内核用例手工装配 spec，不能冒充完整优化器所选
  计划或聚合算子验证；生产 codegen 映射及 PL/system 回退矩阵仍需高层回归。
- 本次不是列式 ABI/Arrow/零拷贝，也未测量吞吐、RSS 或真实取消延迟。查询期 catalog、
  深层 planner/index/类型、其他 SQL 对象、实库事务/权限、异步资源与轻量化指标仍在
  整体目标内。详见 [表函数批量执行](plugin-table-batches.md)。

### 前一检查点：表函数同步 SQL

- 表函数 minor 3 / context v3 追加现有 SQL API，复用 v2 query handle 和宿主
  `PluginSqlContext`。SQL fetch 为 open/next 提供同一次回调的 SQL/poll 首错状态；
  loader 向旧 minor 0/1/2 分别保留原 context 范围，不向 minor 2 暴露 SQL 后缀。
- Rust SDK 增加 `QueryContext`、`Cursor::open_with_context`、`Rows::execute_sql`
  以及普通/Planner 两种 `WITH_SQL`。SQL 依赖在 open/next 明确检查；忽略 open
  SQL 失败仍销毁返回的 cursor，不发布句柄。轮询也可以在 open 中使用，旧实现
  默认调用原 open。query context 不跨线程、不进入 cursor、不在 consumer 中重入。
- 标量与表函数共享类型化 SQL 参数/结果实现；标量 SQL 改用已保留的完整扩展
  context 引用，不从仅覆盖 v1 的引用扩大可访问范围；同时检查 SQL API 保留字段。
- Rust text 新增 `seekdb_rust_sql_series`，在 open 查询字符数、next 参数化查询
  ordinal；仅保存两个整数，明确不标记 immutable。共 13 个服务、16 个扩展对象，
  build ID 同步为 `rust-text-table-sql-v6`。示例重扫使用 close/open，不保存 SQL 指针。
- 完整生产编译/链接通过。SDK/ABI/集成及 16 项文档测试、SDK/插件 Clippy 通过；
  22 项 CTest 通过，包含真实 Rust DSO 的 open/next SQL 往返、分批/EOF/NULL、
  缺少能力、打开/读取失败及 lease 释放。SQL transport 在该层为受控替身。
- 新增内核 parser/resolver/codegen/table fetch 到 Rust open 和真实宿主 SQL 的
  错误注入用例，发现现有 SQL bridge 默认构造的 `ParamStore` 没有绑定分配器，
  非空参数在进入 SQL 状态检查前即返回 -4013。已修复为每次同步调用拥有临时
  arena 的参数存储，避免流式调用把参数内存累计到整条查询结束；标量/表函数
  同时受益。修正后完整生产构建及内核编译/链接/运行通过：真实表函数 SQL 打开
  超时、重复 fetch 保持错误、rescan 重新准入、lease 释放通过；额外六种参数
  表示均经过真实转换进入受控状态检查。原 SQL/PL、catalog、版本包与 DSO
  回归同时通过。源码边界、12 项 build gate 和 diff 已通过。
- 实库 SQL 权限/事务/外层失败回滚、查询期 catalog、完整深层扩展、AI 批处理与
  资源/轻量化仍需继续，整体目标不缩小。详见 [表函数 SQL 契约](plugin-table-sql.md)。

### 前一检查点：表函数协作式查询控制

- 表函数 minor 2 通过保留 v1 前缀的扩展 context 提供 query poll；不包含 SQL
  execute，也不把借用的宿主指针存入 cursor。旧 minor 0/1 服务仍收到精确 v1。
- Rust `Rows` 增加能力探测、预算快照与原始数据库错误；poll 失败后禁止 emit，
  即使插件忽略错误后返回成功也保持失败。`WITH_QUERY_CONTROL` 可选择普通 cursor
  或带 Planner 的服务；minor 2 允许空 estimate 使用宿主默认值，不强迫插件实现 Planner。
- SQL fetch 在回调失败后关闭 cursor、释放 lease 并保存首错，重复 fetch 不重新
  准入；显式 rescan/close 重置流状态。Rust 分词示例扫描长词/空白期间每 4096 字符
  检查状态，模块身份同步为 `rust-text-table-control-v5`。
- SDK 测试、C/Rust 布局对照、Clippy 及 22 项独立 CTest 已通过。新增 SDK 测试
  验证忽略 poll 错误不能继续输出、失败后的重扫/析构及旧 context 回退；loader
  白盒验证旧服务 next 的 context 范围和重复 close。
- loader 新增 5 个受控服务场景：minor 0 与 minor 2 在无 estimate 时使用
  199 行/199 字节/代价 1，minor 1 缺少回调、minor 2 布局过短或保留字段非法
  均拒绝，失败不留下 estimate 或 lease；整个默认估算不调用执行回调。新增后
  重跑 22 项 CTest 全部通过；源码边界及 12 项 build gate 已通过。
- 首次内核回归暴露旧投影 fixture 预期打开失败后自动重试，已更新为显式 rescan，
  并增加失败保持、禁止重复打开与即时关闭断言。真实 Rust DSO 场景增加长单词
  扫描中第三次 poll 超时、lease 释放与重新扫描；修正后的完整内核编译、链接和
  运行通过，既有 SQL/PL、catalog builder/lookup、版本包与 DSO 回归同时通过。
  完整生产目标也再次构建通过。上述是实际内核/DSO 加受控状态注入，不是真实
  客户端 KILL、网络中断或取消延迟测量。
- 这不是线程强制中断、网络取消或后台任务调度；深层 planner/index、查询期
  catalog、其他 DDL/类型、实库事务及 AI/轻量化验证仍在完整目标范围内。

### 前一检查点：标量协作式查询状态与 deadline

- SQL API 增加保留 v1 前缀的 minor 1 / v2 表，提供 `poll_query`，不执行 SQL 即可
  调用真实 `ObExecContext::check_status()`，并读取同一 physical plan 的剩余时间。
  无限 deadline 用 -1 表示；错误保留数据库错误码，和 SQL execute 共享首错状态。
- Rust `Call` 保存原分配范围内的可选扩展 context，提供 `supports_query_control`
  与 `poll_query -> Result<Option<Duration>, sql::Error>`。SDK 验证 API 大小/版本、
  保留字段与输出；context 仍是同线程借用，不是后台任务的取消 token。
- `scalar_function!` 新增可选 `query_control: true`：请求扩展上下文，同时允许
  `sql: false` handler 接受旧版 v1。首次 CTest 暴露直接改为 `sql: true` 会破坏
  旧调用的兼容性，现已分开这两种请求，不放宽原 SQL 模式必须有 v2 的约束。
- Rust 字符计数示例在支持的宿主中于开始、每 4096 字符和结束时检查；未支持时
  保留旧计数路径。build ID 和加载测试身份同步为 `rust-text-query-control-v4`。
- 完整生产构建通过；SDK 单元/ABI/集成（新增 4 项 query control）/14 项文档
  测试及 SDK/Rust text Clippy 通过；修正兼容入口后，22 项 CTest 全部通过，
  源码/二进制边界及 12 项 build gate 通过。
- 最新 kernel runner 完成编译/链接及运行；真实宿主 query status 与 Rust DSO 的
  5 个新增场景通过：无限/有限预算、已超时、QUERY_KILLED、中途第三次状态检查
  注入失败。失败不产生计数结果；重置 deadline 后 poll/execute 仍保留同次调用的
  超时错误。原 SQL/PL、catalog builder/lookup、版本包及 DSO 回归同时通过。
  这些是受控会话/故障测试，不证明客户端 KILL、实际外部请求取消或性能指标。
- 详见 [查询状态契约](plugin-query-control.md)。这不是强制线程/网络取消或异步
  调度；表函数、后台任务、AI 批量/资源、轻量化指标和其余 catalog/深层扩展、
  实库验证目标仍需继续实现和验证，整体目标保持。

### 前一检查点：事务视图内 routine 查找

- `catalog_spi.h` 增加按 struct_size 协商的 build context v2 后缀；保留原 v1
  和 SPI 1.1 回调。Rust 提供 `RoutineKind`、`supports_lookup` 和返回 Option ID
  的 `lookup_routine`，查询所得身份不能安全逃逸构建视图。未知能力不伪装成不存在。
- C++ bridge 与实际 routine builder 接通 lookup；使用安装目标库、当前 owner、
  正常 routine SHOW 可见性及 schema guard 名称比较，读取 base/overlay 的同一
  视图。查找不授予执行权、成员或依赖；错误与 create 共用首错状态，不存在不算错误。
- Rust 示例执行不存在检查、创建后大写名称查回 ID、同名过程不存在检查，再创建
  引用函数。真实 DSO 测试新增查找失败/非法 ID；内核场景新增 base procedure、
  大小写/命名空间、空/NUL/无效 UTF-8/超限名称、非法种类与权限失败。
- 完整生产构建通过。SDK 单元/ABI/集成和 14 项文档测试、SDK/Rust text Clippy
  通过；默认 Rust 链接两次在不同测试目标发生 lld SIGSEGV，使用单次命令的
  `RUSTFLAGS='-C link-arg=-Wl,--threads=1'` 重跑 SDK 测试成功，未修改全局配置。
  随后的默认配置独立 CTest（含 SDK 测试）22 项全部通过，源码/二进制边界和
  12 项 build gate 同时通过。
- 最新 kernel runner 完成实际 Rust DSO 重建、安装、内核编译/链接及运行：15 个
  宿主构建场景通过，新增 base procedure 与 overlay function 查找、大小写、
  命名空间区分和六类查找失败验证通过；真实 Rust 插件完成查找—创建—查回同一 ID—
  引用流程。原包源、版本更新、SQL/PL 和 DSO 回归同时通过。schema/事务仍受控，
  不是实库隔离、回滚或新建函数最终查询结果的证明。
- 后续仍包括实库事务验证、查询期 catalog、其他对象类型、深层 planner/index
  以及 AI/轻量化目标；本阶段只增加安装上下文中的 routine 查找，不缩小整体目标。

### 前一检查点：C ABI/Rust 事务构建接口

- `catalog_spi.h` 新增 service v2（SPI 1.1），以原 v1 为前缀，追加 build 回调和
  独立 build context。原 prepare context/layout 不变；旧服务仍支持，新 minor
  在旧宿主上明确拒绝。loader 校验短布局、未知版本和保留字段，不读取缺失后缀。
- `ICatalogDeclarations` 可持有已绑定的构建程序，保留 module lease 至安装/发布
  结束。CREATE EXTENSION 将其传入上一阶段的 Root builder；安装身份绑定到
  tenant/database/owner/name/version/module，构建最多一次，宿主错误不能被吞掉。
- Rust SDK 提供 `TransactionalInstaller`、`TransactionContext<'txn>`、
  `RoutineId<'txn>` 和 `Service::V2`。上下文不 Send/Sync，ID 不能安全逃逸其生命周期；
  输入/输出身份验证和错误保持在 callback 边界内。不是普通查询 DDL 或事务控制权。
- Rust text 增加 `rust_text_built` control-only 包：prepare 不产生占位 SQL，build
  创建长度函数，再创建引用它的非空判断函数。CMake 交付单一 control；包只提供
  1.0。module build ID 和 catalog service 版本已同步更新，既有包行为保持。
- SDK 单元/ABI/集成/文档测试及 SDK/Rust text Clippy 通过；完整生产构建、源码
  边界及 12 项 build gate 通过。首次独立 CTest 的加载验证器仍预期旧 build ID，
  已同步测试身份；重跑后的 22 项独立 CTest 全部通过。
- 最新 kernel runner 完成 Rust DSO 重建/审计、内核编译/链接和运行。CMake 实际
  安装的 `rust_text_built` 目录只有 control；真实 Rust build 创建两个 routine，
  第二对象通过真实 PL resolver 引用第一对象的预留 ID/schema version，module
  lease 在构建期间保持有效，禁止重跑检查通过。原 SQL/PL、组合包与版本包路径
  同时通过。新函数的最终 SQL 返回值、Root 实库提交/隔离/回滚仍未由此验证。
- 实际 Root 安装提交/隔离/回滚、查询期 catalog、其他对象类别、深层 planner/index
  和 AI/轻量化目标继续推进，不能用受控 fixture 替代这些证据。

### 前一检查点：事务内 routine builder 宿主接线

- 新增宿主内部 `ICatalogBuildProgram` / `ICatalogRoutineBuilder`，安装 resolver
  可以绑定同步构建程序。Root 在原事务内、静态脚本 staging 之后执行它，每次
  create 复用 Rust 内存源校验、真实 parser/resolver、权限和 ID/version reservation。
  对象进入 transaction-local schema/privilege overlay 后返回 ID，后续创建可引用。
- 完整参数由 resolver 保留到持久化结束。4 MiB SQL、4096 对象和 64 MiB wire
  参数预算覆盖静态/动态两部分；失败清空输出 ID、保留首个创建错误、禁止程序重跑。
  回调错误不能被返回成功吞掉。所有对象成功后仍由原 Root operator 和 Rust 协调器
  写入/提交同一 schema 事务，不引入独立提交或提前发布。
- 当前是内核桥接，不是已开放的 native C ABI。现有公开 CatalogContext 仍是安装
  前声明文本；普通查询 SQL SPI 仍不允许 DDL。版本化 SPI、Rust 构建上下文和真实
  插件程序将在此路径上继续接入，不能用宿主 fixture 替代插件可用性的证明。
- 完整生产构建、22 项独立 CTest、源码/二进制边界及 12 项 build gate 均通过。
  最新 kernel runner 已完成编译、链接及运行；新增 9 个构建场景使用真实 PL、
  ID/version reservation 和 schema overlay，验证对象间依赖/fence、参数存活、
  权限/输入/stage 故障、吞错、异常和禁止重跑。原 Rust DSO、SQL/PL、版本包与
  组合依赖路径同时通过；仍不是完整 Root 实库事务/隔离/数据回滚的证明。
- 详见 [事务内 builder](plugin-catalog-builder.md)。完整目标及其他深层扩展、AI、
  轻量化和实库验证要求保持。

### 前一检查点：Rust SDK 生成版本 control

- `schema::VersionControl` 和 `PackageOptions::version_controls` 为每个版本生成
  独立 control。requires 的 None/Some(empty) 分别表示继承/清空；模块、schema、
  relocatable 可选覆盖，缺省字段不复制。生成器保留 SQL 文本和声明顺序，排序
  输出文件；字段、重复版本、文件名和 control/SQL 总数在写入之前校验。
- 主 control、次级 control 与 SQL 都沿用 create-new 写入；已有次级文件也会在
  写入其他文件前阻止覆盖。生成不注册对象、不加载 provider、不执行迁移，也不
  自动承诺路径可换模块或命名空间。安装源选择仍由主 PackageOptions 决定。
- 新增 `seekdb_versioned_schema` Rust generator：1.0 继承 alpha/zulu，middle
  需要 migration，1.1 最终依赖 gamma/zulu。kernel runner 不再手写这组 fixture
  元数据，而是通过 CLI 执行真实 generator、校验文件清单，并将生成物送入实际
  包源/版本计划/catalog/Rust driver 回归；provider 行仍来自受控 SQL 传输。
- SDK 全部单元/ABI/集成/文档测试与 SDK/Rust text 全目标 Clippy 通过；完整生产
  构建、22 项独立 CTest、源码/二进制边界及 12 项 build gate 通过。最新 kernel
  runner 已完成编译、链接和运行，实际执行新增 generator，并将其 6 个产物送入
  版本计划/catalog/Rust driver，缺失临时 provider、别名冲突、最终边与回滚选择
  测试通过；原 SQL/PL、组合包和 Rust DSO 回归同时通过。仍不证明实库隔离或回滚。
- 该改动完善 Rust 开发工具链，不替代查询期 catalog builder、通用对象 DDL、深层
  planner/index、AI 批量/异步资源和实库验证等剩余目标。

### 前一检查点：版本 control 与迁移临时依赖

- Rust 包读取器支持可选 `name--version.control`，每步独立覆盖主 control；目标
  缺省字段继承主文件，`requires = ''` 清空，不沿迁移链累积覆盖值。次级文件禁止
  default_version/directory，沿用 UTF-8、大小、路径逃逸、重复/未知字段校验。
- 最终 requires 与中间步骤 prerequisites 分开保存和传递，总计最多 64 个名字。
  新安装包括执行过的基础版本要求；更新不收集不重放的起点要求。同版本 no-op
  和零脚本 native 新安装不接受临时依赖。所选步骤换模块或命名空间仍明确拒绝。
- C++ catalog 在 schema 执行前锁定两组 provider，最终只记录 requires 对应的
  稳定 ID 边。更新图校验也只替换最终边；临时 provider 缺失或与永久 provider
  解析为同一 ID 时拒绝。source/spec/request 的 preflight 防止中途丢失临时声明。
- CLI 接受 generator 输出版本 control，并检查全部次级文件，包括默认路径外的
  文件。SDK 结构化版本 control 生成尚未接通；可通过 generator 补充手写文件。
- 97 项 runtime 单测、11 项 CLI 单测及两边 Clippy 通过；完整生产构建通过。
  首次生产编译发现安装期 native 声明入口遗漏内部 C struct 新字段初始化，已
  补齐并重新完整构建。源码边界、12 项 build gate 与 22 项独立 CTest 全部通过。
  最新 kernel runner 已完成编译、链接及运行，覆盖真实多步包源→更新计划→catalog/
  Rust driver、临时 provider 缺失/身份别名拒绝、不写最终临时边，以及安装 preflight
  拒绝丢失临时声明；原 Rust DSO、SQL/PL、组合包路径同时通过。catalog 的传输和
  schema 仍使用受控 fixture，不代表实库隔离、锁竞争或数据回滚已经验证。
- 这不实现 runtime-session CatalogContext、通用对象 builder、Server-dev 深度
  planner/index 接口、CASCADE 或完整版本约束，也不证明实库事务隔离和回滚。
  完整目标保持，详细源语义见 [版本包读取](plugin-extension-package.md)。

### 前一检查点：版本更新允许变更 requires

- 版本不同的 UPDATE 不再要求目标 requires 与已安装集合相同；目标来自更新包源，
  支持增加、替换、移除同库已安装 provider。仍需明确的更新路径。同版本 no-op
  保持集合不变，不把编辑 control 当成已安装版本的隐式迁移。
- Rust 新增 `extension_dependency`：接收稳定 SQL ID 边，替换目标的完整 provider
  集合，复用非递归图排序器检查最终图；非法 ID、超限与循环明确失败，不运行 SQL。
  C++ 继续持有唯一 schema 事务，锁定新 provider 和完整同库依赖边，在 detach/
  schema apply 前调用 Rust。权限、原生模块身份与普通 routine 依赖规则不变。
- 版本更新先取得独立的 `sql-extension-dependency-update` 事务写锁，再取得实例
  锁；不复用分配 ID 的序列行，不新增进程 mutex 或独立提交。当前跨库更新也共享
  此管理锁。旧边删除、新边插入、成员集合和目标版本一起提交；错误回滚，未知提交
  不自动重试。完整并发/锁隔离仍需实库证明，不能仅据代码接线宣称已验证。
- 新增 Rust 图替换测试，以及实际包源读取→catalog/Rust driver 的受控传输用例，
  覆盖增加/移除、循环、缺失 provider、读取失败及删除旧边后的写失败。空成员
  DELETE 的测试替身原来固定报告 1 行，生产成员数校验正确拒绝；现已返回真实
  模型中的 0 行，并保留生产校验和详细失败诊断。
- 95 项 Rust runtime 单测与 Clippy、完整生产构建、22 项独立 CTest、源码边界及
  12 项 build gate 已通过；修正替身影响行数后，最新 kernel runner 已完成编译、
  链接和运行。实际更新包源到 catalog/Rust 图检查、增加/移除、循环拒绝及故障
  回滚选择用例通过；原组合包和 Rust DSO/SQL/PL 路径也通过。这不是实库事务隔离
  或实际数据回滚的证据。版本专用 control、CASCADE、跨库/版本约束、实库并发更新
  与回滚仍未验证或未实现，整体目标保持。

### 前一检查点：组合包与 Rust requires 生成

- 新增可交付纯 SQL 包 `text_composed`，依赖同库 `text_ops`，以普通 SQL body
  组合 provider 的字符数/字节数函数。1.0 提供 ASCII 判断，1.1 新增额外字节数；
  CMake plugins 组件交付 control/base/update，不新增动态库或核心 factory。
- Rust SDK 增加 `schema::PackageOptions`、`render_with_options` 和
  `write_to_with_options`，把 SQL/native 安装源与 requires 统一为生成选项。
  最多 64 项，验证名称/重复/自依赖，保留声明顺序；校验先于写入，文件仍只允许
  create-new。旧 render/write/source API 保留原输出，SDK 不查询或自动安装依赖。
- `seekdb_composed_schema` Rust generator 生成上述纯 SQL 包，复用手写脚本但不
  声明 Rust text native module。CLI 使用实际 Rust 包源读取器检查生成物；kernel
  runner 将所有生成文件与 CMake 实际安装产物逐字节比较，再解析 provider、consumer
  和更新脚本，核对两个真实 routine ID 的 PL 依赖及 schema-version fence。
- 完整生产目标、SDK 单测/ABI/文档测试、SDK/Rust text Clippy、22 项独立 CTest、
  源码/二进制边界及 12 项 build gate 已通过。最终 kernel runner 编译/链接/运行
  通过，覆盖组合包生成和真实解析；schema 仍由受控 fixture 提供，不代表数据库
  事务安装、权限或隔离已验证。SDK 首次默认并行测试发生链接器 SIGSEGV，随后
  串行及 CTest 重跑通过，未据此推断崩溃根因或宣称偶发问题已解决。
- 实库脚本增加缺失/跨库 provider 拒绝、函数值（Unicode/ASCII/空/NULL）、稳定
  依赖 ID、update/no-op 保留边、RESTRICT、consumer 删除后旧调用拒绝和整库清理。
  Python 语法及命令入口检查通过；本环境创建 loopback socket 返回 EPERM，未启动
  测试数据库、未运行上述实库用例。保留待实库执行状态，不把环境限制推广为整个
  目标被阻塞。
- 完整目标继续包括事务绑定的查询期 CatalogContext、更多对象类别、依赖变更/
  CASCADE、深层 planner/index 接口、AI 批量与资源契约，以及轻量化测量。

### 前一检查点：Extension 组合依赖

- `requires` 从文件/内存源贯通安装 spec、更新 request 与 catalog。Rust 负责
  名称、数量、重复和自依赖验证；C++ 在活动 schema 事务中锁定同库已安装
  provider，先于 schema apply，随后记录稳定 Extension ID 依赖边。
- 新增 `__all_extension_dependency`（1152）；DROP 在 detach/对象删除之前执行
  传入边 RESTRICT，直接 record-drop 同样检查。删除 consumer 清理出边，数据库
  整体删除在同一 DDL 事务中清理本库依赖。native provider→Extension provider
  锁序与模块管理一致，不新增独立事务或系统表直写 API。
- 更新及同版本 no-op 验证已安装集合，允许重排但暂不允许增删依赖。未实现自动
  安装、版本约束、跨库依赖和 CASCADE；未将这些限制视为最终设计边界。
- 新增实际 catalog/SQL binder/Rust driver 的受控传输测试，覆盖 provider 缺失、
  重复身份、schema/写入失败、依赖记录、RESTRICT、删除清理、更新集合验证，以及
  读取/结果关闭错误。明确断言 commit/rollback 选择，不把事务模型当成实库隔离。
- 93 项 Rust runtime 单测、Clippy、源码边界、12 项 build gate、包含新表的完整
  生产构建及 22 项独立 CTest 已通过。kernel runner 已用当前生产对象完成编译、
  链接和运行，新增实际 catalog/Rust driver 用例与已有 Rust DSO/SQL/PL 路径均
  通过；内存源丢失 requires 的绑定拒绝也已执行。实库并发、提交/回滚、恢复与
  bootstrap/升级尚未验证，不能以受控 transport 的提交选择代替实际数据回滚。
- 验证过程保留两项事实：kernel fixture 遗漏新增字段的显式初始化导致首次
  `-Werror` 编译失败，补齐后完整重跑通过；脚手架诊断断言首次失败，增加原始
  stdout/stderr 输出后，单项和全套重跑通过，首次失败原因仍未复现。未修改或
  放宽诊断断言，也未据重跑成功宣称消除潜在偶发失败。

### 前一检查点：native-source Rust 生成工具

- SDK `schema::Package` 增加 `InstallSource`、`render_with_source` 和
  `write_to_with_source`。native 模式要求 module，允许零脚本生成 control，或只
  带显式更新；拒绝混入基础 SQL。原 render/write_to 调用仍采用 SQL 模式，输出
  create-new，不推断迁移，不在生成阶段执行插件 prepare 或查询 SQL。
- CLI `schema --example NAME` 允许选择同一插件的不同 Cargo generator，默认
  仍为 seekdb_schema。新增 seekdb_native_schema 示例生成 rust_text_native 的
  control 和显式更新，部署格式与上一检查点完全相同，不增加占位基础 SQL。
- CLI 复用 runtime 的只读 `inspect_install_source`，共享实际 control parser、
  默认版本路径和文件源读取规则；拒绝未知/重复 control 键、无效 native 声明及
  默认版本不可达等问题。CLI 为此依赖 host Rust crate；公开 SDK/native 插件
  不依赖 host crate，不加载动态库，也不增加第二套 catalog 或 control 解析器。
- 新增 SDK native 生成/无覆盖测试、CLI control-only/显式更新/非法元数据与
  example 参数测试；仓库外 scaffold 检查成功 generator 的非法 control 也会
  失败并保留 incomplete marker。kernel runner 将 native 和 SQL 两种生成产物
  分别与真实 CMake 安装文件逐字节比较，再走原有真实 Rust DSO/PL resolver。
- SDK/CLI/runtime 单测、四套 Clippy、生产构建、22 项独立 CTest、源码与二进制
  边界及 12 项 build gate 已通过。增强的 scaffold 诊断断言单独重跑通过，确认
  非法 native control 的失败来自实际源读取器。kernel runner 已完成当前生产
  对象的编译/链接/运行，两套生成文件与安装产物逐字节一致，真实 Rust DSO 的
  native-only/附加声明及 PL resolver 路径通过。Root/catalog 仍使用 fixture，
  不代表实库提交、回滚、恢复或任意 SQL 对象 admission 已验证。
- 查询期事务绑定 CatalogContext、native-symbol/type DDL、依赖/CASCADE、深层
  planner/index、AI batch/资源与轻量化等完整目标继续保留。

### 前一检查点：native-only 安装源

- Rust control parser 新增显式 `install_source = 'native'`，要求 native_module 和
  default_version。fresh install 不需要基础 SQL 文件，由 native 安装服务提交
  全部对象；不是读取文件失败后的自动 fallback。当前 control 广告 default_version
  这一项直接安装版本，更新仍用显式 SQL 版本边，不能重放安装回调代替迁移。
- 内存源、内部 C ABI 和 C++ owned source 传递相同模式。普通 SQL 源仍拒绝空
  基础脚本；native 基础源必须零静态脚本。同版本 no-op 与不同版本更新的规则
  保持独立，native 模式不能把缺失迁移变成成功。
- loader 对 native 源要求安装服务存在且提交非空声明；空/注释-only 声明无法
  通过后续内核解析，准备前零对象集无法通过 routine 安装 admission。所有对象
  继续走既有权限、依赖、成员和 Root schema 事务，不新增系统表直写通道。
- 新增 [rust_text_native 示例](../../../plugins/sql_packages/rust_text_native/README.md)：
  初始仅有 control，由 Rust 声明 rust_native_length，另交付显式 1.0→1.1 更新。
  CMake 安装 control/更新文件，不附带占位基础 SQL，也不把 native 库拉入 core。
  Rust text 的新 build ID 为 rust-text-native-install-v1，服务数仍为 12。
- 完整生产构建、92 项 Rust runtime 测试、runtime/Rust text Clippy、22 项独立
  CTest、源码/二进制边界与 12 项 build gate 已通过。真实内核 runner 已完成
  当前生产对象的编译/链接/运行：安装产物无基础 SQL，native-only routine 的实际
  PL/native lookup、lease 保留、显式更新/no-op，以及准备前和空声明拒绝均通过。
  Root/catalog 仍为受控 fixture，不能据此宣称实库提交、回滚和恢复已验证。
- native 模式 control 当前直接编写；SDK/schema CLI 仍要求 SQL-base 包，生成工具
  支持尚待接入。无 control 文件的包发现、查询期事务绑定 CatalogContext、
  native-symbol/type DDL、依赖/CASCADE、深层 planner/index、AI 与轻量化等完整
  目标继续推进，未把 control-only 声明入口当作完整 PG 扩展能力。

### 前一检查点：Rust 插件安装期 CatalogContext

- 新增公开 `catalog_spi.h` 和可选 `<module_id>.catalog.install` 服务。CREATE
  EXTENSION 在基础 SQL 读取及命令权限检查后调用插件 prepare，插件按包名、版本、
  database/owner 提交完整 SQL；声明复制后经过 Rust 源校验和内核解析，仍由既有
  ExtensionRoutineResolver/Root 路径执行对象 admission、成员与 schema 事务。
- C++ loader 校验 service owner、SPI 布局和身份，回调期间不持有 loader mutex。
  `ICatalogDeclarations` 同时持有 SQL 与模块 lease，生命周期覆盖准备、安装和
  schema 发布。非法 UTF-8/NUL、大小/数量、错误线程、关闭后提交和忽略提交错误
  的行为均有检查；失败不返回半份可安装声明，成功提交不是对象创建成功。
- Rust SDK 增加 `catalog::{Installer, CatalogContext, Service}`，提供不可跨线程的
  借用视图和 panic/status 边界。Rust text 实际广告安装服务，为 rust_text_ops
  动态生成第三个 INVOKER 例程 rust_runtime_length；其它包不自动获得该声明。
  build ID 更新为 rust-text-catalog-install-v1，manifest 与动态库共 12 个服务。
- `append_catalog_declarations` 保持片段独立解析，统一执行静态/动态 SQL 限额，
  失败清空、重复追加拒绝。source 保持基础包，完整解析结果还包括动态声明。
  详见 [安装期 CatalogContext 契约与 Rust 示例](plugin-catalog-install.md)。
- 验证已完成：完整 seekdb 生产构建；22 项独立 CTest；SDK 单测、C/Rust ABI 布局
  和借用 compile-fail；SDK/Rust text Clippy；源码/插件二进制边界；12 项 build
  gate。新增 loader 负向用例的状态码拼写已修正后重新编译、运行完整 CTest。
  kernel runner 已用最新生产对象完成编译/链接/运行，真实加载 Rust DSO 并将
  静态两条例程和动态第三条例程送入 PL resolver；核对名称、owner/database、
  native lookup 以及安装阶段 lease 保留和退出后释放。原有文件/内存路径仍通过。
- 这些测试仍使用受控 Root/catalog fixture，不是实库提交、回滚、恢复或并发卸载
  的验证。当前需要有效基础 control/SQL，更新继续使用显式脚本；prepare 不能
  查询 SQL、控制事务或启动外部任务。查询期事务绑定 CatalogContext、native-symbol
  和更多类型 DDL、依赖/CASCADE、深层 planner/index、AI 与轻量化目标仍未完成。

### 前一检查点：内存 SQL 声明接入统一安装路径

- 新增宿主内部 `seekdb_runtime_package_from_source`：Rust 接收已经选定的内存
  SQL 源，校验身份、namespace、依赖和连续版本链，返回与文件读取相同的 owned
  Package。普通字段/依赖/脚本数组/SQL 均有边界；校验 UTF-8、NUL、空基础源、
  自依赖/重复依赖、固定 schema 与 relocatable 冲突，以及 no-op/空更新的区别。
  不读取目录、不重选版本、不拼接片段，也不把源当作 catalog 权限或事务凭证。
- C++ 新增 `validate_extension_package_source` 和 `ExtensionScript::load_source`。
  先验证并独立复制输入，再替换旧状态；支持输入别名当前 source。文件/内存两条
  入口共用 parser helper，保留分片边界、4096 拆分项总限额和失败清空语义。
  输入中动态生成的函数可继续进入既有 ExtensionRoutineResolver，而不是绕过
  schema service 直接写系统表。
- 新增 3 项 Rust 测试、C++→Rust 正常/错误源测试，以及内核内存源解析、自别名、
  调用者修改输入、有效前缀失败、跨片段 token、空更新/no-op 和大小边界回归。
  native-backed PL fixture 保留原文件包路径，另用动态构造的两个 SQL 函数走
  内存声明路径，核对真实 Rust native lookup、例程名称/owner/database 和无 SQL
  执行。该 fixture 不是实库 catalog 提交证据。
- 90 项 Rust runtime 单测和 Clippy、完整生产构建、22 项独立 CTest、源码边界
  及 12 项 build gate 已通过。首次并行回归的 scaffold 链接器报告 OOM/崩溃；
  生产构建结束后未改断言、单独重跑完整 CTest 全部通过。kernel runner 已完成
  最新编译/链接/运行并通过：内存源解析和两个动态 native-backed routine 的真实
  PL resolver 路径均已执行，原有文件包路径继续通过。
- 当前仅为后续 native CatalogContext 打通宿主侧入口，尚未暴露插件可调用的
  catalog 服务或查询内 DDL。现有 Root installer 仍要求顶层且无活动事务，
  不能通过在普通 SQL SPI 中放开 DDL 来暗中独立提交。插件上下文/租约、运行期
  catalog 事务、内存更新计划的观测绑定、深层 planner/index、AI 与轻量化等
  完整目标继续推进，未以这一接入口替代最终扩展对象模型。

### 前一检查点：Rust SQL/schema 生成

- SDK 新增 schema::{scalar_wrapper, Package, Script}。wrapper 共用实际 native
  FunctionDefinition，检查参数数量/类型、重复参数、标识符和名称自遮蔽；沿用
  determinism，SQL data access 和 TEXT/LONGBLOB 字节语义由作者明确选择。
  当前类型映射为 BIGINT/TEXT/LONGBLOB，不把自定义类型悄悄降为普通 bytes。
- Package 组合生成 SQL 和手写完整 routine，保存显式版本更新边；不按分号拆分、
  不推断迁移、不另建 catalog。生成前校验文件名、重复项、默认版本可达性和
  宿主包源大小限制，文件 create-new。默认版本/native module 写入 control。
- cargo-seekdb 新增 `schema --manifest-path Cargo.toml --output NEW_DIR`：预留
  新目录和 incomplete marker，执行项目的 seekdb_schema Cargo example，再检查
  普通文件、UTF-8/NUL、文件布局和大小。生成器失败或返回空产物时保留 marker。
  CLI 不解析 SQL/control 语义、不连接数据库；生成器和 build scripts 是可信
  开发代码，不是沙箱。目录必须由当前生成操作独占，marker 不是原子发布机制。
- `new` 模板新增 schema example，使用配套 rlib 共享声明，生产仍交付被审计的
  cdylib。Rust text 示例生成 rust_text_ops 的 wrapper、手写自定义类型组合函数
  和显式 1.0→1.1 更新。kernel runner 将生成结果与 CMake 安装产物逐字节比较，
  再用真实内核解析和 PL resolver、实际加载的 Rust native catalog 验证这些 SQL。
- SDK 单测/ABI/借用 compile-fail、9 项 CLI 单测、SDK/CLI/Rust text Clippy、
  完整生产构建、22 项独立 CTest、源码边界与 12 项 build gate 已通过。
  kernel runner 已完成真实编译/链接/运行并通过，确认三份生成文件与实际安装
  产物一致，native-backed routine 的基础/更新脚本解析和 PL resolver 通过。
  该 runner 仍使用受控 Root/catalog 事务 fixture，不等同于实库 SQL 提交/回滚。
- 这不是 PG LANGUAGE C/native symbol DDL、任意类型 DDL、运行期 catalog builder
  或完整 SQL 安装事务实现。wrapper 包安装前仍需 native module 已激活；native
  DSO/manifest 与 SQL/control 分别交付，签名/一致性和实库部署不由生成成功证明。
  直接 SQL/catalog 注册、深层 planner/index、AI batch/资源预算与轻量化验收等
  完整目标保留，不以 wrapper 工具替代最终对象模型。

### 前一检查点：Rust 插件项目生成与仓库外构建

- cargo-seekdb 新增 `new NAME --seekdb-root DIR [--output DIR] [--plugin-id ID]`，
  生成独立 Cargo workspace、public ABI cdylib、标量注册事务、生命周期回调、
  manifest、测试、CMake、README 和固定 toolchain。创建阶段不运行外部命令、
  不下载依赖、不修改父工程、不初始化 Git、不安装或装载数据库插件。
- 项目与打包共用 no-overwrite 目录预留机制，分别使用 incomplete marker；
  已有目录/文件/符号链接拒绝覆盖，写入失败保留新目录供检查。模板插入参数
  先校验，路径按 TOML/CMake 字面量编码，不递归替换用户路径中的占位符。
- RustPlugin.cmake 增加显式 external STANDALONE public profile，要求调用点
  位于仓库外独立工程根目录；宿主源码目录不能借此成为插件。仍执行真实
  Cargo 依赖图检查和二进制导出/core import 审计，不是 server-dev 私有接口。
  Cargo metadata 失败现在保留原始诊断，不再仅输出无法定位原因的 traceback。
- 新生成插件已在临时仓库外项目中完成 Rust 测试/Clippy、CMake 构建/审计、
  package 和真实 native loader 调用；验证 Unicode/空值/NULL/非法 UTF-8、
  引用归零、关闭后拒绝调用、激活提交拒绝。测试还覆盖已有输出保持不变、
  dangling symlink、缓存产物存在时私有依赖仍被拒绝，以及 core/nested/未显式
  opt-in 的 CMake 调用点拒绝。
- 7 项 CLI 单测/Clippy、完整 seekdb 构建、22 项 CTest、现有 kernel runner
  均通过。首次 SDK 回归遇到 LLVM 链接崩溃，单并发复验与随后完整回归均通过；
  未据此修改接口或掩盖失败。loader verifier/catalog 仍为受控协议 fixture，
  不能据此声称实库部署、签名、鉴权、跨平台或 Bazel 开发流程已完成。
- SQL/schema 生成、实库测试编排、深层 custom path/plan replacement/index、
  统一运行期 catalog 与 AI/轻量化等完整目标继续推进，未把 SDK 工具完成等同于
  PG 式扩展自由度和 Rust 框架的完整实现。

### 前一检查点：Rust 表函数规划估算

- 在保持 table service v1 布局不变的前提下，minor 1/v2 service 增加可选
  estimate 回调。SDK table_planning::{Context, Estimate, Planner, Service}
  复用已有 Cursor 实现；旧 minor-0 C/C++ 服务仍使用 199 行/199 字节/代价 1。
- C++ loader 校验固定 epoch 的 table binding，并联合固定对象与实现引用；
  只借出对象 ID、声明的转换后参数签名和输出列数，不执行参数、cast 或游标，
  不开放 SQL/session context。SDK 与 host 分别拒绝负数、非有限估算、错误
  布局/保留字段；host 用 NaN 初始化回调输出，避免漏写伪装成零代价成功。
- 实际 FunctionTablePath::estimate_cost 使用插件行数/行宽/代价，进入已有
  relation、候选比较和逻辑算子属性，不另建一套优化器。Rust words 的四个
  table descriptors 共用该协议，示例返回 8 行/40 字节/代价 4，明确只是先验
  示例而非测量模型；它不读取参数常量，也不限制实际输出行数。
- 新增 SDK 正常/零值/错误/NaN/Infinity/panic/元数据及 !Send 测试，C/Rust
  布局核对，native Rust 估算与过期 binding/引用归零，以及原 C 服务 fallback
  测试。完整构建、SDK/示例 Clippy 和 21 项 CTest 已通过；kernel 已重新编译、
  链接并执行成功，15 个完整 SELECT 解析样例进入真实 FunctionTablePath，
  检查逻辑算子的 card/width/cost；真实 add_path 比较使代价 4 的插件候选输给
  代价 2 的受控候选（旧代价 1 则相反）。同时断言估算不执行参数/cast 或打开
  游标。源码/产物边界、12 项 build gate 与 diff 空白检查通过；这不是实库
  EXPLAIN、完整优化器生成物理 spec 或任意自定义 path 的证明。
- 本次没有实现 custom path 注册、计划替换或索引 AM；值/统计/谓词选择率支持、
  深层接口、统一运行期 catalog、Rust 工具及 AI/轻量化的完整目标继续推进。

### 前一检查点：可执行的 Rust around-planning hook

- 新增公开 optimizer_spi.h 和 Rust SDK optimizer::{Definition, Hook, Context,
  Service}，通过原有注册事务贡献 optimizer.plan.v1 hook。真实
  ObOptimizer::optimize 经 module provider/loader 进入 Rust continuation 调度；
  不再只有 hook descriptor 和排序元数据。
- C++ 在执行任何回调前按同一 registry epoch 固定整个链的对象/实现引用并校验
  service；回调期间不持 loader/registry 锁。Rust 保证成功路径恰好调用一次
  continuation，允许提前返回错误 veto；后续规划错误保持精确数据库错误码，
  不允许插件吞错伪造成功。每链最多 64 hooks，每线程最多 16 层嵌套调用。
- SDK 的 call_next 保留原始 database_error，拒绝重复/遗漏 continuation 与
  错误元数据；借用 context 不可跨线程/异步逃逸，unwind panic 留在插件边界。
  C++ 核心/插件异常在返回 Rust 前捕获；host 的 panic=abort 策略未改变。
- Rust text 新增 planning hook 和 seekdb_rust_optimizer_calls() 计数函数，
  当前 11 services/15 contributions，build ID 为 rust-text-optimizer-hook-v1。
  GIS 仍是原有 C/C++ DSO，不依赖 SDK 或 Rust 算法重写。
- 完整 seekdb 构建、21 项 CTest、87 项 Rust runtime 测试、SDK 测试/布局核对、
  runtime/SDK/示例 Clippy 通过。kernel runner 已重新编译/链接/执行，验证真实
  optimizer 入口到 Rust DSO 的调用与错误保留。使用缺少规划服务的受控上下文，
  不是成功生成完整计划或实库查询的证明。追加重入测试已通过：16 层后拒绝
  第 17 层，全部引用归零，同线程后续顶层调用恢复；最终 21 项 CTest、源码
  边界、12 项 build gate 和 diff 空白检查均通过。
- v1 明确只提供同步 around/观察/veto，不暴露 SQL 文本或私有 query/plan 指针。
  计划缓存命中与独立 costing helper 不经过该入口。custom path/replacement、
  executor/index 深度接口、开放 catalog 与 Rust/AI/轻量化完整目标继续推进，
  不将本轮受限 hook 当作 PG 式深度扩展完成。

### 前一检查点：Native-backed SQL routine Extension

- 安装顺序 resolver 将 control 的 native_module 传入 ExtensionInstallSpec，
  preflight 校验 source/bound/request 三者一致。旧裸 CREATE 参数数组入口继续
  拒绝 native 包，避免其省略安装关联；没有给插件开放绕过 catalog 的写路径。
- Root routine 安装/更新/删除不再仅因 native module 关联而拒绝；保留 tenant、
  owner/权限、对象类别、成员依赖、只读和事务检查。已有 catalog recording
  负责同事务 provider 锁定、稳定 ACTIVE 检查与关联持久化，模块 RESTRICT
  查询该关联；DROP Extension 不卸载动态库，UPDATE 不允许替换 module ID。
- 新增 rust_text_ops 安装/更新包，普通 SQL 函数调用 Rust 标量函数和 typed
  重载。CMake 交付 SQL/control，不将可选 Rust/GIS DSO 拉入默认 core 构建。
- 真实 Rust DSO 已通过两条安装函数的 PL body 语义解析，并断言发生 native
  catalog lookup；Query→Root 测试验证 module ID 不丢失并在受控 Root admission
  故障后保留未提交结果。生产构建、21 项 CTest、85 项 Rust runtime 测试和
  边界检查通过；最终 kernel runner 已重新编译/链接/执行通过，验证安装包
  实际交付、更新 SQL 的 PL 解析、no-op 预检与拒绝 module ID 替换。
- 这不是实库 CREATE/ALTER/DROP/鉴权/回滚或模块卸载竞争的证明；显式关联也
  不等于自动发现函数体里的全部 native 依赖。通用 catalog builder、多对象
  DDL/Extension requires/CASCADE、深度 hook、Rust/AI/轻量化目标继续推进。

### 前一检查点：插件声明的表函数 NULL 语义

- SQL 层移除对任意 NULL 的无条件提前结束；保留逻辑类型并传递 is_null，
  不读取 NULL 的物理 payload。loader 在全部隐式转换完成后检查 descriptor
  的 NULL_PROPAGATING；未声明时插件可以自行消费 NULL 并返回行。cast 的
  nullness 与原始参数可不同，不用提前 short-circuit 限制非 strict cast。
- strict 初始空调用返回 OB_ITER_END/no cursor，不进入 table open。SQL 运行
  上下文缓存 EOF，rescan/close 重置，即使未创建游标也不会反复执行转换。
  已有游标 strict NULL rescan 保留 dormant cursor/lease，不调用 table
  rescan/next；后续非 NULL rescan 可恢复，错误仍禁用 next，close 正常释放。
- Rust text 增加共享实现的 words-or-null/words-strict 两个 SQL 对象：前者
  对 NULL 返回 `<NULL>`，后者返回零行。当前 9 个 service、13 个 contributions，
  build ID 为 `rust-text-table-null-v1`。C SQL 示例 generate_series 显式声明
  NULL_PROPAGATING，保留原先 NULL→空结果语义；公开 C ABI 布局不变。
- 独立测试覆盖 typed/unknown NULL、strict/non-strict、任一参数为 NULL、
  游标 NULL/非 NULL 切换、失败恢复与引用释放；受控 cast 覆盖源/结果 nullness
  四种组合及第二参数 NULL；cast 非法返回 END_OF_STREAM 被当作协议错误，
  不会伪装成空结果。21 项 CTest 与插件 Clippy 已通过；生产构建已完成。
  最终 kernel runner 已从源码重新编译/链接/执行通过，覆盖真实 Rust DSO、
  完整 SELECT、FunctionTable 算子与直接取行 EOF 缓存断言。源码/产物边界、
  12 项 build gate 与 diff 空白检查通过；spec 仍手工装配，不冒充实库验证。
- 完整 optimizer→spec、实库事务/权限/持久读写、SQL-aware table context、
  开放 catalog、深度 hooks 与 Rust/AI/轻量化整体目标继续推进。

### 前一检查点：表函数隐式转换与游标固定引用

- bound table open 接入与标量共享的参数转换执行路径；选择仍由 Rust resolver
  完成。先校验全部输入并取得指定 epoch 的对象/实现引用，再执行直接隐式
  cast，输出由 host 复制并受累计 16 MiB 预算约束。不新增多跳选择策略。
- 游标保留已选转换的对象/代码 lease 和实例，直接 rescan 只重新转换值，不
  查询目录或捕获 loader 裸指针。源参数类型/数量变化被拒绝；转换或 next
  失败后禁用 next，成功 rescan 可恢复。close 后转换与表函数引用全部释放。
- describe/new-open 校验固定 binding 的目录 epoch、对象/owner/generation、
  arity/flags/列数与保留字段；describe 失败清空输出。执行实例改从 implementation
  lease 取得，不错误地假定 SQL 对象 owner 就是实现 owner。已准入游标允许
  持有原代码完成查询；SQL 算子 close/reopen 的重扫仍必须通过新游标准入。
- Rust text 新增严格 bytes-only 的 `seekdb_rust_words_bytes`，与 custom-text
  入口分开，避免遗漏转换仍能通过测试。当前 8 个 service、11 个 contributions，
  build ID 为 `rust-text-table-casts-v1`。原 C ABI 与 GIS C/C++ 实现保持不变。
- 真实 Rust DSO 独立回归覆盖隐式转换、输入所有权、重扫新值/类型变化、非法
  UTF-8 后恢复、NULL、旧 epoch 拒绝、已准入游标继续运行及准确 lease 归零。
  生产构建、21 项 standalone CTest、插件 Clippy、源码/产物边界与 12 项
  build gate 已通过。扩展后的 kernel runner 已重新编译/链接/执行通过，
  完整 SELECT 覆盖 custom→bytes 表输入、bytes 直接输入、空值及 typed
  标量消费，并由真实 FunctionTable 算子验证重扫与超时；spec 仍为手工装配。
- 表 SPI v1 没有 SQL suffix，不伪造 SQL-aware cast 的查询上下文；SQL NULL
  仍提前结束，非 strict 行为、完整 optimizer→spec、实库事务/权限/持久读写、
  开放 catalog、深度 hooks 与 Rust/AI/轻量化整体目标均未宣告完成。

### 前一检查点：Rust 表函数 SDK 与真实动态库

- Public Rust SDK 增加 table descriptor/row/context/service 的 C ABI bindings，
  以及 `Registration::table_function`、借用 `Arguments/Rows/Cell`、owned
  `Cursor` 和泛型 `Service`。复用原注册事务、loader 与 lease，没有增加
  私有宿主依赖或修改公开 C ABI。SDK 当前提供固定 typed signature。
- open/next/rescan/close 在 FFI 内转换错误/panic；游标错误后禁用，成功
  rescan 重建自有状态。EOF、行数与字节预算、粘性 emit 错误、实例归属及
  Drop 释放路径有测试，借用不能提升为 static/跨线程使用有编译失败用例。
  新结构逐字段通过 C/Rust 大小/对齐/offset 对照；不是原生插件沙箱。
- Rust text 动态库新增 `seekdb_rust_words(rust_utf8)`，流式返回 custom
  token 与 int64 ordinal，保留 Unicode 与类型身份。7 个 service、10 个
  extension contributions；manifest/build ID 同步为 `rust-text-table-spi-v1`。
- 已通过真实 loader + Rust DSO 的完整 SELECT 解析、表达式取行/标量消费、
  空串/空白/typed NULL 和重扫回归。最终由真实 `ObFunctionTableOp` 的
  open/get_next_row/rescan/close 驱动，验证超时与重复 close，不绕过
  正常算子状态检查。spec 根据生成表达式手工装配，尚未验证 optimizer
  自动生成该计划；没有把受控 activation guard 冒充实库 catalog/鉴权。
- 完整生产构建（本轮宿主无新增源变更，增量 no-op）、21 项 standalone
  CTest、SDK 测试和 Clippy、插件 Clippy、源码/产物边界检查与 12 项
  build gate 已通过。
  物理算子验证曾遇到链接器崩溃（进程已结束后重试成功）及测试定长数组
  容量初始化错误（已修正）；最终 kernel runner 重新编译/链接/执行通过。
  SDK/插件/runtime 文档已同步，原有 Rust 标量/类型及 C/C++ GIS 回归保留。
- 完整 optimizer→spec 生成、实库事务/持久读写、通用 NULL/隐式参数转换、
  开放 catalog、深度 hooks、Rust 工具及 AI/轻量化目标继续推进。

### 前一检查点：表函数固定绑定与列名解析

- 表函数的对象/版本、实际参数逻辑身份和所有列描述在首次推导固化；后续
  推导、列名查询、codegen 与取行不重新选择或 describe。列引用保留逻辑
  类型，stored 参数插入 decoder，执行经既有 bound cursor API 准入。
- 完整 SELECT 回归先后复现并修复两个生产缺口：替换隐藏 binary 常量后
  缺少 calc metadata 导致类型错误；列名检查错误地走 PL collection/UDT
  路径。插件分支改读固定列描述，损坏绑定不再被覆盖成普通 unknown column。
- 补充 wire 截断/清空、跨 arena PL 复制、物理标记冲突、目录变化不重选、
  列名大小写/缺失/错误传播、cursor rescan/close、输出类型粘性错误与
  in-row LOB 输入解码回归。受控 provider/cursor 不替代实际 Rust 表函数
  DSO、完整物理 FunctionTable 算子或实库持久类型读写验证。
- 完整生产 seekdb 构建/链接、21 项 standalone CTest、最终 kernel runner
  从源码重新编译/链接/执行通过；源码边界、12 项 build gate 与 diff 空白
  检查通过。原有 Rust DSO 表达式、CASE/UNION/完整 SELECT 和 encoder
  回归继续通过。runtime README 与类型身份文档已同步。
- 完整开放 catalog、深度 hooks、自定义 compare/hash/index、Rust SDK/
  工具、AI/轻量化和实库验证目标保持不变；本轮不是整体完成声明。

### 前一检查点：存储编码器 TYPE raw binding 固化

- `PluginTypeEncodeExpr::build` 在表达式构造时选择并验证 persistent TYPE、
  对象/owner/格式/版本和已知输入 epoch；assignment cast 也必须与它同版本。
  私有表达式保存值与有界 binary binding 两个参数，构造全部成功才替换
  调用方输入。NULL 记录真实 TYPE epoch，求值不调用 codec；同格式 stored
  复制保留不重复编码行为。公开 C ABI、每行值布局和持久列格式不变。
- 新增 `read_binding`，`calc_result_type2` 和 codegen 读取并校验保存的
  binding，不再按 SQL 类型名查询目录。检查完整 wire、目标标记、源表示、
  epoch 及直接 cast/type-value 绑定；失败清空输出。执行仅使用 plan
  extra-info，不求值隐藏常量，继续走原有 bound encode 版本/lease 准入。
- kernel 回归新增新建 encoder 版本冲突不替换输入、目录变化后既有绑定
  不重选、provider 不可用仍能读取/codegen、所有截断长度拒绝、目标 epoch/
  stored 源改变拒绝、无目标标记的 binary 不能伪造 encoder、PL 跨 arena
  深复制后释放源 arena 仍可读取和推导。三条完整赋值执行链增加推导、
  codegen 和求值不增加目录查询的断言，继续检查编码/解码/转换次数与字节。
- 完整生产 seekdb 构建/链接、21 项 standalone CTest、最终 kernel runner
  从源码重编译/链接/执行通过；源码边界、12 项 build gate 与 diff 空白
  检查通过。原有 26 组 Rust SQL 表达式、11 组集合 binder 和 14 组完整
  SELECT 解析用例继续通过；runtime README、类型身份文档同步。
- 编码执行证据来自受控 codec provider 和真实内核表达式执行，不冒充
  Rust 插件持久列 SQL 写入/读取/恢复或并发实库准入验证。表函数绑定、
  全部计划依赖/失效、optimizer/物理算子、custom compare/hash、递归 CTE、
  开放 catalog、深度 hooks、Rust 工具与 AI/轻量化完整目标仍继续推进。

### 前一检查点：标量子查询身份与完整 SELECT 解析验证

- 使用真实 SQL parser 和 `ObSelectResolver` 复现了
  `SELECT (SELECT seekdb_rust_text('hello'))` 丢失逻辑类型的问题。
  原标量 query-ref visitor 只复制物理列类型；现在从唯一输出表达式自有
  复制插件身份、表示元数据和 epoch，不重查 Rust registry 或执行数据回调。
  外层固定函数 binding 继续校验逻辑实参与版本，不退回普通 bytes 绑定。
- 缺失物理列元数据、错误输出形状、物理 carrier 不匹配，或带插件标记却
  丢失引用语句时明确失败；改为普通原生输出或非标量集合引用时清除旧标记。
  不修改每行值布局、持久格式或公开 C ABI。
- 真实 Rust DSO fixture 新增 14 组完整 SELECT 解析用例，覆盖派生表
  UNION 消费、嵌套标量子查询、typed NULL、CASE/UNION/LIMIT 组合、动态
  返回类型、原生/EXISTS 查询与缺少转换错误。外层函数直接检查固定 binding
  的逻辑实参 ID；重复推导不增加目录查询，绑定阶段不执行数据回调。
  原有 26 组 SQL 表达式与 11 组集合 binder/投影执行回归继续通过。
- 受控 query-ref 测试覆盖独立元数据复制、陈旧 epoch 被外层函数拒绝、
  缺失引用、物理类型冲突、标量/集合转换和普通原生输出替换后的标记清理。
  LIMIT 解析要求 schema guard，测试复用现有受控空 manager/guard fixture；
  没有放宽生产前置条件，也未模拟真实鉴权或生产 schema service。
- 完整生产 seekdb 构建/链接、21 项 standalone CTest、最终 kernel runner
  从源码编译/链接/执行通过；源码边界、12 项 build gate、diff 空白检查通过。
  类型身份文档及 runtime README 同步。上一轮和本轮均是已验证实现进展。
- 本轮证明完整 SELECT 解析与类型绑定，不证明完整 optimizer → UNION/
  SubPlanFilter 执行、零行/多行基数、相关或行值子查询、全部改写/失效，
  也不证明持久列存储恢复。自定义 compare/hash、递归 CTE、encoder binding、
  开放 catalog、深度 hooks、Rust 工具、AI/轻量化等完整目标继续推进。

### 前一检查点：UNION 投影类型与 Rust 公共类型选择接线

- 将 CASE helper 抽为 `PluginBranchType`，复用 Rust 公共类型选择算法，新增
  `prepare_set` 接入真实 `try_add_cast_to_set_child_list` 和 `gen_set_target_list`。
  各投影列先准备全部逻辑转换/decoder，再发布替换；不增加 C++ 选择策略或
  公开插件 ABI。沿用内核左右查询组逐步合并顺序，不改成全局无序解析。
- 原生物理类型/collation 合并保留；转换包装、NULL 目标值和集合输出保留
  对应逻辑类型/epoch，防止下一层 UNION 把已选择的自定义 NULL 当作 bytes。
  未改变的内置插件函数保留原 callback 类型 ID，原生转换后才使用新类型。
- 真实 Rust DSO fixture 新增 11 组集合 binder 用例：NULL 两侧、同类型、
  隐式转 bytes、纯原生字符串、内置整数/浮点提升、普通结果 DISTINCT、
  自定义 DISTINCT 拒绝和不存在隐式转换。成功用例执行转换后的投影并检查
  codegen/执行不再查询目录，另验证嵌套集合输出与类型元数据自有复制。
  原有 26 组 SQL 表达式用例继续运行。受控 stored fixture 增加 decoder
  插入、版本冲突、递归/自定义去重拒绝和后续列失败不发布前列替换。
- 完整 seekdb 生产构建/链接、21 项 standalone CTest、最终 kernel runner
  重新编译/链接/执行通过；源码边界、12 项 build gate 和 diff 空白检查通过。
  首轮编译修正 `ObIArray` 不支持范围遍历；首轮内核测试修正集合表达式
  `same_as` 指针身份断言，改为验证独立拥有的类型元数据及列索引，不改
  生产等价语义。类型身份文档和 runtime README 已同步。
- 这是 UNION ALL 类型绑定与转换后投影执行接线，不是完整 SELECT resolver
  或 UNION 物理算子的端到端验证。最终仍为自定义类型的去重/交并差比较
  需要真实 compare/hash 协议；插件递归 CTE 需要 anchor 转换契约，目前
  明确不支持，不用 opaque bytes 替代。完整优化器/VALUES/子查询/计划
  失效、encoder binding、持久存储/恢复与实库事务仍待验证和扩充。
  开放 catalog、深度 hooks、Rust 开发工具和 AI/轻量化完整目标保持不变。

### 前一检查点：CASE 结果分支与 Rust 公共类型选择接线

- 新增 host 侧 `PluginCaseType`，在 `ObRawExprDeduceType::visit(ObCaseOpRawExpr&)`
  原有物理类型推导前处理 THEN/ELSE 结果分支。自定义类型或 stored 值经
  provider 调用既有 Rust 公共类型选择器；已知输入 epoch 与选择快照必须
  一致，不新增 C++ 转换选择算法或公开插件 ABI。
- 不同逻辑类型通过已注册的直接 implicit cast 转换；同类型 stored 分支
  复用 type-value decoder，NULL 不虚构 callback。先在临时列表准备全部
  分支替换、校验类型/epoch，再写入 CASE。结果由原 CASE 算子完成物理类型、
  collation 与求值，附加逻辑类型/epoch 标记供外层插件函数消费。
- 纯内置逻辑类型保留内核数值提升规则，不用插件 cast 图代替。原生结果若
  超出现有插件值表示范围，不伪装成 bytes 或承诺完整类型描述。自定义结果
  的重复推导复用既有类型和分支绑定；原执行器继续只执行选中分支。
- 真实 Rust DSO SQL fixture 从 16 增为 26 组：新增同类型分支、隐式转回
  bytes、嵌套消费、NULL、省略 ELSE、未选中分支不执行、纯内置 CASE、插件
  内置结果与 decimal 的原生数值提升，以及无适用转换时的绑定期错误。所有
  成功绑定用例继续断言重复推导和 codegen 不增加 provider 查询次数。
- 受控 stored-column fixture 单独验证 decoder 节点插入、公共类型与 TYPE
  版本冲突时不发布部分替换、恢复同版本后可绑定及重复推导不重查目录。该
  fixture 的单一已知类型选择是协议替身，真实多类型选择由上述 Rust DSO
  用例验证；stored 用例不是实际持久列 CASE 扫描、执行或恢复证据。
- 完整 seekdb 生产构建/链接、21 项 standalone CTest、最终 kernel runner
  从源码重新编译/链接/执行均通过。源码边界、12 项 build gate 和 diff
  空白检查通过；runtime README 与类型身份文档同步。
- 完整目标保持不变。UNION 尚未接线；自定义比较/运算符、任意 typmod/shape、
  全部优化器改写/子查询/计划失效、encoder raw binding 固化、实库事务/恢复
  及开放 catalog、深度 hooks、Rust 工具、AI/轻量化等仍需推进。本轮完成的是
  CASE 结果组合的可执行路径，不是完整 PG 类型系统或整体插件目标。

### 前一检查点：查询类型 epoch 传播与组合约束

- 可选 `PluginExprType` 新增查询期 `catalog_epoch_`，由自有类型复制与等价
  判断保留。非 stored 的插件逻辑值必须有非零 epoch；schema 中的 stored
  稳定身份可以保留零值，使用时再绑定 codec。没有修改持久列格式、每行
  datum/ObObj 或插件公开 C ABI，也不以零值绕过运行时 generation/lease。
- 函数从固定 binding 设置结果 epoch，选择/读取绑定时检查参数和自身标记。
  cast、type-value 同样记录结果版本并检查源/结果与 wire 一致；cast 把源
  epoch 传给 provider 的 expected-epoch 参数。同逻辑 ID 但来自不同目录
  版本的组合会明确失败，不因为物理布局相同而继续执行。
- 编码表达式继承已绑定输入的 epoch，在 codegen 校验源、目标和选定 codec。
  检查发现两条列转换 helper 会重拷贝目标 schema 标记，从而把已知版本清
  回零；已修正为保留编码/输入版本。原有跳过特殊生成列物理转换的分支仍保留。
  encoder TYPE 仍在 codegen 选择，本轮没有宣称全部 codec raw binding 固化。
- kernel fixture 新增非 stored 零版本拒绝、同 ID 不同 epoch 的等价性区分、
  跨 arena 生命周期、两层生成表/alias/exec-param 版本保留、陈旧别名与函数
  结果标记拒绝、显式 cast 的源版本约束、已绑定函数到新 TYPE 的版本冲突、
  编码器 codec 冲突，以及 schema 列转换包装保留 epoch 的断言。
- 最终完整生产 seekdb 构建/链接、21 项 standalone CTest、源码边界和 12 项
  build gate 通过。内核回归首次编译补齐测试使用的类型定义头；随后新增的
  所有权测试误用了已失效局部字符串，改从表达式自有标记复制后再修改版本。
  最终 kernel runner 从源码重新编译、链接、执行通过，含全部新增检查和
  既有 16 组真实 SQL→Rust DSO 用例、重复推导/codegen 不重查目录的断言。
  测试修正没有放宽生产校验，diff 空白检查通过。
- runtime README 与类型身份文档同步。已有投影/别名等路径的版本传播得到
  验证，但 CASE/UNION、任意内置包装、全部优化器改写、子查询及完整计划依赖
  失效仍待接线；实库 schema/事务/持久存储恢复不由这些 fixture 证明。
  开放 catalog、深度 hooks、Rust 开发工具与 AI/轻量化等完整目标保持不变。
  上一轮与本轮均产生已验证实现进展，整体目标尚未完成。

### 前一检查点：插件标量函数 raw binding 固化

- `PluginFunctionExpr` 首次类型推导成功后，用新建 binary 常量替换隐藏名称
  参数，保存版本化 `PluginFunctionExtraInfo` wire。固定对象、generation/
  epoch、结果/实参类型以及 stored 参数 codec binding，不原地修改共享名称
  常量，不增加 SQL 实参数量或公开 C ABI 字段。wire 最大 2 MiB；原有参数、
  codec 数量及单个字段长度边界继续生效。
- 后续 `resolve_raw_binding`、类型推导和 codegen 读取固定元数据，校验实参
  逻辑类型、存储表示、直接嵌套函数/cast/type-value epoch 和存储格式，不
  再选择函数或 codec。未绑定名称探测仍可访问目录，codegen 拒绝尚未绑定的
  表达式。缺少已推导类型标记的用户 binary 参数不能冒充编译器 binding。
- plan extra-info 继续自有深拷贝/反序列化；执行不再求值隐藏名称或逐行解析
  wire，只使用已校验的 plan binding，并沿既有 loader epoch/lease 机制取得
  执行准入。读取历史 binding 不自动刷新目录，停用/换代之后的执行仍可被
  拒绝。绑定校验补充非零 epoch 与非空结果类型要求。
- 受控 kernel fixture 新增重复推导、provider 返回变化或不可用时仍读取原
  binding、子参数类型改变拒绝、逐字节截断拒绝、未推导 binary 拒绝、PL 跨
  arena 常量深复制和 codegen 不重查目录的测试。逻辑参数替换为 stored 参数
  时先验证旧绑定拒绝，再显式构造新表达式测试 codec 选择，不静默改写旧计划。
- 首轮内核回归发现重复推导还依赖原名称参数的 literal 标记，修正为 raw
  路径校验真实常量/参数数量，非 raw 路径保留 literal 要求。最终完整生产
  seekdb 构建/链接、21 项 standalone CTest 通过；kernel runner 重新编译、
  链接、执行通过，新增所有 16 组真实 SQL→Rust DSO 用例在重复推导和 codegen
  前后 provider 查询次数不变的断言。源码边界、12 项 build gate 与 diff
  空白检查通过。没有把受控 fixture 当作实库 schema/事务/恢复证据。
- runtime README 和类型身份文档同步。已解决直接函数的重复绑定问题，但
  `PluginExprType` 本身仍没有 epoch；一般投影/别名/包装、cast 消费函数的
  完整版本一致性、CASE/UNION 及计划依赖失效仍待推进。类型参数、完整内置
  coercion 规则、开放 catalog、深度 hooks、Rust 工具与 AI/轻量化等完整目标
  不变。上一轮和本轮均为已验证的实现进展，整体目标尚未完成。

### 前一检查点：公共类型 SQL provider 桥接与 cast 版本约束

- loader、server runtime、`ObServer` 和 `ObIModuleProvider` 新增公共类型查询
  转发，使用既有 Rust selector/registry 快照，不引入 C++ 选择算法。结果为
  自有类型 ID 和 registry epoch；失败清空输出，未初始化/未启用插件时明确
  返回错误。它是 host 内部 C++ API，不扩展公开插件 DSO ABI。
- cast 解析链新增可选 expected epoch，默认零保持原有独立选择语义。非零时，
  若选定 cast 来自不同快照，返回 `OB_STATE_NOT_MATCH`，binding 保持全零；
  不静默使用新版本转换。原有未找到或参数错误仍为失败。执行阶段原子检查
  epoch、获取对象/实现双 lease 的机制不变，解析检查不代替执行准入。
- 实际 Rust DSO loader 回归覆盖未初始化、空/未知输入、非法参数、公共类型
  选择不保留 code lease、匹配与错误 epoch，以及真实 registry 发布变化后
  旧 epoch 拒绝、新选择重新绑定成功。SQL kernel fixture 经 `g_mp` 调用新
  provider 入口，实际转发到生产 loader/Rust selector，并检查 epoch 约束和
  失败清空输出；没有用 C++ 伪造选择结果。
- 最终 21 项 standalone CTest、完整 seekdb 生产构建/链接通过；随后 kernel
  runner 从源码重新编译/链接/执行通过，含新增 provider 检查及既有 16 组
  真实 SQL→Rust DSO 组合执行。关闭实验插件宏的 server runtime 语法编译、
  kernel fixture 语法编译也通过；编译命令确实匹配唯一生产 main 配置。
  源码边界、12 项 build gate 和 diff 空白检查通过。日志中的 cast -4109
  是预期 stale-epoch 负例，不是测试失败。
- runtime README 与类型身份文档同步。CASE/UNION 尚未消费新接口：检查发现
  当前 `PluginExprType` 没有 epoch，函数在推导/codegen 仍可重新解析 binding。
  下一阶段需要把 raw binding 固定下来并传播版本，再完成公共类型分支转换、
  同类型 stored 解码、结果身份以及内置数值/collation 规则协调；不能仅凭
  新增 cast 版本约束就宣称完整多分支 SQL 支持。
- 上一目标实现轮属于已验证进展；本轮继续产生生产接线与实际测试证据，没有
  缩小目标。开放 catalog、深度 hooks、完整类型/计划失效、Rust 工具及真实
  数据库事务/恢复、AI/轻量化验收等剩余工作保持不变。

### 前一检查点：Rust 公共逻辑类型选择与 registry 接入

- 新增 `resolution/common.rs` 与内部 C bridge
  `seekdb_runtime_resolve_common_type`，从已知输入类型选择公共逻辑类型。未知
  NULL 不约束选择、重复类型不增加权重；只考虑输入之间的直接 implicit cast，
  按各不同源类型的最低转换成本加 1 求和。同类型不调用 self-cast，最低目标
  或所需最低 cast 并列时返回歧义。全未知/空输入返回 NOT_FOUND。
- Rust 先验证全部参数与 cast 元数据，再用有界、可失败分配的向量排序/分组，
  不执行插件回调、不保留输入指针、不递归搜索转换路径。最多 1024 个参数、
  4096 个 cast；64 位累计支持合法 UINT32_MAX 代价。失败时有效输出位置写入
  UINT32_MAX；C++ 适配映射未找到、歧义、非法参数与分配失败并清空结果。
- `ObPluginServiceRegistry::resolve_common_type` 在锁内捕获自有不可变快照及
  epoch，锁外调用同一个 Rust 选择器；返回自有类型 ID 与 epoch，不返回执行
  lease。候选准备但未 promote 时不可见；停用后不再参与选择；换代后即使
  结果 ID 相同，epoch 也不同，后续各分支 binding 不能混用旧快照。
- 5 项新增 Rust 回归覆盖 NULL、重复分支、隐式场景、歧义、非法未使用元数据、
  指针/数量边界和最大代价；穷举 4096 种三类型转换图、每图六种输入排列，与
  独立参考模型核对。C++ registry 回归补充发布隔离、同成本目标、输出清理、
  上限拒绝、停用和换代。新增测试初次构建缺少 execution SPI 常量头，补齐后
  最终完整回归通过，没有放宽生产边界。
- 先前异步构建句柄已不存在，本次重新取得可核对的完成结果：85 项 Rust
  测试与严格 Clippy、21 项 standalone CTest、完整生产 seekdb 构建均通过。
  随后 kernel runner 重新编译/链接/执行，真实 SQL→Rust DSO 的 16 组既有
  组合用例通过；源码边界与 12 项 build gate 通过。这不是新增 CASE/UNION
  用例或真实数据库事务/恢复验证。
- runtime README 与类型身份文档同步。当前 selector 是 seekdb 逻辑 ID 策略，
  不是 PG 完整公共类型算法；内置数值提升、collation、typmod、shape 和结果
  物理布局仍需 SQL 层协调。server/provider 与 CASE/UNION 尚未接线，接线应
  在 `ObRawExprDeduceType::visit(ObCaseOpRawExpr&)` 的物理推导前完成分支逻辑
  类型协调，并复用同 epoch 的 cast/decoder binding；UNION、子查询、任意
  包装与计划失效不能用这一选择器的通过测试代替。
- 完整目标不变：继续开放运行期 catalog、深度 planner/index/executor 等
  hook、完整类型与对象依赖、Rust 开发工具，以及真实事务/恢复、AI 执行和
  轻量化验收。本轮完成的是公共类型选择基础，不是整体插件化目标。

### 前一检查点：Rust 标量声明宏与真实插件接入

- SDK 新增 module-level `scalar_function!`，生成固定结果的 `DEFINITION`、静态
  `SERVICE` 与 `provide(version, capabilities)`。服务 ID 从声明复用到 manifest
  表项；版本、能力、flags、SQL 使用和实例 validator 都由作者显式指定，不额外
  导出动态符号、不修改公共 C ABI、不自动注册或发布 catalog 对象。
- `invoke_scalar` 统一封装 admission→context 校验→`Call::from_raw`→业务 handler，
  全部位于既有错误/panic boundary 内。SQL-enabled 服务要求扩展 context；handler
  接收 opaque instance 与调用期 `Call`，保留 arity/type/NULL 语义的选择。宏不
  自动加 strict/cast 策略，不将 shared state 标为安全，也不替插件修复 panic
  后的共享状态。模块生命周期仍由真实 validator 与既有 runtime 管理。
- Rust text 的普通 count 和 host-SQL count 改用该宏，删除相应手写 FFI wrapper
  和重复服务表构造；typed count overload 仍共享同一个 native service。类型/
  codec/cast 和动态返回类型继续使用底层 SDK，验证两种开发方式可混用。
- 新增 3 项宏执行/元数据回归，覆盖 UTF-8、空串、NULL、host emit 失败、无效
  instance/context、SQL v1 拒绝、validator/handler panic，以及显式版本/flags
  和生成表布局。首次 doctest 揭示声明需要 module scope，已修正为真实可编译
  示例；生成的内部参数/入口名称避免遮蔽常见 handler 名。完整 SDK 测试、
  6 项文档测试、SDK/插件严格 Clippy 均通过。
- 重新构建/审计真实 Rust DSO 后，21 项 standalone CTest（含实际打包/加载）
  通过；随后 kernel 测试重新编译、链接并执行通过，保留全部 16 组 SQL→实际
  Rust DSO 组合用例。本轮不改 C++ 内核源码，复用已完成生产构建且 runner
  新鲜度检查通过；不是新增实库 catalog/事务验证。
- SDK 与示例使用文档同步。该宏不是 Rust attribute/proc-macro，也不分析任意
  Rust 函数来推导 SQL 签名；new/schema/test 工具、SQL 生成、运行期 catalog、
  深度 hooks、完整类型/计划失效与 AI/轻量化等仍属于完整目标。

### 前一检查点：Rust 开发工具的实际打包入口

- 新增独立、无第三方 crate 依赖的 `rust/cargo-seekdb`，支持
  `cargo seekdb package --build-dir ... --target ... --output ... --jobs ...`，
  也支持不全局安装的 `cargo run --manifest-path ... -- package ...`。工具不
  链接 host runtime，也不改变 sql-nio 或独立插件的 panic 策略。
- `seekdb_add_rust_plugin` 依据真实声明的库/manifest 路径生成 target-specific
  CMake package recipe。CLI 先执行原有 CMake 构建目标，保留 Cargo resolved
  dependency gate、编译及二进制导出/core-dependency 审计，再复制成库文件与
  `plugin.toml` 同目录的交付结构。不猜测 Cargo 输出路径，不另建一套 manifest。
- 仅允许创建新输出目录；通过 `create_dir` 拒绝已有文件、目录、符号链接和
  并发占用。外部命令不经过 shell，保留空格/分号路径；复制阶段清除 `DESTDIR`。
  失败保留 `.seekdb-package-incomplete`，成功前检查非空普通 manifest 和唯一
  非空普通动态库。不会自动删除失败输出，也不部署到正在运行的数据库。
- 5 项 Rust 单元测试和严格 Clippy 检查通过；新增 `cargo_seekdb` 与
  `rust_plugin_package` CTest。实际包回归检查字节一致性、生产 loader 装载与
  Rust 功能执行、拒绝覆盖、悬空链接、缺少 target、DESTDIR 和构建失败保留。
  该流程分别对 standalone 与顶层生产 CMake 构建运行通过。生产验证针对插件
  目标与打包规则，本轮没有新增内核源文件或重新声明完整 server/SQL 回归通过。
  最终 21 项 CTest、源码边界、12 项 build gate 和 diff 空白检查全部通过。
- 使用说明见 [cargo-seekdb](../../../rust/cargo-seekdb/README.md)，SDK 与 Rust
  示例文档同步。此处 package 是目录结构，不是签名归档、原子发布或断电持久性
  承诺；信任配置的 CMake/Cargo/build scripts，完整 binary/manifest/catalog
  校验仍属于服务端信任边界。
- 工具的 new/schema/test、SQL 生成宏和升级脚本仍待实施；运行期 catalog、
  深度 hooks、完整类型/计划失效、实库事务/恢复和 AI/轻量化验收等完整目标不变。

### 前一检查点：真实 SQL 表达式与 Rust DSO 串联

- 新增 `rust_sql_expression_fixture.h`：SQL 文本经真实 parser/resolver、类型
  推导、codegen、frame allocation 和求值，模块 provider 将请求转发给生产
  loader/registry，实际调用 Rust text DSO。不是用 C++ fixture 返回伪造的
  TYPE/cast/function binding 或计算结果；注册、Rust 选择器、动态结果类型回调、
  object/code lease、执行回调和最终 process-exit shutdown 均走现有实现。
- 共 16 组用例，覆盖 UTF-8 多字节文本、空串、内嵌 NUL、typed NULL、同类型
  转换、转回 BINARY 并截断、构造函数、typed overload、动态 identity 和
  identity-bytes 的隐式参数转换。非法 UTF-8 在 Rust cast 内返回明确错误，
  下游函数不执行；同一模块随后仍能处理合法表达式。断言 provider 的 bound
  function/cast 入口次数、结果以及执行期不重新经过 provider 名称解析。
  loader 内部为函数参数执行的隐式 cast 不计入 provider 的显式 cast 入口计数。
- 提取 `native_activation_fixture.h` 复用已有装载测试的授权/提交替身；这些
  替身仍不证明持久目录、权限、事务提交、回滚或并发隔离。Rust 类型未声明
  PERSISTENT，本轮不伪造持久属性来测试真实列存储。完整实库、schema service、
  数据写入和恢复仍未验证。
- kernel runner 每次通过真实 CMake 目标构建并审计 Rust DSO，再显式执行 native
  子目录安装规则，装入独立临时目录。避免只凭缓存库文件存在就认为产物最新；
  `EXCLUDE_FROM_ALL` 下未被顶层安装包含的库和 manifest 也得到实际安装验证。
  overlay-only 模式不加载 Rust 插件。本轮没有修改生产内核源码，复用已完成
  的生产构建及编译/链接参数，runner 的生产新鲜度检查仍生效。
- 首轮测试缺少纯虚适配方法/C++20 字符串类型处理，修正后又发现 fixture 到
  codegen 前才建立执行上下文，而内置 CAST 类型推导已要求它存在；已将上下文
  注册提前到解析和 formalize 前，没有放宽生产检查。最终包含全部 16 组用例
  的 kernel 回归从源码重新编译、链接、执行通过。共享 fixture 后 19 项
  standalone CTest 通过；Rust binary/source 边界及 12 项 build gate 通过。
- 这补上了真实表达式到实际 Rust 插件的组合执行证据，不是完整插件目标完成。
  完整类型传播、计划依赖失效、运行期 catalog、深度 hooks、Rust 工具链、AI
  执行契约及轻量化/性能验收仍属于后续范围。

### 前一检查点：命名插件类型的 SQL CAST/CONVERT

- MySQL parser 新增 `CAST(value AS plugin_type)` 和 `CONVERT(value, plugin_type)`，
  支持普通和反引号目标名。目标保留为标识符，不进入源列列表；recursive resolver
  调用 `PluginTypeValueExpr::prepare`，等待源列完成类型解析后再绑定目标 TYPE。
  原有内置 CAST/CONVERT/USING 分支保持不变。本次没有增加限定名、typmod、数组
  目标或任意运行时 grammar 注册能力。
- 目标 TYPE 不存在返回 `OB_ERR_INVALID_DATATYPE`；没有允许的 explicit cast
  返回 `OB_ERR_INVALID_TYPE_FOR_OP`。typed NULL、同逻辑类型值和同类型存储解码
  使用既有 typed-value 路径，不要求注册伪造的 self-cast；跨类型继续复用 Rust
  选择器，不新增 C++ 转换选择算法。
- parser 生成成功且没有新增冲突；包含生产改动的完整 seekdb 构建/链接成功，
  随后 kernel 回归从测试源码重新编译、链接并执行通过。五种入口（立即 builder、
  延迟 prepare、CAST、CONVERT、反引号 CAST）分别验证 NULL/identity/decode/
  cross-cast。SQL 源列测试仍由 fixture 绑定 column，但解析期不查目标目录、不选
  cast 的断言以及后续真实类型推导、codegen、frame allocation、求值均已运行。
- 另有不替换源列、不预造 datum 的 11 组字面量/嵌套 SQL 成功用例，验证命名
  转换、嵌套 identity、转回内置 BINARY、typed NULL/空串被插件函数消费，以及
  构造函数→同类型转换→消费函数组合；断言准确函数/cast 回调次数、结果和执行期
  不重查目录。未知类型和无适用转换另有负例。包含新增组合用例的最终 kernel
  回归已重新编译、链接、执行通过。
  这是真实 parser/resolver/表达式执行证据，但 provider 仍受控，不能等同于实库
  schema service、实际 Rust DSO 和持久存储端到端验证。
- 19 项 standalone CTest、源码边界、12 项 build gate 和 diff 空白检查通过。
  后续仍需完整类型传播、计划依赖失效、真实 DML/恢复，以及运行期 catalog、深度
  hooks、Rust 工具和 AI/轻量化验收；本次命名转换接线不代表完整目标完成。

### 前一检查点：typed-value 与命名目标延迟绑定基础

- 新增 `PluginTypeValueExpr` / `PluginTypeValueExtraInfo`，Query/JIT 编号同步为
  1935，并注册 operator / extra-info 工厂。统一表示 typed NULL、同逻辑类型值
  和同类型存储值解码；不同逻辑源类型仍通过 Rust explicit cast 选择。NULL/
  identity 不伪造 cast 对象或 generation，也不要求插件注册同类型转换函数。
- 目标 TYPE 解析后保存逻辑 ID、epoch 和模式，DECODE 模式另存真实 TYPE codec
  binding。执行期类型无需持久格式即可表达 NULL/identity；stored 源必须匹配
  owner、SQL 名称、物理格式和版本。输出字节归属 query buffer，NULL 不进入
  decoder；解码复用 16 MiB 有界、粘性错误 sink。
- `prepare` 保存自有 SQL 类型名，不访问 catalog、不提前推导源列；第一次类型
  推导调用 builder 降低为已绑定表达式，更新值参数和二进制元数据参数的 calc
  metadata，防止后续隐式转换误解新的表示。再次推导/codegen/执行不重查目标
  TYPE 或重选 cast。仍未增加命名目标的 SQL parser/recursive resolver 分支。
- 版本化 binary UNIS 常量与计划 extra-info 采用逐字段、自有存储；PL 跨 arena
  深复制拥有元数据。普通插件函数、native cast、编码器、嵌套 typed-value 读取
  包装时核对 catalog epoch。互相递归读取 cast/typed-value binding 有 64 层上限，
  不依赖无限递归；完整计划缓存 catalog 依赖失效仍未完成。
- 完整生产构建以及覆盖后续改动的增量重验成功。之后 kernel 完整回归重新编译、
  链接、运行通过：直接/延迟入口 × NULL/identity/decode/cross-cast 四种输入，
  真实 formalize、类型推导、codegen 和 frame allocation，检查准确选择/回调次数、
  运行时 NULL、输出所有权、空串/内嵌 NUL、七类 decoder 错误、目录 epoch 不一致
  时不发布替换表达式，以及无持久 codec 的 typed NULL / stored 解码拒绝。
  还覆盖 PL raw-copy、extra-info 深复制/序列化、全部截断和非法模式/深度。
- 最新 19 项 standalone CTest、源码边界、12 项 build gate 及 diff 空白检查通过。
  新 typed-value 测试仍使用受控 provider、已解析源列和输入 datum，不是 SQL
  命名目标语法、真实 Rust DSO 与存储的端到端证据。下一步把命名 CAST/CONVERT
  解析接入 prepare，再继续完整类型/优化器/实库语义；运行期 catalog、深度 hooks、
  Rust 工具和 AI/轻量化的完整目标保持不变。

### 前一检查点：SQL 显式转换的插件源值接线

- `ObRawExprDeduceType::visit(ObSysFunRawExpr&)` 在源列 postorder 类型推导之后、
  普通 SQL CAST 计算物理转换之前，调用 `PluginCastExpr::coerce_sql_cast`。
  用户 `CAST` / `CONVERT(value, type)` 的插件逻辑源值先通过 explicit 场景的
  已注册 cast 转为目标 core 类型，再保留内核长度/字符集等正常转换语义。
  stored 输入复用已绑定 decoder，不把存储 carrier 直接当成文本或数字。
- 已接通 bytes、signed/unsigned integer、floating 与 geometry 的目标映射；
  date/decimal 等未建立 wire 契约的目标明确不支持。无注册 cast 返回类型错误，
  不回退到原始 bytes 解释。内置逻辑结果仍使用原生转换；重复推导不重选插件
  binding。隐式表示转换不经过此入口，原有赋值编码/LOB 链继续通过回归。
- 新增测试从真实 CAST/CONVERT SQL 文本解析生成表达式，检查解析期尚未进行
  cast 选择；将列占位符接到受控 schema column 后执行真实 formalize/type
  deduction、ObStaticEngineExprCG、frame allocation 与求值。覆盖 stored 和
  非 stored 插件值的 BINARY(5)、BINARY(3) 截断、CONVERT、SIGNED(-19)、
  UNSIGNED(UINT64_MAX)、DOUBLE(3.25) 及运行时 NULL。检查 explicit 场景、
  单次选择、准确 cast/decoder 次数、NULL 跳过字节 decoder；也检查普通内置
  CAST 不访问插件选择器，以及无适用 cast / 不支持目标不会替换输入表达式。
- 完整生产 seekdb 构建/链接成功。随后 kernel 完整回归通过，最终包含 NULL
  断言的测试再次从源码编译、链接、执行通过。standalone 重建后的 19 项 CTest、
  源码边界和 12 项 build gate 全部通过。此处 SQL parser/resolver/表达式为真，
  provider、已解析源列与输入 datum 为受控 fixture；不是实库查询、schema
  service 解析或实际 Rust DSO 与存储的端到端证据。本轮复用现有 Rust selector，
  没有新增一份 C++ cast 选择算法。
- 用户 SQL 的 `CAST(value AS plugin_type)` 仍需命名目标的延迟绑定、typed NULL
  及同类型转换路径。初步名字解析时列类型尚未确定，不能直接调用立即绑定的
  builder 代替该阶段。完整类型传播/计划依赖、实库 DML、运行期 catalog、深度
  hooks、Rust 工具和 AI/轻量化目标不变，不能把这次单向显式转换定义为完成。

### 前一检查点：内部 cast 表达式与完整赋值转换链

- 新增 `PluginCastExpr` / `PluginCastExtraInfo`，同步 Query/JIT 编号 1934 与两类
  factory。隐藏 binary varchar 常量保存版本化的逐字段 UNIS binding，codegen
  复制到计划 extra-info；构建时选择一次，重新推导类型及执行不重选 cast。
  普通优化器复制保留同一 arena 的属性共享语义，跨 arena 的 PL 深复制拥有
  binding 字节；不为所有 ObDatum/ObObj 增加插件字段。
- 编码 builder 对不同逻辑类型/裸 bytes 查找 assignment 场景允许的 cast，并在
  编码器之前插入。stored 源值先通过真实 LOB reader 和 bound TYPE decoder 解码，
  再经 bound cast 转换；保留原 column-convert 的物理 LOB 转换与列约束。无适用
  cast 明确拒绝，不擅自放宽 Rust text 插件自身的 explicit 声明。
- cast 保留 v2 SQL context，输出使用 16 MiB 有界、粘性错误及同步复制 sink。
  未知 NULL 不选 cast，已知源类型的运行时 NULL 进入选定回调；NULL 行跳过字节
  decoder。编译期检查 source decoder、cast、直接消费函数/目标 encoder 的绑定
  epoch。cast 标记为 state function，暂不预计算尚无完整 volatility 契约的调用。
- 完整生产构建发现 const factory 的 allocator 使用错误，已改为读取绑定专用的
  局部临时 allocator。真实内核测试随后发现 `ObObj::is_varchar()` 排除 binary
  collation，而 binding 使用 binary varchar，导致有效绑定全部被拒绝；已改为
  `is_varbinary()` 并加入错误 collation 拒绝回归。没有绕过真实 builder 来通过测试。
- 包含修正的完整 seekdb 构建/链接成功；随后 `kernel_script.py --build-dir
  build_plugin_overlay_verify` 重新编译、链接并运行通过。新增覆盖单次选择、state
  标记、跨 arena 复制、计划深复制/序列化、所有截断、损坏字段/源类型、回调结果
  所有权、七类 cast 错误、七类 decoder 错误和 NULL。存储源与转换 epoch 不一致
  时，构建失败且不发布替换表达式。
- 使用生产 ObStaticEngineExprCG 和 frame allocation，对普通文本、非 NULL 空串、
  内嵌 NUL、600 字节分别执行三条完整链：constructor→encode→column-convert、
  bytes→assignment cast→encode→column-convert，以及真实 in-row LOB column datum
  →decode→explicit cast→assignment cast→encode→column-convert。检查原始结果字节
  和每类回调准确次数；组合转换由明确表达式步骤产生，不是自动多跳 cast 搜索。
- 最新 standalone 重建及 19 项 CTest 全部通过；源码边界与 12 项 build gate
  通过。新内核表达式测试使用受控 provider，不等同于真实 Rust DSO 与数据库
  存储写入的端到端证据。内部 cast 仍需扩展完整 SQL coercion/语法、类型传播和
  计划依赖失效；defaults/prepared/trigger/批量 DML/INSERT SELECT、out-row 和实库
  恢复也仍需推进。运行期 catalog、深度 hooks、Rust 开发工具、AI 与轻量化等
  完整设计目标保持不变。

### 前一检查点：Rust cast 选择与 SQL-facing 绑定桥接

- Rust 新增直接 cast 选择器：3×3 explicit/assignment/implicit 场景规则、已知源/
  目标逻辑类型、最低代价与同代价歧义；验证整份有界快照，不按注册顺序选择，
  不保留指针、不执行回调、不在 Rust 匹配过程中分配。C++ registry 保留同一
  快照的所有权，释放锁后调用 Rust；现有函数参数隐式 cast 执行也接入此选择器。
- 新增 pointer-free `seekdb_plugin_sql_cast_binding_v1_t`，loader 解析返回 source/
  target、使用/声明场景、对象/owner/generation/epoch；失败输出清空。bound 执行
  直接获取选定对象/实现，在同一 registry 锁中核对 epoch 并取双 lease，随后核对
  类型及声明场景。不按行重选 cast、不把零 generation/epoch 当通配；已获取引用
  的调用可以完成。结果 sink 校验/复制仍由调用者负责。
- server runtime/provider 已增加 resolve/execute cast 桥接；关闭插件宏的 runtime
  生产参数语法检查通过。公共头新增 host SQL binding，不改变 plugin manifest、
  execution callback 或 Rust SDK ABI；C/C++ GIS 算法与 Rust text 插件源码未修改。
- 80 项 Rust 单测、严格 Clippy、最新 standalone 重建和之后 19 项 CTest 通过。
  新增 C++→Rust 最低代价/歧义与 epoch/lease 检查；真实 Rust DSO 测试覆盖 explicit
  与 assignment 场景、UTF-8/空串/内嵌 NUL/NULL、错误输出、损坏 binding、真实目录
  变更后旧绑定拒绝和显式重新绑定。此处的 assignment 复用 Rust text 已声明的
  implicit UTF-8→bytes cast，不擅自把其 explicit bytes→UTF-8 声明改成 assignment。
- 首次完整构建在依赖 asn1-rs-derive 的链接阶段发生 linker SIGSEGV；等待原构建
  终止后使用原命令重试成功，最新 C++ registry/loader/provider 与 Rust release
  host 已进入最终 seekdb 产物。没有通过更换工具链或跳过失败目标取得通过结果。
  随后 kernel 完整回归在该产物上重新编译运行通过，覆盖新 vtable 与前一轮的
  编码/投影/完整赋值表达式等接合；这不是新增 SQL 自动赋值 cast 或实库验证。
  最新源码边界、12 项 build gate 与 diff 空白检查通过，当前无待完成构建。
- 这一步不是完整 SQL 赋值：仍需内部 cast raw/runtime 表达式、计划元数据复制/
  序列化、编码 builder 插入、stored 源值解码及 SQL 语法/真实服务回归。运行期
  catalog、深度 hooks、Rust 工具链、AI/轻量化/平台/Bazel 等完整目标保持不变。

### 前一检查点：写入编码与完整赋值表达式内核回归通过

- 新增内部 `PluginTypeEncodeExpr` / `PluginTypeEncodeExtraInfo`，工厂注册和 Query/
  JIT 两份表达式枚举同步增加 TYPE_ENCODE。输入为目标逻辑类型的值，输出为 codec
  返回的 binary varchar；外层保留原 column-convert 来处理 LOB 表示和列约束，
  不让 codec 接管普通 DML 的约束机制。计划持有已解析 TYPE binding，复用有界、
  粘性错误结果 sink，NULL 保留独立存储语义，ABI/Rust codec 接口不变。
- column-schema/column-reference 两个转换 builder 已插入编码并为外层保留 stored
  annotation。INSERT VALUES 的逐行值入口单独插入编码，避免末端 values_desc
  已标注为目标存储列时误跳过编码；同格式的 stored→stored 复制不重复编码。
  普通无插件列快速跳过，不为 enum/set 分配插件目标临时表达式。
- 当前只接通同一逻辑类型、NULL 和同格式存储复制。跨类型/裸 bytes 赋值还需
  正常的 assignment cast 解析与执行，不能把拒绝这些输入定义为最终赋值能力。
  旧实库 type_identity_server 的 raw-hex INSERT 尚不能证明新路径；defaults、
  prepared 参数、trigger、批量 DML/INSERT SELECT 与实库写入仍需补齐和验证。
- 原完整构建及覆盖后续 header/source 修正的增量构建均已确认成功。随后真实
  kernel 回归发现赋值 builder 返回 OB_INVALID_ARGUMENT：编码器声明单参数，
  却只覆盖 calc_result_typeN，内核实际调用未实现的 calc_result_type1。已改为
  正确的单参数 override；包含修正的完整 seekdb 目标再次编译、链接成功。
- 最新 kernel 回归在该生产构建之后重新编译并运行通过。覆盖 v1/v2 schema 与
  column-reference builder、INSERT VALUES hook、NULL 赋值、外层 stored annotation、
  无重复编码、原始 bytes 拒绝、计划复制/序列化、缓冲区所有权及七类错误/NULL
  路径；实际编码结果再经真实 in-row LOB reader 解码后交给消费函数，检查最终字节。
- 测试进一步使用生产 ObStaticEngineExprCG 和 frame allocation，完整生成/执行
  constructor → encoder → implicit varchar-to-LOB cast → column-convert，验证普通
  文本、非 NULL 空串、内嵌 NUL 与 600 字节输入，输出恰为 E: 前缀加原内容，且
  每次只调用一次 encoder。这验证真实列转换/LOB 表示，仍使用受控 provider，
  不是存储写入、out-row I/O 或 SQL server 证据。测试的根节点收集不提前设置
  IS_MARKED；不移除内核正常插入的物理 cast 来满足断言。
- 新增 Query/JIT 插件表达式编号一致性与显式编号冲突构建检查，避免再次遗漏
  同名头文件副本；仅约束插件编号，不强制同步两份头中的无关历史差异。新增
  5 个用例及原 7 个 build gate 用例通过；最新 19 项 standalone CTest、源码
  边界与 diff 空白检查通过。公共 C ABI、Rust codec SDK 及 C/C++ GIS 算法未修改。
- 后续仍需 assignment cast 的编译期选择和精确绑定执行、完整 SQL 类型传播与
  存取链、运行期 catalog、深度 hooks、Rust 开发工具链，以及实库/AI/轻量化/
  平台/Bazel 验证。上述结果不是完整插件化目标完成的证明。

### 前一检查点：插件标量消费持久类型参数

- stored raw annotation 不再一律拒绝。推导/codegen 解析当前 TYPE binding，校验
  persistent 属性、稳定对象/owner/格式/版本，并与函数绑定检查同一 epoch；列中
  的历史 generation 不进入执行。计划为存储参数保存按 index 排序的稀疏 codec
  binding，普通参数不分配列表内容。PluginFunctionExtraInfo 版本为 2，新条目
  逐字段 UNIS 编码并持有自己的有界字符数组，不 memcpy ABI 布局或借用 wire。
- 执行使用现有 LOB reader materialize，然后调用上一轮的 bound decode，再把
  复制到查询临时 allocator 的逻辑值交给标量函数/既有隐式 cast。解码输出累计
  上限 16 MiB，检查类型、NULL/大小/保留字段及单次结果；缺失输出、重复输出、
  插件忽略 emit 错误均阻止目标函数执行。存储 NULL 不进入字节 codec，解码输出
  NULL 保留原有 null-propagating 语义。这个输出上限不是整个 LOB 读取的内存预算。
- 新增内核回归使用真实 in-row LOB 头和读取器、受控 codec 移除 `E:` 前缀，
  函数检查最终 `hello` 字节而非仅检查长度；codec 回调返回前改写原字符串，验证
  host 已持有复制结果。覆盖 schema v1/v2、格式/epoch 不匹配、计划复制/序列化/
  销毁 wire、全部截断、非法参数位置、错误/重复/缺失/超限/NULL emit、codec 状态
  错误和存储 NULL，无运行期重新选择 overload。LOB service fixture 不提供 out-row
  存储，provider/输入行受控，不等于真实 SQL server 或 out-row I/O 证据。
- 最初生产编译发现新容器元素缺少打印接口及聚合初始化告警，已修复；最终完整
  seekdb 构建/链接通过。首个 LOB fixture 因未安装 read service 返回 OB_NOT_INIT，
  codec 调用为 0；补齐测试上下文后，包含最终字节所有权断言的 kernel 完整回归
  重新编译执行通过。standalone 重建及之后 19 项 CTest 全部通过，Rust text
  Cargo/二进制边界审计及 diff 检查通过。未改写 C++ GIS 或公共 Rust 插件 ABI。
- 仍须 INSERT/UPDATE encode、赋值转换、独立列输出、表函数、out-row/实库验证，
  以及 UNION/CASE/优化器完整传播、类型比较/索引/typmod、运行期 catalog、深度
  hooks、Rust 工具链、AI/轻量化/平台/Bazel 等完整目标。本轮只打通标量消费的
  decode 路径，不宣称持久类型全链路或总体插件化已完成。

### 前一检查点：SQL-facing bound codec 桥接

- 原 codec 入口要求内部 `ObPluginExtensionInfo`，不能直接使用 SQL 计划的无指针
  binding。本轮新增 loader `decode_bound_type` / `encode_bound_type`，经
  `ObServerPluginRuntime` 和 server provider 对 SQL adapter 开放；不改变插件公开
  C ABI，不把 registry 对象或函数地址交给计划，也没有另建一套 Rust/C++ registry。
- 检查 TYPE kind、有限长度身份/名称、非零 generation、格式/版本及保留字段；
  按对象/owner/generation 核对当前对象，再核对格式/flags，随后调用已有 codec
  joint-lease 路径。查找后发生停用或换代不能静默换实现；无关 epoch 不视为类型
  格式变化，持久列零 generation 不成为执行通配。错误/异常在 host 边界返回。
- codec 仍接收 v1 context，输出由调用者同步检查/复制。真实 Rust text DSO 回归
  新增 bound 接口 UTF-8、空串、含 NUL 字节往返和 NULL encode，非法 size/kind/
  标识符/格式/flags/reserved/generation、输入/结果 sink 错误及关闭后拒绝。
  回调内读取 loader 状态且检查双 lease；不是并发停用竞态或实库存储验证。
- 最终 standalone 重建成功，之后 19 项 CTest 全部通过；完整生产 seekdb 构建/
  链接成功，随后同目录 kernel 完整回归重新编译执行通过。kernel freshness gate
  新增 provider/loader/server 接口与实现输入，防止新 vtable 对接旧产物。Rust text
  Cargo/二进制边界审计和 diff 检查通过，C++ GIS 算法未修改。
- 这是调用桥接，不是持久列自动读写：stored annotation 仍须转为 plan codec
  binding、接通 LOB materialize/decode 和 INSERT/UPDATE encode，当前 stored
  参数的 OB_NOT_SUPPORTED 尚未解除。完整类型语义、catalog、深度 hooks、Rust
  工具链、实库/AI/轻量化/平台/Bazel 验证继续属于总目标，不以本轮结果缩小范围。

### 前一检查点：投影与引用的逻辑类型传播

- `ObRawExpr` 新增可选的 arena-owned `PluginExprType`，记录逻辑类型、物理类型和
  stored 表示；只有插件类型表达式分配内容与字符串。普通 raw expression 增加一个
  指针，未扩充每行 ObDatum/ObObj 或通用结果类型。复制不借用源 arena，重复设置
  相同身份不再分配；失败输入不覆盖旧身份，reset/复制普通表达式清除身份。
  等价比较区分类型，assign 保持原来的缓存 hash 行为。
- 插件函数类型推导附加逻辑结果身份，生成表列 resolver、alias 和 exec-param
  推导复制该身份。非直接嵌套参数通过 annotation 进入既有 overload 选择，codegen
  核对返回身份并复用计划拥有的类型字符串，不按行重解析。这段 C++ host 接线也供
  现有 Rust runtime/SDK 插件使用，没有新增独立对象目录或改写 C++ GIS 算法。
- 持久列初始化从已有 v1/v2 扩展信息提取稳定身份、owner、格式/版本，标记 stored，
  不使用历史 generation；损坏的已知插件元数据明确失败。stored 列作为插件参数
  仍返回 OB_NOT_SUPPORTED，LOB materialize 和 codec decode/encode 尚待接通，
  本轮不将保留元数据等同于持久类型完整读写。
- 新增真实内核 fixture 覆盖跨 arena 复制/清除/等价、非法类型和字符串、重复推导、
  v1/v2 列 schema 初始化、两层派生表、alias/exec-param、推导/codegen 及投影 datum
  的真实 ObExpr::eval（包括 NULL，无运行时重解析）。provider 和输入行受控，
  不是实库查询或存储扫描。测试中不存在的枚举与未初始化 statement hash 已改为
  内核实际的 T_QUESTIONMARK 和 statement factory，再重编译运行。
- 最终完整生产 seekdb 增量构建/链接通过；
  `kernel_script.py --build-dir build_plugin_overlay_verify` 在该构建完成后重新
  编译并执行完整回归通过。独立测试构建通过，之后 19 项 CTest 全部通过；
  Rust text Cargo/二进制边界审计及 diff 检查通过。build_release 的内核入口因
  未启用 experimental-plugin 被 gate 拒绝，没有以那个目录的旧产物作为证据。
- 尚需 UNION/CASE/任意包装表达式、标量子查询与优化器的完整类型传播，以及存储
  codec、typmod/shape、计划依赖失效。运行期 catalog、深度 hooks、Rust 开发工具链、
  实库事务/AI/轻量化/平台/Bazel 验证仍属于完整目标，没有据此宣告插件化已完成。

### 前一检查点：动态返回类型的 C ABI 与 Rust 回调

- 已有 function descriptor 允许省略固定返回类型，但原 execution SPI 缺少推导
  回调。本轮新增保留完整 v1 前缀的 function service v2、result-type minor 2
  和固定大小的 resolved-type 输出。v1 ABI/固定类型服务及原 SQL-context minor
  保持不变；不把未实现的动态返回类型当成 bytes。
- Loader 在既有 Rust overload 选择完成后，为动态标量取得对象/实现双 lease，
  找到真实 module instance，使用有效参数类型调用推导。typed 签名与执行共用
  相同的目标类型规则；untyped envelope 保留原类型和未知 NULL。推导前后检查
  registry epoch，无 loader/registry 锁执行回调；不提供值、SQL 或事务上下文。
  回调须确定性、无副作用、线程安全；错误、缺失 suffix、非法 size/reserved/ID
  拒绝绑定，成功结果写入现有 SQL binding 与表达式计划。
- Rust SDK 新增 `DynamicFunctionDefinition`、`Registration::dynamic_function`、
  `TypeResolution`、`Call::argument_type`。旧 FunctionDefinition 不变，新增入口
  支持 typed/untyped arity 与既有 variadic 规则，通过同一 init/start 注册事务。
  metadata 借用、结果复制，finish 消耗 wrapper；类型名与数量受限，非 Send/Sync，
  unwind panic 在 C 边界内转 status。没有另建 registry/catalog 或事务协调者。
- Rust text DSO 新增 `seekdb_rust_identity`（保留输入逻辑类型）和
  `seekdb_rust_identity_bytes`（typed bytes 签名），共 6 个服务、9 个扩展对象。
  未知 NULL 选 bytes 是样例的策略；typed custom→bytes 经隐式转换，推导同样得到
  bytes。manifest/二进制的 build ID 同步为 `rust-text-result-type-spi-v2`。
- 独立 native 测试覆盖真实 Rust 推导/执行（bytes/int64/custom/未知 NULL/隐式转换）
  与关闭后拒绝；白盒补充旧服务/非法 suffix、空/超长/非法 ID、size/reserved、
  插件错误和抛出异常的返回契约。SDK 30 项测试与 5 项 compile-fail 通过，
  SDK/Rust text 最终 all-targets clippy 通过；最终独立构建及重建后的 19 项
  CTest 全部通过。完整生产 seekdb 构建/链接及同目录完整 kernel 回归通过；
  Rust 格式与 diff 检查通过。没有运行实库 SQL、并发装卸或跨平台 ABI/性能测试。
- 这是参数类型决定逻辑返回类型的能力，不是完整 PG 多态类型系统。typmod、
  collation/shape、常量值推导、跨参数类型约束、列/派生表传播、计划依赖失效、
  运行期 catalog、深度 hooks、Rust 工具链、实库/AI/轻量化/平台/Bazel 仍属完整目标。
  本轮未改变 C++ GIS 算法，也没有宣称 native 回调是沙箱或完成实库事务验证。

### 前一检查点：直接嵌套标量的逻辑类型与计划绑定

- 原始表达式推导递归读取直接嵌套插件函数的逻辑返回类型，并将其交给既有
  overload resolver；不再把自定义结果一律按物理 varchar 降为 bytes。
  codegen 使用新增 `PluginFunctionExtraInfo` 保存对象/owner/generation/epoch、
  静态返回类型和实际参数类型。该信息只存在于插件表达式，不向所有 SQL 值增加字段。
- extra-info 接入现有工厂、deep-copy 和 UNIS 序列化。字符串独立归属计划 allocator，
  反序列化不借用 wire buffer；参数类型名附带 NUL，执行直接借用，避免逐行复制。
  参数数量/长度/NUL、绑定身份、截断输入和失败重新初始化均作检查；异常转 SQL 错误。
- 执行从已编译信息恢复 binding，不在首次执行时按物理类型重新选择重载。
  嵌套解析 epoch 不一致、codegen 返回物理布局不一致、执行时内外逻辑身份不一致
  会明确失败。loader 原有 generation/lease 与转换校验继续生效；这些检查不等于
  完整的 catalog snapshot 或计划缓存依赖失效机制。
- 内置结果类型从任意后缀匹配改为完整身份与已知 GIS 兼容别名匹配；
  `org.test.int64` 这类自定义名称仍按 opaque bytes 表示。嵌套 bool/int32/uint32
  虽在 SQL 中使用整数 datum，交回插件时恢复各自 ABI 字节宽度。有静态返回类型的
  非 NULL 输出检查精确逻辑类型；emit 错误保持粘性，不能被插件忽略或重复输出掩盖。
- 新增真实内核表达式 fixture：raw expression 推导/codegen、工厂复制、序列化后
  销毁 wire 内容、所有截断长度、非法重新初始化、实际嵌套 `ObExpr::eval`、
  反序列化计划执行、无运行时重解析、输出类型/大小/重复/空回调错误、NULL 和窄数值
  ABI。provider 是受控 fixture，不是假定真实 server/loader 的事务行为。
- 最新完整 seekdb 生产构建/链接通过，同目录
  `kernel_script.py --build-dir build_plugin_overlay_verify` 最终完整回归通过。
  独立 runtime 测试构建通过；最终 CTest 19 项全部通过（含 Rust SDK、真实 Rust
  text 与原 C++ GIS 装载、构建边界检查），diff 检查通过。表达式 fixture 最初存在
  const 结果类型赋值和参数数组初始化问题，修正后重编译执行；不以此前测试二进制
  作为最终版本证据。未运行实库端到端 SQL 或性能/跨平台验证。
- Rust runtime/SDK/真实 Rust text DSO 使用既有接口受益于这段 host 接线；本轮没有
  重写 C++ GIS 算法。尚未完成列/投影/派生表/包装表达式与优化器改写的完整类型传播、
  callable 动态结果类型推导、表函数转换及计划依赖失效。统一 catalog、深度 hooks、
  Rust 开发工具链、实库/AI/轻量化/平台与 Bazel 验证继续属于完整目标。

### 前一检查点：标量绑定的隐式参数转换执行

- 复核发现 Rust overload resolver 已按直接隐式 cast 的 cost 选择函数，但 loader
  的 bound scalar 路径原来把未经转换的原始参数直接交给目标函数。本轮在该路径
  新增按实际签名准备/执行参数转换，复用已有 cast 查询顺序与 Rust 选择语义，
  不自行把显式或 assignment cast 当成隐式转换，也不添加多跳转换算法。
- 调用插件前先完成全部参数签名、候选和对象/服务 lease 的准备；依赖转换时核对
  binding catalog epoch，目录变化则返回状态不匹配，不静默改用新规则。准备后所有
  cast 与目标函数的 generation 固定到结果复制完成；回调期间不持有 loader 锁。
  这是保守失效策略，尚未实现只针对相关对象的失效或自动重绑定。
- 转换结果复制到 host 拥有的参数 buffer，累计上限 16 MiB，检查目标逻辑类型、
  字节长度、指针、输出次数及返回码。错误保持不可被后续 emit 覆盖；缺失/错误结果
  不进入目标函数。未知 NULL 补目标类型；有源类型且需要转换的 NULL 仍交给 cast，
  不擅自假设所有转换均为 strict。SQL-aware cast 保留可用 SQL suffix，上下文的
  scalar emit 仅重定向到参数 buffer；旧服务仍由既有逻辑收到 v1 prefix。
- Rust text 插件增加 utf8→bytes 隐式 cast（cost 1），反向 bytes→utf8 仍只显式。
  manifest 同步第五个服务，init 共注册 7 个对象。真实 DSO 回归验证自定义类型
  绑定到仅接受 bytes 的 sql_chars 后，先转换、再调用 SQL-aware 函数；非法 UTF-8
  和旧 epoch 不进入目标 SQL；typed NULL 转换成功。原类型/codec/显式 cast 回归保留。
- 补充 conversion sink 白盒测试，覆盖复制所有权、重复/错误类型、无效指针/大小、
  byte limit 和 NULL/空值。测试最初误用了 C ABI input value 类型作为 output result，
  编译报错后改用正确结构，不用强制指针转换掩盖；之后独立测试构建通过。
  完整 seekdb 生产构建/链接和同目录 kernel 回归通过，Rust text clippy 通过；
  修正并成功重建之后最终完整 CTest 19 项全部通过（包含新增白盒 sink 断言、
  实际 Rust 隐式转换、SDK 26 项运行/3 项 compile-fail 和原 C++ GIS 回归）。
  格式与 diff 检查通过。
- 另确认 SQL 表达式的类型推导/运行时参数映射仍会丢失部分自定义 logical type ID，
  将其按 varchar/bytes 处理。因此本轮 loader 执行证据不等于嵌套 SQL 已选择正确
  自定义重载。下一步需继续类型身份传播；表函数参数转换、类型持久化/恢复、
  runtime catalog、更广对象与深度 hooks、Rust 工具链、实库/AI/轻量化/平台验证
  仍保留在完整目标内，本轮 C++ GIS 算法不变。

### 前一检查点：真实 Rust 类型 codec 与 cast 执行

- Rust text 动态库新增 `rust_utf8` 类型、显式 bytes→type cast、
  `seekdb_rust_text(TEXT)` 构造函数与 `seekdb_rust_char_count(rust_utf8)` 类型重载。
  共 6 个对象通过同一 init 注册事务发布，4 个实现服务；保留原两个函数签名。
  UTF-8 格式保留空值和内嵌 NUL，非法 UTF-8 拒绝，NULL 不用特殊字节编码。
  manifest 与二进制同步更新 build ID/data-format 声明，不将此样例标记为 PERSISTENT。
- 新增 loader 的 `decode_type/encode_type/execute_cast` host 入口，使用已解析对象
  身份，原子取得对象与实现 lease 后按真实 owner/generation 找到实例。无 loader
  锁执行回调；校验 codec ABI/保留字段/函数指针、输入类型与字节上限、格式身份与
  cast 元数据，过期 generation 和终止后的对象拒绝执行。codec 只收到 v1 context，
  不把 scalar SQL opt-in 擅自应用到 codec。输出由调用方 sink 校验并同步复制。
- 实际加载 Rust cdylib 的回归新增 type/cast 查询、构造函数、类型重载、codec
  往返、显式/隐式候选区别、UTF-8/空值/NUL/NULL、错误输入、host emit 错误和
  generation/format/context fence。emit 内检查至少两个活跃 lease，并重新读取
  loader status，验证此路径不持有 loader mutex；终止关闭后拒绝旧类型/cast 身份。
  新用例最初与外层重复终止关闭，最后一个断言失败；已改为每条路径只关闭一次。
- 修正后完整 CTest 19 项全部通过，包含 SDK 26 项运行测试及 3 项 compile-fail
  检查、Rust 与原 C++ GIS 装载；Rust text 和 SDK 的 all-targets clippy 通过。
  独立构建、Rust Cargo/二进制边界、最新完整 seekdb 生产构建与链接通过；同目录
  `kernel_script.py --build-dir build_plugin_overlay_verify` 完整回归通过，diff 检查通过。
- 新增证据是实际 native codec/cast 执行，不是实库 column storage、SQL CAST
  语法/自动转换、持久化恢复、索引或数据库事务证明；catalog/verifier 仍有受控
  fixture。完整目标中的 runtime catalog、更广对象/深度 hooks、Rust 工具链、AI、
  轻量化和平台/Bazel 验证保持不变；本轮没有修改 C++ GIS 算法。

### 前一检查点：Rust 类型/cast 注册与字节结果

- Rust SDK 新增 `TypeDefinition`、`CastDefinition`、`ImplementationReference` 和
  `CastContext`，通过 `Registration::data_type/cast` 使用现有直接注册事务。
  插件显式选择物理格式 ID/version、转换上下文/cost、flags、服务版本和能力，
  不再只有函数注册入口；原 FunctionDefinition API 不变，复用相同的实现引用构造。
- sys 补齐 type/cast descriptor 和 type codec service 的 C ABI 映射。C/Rust
  布局回归逐项核对新增结构的 size/alignment/offset，以及 type/cast kind 与三种
  cast context 的枚举值。没有修改公开 C 头文件或增加另一套 catalog 写入路径。
- Call 新增按逻辑类型 ID 借用的 `bytes` 与同步复制输出的 `emit_bytes`，供自定义
  类型函数/codec 使用；text 和 int64 helper 分别复用通用字节路径。NULL 与空值
  区分，binary 不强制 UTF-8，输入/输出上限 16 MiB，跨两种 emit helper 共用单次
  发出状态，host 失败后不可再次发出。结果类型是否符合绑定仍由 host 校验。
- 新增动态类型/cast 元数据复制、三种转换上下文与完整服务版本保真、混合类型/
  函数事务失败恰好中止一次、自定义字节往返、NULL/空值、错误及字节上限回归。
  SDK 共 26 项运行测试和 3 项 compile-fail 测试通过；SDK 与 Rust text all-targets
  clippy（-D warnings）通过。独立 runtime 构建成功，重建 Rust cdylib 通过
  Cargo/二进制边界；完整 CTest 19 项通过，包括 Rust 与原 C++ GIS 的实际装载回归。
- 本轮是现有 byte-oriented 类型/codec/cast 协议的 Rust 接入，不是完整 PG 类型
  系统。新类型注册测试使用复制型 host fixture，尚未验证真实数据库自定义类型的
  持久化/隐式转换/索引；布局测试也不等于实际 Rust codec 的端到端执行。更深的
  compare/hash/typmod/operator class/statistics、运行期 catalog context、
  planner/index/executor hooks 以及实库/AI/轻量化验证仍属于完整目标。C++ GIS
  算法、生产 host 与公开 C ABI 未改动，本轮没有重跑完整生产内核回归。

### 前一检查点：Rust SDK 类型化 SQL 与逐行结果

- 现有 host SQL SPI 已支持多参数/多行及 DML，但 Rust SDK 原来只暴露单文本参数、
  单整数结果。本轮新增 `Call::execute_sql` 和 `sql::{Value, Row, Outcome, Error}`，
  支持 NULL、i64/u64/f64、UTF-8 text、binary，参数化 SELECT/DML、逐行结果回调、
  affected/returned rows；同时保留 host status、数据库错误码及原始 consumer 错误。
  继续使用现有 C ABI、host session/权限和事务路径，不增加独立 SQL 执行器或协调器。
- 参数数据在同步调用中借用；结果数值复制、文本/字节按行借用，不默认收集所有结果。
  higher-ranked callback 限制行/单元格借用不能逃逸，Row 不可 Send/Sync，同一 Call
  在 consumer 中不可重入。对需要保留的结果显式复制；Rust 不撤销已执行的插件侧操作。
- 检查 1024 参数/列、SQL/累计参数/累计结果分别 16 MiB、SQL NUL、未知/畸形单元格
  和 UTF-8。行数上限是错误界限，不静默截断；0 行上限可用于不返回行的 DML。
  consumer 错误和 unwind panic 在 C 回调内转换并记住，同一次 execute 中即使受控
  host 忽略错误并继续送行，也不会再次调用用户 consumer 或返回成功。
- 原 `query_i64` 复用通用执行路径，保持原签名和 NULL/整数语义。现有 Rust text
  动态库经过该路径，两个函数签名不变；C++ GIS 算法不变。新增 SDK 文档和组合 SQL
  示例，明确 prepared cursor、异步/后台 SQL、运行期 catalog builder 尚需后续 host API。
- 新增 12 项 SQL SDK 回归；SDK 共 21 项运行测试及 3 项 compile-fail 文档测试通过，
  SDK 与 Rust text 插件的 all-targets clippy（-D warnings）通过。独立 runtime
  构建成功，重建的 Rust cdylib 通过 Cargo/二进制边界检查；加入 `rust_extension_sdk`
  CTest 后完整 19 项通过，包含真实 Rust/C++ GIS 装载、注册及对象/安装协调测试。
  这些 SQL 测试使用受控 host，不是实库事务、权限、取消或多行 SQL 执行证明。
- 完整目标保持：实库 Root/catalog 提交/回滚/并发、完整 PL/SPI、更多对象与
  CASCADE/shell、Server-dev planner/index/executor hooks、Rust SDK/工具链、AI 与
  轻量化/平台验证。本轮未重跑完整生产内核回归，生产 host 源码及公开 ABI 未改动。

### 前一检查点：条件 SQL 源码归属与构建配置

- SQL 共享清单新增 5 个 GIS adapter 和 3 个 Extension runtime 源码的条件归属，
  并用独立引用列表描述关闭 core GIS 时替换的 16 个基线源码。引用不构成第二个
  编译 owner；CMake 按完整路径筛选，不会误删其他目录中同名的源码。实验插件宏
  与 runtime 源码由同一个 profile helper 添加，避免命令编译成禁用 stub。
- CMake emitter、Starlark validator、Bazel 调用参数和独立 ownership checker
  使用同一份数据。checker 通过只读 AST 解析清单，不执行 Starlark；条件源码没有
  被豁免。工作区精确计数为 1159 baseline + 8 conditional + 7 separate = 1174。
  已解决下方历史检查点中独立 ownership checker 不识别条件源码的问题。
- 新增四种 CORE_GIS × EXPERIMENTAL 配置的真实 CMake configure 测试，检查源码
  精确集合、重复归属、宏开关与完整路径筛选；同时覆盖非法路径、过期引用、禁止执行
  清单表达式和失败时保留原生成文件。它们不是四种完整服务器编译验证；Bazel 本轮
  仅共享清单和校验，尚未打通实验插件 runtime 的完整构建。
- kernel runner 的前置检查改为核对真实 SQL 编译参数、源码/对象/flags 和链接时间。
  合法的清单重排可能不改变任何编译/链接动作，不再仅凭 CMakeLists 更新时间拒绝
  正确的 no-op 构建；仍拒绝禁用实验宏、漏编对象及尚未链接的新对象。该检查不是完整
  传递依赖验证，运行 kernel 测试前仍必须先完成同目录生产构建，不能 touch 二进制。
- 条件清单改动后的完整 seekdb 构建与同目录 kernel 回归已通过。当前续轮复测通过
  SQL inventory 12 项、profile/ownership 8 项、kernel metadata gate 7 项以及独立
  ownership 检查。后两组接入独立 runtime CTest；构建检查统一增加
  `plugin_build_boundary` 标签，避免新增检查仅能手工运行。
- 接入后重新 configure，按该标签运行的 5 个 CTest 目标全部通过；Rust SDK 离线
  测试 9 项通过（包含 C/Rust ABI 布局、调用边界和注册事务）。再次完成
  `build_plugin_overlay_verify` 的完整 seekdb 构建及同目录完整 kernel 回归，均通过；
  当前配置为 CORE_GIS=OFF、EXPERIMENTAL=ON，未将此结果推广到其余完整构建。
- 实库 Root/catalog 提交、回滚、并发及完整 PL/SPI 执行仍待验证；更多对象类别、
  CASCADE/shell、Server-dev 深度 hooks、Rust SDK/AI 与轻量化/平台验证仍属于完整
  目标。本轮没有修改 C++ GIS 算法或用构建元数据测试替代数据库行为证明。

### 前一检查点：SQL ALTER EXTENSION UPDATE 接线

- 本轮重试本地 socket 探测，在 socket 创建阶段即返回 `EPERM`；无法在当前沙箱
  启动 disposable server，没有重复启动进程或将受控测试描述成实库事务证据。
- 新增 `ALTER EXTENSION name UPDATE [TO 'version']` 的 parser、statement、resolver、
  executor 与命令分发；按 DDL/write 分类但不隐式提交，拒绝 prepared、内部/嵌套
  session 和活动用户事务。省略 TO 交给 control default_version，不比较版本标签。
- 执行器使用真实 host SQL 服务，在权限/密码检查后调用 `ExtensionUpdatePlan::prepare`
  释放旧 guard 并读取认证源身份，随后使用既有顺序更新。Rust 继续负责版本路径和
  事务顺序，Root 对源 ID/version、owner、逐对象权限重复核验；不重跑 base、不自动
  重试 stale plan。成功变更返回 1 行，同版本 no-op 返回 0 行；提交后发布失败警告。
- 内核测试新增正确/错误语法、prepared/无数据库/权限绕过拒绝、DDL/隐式提交分类、
  statement 字符串所有权、权限提取及未初始化 executor 拒绝；新增 prepare→update
  连续编排的真实 Query 测试，Root 观察/准入仍用受控替身。实库脚本扩充更新 ID、
  版本、新旧成员、no-op、不可达版本、非 owner 及用户事务检查，尚未执行。
- 新 resolver/executor 加入共享 CMake/Bazel SQL 清单，同步固定源计数及精确归属
  测试。7 项清单测试和 77 项 Rust runtime 测试通过；首轮完整构建/链接通过，但
  新 executor 准入回归失败，发现 SQL target 未定义实验插件宏，实际 CREATE/
  UPDATE/DROP 均编成 NOT_SUPPORTED stub。已在 SQL target 补宏，回归扩展为同时
  检查三个真实入口。补宏后的三个 executor 生产参数语法检查、完整 seekdb 重构建/
  链接及同目录 `kernel_script.py --build-dir build_plugin_overlay_verify` 完整回归
  均通过，包含新语法、statement、三个 executor 准入、prepare→update 与全部既有
  安装/更新/权限/PL/SQL/SPI 受控测试。源码清单 7 项复测通过，diff 检查通过。
  额外旧 `sql_source_ownership_check.py` 仍不认识现有
  8 个 CMake 条件编译源码且保留旧计数，该独立检查未通过，未据此宣称所有边界通过。
- 完整目标继续：实库 Root/catalog 提交/回滚/并发与完整 PL/SPI 执行、更多对象与
  CASCADE/shell、Server-dev 深度 hooks、Rust SDK、AI、轻量化与平台验证。

### 前一检查点：顺序 CREATE EXTENSION 安装接线

- Query 安装入口从整批预解析改为完整脚本预检后进入 Root 回调。安装和更新复用
  `ExtensionRoutineScriptResolver` 的逐条拥有式解析与共享顺序驱动，安装 spec
  单独绑定 tenant/database/owner/name/version；更新回调不能冒充安装回调，安装
  不接收更新语句或预先附带的成员。旧预解析入口仍可用，与回调输入互斥。
- Root 在原 Rust 安装协调器的同一非并行 DDL 事务内逐条检查、预留最终 ID/version、
  补齐参数身份、暂存 schema 和显式自动授权，然后解析下一条。全部准入后才调用
  routine/schema 写入，消费预留 token；不新建第二个事务，不提前发布。安装阶段
  也使用历史名称 ACL 清理，避免新身份继承旧权限。补充 Root 只读检查和管理命令
  的目标数据库、内部/嵌套 session 拒绝；普通 routine DDL 不改变。
- 新测试把参数化跨函数 CREATE 用例接到真实安装脚本适配器和顺序驱动，保留依赖
  ID/version 与参数的断言，并覆盖 spec 篡改、更新/安装错用、回调快照生命周期。
  新增真实 Query install 编排测试：替代 Root 命令验证旧 guard 已释放、预检失败
  不进入 Root、准入失败停止后续语句。持久事务/ID allocator 仍为受控 fixture。
- 完整 seekdb 构建、链接、源码边界检查已通过；同目录完整
  `python3 rust/plugin-runtime/tests/kernel_script.py --build-dir build_plugin_overlay_verify`
  回归通过，包含新增安装回调/Query 编排测试和原有 UPDATE/权限/PL/SQL/SPI 测试。
  `cargo test --manifest-path rust/Cargo.toml -p seekdb-plugin-runtime --lib extension_install::tests --offline`
  的 7 个 Rust 安装协调/输入测试全部通过，diff 检查通过。这些受控测试不证明实库
  事务行为。真实 Root/catalog 提交、回滚、并发、完整 PL/SPI 执行、
  SQL ALTER UPDATE、其他对象/CASCADE、深度 hook、Rust SDK/AI/轻量化和平台验证
  仍为完整目标中的后续工作。本轮未改 Rust 算法或 C++ GIS 算法。

### 前一检查点：CREATE 函数体与完整依赖解析

- 内核回归新增真实 CREATE/PL body 解析：新建函数进入私有 schema view 后，
  下一条函数体可引用它并产生依赖。测试补齐实际 SQL factory 注册和受控
  runtime/user/database schema；未注册 factory 时曾出现分配失败被 MySQL
  routine 路径降为警告、CREATE 返回成功的现象，并非测试已证明真实内存耗尽。
- 进一步复现 DROP 后引用仍返回成功：PL resolver 会将缺失/参数不符的引用
  转换为运行时 SIGNAL，并设置 AST 的 `has_incomplete_rt_dep_error`；此前
  router 没有把该标记交给安装方，故不能仅靠返回码或 error-info 判断依赖完整。
- 新增 host-only `require_complete_routine_dependencies_` 解析选项，扩展脚本
  显式开启，经正常 CREATE resolver 传到 PL router。该模式保留函数体解析的
  真实错误，并拒绝不完整依赖 AST；普通 MySQL 路径默认关闭，保留延迟报错语义。
  Query 与 Root 安装/更新准入另拒绝显式携带编译错误的 CREATE 参数。普通警告
  不应变成失败：隔离诊断内只有警告、没有 error code 时清理错误对象状态，
  警告本身继续合并给调用方。不改变普通 ALTER 的既有无 body 重编译行为。
- ENUM 警告用例进一步暴露解析期类型上下文缺口：它通过当前 exec context
  建立 subschema 映射，此前可能报未初始化或污染外层 physical plan context。
  每条脚本现在拥有独立 physical plan context，与 SQL ctx/factory/package
  guard 一同临时切换，退出时恢复外层指针。新增无外层 plan context、已有
  外层 plan context 两种路径及成功/失败后类型映射不被污染的检查。
- 新测试扩充为普通 CREATE 用户、参数化跨函数调用、真实 ID/version 预留入口、
  引用身份与版本 fence、未继承视图的 guard 不可见、DROP 后引用拒绝、顺序失败
  后不准入下一条、默认 router 与完整依赖模式的对比，以及 harmless ENUM 警告。
  ID/version 服务及 schema staging/事务仍为受控 fixture；没有真实 Root commit。
  参数个数错误保留现有 PL resolver 的 missing-RETURN 错误优先级，不用泛化错误
  覆盖真实返回值。生产参数语法检查通过；含新模式的完整构建/链接/边界检查
  通过，补充独立 physical plan context 后的增量构建/链接亦通过。最终同目录
  `python3 rust/plugin-runtime/tests/kernel_script.py --build-dir build_plugin_overlay_verify`
  完整回归通过：全部新断言与既有 guard/PL/SQL/SPI/权限/顺序解析测试一同执行，
  包括 warning-only ENUM 的成功创建及外层类型上下文不被修改。diff 检查通过。
- 这不是完整 PL 编译/JIT/SQL 执行、SPI 内嵌 SQL、实库权限/事务/回滚/并发验证；
  顺序 CREATE EXTENSION 安装接线、SQL ALTER UPDATE、完整对象/CASCADE、深度
  hook、Rust SDK/AI/轻量化与平台验证仍保持在完整目标中。本轮未修改 Rust 源码。

### 前一检查点：解析期显式 routine 授权

- 新增 host-only `RoutinePrivilegeOverlay`，绑定数据库和真实准入身份，由 Root
  在 CREATE/DROP 准入后显式记录；schema stage 或 owner 相同均不构成授权。
  自动授权仅包含 EXECUTE/ALTER ROUTINE，校验当前对象 ID/owner/版本/命名空间，
  已删除 ID 不可复用，记录数量有界。与 schema overlay 共同拥有，嵌套 guard、
  PL/SQL/SPI 现有共享所有权路径同时保留授权视图。
- `get_routine_priv_set` 和真实 routine 权限检查均读取该视图；被替换的名称
  不再叠加旧用户/角色 routine ACL，未修改对象、全局/数据库权限和用户存在性
  检查保持原路径。Root 顺序解析与预解析更新入口统一 stage/记录/检查，移除
  原来仅凭 provisional owner + 自动授权开关绕过 Root 检查的分支。
- 版本预留的 Extension UPDATE CREATE/DROP 在同一外部 DDL 事务清理历史名称
  权限（含 grant option），然后 CREATE 按开关授予新身份权限。即使自动授权
  关闭、或历史普通 DROP 遗留名称 ACL，新对象也不应继承；普通无预留 PL DDL
  策略不变。此持久清理路径仍需实库事务/回滚/并发验证。
- 新增真实 guard/Query resolver 回归，使用受控用户 schema 和 Root 授权记录，
  覆盖 owner 无隐式权限、普通用户 ALTER、角色/他人、DROP/同名重建、关闭自动
  授权、全局/数据库权限、未修改名称、错误身份及共享视图生命周期，也覆盖
  16,384 个身份的容量拒绝和失败输出清理。生产参数语法检查已通过；完整构建/
  链接通过，补充持久清理后再次增量构建/链接/边界检查通过，最新同目录完整
  `kernel_script.py` 回归通过，diff 检查通过。测试实际验证了普通用户 ALTER
  的解析拒绝→显式授权后通过→同名重建且关闭自动授权后拒绝，不依赖 SUPER。
  这不是实库认证、Root/catalog 事务、回滚、并发或完整 CREATE body 跨对象编译
  的证据；下一步继续这些语义/事务验证，再接 SQL ALTER UPDATE。总体目标不变，
  本轮未修改 Rust 源码或 C++ GIS 算法。

### 前一检查点：Root 驱动的顺序解析接线

- 新增 core-only `IExtensionRoutineScript` 和同步顺序驱动；Query 的
  `ExtensionRoutineScriptResolver` 绑定固定 update plan、真实 session/services，
  全脚本预检后按索引解析，拥有所有已返回的参数快照直到 Root 命令结束。
  失败后不重试、不暴露本条输出；保留累计 64 MiB/4096 条限制。
- `ExtensionRoutineResolver::update` 在预检成功后释放调用方旧 schema guard，
  经同一个认证/串行化 Root 命令传递回调；回调和完整预解析数组互斥。Root
  在 catalog 锁定实例/版本/成员后的 admit 阶段循环 resolve→权限/身份/版本
  准入→stage，后一条可见前一条的 schema。普通预解析入口继续保留旧限制。
- 回调路径允许连续 ALTER，仍核对它解析时的 ID/owner/旧 schema version；
  CREATE/ALTER 参数在 stage 前补齐最终 routine ID/schema version。apply
  继续消费已预留版本并沿用 Rust catalog 协调器的同一事务，不在解析回调里
  执行普通 DDL、提交或发布 schema。SQL ALTER EXTENSION 语法执行器尚未接通。
- ALTER 属性修改的 resolver 不重建 body 依赖；Root 为已发布对象从当前 DDL
  事务读取原依赖，为新对象保留 CREATE 的解析依赖，连续 ALTER 复制该集合。
  apply 使用此拥有式集合，避免以空数组清掉原依赖。删除检查也包含本脚本
  新建后删除的 ID，只检查最终存活的解析版本，防止留下悬空引用。
- Root 串行入口增加请求线程内的递归检测；同步解析再次进入该非递归锁时
  立即返回状态错误，正常和异常退出均恢复标记/解锁，不等待自己的 deadline。
- 首次构建发现 `ObIArray` 不支持 range-for，改为索引遍历后完整构建/链接和
  边界检查通过。内核回归两次通过：实际 Query resolver + 顺序驱动 + 真实
  版本预留入口，受控准入/stage，验证连续 ALTER 的属性/版本、DROP 后不存在、
  旧参数所有权、旧版本 fence、预检拒绝、准入失败停止及递归锁异常恢复。
  另以替代 Root command 验证实际 Query update 编排的旧 guard 释放和回调。
  最后补充 Root 参数 ID/版本 stamp，以及包括空更新/no-op 在内的锁定视图
  `validate_view` 检查，防止固定 schema 因无 SQL 而被跳过；后续完整重构建/
  链接/边界检查和最新同目录完整 kernel 回归均通过，diff 检查通过。
- 测试未运行真实 Root/catalog 事务、成功 SQL、回滚、认证、并发或完整 CREATE
  body 编译；ALTER 依赖保留路径目前只有构建验证。当前 schema overlay 本身
  不授权，新建 routine 的自动授权尚未进入解析期权限视图，不能宣称普通用户
  依赖这些新授权的后续 SQL 已可用。接下来补齐临时权限、CREATE 跨对象语义
  及实库事务验证，再开放 SQL ALTER UPDATE。完整对象/CASCADE、深度回调、
  Rust SDK/AI 和轻量化目标保持不变；本轮未修改 Rust 源码或 C++ GIS 算法。

### 前一检查点：routine 版本预留与进程内快照保真

- 新增 host-only `RoutineVersionReservation`：通过真实 schema service 分配，
  绑定 service、底层 SQL schema service、当前活动 transaction 对象、routine
  身份及 CREATE/ALTER/DROP 类别。ALTER 同时绑定旧 schema version/参数数，
  需要时先预留旧参数删除版本，再预留新版本；不假设版本连续。
- 凭据不可复制、可移动，take 每次尝试均消耗且失败清空输出。放弃/失败留下
  版本空洞，不回收；它不是权限、RPC 参数或持久事务 ID，协调器必须在事务
  结束时销毁凭据，不能持有凭据重启同一个 transaction 对象。
- Root 更新准入按脚本顺序持有 CREATE/ALTER/DROP 的版本凭据，apply 经现有
  PL service/operator 使用这些版本，继续沿用同一 DDL 事务、依赖/参数/授权
  和提交后 schema 发布路径。DROP 也预留，避免同名 DROP→CREATE 的 routine
  操作版本顺序反转。普通无凭据 DDL 仍自行分配版本。
- 新回归揭示 legacy routine RPC codec 不含 routine/参数 schema version：
  参数快照后版本为 `-1`，实际 CREATE 被凭据拒绝且没有写入。已在两种
  Extension batch 的进程内拥有式快照中补存这些标量版本；不改普通 RPC
  格式，也不让调用方的数字版本代替 Root 写入凭据。
- 初始完整生产构建/链接/边界检查通过。修正快照版本丢失后，再次完整增量
  构建/链接/边界检查通过；最新同目录完整 kernel 回归和 diff 检查通过。
  测试覆盖 move/重复
  take、身份/服务/活动事务/旧参数不匹配、分配失败/异常/非法版本、DROP
  顺序、实际 CREATE/ALTER SQL 生成及版本复用；验证拥有式依赖快照、实际
  schema-version fence 和 dependency SQL。依赖由 fixture 手工构造，不表示
  已完成跨语句 body 编译；旧版本/tombstone 返回冲突。测试使用受控版本分配与
  记录/失败 SQL transport，不证明持久序列、成功 SQL、提交/回滚或实库权限。
- 当前 Root 入口仍接收完整预解析参数，逐条 resolve/admit/stage 尚未接通。
  版本预留不是依赖解析器；还需顺序解析、临时授权、递归 Root 调用及事务中
  视图演进验证，然后接通 SQL ALTER UPDATE。完整对象/CASCADE、深度回调、
  Rust SDK/AI、轻量化及实库验证目标不变；本轮不改变 Rust 或 C++ GIS 算法。

### 前一检查点：逐条解析的诊断缓冲隔离

- `resolve_statement` 增加同步诊断作用域，每条语句从空缓冲开始。普通 routine
  resolver 仍使用原来的 error-info 收集路径，但不会把外层/前一条语句的警告
  和错误写进当前 routine 的 catalog 参数。失败仍清空 wire 输出。
- 作用域退出恢复原诊断指针，合并当前语句的 warnings/notes；保留其顺序、
  level、code、SQLSTATE、位置、时间字段及 ring 淘汰前的总数。新的 error
  覆盖父缓冲 error，没有新 error 时保留父诊断；支持嵌套、异常退出和空父缓冲。
  合并失败不会将原失败改为成功；原成功则转为合并失败并清空输出。
- `ObWarningBuffer::append_warnings` 不复制 error，拒绝自追加；内存分配失败
  返回状态并保留已追加的有效前缀。未改变普通语句的 warning 收集入口，未
  改变公共插件 ABI、Rust panic 设置或 C++ GIS 算法。
- 增加受控 kernel 用例：直接证明原收集器会读取旧警告，随后实际 ALTER 的
  wire error-info 必须为 NO_ERROR；实际不存在错误回传后，下一条 ALTER 仍
  不受污染。另测嵌套/异常/空父缓冲、100 条警告的 ring 合并、字段保留与
  合并后继续追加。最新 fixture 语法检查、`build_plugin_overlay_verify` 完整
  生产构建/最终链接、插件边界检查及同目录完整 kernel 回归均通过；diff
  检查通过。使用受控内存 schema/session；未运行真实服务端 SQL、事务回滚、
  并发或诊断分配失败注入测试，不能据此声称完整顺序更新已经可用。
- 下一步仍是 Root 事务驱动的 resolve/admit/stage，真实 schema version 与
  依赖绑定、临时授权、全脚本预检及递归 Root 调用检查，再接通 SQL ALTER
  UPDATE。完整对象/CASCADE、深度回调、Rust SDK/AI、轻量化及实库验证目标
  不变；本检查点不代表这些能力已完成。

### 前一检查点：单条 routine 解析与查询/package 上下文隔离

- 新增 `ExtensionRoutineResolver::resolve_statement`：在调用方提供的 schema
  view 上解析一条 CREATE/ALTER/DROP，继续运行普通 resolver、权限、密码过期、
  数据库范围及 SQL mode 检查；返回一条完整 wire-owned operation，失败清空。
  它不分配 ID、不 stage、不写入 DDL，也不把 overlay 当作权限。
- 每次调用独立持有 query/statement/expression/schema checker/package/SQL
  context；同步切换当前 exec context 的 SQL、package 与两种 factory，返回或
  异常退出后恢复旧指针及 session statement type。factory 也需隔离，因为
  编译期常量缓存通过 exec context 的 statement factory 找到 query context。
- 现有 CREATE 安装解析改为复用该入口，每条语句解析资源及时销毁，只保留已
  序列化参数；仍先预检全部安装语句，最后整体生成 batch，失败不暴露部分结果。
  增加累计 wire 大小限制，不扩大安装路径的对象支持集合。
- 首轮完整生产构建/链接通过；随后补充 exec factory 切换，后续完整重构建/
  最终链接和插件边界检查也通过。
  新 fixture 首次语法检查发现测试误传 `ObQueryCtx` 构造参数，修正后通过。
  实际运行先发现会话未初始化系统变量默认值/缓存基值，再发现 fixture 将
  `mysql` 名称错误配给 `oceanbase` 的系统数据库 ID；补齐初始化并改用对应内核
  常量后，同目录完整 kernel 回归通过。失败时 runner 输出私有测试日志尾部，
  不丢失临时目录清理前的诊断。diff 检查通过。
- 新用例实际解析连续两条 ALTER：host fixture stage 第一条后，第二条保留其
  comment 并新增 invoker 属性；实际解析 function/procedure DROP 及 IF EXISTS，
  tombstone 后 ALTER 返回不存在。覆盖独立 wire 所有权、非法索引、跨库、权限
  bypass 拒绝、密码过期、空/已有 package guard、外层 SQL/factory/type 恢复。
  使用受控内存 schema 与手工设置身份/权限的会话；无真实认证、Root 事务、
  SQL 执行/写入、完整 CREATE body 编译或并发验证。临时视图演进由 fixture
  显式驱动，不将它当作已实现的 Extension 自动顺序更新。
- 待接入 Root 事务协调器的逐条 resolve/admit/stage 循环，预留 ID/最终 schema
  version 与依赖绑定、全脚本预检、诊断缓冲隔离与递归 Root 调用检查，再开放
  SQL ALTER UPDATE。此处单条解析不表示完整顺序更新或实库事务已经可用。
  完整对象/CASCADE、深度回调、Rust SDK/AI、轻量化与实库验证仍属原目标。

### 前一检查点：routine ID 预留与 DDL 写入复用

- 新增 host-only `RoutineIdReservation`，通过普通 routine 使用的真实 schema
  ID 分配入口预留；绑定 schema service、database、owner、种类、精确名称和
  standalone namespace。凭据不可复制、可以移动，不是 RPC 数字参数或插件
  权限；body/参数可在解析时补全，最终写入必须保留已预留身份。
- `take` 在身份核对前消耗凭据，失败清空输出；写入或版本分配失败不能复用。
  预留失败不发布凭据，重复 reserve 不覆盖尚未使用的 ID；放弃预留留下序列
  空洞，不回收 ID。普通无凭据 CREATE 仍忽略调用方数字 ID 并自行分配。
- Root 更新协调器在 CREATE 权限/名称准入后持有预留凭据，apply 经现有 PL DDL
  service/operator 消耗同一 ID，继续使用原 schema version、参数、依赖、授权
  和同一 DDL 事务路径。ALTER 仍保留原 ID；没有把预留当作权限或事务证明。
- `build_plugin_overlay_verify` 完整生产增量构建、最终链接与插件边界检查通过。
  新 fixture 语法检查、diff 检查与完整 kernel 回归通过。新增用例覆盖移动/重复
  使用、各身份字段不匹配、非法 namespace/名称、分配失败/异常/非法 ID、schema
  version 失败、预留 ID 从 overlay 到实际 INSERT 的复用及失败后重试拒绝，
  普通 CREATE 仍重新分配 ID。测试使用 ID/version provider 和失败 SQL transport
  替身，实际调用预留/overlay/DDL operator/SQL 生成；不能据此证明持久序列分配、
  成功写入、事务回滚或实库权限。
- 当前更新入口仍接收预先解析的 DDL；本轮提前预留发生在 Root 准入阶段，尚未
  在脚本语义解析前使用。后续须把顺序 resolver 接入该协调生命周期，让新对象
  的真实 ID 参与后续语句解析，补齐逐语句 query/package 上下文和依赖，再接通
  SQL ALTER UPDATE。完整对象/CASCADE、深度回调、Rust SDK/AI、轻量化与实库
  验证目标不变。本轮未改 Rust 源码、公共插件 ABI 或 C++ GIS 算法。

### 前一检查点：SPI 重试/游标拥有式视图与 PL 调用传播

- `ObSPIResultSet` 在语句/游标打开时捕获一次拥有式 overlay，空来源也固定；
  `reset_member_for_retry` 保留 owner，`ObSPIRetryCtrlGuard` 在 retry/fetch
  获取新 guard 后重新附加。完整 reset 释放引用并清除捕获标志，重复捕获/
  附加返回错误，不长期借用先前的父 guard 或从后来 session 状态重选来源。
- `ObSchemaGetterGuard::capture_routine_overlay` 校验 runtime guard 并复制强引用，
  失败清空输出。SPI 对无 overlay 的来源维持普通路径。包表达式、包变量、
  package cursor/allocator 以及直接 PL 子程序调用的新 guard 已继承调用方视图。
- 新回归实际调用 SPI 结果 init、三次 reset_member_for_retry、guard 刷新/恢复、
  完整 reset；覆盖父 guard 释放、新 ID/版本与删除标记、最后一个引用释放、
  空来源重用、重复捕获/附加拒绝。schema manager 为受控内存绑定，无真实 SQL。
  首轮 fixture 错误禁止 ResultSet 的访问保护区 enter/leave，执行时失败；确认
  这是正常构造/析构行为后，改为验证四次进入/退出配对且深度归零，复跑通过。
- `build_plugin_overlay_verify` 完整生产重构建/链接及插件边界检查通过；其结束
  后增量编译新增 PL 直接调用接线并再次链接成功，最新完整 kernel 回归通过。
  fixture 语法检查、diff 检查通过。Rust 源码、公共插件 ABI 与 GIS 算法未改变。
- 此轮只证明受控结果生命周期/guard 恢复与既有 kernel 用例，不证明实际游标
  fetch、重试认证、事务/回滚或并发访问。共享临时视图不是冻结内容快照，跨脚本
  语句保留游标的视图演化还需明确处理。接下来推进真实 routine ID 预留/写入
  复用、逐语句 query/package 上下文与顺序 resolver、SQL ALTER UPDATE；完整
  对象/依赖/CASCADE、深度回调、Rust SDK/AI、轻量化与实库验证目标保持不变。

### 前一检查点：PL 嵌套 SQL 准备的临时视图继承

- `ObSchemaGetterGuard::inherit_routine_overlay` 只共享拥有式临时视图，子 guard
  保留自己取得的 base snapshot；不复制父 guard、权限或锁。校验父子初始化/
  runtime 类型/相同 schema service，拒绝自继承与覆盖已有附件；无视图继承为
  no-op。共享的是同一临时对象视图，不是按语句冻结的不可变快照。
- 内核 `PLPrepareCtx` 新增同步借用的父 guard：静态准备显式传入原 guard，
  动态准备从调用者 exec context 捕获，在切换嵌套 session context 前确定来源。
  `ObSql::prepare_pl_sql` 获取新 guard 后、初始化结果集/解析前完成拥有式继承。
  父 guard 只需活到同步 prepare 调用结束，返回的子 guard 不保留父指针。
  无 overlay 时原路径不变，既有准备阶段的权限设置没有被复制或扩大。
- 新增真实 guard 回归：未初始化/错误类型/不同 schema service/自继承拒绝、
  普通来源 no-op、重复附加不覆写、父 reset 后子查询有效、多级继承、删除标记/
  新 ID/版本查询与普通对象回退，以及最后一个 owner reset 后释放。既有五类
  PL 与 TEXT/PS/PL 计划缓存用例现在通过继承的子 guard 执行，均通过。
  同时覆盖 PLPrepareCtx 默认与显式来源字段；未运行完整 prepare/execute SQL。
- `build_plugin_overlay_verify` 按新头文件重编译相关生产对象，完整 seekdb
  构建/最终链接及插件边界检查通过；fixture 语法检查和同目录完整 kernel 回归
  通过。等待同一构建进程到终态后才链接测试，没有混用旧对象或重启活跃构建。
  本轮未修改 Rust 源码、公共插件 ABI 或 GIS 算法。
- 接下来仍须处理 `ObSPIRetryCtrlGuard` 执行/游标 fetch 的新 guard，以及
  `ob_spi.cpp` 中 package expression/变量/cursor/allocator 的新 guard；cursor
  与 retry 要有独立拥有式来源，不能只长期借用某次 prepare 的父指针。
  顺序 resolver 的 query/package 上下文隔离、真实 ID 预留/复用及 SQL ALTER
  UPDATE 尚未完成；完整对象/依赖/CASCADE、深度回调、Rust SDK/AI、轻量化和
  实库事务/恢复验证继续属于原目标。

### 前一检查点：临时 schema 的 SQL 计划缓存隔离

- 普通 `get_plan/add_plan` 和 prepared/PL `get_ps_plan/add_ps_plan` 入口识别
  routine overlay，在访问缓存服务、快速解析、key/参数修改和节点操作之前绕过。
  get 对空 guard 返回 miss；非空 guard 保留引用并返回错误。底层 add 返回
  `OB_NOT_SUPPORTED`（空对象仍为参数错误），不将未发布伪装为插入成功。
- `ObSql` 同步绕过 lookup/hit 统计和缓存发布；非空输出错误不进入吞错分支，
  `plan_added` 保持 false，batched 多语句退回非批处理路径。解析后的缓存访问
  统计与用于共享缓存的参数化也跳过，临时 CALL 的 SQL ID 使用原 SQL，而非
  未生成的 fast-parser key；普通无 overlay 路径不变。
- 新增真实 kernel 入口回归，覆盖 TEXT/PS/PL 模式下两种 get、physical 与 PL
  对象两种 prepared add 实例、普通 add、非法参数、key/逐出标记/引用保持不变。
  受控 fixture 使用真实对象及未初始化 cache，证明提前绕过缓存服务；不证明
  完整 ObSql 执行、真实并发命中/逐出、非空 guard 所有权或事务隔离。
- 生产构建先通过入口改动，再在前一构建明确结束后补充 parser 统计/SQL ID
  处理并重新构建。最终 `build_plugin_overlay_verify` 完整目标/链接、插件边界
  检查、fixture 语法检查和完整 kernel 回归均通过。Rust 源码与公共 ABI 未改动。
- 新确认的后续接线点：`ObSql::prepare_pl_sql` 会新取 schema guard，目前不继承
  外层临时视图。必须传播拥有式 overlay，并核对嵌套 SQL 的 guard/权限语义；
  不能仅凭共享缓存入口隔离就开放顺序 UPDATE。
- `calculable_expr_results_` 属于 query context，package guard 是独立局部缓存；
  顺序 resolver 必须保证语句间独立上下文及包引用寿命。真实 ID 预留/写入复用、
  SQL ALTER UPDATE、完整对象/依赖/CASCADE、深度回调、Rust SDK/AI、轻量化和
  实库回归继续属于完整目标。

### 前一检查点：临时 schema 的 PL 共享缓存与编译元数据隔离

- `ObPLCacheMgr` 的公开 get/add 入口识别 routine overlay；get 在访问共享缓存、
  definer key 调整和统计更新前返回 miss，add 保留私有对象而不发布。get 若收到
  非空输出 guard，则保留引用并返回调用方不会吞掉的 `OB_ERR_UNEXPECTED`。
  匿名块、独立 routine、package 及子程序编译产物同时标记为不可共享缓存。
- 临时视图编译不再经独立事务修改普通 error/dependency catalog；函数执行编译
  及 package/trigger 编译路径均处理。保留 AST/对象依赖和当前调用的错误信息，
  最终依赖仍需由 Extension 事务协调持久化。
- 新增真实 kernel 用例，使用真实 guard、未初始化 cache 与独立内存上下文对象，
  覆盖 PRCR/SFC/ANON/PKG/CALLSTMT 五个入口、空结果、重复 miss、无发布/引用变更、
  key 与对象统计不变、非法参数，以及独立普通 guard 不继承 bypass。
  这是入口隔离测试，不是已初始化共享缓存中的并发命中/逐出测试；非空输出 guard
  的拒绝分支尚无实际对象所有权回归，完整 PL 编译元数据路径也尚未实库执行。
- 首次生产编译发现 static 子程序编译函数错误使用成员 guard，修正为参数 guard；
  fixture 首次语法检查发现内存上下文宏需要 namespace alias，补齐后通过。
  随后 `build_plugin_overlay_verify` 的完整 seekdb 目标重建与最终链接、插件边界
  检查和完整 kernel 回归均通过。runner 同时检查已知 guard/PL 输入新旧时间，
  不使用旧生产对象支持新测试结论。本轮未修改 Rust 源码或公共 ABI。
- 目标继续保持完整：SQL plan/raw expression 与 package guard 复用隔离、真实 ID
  预留/写入复用、顺序 resolver 和 SQL ALTER UPDATE 尚待接通；完整对象模型、
  依赖/CASCADE、深度回调、Rust SDK/AI、轻量化与真实数据库回归均未被缩减。

### 前一检查点：guard 集成回归与独立全量构建通过

- 新增真实 guard/内存 schema manager 的受控集成 fixture，覆盖附加前的 base
  查询、附加后的名称/ID/存在性/版本覆盖、删除不回退、未覆盖键回退、同名新 ID、
  独立 guard 不受影响、reset 释放 shared ownership，以及错误 guard 类型/重复
  附加拒绝。fixture 只绑定测试内存 manager，不初始化真实 SQL/schema 服务，
  不证明生产 guard 获取、DDL 锁、权限或数据库事务。语法检查及实际 kernel 执行均通过。
- 新增 kernel runner 的已知 guard 输入时间检查；旧生产二进制或缺失二进制在
  编译/链接测试前被拒绝，已实际验证旧 `build_release` 返回该错误。此检查不是
  完整依赖审计，也不能用作构建进程已停止的证据；仍必须等待权威构建句柄结束。
- 保留先前状态无法恢复的 `build_release`，新建独立目录
  `build_plugin_overlay_verify`，相同 RelWithDebInfo/toolchain/plugin 配置从头构建。
  初次构建和单独 Rust 目标均以 rust-lld 链接构建脚本段错误结束；加入命令级
  `RUSTFLAGS="-C link-arg=-Wl,--threads=1"`（保留已有 flags）及
  `CARGO_BUILD_JOBS=2` 后，Rust host 构建通过，未改源码或 panic 策略。
- 独立 Rust runtime 的 77 项测试通过。独立目录的完整 C++ 生产构建/最终链接
  及插件依赖边界检查通过；使用同一目录全部重编译的对象运行完整 kernel 回归
  通过，包含新增 guard 分发/隔离/引用释放，以及既有包交付、解析、更新计划、
  参数所有权、Root/runtime 和 catalog 受控回归。新目录的 overlay-only 专项
  复跑也通过，包含完整深拷贝与记录/字节配额；没有混用旧 guard 布局对象。
- 后续完整 kernel 命令使用 `--build-dir build_plugin_overlay_verify`。
  `build_release` 仍不是本轮布局变更后的已验证产物，不能因另一目录构建成功
  就把它当作最新基线。此次未运行真实数据库 SQL，不把受控内存 manager 的
  guard 测试当作真实事务、权限、回滚/恢复或完整插件 SQL 更新能力的证明。
- 下一步保持：完整构建与 guard 集成执行后，补齐真实 ID 预留/写入复用、
  provisional PL/plan 缓存与相关 schema 副作用隔离，再实现顺序语义 resolver。
  SQL ALTER UPDATE、其他对象、完整依赖/CASCADE、深度回调、Rust SDK/AI、轻量化
  和真实数据库验证全部继续属于原目标。

### 前一检查点：routine 顺序解析的临时 schema 视图

- 新增核心 `RoutineSchemaOverlay`：独立深拷贝完整 routine schema，按内核名称
  规则提供名称/ID 索引；区分未覆盖与删除标记。同名重建需新 ID，重复修改保留
  所有旧 schema，使已借用指针不因后续语句失效。容量/索引失败不发布半份结果。
- 在底层 `ObSchemaGetterGuard` 接入 routine 名称/ID/存在性/版本点查询，覆盖
  普通 resolver 和 PL 直接取 schema 的共同入口；只允许 runtime guard 附加一次
  shared ownership，reset 释放。不改变默认无 overlay 查询和全局 schema 状态。
- overlay 专项执行已通过，覆盖 wire 等价深拷贝、参数/正文所有权、旧引用存活、
  删除/重建/冲突、名称空间、非 ASCII 名称与记录/字节配额。首次测试因 fixture
  缺少正常 routine subprogram 字段失败，补齐完整 schema/parameter 字段后通过。
  最新 guard 源文件按生产编译参数的语法检查也通过。
- **完整生产重构建与 guard 运行时集成尚未验证。** guard 布局变化后，不能把
  新头文件编译的完整 kernel 测试和旧生产对象混用。本轮仅运行不实例化 guard
  的 `--overlay-only` 模式，不以其证明实际顺序解析/事务/权限/缓存隔离。
- Extension orchestration 尚未附加该视图；后续必须接入真实 ID 预留及写入复用、
  PL/plan provisional cache 隔离、完整顺序 resolver、权限与依赖，再开放 SQL
  ALTER UPDATE/新对象及重复 ALTER。完整对象类别/CASCADE、深度回调、Rust SDK/
  AI 契约、轻量化和实际数据库验证保持原目标，不以这个 routine 视图替代。

### 前一检查点：更新安装身份与 Rust 脚本计划绑定

- 核心 `ExtensionUpdatePlan` 将已安装 ID/原版本、owner/module 观察、Rust 固定
  起点更新路径与内核语法树保存为独立拥有的计划。显式目标及 control 默认目标
  固化到同一个请求；同版本 no-op、空脚本更新和有序 DROP/CREATE 均保留语义。
- `prepare` 在 Root 读取前释放旧 guard，并拒绝权限绕过、不合适的会话和缺少
  数据库；读取期间数据库或 SQL mode 变化则失败，不静默重读/重选版本。
  读取/选路/解析/身份校验失败清空完整计划，当前更新不暗中更换 native module。
- 新增真实 kernel 回归已通过，覆盖上述输入所有权、目标选择、错误清空、非法
  ID/module 和 Root-observation 边界；复用生产对象和实际安装的包文件进行链接与
  执行，没有重新启动先前输出状态丢失的生产构建。本轮没有修改 Rust 源码，更新
  路径选择实际经过现有 Rust reader。该证据不等同于本轮完整生产重构建成功。
- 最新 kernel 复跑包含观察异常/分配失败清理及非法版本/module 长度用例，全部
  通过；更新计划源文件按生产参数分别开启/关闭实验宏的语法检查均通过，已有
  16 项独立 CTest 复跑全部通过。测试 runner 已明确标注受控传输/Root fixture
  的范围，不再以“无 mock”笼统描述真实内核链接测试。
- `ready` 不是语义准入或更新权限。Root fixture 不执行认证/数据库 SQL；尚未
  验证真实更新成功、owner/SUPER 准入、隔离/锁、回滚或恢复。后续必须接入事务
  感知 resolver、provisional 跨对象/重复 ALTER 解析与用户 `ALTER EXTENSION UPDATE`。
  完整依赖/CASCADE、其他对象、深度扩展点、Rust SDK/AI 契约与实库验证目标不缩减。

### 前一检查点：已安装版本读取与更新起点观察

- 已有 updater capability 新增 `read_update_source`，单条 SELECT 读取安装 ID、
  owner、版本和逻辑 native module；不读取成员、写入、开始/结束事务或装载代码。
  Root 提供同 capability 的认证入口，在选定数据库内核验 owner/SUPER；不返回
  半份或未授权的 staged 结果。调用方须遵守 Root 内部串行化与旧 guard 释放契约。
- 规划读取与 DROP/UPDATE 锁定路径共用实例解码及 Rust 校验；后者保留事务、
  FOR UPDATE、预期 ID/原版本校验与成员锁。规划结果是可过期的独立拥有式观察，
  必须绑定到后续请求，不是永久有效的更新许可或完整成员快照。
- 完整生产构建/链接、源码边界、Root 开/关实验宏语法检查与 16 项独立 CTest
  通过。最终实际 kernel 回归通过：真实 catalog + Rust 校验搭配受控行/传输，覆盖
  完整返回/所有权、空/重复/损坏数据、读取/迭代/关闭失败和无写入/start/end。
  包含保留 Rust 内存错误分类后的最新代码及新增非法 tenant/database ID 测试，
  既有包交付/解析、参数所有权、Root 和 runtime 生命周期回归全部保留。
- 受控结果集不执行数据库 SQL，Root 新用例仅覆盖未初始化拒绝/清空；**不能证明
  实际数据库快照/锁、owner/SUPER 成功准入、并发更新或回滚/恢复**。
- 下一步仍需把该观察绑定到 Rust 更新脚本选择和事务感知 resolver，再完成 SQL
  ALTER UPDATE；provisional 跨对象解析/重复 ALTER、完整依赖/CASCADE、其他对象
  与深度扩展点、Rust SDK/AI 契约及真实数据库验证继续属于完整目标。

### 前一检查点：Root 更新命令与 Observer capability 生命周期

- 新增 Query→Root `update_extension_routines`，由 LocalManagement 内部串行化，
  锁内重新提取真实会话权限/角色并调用既有 schema updater / catalog / Rust 协调器。
  拒绝已有事务、内部/嵌套会话及不匹配的数据库/安装身份；所有失败前置路径清空输出。
  不按空脚本提前成功，不绕过 catalog 的固定 ID/原版本锁定检查。
- Observer 注入并撤销同一 catalog 的 updater capability；命令局部 shared ownership
  覆盖 commit 与 schema publication。三类接口共用同一控制块，无第二个 catalog。
  撤销并不等于排空请求，SQL client 生命周期仍须遵守原有 shutdown/drain 契约。
- 新增 kernel 测试覆盖真实 Query/Root 未初始化拒绝、输出清空、三类 capability
  引用/撤销，以及真实 Observer/catalog 的失败/成功/重复初始化、销毁与保持引用；
  这些初始化只绑定 SQL client，不执行 SQL/schema bootstrap 或恢复。
- 完整生产构建/链接、源码边界、最新 kernel 回归、关闭实验宏的 Root/Observer
  语法检查和 16 项独立 CTest 全部通过。kernel 实际执行了上述新增初始化/引用
  测试，并保留已有包交付、解析、参数所有权及事务错误路径回归。不能将生命周期
  测试当作真实更新权限、并发串行化或数据库提交/回滚证据。
- **已安装版本查询/计划绑定、更新语义 resolver、SQL ALTER UPDATE 仍未完成**；
  provisional 跨对象解析、重复 ALTER/依赖图演化、其他对象类别、完整 CASCADE、
  深度扩展点、Rust SDK/AI 契约及实际数据库验证仍属于完整目标。

### 前一检查点：具体 routine updater 与事务内权限读取

- 新增 `ObPLDDLService::update_routines_extension`，通过已有 catalog/Rust 协调器
  驱动一个非并行 DDL 事务，锁快照后进行 owner/SUPER、普通 routine 权限、只读、
  schema fence、对象和已记录的 typed dependency 准入。暂未接 SQL/Root 用户命令。
- 名称视图使用内核比较规则与独立函数/过程名称空间，保留删除 tombstone，
  顺序处理 CREATE/DROP/单次已发布 routine ALTER。实际调用正常 DDL 外部事务接口，
  保留未修改成员，返回完整最终集合；不收编普通外部对象，不解除其他包成员保护。
- 发现并修正外部事务内自动授权/撤权的旧 schema-cache 读取问题：可选择同事务
  锁定权限行，读取前序 DDL 的真实写入，再沿正常 SQL service 写历史与 operation。
  读取/关闭错误和不合法数据不能当作无权限；普通独立命令默认路径保持不变。
- 新建/已 ALTER 对象的再次 ALTER，以及通过重新定义移除旧依赖，仍需事务感知
  resolver/完整依赖图；当前明确拒绝，不能用过时的完整 routine 参数覆盖前序修改。
  这些是待解除的实现限制，不是最终设计缩减。详细契约见 [更新实现](plugin-extension-install.md)。
- 完整生产构建/链接、插件边界、16 项独立 CTest 和最新 kernel 回归全部通过。
  实验宏开启的 adapter 初次语法检查发现 ObIArray 无 range-for 接口，已修正；
  独立编译暴露 DDL operator/权限 SQL service 的隐式头文件依赖，补齐后分别通过。
  新增 kernel 用例验证真实 updater 未初始化拒绝、事务权限 reader 未启动拒绝/清空，
  以及故障传输下实际 SQL 构造的标识符转义、FOR UPDATE、读取超时透传与清空。
  故障传输不是数据库；**没有真实成功 schema 更新、权限/依赖、回滚/恢复证据**。
- 完整目标仍包括更新语义 resolver、Root/ALTER UPDATE、provisional 跨对象解析、
  全部对象类别/依赖/CASCADE、深度扩展点、Rust SDK、AI 契约及真实数据库验证。

### 前一检查点：有序 routine 更新参数与外部 ALTER 事务

- 新增 core-only 混合 CREATE/DROP/ALTER 操作视图与拥有式 update batch，使用
  普通 DDL 完整 wire codec，保留所有字段与顺序，不把同名 DROP/CREATE 去重。
  支持空更新、自引用赋值，失败清空；最多 4096 项、合计 64 MiB wire 数据。
  不把参数所有权等同于语义解析、授权或 schema 执行能力。
- 普通 ALTER 增加外部 DDL 事务模式，包括 MySQL 转 replacement 的分支；
  不自行开始/结束事务或发布，拒绝未启动/parallel 事务；普通独立调用不变。
- 已加入真实 kernel 回归：混合参数完整编码等价、源销毁后的所有权、操作顺序、
  自引用赋值、非法形状与失败清空、数量/合计内存限制、两种 ALTER 的未启动事务拒绝。
  首轮生产编译暴露容器诊断接口缺失，测试语法检查另发现 ObString 临时值不适配；
  均已修正。修复后 production 参数语法检查、完整生产构建/链接与插件源码边界、
  16 项独立 CTest、实际 kernel 回归全部通过。kernel runner 使用最新生产对象与
  真实顶层包安装产物，执行上述新增用例并保留此前 CREATE/DROP/更新源解析回归。
  本轮没有改动 Rust 源码；独立 CTest 回归既有 C++→Rust 驱动与 native 插件装载，
  没有以这些测试替代成功 ALTER/schema 更新事务的真实数据库证据。
- **具体 schema updater、provisional 顺序解析、完整成员计算、Root/ALTER UPDATE
  接线尚未完成**。不能以本轮数据所有权和事务入口改造声称真实数据库更新可用。
  Rust 协调器与完整灵活性目标保持不变，详见 [更新实现](plugin-extension-install.md)。

### 前一检查点：Rust 更新事务协调与 catalog 版本写入

- 新增 Rust `extension_update`，复用 DROP 的锁定实例阶段驱动；更新固定预期
  安装 ID，回调不能改成另一个实例。原/目标版本作为非空不透明标签验证。
  同版本 no-op 仍锁定并准入/提交，不执行 detach/apply/record；空 SQL 但版本
  不同的更新仍执行这些阶段。未知提交/回滚保留固定 ID，不自动重试。
- 新增 core-only `IExtensionSchemaUpdater`/`IExtensionCatalogUpdater`。catalog
  锁定完整安装/成员快照并比较原版本后才准入、解除保护；同一个 DDL 事务完成
  schema adapter 调用、完整成员集合写回和条件版本更新。安装 ID、owner、模块
  身份不变；版本 UPDATE 要求精确命中一行；新成员与其他安装冲突仍须回滚。
- updater 必须提供全部存活/新增成员，并负责具体 schema 与权限/依赖语义；
  不能只返回新增 ID，也不能提前提交或发布。catalog 的元数据录入方法保持私有。
  成功返回 ID 与 changed 标记，区分已确认 no-op 和已提交版本变化；未知结果
  不设置成功输出。详见 [更新协调与边界](plugin-extension-install.md)。
- 完整生产构建/链接、源码边界检查、77 项 Rust 单测与严格 Clippy、16 项独立
  CTest、最新真实内核回归均通过。测试区分事务模型与实际 kernel 准入，**未证明
  真实 catalog/schema 更新、授权、回滚、并发或恢复**。
- 仍需具体 schema update adapter、已安装版本查询/计划绑定、Root 注入与
  `ALTER EXTENSION UPDATE` 前端；不能把此 capability 当作用户 UPDATE 已可用。
  provisional 对象解析、完整依赖/CASCADE、深度扩展点、Rust SDK/AI 执行契约
  及真实数据库验证继续属于完整目标。

### 前一检查点：Rust 固定起点更新源与内核解析

- Rust 包读取新增独立 update 入口，只搜索从预期已安装版本出发的有向更新边，
  不重放旧基础文件、不以目标直接安装替代更新路径；支持更新文件独立交付和
  显式降级链。保留原有版本图/目录/合计字节限制与路径、UTF-8、control 检查。
- `ExtensionPackageSource` 增加 `from_version_`；C++ 对首项起点、相邻步骤、
  最终目标及 no-op 形状再次验证。新安装时该字段为空，更新起点不能为空。
  Rust opaque handle 新增字段与读取入口，仅属于 host bridge，未修改公开插件 ABI。
- 同版本更新源返回零脚本；空白/注释/零字节更新文件是合法版本边。基础文件仍须
  非空白，缺失更新文件/路径不能当作 no-op。空更新也不绕过版本专用 control 拒绝。
- `ExtensionScript::load_update` 逐文件调用真实内核 parser，可解析更新中的
  DROP/CREATE 等正常语法，支持没有对象更改的更新；失败清空源、版本与所有语法树。
  现有新安装 resolver/installer 明确拒绝更新计划，防止错误复用安装身份与提交协议。
- 完整生产构建/链接与源码边界检查通过；70 项 Rust 单测、严格 Clippy、16 项
  独立 CTest 通过。真实内核回归已通过：实际交付的 text_ops 1.0→1.1 只解析新增
  函数；更新的 DROP/CREATE、空白/零字节/注释/no-op、跨文件错误与 4096 拆分
  项合计限制、失败清空、拒绝进入新安装路径均已执行，已有 CREATE/DROP 回归保留。
- **这不是 ALTER UPDATE 命令已完成**。下一步仍需锁定安装 ID/原版本、权限与
  依赖准入、事务内成员增删改及版本写入、schema 发布与未知结果处理，再接 SQL/
  Root 命令。真实数据库更新/回滚/恢复、provisional 解析、深度扩展点、完整 Rust
  SDK 和 AI 执行契约继续属于完整目标，不能用源读取/解析通过替代这些证据。

### 前一检查点：DROP SQL 与 Root 移除命令接线

- 新增 `DROP EXTENSION name [RESTRICT|CASCADE]` parser、statement、resolver 与
  executor。默认 RESTRICT；CASCADE 会由当前 adapter 明确拒绝，尚无级联实现。
  语句分类为写 DDL，但不隐式提交；拒绝 prepared、已有事务、内部/嵌套执行和
  权限绕过。命令深复制名称及数据库上下文，进入 Root 前释放旧 schema guard。
- Observer 把同一 catalog 实例的 `IExtensionCatalogDropper` 注入 Root；命令持有
  shared ownership 并内部串行化，重新提取真实 session 权限和角色，校验目标库。
  对象 owner/SUPER、每个 routine 的 ALTER ROUTINE、只读与依赖由锁快照后的
  adapter 检查；前端不以数据库级 DROP 权限替代这些对象级检查。
- SQL 执行器直接连接已有 Rust 删除状态机，无需 `--extension-dir`、control/SQL
  文件或模块加载。仅确认提交才返回受影响行；提交后发布失败给警告、不重试。
  未知提交保留错误状态，不把它当作成功删除。
- 新增真实内核测试源覆盖语法/分类、无隐式提交、参数所有权、Root 拒绝与输出清空，
  以及安装/删除 capability 共用一个控制块、单独撤销后仍保持存活。生产参数
  语法检查通过，executor/runtime/Root 关闭实验宏的语法检查也通过；parser 生成
  没有报错/冲突。完整生产构建已编译链接成功，源码边界检查通过；最新真实内核
  回归已执行通过，包括上述 DROP 用例及已有 CREATE/包交付/多文件解析用例。
  这些不启动 SQL 服务，不是 schema 删除事务、授权成功或跨会话可见性验证。
- 67 项 Rust runtime 单测、更新后的 16 项独立 CTest 与 7 项 SQL source inventory
  测试通过。真实数据库脚本扩展为 CREATE/DROP，覆盖调用者事务、非 owner、
  owner/SUPER 删除、重建
  身份及双库隔离；当前 socket 创建仍为 EPERM，脚本仅语法检查，未执行数据库回归。
- 完整目标继续包括 UPDATE、IF EXISTS 等管理选项、provisional 跨对象解析、
  Extension 依赖/CASCADE、其他对象 adapter、深度 hooks 与完整 Rust SDK/AI 契约。

### 前一检查点：Rust 整包移除协调与 routine adapter

- 新增 Rust `extension_drop`：PREFLIGHT→BEGIN→锁定快照/准入→DETACH→APPLY→
  RECORD→COMMIT；失败回滚整个事务，未知提交不盲目回滚或重试，固定锁定的安装 ID。
  新增 7 项 Rust 测试，当前 67 项 runtime 单测与严格 Clippy 通过。
- C++ catalog 已实现独立移除 capability，锁安装再锁成员，读取完整且有界的快照，
  支持 expected ID 拒绝同名重建后的旧请求；准入后在同一 DDL 事务中解除保护、
  删除对象与安装记录。detach 是私有帮助方法，不开放独立绕过成员保护的入口。
- routine adapter 校验 owner/SUPER、普通 ALTER ROUTINE 权限、数据库和成员
  状态、只读及已记录的传入 schema 依赖；包内引用不阻挡整包删除，外部依赖
  RESTRICT 拒绝。复用普通 routine 删除的外部事务模式，保留依赖/授权清理。
  不加载模块、不读源文件；当前只支持纯 SQL routine，不支持 native/其他类别或 CASCADE。
- catalog row reader 新增显式错误返回，query 关闭失败也传递；不能把读取失败
  当成空模块或无依赖。增加 C++→Rust 删除事务模型回归，更新后的 16 项 CTest
  全部通过。这不是 catalog 实际 SQL、schema 事务或权限/依赖的数据库验证。
- 完整生产构建仍在进行；期间又收紧了快照字段检查、私有 detach 与空安装只读
  检查，须在原构建终止后再做增量确认。最新生产参数语法检查与 kernel 回归待结果。
- **DROP SQL 前端、Query/Root 命令和 capability 注入尚未接入**；真实删除/回滚、
  Extension 依赖/CASCADE、UPDATE/provisional 解析、深度扩展点和完整 Rust SDK
  仍属于完整目标。详见 [安装与移除实现](plugin-extension-install.md)。

### 前一检查点：Rust 版本路径与多文件安装源

- Rust 新增有向版本图，版本名作为不透明标签；新安装可从基础 SQL 经最短更新链
  到达目标版本，直接安装脚本优先，同长度选择不依赖目录枚举顺序。允许显式降级
  边和环，拒绝无路径、过量版本/目录项及不合法的版本脚本文件名。
- 包源与 host FFI/C++ adapter 改为拥有独立脚本序列，保留 from/to 版本，最终
  Extension 身份仍使用目标版本。SQL parser 按文件边界处理，合计检查 4096 个
  拆分项；路径合计 SQL 最大 4 MiB。任一步失败不暴露部分源或语法树。
- 所选路径的版本专用 control 尚未实现，因此明确拒绝，不静默忽略依赖/权限变化。
  新增 `text_ops--1.0--1.1.sql` 与安装清单；默认版本仍是 1.0，显式安装 1.1 会
  读取基础和新增函数脚本。**不是 ALTER UPDATE 已实现**；数据库内更新/删除、
  provisional 解析、完整依赖与深度扩展点继续属于未完成目标。
- 包含此前 batch/executor/startup ABI 以及本轮多文件源的增量生产构建已链接成功。
  60 项 Rust 单测、严格 Clippy 与更新后的 16 项 CTest 通过，含真实 C++→Rust
  多文件读取。首次 kernel/交付回归发现两项问题，尚未将这次运行计为通过：
  新增 VERSION 关键词影响 VERSION() 函数解析；顶层插件目录 EXCLUDE_FROM_ALL
  使 SQL 文件安装规则未执行。已补函数名规则并将纯 SQL 安装子目录单独接入顶层，
  保留 native 插件按需构建。parser 生成和修复后的完整生产重建均已通过。
- kernel runner 现在调用真实顶层 CMake install 并解析临时安装产物，覆盖两文件
  text_ops 1.1；新增 VERSION()/extension()/限定函数名兼容、跨文件词法边界、
  失败清空与合计拆分上限用例。最终真实内核回归已通过：实际交付的三个文件、
  两文件三 routine 解析、VERSION()/extension()/@@version 兼容，以及此前的
  CLI 路径、DDL 参数 wire 所有权和 Root 前置拒绝均已执行。
- 同时补充真实 parser/factory 构造 CREATE command 的成功用例，销毁 parser
  arena、切换 session 数据库后，命令仍持有原 name/version/database/ID；默认
  与显式版本都通过。权限提取夹具原先使用无效默认用户 ID，已保留拒绝断言并
  用有效身份验证 DB 级 CREATE ROUTINE 需求，未绕过或放宽生产校验。此处没有
  数据库存在性、授权成功、routine 语义解析或事务执行的证明。
- 本轮再次探测 socket 创建仍为 EPERM，因此真实服务安装/回滚回归未运行。
  当前没有仍在运行的生产构建；完整 Extension 生命周期、跨对象/依赖解析、
  Server-dev/planner/index/type 扩展及完整 Rust SDK/AI 执行契约继续推进。

### 前一检查点：CREATE 执行端与解析资源释放

- `CreateExtensionExecutor` 已替换前端的 NOT_SUPPORTED 占位，连接启动配置包源、
  Rust reader、内核 parser/routine resolver、Root 批量 DDL 和 Rust 安装协调器。
  当前支持纯 SQL、新建 FUNCTION/PROCEDURE 的包；不是完整 PG Extension 支持。
- 新增管理员启动选项 `--extension-dir`；CLI 在切换 base-dir 前解析相对目录，
  runtime 只返回其启动配置的副本。未设置时不搜索 cwd，也不自动创建包目录。
  可指向仓库 `plugins/sql_packages` 或安装后的 `share/seekdb/extension`。
- 新增 `ExtensionRoutineBatch`，通过内核现有 DDL wire codec 保存全部参数、基类
  信息、错误/依赖数据和字符串后备存储。成功 resolve 即销毁 parser/PL factories，
  install 在进入 Root 前释放旧 schema guard；不把旧快照持有到 schema 发布阶段。
  普通命令分发对本命令延迟 guard reset，直到其脚本解析和参数保存完成。
- Executor 使用真实 `ObSql` 服务绑定（包括依赖队列）、重新检查权限与会话身份；
  只接受无已有事务的顶层调用。提交成功后发布失败发出带 extension ID 的警告，
  不把已提交安装误报为可重试错误。
- 本轮新增 C++ 源、CLI/runtime 改动及 kernel 测试源已通过生产参数语法检查；
  executor/runtime 关闭实验宏的语法检查也通过。**旧完整构建仍在运行，本轮新增
  source inventory 和 ABI 改动须在其终止后做增量构建，尚无最新完整链接结果。**
- 新增 kernel 回归包含真实命令行路径解析、参数 wire round-trip、源销毁后的
  数据保持与失败清空；仍待最新链接后执行。新增 `extension_install_server.py`
  覆盖双库、权限、调用者事务与成员保护；尚未运行真实数据库测试。上述代码接线
  不等于 CREATE 的端到端行为已验证，更不表示安装事务/深度扩展整体完成。
- 后续核对补充：active transaction/prepared 与未实现的 `requires` 现在返回明确
  诊断；依赖诊断构造处于异常边界内，失败不暴露参数或发起安装。kernel 回归新增
  依赖拒绝与 `OB_TRANS_UNKNOWN` 不进入 DDL 自动重试集合的断言，语法检查通过。
- 修正 SQL Bazel 清单的过期固定数量：实际为 1112 个 Unity 源、28 个 standalone
  源、15 个 parser 源和 7 个单独归属源，共 1162；保留重复、越界、固定数量与
  semantic group 校验。新增 7 项测试执行真实清单与校验函数，已通过；这不是
  Bazel 引擎执行、目标依赖或完整编译的证明。Bazel 实验插件目标接线仍需完善。

### 前一检查点：Root 安装命令桥接与 CREATE 前端

- `ExtensionRoutineResolver::install` 已连接 Query Root command、串行 Rootserver
  批量 routine DDL adapter 与同一份 Rust/catalog 安装协调器；拒绝加入用户已有事务，
  对尚未实现的 control `requires` 依赖明确拒绝，不静默丢弃。
- 核心中立头 `extension_install.h` 定义安装数据与 `IExtensionCatalogInstaller`。
  Observer 注入同一个 catalog 的共享 capability，不暴露 loader；激活/停用 guard
  与安装 capability 共享 catalog 所有权。shutdown 撤销注入，**仍须先停止接入并
  排空请求**；shared_ptr/原子更新不是服务或 SQL proxy 的排空屏障。
- Root 内部串行化，调用方不能再套同一串行锁。提交后 schema 发布抛异常也保留
  已提交身份，通过独立 publication status 报告，避免误报可重试的安装失败。
- 上述 Root/所有权改动已完成完整 `seekdb` 编译/链接和真实内核回归；测试覆盖
  未初始化命令拒绝、输出清空、注入引用存活与撤销释放，**不是成功安装的数据库测试**。
- 本轮继续新增 `CREATE EXTENSION name [VERSION 'version']` 独立语法、statement、
  resolver、类型分类和数据库级 CREATE ROUTINE 权限提取（首个 adapter 的支持集合）。
  语句为写入/DDL，但不隐式提交；拒绝 prepared、权限绕过和未选择数据库。
  新关键词非保留，不能破坏同名列或 VERSION()。parser 生成已通过；包含这些前端
  改动的完整生产构建和新增内核回归仍待结果，不能借用前一版本的通过记录。
- 15 项独立 CTest 本轮重新构建/运行通过；新增 kernel 回归源已通过生产编译参数
  的语法检查，实际执行需等最新生产链接结束，尚未记录为通过。
- **用户命令执行端仍明确返回 NOT_SUPPORTED**，待连接可信包源选择与 schema guard
  的解析/释放边界；不能把语法被识别当作已能安装。ALTER/DROP、provisional 名称
  解析、依赖、其他对象与深度扩展点仍属于未完成的完整目标。

### 前一检查点：普通 routine resolver 桥接

- 新增 `ExtensionRoutineResolver`：通过正常 `ObResolver` 调用函数/过程 resolver，
  再执行统一权限、只读和密码过期检查。每条语句独立持有 arena/factories/query
  context，返回可供批量 DDL adapter 使用的参数；失败清空整个输出数组。
- 从 host 绑定的 services 构造正常 resolver params，不继承外层 prepare/PL/restore
  状态；拒绝权限检查关闭、缺失 runtime、native 包、不支持的 SQL 及 IF NOT EXISTS。
  保存并恢复外层语句类型，检查实际数据库与固定 schema。会话和 schema guard 须
  存活至参数消费完成，不把这组 host 内部参数作为公开插件授权接口。
- UTF-8 包文本严格转换到 session parser charset 后重解析；SQL mode 必须一致。
  SQL 审计文本转换回系统字符集，不在默认 SQL mode 下重新切割；数据库名深复制，
  不借用随后可能变化的 session 数据库字符串。
- 包含全部新增桥接与上述修正的完整 `seekdb` 目标已编译/链接成功，源码边界和
  diff 检查通过，15 项独立 CTest 再次通过。真实内核 `kernel_script.py` 回归也通过，
  在原有解析用例外覆盖新 resolver 的不支持语句、IF NOT EXISTS/native 包、缺失
  context、权限绕过拒绝与空输出。**没有把这些前置失败测试算作真实会话成功解析、
  权限/只读/definer 语义或 schema 安装回滚的验证。**
- 尚需 SQL 管理命令和 Query→Rootserver command seam 连接本 resolver 与批量
  adapter；provisional 名称解析、依赖、更新/删除与其他对象类别仍未完成。Rust
  继续拥有包源与安装阶段协调，SQL 语义解析留在既有 C++ 内核，没有另写 Rust SQL
  parser，也没有将“桥接代码存在”当作完整 CREATE EXTENSION 已可用。

### 前一检查点：脚本解析与批量 routine adapter

- 在包源读取基础上新增 `sql::ExtensionScript`：真实内核 parser 读取 Rust 包源，
  保留自有 arena 中的语法树；正确拒绝失败尾语句，过滤 `T_EMPTY_QUERY`，不暴露
  可安装的半脚本。语法支持不限定为 descriptor 类别，执行权限/事务支持仍由后续
  resolver 和安装预检决定。
- `install_routines_extension` 将一组已解析 routines 交给同一个 Rust 安装驱动、
  DDL 事务和提交后 schema 发布；原单对象接口只做委托。整批检查权限、目标库、
  并行 DDL 冲突及内核规则下的重名，避免尚未发布的 schema 对象绕过普通查重。
  此入口仍没有用户 SQL 命令调用方，也未提供 provisional 对象名称解析。
- 包含上述代码及空语句识别修正的完整 `seekdb` 已重新编译、最终链接成功，
  源码边界检查通过；15 项原独立 CTest 再次通过。
- 关闭实验插件宏后的 PL DDL service 通过生产参数语法检查；这不是完整无插件
  配置的链接证明。`text_ops` 改为字符数与字节数两个独立 SQL 函数，作为多对象
  源包；不通过互调假装已经支持 provisional 名称解析。
- 新增 `kernel_script.py --build-dir build_release` 已真实执行通过：复用生产
  compiler/linker，只替换 main，连接真实内核 parser、C++ 包 adapter 与 Rust
  host，无 SQL/parser 替身。覆盖两个带语句分号的 routines、字符串内分号、
  错误尾语句后清空结果、DELIMITER 拒绝、纯注释、4096/4097 条边界、一般 DDL
  和实际 `text_ops` 文件。首轮发现纯注释返回 SUCCESS + T_EMPTY_QUERY，修正后
  重建重跑通过；没有把首轮失败算作成功。
- 这些结果仍不是批量 routine 安装、提交/回滚、跨会话可见性或 schema 发布的
  数据库回归证据。下一步须连接正常 resolver、Query→Rootserver command seam
  及 SQL 管理命令，补齐 provisional 名称解析、其他对象类型和完整安装生命周期。

### 前一检查点：包源读取

- 最新完整 `seekdb` 目标已重新编译并最终链接成功，覆盖此前整库清理/RESTRICT
  锁序，以及本轮 Rust package reader、统一 native module ID 校验和 C++ source
  adapter。没有仍在等待结果的完整构建；下文按阶段保留的“构建中”是历史状态。
- 54 项 Rust runtime 单测、严格 Clippy、15 项独立 CTest 全部通过。包源测试调用
  真实文件系统和 C++→Rust adapter，补充 SQL/control UTF-8、NUL、空 SQL、大小
  上限、目录/脚本符号链接逃逸、版本选择、源快照所有权及 FFI 字段越界检查。
- 在新建临时安装前缀下执行实际 CMake 本目录 `plugins` 安装规则；安装得到的
  `text_ops.control` 和 `text_ops--1.0.sql` 再次通过真实包读取器测试。未用这项
  文件交付验证代替 SQL 安装，也未声称整个发行包安装或 Bazel 打包通过。
- 本轮未执行真实数据库 SQL、事务/恢复、Windows ABI 或性能测试。新增文件
  读取能力仍没有 SQL 命令调用方，完整 Extension 安装目标继续保持未完成。

当时的下一接线点是 SQL 脚本解析/安装上下文（现已实现上述 parser 首段）：注意
`split_multiple_stmt` 可能通过 `ObMPParseStat` 报告部分失败，不能只检查返回码。
Query→Rootserver 应沿已有 command seam 和串行 DDL 规则接入，不能从插件或 SQL
代码直接绕过正常 resolver、权限和 schema 发布。仍须打通多对象同事务安装与
provisional 名称解析，而非把单 routine adapter 当作完整脚本执行器。

## 已接入真实调用链的工作

- `rust/plugin-runtime`：module generation 状态机、激活预留、执行引用、并发 drain、终止阶段 BLOCKED 收敛。
- `ObPluginGeneration` 改为 Rust 运行时的 C++ 适配，不再保留第二份 C++ 状态机和引用计数。
- 原生动态库装载迁入 Rust `native.rs`：句柄所有权、平台装载/入口查找/显式关闭统一管理；C++ 继续负责可信产物验证与 catalog 协调。发布后拒绝按失败装载路径关闭；正常停用不隐式卸载；OS 关闭失败保留句柄、身份和产物以供终止阶段重试。Rust 的发布标记不替代 host 的线程停止/引用排空证明。
- 防止仍有执行引用的 generation 进入 STOPPED；引用计数溢出时拒绝新增引用。
- `rust/seekdb-host` 聚合 sql-nio 与插件 runtime；CMake/Bazel 改为提供一份 host Rust 静态库。普通插件动态库独立，GIS 保留 C/C++。
- Rust 单元测试与真实 C++ registry/静态库链接测试，覆盖发布、取消发布、lease 移动、并发 drain，以及 sql-nio 符号共存。
- 新增直接对象注册 SPI：init/start 可在注册事务中逐个注册 SQL 扩展对象，描述符即时深复制，与 service 同事务暂存、同候选发布。SQL extension 插件默认已使用此入口；旧快照路径仍保留兼容测试。详见 [直接注册说明](plugin-direct-registration.md)。
- 注册事务管理迁入 Rust：token、对象所有权、配额、冲突、commit/abort/seal 由 Rust journal 统一管理；C++ 注册事务集合和配额计数已移除，只保留 ABI normalization 与封存后的不可变适配快照。
- 启动依赖规划迁入 Rust：真实 catalog 启动准备路径保留单一 SQL 写事务、包状态与 generation 校验，将排序后的包 ID 映射为节点后调用 Rust 的重复边合并/确定性拓扑排序/环检测；原 C++ 图排序实现已移除。错误不返回部分计划。单次上限为 65,536 节点、1,048,576 条输入边（去重前），超限明确失败。自服务依赖在启动时显式忽略；未将此 DAG 规划器当作已实现 Extension 安装事务或递归类型创建。
- SQL 重载选择迁入 Rust：匹配、隐式转换代价、歧义和未知类型探测只保留一份 Rust 算法。C++ 在锁内取得不可变快照与 epoch，锁外适配/匹配，执行获取仍按 generation 校验；签名 FFI 视图随对象元数据构造并共享，避免每次查询重建全部签名。修正最大合法 cast cost 被当成“无转换”及高代价 typed 重载被 legacy fallback 抢占的问题。此项不表示完整注册表存储/发布已迁入 Rust，也不表示 SQL 自动插入和执行 cast 的全链路已验证。
- 服务端 SQL 入口首段已接线（尚待真实 SQL 回归）：标量 execution context 的 v2 后缀提供参数化 SELECT；通过调用者 session 的内核 prepare/execute、显式权限检查、嵌套语句保存/恢复，返回同步行流。多语句由现有 parser 拒绝，数值/UTF-8/二进制参数不拼接 SQL。SQL extension 新增 `seekdb_sql_add_one`，没有退回本地加法的兼容分支。
- execution context 增加版本协商：execution service SPI minor 0 收到精确大小的 v1 副本；SQL-aware 服务显式选择 minor 1 才收到可选 v2 后缀，并继续检查大小。避免新 host context 被现有 GIS 的严格 v1 大小检查拒绝；GIS 的 C/C++ 源码无需修改。
- DML 的语句级事务接线已实现，但未完成真实服务验证：首次插件 SQL 调用为相关外层 SELECT 建立内核保存点；保存点跨越多次回调/多行，在结果集关闭及收尾错误处理后结束语句。复用既有事务服务，不单独提交；记录 transaction ID，避免游标跨事务后把旧保存点应用到新事务。SPI 已开放 INSERT/UPDATE/DELETE/REPLACE，并新增 `seekdb_sql_exec(TEXT, BIGINT)` 示例。
- 持久类型逻辑身份首段已接线：新列采用 v2 元数据，不再保存运行时 generation；v1 仍可读，两者解码为不可直接执行的逻辑 binding。DDL 新增依赖在 schema 事务内解析当前持久类型 generation，删除依赖解析实际边记录的 generation；新增与 RESTRICT 使用相同 provider 行锁，后者使用当前锁定读取。执行绑定的 generation 校验保持不变。详见 [类型身份说明](plugin-type-identity.md)；尚未实现数据库级 Extension 命名空间。
- 公共 Rust SDK 首段与真实 Rust 文本插件已接线：`rust/extension-sdk` 提供 C ABI 子集、注册事务 RAII、查询生命周期借用、标量 SQL 包装与 panic 边界；`plugins/rust_text` 为独立 cdylib，经现有 loader 注册并执行。函数名称可动态生成，版本/能力要求由插件声明；SDK 不链接 host 私有 runtime。GIS 继续保持 C/C++。这不是完整 SDK、SQL 安装包或统一 Extension object manager 的完成标志。
- Rust 插件 CMake 构建新增 Cargo 可达本地依赖和最终二进制边界检查，每次构建均审计，避免失败后因已有产物而跳过检查；Cargo 自行跟踪增量依赖。SDK/plugin 独立 workspace 使用 unwind，未改变 host 的 panic 策略。构建检查不是对第三方 build script 的安全沙箱。
- 运行时扩展对象索引迁入 `rust/plugin-runtime/src/object_catalog.rs`，原 C++ `ExtensionKey/std::map` 已移除。真实 registry 的候选快照复制、对象插入/查找/枚举、SQL 重载输入及停用移除都使用 Rust 持有的唯一索引；快照共享不可变 C++ 适配对象、缓存签名和 generation 所有权。停用按 owner 一次线性过滤，旧快照保留对象至最后释放，执行 lease 仍独立校验。service 索引、公共 descriptor 语义校验和最终发布协调仍在 C++；数据库级 Extension 成员、命名空间与持久安装事务没有因此完成。详见 [运行时对象目录](plugin-object-catalog.md)。

- Rust control/安装 SQL 包源读取与 C++ owned-source adapter 已加入，支持默认或显式版本、
  纯 SQL 包、声明依赖和 native 逻辑模块 ID。没有执行 SQL/装载模块或新开事务，尚未
  接入 SQL 安装入口。新增 `text_ops` 纯 SQL 源文件及 CMake 安装规则；具体格式、
  信任边界和未完成接线见 [Extension 包源](plugin-extension-package.md)。

## 待完成范围与验收依据

数据库安装目录开始接线：新增 `__all_extension_instance/member` 系统表、独立安装 ID、
数据库/owner 身份和 schema 成员键；Rust 校验请求，C++ 提供只参加调用者事务的
`record_extension_install/drop`。原生模块 RESTRICT 查询已读取数据库安装引用。
新的自动提交 catalog 安装协调器已调用记录接口，由 Rust 驱动预检/事务/创建成员/
记录/提交及失败清理；现已加入复用正常 routine 创建路径的具体 DDL adapter，
已接入核心脚本安装方法和用户 SQL 执行端，**仍缺真实数据库回归与完整多对象安装语义**，不能将此基础代码算作已验证可用的
`CREATE EXTENSION`、完整事务安装或数据库隔离。具体边界和未完成项见
[数据库安装目录](plugin-extension-install.md)。

以下未完成项仍属于本次目标，不能用上面的局部测试替代。

| 工作 | 必须具备的实现与验收证据 |
| --- | --- |
| Package / Extension / Module 分离 | 稳定 SQL object ID、tenant/database 归属、成员与依赖、独立运行时 binding；重启及多数据库隔离测试 |
| SQL 与 catalog 开放注册 | control/安装/更新 SQL、统一 object manager、插件可调用的 catalog API、权限与失效通知；新增函数不改核心 factory |
| 安装事务 | 与 schema/storage 协作的安装上下文；支持集合内失败无可见半对象；暂不支持的 DDL 在执行前明确拒绝 |
| 服务端 SQL API | 标量和表函数 open/next 已接同步参数化 SQL、协作式状态检查和错误保持；仍需完整 prepare/cursor 生命周期及实库 SELECT/DML、权限/事务/取消/重入/并行执行回归；查询上下文不可跨任意线程复用 |
| Public / Server-dev | Public C ABI 与精确构建绑定的 C++ 深度 API 共存；host 接口声明、fingerprint 校验与分档边界检查 |
| 真正可执行的扩展点 | type/operator/aggregate、planner path/custom operator、executor hook、index 支持回调；不是仅新增目录条目 |
| 轻量化和生命周期自由度 | 按需装载与初始化、preload hook、restart-required 深度模块；单次初始化、缺失可选模块与恢复必需模块分别处理 |
| AI 执行契约 | typed batch、结果 buffer 归属、deadline/cancel、模型版本、内存/线程/并发预算、后台任务事务与外部副作用说明 |
| Rust 框架迁移 | 继续迁移 loader/registry/包依赖管理；C++ 保留内核适配，只有一个 catalog 事务协调者；不能长期维护两套 runtime |
| Rust SDK | 低层 FFI、安全上下文与资源包装、SQL 生成和打包工具；panic 边界与 host 构建策略分离 |
| 纵向插件验证 | Rust 文本+纯 SQL 包、查询观测/custom path、GIS 或 ANN 索引，覆盖真实 SQL 执行与恢复 |
| 构建与性能 | 完整 server 构建/回归、CMake 与 Bazel、无插件配置、目标平台 ABI、core 体积/RSS/启动/吞吐/复制成本 |

## 本轮已执行验证（2026-09-07）

- Rust runtime：24 项单元测试通过；Clippy 严格检查通过。其中 8 项新测试覆盖精确/转换/fallback 顺序、最大代价、歧义、未知参数、可变参数和 FFI 校验。
- sql-nio：现有 3 项库单元测试通过；聚合 host 的 `--no-deps` 严格 Clippy 和新增 crate 格式检查通过。
- Standalone CMake：真实 C++ registry + Rust host 集成测试通过。
- 新增真实 DSO 直接注册/旧快照装载与回调执行测试，以及注册深复制/冲突/中止测试；共 5 项 CTest 通过。新增回归覆盖现有注册冲突参考插件开满 4096 个事务/服务后的错误码、资源回收及继续注册。catalog 使用测试替身，不代表真实 SQL 事务回归通过。
- 主工程 CMake 重新配置成功；release host Rust 静态库构建成功；生产工具链单独编译 registry 对象成功。
- GIS、SQL extension、reference 插件构建及二进制边界审计通过；源码边界检查通过。
- 新 SQL SPI 的 DSO 集成测试通过：v1 调用者明确不可用、v2 参数传递/行结果/错误传播，仍使用 SQL executor 替身，不是数据库执行证据。新 SQL adapter、标量表达式及执行上下文已通过生产 clang 单文件编译；完整链接和服务验证尚未完成。
- DML 示例的 DSO 测试覆盖影响行数和 NULL 参数传递，5 项 CTest 继续通过。事务控制与结果集关闭改动已通过生产 clang 单文件编译；这些编译结果不能证明回滚语义。
- 增加真实 C++ registry → Rust 重载选择集成测试后，共 6 项 CTest 通过；覆盖最大代价、歧义、不可变快照复制后的签名视图有效性、换代后旧绑定拒绝及 lease drain。
- 新增独立真实服务回归 `rust/plugin-runtime/tests/sql_spi_server.py`：跨会话提交可见性、多行插件写入后的外层聚合表达式溢出、保留调用者之前的写入和具名保存点、权限拒绝后会话可继续使用。预期错误码显式校验，避免把语法错误当作回滚成功。脚本只做了语法/入口检查，**尚未执行数据库测试**：当前沙箱创建临时 loopback listener 返回 `EPERM`，无法在此环境启动测试服务；PyMySQL 已可用。

首轮完整数据库构建已到最终链接，但因运行中新增的 `plugin_sql_context.cpp` 尚未进入旧生成清单而报未定义符号。现已重新配置，确认新源文件进入生产构建清单，并启动增量完整构建（包括执行上下文头文件变化影响的对象）；最终链接结果待确认。SQL 回归、Bazel 实际构建与其他平台仍未验证。没有将这些项目记为通过。

后续增量构建已完成最终链接，覆盖语句保存点字段和 Rust 重载选择；原生装载迁入 Rust 后的完整目标也已再次链接成功。包含 execution context 版本协商修正的最新 seekdb 完整目标已成功链接；主工程 GIS 与 SQL extension 动态库重新构建及二进制边界审计通过。上述结果是编译/链接证据，不是 SQL 事务与恢复行为的验证。

原生装载迁移验证：28 项 Rust 单元测试、严格 Clippy、7 项 CTest 已通过。其中 OS 关闭失败的所有权保留由注入回调的 Rust 单元测试验证，真实 DSO 测试验证发布后拒绝回滚卸载、缺失入口、重复打开的引用和析构时机。Windows 实现尚未交叉编译或实机运行，不将 Unix 结果视作跨平台证明。

增加 GIS 兼容测试后，共 8 项 CTest 通过：生产 C++ loader → Rust 原生装载 → 现有 C/C++ GIS；v1/v2 调用方的 ST_Point 结果与坐标校验通过，ST_Centroid 调用实际 C++ 几何引擎返回相同点；同时验证 catalog 拒绝发布后的清理和终止卸载。GIS 源码未修改。测试的 catalog 仍为替身，不能替代真实数据库 SQL 回归。

同一 loader 集成程序还分别加载了主工程产物 `build_release/plugins/gis/seekdb_gis.so` 与 `build_release/plugins/sql_extension/seekdb_sql_extension.so`，上述 GIS 调用与 SQL SPI 参数传输测试均通过，不仅测试 standalone 重新编译的 DSO。

依赖规划迁移后，35 项 Rust 单元测试与严格 Clippy 通过；新增 7 项测试覆盖全部 4,096 个无自边四节点有向图、最大长度反向链、重复边、自依赖策略、环下游节点诊断及 FFI 错误原子性。8 项 CTest 继续通过，C++ host bridge 新增图边布局、重复/自服务边与错误输出的跨语言调用检查。包含最新 catalog → Rust 规划调用的完整 seekdb 目标已链接成功。真实启动 catalog 事务/持久依赖行恢复仍未执行，不能由图算法单测或编译结果推断通过。

聚合 host 的含依赖严格 Clippy 检查被已有 `rust/sql-nio/src/cert.rs:34` 的 `redundant_guards` 告警阻断；本轮未顺带修改网络模块的证书解析代码。这与新 runtime 的独立 Clippy 通过是两项不同结果。

## 下一步顺序

Rust SDK/文本插件验证：35 项 runtime 单测、9 项 SDK 测试与各自严格 Clippy 通过；SDK 测试包含公共 C 头文件布局探针、panic/异常 payload、动态名称注册、失败提交自动中止、NULL/UTF-8 与单次结果回调。11 项 CTest 通过，其中 Cargo 边界一项包含 6 个 Python 用例（合法 SDK/插件本地依赖、registry 依赖、传递 host 依赖、相似前缀、符号链接逃逸、非 cdylib 等）。主工程 Rust 插件构建与源码/Cargo/二进制三层审计通过，增量构建仍执行全部检查；主工程生成的 `.so` 也单独通过真实 loader 测试。SQL executor/catalog 为替身；未据此宣称真实数据库 SQL、Windows ABI 或 Bazel Rust 插件打包通过。使用说明见 `plugins/rust_text/README.md` 与 `rust/extension-sdk/README.md`。

SDK 独立 workspace 与 CMake 接入后的完整 `seekdb` 目标增量构建再次成功（包括 release Rust host 与源码边界检查），最终 standalone 11 项 CTest 再次通过。本轮未修改生产 C++ 执行代码，没有以这次增量构建替代尚缺的真实服务行为验证。

运行时对象目录迁移后，41 项 Rust 单元测试与严格 Clippy 通过；新增 6 项目录测试以及真实 C++ → Rust 的 8 线程快照复制/读取/销毁测试，12 项 CTest 全部通过。最终包含线性批量停用的完整 `seekdb` 目标已重新编译、链接成功，源码边界检查通过。原 C++ 对象 map 与 ExtensionKey 已移除；此项只证明运行时索引迁移和现有装载执行链，不能证明数据库级 Extension 身份或原子安装已实现。

数据库安装目录基础加入后，44 项 Rust 单测与严格 Clippy 通过，12 项 CTest 通过
（新增跨语言安装身份/成员布局调用在对象目录测试中）。新系统表生成成功，安装/
移除 catalog 代码通过生产 clang 参数的语法检查；完整 server 目标正在因生成
系统表头变化而重编译，尚未确认最终链接。真实安装/回滚/RESTRICT 数据库行为未
执行；Unix socket 监听也重新探测为 `EPERM`，不是仅缺少 TCP 方案。

安装协调器加入后，48 项 Rust 单测与严格 Clippy、13 项 CTest 通过。新增驱动测试
覆盖各阶段失败、未知提交与失败清理；C++→Rust 集成使用事务模型，不是真实 SQL
事务。最新协调器/异常路径通过生产 clang 语法检查。包含新增系统表的完整构建仍
在运行，期间协调器又有增量修改；该构建结束后还必须再执行一次增量构建，才能
确认最新 C++ coordinator 与 Rust host driver 已一同进入最终产物。

具体 schema adapter 首段：`ObPLDDLService::install_routine_extension` 已调用 Rust 驱动的
catalog 协调器，在一个非并行 DDL 事务中执行真实 SQL routine 创建及 Extension 成员
记录。普通 routine 路径增加外部事务模式，复用 ID/version、参数、依赖和自动授权；
DDL 事务提交保留 schema watermark/epoch/标记，提交后再发布 schema，发布结果与
安装结果分开。该入口只接收已解析、已做 definer 检查的一条纯 SQL routine；没有
SQL 命令或脚本调用方，多对象、成员 DROP 保护和用户事务仍未完成。catalog 和 PL
DDL service 均已通过生产 clang 参数的语法检查；真实数据库安装/回滚/发布未验证。
原系统表完整构建仍在进行，最新接口改动仍需其结束后的增量构建确认。
本轮重建 standalone 后 13 项 CTest 再次通过（catalog/SQL 仍含替身或事务模型，不含
新 routine adapter 的真实数据库回归）。普通管理服务调用方也通过生产语法检查。
新增 rootserver 插件开关：开启时编译真实桥接，关闭时不包含/引用 plugin catalog，
入口返回 `OB_NOT_SUPPORTED`；两种宏状态均通过生产参数语法检查。这不是完整
无插件构建或 Bazel 插件启用的验证。最新 CMake 定义须在原构建结束后的重新生成/
增量构建中生效。

routine 成员删除保护首段：常规 `ObDDLSqlService` 增加事务内成员锁定读取，真实
`ObRoutineSqlService::drop_routine` 在删除前执行，不依赖原生插件编译开关；存在
成员拒绝单独删除，读取/关闭错误及损坏身份不当作无依赖。保留 object ID 的替换
没有额外禁止。schema Unity 单元已通过生产参数语法检查；不再用脱离 Unity 的
单文件检查要求原有代码补齐不相关 include。数据库整体删除/回收站与 Extension
级联移除尚未打通，routine 之外的成员也还未接入。

同时修正 routine adapter 权限预检的 FixedArray 初始化：显式传入 allocator 并
reserve 一项后才 push_back，避免正常请求恒以 OB_NOT_INIT 失败；启用插件宏的
PL DDL service 再次通过生产语法检查。这些不是新 adapter 或删除保护的真实 SQL
回归证据；原完整构建仍运行，且没有覆盖本轮增量。

新增系统表的原完整构建最终以链接失败结束：6 个 Extension schema creator 符号
缺失。根因是生成器创建了 `1151_1200`、`51151_51200`、`61151_61200` 三个新分片，
而 CMake/Bazel 共享的 `share_source_inventory.bzl` 尚未列入它们。已补齐清单；
CMake inventory emitter 新增 `--generated-root`，每次配置核对生成 shard 与编译
清单（包括 generator digest 命中时），不再等待最终链接暴露缺失。

新增 7 项 Python 回归通过，覆盖未编译的新分片、丢失文件/目录、忽略非 shard、
失败时保留旧 inventory，以及真实表定义在临时目录重新生成后与清单精确匹配。
独立集成测试增加 `generated_schema_inventory`，总计 14 项 CTest 已通过。
新的生产增量构建已完成重新配置，实际 schema Unity 对象的符号表确认包含缺失
的全部 6 个 creator；新 Rust host 和 share catalog/schema 对象已编译。完整
seekdb 目标仍运行，最终链接尚待确认；这不是 Bazel 实际执行或数据库 SQL 验证。

整库清理已接线：`ObDDLOperator::drop_database` 在删除数据库对象之前，通过常规
`ObDatabaseSqlService` 在同一活动 DDL 事务中删除该 tenant/database 的安装与成员
记录，因此随后 routine 删除不会被本库的成员保护阻断。instance→member 锁顺序
与逐 Extension 清理一致；不取 provider/runtime 锁，不卸载共享模块。进入回收站
仍保留归属，永久 PURGE 复用整库删除路径。新写入失败须回滚整个数据库 DDL，
不忽略缺表。真实 SQL、回收站恢复、失败回滚和并发验证仍未执行；当前完整构建
启动后才增加这段代码，因此结束后还需一次增量构建来验证最新 schema 与 rootserver
对象一同链接。

整库清理及 RESTRICT 锁序检查：常规 schema Unity 和 rootserver runtime Unity、
catalog 均通过生产语法检查。RESTRICT 改为先锁定数据库安装记录、再读取对象
依赖，匹配数据库 teardown 的 instance→dependency 顺序；展示顺序保留不变。
包含三个新增系统表分片的完整 seekdb 目标已最终链接成功。该构建期间又增加了
整库清理和锁序调整，因此已在确认它终止后启动下一次增量构建，以最终结果确认
最新改动全部进入产物；不能仅据前一个构建通过就宣布最新版本验证完成。

类型逻辑身份改动后，9 项 CTest 通过；新增元数据兼容测试不依赖运行时模块。包含 catalog provider 行锁和 persistent 标记校验的最新完整 seekdb 目标已编译、链接成功；源码边界和 diff 空白检查通过。新增两阶段真实服务脚本 `type_identity_server.py`，覆盖恢复后 CREATE TABLE LIKE、DROP COLUMN/TABLE；仅执行语法/入口检查，数据库重启、旧 v1 列迁移以及并发 RESTRICT 测试尚未执行。

SQL SPI 的下一段首先要验证已接入的写入事务语义：不能仅在单次回调返回错误时撤销，而要覆盖外层表达式/后续行/结果集关闭阶段的失败。真实回归仍需运行，并补充取消、游标跨事务、嵌套 PL、并行执行和 SELECT 调用已有函数的副作用场景。DDL/安装事务仍未开放，不能把 DML 保存点机制直接当作可回滚的 CREATE EXTENSION 实现。

优先推进统一 Extension/object manager 和安装上下文，打通插件注册 SQL 对象的真实路径，并以一个 SQL 包验收。与此同时持续将管理状态和注册逻辑迁移到 Rust；随后用真实 planner/index 插件固定 Server-dev 与执行回调契约。每步保留已有 C/C++ GIS 的可加载性与执行能力，但不以兼容现有窄接口作为最终完成标准。
