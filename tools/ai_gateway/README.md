# SeekDB AI evaluation gateway

This directory contains an independent OpenAI-compatible proxy for AI-column
evaluation. SeekDB itself is unchanged: an AI endpoint points at the gateway,
and the gateway forwards the original JSON request to DeepSeek without changing
the model, messages, prompt, or response body.

The gateway adds only evaluation-side capabilities:

- deterministic latency, timeout, HTTP-error, and connection-loss injection;
- persistent request, token, cache-hit, and cost statistics;
- transparent forwarding of the authorization supplied by SeekDB;
- a fake-upstream test suite that does not call a real model.

It uses only the Python 3 standard library. Request bodies and credentials are
not persisted. The database stores request hashes and usage counters, which are
enough to correlate retries without retaining prompt content.

## Start the gateway

Create a local configuration and provide an administration secret through an
environment variable:

```bash
cd tools/ai_gateway
cp config.example.json config.json

export AI_GATEWAY_ADMIN_TOKEN='TOKEN_USED_FOR_ADMIN_API'

./run_gateway.sh
```

`config.json` is ignored by Git. The example forwards
`/v1/chat/completions` to DeepSeek's `/chat/completions` API. It binds only to
`127.0.0.1`; change `listen.host` only when SeekDB runs on another host, and use
network access control when exposing the port.

The prices in `config.example.json` are an explicit pricing snapshot, not
hard-coded gateway behavior. Confirm and update them before an evaluation. The
current DeepSeek model and price table is published at
<https://api-docs.deepseek.com/quick_start/pricing/>.

Validate a configuration without starting the service:

```bash
python3 gateway.py --config config.json check-config
```

## Point SeekDB at the gateway

Keep the provider, request format, and DeepSeek key unchanged. Only replace the
provider URL with the gateway URL in the endpoint definition:

```sql
CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT(
  'deepseek_via_gateway',
  '{
    "ai_model_name": "real_complete_model",
    "url": "http://127.0.0.1:19130/v1/chat/completions",
    "access_key": "YOUR_DEEPSEEK_KEY",
    "provider": "openai",
    "request_model_name": "deepseek-v4-pro"
  }'
);
```

SeekDB converts `access_key` into the standard `Authorization: Bearer ...`
request header. The gateway forwards that header to DeepSeek unchanged. It does
not validate, replace, log, or persist the key.

## Simulate network conditions

Fault injection is off by default. Runtime changes affect new requests and do
not require a gateway restart:

```bash
curl -sS -X PUT http://127.0.0.1:19130/_gateway/faults \
  -H "X-Gateway-Admin-Token: ${AI_GATEWAY_ADMIN_TOKEN}" \
  -H 'Content-Type: application/json' \
  -d '{
    "enabled": true,
    "fixed_delay_ms": 200,
    "jitter_ms": 300,
    "error_rate": 0.05,
    "error_status": 429,
    "disconnect_before_rate": 0.02,
    "timeout_rate": 0.01,
    "timeout_delay_ms": 65000,
    "disconnect_after_rate": 0.02
  }'
```

The three pre-upstream rates (`error_rate`, `disconnect_before_rate`, and
`timeout_rate`) are mutually exclusive and must add up to at most 1. The random
sequence is reproducible from `seed`.

- `fixed_delay_ms` and `jitter_ms` add normal network latency.
- `error_rate` returns the configured HTTP error without calling DeepSeek.
- `disconnect_before_rate` closes the client connection before calling DeepSeek.
- `timeout_rate` delays and then returns 504 without calling DeepSeek.
- `disconnect_after_rate` calls DeepSeek, records its usage and cost, and then
  drops the response. This models a provider-side success whose response never
  reaches SeekDB. A later SeekDB retry is a second billable upstream call.

Disable every fault while retaining its values:

```bash
curl -sS -X PUT http://127.0.0.1:19130/_gateway/faults \
  -H "X-Gateway-Admin-Token: ${AI_GATEWAY_ADMIN_TOKEN}" \
  -H 'Content-Type: application/json' \
  -d '{"enabled": false}'
```

## Read cost statistics

```bash
curl -sS 'http://127.0.0.1:19130/_gateway/stats?limit=20' \
  -H "X-Gateway-Admin-Token: ${AI_GATEWAY_ADMIN_TOKEN}"
```

The response contains overall totals, per-model totals, and recent request
outcomes. Every request that reaches DeepSeek increments `upstream_calls`, even
when SeekDB does not receive the response. Gateway-injected failures before the
upstream have zero provider cost.

DeepSeek returns `prompt_cache_hit_tokens`, `prompt_cache_miss_tokens`, and
`completion_tokens`. For a priced model, the gateway computes:

```text
cost = cache_hit_tokens  * cache_hit_input_price  / 1,000,000
     + cache_miss_tokens * cache_miss_input_price / 1,000,000
     + completion_tokens * output_price           / 1,000,000
```

If a compatible provider returns only `prompt_tokens`, all prompt tokens are
conservatively counted as cache misses and `cache_breakdown_inferred` is set. If
the request model has no price entry, the usage remains visible and
`unpriced_upstream_calls` increments, but no cost is invented.

The SQLite database persists across gateway restarts. To print it from the
command line or deliberately reset an evaluation:

```bash
python3 gateway.py --config config.json stats --limit 20
python3 gateway.py --config config.json reset
```

The same reset is available through `POST /_gateway/reset` with the admin token.

## Limitations

- The gateway is intended for SeekDB's non-streaming AI calls. A streaming
  response is buffered before forwarding, so it must not be used to benchmark
  token-by-token latency.
- The calculated amount follows the configured token prices. Provider account
  credits, discounts, taxes, or price changes are outside the gateway's view.
- The service is evaluation infrastructure, not part of the SeekDB transaction
  boundary. Killing it or changing its fault profile cannot change committed
  database data directly; it only changes what the existing HTTP client observes.

## Test

```bash
python3 -m unittest -v test_gateway.py
```

The tests start an in-process fake provider and verify transparent forwarding,
authorization forwarding, token-cost accounting, injected HTTP errors, and
provider-success/client-response-loss behavior.
