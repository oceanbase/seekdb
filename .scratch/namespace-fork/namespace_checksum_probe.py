#!/usr/bin/env python3
"""Historical main/index validation in every owner; optional real report faults."""
import argparse
import os
import sqlite3
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True)
p.add_argument('--fts', action='store_true', help='include fulltext auxiliary comparisons')
p.add_argument('--native', action='store_true', help='binary with physical_report_injection enabled')
a = p.parse_args()
exp = BootstrapExperiment(a.binary, 'namespace_checksum', prototype=6)
gate = exp.base / 'round.pause'
hold = exp.base / 'report.hold'
if a.native:
    os.environ['SEEKDB_PHYSICAL_REPORT_GATE'] = str(gate)
    os.environ['SEEKDB_PHYSICAL_REPORT_HOLD'] = str(hold)
child = None


def wait_for(fn, timeout=180):
    until = time.monotonic() + timeout
    result = None
    while time.monotonic() < until:
        assert exp.proc.poll() is None, ('instance exited', exp.base)
        result = fn()
        if result:
            return result
        time.sleep(.3)
    raise AssertionError(('timeout', result, str(exp.base)))


def progress():
    return exp.sql('select frozen_scn,global_broadcast_scn,last_scn,status '
                   'from oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]


def merge_failed():
    return exp.sql('select is_merge_error from oceanbase.__all_virtual_merge_info', log=False)[0][0] == 1


def logs():
    return b'\n'.join(path.read_bytes() for path in (exp.base / 'log').glob('seekdb.log*')
                      if '.wf' not in path.name)


def attempts(frozen):
    marker = f'PHYSICAL_ROUND_CHECK F={frozen}\n'.encode()
    return sum(path.read_bytes().count(marker) for path in (exp.base / 'log').glob('seekdb.log*')
               if '.wf' not in path.name)


def freeze():
    previous = progress()[0]
    exp.sql('update checksum_probe.t set v=v+1')
    exp.sql('update checksum_probe.t set v=v+2', child)
    exp.sql('alter system minor freeze')
    started = time.monotonic()
    exp.sql('alter system major freeze')
    frozen = wait_for(lambda: (lambda r: r[0] if r[0] > previous and r[1] == r[0] else None)(progress()))
    exp.record('freeze_prepared', frozen=frozen, elapsed_s=round(time.monotonic()-started, 3))
    return frozen


try:
    exp.start()
    exp.connection._read_timeout = 330
    exp.sql('set ob_query_timeout=300000000')
    exp.sql('set ob_trx_timeout=300000000')
    exp.sql("alter system set internal_sql_execute_timeout='300s'")
    exp.sql("alter system set ob_compaction_schedule_interval='3s'")
    exp.sql('create database checksum_probe')
    exp.sql('create table checksum_probe.t(id int primary key,v int) '
            'partition by range(id) (partition p0 values less than(100),partition p1 values less than(200))')
    exp.sql('create index by_v on checksum_probe.t(v) local')
    exp.sql('create unique index by_global_v on checksum_probe.t(v) global partition by hash(v) partitions 2')
    exp.sql('insert into checksum_probe.t values(1,10),(2,20),(120,30)')
    if a.fts:
        exp.sql('create table checksum_probe.docs(id int primary key,body text)')
        exp.sql("insert into checksum_probe.docs values(1,'alpha text'),(2,'beta text')")
        exp.sql('create fulltext index body_ft on checksum_probe.docs(body)')
    exp.sql('fork namespace checksum_child from ns1')
    child = connect(exp, 'root@checksum_child')
    child._read_timeout = 330
    exp.sql('set ob_query_timeout=300000000', child)
    exp.sql('set ob_trx_timeout=300000000', child)
    child_id = namespace_id(exp, 'checksum_child')
    # A shared SQL table_id now denotes different layouts and different data.
    exp.sql('alter table checksum_probe.t add column parent_only int default 7')
    exp.sql('alter table checksum_probe.t add column child_only varchar(20) default \'child\'', child)
    exp.sql('update checksum_probe.t set v=v+100', child)
    before_root = exp.sql('select schema_version from oceanbase.__all_table '
                          "where table_name='t'", log=False)[0][0]
    before_child = exp.sql('select schema_version from oceanbase.__all_table '
                           "where table_name='t'", child, log=False)[0][0]
    assert before_root != before_child, (before_root, before_child)
    if a.native:
        gate.write_text('pause historical validation\n')
    frozen = freeze()
    if a.native:
        # F remains fixed while the runtime catalog advances to another V.
        exp.sql("alter table checksum_probe.t comment='after freeze'", child)
        exp.sql('create index after_freeze on checksum_probe.t(child_only) local', child)
        gate.unlink()
    wait_for(lambda: progress()[2] == frozen)
    for connection in (None, child):
        expected = exp.sql('select id,v from checksum_probe.t order by id', connection, log=False)
        for index in ('by_v', 'by_global_v'):
            assert exp.sql(f'select id,v from checksum_probe.t force index({index}) order by id',
                           connection, log=False) == expected
    proof = logs()
    assert b'historical index checksum verified' in proof, str(exp.base)
    if a.fts:
        for owner in (1, child_id):
            assert any(b'historical index checksum verified' in line and b'fts=true' in line
                       and f'namespace_id:{owner},'.encode() in line for line in proof.splitlines()), (owner, str(exp.base))
        for connection in (None, child):
            assert exp.sql("select id from checksum_probe.docs where match(body) against('alpha')",
                           connection, log=False) == ((1,),)
    assert f'namespace_id={child_id}'.encode() in proof or f'namespace_id:{child_id}'.encode() in proof
    exp.record('PASS', case='independent_namespace_local_global_indexes', frozen=frozen,
               root_version=before_root, child_version=before_child)

    if a.native:
        table_id = exp.sql("select table_id from oceanbase.__all_table where table_name='t'", child, log=False)[0][0]
        index_rows = exp.sql('select table_id from oceanbase.__all_table '
                            f"where data_table_id={table_id} and table_name like '%by_global_v'", child, log=False)
        assert len(index_rows) == 1, index_rows
        logical = exp.sql('select tablet_id from oceanbase.__all_part '
                          f'where table_id={index_rows[0][0]} order by part_id', child, log=False)[0][0]
        tablet = physical_id(child_id, logical)
        for fault in ('missing', 'schema_version', 'row_count'):
            gate.write_text('pause before fault\n')
            frozen = freeze()
            wait_for(lambda: exp.sql('select count(*) from oceanbase.__all_virtual_tablet_info t '
                'left join oceanbase.__all_virtual_tablet_meta_table r on r.tablet_id=t.tablet_id '
                f'where t.tablet_id>{1 << 62} and t.tablet_status=1 and t.is_empty_shell=0 '
                f'and (r.tablet_id is null or r.compaction_scn<{frozen})', log=False)[0][0] == 0)
            wait_for(lambda: exp.sql('select tablet_id from oceanbase.__all_virtual_tablet_local_checksum '
                f'where tablet_id={tablet} and compaction_scn={frozen}', log=False))
            hold.write_text(str(tablet))
            with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
                db.execute('begin immediate')
                saved = db.execute('select * from __all_tablet_local_checksum where tablet_id=?', (tablet,)).fetchone()
                assert saved
                if fault == 'missing':
                    db.execute('delete from __all_tablet_local_checksum where tablet_id=?', (tablet,))
                else:
                    db.execute(f'update __all_tablet_local_checksum set {fault}={fault}+1 where tablet_id=?', (tablet,))
            gate.unlink()
            if fault == 'row_count':
                wait_for(merge_failed)
            else:
                wait_for(lambda: attempts(frozen) >= 2)
            time.sleep(3)
            assert progress()[2] < frozen, (fault, progress())
            if fault == 'row_count':
                wait_for(merge_failed)
                diagnostic = exp.sql('select namespace_id,data_table_id,index_table_id from '
                    'oceanbase.__all_virtual_column_checksum_error_info '
                    f'where frozen_scn={frozen}', log=False)
                assert diagnostic and all(row[0] == child_id for row in diagnostic), diagnostic
                exp.record('checksum_error_diagnostic', rows=diagnostic)
            gate.write_text('restore inputs\n')
            with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
                db.execute('begin immediate')
                db.execute('insert or replace into __all_tablet_local_checksum values(' +
                           ','.join('?' for _ in saved) + ')', saved)
            hold.unlink()
            if fault == 'row_count':
                exp.sql('alter system clear merge error')
            gate.unlink()
            wait_for(lambda: progress()[2] == frozen)
            exp.record('PASS', case='child_global_index_report_fault', fault=fault, frozen=frozen, tablet=tablet)

        # Same partition count, different tablet binding: explicit retirement.
        gate.write_text('freeze then replace binding\n')
        frozen = freeze()
        exp.sql('alter table checksum_probe.t truncate partition p0', child)
        gate.unlink()
        wait_for(lambda: progress()[2] == frozen)
        assert b'merge object retired by committed catalog change' in logs(), str(exp.base)
        exp.record('PASS', case='post_freeze_same_count_partition_replacement', frozen=frozen)
    exp.record('PASS', case='namespace_historical_checksum', native=a.native)
finally:
    if child:
        child.close()
    exp.close()
