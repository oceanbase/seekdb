# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Check legacy short-path standby mTLS from a different process launch cwd."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import time
import uuid

from native_cli_preflight import JobAccounting, ProcessInfo, StartupInfo, k as process_api
from native_cli_sql import Pipe, k, read_discovery, verify_product_modules, wide
from native_sql_tls import make_certificates


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class Product:
    """Own only this test's process tree, including the default daemon child."""
    def __init__(self, exe, arguments, cwd, root):
        self.job = process_api.CreateJobObjectW(None, None)
        self.process = ProcessInfo()
        if not self.job:
            raise C.WinError(C.get_last_error())
        try:
            with (root / 'stdout.log').open('wb') as out, (root / 'stderr.log').open('wb') as err:
                info = StartupInfo()
                info.cb, info.flags = C.sizeof(info), 0x100
                info.stdout, info.stderr = (msvcrt.get_osfhandle(s.fileno()) for s in (out, err))
                for handle in (info.stdout, info.stderr):
                    os.set_handle_inheritable(handle, True)
                command = C.create_unicode_buffer(subprocess.list2cmdline([str(exe)] + arguments))
                if not process_api.CreateProcessW(str(exe), command, None, None, True,
                                                  4, None, str(cwd), C.byref(info), C.byref(self.process)):
                    raise C.WinError(C.get_last_error())
                if not process_api.AssignProcessToJobObject(self.job, self.process.process):
                    error = C.get_last_error()
                    process_api.TerminateProcess(self.process.process, 1)
                    raise C.WinError(error)
                if process_api.ResumeThread(self.process.thread) == 0xffffffff:
                    raise C.WinError(C.get_last_error())
        except BaseException:
            self.close()
            raise

    def parent_exit(self):
        code = W.DWORD()
        if not process_api.GetExitCodeProcess(self.process.process, C.byref(code)):
            raise C.WinError(C.get_last_error())
        return code.value

    def close(self):
        # This non-embedded TLS test does not claim graceful shutdown or crash
        # recovery. Termination is confined to the new, job-owned test instance.
        try:
            if not process_api.TerminateJobObject(self.job, 0):
                raise C.WinError(C.get_last_error())
            deadline = time.monotonic() + 30
            while True:
                counts = JobAccounting()
                if not process_api.QueryInformationJobObject(self.job, 1, C.byref(counts), C.sizeof(counts), None):
                    raise C.WinError(C.get_last_error())
                if counts.active == 0:
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError('test process tree did not stop')
                time.sleep(0.1)
            if self.process.process and process_api.WaitForSingleObject(self.process.process, 30000) != 0:
                raise TimeoutError('test parent process did not finish termination')
        finally:
            for handle in (self.process.thread, self.process.process, self.job):
                if handle:
                    process_api.CloseHandle(handle)


def tls_context(certs, client='trusted'):
    context = ssl.create_default_context(cafile=str(certs / 'ca.pem'))
    context.set_alpn_protocols(['h2'])
    if client != 'anonymous':
        name = 'server' if client == 'trusted' else 'untrusted'
        context.load_cert_chain(str(certs / (name + '.pem')), str(certs / (name + '.key')))
    return context


