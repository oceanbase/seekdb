#!/usr/bin/env python3
"""Transparent OpenAI-compatible evaluation gateway for SeekDB AI calls."""

import argparse
import copy
import hashlib
import hmac
import json
import os
import random
import socket
import sqlite3
import sys
import threading
import time
import uuid
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlparse
from urllib.request import Request, urlopen


DEFAULT_CONFIG = {
    "listen": {"host": "127.0.0.1", "port": 19130},
    "public_path": "/v1/chat/completions",
    "upstream": {
        "url": "https://api.deepseek.com/chat/completions",
        "timeout_seconds": 120,
    },
    "admin_token_env": "AI_GATEWAY_ADMIN_TOKEN",
    "max_request_bytes": 8 * 1024 * 1024,
    "storage": {"sqlite_path": "ai_gateway.sqlite3"},
    "faults": {
        "enabled": False,
        "seed": 1,
        "fixed_delay_ms": 0,
        "jitter_ms": 0,
        "error_rate": 0.0,
        "error_status": 503,
        "disconnect_before_rate": 0.0,
        "timeout_rate": 0.0,
        "timeout_delay_ms": 65000,
        "disconnect_after_rate": 0.0,
    },
    "pricing": {"currency": "USD", "models": {}},
}

FAULT_KEYS = set(DEFAULT_CONFIG["faults"])
SAFE_RESPONSE_HEADERS = {
    "content-encoding",
    "content-type",
    "retry-after",
    "x-request-id",
    "x-ratelimit-limit-requests",
    "x-ratelimit-remaining-requests",
    "x-ratelimit-reset-requests",
    "x-ratelimit-limit-tokens",
    "x-ratelimit-remaining-tokens",
    "x-ratelimit-reset-tokens",
}


def deep_merge(base, override):
    result = copy.deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = value
    return result


def load_config(path):
    config = copy.deepcopy(DEFAULT_CONFIG)
    config_dir = os.getcwd()
    if path:
        absolute_path = os.path.abspath(path)
        config_dir = os.path.dirname(absolute_path)
        with open(absolute_path, "r", encoding="utf-8") as stream:
            config = deep_merge(config, json.load(stream))
    db_path = config["storage"]["sqlite_path"]
    if db_path != ":memory:" and not os.path.isabs(db_path):
        config["storage"]["sqlite_path"] = os.path.join(config_dir, db_path)
    validate_config(config)
    return config


def require_number(mapping, key, minimum, maximum=None):
    value = mapping.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("%s must be a number" % key)
    if value < minimum or (maximum is not None and value > maximum):
        raise ValueError("%s must be in range [%s, %s]" % (key, minimum, maximum))


def validate_faults(faults):
    unknown = set(faults) - FAULT_KEYS
    if unknown:
        raise ValueError("unknown fault options: %s" % ", ".join(sorted(unknown)))
    if not isinstance(faults.get("enabled"), bool):
        raise ValueError("enabled must be a boolean")
    if isinstance(faults.get("seed"), bool) or not isinstance(faults.get("seed"), int):
        raise ValueError("seed must be an integer")
    for key in ("fixed_delay_ms", "jitter_ms", "timeout_delay_ms"):
        require_number(faults, key, 0)
    for key in (
        "error_rate",
        "disconnect_before_rate",
        "timeout_rate",
        "disconnect_after_rate",
    ):
        require_number(faults, key, 0, 1)
    pre_total = (
        faults["error_rate"]
        + faults["disconnect_before_rate"]
        + faults["timeout_rate"]
    )
    if pre_total > 1:
        raise ValueError("pre-request fault rates must add up to at most 1")
    status = faults.get("error_status")
    if isinstance(status, bool) or not isinstance(status, int) or status < 400 or status > 599:
        raise ValueError("error_status must be an HTTP error status")


