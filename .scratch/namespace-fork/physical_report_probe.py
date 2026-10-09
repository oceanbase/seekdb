#!/usr/bin/env python3
"""A real freeze cannot finish using missing or obsolete persisted results."""
import argparse
import os
import sqlite3
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True)
a = p.parse_args()
exp = BootstrapExperiment(a.binary, 'physical_report', prototype=6)
gate = exp.base / 'round.pause'
hold = exp.base / 'report.hold'
os.environ['SEEKDB_PHYSICAL_REPORT_GATE'] = str(gate)
os.environ['SEEKDB_PHYSICAL_REPORT_HOLD'] = str(hold)
child = None


def wait_for(fn, timeout=90):
    deadline = time.monotonic() + timeout
    value = None
    while time.monotonic() < deadline:
        assert exp.proc.poll() is None, 'instance exited'
        value = fn()
        if value:
            return value
        time.sleep(.2)
    raise AssertionError(('timeout', value, str(exp.base)))


def progress():
    return exp.sql('select frozen_scn,global_broadcast_scn,last_scn '
                   'from oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]


def attempts(frozen):
    count = 0
    for path in (exp.base / 'log').glob('seekdb.log*'):
        if '.wf' not in path.name:
            count += path.read_bytes().count(f'PHYSICAL_ROUND_CHECK F={frozen}\n'.encode())
    return count


try:
    exp.start()
    exp.connection._read_timeout = 330
    exp.sql('set ob_query_timeout=300000000')
    exp.sql('set ob_trx_timeout=300000000')
    exp.sql("alter system set internal_sql_execute_timeout='300s'")
    exp.sql('create database report_probe')
    exp.sql('create table report_probe.t(id int primary key,v int)')
    exp.sql('insert into report_probe.t values(1,10),(2,20)')
    logical = int(exp.sql("select tablet_id from oceanbase.__all_table where table_name='t'", log=False)[0][0])
    exp.sql('fork namespace report_child from ns1')
    child = connect(exp, 'root@report_child')
    child_id = namespace_id(exp, 'report_child')
    ids = {'initial': physical_id(1, logical), 'child': physical_id(child_id, logical)}
    exp.sql("alter system set ob_compaction_schedule_interval='3s'")
    # Each corruption is applied before the first logical check of a fresh F.
    cases = [('child', '__all_tablet_meta_table', None),
             ('child', '__all_tablet_meta_table', 'create_transaction_id'),
             ('child', '__all_tablet_meta_table', 'physical_create_version'),
             ('child', '__all_tablet_meta_table', 'storage_layout_id'),
             ('initial', '__all_tablet_local_checksum', None),
             ('initial', '__all_tablet_local_checksum', 'create_transaction_id'),
             ('initial', '__all_tablet_local_checksum', 'storage_layout_id')]
    for owner, table, field in cases:
        gate.write_text('pause before checking\n')
        previous = progress()[0]
        exp.sql('update report_probe.t set v=v+1')
        exp.sql('update report_probe.t set v=v+1', child)
        exp.sql('alter system minor freeze')
        started = time.monotonic()
        exp.sql('alter system major freeze')
        exp.record('freeze_prepared', elapsed_s=round(time.monotonic() - started, 3))
        frozen = wait_for(lambda: (lambda r: r[0] if r[0] > previous and r[1] == r[0] else None)(progress()))
        # All physical work must already be complete: otherwise an unrelated
        # merge or a held report batch could explain the observed wait.
        wait_for(lambda: exp.sql(
            'select count(*) from oceanbase.__all_virtual_tablet_info t '
            'left join oceanbase.__all_virtual_tablet_meta_table r on r.tablet_id=t.tablet_id '
            f'where t.tablet_id>{1 << 62} and t.tablet_status=1 and t.is_empty_shell=0 '
            f'and (r.tablet_id is null or r.compaction_scn<{frozen})', log=False)[0][0] == 0)
        tablet = ids[owner]
        wait_for(lambda: exp.sql('select tablet_id from oceanbase.__all_virtual_tablet_meta_table '
                 f'where tablet_id={tablet} and compaction_scn={frozen}', log=False))
        wait_for(lambda: exp.sql('select tablet_id from oceanbase.__all_virtual_tablet_local_checksum '
                 f'where tablet_id={tablet} and compaction_scn={frozen}', log=False))
        identity = exp.sql('select create_transaction_id,physical_create_version,storage_layout_id '
                           'from oceanbase.__all_virtual_tablet_meta_table '
                           f'where tablet_id={tablet}', log=False)[0]
        checksum = exp.sql('select create_transaction_id,storage_layout_id,schema_version '
                           'from oceanbase.__all_virtual_tablet_local_checksum '
                           f'where tablet_id={tablet}', log=False)[0]
        assert identity[0] == checksum[0] and identity[2] == checksum[1], (identity, checksum)
        assert 0 < identity[1] <= frozen and checksum[2] >= 0, (identity, checksum)
        hold.write_text(str(tablet))
        with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
            db.execute('begin immediate')
            saved = db.execute(f'select * from {table} where tablet_id=?', (tablet,)).fetchone()
            assert saved
            if field:
                db.execute(f'update {table} set {field}={field}+1 where tablet_id=?', (tablet,))
            else:
                db.execute(f'delete from {table} where tablet_id=?', (tablet,))
        gate.unlink()
        wait_for(lambda: attempts(frozen) >= 2)
        # Observe several completed scheduler opportunities, not just a short
        # interval before the first traversal could have started.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            assert progress()[2] < frozen, (owner, table, field, progress())
            time.sleep(.2)
        gate.write_text('restore before next round\n')
        with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
            db.execute('begin immediate')
            db.execute(f'insert or replace into {table} values({",".join("?" for _ in saved)})', saved)
        hold.unlink()
        gate.unlink()
        wait_for(lambda: progress()[2] == frozen)
        exp.record('PASS', owner=owner, table=table, fault=field or 'missing_row',
                   frozen=frozen, identity=identity, checksum=checksum)
    expected_initial = exp.sql('select * from report_probe.t order by id', log=False)
    expected_child = exp.sql('select * from report_probe.t order by id', child, log=False)
    assert expected_initial == expected_child == ((1, 17), (2, 27))
    before_restart = {owner: exp.sql(
        'select create_transaction_id,physical_create_version,storage_layout_id '
        f'from oceanbase.__all_virtual_tablet_meta_table where tablet_id={tablet}', log=False)[0]
        for owner, tablet in ids.items()}
    child.close()
    child = None
    exp.connection.close()
    exp.connection = None
    exp.proc.kill()
    exp.proc.wait(timeout=15)
    exp.start()
    child = connect(exp, 'root@report_child')
    assert exp.sql('select * from report_probe.t order by id', log=False) == expected_initial
    assert exp.sql('select * from report_probe.t order by id', child, log=False) == expected_child
    previous = progress()[0]
    exp.sql('alter system major freeze')
    wait_for(lambda: (lambda r: r[0] > previous and r[0] == r[1] == r[2])(progress()))
    for owner, tablet in ids.items():
        identity = exp.sql('select create_transaction_id,physical_create_version,storage_layout_id '
            f'from oceanbase.__all_virtual_tablet_meta_table where tablet_id={tablet}', log=False)[0]
        assert identity == before_restart[owner], (owner, identity, before_restart[owner])
    exp.record('PASS', case='physical_report_identity_and_completeness', recovery=True)
finally:
    if child is not None:
        child.close()
    gate.unlink(missing_ok=True)
    hold.unlink(missing_ok=True)
    exp.close()
