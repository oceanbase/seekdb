# MiniVectorDB 标准输入/输出协议

适用于 `2026-competition-round2`。本文按当前判题器的实际行为说明通信格式，供选手实现和本地自测。
SQL 功能要求、公开用例和评分规则仍以赛题说明为准。

## 1. 程序怎样与判题器通信

正式评测启动 Docker 容器中的数据库程序，通过进程的 **stdin/stdout** 通信：

```text
判题器 ── 一行 JSON 请求 ──> 数据库进程的 stdin
判题器 <── 一行 JSON 响应 ── 数据库进程的 stdout
                              stderr ──> 诊断日志
```

这是 JSON Lines（JSONL）协议，不是直接输入 SQL 的交互终端，也不是 HTTP、TCP 或 MySQL 网络协议。
容器以 `docker run -i` 启动，保持标准输入打开；不分配 `-t`，不需要开放端口。

基本规则：

1. 编码为 UTF-8，每个报文是一个完整 JSON 对象，以一个实际换行字符结束。
2. 一个报文只能占一个物理行。字符串内的换行须由 JSON 序列化器转义为 `\n`。
3. 每次输出后立即 flush，不能等缓冲区满或程序退出才发送。
4. stdout 只输出协议报文；启动日志、调试信息、错误堆栈全部写 stderr。
5. 每个请求恰好返回一个响应，原样回传请求 `id`，包括它的 JSON 类型。
6. 不输出空行、提示符、Markdown 代码围栏、ANSI 颜色或额外的状态行。
7. 不输出 NaN、Infinity 等非标准 JSON 数值，不使用重复对象键。

判题器按“发一个请求、等一个响应”执行。不要额外向 stdout 推送心跳或进度报文。

## 2. 启动握手

程序启动后先打开 `/data` 中的数据文件，完成必要的恢复，再主动输出以下一行并 flush：

```json
{"type":"ready"}
```

必须在等待第一条请求**之前**输出。每次启动，包括恢复重启，都需要输出一次。
当前判题器要求这个对象准确匹配，不要额外添加字段。

以下形式均不符合协议：

```text
READY
READY\tminivectordb
{"ready":true}
{"status":"ready"}
```

`READY` 不是合法 JSON，会导致 `invalid JSON response`；后两个是合法 JSON，但不符合握手格式。
如果程序启动后直接等待 SQL、只向 stderr 输出 ready，或输出后不 flush，判题器可能报 `JSON response timeout`。

## 3. 执行 SQL

判题器写入 stdin 的请求示例：

```json
{"id":1,"op":"execute","sql":"SELECT id FROM docs ORDER BY id;","search_mode":"exact"}
```

| 字段 | 含义 |
| --- | --- |
| `id` | 请求编号；响应必须原样回传，数字不能转成字符串 |
| `op` | 此处为 `execute` |
| `sql` | 真正要执行的 SQL 字符串 |
| `search_mode` | `exact` 或 `ann`；按题目要求选择向量检索模式 |

程序应先解析整行 JSON，再把 `sql` 字段交给 SQL 执行器。
不要把整个 JSON 当 SQL，也不要用分号切割原始通信输入；分号可能出现在 SQL 字符串内部。

### 查询成功

```json
{"id":1,"ok":true,"columns":["id"],"rows":[[1],[2],[3]]}
```

`columns` 与 `rows` 中各列顺序对应。空结果集仍返回列名，`rows` 使用空数组。

### 修改成功

```json
{"id":2,"ok":true,"affected_rows":1}
```

INSERT、UPDATE、DELETE 按实际受影响记录数返回整数 `affected_rows`。
对于公开断言未要求额外字段的成功语句，例如部分建表和事务控制语句，响应仍须包含 `id` 和 `ok:true`。
公开 `.result` 中的 `{}` 不是让程序输出 `{}`，而是表示该语句应成功、没有额外字段断言。

### 执行失败

```json
{"id":3,"ok":false,"error":{"code":"duplicate_key","message":"duplicate primary key"}}
```

`ok` 必须是 JSON 布尔值，不能用 `0`、`1` 或字符串代替。
`error.code` 必须是非空字符串；具体代码映射以发布的配置为准。
上面的 `duplicate_key` 是语义示例，不意味着未发布的全部错误码映射已经固定。
判题不依赖 `message` 的具体措辞。

预期中的 SQL 错误不应终止进程；发送失败响应后继续接收下一条请求。
失败语句的原子性、事务能否继续等行为须满足赛题要求。

