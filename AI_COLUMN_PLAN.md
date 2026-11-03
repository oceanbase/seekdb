# 赛题三（异步 AI 列）继续实施计划 — 2026-10-30

分支: feature/competition-2026    部署: obd 集群 ai_col_t (/home/wxh/seekdb_deploy/seekdb1)
构建: build_release (RelWithDebInfo), `make -j8 observer` 后产物在 build_release/src/observer/seekdb，
需 cp 到 tools/deploy/bin/seekdb 和 /home/wxh/seekdb_deploy/seekdb1/bin/seekdb（repository 目录与其硬链接）。

## 官方测试（tools/deploy/mysql_test/test_suite/ai_column/）
- ai_column_ddl ✅ 通过
- ai_column_t1_dml / ai_column_worker / ai_column_retry_strict / ai_column_vector_index ❌
  全部卡在同一个根因（见下）。
运行方式: 服务器起在 10000 端口后手动执行
  deps/3rd/u01/obclient/bin/mysqltest --user root --port 10000 --host 127.0.0.1 --database test     --test-file mysql_test/test_suite/ai_column/t/<name>.test     --result-file mysql_test/test_suite/ai_column/r/mysql/<name>.result
（t1_dml/worker/retry_strict 需要先启动 mock:
  python3 mysql_test/test_suite/ai_column/include/ai_column_mock.py start 19121）
注意: obd deploy/start 会用 .obd/repository 里的旧二进制覆盖 home 下的 bin，替换二进制后必须
重新 cp 到 tools/deploy/bin/seekdb（与 repository 硬链接）再 start。

## 已完成修复（全部在工作区未提交）
1. ob_sql_session_info: 恢复与 HEAD 一致（删除旧版 int32_t AI API 残留，保留 HEAD 的枚举版本）。
2. ob_ai_column_worker.cpp: 3 处编译错误修复；LOG_WARN→LOG_INFO（该部署 WARN 不落盘，见下）。
3. ob_ai_column_sql_service.cpp（原死代码，已接线）:
   - create/alter/drop 三条 DDL 路径调用 sync_ai_column_meta/delete_ai_column_meta
     (ob_table_helper.cpp create_tables_、ob_ddl_service.cpp alter_table_column、ob_table_sql_service.cpp drop_table)
   - build_hidden_col_name 悬垂栈缓冲 → 调用方传 buf（原 bug 导致三个隐藏列名全是垃圾指针）
   - spec 来源改为 col.get_ai_spec_text()（原读 orig_default_value 恒为空）
   - 表名全限定 oceanbase.__all_ai_column（普通 proxy 无默认库）
   - delete_ai_column_meta 用 add_pk_column（原 add_column 拼出非法 DELETE，导致所有 DROP TABLE 报 1064）
4. ob_ai_column_dml.cpp:
   - check_write_protection: 第二个循环前补 hit=false（否则 INSERT 整行含 AI 列即误拒）
   - build_rowkey_predicate/append_datum_literal: 非空分支补 " = "
   - invalidate_row/create_tasks: 去掉与 escape_sql_string 叠加的多余引号
   - invalidate_row: SET 里追加 rowkey 自赋值 (`id`=`id`)，嵌套钩子才能重建谓词
   - remove_tasks: 空列表直接返回
   - 全文件 LOG_WARN/LOG_ERROR → LOG_INFO（调试期临时，问题解决后应改回）
5. 框架级: ObExecContext 增加 is_ai_invalidation_sql_ 标志 + ObSQLUtils::is_ai_invalidation_nested_sql
   （沿 parent 链检查），加入 is_nested_sql——AI 钩子的嵌套语句必须跳过 set_end_stmt 的
   nested_count_==0 检查，否则嵌套语句一律 -4016。
6. obd 插件（tools/deploy/.obd/plugins/seekdb/1.0.0，pristine 在 deps/3rd/usr/obd/plugins）:
   parameter.yaml 删除 __min_full_resource_pool_memory；start_pre.py not_cmd_opt 加 local_ip；
   generate_general_config.py 不再写该参数。原因: seekdb 二进制已删除这两个启动参数。
   另: activate_obd.sh 的 OBD_HOME 引用未定义 $DIR，调用 obd.sh 前需 export DIR=<repo根>。
   single.yaml home_path 已改到 /home/wxh/seekdb_deploy/seekdb1（/tmp 是 7.5G tmpfs 放不下）。

