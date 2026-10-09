# 10: Phase 2 验收：全查询面回归

**What to build:** 四 prototype 门禁套件（bootstrap/sql_worker/direct/tls）适配单进程形态后全 PASS；将已运行、尤其曾失败的路径固化为聚焦回归。用户已取消完整 mysqltest 和 sysbench 的运行要求。

**Blocked by:** 09

**Status:** 四件套与既往失败路径聚焦回归已通过；性能差距为非阻塞 TODO

- [x] 四套件适配单进程形态，PASS 判据（exit 0 + {"event":"PASS"}）全绿
- [x] 已运行且暴露子空间专有失败的路径纳入四件套，包含 IVF_PQ 崩溃回归
- [ ] point_select 子空间与 ns1 的吞吐差距待收敛（非阻塞 TODO）

## 2026-09-23 验证与 TODO

- 四个 prototype 入口新增 `--in-process`，分别检查冷启动/继承读、事务与重启、DDL/DML/分区/LOB、TLS；保留旧 worker 模式门禁。
- 重启恢复原先仍调用 `reconcile_namespace_workers` 启动 worker，已在双门控形态改为只清空旧 endpoint 记录。TCP 代理入口的 SSLRequest 已交由本进程 NIO 完成 TLS 握手。
- ns1 与 child 各运行 mysqltest storage 的 `rowkey_is_int`、`rowkey_is_char`、`rowkey_is_null`，六项均 `ok`。
- 最小 `SELECT 1` 复现（100 次中位数）改动前 ns1/child 为 129/856 微秒；每语句 schema refresh 改为首次加载及同进程 DDL 发布后，门禁内为 103/102 微秒，单独原始复现为 114/84 微秒。SQL 门禁覆盖 child CREATE/INDEX/UPDATE、二级 fork 和重启后立即可见。
- sysbench 使用文本协议、`--auto_inc=off`、单表 1000 行；OLTP read/write、update、delete、insert、random points/ranges 和独立库 bulk insert 均退出 0。当前 point_select 1t：ns1 9993 TPS、child 5199 TPS；8t：ns1 53847 TPS、child 19974 TPS。
- child AUTO_INCREMENT 在本次验收时返回 4029；2026-09-24 的子空间自增服务修复已使专项回归通过，默认 sysbench prepare 仍待重跑。
- **TODO:** 11 的协议层删除/进程内接口沉淀后重测 point_select 并追平同机 ns1 基线；用默认自增表重跑 sysbench prepare；扩展 mysqltest 到完整基础套件后关闭 10。这些不阻塞 11 的删除工作。

## 2026-09-24 默认自增 sysbench 与登录库回归

- 默认自增 point_select 的子空间 prepare 首先暴露握手指定数据库返回 1049。PyMySQL 独立复现证实：子空间 `SHOW DATABASES` 已包含新库，`root@child` 不指定库可以登录，握手指定该库则失败。原因是登录会话已绑定子空间，`ObMPConnect::load_privilege_info` 仍从全局 ns1 取得 schema guard。登录权限校验、`COM_INIT_DB` 和 `COM_CHANGE_USER` 现使用会话所属 schema service；初始化连接 SQL 也取会话 guard。
- bootstrap 门禁新增子空间握手指定库、切库、无效库拒绝。离线编译 `/tmp/seekdb-ticket10-login-db-fix-build.log` 成功，四门禁 `/tmp/seekdb-ticket10-login-db-{bootstrap,sql,direct,tls}.log` 均退出 0 且有 PASS。
- 默认 `--auto_inc=on` 的 sysbench point_select 在 ns1 与子空间各自 prepare、1/8 线程 10 秒运行均退出 0，忽略错误数 0，表内各 1000 行；`/tmp/seekdb-ticket10-sysbench-autoinc-fixed.log` PASS。1 线程 ns1/child 为 10700/4629 TPS，8 线程为 53296/19277 TPS。性能差距仍明显。
- **TODO:** 完整 mysqltest 基础套件、其余 sysbench 场景默认自增回归、`COM_CHANGE_USER` 专项触发验证，以及子空间 point_select 性能追平 ns1。当前子空间登录库问题已解决，不再阻塞负载扩展。

## 2026-09-24 mysqltest 扩展首批

