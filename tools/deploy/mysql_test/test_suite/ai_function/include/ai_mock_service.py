#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Deterministic Mock AI Service for the seekdb AI competition tasks.

The competition harness runs this service on 0.0.0.0:8000 and points the
DBMS_AI_SERVICE endpoints at it.  Every response is deterministic so that the
mysqltest expectations stay stable.

Supported routes:

  POST /compatible-mode/v1/chat/completions   OpenAI compatible completion
  POST /compatible-mode/v1/embeddings          OpenAI compatible embedding
  POST /v1/rerank                               SiliconFlow style rerank
  POST /api/generate                            Ollama native completion
  POST /api/embed                               Ollama native embedding

Usage:
    python3 ai_mock_service.py [--host 0.0.0.0] [--port 8000]
"""

import argparse
import hashlib
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_DIMENSION = 768


def _digest(*parts):
    h = hashlib.sha256()
    for p in parts:
        h.update(str(p).encode("utf-8"))
        h.update(b"\x00")
    return h.digest()


def _unit_vector(text, dimension=DEFAULT_DIMENSION):
    """Deterministic pseudo embedding in [-1, 1]."""
    raw = _digest("embedding", text)
    values = []
    counter = 0
    while len(values) < dimension:
        raw = _digest("embedding", text, counter)
        counter += 1
        for i in range(0, len(raw), 2):
            if len(values) >= dimension:
                break
            value = int.from_bytes(raw[i:i + 2], "big") / 32767.0 - 1.0
            values.append(round(value, 6))
    return values


def _read_body(handler):
    length = int(handler.headers.get("Content-Length") or 0)
    if length <= 0:
        return {}
    raw = handler.rfile.read(length)
    try:
        return json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return {}


def _as_text(value):
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def _openai_completion(model, prompt):
    return {
        "id": "mock-chatcmpl-1",
        "object": "chat.completion",
        "created": 0,
        "model": model or "mock-model",
        "choices": [{
            "index": 0,
            "message": {
                "role": "assistant",
                "content": "mock completion: " + _as_text(prompt)[:512],
            },
            "finish_reason": "stop",
        }],
        "usage": {
            "prompt_tokens": len(_as_text(prompt)) // 4,
            "completion_tokens": 16,
            "total_tokens": 16 + len(_as_text(prompt)) // 4,
        },
    }


def _openai_embedding(model, inputs, dimension=None):
    if isinstance(inputs, str):
        inputs = [inputs]
    if not isinstance(inputs, list):
        inputs = [_as_text(inputs)]
    dim = int(dimension) if dimension else DEFAULT_DIMENSION
    data = []
    for index, item in enumerate(inputs):
        data.append({
            "object": "embedding",
            "index": index,
            "embedding": _unit_vector(_as_text(item), dim),
        })
    return {
        "object": "list",
        "data": data,
        "model": model or "mock-model",
        "usage": {"prompt_tokens": 1, "total_tokens": 1},
    }


def _tokens(text):
    return [t for t in "".join(ch.lower() if ch.isalnum() else " " for ch in text).split() if t]


def _relevance(query, text):
    """Deterministic lexical relevance of a document to the query.

    The mock does not need to mimic a real model, but the ranking must be
    stable and meaningful: documents sharing more terms with the query rank
    first, and the original order breaks ties.
    """
    query_tokens = _tokens(query)
    doc_tokens = _tokens(text)
    if not query_tokens:
        return 0.0
    overlap = sum(1 for t in query_tokens if t in doc_tokens)
    return overlap / float(len(query_tokens))


def _rerank(model, query, documents, top_n=None, return_documents=False):
    """Score documents against the query and keep the input order for ties."""
    if isinstance(documents, str):
        try:
            documents = json.loads(documents)
        except ValueError:
            documents = [documents]
    if not isinstance(documents, list):
        documents = []

    results = []
    for index, doc in enumerate(documents):
        if isinstance(doc, dict):
            doc_id = doc.get("id", str(index))
            text = _as_text(doc.get("text", doc.get("content", "")))
        else:
            doc_id = str(index)
            text = _as_text(doc)
        score = _relevance(query, text)
        item = {
            "index": index,
            "id": doc_id,
            "relevance_score": round(score, 6),
        }
        if return_documents:
            item["document"] = {"text": text} if not isinstance(doc, dict) else doc
        results.append(item)

    # stable sort: higher score first, original index breaks the tie
    results.sort(key=lambda item: (-item["relevance_score"], item["index"]))
    if top_n:
        results = results[: int(top_n)]
    return {
        "id": "mock-rerank-1",
        "model": model or "mock-model",
        "object": "rerank.result",
        "results": results,
        "usage": {"total_tokens": len(documents)},
    }


def _ollama_generate(model, prompt):
    return {
        "model": model or "mock-model",
        "created_at": "1970-01-01T00:00:00Z",
        "response": "mock ollama: " + _as_text(prompt)[:512],
        "done": True,
        "done_reason": "stop",
        "context": [],
        "eval_count": 16,
        "prompt_eval_count": 1,
    }


def _ollama_embed(model, inputs):
    if isinstance(inputs, str):
        inputs = [inputs]
    if not isinstance(inputs, list):
        inputs = [_as_text(inputs)]
    return {
        "model": model or "mock-model",
        "embeddings": [_unit_vector(_as_text(item)) for item in inputs],
    }


class MockAIHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # keep the bench output clean
        pass

    def _send_json(self, payload, status=200):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") in ("/health", "/healthz"):
            self._send_json({"status": "ok"})
        else:
            self._send_json({"error": "not found"}, 404)

    def do_POST(self):
        body = _read_body(self)
        path = self.path.split("?")[0].rstrip("/")
        model = body.get("model")

        if path == "/compatible-mode/v1/chat/completions":
            prompt = body.get("messages") or body.get("prompt") or ""
            self._send_json(_openai_completion(model, prompt))
        elif path == "/compatible-mode/v1/embeddings":
            inputs = body.get("input")
            if inputs is None:
                inputs = body.get("inputs")
            dimension = body.get("dimensions") or body.get("dimension")
            self._send_json(_openai_embedding(model, inputs, dimension))
        elif path == "/v1/rerank":
            self._send_json(_rerank(
                model,
                body.get("query") or "",
                body.get("documents") or body.get("input") or [],
                body.get("top_n"),
                bool(body.get("return_documents")),
            ))
        elif path == "/api/generate":
            prompt = body.get("prompt") or body.get("messages") or ""
            self._send_json(_ollama_generate(model, prompt))
        elif path == "/api/embed":
            inputs = body.get("input")
            if inputs is None:
                inputs = body.get("prompt")
            self._send_json(_ollama_embed(model, inputs))
        else:
            self._send_json({"error": "not found: " + path}, 404)


def main():
    parser = argparse.ArgumentParser(description="Mock AI Service for seekdb AI tasks")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), MockAIHandler)
    server.daemon_threads = True
    print("mock ai service listening on %s:%d" % (args.host, args.port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
