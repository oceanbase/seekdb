#!/usr/bin/env python3
"""Fork shares capped roots; no canonical snapshot/pin keeps obsolete roots alive."""
import argparse
import resource
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata
from namespace_inprocess_prototype import drop_after_client_close

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', required=True)
a = p.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
e = BootstrapExperiment(a.binary, 'source_roots_only', prototype=6)
b = c = None
try:
    e.start()
    e.sql('CREATE DATABASE root_graph')
    e.sql('CREATE TABLE root_graph.t(id INT PRIMARY KEY, v INT)')
    e.sql('INSERT INTO root_graph.t VALUES(1,10)')
    e.sql('FORK NAMESPACE roots_b FROM ns1')
    e.sql('FORK NAMESPACE roots_c FROM roots_b')
    rows = {v['name']: v for _, v in metadata(e, 1)}
    first, second = rows['roots_b'], rows['roots_c']
    for tree in ('catalog', 'directory'):
        assert first[tree + '_page'] == second[tree + '_page'], (first, second)
        assert first[tree + '_cap'] == second[tree + '_cap'] == first['fork_cap'], (first, second)
    assert second['fork_cap'] >= first['fork_cap']
    assert metadata(e, 3) == [] and metadata(e, 8) == [], 'obsolete snapshot/pin records remain'
    assert all('snapshot_ref' not in v and 'ref_count' not in v and 'parent_ref' not in v
               for v in rows.values()), rows
    b, c = connect(e, 'root@roots_b'), connect(e, 'root@roots_c')
    e.sql('TRUNCATE TABLE root_graph.t', b)
    b.close(); b = None
    drop_after_client_close(e, 'roots_b')
    e.sql('DROP TABLE root_graph.t')
    assert e.sql('SELECT * FROM root_graph.t', c) == ((1, 10),)
    e.sql('UPDATE root_graph.t SET v=11', c)
    assert e.sql('SELECT * FROM root_graph.t', c) == ((1, 11),)
    e.record('PASS', case='shared_capped_roots_without_lineage', obsolete_collections=[],
             parent_deleted=True, descendant_read_write=True)
finally:
    for conn in (b, c):
        if conn is not None: conn.close()
    e.close()
