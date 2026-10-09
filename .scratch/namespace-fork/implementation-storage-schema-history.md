# 物理布局历史改造进度

目标以 [design-storage-schema-boundaries.md](design-storage-schema-boundaries.md) 为准。
本文件记录实现和验证证据，不缩减已确认范围。2026-10-10 开始实施。

### 当前推进：SSTable 来源布局与行 redo 版本

- `ObSSTableBasicMeta` 持久化来源 G，与自身 V 一起定位完整正文。普通创建、合并、DDL、直接复用、重写、空 minor、defrag 及物理恢复参数均传递该身份；固定格式 LS/MDS 文件沿用其定义。
- fork 重写按输入 SSTable 自身 `(G,V)` 读取完整定义，输出保留这个来源。来源 tablet 自己的 G 可能不同，不能用其当前本地描述代替；目标基线安装仍使用目标 G 的完整定义。
- 已删掉 fork context 的索引构建器共享映射、无使用者的 clipped schema 映射及统一预创建步骤。索引构建器归单个重写任务；直接复用文件不读取完整布局，重写只读取一次，不增加缓存。
- 第一版生产编译 `/tmp/seekdb-sstable-layout-build-1.log` 通过。生产启动/fork 写入、DDL（分区、LOB、索引、并发）和真实大合并通过，日志分别为 `/tmp/seekdb-sstable-layout-smoke-1.log`、`/tmp/seekdb-sstable-layout-ddl-1.log`、`/tmp/seekdb-sstable-layout-major-1.log`，大合并 F=1791581451075893007。
- 专项探针首次编译因错误调用 `ObSSTable::get_tablet_id()` 失败，已改为 `get_key().tablet_id_`；第二次编译通过。三代 fork、父子分别加列、实际重写、删除原父表及 kill/restart 通过（`/tmp/seekdb-sstable-layout-native-test-1.log`），重启审计1046个SSTable，其中613个来源G不同于所属tablet。
- 删除共享映射后的第三次原生编译通过。扩展用例增加实际 CREATE INDEX 和两个目标均执行重写的日志断言，通过 `/tmp/seekdb-sstable-layout-native-test-2.log`：重启审计1091个SSTable，658个保留跨G来源。
- 新增 `--compact-mixed`（父比孩子多一列、混合来源普通 minor、两次重启）复现真实缺陷：第二次重启审计 G22/V1050720 返回 OB_ENTRY_NOT_EXIST。首次重启该系统tablet的V为1048744，可正确读取正文；之后重放数据又转储时变成了属于其他表的1050720。失败日志 `/tmp/seekdb-sstable-layout-mixed-test-1.log`；确认探针失败后停止反复重试的测试进程，驱动记录 exit -9，不算成功。
- 根因是 `ObRedoLogGenerator::fill_row_redo_` 用整个事务的 `get_max_table_version()` 写入每行日志。原来的全局版本上界用法不能表示新的精确物理定义，重启/备库重放会让不同表的版本混入。本次改为行 callback 捕获实际 `ctx.table_version_`，单行、批量及行锁路径统一传入；redo 和重放 callback 保留同一版本。修正后的回归结果见下一项。
- 修正后的混合来源minor用例 `/tmp/seekdb-sstable-layout-mixed-test-2.log` 通过，两次重启后审计1072个SSTable、575个跨G来源全部可读，父比孩子多一列的物理输入、实际CREATE INDEX、孩子写入和孙子隔离均正确。
- 新增最小行redo回归 `row_layout_replay_probe.py`：同一事务批量写A、写较新V的B、再更新A并取得行锁，kill重放后转储，再kill核对持久版本。旧版本重跑 `/tmp/seekdb-row-layout-replay-red-2.log` 仍在重启审计失败；停止已确认失败的实例后驱动记录-9。修正后 `/tmp/seekdb-row-layout-replay-green-2.log` 通过，A的所有文件保持V1791582224594248，B保持V1791582224780712，数据两次恢复均正确。
- 最小回归第一轮驱动没有在重启后推进活跃memtable，等不到用户tablet的MINI而超时：`/tmp/seekdb-row-layout-replay-red.log`、`/tmp/seekdb-row-layout-replay-green.log`。后续明确新增写入，等待可读水位再冻结；这些驱动失败不作为产品错误的证据，也不算成功。
- 四次原生构建中第1次探针接口错误失败，第2/3/4次通过；全部注入移除后的最终生产构建 `/tmp/seekdb-sstable-layout-production-final-build-2.log` 通过（清理了临时RedoDataNode中不使用的重复字段，版本仅由callback持有）。最终生产实际major回归 `/tmp/seekdb-sstable-layout-production-major-final.log` 通过，F/广播/完成均为1791582229216858012。未运行完整mysqltest或sysbench。
- 成本：每个SSTable增加一个8字节G；每个未释放的行事务callback增加一个8字节版本，日志复用已有版本字段，没有新增常驻schema缓存或Namespace规则。
- 新用例及失败场景加入四件套。本节不代表正文/G/绑定GC、统一物理进度/逻辑checksum、旧freeze字段移除或实际主备已经完成。

### meta major 的确定布局

