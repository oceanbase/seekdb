# fork 历史 tablet 解析与回收修复目标

日期：2026-10-02。状态：完成；最终 v4 验收全部通过，业务代码已提交并推送。

本文件及测试留在本地，不提交分支。当前代码基线为 `f2b9c159a`。

## 本轮目标摘要

1. **选对历史来源**：祖先 TRUNCATE、DROP、后续物化或修改，不改变后代 fork 时的视图；读取与首次写入使用同一套来源规则。
2. **保住真实依赖**：同时保护快照所需的行版本、物理 tablet 和尚未完成基线接管的源；GC、删除中间 Namespace 和重启后仍成立。
3. **解除后实际回收**：最后依赖消失后，旧物理 tablet、snapshot 与 pin 能释放，重启后不重新出现。
4. **补齐 DDL 发布与恢复**：首次 DDL 正确记录继承对象的删除；原生 DDL 已提交但实例 delta 尚未发布时，fork 不得穿过该窗口，崩溃后必须补齐发布再正常提供服务。
5. **控制改造成本**：保留父链和稀疏 EXCEPTIONS，复用已有生命周期记录；不新增逐 tablet 常驻历史列表、全量地址缓存或 fork 时的逐 tablet 地址清单。
6. **完成验证和交付**：失败用例进入本地四件套；只提交业务代码并推送现有分支，不创建 PR。

当前进度：子 Namespace 的 DDL 发布崩溃恢复已修复；最终 v4 生产构建、原生四阶段恢复和四件套全部通过。业务代码提交 `50557ddbe` 已推送到 `codex/namespace-worker-proxy-v20`，远端完整哈希已核对一致。恢复已移到完整 runtime 安装后的首次 schema 装载，成功发布并解除 pending 后才放行其他请求；首次装载保留调用方已有存储上下文。

## 一、目标

**让 fork 后的历史读取、首次写入和物理回收遵守同一套依赖规则。**

Namespace fork 后，后代按已确定的对象定义和数据快照读取。祖先后续的 TRUNCATE、DROP 和物化，不得破坏后代的历史读取或使其读到错误副本的数据。后代首次写入也必须从这个历史视图建立自己的物理副本。

历史读取涉及的物理 tablet、生命周期边界和行版本须保留到依赖解除；依赖解除后，原有回收流程能够继续清理。查询恢复成功、但 GC 后再次丢失数据，或者永久保留旧 tablet，都不满足目标。

### 用一个例子说明目标

A 的数据为 10；B 从 A fork 后写成 20；C 从 B fork。此后 B 执行 TRUNCATE：B 应为空，C 仍读 20，A 仍读 10。C 第一次写入加 100 后应为 120，且不影响 A/B。只要 C 仍依赖旧物理副本，GC 就必须保留它；完成基线接管或删除 C 后，按剩余依赖允许旧副本回收。重启和并发操作不能改变这些结果。

反向时序也须正确：如果 B 在 C fork 之后才写成 20，C 仍应读到 fork 时的 10。

### 本轮必须同时完成四项

| 工作 | 完成标准 |
| --- | --- |
| 历史来源解析 | 根据累计 fork SCN 选对物理副本，读请求与首次写入使用相同的来源规则 |
| 依赖保留与释放 | 直接继承和未完成基线接管的间接依赖均被保护，最后依赖解除后可以回收 |
| 并发与恢复闭合 | fork、物化、删除、读取和 GC 交错以及重启均不破坏上述规则 |
| 首次 DDL 基准修复 | 首次 delta 正确包含继承表，不能靠漏写删除标记使测试偶然通过 |

### 内存与 fork 成本约束

- 复用已有 tablet 生命周期记录中的创建、删除版本边界，不新增常驻的逐 tablet 历史状态列表。历史可读性由已有持久化记录与读取 SCN 判断。
- fork 只维护 Namespace 级父引用、快照边界、schema 版本和 pin，不增加与表数或 tablet 数成正比的地址清单生成。
- 普通读请求保持按父链查找，不新增全量 Namespace/tablet 扫描或全量常驻地址缓存。物理回收可以在现有 GC 批次中核对依赖；具体同步方式在实现中确认。
- 历史保留必然可能延长磁盘数据和物理对象的存活时间；该成本受真实依赖约束，必须随依赖解除而释放。