def handshake(port, context):
    with socket.create_connection(('127.0.0.1', port), timeout=5) as tcp:
        with context.wrap_socket(tcp, server_hostname='localhost') as connection:
            if connection.selected_alpn_protocol() != 'h2':
                raise AssertionError('gRPC did not negotiate HTTP/2')
            peer = hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest()
            # TLS 1.3 may return from the client handshake before the server
            # rejects its identity. Require authenticated application traffic.
            connection.sendall(b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n' + b'\0\0\0\x04\0\0\0\0\0')
            header = b''
            while len(header) < 9:
                chunk = connection.recv(9 - len(header))
                if not chunk:
                    raise EOFError('gRPC closed before HTTP/2 SETTINGS')
                header += chunk
            if header[3] != 4 or header[5:] != b'\0\0\0\0':
                raise AssertionError('missing gRPC HTTP/2 SETTINGS')
            return dict(peer_sha256=peer, version=connection.version(), alpn='h2', settings=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--daemon', action='store_true')
    parser.add_argument('--skip-rotation', action='store_true', help='Startup smoke only; not rotation evidence')
    args = parser.parse_args()
    source, exe = args.source_root.resolve(), args.exe.resolve(strict=True)
    root = source / 'build_phase0' / ('standby-tls-' + uuid.uuid4().hex)
    if not str(root).isascii() or len(str(root)) > 170:
        raise ValueError('this compatibility check requires a short ASCII test root')
    if shutil.disk_usage(source).free < 3 * 1024**3:
        raise RuntimeError('standby TLS test requires 3 GiB free')
    root.mkdir()
    base, cwd, certs, decoy = [root / name for name in ('instance', 'cwd', 'certs', 'decoy-certs')]
    for directory in (base, cwd, certs, decoy):
        directory.mkdir()
    openssl = source / 'deps/3rd/openssl/bin/openssl.exe'
    make_certificates(openssl, certs)
    make_certificates(openssl, decoy)
    for directory, fixture in ((base, certs), (cwd, decoy)):
        wallet = directory / 'wallet'
        wallet.mkdir()
        for original, target in (('ca.pem', 'ca.pem'), ('server.pem', 'cert.pem'), ('server.key', 'key.pem')):
            shutil.copyfile(fixture / original, wallet / target)
    foreign = {str(p.relative_to(cwd)): sha256(p) for p in cwd.rglob('*') if p.is_file()}
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    parameters = ['enable_rpc_service=true', 'enable_rpc_tls=true', f'rpc_port={port}',
                  'mysql_port_mode=disabled', 'log_disk_size=2G', 'datafile_size=32M']
    arguments = ['--base-dir', str(base), '--log-level', 'WARN'] + ([] if args.daemon else ['--nodaemon'])
    arguments += [part for value in parameters for part in ('--parameter', value)]
    evidence = dict(exe=str(exe), exe_sha256=sha256(exe), sqlite_sha256=sha256(exe.parent / 'sqlite3.dll'),
                    base=str(base), cwd=str(cwd), daemon=args.daemon, arguments=arguments,
                    rotation_requested=not args.skip_rotation, refresh_interval_seconds=3600,
                    rotation_passed=False, passed=False, stages=[])
    def save(stage, **details):
        evidence['stages'].append(dict(stage=stage, **details))
        (root / 'result.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        print('STANDBY_TLS_STAGE=' + stage, flush=True)
    print(f'STANDBY_TLS_ROOT={root}', flush=True)
    product, pipe = None, None
    try:
        product = Product(exe, arguments, cwd, root)
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            code = product.parent_exit()
            if code != 259 and (not args.daemon or code != 0):
                raise RuntimeError(f'product exited before SQL ready: {code}')
            try:
                name = read_discovery(wide(str(base / 'run/sql.pipe')))
            except OSError as error:
                if error.winerror not in (2, 3):
                    raise
            else:
                pipe = Pipe(name)
                pid = W.DWORD()
                if not k.GetNamedPipeServerProcessId(pipe.h, C.byref(pid)):
                    raise C.WinError(C.get_last_error())
                if (pid.value != product.process.pid) != args.daemon:
                    raise AssertionError('wrong SQL server process')
                pipe.login()
                if pipe.query('SELECT 533') != [['533']]:
                    raise AssertionError('SQL readiness failed')
                verify_product_modules(pid.value, exe)
                break
            time.sleep(0.05)
        if pipe is None:
            raise TimeoutError('SQL readiness deadline')
        if args.daemon and product.parent_exit() != 0:
            raise AssertionError('daemon parent did not exit successfully')
        save('sql-ready', pid=pid.value, parent_pid=product.process.pid)
        expected = hashlib.sha256(ssl.PEM_cert_to_DER_cert((certs / 'server.pem').read_text())).hexdigest()
        try:
            trusted = handshake(port, tls_context(certs))
        except ssl.SSLCertVerificationError:
            # Diagnose the old cwd regression without accepting it as a pass.
            wrong = handshake(port, tls_context(decoy))
            decoy_identity = hashlib.sha256(ssl.PEM_cert_to_DER_cert((decoy / 'server.pem').read_text())).hexdigest()
            save('wrong-cwd-wallet', matches_decoy=wrong['peer_sha256'] == decoy_identity, **wrong)
            raise
        if trusted['peer_sha256'] != expected:
            raise AssertionError('standby loaded a wallet outside base-dir')
        save('instance-wallet', **trusted)
        for client in ('anonymous', 'untrusted'):
            try:
                handshake(port, tls_context(certs, client))
            except (ssl.SSLError, ConnectionResetError, ConnectionAbortedError, EOFError) as error:
                save('client-rejected', client=client, error=type(error).__name__)
            else:
                raise AssertionError(f'standby accepted {client} mTLS client')
        if handshake(port, tls_context(certs))['peer_sha256'] != expected:
            raise AssertionError('rejected peers damaged trusted TLS')
        if not args.skip_rotation:
            # Renew using the same key/CA to avoid a deliberately mismatched
            # key-pair window. Keep the unmodified product's 3600-second timer.
            with (certs / 'rotation.log').open('wb') as log:
                subprocess.run([str(openssl), 'x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem',
                                '-CAkey', 'ca.key', '-set_serial', '534', '-days', '2',
                                '-out', 'renewed.pem', '-extfile', 'leaf.cnf'],
                               cwd=certs, stdout=log, stderr=log, check=True, timeout=30)
            renewed = hashlib.sha256(ssl.PEM_cert_to_DER_cert((certs / 'renewed.pem').read_text())).hexdigest()
            shutil.copyfile(certs / 'renewed.pem', base / 'wallet/cert.next')
            os.replace(base / 'wallet/cert.next', base / 'wallet/cert.pem')
            started = time.monotonic()
            save('rotation-written', previous=expected, expected=renewed)
            while True:
                current = handshake(port, tls_context(certs))
                if current['peer_sha256'] == renewed:
                    break
                if current['peer_sha256'] != expected:
                    raise AssertionError('watcher loaded an unrelated certificate')
                if time.monotonic() - started > 3720:
                    raise TimeoutError('certificate watcher did not refresh within 3600 + 120 seconds')
                time.sleep(10)
            evidence['rotation_passed'] = True
            save('rotation-observed', elapsed_seconds=time.monotonic() - started, **current)
        if pipe.query('SELECT 534') != [['534']]:
            raise AssertionError('SQL session did not survive TLS checks')
        actual = {str(p.relative_to(cwd)): sha256(p) for p in cwd.rglob('*') if p.is_file()}
        if actual != foreign:
            raise AssertionError('launch cwd was modified')
        save('checks-complete', foreign_cwd_unchanged=True)
    except BaseException as error:
        save('failed', error=type(error).__name__, detail=str(error))
        raise
    finally:
        if pipe is not None:
            pipe.close()
        if product is not None:
            product.close()
            save('stopped', method='TerminateJobObject on disposable non-embedded instance')
    # After confirming process termination, allow bounded time for transient
    # sharing/lock violations in this test-owned store's cleanup. Other errors
    # and expiration of the deadline still fail the test.
    cleanup_deadline = time.monotonic() + 10
    cleanup_retries = 0
    while True:
        try:
            shutil.rmtree(base / 'store')
            break
        except OSError as error:
            if error.winerror not in (32, 33) or time.monotonic() >= cleanup_deadline:
                save('cleanup-failed', error=type(error).__name__, detail=str(error))
                raise
            cleanup_retries += 1
            time.sleep(0.1)
    evidence['passed'] = True
    save('passed', cleanup_retries=cleanup_retries)
    print('NATIVE_STANDBY_TLS_PASS ROTATION=' + str(evidence['rotation_passed']), flush=True)


if __name__ == '__main__':
    main()
