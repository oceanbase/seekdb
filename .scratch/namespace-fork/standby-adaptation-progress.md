# Namespace 备库适配实施记录

目标：完成当前 fork Namespace 分支的备库适配，并在本项目 `tools/obtest` 下跑通 `t/stanby` 全部 8 个用例。TLS 用例前执行 `generate_wallet.sh`。只提交生产代码；测试、证书、配置、文档留本地，不提交，不提 PR。基线 `a11059085`。

2026-10-03 用户要求开始实施，撤销此前备库 HOLD。本目标不仅要求旧主备场景通过，还需补充 Namespace 注册恢复、在线 fork/DDL/物化追随、历史来源保留和角色切换的聚焦证据。

## 验收清单

- [x] `basic.test`（首版 91s exit 0，最终代码还须复验）
- [x] `cascade_standby.test`
- [x] `one_primary_multi_standby.test`
- [x] `standby_restart.test`（首版 134s exit 0，最终代码还须复验）
- [x] `standby_sstable_replay.test`
- [x] `switchover_roundtrip.test`
- [x] `failover_switchover_reentry.test`
- [x] `tls_standby.test`
- [x] Namespace 主备聚焦：初始/重启装载、动态新增/fork/删除、schema 与映射刷新、父 TRUNCATE/DROP 历史来源、只读访问不物化、升主后各空间继续写入和 fork。
- [x] 生产编译、定向回归、原有四件套及过程失败用例保留。
- [x] 生产差分审查、仅代码提交与推送现有分支、远端 SHA 核对。

## 初始环境及执行

- 在当前项目运行，已有 `build_release` 增量编译通过，日志 `standby-results/build-red-1.log`。
- `tools/obtest/conf/configure.ini` 从模板生成，主备均本机，`port_base=42000`；`cleanup=0`、`cleanup_after=0`，只清理本次实例。
- 数据目录 `/data/1/nijia.nj/test/namespace_standby_20261003_v1`。完整用例清单和环境记录见 `standby-results/environment.json`。
- `generate_wallet.sh` 已执行，`openssl verify` 返回 OK；日志 `standby-results/wallet.log`，私钥不写入日志或提交。
- 首先运行原始 `./mytest t/stanby/basic.test` 建立失败反馈，日志 `standby-results/basic-red-1.log`。

## 首轮失败证据与修复

- 原始 `basic.test` exit 255；物理建备与启动回放成功，读取 `__all_core_table` 返回 `-4002`，SQL 监听尚未启动。
- 最小重启复现：`python3 .scratch/namespace-fork/standby_restart_probe.py startup-stage-red`，exit 1。临时阶段探针确认失败在 TabletAccess 内的 KV 映射扫描：`tx=0 latest=1 valid=1 ret=-4002`。
- KV 扫描仅对有效事务 ID 设置 read_latest 后，重新执行 `startup-kv-fix`：原始 -4002 消失，local services/start service 成功；客户端仍因 `sys_package_ready_` 未设置而超时，exit 1。这是独立就绪缺口，不将其计作绿色。
- 正在将注册表只读装载与模板初始化拆开，在备库 metadata ready 阶段完成只读装载后发布握手就绪。主库原有初始化仍负责持久修复。
- 临时 `[DEBUG-standby-*]` 探针将在定位结束后清除。以上失败命令和原始 basic 须进入本地四件套；尚无完整 obtest 用例 PASS。
- `startup-ready-fix` exit 0：同一恢复目录重启，远程 SQL 成功返回 STANDBY（PID 3044994）。正在重新运行未修改的 basic。

## Namespace 持续追随反馈

- 在 primary 在线 fork `standby_child`，standby 已到达对应 SCN，登录该空间仍返回 1049；`standby-results/dynamic-ns-red.json`。根因是此前 standby timer 只刷新初始空间 schema，不恢复新增空间的注册记录。
- 增加纯装载函数，复用 timer 核对清单及已激活空间的 schema；未激活空间按需装载。
- schema 装载固定 KV 事务快照，在同一 SCN 与已发布版本范围读取 SQL schema；看到 active/pending DDL 标记时返回可重试结果，不清除主库正在回放的标记。
- 备库跳过本地不可更新的 LIVE/owned 缓存；封定回放后的升主准备清理缓存并重置全部已激活空间的 max-ID cache。
- `[DEBUG-standby-*]` 已全部从生产源码清除。第二轮生产编译通过（`build-ns-follow.log`）。
- 扩展 Namespace 夹具第一次失败属于 obtest 将裸 FORK 识别为 deploy 命令；已使用 eval 强制 SQL，保留 ns-setup-red/follow-1 日志，正在复验。

## 在线刷新错误反馈

