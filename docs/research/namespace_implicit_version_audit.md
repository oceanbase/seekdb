# Phase 0a：隐式当前版本调用三分类审计（v2，数据修正版）

状态：审计结论（只读分析，未改任何代码）。
上游文档：`namespace_single_process_audit.md`（§3 给出 343）、`namespace_single_process_plan.md`（Phase 0.1）、`docs/adr/0003-no-ambient-context.md`、issue `.scratch/namespace-fork/issues/01-implicit-version-call-audit.md`。

> **v2 修订说明（数据完整性）**
> v1 的 §2 存在计数错误：把"某个子集的 grep 结果"当成总数（写成 357），并据此推出 327，与 §0 摘要自相矛盾。v2 全部计数由 §11 的单一脚本重算，且**每个数字都附有产生它的命令**。
> **行号口径**：所有 `file:line` 均取自**当前工作区**。审计期间工作区被其它进程并发修改过（`src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp` 等 6 个文件），v2 已按修改后的树重新生成全部行号。

**接受判据**：分类 C（必须 ambient）≤ 10 → 继续；> 10 → 停下重新评估方案。

---

## 0. 摘要

### 0.1 三个访问器的精确构成

命令（`src/` 全树，含 `.h`/`.cpp`/`.ipp`）：

```bash
rg -n --no-heading 'get_runtime_schema_guard' src | wc -l                    # 467
rg -n --no-heading 'get_runtime_refreshed_schema_version' src | wc -l        # 45
rg -n --no-heading 'get_published_schema_version' src | wc -l                # 19
```

| 访问器 | 标识符出现（rg） | 声明/定义/内部转发 | 更长同名/注释 | **调用表达式** | 其中**隐式版本** | 显式版本 |
|---|---|---|---|---|---|---|
| `get_runtime_schema_guard` | 467 | 1 | 110 | **356** | **329** | 22 |
| `get_runtime_refreshed_schema_version` | 45 | 3 | 0 | **42** | **39** | 0 |
| `get_published_schema_version` | 19 | 3 | 0 | **16** | **17** | 0 |
| **合计** | **531** | **7** | **110** | **414** | **385** | **22** |

**归约核对**：467 − 1 − 110 = **356**；45 − 3 = **42**；19 − 3 = **16**；531 − 7 − 110 = **414**；414 − 22 = **392**…

> **⚠️ 上表是 §11 脚本口径，不能与 §4 清单直接相加**（published 出现 17 > 调用 16 即为迹象）。**唯一权威口径是 §2.5 的"清单口径"与 §4 清单**：调用表达式 **417**（S 400 + Phase-3 17）、隐式 **390**、显式 **27**；S 内隐式 **378**、显式 **22**。§0.2/§0.3/§3 均引用该口径。

"非调用"共 117 处的构成（脚本口径）：

- **声明/定义 6 处**：`ob_multi_version_schema_service.h:158/208/216`（三族声明）、同文件 `cpp:656/2371/2405`（三族定义）；脚本另把 `cpp:2402` 的内部转发调用也算作"非调用"（故其表为 7），**清单口径把它算作调用**。
- **更长同名前缀 108 处**：`get_runtime_schema_guard_with_version_in_inner_table`（会命中 `get_runtime_schema_guard` 前缀，必须剔除）。
- **注释 2 处**：`ob_change_stream_fetcher.cpp:374`、`ob_multi_version_schema_service.cpp:2969`。
- **同名无关 1 处**：`ob_ddl_task.h:686`（`ObDDLTask` 成员，与 schema service 无关）。

隐式/显式判定规则：`runtime_schema_version` 缺省或为 `OB_INVALID_VERSION` → 隐式；`core_schema_version` 缺省或为 `false` → 隐式；`namespace_worker_gateway_prototype.ipp` 的 3 处 `id != 0` 三元形式按**显式**计（条件选择，不是"取当前"）。

### 0.2 审计原文的 343 / 279 复核结论

| 审计原文 | 复核结论 |
|---|---|
| "279 `get_runtime_schema_guard` 默认版本调用" | **成立**。精确等于 `rg -n --no-heading 'get_runtime_schema_guard\([A-Za-z_][A-Za-z0-9_:]*\)' src` = **279**，即"单行写法、单 token 实参"的 1 实参调用。口径偏窄（另有 42 处跨行 + 6 处带空格/箭头/解引用），数字本身正确 |
| "45 refreshed" | 与**标识符出现数**一致（45）；调用表达式 **43**（声明 1 + 定义 1）；**全部为隐式当前版本** |
| "19 published" | 与**标识符出现数**一致（19）；调用表达式 **18**（声明 1 + 定义 1 + 同名无关 1）；**全部为隐式当前版本** |
| "总数 343" | **不成立**：任何单一口径都对不上——隐式版本 **390**（S=400 内 378）、调用表达式 **417**、标识符出现 **531**。"343 = 279+45+19" 是把"窄口径 1 实参调用"与两族的"标识符出现数"混加 |

### 0.3 本报告口径

- **需分类点 S = 400** = 全部调用表达式（417）减去 Phase-3 待删的 `namespace_*_prototype.ipp`（17）。
- **其中隐式版本 378 处**（等于 §3 的分目录合计）。
- Phase-3 的 17 处随文件整体删除，**单列不分类**。

### 0.4 分类结果（对 S = 400）

| 分类 | 含义 | 数量 |
|---|---|---|
| A | 能传 session / 任务 / plan / 服务实例上下文 | **393** |
| B | 能从已有编码 id 推出 ns（`database_of`/`local_of`） | **4** |
| C | 必须 ambient（thread_local / scoped guard） | **3** |
| ? | 无法给出确定判定 | **0** |
| **合计** | | **400** |

**判定：GO。** C = 3 ≤ 10。v1 的 C=3 结论**在重新计数后依然成立**（3 处 C 都是隐式版本点，且都在 S 内）；go/no-go 结论不变。

---

## 1. 判定依据：访问器的真实默认参数语义

`src/share/schema/ob_multi_version_schema_service.h`：

```cpp
// h:158
virtual int get_runtime_schema_guard(ObSchemaGetterGuard &guard,
                                    int64_t runtime_schema_version = common::OB_INVALID_VERSION,
                                    const RefreshSchemaMode refresh_schema_mode = RefreshSchemaMode::NORMAL);
// h:208
virtual int get_runtime_refreshed_schema_version(int64_t &schema_version,
                                                 const bool core_schema_version = false) const;
// h:216
virtual int get_published_schema_version(int64_t &schema_version, const bool core_schema_version = false) const;
```

