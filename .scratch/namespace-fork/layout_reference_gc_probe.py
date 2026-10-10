#!/usr/bin/env python3
"""Real SQL DROP, flying pooled objects, external copies and actual layout minor GC."""
import argparse
import os
from pathlib import Path
import resource
import sys
import tempfile
import time
import pymysql

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def run(binary, partitions=1, inherited=False):
    with tempfile.TemporaryDirectory(prefix='seekdb-layout-reference-gc-') as tmp:
        control = Path(tmp) / 'command'
        response = Path(str(control) + '.result')
        os.environ['SEEKDB_LAYOUT_REFERENCE_GC_CONTROL'] = str(control)
        exp = BootstrapExperiment(binary, 'layout_reference_gc', prototype=6)
        exp.extra_parameters = [('minor_compact_trigger', '2'), ('ob_compaction_schedule_interval', '3s')]
        if partitions > 1:
            exp.extra_parameters += [('memory_budget', '8G'), ('datafile_size', '512M'), ('datafile_maxsize', '2G')]
        sequence = 0
        physical = layout = version = 0
        child = None

        def command(action):
            nonlocal sequence
            sequence += 1
            pending = Path(tmp) / 'pending'
            pending.write_text(f'{sequence} {action} {physical} {layout} {version}\n')
            pending.replace(control)
            until = time.monotonic() + 180
            while time.monotonic() < until:
                assert exp.proc.poll() is None, 'server exited during command'
                if response.exists():
                    line = response.read_text()
                    if line.endswith('\n'):
                        values = dict((key, int(value)) for key, value in
                                      (field.split('=') for field in line.split()))
                        if values.get('seq') == sequence:
                            exp.record('reference_gc_command', action=action, **values)
                            assert values['ret'] == 0, values
                            return values
                time.sleep(.1)
            raise TimeoutError((action, sequence, str(exp.base)))

        def gc_stage(name, expected, mapped=None, absent=False):
            command('orphan')
            until = time.monotonic() + 240
            while time.monotonic() < until:
                status = command('cycle')
                # Matching refcounts and a deleted sentinel prove an actual
                # filtered minor ran while this exact combination was held.
                ready = status['orphan'] == -4018
                ready &= all(status[key] == value for key, value in expected.items())
                if mapped is not None:
                    ready &= status['mapped'] == mapped
                if ready:
                    if absent and (status['body'] != -4018 or status['head'] != -4018):
                        continue
                    assert status['body'] == (-4018 if absent else 0), status
                    assert status['head'] == (-4018 if absent else 0), status
                    exp.record('reference_gc_stage_pass', stage=name, **status)
                    return status
            raise TimeoutError((name, status))

        try:
            exp.start()
            exp.connection._read_timeout = 300
            exp.sql('SET ob_query_timeout=300000000')
            exp.sql('SET ob_trx_timeout=300000000')
            exp.sql('SET recyclebin=off')
            exp.sql('ALTER SYSTEM SET undo_retention=0')
            exp.sql('ALTER SYSTEM SET _mvcc_gc_using_min_txn_snapshot=false')
            exp.sql('CREATE DATABASE reference_gc')
            suffix = f' PARTITION BY HASH(id) PARTITIONS {partitions}' if partitions > 1 else ''
            exp.sql('CREATE TABLE reference_gc.t(id INT PRIMARY KEY,v INT)' + suffix)
            with exp.connection.cursor() as cursor:
                for offset in range(0, partitions, 500):
                    cursor.executemany('INSERT INTO reference_gc.t VALUES(%s,%s)',
                                       [(i, i) for i in range(offset, min(offset + 500, partitions))])
            table = exp.user_tables('reference_gc')[0]
            logical = int(table[2])
            if partitions > 1 or inherited:
                ids = ({int(row[0]) for row in exp.sql(
                    f'SELECT tablet_id FROM oceanbase.__all_part WHERE table_id={int(table[1])}', log=False)}
                    if partitions > 1 else {logical})
                assert len(ids) == partitions
                logical = min(ids)
                exp.sql('ALTER SYSTEM MAJOR FREEZE')
                target = exp.sql('SELECT MAX(frozen_scn) FROM oceanbase.__all_freeze_info', log=False)[0][0]
                until = time.monotonic() + 360
                while True:
                    row = exp.sql('SELECT frozen_scn,global_broadcast_scn,last_scn '
                                  'FROM oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)[0]
                    if row == (target, target, target):
                        break
                    assert time.monotonic() < until, ('major incomplete', row, target)
                    time.sleep(1)
                completed = {int(row[0]) for row in exp.sql(
                    "SELECT tablet_id FROM oceanbase.V$OB_SSTABLES WHERE table_type IN ('MAJOR','CO_MAJOR') "
                    f'AND end_log_scn={target}', log=False)}
                assert {(1 << 62) | (1 << 37) | i for i in ids} <= completed
                exp.record('reference_gc_major_ready', partitions=partitions, F=target)
            physical = (1 << 62) | (1 << 37) | logical
            held = command('hold')
            layout, version = held['G'], held['V']
            assert held['old'] == held['current'] == held['a'] == held['b'] == 1, held
            if inherited:
                for action in ('release_pool', 'release_a', 'release_b', 'clear'):
                    command(action)
                exp.sql('FORK NAMESPACE reference_child FROM ns1')

                def connect_child():
                    c = pymysql.connect(host='127.0.0.1', port=exp.port, user='root@reference_child',
                                        autocommit=True, connect_timeout=5, read_timeout=300)
                    exp.sql('SET ob_query_timeout=300000000', c, log=False)
                    return c

                child = connect_child()
                exp.sql('ALTER TABLE reference_gc.t ADD COLUMN child_only INT DEFAULT 7', child)
                exp.sql('UPDATE reference_gc.t SET v=17', child)
                command('inspect')
                exp.sql('DROP TABLE reference_gc.t')
                gc_stage('only_foreign_files', dict(old=0, current=0, a=0, b=0, matching=0), mapped=0)
                files = command('inspect_files')
                assert files['foreign'] > 0 and files['retained'] == 1 and files['body'] == 0, files
                assert exp.sql('SELECT * FROM reference_gc.t', child) == ((0, 17, 7),)
                command('clear')
                control.unlink()
                child.close()
                child = None
                exp.connection.close()
                exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                exp.record('crash_with_foreign_files', pid=exp.proc.pid)
                exp.start()
                child = connect_child()
                files = command('inspect_files')
                assert files['mapped'] == files['matching'] == 0, files
                assert files['foreign'] > 0 and files['retained'] == 1 and files['body'] == 0, files
                assert exp.sql('SELECT * FROM reference_gc.t', child) == ((0, 17, 7),)
                exp.sql('SET recyclebin=off', child)
                exp.sql('DROP TABLE reference_gc.t', child)
                child.close()
                child = None
                gc_stage('foreign_files_released', dict(old=0, current=0, a=0, b=0, matching=0),
                         mapped=0, absent=True)
                command('clear')
                control.unlink()
                exp.connection.close()
                exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                exp.record('crash_after_foreign_files_gc', pid=exp.proc.pid)
                exp.start()
                final = command('inspect_files')
                assert final['retained'] == final['foreign'] == 0, final
                assert final['body'] == final['head'] == -4018, final
                command('clear')
                exp.record('PASS', case='layout_reference_gc_foreign_files', real_takeover=True,
                           original_tablet_gone=True, foreign_file_retains=True,
                           last_file_reclaims=True, two_crash_recoveries=True)
                return
            if partitions > 1:
                samples = [command('inspect') for _ in range(5)]
                gc_stage('large_live_table_with_files', dict(old=1, current=1, a=1, b=1), mapped=1)
                command('release_pool')
                command('release_a')
                command('release_b')
                command('clear')
                control.unlink()
                exp.connection.close()
                exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                exp.record('crash_for_recovery', pid=exp.proc.pid)
                exp.start()
                exp.connection._read_timeout = 300
                exp.sql('SET ob_query_timeout=300000000')
                recovered = command('inspect')
                assert recovered['body'] == recovered['head'] == 0, recovered
                actual = exp.sql('SELECT COUNT(*),SUM(v) FROM reference_gc.t')[0]
                assert tuple(map(int, actual)) == (partitions, partitions * (partitions - 1) // 2), actual
                command('clear')
                exp.record('PASS', case='layout_reference_gc_large', partitions=partitions,
                           actual_major=True, samples=samples, actual_gc=True, recovery=True)
                return
            exp.sql('DROP TABLE reference_gc.t')
            assert not exp.user_tables('reference_gc')
            gc_stage('retired_pool_and_external', dict(old=1, current=1, a=1, b=1), mapped=0)
            command('release_pool')
            gc_stage('two_external_copies', dict(old=0, current=0, a=1, b=1, matching=2), mapped=0)
            command('release_a')
            gc_stage('last_external_copy', dict(old=0, current=0, a=0, b=1, matching=1), mapped=0)
            command('release_b')
            gc_stage('last_reference_released', dict(old=0, current=0, a=0, b=0, matching=0),
                     mapped=0, absent=True)
            command('clear')
            control.unlink()
            exp.connection.close()
            exp.connection = None
            exp.proc.kill()
            exp.proc.wait(timeout=15)
            exp.record('crash_for_recovery', pid=exp.proc.pid)
            exp.start()
            recovered = command('inspect')
            assert recovered['body'] == recovered['head'] == -4018, recovered
            assert recovered['mapped'] == recovered['matching'] == 0, recovered
            assert not exp.user_tables('reference_gc')
            command('clear')
            exp.record('PASS', case='layout_reference_gc', real_sql_drop=True,
                       flying_pool=True, two_external_copies=True, last_copy_retains=True,
                       actual_minor_reclaims=True, restart_preserves_deletion=True)
        finally:
            if child is not None:
                child.close()
            if control.exists():
                control.unlink()
            exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=1,
                        help='>1 verifies collection and GC with live partitions and actual major files')
    parser.add_argument('--inherited', action='store_true', help='retain a deleted parent layout through child files')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.inherited and args.partitions != 1:
        parser.error('--inherited requires --partitions=1')
    run(args.binary, args.partitions, args.inherited)