目标是修复当前已复现的正确性问题。下文区分已有动态证据、实现方向和待验证条件；尚未验证的 GC 与恢复路径不视为已完成。

## 二、已确认的问题

1. A → B → C 多代 fork，B 先执行注释 DDL，再 fork C。B 在 C fork 后 TRUNCATE，C 尚未物化时查询失败，内部 -4725 重试直到客户端 4012。只物化 B 也失败；物化 C 的对照通过。
2. 读解析用祖先当前 EXCEPTIONS 的删除标记直接阻断后代；物理探测固定按最新状态检查，无法发现仅在历史 SCN 下可读的已删除副本。
3. 对象回收的父链判断也按当前 owned/tombstone 截断，需要核对和修正历史依赖。
4. 未先执行注释 DDL的 TRUNCATE 对照虽通过，但首次 schema delta 的 `previous_count=0`，没有生成旧 tablet 删除标记。须定位并修正基准，不能依靠漏记删除事件通过验收。

最初失败、对照场景和历史追溯见 [复现记录](repro-parent-truncate-cold-descendant.md)。后续历史保留、重启、基线接管及并发验证见第八节；已有通过证据不代表完整验收已完成。

## 三、采用的实现方向与约束

- 保留父链与稀疏 EXCEPTIONS，复用物理 tablet 的创建、删除版本边界，以及已有快照 pin、基线接管和回收入口。
- fork 沿用 Namespace 级父引用、fork cap、schema 版本和 snapshot pin 登记；保持 fork 主流程不枚举用户表、索引和 tablet。
- 地址解析归 Namespace 访问边界；共享存储接收物理 tablet ID 与 SCN，不推断 Namespace，不增加 Namespace 1 的正常路径回退。
- table ID 和其他 schema ID 保持本地身份；物理 tablet ID 使用现有编码规则。
- 本次使用现有父链模型，不恢复完整 catalog/directory 树，不为每个 tablet 新建地址快照记录，不引入常驻状态历史列表或一份全量地址缓存。
- 修复若发现现有生命周期记录不足以表达必要的历史状态，先给出具体场景和证据，再讨论补充记录方式；不隐式扩大为目录树重建。

## 四、必须完成的改造

### 4.1 历史读取和物化来源解析

1. 分清请求所属 Namespace 的删除状态与祖先后来的删除状态。已删除的本空间对象不能通过祖先副本重新出现；祖先当前的墓碑不能直接阻断后代已继承的视图。
2. 沿父链累计读取上限，按对应 SCN 检查候选物理 tablet 的可读性；到更远祖先时继续取各层 fork cap 的最小值。
3. 复用 `check_and_get_tablet(..., snapshot_version)` 的生命周期检查。当前 `get_tablet_status(snapshot)` 只支持 MAX，不能将它误用为任意历史状态查询。
4. 区分真正未物化、尚未提交或暂不可见、历史已不可用等结果。必要历史已丢失时返回明确错误，不伪装成“没有副本”并静默选取别的来源。
5. 读解析返回的地址与快照上限必须用于实际行读取。读取保持无物化副作用；首次写入和物化从同一个正确的历史来源建立本空间副本。
6. 父空间在孩子 fork 后才物化的副本，按其代表的数据视图判断；复用已有逻辑可读起点，避免将物理创建时刻当作唯一可见性条件。

### 4.2 历史保留与物理回收

回收需同时满足三类保护：

| 保护对象 | 需要保留到何时 |
| --- | --- |
| 快照所需的行版本 | 对应持久快照与活动读取的保留要求解除 |
| 物理 tablet 对象及其生命周期元数据 | 后代的直接历史读取不再需要该对象，且活动读取已安全衔接 |
| 未完成基线接管的物化副本所依赖的源 | 基线接管完成，或副本删除且相关读取、后台访问已排空 |