- `get_runtime_schema_guard` 不传 `runtime_schema_version`（或传 `OB_INVALID_VERSION`）→ "取**服务端当前** runtime schema"，单进程多 ns 下即歧义点。
- 另两族没有版本参数，只有 `core_schema_version` 布尔量；**不显式传 `true` 就是"取服务端当前版本"**。
- `get_live_runtime_refreshed_schema_version`（`h:213`）是旁路，共 3 处标识符出现（声明 1、定义 1、内部调用 1），无外部调用点，未计入 §0 三访问器。
- `ob_ddl_task.h:686` 的 `int64_t get_published_schema_version()`（及 `ob_ddl_task.cpp:547` 的调用）是**同名无关的 `ObDDLTask` 成员访问器**，语义为"任务记录里的已发布版本"，不是 schema service 查询，v2 从中剔除。v1 计入是不严谨的。

---

## 2. 计数复核（每一行都有命令）

### 2.1 原始出现数

```bash
rg -n --no-heading 'get_runtime_schema_guard' src | wc -l                    # → 467
rg -n --no-heading 'get_runtime_refreshed_schema_version' src | wc -l        # → 45
rg -n --no-heading 'get_published_schema_version' src | wc -l                # → 19
rg -l 'get_runtime_(schema_guard|refreshed_schema_version)|get_published_schema_version' src | wc -l   # → 156 个文件
```

### 2.2 调用表达式与实参个数

```bash
# 单行、单 token、1 实参（= 审计的 279）
rg -n --no-heading 'get_runtime_schema_guard\([A-Za-z_][A-Za-z0-9_:]*\)' src | wc -l          # → 279
# 开括号在行尾（跨行调用，需手数实参）
rg -n --no-heading '[.>)]get_runtime_schema_guard\($' src -g '*.cpp' | wc -l                 # → 56
# 单行内即出现逗号（2/3 实参）
rg -n --no-heading '[.>)]get_runtime_schema_guard\([^)]*,' src -g '*.cpp' | wc -l            # → 5
# 非限定的类内自调用（impl 文件）
rg -n '^[^A-Za-z0-9_:.]get_runtime_schema_guard\(' \
   src/share/schema/ob_multi_version_schema_service.cpp | wc -l                              # → 24（含 1 处定义）
```

| 访问器 / 形态 | guard | refreshed | published |
|---|---|---|---|
| 调用表达式总数 | **356** | **42** | **16** |
| ├ 1 实参 | 327 | 42 | 16 |
| ├ 2 实参 | 23 | 0 | 0 |
| └ 3 实参 | 6 | 0 | 0 |
| 1 实参且**单行单 token**（审计 279 口径） | **279** | — | — |
| 1 实参但**跨行** | 42 | — | — |
| 1 实参但实参含空格/箭头/解引用 | 6 | — | — |

**1 实参 327 的校准**：279 + 42 + 6 = **327**；356 − 327 = 29 是 2/3 实参（24 个在 `.cpp`，5 个在 `.ipp`）。

**隐式版本**（1 实参，或显式传 `OB_INVALID_VERSION` / `core_schema_version=false`）：

| 访问器 | 调用表达式 | 显式版本 | **隐式版本** | 备注 |
|---|---|---|---|---|
| guard | 356 | 27 | **329** | 27 = 22 个外部 `.cpp` + 5 个 `.ipp`（Phase-3） |
| refreshed | 43 | 0 | **43** | 无显式版本点 |
| published | 18 | 0 | **18** | 无显式版本点 |
| **合计（清单口径）** | **417** | **27** | **390** | |
| 其中 Phase-3 `.ipp` | 17 | 5 | 12 | |
| **S = 400（需分类）** | **400** | **22** | **378** | 与 §3 分目录合计完全一致 |

### 2.3 声明、定义、内部自调用、同名无关访问器

```bash
# 声明/定义（4 处）
rg -n 'int\s+get_(runtime_schema_guard|runtime_refreshed_schema_version|published_schema_version)\s*\(' \
   src/share/schema/ob_multi_version_schema_service.h src/rootserver/ddl_task/ob_ddl_task.h
# 更长的同前缀函数（非本访问器，共 108 处）
rg -c 'get_runtime_schema_guard_with_version_in_inner_table' src | awk -F: '{s+=$2} END {print s}'   # → 108
# 纯注释提及（2 处）
rg -n '//.*get_runtime_schema_guard' src      # → ob_change_stream_fetcher.cpp:374、ob_multi_version_schema_service.cpp:2969
```

- **声明/定义 4 处**：`h:158`、`h:208`、`h:216`，与同名无关的 `ob_ddl_task.h:686`。
- **更长同名 108 + 注释 2 = 110 处"非调用"**：正是 v1 把子集当总数的根源——`rg 'get_runtime_schema_guard'` 会把这些前缀命中。
- **内部自调用**：`ob_multi_version_schema_service.cpp` 内 23 处非限定自调用（如 `OB_FAIL(get_runtime_schema_guard(guard))`）**是**调用表达式，已计入 356。该文件内该标识符共 26 处 = 1 定义 + 1 更长同名函数的声明 + 1 注释 + 23 自调用。
- **同名无关访问器**：`ob_ddl_task.h:686` + `ob_ddl_task.cpp:547` 属 `ObDDLTask`，已剔除（不属本报告任何一类）。

### 2.4 Phase-3 待删文件中的 17 处

```bash
rg -n --no-heading 'get_runtime_(schema_guard|refreshed_schema_version)|get_published_schema_version' \
   src/observer/namespace_*_prototype.ipp src/observer/namespace_sql_worker_prototype.ipp | wc -l   # → 17
```

| 文件 | 行号·访问器 |
|---|---|
| `src/observer/namespace_sql_worker_prototype.ipp` | 273R 360G |
| `src/observer/namespace_worker_commands_prototype.ipp` | 76R 101Ge 106Ge |
| `src/observer/namespace_worker_gateway_prototype.ipp` | 474R 479Ge 511Re 512Pe |
| `src/observer/namespace_worker_range_prototype.ipp` | 121G |
| `src/observer/namespace_worker_scan_prototype.ipp` | 73G 92G 155Ge |
| `src/observer/namespace_worker_write_prototype.ipp` | 214G 269G 2087G 2933Ge |

（`G`=guard / `R`=refreshed / `P`=published；`e`=显式版本。这 17 处随文件按 plan Phase 3.1 整体删除，**不参与分类与判据**。）

