# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Exercise the product's configured wallet and SQL TLS over its named pipe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import ssl
import struct
import subprocess
import uuid
import winreg

from native_cli_sql import Pipe, run, wide


class TlsPipe(Pipe):
    def __init__(self, name, context, trace):
        self.tls = None
        super().__init__(name)
        try:
            if self.packet()[0] != 10:
                raise AssertionError('missing product MySQL greeting')
            self.send(struct.pack('<IIB23x', 0x8a01, 1024*1024, 45), 1)
            self.incoming, self.outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
            self.tls = context.wrap_bio(self.incoming, self.outgoing,
                                        server_side=False, server_hostname='localhost')
            while True:
                try:
                    self.tls.do_handshake()
                    self.flush()
                    trace['version'] = self.tls.version()
                    trace['server_verified'] = bool(self.tls.getpeercert())
                    break
                except ssl.SSLWantReadError:
                    self.receive()
            self.send(struct.pack('<IIB23x', 0x8a01, 1024*1024, 45) + b'root\0\0', 2)
            if self.packet()[0] != 0:
                raise AssertionError('encrypted MySQL login rejected')
        except BaseException:
            self.close()
            raise

    def flush(self):
        while self.outgoing.pending:
            data = self.outgoing.read()
            while data:
                data = data[Pipe.io(self, data, len(data), True):]

    def receive(self):
        self.flush()
        self.incoming.write(Pipe.io(self, None, 16384, False))

    def read(self, size):
        if self.tls is None:
            return super().read(size)
        data = b''
        while len(data) < size:
            try:
                part = self.tls.read(size - len(data))
                if not part:
                    raise EOFError('TLS connection closed')
                data += part
            except ssl.SSLWantReadError:
                self.receive()
        return data

    def send(self, data, sequence=0):
        if self.tls is None:
            return super().send(data, sequence)
        data = len(data).to_bytes(3, 'little') + bytes([sequence]) + data
        while data:
            try:
                data = data[self.tls.write(data):]
            except ssl.SSLWantReadError:
                self.receive()
        self.flush()


