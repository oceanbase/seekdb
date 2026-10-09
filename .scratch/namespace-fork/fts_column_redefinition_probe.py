#!/usr/bin/env python3
"""Minimize direct-gate FTS column redefinition Item not match failure."""
import argparse
import resource
from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary):
    experiment = BootstrapExperiment(binary, 'fts_columns', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE NAMESPACE fts_columns')
        with connect(experiment, 'root@fts_columns') as child:
            experiment.sql('CREATE DATABASE phase10', child)
            for i in range(5):
                table = 'phase10.t' + str(i)
                experiment.sql('CREATE TABLE ' + table + '(id INT PRIMARY KEY,body TEXT,v INT)', child)
                experiment.sql("INSERT INTO " + table + " VALUES(1,'alpha text',7),(2,'beta text',8)", child)
                experiment.sql('CREATE FULLTEXT INDEX body_ft ON ' + table + '(body)', child)
                experiment.sql('ALTER TABLE ' + table + ' DROP COLUMN v, ADD COLUMN w INT DEFAULT 4', child)
                assert experiment.sql("SELECT id,w FROM " + table + " WHERE MATCH(body) AGAINST('alpha')", child) == ((1,4),)
                experiment.sql('DROP TABLE ' + table, child)
                experiment.record('case_pass', iteration=i)
        experiment.record('pass')
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
