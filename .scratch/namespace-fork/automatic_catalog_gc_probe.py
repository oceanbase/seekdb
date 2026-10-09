#!/usr/bin/env python3
"""Real timer reclaims COW pages while an RR reader retains an old source root."""
import argparse
import json
import resource
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id
from ddl_catalog_atomic_probe import roots


def page_ids(experiment):
    rows = experiment.sql('SELECT key_json FROM oceanbase.__all_virtual_instance_metadata '
                          'WHERE collection_id=5', log=False)
    return {int(json.loads(row[0])['page_id']) for row in rows}


def run(binary):
    experiment = BootstrapExperiment(binary, 'automatic_catalog_gc', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE gc_probe')
        experiment.sql('CREATE TABLE gc_probe.anchor(id INT PRIMARY KEY)')
        experiment.sql('INSERT INTO gc_probe.anchor VALUES(1)')
        experiment.sql('CREATE TABLE gc_probe.t(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO gc_probe.t VALUES(1,7)')
        experiment.sql('FORK NAMESPACE gc_child FROM ns1')
        child_id = namespace_id(experiment, 'gc_child')
        with connect(experiment, 'root@gc_child') as reader, connect(experiment, 'root@gc_child') as writer:
            # Give this child's root its own COW path first. Immediately after
            # fork the same page is still owned by the parent's current root.
            experiment.sql('UPDATE gc_probe.anchor SET id=2', writer)
            experiment.sql('SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ', reader)
            experiment.sql('BEGIN', reader)
            assert experiment.sql('SELECT * FROM gc_probe.anchor', reader) == ((2,),)
            old_root = int(roots(experiment, child_id)['directory_page'])
            assert old_root != int(roots(experiment, 1)['directory_page'])
            # t was never opened by the reader. The writer publishes a new
            # physical source; the reader must still resolve its old root.
            experiment.sql('UPDATE gc_probe.t SET v=8', writer)
            assert int(roots(experiment, child_id)['directory_page']) != old_root
            for i in range(12):
                experiment.sql("ALTER TABLE gc_probe.t COMMENT='gc-%d'" % i, writer)
            before = len(page_ids(experiment))
            deadline = time.monotonic() + 60
            samples = []
            while time.monotonic() < deadline:
                ids = page_ids(experiment)
                samples.append(len(ids))
                if old_root not in ids:
                    break
                time.sleep(1)
            else:
                raise AssertionError(('old source root was not automatically collected', old_root, samples))
            assert experiment.sql('SELECT * FROM gc_probe.t', reader) == ((1,7),)
            assert experiment.sql('SELECT * FROM gc_probe.t', writer) == ((1,8),)
            experiment.sql('COMMIT', reader)
            assert experiment.sql('SELECT * FROM gc_probe.t', reader) == ((1,8),)
            experiment.record('old_view_survives_automatic_gc', old_root=old_root,
                              pages_before=before, pages_after=len(ids), samples=samples)
        experiment.sql('FORK NAMESPACE gc_grandchild FROM gc_child')
        experiment.sql('DROP NAMESPACE gc_child')
        with connect(experiment, 'root@gc_grandchild') as child:
            assert experiment.sql('SELECT * FROM gc_probe.t', child) == ((1,8),)
        experiment.proc.kill()
        experiment.proc.wait(timeout=15)
        experiment.connection.close()
        experiment.connection = None
        experiment.start()
        with connect(experiment, 'root@gc_grandchild') as child:
            assert experiment.sql('SELECT * FROM gc_probe.t', child) == ((1,8),)
        experiment.record('PASS', case='automatic_catalog_gc', old_view=True,
                          parent_drop=True, crash_restart=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary)
