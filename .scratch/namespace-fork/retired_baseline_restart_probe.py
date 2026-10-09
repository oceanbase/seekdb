#!/usr/bin/env python3
"""A retired intermediate physical copy must finish takeover after restart."""
import argparse
import resource
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id, physical_state
from fork_detached_restart_probe import wait_for


def run(binary):
    exp = BootstrapExperiment(binary, 'retired_baseline_restart', prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '5m')]
    parent = child = None
    try:
        exp.start()
        exp.sql('CREATE DATABASE retired')
        exp.sql('CREATE TABLE retired.t(id INT PRIMARY KEY,v INT,body LONGTEXT,KEY iv(v))')
        exp.sql("INSERT INTO retired.t VALUES(1,10,REPEAT('retained',10000)),(2,20,REPEAT('second',10000))")
        schemas = exp.sql("SELECT table_name,table_id,tablet_id FROM oceanbase.__all_table WHERE database_id=(SELECT database_id FROM oceanbase.__all_database WHERE database_name='retired') AND tablet_id>0")
        logical = [int(row[2]) for row in schemas]
        exp.sql('FORK NAMESPACE retired_parent FROM ns1')
        parent_ns = namespace_id(exp, 'retired_parent')
        parent = connect(exp, 'root@retired_parent')
        exp.sql('UPDATE retired.t SET v=v+1', parent)
        exp.sql('FORK NAMESPACE retired_child FROM retired_parent')
        child_ns = namespace_id(exp, 'retired_child')
        child = connect(exp, 'root@retired_child')
        exp.sql('UPDATE retired.t SET v=v+1', child)
        expected = ((1,12,80000),(2,22,60000))
        def verify(conn):
            assert exp.sql('SELECT id,v,LENGTH(body) FROM retired.t ORDER BY id',conn,log=False) == expected
            assert exp.sql('SELECT id,v FROM retired.t FORCE INDEX(iv) WHERE v>0 ORDER BY id',conn,log=False) == ((1,12),(2,22))
        verify(child)
        parent.close(); parent = None
        wait_for(lambda: drop_parent(exp), 10, 'parent close drain')
        exp.sql('DROP TABLE retired.t')
        sources = [physical_id(ns, tablet) for ns in (1,parent_ns) for tablet in logical]
        before = physical_state(exp,sources)
        assert before, 'intermediate sources already reclaimed before restart'
        exp.record('retired_sources_before_restart',physical=before, schemas=schemas)
        child.close(); child = None
        exp.connection.close(); exp.connection = None
        exp.proc.kill(); exp.proc.wait(timeout=15)
        exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
        exp.start()
        exp.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")
        child = connect(exp, 'root@retired_child')
        verify(child)
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        def reclaimed():
            verify(child)
            return not physical_state(exp,sources)
        wait_for(reclaimed,180,'retired intermediate baseline after restart')
        exp.sql('UPDATE retired.t SET v=v+10 WHERE id=1',child)
        assert exp.sql('SELECT v FROM retired.t WHERE id=1',child) == ((22,),)
        exp.record('PASS',case='retired_baseline_restart',intermediate_namespace_deleted=True,source_table_dropped=True,sources_reclaimed=True)
    finally:
        for conn in (parent,child):
            if conn is not None and conn.open: conn.close()
        exp.close()


def drop_parent(exp):
    import pymysql
    try:
        exp.sql('DROP NAMESPACE retired_parent')
        return True
    except pymysql.OperationalError as error:
        if 'active connections' not in str(error).lower(): raise
        return False

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True)
    args=parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    run(args.binary)
