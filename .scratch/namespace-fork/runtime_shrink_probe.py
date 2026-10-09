#!/usr/bin/env python3
"""A queued request must survive retirement of the last idle request worker."""
import argparse
import os
from pathlib import Path
import queue
import resource
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment


def submit(exp, connection, sql):
    result = queue.Queue()
    def work():
        try:
            result.put(exp.sql(sql, connection, log=False))
        except Exception as exc:
            result.put(exc)
    thread = threading.Thread(target=work, daemon=True)
    thread.start()
    return thread, result


def run(binary):
    exp = BootstrapExperiment(binary, 'runtime_shrink', prototype=6)
    prefix = str(exp.base / 'runtime-shrink')
    os.environ['SEEKDB_RUNTIME_SHRINK_PROBE'] = prefix
    connections = []
    threads = []
    try:
        exp.start()
        busy = exp.connect()
        queued = exp.connect()
        connections.extend((busy, queued))
        exp.sql('SET ob_query_timeout=60000000', busy)
        busy._read_timeout = 70
        thread, busy_result = submit(exp, busy, 'SELECT SLEEP(40)')
        threads.append(thread)
        Path(prefix + '.arm').touch()
        deadline = time.monotonic() + 30
        while not Path(prefix + '.ready').exists():
            assert busy_result.empty(), 'blocking request ended before the race'
            if time.monotonic() >= deadline:
                raise AssertionError('last idle worker did not reach retirement hook')
            time.sleep(.02)
        started = time.monotonic()
        thread, result = submit(exp, queued, 'SELECT 42')
        threads.append(thread)
        try:
            rows = result.get(timeout=2)
        except queue.Empty:
            exp.record('FAIL', case='runtime_shrink', queued=Path(prefix + '.queued').exists(),
                       busy_still_running=busy_result.empty())
            raise AssertionError('request stranded after last idle worker retirement')
        latency_ms = (time.monotonic() - started) * 1000
        assert rows == ((42,),), rows
        assert busy_result.empty(), 'request only ran after the busy worker became available'
        assert Path(prefix + '.queued').exists(), 'enqueue race was not exercised'
        evidence = ''.join(line for log in (exp.base / 'log').glob('seekdb.log*')
                           for line in log.read_text(errors='replace').splitlines()
                           if 'RUNTIME_SHRINK_RACE ' in line)
        assert 'workers=2 idle=1 queued=' in evidence, evidence
        exp.record('PASS', case='runtime_shrink', latency_ms=latency_ms,
                   evidence=evidence)
    finally:
        if exp.proc and exp.proc.poll() is None:
            exp.proc.kill()
        for thread in threads:
            thread.join(timeout=3)
        for connection in connections:
            connection.close()
        exp.close()
        os.environ.pop('SEEKDB_RUNTIME_SHRINK_PROBE', None)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    args = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
