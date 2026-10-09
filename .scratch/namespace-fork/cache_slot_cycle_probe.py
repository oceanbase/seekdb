import sys

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'cache_slot_cycle', prototype=6)
try:
    experiment.start()
    experiment.sql('CREATE NAMESPACE cache_left', log=False)
    experiment.sql('CREATE NAMESPACE cache_right', log=False)
    with connect(experiment, 'root@cache_left') as left, connect(experiment, 'root@cache_right') as right:
        for child, value in ((left, 11), (right, 22)):
            experiment.sql('CREATE DATABASE cache_app', child, log=False)
            experiment.sql('CREATE TABLE cache_app.t(id INT PRIMARY KEY, v INT)', child, log=False)
            experiment.sql(f'INSERT INTO cache_app.t VALUES(1,{value})', child, log=False)
        assert experiment.sql('SELECT v FROM cache_app.t', left, log=False) == ((11,),)
        assert experiment.sql('SELECT v FROM cache_app.t', right, log=False) == ((22,),)
    experiment.sql('DROP NAMESPACE cache_left', log=False)
    experiment.sql('DROP NAMESPACE cache_right', log=False)
    for index in range(16):
        name = 'cache_cycle_' + str(index)
        experiment.sql('CREATE NAMESPACE ' + name, log=False)
        with connect(experiment, 'root@' + name, database='test') as child:
            assert experiment.sql('SELECT 1', child, log=False) == ((1,),)
        experiment.record('activated', index=index, name=name)
        experiment.sql('DROP NAMESPACE ' + name, log=False)
    experiment.record('PASS', case='cache_slot_cycle')
finally:
    experiment.close()
