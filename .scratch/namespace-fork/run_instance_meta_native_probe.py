#!/usr/bin/env python3
"""Local native-KV gate; requires the temporary probe injection test build."""
import argparse
import os
from pathlib import Path
import resource
import sys
import time
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def wait_probe(experiment, recovered, durable_phase):
    deadline = time.monotonic() + 90
    observed = ''
    while time.monotonic() < deadline:
        for log in (experiment.base / 'log').glob('seekdb.log*'):
            with log.open('rb') as stream:
                inode = log.stat().st_ino
                previous = experiment.probe_log_cursors.get(inode, 0)
                if log.stat().st_size < previous:
                    previous = 0
                stream.seek(previous)
                chunk = stream.read()
                experiment.probe_log_cursors[inode] = stream.tell()
                observed += ''.join(line + '\n' for line in chunk.decode(errors='replace').splitlines()
                                    if 'INSTANCE_' in line)
        if 'INSTANCE_META_PROBE_FAIL' in observed:
            failures = [line for line in observed.splitlines() if 'INSTANCE_META_PROBE_FAIL' in line]
            raise RuntimeError('\n'.join(failures[-4:]))
        if (f'INSTANCE_META_PROBE_PASS recovered={int(recovered)} ' in observed
                and 'INSTANCE_META_SEQUENTIAL_SCAN_PASS ' in observed
                and 'INSTANCE_META_LOCK_WAIT_PASS commit_handoff=1 deadline=1' in observed
                and f'INSTANCE_RECORD_PROBE_PASS root_recovered={int(recovered)} ' in observed
                and f'INSTANCE_RECORD_DURABLE phase={durable_phase} ' in observed
                and 'INSTANCE_CATALOG_VIEW_PASS ' in observed
                and 'INSTANCE_DDL_PROBE_PASS' in observed
                and 'INSTANCE_SHARED_TX_PROBE_PASS' in observed
                and 'INSTANCE_BINDING_PROBE_PASS partitions=8000 persisted_source=1 sql_released=1' in observed
                and 'INSTANCE_CREATION_DESCRIPTOR_PASS copy=1 codec=1' in observed
                and 'INSTANCE_PERSISTED_CREATION_DESCRIPTOR_PASS kv=1 sql_released=1 batch_owned=1 malformed=1' in observed
                and 'INSTANCE_PHYSICAL_RETENTION_PROBE_PASS ' in observed):
            experiment.record('native_kv_verified', recovered=recovered,
                              typed_records=True, physical_retention=True,
                              durable_phase=durable_phase)
            return
        if experiment.proc.poll() is not None:
            raise RuntimeError(f'instance exited: {experiment.proc.returncode}')
        time.sleep(.5)
    raise TimeoutError(f'native KV probe did not finish: {experiment.base}')


class GateExperiment(BootstrapExperiment):
    def start(self):
        self.probe_log_cursors = {
            log.stat().st_ino: log.stat().st_size
            for log in (self.base / 'log').glob('seekdb.log*')
        }
        done = threading.Event()
        def monitor():
            while not done.wait(.5):
                log = self.base / 'log/seekdb.log'
                if log.exists() and 'INSTANCE_META_PROBE_FAIL' in log.read_text(errors='replace'):
                    failures = [line for line in log.read_text(errors='replace').splitlines()
                                if 'INSTANCE_META_PROBE_FAIL' in line or '[DEBUG-meta-read]' in line]
                    self.record('FAIL', evidence=failures[-12:])
                    if self.proc is not None and self.proc.poll() is None:
                        self.proc.terminate()
                    return
        thread = threading.Thread(target=monitor, daemon=True)
        thread.start()
        try:
            super().start()
        finally:
            done.set()
            thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    os.environ['SEEKDB_INSTANCE_META_PROBE'] = '1'
    experiment = GateExperiment(args.binary, 'instance_meta_native', prototype=6)
    experiment.extra_parameters = [('minor_compact_trigger', '2')]
    try:
        phases = ('created', 'marked', 'finished', 'verified')
        for index, phase in enumerate(phases):
            experiment.start()
            wait_probe(experiment, index > 0, phase)
            if phase != 'verified':
                experiment.connection.close()
                experiment.connection = None
                experiment.proc.kill()
                experiment.proc.wait(timeout=15)
                experiment.record('crash_for_recovery', pid=experiment.proc.pid,
                                  after_phase=phase)
        experiment.record('PASS', case='instance_meta_native', rollback=True, cross_collection=True,
                          duplicate_key=True, own_writes=True, snapshot=True, locked_current_read=True,
                          sequential_put_put_scan=True, scan_reentrant_write_rejected=True,
                          binary_value_60000=True, key_512=True, value_65536=True, binary_bounds=True,
                          scan=True, erase=True, snapshot_across_minor=True, writer_conflict=True,
                          typed_records=True, catalog_tree=True, capped_roots=True,
                          obsolete_lineage_removed=True, gc_watermark=True, physical_retention=True,
                          crash_recovery=True, durable_fork_drop=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    main()