- 将 G 的快照版本选择与不可变 `(G,V)` 正文分开，仍在同一原生事务写入；major/普通 medium 的 G@F 语义不变。旧 head 的 MVCC 回收不再自动消灭仍需被物理描述引用的正文。
- meta major 改为使用 tablet 已记录的 G、V 取完整正文。删除仅由它调用的 `get_table_schema_to_merge` 和 SQL tablet->table 历史查找 helper；不再加载 Namespace SchemaService，不取 MIN 截断版本或最新定义回退。
- 原生编译 `/tmp/seekdb-meta-layout-native-build-1.log` 通过。新增实际 meta 合并测试，并扩展历史、保留、重启探针，已加入四件套；结果如下。
- 首次实际 meta 用例的调度注入放在 adaptive 入口，而普通模式 tablet 的后台不会进入该入口，因而首轮未实际触发 meta。已将测试触发点移到既有 minor 轮询；只绕过小数据的行数阈值，保留输入资格及真实 DAG、SSTable 安装。该修正仅在测试注入脚本，生产逻辑不加入特殊调度。
- 历史正文测试 `/tmp/seekdb-meta-layout-history-test-1.log` 通过：多块正文、缩小后旧快照、最新事务精确读取旧 V、回滚 V 不可见、SQL 同事务、强制 mini/minor 和 kill/restart 均通过。
- 保留测试 `/tmp/seekdb-meta-layout-retention-test-1.log` 通过：暂停且 freeze 行消失后的重启保护、读者交接仍有效。第二阶段推进 MVS 越过 F，旧 F 被拒绝，当前读取得 V=12，但精确 (G,10) 正文仍可读；标记 `LAYOUT_RETENTION_EXACT_BODY V=10 after_head_gc=1`。这证明正文不会随旧 head MVCC 回收消失，不表示正文 GC 已完成。
- 生产源码全部注入已移除；构建 `/tmp/seekdb-meta-layout-production-build-1.log`、实际大合并 `/tmp/seekdb-meta-layout-production-major-test-1.log` 通过，冻结/广播/完成均为1791580286821641082，并核对实际物理 SSTable。
- meta 测试初始 Namespace 第2轮因立即转储时弱读快照尚未覆盖新写入，原生输入策略报 `first minor upper trans version is bigger than tablet snapshot version`。驱动改为读取 current_scn 后等待实际 LS weak_read_scn，再发 minor freeze；没有更改生产输入资格。第3轮重复请求遇到合法的4213“上一轮未完成”，驱动改为继续检查实际 SSTable，其他错误仍立即失败。
- 孩子第1/2轮在没有已合并的父 major 基线时请求单 tablet medium，返回4109，日志明确 `NO_MAJOR_SSTABLE`。当前 meta 用例改为先建立真实父基线再 fork，这是 meta 输入场景的准备条件；**当时只证明请求时尚无 major，不能据此判断会永久缺失；已保留冷父场景单独验证，结果见下。** 第3轮同样修正4213等待。
- 第4轮父、子均实际生成 META SSTable，日志确认 selected=local V 且 head>V；驱动误用内部枚举字符串 META_MAJOR 查询虚拟表，视图实际展示 META，导致结果检查超时。已修正视图断言并保留该轮失败证据，继续端到端读回/重启验证。
- 新正文表示下，父子旧 F 合并回归 `/tmp/seekdb-meta-layout-old-freeze-test-1.log` 通过：F=1791580493089887012，父 G4762/V1791580368662672，子 G5070/V1791580368525136；F 后 DDL 和实际 mini 完成后仍取各自 G@F，C>F 的新 tablet 明确排除，合并后新列读写正确。
- 第5轮父、子完整通过：`/tmp/seekdb-meta-layout-initial-test-5.log` 使用 G5037/V1791580562758872（head=1791580572782104）；`/tmp/seekdb-meta-layout-child-test-5.log` 使用 G5255/V1791580572566200（head=1791580582393464）。实际 META SSTable 到达各自目标快照，新增列/默认值及父子隔离正确，kill/restart 后读回相同。未改生产输入/调度规则，仅测试控制小规模 meta 触发。
- 新增 `meta_layout_probe.py --owner child --cold-parent`，在四件套保留“父尚无已合并 major 的 fork”路径，单独验证上述 NO_MAJOR_SSTABLE 时序。不能把只建立过父 major 的成功用例替代它。
- 冷父专项 `/tmp/seekdb-meta-layout-cold-child-test-1.log` 通过，未预先建立父 major：等待原生就绪后，孩子实际建立 major，随后按自己的旧 V 完成 META；父子新列读回、kill/restart 后结果正确。前述4109是请求时的未就绪状态，当前没有证明是持续缺失基线的缺陷。驱动继续等待实际首份 major，并保留有界失败，未在生产路径伪造基线或新增特例。
- 本节完成时，正文/G/绑定 GC、fork 源重写归属、统一进度和逻辑 checksum、实际主备仍未完成；后续来源布局进展见文件开头。


## 必须完成

- [ ] 真实物理创建提交版本 C：所有创建、复制、持久化和恢复路径；统一合并资格判断。
- [x] 专用布局元数据 tablet：复用 InstanceMetaStore、原生事务和 MVCC；只做 mini/minor。
- [ ] 稳定布局身份 G：上层分配和绑定，同表分区共用，存储不解析 SQL 表或 Namespace。
- [ ] DDL 与完整布局同事务发布；bootstrap、普通创建、fork 首次 DDL/物化均接入。
- [ ] 合并按 G@F 读取并固定完整布局；普通 medium/meta major 适配；保留现有本地副本。
- [x] 主库 freeze 准备等待既有后台物化/接管；最终锁后新快照复核；超时结束请求。端到端主备仍归后续验证项。
- [ ] 物理进度统一使用 C/incarnation/F，并在提交/回放水位达到 F 后重新枚举。
- [ ] Namespace 上层逻辑 checksum：各表自身历史定义、完整输入、缺项不通过。
- [ ] MVCC 与 SQL 历史保留、重启顺序、备库本机接管及提升主库。
- [ ] 删除 freeze.schema_version 和根/子两套旧路径，不保留隐式回退。
- [ ] 编译、针对性动态验证、8000 分区成本验证；已执行及失败用例加入四件套。
- [ ] 提交并推送当前分支；不提 PR。

## 当前证据