- 直接使用本地 `/u01/obclient/bin/mysqltest` 和隔离单进程实例运行仓库 `tools/deploy/mysql_test/t` 顶层用例。最初 `get/count/empty_table/join_basic/select_basic` 的 ns1 五项全通过；子空间首次带库名登录报 1049。进一步对照证实，子空间首登若不带库名可触发初次 schema 刷新，之后带库名登录成功；单独 cold login 失败并非 mysqltest 参数问题。
- 登录现在等待子空间初次 schema 加载；加载状态在完成时才标记，其他并发调用等待，同线程递归可继续。bootstrap 门禁先用冷子空间连接指定模板库 `test`，再测子空间自建库。修复后 `get/count/empty_table` 子空间结果与 ns1 均通过。
- mysqltest 自动执行的 `SHOW WARNINGS` 曾在子空间返回 1235。虚拟 warning 表现在走本进程原生虚拟扫描，并按会话 schema service 校验运行时版本；否则会因子空间版本大于 ns1 缓存版本反复 `OB_SCHEMA_EAGAIN` 直到 10 秒超时。bootstrap 门禁覆盖空告警和一条实际 1052 告警。修复后 `select_basic` 在 ns1 与子空间均通过。
- `join_basic` 和 `select_basic` 曾在无显式主键表 INSERT 时反复 `OB_TABLET_NOT_EXIST` 直到超时。隐式 heap 主键取号现在通过会话所属的 tablet 自增服务路由逻辑 tablet ID；并行 DML 调用点也传入会话。最小两行 DATE heap 插入和直连门禁中的 heap INSERT/UPDATE/SELECT 已通过。`join_basic` 已推进到第 56 行，当前二级索引 join 扫描 `NOT_SUPPORTED`，待继续处理。
- 最终离线编译 `/tmp/seekdb-ticket10-heap-autoinc-build2.log` 成功；四道门禁 `/tmp/seekdb-ticket10-mysqltest-fixes-{bootstrap,sql,tls}.log` 与 `/tmp/seekdb-ticket10-heap-direct.log` 均 exit 0 + PASS；mysqltest 对照 `/tmp/seekdb-ticket10-mysqltest-heap-after.log`。完整 92 项尚未跑完，不据这批样本关闭工单 10。
- `join_basic` 随后在第 56 行二级索引 join 返回 1235；聚焦日志定位到批量嵌套循环连接请求内部伪列 `OB_HIDDEN_GROUP_IDX_COLUMN_ID`（13），扫描帧解码误把它当作用户 schema 缺失列。现按原生 `ObTableParam` 已支持的三种内部伪列（事务版本、SQL 序列、分组索引）放行，仍拒绝其他缺失列。聚焦 `/tmp/seekdb-ticket10-join-index-after.log`、完整 mysqltest `/tmp/seekdb-ticket10-mysqltest-join-after.log` 中 ns1/child 均通过，直连门禁 `/tmp/seekdb-ticket10-join-pseudo-direct.log` PASS。编译 `/tmp/seekdb-ticket10-join-pseudo-build.log` 成功。

## 2026-09-24 mysqltest 顶层首轮与元数据虚拟表

- 顶层 92 项初扫：55 项 ns1/child 同过，27 项同失败，10 项仅 child 失败。两边同失败多为 `mysqltest` 原部署器连接变量未设置、include 缺运行上下文或结果文件的计划差异，不等于产品回归；单项日志在 `/tmp/seekdb-ticket10-mysqltest-{ns1,child}-*.log`，汇总在 `/tmp/seekdb-ticket10-mysqltest-full-sweep.log`。
- `SHOW COLUMNS`、`DESC`、`SHOW INDEX`、`SHOW COLLATION`、`SHOW CHARACTER SET`、`SHOW CREATE TABLE` 使用的会话/Schema 虚拟表已改为进程内原生扫描。原有子空间帧扫描要求 `ns==1`，所以这组查询统一返回 1235。修后 `column_alias`、`ddlrollback`、`largetimeout`、`special_stmt` 的子空间 mysqltest 通过；bootstrap 门禁覆盖这组查询。编译 `/tmp/seekdb-ticket10-native-schema-virtual-build2.log`、四门禁 `/tmp/seekdb-ticket10-native-schema-virtual-{bootstrap2,sql,direct,tls}.log` 通过。
- `truncate_table` 继续在 child 返回 1235，日志定位为 DDL 内部扫描 `__all_virtual_core_all_table` (11035)。该表及 `__all_virtual_core_column_table` 现走原生扫描，虚拟表读取的内部 SQL 代理改取会话所属服务。完整 `truncate_table` mysqltest ns1/child 均通过（`/tmp/seekdb-ticket10-truncate-core-virtual-mysqltest.log`）；直连门禁新增截断继承表、ns1 数据保持、子空间截断后重插入，`/tmp/seekdb-ticket10-truncate-inherited-direct.log` PASS。编译 `/tmp/seekdb-ticket10-truncate-core-virtual-build.log` 通过。
- **TODO:** 首扫尚余 `rename_table2` 错误码、`view` CREATE 1210、`table_column_related_views` 1235；`join_many_table{,_single_field}` 的 `SET GLOBAL` 需要有权限的部署器会话，不应以 child root 直接运行。其余 ns1/child 同失败项需用原部署器上下文或收窄到可独立执行的基础集。默认自增 sysbench 其余场景和性能差距仍在。

