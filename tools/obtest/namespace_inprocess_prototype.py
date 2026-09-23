#!/usr/bin/env python3
"""Single-process paths for the four namespace prototype gates."""
from pathlib import Path
import statistics
import subprocess
import time

import pymysql

from namespace_worker_bootstrap_prototype import BootstrapExperiment


def connect(experiment, branch="root", **kwargs):
    return pymysql.connect(
        host="127.0.0.1", port=experiment.port, user=branch, password="",
        autocommit=True, connect_timeout=3, read_timeout=40, **kwargs)


def check_single_process(experiment):
    assert not list((experiment.base / "run").glob("namespace-worker-*"))
    assert experiment.proc.poll() is None


def median_query_us(connection):
    with connection.cursor() as cursor:
        for _ in range(10):
            cursor.execute("SELECT 1")
            cursor.fetchall()
        samples = []
        for _ in range(100):
            start = time.perf_counter_ns()
            cursor.execute("SELECT 1")
            cursor.fetchall()
            samples.append((time.perf_counter_ns() - start) / 1000)
    return statistics.median(samples)


def setup_branch(experiment):
    experiment.sql("CREATE DATABASE phase10")
    experiment.sql("CREATE TABLE phase10.parent(id INT PRIMARY KEY, v INT)")
    experiment.sql("INSERT INTO phase10.parent VALUES(1,10),(2,20)")
    experiment.sql("FORK NAMESPACE phase10_child FROM ns1")
    child = connect(experiment, "root@phase10_child")
    assert experiment.sql("SELECT id,v FROM phase10.parent ORDER BY id", child) == ((1, 10), (2, 20))
    check_single_process(experiment)
    return child


def bootstrap_probe(experiment):
    assert experiment.sql("SELECT 1") == ((1,),)
    with setup_branch(experiment) as child:
        assert experiment.sql("SELECT COUNT(*) FROM oceanbase.__all_database", child)[0][0] >= 6
    start = time.perf_counter()
    experiment.sql("CREATE NAMESPACE phase10_empty")
    experiment.record("namespace_create_latency", seconds=round(time.perf_counter() - start, 3))
    # The first connection must be able to select a database from the template.
    with connect(experiment, "root@phase10_empty", database="test") as selected:
        assert experiment.sql("SELECT DATABASE()", selected) == (("test",),)
        assert experiment.sql("SHOW WARNINGS", selected) == ()
        experiment.sql("CREATE TABLE warning_probe(c1 INT PRIMARY KEY, c2 INT)", selected)
        experiment.sql("INSERT INTO warning_probe VALUES(1,8),(2,7)", selected)
        experiment.sql("SELECT 1 AS c1, 2 AS c2 FROM warning_probe GROUP BY c1", selected)
        warning_rows = experiment.sql("SHOW WARNINGS", selected)
        assert warning_rows and warning_rows[0][:2] == ("Warning", 1052), warning_rows
    with connect(experiment, "root@phase10_empty") as empty:
        databases = {row[0] for row in experiment.sql("SHOW DATABASES", empty)}
        assert "phase10" not in databases and "__fork_proto_meta" not in databases, databases
        try:
            experiment.sql("SELECT COUNT(*) FROM __fork_proto_meta.namespaces", empty)
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("child namespace read the control catalog")
        experiment.sql("CREATE DATABASE fresh", empty)
        experiment.sql("CREATE TABLE fresh.t(id INT PRIMARY KEY)", empty)
        experiment.sql("INSERT INTO fresh.t VALUES(1)", empty)
        assert experiment.sql("SELECT id FROM fresh.t", empty) == ((1,),)
        experiment.sql("CREATE VIEW fresh.v AS SELECT id FROM fresh.t", empty)
        assert experiment.sql("SELECT id FROM fresh.v", empty) == ((1,),)
        assert experiment.sql("SHOW COLUMNS FROM fresh.t", empty)[0][0] == "id"
        assert experiment.sql("SHOW INDEX FROM fresh.t", empty)[0][2] == "PRIMARY"
        assert "CREATE TABLE" in experiment.sql("SHOW CREATE TABLE fresh.t", empty)[0][1]
        assert ("t",) in experiment.sql("SHOW TABLES FROM fresh", empty)
        assert experiment.sql("SHOW COLLATION LIKE 'utf8mb4_general_ci'", empty)
        assert experiment.sql("SHOW CHARACTER SET LIKE 'utf8mb4'", empty)
    with connect(experiment, "root@phase10_empty", database="fresh") as selected:
        assert experiment.sql("SELECT DATABASE()", selected) == (("fresh",),)
        assert experiment.sql("SELECT id FROM t", selected) == ((1,),)
        selected.select_db("mysql")
        assert experiment.sql("SELECT DATABASE()", selected) == (("mysql",),)
        selected.select_db("fresh")
        assert experiment.sql("SELECT id FROM t", selected) == ((1,),)
    try:
        connect(experiment, "root@phase10_empty", database="missing_db").close()
    except pymysql.MySQLError:
        pass
    else:
        raise AssertionError("child namespace accepted a missing login database")
    try:
        connect(experiment, "root@__template__").close()
    except pymysql.MySQLError:
        pass
    else:
        raise AssertionError("template namespace accepted a login")
    for statement in ("FORK NAMESPACE copy FROM __template__",
                      "FORK DATABASE ns1 TO old_syntax"):
        try:
            experiment.sql(statement)
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError(f"reserved or retired syntax succeeded: {statement}")
    experiment.record("PASS", case="inprocess_bootstrap", one_process=True,
                      child_login=True, inherited_read=True, empty_namespace=True)


