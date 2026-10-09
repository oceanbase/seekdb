# 07: Phase 2a：DDL/rootserver 调用点显式 ns 化

**What to build:** 152 处 DDL/rootserver 调用点改为显式 ns（DDL 任务上下文自带目标 ns）；CREATE INDEX、DROP DATABASE 等长事务 DDL 在 fork 出的 ns 里正常工作。

**Blocked by:** 06

**Status:** partial; interrupted heap-table recovery and full call-site audit remain TODO

- [ ] 152 处全部改造，无 ambient 取版本残留
- [ ] fork 出的 ns 内 DDL 全链路正常（含长事务 DDL 归口本 ns 调度）

## 2026-09-23 in-process DDL progress

- Removed the forked-namespace DDL gate and routed SQL DDL through the session's schema service and per-namespace root command service. The latter uses per-namespace SQL/DDL proxies and schema backend.
- Child namespace smoke test on port 13461: `CREATE DATABASE`, `CREATE TABLE`, `ALTER TABLE ADD COLUMN`, reads/writes, and `DROP DATABASE ticket07_childdb` succeeded. The parent namespace did not see child objects. A persistent arena for each namespace's `__all_core_table` schema fixed an internal query use-after-free.
- DDL transactions now keep their target namespace; inner SQL takes the session's schema and transaction services. Table locks use the connection's namespace runtime. PL cache flush uses the DDL proxy and resolves the database with the inner session's schema service; this fixed `DROP DATABASE` returning unknown database.
- `CREATE INDEX` on a nonempty child table still times out. Its task record is written to the child `__all_ddl_task_status` with status 0, while the process-wide DDL scheduler and index task still use global schema/SQL services. The task must carry explicit namespace context through scheduling, execution, and recovery.
- `CREATE INDEX` on an empty child table returned -4725 (tablet does not exist) in the empty-table optimization, around `create_tablets_` / `build_single_table_write_defensive`.
- Remaining ticket 07: convert the rootserver/DDL task call sites and background scheduler to explicit namespace context, complete both index paths, and verify recovery and four gates. Tickets 08-13 have not started.

## 2026-09-23 CREATE INDEX follow-up

- Added transient namespace context to index task scheduling and execution; child task records, async SQL, schema lookups, snapshot waits, and direct insert scheduling now use the child namespace's services.
- A nonempty child table index reached and completed PX direct insertion after binding the PX session's namespace runtime, using a child schema guard in the insert operator, and routing the direct insert service through the child's runtime.
- The task then failed checksum validation with `OB_ITER_END`: the source table scan wrote two checksum rows through the global SQL proxy, while the target writer wrote its rows to the child namespace. The scan is being changed to use the session's SQL proxy; rebuild and rerun are pending.
- Task recovery after process restart, the empty-table path, the remaining DDL/rootserver calls, and final gates remain open.
- After routing the source scan checksum through the PX session SQL proxy, `child_t14` produced matching source and index checksum rows and its task reached SUCCESS. The client still timed out because DDL wait helpers read the global error-message table; those helpers now use the requesting session's SQL proxy, pending rebuild and retest.
- Rebuilt and retested with `child_t15`: `CREATE INDEX idx_v_clean ON ticket07_base.child_t15(v)` returned success. `EXPLAIN SELECT id ... WHERE v=15` chose `child_t15(idx_v_clean)`, and `FORCE INDEX` returned the inserted row. Child `information_schema.statistics` remains unsupported by the prototype virtual-table route.
- Empty-table shortcut failed with `OB_TABLET_NOT_EXIST` because its write-defensive MDS call uses the process-wide tablet route. Child namespaces now use the ordinary index task instead. `CREATE INDEX` on empty `child_empty2` succeeded; after inserting `(7,70)`, `EXPLAIN` chose `idx_v` and the forced-index query returned `7`.
- Added child DDL task recovery after the namespace schema's first full refresh. On restart, the child task table initially held 21 old index records; recovery drained it to 0. A recovered `child_t3(idx_v)` served the expected row through a forced-index query. Recovery currently selects create-index task types; other child DDL task types still need namespace context.
- Unique index checksum validation also needed the task's schema service, SQL proxy, and local runtime. After those were passed to `ObDDLWaitColumnChecksumCtx`, `CREATE UNIQUE INDEX idx_u` on nonempty `child_unique2` succeeded. The index served `v=201`, and a second row with `v=201` was rejected with duplicate-key error 1062.
- Final build (`build20`) passed. All four handoff gates returned exit 0 and emitted PASS: bootstrap, SQL worker full, direct worker full, and direct worker TLS.
- This remains a checkpoint, not ticket completion: only create-index tasks are recovered in forked namespaces; other DDL task families and the rest of the 152-call audit remain.

## 2026-09-23 continued audit

- Child namespace multi-table `DROP DATABASE`, indexed-table `DROP TABLE`, and range partition `ADD/DROP PARTITION` succeeded without a pending DDL task.
- `ALTER TABLE ticket07_base.child_redef1 PARTITION BY HASH(id) PARTITIONS 3` timed out after 35 seconds. Its child task record `60000224` (DDL type 1005) remains at status 0. Table redefinition scheduling/execution and recovery still use global task services and need explicit child context.

## 2026-09-23 partition redefinition follow-up

- Added explicit child namespace context to table redefinition task scheduling and recovery. Health checks, snapshot acquisition, local build submission, checksum validation, dependent-object handling, final schema swap, and cleanup now use the task's SQL/schema/root services where required.
- PX repartition transmit and DAS tablet mapping now obtain schema from the child SQL session. Without the DAS guard, the source scan failed with `OB_TABLE_NOT_EXIST` before emitting its first row.
- Final schema swap used the process-wide local runtime to unbind child tablets and retried `OB_TABLET_NOT_EXIST`. It now uses the child DDL service's runtime.
- On port 13461, `ALTER TABLE ticket07_base.child_redef13 PARTITION BY HASH(id) PARTITIONS 3` returned success. `__all_table` shows `part_level=1, part_num=3`; the table retained both rows (`COUNT=2`, `SUM(v)=33`). DDL error record `72000183` reports success and no task remains in `__all_ddl_task_status` for this run.
- Temporary state logs were removed. Final clean build and four prototype gates are pending. The remaining 152-call audit and other DDL task families still need work.
- Final clean build (`/tmp/seekdb-ticket07-redef-build14.log`) succeeded. The initial direct full gate exposed a worker-mode null DDL proxy; the local build task now falls back to the worker's existing global DDL proxy when the in-process root service has no override. A targeted `--case forked` run and then direct full passed.
- On the final binary, bootstrap, SQL worker full, direct worker full, and direct worker TLS all emitted PASS (`/tmp/seekdb-ticket07-redef-gate-{bootstrap2,sql2,direct2,tls}.log`).
- Restarted port 13461 on the final binary: `child_redef13` still has `part_level=1, part_num=3`, with `COUNT=2, SUM(v)=33`. A fresh `child_redef14` completed the same redefinition in 1.3 seconds with the same metadata/data, no pending task, and a successful child DDL error record `73002026`. The parent namespace has zero `__all_table` rows named `child_redef14`.

## 2026-09-23 column redefinition follow-up

