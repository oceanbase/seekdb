"""Verify the prompt body emitted by AI_COMPLETE against a local HTTP endpoint."""

from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from queue import Queue
import subprocess
from threading import Thread


MODEL = "ai_prompt_render_model"
ENDPOINT = "ai_prompt_render_endpoint"


class CompletionHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.server.prompts.put(request["messages"][-1]["content"])
        body = b'{"choices":[{"message":{"content":"ok"}}]}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def run_sql(sql):
    subprocess.run(
        [
            "obclient", "--protocol=tcp", "--host=" + os.environ["OBMYSQL_MS0"],
            "--port=" + os.environ["OBMYSQL_PORT"], "--user=" + os.environ["OBMYSQL_USR"],
            "--database=test", "--default-character-set=utf8mb4",
            "--batch", "--raw", "--skip-column-names",
        ],
        input=sql, encoding="utf-8", stdout=subprocess.PIPE, check=True, timeout=45,
        env={**os.environ, "MYSQL_PWD": os.environ["OBMYSQL_PWD"]},
    )


def check_prompts(server):
    cases = [
        ("ai_prompt('{0}/{0}/{0}', 'repeated value')", "repeated value/repeated value/repeated value"),
        ("ai_prompt('{1} {0} {1}', json_object('a', '汉😀'), json_array(1, null))",
         '[1, null] {"a": "汉😀"} [1, null]'),
        ("ai_prompt('{0}', json_object('template', 'ordinary data', 'b', 2))",
         '{"b": 2, "template": "ordinary data"}'),
        ("ai_prompt('外{0}|{0}', ai_prompt('内{0}', ai_prompt('层{0}', '😀')))", "外内层😀|内层😀"),
        ("ai_prompt('{{0}}:{0}:{name}:{{{1}}}', '{1}', 'x')", "{0}:{1}:{name}:{x}"),
        ("ai_prompt('{01}/{0}', '', 'x')", "x/"),
        ("ai_prompt('{9}{10}{9}', '', '', '', '', '', '', '', '', '', 'a', 'b')", "aba"),
        ("""ai_prompt(CONCAT('字{0}', CHAR(10)), CONCAT('😀"', CHAR(92, 0), '尾'))""",
         '字😀"\\\0尾\n'),
    ]
    for expression, expected in cases:
        run_sql(f"SELECT AI_COMPLETE('{MODEL}', {expression});")
        actual = server.prompts.get(timeout=2)
        if actual != expected:
            raise AssertionError(f"{expression}: expected {expected!r}, got {actual!r}")

    run_sql("""
        CREATE TABLE ai_prompt_render_text (
            t TEXT CHARACTER SET latin1, l LONGTEXT CHARACTER SET utf8mb4, j JSON
        );
    """)
    try:
        run_sql(f"""
            INSERT INTO ai_prompt_render_text VALUES
                (CONCAT(CONVERT(X'E9' USING latin1), '{{0}}'), CONCAT(REPEAT('😀', 20000), '{{1}}|{{0}}'),
                 JSON_ARRAY(1, JSON_OBJECT('k', '值')));
            SELECT AI_COMPLETE('{MODEL}', AI_PROMPT(t, l)) FROM ai_prompt_render_text;
            SELECT AI_COMPLETE('{MODEL}', AI_PROMPT(l, t, j)) FROM ai_prompt_render_text;
        """)
    finally:
        run_sql("DROP TABLE ai_prompt_render_text;")
    long_text = "😀" * 20000
    for expected in ('é' + long_text + '{1}|{0}', long_text + '[1, {"k": "值"}]|é{0}'):
        actual = server.prompts.get(timeout=2)
        if actual != expected:
            raise AssertionError(f"TEXT/LONGTEXT rendering differs (received {len(actual)} characters)")


def main():
    with HTTPServer(("127.0.0.1", 0), CompletionHandler) as server:
        server.prompts = Queue()
        worker = Thread(target=server.serve_forever)
        worker.start()
        try:
            run_sql(f"""CALL DBMS_AI_SERVICE.CREATE_AI_MODEL(
                '{MODEL}', '{{"type":"completion","model_name":"test"}}');""")
            try:
                run_sql(f"""CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT(
                    '{ENDPOINT}', '{{"ai_model_name":"{MODEL}","provider":"openai",
                    "url":"http://127.0.0.1:{server.server_port}/","access_key":"test"}}');""")
                try:
                    check_prompts(server)
                finally:
                    run_sql(f"CALL DBMS_AI_SERVICE.DROP_AI_MODEL_ENDPOINT('{ENDPOINT}');")
            finally:
                run_sql(f"CALL DBMS_AI_SERVICE.DROP_AI_MODEL('{MODEL}');")
        finally:
            server.shutdown()
            worker.join()


if __name__ == "__main__":
    main()
