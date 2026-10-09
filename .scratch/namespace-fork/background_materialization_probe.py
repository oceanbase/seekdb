#!/usr/bin/env python3
"""A never-opened child takes over main/index/LOB sources after parent DROP."""
import argparse
import resource
import time
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id, physical_state
from ddl_catalog_atomic_probe import roots, graph


def run(binary, index_families=False):
    exp = BootstrapExperiment(binary, 'background_materialization', prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
    child = None
    try:
        exp.start()
        exp.sql('CREATE DATABASE readonly_fork')
        exp.sql('CREATE TABLE readonly_fork.t(id INT PRIMARY KEY,v INT,body LONGTEXT,KEY iv(v))')
        exp.sql("INSERT INTO readonly_fork.t VALUES(1,10,REPEAT('a',12000)),(2,20,REPEAT('b',18000))")
        if index_families:
            exp.sql('CREATE TABLE readonly_fork.docs(id INT PRIMARY KEY,body TEXT)')
            exp.sql("INSERT INTO readonly_fork.docs VALUES(1,'alpha text'),(2,'beta text')")
            exp.sql('CREATE FULLTEXT INDEX ft ON readonly_fork.docs(body)')
            exp.sql('CREATE TABLE readonly_fork.vecs(id INT PRIMARY KEY,embedding VECTOR(4))')
            exp.sql("INSERT INTO readonly_fork.vecs VALUES(1,'[1,0,0,0]'),(2,'[2,0,0,0]')")
            exp.sql('CREATE VECTOR INDEX vi ON readonly_fork.vecs(embedding) '
                    'WITH(distance=l2,type=ivf_flat,nlist=1,sample_per_nlist=1)')
        tables = exp.sql("SELECT table_id,tablet_id FROM oceanbase.__all_table WHERE database_id=(SELECT database_id FROM oceanbase.__all_database WHERE database_name='readonly_fork') AND tablet_id>0")
        logical = {int(row[1]) for row in tables}
        exp.sql('FORK NAMESPACE never_opened FROM ns1')
        ns = namespace_id(exp, 'never_opened')
        sources = graph(exp, roots(exp, ns))
        for tablet in list(logical):
            logical.update(value for value in sources[tablet][0][3:] if value)
        inherited = [sources[tablet][0][1] for tablet in sorted(logical)]
        owned = [physical_id(ns, tablet) for tablet in sorted(logical)]
        assert not physical_state(exp, owned), 'fixture must start with a cold child'
        exp.sql('DROP TABLE readonly_fork.t')
        if index_families:
            exp.sql('DROP TABLE readonly_fork.docs')
            exp.sql('DROP TABLE readonly_fork.vecs')
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        started = time.monotonic()
        deadline = started + 420
        last_report = 0
        while time.monotonic() < deadline:
            local = physical_state(exp, owned)
            parents = physical_state(exp, inherited)
            if len(local) == len(owned) and not parents:
                break
            if time.monotonic() - last_report > 15:
                exp.record('background_progress', elapsed_s=round(time.monotonic()-started,2),
                           child_tablets=len(local), expected=len(owned), remaining_sources=parents)
                last_report = time.monotonic()
            time.sleep(1)
        else:
            raise AssertionError(('never-opened child did not release sources', local, parents))
        exp.record('cold_takeover_done', namespace=ns, elapsed_s=round(time.monotonic()-started,2),
                   physical=local, inherited_sources_reclaimed=True)
        activation = f'PROTOTYPE_INPROCESS_NS_ACTIVATE ns={ns} '
        def trace():
            paths = list((exp.base / 'log').glob('seekdb.log*')) + [exp.base / 'process.out']
            return '\n'.join(path.read_text(errors='replace') for path in paths if path.is_file())
        assert activation not in trace(), 'background work activated the child SQL Runtime'
        child = connect(exp, 'root@never_opened')
        assert exp.sql('SELECT id,v,LENGTH(body) FROM readonly_fork.t ORDER BY id',child) == ((1,10,12000),(2,20,18000))
        assert exp.sql('SELECT id,v FROM readonly_fork.t FORCE INDEX(iv) WHERE v>0 ORDER BY id',child) == ((1,10),(2,20))
        def verify_index_families():
            if index_families:
                assert exp.sql("SELECT id FROM readonly_fork.docs WHERE MATCH(body) AGAINST('alpha')",child) == ((1,),)
                assert exp.sql('SELECT id FROM readonly_fork.vecs ORDER BY '
                               'l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1',child) == ((1,),)
        verify_index_families()
        assert activation in trace(), 'activation instrumentation missing'
        child.close(); child = None
        exp.connection.close(); exp.connection = None
        exp.proc.kill(); exp.proc.wait(timeout=15)
        exp.start()
        child = connect(exp, 'root@never_opened')
        assert exp.sql('SELECT id,v,LENGTH(body) FROM readonly_fork.t ORDER BY id',child) == ((1,10,12000),(2,20,18000))
        verify_index_families()
        exp.record('PASS',case='background_readonly_materialization',no_child_connection_before_takeover=True,sql_runtime_unloaded=True,
                   parent_dropped=True,main_index_lob=True,sources_reclaimed=True,restart=True,fulltext_and_vector=index_families)
    finally:
        if child is not None and child.open: child.close()
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--index-families',action='store_true')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary,args.index_families)
