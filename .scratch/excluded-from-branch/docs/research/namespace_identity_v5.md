# Namespace 原型 V5：跨数据库的持久身份与目录根

日期：2026-09-14。分支：`codex/namespace-identity-v5`，基于 V4 `8a7848bce`。

后续的独立快照持有与来源删除见 [V6](namespace_snapshot_lifetime_v6.md)。本文保留 V5 的原始边界和验收结果。

## 要验证的问题

V2–V4 的目录根属于一个 database，尚不能表示包含多个 database 的 namespace。V5 将根的所有者改为持久化的 namespace，并检查同名数据库、同名表、相同 local ID 在不同 namespace 中能否独立读写、加锁和恢复。

这是全 namespace fork 的内核原型阶段。用户、权限、配置和固定 namespace 的 SQL worker 尚未接入；源 namespace 删除、快照独立生命周期及 GC 也尚未实现。

## 最小实现

实验环境变量为 `SEEKDB_NAMESPACE_FORK_PROTOTYPE=3`。模式 2 继续执行 V2–V4 的 database 原型，模式 1 和关闭开关的路径保持原有行为。

`__fork_proto_meta.namespaces` 持久化 namespace ID、名称、来源、catalog 根、directory 根、S 和 schema version。ID 通过引擎表的主键和 AUTO_INCREMENT 分配。Python 只创建实验元数据表并发出 SQL；注册、目录维护、捕获快照和物化都在 C++ 内核执行。

一个 namespace 的 catalog 包含所有实验用户数据库及其表：

| 索引键 | 内容 |
| --- | --- |
| `D<database name>`、`@<local database ID>` | 数据库定义的不可变序列化对象 |
| `T<local database ID>/<table name>`、`#<local table ID>` | 表定义、local table/tablet ID |
| directory 中的 `<local tablet ID>` | 继承的源 tablet 或当前 namespace 的已物化绑定 |

继续使用 V2 的真实不可变 COW B+ tree。namespace 1 接管当前引擎的原生用户目录；首次注册枚举已有数据库和表。后续 CREATE DATABASE/TABLE 在原有 DDL 事务中维护 namespace 1 的根。**一次性接管需要枚举，后续 fork 捕获不按表枚举。** 注册期间要求停止其他 DDL；本原型未验证并发接管。

fork 锁住源 namespace 根，取得 S、写入快照保护，并让目标引用同一对根，将继承 cap 设为 S。没有为每个数据库创建目标原生 database，也没有为每张表创建目标 schema/tablet。目标首次访问某个 tablet 时才沿用 V2–V4 的事务物化路径，写入该 namespace 的 directory；其余条目继续共享。

## 身份如何进入现有引擎

`NamespaceObjectKey` 显式包含 `(namespace_id, local_id)`。为尽快走通现有 schema、锁和 tablet 管理器，原型把这对值可逆地编码到现有 64 位 ID：

```text
namespace 1：storage_id = local_id
其他 namespace：storage_id = (1 << 62) | (namespace_id << 32) | local_id
```

namespace ID 必须在 `(0, 2^30)`，local ID 必须在 `(0, 2^32)`；表登记仍沿用 V2 更严格的 database ID 上限 `2^30`。不同对象类型使用引擎原有各自的索引/管理器。

目录中的 local ID 保持不变，进入旧引擎的物理 ID 按 namespace 区分。这验证了明确传递 namespace 身份的最小通路，**尚未把生产接口、全部对象键和持久格式改为完整的复合键**。

数据库 schema 与表 schema 使用原型已有的进程生命周期持有方式，保证 schema guard 和计划引用不会悬空。V5 新增数据库 schema 持有项，内存随打开的 `(namespace, database)` 数量增长，退出进程释放；尚无 DROP/ALTER 淘汰。没有新增物化绑定缓存，热访问仍复用 V4 的已有 tablet 状态缓存。

## 测试入口

```bash
python3 tools/obtest/namespace_identity_prototype.py \
  --binary build_release/src/observer/seekdb
```

脚本创建独占实例，使用本地二进制，退出时停止进程、保留日志并归档数据。

临时借用现有 SQL 语法作为内核调用入口：

```sql
-- 此前可已有 db1.t1；接管原生用户目录，注册 namespace 1。
FORK DATABASE __empty__ TO a;
-- 此后原生 CREATE DATABASE/TABLE 也维护 a 的跨库目录。
FORK DATABASE a TO b;
SELECT namespace_id, name FROM __fork_proto_meta.namespaces;
-- 假定 b 获得 ID 2；地址显式传入 namespace ID 与逻辑数据库名。
SELECT * FROM __fork_ns_2__db1.t1;
```

`a`、`b` 是 namespace 注册名；上述命令不会创建同名原生 database。`__empty__` 表示首次接管入口，并非通用空 namespace 创建接口。`__fork_ns_2__db1` 是测试地址，不是持久化数据库别名。SQL 名称规范化必须保留地址中的 namespace，不能替换为 schema 中的逻辑名 `db1`。

