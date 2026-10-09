import sys
import pymysql

sys.path.insert(0, 'tools/obtest')
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect

experiment = BootstrapExperiment(sys.argv[1], 'redefinition_dependents', prototype=6)
mode = sys.argv[2] if len(sys.argv) > 2 else 'both'
try:
    experiment.start()
    experiment.sql('CREATE DATABASE redef_deps')
    experiment.sql('FORK NAMESPACE redef_deps_child FROM ns1')
    with connect(experiment, 'root@redef_deps_child') as child:
        experiment.sql('CREATE TABLE redef_deps.parent(id INT PRIMARY KEY)', child)
        experiment.sql('INSERT INTO redef_deps.parent VALUES(1),(2)', child)
        constraints = []
        if mode in ('both', 'check'):
            constraints.append('CONSTRAINT positive_parent CHECK(parent_id>0)')
        if mode in ('both', 'fk'):
            constraints.append('CONSTRAINT parent_fk FOREIGN KEY(parent_id) '
                               'REFERENCES redef_deps.parent(id)')
        experiment.sql('CREATE TABLE redef_deps.t('
                       'id INT PRIMARY KEY, parent_id INT, payload INT, '
                       + ', '.join(constraints) + ')', child)
        experiment.sql('INSERT INTO redef_deps.t VALUES(1,1,7),(2,2,8)', child)
        try:
            with pymysql.connect(host='127.0.0.1', port=experiment.port,
                                 user='root@redef_deps_child', password='',
                                 autocommit=True, read_timeout=120) as ddl:
                experiment.sql('ALTER TABLE redef_deps.t MODIFY COLUMN payload VARCHAR(20)', ddl)
        except Exception as error:
            with connect(experiment, 'root@redef_deps_child') as diagnostic:
                experiment.record('alter_failed', error=repr(error), tasks=experiment.sql(
                    'SELECT task_id,ddl_type,status,ret_code FROM oceanbase.__all_ddl_task_status',
                    diagnostic, log=False), records=experiment.sql(
                    'SELECT ddl_type,ret_code FROM oceanbase.__all_ddl_error_message',
                    diagnostic, log=False))
            raise
        assert experiment.sql('SELECT id,parent_id,payload FROM redef_deps.t ORDER BY id', child) == (
            (1, 1, '7'), (2, 2, '8'))
        definition = experiment.sql('SHOW CREATE TABLE redef_deps.t', child)[0][1]
        assert all(name in definition for name in ('positive_parent', 'parent_fk')
                   if mode == 'both' or (mode == 'check' and name == 'positive_parent')
                   or (mode == 'fk' and name == 'parent_fk')), definition
        for parent_id in ((0,) if mode == 'check' else (999,) if mode == 'fk' else (0, 999)):
            try:
                experiment.sql('INSERT INTO redef_deps.t VALUES(3,%d,9)' % parent_id, child)
            except pymysql.MySQLError as error:
                experiment.record('constraint_rejected', parent_id=parent_id, code=error.args[0])
            else:
                raise AssertionError('constraint accepted parent_id=%d' % parent_id)
        experiment.record('redefined', definition=definition)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, 'root@redef_deps_child') as child:
        assert experiment.sql('SELECT id,parent_id,payload FROM redef_deps.t ORDER BY id', child) == (
            (1, 1, '7'), (2, 2, '8'))
        definition = experiment.sql('SHOW CREATE TABLE redef_deps.t', child)[0][1]
        assert all(name in definition for name in ('positive_parent', 'parent_fk')
                   if mode == 'both' or (mode == 'check' and name == 'positive_parent')
                   or (mode == 'fk' and name == 'parent_fk')), definition
        for parent_id in ((0,) if mode == 'check' else (999,) if mode == 'fk' else (0, 999)):
            try:
                experiment.sql('INSERT INTO redef_deps.t VALUES(3,%d,9)' % parent_id, child)
            except pymysql.MySQLError as error:
                experiment.record('recovered_constraint_rejected', parent_id=parent_id,
                                  code=error.args[0])
            else:
                raise AssertionError('recovered constraint accepted parent_id=%d' % parent_id)
    experiment.record('PASS', case='redefinition_dependents')
finally:
    experiment.close()
