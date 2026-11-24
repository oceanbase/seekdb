"""Local embedding endpoint for the hybrid index mysqltest regression."""

from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from threading import Thread


class EmbeddingHandler(BaseHTTPRequestHandler):
    def log_message(self, _format, *_args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        inputs = request["input"]
        if isinstance(inputs, str):
            inputs = [inputs]
        dimensions = int(request.get("dimensions", request.get("dimension", 1024)))
        vector = [0.1 * (index % 10) for index in range(dimensions)]
        response = json.dumps({
            "object": "list",
            "data": [{"object": "embedding", "index": index, "embedding": vector}
                     for index, _ in enumerate(inputs)],
            "model": request["model"],
            "usage": {"prompt_tokens": len(inputs), "total_tokens": len(inputs)},
        }).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)


@contextmanager
def embedding_endpoint():
    server = ThreadingHTTPServer(("127.0.0.1", 0), EmbeddingHandler)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield "http://127.0.0.1:{}/v1/embeddings".format(server.server_port)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
