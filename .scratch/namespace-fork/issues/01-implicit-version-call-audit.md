# 01: Phase 0a：343 处隐式版本调用三分类审计

**What to build:** 对审计发现的 343 处"隐式当前版本"调用点（279 `get_runtime_schema_guard` + 45 refreshed + 19 published）逐点三分类：能传 session/任务上下文、能推编码 id、必须 ambient。产出分类报告落文档，给出分类 3 的准确数量。

**Blocked by:** None (can start immediately)

**Status:** done

- [x] 343 处全部归类，报告写入 docs/research/（namespace_implicit_version_callsite_classification.md）
- [x] 分类 3 数量 ≤ 10（实测 0）：继续；超预期：停下重新评估方案（可行性卡点）
- [x] 不改任何代码语义