### 数据类型

- BIGINT 按当前判题配置使用 JSON 整数，不能转换成字符串，也不能经浮点转换丢失精度。
- 行数据使用二维数组，SQL NULL 使用 JSON `null`。
- 向量投影使用 JSON 数组；浮点输出必须是有限数值。
- 列名、列顺序、受影响行数及查询结果按公开用例和赛题规则检查。

## 4. 正常关闭与重启

判题器发送：

```json
{"id":4,"op":"shutdown"}
```

程序应回滚未提交事务，完成必要的持久化与资源清理，返回：

```json
{"id":4,"ok":true}
```

然后 flush 并以退出码 0 结束。关闭响应之后 stdout 不得再输出任何内容，包括 `Bye` 或空行。
判题器收到响应后会关闭 stdin 并等待退出。

数据库持久化文件写入 `/data`。正常重启复用原数据目录，不得在启动时无条件清空。
崩溃测试可能直接终止进程而不发送 shutdown，已确认提交的持久性不能依赖正常关闭才实现。

## 5. 完整交互示例

以下编号标记是文档说明，不属于实际报文。

① 程序启动后，stdout：

```json
{"type":"ready"}
```

② 判题器写入 stdin：

```json
{"id":1,"op":"execute","sql":"CREATE TABLE docs (id BIGINT PRIMARY KEY, content VARCHAR(16));","search_mode":"exact"}
```

程序返回 stdout：

```json
{"id":1,"ok":true}
```

③ stdin：

```json
{"id":2,"op":"execute","sql":"INSERT INTO docs VALUES (1, '你好');","search_mode":"exact"}
```

stdout：

```json
{"id":2,"ok":true,"affected_rows":1}
```

④ stdin：

```json
{"id":3,"op":"execute","sql":"SELECT id, content FROM docs ORDER BY id;","search_mode":"exact"}
```

stdout：

```json
{"id":3,"ok":true,"columns":["id","content"],"rows":[[1,"你好"]]}
```

⑤ stdin：

```json
{"id":4,"op":"shutdown"}
```

stdout，随后正常退出：

```json
{"id":4,"ok":true}
```

整个过程中 stderr 可以输出日志，但 stdout 只能有上述五行 JSON。

## 6. 与公开 mysqltest 题包的关系

`mysqltest/cases/*.test` 是 SQL/驱动指令文件，`.result` 是预期断言数组；两者都不是直接传输的通信报文。

例如 `.test` 中的：

```sql
SELECT id FROM docs;
```

需要由自测驱动包装为：

```json
{"id":1,"op":"execute","sql":"SELECT id FROM docs;","search_mode":"exact"}
```

`.result` 中的 `{"columns":["id"],"rows":[[1]]}` 只描述要检查的结果字段，
实际响应还需要 `id` 和 `ok`。

- `--restart` 是驱动指令：执行正常关闭，再用原数据目录重新启动并等待 ready；不要把它当 SQL 发给数据库。
- `--mode ann` 是驱动指令：改变后续请求的 `search_mode`，它本身不占一条 SQL 响应。
- 每个公开用例从新的空数据目录开始；同一用例内的重启复用目录。

只在本地用 `echo 'SELECT ...' | 程序` 测试，不能证明正式 JSON 协议已经实现。

## 7. Docker 自测

Dockerfile 必须默认启动上述 stdin/stdout 模式；不要默认进入 SQL 提示符或启动网络服务。
程序路径和参数由实现决定，但须在 Dockerfile 中设置正确，不能依赖判题器补传私有参数。

下面命令使用 Bash。先设置自己已构建的镜像：

```bash
export VECTORDB_IMAGE='你的镜像名:标签'
```

再执行以下测试。数据、输入和输出保存在新建的临时目录中，不改已有数据库。

