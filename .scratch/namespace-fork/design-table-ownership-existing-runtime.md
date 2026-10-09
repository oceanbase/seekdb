# 设计约束与修订：复用现有执行环境，表归属由 schema 与物理路由表达

> 2026-09-29：用户指出 SQL 执行还会产生其他 `__all_*` 附带状态。只改静态定义和目标表物理路由无法统一约束这些写入，本文方案不作为实施依据。当前问题与收敛方向见 [实例元数据存储](design-instance-metadata-storage.md)。下文保留供追溯。

状态：2026-09-29 修订草案，未实现；下列实现核对项完成前不能宣称整体方案已落地。文档留在本地。

## 1. 硬约束

- 不增加 NamespaceRuntime，不增加实例专用 Runtime。
- 不增加实例专用服务组、plan/PS cache 实例或后台线程。
- 不维护另一份精简服务清单，不为内部空间逐项设置特化参数。
- Namespace 1 继续可以作为默认用户空间及 fork 源，不为容纳控制表改变用户连接语义。
- 现有未拆分的全局服务仍按统一 Namespace 架构清理，不另加一套实例执行环境需要适配。

新增完整内部 Namespace 的方案因资源成本被用户明确拒绝，不以测量或后续通用优化为前提继续推进。单独精简实例执行上下文的方案因维护成本已撤回。

## 2. 修正此前的推导

内部代码通过 Namespace 1 proxy 执行 SQL，与客户端在 Namespace 1 使用同一套执行服务。这件事本身不要求目标表的数据和定义进入 Namespace 1 的 fork 快照。

此前把“执行依赖归属”与“表数据归属”一起拆分，导致新增 Runtime 或第二套服务清单。修订后保留现有 proxy/session/Runtime 绑定，仅修改表定义来源和数据路由。

## 3. 候选方案

### 执行入口

实例管理 SQL 继续使用明确注入的现有 Namespace 1 proxy，使用它已有的 SchemaService、缓存、自增/锁等执行接口。普通 Namespace 1 SQL 使用相同执行环境。内部访问权限通过明确的内部调用能力表达，不因客户端登录 Namespace 1 自动授予。

不增加 INSTANCE SqlTarget，不增加新连接环境；内部 SQL 连接仍绑定既有 NamespaceRuntime。实例表的直接管理读写入口统一注入同一个管理 client，避免多个 Namespace 的本地缓存分别管理一份实例状态。

### schema 来源

将实例内表改为代码生成的固定内表定义，声明表归属和内部可见性，复用现有内表装载机制。主表、库容器、列、索引和 LOB 的完整定义来自同一静态源。

schema 构造/发布与持久化分别处理：实例定义可供现有 schema 读取接口解析，但不经普通 CREATE TABLE 在 Namespace 1 的 `__all_database`、`__all_table`、列、索引和 history 等表中写入记录。bootstrap、全量刷新、增量刷新和重启必须使用相同的声明，不能只跳过第一次写入。

代码已有 `construct_inner_table_schemas()` 和 `broadcast_runtime_schema()` 这条静态定义装载路径；现有 bootstrap 又在 `batch_create_schema()` 中调用普通 DDL 持久化。这证明可用的改造位置存在，但不等于当前已支持“静态实例定义不写入 __all_*”。

实例定义保存在只读共享数据中，由现有 schema 读取接口引用；不创建第二个 SchemaService 或维护线程。动态用户 schema 仍属于各 Namespace。名字解析不能在 Namespace lookup miss 后回退到 Namespace 1。

### 数据地址

表的 INSTANCE / NAMESPACE 属性在统一的上层表绑定与路由处选定物理地址。INSTANCE 使用与 Namespace 地址域不相交的地址，NAMESPACE 使用现有编码和父链解析。沿用先前提出的统一物理编码布局、保留地址域 0 供实例的方案；不创建 Namespace 0。

实例控制表数据不放在 encode(1, local_tablet_id) 下，避免 Namespace 1 后代的通用父链探测命中。存储层仍只接收物理 tablet ID、事务和快照。

### fork 与权限

Namespace 的 `__all_*` 中从源头没有实例对象记录；fork 不会继承这些记录。静态内表定义来自程序，与 fork 快照无关。普通会话不能解析/枚举 INTERNAL_ONLY 内表，管理内部 SQL 可以；权限根据声明检查，不按 `__fork_proto_meta` 名字删除 schema 数组。

这是一般内表可见性规则。需要公开实例信息时，使用明确的系统视图/虚拟表入口，后台读取依赖仍显式注入。不要借此把实例基表暴露成多个 Namespace 可随意修改的本地表。

## 4. 仍需闭合的实现问题

1. 固定内表定义能否通过现有 guard 完整支持按名/ID、列、索引/LOB 与版本解析，而不回查 Namespace history；必须逐入口核对，不能只依赖 full-schema bootstrap 的成功。
2. 元数据发布和所有权枚举需区分“可解析的内表定义”与“该 Namespace 拥有且应参与 DDL/fork/GC 的对象”；不能把 INSTANCE 表加入 Namespace 对象清理和 schema delta。
3. 所有物理路由入口必须消费同一表绑定，包含 scan、DML、LOB、索引、MDS、物理 tablet 反查 schema 及锁资源键。当前 `is_inner_table()` 直接选择 Namespace 空间的规则需要修改。
4. 实例表相关的可变持久化状态（ID 计数、任务状态、锁 owner 等）也必须按数据归属保存，不能把它们又写回可 fork 的 Namespace 内表。控制表当前 Namespace ID 的 AUTO_INCREMENT 依赖需具体核对；可使用已有物理能力或显式实例计数表，但不能新增实例自增服务。
5. 普通 Namespace 服务扫描 schema 的后台路径应遵循内表归属/对象类型，不把静态实例定义当作本地用户对象重复管理。通过统一 schema 枚举接口表达这一点，避免每个服务新增表名或 ns==1 判断。
6. bootstrap 必须先以原生物理路径建立实例内表数据，再用已有 Namespace 1 执行环境完成控制 SQL；不能产生依赖尚未建立的 Namespace 目录才能访问目录的循环。
7. fork pin、控制记录和引用计数等事务依赖必须保持；实例物理地址调整不能拆散原有原子提交或恢复协议。

这些问题属于表归属进入现有 SQL/schema/数据路径的改造，并非一份新增实例服务名单。若具体路径仍要求新增执行服务环境，应回到硬约束调整方案，不能通过补服务和调小参数掩盖冲突。

## 5. 资源与验证边界

不新增 Runtime、cache 实例和线程作为结构性验收项。会增加/调整表定义和访问绑定等必要元数据，已有缓存也会随真实管理 SQL 工作负载使用内存；不承诺零字节开销。

实施后的本地四件套覆盖控制 schema 原始记录不被继承、内部权限、父链不命中实例 tablet、主辅表统一路由、相同逻辑 ID 隔离、事务与重启恢复。无需为被否决的额外 Runtime 方案安排资源测量来争取接受。