- 创建/删除 MDS 数据独立保存 physical_create_version_；物化仍保留原来的逻辑可见性。基础编译、物化回滚与崩溃恢复验证已通过（见后文）；物理调度资格正在接线，上层进度/checksum 尚未统一。
- 已确认 InstanceMetaStore 可按 tablet ID 创建独立存储实例，支持借用 SQL 原生事务；布局存储复用该基础。
- 当前 KV 单值上限 64 KiB，完整布局最大尺寸需要处理，不能默认为任意表定义都能放入一行。
- 新增 `StorageSchemaHistory`：现按 `(G,-1,0)` 保存 MVCC 版本选择，按 `(G,V,chunk)` 保存完整正文，首块含大小和来源版本；大布局按固定位置分块，发布及读取均在同一原生事务快照中完成。旧版 `(G,chunk)` 表示已由上节方案取代。拒绝简化 schema 和同 G 的倒退/重复版本，首次创建不会覆盖现有布局。
- 专用 LS 内部 tablet 已接入创建、删除、mini/minor、活跃读者和持久 freeze 保留。持久保留的故障与恢复证据见后文，完整主备和逻辑校验仍须验证。
- `ObCreateTabletSchema` 与 `ObTabletMeta` 已增加稳定 G，并覆盖复制及持久化；上层分配/绑定已接入共同 DDL/创建入口。major 和普通 medium 已切换布局读取；freeze 准备接线及针对性验证见下文。meta major、完整进度/checksum、布局回收与实际主备仍未完成。

## 后续接线线索

- G 的分配可复用已有 `ObCommonIDUtils::gen_unique_id` / 原生全局 ID 分配能力，无需另造持久计数器或每次 DDL 锁住全局发号行；G 只作身份，不作版本。
- 普通创建的共同入口是 `ObTabletCreator::execute/execute_impl`，bootstrap 的 core 表也走 `ObTableCreator`。必须在 CREATE MDS 注册前选定 G，不能等当前 DDL end 钩子才给物理元数据补值。
- `NamespaceSchemaPublication::stage()` 在同原生事务内读取最终定义；首次物化在 `ensure_tablet()` 已用同事务锁定 Namespace 根并读取冻结创建描述。接入布局归属时必须核对这两条路径的锁顺序，避免新增绑定锁与 Namespace 根锁反转。
- G 整体记录的删除必须等待旧物理 incarnation / 任务 / fork 描述引用满足要求；不能只保护活跃读者，也不能永久遗留无主 G。

## 验证记录

### tablet 本地描述与接管安装的布局归属

- 原生回归复现 `update_tablet_storage_schema` 的版本/正文混合：本地完整 V=20，旧合并携带完整 V=10，列数相同，输出版本仍是20但正文来自10。现在同一G内按版本整体选择描述，同版本优先完整描述；保留已有物理列数要求时明确标为简化，不拼接另一版本的压缩/行存等参数。
- 真实SQL回归复现 mini 的另一入口：只修改列默认值、列数不变，再写入并转储，输出提高了V却仍标记完整。现在只要MemTable带来更高V或更多列，就明确生成简化描述；后续同版本完整布局可以补齐。
- 真实父子DDL回归复现接管跨G污染：子表定义V=1791577063213184，父亲后续DDL V=1791577063380048，原先把后者安装到孩子本地schema。现在批量安装参数显式携带目标G及完整目标布局，在原生受保护读中选择不早于目标C的可读快照；安装时核对目标G，再与孩子本地描述比较。S仅限制源数据，不拿S读取尚未创建的目标G。移除了安装接口中的源schema参数及空源schema回退。
- 原生六个顺序用例及序列化检查通过：旧合并晚完成、新布局晚到、同版本完整/简化描述两种顺序、新版简化描述与旧版完整描述两种顺序。先失败日志 `/tmp/seekdb-local-schema-test-1.log`，修正后 `/tmp/seekdb-local-schema-test-2.log`。
- 真实 mini 回归先失败 `/tmp/seekdb-local-schema-mini-test-2.log`，修正后通过 `/tmp/seekdb-local-schema-mini-test-3.log`（实例 `namespace_fork_PROTOTYPE_local_schema_mini_i0dxc1yj`），版本提高、列数相同、简化标志及两种默认值读回均已断言。
- 真实接管先失败 `/tmp/seekdb-local-schema-fork-test-3.log`；修正后父子分别加列通过 `/tmp/seekdb-local-schema-fork-test-4.log`（实例 `namespace_fork_PROTOTYPE_local_schema_fork_p82z0_jz`），父亲加两列而孩子保持旧定义通过 `/tmp/seekdb-local-schema-fork-wide-test-1.log`（实例 `namespace_fork_PROTOTYPE_local_schema_fork_wide_ucdjmqu2`）。均检查目标安装V、孩子本地更新、未更新的继承行、父亲新列及强制kill重启后的读回。
- 驱动错误也保留：首轮 mini/fork 将调度周期配置为1s，低于参数允许的3s，进程启动失败；fork第2轮在首次安装前暂停全部接管，也暂停了模板初始化，因此主动终止。驱动已改为3s且SQL登录就绪后才暂停。对应 `/tmp/seekdb-local-schema-mini-test-1.log`、`/tmp/seekdb-local-schema-fork-test-1.log`、`/tmp/seekdb-local-schema-fork-test-2.log` 均保留，不能算作产品回归成功。
- 原生构建1至5均通过。上述新用例及故障路径已接入四件套；同时扩展既有 `physical_merge_layout_probe.py`，要求F后DDL及写入完成mini安装，再执行旧F的大合并（最终通过记录见下一条）。
- 移除本轮全部测试注入后，生产构建 `/tmp/seekdb-local-schema-production-build-1.log` 通过；生产大合并 `/tmp/seekdb-local-schema-production-major-1.log` 通过，frozen/broadcast/last 均为1791577530656781012；生产DDL回归 `/tmp/seekdb-local-schema-production-ddl-1.log` 通过。最终原生六个顺序检查 `/tmp/seekdb-local-schema-test-final.log` 通过。
- 扩展旧F用例的失败保留：`/tmp/seekdb-local-schema-old-freeze-test-1.log`、`test-2.log` 分别在默认/180秒期限内等待继承准备超时，未发布F；后台曾遇到物化事务超时并在后续批次重试。`test-3.log` 已完成准备并发布F=1791577919364844021，F后父子mini都以更高V/简化描述完成安装，但驱动只接受虚拟表类型MINOR，漏掉实际类型MINI，因此断言超时。已修正为MINI/MINOR，并给该正确性用例300秒准备期限。诊断时GDB暂停会干扰2秒后台事务期限，第三轮不作为性能测量。
- 扩展旧F用例最终通过 `/tmp/seekdb-local-schema-old-freeze-test-5.log`（实例 `namespace_fork_PROTOTYPE_physical_merge_layout_6inukkte`）：F=1791579089907257018，父G=5263/V=1791578965451280、子G=5571/V=1791578965183320。父子F后分别DDL、写入并完成真实MINI安装后，再执行旧F大合并仍使用各自G@F；F后创建对象C>F明确排除，之后两个分支的新列数据均正确。此前长期等待的线程池原因及确定性回归见下一节。
- 本节尚不覆盖接管重写输入描述的全部生命周期，不能据安装改正就声称跨G所有路径已完成；meta major完整布局、G回收、统一进度/checksum及实际主备仍待整体目标落实。

