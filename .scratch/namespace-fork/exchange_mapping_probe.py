#!/usr/bin/env python3
"""Local regression for table rebinding during forked-namespace EXCHANGE PARTITION."""
import argparse
from pathlib import Path
import resource
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect


def run(binary):
    experiment = BootstrapExperiment(binary, 'exchange_mapping', prototype=6)
    child = None
    try:
        experiment.start()
        experiment.sql('FORK NAMESPACE exchange_child FROM ns1')
        child = connect(experiment, 'root@exchange_child')
        experiment.sql('CREATE DATABASE x', child)
        experiment.sql('CREATE TABLE x.parts(id INT PRIMARY KEY, v INT) '
                       'PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (10), '
                       'PARTITION p1 VALUES LESS THAN (MAXVALUE))', child)
        experiment.sql('CREATE TABLE x.plain(id INT PRIMARY KEY, v INT)', child)
        experiment.sql('INSERT INTO x.parts VALUES(1,11),(11,111)', child)
        experiment.sql('INSERT INTO x.plain VALUES(2,22)', child)
        experiment.sql('ALTER TABLE x.parts EXCHANGE PARTITION p0 '
                       'WITH TABLE x.plain WITHOUT VALIDATION', child)
        assert experiment.sql('SELECT id,v FROM x.parts ORDER BY id', child) == ((2, 22), (11, 111))
        assert experiment.sql('SELECT id,v FROM x.plain', child) == ((1, 11),)
        experiment.record('PASS', case='exchange_mapping')
    finally:
        if child is not None:
            child.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
