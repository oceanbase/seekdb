# fork 历史 tablet 修复完成审计

日期：2026-10-02。目标：`goal-fork-historical-tablet-resolution.md`。基线：`f2b9c159abe566d20e6efc0fcd4ecf1ee05b761e`。

本文件、用例和日志仅保留在本地。最终 v4 验收与交付均已完成。提交 `50557ddbe3f3956afa2b71addbfcf15d380738d6` 已推送到 `codex/namespace-worker-proxy-v20`，ls-remote 确认远端与本地 HEAD 完全一致。

## 逐项验收

证据目录：`gate-results/20261002-history-v4/`。`acceptance-evidence.json` 保存日志哈希、实际 PASS 事件和业务 diff 指纹。

| 要求 | 真实引擎证据 | 结论 |
| --- | --- | --- |
| B/C 均未物化，B TRUNCATE 不破坏 C | direct 有/无前置 COMMENT 均通过；物理信息断言 C 未物化 | 通过 |
| B 在 C fork 前改为与 A 不同的值 | A=10、B=20，截断后 C=20，首次写入 C=120 | 通过 |
| B 在 C fork 后才物化、修改 | B=30，C=10，截断后 C 首次写入为 110 | 通过 |
| C 已物化对照及首次写入同源 | 预先物化 C 对照、两种来源的首次写入均通过 | 通过 |
| DROP/同名重建/新 fork 不混旧对象 | A 重建为 99，新 fork=99，旧 C=20 | 通过 |
| DELETED 来源经 GC、重启仍可读 | 来源 committed、非空壳；实际 GC 及 SIGKILL 重启后 C=20 | 通过 |
| 未完成基线的间接依赖 | C=120，退休 A/B 在接管前保留，接管完成后为空壳或消失 | 通过 |
| 删除中间 Namespace | 删除 B 后 C 仍读 20，重启后保持 | 通过 |
| 最后依赖解除后实际物理移除 | 两个 GC 用例最终记录 physical=[]、all_old_tablets_removed=true | 通过 |
| snapshot/pin 释放、重试与恢复 | 检查 collection 3 和 8 无退休引用；最终释放后 SIGKILL 重启仍无物理对象/引用 | 通过 |
| fork/物化/读取与 GC 关键交错 | 暂停 C 首次物化，交错 fork/GC，再放行；写入成功 C=120，新 fork=120，其写入 300 不影响 C | 通过 |
| 首次 DDL delta 基准完整 | 无 COMMENT 场景断言旧 tablet 墓碑 kind=1、drop_scn>0；不同物化时序核对 was_owned | 通过 |
| 缺失历史、未物化和暂不可用不混淆 | 原生 typed-record 用例断言累计 cap、祖先墓碑边界、缺失 owned 来源报 SNAPSHOT_DISCARDED、EAGAIN 原样返回 | 通过 |
| DDL 登记与释放配对 | 真实 ObDDLSQLTransaction 原生用例覆盖过期 schema 显式回滚、正常回滚、登记失败析构，不释放其他 DDL 登记 | 通过 |
| 提交后、delta 发布前崩溃 | initial/child 均观察到真实 pending 窗口并 SIGKILL；fork 等待且无虚假 Namespace/snapshot/pin；恢复墓碑并解除 pending | 通过 |
| 恢复不依赖父空间先登录 | 两条发布恢复用例重启后先登录旧孩子读取 10/20，再登录所属 Namespace；新 fork 为空，首次写入 110/120 | 通过 |
| bootstrap 初始化顺序 | 原生用例先初始化协调水位，再 ensure_root；四阶段创建/标记删除/完成删除/恢复验证，每阶段间 SIGKILL | 通过 |
| 生产构建和四件套 | 最终生产 build 成功；bootstrap 含原生 KV、SQL、direct、TLS 全通过；direct 执行 15 个命令，包含综合 direct | 通过 |
| 代码-only 提交、推送现有分支、不创建 PR | 提交 50557ddbe 严格为 10 个 src 业务文件，等于验收业务 diff；远端完整哈希与 HEAD 一致，无 PR | 通过 |