目前仅通过 root 测试，未提供 namespace 级权限边界、登录默认 namespace 或 SQL worker 隔离。

## 验收条件

1. 接管前创建 `db1.t1`；接管后创建 `db2.t1`、`db2.t2` 和空数据库。B 在一个 S 继承全部数据库和三张表。
2. fork 前后不可变页完全相同，B 与 A 共享两个根；SHOW/EXPLAIN 后目标物理 tablet 仍为零。
3. A 后续更新不影响 B；稍后 fork 的 C 读取自己的 S。A/B/C 的同名表分别写入，B 的未提交行锁不阻塞 A/C 同 local ID 的行。
4. 六路同时首次读取 B 的另一数据库，只创建一个目标 tablet；C 独立创建其自己的绑定。
5. A 后创建的新数据库不出现在 B；未知 namespace、重复注册和约定不支持的 DROP 被拒绝。
6. 强杀并重启后，注册信息、根及已提交 tablet 恢复；重启后首次打开此前未访问的表仍读取 B/C 各自的 S。
7. 解析真实目录页，确认 local ID 相同、物理绑定按 namespace 区分，旧页未修改，没有 rootserver fork DDL 任务。

## 范围限制

仅覆盖固定两列整数主键表、普通用户数据库、来源保留及源 namespace 1 的多个单代分支。系统数据库和实验元数据不参与 fork。不支持分区、索引、LOB、视图、权限/用户/config fork、目标目录 DDL、多代 fork、namespace DROP、ID 复用或精确 GC。全局快照 pin 和不可变页尚不回收。

这一步建立跨数据库的 namespace 身份和根；还不能证明删除 A 后 B 独立存活，也不代表完整实例已可 fork。

## 2026-09-14 实测证据

主验收实例 `namespace_fork_PROTOTYPE_identity_v5__bmmynzu` 返回 PASS，已停止并归档：

- A/B/C 的 namespace ID 为 `1/2/3`。B 的 S=`1789372802884419006`。
- B fork 时共享 catalog 根 `550137173`、directory 根 `1637206973`，31 个既有不可变页完全不变，catalog 高度 2，目标物理 tablet 为 0。
- A/B/C 的 `db1.t1` local table ID 均为 `500008`，local tablet ID 均为 `200005`；B/C 对应物理 tablet 分别为 `4611686027017522501`、`4611686031312489797`。
- 同名表独立写入及行锁、六路并发首读、强杀恢复、恢复后首读未访问表均通过；最终 B/C 各三个目标 tablet，无 fork DDL 任务。

[验收输出](/data/1/tmp/namespace-v5-identity-address-fixed.log)、[结构化事件](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_identity_v5__bmmynzu/experiment.jsonl)、[目录快照](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_identity_v5__bmmynzu/directory_snapshot.json)、[归档数据](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_identity_v5__bmmynzu/data.tar.gz)。

[最终编译输出](/data/1/tmp/namespace-v5-build-address.log)：既有 release 目录，`source ~/.bashrc` 后执行 `CARGO_NET_OFFLINE=true make -j80 seekdb` 成功，无外部下载。二进制 SHA-256：`160b4877cede5eeeb90e41b56e4b1fade62af052fc6f695dff68df9101518c74`。

两次诊断失败分别暴露了初始化时对不存在的注册表发 SQL 导致内层事务失效，以及数据库名规范化丢失 namespace 地址的问题。最终实现使用 schema 就绪检查，并在公共规范化入口保留显式地址；失败记录不计为通过证据。

同一二进制的回归：

- [V4](/data/1/tmp/namespace-v5-v4-regression.log)通过：已有 tablet 快路径、并发未提交创建、真实查询超时回滚、提交前崩溃及重启。
- [V2 40 表](/data/1/tmp/namespace-v5-v2-regression-logs-kept.log)通过：326 个旧页不变，catalog/directory 高度 3/2，8 个物化条目、32 个继承条目、6 个共享目录页；恢复、并发首读和 GC 超过 S 后读未访问表通过。首次回归的最终日志计数失败，真实数据/目录检查已通过；实验启动参数提高 `max_syslog_file_count` 为 16 后完整重跑通过，未放宽断言。
- [V3](/data/1/tmp/namespace-v5-v3-regression.log)通过：真实转储、minor/major 合并、未访问表保留 S、基线恢复和目标增量正确。这是模式 2 的合并回归，V5 主验收尚未单独覆盖 namespace 模式的完整合并周期。
- [V1 与关闭开关对照](/data/1/tmp/namespace-v5-v1-regression.log)通过：`lazy_fixed_snapshot`、`flag_off_keeps_eager_database_fork` 均为 PASS。
- Python 语法检查和 `git diff --check` 通过；本轮实验实例均已停止并归档。
