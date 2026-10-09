# 08: Phase 2b：inner SQL 显式目标 ns 化

**What to build:** 拆除 ControlSqlNamespaceScope thread_local hack，inner SQL 入口强制显式目标 ns 参数；共享层调用点全部显式指定（全局调度元数据 → 系统 ns ns 1）。

**Blocked by:** 07

**Status:** done

- [x] inner SQL 入口无显式目标 ns 编译不过/运行报错
- [x] thread_local override 机械全部删除
- [x] 存储层读 freeze info 等上向调用定向系统 ns 后正确

## 2026-09-23 implementation and validation

- `inner_call` and `inner_read` now require a namespace ID and reject invalid IDs. The SQL proxy passes its `target_namespace()` into each owned inner connection; transactions forward their owning SQL client's target. External-session inner connections use their session runtime. Worker startup binds both process SQL proxies to its fixed namespace; the shared process binds them to namespace 1.
- Removed `ControlSqlNamespaceScope`, the thread-local override stack, and the unused trace-binding map. In-process namespace proxies no longer wrap reads and writes. Schema metadata and parallel DROP helpers take the target from their SQL client; vector checksum writes use an explicit target proxy. A missing child runtime now errors before local SQL execution.
- Build `/tmp/seekdb-ticket08-build5.log` succeeded. All four worker gates emitted PASS on the final binary: `/tmp/seekdb-ticket08-gate-bootstrap-final.log`, `/tmp/seekdb-ticket08-gate-sql.log`, `/tmp/seekdb-ticket08-gate-direct.log`, and `/tmp/seekdb-ticket08-gate-tls.log`.
- On the double in-process instance, child CREATE TABLE, INSERT, CREATE INDEX, transaction UPDATE/COMMIT, DROP INDEX, and DROP TABLE succeeded; namespace 1 could not see the child table. `FORK DATABASE ticket07_a TO ticket08_inner_sql` succeeded and the new namespace read its inherited two-row table. That FORK path calls `reload_storage_freeze_info()` after commit. After the final binary restart, both the original child and the new fork remained readable.
