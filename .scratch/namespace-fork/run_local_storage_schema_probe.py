#!/usr/bin/env python3
"""Exercise native tablet schema installation, including an old merge completing late."""
import argparse
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    experiment = BootstrapExperiment(args.binary, 'local_storage_schema', prototype=6)
    env = os.environ.copy()
    env['SEEKDB_LOCAL_STORAGE_SCHEMA_PROBE'] = '1'
    command = [experiment.binary, '--nodaemon', '--base-dir=' + str(experiment.base),
               '-P' + str(experiment.port), '--log-level=INFO', '--parameter', 'memory_budget=2G',
               '--parameter', 'cpu_count=4', '--parameter', 'datafile_size=256M',
               '--parameter', 'datafile_maxsize=512M', '--parameter', 'log_disk_size=2G',
               '--parameter', 'max_syslog_file_count=16']
    try:
        experiment.proc = subprocess.Popen(command, env=env, stdout=experiment.output, stderr=subprocess.STDOUT)
        experiment.record('setup', base=experiment.base, pid=experiment.proc.pid, port=experiment.port)
        deadline = time.monotonic() + 60
        cursors = {}
        output = ''
        while time.monotonic() < deadline:
            for log in (experiment.base / 'log').glob('seekdb.log*'):
                with log.open('rb') as stream:
                    stream.seek(cursors.get(log.stat().st_ino, 0))
                    output += ''.join(line.decode(errors='replace') for line in stream if b'LOCAL_SCHEMA_' in line)
                    cursors[log.stat().st_ino] = stream.tell()
            if 'LOCAL_SCHEMA_FAIL' in output:
                raise AssertionError(output)
            if 'LOCAL_SCHEMA_PASS ' in output:
                experiment.record('PASS', case='local_storage_schema', evidence=output)
                return
            if experiment.proc.poll() is not None:
                raise RuntimeError(('process_exited', experiment.proc.returncode, output))
            time.sleep(.1)
        raise TimeoutError(('native_probe_not_completed', output))
    finally:
        experiment.close()


if __name__ == '__main__':
    main()
