#!/usr/bin/env python3
"""Deterministic HTTP receive-error endpoint for the SEEK-511 regression."""

import argparse
import hashlib
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.request import urlopen


def pid_path(port):
    return Path("/tmp/seek511-curl56-{}.pid".format(port))


def log_path(port):
    return Path("/tmp/seek511-curl56-{}.log".format(port))


class FaultServer(ThreadingHTTPServer):
    request_queue_size = 16

    def __init__(self, address):
        super().__init__(address, Handler)
        self.request_count = 0
        self.requests_by_body = {}
        self.count_lock = threading.Lock()

    def record_request(self, body):
        with self.count_lock:
            self.request_count += 1
            body_hash = hashlib.sha256(body).hexdigest()
            self.requests_by_body[body_hash] = self.requests_by_body.get(body_hash, 0) + 1

    def stats(self):
        with self.count_lock:
            return {
                "curl56_requests": self.request_count,
                "requests_by_body": dict(self.requests_by_body),
            }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, _format, *_args):
        return

    def do_GET(self):
        if self.path not in ("/health", "/_test/stats"):
            self.send_error(404)
            return
        value = {"status": "ready"} if self.path == "/health" else self.server.stats()
        body = json.dumps(value, separators=(",", ":")).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        body = b""
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length > 0:
                body = self.rfile.read(length)
        except (OSError, ValueError):
            pass
        self.server.record_request(body)

        # Send a syntactically valid response header and then reset the TCP
        # connection before the declared body arrives. libcurl reports this as
        # CURLE_RECV_ERROR (56), which is the product branch under regression.
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", "1024")
        self.end_headers()
        self.wfile.flush()
        self.connection.setsockopt(
            socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0)
        )
        self.connection.close()
        self.close_connection = True


def serve(port, lifetime):
    server = FaultServer(("127.0.0.1", port))
    timer = threading.Timer(lifetime, server.shutdown)
    timer.daemon = True
    timer.start()
    try:
        server.serve_forever()
    finally:
        timer.cancel()
        server.server_close()


def read_pid(port):
    try:
        return int(pid_path(port).read_text(encoding="ascii").strip())
    except (FileNotFoundError, ValueError):
        return None


def start(port, lifetime):
    stop(port, quiet=True)
    log = log_path(port).open("ab")
    process = subprocess.Popen(
        [sys.executable, str(Path(__file__).resolve()), "serve",
         "--port", str(port), "--lifetime", str(lifetime)],
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=log,
        start_new_session=True,
    )
    log.close()
    pid_path(port).write_text(str(process.pid), encoding="ascii")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            with urlopen("http://127.0.0.1:{}/health".format(port), timeout=0.2) as response:
                if json.load(response).get("status") == "ready":
                    print("curl56_server=ready")
                    return
        except OSError:
            time.sleep(0.05)
    stop(port, quiet=True)
    raise RuntimeError("curl56 fault server did not become ready")


def stop(port, quiet=False):
    pid = read_pid(port)
    if pid is not None:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        pid_path(port).unlink(missing_ok=True)
    if not quiet:
        print("curl56_server=stopped")


def assert_requests(port, expected_total):
    with urlopen("http://127.0.0.1:{}/_test/stats".format(port), timeout=2) as response:
        stats = json.load(response)
    actual = int(stats["curl56_requests"])
    if actual != expected_total:
        raise RuntimeError(
            "expected {} curl56 requests, got {}".format(expected_total, actual)
        )
    if actual != sum(stats["requests_by_body"].values()):
        raise RuntimeError("inconsistent request counters: {}".format(stats))
    print("curl56_retry_budget=verified")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("start", "serve", "assert", "stop"))
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--lifetime", type=int, default=300)
    parser.add_argument("--expected-total", type=int)
    args = parser.parse_args()
    if args.command == "assert" and args.expected_total is None:
        parser.error("assert requires --expected-total")
    return args


def main():
    args = parse_args()
    if args.command == "serve":
        serve(args.port, args.lifetime)
    elif args.command == "start":
        start(args.port, args.lifetime)
    elif args.command == "assert":
        assert_requests(args.port, args.expected_total)
    else:
        stop(args.port)


if __name__ == "__main__":
    main()