### 2.5 三口径对账（最终口径）

| 口径 | guard | refreshed | published | 合计 |
|---|---|---|---|---|
| 标识符出现（`rg`） | 467 | 45 | 19 | **531** |
| − 声明/定义/同名无关 | 1 | 2 | 3 | 6 |
| − 更长同名/注释 | 110 | 0 | 0 | 110 |
| **= 调用表达式** | **356** | **43** | **18** | **417** |
| − 显式版本 | 27 | 0 | 0 | 27 |
| **= 隐式版本** | **329** | **43** | **18** | **390** |
| − Phase-3 待删 `.ipp` | 12 | 4 | 1 | **17** |
| **= 需分类点 S** | **344** | **39** | **17** | **400** |
| S 内隐式 / 显式 | 322 / 22 | 39 / 0 | 17 / 0 | **378 / 22** |

**显式版本 27 处的完整来源**（全是 guard）：`ob_major_merge_progress_checker.cpp:675/1141`、`ob_table_ckm_items.cpp:574`、`ob_ddl_task.cpp:1919`、`ob_constraint_task.cpp:527`、`ob_column_redefinition_task.cpp:108`、`ob_drop_index_task.cpp:131`、`ob_complement_data_task.cpp:140/150/582/1169/1634`、`ob_build_index_task.cpp:1048`、`ob_expr_udf.cpp:600`、`ob_sql_utils.cpp:3109`、`ob_cs_plugin_async_index.cpp:82`、`ob_virtual_table_iterator_factory.cpp:283`、`ob_multi_version_schema_service.cpp:761/765/955/1047`、`.ipp` 5 处（Phase-3）。

---

## 3. 按目录分布（实测）

**需分类点 S = 400**（外部调用表达式）：

| 目录 | guard | refreshed | published | 合计 | 其中隐式版本 | 显式版本 |
|---|---|---|---|---|---|---|
| rootserver（含 ddl_task/parallel_ddl/pl_ddl/freeze/fork_table/truncate_info） | 167 | 5 | 1 | **173** | 167 | 6 |
| share（含 share/schema） | 34 | 14 | 1 | **49** | 41 | 8 |
| observer（含 vector_index/mysql/virtual_table/change_stream/dbms_scheduler/ai_service） | 43 | 14 | 10 | **67** | 65 | 2 |
| sql（含 engine/cmd/expr/px/das/resolver/session/optimizer/code_generator/executor） | 72 | 4 | 4 | **80** | 78 | 2 |
| storage（含 ddl/tablet/ls/lob/fts/compaction） | 24 | 3 | 0 | **27** | 22 | 5 |
| pl | 4 | 0 | 0 | **4** | 4 | 0 |
| **合计** | **344** | **40** | **16** | **400** | **378** | **22** |

**另有 Phase-3 待删 `.ipp` 17 处**（不分类）：guard 12、refreshed 4、published 1（§2.4）。S = 400 与 §0.3/§0.4 一致。

---

## 4. 全量清单（400 需分类点 + 17 Phase-3）

编码：`<行号><访问器><i|e>`。`G`=`get_runtime_schema_guard`，`R`=`get_runtime_refreshed_schema_version`，`P`=`get_published_schema_version`；`i`=隐式当前版本，`e`=显式版本；非 A 类站点加 `[B]`/`[C]` 标记。**清单行数 = 400（A/B/C 需分类）+ 17（Phase-3）= 417。**


### 清单 A. 需分类点 S = 400（非 Phase-3）

**rootserver（173）**

- `src/rootserver/ddl_task/ob_column_redefinition_task.cpp`: 108Ge 530Gi
- `src/rootserver/ddl_task/ob_constraint_task.cpp`: 61Gi 230Gi 527Ge 576Gi 614Gi 822Gi 1112Gi 1777Gi
- `src/rootserver/ddl_task/ob_ddl_common_rs_impl.cpp`: 88Gi 356Gi
- `src/rootserver/ddl_task/ob_ddl_redefinition_task.cpp`: 176Gi 618Gi 956Gi 1051Gi 1186Gi 1308Gi 1405Gi 2250Gi
- `src/rootserver/ddl_task/ob_ddl_scheduler.cpp`: 1602Gi 1611Gi
- `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.cpp`: 442Gi
- `src/rootserver/ddl_task/ob_ddl_task.cpp`: 547Pi 832Gi 1040Gi 1919Ge
- `src/rootserver/ddl_task/ob_drop_fts_index_task.cpp`: 313Gi 391Gi 596Gi
- `src/rootserver/ddl_task/ob_drop_index_task.cpp`: 131Ge 209Gi 373Gi
- `src/rootserver/ddl_task/ob_drop_lob_task.cpp`: 138Gi 265Gi
- `src/rootserver/ddl_task/ob_drop_vec_index_task.cpp`: 505Gi 581Gi 814Gi 993Gi
- `src/rootserver/ddl_task/ob_drop_vec_ivf_index_task.cpp`: 332Gi 438Gi 464Gi 493Gi
- `src/rootserver/ddl_task/ob_fts_index_build_task.cpp`: 344Gi 850Gi 1582Gi 1804Gi 1913Gi
- `src/rootserver/ddl_task/ob_index_build_task.cpp`: 80Gi 500Gi 647Gi 709Gi 748Gi 987Gi 1079Gi 1165Gi 1361Gi
- `src/rootserver/ddl_task/ob_modify_autoinc_task.cpp`: 73Gi 316Gi 390Gi 551Gi
- `src/rootserver/ddl_task/ob_rebuild_index_task.cpp`: 167Gi 252Gi 410Gi 576Gi
- `src/rootserver/ddl_task/ob_table_redefinition_task.cpp`: 754Gi 826Gi
- `src/rootserver/ddl_task/ob_vec_index_build_task.cpp`: 351Gi 410Gi 1664Gi 1832Gi
- `src/rootserver/ddl_task/ob_vec_ivf_index_build_task.cpp`: 338Gi 385Gi 428Gi 478Gi 1592Gi 1755Gi
- `src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp`: 995Gi 1934Gi 2027Gi 3031Gi 3128Gi
- `src/rootserver/fork_table/ob_fork_table_task.cpp`: 542Gi
- `src/rootserver/freeze/ob_checksum_validator.cpp`: 73Gi
- `src/rootserver/freeze/ob_local_major_freeze.cpp`: 314Gi
- `src/rootserver/freeze/ob_major_freeze_helper.cpp`: 92Gi
- `src/rootserver/freeze/ob_major_merge_progress_checker.cpp`: 404Ri 445Gi 468Gi 674Ri 675Ge 890Gi 1140Ri 1141Ge
- `src/rootserver/ob_alter_table_constraint_checker.cpp`: 222Gi
- `src/rootserver/ob_bootstrap.cpp`: 666Gi
- `src/rootserver/ob_ddl_operator.cpp`: 226Gi 234Gi 259Gi 280Gi 303Gi 311Gi 331Gi 388Gi 395Gi 410Gi 500Gi 577Gi 745Gi 1433Gi 1455Gi 1649Gi 1801Gi 2077Gi 2146Gi 2435Gi 2583Gi 2895Gi 3002Gi 3477Gi 3878Gi 3906Gi 4049Gi 4086Gi 4125Gi 4167Gi 4234Gi 4276Gi 4321Gi 4360Gi 4400Gi 4663Gi 4727Gi 5121Gi 5220Gi 5319Gi 5776Gi
- `src/rootserver/ob_ddl_service.cpp`: 3245Gi 4919Gi 5103Gi 5541Gi 7411Gi 7485Gi 7706Gi 7824Gi 8006Gi 8165Gi 8312Gi 8552Gi 10932Gi 12502Gi 12794Gi 14729Gi 20731Gi 22029Ri
- `src/rootserver/ob_index_builder.cpp`: 1791Gi
- `src/rootserver/ob_local_management_service.cpp`: 599Ri 2678Gi 2693Gi 2906Gi
- `src/rootserver/ob_objpriv_mysql_ddl_operator.cpp`: 54Gi 117Gi 170Gi
- `src/rootserver/parallel_ddl/ob_create_index_helper.cpp`: 526Gi
- `src/rootserver/parallel_ddl/ob_ddl_helper.cpp`: 877Gi 970Gi 992Gi 993Gi
- `src/rootserver/parallel_ddl/ob_table_helper.cpp`: 334Gi
- `src/rootserver/pl_ddl/ob_pl_ddl_operator.cpp`: 693Gi 699Gi 728Gi
- `src/rootserver/truncate_info/ob_truncate_info_service.cpp`: 67Gi

