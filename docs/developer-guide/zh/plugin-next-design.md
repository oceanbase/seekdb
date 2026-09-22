# seekdb 插件下一阶段设计：PG 式扩展自由度与 Rust 实现

状态：讨论稿；不是当前实现说明。日期：2026-09-07。

代码基线：`feature/plugin` / `a2df3db67`，工作区原始状态干净。本文仅新增设计，不修改运行时代码，不宣称已经完成构建、性能或兼容性验证。

## 1. 目标与设计调整

两个产品目标分别是：不使用某项能力的用户不承担其主要交付、初始化和运行成本；专业能力可以由插件持续扩充，尤其是 AI 检索、推理、数据接入与任务执行。

此前方案对 ABI 和生命周期投入较多，对插件能参与哪些数据库行为限制较强。下一阶段建议把重点转向完整的扩展对象模型，以及真实的 planner/executor/type/index 回调。生命周期服务于这些能力，不要求所有扩展先实现在线停用、跨版本 ABI 和热升级。

用户的 [PG 插件代码导读](https://yuque.antfin.com/obopensrc/tignua/tgmf739yf6g26bz6) 最值得采用的是 SQL 对象、fmgr、类型语义、索引支持函数、查询 hook 和服务端 SQL 访问能够组合使用。该导读以 PG19 开发版为基础；本文涉及稳定行为时使用 PG18 官方资料，不把 PG19 共享内存接口原样作为 seekdb 契约。

推荐采用：PG 式 Extension 对象管理 + 两档 native API + 按需初始化 + Rust 开发 SDK。两档 API 共用一个 loader、一个对象管理体系和一套错误语义。

## 2. 当前基础和实际障碍

| 代码位置 | 当前基础 | 下一步含义 |
| --- | --- | --- |
| `include/seekdb/plugin/seekdb_plugin_abi.h` | 单一 C 入口、service、init/start/stop/deinit | 可继续承载语言无关的装载协议 |
| `include/seekdb/plugin/extension_spi.h` | 八类固定 descriptor；明确不接受任意 SQL | 不能继续作为创建 SQL 对象的唯一入口 |
| `include/seekdb/plugin/execution_spi.h` | byte-oriented scalar、codec、table cursor | 增加批处理、上下文、取消、资源归属，避免逐行复制成为上限 |
| `src/share/plugin/ob_plugin_catalog.cpp` | 包/操作/服务/扩展目录，以及专用 type/function/argument/column 表 | 分开 SQL 对象身份与 module 的运行状态 |
| `src/share/plugin/plugin_sql_type.h` | 持久列信息包含 owner generation；类型复用 opaque LOB | 长期类型身份应与某次进程装载解耦 |
| `src/share/plugin/ob_plugin_registry.*` | 原子发布、引用 lease、generation | 保留并用于 module/callback 生命周期，不必让普通查询逐行查 registry |
| `src/observer/ob_server_plugin_runtime.cpp` | INSTALL 激活、启动恢复、ready gate | 分离 optional lazy 模块和必需的 preload 模块 |
| `src/sql/ob_result_set.cpp`、`src/sql/ob_sql_utils.cpp` | 部分 DDL 在执行前隐式提交 | PG 式事务安装需要专门设计，不能直接串行调用现有 DDL 就承诺原子性 |
| `rust/sql-nio`、`cmake/Rust.cmake`、`rust/BUILD.bazel` | Rust staticlib + C ABI 已接入 C++，两套构建有基础 | Rust 可行性不依赖引入一套全新的语言工具链 |
| `rust/Cargo.toml` | release/cmake-debug 使用 `panic=abort` | SDK 的可恢复 panic 策略需要独立构建设置 |

当前 source/CMake 边界限制对普通插件有价值；对深度插件应调整为显式的 server-development API profile。核心不得反向依赖某个具体算法包的原则继续保留。

## 3. 分开 Package、Extension、Module

| 概念 | 作用域和身份 | 保存内容 |
| --- | --- | --- |
| Package | 实例可用的交付物，带版本/平台信息 | control、SQL、动态库、资源清单；允许纯 SQL 包没有动态库 |
| Extension | 数据库命名空间中的安装实例 | owner、版本、对象成员、依赖和配置表 |
| Module | 进程内装载的某个具体 artifact | ABI/build fingerprint、函数地址、hook、资源、执行引用 |

seekdb 的 database/schema 语义不等于 PG 的独立 database。建议对象归属键显式包含 tenant/database/object namespace；具体 SQL 限定名规则沿用 seekdb，不直接复制 PG search_path。module 可以跨数据库共享代码，数据库级实例状态和权限必须分开；进程级扩展如查询统计单独声明作用域。

持久 SQL 对象使用稳定 object ID，引用 implementation 的逻辑身份和版本要求。运行时 binding 再解析到实际 module generation。已有持久列中的 generation 字段需要另行制定兼容处理；新设计不让每次重启决定类型身份。

同一 package 的不同版本若需同时服务多个数据库，必须解决符号、静态状态和依赖冲突。第一阶段可拒绝同进程激活互不兼容的 native 版本，给出明确错误；SQL 对象版本更新不能自动意味着 native 热替换安全。

## 4. SQL 对象由插件决定，通过统一 catalog 路径创建

采用 `<name>.control`、`<name>--<version>.sql` 和版本化更新脚本。control 管理 SQL 包版本、依赖、对象命名空间等；binary manifest 管理 ABI 和入口，两份元数据共享字段由打包工具生成并校验，避免双重真相。PG 的 Extension 通过安装脚本创建数据库对象并记录成员关系，更新也使用 SQL 脚本。[PG Extension](https://www.postgresql.org/docs/18/extend-extensions.html)

建议管理语法：`CREATE EXTENSION`、`ALTER EXTENSION UPDATE`、`DROP EXTENSION RESTRICT/CASCADE`。这些是拟议新增能力。现有 `INSTALL PLUGIN` 如果保留，必须明确是哪个作用域的别名，不能在多数据库场景含糊映射。

插件应有三种注册方式，最终收敛到同一个 object manager：

1. 安装 SQL：调用正常的 CREATE FUNCTION/TYPE/CAST/OPERATOR 等 DDL；也能创建配置表、视图和以 SQL 组合的功能。
2. SDK 生成 SQL：Rust attribute 或 C/C++ 辅助工具生成可查看、可修改的安装脚本；不强制手写大量 descriptor。
3. 运行时 catalog API：提供 `CatalogContext`、对象 builder 和事务绑定接口，让插件按运行时输入创建对象。默认记录为插件管理的动态对象，只有显式处于安装/更新上下文或调用成员管理接口时才成为 extension member。

三条入口统一处理 object ID、权限、名称解析、依赖、schema invalidation 和事务。允许插件通过专用 catalog API 注册对象，不开放任意系统表内存修改，也不另建绕过 schema service 的直接 INSERT 通道。

函数的 catalog binding 可表示为 `module + entry_name + calling_convention`；entry_name 可以解析为导出符号或 manifest 中的函数表项。继续支持现有 service ID，但普通 SQL 函数无需被迫设计为一个跨插件业务 service。单一导出不是自由度的必要条件；使用多符号时按声明清单审计，而不是禁止一切额外导出。

补充类似 PG SPI 的服务端 SQL API：参数化 prepare/execute、result cursor、可用阶段、事务参与方式、错误和取消。当前用户身份与事务默认继承，后台任务必须创建自己的 session/transaction。ABI 层返回错误状态；插件不能从任意线程复用查询 context。[PG SPI](https://www.postgresql.org/docs/18/spi.html)

SQL 和 catalog 开放仍然只能使用内核已实现的对象类别。PG 本身也依赖预先实现的 fmgr、AM 和 hook 协议。新业务函数通常不应改核心；引入全新物理算子类别或存储协议，仍可能需要增加通用接口。

## 5. 两档 native API，允许深度扩展

| Profile | 推荐用途 | 接口与兼容策略 |
| --- | --- | --- |
| Public | 文本处理、embedding/rerank provider、普通 UDF、连接器 | C ABI、typed batch、opaque context、公开 catalog/SQL API；按已验证接口版本维护兼容 |
| Server-dev | 新索引、新计划路径、执行器替换、类型统计、系统观测 | 允许使用版本绑定的 server headers 和真实 C++ context；按精确 build/toolchain fingerprint 重编译与验证 |

两档可以共存于一个包。采用 server-dev 不意味着所有接口都必须变为 C++；它表示允许依赖更丰富、变化更快的接口，放宽兼容承诺。

Public profile 也不承诺未经验证的跨版本 ABI。Server-dev 首阶段应校验编译器/标准库/布局配置等影响 C++ ABI 的信息，不能只比较版本字符串。Rust 通过相同版本的 C++ bridge 操作这些对象，不直接推断 C++ class 布局。

PG 允许扩展使用 server API，并要求主版本之间重编译；其明确接口和大量内部接口的稳定程度不同。这支持“给深度扩展提供版本绑定入口”的设计，而非要求所有能力先冻结成长期 ABI。[PG C API/ABI](https://www.postgresql.org/docs/18/xfunc-c.html)

边界检查随 profile 调整：Public 保留现有私有依赖隔离；Server-dev 允许声明的 host API 和 headers。两者都不把核心静态库复制进插件，避免重复单例和状态；都检查 core 不依赖 GIS/模型/算法实现。Server-dev 的链接可用显式 host interface table 或专门导出接口，无需默认导出整个进程所有符号。

## 6. 真正借鉴 PG 的扩展点

| 扩展点 | 插件应能控制的行为 | AI/轻量化用途 |
| --- | --- | --- |
| Function/Aggregate | 标量/批量执行、状态合并、结果类型、volatility/cost | tokenize、embedding、rerank、聚合检索结果 |
| Type/Operator | input/output、binary codec、typmod、compare/hash、cast | sparse vector、tensor、geometry、自定义相似度语义 |
| Index AM/支持函数 | build/insert/delete/scan/recheck、成本与统计契约 | 新 ANN 算法、混合检索、空间索引；需适配 seekdb LSM/MVCC |
| Planner/Custom operator | 提交备选 path、估算代价、生成并执行自定义 plan | GPU 批处理、远端检索、融合算子 |
| Executor hooks | before/after、instrumentation、cancel、finish | 查询统计、模型调用观测、执行策略 |
| Data source | schema discovery、scan、projection/filter pushdown、rescan/cancel | 文件、对象存储、外部 API 数据接入 |
| Background task/Config | 注册任务、资源限额、连接与模型配置 | 增量 embedding、模型缓存和异步索引维护 |

这些是目标接口，不表示当前八类 descriptor 已有上述执行能力。PG 的 Custom Scan 允许扩展在规划时提出路径，并在选中后生成、执行相应计划；值得作为 AI 执行扩展的参照。[PG Custom Scan](https://www.postgresql.org/docs/18/custom-scan.html)

Hook 区分 observation、around 和 replacement 三种模式。observation 只观察；around 通过显式 next 调用后续；replacement 可以接管，但必须声明可替换阶段。不能要求所有 hook 永远调用 previous，否则真正替代计划的能力会被限制；也不能允许无意截断链。注册规定顺序、冲突处理、可重入性、context 生命周期、线程模型。

type 扩展不应永远停留在 BLOB codec；必须与比较、索引、统计、类型参数和协议显示协作。优先参考 PG operator class 将某种类型的支持函数接入已有索引框架的方式；seekdb 仍需先提供自己的索引可扩展框架，不能原样使用 PG GiST 结构。[PG Index AM](https://www.postgresql.org/docs/18/index-api.html)

通用 operator token/函数语法应满足大部分扩展。特殊领域语法可后续做明确的 grammar integration；不把任意运行时替换 parser grammar 设为早期目标，PG 式扩展自由度也不以此为前提。

## 7. 轻量化与 AI 执行契约

文件可用、SQL 对象安装、动态库装载、模型初始化分开。纯 SQL 包不加载 native runtime；普通函数按需装载；全局 hook 使用 preload；模型、GPU context 和连接池再惰性初始化。首次装载采用 single-flight，避免并发请求重复初始化。

需要提前注册的全局 hook 必须 preload；类型恢复、索引恢复等如果依赖插件，在对应对象使用或恢复前加载。缺失 optional AI provider 时，对依赖它的查询报错，管理和无关 SQL 继续可用；缺失恢复必需模块时仍可阻断相关恢复/启动。不能把已有持久格式降级解释为普通字节并继续计算。

计划绑定时完成名称与入口解析，执行/cursor 持有对应 module 引用。频繁调用按批次执行，输入借用只读 buffer，结果使用 host allocator 或带 release callback 的 buffer。释放回调本身也固定 module 生命周期。只有布局一致时才做到零拷贝，LOB materialize 和布局转换的成本需要测量。

Arrow C Data Interface 可作为批量交换的候选；接口定义本身不要求核心链接整套 Arrow。先覆盖与 seekdb 当前向量布局容易映射的类型，不承诺所有类型零拷贝。[Arrow C Data Interface](https://arrow.apache.org/docs/format/CDataInterface.html)

AI 函数还需要显式定义：模型版本、输入上限、批量大小、deadline、取消、内存/并发预算和副作用属性。远端模型调用默认不视为 immutable；由插件声明的 volatility、权限、重试/幂等策略参与计划和执行。数据库回滚不能撤回已发生的外部调用。

远程模型 I/O 优先使用受控任务/调度接口；跨异步边界不能持有借用的 query/transaction 指针。后台数据写入通过新事务进行。避免每个插件无条件创建独立大线程池；确有专用 runtime 的插件必须报告线程和资源预算。

卸载分两档：声明支持 stop/drain 的模块可逻辑停用；深度 hooks、后台资源复杂的模块可以声明 restart-required。物理 dlclose 继续推迟到可证明安全的边界，不把热卸载作为插件准入条件。

轻量化验收同时测量 core/package 体积、冷启动、空载 RSS、线程数、未使用插件的开销、首次调用延迟、批量吞吐、取消延迟和长运行后的资源释放。拆成 `.so` 本身不是这些指标改善的证明。

## 8. 最需要先解决的事务问题

PG 安装/更新脚本在事务中执行，并限制显式事务控制和不能在事务内执行的命令。[PG 安装脚本事务规则](https://www.postgresql.org/docs/18/extend-extensions.html)

seekdb 现有命令执行会按 `cause_implicit_commit()` 提交，部分 DDL 还涉及 schema service 与异步任务。关闭一个 implicit-commit 分支不足以使这些操作整体可回滚。当前 package install 也先提交 catalog 登记再 load，不能直接视为 PG CREATE EXTENSION 的原子性。

建议单独建立 ExtensionInstallContext：绑定安装身份、owner、命名空间、事务和 provisional object set。先将 function/type/cast/operator 等纯 catalog 操作接入同一事务，安装内部能解析自己刚创建的对象，其他会话看不到。创建类型的 shell 和相互引用也在该上下文解决。

对 table/index 等包含实体创建的 DDL，需要证明 storage task、schema publication 和回滚可协作，才能纳入“原子安装支持集合”。第一阶段对尚不支持的命令在预检中明确拒绝；不能运行一半后用逐条 DROP 补偿却称为完整事务。

module 可提前验证、装载，但 provisional SQL 对象和 hook 对外发布要与事务可见性协调。安装回滚允许无害的代码映射残留，不能残留可见 SQL 对象或已启动的外部任务。提交之后再启动任务时，启动失败作为运行状态处理，不能声称已提交事务被撤销。

长期目标是逐步扩大正常 DDL 的事务支持范围。若这一层不改造，安装 SQL 只能提供部分 PG 语义，自由度和失败可恢复性都会受限。这是本方案最大内核工作量，Rust 不会消除它。

## 9. Rust 的三个层次

### 9.1 Rust 编写插件：可行，优先实施

现有公开 C ABI 可以作为接入基础。Rust 插件生成 `cdylib`，使用 `extern "C"` 和 `#[repr(C)]`；crate 内可以使用 traits、Vec、String、async，跨动态库只传明确定义的 ABI。Rust `dylib` 面向 Rust 链接，语言间加载应选 `cdylib`。[Rust linkage](https://doc.rust-lang.org/reference/linkage.html)

建议 SDK 由三部分组成：`seekdb-sys` 提供低层 FFI；`seekdb-extension` 提供 context、datum/batch、SQL/catalog、hook 和资源包装；`cargo-seekdb` 提供 new/build/schema/test/package。名称均为设计提案，不是现成工具。

参考 pgrx 的 attribute、类型映射、自动 schema 生成、手写 SQL 混合和测试体验。pgrx 已证明 Rust 可以实现 SQL 函数、类型和深层 PG hook，但它依赖 PG 的 API、Datum 和错误处理，不能直接作为 seekdb SDK 使用。[pgrx 项目](https://github.com/pgcentralfoundation/pgrx)

Rust 宏只能生成 FFI wrapper 和对象声明；对应内核类别和调用点仍需 seekdb 实现。ABI 通过布局/offset/calling convention 测试核对，不能仅凭 `repr(C)` 就认定跨平台兼容。

### 9.2 Rust 实现插件管理器：可行，适合模块化推进

package/control 解析、版本依赖图、module 状态、回调注册和资源 token 适合用 Rust 实现。SQL parser/resolver、schema service、MVCC、存储恢复和热执行入口继续由 C++ 实现，经窄 bridge 调用。整个 catalog 写入协议只有一个事务协调者，Rust 不另开 SQLite 或复制一套数据字典。

既有 `sql-nio` 为 staticlib。新增 host Rust 组件宜用 rlib crate，由一个 host aggregate staticlib 统一链接到 C++，避免直接堆叠多个携带 Rust runtime 的 staticlib。最终静态链接和 panic 策略需统筹；官方文档专门提示多个 Rust staticlib 的潜在冲突。[Rust 混合链接](https://doc.rust-lang.org/reference/linkage.html#mixed-rust-and-foreign-codebases)

推荐先实现 Rust 插件及 SDK，稳定 Extension/catalog/深层 hook 契约，再逐块迁移 runtime 内部算法。若选择立即用 Rust 重写管理器，也应先定唯一事务所有者和桥接接口，以现有 C/C++ 插件行为作为等价性验收，不并行维护两套 registry。

### 9.3 Rust 深度参与 C++ 内核：可行，需要版本绑定桥接

对 planner/query/index 对象提供 C++ adapter 或同构建版本的 CXX bridge。Rust 使用 `QueryContext<'q>`、`CatalogContext<'txn>` 等有生命周期的 wrapper；mutable wrapper 必须确保独占写权限，不能凭一个裸指针制造 `&mut` 并默认 Send/Sync。[CXX](https://cxx.rs/)

CXX 适合受同一构建控制的 Rust/C++ 边界，不代替对独立插件二进制的 ABI 版本管理。C++ server-dev 插件可直接用公开给该 profile 的真实类/指针；Rust 模块通过 adapter 达到相同能力，不能指望 bindgen 安全生成任意 C++ 类布局。

## 10. Rust 错误与资源模型

当前 workspace release/cmake-debug 是 `panic=abort`，catch_unwind 无法捕获这种 panic。插件若需要把 panic 转成查询错误，应有独立 plugin workspace/profile，使用可 unwind 构建并在每个 FFI 回调入口内部捕获；host 的 sql-nio 策略不能被全局悄悄修改。[Rust FFI 与 unwind](https://doc.rust-lang.org/nomicon/ffi.html#ffi-and-unwinding)

正常业务错误用 Result 转 status；C++ bridge 在返回 Rust 前处理 C++ 异常，异常和 panic 不跨越 C ABI。捕获 panic 不代表插件状态仍可使用：query-local 状态可销毁，受影响的共享模块状态需要标记失败并停止新调用。`catch_unwind` 不处理 abort、内存损坏或所有 OOM 情况；Rust native 插件不是沙箱。

host-owned 和 plugin-owned allocation 分清，由实际分配侧释放。Rust 的 Drop 只覆盖被正确持有和销毁的 Rust 对象；query error、取消和游标未读完时还需 host resource owner 主动 close/cancel。初始化、正常完成、执行失败、取消、shutdown 都要有资源路径。

不要把每个插件的全局 allocator 简单映射成当前 query arena：插件静态数据、线程和异步任务可能比查询活得更久。显式提供 query allocator 和 module allocator；插件内部堆使用独立的资源核算办法。借用数据跨任务必须复制或获得可跨线程的 owned buffer token。

## 11. 用三个纵向样例验证自由度

| 顺序 | 样例 | 验收结果 |
| --- | --- | --- |
| 1 | Rust 文本处理插件 + 一个纯 SQL 组合包 | package/schema 生成、SQL 安装、函数重载、参数化 host SQL、取消；安装失败无可见半对象；新增函数不改核心 factory |
| 2 | 查询观测/自定义 path 插件 | 能读查询上下文、设置 instrumentation、贡献 path、参与 EXPLAIN；多 hook 组合及并发查询语义明确 |
| 3 | GIS 或一个 ANN 索引插件 | 自定义类型、比较/距离 operator、统计、索引 scan/recheck、持久依赖与恢复；核心不链接算法包 |

每阶段同时测试两套构建入口和有意义的平台 ABI；无插件 core baseline 与加载插件后的批量吞吐、RSS 都留可复现结果。当前 runtime/catalog 测试目标在既有文档中有描述，但本分支未找到对应定义，实施前需要建立真正可运行的验证链。

后续引入模型 provider、数据连接器和后台任务，重点验证批量、网络取消和资源预算。外部模型结果不作为确定性的基础回归 oracle，可用确定性本地 fixture 校验调度与错误语义。

## 12. 建议先确认的设计取舍

建议方向已明确：允许版本绑定的深度 native API；SQL 脚本和 catalog API 统一对象管理；module 与 SQL 对象身份分离；Rust 优先覆盖 SDK/插件，runtime 可逐步迁移；普通模块 lazy、全局 hook preload；支持 restart-required 模块，不强制热卸载。

实施前最大的两个待验证问题是 Extension 安装事务的可支持范围，以及 planner/index 回调最小集合能否支撑一个真实插件。先以三个纵向样例验证这些边界，再冻结接口，比继续增加抽象 descriptor 更能推动 AI 扩展生态。

参考既有材料：[原设计](https://yuque.antfin.com/obopensrc/tignua/timw7t5luoyqvrdv)、[此前对比](https://yuque.antfin.com/obopensrc/tignua/xx69d23h2d9w87gw)。本文有意调整此前“所有插件只能通过窄稳定接口”的建议，以满足轻量化与高扩展性并重的新目标。
