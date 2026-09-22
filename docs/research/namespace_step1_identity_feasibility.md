# Step 1 可行性判定：kernel 的 ns 身份能否显式化

状态：**判定通过（GO）**。这是 `namespace_kernel_single_process_gap.md` §4 认定的唯一可行性闸口。
方法：对 kernel 中全部 26 处 ambient scope guard 按**包围函数**归并（25 个函数，含 1 个文件内自由函数），逐个人工核对"ns 从哪来"。
证据：`namespace_fork_kernel_prototype.cpp` 当前工作区（commit `09beb375a`）。复核脚本见 §3。

## 0. 结论

`plan.md` 不变式 2（无 ambient 上下文）与 ADR-0003 在 kernel 上**可以成立**，不需要保留 thread_local 兜底。

| ns 来源 | 函数数 | 说明 |
|---|---:|---|
| **A. 签名里已有显式 ns** | 11 | 零改动语义，只是把 `ControlSqlNamespaceScope` 删掉、直接用入参 |
| **B. 可从入参的编码 id 推出** | 10 | 用 `database_of(id)` / `local_of(id)` 得到 ns |
| **C. 边界函数（结构性常量）** | 3 | 目标 ns 由定义决定，不是外部上下文 |
| **C'. 自由函数（全局 GC）** | 1 | `collect_metadata()`：扫的是全局 `namespaces` 表，目标 ns 恒为 1 |
| **合计** | **25** | 无一个函数必须依赖 ambient |

**关键点**：C 类不是"拿不到 ns"，而是"ns 是结构性常量"。它们是 control 元数据的读写点，作用对象固定是**系统 ns（ns 1）**或**它自己正在创建/删除的那个 ns**；把 ns 1 写成显式参数比 thread_local 更诚实（thread_local 之前隐式就等于 1）。

## 1. 逐函数分类

### A. 签名里已有显式 ns（11）

删掉 guard、直接使用入参即可。

| 函数 | path:line | ns 参数 |
|---|---|---|
| `namespace_schema_version` | `:1712` | `uint64_t ns` |
| `begin_schema_change` | `:1726` | `uint64_t ns` |
| `finish_schema_change` | `:1741` | `uint64_t ns` |
| `begin_schema_recovery` | `:1773` | `uint64_t ns` |
| `finish_schema_recovery` | `:1801` | `uint64_t ns` |
| `database_in_namespace` | `:1909` | `uint64_t ns` |
| `publish_schema_delta` | `:2788` | `uint64_t namespace_id` |
| `is_tablet_owned` | `:2920` | `uint64_t namespace_id` |
| `owned_storage_tablets` | `:2941` | `uint64_t namespace_id` |
| `schema_by_name` | `:3023` | `uint64_t db` |
| `list_schemas` | `:3087` | `uint64_t db` |

### B. 从入参的编码 id 推出 ns（10）

调用点必须传**编码后**的 id（这是 `audit §8.4 #3 encode_object 契约` 那条工作）。

| 函数 | path:line | ns 来源 |
|---|---|---|
| `check_baseline_access` | `:1300` | `tablet_id` |
| `check_table_access` | `:1341` | `table_id` / `tablet_id` |
| `protect_snapshot_tablets` | `:1379` | `candidates` 内的 `ObTabletID` |
| `database_by_id` | `:1922` | `id` |
| `schema_by_id` | `:3049` | `id` |
| `table_id_for_tablet` | `:3064` | `tablet` |
| `check_ddl` | `:3113` | `schema.get_table_id()` / `.get_database_id()` |
| `schedule_baseline` | `:3146` | `tablet.tablet_id_` |
| `resolve_read_tablet` | `:3193` | `tablet_id` |
| `ensure_tablet_impl` | `:3233` | `tablet_id` |

### C. 边界函数：目标 ns 是结构性常量（3）

