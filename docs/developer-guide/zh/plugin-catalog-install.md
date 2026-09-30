# 插件安装期 CatalogContext

状态：实验性实现；2026-09-08。它是 [开放 SQL/catalog 设计](plugin-next-design.md)
中的安装期动态声明入口，不是完整 PG catalog API，也不是查询期任意 DDL。

## 能力与调用路径

native 模块可以根据安装包名、版本、数据库和 owner，在 `CREATE EXTENSION` 时
提交动态 SQL。插件自行决定对象名和 SQL 定义；宿主不需要为每个新增函数修改 factory。
文件 SQL、构建期生成 SQL、插件安装回调的声明均进入现有对象安装路径。

1. 宿主检查命令身份、权限、密码状态以及顶层、无活动事务的执行条件。
2. 读取已配置 Extension 目录中的 control；SQL 模式解析基础 SQL，native 模式只读取包身份。
3. 根据 control 的 `native_module` 查找可选安装服务，持有其模块 lease。
4. 同步调用插件 `prepare`，复制插件通过 `emit_sql` 提交的完整 SQL 片段。
5. 分别解析静态与动态片段，由 ExtensionRoutineResolver 和 Root 协调器执行正常
   对象权限、名称、依赖、成员关系和 schema 事务检查。
6. 安装及 schema 发布路径返回后释放 lease；错误路径也通过 RAII 释放。

`emit_sql` 成功只说明声明已复制/校验，不表示对象已创建或已获准创建。解析成功也
不代表该 DDL 属于当前原子安装支持集合。当前仍以已有 routine 安装能力为准；
不能通过提交 CREATE TYPE/TABLE/INDEX 文本获得尚未接通的对象类别。

## Public C ABI 与服务发现

公开头文件：`include/seekdb/plugin/catalog_spi.h`。

- 服务 ID 为 `<native_module_id>.catalog.install`，业务版本 major 1，当前 SPI 1.0。
- `seekdb_plugin_catalog_service_v1_t` 提供 `prepare(instance, context)`。
- `seekdb_plugin_catalog_context_v1_t` 提供 tenant/database/owner、包名/版本和
  `emit_sql(host_context, utf8_sql, length)`。这些身份来自宿主，不能由插件改写。
- module ID 必须与取得的 service lease owner 一致，仅伪造匹配的服务名不足以接入。
- 校验结构大小、保留字段、SPI 版本和回调指针；缺失可选服务时只安装静态 SQL，
  已找到但不兼容的服务则报错。显式 native 模式要求服务存在，不能降级成空安装。
  缺失服务不等于 native module 已被准许关联，
  后续正常安装仍检查其 ACTIVE 状态和持久关联。
- service ID 仍受 255 字节上限约束。module ID 加后缀超过上限时无法广告此服务，
  普通静态包安装不因此被禁止；native 模式则报不支持。

当前是 seekdb 单租户实现，tenant 固定为 1；database/owner 为有效正身份值。
每次安装上下文均独立，不把进程共享模块误当作数据库级对象实例。

## Rust SDK 用法

`seekdb-extension` 的 `catalog::{Installer, CatalogContext, Service}` 包装上述 ABI。
实现 `Installer::prepare`，使用 `Service::<YourInstaller>::V1` 作为 service table，
并在插件 manifest 的 provides 中广告约定的服务 ID。生命周期/instance 检查仍由
实现者负责；框架不能替插件推断其内部 STARTED 状态。

实际例子在 `plugins/rust_text/src/catalog.rs`：

```rust
impl Installer for TextCatalog {
    fn prepare(
        instance: *mut sys::Handle,
        context: &mut CatalogContext<'_>,
    ) -> seekdb_extension::Result<()> {
        validate_instance(instance)?;
        if context.extension_name() == "rust_text_ops" {
            let sql = scalar_wrapper(
                "rust_runtime_length",
                &count_function::DEFINITION,
                &[("input_text", SqlType::Text)],
                SqlType::BigInt,
                SqlAccess::NoSql,
            ).map_err(|_| sys::INVALID)?;
            context.declare_sql(&sql)?;
        }
        Ok(())
    }
}
```

此例使用 SDK 生成器复用实际 native 定义，生成 INVOKER SQL wrapper；作者也可以
直接提交完整手写 routine。metadata getter 允许针对不同包选择声明，但此版本没有
传入任意安装参数、返回新 object ID 或查询尚未创建对象的能力。

## 生命周期、错误和资源契约

- context、metadata 与回调指针仅在 `prepare` 调用内有效。回调同步、同线程，
  Rust wrapper 不实现 Send/Sync；不能保存借用或将其交给异步任务。
- 宿主调用插件时不持有 loader mutex。服务 lease 不仅覆盖 `prepare`，还覆盖
  后续安装/发布；临时 SQL 已深复制，不保留插件字符串指针。
