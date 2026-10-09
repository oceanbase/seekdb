import sys
sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import setup_branch, connect

experiment = BootstrapExperiment(sys.argv[1], 'inprocess_exchange_partition', prototype=6)
try:
    experiment.start()
    with setup_branch(experiment) as child:
        experiment.sql('CREATE TABLE phase10.ex_part(id INT PRIMARY KEY,v INT) '
                       'PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (10), '
                       'PARTITION p1 VALUES LESS THAN (MAXVALUE))', child)
        experiment.sql('CREATE TABLE phase10.ex_plain(id INT PRIMARY KEY,v INT)', child)
        experiment.sql('INSERT INTO phase10.ex_part VALUES(1,11),(11,111)', child)
        experiment.sql('INSERT INTO phase10.ex_plain VALUES(2,22)', child)
        experiment.sql('ALTER TABLE phase10.ex_part EXCHANGE PARTITION p0 '
                       'WITH TABLE phase10.ex_plain WITHOUT VALIDATION', child)
        assert experiment.sql('SELECT id,v FROM phase10.ex_part ORDER BY id', child) == ((2,22),(11,111))
        assert experiment.sql('SELECT id,v FROM phase10.ex_plain ORDER BY id', child) == ((1,11),)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, 'root@phase10_child') as child:
        assert experiment.sql('SELECT id,v FROM phase10.ex_part ORDER BY id', child) == ((2,22),(11,111))
        assert experiment.sql('SELECT id,v FROM phase10.ex_plain ORDER BY id', child) == ((1,11),)
    experiment.record('PASS', case='exchange_partition_child')
finally:
    experiment.close()
