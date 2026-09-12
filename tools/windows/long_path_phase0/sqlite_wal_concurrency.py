"""Compare SQLite WAL connection churn using the product DLL, without aliases."""
import argparse
import collections
import concurrent.futures
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import threading
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--serial', action='store_true', help='Control: serialize complete connection lifetimes')
    parser.add_argument('--case', choices=['short', 'short-extended', 'long-extended', 'unicode-extended', 'unicode-short', 'unicode-short-extended'])
    parser.add_argument('--dll', type=Path)
    parser.add_argument('--timeout-before-config', action='store_true', help='Diagnostic: apply existing 5000ms busy timeout before WAL/NORMAL')
    parser.add_argument('--mixed', action='store_true', help='Alternate ordinary and extended names for the same short file')
    args = parser.parse_args()
    if args.mixed and args.case not in ('short', 'unicode-short'):
        parser.error('--mixed requires --case short or unicode-short')
    dll = args.dll or args.source_root / 'build_phase0_nio/src/observer/sqlite3.dll'
    lib = C.CDLL(str(dll.resolve()))
    module_name = C.WinDLL('kernel32', use_last_error=True).GetModuleFileNameW
    module_name.argtypes = [C.c_void_p, C.c_wchar_p, C.c_uint32]
    module_name.restype = C.c_uint32
    module_path = C.create_unicode_buffer(32768)
    length = module_name(lib._handle, module_path, len(module_path))
    if not length or length == len(module_path):
        raise C.WinError(C.get_last_error())
    if not os.path.samefile(dll, module_path.value):
        raise RuntimeError('Unexpected loaded DLL')
    print('LOADED_DLL=' + module_path.value, flush=True)
    lib.sqlite3_open_v2.argtypes = [C.c_char_p, C.POINTER(C.c_void_p), C.c_int, C.c_char_p]
    lib.sqlite3_exec.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_void_p, C.c_void_p]
    lib.sqlite3_close.argtypes = [C.c_void_p]
    lib.sqlite3_busy_timeout.argtypes = [C.c_void_p, C.c_int]
    lib.sqlite3_sourceid.restype = C.c_char_p
    print('DLL_SHA256=' + hashlib.sha256(dll.read_bytes()).hexdigest(), flush=True)
    print('SOURCE_ID=' + lib.sqlite3_sourceid().decode(), flush=True)
    print('THREADSAFE=' + str(lib.sqlite3_threadsafe()), flush=True)
    root = args.source_root / 'build_phase0' / ('wal-concurrency-' + uuid.uuid4().hex)
    root.mkdir()
    print('ROOT=' + str(root), flush=True)
    protocol_count = 0
    failed = False
    def units(value):
        return len(value.encode("utf-16-le")) // 2
    for label, extended, long_path in [('short', False, False), ('short-extended', True, False), ('long-extended', True, True), ('unicode-extended', True, True), ('unicode-short', False, False), ('unicode-short-extended', True, False)]:
        if args.case and args.case != label:
            continue
        directory = str(root / label)
        if label.startswith('unicode-short'):
            directory += '\\目录😀 a%'
        if long_path:
            while units(directory) < 2048:
                remaining = 2048 - units(directory) - 1
                if remaining < 1:
                    break
                budget = min(80, remaining)
                pattern = '目录😀 a%' if label == 'unicode-extended' else 'a'
                component = pattern * (budget // units(pattern))
                directory += '\\' + component + 'a' * (budget - units(component))
        Path('\\\\?\\' + directory).mkdir(parents=True)
        name = (('\\\\?\\' if extended else '') + directory + '\\meta.db').encode()
        ordinary_name = (directory + '\\meta.db').encode()
        extended_name = ('\\\\?\\' + directory + '\\meta.db').encode()
        def open_db(selected_name=None):
            db = C.c_void_p()
            rc = lib.sqlite3_open_v2(selected_name or name, C.byref(db), 6, b'win32-longpath')
            if rc:
                if db: lib.sqlite3_close(db)
                raise RuntimeError((label, 'open', rc))
            return db
        db = open_db()
        try:
            rc = lib.sqlite3_exec(db, b'PRAGMA journal_mode=WAL; CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER)', None, None, None)
            if rc: raise RuntimeError((label, 'setup', rc))
        finally:
            lib.sqlite3_close(db)
        if args.mixed:
            ordinary = os.stat(ordinary_name.decode())
            expanded = os.stat(extended_name.decode())
            if not ordinary.st_ino or (ordinary.st_dev, ordinary.st_ino) != (expanded.st_dev, expanded.st_ino):
                raise RuntimeError('File identity mismatch')
            print('SAME_FILE_ID=' + str((ordinary.st_dev, ordinary.st_ino)), flush=True)
        barrier = threading.Barrier(1 if args.serial else 8)
        def worker(index):
            counts = collections.Counter()
            barrier.wait(timeout=30)
            for iteration in range(16):
                db = open_db((ordinary_name if (index + iteration) % 2 == 0 else extended_name) if args.mixed else None)
                try:
                    if args.timeout_before_config:
                        rc = lib.sqlite3_busy_timeout(db, 5000)
                        if rc: raise RuntimeError((label, 'busy timeout', rc))
                    # Without --timeout-before-config, retain the legacy
                    # ordering as a diagnostic control. The build.ps1 entry
                    # passes it to match the Windows production connection.
                    for stage, sql in [('wal', b'PRAGMA journal_mode=WAL'), ('sync', b'PRAGMA synchronous=NORMAL')]:
                        rc = lib.sqlite3_exec(db, sql, None, None, None)
                        counts[(stage, rc)] += 1
                        if rc not in (0, 5, 6): break
                    else:
                        lib.sqlite3_busy_timeout(db, 5000)
                        sql = f'INSERT INTO t VALUES({index * 1000 + iteration}, 1)'.encode()
                        counts[('insert', lib.sqlite3_exec(db, sql, None, None, None))] += 1
                finally:
                    rc = lib.sqlite3_close(db)
                    if rc: raise RuntimeError((label, 'close', rc))
            return counts
        counts = collections.Counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1 if args.serial else 8) as pool:
            for result in pool.map(worker, range(8)): counts.update(result)
        protocol_count += sum(n for (_, rc), n in counts.items() if rc == 15)
        failed |= args.mixed and any(rc != 0 for (_, rc) in counts)
        failed |= any(rc != 0 and not (stage in ('wal', 'sync') and rc in (5, 6))
                      for stage, rc in counts)
        rows = []
        callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_char_p), C.POINTER(C.c_char_p))
        @callback_type
        def read_count(context, columns, values, names):
            rows.append(tuple(values[i].decode() if values[i] is not None else None for i in range(columns)))
            return 0
        db = open_db()
        try:
            rc = lib.sqlite3_exec(db, b'SELECT id, v FROM t ORDER BY id', C.cast(read_count, C.c_void_p), None, None)
            expected = [(str(index * 1000 + iteration), '1') for index in range(8) for iteration in range(16)]
            content_matches = rc == 0 and rows == expected
            failed |= not content_matches
            content_sha256 = hashlib.sha256(json.dumps(rows).encode()).hexdigest()
            reopened_rows = len(rows)
            rows.clear()
            checkpoint_rc = lib.sqlite3_exec(db, b'PRAGMA wal_checkpoint(TRUNCATE)', C.cast(read_count, C.c_void_p), None, None)
            checkpoint = list(rows)
            failed |= checkpoint_rc != 0 or checkpoint != [('0', '0', '0')]
            rows.clear()
            integrity_rc = lib.sqlite3_exec(db, b'PRAGMA integrity_check', C.cast(read_count, C.c_void_p), None, None)
            failed |= integrity_rc != 0 or rows != [('ok',)]
        finally:
            if lib.sqlite3_close(db):
                raise RuntimeError((label, 'verification close'))
        print(json.dumps({'case': label, 'serial': args.serial, 'timeout_before_config': args.timeout_before_config, 'base_units': units(directory), 'reopened_rows': [reopened_rows], 'content_matches': content_matches, 'content_sha256': content_sha256, 'checkpoint_rc': checkpoint_rc, 'checkpoint': checkpoint, 'integrity_rc': integrity_rc, 'integrity': rows, 'results': {f'{stage}:{rc}': n for (stage, rc), n in sorted(counts.items())}}), flush=True)
    print('SQLITE_PROTOCOL_COUNT=' + str(protocol_count), flush=True)
    return 1 if failed or protocol_count else 0


if __name__ == '__main__':
    raise SystemExit(main())