- `ns-suite-1`：原始建备+Namespace 夹具通过；前四项（冷读、动态新增、物化含 LOB、子 DDL）通过，父 TRUNCATE/DROP 后轮询失败。
- 紧凑反馈 `standby_refresh_race_probe.py` 在同一备库连接中反复建表/截断/删除与查询进度。旧二进制先返回 5627 Schema try again，紧接着连接关闭；复现日志 `refresh-race-1.log`、`refresh-race-connection-red.log`。查询前置 refresh 失败时 need_disconnect 默认 true，故可重试发布阶段会破坏连接。
- 修复：已装载并覆盖现有 publication 的 schema 在下一次 DDL pending 时继续服务；真正冷装载返回可重试状态，ObMPQuery 对 OB_SCHEMA_EAGAIN 保留连接。
- 审查发现初始 Namespace 持续只读装载完成后，升主不会重新跑原有 bootstrap 写修复；补充每空间只读→可写的首次访问恢复状态与序列化，泛化初始空间待发布 schema 收尾，不用 ns_id 特判，也不提前打开公共写门。该点需补充主库丢失在 SQL/KV 发布中间阶段的故障证据。

## 级联升主与合并前置失败

- `suite-final-1` 原始 basic、一主多备、备库重启通过；cascade 在 db_s2 升主后的本机 LOAD_MYSQL_SYS_PACKAGE SUCCESS 检查失败。就绪标志此前错误地同时表示“复制来的 SQL 元数据可接受连接”和“本机系统包任务已执行”。改为 Registry 的接入就绪与原有 sys_package_ready 分开；备库不运行写任务，升主后原有本机任务照常执行。
- 原始 `standby_sstable_replay` 主库 major 持续未完成，紧凑命令 `standby_major_progress_probe.py --timeout 2` 返回 FAIL。gdb 排除 CREATE MDS/调度资格拒绝，确认编码系统 tablet 在 schedule_next_medium_primary_cluster 返回 OB_TABLE_IS_DELETED (-4279)。其逻辑 tablet ID 本应直接等于系统 table ID，却被送入仅含用户 owned 映射的路径。
- 修复位于 NamespaceForkKernelPrototype 的 tablet/table 转换边界，按解码后的逻辑 inner tablet 复用既有身份规则，未新增 Namespace 1 退避。
- 系统表问题同样导致 `switchover_roundtrip` 初始 major 卡住；两项 1000 秒等待主动终止，exit 143，不计 PASS。原始日志、紧凑失败和 gdb 证据保留于 standby-results；新生产二进制须重跑原始用例。
- Registry 核对将可删除 ID 清单在 KV 快照前取得，避免升主后本机 package 恢复与新建 Namespace 并发时，由旧快照注销新注册记录。
- 本地套件新增 publication_initial/child 及其 standby restart 组合；均在独立夹具中于 SQL/KV 发布中间阶段丢失 primary，检查升主收尾、写入、新 fork。尚待运行，不计完成。


## 实際校验入口及回归进展（04:52）

- 级联升主修复已动态通过：`suite-production-v2/cascade_standby.log` exit 0（188s）。bootstrap、SQL、TLS 三组及 direct 中原有定向探针均通过，见 `gate-results/standby-production-v2`。
- 系统 tablet 本体完成后，进度校验仍按逻辑 ID 查物理上报。第一次修改了未被主循环调用的 `ObMajorMergeProgressChecker::get_tablet_ids`，v3 仍失败；已撤掉该无效修改，转为实际 `ObChecksumValidator` 的统一地址转换入口。
- v4 普通 tablet 校验推进，但多个索引的批量校验直接调用存储 `ObTableCkmItems::build(table_id, ...)`，重新从逻辑 schema 取地址，剩余 4 个表卡住。紧凑失败见 `major-progress-native-v4-fixed-probe.log`；v5 普通、特殊内表、数据/索引及 FTS 均由校验器构造物理地址/校验项，存储层接口和 schema_id 保持原语义。
- `publication_initial`、`publication_child` 在主库 SQL 提交、KV pending 发布阶段杀主，备库升主后清掉标记、保留新列、继续写入与再 fork，均 exit 0。两种 standby restart 组合此前因测试夹具硬链接二进制的 `Text file busy` 提前失败，已改为复制临时文件再 rename；仍需重跑并不能计作通过。
- Namespace 完整定向追随 v3 通过（110s），含同连接 20 轮 schema 发布冲突、动态 fork、LOB 物化、子 DDL、父 TRUNCATE/DROP 历史读取、DROP 登录拒绝和各空间升主写入/fork。
- 本地 native 测试初次遗漏了 preparation 与 shared-transaction 两套独立注入器。KV 原生/崩溃恢复与 preparation 已通过，shared fault 的失败属于缺探针而非原子性失败；v5 已统一启用三套探针并重跑。生产源码中的探针已全部关闭。
- 不支持公开 Namespace RENAME，验收不再将其列作支持功能。


## 已通过的独立门禁与原始用例（05:05）