- Reproduced a child `DDL_COLUMN_REDEFINITION` task with `ALTER TABLE ... DROP COLUMN x, ADD COLUMN y INT DEFAULT 4` on a nonempty table. The initial task `73108741` stopped at REDEFINITION (status 3), and the client timed out. Recovery was not enabled for column task types, and the task did not carry child namespace context.
- Added task context to column task scheduling/recovery, and changed its schema/SQL/rootserver accesses to use that context. The storage DAG route still returned `OB_SCHEMA_EAGAIN` because its schema validation uses the global schema singleton. Child column redefinition now uses the already working SQL local build task; namespace 1 retains its existing DAG path. The original task had already failed during debugging (`OB_TABLE_NOT_EXIST`) and was not used as a success criterion.
- Fresh `child_col_redef2` and indexed `child_col_redef3` both completed the drop/add operation, retained two rows, and returned the new column default. `child_col_redef3` preserved `idx_v`: a forced-index query for `v=22` returned id 2, and the dependent create-index task succeeded.
- Recovery test: populated `child_col_redef4` with 8192 rows, stopped the test instance when column task `78028731` reached status 3, restarted, and observed a successful child DDL error record and no pending task. The recovered table had `COUNT=8192`, `SUM(y)=32768`, `SUM(v)=90112`.
- Removed all temporary diagnostics. Final build `/tmp/seekdb-ticket07-column-final-build.log` succeeded. Bootstrap, SQL worker full, direct worker full, and direct worker TLS gates all emitted PASS: `/tmp/seekdb-ticket07-column-gate-{bootstrap,sql,direct,tls}.log`.
- On the final binary, indexed `child_col_redef5` completed the same DDL, kept two rows (`SUM(y)=8`, `SUM(v)=33`), and its `idx_v` query returned id 2. Child error records `80002476` (column redefinition) and `80002682` (index rebuild) succeeded; there is no pending table task. The parent namespace has no `child_col_redef5` table.
- Remaining ticket 07: other DDL task families and rootserver call sites, including constraint/autoincrement task recovery and their global service references; audit the full 152-call checklist before marking complete. Tickets 08-13 remain open.

## 2026-09-23 table redefinition recovery follow-up

- Extended child namespace recovery to the table-redefinition family: MODIFY/ADD/DROP/ALTER PRIMARY KEY, MODIFY COLUMN, CONVERT TO CHARACTER, and MODIFY AUTO_INCREMENT WITH REDEFINITION, in addition to TABLE REDEFINITION and ALTER PARTITION BY. The base table task `init(record)` already copies the record's namespace context; no extra scheduler context assignment is needed.
- Fresh nonempty child `DROP PRIMARY KEY` succeeded (task type 1003, record `81002764`, data COUNT=2 and SUM(v)=33).
- A forced restart at status 3 for an 8192-row child DROP PRIMARY KEY task `81012707` led to `OB_TASK_EXPIRED` (-4614). This is the existing deliberate nonretryable heap-table behavior (`check_ddl_can_retry`), so crash recovery of this subtype is still incomplete; it terminated instead of remaining pending. Do not treat the expanded whitelist as proof that all listed task types resume safely.
- A forced restart at status 3 for an 8192-row child MODIFY COLUMN (`INT` to `VARCHAR(100)`) task `82055835` succeeded after recovery. Its child error record reports 0, no task remains, and the table has COUNT=8192 and SUM(CAST(v AS SIGNED))=90112.
- Final build `/tmp/seekdb-ticket07-table-family-final-build.log` succeeded. All four gates emitted PASS: `/tmp/seekdb-ticket07-table-family-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 constraint validation follow-up

- Reproduced child `ADD CONSTRAINT ... CHECK (v > 0)` with nonempty `child_check1`: task type 1 remained at WAIT_TRANS_END (status 2), client timed out. The constraint task lacked transient namespace context on scheduling, and its health check, wait-trans tablet lookup, SQL/schema/rootserver calls, and async validation task used global services.
- `ObDDLService::alter_table` now attaches its task context before scheduling collected DDL tasks. The constraint task uses that context through wait-trans, snapshot, validation, schema change, rollback, lock release, error reporting, and cleanup. Its async validation task carries the owning root service through deep copy and uses its schema/SQL proxies for child namespaces. Namespace 1 keeps its existing global async validation path.
- Fresh child `child_check4`, `child_check7`, and final-binary `child_check8` completed `ADD CHECK` quickly; invalid writes returned 3819. `child_check5`, which contained `v=-1`, rejected `ADD CHECK` with 3819, reported a failed child error record, and left no pending constraint task. The original debug tasks `84003561` and `85024065` failed with -5019 before all call sites were corrected; later tasks were used for success verification.
- Restart recovery: stopped 8192-row child `child_check6` while task `88035998` was at status 2, restarted, and observed a successful error record, no pending task, COUNT=8192 and SUM(v)=90112. An invalid write after recovery was rejected with 3819.
- Parent namespace regression check: `parent_check2` and final-binary `parent_check3` completed `ADD CHECK`; invalid parent write returned 3819. Final-binary child `child_check8` has COUNT=2, SUM(v)=33, successful task `91004893`, no pending constraint task, and no parent namespace table of the same name.
- Foreign-key `ALTER TABLE ... ADD CONSTRAINT ... FOREIGN KEY` returned 1210 (invalid argument) both in child and parent namespaces in this test setup, so the foreign-key validation path remains unverified.
- Final build `/tmp/seekdb-ticket07-constraint-final-build.log` succeeded. All four gates emitted PASS: `/tmp/seekdb-ticket07-constraint-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 DROP INDEX follow-up

- Reproduced child `DROP INDEX idx_v ON ticket07_base.child_col_redef5`: type 6 task `91051176` stayed at status 0 and the client waited. Routed `ObDropIndexTask` schema, SQL, and local root service accesses through its task context and enabled child recovery for `DDL_DROP_INDEX`. After restart, task `91051176` completed with error record 0.
- A fresh drop on `child_drop_index1` still stuck because `ObIndexBuilder` scheduled its task without the DDL service's namespace context. Attached that context at all four index-builder scheduling paths. Restart recovery completed task `92009776` with error record 0 and removed the index.
- With task context attached, a fresh create/drop pair on `child_drop_index1` completed in the background but the DROP INDEX client kept waiting: `ObDropIndexExecutor::wait_drop_index_finish` queried the global error table. It now uses the session's effective SQL proxy. On the final binary, a fresh create/drop pair returned success to the client, changed `EXPLAIN` from `idx_v` range scan to table scan, and preserved `COUNT=2, SUM(v)=33`. Child error records `94000095` and `94000675` report 0, and no type 6 task remains. Parent namespace has no `child_drop_index1`; a parent create/drop index regression also succeeded with two rows intact.
- Final build `/tmp/seekdb-ticket07-drop-index-build3.log` succeeded. All four gates emitted PASS: `/tmp/seekdb-ticket07-drop-index-gate-{bootstrap,sql,direct,tls}.log`.
- Ticket 07 remains open: the full DDL/rootserver call-site audit and other task families, including autoincrement and unverified foreign-key validation, remain.

## 2026-09-23 fulltext-index DDL follow-up

- Reproduced child `CREATE FULLTEXT INDEX` failure before the parent task when write-defensive tablet MDS used the process-wide route. The snapshot helper now takes the owning DDL service's local runtime. The child type 7 FTS task now carries namespace context through scheduling, recovery, schema/SQL lookup, auxiliary task creation, wait-trans, cleanup, and dependent task loading. Auxiliary tasks read their parent record from the same namespace's SQL proxy.
- The first child auxiliary index build then failed with `OB_TABLET_NOT_EXIST` during `DOC_ID()` evaluation. Its online path requested a sequence from the process-wide tablet autoincrement service using a child-local tablet ID. `ObSQLSessionInfo::effective_tablet_autoincrement_service()` now selects a namespace-bound proxy; that proxy routes the logical tablet ID to physical storage before calling the native service. The document ID itself still uses the logical tablet ID, as in the parent path.
- On the final binary, `CREATE FULLTEXT INDEX idx_body ON ticket07_base.child_fts9(body)` returned success. The three auxiliary index schemas are status 2, the base table retains both rows, FTS parent error record `107000906` and four auxiliary index records report 0, and no task remains for the table. A parent FTS creation baseline also succeeded.
- `MATCH(body) AGAINST('alpha')` succeeds on parent `parent_fts1` but returns `NOT_SUPPORTED` on child `child_fts9`. The child FTS query path remains unverified and is a separate SQL read-path issue; FTS DDL and schema publication completed.
- Build `/tmp/seekdb-ticket07-fts-build6.log` succeeded. All four handoff gates emitted PASS: `/tmp/seekdb-ticket07-fts-gate-{bootstrap,sql,direct,tls}.log`.
- FTS restart recovery and the remaining DDL/rootserver call-site audit still need validation.
- Restart recovery verified on `child_fts_recovery1` (8192 rows): stopped the instance when FTS task `107209062` reached status 26 with an auxiliary type 5 task active, restarted, and observed no remaining task for the table and a successful parent error record. All four index schemas reached status 2; the source table kept COUNT=8192 and SUM(id)=33558528.