def sql_probe(experiment):
    with setup_branch(experiment) as child, connect(experiment, "root@phase10_child") as other:
        assert experiment.sql("SELECT CURRENT_SCN()", child)[0][0] > 0
        global_switch = experiment.sql(
            "SELECT variable_value FROM INFORMATION_SCHEMA.GLOBAL_VARIABLES "
            "WHERE variable_name='optimizer_switch'", child)
        assert len(global_switch) == 1, global_switch
        session_variables = experiment.sql("SHOW VARIABLES LIKE 'optimizer_dynamic_sampling'", child)
        global_variables = experiment.sql("SHOW GLOBAL VARIABLES LIKE 'optimizer_dynamic_sampling'", child)
        assert session_variables and session_variables[0][0] == "optimizer_dynamic_sampling"
        assert global_variables and global_variables[0][0] == "optimizer_dynamic_sampling"
        experiment.sql("SET optimizer_switch = (SELECT variable_value FROM "
                       "INFORMATION_SCHEMA.GLOBAL_VARIABLES "
                       "WHERE variable_name='optimizer_switch')", child)
        experiment.sql("CREATE NAMESPACE phase10_fresh")
        try:
            experiment.sql("CREATE NAMESPACE forbidden_from_child", child)
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("child created a namespace")
        experiment.sql("CREATE TABLE phase10.owned(id INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.owned VALUES(1,11),(2,22)", child)
        experiment.sql("CREATE INDEX owned_v ON phase10.owned(v)", child)
        assert experiment.sql("SELECT id FROM phase10.owned FORCE INDEX(owned_v) WHERE v=22", other) == ((2,),)
        experiment.sql("BEGIN", child)
        experiment.sql("UPDATE phase10.owned SET v=99 WHERE id=1", child)
        assert experiment.sql("SELECT v FROM phase10.owned WHERE id=1", other) == ((11,),)
        experiment.sql("ROLLBACK", child)
        assert experiment.sql("SELECT v FROM phase10.owned WHERE id=1", child) == ((11,),)
        experiment.sql("CREATE PROCEDURE phase10.count_owned(OUT n INT) "
                       "BEGIN SELECT COUNT(*) INTO n FROM phase10.owned; END", child)
        experiment.sql("SET @owned_count=0", child)
        experiment.sql("CALL phase10.count_owned(@owned_count)", child)
        assert experiment.sql("SELECT @owned_count", child) == ((2,),)
        experiment.sql("CREATE PROCEDURE phase10.sum_owned(INOUT total INT) BEGIN "
                       "DECLARE done INT DEFAULT 0; DECLARE value INT; "
                       "DECLARE cur CURSOR FOR SELECT v FROM phase10.owned FOR UPDATE; "
                       "DECLARE CONTINUE HANDLER FOR NOT FOUND SET done=1; "
                       "OPEN cur; loop1: LOOP FETCH cur INTO value; "
                       "IF done THEN LEAVE loop1; END IF; "
                       "SET total=total+value; END LOOP; CLOSE cur; END", child)
        experiment.sql("SET @owned_total=0", child)
        experiment.sql("BEGIN", child)
        experiment.sql("CALL phase10.sum_owned(@owned_total)", child)
        assert experiment.sql("SELECT @owned_total", child) == ((33,),)
        experiment.sql("COMMIT", child)
        experiment.sql("CREATE TABLE phase10.ds_left(a INT PRIMARY KEY)", child)
        experiment.sql("CREATE TABLE phase10.ds_right(a INT PRIMARY KEY)", child)
        time.sleep(3)
        sampling_plan = experiment.sql(
            "EXPLAIN EXTENDED_NOADDR SELECT l.a FROM phase10.ds_left l "
            "LEFT JOIN phase10.ds_right r ON l.a=r.a "
            "WHERE r.a IS NOT NULL AND l.a>10", child, log=False)
        assert sum("dynamic sampling level:1" in str(cell)
                   for row in sampling_plan for cell in row) == 2, sampling_plan
        experiment.sql("INSERT INTO phase10.parent VALUES(3,30)")
        assert experiment.sql("SELECT COUNT(*) FROM phase10.parent", child) == ((2,),)
        inherited_plan = experiment.sql(
            "EXPLAIN EXTENDED_NOADDR SELECT * FROM phase10.parent", child, log=False)
        assert not any("table_rows:3" in str(cell)
                       for row in inherited_plan for cell in row), inherited_plan
        experiment.sql("UPDATE phase10.parent SET v=30 WHERE id=1", child)
        assert experiment.sql("SELECT v FROM phase10.parent WHERE id=1") == ((10,),)
        experiment.sql("FORK NAMESPACE phase10_grandchild FROM phase10_child")
    with connect(experiment, "root@phase10_grandchild") as grandchild:
        assert experiment.sql("SELECT v FROM phase10.parent WHERE id=1", grandchild) == ((30,),)
        assert experiment.sql("SELECT SUM(v) FROM phase10.owned", grandchild) == ((33,),)
    with connect(experiment, "root@phase10_fresh") as fresh:
        assert "phase10" not in {row[0] for row in experiment.sql("SHOW DATABASES", fresh)}
    check_single_process(experiment)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, "root@phase10_child") as child:
        assert experiment.sql("SELECT SUM(v) FROM phase10.owned", child) == ((33,),)
    with connect(experiment, "root@phase10_grandchild") as grandchild:
        assert experiment.sql("SELECT v FROM phase10.parent WHERE id=1", grandchild) == ((30,),)
    with connect(experiment, "root@phase10_fresh") as fresh:
        assert "phase10" not in {row[0] for row in experiment.sql("SHOW DATABASES", fresh)}
    with connect(experiment, "root@phase10_child") as child:
        parent_us = median_query_us(experiment.connection)
        child_us = median_query_us(child)
        experiment.record("inprocess_sql_latency", parent_us=parent_us, child_us=child_us)
        assert child_us < parent_us * 4, (parent_us, child_us)
    check_single_process(experiment)
    experiment.record("PASS", case="inprocess_sql", transactions=True,
                      index=True, nested_fork=True, restart=True)