## 最后一个未解决问题（阻塞 4 个 DML 用例）
后台 worker 的 ObTimer 定时任务不执行: 任务行保持 state=1 (PENDING)，
runTimerTask/process_tasks 从未被调用（gdb 断点 0 命中，LOG_INFO 探针无输出）。
- timer_.start() 已补（参照 ObSystemPackageLoadService），仍未触发。
- 19:20 时段（旧二进制 build38/39）曾观察到 TimerWK0_AiColWorker 线程活动，说明调度本身可以工作，
  需对比当时与现在的差异。
- 排查建议: 在 ObAiColumnWorkerScheduler::start() 的 schedule() 返回后加 LOG_INFO 确认注册成功；
  检查 ObTimerService 的线程池是否被占用/暂停；对比 ObSchemaReleaseTimeTask（同文件 ob_service.cpp，
  工作正常的 ObTimer 用户: init 里 schedule，无 timer_.start()）。
- 注意: 本部署 seekdb.log 只写 INFO，WARN/ERROR 全部被吞（ob_log_level=disabled 且改不动），
  调试必须用 LOG_INFO（ob_ai_column_dml.cpp / ob_ai_column_worker.cpp 里现有探针即此用途，
  问题解决后应还原为 LOG_WARN）。

## worker 修好后预计剩余
- t1_dml/worker/retry_strict/vector_index 重跑；vector_index 此前还撞过服务器崩溃
  （create vector index 时 Lost connection，可能与内存或向量索引构建相关，需复现确认）。
- 全部通过后: 整理提交（建议拆分: 框架 nested-sql 标志 / DML hook 修复 / 元数据接线 / obd 插件适配）。

---

# 2026-11-02 进展（本轮）

## 结论：之前的“worker 定时任务不触发”是误判
ObTimer 正常，runTimerTask 每 0.5s 都会执行；之前看不到日志是因为
worker 线程执行过嵌套 SQL 后线程级日志级别被置为 disabled（LOG_INFO 全部被吞）。
调试用 gdb 断点，不要依赖该线程的日志。

## 本轮修复（已验证）
1. __all_ai_column_task 取任务行解析：int 列必须 get_int 而不是 get_uint
   （signed int 用 get_uint 返回 -4001 OB_OBJ_TYPE_ERROR），payload 用
   coalesce(payload,'') 避免 NULL 取值失败。→ worker 能真正取到任务。
2. 写保护 check_write_protection：UPDATE 用 dupd_ctdef_.updated_column_ids_
   判断“是否真的赋值了该列”；原实现用 new_row_ 是否为 NULL 判断，
   导致普通 UPDATE（未改 AI 列）被误拒（1235）。
3. remove_tasks 生成的 SQL 前缀重复 table_id=（DELETE ... WHERE table_id=table_id=...），
   多行 DELETE 只删掉最后一个谓词 → 修正前缀。
4. alter table ... retry ai column 执行器（ObAlterTableExecutor::execute_ai_column_retry）：
   - 原实现先读 session->get_inner_conn() 再 set，首次执行必然 4016；
   - 改为 create_connection_with_owned_session + start_transaction/commit
     （ALTER 语句自身不提交事务，之前 UPDATE 被回滚）；
   - 内部会话 is_inner=true，隐藏列可见、写保护放行；设置
     AI_COL_MODE_INVALIDATE 让 DML hook 建任务；
   - 每行请求号唯一：resolver 生成 rowkey 表达式，执行器拼
     concat('<rid>', ':', cast(rowkey as char))，避免
     (table_id, request_id, column_id) 主键冲突；
   - retry 与其它 alter 动作混用时返回 1235。
5. rewrite_status_to_state_column 的引号扫描 bug：消费闭引号后没有前进，
   导致 where 里每个字符串字面量被重复开引号（'FAILED' → 'FAILED''）。

## 当前测试状态（tools/deploy/mysql_test/test_suite/ai_column）
- ai_column_ddl          ok
- ai_column_t1_dml       ok
- ai_column_retry_strict ok
- ai_column_worker       FAIL：卡在 retry-once 断言；连续多次跑同一套用例的
                         环境里 worker 会卡住（任务 state=1 不再被处理）。
                         手动验证 mock 400→200 的重试链路本身是通的。
