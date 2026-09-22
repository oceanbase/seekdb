# Namespace Fork 单进程化：工作交接（13 个 issue 现状）

状态：本会话产出 **12 个提交**（`9b5eb105c..HEAD`；该分支整体 `master..HEAD` 为 110 个提交，其余为进入本会话前已有的 worker 多进程原型工作），issue 01–04 完成、05 部分完成、06–13 未开。本文是**可审计的交接**：每个 issue 的现状、实证出处、以及继续推进前必须先做的决策。
写于 commit `4417cc165`。所有数字与结论均可在当前工作区复现。

## 0. 一句话现状

`src/namespace/` 边界层（Namespace / Runtime / Registry / 登录绑定）已在单进程内落地并有断言验证；**fork 的数据语义（kernel）仍在 worker 多进程路径上，未在单进程形态下验证**，而它后面挂着 issue 05–13 的全部内容。

## 1. 逐 issue 现状

| Issue | 状态 | 交付 / 证据 | 备注 |
|---|---|---|---|
| **01** Phase 0a 隐式版本调用审计 | ✅ 完成 | `docs/research/namespace_implicit_version_audit.md`；commit `02ab69c5e` | 实证**纠正原文**：343 不成立（任何单一口径都对不上）；279 在"单行单 token"口径下成立；分类 A393 / B4 / **C3** / ?0，判定 **GO** |
| **02** Phase 0b 恒真开关 + bazel 门禁 | ✅ 清理完成 / 🔶 门禁未实证 | 清理 `27155917d`（6 文件 +90/−99，实测 `make` + bootstrap 套件 PASS）；门禁 `ce16cca2f` | 顺带修了仓库级破损：`observer_header_inventory.bzl` 缺 14 个 worker 原型头 → **bazel 在本分支早已完全不可用**。门禁 BUILD 就位，但本环境 bazel 连纯头文件目标都无法在 240–300s 内完成分析，故"违规被拦下"**未取得实证** |
| **03** 骨架 + 登录绑定 | ✅ 完成 | `42d012cea`、`6778769d5`；`docs/research/namespace_phase1a_skeleton.md` | 三件套 + `account@ns` 路由 + session 缓存 runtime；`reset()` 清空防 pooled 会话串号。**过程中修掉 2 个真实缺陷**（runtime 悬垂指针致 worker 崩；用户名缓冲区越界） |
| **04** schema 第二实例 spike | ✅ 完成 | `6af3909bc`、`2d4dac235`；`docs/research/namespace_schema_service_spike.md` | 根因：第二实例 init 失败于 `ObSchemaCache::init()` 往**进程级 KV 注册表**注册同名 cache（`ob_kv_storecache.cpp:479-482`）。修法：instance tag 命名空间化 cache 名。判据 `coexist=true … second_isolation=1` |
| **05** 双 ns 端到端 | 🔶 **部分** | 路由/服务组/热路径见下 | 见 §2。**验收标准当前无法执行**（见 §3.2） |
| **06** Phase 1 性能门禁 | ⬜ 未开始 | — | 依赖 05 |
| **07** DDL/rootserver 显式 ns | ⬜ 未开始 | — | 依赖 kernel 落位；审计已给出 152 处分布 |
| **08** inner SQL 显式目标 ns | ⬜ 未开始 | — | **已探明比原描述更重**，见 §3.1 |
| **09** 后台调度 Registry 化 | ⬜ 未开始 | — | — |
| **10** Phase 2 全查询面回归 | ⬜ 未开始 | — | — |
| **11** 删 IPC 层 | ⬜ 未开始 | — | 删除面实测远大于"14 个 `.ipp`"，见 §3.3 |
| **12** `CREATE NAMESPACE` + 退役 `FORK DATABASE` | ⬜ 未开始 | — | — |
| **13** 允许 drop 源 ns | ⬜ 未开始 | — | — |

## 2. issue 05 已完成的部分（各有断言）

| 能力 | commit | 验证 |
|---|---|---|
| 系统 ns 注册进 registry、runtime 持有 schema service 指针 | `42d012cea` | 门禁 PASS；`init_namespace_registry() => 0`（worker bootstrap 实测） |
| 登录绑定：每次 login 绑 ns 1，账号名剥掉 ns 后缀 | `42d012cea` | probe 证据 `2 ns_id=1 account=root` |
| hot path 4 入口改走 session 绑定的 runtime（`obmp_query` ×3、`ob_sql` ×1） | `e7122dc5f` | 契约断言 `system_routes_to_singleton=1`（ns1 → 单例，行为不变）、`fork_routes_to_owned=1` |
| runtime 拥有 per-ns schema service 实例 | `6778769d5` | `owns_schema=1 service_inited=1 tag=_ns2_probe singleton_leaked=0 owned_isolation=1` |
| **ns2 真的注册进 live registry 并参与路由** | `a9246fb5c` | `registry_count=2 resolve_ret=0 resolved_id=500001 same_runtime=1 fork_parent=1` |

