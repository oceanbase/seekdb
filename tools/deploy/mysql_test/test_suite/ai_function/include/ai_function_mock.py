#!/usr/bin/env python3
"""Deterministic mock AI service for the ai_function mysqltest suite.

A single process answers every endpoint the suite creates:

* ``/compatible-mode/v1/chat/completions`` - OpenAI compatible completion
* ``/compatible-mode/v1/embeddings``       - OpenAI compatible embedding
* ``/v1/rerank``                           - SiliconFlow compatible rerank
* ``/api/generate``                        - native Ollama completion
* ``/api/embed``                           - native Ollama embedding

The answers are deterministic and do not depend on the model name:

* completion text is derived from the request prompt, so it is never empty;
* embedding vectors always have 768 dimensions (or the requested ``dimensions``)
  and are returned in reverse input order, each one carrying its input index;
* rerank results are returned in reverse input order with a score derived from
  the document text, so callers must both sort and map the indexes back.

Usage:
    ai_function_mock.py start 8000    # background process, pid file
    ai_function_mock.py stop 8000
    ai_function_mock.py serve 8000    # foreground
    ai_function_mock.py reset 8000    # truncate the request log
"""

import json
import os
import signal
import socket
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_DIM = 768


def state_dir(port):
    return "/tmp/seekdb_ai_function_mock_%d" % port


def state_path(port, name):
    return os.path.join(state_dir(port), name)


def log_request(port, entry):
    with open(state_path(port, "events.jsonl"), "a", encoding="utf-8") as stream:
        stream.write(json.dumps(entry, ensure_ascii=False) + "\n")


def embedding_for(text, index, dim):
    vector = [0.0] * dim
    vector[0] = float(len(str(text)) + index + 1)
    if dim > 1:
        vector[1] = 0.5
    if dim > 2:
        vector[2] = 0.25
    return vector


def rerank_score(document, query):
    text = document if isinstance(document, str) else str(document.get("text", ""))
    if text == query:
        return 1.0
    # Deterministic pseudo relevance, independent of the position inside a
    # single request, so scores stay comparable across batches.
    digest = sum((i + 1) * ord(char) for i, char in enumerate(text))
    return round((digest % 900) / 1000.0, 4)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            return json.loads(raw.decode("utf-8")) if raw else {}
        except (UnicodeDecodeError, json.JSONDecodeError):
            return {"_raw": raw.decode("utf-8", "replace")}

    def _send(self, status, payload):
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        body = self._read_body()
        path = self.path.split("?")[0]
        log_request(self.server.server_port, {
            "path": path,
            "authorization": self.headers.get("Authorization"),
            "body": body,
        })

        if path.endswith("/chat/completions"):
            messages = body.get("messages") or [{}]
            content = messages[-1].get("content", "")
            if not isinstance(content, str):
                content = json.dumps(content, ensure_ascii=False)
            self._send(200, {
                "id": "mock-completion",
                "object": "chat.completion",
                "model": body.get("model", "mock"),
                "choices": [{
                    "index": 0,
                    "message": {"role": "assistant",
                                "content": "mock completion for: " + content},
                    "finish_reason": "stop",
                }],
                "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2},
            })
        elif path.endswith("/embeddings"):
            values = body.get("input", [])
            if not isinstance(values, list):
                values = [values]
            dim = body.get("dimensions") or DEFAULT_DIM
            data = [
                {"object": "embedding", "index": index,
                 "embedding": embedding_for(value, index, dim)}
                for index, value in reversed(list(enumerate(values)))
            ]
            self._send(200, {"object": "list", "data": data, "model": body.get("model", "mock")})
        elif path.endswith("/rerank"):
            documents = body.get("documents", [])
            if not isinstance(documents, list):
                documents = [documents]
            query = str(body.get("query", ""))
            results = [
                {"index": index, "relevance_score": rerank_score(documents[index], query)}
                for index in reversed(range(len(documents)))
            ]
            self._send(200, {"id": "mock-rerank", "results": results})
        elif path.endswith("/api/generate"):
            prompt = str(body.get("prompt", ""))
            self._send(200, {
                "model": body.get("model", "mock"),
                "created_at": "2026-01-01T00:00:00Z",
                "response": "mock ollama response for: " + prompt,
                "done": True,
            })
        elif path.endswith("/api/embed"):
            values = body.get("input", [])
            if not isinstance(values, list):
                values = [values]
            self._send(200, {
                "model": body.get("model", "mock"),
                "embeddings": [embedding_for(value, index, DEFAULT_DIM)
                               for index, value in enumerate(values)],
            })
        else:
            self._send(404, {"error": {"message": "unknown path: " + path}})


def serve(port):
    os.makedirs(state_dir(port), exist_ok=True)
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    server.serve_forever()


def start_server(port):
    os.makedirs(state_dir(port), exist_ok=True)
    for name in ("events.jsonl", "server.out"):
        with open(state_path(port, name), "w", encoding="utf-8"):
            pass
    output = open(state_path(port, "server.out"), "a", encoding="utf-8")
    process = subprocess.Popen(
        [sys.executable, os.path.abspath(__file__), "serve", str(port)],
        stdin=subprocess.DEVNULL,
        stdout=output,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    with open(state_path(port, "server.pid"), "w", encoding="ascii") as stream:
        stream.write(str(process.pid))
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.05):
                return
        except OSError:
            if process.poll() is not None:
                break
            time.sleep(0.02)
    raise SystemExit("mock ai service did not start on port %d" % port)


def stop_server(port):
    pid_path = state_path(port, "server.pid")
    if not os.path.exists(pid_path):
        return
    try:
        with open(pid_path, "r", encoding="ascii") as stream:
            pid = int(stream.read().strip())
        os.kill(pid, signal.SIGTERM)
        for _ in range(50):
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                break
            time.sleep(0.02)
    except (ValueError, ProcessLookupError):
        pass
    os.remove(pid_path)


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: ai_function_mock.py MODE PORT")
    mode = sys.argv[1]
    port = int(sys.argv[2])
    if mode == "start":
        start_server(port)
    elif mode == "stop":
        stop_server(port)
    elif mode == "serve":
        serve(port)
    elif mode == "reset":
        os.makedirs(state_dir(port), exist_ok=True)
        with open(state_path(port, "events.jsonl"), "w", encoding="utf-8"):
            pass
    else:
        raise SystemExit("unknown mode: " + mode)


if __name__ == "__main__":
    main()