### 主库 freeze 准备、最终复核及请求结束

- 新增 `NamespaceFreezePreparation`，每次用一个原生只读快照遍历所有 live Namespace 的目录，再核对物理 incarnation、数据完整状态及本机接管完成状态。包括默认 Namespace、模板、普通孩子及索引/LOB；无 ns1 特例，无常驻就绪缓存、持久准备任务或新增调度器。
- 等待阶段不持有 freeze manager 的 mutex 或事务锁。已有 NamespaceMaintenance 和接管 DAG 继续推进；最终现有 DDL 排他协调、SQL snapshot_gc 行锁、KV COUNTERS 分配行锁依次取得。COUNTERS 通过 `attach()` 借用同一个 SQL 原生事务，持有至提交/回滚及连接释放后才 detach。
- 最终扫描另开锁后新快照，因为 attach 的读快照可能早于分配行锁等待。最终扫描不取 Namespace 根锁或 KV snapshot watermark 锁，避免与 fork 的“源根、KV 水位、分配行”顺序反转。发现新继承对象则回滚释放锁，回到本次请求期限内等待。
- 用户请求使用原期限；没有外层期限的后台调用使用既有 internal_sql_execute_timeout。超时不续跑，返回最后观察到的 Namespace、physical tablet 及物化/本机基线未完成原因。定时 freeze 不再把超时改成 EAGAIN，也不在相同 duty minute 下一次 tick 重新发起。
- 首个无注入构建 `/tmp/seekdb-freeze-preparation-build-1.log` 通过，生产大合并 `/tmp/seekdb-freeze-preparation-production-major-1.log` 通过。原生构建第 1/2/3 次均通过。
- 未物化与已物化未接管分别 3 秒超时，F 均未发布；等待期间父 DML 成功。释放测试暂停后，既有后台完成孩子全部 362 个绑定（包括索引/LOB）；期间不断核对未出现自动 F，DBA 重试成功。日志 `/tmp/seekdb-freeze-preparation-wait-1.log`；第 2 轮另断言客户端返回具体阻塞原因（`/tmp/seekdb-freeze-preparation-wait-2.log`）。
- 最终锁前提交 fork：锁后复核报告新孩子未就绪，回滚后请求超时且未发布 F（`/tmp/seekdb-freeze-preparation-before-lock-3.log`）。进一步暂停 fork 持有 COUNTERS，让 freeze attach 先取得旧快照并等待，再允许 fork 提交；锁后新快照仍捕获新孩子（`/tmp/seekdb-freeze-preparation-waited-lock-1.log`）。
- 最终锁内并发 fork/DDL 均等待 freeze 提交，父 DML 仍可执行；随后 fork、DDL 成功，读取新列及 DML 正确（`/tmp/seekdb-freeze-preparation-locked-4.log`）。锁内 kill/restart 不保留未提交 F，重试 freeze 和 fork 均成功（`/tmp/seekdb-freeze-preparation-crash-1.log`）。准备中并发删除未物化 Namespace 后，新快照去掉已删除对象，freeze 成功（`/tmp/seekdb-freeze-preparation-delete-1.log`）。
- 定时入口配置 2 秒期限后只尝试一次，跨多个 timer tick 未发布 F（`/tmp/seekdb-freeze-preparation-daily-1.log`）。内存压力入口也在准备超时后清空本次重试槽；后续独立的内存压力观察仍可发起新请求。
- 内存压力专项首次复现旧缺陷：`ObRetryMajorInfo::is_valid()` 恒为 true，reset 后仍报告 `ret=-4012 retry=1`（`/tmp/seekdb-freeze-preparation-pressure-1.log`）。git blame 定位到既有 `3609383cd7`，不是本轮新增。修正为 `frozen_scn_ > 0` 后，真实 dispatcher 的超时返回、未发布 F 及槽位失效均通过（`/tmp/seekdb-freeze-preparation-pressure-2.log`）；第 4/5 次原生构建通过。该失败及回归已加入四件套。
- 8000 分区：共复核 8716 个绑定。原生版本锁内扫描 38.344 毫秒，请求 94.624 毫秒；移除注入后的生产版本为 38.107 / 95.999 毫秒（`/tmp/seekdb-freeze-preparation-large-1.log`、`/tmp/seekdb-freeze-preparation-production-large-final.log`）。这是一次全就绪扫描成本，不代表 fork 后全部物化/接管的耗时。
- 失败记录保留：before-lock/locked 首轮在复制二进制尚未结束时启动，报 Text file busy；后续依赖明确等待复制完成。第 2 轮驱动从 process.out 等待暂停标记，但 seekdb 已把 stderr 重定向到 seekdb.log；引擎实际到达暂停点，修正增量日志读取后第 3 轮通过。相应日志 `before-lock-1/2`、`locked-1/2` 与同前缀完整路径均保留。
- 上述 probe（包括并发和故障场景）已接入四件套，测试暂停控制文件仅由本地注入脚本引入，不进入生产实现。无完整 mysqltest/sysbench。完整主备、物理进度/逻辑 checksum 和 meta major 等目标仍需后续实施，不能把本节视为整体完成。
- 移除全部注入后最终生产构建通过（`/tmp/seekdb-freeze-preparation-production-final-build-2.log`），包含重试槽修正的生产大合并回归通过（`/tmp/seekdb-freeze-preparation-production-major-final-2.log`），全局 frozen/broadcast/last 均达到 1791575974702086012。