def validate_config(config):
    if not isinstance(config.get("public_path"), str) or not config["public_path"].startswith("/"):
        raise ValueError("public_path must start with /")
    upstream_url = config.get("upstream", {}).get("url", "")
    if urlparse(upstream_url).scheme not in ("http", "https"):
        raise ValueError("upstream.url must be an http or https URL")
    require_number(config["upstream"], "timeout_seconds", 0.001)
    require_number(config, "max_request_bytes", 1)
    validate_faults(config["faults"])
    pricing = config.get("pricing", {})
    if not isinstance(pricing.get("currency"), str) or not pricing["currency"]:
        raise ValueError("pricing.currency must be a non-empty string")
    if not isinstance(pricing.get("models"), dict):
        raise ValueError("pricing.models must be an object")
    for model, rates in pricing["models"].items():
        for name in (
            "cache_hit_input_per_million",
            "cache_miss_input_per_million",
            "output_per_million",
        ):
            try:
                if Decimal(str(rates[name])) < 0:
                    raise ValueError
            except (KeyError, InvalidOperation, ValueError):
                raise ValueError("invalid %s price for model %s" % (name, model))


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def int_value(value):
    if isinstance(value, bool):
        return 0
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def format_pico_usd(value):
    return format(Decimal(value) / Decimal(10 ** 12), ".12f")


