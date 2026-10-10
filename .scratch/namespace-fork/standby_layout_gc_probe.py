#!/usr/bin/env python3
"""Real replication with different local layout roots, local minor GC and promotion."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

import pymysql

local = Path(__file__).resolve().parent
work = Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work')


def record(event, **values):
    print(json.dumps(dict(event=event, **values)), flush=True)


def sql(connection, text):
    with connection.cursor() as cursor:
        cursor.execute(text)
        return cursor.fetchall()


def wait(fn, seconds=180):
    until = time.monotonic() + seconds
    last = None
    while time.monotonic() < until:
        try:
            last = fn()
            if last:
                return last
        except pymysql.MySQLError as error:
            last = error.args
        time.sleep(.3)
    raise AssertionError(('timeout', last))


class Node:
    def __init__(self, name, port, rpc, role, controls):
        self.name, self.port, self.rpc, self.role = name, port, rpc, role
        self.controls = controls
        self.sequence = 0
        self.physical = self.layout = self.version = 0
        self.connection = None
        self.process = None
        self.base = work / name

    def pid(self):
        matches = []
        for proc in Path('/proc').iterdir():
            if not proc.name.isdigit():
                continue
            try:
                if proc.joinpath('exe').resolve().parent == self.base / 'bin':
                    matches.append(int(proc.name))
            except OSError:
                pass
        assert len(matches) == 1, (self.name, matches)
        return matches[0]

    def kill(self):
        if self.connection is not None:
            self.connection.close()
            self.connection = None
        pid = self.pid()
        os.kill(pid, signal.SIGKILL)
        if self.process is not None and self.process.pid == pid:
            self.process.wait(timeout=15)
        else:
            # Losing /proc/PID/exe precedes complete thread/process teardown.
            # start_daemon can still see that process's pidfile lock meanwhile.
            wait(lambda: not Path(f'/proc/{pid}').exists(), 15)
        record('node_killed', node=self.name, pid=pid)

    def start(self):
        env = os.environ.copy()
        env['SEEKDB_LAYOUT_REFERENCE_GC_CONTROL_DIR'] = str(self.controls)
        env['LD_LIBRARY_PATH'] = str(self.base / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
        command = [str(self.base / 'bin/observer'), '--nodaemon', '--port', str(self.port),
                   '--data-dir', str(self.base / 'store'), '--redo-dir', str(self.base / 'store/clog'),
                   '--role=' + self.role]
        options = ['enable_rpc_service=true', f'rpc_port={self.rpc}', 'datafile_size=2G',
                   'log_disk_size=2G', 'cpu_count=4', 'memory_limit=8G', 'minor_compact_trigger=2',
                   'ob_compaction_schedule_interval=3s']
        if self.role == 'STANDBY':
            options.append('log_restore_source=6.12.232.130:42000')
        for option in options:
            command.extend(('--parameter', option))
        with (self.base / 'layout-gc-restart.stdout').open('ab') as output:
            self.process = subprocess.Popen(command, cwd=self.base, env=env, stdout=output,
                                            stderr=subprocess.STDOUT, start_new_session=True)
        record('node_started', node=self.name, pid=self.process.pid, role=self.role)

    def connect(self):
        def ready():
            if self.process is not None and self.process.poll() is not None:
                raise RuntimeError((self.name, 'startup exited', self.process.returncode))
            c = pymysql.connect(host='127.0.0.1', port=self.port, user='root@sys', database='oceanbase',
                                autocommit=True, connect_timeout=2, read_timeout=330)
            sql(c, 'SET ob_query_timeout=300000000')
            sql(c, 'SET ob_trx_timeout=300000000')
            return c
        self.connection = wait(ready)

    def command(self, action, orphan=0, retryable=()):
        self.sequence += 1
        pid = self.pid()
        control = self.controls / str(pid)
        pending = self.controls / (str(pid) + '.pending')
        response = self.controls / (str(pid) + '.result')
        pending.write_text(f'{self.sequence} {action} {self.physical} {self.layout} {self.version} {orphan}\n')
        pending.replace(control)
        until = time.monotonic() + 180
        while time.monotonic() < until:
            assert Path(f'/proc/{pid}/exe').exists(), (self.name, 'process exited', action)
            if response.exists():
                line = response.read_text()
                if line.endswith('\n'):
                    result = dict((k, int(v)) for k, v in (field.split('=') for field in line.split()))
                    if result.get('seq') == self.sequence:
                        record('layout_gc_command', node=self.name, action=action, **result)
                        assert result['ret'] == 0 or result['ret'] in retryable, (self.name, action, result)
                        return result
            time.sleep(.1)
        raise TimeoutError((self.name, action, str(control)))

    def major(self):
        def completed():
            values = tuple(map(int, sql(self.connection,
                'SELECT frozen_scn,global_broadcast_scn,last_scn FROM oceanbase.DBA_OB_MAJOR_COMPACTION')[0]))
            return values[0] if values[0] == values[1] == values[2] else None
        before = wait(completed, 360)
        sql(self.connection, 'ALTER SYSTEM MAJOR FREEZE')
        assert not sql(self.connection, 'SHOW WARNINGS')
        def newer():
            value = completed()
            return value if value is not None and value > before else None
        target = wait(newer, 360)
        record('layout_gc_major', node=self.name, target=target)
        return target


def run():
    with tempfile.TemporaryDirectory(prefix='seekdb-standby-layout-gc-') as directory:
        controls = Path(directory)
        primary = Node('db_p.z1.obs0', 42035, 42000, 'PRIMARY', controls)
        replica = Node('db_s.z1.obs0', 42036, 42001, 'STANDBY', controls)
        try:
            # The obtest fixture owns these exact directories. Restart both with
            # explicit per-process controls; no production control interface.
            # Its setup script only sleeps after starting a fresh replica. Wait
            # for the actual bootstrap checkpoint before testing crash recovery.
            replica.connect()
            wait(lambda: sql(replica.connection, 'SELECT id,v FROM replica_ns.t') == ((1, 10),))
            pid = replica.pid()
            def checkpoint_committed():
                for path in (replica.base / 'log').glob('seekdb.log*'):
                    with path.open(errors='replace') as stream:
                        for line in stream:
                            if 'bootstrap checkpoints committed' in line and f'[{pid}]' in line:
                                return True
                return False
            wait(checkpoint_committed)
            record('fixture_replica_bootstrap_complete', pid=pid)
            replica.kill()
            primary.kill()
            primary.start()
            replica.start()
            primary.connect()
            replica.connect()
            for node in (primary, replica):
                sql(node.connection, "ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")
                sql(node.connection, 'ALTER SYSTEM SET minor_compact_trigger=2')
                sql(node.connection, 'ALTER SYSTEM SET undo_retention=0')
                sql(node.connection, 'ALTER SYSTEM SET _mvcc_gc_using_min_txn_snapshot=false')
            sql(primary.connection, 'SET recyclebin=off')
            # This branch belongs to the setup fixture. Its cold materialization
            # is unrelated to the physical roots being checked here.
            sql(primary.connection, 'DROP NAMESPACE replica_seed')
            sql(primary.connection, 'CREATE DATABASE layout_gc')
            sql(primary.connection, 'CREATE TABLE layout_gc.t(id INT PRIMARY KEY,v INT)')
            sql(primary.connection, 'INSERT INTO layout_gc.t VALUES(1,10),(2,20)')
            sql(primary.connection, 'CREATE TABLE layout_gc.witness(id INT PRIMARY KEY,v INT)')
            sql(primary.connection, 'INSERT INTO layout_gc.witness VALUES(1,50)')
            logical = int(sql(primary.connection, "SELECT tablet_id FROM __all_table WHERE "
                "database_id=(SELECT database_id FROM __all_database WHERE database_name='layout_gc') "
                "AND table_name='t'")[0][0])
            primary.physical = replica.physical = (1 << 62) | (1 << 37) | logical
            target = primary.major()
            wait(lambda: sql(replica.connection, "SELECT tablet_id FROM V$OB_SSTABLES "
                f"WHERE tablet_id={replica.physical} AND table_type='MAJOR' AND end_log_scn={target}"), 360)
            assert sql(replica.connection, 'SELECT * FROM layout_gc.t ORDER BY id') == ((1,10),(2,20))
            described = primary.command('describe')
            primary.layout = replica.layout = described['G']
            primary.version = replica.version = described['V']
            held = replica.command('hold_external')
            assert held['G'] == primary.layout and held['V'] == primary.version, held
            assert held['a'] == held['b'] == 1 and held['old'] == held['current'] == 0, held
            sql(primary.connection, 'DROP TABLE layout_gc.t')

            def stage(name, expect_body, copies):
                sentinel = primary.command('orphan')['orphan_id']
                until = time.monotonic() + 360
                attempts = 0
                while time.monotonic() < until:
                    # Every new primary row must actually replay before flushing
                    # the replica. No replica-side metadata writes are performed.
                    fresh = primary.command('orphan')['orphan_id']
                    primary.command('advance', sentinel)
                    wait(lambda: replica.command('inspect', fresh)['orphan'] == 0)
                    p = primary.command('flush', sentinel)
                    s = replica.command('flush', sentinel)
                    attempts += 1
                    if (p['body'] == p['head'] == p['orphan'] == -4018 and p['mapped'] == 0
                            and s['orphan'] == -4018 and s['mapped'] == 0
                            and s['a'] + s['b'] == copies):
                        if copies == 0 and (s['retained'] != 0 or s['body'] == 0 or s['head'] == 0):
                            continue
                        assert s['body'] == s['head'] == expect_body, (name, p, s)
                        record('standby_gc_stage_pass', stage=name, primary=p, replica=s, attempts=attempts)
                        return
                raise AssertionError((name, 'local GC did not reach expected state', p, s))

            stage('primary_deleted_replica_retains_two', 0, 2)
            replica.command('release_a')
            stage('last_replica_external_copy', 0, 1)
            replica.command('release_b')
            stage('replica_last_reference_released', -4018, 0)
            primary.command('clear')
            replica.command('clear')
            replica.kill()
            replica.start()
            replica.connect()
            recovered = replica.command('inspect')
            assert recovered['body'] == recovered['head'] == -4018 and recovered['retained'] == 0, recovered
            assert sql(replica.connection, 'SELECT * FROM layout_gc.witness') == ((1,50),)
            record('standby_gc_crash_recovered', result=recovered)
            primary.kill()
            sql(replica.connection, 'ALTER SYSTEM ACTIVATE STANDBY')
            wait(lambda: sql(replica.connection, 'SELECT role FROM __all_virtual_server_stat') == (('PRIMARY',),))
            sql(replica.connection, 'ALTER TABLE layout_gc.witness ADD COLUMN extra INT DEFAULT 9')
            sql(replica.connection, 'UPDATE layout_gc.witness SET v=60')
            assert sql(replica.connection, 'SELECT * FROM layout_gc.witness') == ((1,60,9),)
            final_target = replica.major()
            final = replica.command('inspect')
            assert final['body'] == final['head'] == -4018 and final['retained'] == 0, final
            replica.command('clear')
            record('PASS', case='standby_layout_gc', different_local_roots=True,
                   actual_replica_minor=True, last_reference_release=True,
                   crash_recovery=True, promotion=True, promoted_ddl_major=final_target)
        finally:
            for node in (primary, replica):
                if node.connection is not None:
                    node.connection.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    run()