### freeze 等待期间的请求线程池缩容竞态（进行中）

- 延长旧 F 用例准备预算至 300 秒后仍超时（`/tmp/seekdb-local-schema-old-freeze-test-4.log`，实例 `namespace_fork_PROTOTYPE_physical_merge_layout_plmxk7n7`）。GDB 栈 `/tmp/seekdb-local-schema-maintenance-stack-4.log` 显示 NamespaceMaintenance 在 `get_tablet_binding_mds_by_rpc -> ex_rpc::sync_call` 等待，唯一请求工作线程正在 freeze 准备循环。同期 runtime 日志连续显示 total_worker_cnt=1、idle_worker_cnt=0、req_queue.total_size=1，排除“布局读取慢”与“请求已经出队后执行卡住”这两个解释。
- `ObAdaptiveWorkerPool::try_shrink_one` 只在缩到零时检查队列，存在最后空闲线程空 pop 后、退出前被入队操作错认为仍可服务的竞态。其余线程仍可能忙于等待后台完成，队列因此无法推进；既有 timeup 救援只覆盖 worker_count>=min，未覆盖该现场的 1<12。
- 新增 `runtime_shrink_probe.py`，通过测试注入固定真实请求队列的交错顺序：一个 SQL 工作线程仍执行阻塞请求，最后空闲线程 pop 未命中后暂停，另一个已登录连接入队完成且跳过扩容，再允许空闲线程尝试退出。断言新请求在原忙线程释放前完成。用例已加入四件套。修复前 `/tmp/seekdb-runtime-shrink-test-1.log` 已确定性失败，原生日志 `workers=2 idle=1 queued=1`，新请求等待超过2秒而忙线程尚未释放。修复将队列复核应用到任意缩容，不再只检查缩到零；修复后同一用例 `/tmp/seekdb-runtime-shrink-test-2.log` 通过，新请求在原忙线程仍阻塞时返回；日志仍确认 workers=2、idle=1、queued=1 的确定性交错。没有新增常驻线程或 Namespace 特例。原 freeze/旧F合并场景已通过，见上一节最终记录。移除全部注入后生产构建 `/tmp/seekdb-runtime-shrink-production-build-1.log` 通过；生产大合并 `/tmp/seekdb-runtime-shrink-production-major-1.log` 通过，frozen/broadcast/last 均为1791579047069212012，物理ID与实际SSTable同时断言。两项新增注入文件只保存在测试目录，不进入生产源文件。

### 物理合并资格与布局读取（进行中）