## 2026-09-24 其余子空间专有 mysqltest 失败

- `table_column_related_views` 随 core 元数据虚拟表修复已在 ns1/child 均通过，复跑见 `/tmp/seekdb-ticket10-mysqltest-next-issues.log`。
- `rename_table2` 原在重命名第一个表的读写防护注册返回 `OB_TABLET_NOT_EXIST`。`ObDDLService::build_single_table_rw_defensive_` 使用全局 ns1 runtime，现优先取本服务任务上下文中的 namespace runtime。随后 `SHOW TABLES` 的虚拟表扫描在 child 返回 1235，已纳入原生会话扫描。完整 `rename_table2` ns1/child 通过，见 `/tmp/seekdb-ticket10-view-no-tablet-mysqltest.log`。
- `view` 原先第一次 1210 来自子空间 schema backend 未初始化 DDL sequence ID：全局 launcher 只初始化 ns1。子空间激活时现使用当前 leader epoch 初始化其 backend。下一次 1210 来自 `make_namespace_schema`/`make_storage_schema` 强制改写视图不存在的 tablet ID；视图现跳过 tablet ID 改写。完整 `view` ns1/child 均通过，bootstrap 门禁另覆盖新建子空间视图和读取。编译 `/tmp/seekdb-ticket10-view-no-tablet-build.log` 成功。
- **TODO:** `join_many_table{,_single_field}` 的 `SET GLOBAL` 需要有权限的部署器会话；27 项 ns1/child 同失败应以部署器变量补齐或独立基础集界定，不应误记为子空间回归。默认自增 sysbench 其余场景和性能差距未结，Ticket 10 暂不关闭。

## 2026-09-24 顶层 92 项复扫结论

- 最终二进制在 `/tmp/seekdb-ticket10-mysqltest-final-sweep.log` 的 92 项 ns1/child 对照：**63 项双过，27 项双失败，2 项仅 child 失败**。初扫的 10 项 child 专有失败已修 8 项；剩余 `join_many_table`、`join_many_table_single_field` 都在 `SET GLOBAL ob_sql_work_area_percentage=100` 返回 1227 SUPER 权限不足，属于当前无部署器权限的执行方式不适用，不能称产品功能失败。
- 27 项双失败中 23 项为 mysqltest `connect` 指令缺 `$OBMYSQL_MS0` 等部署器变量而报 `Missing required argument 'host'`；另外 4 项为期望结果差异：`dist_nest_loop_simple`、`topk`、`view_2` 的执行计划文本长度差异，以及 `information_schema` 的结果内容差异。它们需要完整部署器上下文和对应版本期望文件重新比对。
- 修复后离线编译 `/tmp/seekdb-ticket10-view-no-tablet-build.log` 成功，四门禁 `/tmp/seekdb-ticket10-mysqltest-final-{bootstrap,sql,direct,tls}.log` 均退出 0 且有 PASS；完整 `view`、`rename_table2` 双侧通过 `/tmp/seekdb-ticket10-view-no-tablet-mysqltest.log`。
- **TODO:** 搭建包含部署器变量、权限和版本匹配结果文件的 mysqltest 基础套件运行方式；重跑默认自增 sysbench 其余场景；定位子空间 point_select 1/8 线程吞吐差距。上述不阻塞 11 的 IPC 删除工作。

## 2026-09-24 `GLOBAL_VARIABLES` 子空间回归

