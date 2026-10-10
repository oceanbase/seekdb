"""Restart durability and primary/standby checks using isolated local instances."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import signal
import subprocess
import time

import pymysql


def emit(**value):
    print(json.dumps(value, default=str), flush=True)


class Node:
    def __init__(self, binary, directory, port, rpc_port, role='PRIMARY', source=None):
        self.binary, self.directory = binary, directory
        self.port, self.rpc_port, self.role, self.source = port, rpc_port, role, source
        self.proc = None

    def connect(self, database='cache_test'):
        return pymysql.connect(host='127.0.0.1', port=self.port, user='root',
                               database=database, autocommit=True, connect_timeout=1,
                               read_timeout=120, charset='utf8mb4')

    def query(self, sql, params=None, database='cache_test'):
        with self.connect(database) as conn:
            with conn.cursor() as cur:
                cur.execute(sql, params)
                return cur.fetchall()

    def start(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        command = ['taskset', '-c', '48-55', str(self.binary), '--nodaemon',
                   '--base-dir', str(self.directory), '--port', str(self.port),
                   '--role', self.role, '--log-level', 'INFO',
                   '--parameter', f'rpc_port={self.rpc_port}',
                   '--parameter', 'enable_rpc_service=true',
                   '--parameter', 'memory_budget=4G', '--parameter', 'cpu_count=8',
                   '--parameter', 'datafile_size=2G', '--parameter', 'datafile_maxsize=4G',
                   '--parameter', 'log_disk_size=2G']
        if self.source:
            command.extend(['--parameter', f'log_restore_source={self.source}'])
        started = time.monotonic()
        with (self.directory / 'launch.txt').open('a') as log:
            self.proc = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        while time.monotonic() - started < 180:
            if self.proc.poll() is not None:
                raise RuntimeError(f'{self.directory}: server exited {self.proc.returncode}')
            try:
                self.query('select 1', database=None)
                elapsed = time.monotonic() - started
                emit(phase='ready', role=self.role, pid=self.proc.pid,
                     directory=self.directory, seconds=elapsed)
                return elapsed
            except pymysql.Error:
                time.sleep(0.1)
        raise RuntimeError(f'{self.directory}: startup timeout')

    def stop(self, crash=False):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGKILL if crash else signal.SIGTERM)
            try:
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        time.sleep(0.1)

    def hashes(self):
        result = {}
        with self.connect() as conn:
            for table in ('sbtest1', 'sbtest2', 'sbtest3', 'sbtest4'):
                digest = hashlib.sha256()
                count = 0
                with conn.cursor(pymysql.cursors.SSCursor) as cur:
                    cur.execute(f'select id,k,c,pad from {table} order by id')
                    for row in cur:
                        digest.update(json.dumps(row, separators=(',', ':')).encode())
                        digest.update(b'\n')
                        count += 1
                result[table] = {'rows': count, 'sha256': digest.hexdigest()}
        return result

    def cache_state(self, label):
        slot = "'oceanbase::share::ObServerServiceSlot<oceanbase::storage::ObLSService>::service_'"
        replay_cache = f'{slot}->ls_->ls_tx_svr_.replay_tx_ctx_cache_'
        commands = [
            f'printf "REPLAY_CACHE=%p\\n", {replay_cache}',
            f'printf "REPLAY_FREE_COUNT=%ld\\n", {replay_cache} ? {replay_cache}->free_count_ : 0',
            f'printf "RECOVER_CACHE=%p\\n", {slot}->ls_->tx_table_.tx_ctx_table_.recover_helper_.tx_ctx_cache_',
            'printf "ACTIVE_TX_CTX=%ld\\n", *(long *)&\'oceanbase::transaction::ObTxCtxFactory::active_tx_ctx_count_\'',
            'detach']
        command = ['gdb', '-q', '-batch', '-iex', 'set print thread-events off',
                   '-p', str(self.proc.pid)]
        for item in commands:
            command.extend(['-ex', item])
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        text = result.stdout + result.stderr
        (self.directory / f'{label}.txt').write_text(text)
        for key in ('REPLAY_CACHE', 'REPLAY_FREE_COUNT', 'RECOVER_CACHE', 'ACTIVE_TX_CTX'):
            if key + '=' not in text:
                raise RuntimeError(f'gdb failed to read {key}: {text[-1000:]}')
        values = dict(re.findall(r'(REPLAY_CACHE|REPLAY_FREE_COUNT|RECOVER_CACHE|ACTIVE_TX_CTX)=([^\n]+)', text))
        emit(phase='cache_state', role=self.role, label=label, values=values)
        if values['RECOVER_CACHE'] not in ('(nil)', '0x0'):
            raise RuntimeError('completed recovery scan retained its cache')
        if not 0 <= int(values['REPLAY_FREE_COUNT']) <= 64:
            raise RuntimeError('replay cache exceeded its free-context bound')
        if self.role == 'PRIMARY' and values['REPLAY_CACHE'] not in ('(nil)', '0x0'):
            raise RuntimeError('primary retained its startup replay cache')
        if self.role == 'STANDBY' and int(values['ACTIVE_TX_CTX']) != 0:
            raise RuntimeError('caught-up standby retained an active transaction')
        # Standby catch-up cleanup will belong to a unified resource-release
        # action; this change intentionally retains only its bounded LS cache.
        return values

    def replay_markers(self):
        lines = []
        for file in (self.directory / 'log').glob('seekdb.log*'):
            with file.open(errors='replace') as log:
                for line in log:
                    if any(s in line for s in ('enable replay status success', 'disable local replay(',
                                              'close replay tx ctx cache', 'close recover tx ctx cache',
                                              'restore trans table in memory')):
                        lines.append(line.strip())
        return sorted(lines)


def copy_dir(source, target):
    target.mkdir(exist_ok=False)
    subprocess.run(['cp', '-a', '--reflink=auto', '--sparse=always',
                    str(source) + '/.', str(target)], check=True)
    if (target / 'log').exists():
        (target / 'log').rename(target / f'seed-log-{time.time_ns()}')


def checkpoint_seed(args):
    directory = args.root / 'checkpoint-seed'
    copy_dir(args.snapshot, directory)
    primary = Node(args.baseline, directory, 45551, 45552)
    connections = []
    try:
        primary.start()
        hashes = primary.hashes()
        (args.root / 'expected-hashes.json').write_text(json.dumps(hashes, indent=2))
        primary.query('create table durability_check (id int primary key, v int, payload varchar(512))')
        with primary.connect() as conn:
            with conn.cursor() as cur:
                cur.executemany('insert into durability_check values (%s,%s,%s)',
                                [(i, i * 7, 'committed') for i in range(128)])
        for worker in range(4):
            conn = primary.connect()
            connections.append(conn)
            with conn.cursor() as cur:
                cur.execute('set ob_trx_timeout=1000000000')
                cur.execute('begin')
                cur.executemany('insert into durability_check values (%s,%s,%s)',
                                [(10000 + worker * 2000 + i, i, 'x' * 400) for i in range(2000)])
        primary.query("alter system set _advance_checkpoint_interval='1s'", database=None)
        primary.query('alter system minor freeze', database=None)
        deadline = time.monotonic() + 90
        found = []
        while time.monotonic() < deadline:
            with primary.connect('oceanbase') as conn:
                with conn.cursor(pymysql.cursors.DictCursor) as cur:
                    cur.execute('select * from v$ob_sstables where tablet_id=49401')
                    rows = cur.fetchall()
                    found = []
                    for row in rows:
                        lowered = {key.lower(): value for key, value in row.items()}
                        if lowered.get('table_type') in ('MINOR', 'MINI') and int(lowered.get('size', 0)) > 0:
                            found.append(row)
            if found:
                break
            time.sleep(0.5)
        emit(phase='checkpoint_seed', tx_ctx_sstables=found, populated=bool(found))
        if not found:
            raise RuntimeError('nonempty tx-context SSTable was not produced')
        expected = primary.query('select count(*),sum(v) from durability_check')
        if expected != ((128, 56896),):
            raise RuntimeError(f'committed rows mismatch before crash: {expected}')
        primary.stop(crash=True)
        copy_dir(directory, args.root / 'checkpoint-snapshot')
        return hashes
    finally:
        primary.stop(crash=True)
        for conn in connections:
            try:
                conn.close()
            except pymysql.Error:
                pass


def restart(args):
    if args.resume:
        reference_dir = args.root / f'hash-reference-{time.time_ns()}'
        copy_dir(args.snapshot, reference_dir)
        reference = Node(args.baseline, reference_dir, 45551, 45552)
        try:
            reference.start()
            hashes = reference.hashes()
            (args.root / 'expected-hashes.json').write_text(json.dumps(hashes, indent=2))
        finally:
            reference.stop()
    else:
        hashes = checkpoint_seed(args)
    result = []
    for i, (variant, binary) in enumerate((('master', args.baseline), ('cache', args.candidate))):
        directory = args.root / f'checkpoint-restart-{variant}'
        copy_dir(args.root / 'checkpoint-snapshot', directory)
        node = Node(binary, directory, 45551, 45552)
        try:
            ready = node.start()
            if node.hashes() != hashes:
                raise RuntimeError('sysbench data SHA-256 mismatch after crash recovery')
            rows = node.query('select count(*),sum(v) from durability_check')
            if rows != ((128, 56896),):
                raise RuntimeError(f'crash recovery durability mismatch: {rows}')
            markers = node.replay_markers()
            if variant == 'cache' and not any('close recover tx ctx cache' in line for line in markers):
                raise RuntimeError('candidate did not restore transaction contexts from the SSTable')
            node.stop()
            clean_ready = node.start()
            if node.hashes() != hashes or node.query('select count(*),sum(v) from durability_check') != rows:
                raise RuntimeError('clean restart durability mismatch')
            time.sleep(2)
            state = node.cache_state('after-clean-restart') if variant == 'cache' else None
            result.append({'variant': variant, 'crash_ready_seconds': ready,
                           'clean_ready_seconds': clean_ready, 'hashes': hashes,
                           'committed_rows': rows, 'markers': markers, 'idle_state': state})
            (args.root / 'restart-results.json').write_text(json.dumps(result, indent=2, default=str))
        finally:
            node.stop()
    emit(phase='restart_passed')


def write_batch(primary, iterations):
    def write(worker):
        with primary.connect() as conn:
            with conn.cursor() as cur:
                for _ in range(iterations):
                    cur.execute('begin')
                    cur.execute('update live_guard set v=v+1 where id=%s', (worker,))
                    cur.execute(f'update sbtest{worker % 4 + 1} set k=k+1 where id=%s', (worker + 1,))
                    cur.execute('commit')
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(write, range(8)))


def sync(primary, standby):
    target = int(primary.query('select sync_scn from oceanbase.__all_virtual_server_stat limit 1')[0][0])
    begin = time.monotonic()
    while time.monotonic() - begin < 180:
        rows = standby.query('select sync_scn,readable_scn from oceanbase.__all_virtual_server_stat limit 1')
        if int(rows[0][0]) >= target and int(rows[0][1]) >= target:
            if primary.hashes() != standby.hashes():
                raise RuntimeError('primary/standby sysbench data SHA-256 mismatch')
            if primary.query('select * from live_guard order by id') != standby.query('select * from live_guard order by id'):
                raise RuntimeError('acknowledged transactions differ between primary and standby')
            emit(phase='standby_synced', target_scn=target, seconds=time.monotonic()-begin)
            return time.monotonic() - begin
        time.sleep(0.1)
    raise RuntimeError('standby catch-up timeout')


def standby_test(args):
    results = []
    for variant, binary in (('master', args.baseline), ('cache', args.candidate)):
        directory = args.root / f'standby-{variant}'
        directory.mkdir()
        copy_dir(args.snapshot, directory / 'primary')
        primary = Node(binary, directory / 'primary', 45551, 45552)
        standby = Node(binary, directory / 'standby', 45561, 45562, 'STANDBY', '127.0.0.1:45552')
        try:
            primary.start()
            primary.query('create table live_guard(id int primary key, v int)')
            primary.query('insert into live_guard values(0,0),(1,0),(2,0),(3,0),(4,0),(5,0),(6,0),(7,0)')
            primary.query("alter system set ob_compaction_schedule_interval='3s'", database=None)
            primary.query('alter system minor freeze', database=None)
            primary.query('alter system major freeze', database=None)
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                rows = primary.query('select count(*) from oceanbase.DBA_OB_MAJOR_COMPACTION '
                                     'where LAST_SCN != GLOBAL_BROADCAST_SCN')
                if rows == ((0,),):
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError('primary major compaction did not settle before standby bootstrap')
            time.sleep(2)
            standby.start()
            sync(primary, standby)
            write_batch(primary, 250)
            live_sync = sync(primary, standby)
            # Online tablet creation and schema DDL exercise replay barriers
            # after bootstrap while reusing the bounded standby cache.
            primary.query('create table barrier_guard(id int primary key, v int)')
            primary.query('insert into barrier_guard values(1,123)')
            primary.query('alter table live_guard add column barrier_value bigint not null default 0')
            sync(primary, standby)
            if standby.query('select * from barrier_guard') != ((1, 123),):
                raise RuntimeError('online DDL/barrier replay did not preserve data')
            time.sleep(2)
            state1 = standby.cache_state('after-streaming') if variant == 'cache' else None
            standby.stop(crash=True)
            write_batch(primary, 500)
            restart_ready = standby.start()
            catchup = sync(primary, standby)
            write_batch(primary, 250)
            sync(primary, standby)
            primary.stop(crash=True)
            primary.start()
            sync(primary, standby)
            write_batch(primary, 125)
            sync(primary, standby)
            time.sleep(2)
            state2 = standby.cache_state('after-primary-and-standby-restarts') if variant == 'cache' else None
            rows = primary.query('select sum(v) from live_guard')
            if rows != ((9000,),):
                raise RuntimeError(f'acknowledged transaction count mismatch: {rows}')
            results.append({'variant': variant, 'stream_sync_seconds': live_sync,
                            'standby_restart_ready_seconds': restart_ready,
                            'standby_restart_catchup_seconds': catchup,
                            'committed_transactions': 9000,
                            'idle_states': [state1, state2]})
            (args.root / 'standby-results.json').write_text(json.dumps(results, indent=2, default=str))
        finally:
            standby.stop()
            primary.stop()
    emit(phase='standby_passed')


def inspect_checkpoint(args):
    results = []
    for variant, binary in (('master', args.baseline), ('cache', args.candidate)):
        node = Node(binary, args.root / f'checkpoint-restart-{variant}', 45551, 45552)
        try:
            node.start()
            time.sleep(2)
            command = ['gdb', '-q', '-batch', '-iex', 'set print thread-events off',
                       '-p', str(node.proc.pid), '-ex',
                       'printf "ACTIVE_TX_CTX=%ld\\n", *(long *)&\'oceanbase::transaction::ObTxCtxFactory::active_tx_ctx_count_\'',
                       '-ex', 'detach']
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            text = result.stdout + result.stderr
            match = re.search(r'ACTIVE_TX_CTX=(\d+)', text)
            if match is None:
                raise RuntimeError('cannot inspect recovered context count')
            count = int(match.group(1))
            (args.root / f'checkpoint-active-{variant}.txt').write_text(text)
            results.append({'variant': variant, 'active_contexts': count})
        finally:
            node.stop()
    if results[0]['active_contexts'] != results[1]['active_contexts']:
        raise RuntimeError('recovered active context count differs from master')
    (args.root / 'checkpoint-active-contexts.json').write_text(json.dumps(results, indent=2))
    emit(phase='checkpoint_context_counts', results=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['restart', 'standby', 'inspect'])
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--resume', action='store_true', help='reuse this root\'s completed crash snapshot')
    args = parser.parse_args()
    args.root.mkdir(parents=True, exist_ok=True)
    {'restart': restart, 'standby': standby_test, 'inspect': inspect_checkpoint}[args.phase](args)
