#!/usr/bin/env python3
"""Keep one checksum missing while other groups verify, then crash mid-round.

Requires physical_report_injection. Holds real report publication; physical
compaction and restart use the production paths.
"""
import argparse
import os
import re
import sqlite3
import time

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True)
a = p.parse_args()
exp = BootstrapExperiment(a.binary, 'checksum_group_recovery', prototype=6)
gate = exp.base / 'round.pause'
hold = exp.base / 'report.hold'
completion = exp.base / 'completion.pause'
os.environ['SEEKDB_PHYSICAL_REPORT_GATE'] = str(gate)
os.environ['SEEKDB_PHYSICAL_REPORT_HOLD'] = str(hold)
os.environ['SEEKDB_CHECKSUM_COMPLETION_GATE'] = str(completion)
child = None


class Trace:
    def __init__(self):
        self.positions = {path.stat().st_ino: path.stat().st_size for path in self.paths()}
        self.pairs = set()
        self.completions = set()

    def paths(self):
        return [path for path in (exp.base / 'log').glob('seekdb.log*') if '.wf' not in path.name]

    def read(self):
        for path in self.paths():
            with path.open('rb') as stream:
                inode = os.fstat(stream.fileno()).st_ino
                stream.seek(self.positions.get(inode, 0))
                for raw in stream:
                    if not raw.endswith(b'\n'):
                        break
                    self.positions[inode] = stream.tell()
                    line = raw.decode(errors='replace')
                    pair = re.search(r'historical index checksum verified\(data=\{namespace_id:(\d+), '
                                     r'table_id:(\d+),.*freeze.frozen_scn_=\{val:(\d+)', line)
                    if pair:
                        self.pairs.add(tuple(map(int, pair.groups())))
                    completed = re.search(r'CHECKSUM_BEFORE_DURABLE_COMPLETION F=(\d+)', line)
                    if completed:
                        self.completions.add(int(completed[1]))

    def has_pairs(self, expected):
        self.read()
        return expected <= self.pairs

    def completion_wait(self, frozen):
        self.read()
        return frozen in self.completions