**share（49）**

- `src/share/ob_ddl_common.cpp`: 392Gi 413Gi 468Gi 735Gi 843Gi 1032Gi 1196Ri
- `src/share/ob_sys_time_zone_util.cpp`: 41Gi
- `src/share/schema/ob_latest_schema_guard.cpp`: 59Ri
- `src/share/schema/ob_multi_version_schema_service.cpp`: 656Ge 739Gi 761Ge 765Ge 955Ge 997Ri 1047Ge 1339Ri 1344Gi 1381Ri 1386Gi 1409Gi 1688Gi 1895Gi 1924Gi 1952Gi 2001Gi 2027Gi 2084Gi 2119Ri 2186Ri 2273Ri 2351Ri 2371Ri 2402Ri 2405Pi 2468Ri 2486Ri 2557Ri 2590Gi 2858Gi 2940Gi 2973Gi 2991Gi 3000Gi 3009Gi 3018Gi
- `src/share/schema/ob_schema_utils.cpp`: 290Gi 697Gi
- `src/share/schema/ob_table_sql_service.cpp`: 1834Gi

**observer（67）**

- `src/observer/ai_service/ob_ai_service_executor.cpp`: 61Gi
- `src/observer/change_stream/ob_change_stream_dispatcher.cpp`: 103Ri
- `src/observer/change_stream/ob_change_stream_fetcher.cpp`: 361Ri 757Ri 840Ri
- `src/observer/change_stream/ob_cs_plugin_async_index.cpp`: 82Ge
- `src/observer/dbms_scheduler/ob_dbms_sched_job_executor.cpp`: 126Gi
- `src/observer/mysql/obmp_base.cpp`: 415Ri 532Gi
- `src/observer/mysql/obmp_connect.cpp`: 169Gi 370Gi 522Ri 523Pi 527Gi 572Ri 573Pi 578Gi 800Gi
- `src/observer/mysql/obmp_init_db.cpp`: 86Pi 87Ri 118Ri 193Gi
- `src/observer/mysql/obmp_query.cpp`: 104Pi 515Ri
- `src/observer/mysql/obmp_reset_connection.cpp`: 60Gi
- `src/observer/mysql/obmp_stmt_execute.cpp`: 1253Gi 1855Gi 2020Pi
- `src/observer/mysql/obmp_stmt_fetch.cpp`: 296Gi 460Pi
- `src/observer/mysql/obmp_stmt_prepare.cpp`: 173Pi 253Gi 304Ri
- `src/observer/mysql/obmp_stmt_send_long_data.cpp`: 139Pi
- `src/observer/ob_inner_sql_connection.cpp`: 874Gi 1234Pi
- `src/observer/ob_server.cpp`: 1498Ri 2988Gi 3002Gi
- `src/observer/ob_server_schema_updater.cpp`: 254Ri
- `src/observer/vector_index/ob_hybrid_vector_refresh_task.cpp`: 267Gi 335Gi 413Gi
- `src/observer/vector_index/ob_ivf_async_task.cpp`: 58Gi
- `src/observer/vector_index/ob_ivf_async_task_executor.cpp`: 392Gi 423Gi 456Gi
- `src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp`: 2934Gi
- `src/observer/vector_index/ob_plugin_vector_index_scheduler.cpp`: 90Gi 201Gi 431Gi
- `src/observer/vector_index/ob_plugin_vector_index_service.cpp`: 1517Gi
- `src/observer/vector_index/ob_plugin_vector_index_utils.cpp`: 349Gi 382Gi 1196Gi 1519Gi 1569Gi 1944Gi
- `src/observer/vector_index/ob_vector_index_async_task_util.cpp`: 341Gi 1271Gi 1631Gi 2033Gi
- `src/observer/virtual_table/ob_all_virtual_server_schema_info.cpp`: 36Ri 37Pi 40Gi
- `src/observer/virtual_table/ob_virtual_table_iterator_factory.cpp`: 283Ge
- `src/observer/virtual_table/ob_virtual_table_projector.cpp`: 100Gi

**sql（80）**