**未完成**：ns2 的数据隔离本身（`root@ns2` 读写不串数据、COW/快照语义）、plan cache per-ns（走 `IPlanCacheAccessService` 抽象，非 bounded slice）、性能数字。

## 3. 继续推进前必须先处理的三件事

### 3.1 Step 1 已从"分类问题"升级为"接口改造问题"

`docs/research/namespace_step1_identity_feasibility.md`（`71c05f9c7`、`4417cc165`）：

- **判定通过**：kernel 里 26 处 ambient guard 归并为 **25 个函数**，ns 来源全部可显式化 —— A（签名已有）11 / B（由编码 id 推出）10 / C（结构性常量）3 / C'（自由函数全局 GC）1。**不变式 2 在 kernel 上成立，设计立论未被推翻。**
- **但** `resolve_shared_inner_sql_namespace()`（`namespace_worker_gateway_prototype.ipp:65`）有**三条** ambient 载体：worker 进程全局、`thread_local` override 栈、以及**键为 trace id 的进程级 map**；三条都不命中就**静默返回 ns 1**。第三条比 thread_local 更隐蔽，正是 ADR-0003 要背离的形态。
- 因此 Step 1 还包含**把 ns 参数化到 inner SQL 入口**（`ObInnerSQLConnection::execute` 一族现在不收 ns）。这一步的工作量和风险高于身份分类。

### 3.2 issue 05 的验收标准当前无法执行

- **parser 里没有 `FORK NAMESPACE`**：只有 `FORK TABLE` / `FORK DATABASE`（`sql_parser_mysql_mode.y:4400/4407`）。issue 05 的"FORK NAMESPACE ns2 FROM ns1 在单进程内完成"**需要先加语法**（或改验收口径）。
- **forked ns 的 schema 权威未选型**：现状是 kernel catalog 覆盖层直接服务（`gateway.ipp:544-568`），**没有任何代码为 fork 出的 ns 实例化 schema service**（`alloc_instance()` 仅被探针调用）。路线 A（保留覆盖层）有界；路线 B（actualize 真 per-ns schema service）与 fork O(1) 卖点冲突。**这个选型会改变 issue 05/06 的验收定义**，必须先定。

### 3.3 Phase 3"删 IPC 层"的删除面比 issue 描述大

实测（见 `namespace_kernel_single_process_gap.md` §0 复核表，数字以复核为准）：

| 口径 | 文件数 | 引用数 |
|---|---:|---:|
| kernel 类名，**存活** `.cpp/.h` | 17 | 98 |
| kernel 类名，worker `.ipp`（将删） | 7 | 58 |
| kernel ∨ worker 全局，存活 `.cpp/.h` | 44 | 284 |

worker `.ipp` 只占约 20%，**工作量在存活文件里**，需逐处判定归属（删除 / 改直调 / 仍依赖 IPC）。

## 4. 建议的下一步（按依赖排序）

1. **决策：Step 6 架构选型**（forked ns 的 schema 权威走覆盖层还是真 per-ns service）。这是唯一的前置决策，直接决定 issue 05/06 的验收定义。
2. **据选型结果重写 issue 05/06 的验收标准**（含 `FORK NAMESPACE` 语法是否要做，或改用现有 `FORK DATABASE` 口径验收）。
3. **设计 inner SQL 入口的显式 ns 接口**（Step 1 的接口部分），它决定身份分类里 B 类 10 个函数的 ns 从哪条链传入。
4. **实现 Step 1 并新建两 ns / 两线程并发探针**作为验收件 —— 身份显式化**没有编译期保护**，漏一处会静默落到 ns 1，只能靠运行期探针证明。
5. 之后才是 07–13。

## 5. 可复现的验证入口

```bash
# 编译
source ~/.bashrc && cd build_release && make -j16 seekdb

# 冷启动 / 崩溃恢复门禁（三种模式都应 PASS）
export SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/data/1/nijia.nj/test
python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb
SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE=1  … --binary …   # 第二 schema 实例共存
SEEKDB_NAMESPACE_SERVICE_GROUP_PROBE=1  … --binary …   # ns2 注册 + 服务组 + 路由契约
SEEKDB_NAMESPACE_LOGIN_BIND_PROBE=1     … --binary …   # 登录绑定（结果写 /tmp/ns-login-bind.result）

# 本会话的产出文档
ls docs/research/namespace_{implicit_version_audit,schema_service_spike,phase1a_skeleton,kernel_single_process_gap,step1_identity_feasibility}.md
```

## 6. 诚实边界

- 本交接里"已验证"一律指**有可复现命令或断言**；"未实证"（如 bazel 门禁）已明确标注，未当作通过。
- kernel 落位是**静态分析 + 机制判定**，未做运行期验证；§4.4 的两 ns 探针尚不存在。
- `docs/research/namespace_single_process_spec.md` 仍标注"待发布到 issue tracker"；§3 的决策与 §1 的纠正（`343`、`FORK NAMESPACE` 不存在、门禁未实证）是提交它之前必须补进去的内容。