def wait_for(fn, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        assert exp.proc.poll() is None, ('instance exited', exp.base)
        result = fn()
        if result:
            return result
        time.sleep(.3)
    raise AssertionError(('timeout', str(exp.base)))


def progress():
    return exp.sql('select frozen_scn,global_broadcast_scn,last_scn '
                   'from oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]


def crash_and_restart():
    global child
    child.close()
    child = None
    exp.connection.close()
    exp.connection = None
    exp.proc.kill()
    exp.proc.wait(timeout=15)
    exp.record('crash_for_recovery', pid=exp.proc.pid)
    trace = Trace()
    exp.start()
    child = connect(exp, 'root@group_child')
    return trace


def reports_at_f(frozen, tablet_ids):
    ids = ','.join(map(str, tablet_ids))
    rows = exp.sql('select tablet_id,compaction_scn,report_scn from '
                  f'oceanbase.__all_virtual_tablet_meta_table where tablet_id in ({ids})', log=False)
    assert len(rows) == len(tablet_ids), rows
    assert all(scn == frozen and report < frozen for _, scn, report in rows), rows


try:
    exp.start()
    exp.connection._read_timeout = 330
    exp.sql('set ob_query_timeout=300000000')
    exp.sql('set ob_trx_timeout=300000000')
    exp.sql("alter system set internal_sql_execute_timeout='300s'")
    exp.sql("alter system set ob_compaction_schedule_interval='3s'")
    exp.sql('create database group_probe')
    definitions = {}
    for name in ('blocked', 'ready'):
        exp.sql(f'create table group_probe.{name}(id int primary key,v int)')
        exp.sql(f'create index by_v on group_probe.{name}(v)')
        exp.sql(f'insert into group_probe.{name} values(1,10),(2,20)')
        table_id, logical = exp.sql('select table_id,tablet_id from oceanbase.__all_table '
                                   f"where table_name='{name}'", log=False)[0]
        index_id, index_logical = exp.sql('select table_id,tablet_id from oceanbase.__all_table '
                                         f'where data_table_id={table_id}', log=False)[0]
        definitions[name] = (table_id, logical, index_id, index_logical)
    exp.sql('fork namespace group_child from ns1')
    child = connect(exp, 'root@group_child')
    child_id = namespace_id(exp, 'group_child')
    exp.sql('update group_probe.ready set v=v+100', child)
    gate.write_text('wait for all physical results\n')
    previous = progress()[0]
    exp.sql('alter system minor freeze')
    exp.sql('alter system major freeze')
    frozen = wait_for(lambda: (lambda r: r[0] if r[0] > previous and r[1] == r[0] else None)(progress()))
    wait_for(lambda: exp.sql('select count(*) from oceanbase.__all_virtual_tablet_info t '
        'left join oceanbase.__all_virtual_tablet_meta_table r on r.tablet_id=t.tablet_id '
        f'where t.tablet_id>{1 << 62} and t.tablet_status=1 and t.is_empty_shell=0 '
        f'and (r.tablet_id is null or r.compaction_scn<{frozen})', log=False)[0][0] == 0)
    target = physical_id(1, definitions['blocked'][3])
    wait_for(lambda: exp.sql('select tablet_id from oceanbase.__all_virtual_tablet_local_checksum '
                            f'where tablet_id={target} and compaction_scn={frozen}', log=False))
    hold.write_text(str(target))
    with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
        db.execute('begin immediate')
        saved = db.execute('select * from __all_tablet_local_checksum where tablet_id=?', (target,)).fetchone()
        assert saved
        db.execute('delete from __all_tablet_local_checksum where tablet_id=?', (target,))
    ready_pairs = {(1, definitions['ready'][0], frozen),
                   (child_id, definitions['blocked'][0], frozen),
                   (child_id, definitions['ready'][0], frozen)}
    trace = Trace()
    gate.unlink()
    wait_for(lambda: trace.has_pairs(ready_pairs))
    assert progress()[2] < frozen, progress()
    exp.record('PASS', case='independent_groups_and_owners', frozen=frozen, pairs=sorted(ready_pairs))

    ready_tablet = physical_id(1, definitions['ready'][1])
    all_ids = [physical_id(owner, logical) for owner in (1, child_id)
               for _, data, _, index in definitions.values() for logical in (data, index)]
    # Later DML and a tablet-only medium request cannot consume pending F's
    # major/checksum. Minor continues; it must not advance the major result.
    exp.sql('update group_probe.ready set v=v+7')
    exp.sql('alter system minor freeze')
    exp.sql(f'alter system major freeze tablet_id={ready_tablet}')
    time.sleep(5)
    reports_at_f(frozen, all_ids)
    trace = crash_and_restart()
    wait_for(lambda: trace.has_pairs(ready_pairs))
    assert progress()[2] < frozen, progress()
    reports_at_f(frozen, all_ids)
    assert not exp.sql('select tablet_id from oceanbase.__all_virtual_tablet_local_checksum '
                       f'where tablet_id={target}', log=False)
    exp.record('PASS', case='missing_input_restart_revalidates', frozen=frozen)

    # Finish comparison but stop at the existing durable round publication.
    # report_scn must remain old: it is permission to replace F's checksum.
    completion.write_text('before durable completion\n')
    gate.write_text('restore missing result\n')
    with sqlite3.connect(exp.base / 'store/sstable/meta.db', timeout=15) as db:
        db.execute('begin immediate')
        db.execute('insert or replace into __all_tablet_local_checksum values('
                   + ','.join('?' for _ in saved) + ')', saved)
    hold.unlink()
    gate.unlink()
    wait_for(lambda: trace.completion_wait(frozen))
    assert progress()[2] < frozen, progress()
    reports_at_f(frozen, all_ids)
    trace = crash_and_restart()
    wait_for(lambda: trace.completion_wait(frozen))
    assert trace.has_pairs(ready_pairs | {(1, definitions['blocked'][0], frozen)})
    assert progress()[2] < frozen, progress()
    reports_at_f(frozen, all_ids)
    completion.unlink()
    wait_for(lambda: progress()[2] == frozen)
    wait_for(lambda: exp.sql('select count(*) from oceanbase.__all_virtual_tablet_meta_table '
        f'where tablet_id in ({",".join(map(str, all_ids))}) and report_scn>={frozen}', log=False)[0][0] == len(all_ids))
    assert exp.sql('select id,v from group_probe.ready order by id', log=False) == ((1, 17), (2, 27))
    assert exp.sql('select id,v from group_probe.ready order by id', child, log=False) == ((1, 110), (2, 120))
    exp.record('PASS', case='completion_publication_restart', frozen=frozen)
    exp.record('PASS', case='checksum_group_recovery')
finally:
    if child is not None:
        child.close()
    gate.unlink(missing_ok=True)
    hold.unlink(missing_ok=True)
    completion.unlink(missing_ok=True)
    exp.close()