- ai_column_vector_index FAIL：create vector index 的 DDL 已返回 ret=0，
                         但客户端语句等待 2 分钟后报 -5066 返回；
                         embedding 生成（[0,0,0,0]/[10,..]/[20,..]）正常。

## 调试技巧
- 本部署 WARN/ERROR 完全不落盘，只有 INFO 会写；worker 线程执行过嵌套 SQL
  后连 INFO 也会被吞。日志不可靠。
- 需要栈时：obd 起的进程 ptrace 被 yama 拦截，需用 gdb --nodaemon 自己起服务。

## 2026-11-02 追加（收尾）
修复 is_ai_invalidation_nested_sql 之前把“外层语句自己”也判成 nested 的问题
（它把标记设在 op.get_exec_ctx() 上，随后 is_nested_sql(外层) 为 true，
导致事务控制跳过、语句结束不提交：insert 的行对别的会话不可见、worker 拿不到任务，
也解释了 t1_dml 偶发挂住/掉连接）。改为只沿 parent 链判断。

最终结果（同一份二进制，连续跑）：
- ai_column_ddl          ok
- ai_column_t1_dml       ok（连跑 4 次稳定）
- ai_column_worker       ok
- ai_column_retry_strict ok
- ai_column_vector_index FAIL：create vector index 返回 4016

vector_index 结论：与本功能无关。裸表 test.t_norm(id bigint primary key, v vector(4))
上 create vector index 同样 4016；__all_ddl_error_message 里 2026-10-30 就有同样失败的
create vec index 任务。失败点应是 ObVecIndexBuildTask::process 的
`if (!is_vec_index(index_type)) ret = OB_ERR_UNEXPECTED;`，即本地（seekdb）DDL 链路里
create_index_arg_.index_type_ 丢失，属于既有问题，需要单独查。

### vector_index 追加排查（放弃前记录）
- gdb 断点 ObVecIndexBuildTask::process 显示 create_index_arg_.index_type_=25
  (INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL，is_vec_index() 判定为 true，第一道检查可过)，
  而 task_status_=99(FAIL)，说明失败发生在更早的阶段，后续调用只是在 FAIL 分支重试。
- 裸表建索引同样失败，普通 btree 索引可以建成功，所以是 vec index DDL 链路自身的问题，
  与 AI 列无关；如需继续查，建议在 DDL task 首次 PREPARE 的 check_health()/prepare() 上打断点。

### vector_index 根因定位（2026-11-02 深挖）
链路：ObVecIndexBuildTask(PREPARE) → prepare_rowkey_vid_table →
ObDomainIndexBuilderUtil::prepare_aux_table → local_ddl_serial_call →
ObLocalManagementService::create_aux_index → ObDDLService::create_aux_index。
失败点：ob_ddl_service.cpp:4834 —— schema_guard.get_table_schema(data_table_id)
返回 NULL（data_schema == NULL → OB_ERR_UNEXPECTED）。
guard 用 get_runtime_schema_guard_with_version_in_inner_table() 取版本，
探针显示该版本是新鲜的（与 DDL 同秒），data_table_id 也正确（t_norm=559413），
但该版本的 schema mgr 里没有用户表 —— 即内表版本对应的 schema 快照不完整。
这与之前观察到的内层 SQL 查 __all_core_table(' __all_schema_status') 报
OB_TABLE_NOT_EXIST 是同一个系统性问题：内表机制在本地（seekdb）运行时的
schema 快照不完整。属引擎层既有问题，与 AI 列无关，建议单独立项。

### vector_index 修复尝试（已回退）
尝试在 create_aux_index 里对“内表版本 guard 查不到数据表”回退到最新 runtime guard：
最新 guard 同样查不到该表（用户会话可以查到），说明不是快照版本滞后，而是本地运行时
内层（inner session/DDL）上下文的 schema guard 本身缺用户表 —— 与最早观察到的
内层 SQL 查 __all_core_table 报 OB_TABLE_NOT_EXIST 是同一问题。该回退实验已还原。

最终状态（2026-11-02）：ai_column_ddl / ai_column_t1_dml / ai_column_worker /
ai_column_retry_strict 全部 ok；ai_column_vector_index 因上述引擎层问题未通过。
如继续排查，入口是：内层会话 schema guard 缺用户表/核心表 → 先修内层会话的
schema 可见性（可能与 seekdb 本地运行时的 schema 服务初始化方式有关）。

