# 物理布局历史：逐项验收

日期：2026-10-10。范围依据 [设计](design-storage-schema-boundaries.md) 第1、9、10、11节及 [实施清单](implementation-storage-schema-history.md)；生产代码基线 `d11e76dae`。本次收尾未改变该生产基线的行为。

状态：实现和本轮约定的针对性验收已完成，正在提交推送备份。下列结论来自代码追踪及实际断言。历史失败与修复后的运行均保留在 [验证清单](schema-history-validation.json)。

## 一、实现要求与证据

| 要求 | 当前代码及实际证据 |
| --- | --- |
| 真实物理创建提交时间 C，身份不与继承 S 混用 | `ObTabletCreateDeleteMdsUserData` 的普通创建/物化提交回调写真实 commit_version；assign/reset/编解码携带 C。`PhysicalMergeCandidate::load` 读取已恢复的 C 与 create_transaction_id。`/tmp/seekdb-physical-merge-identity-test-2.log` 对实际提交回调及重启 MDS 值核对，并测试未提交与 C±1/C 的资格。 |
| 专用 schema tablet，原生事务/MVCC，mini/minor | `ObLS` 创建/复制/恢复内部 tablet，`ObAccessService` 提供独立 store；`StorageSchemaHistory` 原生 head+分块正文。`/tmp/seekdb-layout-history-commit-order-test-1.log` 完整布局140127字节、缩短正文、回滚、SQL共用事务、转储/minor及两次恢复通过。 |
| G 稳定、不编码 Namespace，按表分支共享 | `TableStorageLayouts::bind/prepare_create` 为上层归属分配 G；`ObTabletCreator` 在 CREATE MDS 前传入，存储仅保存/读取 G。`/tmp/seekdb-table-layout-test-1.log` 核对56个物理对象、索引/LOB、父子独立 G、后物化不覆盖已发布定义；8000分区 fork 无物理增量见下表。 |
| DDL、目录、完整布局及需要的物理元数据同事务 | `NamespaceSchemaPublication::stage`、`ObTabletCreator` 复用 `TableStorageLayouts::attach`，借用参与者留存到原生 SQL 事务结束。`/tmp/seekdb-layout-abort-crash-test-1.log`、`/tmp/seekdb-layout-commit-crash-test-1.log` 分别断言中止/提交后强杀恢复的目录、布局和读写；原生绑定用例覆盖竞争不等待及退休原子性。 |
| major/medium/meta 使用明确历史，保留 tablet 本地副本 | `read_layout_for_merge` 按 G@F；普通 medium 的目标丢失时重新取得已保护的目标与布局；meta major 持有 tablet 按 G/V 取完整正文。`/tmp/seekdb-physical-merge-layout-test-final.log`、`/tmp/seekdb-medium-layout-target-test-2.log`、`/tmp/seekdb-medium-layout-target-old-test-2.log`、`/tmp/seekdb-meta-layout-cold-child-test-1.log`。本地简化描述、源/目标跨G安装及行redo实际V另见实施记录，不把本地schema称为最新历史的缓存。 |
| freeze 先等待，最终复核与发布协调，超时请求终止 | `NamespaceFreezePreparation` 只检查；最终事务既有DDL锁、Namespace分配行锁、新快照复核后生成F。锁内不等接管，不新增持久名单。locked/waited_lock/delete/crash/daily/pressure 用例的精确证据见下表。 |
| 统一物理资格、进度与报告身份 | `PhysicalMergeCandidate` 同时供调度、`PhysicalMergeProgress` 与checksum资格使用；本机报告持久保存 incarnation/C/G，checksum保存 incarnation/G/V。`/tmp/seekdb-physical-report-native-test-5.log` 验证缺失/错身份/旧报告不放行；候选枚举与水位专项 `/tmp/seekdb-merge-candidates-test-2.log` 通过，见矩阵第3项。 |
| Namespace 上层逻辑校验，完整预期组与各表历史 V | `TableStorageLayouts::read_at` 取得所有绑定各自G@F的V；`ObMajorMergeProgressChecker` 显式装载参与owner的完整服务；`ObChecksumValidator` 读取单表历史、按实际绑定及incarnation构造主表/本地/全局/全文辅助组。缺项等待，不扫描数据重算。native-checksum、group-recovery及冷空间major用例见下表。 |
| 绑定退休，正文/G真实引用回收 | 上层退休绑定与目录同事务；`StorageSchemaHistoryFilter` 在本机schema最老文件minor中合并R时的逻辑根、G@R与本机物理根，仅删R前安全行。当前/旧链/flying/独立外部副本、跨G输入文件和实际复制视图均有释放前后真实minor及强杀证据。持续发布收集改为本次临时变化队列，8000分区一轮收集1.973秒成功；没有新增持久GC任务。 |
| 保留边界/重启/复制 | freeze持久记录、SQL GC fence、持久broadcast/last_merged与活跃读者共同保护；启动未恢复freeze view时保持base边界；完整复制完成前不启用布局过滤。`/tmp/seekdb-layout-retention-test-4.log` 证明无旧读者时保留、暂停且freeze行已消失的重启、读者交接及完成后释放。实际主备及提升见下表。SQL单表历史沿用原有显式历史行；本轮未加入清理这些行的路径。 |
| 删除全局 freeze.schema_version 及根/子两套旧检查 | `ObFreezeInfo`、proxy及内表定义移除字段；原get_freeze_schema_info/get_min_dependent_schema_version消费者删除；共同进度与显式owner校验替代旧分支。`/tmp/seekdb-layout-gc-incremental-production-major-1.log` 断言实际内表仅时间/F/data_version列、物理major和全局完成。仓内定向检索无旧接口消费。 |
| 成本及运行边界 | 8000分区的fork、首次服务、完整major、锁内复核及GC都有实际测量；没有常驻多版本缓存，没有共享化既有tablet schema，没有新独立DDL调度。额外成本包括布局序列化/日志、物理G/C字段、每个行事务callback保存V的8字节、按需读布局与本轮临时引用数组。 |
| 构建、四件套、备份提交 | 已运行及失败条件均由 `run_four_gates.py` 对应驱动覆盖；新增commit-order断言并入原生历史用例，新增冷空间major、候选枚举用例。最终无注入构建及生产实际major已通过，提交推送收尾见末节。本轮不运行完整mysqltest/sysbench，只推分支、不提PR。 |

