#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.
"""Deterministic fault-injection mock for the AI_EMBED regression cases.

This supplements, and does not replace, the competition's official Mock service.
"""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 18082
STATE = Path('/tmp/seekdb_ai_embed_mock_18082')
EVENTS = STATE / 'events.jsonl'


def events():
    return [json.loads(line) for line in EVENTS.read_text().splitlines() if line]


def serve():
    lock = threading.Lock()
    attempts = {}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            dashscope = self.path.startswith('/dashscope/')
            texts = body['input']['texts'] if dashscope else body['input']
            dimension = (body.get('parameters', {}).get('dimension', 3) if dashscope
                         else body.get('dimensions', 3))
            mode = self.path.rsplit('/', 1)[-1]
            with lock:
                attempts[self.path] = attempts.get(self.path, 0) + 1
                attempt = attempts[self.path]
                with EVENTS.open('a') as stream:
                    stream.write(json.dumps({'path': self.path, 'texts': texts,
                                             'dimension': dimension}) + '\n')
            data = [{'text_index' if dashscope else 'index': i,
                     'embedding': [float(len(text.encode('utf-8')))]
                                  + [j + 0.25 for j in range(1, dimension)]}
                    for i, text in enumerate(texts)]
            if mode == 'shuffle':
                data.reverse()
            elif mode == 'duplicate':
                data.append(data[0])
            elif mode == 'missing':
                data.pop()
            elif mode == 'out-of-range':
                data[0]['text_index' if dashscope else 'index'] = len(texts)
            elif mode == 'no-index':
                data[0].pop('text_index' if dashscope else 'index')
            elif mode == 'string-index':
                data[0]['text_index' if dashscope else 'index'] = '0'
            elif mode == 'bad-element':
                data[0]['embedding'][0] = 'invalid'
            elif mode == 'null-element':
                data[0]['embedding'][0] = None
            elif mode == 'overflow-element':
                data[0]['embedding'][0] = 1e100
            elif mode == 'wrong-dimension':
                data[0]['embedding'].append(0.0)
            elif mode == 'object-vector':
                data[0]['embedding'] = {}
            response = {'output': {'embeddings': data}} if dashscope else {'data': data}
            if mode == 'bad-root':
                response = []
            elif mode == 'bad-data':
                response = {'data': {}}
            status = 200
            if mode == 'http400':
                status, response = 400, {'error': 'injected failure'}
            elif mode == 'retry' and attempt == 1:
                status, response = 503, {'error': 'retry once'}
            elif mode == 'fail-late' and any(text == 'row-40' for text in texts):
                response = {'data': []}
            raw = b'{"data": [' if mode == 'malformed' else json.dumps(response).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

    ThreadingHTTPServer(('127.0.0.1', PORT), Handler).serve_forever()


def main():
    command = sys.argv[1]
    STATE.mkdir(exist_ok=True)
    pidfile = STATE / 'server.pid'
    if command == 'serve':
        serve()
    elif command == 'start':
        if pidfile.exists():
            raise RuntimeError('mock already started; stop it before starting a new run')
        EVENTS.write_text('')
        with (STATE / 'server.log').open('w') as output:
            child = subprocess.Popen([sys.executable, __file__, 'serve'],
                                     stdin=subprocess.DEVNULL, stdout=output,
                                     stderr=output, start_new_session=True)
        pidfile.write_text(str(child.pid))
        for _ in range(100):
            if child.poll() is not None:
                raise RuntimeError('mock startup failed; inspect ' + str(STATE / 'server.log'))
            try:
                with socket.create_connection(('127.0.0.1', PORT), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.02)
        raise RuntimeError('mock startup timed out')
    elif command == 'stop':
        if pidfile.exists():
            try:
                os.kill(int(pidfile.read_text()), signal.SIGTERM)
            except ProcessLookupError:
                pass
            pidfile.unlink()
    elif command == 'clear':
        EVENTS.write_text('')
    elif command == 'assert-empty':
        assert not events(), events()
    elif command == 'assert-batch':
        # Check work at the HTTP boundary, independently of SQL output checks.
        rows = events()
        expected = int(sys.argv[2])
        texts = [text for row in rows for text in row['texts']]
        assert len(texts) == expected, (len(texts), expected)
        assert len(set(texts)) == expected, texts
        assert all(0 < len(row['texts']) <= 20 for row in rows), rows
        if expected > 1:
            assert any(len(row['texts']) > 1 for row in rows), rows
        if len(sys.argv) > 3:
            assert sorted(texts) == sorted('row-' + str(i) for i in
                                           map(int, sys.argv[3].split(','))), texts


if __name__ == '__main__':
    main()
