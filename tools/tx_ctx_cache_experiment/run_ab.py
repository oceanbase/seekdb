import argparse
import datetime
import json
import os
from pathlib import Path
import signal
import re
import subprocess
import time

import pymysql

ROOT = None
BASELINE = None
CANDIDATE = None
PORT = None
CPUS = None
THREADS = 8
DURATION = 20
SYSBENCH_LUA = None


def connect(database=None):
    return pymysql.connect(host='127.0.0.1', port=PORT, user='root',
                           password='', database=database, connect_timeout=1,
                           read_timeout=30, autocommit=True)


def start(binary, directory):
    directory.mkdir(exist_ok=False)
    return launch(binary, directory)


def launch(binary, directory):
    with (directory / 'launch.txt').open('a') as output:
        args = ['taskset', '-c', CPUS, str(binary), '--nodaemon', '--port', str(PORT),
                '--base-dir', str(directory), '--log-level', 'INFO',
                '--parameter', 'memory_limit=4G', '--parameter', 'cpu_count=8',
                '--parameter', 'datafile_size=2G', '--parameter', 'datafile_maxsize=4G',
                '--parameter', 'log_disk_size=2G']
        begin = time.monotonic()
        proc = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT)
    while time.monotonic() - begin < 180:
        if proc.poll() is not None:
            raise RuntimeError(f'server exited {proc.returncode}, inspect {directory}')
        try:
            with connect() as conn:
                with conn.cursor() as cur:
                    cur.execute('select 1')
            elapsed = time.monotonic() - begin
            print(json.dumps({'phase': 'ready', 'binary': str(binary), 'pid': proc.pid,
                              'seconds': elapsed, 'directory': str(directory)}), flush=True)
            return proc, elapsed
        except (pymysql.Error, OSError):
            time.sleep(0.1)
    proc.kill()
    proc.wait()
    raise RuntimeError('server ready timeout')


def stop(proc, crash=False):
    if proc.poll() is None:
        proc.send_signal(signal.SIGKILL if crash else signal.SIGTERM)
        try:
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    time.sleep(0.2)


def sysbench(case, command='run', events=0, seconds=20, threads=8, output=None):
    args = ['sysbench', str(SYSBENCH_LUA / f'{case}.lua'), '--mysql-host=127.0.0.1',
            f'--mysql-port={PORT}', '--mysql-user=root', '--mysql-db=cache_test',
            '--tables=4', '--table-size=10000', '--db-ps-mode=disable',
            '--rand-type=uniform', '--rand-seed=20261010', f'--threads={threads}',
            f'--time={seconds}', f'--events={events}', '--report-interval=10', command]
    print(json.dumps({'phase': 'sysbench', 'args': args}), flush=True)
    env = os.environ.copy()
    env['LUA_PATH'] = str(SYSBENCH_LUA / '?.lua') + ';;'
    result = subprocess.run(args, capture_output=True, text=True,
                            timeout=max(180, seconds + 120), env=env)
    if output is not None:
        output.write_text(result.stdout + result.stderr)
    print(result.stdout[-2000:], flush=True)
    if result.returncode or 'FATAL' in result.stdout or 'FATAL' in result.stderr:
        raise RuntimeError(f'sysbench failed: {result.stderr}')
    return result.stdout


def clone_snapshot(name):
    target = ROOT / name
    target.mkdir(exist_ok=False)
    subprocess.run(['cp', '-a', '--reflink=auto', '--sparse=always',
                    str(ROOT / 'snapshot') + '/.', str(target)], check=True)
    # Do not mix the original process log with this restart's log.
    log_dir = target / 'log'
    if log_dir.exists():
        log_dir.rename(target / 'seed-log')
    return target


def prepare():
    proc, _ = launch(BASELINE, ROOT / 'seed') if (ROOT / 'seed').exists() else start(BASELINE, ROOT / 'seed')
    try:
        with connect() as conn:
            with conn.cursor() as cur:
                cur.execute('create database if not exists cache_test')
        with connect('cache_test') as conn:
            with conn.cursor() as cur:
                cur.execute("show tables like 'sbtest1'")
                prepared = bool(cur.fetchall())
        if not prepared:
            sysbench('oltp_read_write', 'prepare', seconds=0, output=ROOT / 'prepare.txt')
        sysbench('oltp_write_only', events=20000, seconds=0, output=ROOT / 'seed-write.txt')
        with connect('cache_test') as conn:
            with conn.cursor() as cur:
                cur.execute('create table recovery_check (id int primary key, value int)')
                cur.execute('insert into recovery_check values (1,42)')
                cur.execute('begin')
                cur.execute('insert into recovery_check values (2,99)')
                # This row must be rolled back when replay reconstructs the unfinished tx.
                stop(proc, crash=True)
    finally:
        stop(proc, crash=True)
    subprocess.run(['cp', '-a', '--reflink=auto', '--sparse=always',
                    str(ROOT / 'seed'), str(ROOT / 'snapshot')], check=True)
    print('SNAPSHOT_READY', flush=True)


