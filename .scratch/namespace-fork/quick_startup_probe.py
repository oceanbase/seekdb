#!/usr/bin/env python3
"""Local 30-second startup readiness signal for the KV directory cutover."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

import pymysql

sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--fork', action='store_true')
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--drop', action='store_true')
    parser.add_argument('--gc', action='store_true')
    args = parser.parse_args()
    experiment = BootstrapExperiment(args.binary, 'kv_startup_30s', prototype=6)
    command = [experiment.binary, '--nodaemon', '--base-dir=' + str(experiment.base),
               '-P' + str(experiment.port), '--log-level=INFO', '--parameter',
               'memory_budget=2G', '--parameter', 'cpu_count=4', '--parameter',
               'datafile_size=256M', '--parameter', 'datafile_maxsize=512M',
               '--parameter', 'log_disk_size=2G', '--parameter',
               'max_syslog_file_count=16']
    try:
        experiment.proc = subprocess.Popen(command, env=os.environ.copy(),
                                           stdout=experiment.output,
                                           stderr=subprocess.STDOUT)
        experiment.record('setup', base=experiment.base, pid=experiment.proc.pid,
                          port=experiment.port)
        deadline = time.monotonic() + args.seconds
        last_error = None
        while time.monotonic() < deadline:
            if experiment.proc.poll() is not None:
                raise RuntimeError(f'exited={experiment.proc.returncode}')
            try:
                experiment.connection = pymysql.connect(
                    host='127.0.0.1', port=experiment.port, user='root', password='',
                    autocommit=True, connect_timeout=2, read_timeout=2)
                break
            except pymysql.MySQLError as error:
                last_error = type(error).__name__
                time.sleep(.2)
        if experiment.connection is None:
            raise TimeoutError(f'not ready in {args.seconds}s; last={last_error}')
        experiment.sql('SELECT 1')
        if args.fork:
            experiment.sql('CREATE DATABASE kvsmoke')
            experiment.sql('CREATE TABLE kvsmoke.t(id INT PRIMARY KEY, v INT)')
            experiment.sql('INSERT INTO kvsmoke.t VALUES (1, 42)')
            experiment.sql('FORK NAMESPACE kvsmoke_child FROM ns1')
            with connect(experiment, 'root@kvsmoke_child') as child:
                assert experiment.sql('SELECT v FROM kvsmoke.t WHERE id=1', child) == ((42,),)
                if args.write:
                    experiment.sql('UPDATE kvsmoke.t SET v=43 WHERE id=1', child)
                    assert experiment.sql('SELECT v FROM kvsmoke.t WHERE id=1', child) == ((43,),)
            physical = None
            if args.gc:
                rows = experiment.sql(
                    'SELECT tablet_id,tablet_status,is_committed,is_empty_shell '
                    'FROM oceanbase.__all_virtual_tablet_info', log=False)
                child_rows = [row for row in rows if row[0] & (1 << 62)
                              and ((row[0] & ~(1 << 62)) >> 37) > 2]
                assert len(child_rows) == 1, child_rows
                physical = child_rows[0][0]
                experiment.record('child_physical', tablet=physical, row=child_rows[0])
            if args.drop:
                drop_deadline = time.monotonic() + 5
                while True:
                    try:
                        experiment.sql('DROP NAMESPACE kvsmoke_child')
                        break
                    except pymysql.OperationalError as error:
                        if 'active connections' not in str(error).lower() \
                                or time.monotonic() >= drop_deadline:
                            raise
                        time.sleep(.1)
                assert experiment.sql('SELECT v FROM kvsmoke.t WHERE id=1') == ((42,),)
                if args.gc:
                    gc_deadline = time.monotonic() + 30
                    while time.monotonic() < gc_deadline:
                        rows = experiment.sql(
                            'SELECT tablet_id,tablet_status,is_committed,is_empty_shell '
                            'FROM oceanbase.__all_virtual_tablet_info '
                            f'WHERE tablet_id={physical}', log=False)
                        if not rows or rows[0][3] == 1:
                            experiment.record('gc_complete', tablet=physical, rows=rows)
                            break
                        time.sleep(.5)
                    else:
                        raise AssertionError(('gc_timeout', physical, rows))
        experiment.record('PASS', elapsed_limit_s=args.seconds)
    finally:
        experiment.close()


if __name__ == '__main__':
    main()