| 函数 | path:line | 目标 ns | 判定 |
|---|---|---|---|
| `ensure_control_schema` | `:1166` | **常量 ns 1** | 建的是 `__fork_proto_meta` 下的全局控制表（`CREATE DATABASE IF NOT EXISTS __fork_proto_meta` / `pages` / `roots` / `namespaces` …），这是系统 ns 的元数据。显式传 1，语义比 ambient 更准确。 |
| `control_namespace` | `:1992` | **常量 ns 1 + 目标 ns 来自入参** | 往 `__fork_proto_meta.namespaces` 插行（`INSERT INTO %s(name,source_id,…)`，`:2037`），即写系统 ns 的控制元数据；被创建/删除的 id 由 `source`/`target` 名字解析得出（`namespace_named(trans, target, id)`，`:2043`）。两个来源都显式。 |
| `begin_namespace_drop` | `:1212` | **常量 ns 1 + 目标 ns 来自入参** | 入参 `name` + `uint64_t &id`；控制元数据读写同样落在系统 ns。 |

> 注意：`control_namespace` 的 ambient 段在 `bootstrap` 分支里读 `GSCHEMASERVICE.get_runtime_schema_guard(guard)` 取 `root.schema_version`（`:2028-2032`）。那个 guard 是**进程级 schema 权威**，在单进程形态下要换成显式选定的服务实例（由 namespace runtime 决定），不是 ns 号问题，属于 `gap §3.6` 的 schema 权威选型。

## 2. 这对设计意味着什么

1. **不变式 2 在 kernel 上成立** —— 24/24 都能显式化，不需要为 kernel 保留 thread_local，也不需要重新讨论整个单进程设计的立论基础。`gap §4 Step 1` 从"看起来可行"升级为"判定可行"。
2. **Step 1 的实际工作量被精确框定**：
   - A 类 11 个：机械（删 guard、用入参）；
   - C 类 3 个：把 ns 1 写成显式常量并加注释说明为什么是 1；
   - **B 类 10 个是真正的风险面** —— 风险不在 kernel 内部，而在调用点是否传了编码 id。`audit §8.4` 已指出 `encode_object` 契约"输入必须编码"，且有 3 个 DDL 调用点必须持有编码 db id（`ob_table_helper.cpp:572,583`、`ob_ddl_service.cpp:737`）。**Step 1 必须与"对外表示统一（ns1 也编码）"一起做**，否则 B 类会退化成"未编码 ⇒ 当 ns 1 处理"的隐式兜底，重新引入静默跨 ns 读错。
3. **仍然没有编译期保护**（`gap` 风险 ①）：漏改一处不会编译失败，而 `current_namespace_id()` 会兜底到 ns 1。所以 Step 1 的验收必须是**运行期探针**：两个 ns、两个线程并发跑，断言各自读到的 schema/tablet 归属正确、互不串。这个探针目前不存在，需要新建。

## 3. 复现方法

分类由脚本从当前源码生成（按 ambient guard 的**包围函数**归并，非按签名文本匹配——后者会漏掉跨行签名）：

```bash
# 1) 列出全部 ambient guard 实例化点
grep -n "ControlSqlNamespaceScope\|ExplicitSqlNamespaceScope" \
  src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp
# 2) 对每个实例化点向前找最近的函数体开始（'NamespaceForkKernelPrototype::' + '{'），
#    归并得到 24 个包围函数；再检查签名/函数体前 45 行是否含显式 ns 或编码 id。
```

实测：26 个实例化点 → 25 个包围函数 → A 11 / B 10 / C 3 / C' 1。

`:1059` 位于文件内自由函数 `collect_metadata()`（`:1057`，非 `NamespaceForkKernelPrototype::` 成员，故按签名文本匹配会漏掉）。它是 GC 全量扫描：`SELECT catalog_page,directory_page FROM %s ORDER BY namespace_id FOR UPDATE NOWAIT`（`:1074`，表即全局 `namespaces`），由 `control_namespace("__gc__","__gc__")` 触发（`:1996`）。它跨 ns 遍历，目标恒为系统 ns，同样应显式传 1；唯一的额外依赖是它持 `metadata_mutex` 期间的锁粒度问题（`gap §6 未知 #5`），与身份显式化无关。

## 4. 未做与不确定