- `src/sql/code_generator/ob_static_engine_cg.cpp`: 6652Gi 6689Gi
- `src/sql/das/ob_das_retry_ctrl.cpp`: 52Gi
- `src/sql/das/ob_das_tablet_mapper.cpp`: 508Gi 600Gi
- `src/sql/das/ob_das_utils.cpp`: 60Gi
- `src/sql/engine/cmd/ob_dcl_executor.cpp`: 136Gi 294Gi 348Gi
- `src/sql/engine/cmd/ob_ddl_executor_util.cpp`: 359Ri 371Ri
- `src/sql/engine/cmd/ob_routine_executor.cpp`: 311Gi
- `src/sql/engine/cmd/ob_set_names_executor.cpp`: 147Gi
- `src/sql/engine/cmd/ob_table_executor.cpp`: 569Ri 571Pi 1311Gi 1700Gi 1947Gi
- `src/sql/engine/cmd/ob_trigger_executor.cpp`: 55Gi
- `src/sql/engine/cmd/ob_user_cmd_executor.cpp`: 170Gi 312Gi 478Gi 583Gi
- `src/sql/engine/cmd/ob_variable_set_executor.cpp`: 722Gi 932Gi
- `src/sql/engine/cmd/ob_vector_index_refresh.cpp`: 270Gi 482Gi
- `src/sql/engine/expr/ob_expr_ai/ob_ai_func_utils.cpp`: 1360Gi
- `src/sql/engine/expr/ob_expr_current_user.cpp`: 69Gi
- `src/sql/engine/expr/ob_expr_current_user_priv.cpp`: 115Gi 205Gi
- `src/sql/engine/expr/ob_expr_inner_info_cols_printer.cpp`: 86Gi 417Gi 876Gi
- `src/sql/engine/expr/ob_expr_inner_table_option_printer.cpp`: 79Gi 165Gi
- `src/sql/engine/expr/ob_expr_mysql_proc_info.cpp`: 174Gi 180Gi
- `src/sql/engine/expr/ob_expr_object_construct.cpp`: 89Gi
- `src/sql/engine/expr/ob_expr_udf.cpp`: 600Ge
- `src/sql/engine/expr/ob_pl_expr_subquery.cpp`: 195Gi
- `src/sql/engine/ob_sql_memory_manager.cpp`: 820Gi
- `src/sql/engine/prepare/ob_execute_executor.cpp`: 71Pi
- `src/sql/engine/px/exchange/ob_px_dist_transmit_op.cpp`: 369Gi 405Gi
- `src/sql/engine/px/exchange/ob_px_repart_transmit_op.cpp`: 119Gi
- `src/sql/engine/px/ob_granule_pump.cpp`: 1149Gi
- `src/sql/engine/px/ob_px_sub_coord.cpp`: 819Gi
- `src/sql/engine/px/ob_px_task_process.cpp`: 385Gi
- `src/sql/engine/px/ob_px_util.cpp`: 766Gi 2146Gi
- `src/sql/executor/ob_cmd_executor.cpp`: 575Gi
- `src/sql/executor/ob_maintain_dependency_info_task.cpp`: 147Gi
- `src/sql/ob_query_retry_ctrl.cpp`: 272Gi
- `src/sql/ob_spi.cpp`: 1113Gi 1235Gi 2474Gi 3900Gi 5916Ri 5918Pi 6318Gi
- `src/sql/ob_sql.cpp`: 1115Gi
- `src/sql/ob_sql_utils.cpp`: 2599Pi 3109Ge
- `src/sql/optimizer/ob_table_location.cpp`: 4972Gi
- `src/sql/optimizer/stat/ob_opt_stat_monitor_manager.cpp`: 755Gi 929Gi
- `src/sql/privilege_check/ob_privilege_check.cpp`: 855Gi
- `src/sql/resolver/cmd/ob_alter_system_resolver.cpp`: 278Gi
- `src/sql/resolver/ddl/ob_create_view_resolver.cpp`: 913Gi
- `src/sql/resolver/ddl/ob_ddl_resolver.cpp`: 3986Gi
- `src/sql/resolver/ddl/ob_fts_index_builder_util.cpp`: 2712Gi
- `src/sql/resolver/ddl/ob_index_builder_util.cpp`: 696Gi 732Gi
- `src/sql/session/ob_basic_session_info.cpp`: 321Gi 772Gi 806Gi
- `src/sql/session/ob_sql_session_info.cpp`: 85Gi
- `src/sql/session/ob_user_resource_mgr.cpp`: 326Gi
- `src/sql/tablelock/ob_lock_executor.cpp`: 459Gi

**storage（27）**

- `src/storage/compaction/ob_runtime_status_cache.cpp`: 72Gi
- `src/storage/compaction/ob_table_ckm_items.cpp`: 574Ge
- `src/storage/ddl/ob_build_index_task.cpp`: 1048Ge
- `src/storage/ddl/ob_complement_data_task.cpp`: 140Ge 150Ge 582Ge 1031Gi 1169Ge 1634Ge
- `src/storage/ddl/ob_ddl_merge_task.cpp`: 1238Gi
- `src/storage/ddl/ob_ddl_struct.cpp`: 469Gi[B]
- `src/storage/ddl/ob_delete_lob_meta_row_task.cpp`: 267Gi
- `src/storage/ddl/ob_tablet_slice_writer.cpp`: 74Gi[B] 114Gi[B]
- `src/storage/fts/ob_fts_doc_word_iterator.cpp`: 224Gi
- `src/storage/lob/ob_lob_location.cpp`: 37Gi
- `src/storage/ls/ob_ls_tablet_service.cpp`: 3055Gi 4658Gi
- `src/storage/ob_dml_running_ctx.cpp`: 267Gi 296Ri
- `src/storage/ob_storage_schema_recorder.cpp`: 268Gi[B]
- `src/storage/ob_tablet_autoincrement_service.cpp`: 546Gi[C]
- `src/storage/ob_tablet_stat_mgr.cpp`: 879Gi[C] 942Gi[C]
- `src/storage/tablet/ob_tablet.cpp`: 2734Gi 2746Ri 4204Ri

**pl（4）**

- `src/pl/ob_pl.cpp`: 170Gi
- `src/pl/pl_cache/ob_pl_cache_mgr.cpp`: 254Gi
- `src/pl/sys_package/ob_dbms_ai_service.cpp`: 274Gi 336Gi

### 清单 B. Phase-3 待删 .ipp（17，不参与分类）

**observer（17）**