### vector_index 二次深挖（修正前一版结论）
前一版说“失败点是 create_aux_index 里 data_schema 为 NULL”——那是第一次尝试的偶发现象；
后续 gdb 复测：create_aux_index 里 data_schema 是能取到的，真正失败在**子任务**：

- 每次失败会在 __all_ddl_error_message 里留两行：父任务 `ddl_type:create vec index`
  和子任务 `ddl_type:create index`，两者都是 -4016。
- 子任务走 ObIndexBuildTask::process()，gdb 断点确认它掉进 `default:` 分支
  （ob_index_build_task.cpp:288），观测到的 task_status_ 为 3(=REDEFINITION) 和 99(=FAIL)；
  但 switch 里这两个 case 明明都存在 —— 说明该子任务记录的状态与 ObIndexBuildTask
  期望的状态语义不一致（很可能是：vec aux 表的数据补全子任务以 vec 索引的初始状态建记录，
  却按普通 create index 被调度成了 ObIndexBuildTask）。

下一步入口：查 create_aux_index_task_() 建子任务记录时写入的 task_status / ddl_type，
以及 ObDDLScheduler 按 ddl_type 选任务类型的分支（schedule_build_index_task 用的是
ObIndexBuildTask）。这仍属引擎层（向量索引 DDL）问题，与 AI 列功能无关。

最终状态（2026-11-02，清理调试代码后复测）：
ai_column_ddl / ai_column_t1_dml / ai_column_worker / ai_column_retry_strict 全部 ok；
ai_column_vector_index 未通过（原因见上）。

### vector_index 三次深挖：拿到子任务的真实错误码（关键进展）
之前看到的 -4016 是父任务继承的；直接查子任务的记录拿到真实错误码：

- `__all_ddl_error_message` 里子任务（`ddl_type:create index`）报 **-4614 = OB_TASK_EXPIRED**。
- 父任务卡在 status=30（WAIT_ROWKEY_VID_TABLE_COMPLEMENT），等 rowkey_vid 辅助表的子任务；
  等待逻辑在 ObVecIndexBuildTask::wait_aux_table_complement()，它靠查 __all_ddl_error_message
  里子任务的结果行判定完成/失败（CheckTaskStatusFn，ob_vec_index_build_task.cpp:828）。

OB_TASK_EXPIRED 的产生点（都在子任务 ObIndexBuildTask 里）：
- ob_index_build_task.cpp:1189：列校验和回调时 task_status_ != VALIDATE_CHECKSUM → expired
- ob_index_build_task.cpp:1223：sstable job 完成回调 execution_id < execution_id_ → expired

可疑关联：create_aux_index_task_() 对 vec rowkey_vid 辅助表会把
`param.ddl_need_retry_at_executor_` 置为 true（is_vec_rowkey_vid_type(index_type) 分支），
子任务因此会在 executor 层被重试；重试后旧 execution 的完成回调后到 → 被判定 expired → 子任务失败。

下一步（未做）：在 ob_index_build_task.cpp:1223 / 1189 打点确认是哪一个回调触发，
并评估“旧 execution_id 的回调应忽略还是报错”。属引擎层 DDL 任务重试协调问题，与 AI 列无关。

---

# 2026-11-03 重新定位结论 + vector_index 修复规划

> **本节结论已被下一节（2026-11-03 收尾）修正**：失败点不在 `ObPxSqcMeta`，
> 而在 `ObBasicSessionInfo::serialize` 的系统变量序列化上。下面保留当时的推理过程。

## 0. 结论先行（覆盖前面 -4614 的解释）

之前看到的 -4614 / -4016 都是**下游表现**。真正的根因是：

> **在 seekdb 本地 PX 路径上，只要 DDL 构建索引的内层 SQL 扫描的表含 LOB 列
> （vector 列在 seekdb 里按 LOB 存储），SQC 参数序列化就会失败（-4016），
> 整个 create index / create vector index 必然失败。**

这不依赖向量索引，也不依赖 AI 列：在裸表上建普通 btree 索引同样失败。

## 1. 最小复现（无需 AI 列、无需向量列）

