# DDL/rootserver namespace 审计记录

状态：进行中。历史设计文档的“152”是当时默认当前版本 schema guard 的调用点数量，不是 152 个已确认缺陷。当前源码在 `src/rootserver/**/*.cpp` 有 168 个 `get_runtime_schema_guard(` 调用点，其中 161 个只传 guard、4 个传两个参数、3 个传三个参数。与历史 152 可对照的是当前 161，而 168 是所有参数形式的总数；两者都不能单独作为缺陷数。

## 审计方法与边界

逐条追踪 DDL 请求、异步任务和重启恢复的 namespace 来源，核对 schema service、SQL proxy、tablet ID 与 MDS payload 是否指向同一 namespace。进程级运行状态和系统配置允许使用全局服务；表、索引、任务记录和 tablet 元数据必须使用所属 namespace。动态检查同时比较子空间、父空间及重启后结果。

本轮静态筛查中，`get_runtime_schema_guard(` 最集中的文件是 `ob_ddl_operator.cpp`（41）、`ob_ddl_service.cpp`（17）、`ob_index_build_task.cpp`（10）、`ob_ddl_redefinition_task.cpp`（8）和 `ob_constraint_task.cpp`（8）。直接使用 `GCTX.schema_service_` 获取 guard 的三处在本轮变更后仍位于本地管理服务的系统变量校验（2）和 major freeze 的 server runtime 检查（1）；它们读取进程级状态。其他调用不能仅凭函数名认定安全，仍需沿各自调用链核对服务归属。

### 168 个 guard 调用点的静态归口

| 目录或文件 | 数量 | schema service 的来源 |
|---|---:|---|
| `ddl_task/` | 77 | 任务 `task_schema_service()`、所属 root service，或由任务显式传给辅助函数；scheduler 的 `start_redef_table` 本轮改为接收调用者 context |
| `ob_ddl_operator.cpp` | 41 | 构造时注入的 schema service；构造者使用所属 DDL 服务/辅助服务 |
| `ob_ddl_service.cpp` | 17 | DDL 服务初始化时注入；子空间服务由 `init_sql_worker` 绑定 |
| `freeze/` | 9 | 进程级 major freeze、server runtime 与 checksum 校验 |
| `parallel_ddl/` | 6 | 创建/删除辅助类初始化时注入所属服务 |
| `fork_table/` | 5 | 4 个全局 guard 用于 ns1 bootstrap/注册表/全局清理；1 个 fork task 用所属服务 |
| 对象权限 DDL operator、本地管理服务、PL DDL | 9 | 构造/初始化时注入；本地管理服务其中 2 个读取进程级系统变量 |
| 约束检查、bootstrap、索引 builder、truncate info | 4 | 所属 DDL 服务或显式参数；bootstrap 为 ns1 |
| **合计** | **168** | 此表只覆盖 schema guard 调用；SQL proxy、MDS、tablet runtime 另行审计 |

子空间服务来源可追到 `namespace_worker_inprocess_prototype.ipp:activate_in_process_namespace` 和 `ObLocalManagementService::init_sql_worker`；任务恢复在 `inprocess_refresh_schema` 中把同一组服务写入 `ObDDLTaskContext`。`ObDDLTask::task_schema_service()` 仅在 namespace 1 缺省回退全局服务。这个归口验证不代表每个 DDL 的其他依赖都已显式化。

## 已确认并修复：TRUNCATE PARTITION 保留全局索引

子空间自有分区表带全局唯一索引，开启 `_ob_enable_truncate_partition_preserve_global_index` 后执行 `TRUNCATE PARTITION`，此前返回 1146。`ObTruncatePartKeyInfo` 解析分区表达式时从 `GCTX.schema_service_` 取 ns1 schema guard，却用它查子空间表；现在从 `ObDDLService` 显式传入所属 schema service。

