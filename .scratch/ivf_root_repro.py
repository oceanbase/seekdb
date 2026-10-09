"""Disposable local root IVF DDL failure probe; never stage this file."""
import threading
import time
import os
import pymysql
from namespace_worker_bootstrap_prototype import BootstrapExperiment

binary = 'build_release/src/observer/seekdb'
e = BootstrapExperiment(binary, 'ivf_root_repro', prototype=6)
worker = None
try:
    e.start()
    e.sql("ALTER SYSTEM SET syslog_level='WARN'")
    e.sql('CREATE TABLE test.t(id INT PRIMARY KEY, embedding VECTOR(3))')
    if os.environ.get('IVF_EMPTY') != '1':
        e.sql("INSERT INTO test.t VALUES (1,'[1,0,0]'),(2,'[2,0,0]'),"
              "(3,'[3,0,0]'),(4,'[4,0,0]'),(5,'[5,0,0]'),(6,'[6,0,0]')")
    outcome = {'done': False, 'error': None}
    def create_index():
        try:
            con = pymysql.connect(host='127.0.0.1', port=e.port, user='root', password='',
                                  autocommit=True, connect_timeout=2, read_timeout=85)
            try:
                with con.cursor() as cur:
                    cur.execute('SET ob_query_timeout=80000000')
                    cur.execute('CREATE VECTOR INDEX idx ON test.t(embedding) '
                                'WITH (distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=3)')
            finally:
                con.close()
        except Exception as exc:
            outcome['error'] = repr(exc)
        finally:
            outcome['done'] = True
    worker = threading.Thread(target=create_index, daemon=True)
    worker.start()
    failed = None
    deadline = time.monotonic() + 65
    while time.monotonic() < deadline and not outcome['done']:
        rows = e.sql('SELECT status,ret_code FROM oceanbase.__all_ddl_task_status '
                     'WHERE ddl_type=18 ORDER BY task_id DESC LIMIT 1', log=False)
        if rows and rows[0][0] == 99:
            failed = rows[0][1]
            break
        time.sleep(.2)
    print('IVF_ROOT_RESULT', {'task_failed_code': failed, **outcome}, flush=True)
    if failed is not None:
        raise AssertionError('root IVF DDL task failed: ' + str(failed))
    if not outcome['done']:
        raise TimeoutError('root IVF DDL did not finish within 65 seconds')
    if outcome['error']:
        raise AssertionError(outcome['error'])
finally:
    e.close()
    if worker:
        worker.join(timeout=2)
