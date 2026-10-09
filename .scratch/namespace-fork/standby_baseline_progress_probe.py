#!/usr/bin/env python3
"""Hold standby baseline while primary completes and reclaims its source."""
import argparse
import os
from pathlib import Path
import resource
import subprocess
import time

import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id, physical_state
from standby_background_copy_probe import free_port


def wait(check, label, seconds=180):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if check():
            return
        time.sleep(.5)
    raise AssertionError(label)


def run(binary):
    os.environ['SEEKDB_BASELINE_PROGRESS_PROBE'] = '1'
    primary = BootstrapExperiment(binary, 'baseline_progress_primary', prototype=6)
    standby = BootstrapExperiment(binary, 'baseline_progress_standby', prototype=6)
    rpc = free_port()
    primary.extra_parameters = [('enable_rpc_service', 'true'), ('rpc_port', str(rpc)),
                                ('ob_compaction_schedule_interval', '3s')]
    control = standby.base / 'pause-baseline'
    environment = os.environ.copy()
    environment['SEEKDB_BASELINE_PAUSE'] = str(control)
    command = [str(Path(binary).resolve()), '--nodaemon', '--base-dir=' + str(standby.base),
               '-P' + str(standby.port), '--role=STANDBY', '--parameter',
               'log_restore_source=127.0.0.1:%d' % rpc, '--parameter', 'enable_rpc_service=true',
               '--parameter', 'rpc_port=%d' % free_port(), '--parameter', 'memory_budget=2G',
               '--parameter', 'cpu_count=4', '--parameter', 'datafile_size=2G',
               '--parameter', 'datafile_maxsize=4G', '--parameter', 'log_disk_size=2G',
               '--parameter', 'max_syslog_file_count=16', '--parameter', 'ob_compaction_schedule_interval=3s']

    def start_standby():
        standby.proc = subprocess.Popen(command, env=environment, stdout=standby.output,
                                        stderr=subprocess.STDOUT)
        standby.record('setup', pid=standby.proc.pid, base=standby.base, port=standby.port)
        def ready():
            assert standby.proc.poll() is None, 'standby exited'
            try:
                standby.connection = connect(standby)
                return True
            except pymysql.MySQLError:
                return False
        wait(ready, 'standby never admitted SQL')

    def edge(exp, tablet):
        return exp.sql('SELECT ref_tablet_id FROM oceanbase.__all_virtual_tablet_info '
                       'WHERE tablet_id=%d' % tablet, log=False)

    def read_child(exp):
        with connect(exp, 'root@slow_baseline') as child:
            return exp.sql('SELECT * FROM slow_copy.t', child, log=False)

    def crash(exp):
        exp.connection.close()
        exp.connection = None
        exp.proc.kill()
        exp.proc.wait(timeout=15)

    try:
        primary.start()
        primary.sql('CREATE DATABASE slow_copy')
        primary.sql('CREATE TABLE slow_copy.t(id INT PRIMARY KEY,v INT)')
        primary.sql('INSERT INTO slow_copy.t VALUES(1,7)')
        logical = int(primary.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE "
                                 "table_name='t' AND database_id=(SELECT database_id FROM "
                                 "oceanbase.__all_database WHERE database_name='slow_copy')", log=False)[0][0])
        start_standby()
        control.write_text('0\n')
        primary.sql('FORK NAMESPACE slow_baseline FROM ns1')
        child_id = namespace_id(primary, 'slow_baseline')
        owned, source = physical_id(child_id, logical), physical_id(1, logical)
        control.write_text(str(owned) + '\n')
        with connect(primary, 'root@slow_baseline') as child:
            primary.sql('UPDATE slow_copy.t SET v=8', child)
        wait(lambda: edge(standby, owned) == ((source,),), 'standby never had an incomplete native fork')
        primary.sql('DROP TABLE slow_copy.t')
        primary.sql('ALTER SYSTEM MINOR FREEZE')
        wait(lambda: edge(primary, owned) == ((0,),) and not physical_state(primary, [source]),
             'primary did not complete and reclaim its source')
        assert edge(standby, owned) == ((source,),)
        assert physical_state(standby, [source]) and not physical_state(standby, [source])[0][3]
        assert read_child(standby) == ((1,8),)
        standby.record('primary_complete_standby_incomplete', source=source, child=owned,
                       source_retained_on_standby=True)
        crash(primary)
        primary.start()
        assert read_child(primary) == ((1,8),)
        crash(standby)
        start_standby()
        assert edge(standby, owned) == ((source,),)
        assert read_child(standby) == ((1,8),)
        assert physical_state(standby, [source]) and not physical_state(standby, [source])[0][3]
        standby.record('both_restarted_while_standby_incomplete', source_retained=True)
        control.unlink()
        wait(lambda: edge(standby, owned) == ((0,),) and not physical_state(standby, [source]),
             'standby did not finish local baseline and reclaim source')
        assert read_child(standby) == ((1,8),)
        primary.sql('ALTER SYSTEM SWITCHOVER TO STANDBY')
        standby.sql('ALTER SYSTEM SWITCHOVER TO PRIMARY')
        with connect(standby, 'root@slow_baseline') as child:
            standby.sql('INSERT INTO slow_copy.t VALUES(2,9)', child)
            assert standby.sql('SELECT * FROM slow_copy.t ORDER BY id', child) == ((1,8),(2,9))
        standby.record('PASS', case='independent_replica_baseline', primary_first=True,
                       both_crash_restarted=True, local_gc=True, promoted_write=True)
    finally:
        standby.close()
        primary.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary)
