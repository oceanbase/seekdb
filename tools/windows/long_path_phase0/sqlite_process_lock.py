"""Exercise WAL writer exclusion and rollback across two independent processes."""
import argparse
import ctypes as C
import json
import multiprocessing as mp
import os
from pathlib import Path
import uuid

from sqlite_identity import identity


class Database:
    def __init__(self, dll, path, vfs):
        self.lib = C.CDLL(str(dll.resolve()))
        self.lib.sqlite3_open_v2.argtypes = [C.c_char_p, C.POINTER(C.c_void_p), C.c_int, C.c_char_p]
        self.lib.sqlite3_exec.argtypes = [C.c_void_p, C.c_char_p, C.c_void_p, C.c_void_p, C.c_void_p]
        self.lib.sqlite3_close.argtypes = [C.c_void_p]
        self.lib.sqlite3_busy_timeout.argtypes = [C.c_void_p, C.c_int]
        self.handle = C.c_void_p()
        rc = self.lib.sqlite3_open_v2(path.encode(), C.byref(self.handle), 6, vfs)
        if rc:
            if self.handle:
                self.lib.sqlite3_close(self.handle)
            raise RuntimeError(('open', rc))
        try:
            rc = self.lib.sqlite3_busy_timeout(self.handle, 200)
            if rc:
                raise RuntimeError(('busy timeout', rc))
            self.execute('PRAGMA journal_mode=WAL')
            self.execute('PRAGMA synchronous=NORMAL')
        except BaseException:
            self.close()
            raise

    def execute(self, sql, expected=0):
        rows = []
        callback_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_int, C.POINTER(C.c_char_p), C.POINTER(C.c_char_p))
        @callback_type
        def callback(context, count, values, names):
            rows.append(tuple(values[i].decode() if values[i] else None for i in range(count)))
            return 0
        rc = self.lib.sqlite3_exec(self.handle, sql.encode(), C.cast(callback, C.c_void_p), None, None)
        if rc != expected:
            raise RuntimeError(('execute', rc, expected))
        return rows

    def close(self):
        if self.handle:
            rc = self.lib.sqlite3_close(self.handle)
            if rc:
                raise RuntimeError(('close', rc))
            self.handle = None


def receive(pipe):
    if not pipe.poll(30):
        raise RuntimeError('Child protocol timed out')
    result = pipe.recv()
    if isinstance(result, dict) and 'error' in result:
        raise RuntimeError(result['error'])
    return result


def child(dll, path, pipe):
    db = None
    try:
        report = identity(dll)
        db = Database(dll, path, b'win32-longpath')
        st = os.stat(path if path.startswith('\\\\?\\') else '\\\\?\\' + path)
        pipe.send({'identity': report, 'file_id': (st.st_dev, st.st_ino)})
        assert receive(pipe) == 'held'
        db.execute('BEGIN IMMEDIATE', expected=5)
        assert db.execute('SELECT id FROM t ORDER BY id') == []
        pipe.send('writer-blocked-reader-isolated')
        assert receive(pipe) == 'released'
        db.execute('BEGIN IMMEDIATE; INSERT INTO t VALUES(2); COMMIT')
        db.execute('BEGIN IMMEDIATE; INSERT INTO t VALUES(3); ROLLBACK')
        db.close()
        db = None
        pipe.send('committed-and-rolled-back')
    except BaseException as error:
        pipe.send({'error': repr(error)})
        raise
    finally:
        if db is not None:
            db.close()
        pipe.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--path-units', type=int, default=0)
    parser.add_argument('--child-ordinary', action='store_true')
    args = parser.parse_args()
    root = args.source_root / 'build_phase0' / ('sqlite-process-' + uuid.uuid4().hex)
    root.mkdir()
    path = str(root / 'data.db')
    units = lambda value: len(value.encode('utf-16-le')) // 2
    if args.path_units:
        if not 280 <= args.path_units <= 4092:
            parser.error('path-units must be 280..4092, reserving four units for WAL/SHM')
        directory = str(root / 'database')
        target = args.path_units - len('\\data.db')
        while units(directory) < target:
            remaining = target - units(directory)
            if remaining == 1:
                directory += 'a'
            else:
                budget = min(80, remaining - 1)
                pattern = '目录😀 a%'
                component = pattern * (budget // units(pattern))
                directory += '\\' + component + 'a' * (budget - units(component))
        Path('\\\\?\\' + directory).mkdir(parents=True)
        path = directory + '\\data.db'
    extended = '\\\\?\\' + path
    report = identity(args.dll)
    # Retain default-VFS interop for the short case. At long paths both
    # processes use the production VFS and extended spelling; ordinary spelling
    # in the child is an explicit additional interoperability diagnostic.
    parent_path = extended if args.path_units else path
    child_path = path if args.child_ordinary else extended
    parent_vfs = b'win32-longpath' if args.path_units else None
    db = Database(args.dll, parent_path, parent_vfs)
    db.execute('CREATE TABLE t(id INTEGER PRIMARY KEY)')
    st = os.stat(extended)
    assert st.st_ino != 0
    parent_pipe, child_pipe = mp.Pipe()
    process = mp.get_context('spawn').Process(target=child, args=(args.dll, child_path, child_pipe))
    process.start()
    child_pipe.close()
    try:
        ready = receive(parent_pipe)
        assert tuple(ready['file_id']) == (st.st_dev, st.st_ino)
        assert ready['identity']['sha256'] == report['sha256']
        db.execute('BEGIN IMMEDIATE; INSERT INTO t VALUES(1)')
        parent_pipe.send('held')
        assert receive(parent_pipe) == 'writer-blocked-reader-isolated'
        db.execute('ROLLBACK')
        parent_pipe.send('released')
        assert receive(parent_pipe) == 'committed-and-rolled-back'
        process.join(30)
        assert process.exitcode == 0
        db.close()
        db = Database(args.dll, parent_path, parent_vfs)
        assert db.execute('SELECT id FROM t ORDER BY id') == [('2',)]
        assert db.execute('PRAGMA wal_checkpoint(TRUNCATE)') == [('0', '0', '0')]
        assert db.execute('PRAGMA integrity_check') == [('ok',)]
        result = {'root': str(root), 'identity': report, 'child_exit': process.exitcode,
                          'file_id': (st.st_dev, st.st_ino), 'writer_exclusion': True,
                          'rollback_and_reopen': True, 'default_vfs_interop': not bool(args.path_units),
                          'main_path_units': units(path), 'side_path_units': units(path) + 4,
                          'parent_extended': bool(args.path_units), 'child_extended': not args.child_ordinary}
        (root / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps(result), flush=True)
    finally:
        db.close()
        parent_pipe.close()
        if process.is_alive():
            process.terminate()  # Only this test's child; the scenario has failed.
            process.join(10)


if __name__ == '__main__':
    main()