## 二、原设计第10节验证矩阵

下表按原顺序逐项核对。测试控制点用于固定竞态或注入缺失输入；对最终文件、数据、恢复及完成水位的断言仍使用实际存储路径。不同阶段的证据只支持其断言范围。

| # | 原要求 | 已检查的断言及日志 |
| --- | --- | --- |
| 1 | F后fork/晚物化，旧轮排除、后轮使用子G | `freeze-preparation-locked-4` 确认F发布后fork成功且仍未物化；`physical-merge-layout-test-final` 实际C>F对象未读旧布局/无旧F文件，父子各用自己的G；真实standby-history两轮证明子G新DDL/major。所有创建使用共同C规则，未增加fork例外。 |
| 2 | C<F、C=F、C>F，未提交 | `creation_identity_native_probe.ipp` 对真实恢复对象逐项断言 participates(C-1)=false、participates(C)=true、participates(C+1)=true；原生提交前断言UNCOMMITTED不参与。日志 `/tmp/seekdb-physical-merge-identity-test-2.log`。 |
| 3 | 水位覆盖F后重新枚举 | `/tmp/seekdb-merge-candidates-test-2.log` 通过：暂停实际717对象旧名单并限制为未完成的一批；新DDL后下一轮第一批前重新枚举718对象并含新tablet。后续F低水位分支反复执行时未枚举/无major/未完成；解除后重新枚举含新对象，C<=F及实际文件/全局完成均断言。源码同时核对loop线程换轮reset与init_for_major先于get_next_tablet。 |
| 4 | C<=F但接管未完成 | `freeze-preparation-wait-2` 未完成时请求等待/超时且F不发布，后台完成后显式重试；`physical-report-native-test-5` 不完整物理状态不放行；实际备库沿F等自己的物理进度。 |
| 5 | 父子DDL/后物化/接管 | `/tmp/seekdb-table-layout-test-1.log` 的late_partition_no_overwrite；`/tmp/seekdb-local-schema-fork-test-4.log` 与 `...fork-wide-test-1.log` 覆盖父/子描述宽度方向；`/tmp/seekdb-meta-layout-cold-child-test-1.log` 冷父来源读取。 |
| 6 | 8000分区 | `/tmp/seekdb-layout-production-8000-test-1.log` 三次fork的physical_delta=0且目录根共享；`/tmp/seekdb-layout-history-service-8000-test-1.log` 首次服务；`/tmp/seekdb-layout-history-major-8000-test-2.log` 8000实际major、数据8000/31996000、锁内复核53259微秒。普通加列按表发布由共同发布入口及分区复用测试核对；未宣称同规模索引/接管压力已经测过。 |
| 7 | 不同Namespace相同table_id/局部V | 原生绑定同一seed先后prepare parent/child得到不同G；同table_id的父子独立DDL/布局及checksum分别读取自己目录。`/tmp/seekdb-table-layout-test-1.log`、`/tmp/seekdb-namespace-checksum-native-test-3.log`。 |
| 8 | 版本分配与提交顺序相反 | `/tmp/seekdb-layout-history-commit-order-test-1.log`：V21先提交，V20事务仍开着时读F分别得到旧V11/新V21；V20后来提交不改变既有读者，最新读及重启为V20/V21。不串行不同G。 |
| 9 | freeze/DDL交错与发布交接崩溃 | `/tmp/seekdb-physical-merge-layout-test-final.log` 真实F后父子DDL仍用各自旧布局；DDL abort/commit crash测试目录与布局一致；freeze locked-crash和retention交接测试保护不丢。 |
| 10 | 转储/minor、任务持久化、重启 | `/tmp/seekdb-layout-retention-test-4.log` 旧F跨mini/minor/两次重启；meta/medium目标专项及 `/tmp/seekdb-checksum-groups-recovery-test-1.log` 实际F文件与报告跨重启。缺失正文原生读返回错误，不取最新。 |
| 11 | DROP/TRUNCATE/等数量分区替换 | `/tmp/seekdb-namespace-checksum-native-test-3.log` 同分区数TRUNCATE更换物理ID后不把旧报告记PASS；`/tmp/seekdb-layout-retirement-native-test-2.log` 绑定退休旧快照保留、SQL/KV回滚、整Namespace退休；真实GC删表文件保护见第三节。 |
| 12 | 部分继承/物化及local/global索引 | freeze准备包含来源树所有适用对象；等待专项不就绪不发布；checksum专项真实local/global索引完整/缺项/错V/错行数；冷空间major案例含main/index/LOB。未以继承父F的checksum替代子输入。 |
| 13 | Namespace创建/删除跨F | `/tmp/seekdb-freeze-preparation-locked-4.log` 与 `...delete-1.log`；绑定按F的MVCC枚举、当前明确已删除owner退出、物理incarnation另判，Namespace ID不复用。源码`TableStorageLayouts::read_at`、`check_progress`及原生退休MVCC断言核对。 |
| 14 | 冷Namespace后台与校验激活 | `/tmp/seekdb-layout-history-cold-major-final-1.log`：首次子登录前4个main/index/LOB对象接管并释放源；此时无激活日志；随后实际major触发完整激活并完成，之后数据/index读及强杀恢复通过。 |
| 15 | 主索引报告先后/分区缺失 | `/tmp/seekdb-checksum-groups-recovery-test-1.log` 缺一组结果时其他组/owner仍检查，后继medium不覆盖待校验F，缺项与最终发布前两次强杀，最终完成后才补report_scn。 |
| 16 | 最终复核前后fork/等待分配锁 | `/tmp/seekdb-freeze-preparation-before-lock-3.log`、`...locked-4.log`、`...waited-lock-1.log`：锁后新快照看见先提交fork，后提交fork被阻到F后，普通DML7.64毫秒完成。 |
| 17 | 复核与DDL/删除/物化交错 | locked-4真实DDL等待而DML继续；delete-1新快照排除已删除owner；waited-lock-1不沿用旧名单。源码锁序：既有协调/fence/分配行，新只读快照不再获取Namespace根锁；读错向上传递。 |
| 18 | 准备/最终提交前后崩溃 | `/tmp/seekdb-freeze-preparation-crash-1.log` 最终提交前kill，无F、分配锁释放、显式重试成功；retention与checksum-group-recovery证明已提交F重启继续且完成前保护保持。不恢复单独准备任务。 |
| 19 | 超时结束，DBA重试 | `/tmp/seekdb-freeze-preparation-wait-2.log`、`...daily-1.log`、`...pressure-2.log`：用户超时返回具体namespace/tablet；定时仅一次；压力retry slot清除；都未发布F、不遗留续跑。 |
| 20 | 备库本机接管/回放及提升 | `/tmp/seekdb-layout-history-standby-final-4/schema_history.log` 实际obtest复制、12个主/本地/全局索引major、强杀恢复、升主新DDL/F2/checksum。`/tmp/seekdb-layout-gc-incremental-standby-2/layout_gc.log` 最终GC算法在不同本机引用下独立回收、恢复、升主再DDL/major。 |

