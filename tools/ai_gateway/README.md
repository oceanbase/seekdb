# AI endpoint evaluation gateway

`test_gateway.py` is a transparent HTTP proxy that sits between the database and
an OpenAI compatible model service. It is used to evaluate the asynchronous AI
column feature under the failure and latency conditions described by the task:
timeouts, HTTP 429/5xx, dropped answers and slow answers.

The gateway is a standalone tool. It is **not** part of the database
transaction boundary: it never modifies the request or the response payload and
it keeps no state that the database depends on.

## Usage

```bash
python3 tools/ai_gateway/test_gateway.py \
    --port 19130 \
    --upstream http://127.0.0.1:19120 \
    --delay-ms 50 \
    --error-rate 0.1 --error-status 429 \
    --timeout-rate 0.05 \
    --drop-response-rate 0.05 \
    --cost-prompt-per-1k 0.001 --cost-completion-per-1k 0.002
```

Point the AI model endpoint of the database at the gateway instead of the real
model service:

```sql
call DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT('gw_ep',
  '{"ai_model_name":"my_model","url":"http://127.0.0.1:19130/v1/chat/completions",
    "access_key":"k","provider":"openai","request_model_name":"my-model"}');
```

## Statistics

`GET /` on the gateway port returns the collected counters:

| field | meaning |
|---|---|
| `requests` | requests received from the database |
| `upstream_calls` | requests that really reached the upstream model |
| `injected_errors` | answers replaced by an injected HTTP error |
| `timeouts` | requests dropped without any answer |
| `dropped_responses` | upstream calls whose answer was lost on the way back |
| `prompt_tokens` / `completion_tokens` | token usage reported by the upstream |
| `total_latency_ms` | accumulated gateway latency |
| `estimated_cost` | cost computed from the configured token prices |

`upstream_calls` is the number that has to be compared with the number of
tasks the database executed: a lost answer may lead to duplicated inference,
and the difference is exactly what this counter exposes.

## Unit test

The unit test uses a fake upstream, so no model credentials are needed:

```bash
python3 -m unittest tools.ai_gateway.test_gateway -v
# or
python3 tools/ai_gateway/test_gateway.py
```
