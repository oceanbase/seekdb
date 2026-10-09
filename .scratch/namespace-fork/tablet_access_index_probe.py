#!/usr/bin/env python3
"""Build indexes on cold inherited sources after removing early materialization."""
import argparse
import resource

from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary):
    experiment = BootstrapExperiment(binary, 'access_indexes', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE access_indexes')
        experiment.sql('CREATE TABLE access_indexes.base(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO access_indexes.base VALUES(1,10),(2,20)')
        experiment.sql('CREATE TABLE access_indexes.docs(body LONGTEXT)')
        experiment.sql("INSERT INTO access_indexes.docs VALUES('alpha text'),('beta text')")
        experiment.sql('CREATE TABLE access_indexes.vectors(id INT PRIMARY KEY, v VECTOR(2048))')
        values = '[' + ','.join(['1'] * 2048) + ']'
        experiment.sql("INSERT INTO access_indexes.vectors VALUES(1,'" + values + "')", log=False)
        experiment.sql('FORK NAMESPACE access_indexes FROM ns1')
        experiment.sql('UPDATE access_indexes.base SET v=99 WHERE id=1')
        experiment.sql("INSERT INTO access_indexes.docs VALUES('alpha later')")
        with connect(experiment, 'root@access_indexes') as child:
            experiment.sql('CREATE INDEX by_v ON access_indexes.base(v)', child)
            assert experiment.sql('SELECT id,v FROM access_indexes.base FORCE INDEX(by_v) ORDER BY id', child) == ((1,10),(2,20))
            experiment.sql('CREATE FULLTEXT INDEX body_ft ON access_indexes.docs(body)', child)
            assert experiment.sql("SELECT body FROM access_indexes.docs WHERE MATCH(body) AGAINST('alpha')", child) == (('alpha text',),)
            experiment.sql("UPDATE /*+ PARALLEL(2) */ access_indexes.docs SET body='gamma text' WHERE body='alpha text'", child)
            assert experiment.sql("SELECT body FROM access_indexes.docs WHERE MATCH(body) AGAINST('alpha')", child) == ()
            assert experiment.sql("SELECT body FROM access_indexes.docs WHERE MATCH(body) AGAINST('gamma')", child) == (('gamma text',),)
            experiment.sql("DELETE /*+ PARALLEL(2) */ FROM access_indexes.docs WHERE body='beta text'", child)
            assert experiment.sql('SELECT COUNT(*) FROM access_indexes.docs', child) == ((1,),)
            experiment.sql("UPDATE access_indexes.docs SET body='alpha text'", child)
            experiment.sql("INSERT INTO access_indexes.docs VALUES('beta text')", child)
            assert experiment.sql('SELECT COUNT(*) FROM access_indexes.docs') == ((3,),)
            experiment.record('case_pass', case='explicit_das_fulltext_context')
            experiment.sql('CREATE VECTOR INDEX by_vector ON access_indexes.vectors(v) WITH(distance=l2,type=ivf_flat,nlist=1,sample_per_nlist=1)', child)
            query = "SELECT id FROM access_indexes.vectors ORDER BY l2_distance(v,'" + values + "') APPROXIMATE LIMIT 1"
            assert experiment.sql(query, child, log=False) == ((1,),)
            experiment.record('case_pass', case='cold_inherited_index_build', normal=True, fulltext=True, vector_2048=True)
        experiment.connection.close()
        experiment.connection = None
        experiment.proc.terminate()
        experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, 'root@access_indexes') as child:
            assert experiment.sql('SELECT id,v FROM access_indexes.base FORCE INDEX(by_v) ORDER BY id', child) == ((1,10),(2,20))
            assert experiment.sql("SELECT body FROM access_indexes.docs WHERE MATCH(body) AGAINST('alpha')", child) == (('alpha text',),)
            assert experiment.sql(query, child, log=False) == ((1,),)
        assert experiment.sql('SELECT v FROM access_indexes.base WHERE id=1') == ((99,),)
        experiment.record('pass', restart=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
