# 四项审计问题收敛

目标：目录页 GC、全量拷贝停机、Namespace 维护职责与显式物理上下文、DDL 发布增量化。

约束：GC 不新增跨轮进度或保护名单；同次调用内快照扫描，短排他阶段核对完整根集合后删除，变化则放弃。停机立即取消 gRPC，再 join。复用调度资源，移除物理 MDS 的隐式 thread-local scope。DDL 按实际绑定变化发布，不逐种语句加判断。

验收要求：GC 标记期间并发读写、根变化拒绝删除（含不变 schema_version 的物化）、正常自动回收和旧视图；拷贝持有期间正常停机；冷后台接管不激活 SQL Runtime；物化/DDL/删除同事务及主备；8000 分区描述 DDL 不逐 tablet 取物理状态，绑定变化仍正确。用例纳入四件套，保留失败证据；不跑全量 mysqltest/sysbench。代码、文档、测试提交推送当前分支，无 PR。

## 验收状态

四项生产修改及定向验收已完成；代码、测试与文档作为本批分支提交交付，不创建 PR。新增/修改用例已接入四件套；本次执行对应的定向用例，没有执行完整四件套、mysqltest 或 sysbench。证据索引为 `four-issues-evidence.json`。

| 项目 | 当前实现 | 验证 |
| --- | --- | --- |
| 目录页 GC | 普通快照扫描，短排他阶段核对完整根集合后限量删除 | 扫描期间并发写、同 schema 版本换根、Namespace 增删、断引用/损坏描述、历史读、自动调度和三次崩溃恢复 |
| 长 RPC 关闭 | gRPC stop 立即取消并等待 handler 释放原生视图 | 真实模块 stop：旧方式 20 秒未返回；修复后 6.49 秒完成且 peer 仍存活 |
| Namespace 职责和物理入口 | 独立维护对象复用定时器；显式原生事务入口；物理 GC 保护回调注入 | 17 个主/索引/LOB/全文/向量 tablet 冷接管，147.58 秒完成并回收父来源，SQL Runtime 未加载；原子回滚、恢复和主备 |
| DDL 发布 | 按描述版本和实际绑定差异更新 | 8000 分区的 4 个描述 DDL 均零物理状态查询/零来源更新；分区增删/TRUNCATE、建删索引/LOB、并发 DDL、提交与回滚 |

## 2026-10-09 过程记录（按时间先后，最终状态见验收表）

- 自动 GC 第一批回归通过：`four-issues-automatic-gc-retry.log`。目录旧页从当前集合删除后，RR 历史读仍正确；父 Namespace 删除、SIGKILL 恢复通过。首次启动因复制二进制尚未完成产生 `ETXTBSY`，保留在 `four-issues-automatic-gc.log`，不是产品失败。
- 停机验证纠正：SIGTERM 本来就触发 SIGKILL；SIGUSR1 的 `ObServer::wait()` 在 stop 标志后直接 `_Exit(0)`。两者均不经过 `ObGrpcServer::stop()`，不能算服务关闭路径的红/绿证据。早期 `four-issues-shutdown-*.log` 保留。新增仅测试版本的 hook，在 SIGUSR1 的退出前调用真实 `ObStandbyModule::stop()`；另提供旧版无期限 Shutdown 开关做红测。生产退出方式不改变。
- 物理入口：创建、删除和自增序列复制显式接收原生事务；物化绑定仍在同一原生事务登记。删除 `PhysicalTabletMdsScope`、线程局部深度、`StorageSpaceHandle::PHYSICAL_MDS` 及相关分支。SQL 桥只负责借用所属原生事务和同步 SQL 事务描述。
- NamespaceMaintenance 自己拥有扫描位置和任务生命周期，复用已有 shared timer；空壳 tablet 任务不再直接调用 Namespace 内核。物理回收保护由组合入口注入，保护持续覆盖筛选和实际回收。Namespace 物理删除的扫描位置也由维护模块持有，移除函数静态状态。
- DDL 发布比较前后绑定，未变化的绑定不再查询物理状态或更新来源树；描述按 schema 版本更新。仍有与分区数相关的内存遍历/比较，本项未承诺整个 DDL 为 O(1)，不新增缓存或 DDL 类型白名单。
- 新增 GC 边界用例：扫描中并发写入成功；schema_version 不变的根变化、Namespace 新增/删除都拒绝本轮删除；断引用对象及大描述分块回收、损坏引用拒绝删除保留。现有 native 测试改为通过 `InstanceNamespaceDirectory::collect_catalog_pages` 执行完整调用。
- 新增 `ddl_publication_cost_probe.py`：8000 分区，初始/子 Namespace 的 COMMENT 与 ADD COLUMN，断言来源更新与物理状态查询都为零，并验证随后单分区写入隔离；加入四件套。
- 构建失败证据：`four-issues-physical-maintenance-build.log` 是新增头文件路径拼错；`four-issues-native-build.log` 是测试头文件在 namespace 内引入及变量重名。已修正，`four-issues-native-build-retry.log` 构建中。

