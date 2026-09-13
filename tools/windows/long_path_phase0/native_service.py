# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Short ASCII service compatibility on a new disposable instance only."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid
import winreg

from native_cli_sql import (k, Overlapped, Pipe, open_handle, read_discovery,
                            verify_product_modules, wide)


class ServiceStatus(C.Structure):
    _fields_ = [(name, W.DWORD) for name in (
        'kind', 'state', 'controls', 'win32_exit', 'service_exit', 'checkpoint',
        'wait_hint', 'pid', 'flags')]


api = C.WinDLL('advapi32', use_last_error=True)
for name, args, result in [
    ('OpenSCManagerW', [W.LPCWSTR, W.LPCWSTR, W.DWORD], W.HANDLE),
    ('OpenServiceW', [W.HANDLE, W.LPCWSTR, W.DWORD], W.HANDLE),
    ('CloseServiceHandle', [W.HANDLE], W.BOOL),
    ('StartServiceW', [W.HANDLE, W.DWORD, C.c_void_p], W.BOOL),
    ('QueryServiceStatusEx', [W.HANDLE, C.c_int, C.c_void_p, W.DWORD,
                             C.POINTER(W.DWORD)], W.BOOL),
    ('ControlService', [W.HANDLE, W.DWORD, C.c_void_p], W.BOOL),
]:
    fn = getattr(api, name)
    fn.argtypes, fn.restype = args, result


def status(service):
    value, size = ServiceStatus(), W.DWORD()
    if not api.QueryServiceStatusEx(service, 0, C.byref(value), C.sizeof(value), C.byref(size)):
        raise C.WinError(C.get_last_error())
    return value


def await_stopped(service):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        value = status(service)
        if value.state == 1:
            return value
        time.sleep(0.1)
    raise TimeoutError('owned service did not stop')


def command(exe, args, root, label):
    with (root / (label + '.stdout')).open('wb') as out, (root / (label + '.stderr')).open('wb') as err:
        result = subprocess.run([str(exe)] + args, cwd=root, stdout=out, stderr=err, timeout=60)
    (root / (label + '.exit')).write_text(str(result.returncode))
    if result.returncode:
        raise RuntimeError(f'{label} failed: {result.returncode}')


