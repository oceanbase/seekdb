# Extension 包源：Rust control、安装与更新 SQL 读取

这是安装/更新链路的源文件层，不是完整 Extension 生命周期实现。Rust 负责读取、校验和
持有包源；C++ 保留 SQL parser/resolver、权限、schema 与事务实现。读取成功不执行
SQL、不启动事务、不装载动态库，也不授予包任何权限。

## 文件布局与版本选择

新安装采用 PG 风格的平铺目录；管理员通过 `--extension-dir` 指定根目录：

```text
<root>/
  text_ops.control
  text_ops--1.0.sql
  text_ops--1.0--1.1.sql
  text_ops--1.1.control   # 可选，覆盖主 control 中该版本的字段
```

兼容读取原有 `<root>/text_ops/text_ops.control` 子目录布局，但同名主 control 在
两种布局中同时存在时拒绝读取，不静默选择其中之一。损坏的平铺 control 也不会
回退到旧包。升级既有部署时由管理员移走旧包目录；安装过程不自动删除历史文件。
仓库源码仍按包分目录组织，CMake 安装产物改为平铺。

`read_extension_package(root, name, requested_version, source, error)` 是核心 C++
入口；空版本选择 control 的 `default_version`，非空版本选择精确的目标版本。
Rust 将 `name--version.sql` 作为基础安装脚本、`name--from--to.sql` 作为有向更新边，
选择从任一基础版本到目标版本所需脚本最少的路径。目标有直接安装脚本时优先使用它。
不隐式选择“最大版本”，不解释 semver 或数字大小；显式降级边也参与搜索，环不会
造成死循环。同长度路径按基础版本、后续版本的字节序稳定选择，不依赖目录枚举顺序。
该最短路径规则参考 [PG 更新与链式安装](https://www.postgresql.org/docs/18/extend-extensions.html#EXTEND-EXTENSIONS-UPDATES)。

返回的 `ExtensionPackageSource::scripts_` 按顺序保存每个文件的 from/to 版本和
独立 SQL 字符串，`version_` 是最终目标版本。基础脚本的 from 为空，其后每项的
from 必须等于前项的 to。不能把文件拼成一段 SQL：尾部注释、字符串和未闭合语句
不得跨文件改变词法边界。C FFI 的旧单文件 SQL 字段对多文件计划返回错误，不截取
基础脚本假装完整成功；新脚本计数和逐项字段接口由 C++ adapter 消费。

### 从已安装版本读取更新

`read_extension_update(root, name, installed_version, requested_version, source, error)`
已通过 C++ adapter 调用 Rust `seekdb_runtime_package_read_update`。起点必须是非空、
合法的已安装版本标签；目标为空时仍使用 control 默认版本。只从这个起点搜索更新边，
不能跳到另一个基础安装脚本，即使目标版本恰好提供更短的直接安装脚本。
更新不需要起点的基础文件；只交付 control 和所需更新文件也可以形成更新计划。

`ExtensionPackageSource::from_version_` 保存预期的已安装版本，新安装时为空。
更新计划首项的 from 等于它，其后仍要求前后版本连续，最后 to 等于目标版本。
C FFI 字段 `SEEKDB_RUNTIME_PACKAGE_FROM_VERSION` 返回该身份，C++ 会再次核对。

相同起止版本返回零脚本计划，不伪造 SQL，也不要求安装/更新 SQL 文件存在。
这不是数据库已处于该版本的证明：更新协调器仍须锁定安装记录，核对安装 ID、
当前版本与权限后才能报告 no-op；读取器不读取或修改任何数据库 catalog。

更新文件允许空白、注释或零字节，表达无需改变对象的版本边；缺失文件仍是错误，
不把“没有找到更新路径”当作空更新。基础安装文件仍不得为空白。no-op 仍读取并
校验目标版本 control，但不收集任何中间步骤的临时依赖。

这已扩展 CREATE 的包源选择并提供独立更新源，但不是 `ALTER EXTENSION UPDATE` 数据库执行实现。
当前顺序 adapter 支持 routine 安装/更新，允许显式关联已加载 native module；
具体事务、成员及验证边界见 [安装进度](plugin-extension-install.md)。
每一步采用主 control 加上该目的版本的可选 control，不继承上一个版本的覆盖值。
未选路径的 SQL/control 不执行也不读取正文；选定文件
缺失、不可读或校验失败时直接失败，不静默改走另一条迁移路径。

包名和版本按原样使用，不修改大小写。它们必须为 1–255 字节，首字节为 ASCII
字母或数字，其余只能为 ASCII 字母、数字、点、下划线、连字符；禁止 `..` 和 `--`。
这限定文件布局，不是 SQL identifier 的命名规则。

## control 格式

```text
default_version = '1.0'
comment = 'Pure SQL text functions'
relocatable = true
superuser = false
# requires = 'base_text, utility'
# native_module = 'seekdb.text'
```

这是 seekdb 当前支持的 control 子集，不是完整 PG control 兼容，也不是 TOML。
每行一个赋值；允许空行、整行和行尾 `#` 注释。字符串必须单引号包围，两个连续
单引号表示一个单引号；字符串内部的 `#` 是普通字符。没有反斜线转义或多行字符串。
`relocatable` 只接受未加引号的 `true`、`false`，默认 false。
`superuser` 同样只接受未加引号的布尔值，默认 true。

| 字段 | 读取语义 |
| --- | --- |
| default_version | 默认安装版本；缺省时调用者必须显式选择版本 |
| directory | 可选脚本及次级 control 目录，默认主 control 所在目录；相对路径相对于主 control，绝对路径也必须位于管理员配置的可信根目录内 |
| module_pathname | 可选 `MODULE_PATHNAME` 文本替换值，最多 4096 字节；按每一步目的版本的有效 control 替换，不自动加载库、不等同于 native_module |
| native_module | 可选的现有 native catalog 逻辑 ID，不是文件路径；只能为小写 ASCII 字母、数字、`._-`，1–255 字节；省略表示纯 SQL 包 |
| install_source | 缺省或 `'sql'` 使用基础 SQL；`'native'` 由安装回调提供全部对象，必须有 native_module 和 default_version；当前只支持直接安装 default_version，更新仍使用显式 SQL 边 |
| schema | 可选固定命名空间，1–255 字节 UTF-8；后续须由 seekdb 名称解析层解释，不等同于 PG schema |
| relocatable | 是否允许安装位置可变；true 与固定 schema 互斥，读取本身不执行对象搬迁 |
| superuser | 安装／更新是否要求 SUPER；默认 true。false 使用调用者本身的权限，逐条 SQL 仍须通过正常权限检查，绝不切换为管理员 |
| requires | 逗号分隔的包名，最多 64 个；拒绝空项、重复和自依赖；仅返回声明，不验证依赖已经安装 |
| comment | 描述性字符串，当前不返回、不持久化，不影响权限 |

重复键、未知键、控制字符与 NUL 被拒绝。包括 `trusted` 在内的
未实现选项不能被静默接受。仅配置 `module_pathname` 时对 SQL 中的
`MODULE_PATHNAME` 作 PG 风格逐字替换（包括字符串和注释），不是参数绑定或 SQL
转义；可信包作者负责完整 SQL 的合法性。未配置时保留原文。不对版本号、schema、
owner 或 native_module 作隐式替换。每个脚本仍独立解析，替换前后分别受包大小限制。
这只实现包源预处理，本身不赋予原生代码权限。原生 `AS ... LANGUAGE C` 声明现已
另行接入实验性 routine 创建和调用路径，参见
[native_math 参考包](../../../plugins/sql_packages/native_math/README.md)；其权限、类型、
依赖与实际安装验证不能由 `MODULE_PATHNAME` 替换成功来代替。

### 安装权限策略

对齐 [PG control 的 superuser 选项](https://www.postgresql.org/docs/18/extend-extensions.html)：
`superuser = false` 不是 `trusted = true`，不会提升执行身份，也不能绕过原生
`LANGUAGE C` 创建、routine、GRANT／REVOKE 的各自权限检查。Root 使用已认证
调用者的权限再次准入；SQL 层在调用安装回调前先检查包策略。

版本 control 可以覆盖主文件的 superuser 字段；选中的多段安装／更新路径只要
任一步要求管理员，整条路径就要求 SUPER。旧的起始版本不会重跑，其权限配置
也不会无故加入本次路径。策略随拥有数据的 source／请求传递并绑定校验，不把
它持久化为对象 owner，也不改变同版本 no-op 的身份和权限检查。

**默认行为变化**：以前没有独立的包级管理员策略，纯 SQL 包仅受对象权限限制；
现在省略 `superuser` 按 PG 默认要求 SUPER。已有允许普通用户安装的包应由可信
作者显式加入 `superuser = false`。随仓库交付的 text_ops、text_composed 和
Rust text SQL wrapper／builder 示例已显式配置；GIS 与 native_math 保留默认要求。

Rust 生成器可通过 `PackageOptions { superuser: Some(false), ..Default::default() }`
设置主 control，通过 `VersionControl.superuser` 设置版本覆盖；None 表示不输出，
沿用继承／默认规则。宿主 C 源结构未扩展：新增独立 policy 入口，旧入口保留
默认要求管理员的语义，避免把旧结构尾部 padding 当作权限标志。

### 版本 control 与迁移期间依赖

可选的 `name--version.control` 按字段覆盖 `name.control`。缺省字段继承主文件；
`requires = ''` 显式清空依赖，而不是继承或追加。次级文件不得设置 `default_version`
或 `directory`，其他字段仍使用同一语法、大小、路径和语义校验；主 control 本身
也必须有效。主 control 的 directory 决定脚本和次级 control 的位置，不允许次级
control 重定向目录。CLI 检查同一脚本目录内所有次级 control。

例如主 control 声明 `requires = 'base'`，基础版本覆盖为 `alpha`，中间版本覆盖为
`beta`，目标版本覆盖为 `gamma`。链式新安装返回永久 `requires_ = [gamma]` 和
临时 `prerequisites_ = [alpha, beta]`；从基础版本更新到目标只返回临时 `[beta]`，
因为起点脚本不会重放。目标没有覆盖文件时，最终依赖恢复为主文件的 `[base]`。

永久依赖与临时依赖分开传过内部 C ABI、C++ source、安装 spec 和更新 request。
两者合计最多 64 个不同名字，禁止交集、重复或自依赖。catalog 在执行前锁定两组
provider，但只将最终 requires 写成 Extension 依赖边；临时 provider 锁持续到事务
结束。普通 SQL 对象依赖仍有效，临时声明不会解除函数体等实际对象引用。

当前选中 SQL 路径中的 native_module/schema/relocatable 必须与目标有效 control
一致；跨步骤换模块或命名空间明确拒绝，不能由一份最终上下文掩盖差异。更新还需
核对已安装的 native 身份。native 新安装仍仅支持 default_version，零脚本 native
源和同版本 no-op 均不得声明临时依赖。

`cargo seekdb schema` 接受 generator 输出次级 control，并校验全部次级文件，
包括默认版本未选中的文件；普通服务器包读取只校验所选版本。Rust SDK 已提供
`PackageOptions::version_controls` / `VersionControl::new(version)`，生成次级文件；
`requires: None` 继承、`Some(&[])` 清空，也可声明 native_module/schema/relocatable。
字符串、依赖、重复版本、文件名及文件总数在写入前校验，现有文件不会覆盖。
安装源选择仍由主 PackageOptions 决定；路径上下文一致性和最终/临时依赖合计上限
由 CLI/服务器包读取器校验，而不是把所有无关版本的依赖合并限制。示例为
`plugins/rust_text/examples/seekdb_versioned_schema.rs`。这不是完整 PG control
或版本约束兼容。

## 资源、生命周期与信任边界

- control 最大 64 KiB，所选全部 SQL 合计最大 4 MiB，均要求 UTF-8、无 NUL 的普通文件。
  基础 SQL 要求非空白，更新边允许空 SQL；语法合法性由后续内核 parser 负责。
- 每个脚本目录最多枚举 4096 项（平铺时包含其他包的文件），版本图最多 1024 个版本，因此所选路径最多 1024 个文件；
  同包命名的 SQL 文件须符合基础/更新两种布局。无安装起点或无法到达目标时返回
  文件不存在错误；不把孤立更新脚本当作基础安装脚本。
- canonical path 必须仍位于包根目录内。包目录、control 和 SQL 的符号链接逃逸
  均拒绝；不能把“路径在根目录中”当作 SQL 或 native 代码可信性的证明。
- 根目录由管理员管理，读取期间必须保持不可变。canonical 校验不防御有权修改
  文件的管理员并发替换文件；多个文件的读取也不是可变目录上的原子快照。
  后续部署流程须完成可信产物发布，而不是允许任意数据库用户写入此目录。
- Rust 持有所有源数据。C FFI 返回 opaque handle，文本 view 只在 handle 存活时
  有效；销毁前必须结束所有借用。C++ 使用 RAII 释放 handle，并复制为自身拥有的
  `ExtensionPackageSource`，返回后不保留 Rust 字符串指针。
- C++ 失败时不返回半份 source；错误映射为参数、文件不存在、I/O 或内存错误。
  diagnostics 尽力提供，内存分配失败不保证还能生成消息。host 仍使用既有 panic
  策略；这不是 native 沙箱，也不承诺所有分配失败都可恢复。

## 交付和当前验证

`plugins/sql_packages/text_ops` 是不含动态库的源文件示例；实验插件开关开启时，
CMake 顶层 `plugins` 安装组件将文件平铺放入 `${CMAKE_INSTALL_DATADIR}/seekdb/extension`。
纯 SQL 子目录不继承 native 插件的 `EXCLUDE_FROM_ALL`；无需构建 GIS/模型动态库
就能交付 SQL 文件。可以执行 `cmake --install build_release --prefix <目录> --component plugins`。
复制文件不等于创建数据库 Extension，也不触发任何 native 初始化。
默认版本仍为 1.0；显式 `CREATE EXTENSION text_ops VERSION '1.1'` 选择基础文件和
新增 `seekdb_is_empty` 函数的更新文件。该命令链路的真实数据库回归仍待执行，
不能据此声称已安装的 1.0 实例可以使用 ALTER 升级。

Rust 单元测试覆盖语法、模块 ID、字段借用范围和错误输出；独立 CTest
`plugin_package_source` 通过生产 C++ adapter 调用真实 Rust 文件读取器，覆盖默认/
显式版本、源文件复制后的生命周期、路径逃逸、UTF-8/NUL/大小限制及实际示例包。
新增覆盖多文件版本链、直接安装优先、不可达路径、更新脚本逃逸/无效内容/合计大小
限制、版本 control 覆盖/显式清空/独立继承、临时依赖与最终依赖分离、非法 control
及路径逃逸拒绝，以及 Rust handle 销毁后更新文本仍然可用。
独立更新入口还覆盖不重放基础 SQL、不能用目标基础脚本替代更新边、显式降级链、
无起点基础文件、空更新/no-op、非法起点与失败清空。最新执行状态见实施进度。
这些测试没有执行安装 SQL，不能证明 routine 安装、数据库权限或回滚语义。

## 内核脚本解析桥接

### 内存声明入口

除文件包外，宿主可调用 `ExtensionScript::load_source(ExtensionPackageSource, mode, error)`。
源包含明确的包名、起止版本、native module、namespace/依赖，以及已经选定且有序的
SQL 片段。它不依赖临时 SQL/control 文件，也不自行读取目录或重新选择版本路径。
这个源构造接口本身是 host-only。native 插件现在可通过安装期 `.catalog.install`
服务提交附加 SQL，复用相同的 Rust 校验与内核解析入口；没有向插件暴露 session、
Root command、直接系统表写入或权限绕过能力。详见 [安装期 CatalogContext](plugin-catalog-install.md)。

`seekdb_runtime_package_from_source` 在 Rust 中校验并深复制输入，复用现有 owned
Package 模型及文本读取/销毁接口。C++ admission 调用同一实现，检查输入身份、UTF-8、
NUL、依赖重复/自依赖、固定 schema 与 relocatable 冲突、版本链连续性、脚本数和
合计 4 MiB SQL 限制。SQL 基础源不可为空；显式 native 基础源必须为零脚本，准备
后必须产生实际语句；同版本更新必须是零脚本；不同版本更新允许
空 SQL 边。调用者提供的是已选定计划，不是版本图，也不是安装身份/权限证明。

文件入口与内存入口使用同一个 parser helper。每个片段独立拆分/解析，不能把尾部
注释或未闭合 token 接到下一片段；总计最多 4096 个拆分项。输入先复制再替换旧状态，
支持 `load_source(script.source(), ...)`；任一验证/解析失败清空旧树、源和 SQL mode，
不返回部分可安装结果。调用者后续修改原始声明不会改变解析器持有的源和 SQL view。

解析后的源继续进入原有 ExtensionRoutineResolver/Root 协调器：权限、成员关系、
依赖、事务和 schema 发布规则不因源来自内存而改变。当前普通查询回调不能借此执行
DDL；安装期服务已持有模块租约，但查询期 CatalogContext 的会话绑定及事务写入尚未接通。内存更新源可以
校验/解析，但 ExtensionUpdatePlan 的正常观测与准备入口目前仍从文件包读取。

验证覆盖 Rust source 语义、真实 C++→Rust 布局/输入、内核解析错误清空、自别名与
输入独立持有，以及用内存动态构造的 native-backed routine 进入真实 PL resolver。
原有文件包用例保留。Root/catalog 事务仍使用受控 fixture，不是实库提交/回滚证据。

`append_catalog_declarations` 在基础源之后追加一次安装期声明，统一重新解析；失败
清空全部状态。`source()` 保持原始静态包身份及脚本，`statements()` 才包含追加后的
完整解析结果。因此追加后再次 `load_source(script.source(), ...)` 是重新加载基础源，
不会复制动态声明。适配器必须持有返回的 `ICatalogDeclarations` 至安装与发布完成。

### 文件包解析与执行边界

`sql::ExtensionScript` 已调用上述 C++→Rust 包读取器，再调用真实 `ObParser`。
它拥有源文件字符串与 parser arena，保存各语句 SQL view 和 ParseNode；reset、
再次 load 或析构会使这些 view 失效。输入不能别名到对象自身先前返回的 source。
包文件固定按 UTF-8 解析，SQL mode 由调用者显式传入，不修改 session 状态。

脚本拆分复用内核 PL-aware 路径，函数体中的分号不按字符串简单切割。
除 splitter 返回码外还检查 `ObMPParseStat`；后半段解析失败时，不能把成功的前缀
当作可安装脚本返回。每个文件分别拆分、完整解析，每条语句构造语法树；整个路径
合计最多 4096 个拆分项，不因分散到多个文件而绕过上限。任一文件失败清空所有源
数据与语法树；路径允许仅含注释的某一步，但整个安装至少要有一条有效语句。

`ExtensionScript::load_update` 已使用独立更新源，并逐文件调用相同的内核 parser。
更新不强制至少有一条语句：空更新或 no-op 可保留完整版本信息而返回空语法树集合。
`ExtensionRoutineResolver::resolve/install` 明确拒绝带 `from_version_` 的更新计划，
不能复用新安装 adapter 将它登记成第二次安装。后续更新 adapter 需要在同一事务中
处理成员增删改与版本写入；解析成功的 DROP/CREATE 树本身不代表已完成这些操作。

这一层允许解析正常内核语法，不把可扩展对象限制为现有 descriptor 类别；但是
**解析成功不授权执行**。例如 CREATE TABLE 的语法树可被读取，但其物理 DDL 还
没有接入 Extension 原子安装支持集合，安装预检必须明确拒绝，不能先执行再补偿。
`DELIMITER` 是客户端指令，不是服务器安装脚本语法。resolver→Root command→schema
adapter 的核心调用链与用户 CREATE 命令执行端已接线；端到端行为仍待数据库回归。

独立真实内核解析回归入口（Linux CMake，需要先完成最新生产构建）：

```bash
cmake --build build_release --target seekdb -j2
python3 rust/plugin-runtime/tests/kernel_script.py --build-dir build_release
```

此脚本复用生产工具链和链接参数，仅替换 main，连接真实 parser、包 adapter 和
Rust host；还会调用顶层 CMake install，将 plugins 组件安装到私有临时目录，再
解析实际交付的 text_ops 文件。它不启动监听服务，与使用 runtime/catalog 替身的独立 CTest 分开，不能
用其通过证明 schema 事务已经正确。

当前多文件回归已在最新完整生产构建后通过：包含顶层安装产物、跨文件注释/未闭合
语句边界、4096 拆分项合计限制，以及新增关键词对 VERSION()/extension()/@@version
和标识符的兼容性。CREATE command 默认/显式版本的正常构造与源数据独立持有也
已验证；这不替代真实数据库 session 下的 routine 解析、授权与安装事务回归。

## 条件构建与验证入口

SQL 编译归属统一维护在 `src/sql/sql_source_inventory.bzl`。新增 SQL 源码时必须
更新真实 owner 列表，不要仅在某个构建入口手工添加文件或给 ownership 检查加豁免。

- `SQL_GIS_PLUGIN_ADAPTER_SOURCES`：关闭 `SEEKDB_ENABLE_CORE_GIS` 时编译的适配层。
- `SQL_EXTENSION_RUNTIME_SOURCES`：开启 `SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS` 时
  编译的安装脚本/解析桥接层；同一 helper 为 SQL target 定义实验插件宏。
- `SQL_CORE_GIS_REPLACED_SOURCES`：关闭 core GIS 时排除的完整路径引用，必须指向
  已有基线 owner，不是另一份编译列表。CMake emitter 与 Starlark validator 都校验。

构建检查已纳入独立 runtime CTest，无需启动数据库即可运行：

```bash
cmake -S rust/plugin-runtime/tests -B build_release/plugin-runtime-tests
ctest --test-dir build_release/plugin-runtime-tests -L plugin_build_boundary --output-on-failure
python3 tools/module_check/sql_source_ownership_check.py
```

标签包括生成 schema 清单、SQL 清单、四种条件配置、kernel runner 构建前置检查和
Rust Cargo 边界测试。四种配置测试只执行 CMake configure 并检查源码/宏选择，
不是四种完整服务器构建；Bazel 目前共享 SQL 清单及校验，实验插件 runtime 的完整
Bazel 构建尚未验证。

前述 `kernel_script.py` 仍是单独的 opt-in 回归，必须先完成同一构建目录的完整
`seekdb` 构建。它核对实际 SQL 实验宏、对象与链接元数据，允许编译动作未改变的
合法 no-op 构建，但不允许用更新时间替代编译。元数据检查既不是全部头文件依赖的
证明，也不代替真实数据库的事务、权限、回滚和并发测试。

## 普通 routine resolver 桥接

新增核心类 `ExtensionRoutineResolver`，接收 `ExtensionScript`、host 绑定的 runtime
services、真实 `ObSqlCtx` 和目标 database ID。它调用正常 `ObResolver` 的函数/
过程 resolver，生成可供 `install_routines_extension` 使用的 `ObCreateRoutineArg`
数组，不让 Rust 自行生成内核 schema 参数或解释 SQL 类型。

- 每条语句解析时拥有独立 arena、statement/expr factory 和 query context。成功
  后通过 `ExtensionRoutineBatch` 保存现有 DDL wire 表示，再销毁所有解析对象；
  返回参数不再依赖这些 arena 或 schema guard，失败时不返回半个数组。
- 只复制 host runtime services，resolver params 从正常默认值新建；不继承外层
  prepare、PL namespace、restore 或权限绕过标记。context 和 services 任一关闭
  权限检查都拒绝。caller 在 resolve 期间保持真实 session 和 schema guard 存活。
- 旧整批参数入口只接受新建纯 SQL FUNCTION/PROCEDURE，无 IF NOT EXISTS；
  它不承载 native 关联。顺序安装入口另以 ExtensionInstallSpec 绑定 native
  module 并交给事务 catalog 检查，不能用裸参数数组绕过。control 的固定
  schema 须与目标 database 一致；parser 可解析不等于已获执行授权。
- 继续调用正常名称、definer、类型与 PL route 分析，然后调用统一
  `ObPrivilegeCheck::check_privilege_new` 和密码过期检查；前者包含只读检查和正常
  CREATE ROUTINE 权限。对象的实际数据库 ID 必须等于目标数据库 ID。
- 文件是 UTF-8，普通 resolver 却使用会话解析字符集：先严格转换到
  `get_charsets4parser()` 指定的字符集再解析，不能把 UTF-8 原样当作 latin1 等
  文本。不可表示的字符须失败而不是替换。script 记录的 SQL mode 须与会话一致。
- 恢复 `ObResolver` 修改的 session/外层 SQL context 语句类型；不把 session 改为
  inner 来完成解析。普通 routine resolver 自身负责其临时数据库切换的正常恢复。
- 数据库名复制到批次 arena，不保留可变 session 数据库名的借用；DDL 审计文本
  转回正常系统字符集。由于已经是单条语句，不再调用默认 SQL mode 下的 splitter。

`resolve` 方法只做解析/权限检查和参数复制；`install` 方法复用 resolve，释放旧
schema guard 后再经 Query Root
command 调用批量 adapter 和 Rust/catalog 协调器。Root 方法内部串行化，不允许
调用者再持有同一个串行锁。用户已有事务明确拒绝；`requires` 在同一安装事务中
解析并锁定同库已安装 Extension，不自动安装，详见 [组合依赖](plugin-extension-install.md)。
native artifact admission、provisional 对象解析仍未实现，不能因返回参数就认为
整个包可以直接提交。提交后发布失败保留 extension ID 与独立 publication status。

## CREATE EXTENSION 执行端（接线完成，数据库行为待验证）

已新增独立的 `CREATE EXTENSION name [VERSION 'version']` parser 节点、statement
和 resolver；目前以当前 database 为目标，名称/版本/数据库名复制到 statement arena。
未选择数据库、prepared 模式、权限绕过和已有用户事务被拒绝。它按写入/DDL 分类，
但 parser 前置检查及 command 对象均不触发隐式提交；不能为了安装而提交用户事务。
初始包准入提取目标库 CREATE ROUTINE 权限，成员仍需正常 resolver/DDL 权限检查。

这不是完整 PG 管理语法；IF NOT EXISTS、SCHEMA 等仍待实现。基本 DROP
接线及其支持范围见 [安装与移除实现](plugin-extension-install.md)。
`CreateExtensionExecutor` 已连接包源、脚本解析和安装方法。只接受真实 host SQL
服务绑定的顶层调用，拒绝 inner/nested SQL、已有用户事务和目标数据库变化。
执行时重新进行准入权限检查；成员继续走正常解析和 Root DDL 的权限/冲突检查。
提交后发布失败返回成功并附带有安装 ID 的警告，不允许盲目重新安装。

`ALTER EXTENSION name UPDATE [TO 'version']` 已接入独立 parser/statement/resolver/
executor。省略 TO 时使用 control 的 default_version，不按版本字符串求“最大值”。
执行器先重新检查会话/密码状态，再释放旧 guard，通过认证 Root 读取源 ID/version，
交给 Rust 选择更新路径；Root 锁定后再次核对源身份和版本，逐条解析/准入更新。
不重跑基础安装脚本，也不在冲突后自动重读并重试。同版本 no-op 仍检查 owner，
返回 0 affected rows；版本变更返回 1。提交后发布失败保留成功结果并警告。
普通 owner 不被额外要求数据库级 CREATE/ALTER；实际脚本语句权限仍逐对象检查。
顶层、非 prepared、无活动用户事务的限制与 CREATE/DROP 一致；尚无实库回归证据。

启动时显式指定管理员控制的目录，例如：

```text
seekdb --extension-dir /absolute/path/to/plugins/sql_packages [其他启动参数]
```

也可指向 CMake 安装产物的 `share/seekdb/extension` 父目录。CLI 在进入 base-dir
之前解析相对路径；嵌入式调用可通过 `ObServerOptions::extension_dir_` 设置启动配置。
默认空目录表示不启用包发现，不按 SQL 参数访问任意目录，不自动复制/创建文件。
仍适用前文管理员不可并发替换包文件的信任契约。

命令分发保留初始 guard 到执行器的会话检查/包预检，然后释放旧快照进入 Root。
CREATE 与 UPDATE 使用 Root 提供的逐条演进视图；每条 DDL 参数经内核 wire codec
保存在回调拥有的独立 batch 中，所有字符串存储随命令存活。Root 使用新 guard
与普通 DDL 冲突检查，提交/发布不占用 Query 旧快照。

真实服务回归入口（只允许显式指定的可丢弃 localhost 实例，需 PyMySQL）：

```bash
python3 rust/plugin-runtime/tests/extension_install_server.py --port 2881 --confirm-disposable-server
```

密码通过 `SEEKDB_TEST_PASSWORD` 提供。脚本创建唯一 fixture 库/用户；成功仅清理
本次创建的对象，失败保留并打印名称。它不会配置/启动服务、部署包或改全局设置。
包含无隐式提交、普通用户授权、双数据库独立安装、更新 ID/版本/新旧成员保留、
同版本 no-op 和成员保护检查；**尚未执行**，
不能以脚本存在或 kernel 前端测试代替成功安装/事务/恢复的真实数据库证据。

## 接下来必须接通的层次

1. SQL 安装入口传入真实数据库/session 上下文，验证 package 与依赖可用性。
2. 已加入内核 parser 与普通 routine resolver 桥接；继续通过 Query→Rootserver
   command seam 将结果接到批量 routine adapter，并用真实会话验证成功和失败路径；
   不能按分号自行切割，不能把每条语句交给自动提交 executor。
3. 统一安装上下文连接 [schema 安装协调器](plugin-extension-install.md)，在同一
   DDL 事务中记录实际对象和成员，协调 provisional 名称解析和提交后发布。
4. 再接入更新/删除脚本、native artifact admission、完整依赖及 SDK SQL 生成。

上述范围仍属于整体设计目标，包读取器并未替代它们。