## 2026-09-23 MODIFY AUTO_INCREMENT DDL follow-up

- Reproduced `ALTER TABLE child_ai1 MODIFY id INT AUTO_INCREMENT`: child task `108037693` (type 4) stayed at `MODIFY_AUTOINC` (status 9), and the client timed out. Routed the modify-autoinc task's schema/SQL/root accesses and async max-value scan through its namespace context, and enabled type 4 child recovery. The async task carries the owning root service through deep copy; table redefinition's use of the same async task now passes its owner too.
- Restart recovery completed the original task with error record 0. A fresh child `child_ai3` ALTER returned success; `__all_table` reports `autoinc_column_id=16`, `auto_increment=3`, and its two source rows remain (`COUNT=2`, `SUM(v)=33`). Parent `parent_ai3` completed the same ALTER.
- Implicit auto-increment INSERT in child namespace returns `Schema error` even for a table created with AUTO_INCREMENT from the start (`child_ai2`). The same parent INSERT succeeds. This is the pre-existing child DML autoincrement service route gap; successful ALTER does not establish usable implicit allocation yet.
- Final build `/tmp/seekdb-ticket07-autoinc-build2.log` succeeded. All four handoff gates emitted PASS: `/tmp/seekdb-ticket07-autoinc-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 DROP FULLTEXT INDEX follow-up

- Reproduced child `DROP INDEX idx_body ON child_fts9`: type 12 task `109214097` stayed at status 0 and the client timed out. `ObDropFTSIndexTask` now carries namespace task context into schema/error-table reads, child DROP submissions, message updates, and cleanup; child recovery includes FTS/multivalue/SPIV drop task types.
- After restart, task `109214097` completed with error record 0; all FTS auxiliary index schemas disappeared, and the base table kept two rows. Fresh `child_fts10` CREATE FULLTEXT INDEX then DROP INDEX both returned success to the client. Its parent type 12 record `110011021` and four type 6 child records report 0, no task remains, and source data still has COUNT=2, SUM(id)=3. Parent `parent_fts2` passed the same create/drop sequence.
- Build `/tmp/seekdb-ticket07-drop-fts-build1.log` succeeded. All four handoff gates emitted PASS: `/tmp/seekdb-ticket07-drop-fts-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 DDL retry task routing follow-up

- Audited `ObDDLRetryTask` for DROP DATABASE/TABLE, TRUNCATE, and partition maintenance. Its record initialization, schema-change status query, internal root commands, error reporting, and cleanup still used process-wide services. The task now takes namespace context; four `ObLocalManagementService` scheduling sites attach the owning DDL service's context, and child recovery admits the retry-task family. Waiting for dependent index tasks now accepts the owning SQL proxy.
- Build `/tmp/seekdb-ticket07-retry-build1.log` succeeded. On the new binary, child `ticket07_dropdb3` and parent `ticket07_parentdrop3` each completed CREATE DATABASE, CREATE TABLE, INSERT, and DROP DATABASE; neither database remains.
- The ordinary SQL DROP DATABASE path leaves `is_add_to_scheduler_` false, so this smoke test did not execute a type 501 retry task. Its task-path recovery remains dynamically unverified.
- All four handoff gates emitted PASS on the retry-task binary: `/tmp/seekdb-ticket07-retry-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 DROP LOB task routing follow-up

- `ObDropLobTask` now binds its recovered/scheduled record's namespace context and uses the owning SQL proxy when deleting its task record. Child task recovery now admits `DDL_DROP_LOB`. Its existing local root-service calls then resolve against the owning namespace.
- A child `LONGTEXT` table with two rows completed `ALTER TABLE ... DROP COLUMN body`, retaining COUNT=2 and SUM(v)=33. The error record was type 1010 (column redefinition), so this does not exercise type 11. An empty table with `ALGORITHM=INSTANT` also chose type 1010. The current `handle_drop_all_lob_columns_` explicitly changes dropping the last LOB column to INPLACE/offline, which appears to make the type 11 creation path unreachable from these SQL forms. Dynamic DROP LOB recovery remains unverified.
- Build `/tmp/seekdb-ticket07-drop-lob-build1.log` succeeded. Restarted the in-process test instance and rechecked both child tables. All four handoff gates emitted PASS: `/tmp/seekdb-ticket07-drop-lob-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 vector index and rebuild routing follow-up

