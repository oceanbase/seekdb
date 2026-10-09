# Namespace fork 内核原型 V2

日期：2026-09-14。分支：`codex/namespace-fork-kernel-v2`，基于 V1 `2b8d3e355`。状态：内核 V2 已实现，40 表真实引擎功能验收通过。

本文保留 V2 当时的范围与证据。后续 [V3 转储和合并验收](namespace_fork_compaction_v3.md) 已补后台基线交接及后台目录识别；来源保留和全局快照保护仍是前提。

## 本轮问题与接口

客户端只发送普通 SQL，继承表在 catalog 中已经存在，目标物理 tablet 在首次存储访问时创建。语义解析、SHOW 和 PREPARE 不承担创建。内部创建不得隐式提交用户事务，也不得要求用户重新取得事务快照。

```text
普通 SQL
  → ObSchemaGetterGuard：从捕获的 catalog 查询不可变表定义
  → 编译/执行（保留原业务事务）
  → ObAccessService 的读写检查入口
  → 查目录；必要时创建目标 tablet、COW 发布绑定
  → 既有 tablet / MVCC / fork 基线读写
```

进程开关 `SEEKDB_NAMESPACE_FORK_PROTOTYPE=2` 启用本轮；`1` 保留 V1，关闭开关保留现有路径。来源库名前缀 `__fork_proto_a`，目标前缀 `__fork_proto_b`。这是独占实例中的实验协议。

## 持久目录

页为不可变 B+ 树节点，每页最多 8 个键（内部节点最多 9 个子引用），支持叶子/内部节点分裂、路径复制、点查和按路径遍历。目录根或子引用携带快照上限；复制受限路径时，将上限下推至所有未修改子引用或叶条目，只有新物化绑定解除上限。修改一个条目不会解除其他条目的 S 限制。

来源正常 CREATE TABLE 在原 DDL 事务中登记完整表定义和两个树根：

- catalog：表名/原表 ID → 固定 schema 对象及原 tablet 身份。
- directory：原 tablet ID → schema 对象、来源物理 tablet 或目标自有绑定。

fork 创建空目标 database，在同一元信息事务中登记快照保护并引用来源已有根。它不临时扫描来源建树，不逐表创建目标实体，也不写新树页。

为避免同时新增页 IO 管理器，原型用两个独立于 A/B 的引擎表存储元信息：

```sql
CREATE DATABASE __fork_proto_meta;
CREATE TABLE __fork_proto_meta.pages(
  id BIGINT UNSIGNED PRIMARY KEY, payload VARBINARY(60000));
CREATE TABLE __fork_proto_meta.roots(
  database_id BIGINT UNSIGNED PRIMARY KEY,
  source_id BIGINT UNSIGNED,
  catalog_page BIGINT UNSIGNED, catalog_cap BIGINT,
  directory_page BIGINT UNSIGNED, directory_cap BIGINT,
  snapshot BIGINT, schema_version BIGINT);
```

它们由本次驱动初始化。页数据通过已有引擎 WAL/MVCC 持久化，根更新和 tablet 的 CREATE MDS 注册共用一个引擎事务，没有独立文件提交或 SQLite 双写。页 ID 使用既有校验函数；复用 ID 时比较完整内容，碰撞报错。页和不可变 schema 对象本轮不在线回收。

正式设计的 `ObObjectReaderWriter` 元信息页、checkpoint registry 和引用 GC 尚未接入；这里验证的是实际 COW 和事务持久性，不代表正式元信息存储方案已完成。

## Catalog 与对象身份

SQL schema guard 通过目标目录取得已序列化的固定定义，不能回查来源当前 schema 作为快照定义。schema 缓存拥有独立 allocator，跨语句存活。捕获后新增来源表会更新来源根，不进入目标旧根。

为了在单进程原型中复用现有物理键，使用受限编码：

```text
实验全局 ID = (1 << 62) | (目标 database ID << 32) | 原局部 ID
```

