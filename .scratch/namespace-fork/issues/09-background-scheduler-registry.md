# 09: Phase 2c：后台调度 Registry 化

**What to build:** freeze/merge 调度器保持全局单例，改为遍历 Registry 逐 ns 取 schema guard 传入进度检查器（checker 内部零改动）；全局调度元数据统一定向系统 ns；多 ns 下合并/冻结正确运转。

**Blocked by:** 08

**Status:** done

- [x] major/minor merge 在多 ns 下全局一轮完成，broadcast scn/freeze info 语义不变
- [x] 调度器不持有任何单一 ns 的 schema service 引用
- [x] 新增 ns 后下一轮合并自动覆盖

## 2026-09-23 验证

- Committed and pushed as `9344885b4`; final build and bootstrap/sql/direct/tls worker-mode gates passed.
- Scheduler 每轮从 Registry 获取 namespace，使用各自 schema guard 检查实际存在的 owned physical tablets；原有 progress checker 仍负责 ns1。冻结元数据留在 ns1。
- Medium compaction 与 meta merge 经 schema runtime resolver 把 child physical tablet 映射回其 schema service 和 logical tablet ID。修复前 child 的 major merge 被误判为 deleted table。
- 双门控实例第一轮 global broadcast/last merged 均达到 `1790174570673158668`；新增 `ticket09_merge_new` namespace 并写入 owned table 后，第二轮均达到 `1790175200135039004`。新 namespace 的 tablet `4620275961609612609` compaction SCN 为 `1790175200135039008`，report SCN 为 broadcast SCN，三行数据合计 30 可读。
- 新 namespace 的 physical tablet minor freeze 后产生新的 type 12 SSTable，end log SCN 为 `1790175452097422000`。
- 既有 DDL 遗留 schema 可能列出已被清理的 physical tablet；进度检查只等待 metadata 中实际存在的 tablet，避免永远挂起。
