#!/usr/bin/env python3
"""Native layout history: MVCC, chunk shrink, abort, dump and crash recovery."""
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
    os.environ['SEEKDB_LAYOUT_HISTORY_PROBE'] = '1'
    experiment = BootstrapExperiment(args.binary, 'storage_schema_history', prototype=6)
    experiment.extra_parameters = [('minor_compact_trigger', '2')]
    try:
        for recovered in (0, 1):
            cursors = {log.stat().st_ino: log.stat().st_size
                       for log in (experiment.base / 'log').glob('seekdb.log*')}
            experiment.start()
            output = ''
            for log in (experiment.base / 'log').glob('seekdb.log*'):
                with log.open('rb') as stream:
                    stream.seek(cursors.get(log.stat().st_ino, 0))
                    output += '\n'.join(line.decode(errors='replace') for line in stream
                                        if b'LAYOUT_HISTORY_' in line)
            expected = f'LAYOUT_HISTORY_PASS recovered={recovered} '
            if 'LAYOUT_HISTORY_FAIL' in output or expected not in output:
                raise AssertionError(output[-10000:])
            experiment.record('layout_history_verified', recovered=recovered)
            if not recovered:
                experiment.connection.close()
                experiment.connection = None
                experiment.proc.kill()
                experiment.proc.wait(timeout=15)
                experiment.record('crash_for_recovery', pid=experiment.proc.pid)
        experiment.record('PASS', case='storage_schema_history', chunks=True, shrink=True,
                          native_mvcc=True, rollback=True, dump=True, minor=True, shared_sql_tx=True, crash_recovery=True,
                          physical_birth_codec=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    main()
