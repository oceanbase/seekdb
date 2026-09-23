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
        experiment.sql("CREATE TABLE phase10.records(id INT PRIMARY KEY, v VARCHAR(64), amount DECIMAL(12,2))", child)
        experiment.sql("INSERT INTO phase10.records VALUES(1,'first',12.34),(2,'second',56.78)", child)
        experiment.sql("CREATE INDEX records_v ON phase10.records(v)", child)
        assert experiment.sql("SELECT id FROM phase10.records FORCE INDEX(records_v) WHERE v='second'", child) == ((2,),)
        experiment.sql("CREATE TABLE phase10.parts(id INT PRIMARY KEY, v INT) PARTITION BY HASH(id) PARTITIONS 4", child)
        experiment.sql("INSERT INTO phase10.parts VALUES(1,10),(2,20),(3,30),(4,40)", child)
        assert experiment.sql("SELECT COUNT(*),SUM(v) FROM phase10.parts", child) == ((4, 100),)
        assert experiment.sql(
            "SELECT /*+ parallel(2) */ SUM(v) FROM phase10.parts", child) == ((100,),)
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
