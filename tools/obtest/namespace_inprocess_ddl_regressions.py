"""DDL and namespace data paths that previously failed in child namespaces."""

import pymysql
import threading
import time


def before_restart(experiment, child):
    sql = lambda statement: experiment.sql(statement, child)

    # Keep a normal heap-table rewrite alongside the interrupted case below.
    sql("CREATE TABLE phase10.drop_pk(id INT PRIMARY KEY, v INT)")
    sql("INSERT INTO phase10.drop_pk VALUES(1,11),(2,22)")
    sql("ALTER TABLE phase10.drop_pk DROP PRIMARY KEY")
    assert sql("SELECT COUNT(*),SUM(v) FROM phase10.drop_pk") == ((2, 33),)
    assert "PRIMARY KEY" not in sql("SHOW CREATE TABLE phase10.drop_pk")[0][1]

    sql("CREATE TABLE phase10.checked(id INT PRIMARY KEY, v INT)")
    sql("INSERT INTO phase10.checked VALUES(1,11),(2,22)")
    sql("ALTER TABLE phase10.checked ADD CONSTRAINT positive_v CHECK (v > 0)")
    try:
        sql("INSERT INTO phase10.checked VALUES(3,-1)")
    except pymysql.MySQLError as error:
        assert error.args[0] == 3819, error.args
    else:
        raise AssertionError("child CHECK constraint allowed an invalid row")

    sql("CREATE TABLE phase10.redef_ref(id INT PRIMARY KEY)")
    sql("INSERT INTO phase10.redef_ref VALUES(1),(2)")
    sql("CREATE TABLE phase10.redef_deps(id INT PRIMARY KEY, ref_id INT, payload INT, "
        "CONSTRAINT redef_positive CHECK(ref_id>0), "
        "CONSTRAINT redef_fk FOREIGN KEY(ref_id) REFERENCES phase10.redef_ref(id))")
    sql("INSERT INTO phase10.redef_deps VALUES(1,1,7),(2,2,8)")
    sql("ALTER TABLE phase10.redef_deps MODIFY COLUMN payload VARCHAR(20)")
    assert sql("SELECT id,ref_id,payload FROM phase10.redef_deps ORDER BY id") == (
        (1, 1, "7"), (2, 2, "8"))
    for ref_id in (0, 999):
        try:
            sql("INSERT INTO phase10.redef_deps VALUES(3,%d,'9')" % ref_id)
        except pymysql.MySQLError as error:
            assert error.args[0] == (3819 if ref_id == 0 else 1452), error.args
        else:
            raise AssertionError("redefined constraint accepted ref_id=%d" % ref_id)

    sql("CREATE TABLE phase10.repartition(id INT PRIMARY KEY, v INT)")
    sql("INSERT INTO phase10.repartition VALUES(1,11),(2,22)")
    sql("ALTER TABLE phase10.repartition PARTITION BY HASH(id) PARTITIONS 3")
    assert sql("SELECT COUNT(*),SUM(v) FROM phase10.repartition") == ((2, 33),)

    experiment.sql("ALTER SYSTEM SET _ob_enable_truncate_partition_preserve_global_index=true")
    sql("CREATE TABLE phase10.trunc_global(id INT PRIMARY KEY,k INT,c VARCHAR(20)) "
        "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (100), "
        "PARTITION p1 VALUES LESS THAN (200))")
    sql("CREATE UNIQUE INDEX idx_k ON phase10.trunc_global(k) GLOBAL "
        "PARTITION BY HASH(k) PARTITIONS 2")
    sql("INSERT INTO phase10.trunc_global VALUES(1,1,'a'),(2,2,'b'),(120,3,'c')")
    sql("ALTER TABLE phase10.trunc_global TRUNCATE PARTITION p0")
    assert sql("SELECT id,k FROM phase10.trunc_global FORCE INDEX(idx_k) ORDER BY id") == ((120, 3),)
    sql("INSERT INTO phase10.trunc_global VALUES(5,5,'new')")
    assert sql("SELECT id,k FROM phase10.trunc_global FORCE INDEX(idx_k) ORDER BY id") == (
        (5, 5), (120, 3))
    assert sql("SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
        (1, 1), (2, 2), (120, 3))
    sql("ALTER TABLE phase10.trunc_inherited TRUNCATE PARTITION p0")
    assert sql("SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
        (120, 3),)
    sql("INSERT INTO phase10.trunc_inherited VALUES(5,5)")
    assert sql("SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
        (5, 5), (120, 3))
    assert experiment.sql(
        "SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
            (1, 1), (2, 2), (120, 3))
    experiment.sql("ALTER SYSTEM SET _ob_enable_truncate_partition_preserve_global_index=false")

    sql("CREATE TABLE phase10.auto_owned(id BIGINT PRIMARY KEY AUTO_INCREMENT, v INT)")
    sql("INSERT INTO phase10.auto_owned(v) VALUES(30)")
    sql("INSERT INTO phase10.auto_owned(id,v) VALUES(10,40)")
    sql("INSERT INTO phase10.auto_owned(v) VALUES(50)")
    assert sql("SELECT id,v FROM phase10.auto_owned ORDER BY id") == (
        (1, 30), (10, 40), (11, 50))
    sql("ALTER TABLE phase10.auto_owned AUTO_INCREMENT=100")
    sql("INSERT INTO phase10.auto_owned(v) VALUES(60)")
    assert sql("SELECT id FROM phase10.auto_owned WHERE v=60") == ((100,),)

    sql("CREATE TABLE phase10.fork_src(id INT PRIMARY KEY, v INT)")
    sql("INSERT INTO phase10.fork_src VALUES(1,11),(2,22)")
    sql("FORK TABLE phase10.fork_src TO phase10.fork_dst")
    sql("UPDATE phase10.fork_src SET v=99 WHERE id=1")
    assert sql("SELECT id,v FROM phase10.fork_dst ORDER BY id") == (
        (1, 11), (2, 22))