## 三、最终GC修订的回归证据

- `/tmp/seekdb-layout-gc-publication-fixed-8000-1.log`：8000个实际major，持续627次CREATE/DROP期间真实minor回收无引用布局且保留活跃布局；一次引用收集1972780微秒，完整GC等待周期39.303秒。此前全量失效协议在8000/256分区持续发布均超时；修复后不靠停止发布才能成功。
- `/tmp/seekdb-layout-gc-capture-race-fixed-1.log`：实际同地址CAS也导致变化key补扫；回调元数据缺失错误保留，不被当作对象删除；两次重启。
- `/tmp/seekdb-layout-gc-reference-fixed-1.log`：真实删表，旧池对象/flying/两份独立外部副本逐步释放；每阶段实际minor，最后引用消失后正文/G删除并跨重启保持。
- `/tmp/seekdb-layout-gc-foreign-fixed-1.log`：孩子文件引用父G，父归属/当前tablet消失后仍保留；最后来源释放后回收，两次强杀恢复。
- `/tmp/seekdb-layout-gc-incremental-copy-2.log`：真实暂停完整复制，唯一源端复制视图保留旧tablet；释放前不删布局，结束后源端minor回收；备端另一复制表精确布局/数据与重启正确。
- `/tmp/seekdb-layout-gc-incremental-standby-2/layout_gc.log`：主库已回收时备库两份旧副本仍保护布局；释放最后副本、真实minor、重启、升主后的DDL/major均通过。