```sql
create database if not exists vt16;
use vt16;
create table t16(id bigint primary key, a int, c text);   -- 关键：有 LOB 列
insert into vt16.t16 values (1,1,'hello'),(2,2,'world');  -- 关键：非空
create index idx_t16_a on vt16.t16(a);                    -- → ERROR 4016
```

对照组（都能成功）：
- 同样的表但 c 列换成 int；
- 同样的 LOB 表但**空表**建索引（不触发构建 SQL）；
- 在含 vector(4) 列的**非空**表上建 btree 索引 → 同样 -4016（vector 就是 LOB）。

## 2. 已确认的失败链（每层都有探针证据）

1. `ObIndexSSTableBuildTask::process` 生成构建 SQL：
   `INSERT /*+ ... */ INTO <隐藏索引表> (a,id) SELECT ... FROM <源表> AS OF SNAPSHOT ...`
   ——SQL 文本、schema 获取、执行计划生成都正常。
2. 语句执行选中 PX 计划：`PHY_PX_FIFO_COORD` → `ObPxScheduler::try_schedule_next_dfo`
   → `ObParallelDfoScheduler::schedule_dfo(dfo_id=1)` → `dispatch_sqc`。
3. `dispatch_sqc` 里：
   `args.serialize(ser_buf, ser_len, ser_pos)` 返回 **-4016**，其中
   - `serialize_common_parts_1`（phy_plan + exec_ctx）成功；
   - `LST_DO_CODE(OB_UNIS_ENCODE, sqc_)` 失败；
   - `args.sqc_.get_serialize_size() = 377`，缓冲余量充足（520/2884）。
4. -4016 冒泡：`wait_aux_table_complement` → 子任务 FAIL → 父任务 FAIL → 客户端 4016。

> 曾在中间层看到 -5019（table not exist）：那是**首次执行**时计划缓存未命中、
> 隐藏索引表 schema 尚未对编译可见导致的另一次失败；重试后计划缓存命中，
> 稳定复现的是上面的 -4016。两者都要留意，但当前阻塞点是 -4016。

## 3. 已排除的可能

| 假设 | 结论 |
|---|---|
| AI 列 DML 钩子影响 | 否。`collect_ai_columns` 的 4 条失败分支断点 0 命中 |
| 被索引表 schema 取不到 | 否。data/index schema 均能取到 |
| 构建 SQL 有问题 | 否。同样的 SQL 手工执行（含 hint / AS OF SNAPSHOT）到普通表成功 |
| PX hint 是触发条件 | 否。删掉 `use_px`/`enable_parallel_dml` hint 后计划**仍然走 PX**，仍失败 |
| 计划缓存/快照过期 | 否。延时 60s 后仍复现 |

## 4. 修复方案（按优先级）

### 方案 A（首选）：修 SQC 参数序列化
在 `ObPxSqcMeta` 的 31 个序列化成员上定位具体失败成员，然后补上空指针 / 尺寸判断。

步骤：
1. 临时把 `src/sql/engine/px/ob_dfo.cpp:30` 的
   `OB_SERIALIZE_MEMBER(ObPxSqcMeta, ...)` 展开成手写版
   （`OB_DEF_SERIALIZE / OB_DEF_DESERIALIZE / OB_DEF_SERIALIZE_SIZE`），
   在每个成员前/后写探针到 `/tmp/inner_fail.log`，一次编译定位成员。
   重点怀疑：`access_table_location_keys_`、`partition_pruning_table_locations_`、
   `rescan_batch_params_`、`temp_table_ctx_`（含 LOB aux 表相关项）。
2. 定位后判断原因属于哪类：
   - 数组指针非空但元素为 null → 走的是 `OB_UNIS_ENCODE_ARRAY_POINTER_IF` 的 -4016；
   - get_serialize_size 与 serialize 实际长度不一致 → `CHECK_SERIALIZE_SIZE` 报 -4016；
   - 成员未初始化（assign 未覆盖）→ 补 assign/serialize 一致性。
3. 修复完成后**必须还原手写序列化**，只保留最小修复。

### 方案 B（兜底）：让 DDL 构建不走 PX
已验证仅删 hint 不够（计划仍 PX）。可行路径：
- 在 `ObDDLTaskUtil::generate_build_replica_sql` 之外，强制该计划
  `set_use_px(false)` / 走 local 计划；或在 DDL session 上关闭并行执行
  （需要找到 seekdb 里让 DDL 计划落为 local plan 的开关）。
