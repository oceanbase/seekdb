#!/usr/bin/env python3
"""Tests for the standalone SeekDB AI evaluation gateway."""

import http.client
import json
import os
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

import gateway


class FakeProvider:
    def __init__(self):
        self.events = []
        self.lock = threading.Lock()

        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt, *args):
                return

            def do_POST(self):
                size = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(size)
                with owner.lock:
                    owner.events.append(
                        {
                            "path": self.path,
                            "body": raw,
                            "authorization": self.headers.get("Authorization"),
                            "gateway_request_id": self.headers.get(
                                "X-Gateway-Request-Id"
                            ),
                        }
                    )
                response = {
                    "id": "fake-completion",
                    "model": "deepseek-v4-pro-202608",
                    "choices": [
                        {
                            "index": 0,
                            "message": {
                                "role": "assistant",
                                "content": "English",
                            },
                            "finish_reason": "stop",
                        }
                    ],
                    "usage": {
                        "prompt_tokens": 110,
                        "prompt_cache_hit_tokens": 100,
                        "prompt_cache_miss_tokens": 10,
                        "completion_tokens": 20,
                        "total_tokens": 130,
                    },
                }
                payload = json.dumps(response, separators=(",", ":")).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("X-Request-Id", "fake-provider-request")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @property
    def url(self):
        return "http://127.0.0.1:%d/chat/completions" % self.server.server_port

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


class GatewayTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.provider = FakeProvider()
        self.saved_environment = {
            name: os.environ.get(name)
            for name in ("TEST_AI_GATEWAY_ADMIN_TOKEN",)
        }
        os.environ["TEST_AI_GATEWAY_ADMIN_TOKEN"] = "admin-secret"
        config = gateway.deep_merge(
            gateway.DEFAULT_CONFIG,
            {
                "listen": {"host": "127.0.0.1", "port": 0},
                "upstream": {
                    "url": self.provider.url,
                    "timeout_seconds": 2,
                },
                "admin_token_env": "TEST_AI_GATEWAY_ADMIN_TOKEN",
                "storage": {
                    "sqlite_path": os.path.join(
                        self.tempdir.name, "gateway.sqlite3"
                    )
                },
                "pricing": {
                    "currency": "USD",
                    "models": {
                        "deepseek-v4-pro": {
                            "cache_hit_input_per_million": "0.003625",
                            "cache_miss_input_per_million": "0.435",
                            "output_per_million": "0.87",
                        }
                    },
                },
            },
        )
        gateway.validate_config(config)
        self.server = gateway.create_server(config)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = "http://127.0.0.1:%d" % self.server.server_port

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.provider.close()
        self.tempdir.cleanup()
        for name, value in self.saved_environment.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value

    def _completion(self):
        document = {
            "model": "deepseek-v4-pro",
            "messages": [
                {"role": "system", "content": "Output the language name."},
                {"role": "user", "content": "OceanBase is a database."},
            ],
        }
        raw = json.dumps(document, separators=(",", ":")).encode("utf-8")
        request = Request(
            self.base_url + "/v1/chat/completions",
            data=raw,
            headers={
                "Authorization": "Bearer deepseek-secret",
                "Content-Type": "application/json",
            },
            method="POST",
        )
        return request, raw

    def _admin(self, path, method="GET", document=None):
        raw = None
        headers = {"X-Gateway-Admin-Token": "admin-secret"}
        if document is not None:
            raw = json.dumps(document).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = Request(
            self.base_url + path,
            data=raw,
            headers=headers,
            method=method,
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.loads(response.read().decode("utf-8"))

    def test_transparent_forwarding_and_cost_accounting(self):
        request, raw = self._completion()
        with urlopen(request, timeout=2) as response:
            payload = json.loads(response.read().decode("utf-8"))
            self.assertEqual(200, response.status)
            self.assertEqual("fake-provider-request", response.headers["X-Request-Id"])
        self.assertEqual("English", payload["choices"][0]["message"]["content"])
        self.assertEqual(1, len(self.provider.events))
        event = self.provider.events[0]
        self.assertEqual("/chat/completions", event["path"])
        self.assertEqual(raw, event["body"])
        self.assertEqual("Bearer deepseek-secret", event["authorization"])
        self.assertTrue(event["gateway_request_id"])

        _, stats = self._admin("/_gateway/stats?limit=5")
        self.assertEqual(1, stats["requests_received"])
        self.assertEqual(1, stats["upstream_calls"])
        self.assertEqual(100, stats["prompt_cache_hit_tokens"])
        self.assertEqual(10, stats["prompt_cache_miss_tokens"])
        self.assertEqual(20, stats["completion_tokens"])
        self.assertEqual(0.909091, stats["cache_hit_ratio"])
        self.assertEqual("0.000022112500", stats["estimated_cost"]["amount"])
        self.assertEqual(0, stats["unpriced_upstream_calls"])
        self.assertEqual("forwarded", stats["recent"][0]["outcome"])

    def test_injected_http_error_does_not_call_or_charge_provider(self):
        _, faults = self._admin(
            "/_gateway/faults",
            method="PUT",
            document={
                "enabled": True,
                "error_rate": 1.0,
                "error_status": 429,
                "disconnect_before_rate": 0.0,
                "timeout_rate": 0.0,
                "disconnect_after_rate": 0.0,
            },
        )
        self.assertTrue(faults["enabled"])
        request, _ = self._completion()
        with self.assertRaises(HTTPError) as raised:
            urlopen(request, timeout=2)
        self.assertEqual(429, raised.exception.code)
        raised.exception.read()
        raised.exception.close()
        self.assertEqual([], self.provider.events)
        _, stats = self._admin("/_gateway/stats")
        self.assertEqual(1, stats["injected_faults"])
        self.assertEqual(0, stats["upstream_calls"])
        self.assertEqual("0.000000000000", stats["estimated_cost"]["amount"])

    def test_delay_is_applied_before_transparent_forwarding(self):
        self._admin(
            "/_gateway/faults",
            method="PUT",
            document={
                "enabled": True,
                "fixed_delay_ms": 80,
                "jitter_ms": 0,
            },
        )
        request, _ = self._completion()
        started = time.monotonic()
        with urlopen(request, timeout=2) as response:
            self.assertEqual(200, response.status)
        self.assertGreaterEqual(time.monotonic() - started, 0.07)

    def test_response_loss_after_provider_success_is_still_charged(self):
        self._admin(
            "/_gateway/faults",
            method="PUT",
            document={
                "enabled": True,
                "disconnect_after_rate": 1.0,
            },
        )
        request, _ = self._completion()
        with self.assertRaises(
            (http.client.RemoteDisconnected, URLError, ConnectionResetError)
        ):
            urlopen(request, timeout=2)
        for _ in range(50):
            _, stats = self._admin("/_gateway/stats")
            if stats["requests_received"]:
                break
            time.sleep(0.01)
        self.assertEqual(1, len(self.provider.events))
        self.assertEqual(1, stats["upstream_calls"])
        self.assertEqual("0.000022112500", stats["estimated_cost"]["amount"])
        self.assertEqual(
            "injected_response_loss", stats["recent"][0]["outcome"]
        )

    def test_admin_reset_deletes_persisted_statistics(self):
        request, _ = self._completion()
        with urlopen(request, timeout=2):
            pass
        status, result = self._admin("/_gateway/reset", method="POST")
        self.assertEqual(200, status)
        self.assertEqual(1, result["deleted_requests"])
        _, stats = self._admin("/_gateway/stats")
        self.assertEqual(0, stats["requests_received"])


if __name__ == "__main__":
    unittest.main()