继续执行后，`SYNC_TRUNCATE_INFO` MDS 中的 index tablet ID 仍是逻辑 ID，存储事务返回 4725。namespace 写边界现在转换该 payload 中的 tablet ID。对于继承自父空间的全局索引，先按 fork 快照把索引 tablet 物化到子空间，再登记 truncate MDS，避免修改父空间物理 tablet。直接改用异步重建索引的尝试卡在 checksum validation，因此没有保留。

四件套的 direct full DDL 回归覆盖子空间自有表和继承表两种全局索引截断；每种都强制走索引查询，继承表还检查父空间数据，重启后重复核对。独立最小复现的通过日志为 `/tmp/seekdb-ddl-audit-truncate-green2.log` 和 `/tmp/seekdb-ddl-audit-truncate-inherited-materialized2.log`。本轮最终编译及四件套均通过：`/tmp/seekdb-ddl-audit-build-final.log`、`/tmp/seekdb-ddl-audit-gate-{bootstrap,sql,direct-final,tls}.log`。

## 后续审计入口

- `ObRedefCallback::modify_info` 的队列未命中检查已改用任务所属 SQL proxy；缺失 child context 时显式报错。
- `start_redef_table` 已从本地管理服务接收 DDL context，用所属 schema/SQL 服务建任务，并把 context 附到新任务记录；该入口在当前 SQL 四件套中没有直接可触发语句，尚缺动态专项验证。
- 同组 `abort_redef_table`、`finish_redef_table`、`copy_table_dependents` 经 `modify_redef_task` 时仍在全局 SQL proxy 读写任务记录；当前源码没有从 SQL 到这些 legacy RPC 的调用链，但公开的本地管理服务入口仍可收到子空间任务 ID。下一轮需统一传入调用者 context 并核对队列未命中分支。
- 建任务前的 compaction checksum 检查读取进程级虚表。尝试使用 child SQL proxy 后，现有子空间 `CREATE INDEX` 返回 1235；因此已恢复全局读取。还需核对该进程级错误表中 child 对象 ID 的编码与隔离，不能只凭 SQL 建索引成功认定校验语义正确。
- `ObPartitionExchange::update_table_all_monitor_modified_` 过去用全局 SQL proxy 读统计、在所属 DDL 事务中写统计；现读写均用同一事务。交换分区虽非 seekdb 核心功能，该错误跨越了通用 namespace/事务边界。
- `ObDDLTaskUtil::get_domain_index_share_table_snapshot` 的离线重建分支原用全局 root/schema/SQL 服务。显式主键、FTS 索引、两行数据的子空间表执行 `ALTER TABLE ... MODIFY COLUMN v VARCHAR(20)` 时，父任务 type 1001 在复制依赖索引阶段返回 `OB_ERR_UNEXPECTED(-4016)`，客户端超时；同一 SQL 在 ns1 成功。临时阶段日志确认：ns1 schema guard 对子空间 rowkey-doc 表 ID 500020 返回空 schema。现在表/列重定义父任务都把所属 root service 传给 FTS/向量子任务的 snapshot 辅助函数；新建索引的 snapshot 入口也接收所属 root service。聚焦 `/tmp/seekdb-ddl-audit2-fts-route-focused.log` 和 `/tmp/seekdb-ddl-audit2-fts-column-focused.log` 均 PASS，两类重定义及重启查询已写入 direct full 四件套。临时诊断代码已移除。
- 其余默认版本 guard 调用和 DDL 任务族仍需逐项追踪，特别是异常、重试与重启恢复路径。历史的 152 项验收框暂不勾选。

本轮构建 `/tmp/seekdb-ddl-audit2-fts-sibling-build.log` exit 0；四件套 `/tmp/seekdb-ddl-audit2-final2-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。direct full 覆盖两类 FTS 重定义、恢复后 FTS 查询和既有 EXCHANGE PARTITION。完整 mysqltest/sysbench 未运行。
