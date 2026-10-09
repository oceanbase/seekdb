import sys
import threading
import time

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

binary = sys.argv[1]
steps = int(sys.argv[2]) if len(sys.argv) > 2 else 12
hold_seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 0
expected_count = 1 << steps
experiment = BootstrapExperiment(binary, 'drop_pk_recovery', prototype=6)
errors = []
try:
    experiment.start()
    experiment.sql('CREATE DATABASE pk_recovery')
    experiment.sql('FORK NAMESPACE pk_child FROM ns1')
    with connect(experiment, 'root@pk_child') as child:
        experiment.sql('CREATE TABLE pk_recovery.t(id INT PRIMARY KEY, v INT)', child)
        experiment.sql('INSERT INTO pk_recovery.t VALUES(1,11)', child)
        for step in range(steps):
            experiment.sql(f'INSERT INTO pk_recovery.t SELECT id+{1 << step},v FROM pk_recovery.t', child, log=False)
        assert experiment.sql('SELECT COUNT(*) FROM pk_recovery.t', child) == ((expected_count,),)
        def alter():
            try:
                with connect(experiment, 'root@pk_child') as ddl:
                    experiment.sql('ALTER TABLE pk_recovery.t DROP PRIMARY KEY', ddl)
            except Exception as error:
                errors.append(str(error))
        thread = threading.Thread(target=alter, daemon=True)
        thread.start()
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            tasks = experiment.sql('SELECT task_id,status,execution_id FROM oceanbase.__all_ddl_task_status', child, log=False)
            if any(row[1] == 3 and row[2] == 1 for row in tasks):
                break
            time.sleep(.01)
        assert any(row[1] == 3 and row[2] == 1 for row in tasks), tasks
        experiment.record('paused', tasks=tasks)
        if hold_seconds:
            time.sleep(hold_seconds)
            experiment.record('held', seconds=hold_seconds, tasks=experiment.sql(
                'SELECT task_id,status,execution_id FROM oceanbase.__all_ddl_task_status',
                child, log=False))
        experiment.record('checksums_before_restart', rows=experiment.sql(
            'SELECT table_id, execution_id, ddl_task_id, task_id, COUNT(*) '
            'FROM oceanbase.__all_ddl_checksum GROUP BY table_id, execution_id, ddl_task_id, task_id',
            child, log=False))
        experiment.record('tables_before_restart', tables=experiment.sql(
            "SELECT table_name,table_id,tablet_id,table_type FROM oceanbase.__all_table "
            "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
            "WHERE database_name='pk_recovery')", child, log=False))
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    thread.join(timeout=1)
    experiment.start()
    deadline = time.monotonic() + 45
    with connect(experiment, 'root@pk_child') as child:
        while time.monotonic() < deadline:
            tasks = experiment.sql('SELECT task_id,status,execution_id FROM oceanbase.__all_ddl_task_status', child, log=False)
            if not tasks or any(row[1] == 99 for row in tasks):
                break
            time.sleep(.5)
        rows = experiment.sql('SELECT COUNT(*),SUM(v) FROM pk_recovery.t', child)
        definition = experiment.sql('SHOW CREATE TABLE pk_recovery.t', child)
        table_rows = experiment.sql(
            "SELECT table_name,table_id,tablet_id,table_type FROM oceanbase.__all_table "
            "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
            "WHERE database_name='pk_recovery')", child, log=False)
        experiment.record('tables_after_restart', tables=table_rows)
        experiment.record('checksums_after_restart', rows=experiment.sql(
            'SELECT table_id, execution_id, ddl_task_id, task_id, COUNT(*) '
            'FROM oceanbase.__all_ddl_checksum GROUP BY table_id, execution_id, ddl_task_id, task_id',
            child, log=False))
        for name, _, _, _ in table_rows:
            if name.startswith('__hidden_'):
                try:
                    hidden_rows = experiment.sql(
                        'SELECT COUNT(*) FROM pk_recovery.`' + name + '`', child)
                    experiment.record('hidden_rows', count=hidden_rows[0][0])
                except Exception as error:
                    experiment.record('hidden_rows_error', error=str(error))
        experiment.record('result', tasks=tasks, rows=rows, definition=definition, errors=errors)
        assert not tasks and rows[0][0] == expected_count and int(rows[0][1]) == expected_count * 11 and 'PRIMARY KEY' not in definition[0][1]
    experiment.record('PASS', case='drop_pk_recovery')
finally:
    experiment.close()
