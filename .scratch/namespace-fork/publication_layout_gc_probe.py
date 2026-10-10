#!/usr/bin/env python3
"""Real layout minor reclamation while SQL continuously creates/drops tablets."""
import argparse
import os
from pathlib import Path
import resource
import tempfile
import threading
import time

import pymysql

from standby_background_copy_probe import BootstrapExperiment
from standby_copy_layout_gc_probe import CopyNode
from standby_layout_gc_probe import record, wait


def run(binary, partitions):
    with tempfile.TemporaryDirectory(prefix='seekdb-publication-layout-gc-') as directory:
        controls = Path(directory)
        os.environ['SEEKDB_LAYOUT_REFERENCE_GC_CONTROL_DIR'] = str(controls)
        os.environ['SEEKDB_LAYOUT_REFERENCE_GC_CAPTURE_US'] = '5000000'
        exp = BootstrapExperiment(binary, 'publication_layout_gc', prototype=6)
        exp.extra_parameters = [('memory_budget', '8G'), ('datafile_size', '512M'),
                                ('datafile_maxsize', '2G'), ('minor_compact_trigger', '2'),
                                ('ob_compaction_schedule_interval', '3s')]
        stop = threading.Event()
        operations, errors = [], []
        worker = None

        def publish():
            try:
                connection = pymysql.connect(host='127.0.0.1', port=exp.port, user='root',
                    autocommit=True, connect_timeout=5, read_timeout=30)
                try:
                    with connection.cursor() as cursor:
                        cursor.execute('SET recyclebin=off')
                        while not stop.is_set():
                            for statement in ('CREATE TABLE publication_gc.churn(id INT PRIMARY KEY,v INT)',
                                              'DROP TABLE publication_gc.churn'):
                                started = time.monotonic()
                                cursor.execute(statement)
                                operations.append((started, time.monotonic(), statement.split()[0]))
                finally:
                    connection.close()
            except BaseException as error:
                errors.append(repr(error))

        try:
            exp.start()
            exp.connection._read_timeout = 330
            exp.sql('SET ob_query_timeout=300000000')
            exp.sql('SET ob_trx_timeout=300000000')
            exp.sql('ALTER SYSTEM SET undo_retention=0')
            exp.sql('ALTER SYSTEM SET _mvcc_gc_using_min_txn_snapshot=false')
            exp.sql('CREATE DATABASE publication_gc')
            exp.sql('CREATE TABLE publication_gc.t(id INT PRIMARY KEY,v INT) '
                    f'PARTITION BY HASH(id) PARTITIONS {partitions}')
            with exp.connection.cursor() as cursor:
                for offset in range(0, partitions, 500):
                    cursor.executemany('INSERT INTO publication_gc.t VALUES(%s,%s)',
                        [(i, i) for i in range(offset, min(offset + 500, partitions))])
            table = exp.user_tables('publication_gc')[0]
            ids = {int(row[0]) for row in exp.sql('SELECT tablet_id FROM oceanbase.__all_part '
                f'WHERE table_id={int(table[1])}', log=False)}
            assert len(ids) == partitions
            controller = CopyNode(exp, controls)
            controller.physical = (1 << 62) | (1 << 37) | min(ids)
            target = controller.major()
            completed = {int(row[0]) for row in exp.sql(
                "SELECT tablet_id FROM oceanbase.V$OB_SSTABLES WHERE table_type IN ('MAJOR','CO_MAJOR') "
                f'AND end_log_scn={target}', log=False)}
            assert {(1 << 62) | (1 << 37) | i for i in ids} <= completed
            described = controller.command('describe')
            controller.layout, controller.version = described['G'], described['V']
            sentinel = controller.command('orphan')['orphan_id']
            worker = threading.Thread(target=publish)
            worker.start()
            wait(lambda: len(operations) >= 4 or errors, 30)
            assert not errors, errors
            started = time.monotonic()
            before = len(operations)
            record('publication_gc_pressure_started', partitions=partitions, operations=before,
                   orphan=sentinel, F=target)
            until = started + 180
            try:
                while True:
                    status = controller.command('cycle')
                    assert not errors and worker.is_alive(), errors
                    if status['orphan'] == -4018:
                        break
                    assert time.monotonic() < until, status
            except BaseException:
                # Same instance/data, only the SQL publisher stops. Preserve the
                # original failure even if the quiet control also cannot collect.
                stop.set()
                worker.join(timeout=40)
                try:
                    quiet = controller.command('inspect')
                    record('publication_gc_quiet_control', result=quiet, errors=errors)
                except BaseException as error:
                    record('publication_gc_quiet_control_failed', error=repr(error))
                raise
            finished = time.monotonic()
            assert status['body'] == status['head'] == 0, status
            assert len(operations) > before + 2, operations
            record('publication_gc_progress', elapsed_s=finished - started,
                   completed_operations=len(operations) - before, result=status)
            stop.set()
            worker.join(timeout=40)
            assert not worker.is_alive() and not errors, errors
            actual = exp.sql('SELECT COUNT(*),SUM(v) FROM publication_gc.t')[0]
            assert tuple(map(int, actual)) == (partitions, partitions * (partitions - 1) // 2), actual
            controller.command('clear')
            record('PASS', case='publication_layout_gc', partitions=partitions,
                   actual_minor_during_publication=True, live_layout_retained=True)
        finally:
            stop.set()
            if worker is not None:
                worker.join(timeout=40)
            record('publication_gc_workload', operations=len(operations), errors=errors,
                   spans=operations)
            exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=8000)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.partitions)