- 风险：影响所有索引构建的并行度；只在确认方案 A 成本过高时采用。

### 方案 C（放弃）
不修引擎，保留 4/5 用例（40/50 分），把 vector_index 记为已知问题。

## 5. 分步执行计划

| 步骤 | 内容 | 验证 |
|---|---|---|
| S1 | 手工探针定位 `ObPxSqcMeta` 失败成员 | `/tmp/inner_fail.log` 出现具体成员名 |
| S2 | 依据原因写最小修复 | 最小复现（vt16.t16）能建索引成功 |
| S3 | 回归：裸表 vector 索引 | `create vector index` 返回 0，`show index` 可见 |
| S4 | 回归：4 个已过用例 | ddl / t1_dml / worker / retry_strict 全 ok |
| S5 | 跑 `ai_column_vector_index` | 用例通过 |
| S6 | 清理探针、提交 | `grep -rn 'inner_fail\|PROBE' src` 为空 |

## 6. 环境与调试注意事项

- 部署：`build_release && make -j8 observer`，产物同时拷到
  `tools/deploy/bin/seekdb`、`tools/deploy/.obd/repository/seekdb/1.4.0.0/*/bin/seekdb`、
  `/home/wxh/seekdb_deploy/seekdb1/bin/seekdb`。
- 该部署 **WARN/ERROR 不落盘，DDL 线程 INFO 也会被吞** → 探针用 `fprintf` 写文件最可靠。
- 需要 gdb：`./bin/seekdb --nodaemon ...` 自己起；被杀后删 `run/seekdb.pid`。
- 停止手工实例用 `pkill -x seekdb`（`pkill -f 'seekdb --port'` 会误杀执行它的 shell）。

---

# 2026-11-03 收尾：真正根因（修正上一节结论）+ 修复 + 全部用例通过

## 1. 真正根因

上一节把失败点归到 `ObPxSqcMeta` 的序列化成员上，这是**错的**。
把 `ObPxSqcMeta` 的 31 个成员展开成手写序列化逐个加探针后，**31 个探针一个都没触发**。
沿调用链逐层加探针后，真实链路是：

```
ObPxInitSqcArgs::serialize
  → serialize_common_parts_1 (phy_plan / exec_ctx 都正常)
    → ObExecContext::serialize
      → *my_session_ → ObBasicSessionInfo::serialize   ← 这里 -4016
        → 系统变量循环：sys_vars_[store_idx] 为 NULL → OB_ERR_UNEXPECTED
```

失败的具体条目（探针实测）：

```
sys_var[0] class_id=15 store_idx=15  slot=OK   name=sql_mode
sys_var[1] class_id=100 store_idx=100 slot=OK   name=ob_query_timeout
sys_var[2] class_id=103 store_idx=103 slot=OK   name=ob_trx_timeout
sys_var[3] class_id=2   store_idx=2   slot=NULL name=autocommit   ← 这里
```

### 为什么会是 autocommit

1. seekdb 这个 fork 把系统变量改成了**按需惰性物化**：
   `ObBasicSessionInfo::ensure_sys_var_loaded()` 才真正创建 `sys_vars_[store_idx]` 对象。
   普通会话初始化**不会**走 `reset_sys_vars()`（那只在 COM_RESET_CONNECTION 时调用），
   所以绝大多数 `sys_vars_[]` 槽位一开始都是 NULL。
2. `ObBasicSessionInfo::set_autocommit()` 是唯一一条**只写缓存、不物化对象**的改值路径：
   ```cpp
   int ObBasicSessionInfo::set_autocommit(bool autocommit) {
     sys_vars_cache_.set_autocommit(autocommit);          // 只写 sys_vars_cache_.inc_data_
     return sys_var_inc_info_.add_sys_var_id(SYS_VAR_AUTOCOMMIT);  // 但登记进"已修改"列表
   }
   ```
   其它改值路径（`update_sys_variable`）都会先 `inner_get_sys_var()` 物化对象、
   再把新值写回对象，所以只有 autocommit 会出现"在脏列表里、但槽位是 NULL"。