- 新增不感知 Namespace 的 `PhysicalMergeCandidate`，从真实创建 MDS 读取 incarnation/C/G。未提交 CREATE、已提交删除和存活对象分别处理；合并轮次以 C<=F 判断。`round_satisfied` 与“真的产生了合并结果”分开，C>F 不伪造 finish/checksum。
- major 迭代改为先获取物理对象再显式判定原生状态，避免原先过滤器隐藏尚未提交的状态。新轮次在合并循环线程接收并重建迭代器，先验证可读水位，再取得候选 ID；reload 线程不再修改正在遍历的数组。
- major 跳过所有 F<C 的旧 freeze，直接读取 G@F 并固定进 medium_info；删除旧的跨对象版本比较、schema 缺失后跳下一个 freeze、改用普通 medium 等分支。普通 medium 也读取自己的目标 B；旧 B 已不受保留或早于 C 时，在注册原生读者后选择新的可读 B，同时重算合并输入，不把当前布局贴到旧快照上。
- 首轮构建通过（`/tmp/seekdb-physical-merge-layout-build-1.log`），真实 major 用例失败（`/tmp/seekdb-physical-merge-layout-major-test-1.log`）：只有父/模板 bootstrap core 对象未完成。旧 `schema_version==0` 判断把有效的对象定义 V=0 当作无效全局版本，已删除该判断；没有按 Namespace/对象编号增加例外。
- 修正后生产构建通过（`/tmp/seekdb-physical-merge-layout-build-2.log`）；真实 major 通过（`/tmp/seekdb-physical-merge-layout-major-test-2.log`，实例 `namespace_fork_PROTOTYPE_major_progress_47h8bv4e`，F=1791573603250786024）。冻结、广播、完成水位一致，实际 MAJOR 使用编码后的物理 tablet ID。
- 原生资格测试通过（`/tmp/seekdb-physical-merge-identity-test-2.log`，实例 `namespace_fork_PROTOTYPE_shared_transaction_4qf8vcyg`）：真实未提交 CREATE 不参与；回滚与提交后 crash；恢复 370 个 tablet，358 个使用持久 MDS 状态；每个对象明确断言 C-1 不参与、C/C+1 参与，并与崩溃前原生提交记录核对。
- 父子布局测试通过（`/tmp/seekdb-physical-merge-layout-test-4.log`，实例 `namespace_fork_PROTOTYPE_physical_merge_layout_tpqvyoqe`）：父子分别 DDL，F 后再分别加列，实际合并仍使用各自 F 时的 G/V；F=1791573906729989023，父 G=2037/V=1791573906570696，子 G=2347/V=1791573906483312。之后读写新列正确。F 后创建的 tablet 明确 C>F、member=0，未生成 F 的假 MAJOR。
- 测试驱动失败也保留：首次两个驱动在二进制复制尚未结束时启动，得到 Text file busy（`/tmp/seekdb-physical-merge-layout-test-1.log`、`/tmp/seekdb-physical-merge-identity-test-1.log`），等待复制完成后重跑。父子测试第 2/3 轮把 freeze 返回后尚未刷新的进度视图值 1 当作本轮 F，原生日志实际已经读到正确 G@F；改为等待新 F 在视图出现，第 4 轮通过。测试还保留完整行读取游标，避免半行日志被消费。
- 普通 medium 接线首轮编译失败：重选输入接口需要非 const ObTablet，已修正。修正后的原生构建通过（`/tmp/seekdb-physical-merge-medium-native-build-2.log`）。新测试及注入入口已加入四件套。
- 普通与强制旧目标的首轮驱动均失败（`/tmp/seekdb-medium-layout-target-test-1.log`、`/tmp/seekdb-medium-layout-target-old-test-1.log`）：实际 tablet freeze 命令返回成功，但未到布局准备。静态核对确认 `ObScheduleTabletFunc(merge_version, reason)` 的枚举被当作第二个整数参数 loop_cnt，merge_reason 仍为 NONE；用户请求和 mini 后事件两处均有此错。构造函数调整为枚举在第二位、循环次数在第三位，使旧调用正确传递原因且避免整数误传；正常轮次显式传 NONE 和 loop_cnt。
- 原因参数修正后的原生构建通过（`/tmp/seekdb-physical-merge-medium-native-build-3.log`）。真实 tablet freeze 两项均通过：普通目标 B=1791574214151667160/V=1791574214148104（`/tmp/seekdb-medium-layout-target-test-2.log`，实例 `namespace_fork_PROTOTYPE_medium_layout_target_0pcp9nu5`）；测试注入过旧目标 1 后重选 B=1791574214935918076>=C，使用 V=1791574214915232（`/tmp/seekdb-medium-layout-target-old-test-2.log`，实例 `namespace_fork_PROTOTYPE_medium_layout_target_dbiue2j0`）。两项均断言实际 MAJOR 结果位于选定 B、新加列默认值正确；不只断言调度函数返回成功。
- 扩展历史保留测试通过（`/tmp/seekdb-medium-layout-retention-test-1.log`，实例 `namespace_fork_PROTOTYPE_layout_retention_it0oeg02`）：保留、暂停恢复、原生读者交接以及释放后拒绝旧 F 均通过；旧 F=1791574037475174043 已被回收后，`read_current` 返回新的受保护目标 1791574091456492010 和 V=12，未将新布局作为旧 F 的结果返回。
- 最终全部本轮生产源码注入已移除，生产构建通过（`/tmp/seekdb-physical-merge-production-final-build-2.log`）。生产真实 major 最终回归通过（`/tmp/seekdb-physical-merge-production-major-final.log`，实例 `namespace_fork_PROTOTYPE_major_progress_ymfwurg1`，F=1791574304026577012）。原因参数修正后再次执行父子/F 后 DDL 回归通过（`/tmp/seekdb-physical-merge-layout-test-final.log`，实例 `namespace_fork_PROTOTYPE_physical_merge_layout_aqro4e3c`，F=1791574305122558023）。只执行针对性用例，未跑完整 mysqltest/sysbench。
- 尚未完成：上层 SQLite 进度/incarnation 校验、Namespace checksum、G/绑定回收、meta major 和实际主备。freeze 准备的后续进展见前节。特别是 meta major 现有实现会合入已确定的 minor 并将结果快照推进到 tablet.snapshot_version；现有 medium_info 又会在 MDS minor 中按 <=last_major_snapshot 回收，不能直接假定旧 medium_info 永久存在，也不能无条件拿旧 major 的布局解释后续加列数据。该路径必须补足确定布局及其持久生命周期，再删除剩余 SQL schema helper，不能以本节通过代替完成。

### 持久布局历史保留（进行中）

