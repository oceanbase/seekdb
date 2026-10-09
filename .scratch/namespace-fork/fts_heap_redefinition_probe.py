import sys
import threading
import time

from fork_parent_truncate_probe import BootstrapExperiment, connect

experiment = BootstrapExperiment(sys.argv[1], 'fts_heap_redef', prototype=6)
try:
    experiment.start()
    experiment.sql('CREATE DATABASE fts_redef')
    experiment.sql('CREATE TABLE fts_redef.inherited(body TEXT, v INT)')
    experiment.sql("INSERT INTO fts_redef.inherited VALUES('alpha text',7),('beta text',8)")
    experiment.sql('CREATE FULLTEXT INDEX inherited_body ON fts_redef.inherited(body)')
    experiment.sql('FORK NAMESPACE fts_redef_child FROM ns1')
    experiment.sql("INSERT INTO fts_redef.inherited VALUES('parent later',99)")
    with connect(experiment, 'root@fts_redef_child') as child:
        assert experiment.sql('SELECT COUNT(*) FROM fts_redef.inherited', child) == ((2,),)
        experiment.sql('CREATE TABLE fts_redef.owned(body TEXT, v INT)', child)
        experiment.sql("INSERT INTO fts_redef.owned VALUES('alpha text',7),('beta text',8)", child)
        experiment.sql('CREATE FULLTEXT INDEX owned_body ON fts_redef.owned(body)', child)
        for table in ('owned', 'inherited'):
            errors = []
            def alter():
                try:
                    with connect(experiment, 'root@fts_redef_child') as ddl:
                        experiment.sql(f'ALTER TABLE fts_redef.{table} MODIFY COLUMN v VARCHAR(20)', ddl)
                except Exception as error:
                    errors.append(str(error))
            worker = threading.Thread(target=alter, daemon=True)
            worker.start()
            time.sleep(5)
            experiment.record('ddl_state', table=table, errors=errors, active=worker.is_alive(),
                tasks=experiment.sql('SELECT task_id,parent_task_id,ddl_type,status,execution_id,ret_code FROM oceanbase.__all_ddl_task_status', child, log=False),
                ddl_errors=experiment.sql('SELECT task_id,ddl_type,ret_code,user_message,dba_message FROM oceanbase.__all_ddl_error_message', child, log=False),
                tables=experiment.sql("SELECT table_name,table_id,tablet_id FROM oceanbase.__all_table WHERE database_id=(SELECT database_id FROM oceanbase.__all_database WHERE database_name='fts_redef')", child, log=False))
            worker.join(timeout=35)
            assert not worker.is_alive(), (table, errors)
            assert not errors, (table, errors)
            rows = experiment.sql(
                f"SELECT v FROM fts_redef.{table} WHERE MATCH(body) AGAINST('alpha')", child)
            assert rows == (('7',),), (table, rows)
            experiment.sql(f"INSERT INTO fts_redef.{table} VALUES('gamma text','9')", child)
            assert experiment.sql(f'SELECT COUNT(*) FROM fts_redef.{table}', child) == ((3,),)
            experiment.record('redefined', table=table, rows=rows)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, 'root@fts_redef_child') as child:
        for table in ('owned', 'inherited'):
            assert experiment.sql(f'SELECT COUNT(*) FROM fts_redef.{table}', child) == ((3,),)
            assert experiment.sql(
                f"SELECT v FROM fts_redef.{table} WHERE MATCH(body) AGAINST('gamma')", child) == (('9',),)
    experiment.record('PASS', case='fts_heap_redefinition')
finally:
    experiment.close()