- `src/observer/namespace_sql_worker_prototype.ipp`: 273Ri 360Gi
- `src/observer/namespace_worker_commands_prototype.ipp`: 76Ri 101Ge 106Ge
- `src/observer/namespace_worker_gateway_prototype.ipp`: 474Ri 479Ge 511Re 512Pe
- `src/observer/namespace_worker_range_prototype.ipp`: 121Gi
- `src/observer/namespace_worker_scan_prototype.ipp`: 73Gi 92Gi 155Ge
- `src/observer/namespace_worker_write_prototype.ipp`: 214Gi 269Gi 2087Gi 2933Ge


---

## 5. 三分类判定规则

> issue 判据：**A** = 可从 session / DDL 任务 / plan / 已携带目标 ns 的对象到达；**B** = 可从已有编码 tablet/table id 经 `database_of`/`local_of` 推出；**C** = 两者皆不成立，今天唯一手段是 thread_local/scoped guard。

| 情形 | 归类 | 理由 |
|---|---|---|
| 接收者是构造/init 注入的实例（`schema_service_`、形参 `schema_service`、`multi_version_schema_service_`） | **A** | 把注入源从全局换成 Registry/Runtime 解析出的实例，调用点代码不变 |
| 所在类属 per-ns 服务组（plan cache / PS cache / session mgr / 会话级），或初始化由登录绑定、DDL 任务派发自带目标 | **A** | 单进程化后实例天然只服务一个 ns |
| 全局入口（`GCTX.schema_service_`/`GSCHEMASERVICE`/`get_instance()`/`server_service<ObSchemaRuntimeService>()`）但同一作用域内已有 session / exec ctx / plan ctx / table_id / database_id | **A**（接收者需换实例） | 有显式载体可传 |
| 同上，但作用域内只有**编码 id** 且它是唯一 ns 线索 | **B** | `database_of(id)` 可直接推出 ns |
| 进程级单例 / 全局调度实体，实例须跨 ns 工作，作用域内无 session/任务/plan，id 线索是"集合"而非"单点" | **C** | 需 per-ns 实例化或新增显式参数；今天没有这条路径 |
| 无法确认可达性 | **?** | 保留，不猜 |

A 类细分：**A1** 接收者已是本 ns 实例（零改动语义）；**A2** 换接收者（全局 → 本 ns 实例）；**A3** 补参数（显式传递，非 ambient）。

---

## 6. 分类结果

| 分类 | S = 400（需分类） | 全部调用表达式 = 417（含 Phase-3） |
|---|---|---|
| A | **393** | 410 |
| B | **4** | 4 |
| C | **3** | 3 |
| ? | **0** | 0 |
| 合计 | **400** | **417** |

站点级集合由 §11 脚本显式给出（不依赖启发式），与 §4 清单逐行一致。

### 6.1 A 类论证（按层归组）

**rootserver（173 全 A）**

| 组 | 点 | 载体 |
|---|---|---|
| `ObDDLOperator` | 41 | 成员 `schema_service_` |
| `ObDDLService` | 18 | 成员 `schema_service_` |
| DDL 任务派生类 | 69 | 任务对象持 `table_id`/`task_id`/`schema_version_`，per-task 对象 |
| `parallel_ddl`（`ObDDLHelper`/`ObTableHelper`/`ObCreateIndexHelper`） | 6 | helper 持 `schema_service_` |
| `pl_ddl`（`ObPLDDLOperator`） | 3 | 持 `schema_service_` / `get_multi_schema_service()` |
| `freeze`（checker/validator/freeze helper） | 11 | 进度由全局调度遍历 ns 时传入 guard；checker 方法已收 `ObSchemaGetterGuard&` |
| `fork_table` | 6 | 5 处在 `NamespaceForkKernelPrototype`（翻译层本身），1 处在 `ObForkTableTask` |
| 其余散点（`ObLocalManagementService` 4、`ObDDLScheduler` 2、`ObDDLTabletScheduler` 1、`ObAlterTableConstraintChecker` 1、`ObIndexBuilder` 1、`ObBootstrap` 1、`schema` 命名空间内 3、`ObForkDatabaseService` 1 等） | 19 | 管理入口/调度器，上下文含目标 database/table |

**share（49 全 A）**

| 组 | 点 | 载体 |
|---|---|---|
| `ob_multi_version_schema_service.cpp`（类内部，含 23 处非限定自调用 + 14 处限定调用） | 37 | per-ns 实例后天然正确（A1） |
| `ob_ddl_common.cpp`（`ObDDLUtil` + `ObColumnNameMap`） | 7 | 形参/成员 `schema_service`（A1） |
| `ob_schema_utils.cpp` | 2 | 形参/成员（A1） |
| `ob_sys_time_zone_util.cpp` | 1 | 接收者就是形参 `schema_service`（A1） |
| `ob_table_sql_service.cpp` | 1 | 成员 `multi_version_schema_service_`（A1） |
| `ob_latest_schema_guard.cpp` | 1 | 成员 `schema_service_`（A1） |

**observer（67 = A 63 + B 1 + C 3）**：`ObMP*` 家族 27（`ObMPBase` 持 `session`），`ObServer`/`ObServerSchemaUpdater`/`ObDBMSSchedJobExecutor` 5，`ObCS*` 5，虚表 5，`ObInnerSQLConnection` 2，vector_index 持服务/table_id 者 9，`ObAIServiceExecutor` 1，其余 9（`ObPluginVectorIndexUtils` 6 等，同属 per-ns 服务组或持 `table_id`/`index_id`），B/C 4（§7/§8）。

**sql（80 全 A）**：cmd 执行器 19，`ObSPIService`/`ObSPIResultSet` 7，session 族 10，`ObExpr*` 13，PX 8，resolver 6，DAS/CG 5，其它 12（plan ctx / 任务 / 参数）。

**storage（27 = A 20 + B 4 + C 3）**

| 组 | 点 | 载体 |
|---|---|---|
| `ob_tablet.cpp` 3、`ob_ls_tablet_service.cpp` 2、`ob_lob_location.cpp` 1、`ob_fts`(doc word iterator) 1、`ob_ddl_merge_task` 1、`ob_delete_lob_meta_row_task` 1、`ob_ddl_struct` 1、`ob_ddl_dag`/`ob_build_index_task` 1 | 11 | 作用域内有编码 `tablet_id`/`table_id` 或成员 schema service |
| `ob_complement_data_task.cpp`（`ObComplementDataParam`/`WriteTask`/`Dag`/`LocalScan`） | 6 | DDL DAG 任务，持 schema 版本 |
| `ob_dml_running_ctx.cpp` 2、`ob_table_ckm_items.cpp` 1、`ob_unique_checking`(merge task) 1、`ob_runtime_status_cache.cpp` 1、`ob_lob_location.cpp`（已计） | 5 | 形参/成员 schema service |
| B 4（`ob_tablet_slice_writer.cpp` 2、`ob_ddl_struct.cpp` 1、`ob_storage_schema_recorder.cpp` 1，§7） | 4 | 编码 id |
| C 3（`ob_tablet_stat_mgr.cpp` 2、`ob_tablet_autoincrement_service.cpp` 1，§8） | 3 | 无载体单例 |

