# 类 PG 灵活设计：seekdb 插件扩展能力深化

状态：设计提议，不是全部能力已实现的声明。整理日期：2026-09-08。

目标语雀页：https://yuque.antfin.com/obopensrc/tignua/qtpvzcon9zl21gr8 。本地待发布稿。2026-09-10 已通过 CLI 定位目标《类PG灵活设计》（文档 ID：575566492），但读取正文因 `uv_interface_addresses returned Unknown system error 1` 失败；未执行远端更新，避免覆盖尚未读取的已有内容。

本文整理 [下一阶段设计](plugin-next-design.md) 中的灵活性内容，重点回答“插件能决定什么”，Rust 实现细节另见 [Rust 专题目标页](https://yuque.antfin.com/obopensrc/tignua/cx0pnahie5lgenps)。既有材料：[PG 插件分析](https://yuque.antfin.com/obopensrc/tignua/tgmf739yf6g26bz6)、[原设计](https://yuque.antfin.com/obopensrc/tignua/timw7t5luoyqvrdv)、[此前对比](https://yuque.antfin.com/obopensrc/tignua/xx69d23h2d9w87gw)。这些语雀链接本次未成功读取；本文依据本地讨论稿整理，不声称逐字复现远端内容。

## 1. 总体方向：让插件成为数据库能力的提供者

seekdb 的两个产品目标是轻量化与 AI 功能丰富。插件化不仅用于拆分动态库，还应让函数、类型、索引算法、查询规划、外部数据访问和后台任务能够独立演进。

建议采用“PG 式扩展对象管理 + 两档 native API + 按需初始化”。不把少量固定 descriptor 作为长期能力上限，也不要求每个插件先满足长期 ABI、热升级和安全卸载，才允许接入深层能力。

这里的自由度是：插件作者决定 SQL 对象和执行语义，内核提供可组合的注册、事务、依赖和执行协议。它不是允许任意绕过数据库一致性约束。

## 2. 将交付物、数据库对象和运行代码分开

| 概念 | 管理什么 | 生命周期 |
| --- | --- | --- |
| Package | control、SQL 脚本、动态库、模型及其他资源 | 安装到实例可用目录；纯 SQL 包可以没有动态库 |
| Extension | 数据库范围内的安装实例、版本、成员和依赖 | CREATE / UPDATE / DROP；不等同于动态库装卸 |
| Module | 已装载代码、回调、运行状态和资源 | load / init / drain；复杂模块可以要求重启 |

SQL 对象使用稳定逻辑身份，执行时再绑定具体 module generation。不能让动态库重载改变持久类型身份，也不能把 DROP EXTENSION 自动解释为立即 dlclose。

命名与作用域适配 seekdb 的 tenant/database 语义，不照搬 PG database/schema/search_path。共享代码不意味着共享数据库级配置、用户身份或权限。同进程不同包版本的符号与全局状态冲突，应检测并拒绝或明确隔离。

## 3. 插件可以直接注册 SQL catalog

目标明确为“可以”，但直接注册指使用内核 catalog API，而不是插件自行 INSERT 系统表或修改 schema 内存。

三种入口共用一个对象管理路径：

1. 安装 SQL：插件提供 control、安装和更新脚本，使用正常 DDL 定义函数、类型、operator、cast、表和视图等。
2. SDK 生成 SQL：从 Rust attribute 或 C/C++ 声明生成可检查、可修改的脚本，允许与手写 SQL 混合。
3. 运行时 catalog API：提供绑定当前会话与事务的 CatalogContext，以及 create / alter / drop / lookup 等对象接口，允许按运行时输入创建对象。

三条路径统一处理名称解析、对象 ID、权限、依赖、保存点、提交可见性和缓存失效。运行期产生的业务对象不自动成为 extension member；只有安装/更新上下文或显式成员操作才建立成员关系。

普通 SQL 函数可以通过 module + entry name + calling convention 绑定实现，不必全部包装为跨插件 service。可以保留单入口函数表，也可允许声明清单中的多导出符号。

PG 的 extension 通过 control 与 SQL 脚本组织相关数据库对象，使用成员关系管理安装、更新和删除。借鉴的是这一对象模型，而不是将 descriptor 数量扩充后继续封闭入口。[PG Extension 文档](https://www.postgresql.org/docs/18/extend-extensions.html)

### 查询期创建对象的语义

以下是目标契约，不是现有 API 用法：

- 默认继承调用者数据库、用户和事务，不另开独立提交的 catalog 写事务。
- 成功创建后，同一事务的后续 SQL/PL 能解析该对象及其权限；其他事务按已提交 schema 可见性规则访问。
- 语句失败、ROLLBACK TO SAVEPOINT 和整体回滚同时撤销对象、权限、依赖与待处理失效请求。
- 提交前完成必要的 schema 版本和一致性检查；只有确认提交后才发布刷新和缓存失效。
- 提交后失效任务投递失败属于需重试的后处理，不能谎报数据库回滚，也不能重试数据库提交。

允许逐步支持不同对象类别，但应明确列出支持集合，不能将“安装阶段能登记对象”描述成“查询期任意 catalog DDL 已经可用”。

## 4. 两档 API：公共开发与深度开发并存

| Profile | 面向能力 | 接口约定 |
| --- | --- | --- |
| Public | 普通 UDF、文本处理、AI provider、连接器 | 版本化 C ABI、typed batch、opaque context、SQL/catalog API |
| Server-dev | planner、执行器、索引、类型统计、系统观测 | 允许声明的 server headers 与真实内核上下文，按精确构建版本重编译 |

两个 profile 共用 loader、对象身份和错误语义，一个包也可以同时使用两档接口。Public 的兼容承诺只覆盖实际验证过的版本；Server-dev 校验 build/toolchain fingerprint，不能只比较产品版本号。

构建边界应按 profile 检查：Public 禁止依赖私有实现；Server-dev 允许指定的宿主接口。两者都不能把核心静态库复制进插件形成第二套单例，也不能让核心反向链接 GIS、模型或具体索引算法包。

PG 的 C 扩展依赖服务端 API，其 ABI 兼容与重编译要求不是“长期冻结所有内部结构”。seekdb 可借鉴版本绑定的深度开发方式，但不宣称 PG 二进制能直接加载。[PG C 函数与 ABI 说明](https://www.postgresql.org/docs/18/xfunc-c.html)

## 5. 开放真实的执行扩展点

| 扩展方向 | 插件实际可控制的行为 | 示例 |
| --- | --- | --- |
| Function / Aggregate | 标量与批量计算、聚合状态、合并、类型及成本属性 | tokenize、embedding、rerank |
| Type / Operator | codec、类型参数、compare/hash、cast、统计协作 | geometry、sparse vector、tensor |
| Index | build、维护、scan、recheck、代价和支持函数 | 空间索引、ANN、混合检索 |
| Planner / Custom operator | 提供备选 path、代价估算、生成和执行计划、EXPLAIN | GPU 批处理、远端检索、融合算子 |
| Executor hook | 观察、包装或接管明确的执行阶段 | profiling、模型调用观测、执行策略 |
| Data source | schema discovery、scan、投影与过滤下推、rescan/cancel | 文件、对象存储、外部 API |
| Background task / Config | 任务注册、配置、资源归属与预算 | 增量 embedding、模型缓存 |

PG Custom Scan 提供从候选路径到计划和执行的扩展协议。这比“注册一个 planner descriptor，但没有真实调用点”更接近所需自由度。[PG Custom Scan](https://www.postgresql.org/docs/18/custom-scan.html)

Hook 建议区分 observation、around、replacement。观察型不得改变语义；包装型显式调用 next；替换型可在允许的阶段接管。统一规定顺序、冲突、重入、线程和 context 生命周期，不强制所有 hook 永远调用 previous。

类型不应永远等同于带 codec 的 BLOB。索引接口必须适配 seekdb 的 LSM/MVCC、恢复与执行模型，不能原样复制 PG 索引内部结构。任意运行期修改 parser grammar 不作为初期目标，但通用函数、operator 和对象接口不应限制普通业务扩展。

## 6. 提供真正可用的服务端 SQL 接口

提供参数化 prepare/execute、结果 cursor、取消、错误和资源管理，让插件组合既有 SQL 能力。PG SPI 的核心价值正是允许服务端函数访问 SQL 执行能力。[PG SPI](https://www.postgresql.org/docs/18/spi.html)

seekdb 的接口应明确区分安装、查询和后台阶段。查询阶段默认继承当前权限与事务；后台任务建立自己的 session/transaction。不能让插件从任意线程复用 query context，也不能在普通函数中任意提交调用者事务。

SQL 与 catalog 开放只覆盖内核已实现的对象/执行协议。新增普通函数不应修改核心 factory；引入完全新的一类物理算子或存储协议，仍可能需要先增加通用内核接口。

## 7. 轻量化与 AI 对生命周期的要求

将文件部署、SQL 安装、代码装载、模型初始化分开。普通模块 lazy load，首次初始化 single-flight；全局 hook 明确 preload；模型、GPU context 和连接池进一步按需初始化。

缺失可选 AI provider 时应只影响依赖它的查询。恢复所必需的类型/索引模块则必须在相关恢复前加载，不能用“可选插件”掩盖数据解释能力缺失。

执行以批量为优先，输入借用只读缓冲区，输出明确分配与释放方。计划、游标、异步任务和释放回调必须持有相应模块引用。只有布局兼容时才承诺零拷贝。

AI 接口显式携带模型版本、deadline、取消、并发和内存预算；远端模型调用不能默认视作 immutable。跨异步边界使用 owned 数据，不保留查询借用指针；数据库回滚不能撤回已发生的外部调用。

不强制每个模块支持热卸载。能证明安全的模块支持 stop/drain；深度 hook 或复杂后台资源可以声明 restart-required。验收必须测量未使用插件时的启动、RSS、线程和包体成本，而不只检查是否产出了动态库。

## 8. 最大的适配工作：事务与可见性

PG 安装/更新脚本在事务中执行，限制显式事务控制及不能在事务块内执行的命令。[PG 安装事务规则](https://www.postgresql.org/docs/18/extend-extensions.html)

seekdb 不能仅关闭 DDL 的 implicit commit 就承诺同样的原子性。需要统一 caller transaction、schema 写入、权限、依赖、provisional 对象解析、schema publication 和缓存失效。

优先完成 routine 等纯 catalog 对象的纵向闭环，再扩大类型、cast、operator 及实体 DDL 的覆盖。表和索引还需要验证 storage task 与恢复语义。未支持的语句在执行前明确拒绝，不能逐条 DROP 补偿后称为事务回滚。

## 9. 与 Rust、现有 GIS 的关系

灵活性来自宿主开放的对象和执行协议，不由实现语言自动提供。Rust 适合实现 package/依赖、registry、生命周期、事务私有日志及 SDK；parser/resolver、schema、MVCC、存储等继续通过 C++ 桥接。

GIS 可以继续使用 C++，没有必要为迁移插件管理器而重写 GIS 算法。C++ GIS、Rust AI 插件和纯 SQL 扩展共用对象模型及运行时。深度 Rust 插件通过同构建版本的 C++ adapter 访问内核，不直接假设 C++ class 布局。

## 10. 当前基础与验收路线

本地工作区已存在 C ABI/SPI、Rust runtime/SDK、对象与扩展包管理，以及 routine catalog 写入和事务协调的部分基础。不能因此宣称全部 PG 式自由度已经完成；当前进度与限制见 [实施进度](plugin-implementation-status.md)。本文不新增构建或测试通过声明。

尤其需要继续完成查询期公开 CREATE/ALTER/DROP、session 跨语句对象/权限视图、提交后对象缓存失效，以及真实数据库权限、保存点、并发和提交可见性验证。深层 planner/index/type 协议也不能用既有 descriptor 的存在代替。

建议按三个纵向样例验收：

1. Rust 文本插件 + 纯 SQL 包：SQL/SDK/运行期 catalog 入口汇合；安装失败和查询保存点不残留可见半对象。
2. 查询观测 + 自定义 path：真实参与规划、执行和 EXPLAIN；多 hook 组合、取消与资源释放可测。
3. GIS 或 ANN：验证类型语义、operator、索引 scan/recheck、依赖和恢复；核心不链接算法包。

结论：可以更积极地借鉴 PG。应放开插件能提供的对象和行为，允许版本绑定的深度 native API，并用统一事务、依赖和资源契约承接自由度；不必把长期 ABI 与热卸载作为所有扩展的前置条件。