目标 database ID 限制在 30 位，来源 table/tablet ID 限制在 32 位。这是实验身份编码，不是完整 `TabletKey(namespace_id, tablet_id)` 改造，也不验证权限或独立 SQL worker。

## 存储物化与事务

首次读写在 `ObAccessService::check_read_allowed_` / `check_write_allowed_` 进入，catalog 解析没有物化副作用。

内部事务锁定目标根记录，将同一分支的物化请求串行化；检查原 `tablet_id=0` 快照保护、存储保留边界和继承条目。然后通过 `ObTabletCreator` 注册物理创建 MDS，登记既有 tablet-to-table 映射，写 COW 页和新目录根，一起提交。用户事务的 commit/rollback 不由这次元信息事务执行。

为缩小实现，本轮已物化表的再次访问也会开启内部事务、锁根并查询目录；尚未增加已绑定条目的快速路径。因此本轮只验证机制和功能，不能用来承诺首次查询或稳定运行的延迟。

普通 tablet 创建会在提交时重设 `create_commit_version`。本轮新增专用 `PROTOTYPE_MATERIALIZE_TABLET` MDS 类型，保留已登记的逻辑可见版本，`create_commit_scn` 仍记录实际物理提交。这使在物化之前已取得快照的业务事务能继续读取，而不修改其快照或跳过现有可见性检查。

物化不创建 FORK TABLE 后台任务，也不调用主动 freeze。来源仍通过既有粗粒度快照保护存活，读写复用既有未完成 fork 基线。正常转储/major 合并后的基线交接是后续里程碑，本轮不宣称长期运行能力。

## 验收入口

```bash
python3 tools/obtest/namespace_fork_kernel_prototype.py \
  --binary build_release/src/observer/seekdb
```

默认 40 张来源表，足以触发叶子和内部节点分裂；首次访问少量表。驱动负责启动独占实例、初始化元信息存储、普通 SQL 验收和保留日志，完全不发送逐表 FORK。

必须覆盖：

- 捕获前后树页数不增加，源/目标共享根，目标物理 tablet 为 0。
- SHOW / EXPLAIN / PREPARE 能读取 catalog，物理 tablet 仍为 0；EXECUTE 才创建。
- 未物化表的直接 INSERT，以及业务事务中首次 UPDATE 两张表后全部回滚。
- 8 个并发连接首次查询同一表，只产生一次物化。
- COW 后，不同叶子中的未物化表仍按 S 读取；来源后续提交及新快照 GC 水位前进不改变该结果。
- 来源后来新建的表不出现在目标 catalog。
- 已有来源表的 ALTER/DROP、目标 DROP（含 IF EXISTS）和 CREATE 明确拒绝，失败不改变目录或已有目标数据。
- 杀进程并重启后，恢复目录根、已物化目标修改和仍未访问的继承表。
- 独立解码持久树页，检查键序、分隔区间、叶子等深、旧页不变，以及 COW 后仍继承的条目保留 S。

物理 tablet 数使用 `__all_virtual_tablet_info` 的本地 tablet 迭代器核验，不使用异步上报表作为即时创建计数。

GC 水位更新按需触发，空闲实例不会只因时间经过而持续推进。验收先执行已提交物化后的崩溃重启，再等待正常 primary catchup 将水位推进至 S 之后；没有直接修改水位，也没有强制转储来源或目标。这个检查验证快照保护与目录上限，不等于完成物理版本回收和基线交接验收。

## 2026-09-14 验收结果

最终运行结果为 PASS，实例已停止，数据已压缩保留。没有设置 FORK TABLE 的 DEBUG_SYNC 暂停点，整个过程没有创建后台 fork 任务。