- 已接入 schema tablet 的 mini/minor 保留输入：SQL snapshot_gc_scn 栅栏、未结束 freeze、本机待完成广播，以及既有活跃读者/弱读水位。生成 F 前注册短期原生读者，freeze SQL 提交后结束；复用既有 snapshot_gc 行锁和先读 GC 栅栏再读 freeze 列表的顺序，不新增持久 pin 表。
- 启动未恢复完整视图时只允许沿用 tablet 已有 MVCC 边界，不推进；新增 `StorageSchemaHistory::read_at` 在注册读者后核对持久保护和本机可读水位。合并消费者尚未切换，不能据此声称合并/校验已经完成。
- 第一轮生产构建通过（`/tmp/seekdb-layout-retention-build-1.log`）。专项测试版构建通过（`/tmp/seekdb-layout-retention-native-build-1.log`）。
- 首个 `run_layout_retention_probe.py` 通过：没有 F 之前的活跃读者，SQL GC 栅栏及原生弱读水位均大于 F，旧 V=10 仍可跨强制转储/minor读取，最新为 V=11；kill/restart 后仍可读，测试输入标记轮次完成后推进 MVS 并拒绝旧 F。实例 `namespace_fork_PROTOTYPE_layout_retention_2wd3x_2c`，日志 `/tmp/seekdb-layout-retention-test-1.log`。该测试控制已完成进度输入，不验证 checksum 完成判断，也不是实际主备测试。
- 补充暂停恢复用例后复现失败：本机持久广播 F 尚未结束，移除 freeze 行后，`get_schema_history_retention` 返回了大于 F 的边界。内存调度器因暂停尚未启动广播，不能作为恢复依据。实例 `namespace_fork_PROTOTYPE_layout_retention_6mscmser`，`seekdb.log` 中 `LAYOUT_RETENTION_FAIL line=200 ... retained == frozen`；确认失败后终止该测试实例，驱动记录 exit -9，日志 `/tmp/seekdb-layout-retention-test-2.log`。失败场景已在四件套用例中保留。
- 已改为既有 freeze reload 同时读取本机持久合并进度，计算一个 SCN 边界并与视图一起安装；新增的是可重建的 8 字节派生边界，无持久 pin、名单或独立恢复任务。修正后的专项构建通过（`/tmp/seekdb-layout-retention-native-build-3.log`），完整暂停恢复用例通过：删除 freeze 行、本机暂停未装载广播、kill/restart、mini/minor 中保留旧 F、完成后释放。实例 `namespace_fork_PROTOTYPE_layout_retention_ppewqiq4`，日志 `/tmp/seekdb-layout-retention-test-3.log`。完整主备验证仍待后续接线。
- 移除注入后的生产构建通过（`/tmp/seekdb-layout-retention-production-final-build.log`），已有 `primary_major_namespace_probe.py` 通过真实 `ALTER SYSTEM MAJOR FREEZE` 和进度完成/物理 ID 断言，F=1791572487823549012，实例 `namespace_fork_PROTOTYPE_major_progress_r_3agyp0`，日志 `/tmp/seekdb-layout-retention-production-major-test.log`。合并布局消费者此时仍是旧路径，此结果只覆盖本次保留接入对真实发布入口的影响。
- 保留交接的取样顺序已修正：回收器先读持久边界，再检查活跃读者；读者先注册，再检查持久边界。反过来会在“读者进入、轮次完成”交错时同时漏掉两种保护。专项用例在真实 `get_multi_version_start` 两次取样之间确定性插入读者与完成进度变更，断言输出不越过 F、读者仍读到 V=10，再释放并确认 MVS 可推进。测试构建 `/tmp/seekdb-layout-retention-handoff-build.log` 通过，用例 `/tmp/seekdb-layout-retention-test-4.log` 通过，实例 `namespace_fork_PROTOTYPE_layout_retention_7t9vdj26`。包括暂停且 freeze 行消失后的重启保护，全部已纳入四件套。
- 本轮全部测试注入已从生产代码移除，最终生产构建 `/tmp/seekdb-layout-retention-production-final-build-2.log` 通过；没有运行完整 mysqltest/sysbench。完整目标仍未完成：合并布局消费、资格/进度/checksum、新 freeze 准备、布局对象 GC、非全局任务保护及实际主备并发仍需接线验证。
- SQL 逻辑历史静态核对：`ObTableSqlService::delete_from_all_table_history/delete_from_all_column_history` 实际插入 `is_deleted=1` 的新版记录；`ObDropIncPartHelper::drop_partition_info` 同样写分区/子分区历史 tombstone。当前在 `src` 的实际 DELETE/exec_delete/splice_delete_sql 调用中未找到这些 schema 历史表的物理行回收入口，因此本轮保持历史行，不新造回收线程；此证据不替代后续历史定义读取的动态验证。

### 上层发布接线（进行中）

- 新增上层 `TableStorageLayouts`，在 schema tablet 的 `TABLE_STORAGE_LAYOUTS` 集合中保存不可变 `(namespace_id, table_id) -> G`。fork 不复制该集合；首次 DDL/物化用所属目录中的完整定义初始化独立 G，后续分区只复用身份。物理布局正文仍由不感知 Namespace/SQL 的 `StorageSchemaHistory` 管理。
- 普通/bootstrap CREATE 在注册创建 MDS 前选择 G；物化显式传入实际 Namespace 身份。DDL 的既有最终目录发布入口同步发布完整布局，即使只是表定义版本变化也发布。初始目录建立也借用一个 SQL 原生事务，布局与目录同时结束。
- 已有绑定读取不加锁。首次绑定使用原生非等待 INSERT，竞争失败要求整个所属事务回滚；不在持有 Namespace 根锁时等待另一个尚需根锁的创建者。布局更新沿用已有 DDL 发布协调；没有提前获取 Namespace 根锁或扩大全 Namespace 的串行范围。
- SQL 事务增加通用资源持有入口，KV 借用者保持到原生事务结束且连接释放之后；短生命周期的 tablet creator 返回不会提前撤掉快照/GC保护。
- G 整体 GC、冻结持久历史保护与合并消费仍未接线，不能把本段视作整体完成。当前绑定/布局的删除尚须与物理 incarnation 和任务引用一起落实。
- 专项用例 `run_table_storage_layout_probe.py` 已加入四件套并通过：原生首次绑定竞争、父子 G 独立、旧 seed 不覆盖新布局、回滚与参与者生命周期；真实分区/LOB/索引 DDL、晚物化和 kill 恢复后逐项核对 SQL V、布局 V、物理 G。
- 接线首轮编译失败：`ObArray<shared_ptr<void>>` 的虚拟打印要求元素提供 `to_string`。已改为不依赖该容器的事务资源链表，并重新构建全部包含公共事务头的目标。
- 专项探针编译失败：误写 `meta.table_id_`；该持久字段实际为 `create_table_id_`，已修正。
- 修正后测试版本编译通过（`/tmp/seekdb-layout-publication-build-3.log`）。`run_table_storage_layout_probe.py` 通过：358 个初始 SQL tablet 均有非零 G；重启逐项核对 7 个表对象、56 个物理 tablet 的绑定及布局 V；原生冲突立即返回、旧 seed 不覆盖新版、父子独立、回滚和参与者生命周期均通过。实例 `namespace_fork_PROTOTYPE_table_storage_layout__hwllbbt`，日志 `/tmp/seekdb-table-layout-test-1.log`。
- 两项 DDL 同事务崩溃检查通过：初始 Namespace 在提交前崩溃，目录与布局同时恢复旧版本；子 Namespace 在提交后崩溃，两者同时恢复新版本。命令为 `ddl_catalog_atomic_probe.py --layout-history` 分别配 `--owner initial --fault abort_crash`、`--owner child --fault commit_crash`；日志 `/tmp/seekdb-layout-abort-crash-test-1.log`、`/tmp/seekdb-layout-commit-crash-test-1.log`。两项已收入四件套。
- 所有本轮测试注入已移除，补充普通物理 tablet 创建必须携带有效 G 的入口校验（固定 schema 的 LS 内部 tablet 例外）后，最终生产构建通过（`/tmp/seekdb-layout-publication-production-final-build.log`）；专门 schema/instance tablet 不依赖自己的 G。
- 生产版 `ddl_catalog_schema_probe.py` 通过：分区与 LOB，索引创建/删除，表/分区 TRUNCATE，分区增删、库删除，4 路并发 DDL，旧 fork 视图保持。日志 `/tmp/seekdb-layout-production-ddl-test-1.log`，实例 `namespace_fork_PROTOTYPE_ddl_catalog_schema_hzkw5ofk`。
- 生产版 `large_partition_fork_probe.py`（8000 分区）通过：建表 12.438 秒，三次 fork 分别 2.493/4.034/3.128 毫秒，物理 tablet 增量均为0；孩子单分区写入与父子读回正确。日志 `/tmp/seekdb-layout-production-8000-test-1.log`，实例 `namespace_fork_PROTOTYPE_large_partition_fork_uljy562p`。这是本次单轮测量，未测尚未实现的 freeze 最终复核成本；8000 分区共 G 的逐物理对象检查仍待扩展原生审计覆盖。

