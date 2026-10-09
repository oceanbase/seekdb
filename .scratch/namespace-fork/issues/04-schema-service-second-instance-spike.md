# 04: Phase 1b：schema service 第二实例共存 spike

**What to build:** 验证进程内两个 ObMultiVersionSchemaService 实例共存：排清 refresh 定时器、inner SQL 连接池、publish signal 等隐藏全局依赖，逐个实例化或隔离。这是单进程化最大技术风险点，先行排雷。

**Blocked by:** 03

**Status:** done

- [x] 第二实例 init + 版本推进（broadcast）正常；inner-SQL 全量 refresh 需会话绑定上下文（worker IPC 机制，非多实例阻碍，单进程后消失）
- [x] 清单与处置落 docs/research/namespace_schema_service_spike.md
- [x] 实测 the_one_version 与 second_version 各自独立（1790099913891192 vs 1051440）
