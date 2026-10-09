# seekdb Namespace Fork

seekdb 的多 namespace fork 特性：单进程内承载多个相互隔离的数据库命名空间，fork 秒级派生新命名空间，共享同一套存储引擎与计算资源。

## Language

**Namespace（ns）**:
一个隔离数据库实例的身份：名字、血缘（fork 自谁）、存储根。纯元数据，不含任何运行时状态。
_Avoid_: tenant、instance、worker（指进程的旧用法）

**NamespaceRuntime（Runtime）**:
一个 Namespace 的进程内运行时：该 ns 持有的 per-ns 服务组（schema service、plan cache、session mgr 等）。纯计算层，构造即绑定 ns，内部零 ns 感知。
_Avoid_: NsContext、worker 进程

**NamespaceRegistry（Registry）**:
进程内唯一的 ns 全局注册表：名字/id → Namespace + Runtime。ns 语义的合法解释点之一（另两个是登录路由与 id 编码翻译层）。

**系统 ns（system namespace）**:
ns 1，bootstrap 自带的第一个 ns，承载全局调度元数据（如 `__all_freeze_info`），永久存在。
_Avoid_: 与 __template__ 混称

**模板 ns（`__template__`）**:
内置只读模板 Namespace，fork 空 ns 的母本；不承载持续写入，拒绝登录/DDL/drop。

**例外表（exceptions）**:
fork 后记录各 ns 私有 tablet 的覆盖层清单：`encode(base_ns, tablet) → owner_ns`。读路径先查例外表、未命中回源 ns，实现存储层共享物理 B+ 树下的 per-ns 覆盖。

**worker（遗留）**:
worker 进程模式的遗留术语。单进程化后进程消亡，概念由 NamespaceRuntime 取代。
