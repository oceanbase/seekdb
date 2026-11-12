# 向上游提交的 PR

本文件记录本队向 OceanBase seekDB 上游仓库提交的 PR 信息，供提测平台填写。

---

## PR 链接

```
https://github.com/oceanbase/seekdb/pull/2586
```

---

## PR 明细（实测回读自 GitHub API）

| 项目 | 值 |
| --- | --- |
| PR 编号 | `#2586` |
| 标题 | `docs(competition): add seekdb 2026 basic-practice deliverables` |
| 状态 | `open`（**不要求被合并**，提交即计分） |
| base 分支 | `2026-competition` @ `5bce9e06d1281347ad61344e4b01fab9158c58a2` |
| head 分支 | `liyunlong136:feat/basic-practice` @ `3a1d126ca41e22d0841e5cfa79b9f406de783254` |
| 提交者 | `liyunlong136` |
| 变更文件 | 4 个（`+270 / -0`） |
| PR 描述含验证行 | 是 ✅ |

### 变更文件

| 文件 | 变更 |
| --- | --- |
| `README.md` | 末尾追加本队验证行 |
| `competition/basic-practice/README.md` | 新增：基础实践题交付说明 |
| `competition/basic-practice/deploy_seekdb.py` | 新增：部署与遥测采集脚本 |
| `competition/basic-practice/telemetry.json` | 新增：实例原始遥测负载 |

> 本 PR 只新增文档与复现脚本，**不修改任何产品代码**，因此不会与竞赛基线产生冲突。

---

## 关于上游分支选择

上游仓库 `oceanbase/seekdb` 存在多个竞赛相关分支：

| 分支 | 用途 | 本队是否使用 |
| --- | --- | --- |
| `2026-competition` | 官方 2026 竞赛基线（含冻结基线与公开测试用例） | ✅ **PR 目标分支** |
| `2026-competition-round2` | 第二轮分支 | ❌ |
| `2025-final-competition` | 2025 届分支 | ❌ |
| `master` | 主干 | ❌ |

PR 的 base 选择 `2026-competition`，与赛题要求的评测基线一致。

---

## 备选 PR 目标

若评测方要求把 PR 提交到 `master` 而不是 `2026-competition`，可用同一条 head 分支再提一个 PR：

```
head: liyunlong136:feat/basic-practice
base: master
```

---

## 本队验证行

> **本队验证行：tangtang-cx / minmax-project · 2026-competition · seekdb-basic-practice**

该验证行同时出现在：
1. Fork 仓库根目录 `README.md` 末尾；
2. PR #2586 的描述中；
3. [`README.md`](README.md) 中。
