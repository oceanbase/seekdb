# 两批后续工作

目标：完成用户批准的两批工作（基线 a73ae2b0c）。只提交生产代码并推送现有分支，不提交测试/文档，不提 PR，不跑全量 mysqltest/sysbench。

## 验收范围

1. 已物化 tablet 的写入不获取创建专用 LOB schema，不复制完整 ObTableSchema；使用固定版本的原有写入参数。
2. DDL 已有 table/schema 信息向下传；缺失物化才反查映射；同一操作复用 schema 和 tablet 对应关系；8000 分区场景验证没有按目标 tablet 重复复制整个分区列表。
3. 公共 LOB 读取支持调用方缓冲区；行外向量去掉新增的整值临时副本和 memcpy，仍保留 Namespace/实际事务/保护。
4. 统一逻辑准备入口与访问保护绑定；私有化底层解析/物化接口，核对所有消费者及资源释放次序。
5. 删除只等待相关请求/资源；无关长扫描不拖住删除；孩子读取父的物理来源、基线任务、历史来源及并发回收仍受保护。
6. 新回归加入本地四件套，保留历史失败用例，最终生产编译/聚焦验证/四件套和源码审查，通过后生产代码提交推送并核对远端 SHA。

## 实施中

- TabletBinding 改为借用不可变逻辑 schema，在实际首次创建时才加载 LOB family；按操作构建并复用 tablet 序号映射。
- EngineWrite 共享持有请求的 SchemaGuard，避免 Guard 生命周期结束后 schema 指针悬空；写参数直接读取逻辑定义。每批验证改用集合。
- ensure_tablet 的 schema provider 仅在物理存在性、owned、事务加锁复查之后执行。创建接口已接收独立物理 tablet ID，无需改写逻辑表定义。
- MetadataTabletPreparation 在一批元数据操作内固定 Guard，复用 binding 及 tablet→binding；已有物理对象不查询映射/SchemaService。
- 原生 ObBatchCreateTabletHelper::add_table_schema_ 删除不必要的整个表定义副本；直接转换只读 schema 为创建所需物理描述。
- 公共 LOB full-data 接口支持调用方目标缓冲区；Namespace adapter 和原生实现均支持；向量表达式使用该入口。
- 第二批还未实施。设计审计重点：请求归属与实际物理来源区分；物理回收候选只排除实际忙对象；解析和来源保护之间不得出现回收窗口；同一 TabletAccess 复用不能丢掉仍使用的来源；基线任务保护目的与源链。

## 当前执行/失败记录

- build-1 在仓库根目录误运行 make，无 Makefile；立即改为 skill 要求的 build_release，未初始化新 build。
- build-2：/tmp/seekdb-tablet-preparation-build-2.log。发现自动编辑误删 add_arg_to_batch_arg 的两个闭括号，已修复；本轮其余任务仍运行，不能重启同一编译目录。统一执行句柄 25936。
- 新 unrelated drop 回归：tablet_access_isolation_probe.py，旧生产二进制执行中，日志 /tmp/seekdb-tablet-isolation-red.log，句柄 40608。

尚未声称任何批次验收完成，尚未提交。

## 后续进展

- build-2 已结束（失败），build-3 编译通过。生产阶段一二进制 /data/1/tmp/seekdb-tablet-preparation/seekdb-batch1。
- shared_transaction_probe（普通组）通过：/tmp/seekdb-tablet-preparation-smoke-1.log。
- 冷普通/FTS/2048维 IVF 构建及重启通过：/tmp/seekdb-tablet-preparation-index-2.log。index-1 是复制二进制尚未完成时启动造成 Text file busy，未进入数据库测试；已等待复制完成后补跑。
- 旧二进制确实复现无关 DROP 等长查询结束：/tmp/seekdb-tablet-isolation-red.log。原断言失败，未缩小检查。
- 第二批已落地第一版：TabletAccessProtection 记录来源 Namespace 与实际物理源链；DROP 只等待该 Namespace 的请求；GC 与短暂解析/注册窗口互斥，跳过被引用物理候选，不再等全进程计数归零。复用上下文累计保护实际资源直到原生上下文释放。基线任务持有目的及其未完成的源链。
- raw resolve_read_tablet/ensure_tablet/check_table_access 私有化，仅 TabletAccess 可调用；LOB 显式传请求所属 Namespace，物理 locator 来源单独跟踪。
- DDL 调度轮增加 batch 接口复用同一版本的 MetadataTabletPreparation；逐项错误仍分别返回，用来避免8000目标各自重新建立整表索引。
- build-4 与补编 build-5 均成功；build-5 二进制正在复制到 seekdb-batch2（句柄65940），必须等复制完成后运行测试。
- 新本地原生探针 tablet_binding_native_probe.ipp 构造8000逻辑分区并逐一解析，断言返回同一schema指针；已接入 native_probe_injection.py 和原生门禁等待标记。尚未执行。
- isolation用例已加入本地四件套，并扩充父Namespace逻辑删除时孩子仍读取其物理来源、关闭孩子后再删除的检查。