class StatsStore:
    def __init__(self, path, currency="USD"):
        self.path = path
        self.currency = currency
        parent = os.path.dirname(path)
        if path != ":memory:" and parent:
            os.makedirs(parent, exist_ok=True)
        self._shared_memory_connection = None
        if path == ":memory:":
            self.path = "file:seekdb_ai_gateway?mode=memory&cache=shared"
            self._shared_memory_connection = self._connect()
        self._initialize()

    def _connect(self):
        connection = sqlite3.connect(
            self.path,
            timeout=30,
            uri=self.path.startswith("file:"),
        )
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA busy_timeout = 30000")
        if self.path != ":memory:":
            connection.execute("PRAGMA journal_mode = WAL")
        return connection

    def _initialize(self):
        with closing(self._connect()) as connection:
            with connection:
                connection.execute(
                    """
                    CREATE TABLE IF NOT EXISTS requests (
                      id INTEGER PRIMARY KEY AUTOINCREMENT,
                      request_id TEXT NOT NULL UNIQUE,
                      started_at TEXT NOT NULL,
                      path TEXT NOT NULL,
                      request_model TEXT,
                      response_model TEXT,
                      request_bytes INTEGER NOT NULL,
                      request_sha256 TEXT NOT NULL,
                      upstream_called INTEGER NOT NULL,
                      upstream_status INTEGER,
                      client_status INTEGER,
                      outcome TEXT NOT NULL,
                      fault_type TEXT,
                      duration_ms INTEGER NOT NULL,
                      prompt_tokens INTEGER NOT NULL,
                      prompt_cache_hit_tokens INTEGER NOT NULL,
                      prompt_cache_miss_tokens INTEGER NOT NULL,
                      completion_tokens INTEGER NOT NULL,
                      total_tokens INTEGER NOT NULL,
                      cache_breakdown_inferred INTEGER NOT NULL,
                      currency TEXT NOT NULL,
                      pricing_model TEXT,
                      cache_hit_rate_per_million TEXT,
                      cache_miss_rate_per_million TEXT,
                      output_rate_per_million TEXT,
                      cost_pico_usd INTEGER,
                      error_type TEXT
                    )
                    """
                )
                connection.execute(
                    "CREATE INDEX IF NOT EXISTS requests_started_at_idx "
                    "ON requests(started_at)"
                )
                connection.execute(
                    "CREATE INDEX IF NOT EXISTS requests_model_idx "
                    "ON requests(request_model)"
                )

    def record(self, event):
        columns = (
            "request_id",
            "started_at",
            "path",
            "request_model",
            "response_model",
            "request_bytes",
            "request_sha256",
            "upstream_called",
            "upstream_status",
            "client_status",
            "outcome",
            "fault_type",
            "duration_ms",
            "prompt_tokens",
            "prompt_cache_hit_tokens",
            "prompt_cache_miss_tokens",
            "completion_tokens",
            "total_tokens",
            "cache_breakdown_inferred",
            "currency",
            "pricing_model",
            "cache_hit_rate_per_million",
            "cache_miss_rate_per_million",
            "output_rate_per_million",
            "cost_pico_usd",
            "error_type",
        )
        placeholders = ",".join("?" for _ in columns)
        values = [event.get(column) for column in columns]
        with closing(self._connect()) as connection:
            with connection:
                connection.execute(
                    "INSERT INTO requests (%s) VALUES (%s)"
                    % (",".join(columns), placeholders),
                    values,
                )

    def reset(self):
        with closing(self._connect()) as connection:
            with connection:
                deleted = connection.execute("SELECT COUNT(*) FROM requests").fetchone()[0]
                connection.execute("DELETE FROM requests")
        return deleted

    def summary(self, limit):
        limit = max(0, min(500, int(limit)))
        with closing(self._connect()) as connection:
            totals = connection.execute(
                """
                SELECT COUNT(*) AS requests_received,
                       COALESCE(SUM(upstream_called), 0) AS upstream_calls,
                       COALESCE(SUM(CASE WHEN upstream_status BETWEEN 200 AND 299
                                         THEN 1 ELSE 0 END), 0)
                         AS successful_upstream_calls,
                       COALESCE(SUM(CASE WHEN fault_type IS NOT NULL
                                         THEN 1 ELSE 0 END), 0)
                         AS injected_faults,
                       COALESCE(SUM(prompt_tokens), 0) AS prompt_tokens,
                       COALESCE(SUM(prompt_cache_hit_tokens), 0)
                         AS prompt_cache_hit_tokens,
                       COALESCE(SUM(prompt_cache_miss_tokens), 0)
                         AS prompt_cache_miss_tokens,
                       COALESCE(SUM(completion_tokens), 0) AS completion_tokens,
                       COALESCE(SUM(total_tokens), 0) AS total_tokens,
                       COALESCE(SUM(cost_pico_usd), 0) AS cost_pico_usd,
                       COALESCE(SUM(CASE WHEN upstream_called = 1
                                         AND cost_pico_usd IS NULL
                                         THEN 1 ELSE 0 END), 0)
                         AS unpriced_upstream_calls
                  FROM requests
                """
            ).fetchone()
            by_model = connection.execute(
                """
                SELECT COALESCE(request_model, '<unknown>') AS model,
                       COUNT(*) AS requests_received,
                       COALESCE(SUM(upstream_called), 0) AS upstream_calls,
                       COALESCE(SUM(prompt_cache_hit_tokens), 0)
                         AS prompt_cache_hit_tokens,
                       COALESCE(SUM(prompt_cache_miss_tokens), 0)
                         AS prompt_cache_miss_tokens,
                       COALESCE(SUM(completion_tokens), 0) AS completion_tokens,
                       COALESCE(SUM(cost_pico_usd), 0) AS cost_pico_usd
                  FROM requests
                 GROUP BY request_model
                 ORDER BY upstream_calls DESC, model
                """
            ).fetchall()
            recent = connection.execute(
                """
                SELECT request_id, started_at, path, request_model,
                       response_model, upstream_called, upstream_status,
                       client_status, outcome, fault_type, duration_ms,
                       prompt_tokens, prompt_cache_hit_tokens,
                       prompt_cache_miss_tokens, completion_tokens,
                       total_tokens, cache_breakdown_inferred, currency,
                       cost_pico_usd, error_type
                  FROM requests
                 ORDER BY id DESC
                 LIMIT ?
                """,
                (limit,),
            ).fetchall()

        result = dict(totals)
        hit = result["prompt_cache_hit_tokens"]
        miss = result["prompt_cache_miss_tokens"]
        result["cache_hit_ratio"] = (
            round(hit / float(hit + miss), 6) if hit + miss else None
        )
        result["estimated_cost"] = {
            "currency": self.currency,
            "amount": format_pico_usd(result.pop("cost_pico_usd")),
        }
        model_items = []
        for row in by_model:
            item = dict(row)
            item["estimated_cost"] = {
                "currency": result["estimated_cost"]["currency"],
                "amount": format_pico_usd(item.pop("cost_pico_usd")),
            }
            model_items.append(item)
        recent_items = []
        for row in recent:
            item = dict(row)
            cost = item.pop("cost_pico_usd")
            item["estimated_cost"] = None if cost is None else {
                "currency": item["currency"],
                "amount": format_pico_usd(cost),
            }
            recent_items.append(item)
        result["by_model"] = model_items
        result["recent"] = recent_items
        return result

