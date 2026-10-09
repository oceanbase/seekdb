# 事务共用：实施与验收记录（本地，不提交）

目标：物化的 CREATE MDS、LOB 绑定、序列、SQL tablet 映射和 KV owned 共用一个原生事务。基线 50557ddbe。用户授权完成、验证并只推送生产代码分支；不提 PR，不提交测试和文档。

## 实现

- InstanceMetaStore 增加 attach/detach，借用活跃原生 RC 事务；借用模式拒绝自行 commit/rollback，析构只清理自己的注册。
- 内部 SQL 连接作用域桥接取得实际原生描述符，恢复连接绑定及存储会话，操作后同步 SQL 事务视图。
- 物化工厂锁内按最新持久状态检查；CREATE、绑定、序列、映射与 owned 一次提交。所有失败显式结束 SQL 事务，之后再解除 KV 快照及 GC 保护。
- 删除 repair_owned 双提交补偿；DDL 独立发布及恢复仍由其现有流程承担。
- 成功路径增量更新 owned 缓存，避免逐 tablet 创建时反复全量加载造成平方成本。失败/结果不确定及已经被并发请求完成时使缓存失效。晚到的成功发布不得覆盖已加载的墓碑。
- 故障测试发现回滚后的 CREATE 对象等待 GC，立即按相同物理 ID 重试被误判为重复创建。原生创建检查复用 GC 的已回滚对象判定与带对象身份校验的删除；底层不增加 Namespace 语义。

## 已有证据

- release 首编通过：`/tmp/seekdb-shared-tx-build.log`。
- 生产首次写入、DROP Namespace、回收通过：`/tmp/seekdb-shared-tx-smoke.log`。
- 原生 KV/SQL 借用事务初次验证通过，含四轮进程启动及恢复：`/tmp/seekdb-shared-tx-native.log`。新增 INSTANCE_SHARED_TX_PROBE_PASS 在每次启动均为门禁条件。
- 故障组通过：`/tmp/seekdb-shared-tx-faults-v4.log`。物理创建后失败、owned 后失败、这两个阶段崩溃、提交后崩溃、提交成功但应答超时、并发请求、先取得旧快照再锁内检查已提交创建均已覆盖。重启检查先于孩子登录，排除 schema 发布补登的干扰。

## 失败与定位

- `/tmp/seekdb-shared-tx-faults.log`：测试进程抢在二进制复制完成前启动，ETXTBSY；后续使用已完成复制的新文件运行。
- `/tmp/seekdb-shared-tx-faults-v2.log` 及 `/tmp/seekdb-shared-tx-rollback-red.log`：已回滚物理对象仍在 tablet manager，check_pure_data_or_mixed_tablets_info 仅按存在性拒绝相同 ID 重试，返回 4016。修复后 v4 通过。该用例保留在四件套。
- 最初自增断言要求下一值为 2 不正确：fork 拷贝序列高水位，下一分配值可为 1000001；用例改为验证大于已有 ID、连续两次分配单调增加且父空间不变。
- `/tmp/seekdb-shared-tx-faults-v3.log`：并发 UPDATE 的继承扫描在进入物化工厂前返回现有 EAGAIN；用例保留“拒绝读未提交副本、之后重试成功”的验证，并增加两次 INSERT 与 before_lock 暂停，真正覆盖创建竞争和先取得旧快照的请求。

- `/tmp/seekdb-shared-tx-redo.log`：强制提交 CREATE redo 后回滚，重试仍超时。已确认重试持有 Namespace 访问共享保护，旧空壳任务对所有候选都要求排空此保护，导致从未提交的对象也无法回收；native MDS merge 对已回滚对象返回 TABLET_NOT_EXIST，不能替代空壳转换。已修正并使 redo 回滚两例通过：区分已回滚创建与已提交删除，前者直接走原生空壳/GC 流程，后者继续完整历史引用保护。相关强制 redo 失败用例已加入四件套。

## 交付状态

- 已落盘 redo 的中止创建恢复分支：已完成。
- 最终生产与本地插桩二进制、本地四件套：已完成；direct 的既有 bootstrap 失败保留，续跑通过。
- 最终差分审查、证据清单、生产代码提交和分支推送：已完成。

- `/tmp/seekdb-shared-tx-redo-v2.log`：强制 redo 的两次回滚均通过，崩溃重启后失败。最小复现 `/tmp/seekdb-shared-tx-recovery-observe.log` 在 30 秒内反复检查，无已提交副本/owned/映射，但读取持续 4023。原生回放重建事务仍为 for_replay；ObLSTxService::activate 只 online，没有结束无最终日志的遗留事务；定时 GC 则明确跳过 for_replay。正在原生本地日志激活阶段补齐回放完成后的持久化 abort，避免在 Namespace 中猜测/补偿事务结果。

## 恢复修正与最终验收

- `/tmp/seekdb-shared-tx-recovery-v6.log`：强制 redo 的 crash_create、crash_owned 通过，恢复后无需客户端重试即读取继承数据；重新物化、主/LOB/序列、再次重启均通过。测试中的观察用重试循环已经移除。
- 原生本地日志激活在回放完全结束后，将无最终决议的回放事务转到正常持久化 abort；等待行和 MDS 回调结束后才 online。不添加 Namespace 判断，不靠目录补登记。
- 已回滚 redo 对象的空壳候选保留 ObTabletHandle；转换时在 bucket 锁内校验对象身份，防止候选 ID 指向后续重建对象。已提交删除继续使用历史引用保护。
- 最终差分审查覆盖唯一事务拥有者、视图同步、失败显式回滚、GC 注册释放、缓存发布、回放收尾和对象身份；当前无未处理的 Blocker/Major。动态门禁尚在执行，以完成后的结果为准。
- 最终门禁目录：`gate-results/shared-transaction-final/`；生产构建：`/tmp/seekdb-shared-tx-final-production-build.log`；插桩构建：`/tmp/seekdb-shared-tx-final-probe-build.log`。

- 最终 direct 首轮在 fresh bootstrap 被既有超时溢出中断：`/data/1/tmp/seekdb-ns-probes/namespace_fork_PROTOTYPE_parent_truncate_ffjg1g4h/log/seekdb.log:5847` 的 MDS 扫描 timeout=`-9223372036854775807`。与 TODO 中 2026-10-02 的 read_raw_data 超时加法问题一致，相关源文件本轮未改。原日志保留 `gate-results/shared-transaction-final/direct.log`；从父 TRUNCATE 场景继续的结果在 `gate-results/shared-transaction-final-resume/direct.log`。不把重新运行当作此旧问题已修复。

最终 bootstrap-native-kv、bootstrap、sql、tls 和 direct 续跑全部通过。完整证据与生产差分 hash 见 `shared-transaction-acceptance.json`。20 个生产源文件；测试、插桩、评估和日志保留本地，不提交。不运行完整 mysqltest/sysbench。

提交并推送 `a9bf8740a85b8b83319f76ec4bb9562ae2ec83cd` 至 `origin/codex/namespace-worker-proxy-v20`。通过 gh 配置认证后执行 git push，gh API 确认远端 SHA 与 HEAD 一致；未创建 PR。提交只含 20 个 src/ 下生产文件。已跟踪工作区干净，`.scratch/` 保持本地未跟踪。
