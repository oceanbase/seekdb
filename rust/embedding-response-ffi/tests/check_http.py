#!/usr/bin/env python3
"""Check actual production task HTTP bodies and decoded results against a local server."""
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
import argparse
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter", type=Path, required=True)
    args = parser.parse_args()
    received = []
    errors = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            try:
                length = int(self.headers["Content-Length"])
                body = self.rfile.read(length)
                request = json.loads(body)
                assert self.headers["Content-Type"] == "application/json"
                assert self.headers["Authorization"] == "Bearer mock-key"
                received.append((self.path, request))
                # Leave time between receiving the request and returning the response.
                time.sleep(0.01)
                vector = [1.25, -2.5]
                if request["encoding_format"] == "base64":
                    vector = base64.b64encode(struct.pack("=ff", *vector)).decode()
                data = [{"embedding": vector} for _ in request["input"]] if "dimensions" in request else []
                response = json.dumps({"data": data}).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(response)))
                self.end_headers()
                self.wfile.write(response)
            except Exception as error:
                errors.append(repr(error))
                self.send_error(500)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix="embedding-http-") as directory:
                env = dict(os.environ, NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
                subprocess.run([str(args.adapter.resolve()), "--http",
                                f"http://127.0.0.1:{server.server_port}"],
                               cwd=directory, env=env, check=True, timeout=100)
        finally:
            server.shutdown()
            thread.join()
    assert not errors, errors
    texts = ["", "".join(chr(i) for i in range(32)), "\0" * 4096, '中文🙂"\\/\n']
    texts.extend(f"chunk-{i}" for i in range(4, 23))
    expected = []
    for scenario in ["float", "base64", "silicon", "zero", "negative"]:
        items = texts[:1] if scenario in ("zero", "negative") else texts
        for start in range(0, len(items), 10):
            request = {"input": items[start:start + 10], "model": 'model"\\\n' + "x" * 4096,
                       "encoding_format": "base64" if scenario in ("base64", "silicon") else "float"}
            if scenario not in ("zero", "negative"):
                request["dimensions"] = 2
            expected.append(("/" + scenario, request))
    assert received == expected, "HTTP bodies or batch order differ; empty input must send no request"
    print(f"PASS: {len(received)} captured HTTP requests, escaped text, long model, batches, formats, optional dimensions and empty input")


if __name__ == "__main__":
    main()