class GatewayState:
    def __init__(self, config):
        self.config = config
        self.store = StatsStore(
            config["storage"]["sqlite_path"],
            config["pricing"]["currency"],
        )
        self._fault_lock = threading.Lock()
        self._faults = copy.deepcopy(config["faults"])
        self._random = random.Random(self._faults["seed"])

    def get_faults(self):
        with self._fault_lock:
            return copy.deepcopy(self._faults)

    def update_faults(self, changes):
        if not isinstance(changes, dict):
            raise ValueError("fault configuration must be a JSON object")
        with self._fault_lock:
            updated = copy.deepcopy(self._faults)
            updated.update(changes)
            validate_faults(updated)
            seed_changed = updated["seed"] != self._faults["seed"]
            self._faults = updated
            if seed_changed:
                self._random = random.Random(updated["seed"])
            return copy.deepcopy(self._faults)

    def choose_pre_fault(self, faults):
        if not faults["enabled"]:
            return None
        with self._fault_lock:
            value = self._random.random()
        threshold = faults["disconnect_before_rate"]
        if value < threshold:
            return "disconnect_before"
        threshold += faults["timeout_rate"]
        if value < threshold:
            return "timeout"
        threshold += faults["error_rate"]
        if value < threshold:
            return "error"
        return None

    def choose_disconnect_after(self, faults):
        if not faults["enabled"] or faults["disconnect_after_rate"] <= 0:
            return False
        with self._fault_lock:
            return self._random.random() < faults["disconnect_after_rate"]

    def delay_seconds(self, faults):
        if not faults["enabled"]:
            return 0
        jitter = 0
        if faults["jitter_ms"]:
            with self._fault_lock:
                jitter = self._random.uniform(0, faults["jitter_ms"])
        return (faults["fixed_delay_ms"] + jitter) / 1000.0


def extract_usage(response_body, request_model, config):
    usage = {}
    response_model = None
    try:
        document = json.loads(response_body.decode("utf-8"))
        if isinstance(document, dict):
            response_model = document.get("model")
            if isinstance(document.get("usage"), dict):
                usage = document["usage"]
    except (UnicodeDecodeError, json.JSONDecodeError):
        pass
    prompt = int_value(usage.get("prompt_tokens"))
    completion = int_value(usage.get("completion_tokens"))
    total = int_value(usage.get("total_tokens")) or prompt + completion
    has_breakdown = (
        "prompt_cache_hit_tokens" in usage
        or "prompt_cache_miss_tokens" in usage
    )
    hit = int_value(usage.get("prompt_cache_hit_tokens"))
    miss = int_value(usage.get("prompt_cache_miss_tokens"))
    inferred = 0
    if prompt and not has_breakdown:
        hit = 0
        miss = prompt
        inferred = 1
    elif prompt and hit + miss < prompt:
        miss += prompt - hit - miss
        inferred = 1

    rates = config["pricing"]["models"].get(request_model)
    cost_pico_usd = None
    rate_values = (None, None, None)
    if rates is not None:
        hit_rate = Decimal(str(rates["cache_hit_input_per_million"]))
        miss_rate = Decimal(str(rates["cache_miss_input_per_million"]))
        output_rate = Decimal(str(rates["output_per_million"]))
        pico = (
            Decimal(hit) * hit_rate
            + Decimal(miss) * miss_rate
            + Decimal(completion) * output_rate
        ) * Decimal(10 ** 6)
        cost_pico_usd = int(pico.quantize(Decimal("1"), rounding=ROUND_HALF_UP))
        rate_values = (str(hit_rate), str(miss_rate), str(output_rate))
    return {
        "response_model": response_model,
        "prompt_tokens": prompt,
        "prompt_cache_hit_tokens": hit,
        "prompt_cache_miss_tokens": miss,
        "completion_tokens": completion,
        "total_tokens": total,
        "cache_breakdown_inferred": inferred,
        "pricing_model": request_model if rates is not None else None,
        "cache_hit_rate_per_million": rate_values[0],
        "cache_miss_rate_per_million": rate_values[1],
        "output_rate_per_million": rate_values[2],
        "cost_pico_usd": cost_pico_usd,
    }