- 复查 92 项日志发现 `view_2` 在子空间先于结果对比失败：`INFORMATION_SCHEMA.GLOBAL_VARIABLES` 底层虚拟表误走帧扫描，返回 1235。修复前聚焦 `/tmp/seekdb-ticket10-global-variables-before.log` 重现；现将 `OB_ALL_VIRTUAL_GLOBAL_VARIABLE_TID` 路由本进程原生虚拟表扫描，聚焦 `/tmp/seekdb-ticket10-global-variables-after2.log` 验证读取及 `SET optimizer_switch=(SELECT ...)` 均成功。SQL 门禁加入该子查询回归。
- 完整 `view_2` 单项 `/tmp/seekdb-ticket10-view2-after-global-variable.log`：ns1 退出 0，child 已越过原 1235，仍因执行计划中两处 dynamic sampling level 期望值差异退出 1。该项仍未算通过，也不能再归类为只有测试环境缺变量。
- 离线编译 `/tmp/seekdb-ticket10-global-variables-build.log` 成功，四门禁 `/tmp/seekdb-ticket10-global-variable-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。
- 同类会话变量虚拟表也误走旧扫描：子空间 `SHOW VARIABLES LIKE 'optimizer_dynamic_sampling'` 返回 1235，ns1 正常。现将 `OB_ALL_VIRTUAL_SESSION_VARIABLE_TID` 路由原生虚拟表服务；聚焦 `/tmp/seekdb-ticket10-ds-vars-after.log` 的 ns1/child 会话、全局变量结果一致，SQL 门禁新增两种 SHOW。离线编译 `/tmp/seekdb-ticket10-session-variable-build.log` 成功，四门禁 `/tmp/seekdb-ticket10-session-variable-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。

## 2026-09-24 子空间优化器存储估算

- `view_2` 原先在子空间的两处 dynamic sampling level 变为 0；最小对照 `/tmp/seekdb-ticket10-ds-plan-probe.log` 证实 ns1/child 系统变量均为 1，子空间行数估算和块数估算因直接拿逻辑 tablet ID 调原生存储返回 4725。优化器估算入口现显式接收会话，按 namespace 将子空间自建表 tablet/index/range 解析到物理存储；`/tmp/seekdb-ticket10-ds-plan-routed2.log` 两侧都恢复 dynamic sampling level 1。
- 继承表不得把父空间 fork 后新增的行计入子空间估算。`/tmp/seekdb-ticket10-estimate-cap-probe.log` 证实子空间实际 2 行而直接查询父 tablet 的计划估出 5 行。对受 fork SCN 上限约束、仍从父 tablet 读取的表，行数接口现返回不可靠估算，让优化器回退；`/tmp/seekdb-ticket10-estimate-cap-safe2.log` 计划不再泄漏父空间后续 3 行。块数接口仍只提供物理最新块数，缺乏 SCN 参数；若要求严格的快照时点采样成本，后续需扩展该接口。
- SQL 门禁新增子空间自建双表动态采样和继承表父空间新增行后的估算检查。最终离线编译 `/tmp/seekdb-ticket10-estimate-routing-final-build.log` 成功；四门禁 `/tmp/seekdb-ticket10-estimate-final-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS，完整 mysqltest `view_2` ns1/child 双通过 `/tmp/seekdb-ticket10-estimate-final-view2.log`。

## 2026-09-24 部署器变量补齐与分区本地索引

- 在隔离实例中补齐 `$OBMYSQL_MS0/$OBMYSQL_PORT/$OBMYSQL_USR/$OBMYSQL_PWD` 并创建测试所需 `admin` 后，复跑先前因缺 host 失效的 23 项：15 项 ns1/child 双侧通过，1 项 `generated_column` 双侧结果差异，7 项仅 child 失败。后者包括 `bulk_insert` 的 minor freeze 4006、`create_using_type` 元数据结果缺失、`idx_unique_many_idx_one_ins` 唯一键报错文案、`two_order_by` 的 1235，以及 `minitest`/`trx_timeout`/`update_behavior` 的 `SET GLOBAL` 权限 1227。汇总 `/tmp/seekdb-ticket10-vars-full23.log`，单项 `/tmp/seekdb-ticket10-vars-{ns1,child}-*.log`。这批结果替代此前把 23 项全归为环境错误的判断。
- `generated_column` 原在第 71 行子空间分区表 `CREATE INDEX` 返回 1146。最小复现：含生成列的两分区表插入一行、建本地索引、强制索引读取。索引任务的 tablet 调度器仍从 ns1 schema service 查 child 表，也把 child 任务记录写向 ns1。现由 index 任务显式传入 schema service 和 SQL proxy；进程级会话信息与磁盘统计仍走进程级 SQL 代理。
- 建索引后强制索引读取曾返回 1210：本地索引回表复用尚未打开的数据扫描迭代器，原生扫描服务允许空迭代器复用，子空间扫描适配器却返回无效参数。适配器现返回成功。聚焦 `/tmp/seekdb-ticket10-generated-index-fixed.log` 中 ns1/child 建索引及强制索引读取均成功；直连门禁已加入这一场景。最终编译 `/tmp/seekdb-ticket10-part-index-build-final.log`，四门禁 `/tmp/seekdb-ticket10-part-index-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 + PASS。
- 完整 `generated_column` 复跑 `/tmp/seekdb-ticket10-generated-column-mysqltest-after.log`：child 已越过第 71 行，但第 192 行空表 `ALTER TABLE jx_t3 ADD INDEX idx3(c3(20))` 返回 4725，需另查空表 ALTER INDEX 路径；ns1 只因期望结果末尾空行差异退出 1。该完整用例尚未通过。