- `ObRebuildIndexTask` now carries its record's namespace context through schema guards, vector job lookup, child CREATE/DROP INDEX commands, child error polling, lock release, task message update, and cleanup. Both `ObDDLService::rebuild_vec_index` and the ordinary rebuild-index submission path now start transactions on the owning SQL proxy and attach the context before scheduling. Child recovery admits type `DDL_REBUILD_INDEX`.
- `ObVecIndexBuildTask` and its auxiliary-table utility now use the owning schema/SQL/root services, including child task status reads, failure cleanup, and local auxiliary index creation. Child recovery admits type `DDL_CREATE_VEC_INDEX`.
- On a child heap table with a VECTOR(3) column, inserting three rows timed out before index creation. A subsequent `CREATE VECTOR INDEX` produced type 15 task `113010651` at status 0. After restart on the routed binary, recovery advanced it to FAIL (status 99, -4007), but the task record remained and no error record appeared. The prototype scan logged `NOT_SUPPORTED` for table ID 11111. This does not establish working vector DDL or rebuild; the child vector query/data path and failed-task cleanup still need diagnosis.
- The failed type 15 task had submitted type 14 DROP VECTOR INDEX task `114000290`; the latter still used global services and stayed at status 0. Routed that drop task through the child context and enabled its recovery. After restart, both task records disappeared: type 15 reported `-4007` and type 14 reported success. The failure cleanup is now verified, while successful vector index creation/rebuild remains blocked. Table ID 11111 is `__all_virtual_session_info`; its child prototype scan returned `NOT_SUPPORTED`, which may be involved in this failure and needs a focused trace.
- The type 15 failure before auxiliary tables also exposed a cleanup state bug: the no-index-to-drop branch did not mark its drop phase complete. It now records an absent drop child task and can advance to cleanup on the next pass.
- Final build `/tmp/seekdb-ticket07-vector-build3.log` succeeded. The four worker-mode handoff gates all emitted PASS on this binary: `/tmp/seekdb-ticket07-vector-final-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 process-wide session-info routing follow-up

- Confirmed that `SELECT COUNT(*) FROM oceanbase.__all_virtual_session_info` works in namespace 1 but returns `NOT_SUPPORTED` in the child. While a child session ran `SELECT SLEEP(8)`, the namespace-1 view showed that child session and SQL text. DDL task cancellation now queries this process-wide view through the explicit system SQL proxy. Child index-build restart skips the worker-local old-session probe and resubmits its local build, consistent with the table-redefinition task path.
- Build `/tmp/seekdb-ticket07-session-info-build1.log` succeeded. A fresh empty child vector table `child_vec_rebuild2` returned `NOT_SUPPORTED` from CREATE VECTOR INDEX; its type 15 parent and a type 5 auxiliary build reported `-4007`, while type 14 and type 6 cleanup tasks succeeded. The failure is now inside auxiliary vector index construction rather than an orphaned parent task. Successful vector DDL/rebuild remains open.
- The precise auxiliary failure is the prototype's intentional vector direct-insert guard in `src/storage/ddl/ob_ddl_struct.cpp:565`: `fill_ddl_table_schema()` returns `OB_NOT_SUPPORTED` for vector index complement because the namespace direct-insert protocol omits related data/parameter table schemas. The shared direct-insert log confirms failure at DAG initialization. Completing vector DDL requires carrying those schema facts through the existing namespace protocol; task-context routing alone cannot make it succeed.
- All four handoff gates emitted PASS after session-info routing: `/tmp/seekdb-ticket07-session-info-gate-{bootstrap,sql,direct,tls}.log`.

## 2026-09-23 FORK TABLE task routing follow-up

- `ObForkTableTask` now binds its record's namespace context; cleanup unlocks using the owning schema service and SQL proxy. The fork-table service attaches that context before scheduling, its snapshot helper takes the owning DDL service explicitly, and child recovery admits type `DDL_FORK_TABLE`.
- Build `/tmp/seekdb-ticket07-fork-table-build1.log` succeeded. On that binary, namespace-1 `FORK TABLE ticket07_parentfork.src TO ticket07_parentfork.dst` succeeded and the destination retained COUNT=2, SUM(v)=33. Child `FORK TABLE child_drop_lob1 TO child_fork_table3` still returned `OB_TABLET_NOT_EXIST` before a DDL task was created.
- The child failure is after snapshot acquisition: target tablet creation sends `CREATE_TABLET_NEW_MDS` with a fork source tablet ID still in child logical form (`ob_table_creator.cpp` logged source 283974). The namespace MDS translator in `namespace_worker_write_prototype.ipp` routes destination IDs but assumes fork source IDs are already physical, which is true for namespace fork but false for child-local FORK TABLE. Resolving child-local and inherited source tablets at this boundary needs care with snapshot caps; this storage route remains open.
- Four handoff gates emitted PASS on the fork-table routing binary: `/tmp/seekdb-ticket07-fork-table-gate-{bootstrap,sql,direct,tls}.log`.

## TODO carried forward while ticket 08 proceeds

- [ ] Finish the remaining ticket 07 audit and dynamic gaps. Child vector direct-insert schema facts, nonempty backfill/query, and FORK TABLE source-tablet resolution were completed on 2026-09-24 below.
- [ ] Recover interrupted child `DROP PRIMARY KEY` heap-table redefinition safely. A forced restart at task status 3 returns `OB_TASK_EXPIRED` (-4614); fresh execution succeeds. This does not block tickets 08–13, but ticket 07 recovery acceptance remains open.

## 2026-09-24 AUTO_INCREMENT follow-up

- Child-owned implicit INSERT previously returned 4029 because the process-wide `ObAutoincrementService` read namespace 1's `__all_auto_increment`; the child's newly created sequence row existed only in its own catalog. Each in-process namespace now has its own service bound to its SQL proxy. Expression evaluation, INSERT/REPLACE/UPSERT sequence sync, handle release, and DDL cache/sequence operations select that service explicitly.
- `namespace_autoincrement_prototype.py` reproduced the 4029 before the change, then passed child-owned implicit allocation, explicit value followed by implicit allocation, `ALTER TABLE ... AUTO_INCREMENT=100`, two child namespaces with matching local table IDs, restart, and adding AUTO_INCREMENT to an existing child table. Build and four in-process gates passed: `/tmp/seekdb-auto-inc-build2.log`, `/tmp/seekdb-auto-inc-full2.log`, `/tmp/seekdb-auto-inc-gate-{bootstrap,sql,direct,tls}.log`.
- The wider DDL task audit and vector/FORK TABLE issues remained open at this checkpoint; the FORK TABLE follow-up is recorded below.

## 2026-09-24 child FORK TABLE follow-up

- Minimal child-owned `FORK TABLE` reproduced 4725. The CREATE_TABLET MDS carried a raw child-local fork source; after converting that source, post-create `ObForkTableHelper` still opened the logical source tablet, and its `TABLET_FORK` MDS still carried logical destination tablet IDs. These three boundaries now resolve to physical source/destination IDs, with inherited source reads capped by the namespace fork snapshot. Forked table auto-increment metadata uses the child service.
- `namespace_fork_table_prototype.py` passes child-owned copy, child auto-increment source/copy with a following INSERT, inherited parent source after the parent mutates beyond the fork cap, and restart recovery. The final snapshot-range clamp build `/tmp/seekdb-fork-table-cap-build.log`, focused `/tmp/seekdb-fork-table-cap-focused.log`, and four gates `/tmp/seekdb-fork-table-cap-{bootstrap,sql,direct,tls}.log` passed. Temporary debug logs were removed.
- Remaining ticket 07 includes child vector direct-insert schema facts, the full 152-call audit, and the other dynamic gaps recorded above.

## 2026-09-24 vector direct-insert schema follow-up

- `namespace_vector_index_prototype.py` reproduced `NOT_SUPPORTED` for an empty child vector table before the change. The SQL direct-insert start now supplies the related data and parameter table schemas; the namespace route converts them to physical IDs and the storage DAG uses them to prepare vector index metadata. The empty-table CREATE VECTOR INDEX now succeeds and survives restart.
- Build `/tmp/seekdb-vector-index-build1.log`, focused `/tmp/seekdb-vector-index-empty-final.log`, and four in-process gates `/tmp/seekdb-vector-index-gate-{bootstrap,sql,direct,tls}.log` passed.
- The same test with `--nonempty` still fails: the auxiliary direct-insert starts and its source scan opens, but the PX transmit task returns `OB_TABLET_NOT_EXIST` (-4725); CREATE VECTOR INDEX waits until the client times out. A temporary log at the namespace scan iterator's `get_next_row` did not fire, so the failing tablet access is downstream of or separate from that fetch call. Repro: `/tmp/seekdb-vector-index-nonempty1.log` and `/tmp/seekdb-vector-fetch-debug-focused.log`. This is a separate child vector backfill/read issue. Successful empty-table DDL does not establish nonempty index build or vector query correctness.

## 2026-09-24 nonempty vector index follow-up

- The nonempty backfill failure was `VEC_VID()` calling the process-wide tablet autoincrement service with a child-local tablet ID. It now selects the session's namespace-aware service, as `DOC_ID()` already does. Backfill then reached the vector direct-insert writer: its existing in-process route omitted the spill factory required by the native vector writer. The route now supplies the shared factory.
- Nonempty `CREATE VECTOR INDEX` then succeeded, but approximate query first returned `NOT_SUPPORTED` because vector auxiliary scans deliberately have no output expression pointer; after supplying one they returned `OB_NOT_INIT` because the vector plugin casts the iterator to concrete `ObTableScanIterator`. Child vector auxiliary scans now route their IDs to physical storage and use the native scan service, preserving that concrete iterator contract.
- Final build `/tmp/seekdb-vector-native-scan-build.log`, empty and nonempty focused tests `/tmp/seekdb-vector-final-{empty,nonempty}.log`, and four in-process gates `/tmp/seekdb-vector-final-{bootstrap,sql,direct,tls}.log` passed. The nonempty test checks two rows, approximate nearest-neighbor result, and the same result after restart. Inherited vector-index queries and rebuild remain outside this focused test.

## 2026-09-24 child FTS query follow-up

- `namespace_fts_query_prototype.py` reproduced child `MATCH ... AGAINST` returning `NOT_SUPPORTED` while the same parent query succeeded. The child inverted-index document-count scan requested storage aggregate pushdown, which the frame-based scan rejects. Routing aggregate scans to the native in-process tablet service removed that rejection; ordinary FTS auxiliary scans also need the native iterator to avoid repeated reopen and query timeout.
- Final build `/tmp/seekdb-fts-native-index-build.log`, focused `/tmp/seekdb-fts-query-final-focused.log`, and four gates `/tmp/seekdb-fts-query-final-{bootstrap,sql,direct,tls}.log` passed. The focused test checks distinct terms against two rows, parent comparison, and child query after restart.

## 2026-09-24 heap hidden primary key follow-up

- mysqltest `join_basic` and `select_basic` exposed child heap INSERT retrying `OB_TABLET_NOT_EXIST` until timeout. `ObDMLService::get_heap_table_hidden_pk` used the global tablet autoincrement service with a child logical tablet ID; it now uses the session's namespace service, including the PDML caller. Two DATE rows in a child heap table inserted and counted correctly (`/tmp/seekdb-ticket10-heap-insert-after.log`); direct gate covers INSERT, UPDATE and query. Remaining `join_basic` failure is a separate secondary-index scan issue.

## 2026-09-24 mysqltest DDL 追加验证

- 子空间 `TRUNCATE TABLE` 原因是 DDL 内部 `__all_virtual_core_all_table` 扫描落到帧代理；进程内虚拟表扫描现使用会话 namespace 的 SQL proxy。完整 mysqltest `truncate_table` ns1/child 通过，直连门禁验证截断继承表不影响 ns1 且可重插入。
- `rename_table2` 原在重命名的 tablet 读写防护注册中使用 ns1 全局本地 runtime，现优先使用 DDL 服务的 task context runtime；`SHOW TABLES` 虚拟表同时改原生会话扫描。完整 mysqltest ns1/child 通过。
- 子空间 `CREATE VIEW` 需要独立 schema backend 的 DDL sequence ID，现按当前 leader epoch 初始化；view 无 tablet，fork schema 转换跳过 tablet 改写。完整 mysqltest `view` ns1/child 通过，bootstrap 门禁有新建视图后查询。详见工单 10 及 `/tmp/seekdb-ticket10-view-no-tablet-mysqltest.log`。
- 这些用例加强了 DDL 面覆盖，但 152 调用点的显式 namespace 审计和全部长事务任务类型恢复仍未逐项完成，工单 07 验收框暂不勾选。

## 2026-09-24 分区本地索引调度器

- 子空间生成列分区表非空 `CREATE INDEX` 原返回 1146：`ObDDLTabletScheduler` 沿用 ns1 schema service。现在由 index 任务传入所属 namespace schema service 和 SQL proxy，调度器的表/分区元数据、任务记录、checksum 查询跟随所属 namespace；进程级会话信息与磁盘统计继续用系统 SQL proxy。子空间适配扫描服务也允许复用尚未打开的空迭代器，解决随后的本地索引回表 1210。
- 最小复现和四门禁均通过，见工单 10 对应记录。完整 `generated_column` 第 192 行的空表 `ALTER TABLE ... ADD INDEX` 仍返回 4725，暂列后续 TODO；工单 07 的全调用点审计与中断 `DROP PRIMARY KEY` 恢复同样未结束。

## 2026-09-24 空表 ALTER ADD INDEX

- 子空间 `ALTER TABLE` 只新增索引时会命中空表捷径，`build_single_table_write_defensive` 用全局 tablet MDS 返回 4725。共同的空表索引检查入口现对子空间返回非捷径，交给已验证的普通 DDL task。完整 `generated_column` 在修正 `obsys` 测试连接所属 namespace 后，ns1/child 均通过；四套门禁也通过，详见工单 10。

## 2026-09-24 唯一索引失败上报

- 子空间非空唯一索引冲突此前返回通用 `Duplicated primary key`，原因是直插 writer 同步调用未绑定所属 namespace 上下文，存储层错误上报读取 ns1 目录。现通过显式直插上下文传递 schema service、SQL proxy 和逻辑 ID，mysqltest `idx_unique_many_idx_one_ins` 双侧通过；详情与日志见工单 10。剩余 DDL 全调用点审计和中断 DROP PRIMARY KEY 恢复 TODO 保留。

## 2026-09-24 子空间 MINOR FREEZE

- 子空间 `ALTER SYSTEM MINOR FREEZE` 原 4006 已修复：子空间本地管理服务初始化专属 `ObRootMinorFreeze` 并显式绑定对应 rootserver runtime；原 `bulk_insert` mysqltest ns1/child 均通过，详见工单 10。DDL 全调用点审计与中断 DROP PRIMARY KEY 恢复仍待处理。

## 2026-09-24 scheduler 静态审计线索

- `ob_ddl_scheduler.cpp` 的 `create_ddl_task(param, proxy, record)` 已有显式 SQL client，但部分任务构造仍读取 `GCTX`；其中 `fetch_new_task_id(*GCTX.sql_proxy_)` 实际参数当前被实现忽略，单纯替换该调用不能证明 namespace 正确。
- 实际需动态覆盖的路径包括 `ObRedefCallback::modify_info` 在队列未命中后向全局 ns1 查询任务存在性、`start_redef_table` 的全局 schema/SQL 获取，以及 IVF 创建/删除任务中的全局 schema、错误表和任务清理代理。现有子空间向量门禁使用 HNSW，不能代表 IVF 已通过；先做可触发的 IVF SQL/任务恢复复现，再据结果逐处显式化。

## 2026-09-24 子空间 IVF 索引与四件套回归

- 原始非空 IVFFLAT 最小复现：ns1 建索引成功，child 建索引卡住。恢复白名单缺少 IVF 六种任务类型；创建任务从记录恢复时未绑定所属 root service。修正后任务能推进，但 PX 表达式查询 centroid 表仍从全局 ns1 schema service 查 child 本地表 ID，返回 1146。
- IVF 表达式现在从 `ObEvalCtx` 会话显式传 namespace ID 到向量服务；服务使用所属 namespace schema service 和 SQL proxy 读取辅助表。子空间非空 IVFFLAT 建索引、近似查询及重启后查询通过。删除任务也从记录恢复所属 context，并用该 context 的 SQL proxy 读写任务消息、错误与清理；子空间 DROP INDEX 通过。失败建索引的全局错误/清理路径同步改为任务所属服务。
- 编译 `/tmp/seekdb-ticket07-ivf-drop-build3.log` exit 0；聚焦生命周期 `/tmp/seekdb-ticket07-ivf-lifecycle-fixed.log` PASS；四件套 `/tmp/seekdb-ticket07-ivf-suite-{bootstrap,sql,direct2,tls}.log` 均 exit 0 且 PASS。direct 套件新增非空 IVF 创建、查询、重启后查询和删除，以及 FTS 重启回归。
- 仍待 152 调用点逐项审计、子空间 DROP PRIMARY KEY 堆表中断恢复；IVF SQ8/PQ、失败建索引清理及异步 cache 加载未专项覆盖，不能由 IVFFLAT 成功推断它们已通过。

## 2026-09-24 IVF PQ 子空间批量扫描

- 非空 IVF_SQ8、IVF_PQ 在 ns1 和 child 的建索引、精确/近似查询、删除聚焦探测均通过，见 `/tmp/seekdb-ticket07-ivf-variants-final.log`。此前 child IVF_PQ 近似查询触发 SIGSEGV；core 栈定位到 `ObDASIvfPQScanIter::try_write_pq_centroid_cache` 读取损坏的 datum 指针。
- 根因是进程内扫描一次返回 32 行，而表达式帧 `max_batch_size_` 仅为 16，写入越界。`InProcessScanIterator::get_next_rows` 现按表达式帧容量限制返回行数。聚焦 child PQ `/tmp/seekdb-ticket07-pq-child-frame.log` PASS；direct 门禁新增 20 行 IVF_PQ 创建、近似查询、重启后查询和删除，四件套 `/tmp/seekdb-ticket07-pq-frame-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。
- TODO：152 个 DDL/rootserver 调用点逐项审计；中断的 child `DROP PRIMARY KEY` 堆表任务恢复；IVF 失败建索引清理及异步 cache 加载专项验证。堆表重定义目前被有意标记为不可重试，直接解除会有重复行风险，需设计安全恢复路径。

