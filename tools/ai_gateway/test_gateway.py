#!/usr/bin/env python3
"""Transparent AI endpoint evaluation gateway.

The gateway sits between the database and an OpenAI compatible model service.
It forwards every request unchanged and, in addition, injects the failure
modes the AI column feature has to survive:

  * latency      - a fixed delay before the upstream call is answered
  * http errors  - a configurable status code (429 / 5xx) is returned
  * timeouts     - the request is dropped without any answer
  * lost answers - the upstream call is executed, but the response is dropped
                   (the "upstream finished but the answer was lost" case that
                   may lead to duplicated inference)
  * usage        - request counters and the token usage reported by the
                   upstream are collected, so that the number of model calls
                   that really reached the upstream can be compared with the
                   number of tasks the database executed.

The gateway is a standalone tool: it is not part of the database transaction
boundary and it never modifies the request or the response payload.
"""

import argparse
import json
import os
import random
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_COST_PER_1K_PROMPT_TOKENS = 0.0
DEFAULT_COST_PER_1K_COMPLETION_TOKENS = 0.0


class GatewayStats(object):
    """Counters of the gateway, guarded by a lock."""

    def __init__(self):
        self.lock = threading.Lock()
        self.reset()

    def reset(self):
        with self.lock:
            self.requests = 0
            self.upstream_calls = 0
            self.injected_errors = 0
            self.dropped_responses = 0
            self.timeouts = 0
            self.prompt_tokens = 0
            self.completion_tokens = 0
            self.total_latency_ms = 0.0

    def snapshot(self):
        with self.lock:
            return {
                "requests": self.requests,
                "upstream_calls": self.upstream_calls,
                "injected_errors": self.injected_errors,
                "dropped_responses": self.dropped_responses,
                "timeouts": self.timeouts,
                "prompt_tokens": self.prompt_tokens,
                "completion_tokens": self.completion_tokens,
                "total_latency_ms": round(self.total_latency_ms, 3),
            }

    def record_request(self, latency_ms):
        with self.lock:
            self.requests += 1
            self.total_latency_ms += latency_ms

    def record_upstream_call(self, usage):
        with self.lock:
            self.upstream_calls += 1
            if usage:
                self.prompt_tokens += int(usage.get("prompt_tokens", 0) or 0)
                self.completion_tokens += int(usage.get("completion_tokens", 0) or 0)

    def record_injected_error(self):
        with self.lock:
            self.injected_errors += 1

    def record_dropped_response(self):
        with self.lock:
            self.dropped_responses += 1

    def record_timeout(self):
        with self.lock:
            self.timeouts += 1


class GatewayConfig(object):
    def __init__(self, args):
        self.upstream = args.upstream
        self.delay_ms = args.delay_ms
        self.error_rate = args.error_rate
        self.error_status = args.error_status
        self.timeout_rate = args.timeout_rate
        self.drop_response_rate = args.drop_response_rate
        self.stats_file = args.stats_file
        self.cost_prompt_per_1k = args.cost_prompt_per_1k
        self.cost_completion_per_1k = args.cost_completion_per_1k
        self.seed = args.seed


def _usage_of(payload):
    if isinstance(payload, dict):
        usage = payload.get("usage")
        if isinstance(usage, dict):
            return usage
    return None


def _cost_of(config, stats):
    prompt = stats["prompt_tokens"] / 1000.0 * config.cost_prompt_per_1k
    completion = stats["completion_tokens"] / 1000.0 * config.cost_completion_per_1k
    return round(prompt + completion, 6)


class GatewayHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    config = None
    stats = None

    def log_message(self, fmt, *args):
        if os.environ.get("AI_GATEWAY_VERBOSE"):
            sys.stderr.write("[gateway] " + (fmt % args) + "\n")

    def do_POST(self):
        start = time.time()
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length else b""
        cfg = self.config
        stats = self.stats
        stats.record_request(0.0)
        rng = random.Random()
        if cfg.seed is not None:
            rng.seed(cfg.seed + stats.requests)

        if cfg.delay_ms > 0:
            time.sleep(cfg.delay_ms / 1000.0)

        if cfg.timeout_rate > 0 and rng.random() < cfg.timeout_rate:
            # no answer at all: the client has to hit its own timeout
            stats.record_timeout()
            stats.record_request((time.time() - start) * 1000.0)
            self.close_connection = True
            return

        if cfg.error_rate > 0 and rng.random() < cfg.error_rate:
            stats.record_injected_error()
            stats.record_request((time.time() - start) * 1000.0)
            payload = json.dumps({"error": {"message": "injected gateway failure"}}).encode()
            self.send_response(cfg.error_status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        status, upstream_body = self._forward(body)
        stats.record_upstream_call(_usage_of(_decode(upstream_body)))
        if cfg.drop_response_rate > 0 and rng.random() < cfg.drop_response_rate:
            # the upstream call really happened, but the answer is lost
            stats.record_dropped_response()
            stats.record_request((time.time() - start) * 1000.0)
            self.close_connection = True
            return

        stats.record_request((time.time() - start) * 1000.0)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(upstream_body)))
        self.end_headers()
        self.wfile.write(upstream_body)

    def do_GET(self):
        # statistics endpoint
        payload = json.dumps(self._stats_payload()).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _stats_payload(self):
        stats = self.stats.snapshot()
        stats["estimated_cost"] = _cost_of(self.config, stats)
        return stats

    def _forward(self, body):
        import urllib.error
        import urllib.request
        url = self.config.upstream + self.path
        req = urllib.request.Request(url, data=body, method="POST")
        for key in ("Content-Type", "Authorization"):
            if key in self.headers:
                req.add_header(key, self.headers[key])
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as err:
            return err.code, err.read()
        except Exception as err:  # pragma: no cover - network failure
            payload = json.dumps({"error": {"message": str(err)}}).encode()
            return 502, payload


def _decode(raw):
    try:
        return json.loads(raw.decode("utf-8"))
    except Exception:
        return None


def build_handler(config, stats):
    class _Handler(GatewayHandler):
        pass
    _Handler.config = config
    _Handler.stats = stats
    return _Handler


def serve(args):
    config = GatewayConfig(args)
    stats = GatewayStats()
    handler = build_handler(config, stats)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    sys.stderr.write("[gateway] listening on %d, upstream %s\n" % (args.port, config.upstream))
    server.serve_forever()


def main(argv=None):
    parser = argparse.ArgumentParser(description="AI endpoint evaluation gateway")
    parser.add_argument("--port", type=int, default=19130)
    parser.add_argument("--upstream", required=True,
                        help="upstream base url, e.g. http://127.0.0.1:19120")
    parser.add_argument("--delay-ms", type=int, default=0)
    parser.add_argument("--error-rate", type=float, default=0.0)
    parser.add_argument("--error-status", type=int, default=500)
    parser.add_argument("--timeout-rate", type=float, default=0.0)
    parser.add_argument("--drop-response-rate", type=float, default=0.0)
    parser.add_argument("--stats-file", default="")
    parser.add_argument("--cost-prompt-per-1k", type=float,
                        default=DEFAULT_COST_PER_1K_PROMPT_TOKENS)
    parser.add_argument("--cost-completion-per-1k", type=float,
                        default=DEFAULT_COST_PER_1K_COMPLETION_TOKENS)
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args(argv)
    serve(args)


if __name__ == "__main__":
    main()