| 检查 | 实测结果 |
| --- | --- |
| 捕获 40 张来源表 | 元信息页/对象总数保持 326；源/目标共用 catalog 根 `2168368128`、directory 根 `3511752161` |
| fork 后及 SHOW / EXPLAIN / PREPARE 后 | 目标物理 tablet 数均为 0；EXECUTE 后为 1 |
| 直接 INSERT，首次 UPDATE 两张表后 ROLLBACK | 目标新增行与来源隔离；两张表更新均被业务事务回滚 |
| 8 个连接同时首次查询一张表 | 只创建 1 个目标 tablet，所有查询结果一致 |
| 崩溃重启 | 已提交目录根及 tablet 数一致；目标新增行保留；尚未打开的表恢复后正常物化 |
| 快照保护 | S=`1789367766835324009`；正常恢复后的 GC 水位=`1789367784445225031`；未访问表仍读到 S 的旧行 |
| B+ 树独立解码 | catalog 高 3 层、directory 高 2 层；键序、区间与叶子等深校验通过 |
| COW 后目录 | 8 个自有绑定、32 个继承条目；继承上限均为 S；与捕获目录仍共享 6 个页 |
| 不可变性 | 捕获时的全部 326 个页/对象内容保持不变 |
| 不支持的 DDL | 来源 ALTER/DROP、目标 DROP（含 IF EXISTS）和 CREATE 均报 1235；目录和既有目标数据不变 |

证据：

- [完整验收输出](/data/1/tmp/namespace-v2-acceptance-final.log)。
- [结构化事件](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kernel_v2_iiqhd5vw/experiment.jsonl)。
- [目录根与全部持久页的独立检查输入](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kernel_v2_iiqhd5vw/directory_snapshot.json)。
- [引擎日志](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kernel_v2_iiqhd5vw/log/seekdb.log)。
- [停止后的实例数据](/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_kernel_v2_iiqhd5vw/data.tar.gz)。
- [最终增量编译输出](/data/1/tmp/namespace-v2-build-ddl.log)，`source ~/.bashrc` 后在既有 `build_release` 中执行 `CARGO_NET_OFFLINE=true make -j80 seekdb`，成功。

本次二进制：`build_release/src/observer/seekdb`，SHA-256：`389cbd2872a226ea063b0e93e3234c88868445d0b633ffbbe0a83c2d78767d98`。没有进行吞吐或延迟 benchmark。

同一二进制运行 `python3 tools/obtest/namespace_fork_prototype.py --binary build_release/src/observer/seekdb --tables 2`，V1 的 `lazy_fixed_snapshot` 和关闭开关的 `flag_off_keeps_eager_database_fork` 均通过，见 [回归输出](/data/1/tmp/namespace-v2-v1-regression-final.log)。V1 的水位检查补充了一个与 A/B 无关的 probe tablet 转储，以正常 mini merge 请求 GC 更新，修正原测试依赖启动时序的偶然性；V2 使用前述正常重启流程。Python 语法检查及 `git diff --check` 通过。

实现入口：

- [namespace_fork_kernel_prototype.cpp](../../src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp)：不可变 B+ 树、根捕获、固定 schema 和存储物化事务。
- [ob_schema_getter_guard.cpp](../../src/share/schema/ob_schema_getter_guard.cpp)：继承 catalog 的名字/ID 查找与枚举。
- [ob_access_service.cpp](../../src/storage/tx_storage/ob_access_service.cpp)：实际首次读写触发。
- [ob_tablet_create_delete_mds_user_data.cpp](../../src/storage/tablet/ob_tablet_create_delete_mds_user_data.cpp)：逻辑可见版本与物理提交 SCN 分离。
- [namespace_fork_kernel_prototype.py](../../tools/obtest/namespace_fork_kernel_prototype.py)：独占实例、一条命令验收与证据保存。

## 边界

仍只支持非分区、两列整数 `id/v`、主键 `id` 的普通表，来源存活；已有来源表的 DDL 和目标 DDL 不属于本轮。来源建表按顺序执行，未验证并发 DDL。未做索引、LOB、多代分支、来源删除、普通转储交接、在线引用/页回收及跨进程 namespace。重启验收在已提交物化后杀进程，不是逐个事务提交阶段的故障注入。元信息表、页格式、ID 编码和 MDS 类型均为一次性原型，不供生产实例使用。