def compare():
    results = []
    order = [('master', BASELINE), ('cache', CANDIDATE),
             ('cache', CANDIDATE), ('master', BASELINE),
             ('master', BASELINE), ('cache', CANDIDATE)]
    for i, (name, binary) in enumerate(order):
        directory = clone_snapshot(f'uniform-{i}-{name}')
        proc, ready = launch(binary, directory)
        try:
            with connect('cache_test') as conn:
                with conn.cursor() as cur:
                    cur.execute('select * from recovery_check order by id')
                    rows = cur.fetchall()
                    if rows != ((1, 42),):
                        raise RuntimeError(f'recovery mismatch: {rows}')
            result = {'round': i, 'variant': name, 'ready_seconds': ready,
                      'directory': str(directory), 'recovery_rows': rows}
            lines = []
            for logfile in (directory / 'log').glob('seekdb.log*'):
                with logfile.open(errors='replace') as log:
                    for line in log:
                        if any(marker in line for marker in ('enable replay status success',
                                                               'disable local replay(',
                                                               'close replay tx ctx cache')):
                            lines.append(line.strip())
            lines.sort()
            result['replay_markers'] = lines
            starts = [datetime.datetime.fromisoformat(line[1:27]) for line in lines
                      if 'enable replay status success' in line]
            ends = [datetime.datetime.fromisoformat(line[1:27]) for line in lines
                    if 'disable local replay(' in line]
            if starts and ends:
                result['replay_seconds'] = (ends[-1] - starts[0]).total_seconds()
            with connect() as conn:
                with conn.cursor() as cur:
                    cur.execute("alter system set syslog_level='ERROR'")
            # Warm up before the measured workloads, after replay duration was recorded.
            sysbench('oltp_write_only', seconds=5, threads=THREADS,
                     output=ROOT / f'uniform-warmup-{i}.txt')
            for case in ('oltp_write_only', 'oltp_read_write'):
                result[case] = sysbench(case, seconds=DURATION, threads=THREADS,
                                       output=ROOT / f'uniform-{case}-{i}-{name}.txt')
            result['valid_sysbench'] = all(
                re.search(r'ignored errors:\s+0\s', result[case]) is not None
                for case in ('oltp_write_only', 'oltp_read_write'))
            results.append(result)
            (ROOT / 'uniform_results.json').write_text(json.dumps(results, indent=2))
        finally:
            stop(proc)
        print(json.dumps({'phase': 'round_done', 'round': i, 'variant': name}), flush=True)


def inspect_idle():
    directory = clone_snapshot(f'idle-inspection-{time.time_ns()}')
    proc, _ = launch(CANDIDATE, directory)
    sessions = []
    try:
        for i in range(4):
            conn = connect('cache_test')
            sessions.append(conn)
            with conn.cursor() as cur:
                for _ in range(100):
                    cur.execute('update sbtest1 set k=k+1 where id=%s', (i + 1,))
        slot = "'oceanbase::share::ObServerServiceSlot<oceanbase::storage::ObLSService>::service_'"
        sessions_slot = "'oceanbase::share::ObServerServiceSlot<oceanbase::sql::ObSQLSessionMgr>::service_'"
        commands = ['set pagination off', 'set print thread-events off',
                    f'printf "REPLAY_CACHE=%p\\n", {slot}->ls_->ls_tx_svr_.replay_tx_ctx_cache_',
                    f'printf "RECOVER_CACHE=%p\\n", {slot}->ls_->tx_table_.tx_ctx_table_.recover_helper_.tx_ctx_cache_',
                    'printf "ACTIVE_TX_CTX=%ld\\n", *(long *)&\'oceanbase::transaction::ObTxCtxFactory::active_tx_ctx_count_\'',
                    f'printf "SESSION_OBJECTS=%ld\\n", {sessions_slot}->sessinfo_map_.alloc_handle_.active_count_',
                    'detach']
        def snapshot(label):
            args = ['gdb', '-q', '-batch', '-iex', 'set print thread-events off', '-p', str(proc.pid)]
            for command in commands:
                args.extend(['-ex', command])
            result = subprocess.run(args, capture_output=True, text=True, timeout=60)
            output = result.stdout + result.stderr
            (ROOT / f'{label}.txt').write_text(output)
            print(output[-3000:], flush=True)
            if result.returncode or any(key + '=' not in output for key in
                                       ('REPLAY_CACHE', 'RECOVER_CACHE', 'ACTIVE_TX_CTX', 'SESSION_OBJECTS')):
                raise RuntimeError('idle gdb snapshot failed')
        snapshot('connections-open')
        for conn in sessions:
            conn.close()
        sessions.clear()
        time.sleep(2)
        snapshot('connections-closed')
    finally:
        for conn in sessions:
            conn.close()
        stop(proc)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['prepare', 'compare', 'inspect'])
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--port', type=int, default=45461)
    parser.add_argument('--cpus', default='0-7')
    parser.add_argument('--threads', type=int, default=8)
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--sysbench-lua', type=Path, default=Path('/usr/sysbench/share/sysbench'))
    args = parser.parse_args()
    ROOT = args.root.resolve()
    ROOT.mkdir(parents=True, exist_ok=True)
    BASELINE = args.baseline.resolve()
    CANDIDATE = args.candidate.resolve()
    PORT = args.port
    CPUS = args.cpus
    THREADS = args.threads
    DURATION = args.seconds
    SYSBENCH_LUA = args.sysbench_lua.resolve()
    {'prepare': prepare, 'compare': compare, 'inspect': inspect_idle}[args.phase]()