## 2026-09-24 堆表 DROP PRIMARY KEY 中断恢复复现收敛

- 新聚焦脚本 `.scratch/namespace-fork/drop_primary_recovery_probe.py` 在 child 非空表执行 `DROP PRIMARY KEY`，轮询 `__all_ddl_task_status` 到 status=3、execution_id=1 时停止进程，重启后检查任务、原表行数及主键。release 二进制未启用 debug sync，所以使用真实任务状态轮询；脚本无需完整 mysqltest/sysbench。
- 8192 行样本 `/tmp/seekdb-ticket07-drop-pk-repro2.log` 和 4096 行样本 `/tmp/seekdb-ticket07-drop-pk-repro4.log` 均在重启后变为 status=99，原表数据完整但主键仍在，验证了现存恢复失败；1024 行样本 `/tmp/seekdb-ticket07-drop-pk-repro3.log` 在同一 status=3 点重启后成功，说明此状态不能证明本地构建是否已经持久完成。
- 代码边界：`ObTableRedefinitionTask::check_ddl_can_retry` 将 heap-plan 标记为不可重试；`reap_old_local_build_task` 对 child 无条件要求启动新 inner SQL，而 `push_task_execution_id` 对不可重试的 execution_id=1 返回 `OB_TASK_EXPIRED`。直接撤销这一保护会对已部分写入的隐藏堆表重复插行；下一步需识别旧任务完整结果或安全清空隐藏目标后重建，并用 4096 行红样本及 1024 行已完成样本同时验收。

