import sys
sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect
experiment = BootstrapExperiment(sys.argv[1], 'ivf_cache_collision', prototype=6)
try:
    experiment.start()
    for namespace in ('cache_a', 'cache_b'):
        experiment.sql('CREATE NAMESPACE ' + namespace)
        with connect(experiment, 'root@' + namespace) as child:
            experiment.sql('CREATE DATABASE vec', child)
            experiment.sql('CREATE TABLE vec.t(id INT PRIMARY KEY, embedding VECTOR(4))', child)
            values = ','.join("(%d,'[%d,%d,%d,%d]')" % (i, i if namespace == 'cache_a' else 21-i, i, i, i) for i in range(1, 21))
            experiment.sql('INSERT INTO vec.t VALUES ' + values, child)
            experiment.sql('CREATE VECTOR INDEX idx ON vec.t(embedding) WITH (distance=l2,type=ivf_pq,nlist=2,sample_per_nlist=5,m=2)', child)
            index = experiment.sql("SELECT tablet_id FROM oceanbase.__all_table WHERE table_name LIKE '%_idx' AND table_type=5", child)
            experiment.record('index', namespace=namespace, index=index)
            result = experiment.sql('SELECT id FROM vec.t ORDER BY l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1', child)
            experiment.record('nearest', namespace=namespace, result=result)
            cache = experiment.sql('SELECT rowkey_vid_tablet_id,statistics FROM oceanbase.__all_virtual_vector_index_info', log=False)
            experiment.record('cache', namespace=namespace, rows=cache)
    experiment.record('PASS', case='ivf_cache_collision')
finally:
    experiment.close()
