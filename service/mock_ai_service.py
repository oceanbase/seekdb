#!/usr/bin/env python3
"""
Mock AI Service for SeekDB (seekdb) AI-function self-testing.

Pretends to be the AI providers that the seekdb AI SQL functions talk to, so
ai_complete / ai_embed / ai_rerank / ai_prompt can be tested end-to-end without
a real AI provider (and without spending money).

Pure Python standard library only -- NO `pip install` required.

Run:      python3 mock_ai_service.py
Listens:  http://0.0.0.0:8000   (matches the grading environment requirement)

The response formats below mirror what seekdb parses in
  src/sql/engine/expr/ob_expr_ai/ob_ai_func_utils.cpp

  OpenAI completion   -> {"choices": [{"message": {"content": "..."}}]}
  OpenAI embedding    -> {"data": [{"index": i, "embedding": [...]}, ...]}
  Ollama completion   -> {"response": "..."}
  Ollama embedding    -> {"embeddings": [[...], ...]}
  Siliconflow rerank  -> {"results": [{"index": i, "relevance_score": s}, ...]}
  Dashscope rerank    -> {"output": {"results": [...]}}
  Dashscope embedding -> {"output": {"embeddings": [{"embedding": [...]}, ...]}}

Deterministic behaviours used by self_test.test:
  * completion echoes the received prompt with an "OK:" prefix.
  * embedding returns a vector of length `dimensions` (default 4) whose first
    element encodes the input's 0-based index (input 0 -> 1.0, 1 -> 2.0, ...).
  * rerank assigns doc i the score `1.0 - 0.1*i` and returns results in REVERSE
    index order, forcing seekdb to sort by relevance score itself.
"""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "0.0.0.0"
PORT = 8000
DEFAULT_DIM = 4


def make_vector(idx, dim):
    """Deterministic vector of length `dim`; first element encodes `idx`."""
    v = [0.0] * dim
    v[0] = float(idx + 1)
    return v


def _requested_dim(body, params=None):
    d = (params or body or {}).get("dimensions")
    if d is None and params is not None:
        d = params.get("dimension")
    try:
        return int(d) if d is not None else DEFAULT_DIM
    except (TypeError, ValueError):
        return DEFAULT_DIM


# ---------------------------------------------------------------------------
# completion
# ---------------------------------------------------------------------------
def openai_complete(body):
    prompt = ""
    for m in body.get("messages") or []:
        if isinstance(m, dict) and m.get("role") == "user":
            prompt = m.get("content", "")
    return {"choices": [{"message": {"content": "OK:" + str(prompt)}}]}


def ollama_complete(body):
    return {"response": "OK:" + str(body.get("prompt", ""))}


# ---------------------------------------------------------------------------
# embedding
# ---------------------------------------------------------------------------
def openai_embed(body):
    inputs = body.get("input") or []
    dim = _requested_dim(body)
    return {"data": [{"index": i, "embedding": make_vector(i, dim)}
                     for i in range(len(inputs))]}


def ollama_embed(body):
    inputs = body.get("input") or []
    dim = _requested_dim(body)
    return {"embeddings": [make_vector(i, dim) for i in range(len(inputs))]}


def dashscope_embed(body):
    inp = body.get("input") or {}
    inputs = inp.get("texts") or []
    dim = _requested_dim(body, body.get("parameters"))
    return {"output": {"embeddings": [{"embedding": make_vector(i, dim)}
                                      for i in range(len(inputs))]}}


# ---------------------------------------------------------------------------
# rerank
# ---------------------------------------------------------------------------
def _rerank_results(docs):
    n = len(docs)
    results = [{"index": i, "relevance_score": round(1.0 - 0.1 * i, 2)}
               for i in range(n)]
    results.reverse()  # deliberately unsorted -> seekdb must sort
    return results


def siliconflow_rerank(body):
    return {"results": _rerank_results(body.get("documents") or [])}


def dashscope_rerank(body):
    inp = body.get("input") or {}
    return {"output": {"results": _rerank_results(inp.get("documents") or [])}}


# ---------------------------------------------------------------------------
# routing
# ---------------------------------------------------------------------------
ROUTES = {
    "/openai/chat": openai_complete,
    "/openai/embed": openai_embed,
    "/ollama/chat": ollama_complete,
    "/ollama/embed": ollama_embed,
    "/rerank": siliconflow_rerank,
    "/dashscope/rerank": dashscope_rerank,
    "/dashscope/embed": dashscope_embed,
    # negative case: empty/invalid response -> ai_complete must error
    "/openai/chat/bad": lambda body: {},
}


def fallback_route(body):
    """Body-based routing as a safety net when the URL path is unknown."""
    if "messages" in body:
        return openai_complete(body)
    if "prompt" in body and "stream" in body:
        return ollama_complete(body)
    inp = body.get("input")
    if isinstance(inp, dict):
        if "texts" in inp:
            return dashscope_embed(body)
        if "documents" in inp:
            return dashscope_rerank(body)
    if "query" in body and "documents" in body:
        return siliconflow_rerank(body)
    if "input" in body:
        return openai_embed(body)
    return None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # we log with our own format below

    def _send_json(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        body = {}
        if raw:
            try:
                body = json.loads(raw.decode("utf-8"))
            except Exception:
                body = {}
        path = self.path.split("?")[0].rstrip("/") or "/"
        print("[mock] POST %s  body=%s" % (path, json.dumps(body, ensure_ascii=False)),
              flush=True)
        handler = ROUTES.get(path) or fallback_route
        resp = handler(body) if handler else {"error": "no route for " + path}
        if resp is None:
            resp = {"error": "no route for " + path}
        self._send_json(resp)

    def do_GET(self):
        self._send_json({"ok": True, "service": "seekdb mock ai service"})


if __name__ == "__main__":
    print("[mock] listening on %s:%d" % (HOST, PORT), flush=True)
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