```bash
(
  set -euo pipefail
  : "${VECTORDB_IMAGE:?请先设置已构建的镜像名}"
  test_root=$(mktemp -d)
  container_name="vectordb-protocol-check-$(basename "$test_root")"
  mkdir "$test_root/data"
  chmod 777 "$test_root/data"
  trap 'docker rm -f "$container_name" >/dev/null 2>&1 || true' EXIT
  cat > "$test_root/requests.jsonl" <<'JSONL'
{"id":1,"op":"execute","sql":"CREATE TABLE docs (id BIGINT PRIMARY KEY, content VARCHAR(16));","search_mode":"exact"}
{"id":2,"op":"execute","sql":"INSERT INTO docs VALUES (1, '你好');","search_mode":"exact"}
{"id":3,"op":"execute","sql":"SELECT id, content FROM docs ORDER BY id;","search_mode":"exact"}
{"id":4,"op":"shutdown"}
JSONL
  echo "自测产物：$test_root"
  timeout 180s docker run --rm -i --name "$container_name" \
    --network=none --cpus=2 --memory=2g --memory-swap=2g --pids-limit=128 \
    --read-only --cap-drop=ALL --security-opt=no-new-privileges \
    --mount "type=bind,src=$test_root/data,dst=/data" \
    "$VECTORDB_IMAGE" \
    < "$test_root/requests.jsonl" \
    > "$test_root/stdout.jsonl" 2> "$test_root/stderr.log"
  python3 - "$test_root/stdout.jsonl" <<'PY'
import json
import sys
from pathlib import Path

raw = Path(sys.argv[1]).read_bytes()
assert raw.endswith(b'\n'), '最后一条响应必须以换行结束'
lines = raw.decode('utf-8').splitlines()
assert len(lines) == 5, f'预期5行输出，实际{len(lines)}行：{lines[:6]!r}'
messages = [json.loads(line) for line in lines]
assert messages[0] == {'type': 'ready'}, messages[0]
for ident, response in enumerate(messages[1:], 1):
    assert type(response.get('id')) is int and response['id'] == ident, response
    assert response.get('ok') is True, response
assert type(messages[2].get('affected_rows')) is int, messages[2]
assert messages[2]['affected_rows'] == 1, messages[2]
assert messages[3].get('columns') == ['id', 'content'], messages[3]
assert messages[3].get('rows') == [[1, '你好']], messages[3]
print('基础协议样例通过')
PY
)
```

这是基础格式与功能检查，不是完整正式判题。此命令提前写入全部请求，因此**不能验证 ready 是否在收到请求前主动发送，
也不能完整验证逐请求 flush 和超时**；实现时仍须遵守前述交互顺序。
180 秒是这个本地自测命令的总时限，不是正式比赛每个阶段的统一时限。
如命令失败，查看打印目录中的 `stdout.jsonl` 和 `stderr.log`；超时后 trap 会清理该测试容器。

## 8. 常见失败定位

| 错误 | 检查方向 |
| --- | --- |
| `invalid JSON response` | stdout 第一行是否是 READY、提示符、空行、日志；是否输出了多行格式化 JSON |
| `expected ready, got ...` | 启动对象是否准确为 `{"type":"ready"}` |
| `JSON response timeout` | 是否主动输出 ready、每条报文是否带换行并 flush、进程是否卡住 |
| `database exited before response` | Docker 入口参数、依赖、文件权限、程序崩溃；查看 stderr |
| `response id differs from request` | 是否漏掉 id、返回了错误编号或把数字 id 转成字符串 |
| `missing boolean ok` | 是否缺少 ok，或使用了字符串/数字代替布尔值 |
| `unsolicited or duplicate response` | 是否多发响应、心跳、空行或日志 |
| `extra stdout or nonzero exit after shutdown` | shutdown 响应后是否继续输出，或以非零退出码结束 |

修复握手后，还要检查 execute 与 shutdown。仅把 `READY` 替换成 JSON，而继续按纯 SQL 读取输入，仍然不能通过协议验证。


## 9. 使用题包附带的交互式检查工具

题包新增 `protocol_check/run.py`，它按正式顺序等待 ready、发送请求、读取响应，并支持公开用例的重启与模式切换。
相比第7节的批量输入示例，这个工具还能检测启动握手时序、逐请求flush和响应超时。
从题包根目录执行：

```bash
export VECTORDB_IMAGE='你的镜像名:标签'
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" --handshake-only --output "protocol-results/handshake-$(date +%Y%m%d-%H%M%S)"
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" --case sql_crud --output "protocol-results/crud-$(date +%Y%m%d-%H%M%S)"
python3 protocol_check/run.py --image "$VECTORDB_IMAGE" --output "protocol-results/public-$(date +%Y%m%d-%H%M%S)"
```

工具使用 `mysqltest/manifest.json` 指向的公开样例，不包含隐藏用例，不计算分数或发起提测。
解析失败会展示收到的stdout原始行片段；详细使用方法见 `protocol_check/README.md`。
`protocol_check/example_stdio.py` 是可运行的通信骨架，但尚未接入数据库，不能作为SQL实现提交。