def after_restart(experiment, child):
    sql = lambda statement: experiment.sql(statement, child)
    assert sql("SELECT COUNT(*),SUM(v) FROM phase10.drop_pk") == ((2, 33),)
    assert "PRIMARY KEY" not in sql("SHOW CREATE TABLE phase10.drop_pk")[0][1]
    assert sql("SELECT COUNT(*),SUM(v) FROM phase10.repartition") == ((2, 33),)
    assert sql("SELECT id,k FROM phase10.trunc_global FORCE INDEX(idx_k) ORDER BY id") == (
        (5, 5), (120, 3))
    assert sql("SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
        (5, 5), (120, 3))
    assert experiment.sql(
        "SELECT id,k FROM phase10.trunc_inherited FORCE INDEX(idx_k) ORDER BY id") == (
            (1, 1), (2, 2), (120, 3))
    assert sql("SELECT COUNT(*),SUM(v) FROM phase10.checked") == ((2, 33),)
    try:
        sql("INSERT INTO phase10.checked VALUES(3,-1)")
    except pymysql.MySQLError as error:
        assert error.args[0] == 3819, error.args
    else:
        raise AssertionError("recovered CHECK constraint allowed an invalid row")
    assert sql("SELECT id,ref_id,payload FROM phase10.redef_deps ORDER BY id") == (
        (1, 1, "7"), (2, 2, "8"))
    for ref_id in (0, 999):
        try:
            sql("INSERT INTO phase10.redef_deps VALUES(3,%d,'9')" % ref_id)
        except pymysql.MySQLError as error:
            assert error.args[0] == (3819 if ref_id == 0 else 1452), error.args
        else:
            raise AssertionError("recovered constraint accepted ref_id=%d" % ref_id)
    sql("INSERT INTO phase10.auto_owned(v) VALUES(70)")
    assert sql("SELECT id FROM phase10.auto_owned WHERE v=70")[0][0] > 100
    assert sql("SELECT id,v FROM phase10.fork_dst ORDER BY id") == (
        (1, 11), (2, 22))
    assert sql("SELECT v FROM phase10.fork_src WHERE id=1") == ((99,),)


def interrupted_heap_recovery(experiment, connect):
    row_count = 1 << 18
    with connect(experiment, "root@phase10_child") as child:
        experiment.sql("CREATE TABLE phase10.drop_pk_recovery(id INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.drop_pk_recovery VALUES(1,11)", child)
        for step in range(18):
            experiment.sql(
                "INSERT INTO phase10.drop_pk_recovery "
                f"SELECT id+{1 << step},v FROM phase10.drop_pk_recovery", child, log=False)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.drop_pk_recovery", child) == ((row_count,),)
        old_tasks = {row[0] for row in experiment.sql(
            "SELECT task_id FROM oceanbase.__all_ddl_task_status", child, log=False)}
        errors = []

        def alter():
            try:
                with connect(experiment, "root@phase10_child") as ddl:
                    experiment.sql("ALTER TABLE phase10.drop_pk_recovery DROP PRIMARY KEY", ddl)
            except pymysql.MySQLError as error:
                errors.append(error)

        worker = threading.Thread(target=alter, daemon=True)
        worker.start()
        deadline = time.monotonic() + 20
        task_id = None
        while time.monotonic() < deadline:
            rows = experiment.sql(
                "SELECT task_id,status,execution_id FROM oceanbase.__all_ddl_task_status",
                child, log=False)
            matches = [row[0] for row in rows
                       if row[0] not in old_tasks and row[1:] == (3, 1)]
            if matches:
                task_id = matches[0]
                break
            time.sleep(.01)
        assert task_id is not None, rows
        assert experiment.sql(
            "SELECT COUNT(*) FROM oceanbase.__all_ddl_checksum "
            f"WHERE ddl_task_id={task_id} AND execution_id=1", child, log=False) == ((0,),)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    worker.join(timeout=1)
    experiment.start()
    with connect(experiment, "root@phase10_child") as child:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            tasks = experiment.sql(
                "SELECT status FROM oceanbase.__all_ddl_task_status "
                f"WHERE task_id={task_id}", child, log=False)
            if not tasks or tasks[0][0] == 99:
                break
            time.sleep(.5)
        assert not tasks, (task_id, tasks, errors)
        assert experiment.sql(
            "SELECT COUNT(*),SUM(v),MIN(id),MAX(id) "
            "FROM phase10.drop_pk_recovery", child) == ((row_count, row_count * 11, 1, row_count),)
        assert "PRIMARY KEY" not in experiment.sql(
            "SHOW CREATE TABLE phase10.drop_pk_recovery", child)[0][1]
        execution_ids = experiment.sql(
            "SELECT DISTINCT execution_id FROM oceanbase.__all_ddl_checksum "
            f"WHERE ddl_task_id={task_id}", child, log=False)
        assert execution_ids == ((2,),), execution_ids
    experiment.record("interrupted_drop_primary_recovery", task_id=task_id,
                      rows=row_count, execution_id=2)