def empty_usage():
    return {
        "response_model": None,
        "prompt_tokens": 0,
        "prompt_cache_hit_tokens": 0,
        "prompt_cache_miss_tokens": 0,
        "completion_tokens": 0,
        "total_tokens": 0,
        "cache_breakdown_inferred": 0,
        "pricing_model": None,
        "cache_hit_rate_per_million": None,
        "cache_miss_rate_per_million": None,
        "output_rate_per_million": None,
        "cost_pico_usd": None,
    }


def create_handler(state):
    class GatewayHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "SeekDBAIGateway/1.0"

        def log_message(self, fmt, *args):
            sys.stderr.write(
                "%s - - [%s] %s\n"
                % (self.address_string(), self.log_date_time_string(), fmt % args)
            )

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/_gateway/health":
                self._send_json(200, {"status": "ok"})
            elif parsed.path == "/_gateway/faults":
                if self._authorize_admin():
                    self._send_json(200, state.get_faults())
            elif parsed.path == "/_gateway/stats":
                if self._authorize_admin():
                    values = parse_qs(parsed.query)
                    try:
                        limit = int(values.get("limit", ["50"])[0])
                    except ValueError:
                        self._send_json(400, {"error": "limit must be an integer"})
                        return
                    self._send_json(200, state.store.summary(limit))
            else:
                self._send_json(404, {"error": "not found"})

        def do_PUT(self):
            parsed = urlparse(self.path)
            if parsed.path != "/_gateway/faults":
                self._send_json(404, {"error": "not found"})
                return
            if not self._authorize_admin():
                return
            try:
                changes, _ = self._read_json_body()
                faults = state.update_faults(changes)
            except (ValueError, json.JSONDecodeError) as error:
                self._send_json(400, {"error": str(error)})
                return
            self._send_json(200, faults)

        def do_POST(self):
            parsed = urlparse(self.path)
            if parsed.path == "/_gateway/reset":
                if self._authorize_admin():
                    deleted = state.store.reset()
                    self._send_json(200, {"deleted_requests": deleted})
                return
            if parsed.path != state.config["public_path"]:
                self._send_json(404, {"error": "not found"})
                return
            self._proxy(parsed.path)

        def _proxy(self, path):
            started_at = utc_now()
            started = time.monotonic()
            request_id = uuid.uuid4().hex
            recorded = False
            raw = b""
            request_model = None
            event = {
                "request_id": request_id,
                "started_at": started_at,
                "path": path,
                "request_model": None,
                "response_model": None,
                "request_bytes": 0,
                "request_sha256": hashlib.sha256(b"").hexdigest(),
                "upstream_called": 0,
                "upstream_status": None,
                "client_status": None,
                "outcome": "gateway_error",
                "fault_type": None,
                "duration_ms": 0,
                "currency": state.config["pricing"]["currency"],
                "error_type": None,
            }
            event.update(empty_usage())

            def record_event():
                nonlocal recorded
                if recorded:
                    return
                recorded = True
                event["duration_ms"] = int((time.monotonic() - started) * 1000)
                try:
                    state.store.record(event)
                except Exception as error:
                    sys.stderr.write("failed to record gateway event: %s\n" % error)

            try:
                try:
                    body, raw = self._read_json_body()
                except (ValueError, json.JSONDecodeError) as error:
                    event["client_status"] = 400
                    event["error_type"] = "invalid_request"
                    record_event()
                    self._send_json(400, {"error": str(error)}, request_id)
                    return
                request_model = body.get("model") if isinstance(body, dict) else None
                if request_model is not None and not isinstance(request_model, str):
                    request_model = str(request_model)
                event["request_model"] = request_model
                event["request_bytes"] = len(raw)
                event["request_sha256"] = hashlib.sha256(raw).hexdigest()
                faults = state.get_faults()
                delay = state.delay_seconds(faults)
                if delay:
                    time.sleep(delay)
                pre_fault = state.choose_pre_fault(faults)
                if pre_fault == "disconnect_before":
                    event["fault_type"] = pre_fault
                    event["outcome"] = "injected_disconnect"
                    event["error_type"] = "injected_disconnect_before_upstream"
                    record_event()
                    self._disconnect()
                    return
                if pre_fault == "timeout":
                    event["fault_type"] = pre_fault
                    event["outcome"] = "injected_timeout"
                    event["error_type"] = "injected_timeout_before_upstream"
                    time.sleep(faults["timeout_delay_ms"] / 1000.0)
                    event["client_status"] = 504
                    record_event()
                    self._send_json(
                        504,
                        {"error": {"message": "injected gateway timeout"}},
                        request_id,
                    )
                    return
                if pre_fault == "error":
                    status = faults["error_status"]
                    event["fault_type"] = pre_fault
                    event["outcome"] = "injected_error"
                    event["client_status"] = status
                    event["error_type"] = "injected_http_%d" % status
                    record_event()
                    self._send_json(
                        status,
                        {"error": {"message": "injected gateway error"}},
                        request_id,
                    )
                    return

                status, response_body, response_headers = self._call_upstream(raw, request_id)
                event["upstream_called"] = 1
                event["upstream_status"] = status
                event.update(extract_usage(response_body, request_model, state.config))
                if state.choose_disconnect_after(faults) and 200 <= status <= 299:
                    event["fault_type"] = "disconnect_after"
                    event["outcome"] = "injected_response_loss"
                    event["error_type"] = "injected_disconnect_after_upstream"
                    record_event()
                    self._disconnect()
                    return
                event["client_status"] = status
                event["outcome"] = "forwarded"
                record_event()
                self._send_bytes(status, response_body, response_headers, request_id)
            except HTTPError as error:
                response_body = error.read()
                error.close()
                event["upstream_called"] = 1
                event["upstream_status"] = error.code
                event["client_status"] = error.code
                event["outcome"] = "forwarded_upstream_error"
                event["error_type"] = "upstream_http_%d" % error.code
                event.update(extract_usage(response_body, request_model, state.config))
                record_event()
                self._send_bytes(
                    error.code,
                    response_body,
                    dict(error.headers.items()),
                    request_id,
                )
            except (URLError, TimeoutError, socket.timeout, OSError) as error:
                event["upstream_called"] = 1
                event["client_status"] = 502
                event["outcome"] = "upstream_transport_error"
                event["error_type"] = type(error).__name__
                record_event()
                self._send_json(
                    502,
                    {"error": {"message": "upstream transport error"}},
                    request_id,
                )
            finally:
                record_event()

        def _call_upstream(self, raw, request_id):
            headers = {
                "Content-Type": self.headers.get("Content-Type", "application/json"),
                "Accept": "application/json",
                "User-Agent": "seekdb-ai-gateway/1.0",
                "X-Gateway-Request-Id": request_id,
            }
            incoming_auth = self.headers.get("Authorization", "")
            if incoming_auth:
                headers["Authorization"] = incoming_auth
            request = Request(
                state.config["upstream"]["url"],
                data=raw,
                headers=headers,
                method="POST",
            )
            response = urlopen(
                request,
                timeout=state.config["upstream"]["timeout_seconds"],
            )
            with response:
                return response.status, response.read(), dict(response.headers.items())

        def _read_json_body(self):
            content_length = self.headers.get("Content-Length")
            if content_length is None:
                raise ValueError("Content-Length is required")
            try:
                size = int(content_length)
            except ValueError:
                raise ValueError("invalid Content-Length")
            if size < 0 or size > state.config["max_request_bytes"]:
                raise ValueError("request body is too large")
            raw = self.rfile.read(size)
            if len(raw) != size:
                raise ValueError("incomplete request body")
            return json.loads(raw.decode("utf-8")), raw

        def _authorize_admin(self):
            env_name = state.config.get("admin_token_env")
            expected = os.environ.get(env_name, "") if env_name else ""
            if expected:
                supplied = self.headers.get("X-Gateway-Admin-Token", "")
                if hmac.compare_digest(supplied, expected):
                    return True
                self._send_json(401, {"error": "invalid admin token"})
                return False
            if self.client_address[0] in ("127.0.0.1", "::1"):
                return True
            self._send_json(403, {"error": "admin API requires AI_GATEWAY_ADMIN_TOKEN"})
            return False

        def _send_json(self, status, document, request_id=None):
            payload = json.dumps(document, ensure_ascii=False).encode("utf-8")
            self._send_bytes(
                status,
                payload,
                {"Content-Type": "application/json; charset=utf-8"},
                request_id,
            )

        def _send_bytes(self, status, payload, headers, request_id=None):
            try:
                self.send_response(status)
                sent_content_type = False
                for name, value in headers.items():
                    lower_name = name.lower()
                    if lower_name in SAFE_RESPONSE_HEADERS:
                        self.send_header(name, value)
                        sent_content_type = sent_content_type or lower_name == "content-type"
                if not sent_content_type:
                    self.send_header("Content-Type", "application/json")
                if request_id:
                    self.send_header("X-Gateway-Request-Id", request_id)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
            except (BrokenPipeError, ConnectionResetError, OSError):
                self.close_connection = True

        def _disconnect(self):
            self.close_connection = True
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self.connection.close()
            except OSError:
                pass

    return GatewayHandler