## 2026-09-24 既往失败用例门禁补充

- `direct` 门禁新增非中断 `DROP PRIMARY KEY`、CHECK、改分区、AUTO_INCREMENT、子空间自有/继承 `FORK TABLE`，均检查行数据，并在实例重启后重新检查。中断堆表恢复仍由上方 4096 行红样本单独跟踪，不能以正常完成的门禁替代。

## 2026-09-24 中断堆表恢复补充诊断

- 聚焦脚本现要求停机点同时满足 status=3、execution_id=1，并记录目标 checksum。4096 行红样本 `/tmp/seekdb-ticket07-drop-pk-recovery-probe-4096-checksum.log` 在停机前和重启后 `__all_ddl_checksum` 均无行；重启任务 status=99，源表 4096 行和原主键保持完整。1024 行在较早点 status=3、execution_id=-1 停机也能失败（`/tmp/seekdb-ticket07-drop-pk-recovery-probe-1024.log`），不能将这一状态视为构建已完成。
- 曾试验只在目标 tablet checksum 完整时认领旧构建，但以上红样本没有完成标记，仍走不可重试保护；该试验未形成有效修复，生产代码已撤回。TODO：在确认旧异步 SQL/存储 DAG 退出后，安全重建或清空隐藏堆表及其直插状态，再以新 execution_id 重试，并同时验证部分构建和完成构建两个崩溃窗口。此项不阻塞后续工单。

## 2026-09-24 IVF 后台缓存加载

- 子空间非空 IVFFLAT 的后台加载原先只枚举 ns1 schema，建索引后不执行近似查询时缓存虚拟表保持空。调度器现在检查已激活子空间的 IVF 索引，加载器按所属 namespace 枚举 schema 和物理 tablet，写入时使用所属 schema service，辅助 SQL 使用所属 SQL proxy。聚焦脚本 `.scratch/namespace-fork/ivf_async_cache_probe.py` 在首次近似查询之前观察到子空间物理 tablet 的 `cache_type=0;count=2`，随后查询成功；日志 `/tmp/seekdb-ticket07-ivf-async-cache-flat-final.log` PASS。
- IVF_PQ 聚焦 `/tmp/seekdb-ticket07-ivf-async-cache-pq-final.log` 也在查询前看到普通 centroid `count=2`。PQ centroid cache 节点出现但 `count=0`，首次近似查询后仅 ns1 对应缓存填入 40 个 centroid；因此不能宣称子空间 PQ centroid 的后台缓存已预热。此项保留 TODO，须区分小样本不足 `capacity=512` 的行为与加载路由缺失。
- direct 四件套新增子空间 IVFFLAT 在首次近似查询前的后台加载断言。IVF 失败建索引清理仍缺可触发异步任务失败的聚焦样本；继承父空间的 IVF index 后台加载也未专项验证。以上与 152 调用点全审计、中断堆表 DROP PRIMARY KEY 恢复一起保留 TODO，均不阻塞已开展的后续工单。
- 本次二进制编译 `/tmp/seekdb-ticket07-ivf-async-cache-build2.log` exit 0；新增断言后的四件套 `/tmp/seekdb-ticket07-async-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且输出 PASS。未运行完整 mysqltest 或 sysbench。
- 进一步复查发现 IVF 查询侧仍以未编码的本地 tablet ID 访问进程级 cache；`/tmp/seekdb-ticket07-ivf-cache-collision-red.log` 的两个子空间查询分别产生 `200010`、`200017` 的裸 ID 缓存条目。跨子空间若共享本地 ID（例如从同一索引 fork）会有碰撞风险。查询侧现用会话 runtime 将 centroid tablet ID 编为含 namespace 的 cache key，与后台任务一致。direct 门禁新增 PQ 查询后所属子空间物理 cache key 的 `cache_type=1;count=40` 断言；离线编译 `/tmp/seekdb-ticket07-ivf-cache-query-key-build.log` 及四件套 `/tmp/seekdb-ticket07-cache-key-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。PQ 后台首次查询前的预热仍为上方 TODO。

## 2026-09-24 IVF_PQ 后台预热完成条件

- 旧二进制聚焦 `/tmp/seekdb-ticket07-pq-cache-red.log` 在首次近似查询前稳定看到 PQ centroid cache `capacity=512;count=0`，而普通 centroid `count=2`。后台 SQL 实际读到 40 个 PQ centroid，但 `scan_and_write_ivf_cent_cache` 只有 `count==capacity` 才标记完成，否则清空；前台查询已有“扫描完成且 count>0”判据。
- 后台扫描采用同一完成条件。新二进制 `/tmp/seekdb-ticket07-pq-cache-green.log` 在首次近似查询前看到所属子空间物理 tablet 的 PQ centroid `count=40`，后续查询 PASS。direct 四件套新增该预热断言，离线编译 `/tmp/seekdb-ticket07-pq-cache-build.log`、四件套 `/tmp/seekdb-ticket07-pq-cache-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。PQ 后台预热 TODO 已关闭；失败建索引清理与继承 IVF 的专项验证仍未完成。
- 失败建索引清理的追加探测：4 维向量用 IVF_PQ `m=3` 被解析器直接拒绝，未创建异步任务；`nlist=64`、仅 2 条训练数据的建索引反而成功且五条 DDL error 记录全为 0（`/tmp/seekdb-ticket07-ivf-failed-create-nlist64.log`）。尚无可触发 IVF 异步构建失败的样本，此项继续保留 TODO，不阻塞工单 11 的结构收尾。

## 2026-09-24 DDL 任务全局服务审计增量

- `ObDDLTask::is_local_build_need_retry` 在 `OB_TABLE_NOT_EXIST` 分支原取 ns1 的 runtime schema guard；现从任务持有的 schema service 取得。`ObDDLTaskUtil::check_and_cancel_single_replica_dag` 原从 ns1 schema 枚举源/目标 tablet，并用进程级 rootserver runtime 取消 DAG；现两者均选任务 context 的所属服务，ns1 维持原 fallback。
- 离线编译 `/tmp/seekdb-ticket07-ddl-task-audit-build.log` exit 0；四件套 `/tmp/seekdb-ticket07-ddl-task-audit-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。门禁没有注入 `OB_TABLE_NOT_EXIST` 重试或取消中的 DAG，因此只能证明现有 DDL 回归未受损，不能把 152 调用点审计或这两个异常路径的专项验证标为完成。
- 下一处未改：`ObSyncTabletAutoincSeqCtx::init` 用 ns1 schema service 取重定义源/目标 tablet，随后 `read_migration_sequences`/`write_migration_sequences` 将所得 ID 直接交原生存储服务。子空间自有 tablet 需编码，继承源 tablet 还需确定真实物理来源与 fork cap；只替换 schema service 会使后半段仍读错。该路径仅在带 FTS 的隐藏主键表重定义时触发，须先做源/目标路由和专项用例，保留 TODO。