- 首轮编译：新增文件引用不存在的 `lib/allocator/ob_arena_allocator.h`，已改为已有的 `ob_allocator.h`；等待编译尾部完成后重新构建。
- 新增 `run_storage_schema_history_probe.py` 并加入四件套 bootstrap-native-kv，覆盖分块布局、缩小后历史读取、回滚、转储及重启；尚未执行。
- 原生物化 identity 探针补充真实提交 C 检查；原有创建描述探针补充 G 的复制/序列化检查；尚未执行。
- 旧 tx-data 转 MDS helper 没有有效创建提交版本的保证，仓内无生产调用；没有用其日志 SCN 推导 C。新 C 只由实际创建提交回调写入。
- 第二轮编译触发 ObTablet 内存大小静态断言：G 和缓存 MDS 中的 C 共增加 16 字节，ObTablet + ObRowkeyReadInfo 从 1376 到 1392 字节。已同步断言；8000 个常驻 tablet 仅这两个字段增加约 125 KiB，不计新元数据 tablet 自身开销。
- 第三轮测试版本编译通过。首个动态原生探针通过（140127 字节布局、缩小、回滚、旧快照、C codec），但 Python 驱动在错误的 process.out 中找标记而失败；实际标记在 seekdb.log。已修正并补充明确 minor 完成及 SQL 同事务提交/回滚检查，待重跑。失败实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_storage_schema_history_tlqh37eq`。
- 修正后 `run_storage_schema_history_probe.py` 完整通过：140127 字节布局跨 3 个 KV 行；缩小后旧快照跨 mini/minor 读取；大布局更新回滚；实际 SQL 元数据与布局同原生事务提交/回滚；kill 后恢复已提交大小布局；C 与继承 S、提交日志 SCN 分离的 codec 检查。实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_storage_schema_history_i53hw3f1`；驱动日志 `/tmp/seekdb-layout-history-native-test-2.log`。
- 移除布局测试注入后，生产版 `source ~/.bashrc && make -j80` 通过（`/tmp/seekdb-schema-history-production-build.log`）。上述成功不覆盖尚未实现的持久 freeze 历史保留或合并切换。
- 生产版 `quick_startup_probe.py --fork --write` 通过，父表数据、fork 继承读、孩子首次写及读回均正确。实例：`/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kv_startup_30s_15z3z5bv`；日志 `/tmp/seekdb-schema-history-smoke.log`。
- 物化故障探针首轮在恢复检查失败：它错误地要求 `get_latest` 的附加 node trans_version 总是有效；源码 `ob_i_tablet_mds_interface.ipp` 的持久缓存/SSTable 分支明确 reset 该输出。持久化 C 与崩溃前 on_commit 日志一致（例：tx=480，S=1791569247291981025，C=1791569249645304025）。修正探针，仅在 node version 有效时比较，并由 Python 始终用崩溃前真实提交日志逐项核对恢复 C。失败实例 `namespace_fork_PROTOTYPE_shared_transaction_bb5k5mvj` 已保留；该命令已纳入四件套。
- 第二次物化故障探针失败于独立证据收集：普通 INFO 日志不是每个事务都有，例 tx=497 缺少提交行。新增仅测试版本启用、同步输出的 `CREATION_PHYSICAL_COMMIT` 回调探针，直接记录 native commit 参数与 C，避免依赖普通日志采样。失败实例 `namespace_fork_PROTOTYPE_shared_transaction_grqzird2` 已保留。
- 第三次物化故障用例通过：`shared_transaction_probe.py --faults --creation-identities --case rollback_create --case crash_committed`。最后一次恢复逐项核对 370 个 tablet 的 C 等于崩溃前原生 commit_version，367 个走已落盘 MDS 分支；回滚和提交后 crash 恢复均通过。实例 `namespace_fork_PROTOTYPE_shared_transaction_zr4xor0k`，日志 `/tmp/seekdb-physical-birth-test-3.log`。
- 所有临时生产源码注入已移除，最终生产构建通过：`/tmp/seekdb-schema-history-production-final-build-3.log`。本轮用例与失败已收入四件套；未跑完整 mysqltest/sysbench。精简结果备份见 [验证记录](schema-history-validation.json)。