def create_server(config):
    state = GatewayState(config)
    server = ThreadingHTTPServer(
        (config["listen"]["host"], int(config["listen"]["port"])),
        create_handler(state),
    )
    server.daemon_threads = True
    server.gateway_state = state
    return server


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", help="gateway JSON configuration file")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("serve", help="run the HTTP gateway")
    stats = subparsers.add_parser("stats", help="print persisted statistics")
    stats.add_argument("--limit", type=int, default=50)
    subparsers.add_parser("reset", help="delete persisted request statistics")
    subparsers.add_parser("check-config", help="validate configuration")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    try:
        config = load_config(args.config)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit("invalid gateway configuration: %s" % error)
    if args.command == "serve":
        server = create_server(config)
        host, port = server.server_address[:2]
        sys.stderr.write(
            "SeekDB AI gateway listening on %s:%s, forwarding %s to %s\n"
            % (host, port, config["public_path"], config["upstream"]["url"])
        )
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    elif args.command == "stats":
        store = StatsStore(
            config["storage"]["sqlite_path"],
            config["pricing"]["currency"],
        )
        print(json.dumps(store.summary(args.limit), indent=2, ensure_ascii=False))
    elif args.command == "reset":
        store = StatsStore(
            config["storage"]["sqlite_path"],
            config["pricing"]["currency"],
        )
        print(json.dumps({"deleted_requests": store.reset()}))
    elif args.command == "check-config":
        print("gateway_config=ok")


if __name__ == "__main__":
    main()