def exercise(service, exe, base, root, restart):
    if not api.StartServiceW(service, 0, None):
        raise C.WinError(C.get_last_error())
    lock = pipe = process = None
    start = time.monotonic()
    try:
        while time.monotonic() - start < 180:
            value = status(service)
            if value.state == 1:
                raise RuntimeError(f'service stopped before SQL: {value.win32_exit}/{value.service_exit}')
            if lock is None:
                try:
                    lock = open_handle(wide(str(base / 'run/seekdb.clients')))
                except OSError as error:
                    if error.winerror not in (2, 3):
                        raise
                if lock is not None:
                    ov = Overlapped()
                    if not k.LockFileEx(lock, 1, 0, 1, 0, C.byref(ov)):
                        raise C.WinError(C.get_last_error())
            if lock is not None:
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
                    if pid.value != status(service).pid:
                        raise AssertionError('SCM and named pipe process identities differ')
                    process = k.OpenProcess(0x100000 | 0x1000, False, pid.value)
                    if not process:
                        raise C.WinError(C.get_last_error())
                    pipe.login()
                    verify_product_modules(pid.value, exe)
                    break
            time.sleep(0.02)
        if pipe is None:
            raise TimeoutError('service SQL readiness deadline')
        if not restart:
            pipe.query('CREATE DATABASE seek533_service')
            pipe.query('CREATE TABLE seek533_service.persistence (id INT PRIMARY KEY)')
            pipe.query('BEGIN')
            pipe.query('INSERT INTO seek533_service.persistence VALUES (533)')
            pipe.query('COMMIT')
        if pipe.query('SELECT id FROM seek533_service.persistence') != [['533']]:
            raise AssertionError('service persistent data mismatch')
        result = dict(restart=restart, pid=pid.value, sql_ready_seconds=time.monotonic() - start,
                      persistent_rows=[533], stop='SCM control')
        pipe.close()
        pipe = None
        # Use the service's SCM stop contract on both launches. Embedded client
        # release calls _Exit and does not report STOPPED to SCM. Neither path
        # is a graceful database shutdown; this is a disposable service smoke.
        stopped = ServiceStatus()
        if not api.ControlService(service, 1, C.byref(stopped)):
            raise C.WinError(C.get_last_error())
        k.CloseHandle(lock)
        lock = None
        if k.WaitForSingleObject(process, 30000) != 0:
            raise TimeoutError('service process remains after stop/client release')
        code = W.DWORD()
        if not k.GetExitCodeProcess(process, C.byref(code)) or code.value != 0:
            raise AssertionError(f'service process exit: {code.value}')
        stopped = await_stopped(service)
        result.update(process_exit=code.value, scm_state=stopped.state,
                      scm_win32_exit=stopped.win32_exit, scm_service_exit=stopped.service_exit)
        (root / ('restart' if restart else 'first')).with_suffix('.json').write_text(
            json.dumps(result, indent=2), encoding='utf-8')
        if stopped.win32_exit or stopped.service_exit:
            raise AssertionError(f'SCM recorded error {stopped.win32_exit}/{stopped.service_exit}')
        print('SERVICE_SQL_PASS ' + json.dumps(result), flush=True)
        return result
    finally:
        if pipe is not None:
            pipe.close()
        if lock is not None:
            k.CloseHandle(lock)
        if process is not None:
            k.CloseHandle(process)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--exe', type=Path, help='Test the extracted package product')
    args = parser.parse_args()
    source = Path(args.source_root).resolve()
    exe = args.exe.resolve(strict=True) if args.exe else source / 'build_phase0_nio/src/observer/seekdb.exe'
    if not str(exe).isascii() or len(str(exe)) >= 240:
        raise ValueError('service smoke requires a short ASCII installation path')
    if shutil.disk_usage(source).free < 3 * 1024**3:
        raise RuntimeError('service smoke requires 3 GiB free')
    root = source / 'build_phase0' / ('native-service-' + uuid.uuid4().hex)
    root.mkdir()
    base = root / 'instance'
    name = 'seek533-' + uuid.uuid4().hex
    manager = api.OpenSCManagerW(None, None, 1)
    if not manager:
        raise C.WinError(C.get_last_error())
    service, installed = None, False
    with exe.open('rb') as binary:
        exe_sha256 = hashlib.file_digest(binary, 'sha256').hexdigest()
    evidence = dict(service=name, base=str(base), exe=str(exe),
                    exe_sha256=exe_sha256,
                    passed=False, runs=[])
    print('SERVICE_LOG_ROOT=' + str(root), flush=True)
    try:
        existing = api.OpenServiceW(manager, name, 4)
        if existing:
            api.CloseServiceHandle(existing)
            raise RuntimeError('refusing pre-existing service')
        if C.get_last_error() != 1060:
            raise C.WinError(C.get_last_error())
        command(exe, ['--install-service', name, '--nodaemon', '--embedded',
                      '--base-dir', str(base), '--parameter', 'log_disk_size=2G',
                      '--parameter', 'datafile_size=32M', '--parameter', 'mysql_port_mode=disabled'], root, 'install')
        installed = True
        # SCM does not inherit this Python process's environment. Scope the
        # opt-out to this newly created disposable service, never the host.
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            'SYSTEM\\CurrentControlSet\\Services\\' + name, 0, winreg.KEY_SET_VALUE) as key:
            winreg.SetValueEx(key, 'Environment', 0, winreg.REG_MULTI_SZ, ['TELEMETRY_ENABLED=false'])
        service = api.OpenServiceW(manager, name, 4 | 16 | 32)
        if not service:
            raise C.WinError(C.get_last_error())
        for restart in (False, True):
            evidence['runs'].append(exercise(service, exe, base, root, restart))
        evidence['sql_checks_passed'] = True
    finally:
        cleanup_errors = []
        if service:
            try:
                if status(service).state != 1:
                    stopped = ServiceStatus()
                    if not api.ControlService(service, 1, C.byref(stopped)):
                        raise C.WinError(C.get_last_error())
                    await_stopped(service)
            except BaseException as error:
                cleanup_errors.append(repr(error))
            finally:
                api.CloseServiceHandle(service)
        try:
            if installed:
                command(exe, ['--remove-service', name], root, 'remove')
                found = api.OpenServiceW(manager, name, 4)
                if found:
                    api.CloseServiceHandle(found)
                    raise AssertionError('service still installed after removal')
                if C.get_last_error() != 1060:
                    raise C.WinError(C.get_last_error())
                evidence['removed'] = True
        except BaseException as error:
            cleanup_errors.append(repr(error))
        finally:
            api.CloseServiceHandle(manager)
            evidence['passed'] = bool(evidence.get('sql_checks_passed') and
                                      evidence.get('removed') and not cleanup_errors)
            evidence['cleanup_errors'] = cleanup_errors
            (root / 'result.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
        if cleanup_errors:
            raise RuntimeError('owned service cleanup failed: ' + '; '.join(cleanup_errors))
    if evidence['passed'] and evidence.get('removed'):
        # Keep configuration and logs. Remove only this successful test's store.
        shutil.rmtree(base / 'store')
        print('NATIVE_SERVICE_PASS', flush=True)


if __name__ == '__main__':
    main()
