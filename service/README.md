# SeekDB AI 函数 60 分本地自测环境

评分标准里写明了 `ai_function_enhancement.test 依赖运行在 0.0.0.0:8000 的 Mock AI Service`。
这个目录就是帮你在本地复刻出同样的环境，把 6 类 AI 函数增强（共 60 分）真实跑一遍，
不需要接真实的 AI 厂商、不需要花钱、也不需要装任何 Python 第三方包。

## 目录内容

| 文件 | 作用 |
|---|---|
| `mock_ai_service.py` | 假 AI 服务（OpenAI / Ollama / Siliconflow），纯 Python 标准库 |
| `self_test.test` | mysqltest 自测脚本，覆盖评分标准全部 6 类 |
| `README.md` | 本说明 |

## 一、这个 service 是什么

它**不是要下载的工具**，而是一个我用 Python 标准库 `http.server` 写的 HTTP 服务器：

- 监听 `0.0.0.0:8000`（和评分环境一致）；
- 假装自己就是 OpenAI / Ollama / 硅基流动，按 seekdb 代码里 `ob_ai_func_utils.cpp`
  的协议返回伪造的 completion / embedding / rerank 结果；
- 你跑 `ai_complete` / `ai_embed` / `ai_rerank` 时，seekdb 会把 HTTP 请求打到它这里。

**要不要装东西？** 不用。只要机器上有 Python 3（运行 `python3 --version` 确认）。不需要 `pip install`。

## 二、运行步骤

> 观察者（seekdb observer）跑在哪台机器，mock 服务就起在哪台机器。
> 你的构建是在 Linux VM 里（`~/seekdb-20261026`），所以下面都在 VM 里执行。
> 如果本目录不在 VM 里，先把这 3 个文件同步/复制过去。

### 1. 启动 Mock AI Service（终端 A）

```bash
cd <本目录>
python3 mock_ai_service.py
```

看到 `[mock] listening on 0.0.0.0:8000` 即成功。保持它不退出。

验证一下通不通（另开终端）：

```bash
curl -s http://127.0.0.1:8000/     # 期望 {"ok": true, "service": "seekdb mock ai service"}
```

### 2. 跑自测（终端 B）

用你平时跑 `ai_prompt.test` 的同一套 mysqltest 命令，把目标换成本目录的 `self_test.test`：

```bash
python3 tools/deploy/mysqltest.py run \
    --base-dir <你的 base 目录> \
    --work-dir <你的 work 目录> \
    --record \
    --test-set <本目录>/self_test.test
```

- `--record` 会把实际输出写成 `.result` 文件，方便你逐条核对。
- 用 `--record` 跑一遍后，再不带 `--record` 跑一遍即可做“通过/失败”判定（结果一致即通过）。

（如果你更习惯用客户端直接喂 SQL：把 `self_test.test` 里的 SQL 逐段贴进 obclient/mysql
执行即可；`--error`、`--echo` 这些是 mysqltest 指令，手动执行时删掉即可。）

## 三、预期结果速查

mock 的返回是确定性的，所以每条都能对得上：

| 类别 | 检查点 | 预期 |
|---|---|---|
| AI_PROMPT | JSON object/array 参数 | 返回 `{"template":"obj {0}","args":[{"k":"v"}]}` 这类 JSON |
| AI_PROMPT | 重复占位符 | `ai_complete('mock_gpt', ai_prompt('{0}{0}{0}','x'))` 返回含 `OK:xxx` |
| AI_PROMPT | TEXT/LONGTEXT | 正常返回 JSON，不报错 |
| AI_EMBED 单行 | 默认维度 | `json_length(...)` = 4 |
| AI_EMBED 单行 | 显式 dim=3 | `json_length(...)` = 3 |
| AI_EMBED 单行 | 维度校验 | `$[0]` = 1.0 |
| AI_COMPLETE | 基础调用 / NULL 配置 / temperature/max_tokens | 都返回含 `OK:hello` |
| AI_COMPLETE | 响应非空校验 | `mock_ep_gpt_bad` 报错 4070 |
| Ollama | 空 access key + 原生 completion/embed | 返回 `OK:hello` / 长度 4 |
| AI_RERANK | 字符串数组 | `$[0].index`=0, `$[2].index`=2（按分降序） |
| AI_RERANK | 对象数组 doc_key | `$[0].id`=`d0`（按 `_model_score` 降序） |
| AI_EMBED 批量 | 多行 + NULL + 顺序/维度 | row1→(3, 1.0), row2→(3, 2.0), row3→NULL |

> 说明：`OK:` 前缀是 mock 故意加的，用于证明“占位符替换后真实的 prompt 确实被发到了模型”。
> 例：`ai_prompt('{0}{0}{0}','x')` 替换后是 `xxx`，所以模型回显 `OK:xxx`。

## 四、本次同步修改的源码（已改）

自测依赖的 3 处源码修复：

1. **rerank doc_key 路径排序**（`ob_expr_ai_rerank.cpp` 的 `sort_document_array_by_model_result`）：
   原来 doc_key 路径只按模型返回顺序映射，不排序；现在按 `_model_score` 降序排序，
   与无 doc_key 路径的 `sort_result_array_by_score` 行为一致（对应「AI_RERANK 顺序」4 分）。

2. **批量 ai_embed 的 dim<=0 对齐**（`ob_expr_ai_embed.cpp` 的 `eval_ai_embed_batch`）：
   原来 dim<=0 被静默忽略（改用默认维度），现在与单行语义一致，报
   `OB_INVALID_ARGUMENT`；同时补了 dim 参数为 NULL 时的判空，避免读脏值。

3. **（已有）单行/批量 NULL 传播**：批量 NULL 输入 → NULL 输出，符合「NULL 输入传播」要求。

> 改完后需要在 VM 里重新 `build.py build`（或你原来的构建命令）编译一次，再跑自测。

## 五、常见问题

- **`python3: command not found`**：VM 里装 Python 3（`sudo apt-get install -y python3`）。
- **自测全报 4070/连接失败**：确认 mock 服务确实在 `0.0.0.0:8000` 上，且 seekdb 的 endpoint
  `url` 用的是 `127.0.0.1:8000`（本机）。
- **`json_length(ai_embed(...))` 报类型错**：OceanBase 个别版本要求先转 JSON，可改成
  `json_length(cast(ai_embed(...) as json))` 再试。
- **端口被占**：改 `mock_ai_service.py` 里的 `PORT`，并同步改 `self_test.test` 里的 `url`。
