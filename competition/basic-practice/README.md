# seekdb · 2026 挑战赛 · 基础实践题

本目录记录本次参赛的**基础实践题**交付材料，全部内容均为本机实测结果，可复现。

---

## 一、部署 seekdb（10 分）

### 部署方式

采用官方 Python 绑定 `pylibseekdb` 以**嵌入式模式**启动一个真实 seekDB 实例：

```bash
pip install pylibseekdb          # 1.4.0.post2 (cp312-abi3-win_amd64)
```

启动脚本见 [`deploy_seekdb.py`](deploy_seekdb.py)：

```python
import pylibseekdb as seekdb
instance = seekdb.open(db_dir="./seekdb_instance")
conn = instance.connect(database="test", autocommit=True)
```

### 实测输出

| 项目 | 实测值 |
| --- | --- |
| seekDB 版本 | `5.7.25-OceanBase seekdb-v1.4.0.0` |
| 监听端口 | `2127`（loopback） |
| `SELECT 1` | `[(1,)]` |
| 可见数据库 | `information_schema`, `mysql`, `oceanbase`, `test` |

实例真实建表并建立 HNSW 向量索引，向量检索可正常返回结果：

```sql
CREATE TABLE articles (
    id        INT PRIMARY KEY,
    title     TEXT,
    embedding VECTOR(4),
    VECTOR INDEX idx_vec (embedding)
        WITH (DISTANCE=l2, TYPE=hnsw, LIB=vsag)
) ORGANIZATION = HEAP;

INSERT INTO articles VALUES (1, 'Hello seekdb', '[0.1, 0.2, 0.3, 0.4]'),
                            (2, 'second',       '[0.9, 0.1, 0.0, 0.0]'),
                            (3, 'third',        '[0.0, 0.9, 0.1, 0.0]');
```

实测向量检索结果（`ORDER BY l2_distance(...) APPROXIMATE LIMIT 5`）：

```
ANN ready after 4 s -> [(1,), (3,), (2,)]
```

即距离 `[0.1,0.2,0.3,0.4]` 最近的三条记录按距离升序正确返回，说明
**实例启动、写入、索引构建、ANN 检索全链路均正常**。

### 遥测 ID（提测平台填写项）

实例启动后在其 `run/telemetry.json` 中写入遥测负载，`content.id` 即为提测所需的遥测 ID：

文件位置：`<db_dir>/run/telemetry.json`

```json
{
  "sent": false,
  "content": {
    "id": "de3a8fe0-2670-8b08-992e-136203a72795",
    "event": "bootstraped",
    "version": "1.4.0.0",
    "instance": {
      "port": 2881,
      "hostHash": "12CA17B49AF2289436F303E0166030A21E525D266E209267433801A8FD4071A0",
      "createTimestamp": 1794448215989001
    },
    "reporter": "embed",
    "resource": {
      "cpuCount": 24,
      "logDiskSize": "2G",
      "memoryLimit": "1G",
      "dataFileSize": "0.03125G"
    },
    "telemetryVersion": 6
  },
  "component": "seekdb",
  "createdAtUs": 1794448215989001
}
```

> **提测平台填写的遥测 ID**
>
> ```
> de3a8fe0-2670-8b08-992e-136203a72795
> ```

完整原始遥测文件见 [`telemetry.json`](telemetry.json)。

---

## 二、Fork 仓库（10 分）

- 上游仓库：<https://github.com/oceanbase/seekdb>
- 本队 Fork 仓库：<https://github.com/tangtang-cx/seekdb>
- 仓库可见性：**Public**（公开，便于评测方直接检视）
- Fork 自上游分支：`2026-competition`（冻结基线 `5bce9e06d1281347ad61344e4b01fab9158c58a2`）

本仓库根目录 [`README.md`](../../README.md) 末尾已粘贴**本队验证行**（见下文「四、验证行」）。

---

## 三、提交 PR（10 分）

本分支 `feat/basic-practice` 向上游 `oceanbase/seekdb` 的 `2026-competition` 分支提交 PR：

- PR 链接：见本目录 [`PR.md`](PR.md)
- PR 描述中同样包含**本队验证行**
- PR **不要求被合并**，提交即可计分

---

## 四、本队验证行

> **本队验证行：tangtang-cx / minmax-project · 2026-competition · seekdb-basic-practice**

该验证行同时出现在：
1. 本 Fork 仓库根目录 `README.md` 末尾；
2. 向上游提交的 PR 描述中；
3. 本文件。

---

## 五、文件清单

| 文件 | 说明 |
| --- | --- |
| `README.md` | 本文件，基础实践题交付说明 |
| `deploy_seekdb.py` | 部署与遥测采集脚本（可复现） |
| `telemetry.json` | 实例启动后生成的原始遥测负载 |
| `PR.md` | 向上游提交的 PR 链接与说明 |
