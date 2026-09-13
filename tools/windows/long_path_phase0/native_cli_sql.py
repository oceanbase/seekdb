# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Direct native CLI persistence check; no bindings, aliases or TCP fallback."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid
import winreg


class Overlapped(C.Structure):
    _fields_ = [('internal', C.c_size_t), ('high', C.c_size_t),
                ('offset', W.DWORD), ('offset_high', W.DWORD), ('event', W.HANDLE)]


k = C.WinDLL('kernel32', use_last_error=True)
for name, args, result in [
    ('CreateFileW', [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
    ('CloseHandle', [W.HANDLE], W.BOOL),
    ('GetNamedPipeServerProcessId', [W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
    ('OpenProcess', [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
    ('GetExitCodeProcess', [W.HANDLE, C.POINTER(W.DWORD)], W.BOOL),
    ('LockFileEx', [W.HANDLE, W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.POINTER(Overlapped)], W.BOOL),
    ('CreateEventW', [C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR], W.HANDLE),
    ('WaitForSingleObject', [W.HANDLE, W.DWORD], W.DWORD),
    ('GetOverlappedResult', [W.HANDLE, C.POINTER(Overlapped), C.POINTER(W.DWORD), W.BOOL], W.BOOL),
    ('CancelIoEx', [W.HANDLE, C.POINTER(Overlapped)], W.BOOL),
    ('K32EnumProcessModules', [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD)], W.BOOL),
    ('K32GetModuleFileNameExW', [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD], W.DWORD),
]:
    f = getattr(k, name)
    f.argtypes, f.restype = args, result
for name in ('ReadFile', 'WriteFile'):
    getattr(k, name).argtypes = [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(Overlapped)]
    getattr(k, name).restype = W.BOOL
INVALID = C.c_void_p(-1).value


def wide(path):
    return '\\\\?\\' + os.path.abspath(path)


def open_handle(path, flags=0):
    h = k.CreateFileW(path, 0xC0000000, 3, None, 3, flags, None)
    if h == INVALID:
        raise C.WinError(C.get_last_error())
    return h


def read_discovery(path):
    # Match the native client/probe sharing contract. CRT open does not share
    # deletion, so it can conflict with endpoint publication or cleanup and
    # reduces the original Win32 error to a generic PermissionError.
    handle = k.CreateFileW(path, 0x80000000, 7, None, 3, 0, None)
    if handle == INVALID:
        raise C.WinError(C.get_last_error())
    try:
        data = C.create_string_buffer(129)
        count = W.DWORD()
        if not k.ReadFile(handle, data, len(data), C.byref(count), None):
            raise C.WinError(C.get_last_error())
        name = data.raw[:count.value].decode('utf-8')
        if count.value >= len(data) or not re.fullmatch(r'[0-9-]+', name):
            raise ValueError('invalid discovery')
        return name
    finally:
        k.CloseHandle(handle)


def check_discovery_reader(source):
    root = source / 'build_phase0' / ('discovery-reader-' + uuid.uuid4().hex)
    root.mkdir()
    path = root / 'sql.pipe'
    path.write_text('1234-5678', encoding='utf-8')
    # Hold DELETE access as a rename/delete operation would. The old CRT
    # reader must reproduce its sharing failure; the wide reader must succeed.
    handle = k.CreateFileW(wide(str(path)), 0x10000, 7, None, 3, 0, None)
    if handle == INVALID:
        raise C.WinError(C.get_last_error())
    try:
        try:
            path.read_text(encoding='utf-8')
        except PermissionError:
            pass
        else:
            raise AssertionError('CRT sharing control did not reproduce')
        if read_discovery(wide(str(path))) != '1234-5678':
            raise AssertionError('wide discovery reader returned wrong endpoint')
    finally:
        k.CloseHandle(handle)
    # A real sharing denial must still fail, with its original Win32 code.
    handle = k.CreateFileW(wide(str(path)), 0x80000000, 0, None, 3, 0, None)
    if handle == INVALID:
        raise C.WinError(C.get_last_error())
    try:
        try:
            read_discovery(wide(str(path)))
        except OSError as error:
            if error.winerror != 32:
                raise
        else:
            raise AssertionError('sharing denial was hidden')
    finally:
        k.CloseHandle(handle)
    path.write_text('malformed endpoint', encoding='utf-8')
    try:
        read_discovery(wide(str(path)))
    except ValueError:
        pass
    else:
        raise AssertionError('malformed discovery was accepted')
    path.unlink()
    try:
        read_discovery(wide(str(path)))
    except OSError as error:
        if error.winerror != 2:
            raise
    else:
        raise AssertionError('missing discovery was accepted')
    (root / 'result.json').write_text(json.dumps(dict(
        crt_sharing_failure_reproduced=True, wide_reader_passed=True,
        sharing_error=32, missing_error=2, malformed_rejected=True)), encoding='utf-8')
    print(f'DISCOVERY_READER_PASS LOG_ROOT={root}', flush=True)


def verify_product_modules(pid, exe):
    process = k.OpenProcess(0x0400 | 0x0010, False, pid)
    if not process:
        raise C.WinError(C.get_last_error())
    try:
        capacity = 128
        for _ in range(3):
            modules = (W.HMODULE * capacity)()
            needed = W.DWORD()
            if not k.K32EnumProcessModules(process, modules, C.sizeof(modules), C.byref(needed)):
                raise C.WinError(C.get_last_error())
            if needed.value <= C.sizeof(modules):
                break
            capacity = needed.value // C.sizeof(W.HMODULE) + 16
        else:
            raise RuntimeError('module list kept growing while taking identity snapshot')
        found = {}
        for module in modules[:needed.value // C.sizeof(W.HMODULE)]:
            path = C.create_unicode_buffer(32768)
            length = k.K32GetModuleFileNameExW(process, module, path, len(path))
            if not length or length >= len(path):
                raise RuntimeError(f'cannot resolve loaded module: Win32={C.get_last_error()}')
            name = os.path.basename(path.value).lower()
            if name in ('seekdb.exe', 'sqlite3.dll', 'libcurl.dll'):
                expected = exe if name == 'seekdb.exe' else exe.parent / name
                if not os.path.samefile(path.value, expected):
                    raise AssertionError(f'loaded module does not match distribution: {path.value}')
                with open(path.value, 'rb') as binary:
                    identity = hashlib.file_digest(binary, 'sha256').hexdigest()
                found[name] = dict(path=path.value, sha256=identity)
        if set(found) != {'seekdb.exe', 'sqlite3.dll', 'libcurl.dll'}:
            raise AssertionError(f'missing expected product modules: {found!r}')
        print('LOADED_MODULES=' + json.dumps(dict(pid=pid, modules=found)), flush=True)
    finally:
        k.CloseHandle(process)


class Pipe:
    def __init__(self, name):
        self.h = open_handle('\\\\.\\pipe\\' + name, 0x40000000)

    def close(self):
        k.CloseHandle(self.h)

    def io(self, data, size, writing):
        buf = C.create_string_buffer(data if writing else size)
        ov = Overlapped()
        ov.event = k.CreateEventW(None, True, False, None)
        if not ov.event:
            raise C.WinError(C.get_last_error())
        count = W.DWORD()
        try:
            fn = k.WriteFile if writing else k.ReadFile
            if not fn(self.h, buf, size, C.byref(count), C.byref(ov)):
                if C.get_last_error() != 997:
                    raise C.WinError(C.get_last_error())
                if k.WaitForSingleObject(ov.event, 10000) != 0:
                    k.CancelIoEx(self.h, C.byref(ov))
                    k.GetOverlappedResult(self.h, C.byref(ov), C.byref(count), True)
                    raise TimeoutError('named pipe I/O timeout')
                if not k.GetOverlappedResult(self.h, C.byref(ov), C.byref(count), False):
                    raise C.WinError(C.get_last_error())
            if count.value == 0:
                raise EOFError('named pipe closed')
            return count.value if writing else buf.raw[:count.value]
        finally:
            k.CloseHandle(ov.event)

    def read(self, n):
        data = b''
        while len(data) < n:
            data += self.io(None, n-len(data), False)
        return data

    def packet(self):
        header = self.read(4)
        n = int.from_bytes(header[:3], 'little')
        if not 0 < n <= 1024*1024:
            raise ValueError('unexpected MySQL packet size')
        data = self.read(n)
        if data[0] == 255:
            raise RuntimeError('MySQL error: ' + data[1:].decode('utf-8', 'replace'))
        return data

    def send(self, data, sequence=0):
        data = len(data).to_bytes(3, 'little') + bytes([sequence]) + data
        while data:
            data = data[self.io(data, len(data), True):]

    def login(self):
        if self.packet()[0] != 10:
            raise ValueError('invalid server greeting')
        # Protocol 4.1, secure connection; fresh test instance has empty root password.
        self.send(struct.pack('<IIB23x', 0x8201, 1024*1024, 45) + b'root\0\0', 1)
        if self.packet()[0] != 0:
            raise ValueError('login not accepted')

    def query(self, sql):
        self.send(b'\x03' + sql.encode('utf-8'))
        first = self.packet()
        if first[0] == 0:
            return []
        columns = first[0]
        if not 0 < columns < 251:
            raise ValueError('unexpected column count')
        for _ in range(columns):
            self.packet()
        if self.packet()[0] != 254:
            raise ValueError('missing column EOF')
        rows = []
        while True:
            row = self.packet()
            if row[0] == 254 and len(row) < 9:
                return rows
            values, offset = [], 0
            for _ in range(columns):
                length = row[offset]
                offset += 1
                if length >= 251 or offset + length > len(row):
                    raise ValueError('unexpected test row encoding')
                values.append(row[offset:offset+length].decode('utf-8'))
                offset += length
            rows.append(values)


class NativeDebugEvents:
    """Optional x64 Windows startup diagnostics for this harness-owned child."""
    def __init__(self, process, logs):
        self.process, self.logs = process, logs
        k.WaitForDebugEvent.argtypes = [C.c_void_p, W.DWORD]
        k.WaitForDebugEvent.restype = W.BOOL
        k.ContinueDebugEvent.argtypes = [W.DWORD, W.DWORD, W.DWORD]
        k.ContinueDebugEvent.restype = W.BOOL
        k.DebugActiveProcessStop.argtypes = [W.DWORD]
        k.DebugActiveProcessStop.restype = W.BOOL

    def pump(self):
        # DEBUG_EVENT has a 16-byte header and a 160-byte union on Windows x64.
        event = (C.c_uint64 * 22)()
        if not k.WaitForDebugEvent(C.byref(event), 0):
            return
        raw = bytes(event)
        kind, pid, tid = struct.unpack_from('<III', raw)
        status = 0x00010002  # DBG_CONTINUE
        if kind == 3:
            print(f'DEBUG_IMAGE_BASE={struct.unpack_from("<Q", raw, 40)[0]:#x}', flush=True)
        if kind in (3, 6):
            file_handle = struct.unpack_from('<Q', raw, 16)[0]
            if file_handle:
                k.CloseHandle(file_handle)
        if kind == 1:
            code = struct.unpack_from('<I', raw, 16)[0]
            address = struct.unpack_from('<Q', raw, 32)[0]
            count = min(struct.unpack_from('<I', raw, 40)[0], 15)
            params = struct.unpack_from('<' + 'Q'*count, raw, 48)
            first = struct.unpack_from('<I', raw, 168)[0]
            if code != 0x80000003:
                status = 0x80010001  # DBG_EXCEPTION_NOT_HANDLED
            if code == 0xc0000409 or not first:
                print(f'DEBUG_EXCEPTION code={code:#x} address={address:#x} '
                      f'thread={tid} first={first} parameters={params}', flush=True)
                import msvcrt
                dbg = C.WinDLL('dbghelp', use_last_error=True)
                dump = dbg.MiniDumpWriteDump
                dump.argtypes = [W.HANDLE, W.DWORD, W.HANDLE, W.DWORD,
                                 C.c_void_p, C.c_void_p, C.c_void_p]
                dump.restype = W.BOOL
                target = self.logs / 'startup.dmp'
                with target.open('wb') as f:
                    ok = dump(int(self.process._handle), pid, msvcrt.get_osfhandle(f.fileno()),
                              0, None, None, None)
                    error = C.get_last_error() if not ok else 0
                print(f'DEBUG_DUMP={target} success={bool(ok)} error={error}', flush=True)
        if not k.ContinueDebugEvent(pid, tid, status):
            raise C.WinError(C.get_last_error())

    def detach(self):
        k.DebugActiveProcessStop(self.process.pid)


def run(exe, base, cwd, logs, restart, debug=False, daemon=False, marker=533, hold=None, default_tcp=False,
        extra_parameters=()):
    lock, pipe, child = None, None, None
    started = time.monotonic()
    # Keep each launch's output instead of overwriting earlier evidence.
    launch = f'{restart}-{uuid.uuid4().hex}'
    with open(logs / f'{launch}.stdout.log', 'wb') as out, open(logs / f'{launch}.stderr.log', 'wb') as err:
        # Embedded mode keeps the default TCP port unless explicitly disabled.
        # Exercise the instance's discovered pipe.
        process = subprocess.Popen([str(exe), '--base-dir', base, '--embedded'] + ([] if daemon else ['--nodaemon']) + [
                                    '--parameter', 'log_disk_size=2G', '--parameter', 'datafile_size=32M',
                                    ] + ([] if default_tcp else ['--parameter', 'mysql_port_mode=disabled']) +
                                    [part for value in extra_parameters for part in ('--parameter', value)],
                                   cwd=cwd, stdout=out, stderr=err, creationflags=2 if debug else 0,
                                   env=dict(os.environ, TELEMETRY_ENABLED='false'))
        debugger = NativeDebugEvents(process, logs) if debug else None
        print(f'START PID={process.pid} RESTART={restart}', flush=True)
        try:
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                if debugger is not None:
                    debugger.pump()
                if process.poll() is not None and (not daemon or process.returncode != 0):
                    raise RuntimeError(f'product exited before ready: {process.returncode}')
                if lock is None:
                    try:
                        lock = open_handle(wide(base + '\\run\\seekdb.clients'))
                    except OSError as e:
                        if e.winerror not in (2, 3):
                            raise
                    if lock is not None:
                        ov = Overlapped()
                        if not k.LockFileEx(lock, 1, 0, 1, 0, C.byref(ov)):
                            raise C.WinError(C.get_last_error())
                        print('CLIENT_LOCK_HELD', flush=True)
                discovery = wide(base + '\\run\\sql.pipe')
                name = None
                if lock is not None:
                    try:
                        name = read_discovery(discovery)
                    except OSError as error:
                        # Absence before publication is expected. Permission,
                        # sharing and all other errors fail with their Win32 code.
                        if error.winerror not in (2, 3):
                            raise
                if name is not None:
                    if debugger is not None:
                        debugger.detach()
                    pipe = Pipe(name)
                    child_pid = W.DWORD()
                    if not k.GetNamedPipeServerProcessId(pipe.h, C.byref(child_pid)):
                        raise C.WinError(C.get_last_error())
                    if daemon:
                        child = k.OpenProcess(0x100000 | 0x1000, False, child_pid.value)
                        if not child:
                            raise C.WinError(C.get_last_error())
                        if child_pid.value == process.pid or process.wait(timeout=30) != 0:
                            raise AssertionError('daemon did not create a successful separate child')
                        print(f'DAEMON_CHILD PID={child_pid.value} PARENT={process.pid}', flush=True)
                    elif child_pid.value != process.pid:
                        raise AssertionError('discovered pipe belongs to another process')
                    pipe.login()
                    print(f'SQL_READY_SECONDS={time.monotonic() - started:.3f} RESTART={restart}', flush=True)
                    verify_product_modules(child_pid.value, exe)
                    break
                time.sleep(0.01)
            if pipe is None:
                raise TimeoutError('SQL readiness deadline')
            public_paths = pipe.query('SELECT SHA2(@@global.pid_file, 256), SHA2(@@global.socket, 256)')
            expected_paths = [[hashlib.sha256((base + suffix).encode('utf-8')).hexdigest()
                               for suffix in ('\\run\\observer.pid', '\\run\\sql.sock')]]
            if public_paths != expected_paths:
                raise AssertionError(f'public instance paths mismatch: {public_paths!r}')
            print(f'PUBLIC_PATHS_PASS RESTART={restart}', flush=True)
            if not restart:
                pipe.query('CREATE DATABASE seek533')
                pipe.query('CREATE TABLE seek533.persistence (id INT PRIMARY KEY, value VARCHAR(40))')
                pipe.query('CREATE TABLE seek533.writes (id INT PRIMARY KEY)')
                pipe.query('BEGIN')
                pipe.query(f"INSERT INTO seek533.persistence VALUES ({marker}, 'native-long-path')")
                pipe.query('COMMIT')
            rows = pipe.query('SELECT id, value FROM seek533.persistence ORDER BY id')
            if rows != [[str(marker), 'native-long-path']]:
                raise AssertionError(f'persisted rows mismatch: {rows!r}')
            print(f'SQL_READ_PASS RESTART={restart} MARKER={marker}', flush=True)
            previous = int(pipe.query('SELECT COUNT(*) FROM seek533.writes')[0][0])
            if restart and previous < 1:
                raise AssertionError('previous committed write missing after restart')
            pipe.query('BEGIN')
            pipe.query(f'INSERT INTO seek533.writes VALUES ({previous + 1})')
            pipe.query('COMMIT')
            pipe.query('BEGIN')
            pipe.query('INSERT INTO seek533.writes VALUES (-1)')
            pipe.query('ROLLBACK')
            writes = pipe.query('SELECT id FROM seek533.writes ORDER BY id')
            if writes != [[str(i)] for i in range(1, previous + 2)]:
                raise AssertionError(f'committed/rolled-back writes mismatch: {writes!r}')
            print(f'SQL_WRITE_ROLLBACK_PASS RESTART={restart} ROWS={previous + 1}', flush=True)
            if hold is not None:
                hold(pipe, name)
        finally:
            if debugger is not None:
                debugger.detach()
            if pipe is not None:
                pipe.close()
            if lock is not None:
                k.CloseHandle(lock)
            if child is not None:
                try:
                    result = k.WaitForSingleObject(child, 30000)
                    if result != 0:
                        raise RuntimeError(f'daemon child wait failed: {result}; instance preserved')
                    child_code = W.DWORD()
                    if not k.GetExitCodeProcess(child, C.byref(child_code)):
                        raise C.WinError(C.get_last_error())
                    if child_code.value != 0:
                        raise RuntimeError(f'daemon child exit {child_code.value}')
                    print(f'DAEMON_CHILD_EXIT=0 RESTART={restart}', flush=True)
                finally:
                    k.CloseHandle(child)
            try:
                code = process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()  # Only this harness-owned disposable test process.
                process.wait()
                raise TimeoutError('product did not exit after client release')
        if code != 0:
            raise RuntimeError(f'product exit {code}')
        print(f'PRODUCT_EXIT=0 RESTART={restart} TOTAL_SECONDS={time.monotonic() - started:.3f}', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', required=True)
    parser.add_argument('--exe', type=Path, help='Validate an installed product instead of the build output')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--reader-self-test', action='store_true')
    parser.add_argument('--base-units', type=int, default=int(os.environ.get('SEEKDB_NATIVE_SQL_BASE_UNITS', '280')))
    parser.add_argument('--unicode-path', action='store_true', default=os.environ.get('SEEKDB_NATIVE_SQL_UNICODE') == '1')
    parser.add_argument('--daemon', action='store_true', default=os.environ.get('SEEKDB_NATIVE_SQL_DAEMON') == '1')
    parser.add_argument('--cleanup-success', action='store_true',
                        default=os.environ.get('SEEKDB_NATIVE_SQL_CLEANUP_SUCCESS') == '1')
    parser.add_argument('--default-tcp', action='store_true',
                        default=os.environ.get('SEEKDB_NATIVE_SQL_DEFAULT_TCP') == '1')
    parser.add_argument('--instance-files', action='store_true',
                        default=os.environ.get('SEEKDB_NATIVE_SQL_INSTANCE_FILES') == '1')
    args = parser.parse_args()
    if args.debug and args.daemon:
        parser.error('daemon and debug cannot be combined')
    if not 160 <= args.base_units <= 2048:
        parser.error('base-units must be between 160 and 2048')
    source = Path(args.source_root).resolve()
    if args.reader_self_test:
        check_discovery_reader(source)
        return
    exe = args.exe.resolve(strict=True) if args.exe else source / 'build_phase0_nio/src/observer/seekdb.exe'
    required_gib = 3
    if shutil.disk_usage(source).free < required_gib*1024**3:
        raise RuntimeError(f'native SQL test needs {required_gib} GiB free for log budget and initialization')
    # PALF restart cleanup must not classify all descendants as temporary.
    root = source / 'build_phase0' / ('native-sql-' + uuid.uuid4().hex + '.tmp')
    root.mkdir()
    cwd = root / 'cwd'
    cwd.mkdir()
    if args.instance_files:
        (cwd / 'log').mkdir()
        (cwd / 'log' / 'sentinel').write_text('foreign cwd log', encoding='utf-8')
    base = str(root / 'instance')
    def units(path):
        return len(path.encode('utf-16-le')) // 2
    pattern = '目录😀 a%' if args.unicode_path else 'a'
    while units(base) < args.base_units:
        remaining = args.base_units - units(base)
        if remaining == 1:
            base += 'a'
        else:
            budget = min(80, remaining - 1)
            component = pattern * (budget // units(pattern))
            component += 'a' * (budget - units(component))
            base += '\\' + component
    if os.path.exists(wide(base)):
        raise RuntimeError('first-init target already exists')
    with exe.open('rb') as binary:
        identity = hashlib.file_digest(binary, 'sha256').hexdigest()
    sqlite_dll = exe.parent / 'sqlite3.dll'
    with sqlite_dll.open('rb') as binary:
        sqlite_identity = hashlib.file_digest(binary, 'sha256').hexdigest()
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r'SYSTEM\CurrentControlSet\Control\FileSystem') as key:
        policy, _ = winreg.QueryValueEx(key, 'LongPathsEnabled')
    print(f'LOG_ROOT={root} BASE_UNITS={units(base)} UNICODE={args.unicode_path} EXE_SHA256={identity} LONG_PATHS_ENABLED={policy}', flush=True)
    evidence = dict(base=base, base_units=units(base), exe=str(exe), exe_sha256=identity,
                    distributed_sqlite_sha256=sqlite_identity, policy=policy,
                    daemon=args.daemon, default_tcp=args.default_tcp,
                    instance_files=args.instance_files, tmp_ancestor=True, passed=False)
    (root / 'result.json').write_text(json.dumps(evidence, ensure_ascii=True, indent=2), encoding='utf-8')
    def load_data_files(pipe, name):
        source_file = root / 'input.csv'
        # An extra field produces the existing per-row diagnostic log while
        # the first column still imports the two expected IDs. Valid input
        # alone does not create an obloaddata error log.
        source_file.write_bytes(b'1\textra-field\n2\n')
        # The input is a short absolute path. This check concerns the instance's
        # diagnostic log, not an additional long external import path contract.
        literal = source_file.as_posix().replace("'", "''")
        pipe.query("SET GLOBAL secure_file_priv = ''")
        pipe.query('CREATE TABLE seek533.loaded (id INT PRIMARY KEY)')
        pipe.query(f"LOAD DATA INFILE '{literal}' INTO TABLE seek533.loaded")
        if pipe.query('SELECT id FROM seek533.loaded ORDER BY id') != [['1'], ['2']]:
            raise AssertionError('LOAD DATA row content mismatch')
        outputs = list(Path(wide(base + '\\log')).glob('obloaddata.log.*'))
        if not outputs or not any(b'BatchId' in p.read_bytes() for p in outputs):
            raise AssertionError('LOAD DATA diagnostic log missing from instance')
        print('LOAD_DATA_INSTANCE_LOG_PASS rows=2 foreign_cwd=1', flush=True)
    run(exe, base, cwd, root, False, args.debug, args.daemon, default_tcp=args.default_tcp,
        hold=load_data_files if args.instance_files else None)
    def loaded_readback(pipe, name):
        if pipe.query('SELECT id FROM seek533.loaded ORDER BY id') != [['1'], ['2']]:
            raise AssertionError('LOAD DATA rows missing after restart')
        print('LOAD_DATA_RESTART_PASS rows=2', flush=True)
    run(exe, base, cwd, root, True, args.debug, args.daemon, default_tcp=args.default_tcp,
        hold=loaded_readback if args.instance_files else None)
    if args.instance_files:
        if (sorted(p.relative_to(cwd).as_posix() for p in cwd.rglob('*')) != ['log', 'log/sentinel']
                or (cwd / 'log' / 'sentinel').read_text(encoding='utf-8') != 'foreign cwd log'):
            raise AssertionError('product changed foreign cwd fixtures')
    elif list(cwd.iterdir()):
        raise AssertionError('product created files in original cwd')
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r'SYSTEM\CurrentControlSet\Control\FileSystem') as key:
        policy_end, _ = winreg.QueryValueEx(key, 'LongPathsEnabled')
    evidence['policy_end'] = policy_end
    if policy_end != policy:
        raise AssertionError('LongPathsEnabled changed during this run; retain results as mixed-policy evidence')
    # Record actual files after all processes have exited; include the full
    # UTF-16 length even though the JSON stores paths relative to each instance.
    bases = [base]
    def walk_error(error):
        raise error
    for instance, instance_base in enumerate(bases):
        files = []
        for directory, children, names in os.walk(wide(instance_base), onerror=walk_error):
            for name in sorted(names):
                path = os.path.join(directory, name)
                files.append(dict(path=os.path.relpath(path, wide(instance_base)),
                                  units=units(path[4:]), bytes=os.path.getsize(path)))
        (root / f'instance-{instance}-files.json').write_text(
            json.dumps(files, ensure_ascii=True, indent=2), encoding='utf-8')
        print(f'INSTANCE_FILES={instance} COUNT={len(files)} BYTES={sum(f["bytes"] for f in files)}', flush=True)
    evidence['passed'] = True
    (root / 'result.json').write_text(json.dumps(evidence, ensure_ascii=True, indent=2), encoding='utf-8')
    if args.cleanup_success:
        # Only this run's disposable stores, after SQL/restart/process checks.
        # Retain launch output, instance logs, configuration and file inventory.
        for instance_base in bases:
            shutil.rmtree(wide(instance_base + '\\store'))
        evidence['stores_removed_after_success'] = True
        (root / 'result.json').write_text(json.dumps(evidence, ensure_ascii=True, indent=2), encoding='utf-8')
    print('NATIVE_SQL_PERSISTENCE_PASS', flush=True)


if __name__ == '__main__':
    main()
