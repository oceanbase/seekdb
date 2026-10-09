import sys
import threading
import time

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'ivf_failed_create', prototype=6)
options = sys.argv[2] if len(sys.argv) > 2 else 'distance=l2,type=ivf_pq,nlist=2,sample_per_nlist=5,m=3'
errors = []
try:
    experiment.start()
    experiment.sql('CREATE NAMESPACE ivf_fail_child')
    with connect(experiment, 'root@ivf_fail_child') as child:
        experiment.sql('CREATE DATABASE vec', child)
        experiment.sql('CREATE TABLE vec.t(id INT PRIMARY KEY, embedding VECTOR(4))', child)
        experiment.sql("INSERT INTO vec.t VALUES(1,'[1,0,0,0]'),(2,'[0,1,0,0]')", child)

        def build():
            try:
                with connect(experiment, 'root@ivf_fail_child') as ddl:
                    experiment.sql('CREATE VECTOR INDEX bad_pq ON vec.t(embedding) '
                                   'WITH (' + options + ')', ddl)
            except Exception as error:
                errors.append(str(error))

        thread = threading.Thread(target=build, daemon=True)
        thread.start()
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            tasks = experiment.sql('SELECT task_id,ddl_type,status,ret_code FROM '
                                   'oceanbase.__all_ddl_task_status', child, log=False)
            if not tasks and not thread.is_alive():
                break
            time.sleep(.5)
        thread.join(timeout=1)
        tables = experiment.sql("SELECT table_name,table_type FROM oceanbase.__all_table "
                                "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
                                "WHERE database_name='vec')", child, log=False)
        records = experiment.sql('SELECT ddl_type,ret_code FROM oceanbase.__all_ddl_error_message',
                                 child, log=False)
        indexes = experiment.sql('SHOW INDEX FROM vec.t', child)
        experiment.record('result', tasks=tasks, tables=tables, errors=errors, records=records)
        assert errors and not tasks, (errors, tasks)
        assert all(name == 't' or name.startswith('__AUX_LOB_') for name, _ in tables), tables
        assert all(row[2] == 'PRIMARY' for row in indexes), indexes
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, 'root@ivf_fail_child') as child:
        assert not experiment.sql('SELECT task_id FROM oceanbase.__all_ddl_task_status', child)
        assert all(row[2] == 'PRIMARY' for row in experiment.sql('SHOW INDEX FROM vec.t', child))
        experiment.sql('CREATE VECTOR INDEX good_flat ON vec.t(embedding) '
                       'WITH (distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=5)', child)
        assert experiment.sql('SELECT COUNT(*) FROM vec.t', child) == ((2,),)
    experiment.record('PASS', case='ivf_failed_create_cleanup')
finally:
    experiment.close()