上述后续修改仍待测试版本及正式版本验证，不表示四项目标完成。尚未提交或推送。

### 已取得的定向证据

- `four-issues-native-build-retry.log`、`four-issues-production-build.log`：测试/正式二进制构建通过。正式产物保存为 `/data/1/nijia.nj/test/namespace_fork_build_artifacts/background-materialization-artifacts/seekdb-four-issues-production`。
- `four-issues-service-stop-red.log`：真实模块 stop 路径，开启测试用旧无期限 Shutdown，客户端持有拷贝视图时超过 20 秒未返回（预期红测）。`four-issues-service-stop-green.log`：立即取消版本 PASS，6.49 秒内退出（含原 prepare_stop 的 5 秒等待），原生视图释放、peer 仍活着。没有改变正常进程的 `_Exit`。
- `four-issues-ddl-cost-retry.log`：8000 分区，两个 Namespace 的 COMMENT/ADD COLUMN 共四项全部 PASS；每次 descriptions=1、source_updates=0、physical_status_lookups=0。之后孩子单分区写入隔离正确。首次脚本错误读取 process.out，实际上 stderr 已重定向 seekdb.log，修正后重跑通过。
- `four-issues-ddl-atomic.log`：孩子 TRUNCATE 的 SQL 元数据、来源根及原生 DELETE 同时提交；提交边界 SIGKILL 后恢复通过。
- `four-issues-shared-atomic.log`：原生物化 CREATE/LOB 绑定、自增序列、KV 来源更新同事务，flush redo 后回滚和崩溃恢复通过。
- `four-issues-automatic-gc-final.log`：新 NamespaceMaintenance 驱动自动目录 GC，RR 历史读、父删除、SIGKILL 恢复 PASS。
- `four-issues-ddl-partitions.log`：主/LOB 绑定、COMMENT/ADD COLUMN、建删索引、TRUNCATE/DROP TABLE、ADD/TRUNCATE/DROP PARTITION、四个并发 DDL、父快照保留及 DROP DATABASE 全部 PASS。
- 原生四阶段脚本发现两个测试自身的问题，保留 `four-issues-native-kv.log` 和 `four-issues-native-kv-retry.log`：首次运行专属的 GC marker 不应要求在每次恢复重复；已完成 DROP 的测试 Namespace 可能已被后台回收，不能通过是否存在模板推断测试阶段。现在由 harness 显式提供上一轮已验证的阶段，验证删除记录和名称均消失。更新测试版本构建中。

### 实现边界

- GC 保留整图标记，30 秒调用预算、最多 256 个删除候选、最多 1 秒排他阶段。持续发布导致根变化时，本轮返回 EAGAIN；极大图和高频变更仍可能延迟回收，不承诺固定回收时限。未新增跨轮 GC 状态。后台物化的可丢失游标是已有机制，现在归维护对象持有。
- DDL 比较仍与已有分区数相关；删除的是逐 tablet 物理查询和重复来源发布。两份 schema 与比较容器均为本次事务临时对象，没有新增常驻 schema 缓存。
- Namespace 维护复用共享定时器，没有每 Namespace 线程或 SQL Runtime。任务停止先取消并等待结束，再停止存储依赖。

### 最终验证

- `four-issues-native-kv-final.log`：created → marked → finished → verified 四阶段及三次 SIGKILL 恢复全部 PASS；首次阶段包含完整 GC 并发边界断言。最后一轮允许后台已经回收的删除记录，并核对名称不存在；记录尚在时则核对 DELETED 状态及重复删除幂等。
- `four-issues-ddl-rollback.log`：孩子 DDL 的 SQL、来源树及物理 DELETE 回滚一致，之后成功重试及继承数据读取 PASS。
- `four-issues-cold-families.log`：17 个 physical tablet 在 147.58 秒完成冷接管，父来源实际回收；首次登录/崩溃重启后主表、索引、LOB、全文、向量查询正确，后台未加载 SQL Runtime。
- `four-issues-standby/results.json`：正式 `namespace_fork_local` 返回 0，113.92 秒，覆盖主备同步、父 TRUNCATE/DROP 历史来源、Namespace 删除准入、升主懒加载/续写和升主后 fork。
- `four-issues-production-final-build.log`：最终无 hook 正式构建通过；`git diff --check` 通过；源码无 LOCAL test hook、PhysicalTabletMdsScope 或 PHYSICAL_MDS 分支。
- 本节覆盖上面的历史“待验证/构建中”状态；所有新增测试的错误等待/日志路径/恢复阶段判断均已修正，早期失败日志保留且在证据索引注明性质。
