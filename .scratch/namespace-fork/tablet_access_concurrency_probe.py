#!/usr/bin/env python3
"""Cold concurrent writers: no client retry may hide partial CREATE visibility."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import resource
import threading

from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary, rounds):
    experiment = BootstrapExperiment(binary, 'access_concurrency', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE access_repro')
        experiment.sql('CREATE TABLE access_repro.t(id BIGINT AUTO_INCREMENT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO access_repro.t VALUES(1,10)')
        for iteration in range(rounds):
            name = 'access_race_' + str(iteration)
            experiment.sql(f'FORK NAMESPACE {name} FROM ns1')
            barrier = threading.Barrier(2)
            def insert(value):
                with connect(experiment, 'root@' + name) as connection:
                    barrier.wait(timeout=15)
                    experiment.sql(f'INSERT INTO access_repro.t VALUES({value},{value})', connection)
            with ThreadPoolExecutor(max_workers=2) as pool:
                pending = [pool.submit(insert, value) for value in (20, 30)]
                for result in pending:
                    result.result(timeout=30)
            with connect(experiment, 'root@' + name) as connection:
                actual = experiment.sql('SELECT id,v FROM access_repro.t ORDER BY id', connection)
                assert actual == ((1, 10), (20, 20), (30, 30)), actual
            assert experiment.sql('SELECT id,v FROM access_repro.t') == ((1, 10),)
            experiment.record('case_pass', case='cold_concurrent_insert', iteration=iteration)
            experiment.sql(f'DROP NAMESPACE {name}')
        experiment.record('pass', rounds=rounds)
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--rounds', type=int, default=8)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.rounds)