- production-v3：basic、cascade、一主多备、standby_restart、failover_switchover_reentry、TLS 均 exit 0。两套 DDL 发布丢主（未重启 standby）和 Namespace 全部聚焦用例通过。
- native-v5：原生 KV/多次崩溃恢复、8000 分区借用、preparation 统计、物化事务 rollback/crash/unknown commit/并发以及 flush-redo 组合全通过；`gate-results/standby-native-v5/bootstrap-native-kv.log`。
- production-v5：紧凑 major 复现 PASS；SSTable 原始用例运行的全过程 PASS（101s），并在其尾部再次断言冻结/广播/完成水位相等。
- SSTable 测试只改了一处地址获取：此用例固定运行 Namespace 1，把 SQL schema 的逻辑 tablet ID 转成物理地址后查询 V$OB_SSTABLES。未降低行数/值、复制 SCN、水位或只读拒绝断言。测试改动留本地，不提交。
- 已核对 production-v2/v3 与最终源码差异严格限于 3 个 rootserver/freeze 校验文件；其他生产路径相同。最终源码 patch 与 production-v5 的构建 manifest 完全一致。
- switchover_roundtrip、publication_restart_initial/child 仍在 production-v5 中执行；未写作通过。


## 原始备库验收完成（05:10）

- 8/8 原始用例全部通过；逐项 binary、耗时、日志保存在 `standby-results/original-standby-final-results.json`。
- switchover_roundtrip 157s exit 0，含正反两次切换、角色重启恢复，以及主备 3 轮冻结/广播/完成水位与 readable/sync SCN 一致检查。
- publication_restart_initial/child 的所有数据、发布标记、写入、再 fork 断言通过，但脚本退出时对旧 standby 连接重复 close，得到 Already closed，整体 exit 1。已用 ExitStack 注册新旧连接且仅关闭仍打开的连接，正在各自重跑，不以断言中途 PASS 替代成功退出。


## 最终验证完成

- 原始备库 8/8 PASS，扩展 Namespace 5/5 PASS。最终汇总 `standby-results/final-results.json`，记录每个用例实际二进制、退出码、耗时和日志，不把早期被中断或脚本失败的执行冒充成功。
- standby 重启后的 initial/child DDL pending 发布恢复最终各自完整 exit 0。SQL 新列、已复制的数据、升主后的写入、新 fork 和持久发布标记均已检查。
- 四件套 bootstrap（含 native KV/事务故障）/SQL/direct/TLS 全部组成项通过；direct 的新备库/major 项来自上述汇总，其余定向项见 gate-results/standby-production-v2。源代码差异核对证明最终增量仅涉及独立的 3 个合并校验文件，已用最终正式二进制跑过 major 与原始 2 个 major 场景。
- 生产 binary：standby-artifacts/seekdb-production-v5；source patch/sha256：standby-results/code-production-v5.diff、binary-production-v5.sha256。源码无任何本地测试探针；暂存的生产 patch 与该构建 manifest 一致。
- 测试入口：run_four_gates.py，direct 包含 primary_major_namespace_probe.py 和 run_standby_suite.py（8 原始 + 5 扩展）；TLS 自动生成并校验证书。SSTable 单处测试地址适配、所有测试/文档/配置/证书均不提交。
- 此次修复当前备库适配及已证实的 Namespace 地址/就绪/追随/恢复缺口。Namespace schema 版本与全局冻结的终极方案、实例共享内表、本机私有存储及设计矩阵中尚未动态覆盖的长期 GC/任务交错保留原 TODO，未声称全覆盖。


## 交付

- 提交并推送：`a04ffaf18480f4a9a0aa94805e5e1b6d13e03db8`，分支 `codex/namespace-worker-proxy-v20`，远端 SHA 已核对。
- 22 个 src/ 生产文件，376 增/47 删；提交内容与已测试 production-v5 源码 manifest 完全一致。
- 未创建 PR。SSTable 测试单处地址适配、其他本地测试/门禁/文档/配置/证书均未进入提交。
- 汇总：8/8 原始备库 + 5/5 扩展 Namespace 场景，全四件套组成项通过；见 standby-results/final-results.json、artifact-manifest.json、remote-verification.json。

## 2026-10-04：KV 扫描统一使用普通快照

- 提交 `91e641c0512e927a315bb349ae7fa03e40775c36`：删除按事务 ID 设置 `read_latest` 的分支。每次扫描取得当前事务写入序号，普通读保留固定提交快照，锁定读使用获取行锁后的快照。
- 原来用 `put → put → scan` 论证必须开启 `read_latest` 不成立；此次定向测试确认普通快照已能读到最后一次自身写入，同时隔离其他事务后续提交。
- 本地四件套原生 KV 探针新增连续两次 put 后点读/扫描、固定快照、锁后最新值及扫描重入写拒绝断言。原生全探针在首次启动及三次崩溃恢复后均通过，包括 SQL/KV 共用事务和合并后快照。
- 生产版 `basic`（87.25 秒）、`namespace_fork_local`（84.18 秒）均 exit 0；后者含 20 轮 DDL 刷新、在线 fork、物化/LOB、父 TRUNCATE/DROP 后继承读及升主写入。
- 源码差分、二进制 SHA256、编译和测试日志见 `kv-snapshot-results/`。只提交一个生产文件，测试与文档保留本地。
