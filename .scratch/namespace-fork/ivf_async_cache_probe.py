import sys
import time
import re

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'ivf_async_cache', prototype=6)
pq = len(sys.argv) > 2 and sys.argv[2] == 'pq'
try:
    experiment.start()
    experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
    experiment.sql('CREATE NAMESPACE ivf_async_child')
    namespace_id = experiment.sql("SELECT namespace_id FROM __fork_proto_meta.namespaces "
                                  "WHERE name='ivf_async_child'")[0][0]
    with connect(experiment, 'root@ivf_async_child') as child:
        experiment.sql('CREATE DATABASE vec', child)
        experiment.sql('CREATE TABLE vec.t(id INT PRIMARY KEY, embedding VECTOR(4))', child)
        values = ','.join(f"({i},'[{i},0,0,0]')" for i in range(1, 21 if pq else 7))
        experiment.sql('INSERT INTO vec.t VALUES ' + values, child)
        options = ('distance=l2,type=ivf_pq,nlist=2,sample_per_nlist=5,m=2' if pq else
                   'distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=5')
        experiment.sql('CREATE VECTOR INDEX idx ON vec.t(embedding) '
                       'WITH (' + options + ')', child)
        indexes = experiment.sql("SELECT table_name,tablet_id FROM oceanbase.__all_table "
                                 "WHERE table_type=5 AND table_id>=500000", child)
        experiment.record('indexes', namespace_id=namespace_id, rows=indexes)
    for tick in range(5):
        rows = experiment.sql('SELECT rowkey_vid_tablet_id,statistics FROM '
                              'oceanbase.__all_virtual_vector_index_info', log=False)
        experiment.record('cache', tick=tick, rows=rows)
        if tick != 4:
            time.sleep(10)
    assert any(row[0] >> 32 & ((1 << 30) - 1) == namespace_id for row in rows), rows
    assert any('count=2' in row[1] for row in rows), rows
    if pq:
        assert any(re.search(r'cache_type=1;[^}]*count=40;', row[1]) for row in rows), rows
    with connect(experiment, 'root@ivf_async_child') as child:
        experiment.sql('SELECT id FROM vec.t ORDER BY '
                       'l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1', child)
    experiment.record('cache_after_query', rows=experiment.sql(
        'SELECT rowkey_vid_tablet_id,statistics FROM '
        'oceanbase.__all_virtual_vector_index_info', log=False))
    experiment.record('PASS', case='ivf_async_cache', pq=pq)
finally:
    experiment.close()
