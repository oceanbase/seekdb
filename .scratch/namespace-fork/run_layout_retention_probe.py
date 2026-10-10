#!/usr/bin/env python3
"""Unfinished freeze retains layout MVCC through mini/minor, crash and release."""
import argparse
import os
from pathlib import Path
import resource
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    os.environ['SEEKDB_LAYOUT_RETENTION_PROBE'] = '1'
    experiment = BootstrapExperiment(args.binary, 'layout_retention', prototype=6)
    experiment.extra_parameters = [('minor_compact_trigger', '2')]
    try:
        for recovered in (0, 1, 2):
            cursors = {log.stat().st_ino: log.stat().st_size
                       for log in (experiment.base / 'log').glob('seekdb.log*')}
            experiment.start()
            output = ''
            for log in (experiment.base / 'log').glob('seekdb.log*'):
                with log.open('rb') as stream:
                    stream.seek(cursors.get(log.stat().st_ino, 0))
                    output += '\n'.join(line.decode(errors='replace') for line in stream
                                        if b'LAYOUT_RETENTION_' in line or b'LAYOUT_RECLAMATION_' in line)
            expected = (f'LAYOUT_RETENTION_PASS recovered={recovered} ' if recovered < 2
                        else 'LAYOUT_RECLAMATION_RECOVERED head_absent=1 bodies_absent=1')
            if ('LAYOUT_RETENTION_FAIL' in output or expected not in output
                    or (recovered == 1 and 'LAYOUT_RECLAMATION_OLD_BODY V=10 absent=1 current=12' not in output)
                    or (recovered == 1 and 'LAYOUT_RECLAMATION_RETIRED head_absent=1 bodies_absent=1' not in output)):
                raise AssertionError(output[-10000:])
            experiment.record('layout_retention_verified', recovered=recovered, evidence=output)
            if recovered < 2:
                experiment.connection.close()
                experiment.connection = None
                experiment.proc.kill()
                experiment.proc.wait(timeout=15)
                experiment.record('crash_for_recovery', pid=experiment.proc.pid)
        experiment.record('PASS', case='layout_retention', no_old_reader=True,
                          mini_minor=True, crash_recovery=True, release_after_completion=True,
                          paused_broadcast_without_freeze_row=True, concurrent_reader_handoff=True,
                          unreferenced_body_gc=True, retired_layout_gc=True, reclamation_crash_recovery=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    main()
