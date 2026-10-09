#!/usr/bin/env python3
"""Unrelated namespace DROP must finish while another namespace still scans."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import resource
import time

from fork_parent_truncate_probe import (BootstrapExperiment, connect, namespace_id,
                                       physical_id, physical_state, tablet_id)


from fork_history_gc_probe import drop_namespace


def run(binary):
    experiment = BootstrapExperiment(binary, 'access_isolation', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO nstrunc_repro.t VALUES(1,10),(2,20),(3,30)')
        logical = tablet_id(experiment, 't')
        experiment.sql('FORK NAMESPACE source_ns FROM ns1')
        source_ns = namespace_id(experiment, 'source_ns')
        with connect(experiment, 'root@source_ns') as source:
            experiment.sql('UPDATE nstrunc_repro.t SET v=v+100', source)
        experiment.sql('FORK NAMESPACE reader_ns FROM source_ns')
        experiment.sql('FORK NAMESPACE unrelated_ns FROM ns1')
        with connect(experiment, 'root@reader_ns') as reader:
            experiment.sql('SET ob_query_timeout=30000000', reader)
            experiment.sql('SET ob_query_timeout=2000000')
            with ThreadPoolExecutor(max_workers=1) as pool:
                query = pool.submit(experiment.sql,
                    'SELECT id,SLEEP(3) FROM nstrunc_repro.t ORDER BY id', reader)
                # Wait until SQL is actively executing; SLEEP runs after opening
                # the table scan and the query retains the iterator until completion.
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    rows = experiment.sql('SHOW FULL PROCESSLIST', log=False)
                    if any('SELECT id,SLEEP(3)' in str(row) for row in rows):
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError('reader never entered execution')
                time.sleep(.5)
                assert not query.done()
                start = time.monotonic()
                experiment.sql('DROP NAMESPACE unrelated_ns')
                elapsed = time.monotonic() - start
                assert not query.done(), 'DROP waited for the unrelated reader'
                assert elapsed < 2, elapsed
                experiment.record('case_pass', case='unrelated_drop_during_scan', elapsed=elapsed)
                experiment.sql('DROP NAMESPACE source_ns')
                assert not query.done(), 'logical parent DROP waited for a descendant reader'
                physical = physical_state(experiment, [physical_id(source_ns, logical)])
                assert physical and physical[0][-1] == 0, physical
                experiment.record('case_pass', case='source_retained_during_parent_drop', physical=physical)
                assert query.result(timeout=15) == ((1,0),(2,0),(3,0))
            assert experiment.sql('SELECT id,v FROM nstrunc_repro.t ORDER BY id', reader) == ((1,110),(2,120),(3,130))
        drop_namespace(experiment, 'reader_ns')
        experiment.record('pass')
    finally:
        experiment.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary)