## 实现与成本约束

- 保留父链和稀疏 EXCEPTIONS；墓碑新增 was_owned/create_scn 两个标量并启用 drop_scn，不新增常驻历史状态列表。
- fork 仍只登记 Namespace、父链、schema 版本、快照和 pin，不枚举用户表/tablet 生成地址清单。
- 普通读取按累计 cap 查父链；AccessService 使用返回的物理 ID 与 cap；读取自身不物化。
- 全量物理对象/Namespace 核对只用于 GC 和退休基线依赖验证；新增的 source graph 是单次调用的临时数据。
- Namespace 地址解析在上层，共享存储复用物理生命周期接口，schema ID 未编码 Namespace。
- 物理回收屏障覆盖引用判断直到空壳转换；100ms 内无法排空则延后 GC。新访问等待窗口结束，并遵守请求取消/超时。
- 初始空间 bootstrap 完成后与其他空间使用同一 DDL lifecycle；没有新增正常请求的 Namespace 1 回退。
- 子空间恢复在完整 runtime 安装后进入原有首次装载 gate；schema_loaded 只有在 schema、delta 和 pending 全部处理成功后才为真。内部 SQL 同线程重入沿用已有机制，其他线程等待。
- 首次装载不创建额外独立存储上下文；SQL proxy 自行绑定 owner，保留调用方已有上下文。
- 临时探针、外部 .scratch include 和 FreezeInfoMgr 测试 getter 已移除；生产二进制不含 INSTANCE_META_PROBE_PASS/INSTANCE_DDL_PROBE_PASS；git diff --cached --check 通过。

## 最终证据

- 生产二进制：`/data/1/tmp/seekdb-ns-artifacts/seekdb-history-production-v4`。
- 同一业务代码的原生探针二进制：`/data/1/tmp/seekdb-ns-artifacts/seekdb-history-native-v4`。
- 生产构建：`/tmp/seekdb-ns-history-production-v4-build.log`、移除探针后的再次构建 `/tmp/seekdb-ns-history-production-v4-final-build.log`。
- 原生构建：`/tmp/seekdb-ns-history-native-v4-build.log`。
- 验收日志：`gate-results/20261002-history-v4/{bootstrap-native-kv,bootstrap,sql,direct,tls}.log`。
- 业务 diff SHA256：`dccd161b13a6afc02f9320b297d7de7ce559a589e78cf57c88241aa165ad247b`。
- 原始与中间失败日志保留：错误祖先来源、最终回收未释放、GC 窗口 UPDATE 4023、DDL 计数被错误递减、bootstrap 协调重复插入、子空间恢复 pending 未解除及随后暴露的 4006。相关回归已纳入四件套。

## 范围与独立 TODO

完整物理 schema 历史、DDL 物化触发入口全面收敛、备库及实例管理 SQL 等仍按既有 TODO 推进；本轮未运行完整 mysqltest/sysbench，不宣称覆盖所有调度交错。

一次 fresh bootstrap 在 MDS SSTable 读取时因近 INT64_MAX 超时相加得到负数而失败，发生在 Namespace 控制元数据初始化前；相关源码与本轮基线相同，之后同场景以及最终四件套均通过。已记录独立 TODO，保留失败日志和 direct 发布恢复场景，不把该次失败归于已修正的 Namespace 恢复顺序。

## 交付核对

- 生产二进制 SHA256：`f42338fa16bcfc94dc4dffecb1b25977608c8f79e7227718085fedc56cfd2e35`。
- 原生探针二进制 SHA256：`1a7de2d47d60cc9e826c22381a8d50506107da24ccc2ca33e4af2fa932adfc0f`。
- 提交差异与 `production-code.diff` 逐字节一致，文件白名单核对通过。
- tracked 工作区干净，只有未跟踪的本地 `.scratch/`；所有本轮测试脚本及构建进程均已退出，测试实例已停止。
