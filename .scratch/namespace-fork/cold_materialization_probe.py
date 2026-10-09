#!/usr/bin/env python3
"""Create a main/LOB binding directly from KV before any child Runtime load."""
import argparse
import os
from pathlib import Path
import resource
from ddl_catalog_atomic_probe import roots, graph
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, tablet_id, physical_id


def run(binary):
    experiment = BootstrapExperiment(binary, 'cold_materialization', prototype=6)
    control = experiment.base / 'cold-materialize-control'
    os.environ['SEEKDB_COLD_MATERIALIZE_CONTROL'] = str(control)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id INT PRIMARY KEY,b LONGTEXT)')
        experiment.sql("INSERT INTO nstrunc_repro.t1 VALUES(1,REPEAT('x',12000))")
        logical = tablet_id(experiment, 't1')
        for corrupt in (0, 1):
            name = 'cold_source_' + str(corrupt)
            control.write_text(f'{name} {logical} {corrupt}\n')
            experiment.sql(f'FORK NAMESPACE {name} FROM ns1')
            control.unlink()
            ns = namespace_id(experiment, name)
            trace = (experiment.base / 'log/seekdb.log').read_text(errors='replace')
            assert f'COLD_MATERIALIZATION namespace={ns} tablet={logical} wrong_generation={corrupt} runtime_slots=0 ret=0' in trace
            sources = graph(experiment, roots(experiment, ns))
            main, cap = sources[logical]
            assert cap == 0 and all(main[3:])
            for member in main[3:]:
                value, cap = sources[member]
                assert value[1] == physical_id(ns, member) and cap == 0
                assert value[2] == main[2]
            with connect(experiment, 'root@' + name) as child:
                assert experiment.sql('SELECT id,LENGTH(b) FROM nstrunc_repro.t1', child) == ((1, 12000),)
                experiment.sql("UPDATE nstrunc_repro.t1 SET b=REPEAT('y',14000)", child)
                assert experiment.sql('SELECT id,LENGTH(b) FROM nstrunc_repro.t1', child) == ((1, 14000),)
        experiment.record('PASS', case='cold_catalog_materialization', runtime_slots_loaded=0,
                          wrong_incarnation_rejected=True, main_lob_atomic=True)
    finally:
        control.unlink(missing_ok=True)
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