## 2026-09-24 中断 DROP PRIMARY KEY 恢复再确认

- 当前 HEAD 的 4096 行聚焦复现 `/tmp/seekdb-ticket07-drop-pk-current-red.log` 仍红：停机前任务 status=3、execution_id=1，目标隐藏表已创建，目标 checksum 空；重启后任务 status=99、execution_id=1，源表 4096 行与原主键完整保留，目标 checksum 仍空。测试脚本保留在 `.scratch/namespace-fork/drop_primary_recovery_probe.py`。
- 确认代码路径：`ObDDLRedefinitionTask::reap_old_local_build_task` 对 child 无条件要求重新执行内层 SQL；`ObTableRedefinitionTask::send_build_replica_request_by_sql` 调用 `push_task_execution_id`；不可重试堆表的 execution_id=1 在 `calc_next_execution_id` 返回 `OB_TASK_EXPIRED`。旧构建未报告 checksum 时没有可认领的完成标记；直接放开 retry 仍存在隐藏堆表部分写入后重复插行风险。此问题继续按非阻塞 TODO 跟踪，完成安全清理或续做方案之前不加入要求 PASS 的四件套。

## 2026-09-24 已完成构建的回调丢失窗口

- `reap_old_local_build_task` 现在对 child 的旧执行号先检查目标 tablet 的持久 checksum；若所有目标已完成，则认领旧结果并推进，不再无条件重跑。checksum 未齐仍走原路径，堆表不可重试保护保持不变。
- 隔离构建中临时在目标 checksum 全部报告后、异步完成回调前暂停 5 秒：4096 行任务保持 status=3、execution_id=1，目标 checksum 三行完整；此时停机重启后任务成功消失、表无主键、行数和 SUM 正确，仍使用 execution_id=1，无重复构建。聚焦日志 `/tmp/seekdb-ticket07-heap-complete-claim-window.log`。临时暂停代码已删除。
- 另外仅供调查的放开重试试验在 4096、16384、65536 行早期停机样本中表面成功；它不能证明已持久化部分隐藏堆表时不会重复行，试验代码已撤回。checksum 未齐的红样本仍是 TODO，不能把这次“已完成结果认领”算作整个中断恢复完成。
- 正式离线编译 `/tmp/seekdb-ticket07-heap-complete-claim-final-build.log` 成功；四门禁 `/tmp/seekdb-ticket07-claim-final-{bootstrap,sql,direct,tls}.log` 全部 exit 0 且 PASS。未运行完整 mysqltest 或 sysbench。
- 正式二进制的 checksum 未齐窗口 `/tmp/seekdb-ticket07-heap-complete-claim-incomplete-window.log` 仍稳定得到 status=99、原主键保留、源表 4096 行完整，证明本次没有误放开不安全的堆表重试；仍需真正的目标清理/续做方案。

## 2026-09-24 FTS 隐藏主键重定义的自增序列路由

- 聚焦 `.scratch/namespace-fork/fts_heap_redefinition_probe.py` 将子空间自有、继承的带 FTS 堆表从 `v INT` 改为 `v VARCHAR(20)`。旧二进制自有表任务 type 1001 在五秒内进入 status=99、ret=-5019，客户端最终超时；日志 `/tmp/seekdb-ticket07-fts-heap-redef-error.log`。`INT→BIGINT` 先前成功，但没有可靠触发这条重定义路径。
- 诊断埋点确认 `sync_tablet_autoinc_seq` 被调用，且该上下文原用全局 ns1 schema service 查子空间表 ID。现在调用者显式传所属 schema service 与 namespace ID；上下文获取源/目标逻辑 tablet 后，子空间源 tablet 编码并沿血缘解析实际物理来源，目标 tablet 编码为本空间物理 ID，原生序列读写只接收物理 ID。临时埋点已移除。
- 修复后聚焦 `/tmp/seekdb-ticket07-fts-autoinc-inherited-restart.log` PASS：自有和继承 FTS 表均完成类型重定义，继承表不看到 fork 后父空间新增行；两表继续 INSERT、FTS MATCH 和重启后读取均正确。direct 四件套新增旧失败的自有 FTS 堆表重定义、续写和重启断言。
- 离线编译 `/tmp/seekdb-ticket07-fts-autoinc-route-build.log` exit 0；四门禁 `/tmp/seekdb-ticket07-fts-autoinc-{bootstrap,sql,tls}.log` 和 `/tmp/seekdb-ticket07-fts-autoinc-direct-first.log` 均 exit 0 且 PASS。完整 mysqltest/sysbench 未运行。

## 2026-09-24 继承 IVF 索引的后台预热

- 红样本 `/tmp/seekdb-ticket07-ivf-inherited-active-flat-long.log`：ns1 创建非空 IVFFLAT 后 fork 子空间、激活子空间，首次近似查询前连续 50 秒无子空间缓存；查询本身可按 fork 快照返回正确结果。加载器此前只枚举子空间 runtime 本地表，且只检查子空间本地物理 tablet，因此漏掉继承索引。
- 加载器现按子空间可见数据库枚举本地和继承的表，解析继承 tablet 的实际物理来源做存在性检查；加载任务与 cache key 仍用子空间编码 ID。缓存失效检查也沿血缘查物理 tablet，避免将继承缓存误删。
- 修复后聚焦 `.scratch/namespace-fork/ivf_inherited_probe.py`：IVFFLAT `/tmp/seekdb-ticket07-ivf-inherited-flat-green.log` 在首次查询前出现子空间 `cache_type=0;count=2`；IVF_PQ `/tmp/seekdb-ticket07-ivf-inherited-pq-green2.log` 同时出现子空间普通 centroid `count=2` 和 PQ centroid `count=40`。父空间 fork 后新增更近向量，子空间查询仍只返回 fork 快照内行，PQ 近似结果允许返回快照中的任一候选。
- direct 四件套新增旧失败的继承 IVFFLAT 预热、fork 快照隔离与重启查询。离线编译 `/tmp/seekdb-ticket07-ivf-inherited-build.log` exit 0；`/tmp/seekdb-ticket07-ivf-inherited-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。完整 mysqltest/sysbench 未运行。
- TODO（非阻塞，后续架构治理）：`ObPluginVectorIndexLoadScheduler` 把 LS 的适配器维护/内存数据同步/HNSW/IVF 与跨 ns schema 枚举混在同一定时器。将每个 ns 的索引发现、schema version 和任务规划下沉到该 ns 的 runtime；LS 层保留角色、日志、执行和统一内存预算，删除 root 先查、再遍历 ns 求 `has_ivf_index` 的重复流程。当前 IVF 缓存按 ns 编码的索引 tablet ID 分项保存，共享管理器不等于跨 ns 复用缓存数据；若需复用继承索引的只读内容，须先证明物理来源、fork 快照上限、schema/参数版本和失效条件一致。
- 07 仍有 152 调用点全审计、checksum 未齐的中断堆表 `DROP PRIMARY KEY` 恢复，以及 IVF 异步建索引失败后的清理触发样本；这些按非阻塞 TODO 跟踪。

## 2026-09-24 IVF 异步建索引失败清理验证

- 在 `ObVecIVFIndexBuildTask::prepare_pq_centroid_table` 临时注入不可重试错误，故障点位于普通 centroid 建成后、PQ centroid 创建前。仅用于隔离实验，注入代码已移除；正式源码与 `c733a12c2` 一致。
- `.scratch/namespace-fork/ivf_failed_create_probe.py` 的故障构建 `/tmp/seekdb-ticket07-ivf-failed-create-fault-build.log`、运行 `/tmp/seekdb-ticket07-ivf-failed-create-fault-restart.log`：客户端收到 4016，父任务与子任务均退出；`vec` 只剩原表和 LOB 辅表，`SHOW INDEX` 只剩 PRIMARY。重启后仍无残留任务或向量索引，同表随后成功创建 IVFFLAT。故障构建覆盖的是实际 `FAIL → clean_on_failed → DROP` 路径，而非解析器提前拒绝。
- 移除注入后离线重编译 `/tmp/seekdb-ticket07-ivf-failed-create-final-build.log` exit 0；正式 direct 门禁 `/tmp/seekdb-ticket07-ivf-failed-create-final-direct.log` exit 0 且 PASS，包含正常 IVF_PQ 创建。此前的 IVF 异步失败清理 TODO 至此有专项验证；其余 07 遗留仍为 152 调用点全审计和 checksum 未齐的中断堆表恢复。

## 2026-09-24 CHECK/FK 重定义子任务的 namespace 上下文

- 聚焦子空间有 CHECK 与外键的表做 `payload INT → VARCHAR(20)`：旧路径父任务 type 1001 很快 status=99、ret=-4015；仅将子任务记录写入所属 SQL proxy 后，父任务停在 status=5，CHECK 子任务 type 1 停在 status=2，等待超 120 秒。诊断确认即时调度的子任务 record 未携带父任务 context，仍在全局 runtime 等待。
- `add_constraint_ddl_task` 和 `add_fk_ddl_task` 现在使用父任务的管理服务读隐藏 schema、所属 SQL proxy 写子任务记录，并在调度前传递 context。重启恢复仍由 `recover_task(context)` 绑定所属服务；临时等待诊断埋点已删除。
- 正式二进制聚焦 `/tmp/seekdb-ticket07-redef-both-errors.log` PASS：CHECK/FK 共存表完成重定义，数据正确；非法 CHECK/FK 写入在重启前后分别返回 3819/1452。此前只带 CHECK 的旧红样本在上下文修正后也通过。direct 四件套新增这一历史失败路径和精确错误码断言。
- 离线编译 `/tmp/seekdb-ticket07-redef-dependents-final-build.log` exit 0；四门禁 `/tmp/seekdb-ticket07-redef-dependents-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS；精确断言后的 direct 复跑 `/tmp/seekdb-ticket07-redef-dependents-gate-direct-final.log` 也 exit 0 且 PASS。工单 07 仍有全调用点审计和 checksum 未齐的中断堆表恢复，不能据此标记全部完成。

