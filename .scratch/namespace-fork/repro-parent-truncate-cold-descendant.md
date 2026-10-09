# 父 Namespace TRUNCATE 破坏未物化后代的历史读取

日期：2026-10-02。状态：已复现，尚未修复。仅本地记录，不提交分支。

当前目标、实现方向及验收条件见 [历史 tablet 解析与回收修复目标](goal-fork-historical-tablet-resolution.md)。先前完整目录树的建议已撤回，当前采用父链与稀疏 EXCEPTIONS 的修复方向。

## 运行基线

- 工作区 HEAD：`f2b9c159a`；业务代码未修改。
- 使用现有 `build_release/src/observer/seekdb`。该二进制 2026-09-29 13:05:11 完成链接，包含当前共享 codec 和实例元数据虚拟表；内置版本标签仍为配置时生成的 `cc300c5ae`，不能把标签当作此次链接的完整源码标识。
- 一台真实 seekdb 实例，Namespace 共用一个 LS；通过 MySQL 客户端协议执行 SQL。
- [聚焦脚本](fork_parent_truncate_probe.py) 默认保证 B、C 的目标用户 tablet 未物化，支持父或子预先物化的对照。诊断虚拟表中的 key ID 为 JSON 字符串，脚本已显式转换为整数。

## 可重复的最小业务场景

1. A（ns1）创建普通主键表 `nstrunc_repro.t1(id INT PRIMARY KEY,v INT)`，插入 `(1,10)`。
2. 从 A fork B；在 B 执行 `ALTER TABLE nstrunc_repro.t1 COMMENT='parent metadata ddl'`。此操作更新本空间的表元数据，物理 tablet 检查确认用户表仍未物化。
3. 从 B fork C。B、C 均能读到 `(1,10)`，C 尚无自己的目标物理 tablet。
4. B 执行 `TRUNCATE TABLE nstrunc_repro.t1`；确认 B 为空、A 仍有 `(1,10)`，C 的表定义仍指向旧逻辑 tablet。
5. C 查询原表。应读到 fork 时的 `(1,10)`；实际反复返回内部 `OB_TABLET_NOT_EXIST(-4725)`，客户端达到设置的 2 秒查询超时后返回 `4012`。

运行命令（从仓库根目录）：

```bash
python3 .scratch/namespace-fork/fork_parent_truncate_probe.py \
  --binary build_release/src/observer/seekdb --repeat 2 --prime-parent-schema
```

当前该命令退出码为 1；修复后应为 0，并返回原行。脚本不会把已知失败当作通过。

## 实测和对照

| 场景 | 次数 | 结果 | 日志 |
| --- | --- | --- | --- |
| B 未先做元数据 DDL，B/C 均未物化，直接在 B 截断 | 2 | C 均读到 `(1,10)` | `/tmp/seekdb-ns-parent-truncate-cold-rerun.log` |
| B 先做注释 DDL，B/C 均未物化，再在 B 截断 | 2 | C 均在约 2 秒后收到 4012 | `/tmp/seekdb-ns-parent-truncate-primed.log` |
| 同上，C 在截断前预先物化 | 1 | C 正常读到 `(1,10)` | `/tmp/seekdb-ns-parent-truncate-warm-child.log` |
| 同上，只预先物化 B，C 保持继承态 | 1 | C 在约 2 秒后收到 4012 | `/tmp/seekdb-ns-parent-truncate-warm-parent.log` |

预先物化使用两次 UPDATE，最终恢复 `(1,10)`，比较的逻辑数据相同。注释 DDL 场景实测 `@@recyclebin=0`。

两次冷后代失败中，旧源物理 tablet 均仍为 NORMAL、已提交且非空壳；例如第一轮旧源 `4611686155866541377` 仍存在。C 的旧逻辑 tablet 仍为 `200001`。B 截断后出现记录：

```json
{"namespace_id":"3","tablet_id":"200001"}
{"table_id":500003,"kind":1,"drop_scn":0}
```

仅物化 B 的对照中，B 旧 tablet 已标记 DELETED，但仍已提交、非空壳；失败仍经过同一父链删除标记判断。

## 根因与首次通过的区别

`InstanceNamespaceMetadata::resolve_read_tablet()` 沿父链读取祖先当前的 EXCEPTIONS。遇到 `kind=1` 直接返回 `OB_TABLET_NOT_EXIST`，没有按后代 fork 的可见性边界判断该删除是否发生在 fork 之后。

C 自己没有物理副本时，读取需要穿过 B；B 在 C fork 后生成的删除标记阻断了这个地址解析。C 预先物化时直接使用自己的 tablet，查询不再经过该父链判断。

第一次直接 TRUNCATE 的通过不能证明此路径正确：日志中 `PROTOTYPE_NAMESPACE_SCHEMA_DELTA` 的 `previous_count=0`，没有生成旧 tablet 的删除标记。加入注释 DDL 后，随后的 TRUNCATE 为 `previous_count=1`，生成删除标记并稳定触发故障。为什么首次 DDL 的历史基准未包含已继承表，另列为待核对项；当前复现只确认这个差异，不将它当作正常保留历史的机制。

## 四件套和后续 TODO

