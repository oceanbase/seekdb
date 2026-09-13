# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Exercise production telemetry delivery against disposable loopback HTTPS peers."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import ssl
import subprocess
import threading
import uuid

from native_sql_tls import make_certificates


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        data = self.rfile.read(int(self.headers['Content-Length']))
        payload = json.loads(data)
        status = 503 if self.path == '/reject' else 200
        # Persist no host metadata or private keys in the test result.
        self.server.requests.append(dict(path=self.path, status=status,
                                         keys=sorted(payload),
                                         payload_sha256=hashlib.sha256(data).hexdigest()))
        self.send_response(status)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *_):
        pass


class CrlHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Serve only generated public CRLs, never a directory containing keys.
        data = self.server.crls.get(self.path)
        if data is None:
            self.send_error(404)
            return
        self.server.requests.append(self.path)
        self.send_response(200)
        self.send_header('Content-Type', 'application/pkix-crl')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


def add_crl(openssl, certificates, url):
    # Schannel checks revocation even with an explicit test CA. Supply a valid
    # empty CRL rather than weakening the production verification settings.
    with (certificates / 'leaf.cnf').open('a') as config:
        config.write('crlDistributionPoints=URI:' + url + '\n')
    (certificates / 'index.txt').write_text('')
    (certificates / 'crlnumber').write_text('01\n')
    (certificates / 'ca.cnf').write_text(
        '[ca]\ndefault_ca=local_ca\n[local_ca]\ndatabase=index.txt\n'
        'certificate=ca.pem\nprivate_key=ca.key\ndefault_md=sha256\n'
        'default_crl_days=1\ncrlnumber=crlnumber\n')
    commands = [
        ['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key',
         '-set_serial', '533', '-days', '2', '-out', 'server.pem', '-extfile', 'leaf.cnf'],
        ['ca', '-gencrl', '-batch', '-config', 'ca.cnf', '-out', 'ca.crl.pem'],
        ['crl', '-in', 'ca.crl.pem', '-outform', 'DER', '-out', 'ca.crl'],
    ]
    with (certificates / 'crl-generation.log').open('wb') as log:
        for command in commands:
            subprocess.run([str(openssl)] + command, cwd=certificates, stdout=log, stderr=log,
                           timeout=30, check=True,
                           env=dict(os.environ, OPENSSL_CONF=str(certificates / 'request.cnf')))


def start_server(certificates):
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    server.requests = []
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certificates / 'server.pem', certificates / 'server.key')
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--exe', required=True)
    args = parser.parse_args()
    source, exe = Path(args.source_root).resolve(), Path(args.exe).resolve()
    root = source / 'build_phase0' / ('telemetry-https-' + uuid.uuid4().hex)
    root.mkdir()
    print('TELEMETRY_HTTPS_ROOT=' + str(root), flush=True)
    evidence = dict(passed=False, exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                    python_tls=ssl.OPENSSL_VERSION, system_trust_store_modified=False,
                    peer_verification_overridden_by_test=False)
    servers = []
    try:
        crls = ThreadingHTTPServer(('127.0.0.1', 0), CrlHandler)
        crls.daemon_threads = True
        crls.crls, crls.requests = {}, []
        thread = threading.Thread(target=crls.serve_forever, daemon=True)
        thread.start()
        servers.append((crls, thread))
        openssl = source / 'deps/3rd/openssl/bin/openssl.exe'
        for name in ('trusted', 'untrusted'):
            certificates = root / name
            certificates.mkdir()
            make_certificates(openssl, certificates)
            route = '/' + name + '.crl'
            add_crl(openssl, certificates, f'http://127.0.0.1:{crls.server_port}{route}')
            crls.crls[route] = (certificates / 'ca.crl').read_bytes()
            servers.append(start_server(certificates))
        trusted, untrusted = (entry[0] for entry in servers[1:])
        trusted_url = f'https://localhost:{trusted.server_port}/'
        untrusted_url = f'https://localhost:{untrusted.server_port}/'
        mismatch_url = f'https://127.0.0.1:{trusted.server_port}/'
        command = [str(exe), '--https', str(root / 'instance'),
                   str(root / 'trusted/ca.pem'), trusted_url, untrusted_url, mismatch_url]
        env = os.environ.copy()
        env['TELEMETRY_ENABLED'] = 'false'
        with (root / 'client.log').open('wb') as log:
            result = subprocess.run(command, cwd=root, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=90)
        evidence.update(child_exit=result.returncode, trusted_requests=trusted.requests,
                        untrusted_requests=untrusted.requests, crl_requests=crls.requests,
                        revocation_checks_disabled=False)
        if result.returncode:
            raise RuntimeError('production telemetry test failed; see client.log')
        if untrusted.requests:
            raise AssertionError('untrusted endpoint received telemetry')
        if [(r['path'], r['status']) for r in trusted.requests] != [('/reject', 503), ('/', 200)]:
            raise AssertionError('wrong delivery count or hostname rejection failed')
        if any(r['keys'] != ['component', 'content'] for r in trusted.requests):
            raise AssertionError('delivery metadata leaked into endpoint payload')
        if trusted.requests[0]['payload_sha256'] != trusted.requests[1]['payload_sha256']:
            raise AssertionError('retry changed pending payload')
        state = json.loads((root / 'instance/run/telemetry.json').read_text(encoding='utf-8'))
        if state['sent'] is not True:
            raise AssertionError('trusted acknowledgement was not persisted')
        evidence['passed'] = True
        print('TELEMETRY_HTTPS_PASS trusted_2xx=1 trusted_503=1 untrusted_posts=0 wrong_host_posts=0')
    finally:
        for server, thread in servers:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
        (root / 'result.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