3. `ObSQLUtils::LinkExecCtxGuard::link_current_context()` 对**嵌套 SQL** 会强制
   `session_.set_autocommit(false)` —— DDL 构建索引的内层 SQL 正好是嵌套 SQL，
   于是 autocommit 被登记成"已修改"，而 `sys_vars_[2]` 仍然是 NULL。
4. 走 PX 时 `ObParallelDfoScheduler::dispatch_sqc` 要把整个 session 序列化给 worker，
   序列化会遍历脏列表、逐个读 `sys_vars_[idx]->serialize()`，
   碰到 NULL 槽位就返回 `OB_ERR_UNEXPECTED(-4016)` → 冒泡到客户端。

不触发 PX 的场景（表里没有 LOB 列、或表是空的）不会序列化 session，所以看不出问题；
这也解释了为什么"只有含 LOB 列且非空的表建索引才失败"。

## 2. 修复（src/sql/session/ob_basic_session_info.cpp，两处）

1. **`set_autocommit()` 物化并同步对象**（根因修复）：
   先 `ensure_sys_var_loaded(SYS_VAR_AUTOCOMMIT)` 把对象建出来，
   再把请求的值同时写进 `sys_vars_[store_idx]->set_value()` 和 `sys_vars_cache_`，
   保证"缓存里的有效值"和"序列化要读的对象值"始终一致。
   物化失败时退回旧行为（只写缓存），不引入新的失败路径。
2. **序列化遇到 NULL 槽位不再整体失败**（防御）：
   `OB_DEF_SERIALIZE` 与 `OB_DEF_SERIALIZE_SIZE` 用**完全相同**的规则过滤
   （没有物化对象的变量直接跳过），两侧对称，不会出现长度不一致。
   跳过时打 LOG_WARN 便于诊断。

修复后 worker 侧从 `sys_vars_cache_.inc_data_` 与逐个反序列化的系统变量
拿到正确的 autocommit 值（deserialize 路径会调用
`process_session_variable(SYS_VAR_AUTOCOMMIT, ...)`，它同时写值和 inc 标志位）。

## 3. 验证结果（同一份二进制，连续跑）

最小复现（LOB 表建普通索引）：

```sql
create table vt16.t16(id bigint primary key, a int, c text);
insert into vt16.t16 values (1,1,'hello'),(2,2,'world');
create index idx_t16_a on vt16.t16(a);      -- 修复前 ERROR 4016，修复后成功
```

裸表向量索引（S3）：

```sql
create table vt8.t8(id bigint primary key, v vector(4));
insert into vt8.t8 values (1,'[1,2,3,4]'),(2,'[5,6,7,8]');
create vector index idx_t8_v on vt8.t8(v)
  with(distance=l2, type=hnsw, lib=vsag, m=10, ef_construction=12, ef_search=40);
show index from vt8.t8;                      -- Index_type = VECTOR
```

官方用例（tools/deploy/mysql_test/test_suite/ai_column/）：

| 用例 | 结果 |
|---|---|
| ai_column_ddl | ok |
| ai_column_t1_dml | ok |
| ai_column_worker | ok |
| ai_column_retry_strict | ok |
| **ai_column_vector_index** | **ok**（此前一直失败） |

5 个用例全部通过，`.result` 文件无 diff。

## 4. S1–S6 收尾状态

| 步骤 | 状态 |
|---|---|
| S1 定位失败成员 | 完成（不在 ObPxSqcMeta，在 ObBasicSessionInfo 的系统变量序列化） |
| S2 最小修复 | 完成（set_autocommit 物化 + 序列化跳过 NULL 槽位） |
| S3 裸表 vector 索引 | 通过 |
| S4 4 个已过用例回归 | 全过 |
| S5 ai_column_vector_index | 通过 |
| S6 清理探针、提交 | 探针全部还原（`ob_dfo.cpp` / `ob_exec_context.cpp` 回到 HEAD 形态） |

## 5. 经验

- 该部署 **WARN/ERROR 不落盘**，探针必须用 `fprintf(stderr, ...)` 才能看见；
  上一轮误以为"日志里什么都没有"是日志级别问题，实际是探针写到了 stdout/stderr。
- 展开 31 个成员做二分之前，先沿**调用链**加探针更省时间：
  这次第一轮探针就证明 `ObPxSqcMeta` 是无辜的，避免了在 31 个成员里空转。
