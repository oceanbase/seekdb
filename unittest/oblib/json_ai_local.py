#!/usr/bin/env python3
"""Exercise SQL AI JSON integration using an HTTP fixture bound only to loopback."""

import argparse
import http.server
import json
import threading
import uuid

import pymysql


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        provider, kind = self.path.strip('/').split('/')
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        self.server.requests.append((provider, kind, body))
        if kind == 'completion':
            response = {'choices': [{'message': {'content': 'fixture 雪 response'}}]}
            if provider == 'dashscope':
                response = {'output': response}
            elif provider == 'ollama':
                response = {'response': 'fixture 雪 response'}
        elif kind == 'dense_embedding':
            contents = body['input']['texts'] if provider == 'dashscope' else body['input']
            embeddings = [{'embedding': [1.0, 2.0, 3.0]} for _ in contents]
            response = {'output': {'embeddings': embeddings}} if provider == 'dashscope' else {'data': embeddings}
        else:
            contents = body['input']['documents'] if provider == 'dashscope' else body['documents']
            results = [{'index': index, 'relevance_score': (int(value.split('_')[-1]) + 1) / 1000}
                       for index, value in enumerate(contents)]
            results.sort(key=lambda value: value['relevance_score'], reverse=True)
            response = {'output': {'results': results}} if provider == 'dashscope' else {'results': results}
        encoded = json.dumps(response, ensure_ascii=False).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def run(port):
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.requests = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    connection = pymysql.connect(host='127.0.0.1', port=port, user='root', database='test',
                                 autocommit=True, read_timeout=60)
    created_models = []
    created_endpoints = []
    checks = 0
    try:
        with connection.cursor() as cursor:
            for provider, kind in [('openai', 'completion'), ('dashscope', 'completion'),
                                   ('openai', 'dense_embedding'), ('dashscope', 'dense_embedding'),
                                   ('siliconflow', 'rerank'), ('dashscope', 'rerank')]:
                print(json.dumps({'case': provider + '.' + kind}), flush=True)
                name = 'json_fixture_' + uuid.uuid4().hex
                endpoint = name + '_endpoint'
                cursor.execute('call DBMS_AI_SERVICE.CREATE_AI_MODEL(%s,%s)',
                               (name, json.dumps({'type': kind, 'model_name': 'fixture-model'})))
                created_models.append(name)
                cursor.execute('call DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT(%s,%s)',
                               (endpoint, json.dumps({'ai_model_name': name, 'request_model_name': 'fixture-model',
                                'url': 'http://127.0.0.1:' + str(server.server_port) + '/' + provider + '/' + kind,
                                'access_key': 'fixture-only-key', 'provider': 'aliyun-dashscope' if provider == 'dashscope' else provider})))
                created_endpoints.append(endpoint)
                request_start = len(server.requests)
                if kind == 'completion':
                    config = {'temperature': 0.25, 'nested': {'quoted': '雪"\\'}, 'result_format': 'text'}
                    cursor.execute('select ai_complete(%s, ai_prompt(%s,%s), %s)',
                                   (name, 'Hello {0}', '雪', json.dumps(config)))
                    assert cursor.fetchone()[0] == 'fixture 雪 response'
                    body = server.requests[-1][2]
                    assert body['model'] == 'fixture-model'
                    if provider == 'ollama':
                        assert body['prompt'] == 'Hello 雪'
                    else:
                        messages = body['input']['messages'] if provider == 'dashscope' else body['messages']
                        assert messages == [{'role': 'user', 'content': 'Hello 雪'}]
                    parameters = body['parameters'] if provider == 'dashscope' else body
                    assert parameters['temperature'] == 0.25 and parameters['nested'] == config['nested']
                    if provider == 'dashscope':
                        assert parameters['result_format'] == 'message'
                    checks += 1
                elif kind == 'dense_embedding':
                    cursor.execute('select ai_embed(%s,%s,3)', (name, 'hello 雪'))
                    assert json.loads(cursor.fetchone()[0]) == [1.0, 2.0, 3.0]
                    body = server.requests[-1][2]
                    assert body['model'] == 'fixture-model'
                    if provider == 'dashscope':
                        assert body['input']['texts'] == ['hello 雪']
                        assert body['parameters']['dimension'] == 3 and 'dimensions' not in body['parameters']
                    else:
                        assert body['input'] == ['hello 雪'] and body['dimensions'] == 3
                    checks += 1
                else:
                    documents = ['doc_' + str(index) for index in range(25)]
                    cursor.execute('select ai_rerank(%s,%s,cast(%s as json))', (name, 'fixture query', json.dumps(documents)))
                    result = json.loads(cursor.fetchone()[0])
                    assert [item['index'] for item in result] == list(reversed(range(25)))
                    assert [item['relevance_score'] for item in result] == [(index + 1) / 1000 for index in reversed(range(25))]
                    assert len(server.requests) - request_start == 2
                    objects = [{'text': value, 'id': index} for index, value in enumerate(documents)]
                    cursor.execute('select ai_rerank(%s,%s,cast(%s as json),%s)',
                                   (name, 'fixture query', json.dumps(objects), 'text'))
                    result = json.loads(cursor.fetchone()[0])
                    assert [item['id'] for item in result] == list(reversed(range(25)))
                    assert [item['_model_score'] for item in result] == [(index + 1) / 1000 for index in reversed(range(25))]
                    assert [item['text'] for item in result] == list(reversed(documents))
                    checks += 2
    finally:
        try:
            with connection.cursor() as cursor:
                for name in reversed(created_endpoints):
                    cursor.execute('call DBMS_AI_SERVICE.DROP_AI_MODEL_ENDPOINT(%s)', (name,))
                for name in reversed(created_models):
                    cursor.execute('call DBMS_AI_SERVICE.DROP_AI_MODEL(%s)', (name,))
        finally:
            connection.close()
            server.shutdown()
            server.server_close()
            thread.join()
    print(json.dumps({'checks': checks, 'loopback_requests': len(server.requests), 'models_cleaned': len(created_models)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    run(parser.parse_args().port)
