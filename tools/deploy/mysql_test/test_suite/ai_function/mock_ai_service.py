#!/usr/bin/env python3
"""Deterministic HTTP fixture for ai_function_enhancement.test."""

import argparse
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    requests = []
    lock = threading.Lock()

    def log_message(self, *arguments):
        pass

    def respond(self, value, status=200):
        payload = json.dumps(value, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        self.respond(self.statistics())

    @classmethod
    def statistics(cls):
        with cls.lock:
            inputs = [body.get("input", []) for _, body in cls.requests]
            return {
                "request_count": len(cls.requests),
                "max_embed_batch": max(
                    (len(value) for value in inputs if isinstance(value, list)),
                    default=0,
                ),
            }

    def do_POST(self):
        try:
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            if not isinstance(body, dict):
                raise ValueError("request must be an object")
            if self.headers.get_content_type() != "application/json":
                raise ValueError("JSON content type is required")
            if not body.get("model"):
                raise ValueError("model is required")
            if self.path.startswith("/api/"):
                if self.headers.get("Authorization"):
                    raise ValueError("native Ollama requests should not require authentication")
            elif not self.headers.get("Authorization", "").startswith("Bearer "):
                raise ValueError("OpenAI requests require authentication")

            if self.path == "/stats":
                self.respond({"choices": [{"message": {"content": json.dumps(self.statistics())}}]})
                return
            with self.lock:
                self.requests.append((self.path, body))
            if self.path == "/disconnect":
                self.close_connection = True
                return
            if self.path == "/malformed":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"{invalid")
                return
            if self.path == "/fail-later" and any(len(value) > 20 for value in body["input"]):
                self.respond({"error": "later batch failed"}, 400)
                return
            if self.path == "/error":
                self.respond({"error": "test error"}, 400)
                return
            if self.path == "/array":
                self.respond([])
                return
            if self.path == "/empty":
                self.respond({"choices": [{"message": {"content": ""}}]})
                return
            if self.path == "/wrong-content":
                self.respond({"choices": [{"message": {"content": []}}]})
                return

            if "messages" in body or "prompt" in body:
                if self.path.startswith("/api/") and body.get("stream") is not False:
                    raise ValueError("native Ollama generation must disable streaming")
                content = body.get("prompt", "")
                if "messages" in body:
                    content = body["messages"][-1]["content"]
                if self.path.endswith("inspect"):
                    content = json.dumps(body, ensure_ascii=True)
                if self.path.startswith("/api/"):
                    self.respond({"response": content, "done": True})
                else:
                    self.respond({"choices": [{"message": {"content": content}}]})
            elif "input" in body:
                dimension = body.get("dimensions", 3)
                if not isinstance(dimension, int) or dimension <= 0:
                    raise ValueError("invalid dimensions")
                embeddings = [
                    [len(value.encode("utf-8"))] + list(range(1, dimension))
                    for value in body["input"]
                ]
                if self.path == "/bad-vector":
                    embeddings[0] = ["invalid"] * dimension
                elif self.path == "/bad-dimension":
                    embeddings[0].append(0)
                elif self.path == "/non-finite":
                    embeddings[0][0] = 1e100
                if self.path.startswith("/api/"):
                    self.respond({"embeddings": embeddings})
                else:
                    data = [{"index": index, "embedding": value}
                            for index, value in enumerate(embeddings)]
                    if self.path == "/duplicate":
                        data[-1]["index"] = 0
                    elif self.path == "/bad-index":
                        data[0]["index"] = len(data)
                    elif self.path == "/missing":
                        data.pop()
                    self.respond({"data": list(reversed(data))})
            elif "documents" in body:
                results = [{"index": index, "relevance_score": len(value)}
                           for index, value in enumerate(body["documents"])]
                if self.path == "/bad-rerank":
                    results[0]["index"] = len(results)
                elif self.path == "/duplicate-rerank":
                    results[-1]["index"] = 0
                elif self.path == "/missing-rerank":
                    results.pop()
                self.respond({"results": list(reversed(results))})
            else:
                raise ValueError("unknown request")
        except (KeyError, TypeError, ValueError) as error:
            self.respond({"error": str(error)}, 400)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000)
    arguments = parser.parse_args()
    with ThreadingHTTPServer(("127.0.0.1", arguments.port), Handler) as server:
        print("Mock AI Service listening on 127.0.0.1:{}".format(arguments.port), flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
