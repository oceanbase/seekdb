# DDL/rootserver namespace 审计记录

状态：进行中。历史设计文档的“152”是当时默认当前版本 schema guard 的调用点数量，不是 152 个已确认缺陷。当前源码在 `src/rootserver/**/*.cpp` 有 168 个 `get_runtime_schema_guard(` 调用点；这个数字包含已经由所属服务显式路由的调用，也不能单独作为缺陷数。

## 审计方法与边界

逐条追踪 DDL 请求、异步任务和重启恢复的 namespace 来源，核对 schema service、SQL proxy、tablet ID 与 MDS payload 是否指向同一 namespace。进程级运行状态和系统配置允许使用全局服务；表、索引、任务记录和 tablet 元数据必须使用所属 namespace。动态检查同时比较子空间、父空间及重启后结果。

本轮静态筛查中，`get_runtime_schema_guard(` 最集中的文件是 `ob_ddl_operator.cpp`（41）、`ob_ddl_service.cpp`（17）、`ob_index_build_task.cpp`（10）、`ob_ddl_redefinition_task.cpp`（8）和 `ob_constraint_task.cpp`（8）。直接使用 `GCTX.schema_service_` 获取 guard 的三处在本轮变更后仍位于本地管理服务的系统变量校验（2）和 major freeze 的 server runtime 检查（1）；它们读取进程级状态。其他调用不能仅凭函数名认定安全，仍需沿各自调用链核对服务归属。

## 已确认并修复：TRUNCATE PARTITION 保留全局索引

子空间自有分区表带全局唯一索引，开启 `_ob_enable_truncate_partition_preserve_global_index` 后执行 `TRUNCATE PARTITION`，此前返回 1146。`ObTruncatePartKeyInfo` 解析分区表达式时从 `GCTX.schema_service_` 取 ns1 schema guard，却用它查子空间表；现在从 `ObDDLService` 显式传入所属 schema service。

继续执行后，`SYNC_TRUNCATE_INFO` MDS 中的 index tablet ID 仍是逻辑 ID，存储事务返回 4725。namespace 写边界现在转换该 payload 中的 tablet ID。对于继承自父空间的全局索引，先按 fork 快照把索引 tablet 物化到子空间，再登记 truncate MDS，避免修改父空间物理 tablet。直接改用异步重建索引的尝试卡在 checksum validation，因此没有保留。

四件套的 direct full DDL 回归覆盖子空间自有表和继承表两种全局索引截断；每种都强制走索引查询，继承表还检查父空间数据，重启后重复核对。独立最小复现的通过日志为 `/tmp/seekdb-ddl-audit-truncate-green2.log` 和 `/tmp/seekdb-ddl-audit-truncate-inherited-materialized2.log`。本轮最终编译及四件套均通过：`/tmp/seekdb-ddl-audit-build-final.log`、`/tmp/seekdb-ddl-audit-gate-{bootstrap,sql,direct-final,tls}.log`。

## 后续审计入口

- `ob_ddl_scheduler.cpp:516`：`ObRedefCallback::modify_info` 在队列未命中后到 `GCTX.sql_proxy_` 查询任务存在性。需要验证子空间任务恢复期间的队列未命中分支是否会错误报告任务消失。
- `ob_ddl_scheduler.cpp:1078`：建任务前的 compaction checksum 检查仍取全局 schema/SQL 服务；先确认适用任务类型和子空间触发条件，再改为请求所属服务。
- `ob_ddl_scheduler.cpp:1588`：`start_redef_table` 从全局 schema/SQL 服务准备目标表和任务；调用来自本地管理服务，需要核对外部 RPC 的 namespace 传递方式。
- 其余默认版本 guard 调用和 DDL 任务族仍需逐项追踪，特别是异常、重试与重启恢复路径。历史的 152 项验收框暂不勾选。
