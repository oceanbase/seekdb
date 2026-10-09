# Namespace 架构当前主要问题

更新日期：2026-10-03。完成状态核对至 `a11059085`。本文记录当前仍开放的七类问题，供架构取舍和后续工作排序使用。备库适配按用户指示 HOLD。文档留在本地，不提交分支；本次整理没有重新执行旧故障用例。

Namespace 目录已迁到 InstanceMetaStore，生产代码已删除 `__fork_proto_meta` SQL 控制库及其名称过滤。Schema、SQL proxy、计划/PS 缓存、自增、统计、DBMS 调度和表锁的主要全局入口已清理。下面的问题不能被这些已完成项替代。

## 七类问题与当前进度

| 问题 | 具体缺口 | 已明确的方向和当前进度 |
| --- | --- | --- |
| 1 实例共享元数据归属 | Namespace 目录迁 KV 只覆盖第一批数据。其他共享内表仍可能通过 Namespace SQL 目录创建、写入并随 fork 继承，`__all_freeze_info` 是已讨论的例子。SQL 本身还会产生附带内表更新。 | 按持久数据域提供通用存储与目录，实例共享目录不进入 Namespace schema 和父链。复用现有 LS 的事务型 KV 基础，其他模块尚未整体接入。 |
| 2 实例管理入口及配置作用域 | 人需要标准客户端查询元数据；大量内部操作也需要易用的存取接口。现有登录先绑定 NamespaceRuntime，再通过其 SchemaService 认证，不能直接作为独立实例管理上下文。配置及管理语法的作用域、权限、持久化和复制策略尚未系统定义。 | 受限元数据 SQL，复用现有 Rust MySQL 协议；独立实例认证和轻量会话。配置按 Namespace、实例共享、本机作用域声明。parser/执行器、表声明、账号和连接分派未实现；虚拟表已存在，但其最终可见性策略未定。 |
| 3 实例私有持久化 | 不应复制到备库的本机数据目前依赖 SQLite。现有 LS KV 会被物理复制，不能直接承担这类数据。 | 需要通用本机存储。具体引擎及 WAL/checkpoint 实现尚未选择；用户已要求暂放一边。共用逻辑存取层不等于共用复制日志或事务。 |
| 4 冻结及大合并 schema 边界 | Namespace 各自的 schema 版本不能互相比较。全局 freeze 记录的根 schema 版本不能约束其他 Namespace；当前 tablet storage schema 又不是完整、可按 SCN 查询的布局历史。 | 近期记录各 Namespace 的正确冻结边界；长期由 DDL 事务发布完整物理布局历史，合并从存储侧按 SCN 取布局。长期方案为后续 TODO，近期修复尚未实施。fork 初始布局与旧 freeze 的适用条件仍需具体化。 |
| 5 后台服务归属及生命周期 | 向量调度器、adapter 缓存、异步 change stream 的 Namespace 归属改造尚未完成。历史上复现子索引未发现、adapter 不齐、手动任务超时、async 子空间写入及根近似查询超时；不能据旧二进制结果认定这些症状在新物化入口下仍完全相同，需重验。删除 Namespace 后服务组仍保留到进程退出。 | 按 schema/逻辑对象工作的状态归 Namespace；LS、物理日志读取和执行资源共享。任务携带 owner，支持取消、排空和销毁。DBMS_SCHEDULER 已按 Namespace 持有，真实任务执行和待执行任务重启恢复仍缺验证。逻辑 DROP 不再等待无关 Namespace 的长读，但这不等于 Runtime 已可销毁。 |
| 6 fork 整表锁与 DML 冲突覆盖 | fork 整表锁路由不加入 tablet；逻辑 TABLE 键与 DML 的 TABLET 键是否通过其他机制冲突尚未动态核实，特别是锁持有期间才物化的新 tablet。 | 保留待与用户核对的语义问题，先复现再确定处理。表锁服务归 Namespace 与孤儿锁 GC 已完成；父 TRUNCATE 历史来源、物化事务及 DDL 入口收敛也已完成，不能继续算到此项。 |
| 7 备库追随、恢复和切主 | 同一 LS 可承载复制，但只读启动、Namespace/schema 在线追随、DDL 发布中间态、本机接管及历史回收、升主恢复仍需适配。 | **HOLD（用户 2026-10-03 确认）**。静态核对及四个工作包已写入 [备库适配方案](design-fork-namespace-standby-adaptation.md)，未实施、未运行主备验收。 |

