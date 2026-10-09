#!/usr/bin/env python3
"""Exercise child hybrid vector maintenance with a loopback embedding endpoint."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import resource
import threading

from namespace_inprocess_prototype import connect
from namespace_fork_prototype import Experiment


class Endpoint(BaseHTTPRequestHandler):
    requests = 0

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        type(self).requests += 1
        body = json.dumps({
            "object": "list",
            "data": [{"object": "embedding", "index": 0,
                      "embedding": [0.01] * 1024}],
            "model": "bge-M3",
            "usage": {"prompt_tokens": 1, "total_tokens": 1},
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def run(binary):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Endpoint)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    experiment = Experiment(binary, "hybrid_nonempty", prototype=6)
    try:
        experiment.start()
        experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
        experiment.sql("CREATE DATABASE hybrid_nonempty")
        experiment.sql(
            "CALL DBMS_AI_SERVICE.CREATE_AI_MODEL('ob_embed', "
            "'{\"type\":\"dense_embedding\",\"model_name\":\"bge-M3\"}')")
        config = json.dumps({
            "ai_model_name": "ob_embed", "scope": "all",
            "url": "http://127.0.0.1:%d/" % server.server_port,
            "access_key": "test", "request_model_name": "bge-M3",
            "provider": "openai",
        })
        experiment.sql("CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT('local_embed', %s)" %
                       ("'" + config + "'"))
        experiment.sql("FORK NAMESPACE hybrid_nonempty_child FROM ns1")
        with connect(experiment, "root@hybrid_nonempty_child") as child:
            experiment.sql(
                "CREATE TABLE hybrid_nonempty.t(id INT PRIMARY KEY, txt VARCHAR(100), "
                "VECTOR INDEX idx_txt(txt) WITH (distance=l2,type=hnsw,"
                "model=ob_embed,dim=1024,sync_mode=immediate))", child)
            experiment.sql("SET ob_query_timeout=60000000", child)
            experiment.sql("INSERT INTO hybrid_nonempty.t VALUES(1,'hello')", child)
            experiment.record("embedding_requests", count=Endpoint.requests)
            experiment.sql(
                "CALL dbms_vector.rebuild_index('hybrid_nonempty.idx_txt',"
                "'hybrid_nonempty.t','txt',0)", child)
            experiment.record("manual_tasks", active=experiment.sql(
                "SELECT COUNT(*) FROM oceanbase.__all_vector_index_task"),
                history=experiment.sql(
                "SELECT COUNT(*) FROM oceanbase.__all_vector_index_task_history"))
        experiment.record("PASS", case="hybrid_nonempty", embedding_requests=Endpoint.requests)
    finally:
        experiment.close()
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