**pl（4 全 A）**：`ObPL` 1、`ObPLCacheMgr` 1（per-ns plan cache）、`ObDBMSAiService` 2。

关键证据（3 处）：

- `ob_ddl_common_rs_impl.cpp:88` 上一行即 `ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();`，同作用域内有 `table_id`/`target_table_id`/`task->get_task_id()` → A（换实例）。
- `ob_truncate_info_service.cpp:67` 接收者写全局，但形参是 `const ObTableSchema &data_table_schema`（`data_table_id = data_table_schema.get_table_id()`）→ A。
- `obmp_base.cpp:532` 所在函数前文已有 `if (OB_ISNULL(session) || OB_ISNULL(gctx_.schema_service_))`（L526）→ session 在手 → A。

---

## 7. B（4 处）

| # | 位置 | 已有的编码 id | 判定 |
|---|---|---|---|
| 1 | `src/storage/ob_storage_schema_recorder.cpp:268` | 成员 `tablet_id_`、`table_id_` | B：`tablet_id_` 编码即含 ns。**注**：更严口径下亦可判 C（§8.3 备选），本报告按"id 线索在作用域内"判 B |
| 2 | `src/storage/ddl/ob_tablet_slice_writer.cpp:74` | 形参 `table_id`、`tablet_id` | B |
| 3 | `src/storage/ddl/ob_tablet_slice_writer.cpp:114` | 同上（`ObBatchDatumRows` 重载） | B |
| 4 | `src/storage/ddl/ob_ddl_struct.cpp:469`（`ObDDLWriteStat::assign`） | 形参 `table_id` | B |

> 前提：单进程化后编码 id 保留（`namespace_single_process_spec.md` 存储侧保留 `tablet id [ns:32][local:32]` 与例外表），故 B 是成立的技术路径。

---

## 8. C（3 处，逐点给出 A/B 失败原因与显式载体）

### C-1 `src/storage/ob_tablet_stat_mgr.cpp:879`（`ObTabletStatMgr::refresh_queuing_mode`）

```cpp
ObMultiVersionSchemaService *schema_service =
    ::oceanbase::share::server_service<...ObSchemaRuntimeService>()->get_schema_service();   // L871
...
} else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {               // L879
```

- **A 不成立**：`ObTabletStatMgr` 是 `server_module_init(ObTabletStatMgr*&)` 注册的**进程级单例**（`ob_tablet_stat_mgr.h:346-352`），成员无 schema service，`refresh_queuing_mode()` 无参，`TabletStatUpdater` 是无参 timer task；作用域内无 session/任务/plan。
- **B 不成立**：作用域内确有 `ObSEArray<ObTabletID, 64> tablet_ids`，但它是"当前所有 stream 的**集合**"，随后 `get_tablet_to_table_history(tablet_ids, …)` 按 table mode 聚合；一个 `schema_guard` 只能服务一个 ns，集合跨 ns 时无法用单 guard 表达。
- **显式载体**：`ObTabletStatMgr` 改 per-ns 实例（构造注入本 ns schema service），或给 `refresh_queuing_mode` 增加"目标 schema service / Runtime&"参数由创建者传入。**不允许** `thread_local 当前 ns`。

### C-2 `src/storage/ob_tablet_stat_mgr.cpp:942`

与 C-1 同函数、同实例（每 `MAX_SCHEMA_GUARD_REFRESH_CNT` 条重置 guard 的刷新分支），载体与修法同 C-1。

### C-3 `src/storage/ob_tablet_autoincrement_service.cpp:546`（`ObTabletAutoincrementService::collect_database_cache_invalidation`）

```cpp
ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
if (OB_FAIL(schema_service.get_runtime_schema_guard(schema_guard))) { ... }
```

- **A 不成立**：`static ObTabletAutoincrementService &get_instance()` 单例（`ob_tablet_autoincrement_service.h:133`），`init()` 无参，缓存失效由 timer 驱动，作用域内无 session/任务。
- **B 不成立**：形参是 `const ObDatabaseSchema &database_schema`，只能拿到 `database_id`——`database_of`/`local_of` 解的是 tablet/table id，**`database_id` 不是编码 id**；此处也没有 tablet id（正要枚举 database 下所有表）。
- **显式载体**：`namespace_single_process_plan.md` 已明确"autoincrement 现为 `get_instance` 单例，需改 per-ns"，即改为 Runtime 持有的 per-ns 实例并构造注入本 ns schema service；届时本点变 A1。

### 8.3 ? 与更严口径

- **? = 0**：400 个点全部在 §6.1 给出归因。
- 若采用"只有作用域内 id 是单点才可判 B"的更严口径，以下 4 点并入 C（**C 上限 = 3 + 4 = 7，仍 ≤ 10**）：

| 位置 | 更严口径的理由 |
|---|---|
| `src/storage/ob_runtime_status_cache.cpp:72` | `ObRuntimeStatusCache` 是无参构造 plain struct，作用域内只有 `get_server_runtime_info`，无 id |
| `src/sql/optimizer/stat/ob_opt_stat_monitor_manager.cpp:755` | 现为 `server_module_init` 单例，作用域内无 session/id |
| `src/sql/optimizer/stat/ob_opt_stat_monitor_manager.cpp:929` | 同上 |
| `src/storage/ob_storage_schema_recorder.cpp:268` | 见 §7 注 |

---

## 9. 判据结论

| 口径 | C | 判据（C ≤ 10） |
|---|---|---|
| 本报告口径（§5） | **3** | **通过（GO）** |
| 更严口径（§8.3 四点并入） | 7 | 通过（GO） |
| 若把"可补参数"也一律计 C | ≥ 100 | 不成立：补参数是显式传递，不违反 ADR-0003 |

**结论：C = 3（上限 7），≤ 10，Phase 0.1 卡点通过，按 `namespace_single_process_plan.md` 继续 Phase 1。**

补充判断（不软化结论）：

