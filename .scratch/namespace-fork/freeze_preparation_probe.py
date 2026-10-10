#!/usr/bin/env python3
"""Real freeze readiness, terminal timeout and native allocation-lock races.

Except --case large, requires freeze_preparation_injection.py and
baseline_progress_injection.py in the disposable test binary.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import re
import resource
import time

import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id
from ddl_catalog_atomic_probe import roots, graph
from physical_merge_layout_probe import wait_for


def frozen(exp):
    return int(exp.sql('SELECT MAX(frozen_scn) FROM oceanbase.__all_freeze_info', log=False)[0][0])


def request(exp, statement, timeout=15):
    with connect(exp) as connection:
        connection._read_timeout = timeout + 15
        exp.sql('SET ob_query_timeout=%d' % (timeout * 1000000), connection, log=False)
        exp.sql('SET ob_trx_timeout=%d' % (timeout * 1000000), connection, log=False)
        started = time.monotonic()
        try:
            exp.sql(statement, connection)
            return dict(error=None, seconds=time.monotonic()-started)
        except pymysql.MySQLError as error:
            return dict(error=error.args, seconds=time.monotonic()-started)


def timeout_result(result):
    assert result['error'] and result['error'][0] == 4012, result


class Evidence:
    def __init__(self, exp):
        self.exp = exp
        self.positions = {}
        self.lines = []

    def read(self):
        for path in (self.exp.base / 'log').glob('seekdb.log*'):
            if path.suffix == '.wf':
                continue
            inode = path.stat().st_ino
            with path.open('rb') as stream:
                stream.seek(self.positions.get(inode, 0))
                while True:
                    start = stream.tell()
                    line = stream.readline()
                    if not line or not line.endswith(b'\n'):
                        stream.seek(start)
                        break
                    if any(marker in line for marker in (b'freeze locked baseline recheck',
                            b'freeze waits for namespace baselines', b'freeze preparation or publication failed',
                            b'FREEZE_PREPARATION_', b'daily major freeze request timed out')):
                        self.lines.append(line.decode(errors='replace'))
                self.positions[inode] = stream.tell()
        return self.lines

    def contains(self, *words):
        return [line for line in self.read() if all(word in line for word in words)]


def run(binary, case, partitions):
    exp = BootstrapExperiment(binary, 'freeze_preparation_' + case, prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
    if case == 'large':
        exp.extra_parameters += [('memory_budget', '8G'), ('datafile_size', '512M'),
                                 ('datafile_maxsize', '2G')]
    barrier = exp.base / 'freeze-barrier'
    baseline_pause = exp.base / 'pause-baseline'
    materialization_pause = exp.base / 'pause-materialization'
    allocation_pause = exp.base / 'pause-allocation'
    os.environ['SEEKDB_FREEZE_PREPARATION_BARRIER'] = str(barrier)
    os.environ['SEEKDB_BASELINE_PAUSE'] = str(baseline_pause)
    os.environ['SEEKDB_MATERIALIZATION_PAUSE'] = str(materialization_pause)
    os.environ['SEEKDB_NS_ALLOCATION_PAUSE'] = str(allocation_pause)
    os.environ['SEEKDB_BASELINE_PROGRESS_PROBE'] = '1'
    if case == 'pressure':
        os.environ['SEEKDB_FREEZE_PRESSURE_PROBE'] = '1'
    pool = ThreadPoolExecutor(max_workers=3)
    try:
        exp.start()
        exp.sql('ALTER SYSTEM SUSPEND MERGE')
        initial = frozen(exp)
        evidence = Evidence(exp)
        if case == 'large':
            exp.connection._read_timeout = 300
            exp.sql('SET ob_query_timeout=300000000')
            exp.sql('SET ob_trx_timeout=300000000')
            exp.sql('CREATE DATABASE freeze_cost')
            exp.sql('CREATE TABLE freeze_cost.t(id INT PRIMARY KEY,v INT) '
                    'PARTITION BY HASH(id) PARTITIONS %d' % partitions)
            with exp.connection.cursor() as cursor:
                for offset in range(0, partitions, 500):
                    cursor.executemany('INSERT INTO freeze_cost.t VALUES(%s,%s)',
                        [(i, i) for i in range(offset, min(offset + 500, partitions))])
            result = request(exp, 'ALTER SYSTEM MAJOR FREEZE', 120)
            assert result['error'] is None, result
            assert frozen(exp) > initial
            wait_for(lambda: evidence.contains('freeze locked baseline recheck', 'ready:true'),
                     'freeze final recheck log did not arrive', 10)
            checks = evidence.contains('freeze locked baseline recheck', 'ready:true')
            assert len(checks) == 1, checks
            fields = {key: int(value) for key, value in re.findall(r'(\w+):\s*(\d+)', checks[0])}
            cost = int(re.search(r'cost_us=(\d+)', checks[0])[1])
            assert fields['inspected_tablets'] >= partitions, checks
            target = frozen(exp)
            exp.record('freeze_preparation_measured', partitions=partitions,
                       inspected=fields['inspected_tablets'], locked_recheck_us=cost,
                       request=result, frozen=target)
            exp.sql('ALTER SYSTEM RESUME MERGE')
            def major_complete():
                row = exp.sql('SELECT frozen_scn,global_broadcast_scn,last_scn '
                              'FROM oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]
                return row == (target, target, target)
            started = time.monotonic()
            wait_for(major_complete, '8000-partition major did not complete', 300)
            expected = (partitions, partitions * (partitions - 1) // 2)
            actual = exp.sql('SELECT COUNT(*),SUM(v) FROM freeze_cost.t', log=False)[0]
            assert tuple(map(int, actual)) == expected, actual
            table = int(exp.sql("SELECT table_id FROM oceanbase.__all_table WHERE "
                "table_name='t' AND database_id=(SELECT database_id FROM oceanbase.__all_database "
                "WHERE database_name='freeze_cost')", log=False)[0][0])
            logical = {int(row[0]) for row in exp.sql(
                f'SELECT tablet_id FROM oceanbase.__all_part WHERE table_id={table}', log=False)}
            expected_physical = {physical_id(1, tablet) for tablet in logical}
            completed = {int(row[0]) for row in exp.sql(
                "SELECT tablet_id FROM oceanbase.V$OB_SSTABLES WHERE table_type IN ('MAJOR','CO_MAJOR') "
                f'AND end_log_scn={target}', log=False)}
            assert len(expected_physical) == partitions and expected_physical <= completed
            exp.record('PASS', case='freeze_preparation_large', partitions=partitions,
                       inspected=fields['inspected_tablets'], locked_recheck_us=cost,
                       request=result, frozen=target, major_seconds=time.monotonic()-started,
                       checked_major_tablets=len(expected_physical), rows=actual)
            return

        baseline_pause.write_text('0\n')
        materialization_pause.touch()
        if case == 'pressure':
            exp.sql('FORK NAMESPACE pressure_child FROM ns1')
            result = request(exp, 'ALTER SYSTEM MAJOR FREEZE', 3)
            timeout_result(result)
            assert evidence.contains('FREEZE_PREPARATION_PRESSURE ret=-4012 retry=0')
            assert frozen(exp) == initial
            exp.record('PASS', case='pressure_freeze_terminal_timeout', request=result,
                       retry_slot_cleared=True, unpublished=True)
            return
        if case == 'delete':
            exp.sql('FORK NAMESPACE deleted_child FROM ns1')
            freeze = pool.submit(request, exp, 'ALTER SYSTEM MAJOR FREEZE', 15)
            wait_for(lambda: evidence.contains('freeze waits for namespace baselines',
                                              'needs_materialization:true'),
                     'freeze did not wait for the live inherited child', 5)
            deletion = request(exp, 'DROP NAMESPACE deleted_child', 5)
            assert deletion['error'] is None, deletion
            result = freeze.result(timeout=15)
            assert result['error'] is None and frozen(exp) > initial, result
            exp.record('PASS', case='freeze_concurrent_namespace_delete', request=result,
                       deletion=deletion, fresh_live_set=True)
            return
        if case == 'daily':
            exp.sql('FORK NAMESPACE daily_child FROM ns1')
            # Leave enough of the same duty minute to observe two timer ticks.
            while time.localtime().tm_sec > 40:
                time.sleep(.5)
            duty = time.strftime('%H:%M')
            exp.sql("ALTER SYSTEM SET internal_sql_execute_timeout='2s'")
            exp.sql("ALTER SYSTEM SET major_freeze_duty_time='%s'" % duty)
            wait_for(lambda: evidence.contains('daily major freeze request timed out'),
                     'background request had no finite timeout', 20)
            time.sleep(6)
            failures = evidence.contains('daily major freeze request timed out')
            assert len(failures) == 1, failures
            assert frozen(exp) == initial
            exp.sql("ALTER SYSTEM SET major_freeze_duty_time='disable'")
            exp.record('PASS', case='daily_freeze_terminal_timeout', attempts=1,
                       unpublished=True, configured_timeout_s=2)
            return
        if case in ('before_lock', 'locked', 'crash', 'waited_lock'):
            if case == 'locked':
                exp.sql('CREATE DATABASE lock_window')
                exp.sql('CREATE TABLE lock_window.t(id INT PRIMARY KEY,v INT)')
                exp.sql('INSERT INTO lock_window.t VALUES(1,10)')
            phase = 'locked' if case == 'crash' else 'before_lock' if case == 'waited_lock' else case
            barrier.write_text(phase + '\n')
            freeze = pool.submit(request, exp, 'ALTER SYSTEM MAJOR FREEZE', 12)
            wait_for(lambda: evidence.contains('FREEZE_PREPARATION_BARRIER phase=' + phase),
                     'freeze barrier not reached', 8)
            if case == 'crash':
                assert frozen(exp) == initial
                exp.connection.close()
                exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                result = freeze.result(timeout=15)
                assert result['error'] and result['error'][0] in (2006,2013), result
                for control in (barrier, baseline_pause, materialization_pause):
                    control.unlink()
                exp.start()
                assert frozen(exp) == initial, 'uncommitted freeze survived the crash'
                retry = request(exp, 'ALTER SYSTEM MAJOR FREEZE', 30)
                assert retry['error'] is None and frozen(exp) > initial, retry
                fork = request(exp, 'FORK NAMESPACE after_crash FROM ns1', 10)
                assert fork['error'] is None, fork
                exp.record('PASS', case='freeze_locked_crash_rollback', lost_request=result,
                           explicit_retry=retry, allocation_lock_released=True, fork=fork)
                return
            if case == 'waited_lock':
                allocation_pause.touch()
            fork = pool.submit(request, exp, 'FORK NAMESPACE race_child FROM ns1', 15)
            if case == 'waited_lock':
                wait_for(lambda: evidence.contains('FREEZE_PREPARATION_FORK_ALLOCATION_LOCKED'),
                         'fork allocation lock not reached', 5)
                barrier.unlink()
                time.sleep(1)
                assert not freeze.done() and not fork.done()
                assert not evidence.contains('freeze locked baseline recheck')
                allocation_pause.unlink()
            if case in ('before_lock', 'waited_lock'):
                fork_result = fork.result(timeout=8)
                assert fork_result['error'] is None, fork_result
                if barrier.exists():
                    barrier.unlink()
                checks = wait_for(lambda: evidence.contains('freeze locked baseline recheck', 'ready:false'),
                                  'fresh locked view missed the newly committed fork', 8)
                result = freeze.result(timeout=20)
                timeout_result(result)
                assert frozen(exp) == initial
                exp.record('PASS', case='freeze_recheck_new_fork', waited_for_lock=case == 'waited_lock', request=result,
                           fork=fork_result, recheck=checks[-1].strip(), unpublished=True)
            else:
                ddl = pool.submit(request, exp, 'ALTER TABLE lock_window.t ADD COLUMN c INT DEFAULT 7', 15)
                time.sleep(1)
                assert not fork.done(), 'fork committed while freeze held the allocation row'
                assert not ddl.done(), 'DDL committed while freeze held DDL coordination'
                # Ordinary writes do not need the Namespace allocation row.
                dml = request(exp, 'UPDATE lock_window.t SET v=11', 3)
                assert dml['error'] is None, dml
                assert frozen(exp) == initial
                barrier.unlink()
                result = freeze.result(timeout=15)
                assert result['error'] is None, result
                fork_result = fork.result(timeout=15)
                assert fork_result['error'] is None, fork_result
                ddl_result = ddl.result(timeout=15)
                assert ddl_result['error'] is None, ddl_result
                assert exp.sql('SELECT v,c FROM lock_window.t') == ((11,7),)
                published = frozen(exp)
                assert published > initial
                child = namespace_id(exp, 'race_child')
                assert not exp.sql('SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info '
                                   'WHERE tablet_id BETWEEN %d AND %d' %
                                   (physical_id(child, 0), physical_id(child, (1 << 37)-1)), log=False)
                exp.record('PASS', case='freeze_counter_lock_orders_fork', request=result,
                           fork=fork_result, ddl=ddl_result, concurrent_dml=dml,
                           frozen=published, child_unmaterialized=True)
            return

        exp.sql('CREATE DATABASE freeze_wait')
        exp.sql('CREATE TABLE freeze_wait.t(id INT PRIMARY KEY,v INT,body LONGTEXT,KEY iv(v))')
        exp.sql("INSERT INTO freeze_wait.t VALUES(1,10,REPEAT('a',12000))")
        exp.sql('FORK NAMESPACE wait_child FROM ns1')
        child_id = namespace_id(exp, 'wait_child')
        bindings = graph(exp, roots(exp, child_id))
        expected = len(bindings)
        freeze = pool.submit(request, exp, 'ALTER SYSTEM MAJOR FREEZE', 3)
        wait_for(lambda: evidence.contains('freeze waits for namespace baselines',
                                          'needs_materialization:true'), 'missing inherited readiness', 2)
        dml = request(exp, 'UPDATE freeze_wait.t SET v=11', 2)
        assert dml['error'] is None and not freeze.done(), (dml, freeze.done())
        result = freeze.result(timeout=10)
        timeout_result(result)
        assert 'materialization' in result['error'][1], result
        assert frozen(exp) == initial
        exp.record('unmaterialized_timeout', request=result, concurrent_dml=dml)

        with connect(exp, 'root@wait_child') as child:
            exp.sql("ALTER TABLE freeze_wait.t COMMENT='materialize schema bindings'", child)
            exp.sql('UPDATE freeze_wait.t SET v=12', child)
            assert exp.sql('SELECT v,LENGTH(body) FROM freeze_wait.t', child) == ((12,12000),)
        result = request(exp, 'ALTER SYSTEM MAJOR FREEZE', 3)
        timeout_result(result)
        assert 'local baseline completion' in result['error'][1], result
        assert evidence.contains('freeze waits for namespace baselines', 'needs_materialization:false')
        assert frozen(exp) == initial
        exp.record('incomplete_takeover_timeout', request=result)
        baseline_pause.unlink()
        materialization_pause.unlink()
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        low, high = physical_id(child_id, 0), physical_id(child_id, (1 << 37)-1)
        started = time.monotonic()
        last_report = [0]

        def all_complete():
            rows = exp.sql('SELECT COUNT(*),SUM(ref_tablet_id<>0) FROM oceanbase.__all_virtual_tablet_info '
                           'WHERE tablet_id BETWEEN %d AND %d' % (low, high), log=False)
            assert frozen(exp) == initial, 'timed out freeze published later without a new request'
            if time.monotonic() - last_report[0] > 15:
                exp.record('background_progress', owned=rows, expected=expected,
                           seconds=time.monotonic()-started)
                last_report[0] = time.monotonic()
            return rows[0][0] == expected and rows[0][1] == 0

        wait_for(all_complete, 'background failed to complete all child bindings', 420)
        result = request(exp, 'ALTER SYSTEM MAJOR FREEZE', 30)
        assert result['error'] is None, result
        assert frozen(exp) > initial
        checks = evidence.contains('freeze locked baseline recheck', 'ready:true')
        assert checks, evidence.lines
        exp.record('PASS', case='freeze_readiness_timeout_retry', inherited=True,
                   incomplete=True, main_index_lob=True, background_completed=True,
                   no_automatic_retry=True, explicit_retry=result, recheck=checks[-1].strip())
    finally:
        for control in (barrier, baseline_pause, materialization_pause, allocation_pause):
            if control.exists():
                control.unlink()
        pool.shutdown(wait=True)
        exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--case', choices=('wait', 'before_lock', 'locked', 'large', 'daily', 'crash', 'waited_lock', 'delete', 'pressure'), default='wait')
    p.add_argument('--partitions', type=int, default=8000)
    a = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(a.binary, a.case, a.partitions)
