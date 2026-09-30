# 插件直接注册 SQL 对象

这是已实现的注册入口，范围为插件 `init/start` 阶段。它消除了“所有对象必须先组成永久 snapshot，再提供 discovery service”的要求；不是完整的 `CREATE EXTENSION`、任意 SQL 安装脚本或运行期 DDL API。

## 接口与使用方式

`include/seekdb/plugin/extension_spi.h` 新增 `seekdb_plugin_host_api_v2_t`，保留原 host API 的完整 v1 前缀，在末尾追加独立版本的注册 SPI。旧插件仍可以使用快照，新的插件通过 `struct_size` 和 `registration_spi_major` 检测支持情况，不能直接假定任意 host 都提供新字段。

注册顺序为：

1. 检查 host 表长度、新 SPI 版本及回调是否存在。
2. 用原 `begin_registration` 创建事务。
3. 调用 `register_service` 和/或新 `register_extension`，可以循环逐个注册。
4. 用原 `commit_registration` 提交暂存集合；出错时调用 `abort_registration`。
5. 返回初始化/启动结果，由 loader 统一校验、提交 catalog、发布 registry。

`register_extension(host, transaction, kind, descriptor, descriptor_bytes)` 接受现有类型、函数、cast、index access method、optimizer/DAS hook、catalog object 和表函数描述符。每一类仍需要内核已经实现的协议；注册 index/hook 元数据不等于其执行回调已打通。未知类别明确返回不支持。

`descriptor_bytes` 必须等于描述符的 `struct_size`。函数可传带完整 v1 前缀的 typed v2 描述符，参数签名等后缀也经过校验。描述符及其引用的字符串/数组在调用期间保持有效即可；host 在回调返回前完成复制，因此插件可以用局部变量构造对象。执行 service table 本身仍必须保持至 module 安全停用，不能因为描述符复制就提前释放回调地址。

示例见 [SQL extension 插件](../../../plugins/sql_extension/seekdb_sql_extension_plugin.c)：默认直接注册一个类型、五个标量函数和一个表函数，不再声明 discovery service。新增 `seekdb_sql_add_one` 和 `seekdb_sql_exec` 用于验证参数化 host SQL 调用。测试构建设置 `SEEKDB_SQL_SNAPSHOT_REGISTRATION` 可走旧快照路径，用于比较两条路径的解析/执行行为。

## 原子性和失败处理

服务与对象共用同一个注册事务。一次 commit 成功，只代表它们进入该 module 的暂存集合，并非 SQL 对象已对其他查询可见。最终发布仍使用已有的 catalog activation permit 和 registry candidate 协议，不新增绕过 schema/catalog 协调的系统表写入通道。

同一事务的重复对象 ID 在 register 时拒绝；两个事务先后暂存相同 ID 时，后提交者在 commit 时拒绝。冲突或分配失败不会单独提交该事务的服务。签名、实现服务绑定及与快照/已安装对象的冲突还会在最终候选准备阶段检查，因此插件必须处理最终激活失败，不能把暂存成功当成装载成功。

事务中止释放服务和对象的全部暂存项。停止接受注册后，register/commit 均拒绝执行；未结束的事务导致装载失败并清理资源。所有直接注册事务共享对象数量与描述符字节预算。持久对象仍须满足 manifest 的 catalog/data-format/capability 声明；直接注册不绕过快照路径的这些校验。

## 验证与尚未完成的部分

`rust/plugin-runtime/tests` 现在包含：

- C++ registry / Rust lifecycle 集成测试。
- 真实动态库的直接注册与旧快照两种装载测试，执行 `seekdb_add_one` 回调得到 `41 + 1 = 42`。
- catalog 拒绝候选时，无可见服务或对象残留；catalog 测试替身不构成真实数据库持久事务验证。
- 描述符深复制、错误输入、跨事务冲突、服务/对象联合提交与中止、跨事务对象配额以及停止接受注册的测试。

注册事务管理器已经迁入 `rust/plugin-runtime/src/registration.rs`：Rust 持有 token、规范化对象的所有权、配额和提交/中止状态。C++ 校验并复制公开描述符，成功交给 Rust 后不再拥有它；Rust 在中止或清理时调用 C++ 的纯元数据析构适配，恰好释放一次。只有停止接受注册后，C++ 才取出不可变结果供现有 catalog/registry 候选协议使用。

已结束 token 的地址保留到注册域销毁，避免旧 token 命中新事务；每次模块激活最多发行 `SEEKDB_PLUGIN_MAX_REGISTRATION_TRANSACTIONS`（65536）个 token，空事务和已结束事务也计入。同时打开的事务仍最多 4096 个，达到上限后中止它们可以继续创建新事务。对象和服务暂存数组在 commit/abort 时释放，不随失效 token 保留。发行上限与并发事务数、对象/服务数量预算不同，用于约束 token 保留内存。

后续统一 Extension/object manager、安装 SQL、运行期 catalog context、tenant/database 作用域以及其余 loader/registry 迁移仍在实施范围内；没有将这一阶段标记为完整 Rust 框架迁移。
