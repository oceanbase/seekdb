"""DDL and namespace data paths that previously failed in child namespaces."""

import pymysql


def before_restart(experiment, child):
    sql = lambda statement: experiment.sql(statement, child)

    # The heap-table rewrite succeeds without interruption. Its interrupted
    # recovery is tracked separately because replaying partial rows is unsafe.
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
