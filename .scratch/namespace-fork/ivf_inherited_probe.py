import re
import sys
import time

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id

experiment = BootstrapExperiment(sys.argv[1], 'ivf_inherited', prototype=6)
pq = len(sys.argv) > 2 and sys.argv[2] == 'pq'
try:
    experiment.start()
    experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
    experiment.sql('CREATE DATABASE ivf_inherited')
    experiment.sql('CREATE TABLE ivf_inherited.t(id INT PRIMARY KEY, embedding VECTOR(4))')
    values = ','.join(f"({i},'[{i},0,0,0]')" for i in range(1, 21 if pq else 7))
    experiment.sql('INSERT INTO ivf_inherited.t VALUES ' + values)
    options = ('distance=l2,type=ivf_pq,nlist=2,sample_per_nlist=5,m=2' if pq else
               'distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=5')
    experiment.sql('CREATE VECTOR INDEX idx ON ivf_inherited.t(embedding) WITH (' + options + ')')
    experiment.sql('FORK NAMESPACE ivf_inherited_child FROM ns1')
    child_id = namespace_id(experiment, 'ivf_inherited_child')
    with connect(experiment, 'root@ivf_inherited_child') as child:
        assert experiment.sql('SELECT COUNT(*) FROM ivf_inherited.t', child) == ((20 if pq else 6,),)
    experiment.sql("INSERT INTO ivf_inherited.t VALUES (100,'[0,0,0,0]')")
    query = ('SELECT id FROM ivf_inherited.t ORDER BY '
             'l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1')
    for tick in range(20):
        caches = experiment.sql('SELECT rowkey_vid_tablet_id,statistics FROM '
                                'oceanbase.__all_virtual_vector_index_info', log=False)
        experiment.record('cache_before_query', tick=tick, rows=caches)
        child_caches = [statistics for tablet_id, statistics in caches
                        if (tablet_id & ((1 << 62) - 1)) >> 37 == child_id]
        if any('cache_type=0' in statistics and 'count=2' in statistics
               for statistics in child_caches) and (not pq or any(
                   re.search(r'cache_type=1;[^}]*count=40;', statistics)
                   for statistics in child_caches)):
            break
        time.sleep(2)
    else:
        raise AssertionError('inherited IVF background cache was not loaded: %r' % (caches,))
    with connect(experiment, 'root@ivf_inherited_child') as child:
        assert experiment.sql('SELECT COUNT(*) FROM ivf_inherited.t', child) == ((20 if pq else 6,),)
        result = experiment.sql(query, child)
        if pq:
            assert len(result) == 1 and 1 <= result[0][0] <= 20, result
        else:
            assert result == ((1,),), result
        experiment.record('child_query', namespace_id=child_id, rows=result)
    experiment.record('cache_after_query', rows=experiment.sql(
        'SELECT rowkey_vid_tablet_id,statistics FROM oceanbase.__all_virtual_vector_index_info',
        log=False))
    experiment.record('PASS', case='ivf_inherited', pq=pq)
finally:
    experiment.close()
