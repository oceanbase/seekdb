#!/usr/bin/env python3
"""Cold aggregate, partition and fulltext/LOB reads through prepared access."""
import argparse
import resource

from fork_parent_truncate_probe import (BootstrapExperiment, connect, namespace_id,
                                       physical_id, physical_state)


def run(binary):
    experiment = BootstrapExperiment(binary, 'access_reads', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE access_repro')
        experiment.sql('CREATE TABLE access_repro.parts(id INT PRIMARY KEY,v INT) PARTITION BY HASH(id) PARTITIONS 4')
        experiment.sql('INSERT INTO access_repro.parts VALUES(1,10),(2,20),(3,30),(4,40)')
        experiment.sql('CREATE TABLE access_repro.docs(id INT PRIMARY KEY,body LONGTEXT)')
        experiment.sql("INSERT INTO access_repro.docs VALUES(1,CONCAT('alpha ',REPEAT('word ',2200))),(2,'beta text')")
        experiment.sql('CREATE FULLTEXT INDEX body_ft ON access_repro.docs(body)')
        logical = [int(row[0]) for row in experiment.sql(
            "SELECT tablet_id FROM oceanbase.__all_table WHERE database_id="
            "(SELECT database_id FROM oceanbase.__all_database WHERE database_name='access_repro') "
            "AND tablet_id>0", log=False)]
        experiment.sql('FORK NAMESPACE access_reads FROM ns1')
        ns = namespace_id(experiment, 'access_reads')
        experiment.sql('INSERT INTO access_repro.parts VALUES(5,50)')
        experiment.sql("INSERT INTO access_repro.docs VALUES(3,'alpha later')")
        with connect(experiment, 'root@access_reads') as child:
            assert not physical_state(experiment, [physical_id(ns, tablet) for tablet in logical])
            assert experiment.sql('SELECT COUNT(*),SUM(v),MIN(v),MAX(v) FROM access_repro.parts', child) == ((4,100,10,40),)
            assert experiment.sql('SELECT id,v FROM access_repro.parts ORDER BY id', child) == ((1,10),(2,20),(3,30),(4,40))
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('alpha')", child) == ((1,),)
            assert experiment.sql('SELECT id,LENGTH(body) FROM access_repro.docs ORDER BY id', child) == ((1,11006),(2,9))
            assert not physical_state(experiment, [physical_id(ns, tablet) for tablet in logical])
            experiment.record('case_pass', case='cold_reads_without_materialization')
            experiment.sql("UPDATE access_repro.docs SET body='gamma child' WHERE id=1", child)
            experiment.sql('DELETE FROM access_repro.docs WHERE id=2', child)
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('gamma')", child) == ((1,),)
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('alpha')", child) == ()
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('beta')", child) == ()
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('alpha') ORDER BY id") == ((1,),(3,))
            assert experiment.sql("SELECT id FROM access_repro.docs WHERE MATCH(body) AGAINST('beta')") == ((2,),)
            experiment.record('case_pass', case='inherited_fulltext_update_delete')
        experiment.record('pass')
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