剩余：第二批动态验证/并发缺陷修复；原生8000分区与准备次数/缓冲区证据；聚焦多分区DDL；全部四件套（保留所有历史失败用例）；最终审查/只提交生产代码/推送核对。

## 第二批动态证据及新发现

- build-6 生产候选 /data/1/tmp/seekdb-tablet-preparation/seekdb-production-v1；构建日志 /tmp/seekdb-tablet-preparation-build-6.log。
- 无关删除及父物理来源保留通过：/tmp/seekdb-tablet-isolation-fixed-2.log。无关删除约7ms；父Namespace删除时孩子仍在扫描，读结果完整。fixed-1 最后关闭客户端后立刻 DROP reader 被异步会话清理拒绝，已沿用既有 drop_namespace helper 只等待活动连接释放；核心并发断言未改变。
- 历史物化/fork/GC确定性交错通过：/tmp/seekdb-tablet-preparation-history-concurrency-1.log。
- 历史来源、基线完成、回收、pins清理、两次重启通过：/tmp/seekdb-tablet-preparation-history-gc-1.log。
- 原生probe-v1：/data/1/tmp/seekdb-tablet-preparation/seekdb-probe-v1，/tmp/seekdb-tablet-preparation-probe-build-1.log 编译通过；原生KV四阶段崩溃恢复/共享事务/快照pin/8000分区全部通过，日志 /tmp/seekdb-tablet-preparation-native-1.log。8000分区逐项解析同一逻辑schema引用，第一轮约1857us，仅说明这个原生准备调用，不是完整DDL基准。
- 新增 tablet_preparation_probe.py（32分区+LOB+索引+列DDL+2048维行外向量+重启），加入四件套。初次在冷孩子全分区UPDATE返回1210，/tmp/seekdb-tablet-preparation-partitions-1.log；旧a73生产二进制相同失败：/tmp/seekdb-tablet-preparation-partitions-baseline.log；NO_PARALLEL对照也失败，原始场景保留。
- 根因定位：ObDASDomainUtils::build_ft_doc_word_infos 在DAS PX线程无StorageSessionScope时无条件读取 active_worker_storage_space 并调用 storage_access_mode；即使没有任何FTS相关索引也失败，发生在物化及写入准备之前。当前修正为SQL执行上下文在创建DAS任务时传入NamespaceRuntime，FTS信息使用它，不依赖TLS猜测。没有Namespace 1退避，也没有分区/语法特例。该修正正在build-7编译（统一执行句柄见会话，日志 /tmp/seekdb-tablet-preparation-build-7.log）；需原始跨分区用例证明。
- 原生产v1 FTS DROP COLUMN + ADD COLUMN 5轮通过：/tmp/seekdb-tablet-preparation-fts-1.log。
- 原生、事务故障、准备计数三种本地hook已从生产源码移除。native_probe_injection.py 增加8000分区用例；preparation_probe_injection.py 可观测首次binding准备次数、映射反查、LOB直接填充目标缓冲区。probe-v1有这些hook但仅原生用例已运行，准备次数/向量目标缓冲区动态计数尚未验收。

## 最终候选审查及上下文补齐

- EngineWrite 去除遗留 RPC 序列化大小检查以及每次遍历整表 tablet 的成员检查；请求已经是进程内固定 schema 的 SQL 执行计划，Namespace 路由检查仍保留。避免暖写再次遍历8000分区。
- 审查补齐普通索引 DAS runtime 注入（不能只在 domain index 时赋值），保留 LOB iterator 原有初始化检查，并传播原生 LOB 参数初始化失败。
- production-v2 中间版本因普通索引 runtime 未注入，在启动系统包加载返回 NOT_INIT；保留 /tmp/seekdb-tablet-preparation-partitions-2.log，已修正。
- build-9 production-v3 与 probe-build-2 的业务源码哈希一致，manifest: tablet-preparation-production-v3.json。bootstrap/sql/tls 门禁通过，gate-results/tablet-preparation-final-v3。
- partitions-3 命中既有 bootstrap MDS 相对超时溢出：scan timeout=-9223372036854775790，属于之前明确的独立 TODO，并非本次修复。失败日志保留。
- partitions-4 冷孩子32分区 CREATE INDEX 通过；原始 UPDATE 从1210推进至1235，定位后续 prepare_execution 在 StorageSessionScope 之前访问 serving_namespace，PX中未绑定造成 send_logical_schema=false。已将会话scope移到写准备入口（build-10），原场景继续验证，不缩小分区数或改默认提示绕开。
- 本地回归增加普通索引 UPDATE/DELETE/ROLLBACK 与 FTS UPDATE/DELETE，核对 stderr 已由服务重定向到 seekdb.log，准备次数与直接缓冲区标记仍从该文件采集。

