#!/usr/bin/env python3
"""Real 8000-partition SQL fork, constant root sharing and one-partition COW."""
import argparse
import json
import resource
import time

from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, namespace_id
from ddl_catalog_atomic_probe import roots, graph


def inventory(exp):
    rows = exp.sql('SELECT collection_id,key_json FROM '
                   'oceanbase.__all_virtual_instance_metadata', log=False)
    keys = {}
    for collection, key in rows:
        keys.setdefault(int(collection), set()).add(key)
    physical = int(exp.sql('SELECT COUNT(*) FROM oceanbase.__all_virtual_tablet_info', log=False)[0][0])
    return keys, physical


def run(binary, partitions):
    exp = BootstrapExperiment(binary, 'large_partition_fork', prototype=6)
    exp.extra_parameters = [('memory_budget', '8G'), ('datafile_size', '512M'),
                            ('datafile_maxsize', '2G'), ('ob_compaction_schedule_interval', '5m')]
    try:
        exp.start()
        exp.connection._read_timeout = 300
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql('SET ob_trx_timeout=300000000')
        exp.sql('CREATE DATABASE large_fork')
        started = time.monotonic()
        exp.sql('CREATE TABLE large_fork.t(id INT PRIMARY KEY,v INT) '
                'PARTITION BY HASH(id) PARTITIONS %d' % partitions)
        create_seconds = time.monotonic() - started
        exp.sql('INSERT INTO large_fork.t VALUES(1,10)')
        table = int(exp.sql("SELECT table_id FROM oceanbase.__all_table WHERE "
                            "table_name='t' AND database_id=(SELECT database_id FROM "
                            "oceanbase.__all_database WHERE database_name='large_fork')", log=False)[0][0])
        assert int(exp.sql('SELECT COUNT(*) FROM oceanbase.__all_part WHERE table_id=%d' % table,
                           log=False)[0][0]) == partitions
        source_root = roots(exp, 1)
        samples = []
        for i in range(3):
            before, physical_before = inventory(exp)
            started = time.monotonic()
            name = 'large_child_%d' % i
            exp.sql('FORK NAMESPACE %s FROM ns1' % name)
            duration = time.monotonic() - started
            child_id = namespace_id(exp, name)
            child_root = roots(exp, child_id)
            after, physical_after = inventory(exp)
            added = {key: len(value - before.get(key, set())) for key, value in after.items()}
            # The timer can materialize a bounded batch during observation.
            # Neither the request nor that batch may clone the whole table.
            assert added.get(1, 0) == 1 and added.get(2, 0) == 1, added
            assert added.get(5, 0) < 128 and physical_after - physical_before < 128, (added, physical_before, physical_after)
            assert child_root['catalog_page'] == source_root['catalog_page'], (source_root, child_root)
            assert int(child_root['catalog_cap']) > 0
            sample = dict(namespace=child_id, seconds=duration, added=added,
                          physical_delta=physical_after - physical_before,
                          directory_shared=child_root['directory_page'] == source_root['directory_page'])
            samples.append(sample)
            exp.record('fork_cost', **sample)
        with connect(exp, 'root@large_child_0') as child:
            child._read_timeout = 300
            exp.sql('SET ob_query_timeout=300000000', child)
            assert exp.sql('SELECT * FROM large_fork.t WHERE id=1', child) == ((1, 10),)
            child_id = namespace_id(exp, 'large_child_0')
            old = graph(exp, roots(exp, child_id))
            before, physical_before = inventory(exp)
            started = time.monotonic()
            exp.sql('UPDATE large_fork.t SET v=11 WHERE id=1', child)
            update_seconds = time.monotonic() - started
            new = graph(exp, roots(exp, child_id))
            after, physical_after = inventory(exp)
            changed = [key for key, value in old.items() if value[0][0] == table and new[key] != value]
            assert len(changed) == 1, changed
            assert len(after.get(5, set()) - before.get(5, set())) < 128
            assert exp.sql('SELECT * FROM large_fork.t WHERE id=1', child) == ((1, 11),)
            assert exp.sql('SELECT * FROM large_fork.t WHERE id=1') == ((1, 10),)
            exp.record('single_partition_cow', changed=changed, seconds=update_seconds,
                       new_pages=len(after.get(5, set()) - before.get(5, set())),
                       physical_delta=physical_after - physical_before)
        exp.record('PASS', case='large_partition_fork', partitions=partitions,
                   create_seconds=create_seconds, forks=samples)
    finally:
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=8000)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.partitions)