## 2026-09-24 空表 ALTER INDEX 与完整 generated_column

- `generated_column` 第 192 行可独立复现：子空间空表 `ALTER TABLE jx_t3 ADD INDEX idx3(c3(20))` 返回 4725，日志指向 `ObTabletBindingHelper::build_single_table_write_defensive`。该路径属于空表索引捷径，仍直接写全局 tablet MDS。共同的 `ObCreateIndexOnEmptyTableHelper` 现对 child namespace 跳过这条捷径，使用已支持 child 的普通 DDL task；ns1 仍使用捷径。聚焦 `/tmp/seekdb-ticket10-empty-alter-index-fixed.log` 中两侧 `ALTER ADD INDEX` 和 `SHOW INDEX` 均成功。
- 直连门禁新增空表含两个内建索引、ALTER 加第三索引、插入后强制第三索引读取。编译 `/tmp/seekdb-ticket10-empty-alter-index-build.log` 成功，四门禁 `/tmp/seekdb-ticket10-empty-alter-index-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 + PASS。
- 完整用例第 210 行的下一失败来自测试脚本 `obsys` 明确连接 ns1 的 `admin`，而子空间表的 `__all_table` 不在 ns1。隔离运行副本中创建 child admin、将 `obsys` 登录名指向 `admin@mt_child`，并移除 wrapper 的末尾空行后，完整 `generated_column` ns1/child 均 exit 0：`/tmp/seekdb-ticket10-generated-column-mysqltest-final.log`。源码测试文件和结果文件未改；修正的是隔离运行上下文。

## 2026-09-24 23 项复测的更新结论

- 隔离 mysqltest wrapper 修正了两项运行上下文：去掉 `source` 行尾造成的多余结果空行，并为子空间测试的硬编码 `obsys` 连接创建 `admin@mt_child` 后指向该 namespace。修正后 `generated_column` 和 `create_using_type` 均在 ns1/child 双侧退出 0；后者此前的 `__all_table` 缺行是 `obsys` 错连 ns1，不是目录复制错误。
- 这 23 项截至 `/tmp/seekdb-ticket10-vars-seven-after.log` 更新为 **17 项双侧通过、6 项仅子空间失败**。六项是 `bulk_insert`（`ALTER SYSTEM MINOR FREEZE` 4006）、`idx_unique_many_idx_one_ins`（重复键错误文案不匹配）、`minitest`/`trx_timeout`/`update_behavior`（`SET GLOBAL` 返回 1227 SUPER）、`two_order_by`（复杂 UNION/EXCEPT 返回 1235）。它们不阻塞工单 11–13，保留 TODO；工单 10 的完整基础集、默认自增 sysbench 其余场景及 point_select 性能差距仍未验收。
- 扫描取数 `F` 帧删除后，默认自增 point_select 的 ns1/child prepare、1/8 线程运行均成功，见 `/tmp/seekdb-ticket11-typed-scan-fetch-sysbench.log`。1 线程 ns1/child 约 11182/5393 TPS，8 线程约 51665/19959 TPS；测试时段与先前不同，不能将 TPS 变化归因于本次改动。性能差距仍为 TODO，不阻塞后续协议清理。

## 2026-09-24 唯一索引冲突文案

- 最小复现 `/tmp/seekdb-ticket10-unique-index-probe.py` 在相同两行 `v=610` 上先红：ns1 1062 `Duplicate entry '610' for key 'idx_1'`，child 1062 `Duplicated primary key`，见 `/tmp/seekdb-ticket10-unique-index-probe-baseline.log`。临时标记确认子空间直插 writer 同步 append/close 的线程没有绑定 owner 上下文，错误上报因此从全局 ns1 schema 和 SQL proxy 查索引任务失败，内部码保持 `-5024`；ns1 正常转换为 `-5358`。
- 在同步 writer append/close 调用域绑定现有 `ObIDirectInsertWorkerContext`，由它显式提供所属 namespace 的 schema service、SQL proxy 及逻辑 table/tablet ID；存储层保持 namespace 盲。`ObTabletSliceWriter` 的两种重复键上报均使用该上下文。最小复现转绿 `/tmp/seekdb-ticket10-unique-index-probe-debug2.log`，原 `idx_unique_many_idx_one_ins` mysqltest 的 ns1/child 都 exit 0：`/tmp/seekdb-ticket10-unique-index-final-mysqltest.log`。direct 门禁加入真实唯一索引冲突错误文本断言；离线编译 `/tmp/seekdb-ticket10-unique-index-final-build.log`，四门禁、非空向量索引及 FTS `/tmp/seekdb-ticket10-unique-index-final-{bootstrap,sql,direct,tls,vector,fts}.log` 均 exit 0 且 PASS。临时 debug 标记已全部移除。
- 23 项变量补齐的子空间专有失败由 6 项降至 5 项：`bulk_insert` minor freeze 4006、`minitest`/`trx_timeout`/`update_behavior` 的 `SET GLOBAL` 权限 1227、`two_order_by` 复杂 UNION/EXCEPT 1235。工单 10 完整基础集和性能仍是 TODO，不阻塞后续。

## 2026-09-24 四路集合查询 scan 上限

- 当前二进制原 `two_order_by` mysqltest 复现：ns1 exit 0，child exit 1，唯一差异为复杂集合查询 1235，见 `/tmp/seekdb-ticket10-two-order-current.log`。最小复现 `/tmp/seekdb-ticket10-two-order-probe.py` 将查询缩为同表四个扫描分支的 `UNION ALL` 或 `UNION`：三路可成功，四路 child 稳定 1235；常量四路成功，说明与表扫描打开数有关。实例日志中第 5 次扫描打开记录 `PROTOTYPE_V22_SCAN_OPEN stage=in_process ret=-4007`。
- 原因是 `ReadScans::open` 延续 IPC 原型的 `scans.size() >= 4` 硬上限；现移除此人为限制，依靠 SQL 执行生命周期释放扫描句柄。direct 门禁加入同表四路 `UNION ALL` 真正执行结果断言。离线编译 `/tmp/seekdb-ticket10-four-scans-build.log` 成功；原 `two_order_by` mysqltest `/tmp/seekdb-ticket10-four-scans-mysqltest.log` ns1/child 均 exit 0；四门禁、向量索引、FTS 和最小复现 `/tmp/seekdb-ticket10-four-scans-{bootstrap,sql,direct,tls,vector,fts,probe}.log` 均 exit 0 且通过。23 项变量补齐对照中的子空间专有失败由 5 项降至 4 项：`bulk_insert` minor freeze 4006，以及三个 `SET GLOBAL` 权限 1227。完整基础集和性能仍待验收。

## 2026-09-24 子空间 MINOR FREEZE

- 最小对照 `/tmp/seekdb-ticket10-minor-freeze-probe.py` 在当前二进制中证实 ns1 minor/major freeze 均成功，child major 成功但 minor 返回 4006（`/tmp/seekdb-ticket10-minor-freeze-probe-baseline.log`）。原因是子空间 `ObLocalManagementService::init_sql_worker` 未初始化 `root_minor_freeze_`；该服务沿用全局 runtime 入口，不能路由子空间 tablet。
- `ObRootMinorFreeze` 现在接受可选的显式本地 runtime；子空间初始化时传入已绑定的 namespace runtime 并启用该服务，全局实例仍按原入口。子空间 `ALTER SYSTEM MINOR FREEZE` 在 `/tmp/seekdb-ticket10-minor-freeze-probe-after.log` 成功，原 `bulk_insert` mysqltest ns1/child 均 exit 0：`/tmp/seekdb-ticket10-minor-freeze-mysqltest.log`。direct 门禁新增该管理命令。离线编译 `/tmp/seekdb-ticket10-minor-freeze-build.log` 和四门禁、向量索引、FTS `/tmp/seekdb-ticket10-minor-freeze-{bootstrap,sql,direct,tls,vector,fts}.log` 均 exit 0 且 PASS。
- 23 项变量补齐对照中原有 6 项子空间专有失败已修 3 项（唯一索引报错、四路集合扫描、minor freeze）；剩余 3 项都在 `SET GLOBAL` 返回 1227，需区分系统空间管理权限与测试运行上下文。完整基础集、默认自增 sysbench 其余场景及 point_select 性能仍未验收。

## 2026-09-24 `SET GLOBAL` 三项的权限归类

- `minitest`、`trx_timeout`、`update_behavior` 的剩余 1227 均来自测试脚本在 `root@child` 会话执行 `SET GLOBAL`。`ObVariableSetExecutor` 明确拒绝 `serving_namespace()>1` 的全局变量修改，因为全局变量由 ns1 拥有；即便子空间用户有 SUPER，也不应让它变更整个进程的配置。这三项属于当前 mysqltest 执行方式与产品权限模型不符，不能作为子空间 SQL 功能失败或通过来计数。
- TODO：为完整 mysqltest 基础集建立系统空间管理连接，在 ns1 执行全局配置准备/恢复，子空间连接仅执行被测 SQL；保留原期望结果比对。之后再收敛 27 项双侧失败、默认自增 sysbench 全场景和 point_select 性能差距。

## 2026-09-24 将既往失败场景纳入四件套

- 用户明确免跑完整 mysqltest/sysbench；已把此前专项跑过且出现失败、后来修复的代表性用例直接放入四件套的 `direct` 路径：非中断的子空间 `DROP PRIMARY KEY` 堆表改写、CHECK 约束及非法写入、非空表改分区、隐式/显式及 ALTER 后的 AUTO_INCREMENT、子空间自有及已删除源空间继承表的 `FORK TABLE`、多字节 outrow LOB 切片、IVF_SQ8 非空建索引与重启查询/删除、空表 HNSW 建索引。原门禁已有 IVF_PQ 曾崩溃的 20 行近似查询、IVFFLAT、FTS、唯一索引报错、四路集合查询、空表 ALTER ADD INDEX、分区生成列索引、MINOR FREEZE、重命名和源空间删除等回归。
- 中断的 `DROP PRIMARY KEY` 仍红，不应放进要求 PASS 的门禁；聚焦复现 `.scratch/namespace-fork/drop_primary_recovery_probe.py` 保留，待安全恢复实现后纳入。`SET GLOBAL` 的三项 mysqltest 在子空间按权限模型返回 1227，需由系统空间管理连接准备，不是应被改成 PASS 的子空间语句。
- 聚合门禁一度在源空间同名重建后登录返回 4019：新增独立继承空间使进程内第五个以上子空间激活在 schema cache `service_init` 触达 `ObKVGlobalCache` 32 项注册上限；日志 `/tmp/seekdb-regression-add-direct2.log`。将继承 `FORK TABLE` 合入现有源删除后代空间后，`/tmp/seekdb-regression-add-direct3.log` exit 0 + PASS。长期反复创建/删除空间的 cache 注册容量需单独修复，不能把门禁少建一个空间当作产品容量问题已解决。
- 最终 Python 语法与 `git diff --check` 通过；四件套 `/tmp/seekdb-regression-add-{bootstrap,sql,direct3,tls}.log` 均 exit 0 且有 PASS。仅测试文件改动，无二进制重编；已提交并推送 `415d525dc`，`.scratch/` 仍不入 git。

- 按用户最新要求，不再运行完整 mysqltest 或 sysbench。`namespace_inprocess_prototype.py` 的四件套增加聚焦断言：bootstrap 覆盖 USING HASH/BTREE 的所属 namespace 元数据和视图列；SQL 覆盖 child `SET GLOBAL` 应拒绝的权限模型；direct 覆盖四路 `UNION/EXCEPT`、跨步骤重命名后索引读取、非空 IVF 与 FTS 创建/查询/重启及 IVF 删除。原有断言继续覆盖 `SHOW WARNINGS`、虚拟 schema 表、冷登录指定库、动态采样、TRUNCATE、堆表 INSERT、二级索引 JOIN、唯一索引冲突文案、生成列分区索引、空表 ALTER INDEX、MINOR FREEZE 等曾失败路径。
- 离线编译 `/tmp/seekdb-ticket07-ivf-drop-build3.log` 成功；四件套 `/tmp/seekdb-ticket07-ivf-suite-{bootstrap,sql,direct2,tls}.log` 全部 exit 0 + PASS。新的集合查询首次断言错误地期待 `EXCEPT` 保留重复行，按 SQL 去重语义修正后 direct 复跑通过。
- `minitest`、`trx_timeout`、`update_behavior` 原始用例的 child `SET GLOBAL` 1227 是预期权限拒绝，四件套明确断言拒绝；这些原用例需要管理连接才能按原结果跑完整。本次保留既往吞吐差距记录，不将未跑的 sysbench 场景计为通过。

## 2026-09-24 IVF PQ 崩溃回归纳入 direct 门禁

- 曾触发 SIGSEGV 的 child IVF_PQ 20 行近似查询已加入 direct 门禁，并覆盖重启后查询及 DROP INDEX。修正进程内扫描批量上限后，ns1/child IVF_SQ8、IVF_PQ 聚焦生命周期和四件套均通过：`/tmp/seekdb-ticket07-ivf-variants-final.log`、`/tmp/seekdb-ticket07-pq-frame-{bootstrap,sql,direct,tls}.log`。
- 按用户要求不运行完整 mysqltest、sysbench；既往失败场景继续以四件套中的聚焦断言维护。未覆盖的完整用例集与性能差距保留为 TODO，不视为当前后续工单的阻塞。

## 2026-09-24 源空间删除回归纳入 direct 门禁

- direct 新增活跃连接时 DROP NAMESPACE 拒绝、删除后同名 CREATE NAMESPACE 成功、旧子空间仍能读取原源数据、新同名空间无旧用户库的断言。`/tmp/seekdb-ticket12-namespace-command-direct-drop.log` exit 0 且 PASS；包含重启和异步 GC 的完整源删除专项另见 `/tmp/seekdb-ticket12-namespace-command-drop.log`，同样 PASS。

## 2026-09-24 旧目录模板迁移纳入 bootstrap 门禁

- bootstrap 模拟缺少 `__template__` 的旧数据目录，覆盖 ns1 用户库、`test` 用户表、系统库用户表/视图/独立过程、账号与角色的隔离，以及迁移后创建空空间、登录和重启。最初迁移错误地删除默认 `test` 库，已改为清空重建；最初遗漏独立过程和账号，已按 schema/用户目录补齐。最终 `/tmp/seekdb-ticket12-template-role-bootstrap.log` exit 0 + PASS。

## 2026-09-24 迁移续做与历史别名纳入 bootstrap 门禁

- bootstrap 的第二个隔离实例先把一个旧目录的暂存模板清理到一半，再将 ns1 历史名称改为 `a` 并重启；断言续做复用原暂存 ID、清掉剩余库/账号/过程、保留 ns1 数据，新空空间登录后建表及再次重启读取均成功。原先只在聚焦脚本中通过的两条失败恢复场景现成为四件套固定回归。`/tmp/seekdb-ticket12-resume-alias-bootstrap-final.log` exit 0，最后输出 `PASS`；增量离线编译和其余三门禁 `/tmp/seekdb-ticket12-resume-alias-{build,sql,direct,tls}.log` 均 exit 0，三门禁也均输出 `PASS`。

## 2026-09-24 子空间 IVF 后台加载纳入 direct 门禁

- direct 门禁现在设置 IVF 后台加载时段，在子空间非空 IVFFLAT 建索引后、首次近似查询前，轮询向量缓存虚拟表，要求所属 namespace 的物理 tablet 出现 `cache_type=0;count=2`。原先该缓存一直为空；聚焦 `/tmp/seekdb-ticket07-ivf-async-cache-flat-final.log` 和四件套 `/tmp/seekdb-ticket07-async-gate-{bootstrap,sql,direct,tls}.log` 均 PASS。
- 既有 direct 门禁继续覆盖此前崩溃的 20 行 IVF_PQ 近似查询和重启后查询。PQ centroid 的子空间后台预热仍是工单 07 TODO，不以 PQ 查询成功冒充预热成功。
- 查询侧缓存曾以未编码的本地 tablet ID 为键；现在从会话 runtime 编入 namespace。direct 门禁又增加查询后所属子空间 PQ centroid cache `count=40` 的断言，防止查询重新写到裸本地 ID 的进程级缓存。最终离线编译和四件套 `/tmp/seekdb-ticket07-ivf-cache-query-key-build.log`、`/tmp/seekdb-ticket07-cache-key-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。
- 随后补上首次近似查询前 PQ centroid 后台预热 `count=40` 的断言。旧版红样本 `/tmp/seekdb-ticket07-pq-cache-red.log`、修复版聚焦 `/tmp/seekdb-ticket07-pq-cache-green.log` 和最终四件套 `/tmp/seekdb-ticket07-pq-cache-gate-{bootstrap,sql,direct,tls}.log` 记录了从空缓存到后台加载完成的变化；完整 mysqltest/sysbench 未运行。