## 2026-09-24 子空间 EXCHANGE PARTITION 既有缺口

- 最小聚焦：child 自建 RANGE 分区表和普通表，各插入行后执行 `ALTER TABLE phase10.ex_part EXCHANGE PARTITION p0 WITH TABLE phase10.ex_plain WITHOUT VALIDATION` 返回 4725；相同语句在 ns1 成功。迁移例外表 delta 前的 `cdac1ab77` 二进制同样失败，见 `/tmp/seekdb-ticket11-exception-exchange-{ns1,baseline}.log`，因此不是后续 delta planner 改造引入。
- 根因在 `ObPartitionExchange::build_single_table_rw_defensive_`：它把子空间逻辑 tablet ID 交给进程级 `rootserver_local_runtime()`，在 ns1 存储视图里找不到 tablet。改为优先使用 DDL task context 的本地 runtime 后，child 交换及重启后行数据校验通过；direct 门禁加入同一回归。正式提交 `f725338a5` 已推送，离线编译 `/tmp/seekdb-exchange-runtime-final.log` 对应二进制及四门禁 `/tmp/seekdb-exchange-gate-{bootstrap,sql,direct,tls}.log` 均 PASS。feature 虽非 seekdb 重点，但此处是通用的 namespace runtime 选择缺陷，已修复而非掩盖。

## 2026-09-24 中断堆表 DROP PRIMARY KEY 恢复

- 4096 行旧红样本 checksum 为空，原任务在重启后因不可重试堆表执行号保护而 status=99。`reap_old_local_build_task` 的完整 checksum 认领已在前面实现；对不完整结果，现仅在恢复的 child 堆表任务上允许新 execution_id。正常执行中 `is_ddl_retryable_` 保持 false，旧异步 SQL 随进程结束，重建结果仍需原有 checksum 校验才能换表。
- 聚焦 4096 行 `/tmp/seekdb-drop-pk-retry-experiment2-4096.log`、262144 行停机前等待 50 ms `/tmp/seekdb-drop-pk-retry-experiment-262144-hold005.log`、1048576 行等待 150 ms `/tmp/seekdb-drop-pk-retry-experiment-1048576-hold015.log` 均在旧 checksum 为空的窗口恢复成功；最终 execution_id=2、原行数和 SUM 正确且主键删除。16384/65536 行旧 checksum 已完成的窗口保持 execution_id=1 认领旧结果。
- direct 门禁加入 262144 行真实中断样本，要求停机前旧 checksum 为空、重启后 execution_id=2、行数/SUM/MIN/MAX 和无主键均正确。离线构建 `/tmp/seekdb-drop-pk-recovery-final-build.log` exit 0；四件套 `/tmp/seekdb-drop-pk-recovery-gate-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。完整 mysqltest/sysbench 未运行。

## 2026-09-24 DDL/rootserver 审计启动：保留全局索引的分区截断

- 当前源码 rootserver 的 `get_runtime_schema_guard(` 调用点为 168（历史 152 是旧快照的筛查数量，非缺陷数量）。已建立受版本控制的审计记录 `docs/research/namespace_ddl_rootserver_audit.md`，逐项区分进程级和对象所属 namespace。
- 子空间自有表的 `TRUNCATE PARTITION` + global unique index 先返回 1146：分区表达式解析从 `GCTX.schema_service_` 取 ns1 guard。显式传 DDL 所属 schema service 后，`SYNC_TRUNCATE_INFO` MDS 中的逻辑 index tablet ID 又返回 4725；现于 namespace 写边界转换。
- 继承表同一路径的 index tablet 在父空间。DDL 事务之前按 fork 快照在子空间物化索引 tablet，再登记 MDS。最小复现确认子空间截断、父空间不变及重启恢复；公共 direct/TLS DDL 回归补充自有表、继承表、索引强制查询和截断后写入。
- `ob_ddl_scheduler.cpp` 的队列未命中存在性查询、建任务前 checksum 检查和 `start_redef_table` 仍值得沿调用链审计。此前临时尝试“改用重建索引”卡在 checksum validation，未保留该实现。
- 历史 152 项验收框保持未勾选；本轮只关闭已复现的分区截断缺陷，其余调用点继续逐项核对。

## 2026-09-24 guard 调用点归口与 domain-index 重定义

- 当前 rootserver 有 168 个 `get_runtime_schema_guard(` 调用，其中 161 个用默认版本；按目录/拥有者归口见受控文档 `docs/research/namespace_ddl_rootserver_audit.md`。这不等于全部 DDL/rootserver 依赖均完成审计。
- 队列未命中的重定义任务存在性检查现用任务所属 SQL proxy；`start_redef_table` 从调用它的本地管理服务接收 context。`EXCHANGE PARTITION` 更新 monitor_modified 时，读写现用同一所属 DDL 事务。
- 审计时尝试把建任务前 checksum 虚表查询改到子空间 SQL proxy，子空间 CREATE INDEX 立即返回 1235；该虚表由进程级元数据服务支持，已恢复全局查询并标为需要继续核对 ID 隔离的例外。
- 显式主键 FTS 子空间表的 `MODIFY v INT→VARCHAR` 旧任务 type 1001 在复制依赖索引时 -4016，客户端超时；相同 ns1 语句通过。临时阶段日志证实子空间 rowkey-doc 表 ID 500020 在 ns1 guard 下返回空 schema。表/列重定义子任务及新建索引 snapshot 入口现在显式传所属 root service；两类聚焦用例通过，临时埋点已清理。direct full 添加两类 FTS 重定义、续写和重启查询。
- 最终离线构建 `/tmp/seekdb-ddl-audit2-fts-sibling-build.log`、四门禁 `/tmp/seekdb-ddl-audit2-final2-{bootstrap,sql,direct,tls}.log` 均通过。剩余 legacy RPC 修改任务时的全局 SQL proxy、checksum 虚表 ID 隔离及其它全局依赖，工单仍未验收。
