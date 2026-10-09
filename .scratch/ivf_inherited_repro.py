"""Disposable child IVF index on an inherited table; never stage this file."""
import pymysql
from namespace_worker_bootstrap_prototype import BootstrapExperiment

experiment = BootstrapExperiment('build_release/src/observer/seekdb',
                                 'ivf_inherited_repro', prototype=6)
child = None
try:
    experiment.start()
    experiment.sql('CREATE TABLE test.inherited_vec(id INT PRIMARY KEY, embedding VECTOR(4))')
    experiment.sql("INSERT INTO test.inherited_vec VALUES "
                   "(1,'[1,0,0,0]'),(2,'[2,0,0,0]'),(3,'[3,0,0,0]'),"
                   "(4,'[4,0,0,0]'),(5,'[5,0,0,0]'),(6,'[6,0,0,0]')")
    experiment.sql('FORK NAMESPACE inherited_index_child FROM ns1')
    child = pymysql.connect(host='127.0.0.1', port=experiment.port,
                            user='root@inherited_index_child', password='',
                            autocommit=True, connect_timeout=3, read_timeout=65)
    experiment.sql('SET ob_query_timeout=60000000', child)
    experiment.sql('CREATE VECTOR INDEX inherited_embedding ON test.inherited_vec(embedding) '
                   'WITH (distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=3)', child)
    rows = experiment.sql('SELECT id FROM test.inherited_vec ORDER BY '
                          'l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1', child)
    assert rows == ((1,),), rows
    print('IVF_INHERITED_PASS', rows, flush=True)
finally:
    if child is not None:
        child.close()
    experiment.close()