回收边界：每G保留下界及以上版本，允许保守多留中间版本；超时或未完成全量复制时本次不做过滤。精确正文消费者必须持有含该文件的tablet；单独SSTable句柄不是布局GC根。当前meta/fork/复制消费者已核对所有权。5秒收集期限检查在对象/文件访问之间，并非单次磁盘I/O强制中断承诺。

## 四、收尾与保留限制

- 候选枚举专项 `/tmp/seekdb-merge-candidates-test-2.log` 通过。首轮测试把布局trace读自process.out，实际trace在seekdb.log，导致热身major后的观察断言失败；改用已有增量Trace并等待日志落地后重跑，未改生产逻辑。首轮helper缺少头保护导致unity重复定义的编译失败也保留在验证清单。
- 最终无注入构建 `/tmp/seekdb-layout-history-production-final-build.log` 通过，SHA256 `3c1ee2c2c29cb96a48febbc55f82fa70bf50ccddd77d05c78df678450fdc1d1c`，与此前同生产源码二进制一致；`git diff --exit-code -- src` 无差异，临时hook已全部移除。
- `/tmp/seekdb-layout-history-production-final-major.log` 通过，F1791599177587389006的真实物理major及frozen/broadcast/last一致。
- 本次驱动Python编译检查、`git diff --check`通过；新增候选用例及增强后的原生历史/冷空间major/主备GC用例均有四件套入口。
- 提交推送备份正在进行；完成后以分支 `codex/namespace-worker-proxy-v20` 的本地HEAD与远端HEAD核对为准，不创建PR。

本轮未运行完整mysqltest/sysbench，未测8000分区同时带同规模索引和继承接管压力。没有引入SQL历史行清理机制；已有显式历史行继续保留。共享各tablet常驻schema、独立DDL调度、实例私有存储均保持原先划定的后续范围。