1. **没有发现"必须 thread_local"的点。** 3 处 C 的解法都是 per-ns 实例化 / 显式参数，与 ADR-0003 一致。
2. **真正的风险不在 C 的数量**：A 类中约 150+ 处接收者是进程全局入口（`GCTX`/`GSCHEMASERVICE`/`get_instance()`/`server_service<ObSchemaRuntimeService>()`）。单进程化必须做到：
   - `ObSchemaRuntimeService` 从"进程全局单例"变为"per-ns Runtime 注册"（否则 `ob_tablet.cpp`、`ob_ls_tablet_service.cpp`、`ob_lob_location.cpp`、`ob_tablet_stat_mgr.cpp` 等存储侧点会继续取到错误的 ns）；
   - `ObDDLTask` 创建时携带目标 ns 的服务实例（覆盖 69 处）；
   - 登录绑定落实 session 的 schema service（覆盖 observer/mysql 27 处）。
3. 建议把"全局入口接收者替换"拆成独立工作项，避免被"C=3 很小"误判为工作量小。

---

## 10. 与 v1 的差异（变更记录）

| 项 | v1（错误/不严谨） | v2（实测） |
|---|---|---|
| guard 标识符出现 | 357 | **467** |
| 调用表达式总数 | 未给出 | **417**（guard 356 / refreshed 43 / published 18） |
| 隐式版本点 | 327 | **390**（guard 329 / refreshed 43 / published 18）；S=400 内 378 |
| 审计 279 的地位 | 判为"偏低 48" | **279 正确**（"单行单 token 1 实参"口径），只是口径偏窄 |
| 需分类点 | 398 | **400**（全部调用表达式 417，另 17 处 Phase-3 单列） |
| 总数 343 | 判为"两种口径都不精确" | **确认不成立**：三种口径分别为 390 / 417 / 531 |
| 同名无关访问器 | 计入 published | 剔除（§2.3） |
| 行号基准 | 审计期间旧树 | 当前工作区（并发改动后） |
| A/B/C | 391/4/3（对 398） | **393/4/3（对 400）** |
| **C 与 go/no-go** | C=3，GO | **C=3，GO（结论不变）** |

---

## 11. 复现脚本（单一来源）

以下脚本产出 §0–§4 的全部数字（只读）。站点级 A/B/C 由显式集合给出（与 §5 判据一一对应），不依赖启发式。

```python
import re, subprocess, collections
ACC = ['get_runtime_schema_guard',
       'get_runtime_refreshed_schema_version',
       'get_published_schema_version']
files = subprocess.run(['rg','-l','|'.join(ACC),'src'],
                       capture_output=True, text=True).stdout.split()
occ, calls, decls = [], [], []
for f in files:
    txt = open(f, errors='replace').read()
    for name in ACC:
        for m in re.finditer(name, txt):
            lno = txt.count('\n', 0, m.start()) + 1
            occ.append((f, lno, name))
            j = m.end()
            while j < len(txt) and txt[j] in ' \t': j += 1
            if j >= len(txt) or txt[j] != '(':        # 更长同名函数 / 注释
                continue
            before = txt[max(0, m.start()-100):m.start()]
            if re.search(r'(virtual\s+)?(int|int64_t)\s+$', before):   # 声明/定义
                decls.append((f, lno, name)); continue
            depth = 0; top = []; cur = ''; k = j
            while k < len(txt):
                ch = txt[k]
                if ch == '(': depth += 1
                elif ch == ')':
                    depth -= 1
                    if depth == 0: break
                if depth >= 1:
                    if depth == 1 and ch == ',': top.append(cur.strip()); cur = ''
                    else: cur += ch
                k += 1
            top.append(cur.strip())
            top = [re.sub(r'^\(+', '', a).strip() for a in top]
            l1 = txt.count('\n', 0, k) + 1 if k < len(txt) else lno
            calls.append(dict(f=f, line=lno, name=name, nargs=len(top),
                              args=[a.replace('\n',' ')[:70] for a in top],
                              multi=(l1 != lno)))
def variant(c):
    a = c['args']
    if c['name'] == 'get_runtime_schema_guard':
        return 'implicit' if (c['nargs'] == 1 or a[1] == 'OB_INVALID_VERSION') else 'explicit'
    return 'implicit' if (c['nargs'] == 1 or a[1] == 'false') else 'explicit'
def layer(f): return 'ipp' if f.endswith('.ipp') else ('h' if f.endswith('.h') else 'cpp')
for c in calls:
    c['variant'] = variant(c); c['layer'] = layer(c['f'])
print('occ', len(occ), collections.Counter(x[2] for x in occ))
print('calls', len(calls), collections.Counter(c['name'] for c in calls))
print('decls', len(decls), decls)
print('implicit', len([c for c in calls if c['variant'] == 'implicit']))
print('layer', collections.Counter(c['layer'] for c in calls))
G = [c for c in calls if c['name'] == 'get_runtime_schema_guard']
simple = lambda c: c['nargs'] == 1 and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_:]*', c['args'][0]) is not None
print('audit-279', len([c for c in G if simple(c) and not c['multi']]))

# 站点级分类（显式集合，不启发式）
C_SITE = {('src/storage/ob_tablet_stat_mgr.cpp', 879),
          ('src/storage/ob_tablet_stat_mgr.cpp', 942),
          ('src/storage/ob_tablet_autoincrement_service.cpp', 546)}
B_SITE = {('src/storage/ddl/ob_tablet_slice_writer.cpp', 74),
          ('src/storage/ddl/ob_tablet_slice_writer.cpp', 114),
          ('src/storage/ddl/ob_ddl_struct.cpp', 469),
          ('src/storage/ob_storage_schema_recorder.cpp', 268)}
def klass(c):
    if (c['f'], c['line']) in C_SITE: return 'C'
    if (c['f'], c['line']) in B_SITE: return 'B'
    return 'A'
for c in calls: c['klass'] = klass(c)
nonipp = [c for c in calls if c['layer'] != 'ipp']
print('S total', len(calls), 'S-prime', len(nonipp))
print('A/B/C S-prime', collections.Counter(c['klass'] for c in nonipp))
```

## 12. 报告核对脚本

```python
# 清单行数必须等于 S
import re, json
inv = open('docs/research/namespace_implicit_version_audit.md').read()
sec = inv.split('## 4. 全量清单')[1].split('## 5. ')[0]
toks = re.findall(r'\b\d+[GRP][ie]', sec)
print('inventory rows/站点 =', len(toks))   # 必须 = 400 + 17 = 417
```
