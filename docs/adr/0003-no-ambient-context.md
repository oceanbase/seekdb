# 无 ambient 上下文：禁全局单例与 thread_local，一律显式传递

全库原则（不限于 ns）：上下文与服务实例一律显式传递（函数参数、构造注入），禁止引入 thread_local 上下文与 `get_instance()` 全局单例。这是对 OceanBase MTL 模式的刻意背离：MTL 用 thread_local 传递"当前租户"，谁 set、何时失效、跨线程谁带上全靠人肉纪律，漏一处即静默读错数据。

## Consequences

- ns 只能经 session/任务显式传入；inner SQL 入口强制带目标 ns 参数（替代 `ControlSqlNamespaceScope` 的 thread_local hack）。
- 配套不变式 2b：传递的是服务实例而非 ns 身份，模块不得收 ns_id 后内部分支；ns_id/ns_name 的解释权只归入口层（登录路由、Registry、id 编码翻译）。
- 存量单例不强制一次清完，但新增代码禁止引入，改造经过时顺手实例化；由 bazel visibility 门禁兜底。
