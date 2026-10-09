import sys

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import setup_branch, connect

experiment = BootstrapExperiment(sys.argv[1], 'truncate_global_index', prototype=6)
try:
    experiment.start()
    experiment.sql("ALTER SYSTEM SET _ob_enable_truncate_partition_preserve_global_index=true")
    with setup_branch(experiment) as child:
        experiment.sql(
            "CREATE TABLE phase10.trunc_global(id INT PRIMARY KEY,k INT,c VARCHAR(20)) "
            "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (100), "
            "PARTITION p1 VALUES LESS THAN (200))", child)
        experiment.sql(
            "CREATE UNIQUE INDEX idx_k ON phase10.trunc_global(k) GLOBAL "
            "PARTITION BY HASH(k) PARTITIONS 2", child)
        experiment.sql(
            "INSERT INTO phase10.trunc_global VALUES(1,1,'a'),(2,2,'b'),(120,3,'c')", child)
        experiment.sql("ALTER TABLE phase10.trunc_global TRUNCATE PARTITION p0", child)
        assert experiment.sql(
            "SELECT id,k FROM phase10.trunc_global FORCE INDEX(idx_k) ORDER BY id", child) == ((120, 3),)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, 'root@phase10_child') as child:
        assert experiment.sql(
            "SELECT id,k FROM phase10.trunc_global FORCE INDEX(idx_k) ORDER BY id", child) == ((120, 3),)
    experiment.record('PASS', case='truncate_global_index_child')
finally:
    experiment.close()