1. 修正 `protect_snapshot_tablets` / `filter_unreferenced_tablets` 的依赖判断，覆盖多代链和已删除祖先的中间记录。
2. owned 表示本空间副本已发布，不等于源依赖已经解除。需要核对持久化的 `fork_info_` 与基线完成状态，保护仍被读取的源 tablet。
3. 把引用判断到实际空壳转换之间的并发协调闭合，覆盖 fork、物化发布、Namespace 删除和已有读取。保护结论失效时重试，不能按过期结论释放数据。
4. 快照 pin、祖先关系及物理依赖在重启后可恢复；释放与重试具有幂等性。
5. 最后一项相关依赖解除后，候选旧 tablet 能进入正常空壳及物理清理流程。验收同时检查“该保留时保留”和“该释放时释放”。

### 4.3 首次 DDL delta 基准

定位首次历史视图为何未包含继承表，使首次及后续 delta 都能够准确记录删除、创建和物理归属变化。修复后，无前置注释 DDL与有前置注释 DDL的 TRUNCATE 应遵守相同的历史读取规则。

## 五、验收条件

所有用例通过标准客户端访问真实引擎；检查逻辑数据、实际物化情况、生命周期状态和回收结果。

| 场景 | 必须满足的结果 |
| --- | --- |
| B/C 均未物化，B 在 C fork 后 TRUNCATE | B 为空，C 仍读到 fork 时的数据，A 保持原数据；覆盖有/无前置注释 DDL |
| B 在 fork C 之前物化并修改数据 | B 截断后，C 读到 B 在 fork 时的值；令该值与 A 不同，防止错误回退 A 也通过 |
| B 在 fork C 之后才物化并修改数据 | C 看不到 B 后来的修改；B 随后截断仍不破坏 C 的历史读取 |
| C 自己物化与首次写入 | 历史来源正确，C 写入不影响 A/B，已有快照边界保持 |
| 父空间 DROP、删表后同名重建 | 已有后代保留旧对象视图；新 fork 得到新对象视图，不混用旧 tablet |
| 源 tablet 已 DELETED但仍被依赖，触发 GC | 数据和对象继续可读，不变为空壳；重启后结果一致 |
| 物化副本的基线接管尚未完成 | 源仍被保护，读取与接管可正常推进；完成后按剩余依赖决定回收 |
| 多代 fork，删除中间 Namespace | 后代仍能解析祖先数据，持久引用和恢复完整 |
| 相关引用全部解除 | 旧 tablet 最终能够完成物理清理，恢复/重试不重新产生引用或泄漏 |
| fork、物化、删除、读取与 GC 交错 | 快照边界、来源选择及回收安全保持；至少用可控交错覆盖关键发布窗口 |

遇到过失败的用例全部保留在本地四件套中。现有冷孩子和仅父物化失败已经接入 `run_four_gates.py` 的 direct 入口；新增用例在实施时接入。按用户要求不运行完整 mysqltest 或 sysbench。

## 六、范围边界

本轮完成历史 tablet 解析、相关物化来源、依赖保留、回收协调及首次 delta 基准修复。DDL 物化触发入口的全面收敛、完整物理 schema 历史、备库实现、实例管理 SQL 和其他服务归属改造仍按各自 TODO 推进。

上述范围边界不豁免本轮正确性：如果相关入口、生命周期记录或发布顺序直接阻碍本轮验收，需要修到能够闭合语义，再说明具体新增工作。

## 七、完成标准与交付

- 当前已复现的失败变为通过，新增历史来源与回收场景通过，并保留日志证据。
- 从 source selection 到历史数据读取、基线接管和最后回收使用一致的依赖规则；仅恢复查询成功不算完成。
- 正常读取不新增全量 Namespace/tablet 遍历，复用已有生命周期字段与存储接口。
- 交付只包含业务代码提交，推送现有分支，不创建 PR；本文件、历史资料、测试和日志留在本地。
- 未验证的并发或恢复路径明确记录，不以已通过的查询用例替代 GC 与恢复验收。

