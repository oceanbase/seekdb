import sys
sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'fts_explicit_pk_column', prototype=6)
try:
    experiment.start()
    experiment.sql('CREATE DATABASE fts_redef')
    experiment.sql('FORK NAMESPACE fts_redef_child FROM ns1')
    with connect(experiment, 'root@fts_redef_child') as child:
        experiment.sql('CREATE TABLE fts_redef.rows(id INT PRIMARY KEY, body TEXT, v INT)', child)
        experiment.sql("INSERT INTO fts_redef.rows VALUES(1,'alpha text',7),(2,'beta text',8)", child)
        experiment.sql('CREATE FULLTEXT INDEX body_ft ON fts_redef.rows(body)', child)
        experiment.sql('ALTER TABLE fts_redef.rows DROP COLUMN v, ADD COLUMN w INT DEFAULT 4', child)
        assert experiment.sql("SELECT id,w FROM fts_redef.rows WHERE MATCH(body) AGAINST('alpha')", child) == ((1,4),)
    experiment.record('PASS', case='fts_explicit_pk_column_redefinition')
finally:
    experiment.close()
