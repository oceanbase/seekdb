# Namespace 服务归属与全局回退清理：工单拆分草案

来源：同目录的服务归属规格。待用户确认粒度和依赖后发布；本文件不是已发布工单。

1. **统一请求服务入口与系统 ns schema 路径**  
   **Blocked by:** None.  
   **What it delivers:** 任意 Namespace 的登录会话都有完整 Runtime；系统 ns 普通查询也经自己的 Runtime 取得 schema。缺少 Runtime 或必需实例时请求明确失败。作为扩展阶段，旧入口暂时保留供后续逐批迁移。  
   **Acceptance:** 系统 ns 与两个 fork Namespace 的同名异构表查询各自正确；模拟缺失绑定时不读取系统 ns 数据；现有四件套通过。

2. **内部 SQL 与 SQL proxy 显式目标**  
   **Blocked by:** 1.  
   **What it delivers:** 普通 SQL 引发的内部 SQL、目录写入和代理查询始终使用所属 Runtime 的 SQL proxy；真正的全局调度元数据操作显式指定系统 ns。  
   **Acceptance:** 两个 Namespace 的 DDL/目录与错误记录不串库；无会话或无目标的内部 SQL 返回错误；四件套覆盖曾失败的目录场景。

3. **查询与 DAS 的 schema 查找**  
   **Blocked by:** 1.  
   **What it delivers:** 普通查询和 DAS tablet 映射始终从会话所属 schema 实例取 guard，不直接访问进程全局 schema。  
   **Acceptance:** 相同 schema_id 的两个 Namespace 独立解析；分区与索引结果正确；无上下文时可见错误。

4. **PX 与虚拟表继承会话所属服务**  
   **Blocked by:** 1, 2.  
   **What it delivers:** PX 子任务和虚拟表内部查询沿用发起会话的 Runtime 与 SQL proxy，跨执行线程仍使用所属 schema。  
   **Acceptance:** 两个 Namespace 的 PX、会话/目录虚拟表各自可见；任务缺失所有者时明确失败。

5. **DML、range 与 direct insert 显式绑定**  
   **Blocked by:** 1.  
   **What it delivers:** 普通写入、范围扫描和 direct insert 不再接收全局服务回退，所有请求使用 Runtime 指定的服务；无状态适配器仍可共享。  
   **Acceptance:** 两个 Namespace 对同名表并发读写互不影响；缺少必需绑定时失败而非写入系统 ns；四件套覆盖 bulk write 和 range 查询。

6. **两类自动增量服务归所属 Namespace**  
   **Blocked by:** 1, 2.  
   **What it delivers:** 表序列和 tablet 序列的请求、DDL 和内部写入均通过所属 Runtime；物理 tablet 翻译仍在既有边界完成。  
   **Acceptance:** 同名表及相同逻辑 tablet ID 的两个 Namespace 自增独立；重启后续写正确；不直接取系统 ns 单例。

7. **DDL 前台流程统一 schema lifecycle**  
   **Blocked by:** 1, 2.  
   **What it delivers:** 系统 ns 与 fork Namespace 的 CREATE/ALTER/DROP 使用同一 Runtime 服务契约；schema 发布与本地执行所需依赖显式配置，普通 DDL 不按 ns ID 选类或走全局回退。  
   **Acceptance:** 两侧 DDL、可见性和约束结果一致；管理权限仍显式受控；四件套覆盖继承表和自有表。

8. **索引 DDL 任务及恢复保留所属服务**  
   **Blocked by:** 7.  
   **What it delivers:** B-tree 与全文索引的建删、后台执行、失败清理和重启恢复始终使用任务所属 Runtime 服务。  
   **Acceptance:** 子 Namespace 的索引任务记录、checksum、错误和清理留在本空间；中断重启后完成或明确失败；系统 ns 同路径通过。

9. **表与列重定义任务及恢复保留所属服务**  
   **Blocked by:** 7.  
   **What it delivers:** 表/列重定义的后台构建、schema 交换、失败回滚和重启恢复使用任务所属 Runtime 服务。  
   **Acceptance:** 自有与继承表重定义在中断重启后结果正确；源表和目标表数据不串 Namespace；系统 ns 同路径通过。

10. **约束 DDL 与异常任务路径保留所属服务**  
    **Blocked by:** 7.  
    **What it delivers:** 约束验证、任务取消、队列未命中和异常清理明确保留所属 Namespace，不把缺失上下文当成系统 ns。  
    **Acceptance:** 两侧 CHECK/FK 约束结果与错误记录正确；取消和失败清理不误操作别的 Namespace；无所属任务明确失败。

11. **共享存储的 tablet schema 解析去除默认 schema**  
   **Blocked by:** 1.  
   **What it delivers:** compaction、存储读写及相关后台操作由物理 tablet 身份解析所属 schema，或由调用者直接提供服务；共享 resolver 不默认返回系统 ns schema。  
   **Acceptance:** 同逻辑 tablet ID 的两个 Namespace 在合并、读取及重启后都能找到正确 schema；缺少映射时明确报错。

12. **预处理语句与 plan cache 按 Namespace 隔离**  
   **Blocked by:** 1, 2.  
   **What it delivers:** 每个 Namespace 持有自己的 PS cache，与其 plan cache 和会话生命周期一致；prepare、execute、close、失效不复用其他 Namespace 的元数据。  
   **Acceptance:** 同 database_id、同 SQL 文本但不同表结构的两个 Namespace 可交替 prepare/execute；DDL 后各自失效；重连与关闭无跨空间残留。

13. **优化器统计与监控按 Namespace 隔离**  
    **Blocked by:** 2, 12.  
    **What it delivers:** 统计读取、收集、监控、刷新和缓存失效均使用所属 Namespace 的实例与 SQL proxy；共享 logical schema_id 不引起缓存碰撞。  
    **Acceptance:** 两侧相同 table_id 的不同数据规模产生各自统计和计划；手动收集、监控刷新及重启后仍隔离。

14. **向量服务使用所属 schema 和 SQL 依赖**  
    **Blocked by:** 2, 11.  
    **What it delivers:** 向量建索引、查询、后台加载与失败清理均显式取所属 Namespace 的依赖；保留已隔离的物理 tablet 缓存键。调度器与缓存实例拆分留在既有独立 TODO。  
    **Acceptance:** 自有及继承向量索引在两侧可查询、重启后可预热和清理；无全局 schema/SQL 回退。

15. **收缩旧入口并统一服务生命周期**  
    **Blocked by:** 2–14.  
    **What it delivers:** 删除正常请求可达的单例入口、fallback 参数和系统 ns 专属服务选择；统一所有 Namespace 的服务注册、校验与销毁，保留明确的 bootstrap/管理能力。  
    **Acceptance:** 正常请求和 Namespace-local 后台路径无隐式全局服务来源；缺实例显式报错；系统 ns/两个 fork Namespace 的四件套及重启门禁通过；仅代码文件提交，测试脚本留在本地。

## 依赖说明

- 1 是扩展入口，15 是删除旧入口；中间各票可独立保持现有功能可用。
- 2、3、5、11、12 在 1 完成后按各自实际依赖推进，不需要串成单一长链。
- 8、9、10 分别覆盖独立 DDL 任务家族，仅依赖 7 的前台 DDL 服务契约。
- 13 依赖 12，因为统计刷新会触发 plan cache 失效。
- 15 等待所有迁移票，避免删掉仍被调用的旧入口。