- 已接入本地 `run_four_gates.py` 的 direct 入口：无前置 DDL、孩子预先物化、冷孩子失败、仅父预先物化失败四个场景。
- 本次仅跑上述聚焦场景，没有跑完整四件套、mysqltest 或 sysbench。
- 后续修复应使祖先的删除和归属变更按 fork 边界解析，并协调历史地址保留与物理回收；仅忽略所有父墓碑不足以建立完整规则。
- 需单独定位首次 schema delta 的历史基准；保留这个无前置 DDL 的对照。
- 2026-10-01 首次执行因 socket 权限失败，日志 `/tmp/seekdb-ns-parent-truncate-cold.log`。2026-10-02 权限恢复后的首次尝试因诊断 key ID 字符串导致脚本 TypeError，日志 `/tmp/seekdb-ns-parent-truncate-cold-retry.log`；两者均未执行到截断场景，不计入上述业务结论。
- 实例均已停止，数据归档在 `/data/1/tmp/seekdb-ns-probes/namespace_fork_PROTOTYPE_parent_truncate_*`；冷后代失败实例为 `namespace_fork_PROTOTYPE_parent_truncate__v4f1xja`。

## 2026-10-02 历史追溯：目录树为何退役

- 不可变 catalog/directory 原型由 `f736de6fd`（2026-09-14）引入；`bd1a46a57`（2026-09-21）将正常目录维护改为父链与 EXCEPTIONS。
- 原记录已从该提交导出到 [历史文档](history-20260921-worker-functional-delivery.md)，仅作历史证据，内容不代表当前实现。第 240 行记录 pages 共 38503 行，150 轮手动 GC 后剩 2669 行，约 93% 为不可达页；FANOUT=8、逐对象登记和路径复制产生大量废页，GC 仅手动、单轮最多 256 页。第 250～258 行记录最终方案：由物理 ID 公式与父链探测来源，只登记 owned/tombstone，退役目录树全部写入。
- 同期 Namespace 的原生 SchemaService 已通过自身 `__all_*` 表管理 schema；额外 catalog 的 schema 权威已不再需要。重新启用一份完整地址树会重新引入索引维护、路径复制及页 GC 成本，应先检查现有父链模型能否正确解析历史来源，再决定是否需要该改造。
- 历史文档第 264 行声称后代删除可见性通过 `drop_scn` 与 `fork_cap` 比较实现，但 `bd1a46a57` 的删除代码实际写 `kind=1, drop_scn=0`，其 `resolve_inherited_tablet` 没有祖先 tombstone/SCN 判断。该说明与代码不一致，不能用它证明历史边界完整。
- 本次稳定失败的直接判断 `inherited.kind == 1 -> OB_TABLET_NOT_EXIST` 由 `cc300c5ae`（2026-09-29，线上目录切到实例 KV）增加，`git blame` 与提交 diff 已确认。9 月 21 日旧 SQL resolver 只检查请求所属 Namespace 的 tombstone，沿祖先链仅探测物理 tablet。尚未运行旧二进制对照，不能把源码差异当作所有旧场景都正常的动态证明。
- 此次回溯尚未修改生产代码。此前提出重启不可变地址树的方案需要重新评估，不能将“当前 ancestor tombstone 判断出错”直接推导为“必须恢复旧树”。

### 当前修复方向

保留父链与稀疏 EXCEPTIONS，先补齐历史解析及回收规则，当前不恢复完整 catalog/directory 树。源码依据：`ObTabletCreateDeleteHelper::check_and_get_tablet` 已接受读取 snapshot；`check_read_snapshot_for_deleted` 使用持久化的 `delete_commit_version_` 允许删除之前的读取，包括重启后从 tablet meta 恢复的删除记录。当前 `probe_physical_tablet` 却固定使用 MAX_TRANS_VERSION，不能发现仅在历史 SCN 可读的已删除副本。物化的 `create_commit_version_` 保留逻辑诞生版本，实际创建提交另记 `create_commit_scn_`，所以父空间在孩子 fork 之后才物化的情形不能仅按物理创建时刻直接排除，需结合这个已有语义验证。

计划：请求所属空间的删除与祖先当前删除分开处理；沿父链累计 fork cap，按对应历史 SCN 探测物理副本，历史数据被回收不能伪装成“从未物化”；物理回收不得因中间祖先当前 owned/tombstone 而提前切断历史依赖。fork 沿用 Namespace 级 parent/fork_cap、schema 和 pin 登记，不新增逐 tablet 地址快照。上述是源码支持的修复方向，尚未改生产代码或运行修复后二进制；必须补齐仅父物化、父后物化、重启、GC 与多代链的专项动态验证。

### 状态与回收机制核对

- 此处历史读取主要使用最新生命周期记录里的 `create_commit_version_` / `delete_commit_version_` 边界，不要求增加一份常驻的状态历史列表。`ObTabletStatusCache` 缓存 NORMAL 状态和这两个版本；状态更新/回放使缓存失效。未命中时 `get_latest` 从 MDS 取最新记录，并可经 tablet meta 的 `last_persisted_committed_tablet_status_` 或 MDS SSTable恢复。`get_tablet_status(snapshot)` 本身只接受 MAX，不能把它当作任意历史状态查询；真正的历史可读性由 `check_and_get_tablet(..., snapshot_version)` 用上述边界判断。
- 行版本保留与对象保留是两个门槛。`ObFreezeInfoMgr::get_min_reserved_snapshot` 已使用快照/pin 限制合并清理旧版本；`ObEmptyShellTask` 在把已删除 tablet 转为空壳前调用 `protect_snapshot_tablets`。当前对象保护的父链扫描使用祖先当前 owned/tombstone 截断，需要修正。
- 已物化也不必然意味着不再依赖源 tablet：`ObTablet::auto_get_read_tables` 在 `fork_info_.is_complete()==false` 时经 `get_fork_src_read_tables_` 递归读取源；后者遇到源空壳直接失败。因此物理回收还要保护尚未完成基线接管的物化副本所引用的源，不能只看 Namespace owned。这里确认的是源码依赖，尚未做专门的故障动态复现。
- GC 评估到实际空壳转换之间，还需核对 fork/物化发布、已有读请求和恢复的协调。现有 hook 可以复用，但不能宣称只改删除判断或探测参数就完成修复。
