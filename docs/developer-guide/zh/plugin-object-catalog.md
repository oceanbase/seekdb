# Rust 运行时对象目录与后续 Extension 对象管理

状态：运行时索引已接入；数据库级 Extension 安装模型尚未接入。

## 已实现的职责

`rust/plugin-runtime/src/object_catalog.rs` 管理运行时对象的 `(kind, object_id)`
索引及快照共享所有权。`ObPluginServiceRegistry::RegistrySnapshot` 不再保存第二份
C++ 对象 map；其 `ExtensionCatalog` 是 Rust handle 的薄适配。

C++ 继续将公开 C ABI descriptor 规范化为不可变 `ExtensionEntry`，其中包括
对象元数据、缓存参数签名和 generation 的共享引用。成功插入才将该适配对象交给
Rust 目录管理，失败时仍由 C++ 销毁。目录的释放回调只销毁 host 数据，不执行插件
生命周期或业务回调。

候选激活复制当前目录，Rust 只复制共享引用，不复制每个对象的名称、签名和内容。
变更只发生在私有候选中；最终仍由已有 C++ 发布协调将 service 与对象快照一起切换，
保留 catalog 激活提交、候选预留和 generation 的同一套顺序。并未增加第二个事务
协调者，也没有给插件公开这个 host 内部索引 API。

停用先在私有快照中线性过滤相应 owner 的对象，再走原来的状态切换和发布路径。
旧快照可以继续读取旧对象，但持有元数据不等于取得执行权限：真正执行仍须校验
generation 并同时取得对象与实现 service 的 lease。

## 生命周期和成本

- 发布后的目录只读，允许并发读取和复制；修改、销毁某个目录 handle 必须排他。
- 不同目录共享的对象引用使用原子计数，最后一个拥有者释放时恰好调用一次 host
  数据释放回调。回调必须线程安全、不得抛出异常、不得重入目录或调用插件代码。
- 借用对象指针的有效期受所属目录限制，不能在该目录移除对象或销毁后继续使用；
  若另一个快照明确保留了对象，可以在其生命周期内重新借用。
- 有序向量提供按 kind/id 确定性枚举和二分查找；复制为 O(N)，批量停用为一次
  O(N) 过滤，避免逐个删除造成 O(N²) 移动。插入会移动部分引用，当前上限仍为
  4096 个对象。尚无数据库负载性能测量，不宣称此迁移已改善整体吞吐。
- 可失败的 Vec/handle 分配报告错误；Arc 控制块分配仍遵循 host 的 OOM abort
  策略。Rust host 不承诺所有 OOM 都能作为可恢复错误返回。

## 与完整 Extension 模型之间的缺口

当前索引仍属于**进程级模块目录**，不是数据库的 `pg_extension` 等价物。
实际 SQL 查找通过 `ObIModuleProvider::resolve_plugin_sql_object` 读取它；该接口
尚无 tenant/database/安装实例参数。持久的 `__all_sql_extension_*` 行也仍由包激活
路径维护。这些事实决定了不能仅添加 `CREATE EXTENSION` 语法就宣布数据库级隔离。

后续需继续完成：

1. 定义独立的数据库级安装实例、稳定对象 ID、owner 和成员关系；不能用一次
   module generation 代替安装身份。
2. 将 SQL resolver 的用户/数据库上下文传到统一 object manager，并把持久对象
   身份绑定到当前可用实现；处理名称冲突、权限、失效通知及多数据库隔离。
3. 安装 SQL、SDK 生成 SQL、运行时 catalog builder 共用对象创建路径。当前
   init/start 注册的运行时对象不能隐式充当任意数据库的 Extension member。
4. 引入与 schema/storage 事务协作的安装上下文，规定支持的 DDL 集合、未提交
   对象的解析与可见性、失败回滚和提交后的任务启动。现有注册 journal、DML
   保存点或快照发布都不能单独替代这一事务。

本次迁移使已有执行对象只保留一个运行时索引，属于 Rust 框架实现的进展；上述
数据库层职责仍属于完整目标，不能以运行时单测替代验收。

## 验证

Rust 新增测试覆盖快照最后释放、插入失败保留所有权、名称深复制、稳定排序、
容量上限、FFI 空指针/越界与批量移除。真实 C++ bridge 测试另以 8 个线程各执行
256 轮复制/读取/释放，验证独立快照不互相移除对象以及 host payload 只销毁一次。

现有 registry/loader/GIS/Rust 文本插件 CTest 使用该索引，不另用替代实现。
完整数据库事务、取消、恢复、跨平台 ABI 和数据库负载性能测试仍需补齐。
