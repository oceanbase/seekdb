# 13: Phase 4b：允许 drop 源 ns

**What to build:** 解除 prototype"禁止 drop 被 fork 过的 ns"限制：子 ns 例外表回源指针改指 snapshot 根，源 ns drop 后子 ns 读写不受影响；drop 只删元数据 + 注销 Registry，存储异步 GC 回收；有活跃连接时拒绝 drop。

**Blocked by:** 11

**Status:** functional gates passed; metadata rebase TODO is nonblocking

- [x] drop 有活跃连接的 ns 报错拒绝
- [x] drop 源 ns 后子 ns 读写正常，数据不丢
- [x] 存储空间经异步 GC 最终回收

实测：`namespace_source_drop_prototype.py` 覆盖活跃连接拒绝、删除源空间后子空间读写、进程重启后读取，以及删除最后子空间后 20 秒内异步 GC 清空源空间 owned tablet 记录；`/tmp/seekdb-ticket13-access-source-drop.log` PASS。实现保留已删除源空间的不可登录祖先行与例外记录，供仍存活的子空间按 fork SCN 回源；后台任务按页扫描并回收无人引用的 tablet。

编译 `make -j80 seekdb` 通过；最终二进制的四道单进程门禁全部 PASS：`/tmp/seekdb-ticket13-access-{bootstrap,sql,direct,tls}.log`。`git diff --check` 通过。

TODO（不阻塞后续工单）：当前例外表只有 owned/tombstone 记录，没有可改写的回源指针。若须严格满足“子空间回源指针改指 snapshot 根”，需设计快照根重定向并在最后一个引用消失后清理祖先墓碑和名称占用；当前方案通过不可登录的 state=2 祖先行提供等价的读写与重启行为，但不会复用已删除名称。

## 2026-09-24 已删除名称复用

- 先在原二进制扩展源空间删除回归：删除 `source_drop` 后立刻同名 `CREATE NAMESPACE` 返回 1062（`/tmp/seekdb-ticket13-name-reuse-red.log`）。根因是 `state=2` 祖先行仍占用唯一名称；子空间继承按不可变的 `parent_namespace` ID 与 fork cap 解析，无需祖先名称。
- `finish_namespace_drop` 现在在同一事务中将 tombstone 的 `name` 置 NULL，保留旧 namespace ID、父链和例外记录，供存活子空间在重启后回源。回归验证新同名 namespace ID 不同、无旧用户库；旧子空间仍读写旧源数据，重启后两者行为保持，最后删除旧子空间后异步 GC 清空旧源 owned tablet。`/tmp/seekdb-ticket13-name-reuse-final.log` PASS。
- 离线编译 `/tmp/seekdb-ticket13-name-reuse-build.log` 成功；四门禁 `/tmp/seekdb-ticket13-name-reuse-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。剩余：将子空间回源重定向到独立 snapshot 根，并在最后引用消失后清掉旧祖先行与 tombstone；本次仅解决名称复用。

## 2026-09-24 无引用祖先墓碑回收

- 扩展源空间删除回归先红：最后一个子空间删除、旧源 owned tablet 已被异步回收后，`__fork_proto_meta.namespaces` 的源/子 `state=2` 行仍一直存在，`/tmp/seekdb-ticket13-tombstone-prune-red.log`。
- 后台 tablet GC 现每轮有界地按 namespace ID 降序检查至多 64 个删除态行。仅当没有直接子 namespace 且没有未回收的 owned tablet 时，在同一事务删除该行的例外记录及 namespace 行；清掉进程内目录/例外/父链缓存。名称仍由前一提交在 DROP 事务内释放。
- 三级链回归覆盖：源删除后同名重建与旧子空间隔离；重启后旧子空间可读；再 fork 孙空间，删除中间空间后孙空间仍可读；最后删除孙空间后旧源的 owned tablet 和三级祖先行全部异步消失。`/tmp/seekdb-ticket13-tombstone-prune-final.log` PASS。早期单条/轮的版本超过 20 秒测试窗口，见 `/tmp/seekdb-ticket13-tombstone-prune-grand.log`，已改为有界批量扫描。
- 离线编译 `/tmp/seekdb-ticket13-tombstone-prune-final-build.log` 成功；四门禁 `/tmp/seekdb-ticket13-tombstone-prune-{bootstrap,sql,direct,tls}.log` 均 exit 0 且 PASS。严格的“子空间回源指针改指独立 snapshot 根”仍未实现，当前仍依赖已删除祖先行维持存活子空间的回源链，故工单 13 不关闭。
