#!/usr/bin/env python3
"""A real full-copy RPC view retains a retired layout until its last file transfer."""
import argparse
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import time

import pymysql

from standby_background_copy_probe import BootstrapExperiment, free_port
from standby_layout_gc_probe import Node, wait, record


class CopyNode(Node):
    def __init__(self, experiment, controls):
        super().__init__(experiment.base.name, experiment.port, 0, '', controls)
        self.experiment = experiment
        self.base = experiment.base
        self.connection = experiment.connection

    def pid(self):
        assert self.experiment.proc.poll() is None
        return self.experiment.proc.pid


def describe_ready(controller):
    def ready():
        # SQL/data can become readable before the async freeze-info reload has
        # installed its history-retention view. Wait for that real view; do not
        # synthesize a retention boundary or suppress other errors.
        result = controller.command('describe', retryable=(-4006,))
        return result if result['ret'] == 0 else None
    return wait(ready)


def run(binary):
    with tempfile.TemporaryDirectory(prefix='seekdb-copy-layout-gc-') as directory:
        controls = Path(directory)
        os.environ['SEEKDB_LAYOUT_REFERENCE_GC_CONTROL_DIR'] = str(controls)
        primary = BootstrapExperiment(binary, 'copy_layout_gc_primary', prototype=6)
        replica = BootstrapExperiment(binary, 'copy_layout_gc_replica', prototype=6)
        rpc = free_port()
        pause = controls / 'copy-pause'
        primary.extra_parameters = [('enable_rpc_service', 'true'), ('rpc_port', str(rpc)),
                                    ('ob_compaction_schedule_interval', '3s'), ('minor_compact_trigger', '2')]
        try:
            primary.start()
            primary.connection._read_timeout = 330
            primary.sql('USE oceanbase')
            primary.sql('SET ob_query_timeout=300000000')
            primary.sql('SET ob_trx_timeout=300000000')
            primary.sql('SET recyclebin=off')
            primary.sql('ALTER SYSTEM SET undo_retention=0')
            primary.sql('ALTER SYSTEM SET _mvcc_gc_using_min_txn_snapshot=false')
            primary.sql('CREATE DATABASE copy_layout_gc')
            primary.sql('CREATE TABLE copy_layout_gc.t(id INT PRIMARY KEY,v INT)')
            primary.sql('INSERT INTO copy_layout_gc.t VALUES(1,10)')
            primary.sql('CREATE TABLE copy_layout_gc.witness(id INT PRIMARY KEY,v INT)')
            primary.sql('INSERT INTO copy_layout_gc.witness VALUES(1,50)')
            tables = {name: int(tablet) for name, _, tablet in primary.user_tables('copy_layout_gc')}
            controller = CopyNode(primary, controls)
            controller.physical = (1 << 62) | (1 << 37) | tables['t']
            target = controller.major()
            described = controller.command('describe')
            controller.layout, controller.version = described['G'], described['V']
            pause.write_text(str(controller.physical))
            env = os.environ.copy()
            env['SEEKDB_STANDBY_COPY_PAUSE'] = str(pause)
            command = [str(Path(binary).resolve()), '--nodaemon', '--base-dir=' + str(replica.base),
                       '-P' + str(replica.port), '--role=STANDBY']
            for option in [f'log_restore_source=127.0.0.1:{rpc}', 'enable_rpc_service=true',
                           f'rpc_port={free_port()}', 'memory_budget=2G', 'cpu_count=4',
                           'datafile_size=2G', 'datafile_maxsize=4G', 'log_disk_size=2G',
                           'max_syslog_file_count=16', 'ob_compaction_schedule_interval=3s', 'minor_compact_trigger=2']:
                command.extend(('--parameter', option))
            def start_replica():
                replica.proc = subprocess.Popen(command, env=env, stdout=replica.output, stderr=subprocess.STDOUT)
                replica.record('setup', base=replica.base, pid=replica.proc.pid, port=replica.port)

            start_replica()
            wait(lambda: Path(str(pause) + '.ready').exists(), 90)
            record('copy_layout_paused', physical=controller.physical, G=controller.layout, V=controller.version)
            primary.sql('DROP TABLE copy_layout_gc.t')
            controller.command('orphan')
            until = time.monotonic() + 150
            while True:
                captured = controller.command('cycle')
                if captured['mapped'] == 0 and captured['orphan'] == -4018:
                    break
                assert time.monotonic() < until, captured
            # No test-owned handles, no child fork, no logical owner remains.
            # The source RPC view alone still contains the removed tablet/files.
            assert captured['old'] == captured['current'] == captured['a'] == captured['b'] == 0, captured
            assert captured['matching'] > 0 and captured['retained'] == 1, captured
            assert captured['body'] == captured['head'] == 0, captured
            record('copy_view_layout_retained', result=captured)
            pause.unlink()

            def readable():
                assert replica.proc.poll() is None, replica.proc.returncode
                try:
                    if replica.connection is None:
                        replica.connection = pymysql.connect(host='127.0.0.1', port=replica.port, user='root',
                            autocommit=True, connect_timeout=2, read_timeout=10)
                    return replica.sql('SELECT * FROM copy_layout_gc.witness', log=False) == ((1,50),)
                except pymysql.MySQLError:
                    if replica.connection is not None:
                        replica.connection.close()
                        replica.connection = None
                    raise
            wait(readable, 180)
            assert [row[0] for row in replica.user_tables('copy_layout_gc')] == ['witness']
            replica_controller = CopyNode(replica, controls)
            replica_controller.physical = (1 << 62) | (1 << 37) | tables['witness']
            copied = describe_ready(replica_controller)
            assert copied['body'] == copied['head'] == 0, copied
            record('copy_layout_readable', witness=copied)

            controller.command('orphan')
            until = time.monotonic() + 180
            while True:
                released = controller.command('cycle')
                if (released['matching'] == released['retained'] == 0
                        and released['body'] == released['head'] == released['orphan'] == -4018):
                    break
                assert time.monotonic() < until, released
            record('copy_view_release_reclaimed_layout', result=released)
            controller.command('clear')
            replica_controller.command('clear')
            replica.connection.close()
            replica.connection = None
            replica.proc.kill()
            replica.proc.wait(timeout=15)
            replica.record('crash_after_full_copy')
            start_replica()
            wait(readable, 180)
            recovered = describe_ready(replica_controller)
            assert recovered['body'] == recovered['head'] == 0, recovered
            assert [row[0] for row in replica.user_tables('copy_layout_gc')] == ['witness']
            replica_controller.command('clear')
            record('PASS', case='standby_copy_layout_gc', actual_copy_pause=True,
                   source_tablet_removed=True, only_copy_view_retains=True,
                   source_minor_reclaims_after_release=True, copied_layout_readable=True,
                   replayed_drop=True, replica_crash_recovery=True, F=target)
        finally:
            pause.unlink(missing_ok=True)
            replica.close()
            primary.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