- 单片段必须是非空、无 NUL 的 UTF-8，SQL 总字节数与基础脚本合计不超过 4 MiB。
  最多提交 4096 个动态片段，内核拆分后的静态/动态语句项总计也不超过 4096。
- 片段分别解析，不拼接。末尾行注释不会吞掉下一片段，未闭合 token 不能跨片段修复。
- 同线程提交错误为 sticky：忽略某次失败后再返回成功，仍导致准备失败。SDK 还将
  unwind panic 转为状态码；它不处理 abort、内存破坏，也不自动修复共享状态。
  错线程调用直接拒绝且不写入归属线程的状态，避免为记录错误引入数据竞争。
- 准备回调不得执行外部副作用、后台任务、查询或事务控制。此 API 本身不给这些
  能力，但 native 插件是可信进程内代码，不是可强制隔离外部系统调用的沙箱。
- SQL 定义应可根据同一包版本和身份重现，变更通过显式迁移描述。数据库回滚不能
  撤销插件自行发出的网络请求；不要用准备回调执行模型调用或建立外部资源。

## 当前边界与后续目标

已增加 [事务内 routine builder](plugin-catalog-builder.md)：新版服务 SPI 1.1 在
原 prepare/emit SQL 上追加 build 回调。Rust `TransactionContext` 可在同一安装
事务中逐对象预留 ID、更新 schema view 并继续构建；原 v1 prepare 布局不变。
这仍不是普通查询中的 catalog DDL 或完整事务控制 API。

现在支持只用 control 描述安装源，由 native 回调提供全部初始 SQL：

```text
default_version = '1.0'
native_module = 'org.seekdb.rust-text'
install_source = 'native'
```

`install_source` 缺省或 `'sql'` 保持基础 SQL + 可选附加声明；`'native'` 要求
native_module 和 default_version，fresh install 不读取基础 SQL 文件，也不会
因 SQL 文件缺失而自动切换模式。native 模式当前只广告 default_version 这一项
直接安装版本；请求其它版本会失败，不能根据任意输入虚构版本可用性。更新仍通过
版本图选择显式 SQL 边，不必有旧基础脚本。

native 源在准备前有零棵解析树，不具备安装资格。服务缺失、声明为空、只有注释，
或者解析/对象 admission 失败都会拒绝安装；回调完成后必须存在实际有效语句。
内存源也显式携带该模式，不能把普通 SQL 包的空输入当成 native 安装。

可运行的开发回归样例为 `plugins/sql_packages/rust_text_native`，它没有基础 SQL，
由 Rust 插件声明 rust_native_length，另带显式 1.0→1.1 更新。Rust SDK 的
`Package::write_to_with_source(directory, InstallSource::Native)` 已可生成该类包；
零脚本只生成 control，存在脚本时必须全部为显式更新。CLI 使用 `--example
seekdb_native_schema` 选择此例，并通过实际 Rust 包读取器验证 control 和默认安装
版本，不在构建时运行 prepare。手写 control 仍可使用。完全无 control 文件的包发现
尚未提供。`ALTER EXTENSION UPDATE` 只运行显式更新脚本，
不会重放 prepare 或自动推导动态对象差异。动态创建的例程作为正常 extension member
保存，后续更新/删除沿用成员规则，而非仅存在于进程 registry。

`ExtensionScript::source()` 保留基础源；动态 SQL 独立保存，完整树见 `statements()`。
宿主适配器须保留 `ICatalogDeclarations` 对象至事务/发布结束。不能仅提取 SQL 后
立即释放 lease，也不能把重新加载基础 source 当成保留了动态声明。

后续仍需实现事务绑定的查询期 CatalogContext、对象 builder、native-symbol/type
DDL、更多对象类别和依赖/CASCADE。安装期声明不应取代这些目标，也不能在普通
SQL SPI 中暗中独立提交 DDL 来伪装为调用者事务的一部分。

## 验证方法

SDK 回归核对 C/Rust 布局、身份与大小检查、错误保留、panic 边界和不可跨线程借用。
独立 loader 回归实际加载 Rust DSO，验证动态 SQL、归属和 ABI 错误、SQL 限额及
回调后 lease 保留/释放；白盒回归检查输入复制、UTF-8/NUL、错线程及关闭状态。
kernel runner 使用生产对象文件执行真实 parser/PL resolver，检查静态两条加动态
一条例程的解析、名称、owner/database、native lookup 和安装阶段 lease。
另覆盖 control-only native 包的单条例程、显式更新/no-op 路径、空/注释声明拒绝，
并在安装产物中检查基础 SQL 文件确实不存在。

这些内核测试使用受控 Root/catalog fixture，不启动监听服务，不能证明实库的提交、
回滚、恢复或并发卸载行为。具体验证结果见 [实施进度](plugin-implementation-status.md)。
