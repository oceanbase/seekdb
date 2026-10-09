# 03: Phase 1a：Namespace/Runtime/Registry 骨架 + root@ns 登录绑定

**What to build:** 建 Namespace（身份/血缘/存储根）、NamespaceRuntime（per-ns 服务组持有者）、NamespaceRegistry（唯一新全局）骨架；登录路由解析 `root@ns`，session 登录时一次绑定 Runtime。单 ns（ns 1）形态下跑通，外部行为与单体完全一致。

**Blocked by:** 02

**Status:** done

实施备注：worker 自注册 name 为空（spawn 协议未携带名字），name 路由登录目前只能走 proxy 入口；`root@`（空后缀）按无后缀处理落 home ns，与 proxy 的报错略有差异。均在工单 05 双 ns e2e 中收尾。

- [x] 无 `@` 登录落 home ns（worker=自身 ns，否则 ns 1），四 prototype 套件 PASS；mysqltest 未单独跑（部署链路是单体形态，门禁以 prototype 套件为准）
- [x] session 缓存 Runtime 指针（`ObSQLSessionInfo::set_ns_runtime`，登录时一次绑定）
- [x] 无 thread_local 当前 ns、无 get_instance 新增（唯一新全局 `ns::namespace_registry()` 为 ADR 0003 特许）