## 八、实施证据（2026-10-02，持续更新）

- 加强红用例：B 在 fork C 前把数据改为 20（A 为 10），B 截断后 C 超时。日志 `/tmp/seekdb-ns-truncate-distinct-red.log`。修复后 C 读 20，首次写入后为 120，B 为空、A 为 10。日志 `/tmp/seekdb-ns-truncate-distinct-fix.log`。
- B 在 fork C 后才物化并改为 30，C 始终读 fork 时的 10；B 截断后 C 首次写入得到 110。此场景无前置注释 DDL，断言旧 tablet 墓碑已生成且删除边界为正。日志 `/tmp/seekdb-ns-truncate-late-materialization.log`。
- 多代链中删除 B Namespace，A DROP 后同名重建为 99：已有 C 仍读 20，新 fork 读 99；经过实际 GC 和强制停止后重启，C 仍读 20，首次写入为 120。最后删除 C，旧 A/B/C 均进入空壳。日志 `/tmp/seekdb-ns-history-gc-restart-birth.log`。
- 上一条初版最后回收失败，日志 `/tmp/seekdb-ns-history-gc-restart.log`：GC 检查早于建表的模板 fork 时，已转换的空壳不能再提供创建边界。墓碑增加已有物理副本的逻辑创建边界，解决误判；这是一条稀疏记录中的标量，不增加状态历史列表。
- 基线接管专项：旧 B 被截断并删除所属 Namespace，C 物化后继续读取 120；后台完成接管后，旧 A/B 已为空壳或已移除，C 仍读 120；最后删除 C，其 tablet 进入空壳。日志 `/tmp/seekdb-ns-history-baseline-completion-fast.log`。测试通过启动配置把正常调度周期从 120s 缩为 3s；先前 90s 等待未覆盖默认调度周期的失败日志 `/tmp/seekdb-ns-history-baseline-completion.log` 保留。
- 可控交错：暂停 C 的首次物化，期间尝试从 C fork 并等待 GC 执行，再放行写入。初版正常 UPDATE 收到新增的 4023 重试错误，日志 `/tmp/seekdb-ns-history-concurrency-retry.log`；访问入口等待 GC 窗口结束后通过，C 为 120，新 fork 为 120，后者独立写成 300 不影响 C。日志 `/tmp/seekdb-ns-history-concurrency-wait-fix.log`。此证据覆盖一个关键交错，不代表所有异常路径已经验证。
- 稀疏墓碑补充 `was_owned` 和 `create_scn`，启用原有 `drop_scn`。删除边界在原生 DDL 提交后取得；Namespace 的 DDL pending 标记阻止 fork 穿过尚未发布的边界。所有正常 Namespace 使用同一 schema lifecycle，初始空间仅 bootstrap 阶段允许未安装目录时的初始化行为。
- DDL 登记配对审计发现真实失败：原生 SQL 事务已启动但 schema 检查失败，回滚仍递减另一笔 DDL 的登记计数。真实 `ObDDLSQLTransaction` 用例观察到计数从 1 变成 0，日志 `/tmp/seekdb-ns-ddl-fence-red.log`。修复为仅成功登记的事务执行对应释放；原生测试覆盖过期 schema 的显式回滚、成功登记的正常回滚，以及登记失败后的析构回滚。该测试已纳入 bootstrap-native-kv 入口。
- 最终原生测试通过四阶段创建/标记删除/完成删除/恢复验证，以及每阶段间的 SIGKILL 重启；包含历史来源、缺失副本不得错误回退、暂不可用错误传递和 pin 重载。日志 `gate-results/20261002-history-final/bootstrap-native-kv.log`。
- 无临时测试注入的最终生产构建通过，日志 `/tmp/seekdb-ns-history-production-final-build.log`；bootstrap、SQL、TLS 三项已通过，日志位于 `gate-results/20261002-history-final/`。
- 发布恢复专项已验证初始空间：观察到 native TRUNCATE 提交后 pending_schema_version 高于已发布版本，FORK 等待；此时 SIGKILL。重启后旧孩子仍读 10、首次写入 110，新 fork 为空，墓碑恢复且无额外 pin。日志 `gate-results/20261002-history-recovery/direct.log`。
- 子空间用例在新的空实例上遇到 bootstrap 持续主键重复：冻结检测线程先建立 collection 7 协调水位，ensure_root 却无条件再插入。真实日志见该目录的子空间实例，确定性原生红用例见 `/tmp/seekdb-ns-bootstrap-coordination-red.log`。ensure_root 已改为复用已有协调记录并保持水位单调，新的原生 gate 首次启动已通过，正在重跑最终验收。
- 新用例已加入本地四件套 direct 入口，包含初始空间和子空间的“原生 TRUNCATE 提交后、实例 delta 发布前”崩溃恢复，以及最后引用/pin 释放后的重启验证。bootstrap 初始化顺序修复后的最终四件套、完成审计及代码-only 提交推送仍待完成；不能据此宣称目标完成。最终回收用例既等待进入空壳，也等待物理管理器查询不再包含旧 tablet，PASS 已直接记录 `physical=[]`；增强用例进一步核对 collection 3 的 snapshot 与 collection 8 的 pin 不复活。
- 初始化水位修复后的 v2 验证：原生四阶段、bootstrap、SQL、TLS 及 direct 前 12 个命令通过。加强后的最终释放用例验证旧物理对象移除、snapshot/pin 释放，并在重启后保持。日志位于 `gate-results/20261002-history-v2/`。
- 发布恢复专项：初始空间通过；子空间失败，查询曾成功但 `pending_schema_version` 仍为 INT64_MAX，说明恢复未完成。日志 `gate-results/20261002-history-v2-recovery/direct.log`。恢复函数返回值原先被忽略；改为传递错误后，重启登录报 4006，日志 `/tmp/seekdb-ns-child-publish-recovery-fix.log`。已定位恢复内部 SQL 早于 Namespace runtime 完整绑定；该问题仍需修复并重新验收。
- 子空间发布恢复已修复：恢复从服务构造阶段移到完整 runtime 安装后的首次 schema 装载，沿用原有 mutex/cv 和同线程内部 SQL 重入处理；装载完成标志覆盖 schema 刷新、delta 发布和 pending 解除。真实崩溃恢复后旧孩子仍读 20、新 fork 为空、首次写入为 120，日志 `/tmp/seekdb-ns-child-publish-recovery-v3.log`。v3 原生四阶段、bootstrap、SQL、direct（15 个命令）和 TLS 均通过，日志 `gate-results/20261002-history-v3/`。
- 交付核对删除了首次装载中不必要的 `IndependentStorageScope`；所属 SQL proxy 自行绑定 runtime，不覆盖已有请求的存储上下文。发布恢复用例加强为重启后先登录旧孩子并读取，再登录 DDL 所属 Namespace，避免验收依赖父空间先被客户端激活。最终 v4 验证正在执行。
- 最终 v4 验证全部通过：原生四阶段、bootstrap、SQL、direct 的 15 个命令及 TLS。加强后的两条发布恢复用例先登录孩子读取 10/20，再登录父空间，恢复 pending、墓碑及新 fork 均正确；两个 GC 用例确认最终 physical=[]、snapshot/pin 释放，其中一条验证最终释放后的重启。生产构建和移除临时探针后的再次构建均成功。证据及逐项核对见 [完成审计](historical-tablet-completion-audit.md) 和 `gate-results/20261002-history-v4/acceptance-evidence.json`。

## 九、最终交付

- 提交：`50557ddbe3f3956afa2b71addbfcf15d380738d6`。
- 分支：`codex/namespace-worker-proxy-v20`；通过 gh 认证的 Git 推送成功，ls-remote 确认远端与本地 HEAD 一致。
- 提交只包含 10 个业务代码文件，逐字节等于最终验收的业务 diff；未提交文档、测试、日志或探针，未创建 PR。
- 所有 tracked 文件干净；`.scratch/` 留在本地。逐项证据见 [完成审计](historical-tablet-completion-audit.md)。