## 已完成，不再计入 TODO

- `50557ddbe`：父 TRUNCATE/DROP 后冷后代的历史来源解析、首次 schema delta 基准、物理对象与数据历史保留、基线依赖及回收协调；四件套和定向恢复验证已完成。
- `a9bf8740a`：物理主/LOB tablet 创建、映射与 KV owned 共用一个原生内部事务，消除该物化流程的双提交窗口。任意 SQL 与 KV 混合事务不在本项范围，仍按此前约定留待实际需求。
- `a73ae2b0c`：采用保留继承读的方案 A，统一 TabletAccess；删掉两套写策略、DDL 提前物化分支及物理 AccessService 的 Namespace hooks。
- `a11059085`：借用固定逻辑 schema，创建专用 schema/LOB 绑定按需获取，批次复用准备，8000 分区避免逐 tablet 复制整表定义，行外向量直接填入调用方缓冲区；访问保护按请求 Namespace 与实际物理源划分，无关长读不再阻塞 DROP。
- 主要 Schema/SQL proxy、DDL、计划/PS 缓存、自增、统计、DBMS_SCHEDULER 和表锁服务的全局入口清理已有交付；实例 KV 和通用诊断虚拟表已实现。后台归属、最终可见性和剩余入口审计仍分别列在开放项中。

## 具体缺陷与验证收尾

| 项目 | 当前状态及下一步 |
| --- | --- |
| Bootstrap MDS 超时加法溢出 | 多轮验收重复观察到，近 INT64_MAX 的相对超时加当前时钟后变负，导致 bootstrap -4002。未修复；需修正超时转换并加入确定性回归。其他历史 -4002 尚不能都归为此原因。 |
| COM_RESET_CONNECTION 后读表失败 | 历史记录为初始和子 Namespace 重置连接后读已有表返回 6002；需追踪事务描述符生命周期并在当前版本重验。 |
| DBMS_SCHEDULER 的执行与重启恢复 | 服务拆分已完成，目前已有过期任务归属及状态更新证据；真实执行、未完成任务重启续作仍待验证。 |
| 剩余服务入口及异常路径审计 | ServiceSlot 全表、异步任务 owner 传递、旧 RPC 队列未命中和任务异常路径的动态覆盖仍需收尾；不能把共享物理服务一概判为应拆分。 |
| fork 的运行状态继承规则 | 需明确任务定义与执行占有、推进位置、会话和锁记录哪些继承、哪些在孩子重新初始化。尚未逐类定案。 |
| 历史 binary prepared execute 故障 | 旧记录有 -4006，但后续三空间 binary prepared SELECT 已通过。原场景需核对是否仍有不同触发条件，不作为当前已确认故障重复列报。 |

## 状态解释

1、2、3 是数据归属和管理接口问题；4 是合并正确性问题；5、6 是逻辑与物理职责及 fork 完整性问题；7 是复制恢复的实施缺口。它们相互影响，但不应靠一个包含全部服务的新 Runtime 解决。

另有 DDL/rootserver 全路径审计、bootstrap 偶发 `-4002`、连接 reset 后事务异常等具体收尾。历史文档中的中断 DROP PRIMARY KEY、EXCHANGE PARTITION、IVF 预热/失败清理和 FTS 重定义已有后续修复与聚焦验证，不能再从旧段落把它们列作当前未修缺陷。完整 mysqltest/sysbench 按用户要求不运行。

更进一步的设计问题是：数据属于 Namespace，并不自动意味着它适合被 fork。表定义、用户数据与任务定义的继承，必须区别于执行占有、会话和锁状态。整体重构方案将这个维度作为显式设计项，具体任务继承策略仍需核对。

## 关联记录

- [整体架构重构方案](design-namespace-architecture-rebuild.md)
- [服务归属 TODO](todo-service-ownership-followup.md)
- [全局服务入口清理](todo-remove-global-namespace-service-access.md)
- [实例元数据 SQL 通道](design-instance-metadata-sql-channel.md)
- [Tablet 物理 schema 历史](design-tablet-storage-schema-history.md)
- [父 Namespace TRUNCATE 的冷后代读取复现](repro-parent-truncate-cold-descendant.md)
- [实例 KV 实施记录](instance-meta-progress.md)

现有实施记录含按时间追加的历史状态。发生冲突时，应结合最后的业务切换记录及当前源码判断，不能仅用文档开头的“未切换”描述判定现状。
