import sys
import threading
import time
sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'fts_explicit_pk_redef', prototype=6)
try:
    experiment.start()
    experiment.sql('CREATE DATABASE fts_redef')
    experiment.sql('FORK NAMESPACE fts_redef_child FROM ns1')
    with connect(experiment, 'root@fts_redef_child') as child:
        experiment.sql('CREATE TABLE fts_redef.rows(id INT PRIMARY KEY, body TEXT, v INT)', child)
        experiment.sql("INSERT INTO fts_redef.rows VALUES(1,'alpha text',7),(2,'beta text',8)", child)
        experiment.sql('CREATE FULLTEXT INDEX body_ft ON fts_redef.rows(body)', child)
        errors = []
        def alter():
            try:
                with connect(experiment, 'root@fts_redef_child') as ddl:
                    experiment.sql('ALTER TABLE fts_redef.rows MODIFY COLUMN v VARCHAR(20)', ddl)
            except Exception as error:
                errors.append(str(error))
        worker = threading.Thread(target=alter, daemon=True)
        worker.start()
        time.sleep(8)
        experiment.record('ddl_state', active=worker.is_alive(), errors=errors,
            tasks=experiment.sql('SELECT task_id,parent_task_id,ddl_type,status,execution_id,ret_code FROM oceanbase.__all_ddl_task_status', child, log=False),
            ddl_errors=experiment.sql('SELECT task_id,ddl_type,ret_code,dba_message FROM oceanbase.__all_ddl_error_message', child, log=False))
        worker.join(timeout=60)
        assert not worker.is_alive(), errors
        assert not errors, errors
        assert experiment.sql("SELECT id,v FROM fts_redef.rows WHERE MATCH(body) AGAINST('alpha')", child) == ((1,'7'),)
    experiment.record('PASS', case='fts_explicit_pk_redefinition')
finally:
    experiment.close()
