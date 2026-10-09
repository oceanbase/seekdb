#!/usr/bin/env python3
"""SQL snapshot lifetime, late materialization and own writes through source roots."""
import argparse
import resource
import os
import re
from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary, instrumented=False, procedures=False):
    if instrumented:
        os.environ["SEEKDB_SQL_VIEW_PROBE"] = "1"
    experiment = BootstrapExperiment(binary, 'sql_read_view', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE views')
        experiment.sql('CREATE TABLE views.anchor(id INT PRIMARY KEY)')
        experiment.sql('INSERT INTO views.anchor VALUES(1)')
        experiment.sql('CREATE TABLE views.t(id INT PRIMARY KEY,v INT,b LONGTEXT) PARTITION BY HASH(id) PARTITIONS 8')
        experiment.sql("INSERT INTO views.t VALUES(1,10,REPEAT('a',12000)),(2,20,REPEAT('b',14000))")
        experiment.sql('FORK NAMESPACE view_child FROM ns1')
        with connect(experiment, 'root@view_child') as reader, connect(experiment, 'root@view_child') as writer:
            experiment.sql("SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ", reader)
            experiment.sql('BEGIN', reader)
            assert experiment.sql('SELECT * FROM views.anchor', reader) == ((1,),)
            # The transaction has selected its root, but has never opened t.
            experiment.sql('UPDATE views.t SET v=11 WHERE id=1', writer)
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM views.t ORDER BY id', reader) == ((1,10,12000),(2,20,14000))
            experiment.sql('UPDATE views.t SET v=23 WHERE id=2', reader)
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM views.t ORDER BY id', reader) == ((1,10,12000),(2,23,14000))
            assert experiment.sql('SELECT /*+ PARALLEL(4) */ SUM(v) FROM views.t', reader) == ((33,),)
            experiment.sql('ROLLBACK', reader)
            assert experiment.sql('SELECT id,v FROM views.t ORDER BY id', reader) == ((1,11),(2,20))
            experiment.record('case_pass', case='rr_late_other_materialization_own_write_rollback_px')

            experiment.sql("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED", reader)
            experiment.sql('BEGIN', reader)
            assert experiment.sql('SELECT v FROM views.t WHERE id=1', reader) == ((11,),)
            experiment.sql('UPDATE views.t SET v=12 WHERE id=1', writer)
            assert experiment.sql('SELECT v FROM views.t WHERE id=1', reader) == ((12,),)
            experiment.sql('COMMIT', reader)
            experiment.record('case_pass', case='rc_refresh_after_rr_reuse')

        experiment.sql('FORK NAMESPACE view_cold FROM ns1')
        with connect(experiment, 'root@view_cold') as child:
            experiment.sql('SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ', child)
            experiment.sql('BEGIN', child)
            assert experiment.sql('SELECT * FROM views.anchor', child) == ((1,),)
            experiment.sql('TRUNCATE TABLE views.t')
            experiment.sql("INSERT INTO views.t VALUES(3,30,'new-parent')")
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM views.t ORDER BY id', child) == ((1,10,12000),(2,20,14000))
            experiment.sql("UPDATE views.t SET v=15,b=REPEAT('z',16000) WHERE id=1", child)
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM views.t ORDER BY id', child) == ((1,15,16000),(2,20,14000))
            experiment.sql('COMMIT', child)
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM views.t ORDER BY id', child) == ((1,15,16000),(2,20,14000))
            experiment.record('case_pass', case='rr_late_parent_truncate_first_own_materialization_lob')
        experiment.sql('CREATE TABLE views.fk_parent(id INT PRIMARY KEY)')
        experiment.sql('CREATE TABLE views.fk_child(id INT PRIMARY KEY, p INT, CONSTRAINT fk_view FOREIGN KEY(p) REFERENCES views.fk_parent(id))')
        experiment.sql('INSERT INTO views.fk_parent VALUES(1)')
        experiment.sql('FORK NAMESPACE view_delayed FROM ns1')
        with connect(experiment, 'root@view_delayed') as child:
            experiment.sql('SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED', child)
            experiment.sql('INSERT INTO views.fk_child VALUES(1,1)', child)
            experiment.sql('INSERT INTO views.fk_child VALUES(1,1) ON DUPLICATE KEY UPDATE p=VALUES(p)', child)
            experiment.sql('REPLACE INTO views.fk_child VALUES(1,1)', child)
            assert experiment.sql('SELECT * FROM views.fk_child', child) == ((1,1),)
            if procedures:
                experiment.sql('CREATE PROCEDURE views.read_nested() BEGIN SELECT SUM(id) FROM views.fk_parent; SELECT SUM(p) FROM views.fk_child; END', child)
                with child.cursor() as cur:
                    cur.execute('CALL views.read_nested()')
                    assert cur.fetchall() == ((1,),)
                    assert cur.nextset()
                    assert cur.fetchall() == ((1,),)
                    while cur.nextset():
                        pass
            experiment.record('case_pass', case='delayed_fk_upsert_replace', procedures=procedures)
        if instrumented:
            trace = experiment.engine_log()
            scans = re.findall(r'SQL_VIEW_LOOKUP ns=(\d+) table=(\d+) snapshot=(-?\d+) found=(\d+)', trace)
            user_scans = [row for row in scans if int(row[1]) >= 500000 and int(row[1]) < (1 << 40)]
            assert user_scans, 'no user scan evidence'
            missing = [row for row in user_scans if row[3] != '1']
            experiment.record('route_evidence', user_scans=len(user_scans), missing=missing)
            assert not missing, missing
            assert 'SQL_VIEW_ROUTE ' in trace, 'new source route never used' 
        experiment.record('PASS', case='sql_read_view', rr=True, rc=True, px=True, own_writes=True, parent_truncate=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--instrumented', action='store_true')
    parser.add_argument('--procedures', action='store_true')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary, args.instrumented, args.procedures)
