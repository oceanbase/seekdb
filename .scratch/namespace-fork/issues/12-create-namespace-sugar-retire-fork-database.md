# 12: Phase 4a：CREATE NAMESPACE 语法糖 + FORK DATABASE 退役

**What to build:** `CREATE NAMESPACE ns3` 一条语句建空 ns（内部从内置只读模板 __template__ fork，对用户隐藏模板概念）；FORK DATABASE 语法移除（语义被 FORK NAMESPACE 完全覆盖）。

**Blocked by:** 11

**Status:** fresh and old prototype data paths verified; modified built-in system objects remain a migration TODO

- [x] CREATE NAMESPACE 秒级建成空 ns，可正常登录建表
- [x] __template__ 拒绝登录/DDL/drop，仅作 fork 母本
- [x] FORK DATABASE 语法移除，相关代码清理

## 2026-09-24 实测与 TODO

- 新实例系统包加载结束、外部连接开放之前，自动登记 ns1 并固定 `__template__` 快照。`CREATE NAMESPACE phase10_empty` 用时 0.012 秒，子空间无父空间用户库，可建库、建表、写入、读回；重启后仍可登录。模板不注册到登录 Registry，显式从内部名 fork 被解析器拒绝。
- 新语法 `FORK NAMESPACE child FROM ns1` 和二级 fork 均通过；`FORK DATABASE` 被解析器拒绝。生命周期语句要求系统空间会话和 SUPER。子空间 `SHOW DATABASES` 不再显示控制库，直接读控制表被拒绝。
- `make -j80 seekdb` 成功；四套单进程门禁 exit 0 + PASS：`/tmp/seekdb-ticket12-final-bootstrap.log`、`/tmp/seekdb-ticket12-guard-{sql,direct,tls}.log`。
- 当时待办：旧目录安全模板迁移、`ObForkDatabase*` 内部命名和旧 Worker 脚本。前两项的当前进展见下文；旧脚本命名仍随工单 11 清理。
- 迁移前提审计：`ensure_control_schema()` 目前仅在 ns1 行不存在时创建模板；若旧目录已含 ns1 用户表，启动后直接从当前 ns1 fork 模板会把用户库带入新建空 namespace，因此不能仅补一条 `control_namespace(ns1, __template__)`。需要构造只有系统 schema 的模板并验证旧目录用户库不会泄漏。
- 旧目录迁移试验 `/tmp/seekdb-ticket12-old-template-migration-focused.log`：隔离实例先在 ns1 建 `legacy_user.secret`，删模板行后重启。尝试将模板建为无父级的独立空 namespace：模板登记成功，ns1 原表仍可读，`CREATE NAMESPACE migrated_empty` 成功；但新 namespace 登录时读取系统 tablet 5 返回 `OB_TABLET_NOT_EXIST` (-4725)，最终 4012 超时。实例日志 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_old_template_migration_yhithuji/log/seekdb.log`。该尝试的代码已撤回；后续改为保留 ns1 系统 tablet 血缘并清理暂存空间用户对象，见下文。

## 2026-09-24 内部 namespace 命令命名

- 退役语法留下的 `ObForkDatabase*` 类型、`fork_database` root command、`T_FORK_DATABASE` 节点和对应源文件已统一为 `NamespaceCommand*` / `namespace_command` / `T_NAMESPACE_COMMAND`，参数字段改为 `source_name_` / `target_name_`。语句类型数值 128、解析节点数值 4917、RPC 字段顺序和 SQL 语法保持原值；`FORK DATABASE` 仍被拒绝。
- 离线编译 `/tmp/seekdb-ticket12-namespace-command-build3.log` exit 0；四件套 `/tmp/seekdb-ticket12-namespace-command-{bootstrap,sql,direct-drop,tls}.log` 和源空间删除专项 `/tmp/seekdb-ticket12-namespace-command-drop.log` 均 exit 0 且 PASS。direct 门禁新增曾失败的活跃连接拒绝删除、源删除后同名重建和旧子空间可读断言。
- 命名整理阶段尚未触及旧目录迁移；后续实现与结果见下文。

## 2026-09-24 旧数据目录模板迁移

- 启动恢复在 ns1 已存在、`__template__` 缺失时，从实际登记的 ns1 名称 fork 保留名 `__template_build__`。该名称不进入登录 Registry；若进程中断，下一次启动复用同一暂存 namespace ID 并继续清理。清理和校验成功后才原子改名为 `__template__`，然后开放外部登录。
- 暂存空间通过本空间 DDL 删除继承的普通用户库，重建空 `test` 默认库；还会删除系统库里的普通用户表、视图、独立过程和额外用户/角色。ns1 原表、过程和账号保持可用。账号名和 host 用反引号转义，避免 SQL mode 改变字符串转义语义。
- 原失败复现已加入 bootstrap 四件套：模拟旧目录缺模板，ns1 同时持有普通用户库、`test` 用户表、`mysql` 用户表/视图/过程及账号/角色；重启后新空空间能用 `test` 登录建表、无上述用户对象或凭据，再重启读回自己的表。`/tmp/seekdb-ticket12-template-role-bootstrap.log` PASS。部分清理后中断的续做 `/tmp/seekdb-ticket12-template-user-resume.log` PASS；ns1 历史别名为 `a` 的迁移 `/tmp/seekdb-ticket12-template-legacy-alias.log` PASS。离线编译 `/tmp/seekdb-ticket12-template-migration-build8.log` exit 0。
- TODO：若旧目录曾直接修改内置系统表、内置 root 账号或系统包定义，当前迁移只移除新增对象与账号，无法从旧数据还原内置对象的初始内容。此类目录需进一步定义可信基线与校验，不应把本轮聚焦样本外推为任意旧目录均已安全迁移。
- 最终四件套 `/tmp/seekdb-ticket12-template-role-bootstrap.log`、`/tmp/seekdb-ticket12-template-final2-{sql,direct,tls}.log` 均 exit 0 且 PASS；`python3 -m py_compile` 和 `git diff --check` 通过。迁移中断续做 `/tmp/seekdb-ticket12-template-user-resume.log` 也 PASS。
