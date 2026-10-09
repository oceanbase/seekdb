import sys
import time

sys.path.insert(0, '.scratch/excluded-from-branch/tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'ivf_parent_drop', prototype=6)
try:
    experiment.start()
    experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
    experiment.sql('CREATE DATABASE ivf_drop')
    experiment.sql('CREATE TABLE ivf_drop.t(id INT PRIMARY KEY, embedding VECTOR(4))')
    experiment.sql("INSERT INTO ivf_drop.t VALUES " + ','.join(
        f"({i},'[{i},0,0,0]')" for i in range(1, 7)))
    experiment.sql('CREATE VECTOR INDEX idx ON ivf_drop.t(embedding) WITH '
                   '(distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=5)')
    experiment.sql('FORK NAMESPACE ivf_drop_child FROM ns1')
    child_id = experiment.sql("SELECT namespace_id FROM __fork_proto_meta.namespaces "
                              "WHERE name='ivf_drop_child'")[0][0]
    with connect(experiment, 'root@ivf_drop_child') as child:
        experiment.record('child_indexes_before_drop', rows=experiment.sql(
            'SHOW INDEX FROM ivf_drop.t', child))
    experiment.sql('DROP INDEX idx ON ivf_drop.t')
    experiment.record('root_indexes_after_drop', rows=experiment.sql('SHOW INDEX FROM ivf_drop.t'))
    with connect(experiment, 'root@ivf_drop_child') as child:
        experiment.record('child_indexes_after_drop', rows=experiment.sql(
            'SHOW INDEX FROM ivf_drop.t', child))
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    experiment.record('root_indexes_after_restart', rows=experiment.sql(
        'SHOW INDEX FROM ivf_drop.t'))
    with connect(experiment, 'root@ivf_drop_child') as child:
        experiment.record('child_indexes_after_restart', rows=experiment.sql(
            'SHOW INDEX FROM ivf_drop.t', child))
        experiment.record('child_count', rows=experiment.sql(
            'SELECT COUNT(*) FROM ivf_drop.t', child))
    marker = 1 << 62
    child_cache = False
    for tick in range(20):
        caches = experiment.sql('SELECT rowkey_vid_tablet_id,statistics FROM '
                                'oceanbase.__all_virtual_vector_index_info', log=False)
        child_caches = [(tablet_id, statistics) for tablet_id, statistics in caches
                        if tablet_id & marker and ((tablet_id & ~marker) >> 37) == child_id]
        experiment.record('cache', tick=tick, child=child_caches, all_count=len(caches))
        child_cache = any('cache_type=0' in statistics and 'count=2' in statistics
                          for _, statistics in child_caches)
        if child_cache:
            break
        time.sleep(2)
    experiment.record('result', child_cache=child_cache, child_id=child_id)
finally:
    experiment.close()
