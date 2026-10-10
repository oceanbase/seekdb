#!/usr/bin/env python3
"""Real layout publication, standby major, crash recovery and promoted DDL/major."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

import pymysql

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True)
a = p.parse_args()
local = Path(__file__).resolve().parent
work = Path('/data/1/nijia.nj/test/namespace_standby_20261003_v1/work')


def connect(port, namespace='sys'):
    c = pymysql.connect(host='127.0.0.1', port=port, user='root@' + namespace,
                        database='oceanbase', autocommit=True, connect_timeout=3,
                        read_timeout=330, write_timeout=30)
    sql(c, 'set ob_query_timeout=300000000')
    sql(c, 'set ob_trx_timeout=300000000')
    return c


def sql(c, query):
    with c.cursor() as cur:
        cur.execute(query)
        return cur.fetchall()


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


def record(event, **facts):
    print(json.dumps(dict(event=event, **facts)), flush=True)


def keep(stack, connection):
    # Crash/restart deliberately closes the old connection before scope exit.
    stack.callback(lambda: connection.close() if connection.open else None)
    return connection


def owned_pid(node):
    base = work / node / 'bin'
    found = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.joinpath('exe').resolve().parent == base:
                found.append(int(proc.name))
        except OSError:
            pass
    assert len(found) == 1, (node, found)
    return found[0]


def verify(c, child, promoted=False):
    expected = ((1, 110), (2, 120), (120, 130)) if child else ((1, 10), (2, 20), (120, 30))
    if promoted and child:
        expected = ((1, 110), (2, 120), (3, 140), (120, 130))
    for index in ('PRIMARY', 'by_local_v', 'by_global_v'):
        assert sql(c, f'select id,v from layout_replay.t force index({index}) order by id') == expected
    column, default = ('child_only', 9) if child else ('parent_only', 7)
    assert sql(c, f'select distinct {column} from layout_replay.t') == ((default,),)
    if promoted and child:
        assert sql(c, 'select distinct promoted_only from layout_replay.t') == ((13,),)


def physical_ids(c, owner):
    tables = [int(row[0]) for row in sql(c, "select table_id from __all_table where "
        "database_id=(select database_id from __all_database where database_name='layout_replay')")]
    ids = ','.join(map(str, tables))
    tablets = {int(row[0]) for row in sql(c, f'select tablet_id from __all_table where table_id in ({ids}) and tablet_id>0')}
    tablets.update(int(row[0]) for row in sql(c, f'select tablet_id from __all_part where table_id in ({ids}) and tablet_id>0'))
    assert len(tablets) >= 6, tablets
    return {(1 << 62) | (owner << 37) | tablet for tablet in tablets}


def freeze(c):
    def completed_target():
        row = tuple(map(int, sql(c, 'select frozen_scn,global_broadcast_scn,last_scn '
                                'from DBA_OB_MAJOR_COMPACTION')[0]))
        return row[0] if row[0] == row[1] == row[2] else None
    # Recovery/promotion can finish the physical SSTables before the previous
    # round's logical checksum and durable completion. A new major request in
    # that interval returns OK with a warning and does not publish a new F.
    before = wait(completed_target)
    sql(c, 'alter system minor freeze')
    sql(c, 'alter system major freeze')
    warnings = sql(c, 'show warnings')
    assert not warnings, ('major freeze was not accepted', warnings)
    record('major_requested', port=c.port, previous=before)
    def done():
        target = completed_target()
        return target if target is not None and target > before else None
    frozen = wait(done)
    record('major_completed', port=c.port, frozen=frozen)
    return frozen


def physical_complete(c, tablets, frozen):
    rows = sql(c, 'select tablet_id from V$OB_SSTABLES where table_type in (\'MAJOR\',\'CO_MAJOR\') '
               f'and end_log_scn={frozen} and tablet_id in ({",".join(map(str, tablets))})')
    return {int(row[0]) for row in rows} == tablets


with contextlib.ExitStack() as stack:
    root = keep(stack, wait(lambda: connect(42035)))
    standby = keep(stack, wait(lambda: connect(42036)))
    # The shared setup fixture creates this unrelated branch. Retire it before
    # measuring the two owners under test; the DROP also exercises KV replay.
    sql(root, 'drop namespace replica_seed')
    sql(root, "alter system set ob_compaction_schedule_interval='3s'")
    sql(root, "alter system set internal_sql_execute_timeout='300s'")
    sql(root, 'create database layout_replay')
    sql(root, 'create table layout_replay.t(id int primary key,v int) '
        'partition by range(id) (partition p0 values less than(100),partition p1 values less than(200))')
    sql(root, 'create index by_local_v on layout_replay.t(v) local')
    sql(root, 'create unique index by_global_v on layout_replay.t(v) global partition by hash(v) partitions 2')
    sql(root, 'insert into layout_replay.t values(1,10),(2,20),(120,30)')
    sql(root, 'fork namespace layout_replay_child from ns1')
    child = keep(stack, connect(42035, 'layout_replay_child'))
    sql(root, 'alter table layout_replay.t add column parent_only int default 7')
    sql(child, 'alter table layout_replay.t add column child_only int default 9')
    sql(child, 'update layout_replay.t set v=v+100')
    owner = next(int(json.loads(k)['namespace_id']) for k, v in sql(root,
        'select key_json,value_json from __all_virtual_instance_metadata where collection_id=1')
        if json.loads(v)['name'] == 'layout_replay_child')
    expected_tablets = physical_ids(root, 1) | physical_ids(child, owner)
    frozen = freeze(root)
    wait(lambda: physical_complete(standby, expected_tablets, frozen))
    with connect(42036, 'layout_replay_child') as replica_child:
        verify(replica_child, True)
    verify(standby, False)
    record('standby_layout_major', frozen=frozen, tablets=len(expected_tablets), namespace=owner)

    # Crash the replica after its own physical results exist, then reopen it
    # before promotion. The source remains available while it catches up.
    standby.close()
    pid = owned_pid('db_s.z1.obs0')
    os.kill(pid, signal.SIGKILL)
    wait(lambda: not Path(f'/proc/{pid}/exe').exists(), 15)
    subprocess.run(['python3', str(local / 'standby_restart_probe.py'),
                    'layout-history', '--binary', a.binary], check=True)
    standby = keep(stack, wait(lambda: connect(42036)))
    wait(lambda: physical_complete(standby, expected_tablets, frozen))
    with connect(42036, 'layout_replay_child') as replica_child:
        verify(replica_child, True)
    verify(standby, False)
    record('standby_layout_crash_recovered', frozen=frozen)

    os.kill(owned_pid('db_p.z1.obs0'), signal.SIGKILL)
    sql(standby, 'alter system activate standby')
    wait(lambda: sql(standby, 'select role from __all_virtual_server_stat') == (('PRIMARY',),))
    with connect(42036, 'layout_replay_child') as promoted:
        sql(promoted, 'alter table layout_replay.t add column promoted_only int default 13')
        sql(promoted, 'insert into layout_replay.t(id,v) values(3,140)')
        verify(promoted, True, True)
    next_frozen = freeze(standby)
    assert next_frozen > frozen
    wait(lambda: physical_complete(standby, expected_tablets, next_frozen))
    verified = set()
    for path in (work / 'db_s.z1.obs0/log').glob('seekdb.log*'):
        with path.open('rb') as log:
            for line in log:
                if b'historical index checksum verified' not in line or str(next_frozen).encode() not in line:
                    continue
                for ns in (1, owner):
                    if f'namespace_id:{ns},'.encode() in line:
                        verified.add(ns)
    assert verified == {1, owner}, (verified, next_frozen)
    verify(standby, False)
    with connect(42036, 'layout_replay_child') as promoted:
        verify(promoted, True, True)
record('PASS', case='standby_schema_history', before=frozen, after=next_frozen,
       physical_tablets=len(expected_tablets), independent_ddl=True,
       standby_major=True, crash_recovery=True, promoted_ddl=True,
       promoted_major=True, historical_checksums=sorted(verified))