- 本次是**静态判定**：我核对了每个函数的 ns 来源，但**没有**逐个验证 B 类 10 个函数的**全部调用点**是否真的传入编码 id。`gap §6 未知 #2` 列出的 3 处（`ob_drop_table_helper.cpp:62`、`ob_table_sql_service.cpp:704,2203`）仍需插桩确认。
- `collect_metadata()` 的 ns 已按"全局 GC ⇒ 系统 ns"归为 C'，但它与 `control_namespace` 的 `__gc__` 分支耦合，实现时应一起改。

---

## 5. 机制补充：ambient 载体不止一个（这会改变工作量估计）

上一节判定的是"**ns 能否从数据/入参推出**"——结论是能。但"身份显式化"还要**拆掉 ambient 载体**，而载体的实际形态比 `gap` 文档描述的更重。实测 `resolve_shared_inner_sql_namespace()`（`namespace_worker_gateway_prototype.ipp:65-80`，即 kernel `current_namespace_id()` 的实现）有**三条解析路径**：

| # | 载体 | 位置 | 生命周期 | Phase 3 处置 |
|---|---|---|---|---|
| 1 | `worker_process` / `worker_namespace` 进程全局 | `protocol.h` | 进程 | 随 worker 模式删除 |
| 2 | `thread_local std::vector<uint64_t> inner_sql_namespace_overrides` | `gateway.ipp:34/57-63` | 线程（RAII push/pop） | **需替换为显式参数** |
| 3 | `shared_inner_sql_namespaces`：**进程级 map，键是 trace id 的 seq** | `gateway.ipp:65-80` | 跨线程，靠 trace 传播 | **需替换为显式参数** |
| — | 三条都不命中 → **兜底返回 ns 1** | `:76` | — | 这是"静默跨 ns 读错"的来源 |

第 3 条尤其值得注意：它把 ns 绑定挂在 **trace id** 上，靠"同一个 trace 在同一进程内传播"来跨线程传递上下文——这正是 ADR-0003 要刻意背离的 MTL 式做法，而且比 thread_local 更隐蔽（跨线程存活、靠 trace 巧合命中、miss 即静默落到 ns 1）。

**因此 Step 1 的完整工作量 = 身份分类（已判定，25 个函数）+ 把 ns 参数化到 inner SQL 入口**：

- 三条载体的全部读写点要改成显式传参；
- 关键是 inner SQL 的入口接口：`ObInnerSQLConnection::execute(...)` 一族目前不带 ns，ns 是从上面的载体里"捡"的。要让"共享层显式指定目标 ns"（`plan.md` inner SQL 规则），这个接口必须接收 ns 参数，并逐层传给它的一般调用者；
- 调用面实测：`current_namespace_id()` / `resolve_shared_inner_sql_namespace` 在 gateway 文件之外共 **13 处出现**：`ob_drop_table_helper.cpp` 5、`namespace_fork_kernel_prototype.cpp` 4、`ob_table_sql_service.cpp` 2、`namespace_worker_protocol_prototype.h` 的声明 1、kernel 头 1。

**这修正了 `gap §4` 的 Step 1 描述**：Step 1 不只是"24 个 kernel 函数拿 ns"，还包括"inner SQL 入口接收 ns"。前者是本文件 §1 的静态分类（已完成），后者是一个接口改造 + 调用链参数化，工作量与风险都更高。Step 1 的验收件也因此必须包括：**两 ns 并发探针**（证明两条 ambient 路径拆掉后不会静默落到 ns 1）。

## 6. 本次结论的边界

- §1 是静态判定，成立；§5 是机制补充，说明"显式化"的实现面比原估计宽。
- 二者都不改变总体判断（不变式 2 可成立），但**把 Step 1 从"分类问题"升级为"接口改造问题"**。若要在下一阶段开工，应先做 §5 的接口设计（inner SQL 入口带 ns），因为它决定 §1 中 B 类 10 个函数的 ns 从哪条链传进来。
- 判定结论不依赖运行时行为，因此**不需要新探针即可成立**；但若要进入实现，两 ns 并发探针是 Step 1 的必备验收件。