def direct_probe(experiment):
    with setup_branch(experiment) as child:
        experiment.sql("TRUNCATE TABLE phase10.parent", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.parent", child) == ((0,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.parent") == ((2,),)
        experiment.sql("INSERT INTO phase10.parent VALUES(3,30)", child)
        assert experiment.sql("SELECT id FROM phase10.parent", child) == ((3,),)
        experiment.sql("CREATE TABLE phase10.records(id INT PRIMARY KEY, v VARCHAR(64), amount DECIMAL(12,2))", child)
        experiment.sql("INSERT INTO phase10.records VALUES(1,'first',12.34),(2,'second',56.78)", child)
        experiment.sql("CREATE TABLE phase10.heap_rows(d DATE)", child)
        experiment.sql("INSERT INTO phase10.heap_rows VALUES('2078-10-10'),('1970-11-01')", child)
        experiment.sql("UPDATE phase10.heap_rows SET d='1970-11-02' WHERE d='1970-11-01'", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.heap_rows", child) == ((2,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.heap_rows WHERE d='1970-11-02'", child) == ((1,),)
        experiment.sql("CREATE TABLE phase10.join_left(a BIGINT)", child)
        experiment.sql("INSERT INTO phase10.join_left VALUES(32)", child)
        experiment.sql("CREATE TABLE phase10.join_right(b YEAR(4), KEY key_b(b))", child)
        experiment.sql("INSERT INTO phase10.join_right VALUES(1901)", child)
        assert experiment.sql(
            "SELECT a,b FROM phase10.join_left,phase10.join_right WHERE a>b", child) == ()
        experiment.sql("CREATE INDEX records_v ON phase10.records(v)", child)
        assert experiment.sql("SELECT id FROM phase10.records FORCE INDEX(records_v) WHERE v='second'", child) == ((2,),)
        experiment.sql("CREATE TABLE phase10.parts(id INT PRIMARY KEY, v INT) PARTITION BY HASH(id) PARTITIONS 4", child)
        experiment.sql("INSERT INTO phase10.parts VALUES(1,10),(2,20),(3,30),(4,40)", child)
        assert experiment.sql("SELECT COUNT(*),SUM(v) FROM phase10.parts", child) == ((4, 100),)
        assert experiment.sql(
            "SELECT /*+ parallel(2) */ SUM(v) FROM phase10.parts", child) == ((100,),)
        experiment.sql(
            "CREATE TABLE phase10.generated_parts(c1 INT,c2 VARCHAR(20),"
            "c3 INT GENERATED ALWAYS AS (LENGTH(c4)),c4 VARCHAR(20)) "
            "PARTITION BY KEY(c3,c4) PARTITIONS 2", child)
        experiment.sql(
            "INSERT INTO phase10.generated_parts(c1,c2,c4) VALUES(1,'x','ab')", child)
        experiment.sql("CREATE INDEX generated_parts_c2 ON phase10.generated_parts(c2)", child)
        assert experiment.sql(
            "SELECT c1,c2,c3,c4 FROM phase10.generated_parts "
            "FORCE INDEX(generated_parts_c2)", child) == ((1, "x", 2, "ab"),)
        experiment.sql("CREATE TABLE phase10.blobs(id INT PRIMARY KEY, payload MEDIUMBLOB)", child)
        with child.cursor() as cursor:
            cursor.execute("INSERT INTO phase10.blobs VALUES(1,%s)", (b"namespace-blob",))
        assert experiment.sql("SELECT payload FROM phase10.blobs", child) == ((b"namespace-blob",),)
        with child.cursor() as cursor:
            cursor.execute("INSERT INTO phase10.blobs VALUES(2,%s)", (b"A" * 8000 + b"B" * 8000,))
        assert experiment.sql(
            "SELECT LENGTH(payload),SUBSTR(payload,7999,4),SUBSTR(payload,-1,1) "
            "FROM phase10.blobs WHERE id=2", child) == ((16000, b"AABB", b"B"),)
        with child.cursor() as cursor:
            cursor.execute("UPDATE phase10.blobs SET payload=payload WHERE id=2")
            cursor.execute("UPDATE phase10.blobs SET payload=%s WHERE id=2", (b"A" * 8000 + b"B" * 8000,))
            cursor.execute("UPDATE phase10.blobs SET payload=%s WHERE id=2", (b"A" * 8000 + b"C" * 8000,))
        assert experiment.sql(
            "SELECT SUBSTR(payload,7999,4),SUBSTR(payload,-1,1) "
            "FROM phase10.blobs WHERE id=2", child) == ((b"AACC", b"C"),)
        experiment.sql("CREATE TABLE phase10.runtime_ddl(id INT PRIMARY KEY, v INT)", child)
        with child.cursor() as cursor:
            cursor.executemany("INSERT INTO phase10.runtime_ddl VALUES(%s,%s)",
                               [(i, i + 1000) for i in range(1, 201)])
        experiment.sql("CREATE UNIQUE INDEX runtime_v ON phase10.runtime_ddl(v)", child)
        experiment.sql("ALTER TABLE phase10.runtime_ddl MODIFY COLUMN v BIGINT", child)
        assert experiment.sql("SELECT COUNT(*),SUM(v) FROM phase10.runtime_ddl", child) == ((200, 220100),)
        experiment.sql("ALTER TABLE phase10.records ADD COLUMN revision INT DEFAULT 7", child)
        assert experiment.sql("SELECT revision FROM phase10.records WHERE id=1", child) == ((7,),)
        experiment.sql("DROP INDEX records_v ON phase10.records", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.records", child) == ((2,),)
    check_single_process(experiment)
    experiment.record("PASS", case="inprocess_direct", ddl=True, partition=True,
                      index=True, lob=True)


def tls_probe(experiment):
    wallet = experiment.base / "wallet"
    ssl = dict(ssl_ca=str(wallet / "ca.pem"),
               ssl_cert=str(wallet / "server-cert.pem"),
               ssl_key=str(wallet / "server-key.pem"),
               ssl_verify_cert=True, ssl_verify_identity=True)
    with connect(experiment, **ssl) as control:
        assert control._sock.cipher() is not None
        experiment.sql("CREATE DATABASE phase10", control)
        experiment.sql("CREATE TABLE phase10.secure(id INT PRIMARY KEY, v INT)", control)
        experiment.sql("INSERT INTO phase10.secure VALUES(1,42)", control)
        experiment.sql("FORK NAMESPACE phase10_tls_child FROM ns1", control)
    with connect(experiment, "root@phase10_tls_child", **ssl) as child:
        cipher = child._sock.cipher()
        assert cipher is not None and cipher[1] in ("TLSv1.2", "TLSv1.3"), cipher
        assert experiment.sql("SELECT v FROM phase10.secure WHERE id=1", child) == ((42,),)
    check_single_process(experiment)
    experiment.record("PASS", case="inprocess_tls", protocol=cipher[1], child_login=True)


def run_case(binary, case):
    experiment = BootstrapExperiment(binary, "inprocess_" + case, prototype=6)
    try:
        if case == "tls":
            script = Path(__file__).with_name("generate_wallet.sh")
            subprocess.run([str(script)], cwd=experiment.base, check=True,
                           capture_output=True, text=True)
            experiment.extra_parameters = (
                ("ssl_client_authentication", "true"),
                ("sql_protocol_min_tls_version", "TLSv1.2"),
                ("ob_ssl_invited_common_names", "seekdb-client"))
        experiment.start()
        {"bootstrap": bootstrap_probe, "sql": sql_probe,
         "direct": direct_probe, "tls": tls_probe}[case](experiment)
    finally:
        experiment.close()
