#!/usr/bin/env python3
import http.server
import json
import socketserver
import sys

PORT = 8000

class AIMockHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rpath = self.path
        post_data = self.rfile.read(content_length) if content_length > 0 else b""
        
        try:
            data = json.loads(post_data.decode('utf-8')) if post_data else {}
        except Exception:
            data = {}

        response_data = {}
        path = self.path

        if path == "/v1/chat/completions":
            response_data = {
                "id": "chatcmpl-mock",
                "object": "chat.completion",
                "created": 1677652288,
                "model": data.get("model", "gpt-4"),
                "choices": [{
                    "index": 0,
                    "message": {
                        "role": "assistant",
                        "content": "Mocked OpenAI chat completion response."
                    },
                    "finish_reason": "stop"
                }],
                "usage": {"prompt_tokens": 10, "completion_tokens": 10, "total_tokens": 20}
            }
        elif path == "/v1/embeddings":
            input_data = data.get("input", [])
            if isinstance(input_data, str):
                input_data = [input_data]
            
            embeddings = []
            for i in range(len(input_data)):
                embeddings.append({
                    "object": "embedding",
                    "embedding": [0.1 + i*0.01, 0.2, 0.3, 0.4, 0.5],
                    "index": i
                })
            
            response_data = {
                "object": "list",
                "data": embeddings,
                "model": data.get("model", "text-embedding-v1"),
                "usage": {"prompt_tokens": 5 * len(input_data), "total_tokens": 5 * len(input_data)}
            }
        elif path == "/v1/rerank":
            documents = data.get("documents", [])
            results = []
            for i in range(len(documents)):
                # Default score: higher index -> lower score to test ordering
                # Except if we want to test ties
                score = 0.9 - i * 0.1
                if score < 0: score = 0.0
                results.append({"index": i, "relevance_score": score})
            
            response_data = {
                "id": "rerank-mock",
                "results": results,
                "usage": {"total_tokens": 15}
            }
        elif path in ["/api/generate", "/api/chat"]:
            response_data = {
                "model": data.get("model", "llama3"),
                "response": "Mocked Ollama response.",
                "message": {"role": "assistant", "content": "Mocked Ollama message."},
                "done": True,
                "total_duration": 123456789
            }
        elif path in ["/api/embed", "/api/embeddings"]:
            input_data = data.get("input", [])
            if isinstance(input_data, str):
                input_data = [input_data]
            
            embeddings = []
            for i in range(len(input_data)):
                embeddings.append([0.1 + i*0.01, 0.2, 0.3])
            
            response_data = {
                "model": data.get("model", "llama3"),
                "embeddings": embeddings,
                "embedding": embeddings[0] if embeddings else []
            }
        else:
            self.send_response(404)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": f"Endpoint not found: {path}"}).encode('utf-8'))
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response_data).encode('utf-8'))

    def do_GET(self):
        path = self.path
        response_data = {}
        if path == "/v1/models":
            response_data = {
                "object": "list",
                "data": [
                    {"id": "gpt-4", "object": "model"},
                    {"id": "text-embedding-v1", "object": "model"}
                ]
            }
        elif path == "/api/tags":
            response_data = {
                "models": [
                    {"name": "llama3:latest", "model": "llama3:latest"}
                ]
            }
        else:
            self.send_response(404)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": f"Endpoint not found: {path}"}).encode('utf-8'))
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(response_data).encode('utf-8'))

    def log_message(self, format, *args):
        # Silence or customize logging if needed
        print(f"[MockAIService] {self.client_address[0]} - - {format%args}")

if __name__ == "__main__":
    server_address = ("0.0.0.0", PORT)
    with socketserver.TCPServer(server_address, AIMockHandler) as httpd:
        print(f"Mock AI Service running on 0.0.0.0:{PORT}...")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down Mock AI Service.")