- build-10 production-v4 原始32分区UPDATE、暖UPDATE、冷索引构建、列DDL、普通索引UPDATE/DELETE/ROLLBACK、2048维向量、重启全部通过：gate-results/tablet-preparation-final-v4-retry/direct.log。首次direct启动仍命中既有MDS相对超时溢出，保留final-v4失败日志。
- native-v2 四阶段KV恢复和8000分区探针通过；共享事务故障测试的常量写并发断言暴露错误假设：A SET v=11、B SET v=v+1，在内部物化事务释放后B可能先获得用户行锁，B完成在A之前，此时最终11是合法串行结果。日志保留final-v3/bootstrap-native-kv.log。测试改为两个加1，并严格要求12（普通组原允许11的弱断言也移除），继续验证是否丢失更新，不放宽断言。
- 业务源码v4 manifest: tablet-preparation-production-v4.json，正在生成匹配probe-v3，四件套direct顺序推进中。

## 源码审查（生产v4）

- EngineWrite 的 schema 由共享 SchemaGuard 固定，execution/context/plan 先释放，binding 与 guard 后释放；MetadataTabletPreparation 的 binding 先于 guard 释放。FTS 原生 iterator 在 init 时复制所需列参数，不保存 ObTableSchema 引用。
- TabletAccess 复用累计请求NS与物理源链，EngineScan、DAS扫描、FTS iterator 和基线ctx均在释放原生资源后释放保护；创建provider在本地物理存在、owned、目录行锁复查之后才执行。
- metadata读锁覆盖解析到物理来源注册，GC写锁覆盖持久依赖过滤、活动物理候选排除和实际回收，避免保护注册窗口。DROP仅等待请求归属NS；父来源通过物理tablet计数保护，不连带阻塞父的逻辑删除。
- DDL batch保留逐项状态；完成回调可先到达，原有BUILD_SUCCEED检查阻止迟到调度结果覆写成功状态。部分分配失败由外层重试，已有DAG去重逻辑保持。
- LOB目标缓冲区检查容量，空值/原有无目标缓冲区路径保持；复用原入口初始化检查，初始化失败向上传递。DAS runtime由SQL任务所属session提供，不再从PX线程未绑定的StorageSessionScope推断。
- 生产v4动态：无关DROP约4.85ms时读者仍在跑、父DROP源对象保留、冷读取无物化、8轮并发首次插入、普通/FTS/IVF构建与重启、FTS UPDATE/DELETE均通过。

## 准备次数、直接缓冲区的有效动态证据

- probe-v3业务源码与production-v4 manifest逐文件一致；探针已移除，最终生产重编译成功。
- instrumented-1/2 保留失败：用例的向量恰好8192字节，表默认LOB_INROW_THRESHOLD=8192，所以没有经过行外读取；普通l2_distance也不足以证明vec_vector表达式路径。不能把早先2048维测试记成行外覆盖。
- 修正测试：表显式LOB_INROW_THRESHOLD=4096，保留普通l2_distance并显式调用vec_vector。/tmp/seekdb-tablet-preparation-instrumented-3.log通过：冷32分区DDL主表binding仅一次；暖UPDATE无创建schema准备和映射反查；继承/物化后行外8192字节向量均直接写入调用方缓冲区；全部SQL结果与重启通过。
- 匹配v4的bootstrap/sql/tls通过；native-v3完整共享事务故障组通过，两个并发增量得到12。刷redo后的rollback/crash组仍在继续。

## 验收收口

- 刷redo后的rollback_create/rollback_owned/crash_create/crash_owned恢复组通过。匹配当前源码的native-v3四阶段KV恢复、8000分区借用引用探针、共享事务全部故障组通过。
- instrumented-3已接入run_four_gates.py的bootstrap-native-kv步骤；本轮是同一业务源码的独立执行证据（该条接入发生在native门禁完成之后），下次统一入口会自动运行。
- 生产构建最终SHA256与已测production-v4逐字节一致：c137225798554488b3c7de5a54133fe72266a87370d5c4e140e3eb983c240702。probe-v3 SHA256：76f84e52bef7399bc7a9949c25c42f725eec34100f281d55c52f2bceb493888f。所有本地hook已移除。
- 当前startup/sql/tls/native均通过，direct最后全功能矩阵继续运行；其前面的定向历史、TRUNCATE、并发、FTS和故障恢复用例均已通过。

- 四件套全部步骤验收完成：final-v4/bootstrap.log、sql.log、tls.log、bootstrap-native-kv.log，final-v4-retry/direct.log（全部定向用例及最后inprocess_direct完整矩阵PASS）。
- 无新增Blocker/Major遗留；原有bootstrap MDS超时加法溢出仍单列TODO，本轮复现和复跑证据均保留；不把重跑通过表述成此旧缺陷已修复。
- 所有测试/文档/探针仅在.scratch，暂存区严格按v4生产源码manifest加入26个src文件。无PR。

- 无探针生产二进制最终32分区+真实行外vector用例及重启通过：/tmp/seekdb-tablet-preparation-production-final.log。
- 已提交并推送 a11059085249f624c0a69ad7bce5543f954f62ab 到 codex/namespace-worker-proxy-v20，仅26个生产代码文件。没有PR，没有测试/文档提交。工作树仅.scratch未跟踪。