def make_certificates(openssl, root):
    # Generate new disposable test keys inside this VM run. No existing host
    # credentials or private keys are read, and key contents are never logged.
    commands = [
        ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
         '-keyout', 'ca.key', '-out', 'ca.pem', '-subj', '/CN=seek533-test-ca',
         '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign'],
        ['req', '-new', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'server.key',
         '-out', 'server.csr', '-subj', '/CN=localhost'],
        ['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key',
         '-set_serial', '533', '-days', '2', '-out', 'server.pem', '-extfile', 'leaf.cnf'],
        ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
         '-keyout', 'untrusted.key', '-out', 'untrusted.pem', '-subj', '/CN=untrusted-client',
         '-addext', 'basicConstraints=critical,CA:FALSE', '-addext', 'extendedKeyUsage=clientAuth'],
    ]
    (root / 'leaf.cnf').write_text('basicConstraints=critical,CA:FALSE\n'
                                 'keyUsage=critical,digitalSignature,keyEncipherment\n'
                                 'extendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:localhost\n')
    (root / 'request.cnf').write_text('[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=localhost\n')
    with (root / 'certificate-generation.log').open('wb') as log:
        for command in commands:
            subprocess.run([str(openssl)] + command, cwd=root, stdout=log, stderr=log,
                           timeout=30, check=True,
                           env=dict(os.environ, OPENSSL_CONF=str(root / 'request.cnf')))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True)
    args = parser.parse_args()
    source = Path(args.source_root).resolve()
    exe = source / 'build_phase0_nio/src/observer/seekdb.exe'
    openssl = source / 'deps/3rd/openssl/bin/openssl.exe'
    if shutil.disk_usage(source).free < 3 * 1024**3:
        raise RuntimeError('TLS product check requires 3 GiB free')
    root = source / 'build_phase0' / ('native-tls-' + uuid.uuid4().hex)
    root.mkdir()
    print('TLS_LOG_ROOT=' + str(root), flush=True)
    cwd = root / 'cwd'
    cwd.mkdir()
    # Wrong-root wallet must not be consulted by the product.
    (cwd / 'wallet').mkdir()
    for name in ('ca.pem', 'server-cert.pem', 'server-key.pem'):
        (cwd / 'wallet' / name).write_text('invalid test wallet\n')
    certs = root / 'certificates'
    certs.mkdir()
    make_certificates(openssl, certs)
    base = str(root / 'instance')
    units = lambda value: len(value.encode('utf-16-le')) // 2
    while units(base) < 2048:
        left = 2048 - units(base)
        if left == 1:
            base += 'a'
        else:
            budget = min(left - 1, 80)
            pattern = '目录😀 a%'
            component = pattern * (budget // units(pattern))
            base += '\\' + component + 'a' * (budget - units(component))
    wallet = Path(wide(base + '\\wallet'))
    wallet.mkdir(parents=True)
    for src, dst in [('ca.pem', 'ca.pem'), ('server.pem', 'server-cert.pem'), ('server.key', 'server-key.pem')]:
        shutil.copyfile(certs / src, wallet / dst)
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\FileSystem') as key:
        policy, _ = winreg.QueryValueEx(key, 'LongPathsEnabled')
    with exe.open('rb') as binary:
        identity = hashlib.file_digest(binary, 'sha256').hexdigest()
    evidence = dict(base=base, base_units=units(base), policy=policy, exe_sha256=identity,
                    openssl_client=ssl.OPENSSL_VERSION, tls=[], passed=False)
    result = root / 'result.json'
    result.write_text(json.dumps(evidence, indent=2), encoding='utf-8')

    def tls_check(plain, name):
        for client in ('anonymous', 'trusted', 'untrusted'):
            trace = dict(client_certificate_messages=0)
            def message_callback(conn, direction, version, content_type, message_type, data):
                # Count an actual outgoing, nonempty Certificate message. Do
                # not retain its bytes or count merely configuring a key.
                if direction == 'write' and int(content_type) == 22 and int(message_type) == 11 and len(data) > 8:
                    trace['client_certificate_messages'] += 1
            ctx = ssl.create_default_context(cafile=str(certs / 'ca.pem'))
            ctx._msg_callback = message_callback  # CPython test client, recorded in the environment matrix.
            ctx.minimum_version = ssl.TLSVersion.TLSv1_2
            if client != 'anonymous':
                prefix = 'server' if client == 'trusted' else 'untrusted'
                ctx.load_cert_chain(str(certs / (prefix + '.pem')), str(certs / (prefix + '.key')))
            try:
                connection = TlsPipe(name, ctx, trace)
            except ssl.SSLError as error:
                if client != 'untrusted' or 'UNKNOWN_CA' not in str(error) or not trace['client_certificate_messages']:
                    raise
                evidence['tls'].append(dict(client=client, rejected=True, ssl_reason=error.reason, trace=trace))
            except OSError as error:
                # NIO propagates process_new_packets verification errors as
                # InvalidData and closes the pipe, which may precede an alert.
                # Require the certificate flight and verified server handshake,
                # the precise peer-close result, and live SQL below. Other I/O
                # errors still fail the test.
                if (client != 'untrusted' or error.winerror not in (109, 233)
                        or not trace['client_certificate_messages'] or not trace.get('server_verified')):
                    raise
                evidence['tls'].append(dict(client=client, rejected=True, win32=error.winerror, trace=trace))
            else:
                try:
                    if client == 'untrusted':
                        raise AssertionError('untrusted client certificate accepted')
                    if connection.query('SELECT id FROM seek533.persistence') != [['533']]:
                        raise AssertionError('TLS SQL persistent data mismatch')
                    cipher = connection.query("SHOW SESSION STATUS LIKE 'Ssl_cipher'")
                    # The baseline SQL session initializes ssl_cipher to empty
                    # and does not populate it from NIO. Prove TLS through the
                    # authenticated SSL transport and encrypted SQL exchange;
                    # retain the SQL status result without inventing support.
                    if connection.tls.version() not in ('TLSv1.2', 'TLSv1.3') or not connection.tls.cipher():
                        raise AssertionError('TLS transport has no negotiated cipher')
                    evidence['tls'].append(dict(client=client, version=connection.tls.version(),
                                                cipher=connection.tls.cipher()[0], sql_cipher=cipher, trace=trace))
                finally:
                    connection.close()
        # A rejected TLS peer must not damage the existing SQL session.
        if plain.query('SELECT id FROM seek533.persistence') != [['533']]:
            raise AssertionError('TLS rejection damaged the active instance')
        print('PRODUCT_TLS_CONNECTIONS_PASS', flush=True)

    try:
        for restart in (False, True):
            run(exe, base, cwd, root, restart, hold=tls_check,
                extra_parameters=('ssl_client_authentication=true', 'ob_ssl_invited_common_names=localhost'))
        # Corrupt only the owned test wallet. Startup must fail, with no pipe
        # publication or cleartext fallback, even though the database exists.
        (wallet / 'server-cert.pem').write_text('invalid PEM\n')
        with (root / 'invalid-wallet.stdout').open('wb') as out, (root / 'invalid-wallet.stderr').open('wb') as err:
            code = subprocess.run([str(exe), '--base-dir', base, '--embedded', '--nodaemon',
                                   '--parameter', 'ssl_client_authentication=true',
                                   '--parameter', 'ob_ssl_invited_common_names=localhost',
                                   '--parameter', 'mysql_port_mode=disabled',
                                   '--parameter', 'log_disk_size=2G', '--parameter', 'datafile_size=32M'],
                                  cwd=cwd, stdout=out, stderr=err, timeout=180,
                                  env=dict(os.environ, TELEMETRY_ENABLED='false')).returncode
        if code == 0 or Path(wide(base + '\\run\\sql.pipe')).exists():
            raise AssertionError('invalid TLS wallet did not fail closed')
        logs = Path(wide(base + '\\log\\seekdb.log')).read_text(encoding='utf-8', errors='replace')
        if 'nio_start failed' not in logs or 'start_err=6' not in logs:
            raise AssertionError('missing product ETLS diagnostic')
        evidence.update(invalid_wallet_exit=code, invalid_wallet_etls=True, passed=True)
    finally:
        # Restore the generated test certificate for diagnostic replay.
        shutil.copyfile(certs / 'server.pem', wallet / 'server-cert.pem')
        result.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    if evidence['passed']:
        shutil.rmtree(wide(base + '\\store'))
        print('NATIVE_PRODUCT_TLS_PASS', flush=True)


if __name__ == '__main__':
    main()
