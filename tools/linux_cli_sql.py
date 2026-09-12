"""Exercise the existing Linux cwd, Unix socket and embedded exit contracts."""
import argparse
import fcntl
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import struct
import subprocess
import time
import uuid

import pymysql


def file_sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def run_mysqltest(source, base, root, connection):
    binary = source / 'deps/3rd/u01/obclient/bin/mysqltest'
    suite = source / 'tools/deploy/mysql_test'
    results = []
    with connection.cursor() as cursor:
        cursor.execute('CREATE DATABASE seek533_mysqltest')
    for case in ('big_trans_with_mutil_redo', 'ms_lose_rollback'):
        test = suite / 't' / (case + '.test')
        expected = suite / 'r/mysql' / (case + '.result')
        output = root / 'mysqltest' / case
        output.mkdir(parents=True)
        command = [str(binary), '--no-defaults', '--protocol=socket', '--socket=run/sql.sock',
                   '--user=root', '--database=seek533_mysqltest', '--connect-timeout=5',
                   '--max-connect-retries=1', '--default-character-set=utf8mb4',
                   '--basedir=' + str(suite), '--test-file=' + str(test),
                   '--result-file=' + str(expected), '--logdir=' + str(output),
                   '--tmpdir=' + str(output)]
        record = dict(case=case, command=command, binary_sha256=file_sha(binary),
                      test_sha256=file_sha(test), expected_sha256=file_sha(expected))
        started = time.monotonic()
        with (output / 'stdout.log').open('wb') as out, (output / 'stderr.log').open('wb') as err:
            try:
                completed = subprocess.run(command, cwd=base, stdout=out, stderr=err,
                                           timeout=180, check=False)
                record['exit_code'] = completed.returncode
            except subprocess.TimeoutExpired:
                record['timed_out'] = True
            finally:
                record['seconds'] = round(time.monotonic() - started, 3)
                (output / 'result.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        if record.get('exit_code') != 0:
            raise RuntimeError(f'mysqltest {case} failed; retained {output}')
        results.append(record)
        print(f'LINUX_MYSQLTEST_PASS case={case}', flush=True)
    return results


def run(exe, base, cwd, root, restart, mysqltest_source=None):
    launch = dict(restart=restart, passed=False)
    lock = None
    connection = None
    started = time.monotonic()
    with (root / f'{restart}.stdout.log').open('wb') as out, (root / f'{restart}.stderr.log').open('wb') as err:
        process = subprocess.Popen(
            [str(exe), '--base-dir', str(base), '--embedded', '--nodaemon',
             '--parameter', 'mysql_port_mode=disabled', '--parameter', 'log_disk_size=2G',
             '--parameter', 'datafile_size=32M'], cwd=cwd, stdout=out, stderr=err,
            env=dict(os.environ, TELEMETRY_ENABLED='false'))
        launch['pid'] = process.pid
        print(f'LINUX_SQL_START pid={process.pid} restart={restart}', flush=True)
        try:
            deadline = started + 180
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f'Product exited before SQL ready: {process.returncode}')
                if lock is None:
                    try:
                        lock = os.open(base / 'run/seekdb.clients', os.O_RDONLY)
                    except FileNotFoundError:
                        pass
                    else:
                        fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
                if lock is not None and (base / 'run/sql.sock').exists():
                    # The original Linux ABI binds run/sql.sock relative to the
                    # product cwd. Connect from that same directory without
                    # requiring a long sockaddr_un path or a filesystem alias.
                    previous = Path.cwd()
                    try:
                        os.chdir(base)
                        connection = pymysql.connect(unix_socket='run/sql.sock', user='root',
                                                     password='', charset='utf8mb4', autocommit=True,
                                                     connect_timeout=3, read_timeout=10, write_timeout=10)
                    except pymysql.err.OperationalError as error:
                        if error.args[0] not in (2002, 2003):
                            raise
                    finally:
                        os.chdir(previous)
                    if connection is not None:
                        with connection.cursor() as cursor:
                            cursor.execute('SELECT START_SERVICE_TIME FROM oceanbase.V$OB_SERVER_STAT')
                            ready = cursor.fetchall()
                        if len(ready) == 1 and int(ready[0][0]) > 0:
                            break
                        connection.close()
                        connection = None
                time.sleep(0.02)
            if connection is None:
                raise TimeoutError('SQL readiness deadline')
            peer_pid = struct.unpack('3i', connection._sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[0]
            if peer_pid != process.pid or Path(f'/proc/{process.pid}/cwd').resolve(strict=True) != base:
                raise AssertionError('Unix endpoint/process cwd belongs to another instance')
            launch.update(ready_seconds=round(time.monotonic() - started, 3), peer_pid=peer_pid,
                          product_cwd_matches=True, start_service_time=int(ready[0][0]))
            with connection.cursor() as cursor:
                if not restart:
                    cursor.execute('CREATE DATABASE seek533')
                    cursor.execute('CREATE TABLE seek533.persistence (id INT PRIMARY KEY, value VARCHAR(40))')
                    cursor.execute('BEGIN')
                    cursor.execute("INSERT INTO seek533.persistence VALUES (533, 'posix-cwd')")
                    cursor.execute('COMMIT')
                    cursor.execute('CREATE TABLE seek533.writes (id INT PRIMARY KEY)')
                cursor.execute('SELECT id, value FROM seek533.persistence ORDER BY id')
                if cursor.fetchall() != ((533, 'posix-cwd'),):
                    raise AssertionError('Persisted value missing')
                cursor.execute('SELECT COUNT(*) FROM seek533.writes')
                previous = cursor.fetchone()[0]
                if previous != int(restart):
                    raise AssertionError('Prior committed write count differs')
                cursor.execute('BEGIN')
                cursor.execute('INSERT INTO seek533.writes VALUES (%s)', (previous + 1,))
                cursor.execute('COMMIT')
                cursor.execute('BEGIN')
                cursor.execute('INSERT INTO seek533.writes VALUES (-1)')
                cursor.execute('ROLLBACK')
                cursor.execute('SELECT id FROM seek533.writes ORDER BY id')
                if cursor.fetchall() != tuple((i,) for i in range(1, previous + 2)):
                    raise AssertionError('Commit/rollback content differs')
                launch['committed_rows'] = previous + 1
            if mysqltest_source is not None:
                launch['mysqltest'] = run_mysqltest(mysqltest_source, base, root, connection)
            launch['passed'] = True
        finally:
            if connection is not None:
                connection.close()
            if lock is not None:
                os.close(lock)
            try:
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                launch['passed'] = False
                launch['termination_requested_after_timeout'] = True
                process.terminate()  # Graceful stop of this test-owned process.
                code = process.wait(timeout=30)
            launch.update(exit_code=code, total_seconds=round(time.monotonic() - started, 3))
            (root / f'{restart}.json').write_text(json.dumps(launch, indent=2), encoding='utf-8')
            if code != 0 or not launch['passed']:
                raise RuntimeError(f'Product launch failed; retained {root}/{restart}.json')
    print(f'LINUX_SQL_LAUNCH_PASS restart={restart} rows={launch["committed_rows"]}', flush=True)
    return launch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--mysqltest', action='store_true', help='Run existing redo and rollback mysqltest cases')
    args = parser.parse_args()
    if platform.system() != 'Linux':
        parser.error('This check requires Linux process and socket credentials')
    source = args.source_root.resolve()
    exe = source / 'build_release/src/observer/seekdb'
    if not exe.is_file():
        raise FileNotFoundError('Build the product through build.sh release first')
    if shutil.disk_usage(source).free < 3 * 1024**3:
        raise RuntimeError('Product SQL check requires 3 GiB free')
    root = source / 'build_phase0' / ('linux-sql-' + uuid.uuid4().hex)
    root.mkdir(parents=True)
    cwd = root / 'cwd'
    cwd.mkdir()
    base = root / 'instance'
    evidence = dict(exe_sha256=file_sha(exe), platform=platform.platform(),
                    pymysql=importlib.metadata.version('PyMySQL'), base=str(base), passed=False)
    result = root / 'result.json'
    result.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('LINUX_SQL_ROOT=' + str(root), flush=True)
    if base.exists():
        raise AssertionError('First-init instance already exists')
    launches = [run(exe, base, cwd, root, restart,
                    source if args.mysqltest and not restart else None) for restart in (False, True)]
    if list(cwd.iterdir()):
        raise AssertionError('Product wrote into original cwd')
    files = [dict(path=str(path.relative_to(base)), bytes=path.stat().st_size)
             for path in base.rglob('*') if path.is_file()]
    evidence.update(launches=launches, files=files, passed=True)
    result.write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print('LINUX_SQL_PERSISTENCE_PASS', flush=True)


if __name__ == '__main__':
    main()
