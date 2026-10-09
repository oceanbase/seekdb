#!/usr/bin/env python3
"""8000-partition metadata DDL must publish no tablet-source changes or lookups."""
import argparse
import os
import re
import resource
import time
from pathlib import Path
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id
from ddl_catalog_atomic_probe import roots

PATTERN = re.compile(r'DDL_PUBLICATION_COST namespace=(\d+) version=(\d+) definitions=(\d+) sources=(\d+) physical_status=(\d+) bindings=(\d+) ret=(-?\d+)')


def run(binary, partitions):
    exp = BootstrapExperiment(binary, 'ddl_publication_cost', prototype=6)
    exp.extra_parameters = [('memory_budget', '8G'), ('datafile_size', '512M'),
                            ('datafile_maxsize', '2G'), ('ob_compaction_schedule_interval', '5m')]
    previous = os.environ.get('SEEKDB_TEST_DDL_COST')
    os.environ['SEEKDB_TEST_DDL_COST'] = '1'
    try:
        exp.start()
        exp.connection._read_timeout = 300
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql('SET ob_trx_timeout=300000000')
        exp.sql('CREATE DATABASE publication_cost')
        exp.sql(f'CREATE TABLE publication_cost.t(id INT PRIMARY KEY,v INT) PARTITION BY HASH(id) PARTITIONS {partitions}')
        exp.sql('INSERT INTO publication_cost.t VALUES(1,10)')
        exp.sql('FORK NAMESPACE cost_child FROM ns1')
        with connect(exp, 'root@cost_child') as child:
            child._read_timeout = 300
            exp.sql('SET ob_query_timeout=300000000', child)
            for name, conn in (('ns1', exp.connection), ('cost_child', child)):
                owner = namespace_id(exp, name)
                for statement in ("ALTER TABLE publication_cost.t COMMENT='description only'",
                                  'ALTER TABLE publication_cost.t ADD COLUMN extra INT DEFAULT 7'):
                    old = roots(exp, owner)
                    started = time.monotonic()
                    exp.sql(statement, conn)
                    state = roots(exp, owner)
                    rows = []
                    # seekdb redirects stderr to its runtime log after startup.
                    for output in (exp.base / 'log').glob('seekdb.log*'):
                        if not output.is_file(): continue
                        with output.open(errors='replace') as stream:
                            for line in stream:
                                if 'DDL_PUBLICATION_COST' in line:
                                    rows.extend(tuple(map(int, row)) for row in PATTERN.findall(line))
                    match = [row for row in rows if row[0] == owner and row[1] == int(state['schema_version'])]
                    assert len(match) == 1, ('publication counters missing: requires local hook', rows, state)
                    ns, version, definitions, sources, physical, bindings, ret = match[0]
                    assert ret == 0 and definitions >= 1 and sources == 0 and physical == 0, match
                    assert bindings >= partitions, match
                    assert state['schema_version'] > old['schema_version']
                    if name == 'ns1':
                        assert state['directory_page'] == old['directory_page'], (old, state)
                    exp.record('publication_cost', owner=name, statement=statement, partitions=partitions,
                               seconds=time.monotonic()-started, definitions=definitions,
                               source_updates=sources, physical_status_lookups=physical, bindings=bindings)
                assert exp.sql('SELECT * FROM publication_cost.t WHERE id=1', conn) == ((1, 10, 7),)
            exp.sql('UPDATE publication_cost.t SET v=11 WHERE id=1', child)
            assert exp.sql('SELECT * FROM publication_cost.t WHERE id=1', child) == ((1, 11, 7),)
            assert exp.sql('SELECT * FROM publication_cost.t WHERE id=1') == ((1, 10, 7),)
        exp.record('PASS', case='ddl_publication_cost', partitions=partitions, owners=2, statements=4)
    finally:
        exp.close()
        if previous is None: os.environ.pop('SEEKDB_TEST_DDL_COST', None)
        else: os.environ['SEEKDB_TEST_DDL_COST'] = previous


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=8000)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.partitions)
