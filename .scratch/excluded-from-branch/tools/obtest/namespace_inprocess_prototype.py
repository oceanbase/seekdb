#!/usr/bin/env python3
"""Single-process paths for the four namespace prototype gates."""
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time

import pymysql
import mysql.connector

from namespace_worker_bootstrap_prototype import BootstrapExperiment


def connect(experiment, branch="root", **kwargs):
    return pymysql.connect(
        host="127.0.0.1", port=experiment.port, user=branch, password="",
        autocommit=True, connect_timeout=3, read_timeout=40, **kwargs)


def drop_after_client_close(experiment, name):
    # COM_QUIT has no reply; client close can precede server lease release.
    for attempt in range(30):
        try:
            experiment.sql(f"DROP NAMESPACE {name}")
            return
        except pymysql.MySQLError as error:
            if (error.args[0] != 4179 or "active connections" not in str(error)
                    or attempt == 29):
                raise
            time.sleep(0.1)


def check_single_process(experiment):
    assert not list((experiment.base / "run").glob("namespace-worker-*"))
    assert experiment.proc.poll() is None

def last_registered_namespace_id(experiment):
    log = (experiment.base / "log/seekdb.log").read_text(errors="replace")
    matches = re.findall(r"PROTOTYPE_NAMESPACE_KV_REGISTER\(ret=0, id=(\d+),", log)
    assert matches, "fork registration missing from server log"
    return int(matches[-1])


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


def setup_branch(experiment, seed_global_index=False):
    experiment.sql("CREATE DATABASE phase10")
    experiment.sql("CREATE TABLE phase10.parent(id INT PRIMARY KEY, v INT)")
    experiment.sql("INSERT INTO phase10.parent VALUES(1,10),(2,20)")
    if seed_global_index:
        experiment.sql("CREATE TABLE phase10.trunc_inherited(id INT PRIMARY KEY,k INT) "
                       "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (100), "
                       "PARTITION p1 VALUES LESS THAN (200))")
        experiment.sql("CREATE UNIQUE INDEX idx_k ON phase10.trunc_inherited(k) GLOBAL "
                       "PARTITION BY HASH(k) PARTITIONS 2")
        experiment.sql("INSERT INTO phase10.trunc_inherited VALUES(1,1),(2,2),(120,3)")
    experiment.sql("FORK NAMESPACE phase10_child FROM ns1")
    experiment.namespace_ids = getattr(experiment, "namespace_ids", {})
    experiment.namespace_ids["phase10_child"] = last_registered_namespace_id(experiment)
    child = connect(experiment, "root@phase10_child")
    assert experiment.sql("SELECT id,v FROM phase10.parent ORDER BY id", child) == ((1, 10), (2, 20))
    check_single_process(experiment)
    return child


def legacy_template_probe(experiment):
    experiment.sql("CREATE DATABASE legacy_template_user")
    experiment.sql("CREATE TABLE legacy_template_user.secret(id INT PRIMARY KEY)")
    experiment.sql("INSERT INTO legacy_template_user.secret VALUES(42)")
    experiment.sql("CREATE TABLE test.legacy_template_rows(id INT PRIMARY KEY)")
    experiment.sql("INSERT INTO test.legacy_template_rows VALUES(9)")
    experiment.sql("CREATE TABLE mysql.legacy_template_rows(id INT PRIMARY KEY)")
    experiment.sql("INSERT INTO mysql.legacy_template_rows VALUES(8)")
    experiment.sql("CREATE VIEW mysql.legacy_template_view AS "
                   "SELECT id FROM mysql.legacy_template_rows")
    experiment.sql("CREATE PROCEDURE mysql.legacy_template_proc() SELECT 11")
    experiment.sql("CREATE USER 'legacy_template_login'@'%' IDENTIFIED BY 'test-pass'")
    experiment.sql("CREATE ROLE legacy_template_role")
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    assert experiment.sql("SELECT id FROM legacy_template_user.secret") == ((42,),)
    assert experiment.sql("SELECT id FROM test.legacy_template_rows") == ((9,),)
    assert experiment.sql("SELECT id FROM mysql.legacy_template_view") == ((8,),)
    assert experiment.sql(
        "SELECT routine_name FROM oceanbase.__all_routine "
        "WHERE routine_name='legacy_template_proc'") == (("legacy_template_proc",),)
    assert experiment.sql(
        "SELECT user_name FROM oceanbase.__all_user "
        "WHERE user_name='legacy_template_login'") == (("legacy_template_login",),)
    assert experiment.sql(
        "SELECT user_name FROM oceanbase.__all_user "
        "WHERE user_name='legacy_template_role'") == (("legacy_template_role",),)
    experiment.sql("CREATE NAMESPACE phase10_migrated")
    with connect(experiment, "root@phase10_migrated", database="test") as migrated:
        assert experiment.sql("SELECT DATABASE()", migrated) == (("test",),)
        assert "legacy_template_user" not in {
            row[0] for row in experiment.sql("SHOW DATABASES", migrated)}
        assert ("legacy_template_rows",) not in experiment.sql("SHOW TABLES FROM test", migrated)
        assert not [row for row in experiment.sql("SHOW TABLES FROM mysql", migrated)
                    if row[0].startswith("legacy_template_")]
        assert experiment.sql(
            "SELECT routine_name FROM oceanbase.__all_routine "
            "WHERE routine_name='legacy_template_proc'", migrated) == ()
        assert experiment.sql(
            "SELECT user_name FROM oceanbase.__all_user "
            "WHERE user_name='legacy_template_login'", migrated) == ()
        assert experiment.sql(
            "SELECT user_name FROM oceanbase.__all_user "
            "WHERE user_name='legacy_template_role'", migrated) == ()
        experiment.sql("CREATE TABLE test.owned(id INT PRIMARY KEY)", migrated)
        experiment.sql("INSERT INTO test.owned VALUES(7)", migrated)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, "root@phase10_migrated", database="test") as migrated:
        assert experiment.sql("SELECT id FROM owned", migrated) == ((7,),)
        assert ("legacy_template_rows",) not in experiment.sql("SHOW TABLES FROM test", migrated)
        assert not [row for row in experiment.sql("SHOW TABLES FROM mysql", migrated)
                    if row[0].startswith("legacy_template_")]
        assert experiment.sql(
            "SELECT routine_name FROM oceanbase.__all_routine "
            "WHERE routine_name='legacy_template_proc'", migrated) == ()
    try:
        pymysql.connect(host="127.0.0.1", port=experiment.port,
                        user="legacy_template_login@phase10_migrated",
                        password="test-pass", connect_timeout=3).close()
    except pymysql.MySQLError:
        pass
    else:
        raise AssertionError("migrated template leaked a legacy login")
    with pymysql.connect(host="127.0.0.1", port=experiment.port,
                         user="legacy_template_login", password="test-pass",
                         connect_timeout=3):
        pass


def legacy_template_resume_probe(binary):
    experiment = BootstrapExperiment(binary, "template_resume_alias", prototype=6)
    try:
        experiment.start()
        for name in ("legacy_resume_a", "legacy_resume_b"):
            experiment.sql(f"CREATE DATABASE {name}")
            experiment.sql(f"CREATE TABLE {name}.t(id INT PRIMARY KEY)")
            experiment.sql(f"INSERT INTO {name}.t VALUES(1)")
        experiment.sql("CREATE TABLE legacy_resume_a.autoinc_t("
                       "id INT AUTO_INCREMENT PRIMARY KEY, payload INT)")
        experiment.sql("INSERT INTO legacy_resume_a.autoinc_t(payload) VALUES(10)")
        experiment.sql("CREATE USER 'legacy_resume_login'@'%' IDENTIFIED BY 'test-pass'")
        experiment.sql("CREATE PROCEDURE mysql.legacy_resume_proc() SELECT 11")
        experiment.sql("DELETE FROM __fork_proto_meta.namespaces WHERE name='__template__'")
        experiment.sql("FORK NAMESPACE template_staging FROM ns1")
        staging_id = experiment.sql(
            "SELECT namespace_id FROM __fork_proto_meta.namespaces "
            "WHERE name='template_staging'")[0][0]
        with connect(experiment, "root@template_staging") as staging:
            experiment.sql("DROP DATABASE legacy_resume_a", staging)
        experiment.sql(
            "UPDATE __fork_proto_meta.namespaces SET name='__template_build__' "
            "WHERE name='template_staging'")
        experiment.sql("UPDATE __fork_proto_meta.namespaces SET name='a' WHERE namespace_id=1")
        experiment.connection.close()
        experiment.connection = None
        experiment.proc.terminate()
        experiment.proc.wait(timeout=20)
        experiment.start()
        assert experiment.sql(
            "SELECT namespace_id,parent_namespace FROM __fork_proto_meta.namespaces "
            "WHERE name='__template__'") == ((staging_id, 1),)
        assert experiment.sql("SELECT id FROM legacy_resume_a.t") == ((1,),)
        assert experiment.sql("SELECT id FROM legacy_resume_b.t") == ((1,),)
        experiment.sql("INSERT INTO legacy_resume_a.autoinc_t(payload) VALUES(20)")
        autoinc_rows = experiment.sql(
            "SELECT id,payload FROM legacy_resume_a.autoinc_t ORDER BY id")
        assert len(autoinc_rows) == 2
        assert autoinc_rows[0] == (1, 10)
        assert autoinc_rows[1][0] > autoinc_rows[0][0]
        assert autoinc_rows[1][1] == 20
        experiment.sql("CREATE NAMESPACE resumed_empty")
        with connect(experiment, "root@resumed_empty", database="test") as empty:
            assert "legacy_resume_a" not in {
                row[0] for row in experiment.sql("SHOW DATABASES", empty)}
            assert "legacy_resume_b" not in {
                row[0] for row in experiment.sql("SHOW DATABASES", empty)}
            assert experiment.sql(
                "SELECT user_name FROM oceanbase.__all_user "
                "WHERE user_name='legacy_resume_login'", empty) == ()
            assert experiment.sql(
                "SELECT routine_name FROM oceanbase.__all_routine "
                "WHERE routine_name='legacy_resume_proc'", empty) == ()
            experiment.sql("CREATE TABLE test.owned(id INT PRIMARY KEY)", empty)
            experiment.sql("INSERT INTO test.owned VALUES(7)", empty)
        experiment.connection.close()
        experiment.connection = None
        experiment.proc.terminate()
        experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, "root@resumed_empty", database="test") as empty:
            assert experiment.sql("SELECT id FROM owned", empty) == ((7,),)
        experiment.record("template_resume_alias", staging_id=staging_id, restart=True)
    finally:
        experiment.close()


def bootstrap_probe(experiment):
    # mysqltest regressions: select_basic, column_alias, view,
    # table_column_related_views, create_using_type, special_stmt.
    assert experiment.sql("SELECT 1") == ((1,),)
    legacy_template_probe(experiment)
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
        assert "phase10" not in databases and "legacy_template_user" not in databases, databases
        assert "__fork_proto_meta" not in databases, databases
        assert ("legacy_template_rows",) not in experiment.sql("SHOW TABLES FROM test", empty)
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
        assert experiment.sql("SHOW COLUMNS FROM fresh.v", empty)[0][0] == "id"
        experiment.sql("CREATE TABLE fresh.using_hash_t(c1 INT, PRIMARY KEY USING HASH (c1))", empty)
        experiment.sql("CREATE TABLE fresh.using_btree_t(c1 INT, PRIMARY KEY USING BTREE (c1))", empty)
        using_types = experiment.sql(
            "SELECT t.table_name,t.index_using_type FROM oceanbase.__all_table t "
            "JOIN oceanbase.__all_database d ON t.database_id=d.database_id "
            "WHERE d.database_name='fresh' AND t.table_name IN "
            "('using_hash_t','using_btree_t') ORDER BY t.table_name", empty)
        assert len(using_types) == 2 and using_types[0][1] != using_types[1][1], using_types
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
    experiment.record("bootstrap_checks", one_process=True, child_login=True,
                      inherited_read=True, empty_namespace=True)


def sql_probe(experiment):
    # mysqltest regressions: view_2 and the child SET GLOBAL permission cases.
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
        try:
            experiment.sql("SET GLOBAL ob_sql_work_area_percentage=100", child)
        except pymysql.MySQLError as error:
            assert error.args[0] == 1227, error.args
        else:
            raise AssertionError("child changed a process global variable")
        experiment.sql("CREATE NAMESPACE phase10_fresh")
        try:
            experiment.sql("CREATE NAMESPACE forbidden_from_child", child)
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("child created a namespace")
        experiment.sql("CREATE TABLE phase10.owned(id INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.owned VALUES(1,11),(2,22)", child)
        experiment.sql("CREATE USER 'phase10_revoke_probe'@'%' IDENTIFIED BY 'ProbePass9!'", child)
        experiment.sql("GRANT SUPER ON *.* TO 'phase10_revoke_probe'@'%'", child)
        experiment.sql("REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'phase10_revoke_probe'@'%'", child)
        experiment.sql("SET NAMES utf8mb4", child)
        assert experiment.sql("SELECT CURRENT_USER()", child)[0][0].startswith("root@")
        experiment.sql("CREATE VIEW phase10.owned_view AS SELECT id,v FROM phase10.owned", child)
        assert experiment.sql("SELECT v FROM phase10.owned_view WHERE id=2", other) == ((22,),)
        experiment.sql("CREATE OR REPLACE VIEW phase10.owned_view AS "
                       "SELECT id,v+1 AS next_v FROM phase10.owned", child)
        assert experiment.sql("SELECT next_v FROM phase10.owned_view WHERE id=2", other) == ((23,),)
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
    with connect(experiment, "root@phase10_child") as child, \
            connect(experiment, "root@phase10_grandchild") as grandchild:
        experiment.sql("ALTER TABLE phase10.parent ADD COLUMN note INT DEFAULT 7", grandchild)
        for connection in (child, grandchild):
            experiment.sql("PREPARE ns_ps FROM 'SELECT * FROM phase10.parent WHERE id=1'", connection)
        assert experiment.sql("EXECUTE ns_ps", child) == ((1, 30),)
        assert experiment.sql("EXECUTE ns_ps", grandchild) == ((1, 30, 7),)
        for connection in (child, grandchild):
            experiment.sql("DEALLOCATE PREPARE ns_ps", connection)
    with connect(experiment, "root@phase10_child") as child:
        parent_us = median_query_us(experiment.connection)
        child_us = median_query_us(child)
        experiment.record("inprocess_sql_latency", parent_us=parent_us, child_us=child_us)
        assert child_us < parent_us * 4, (parent_us, child_us)
    check_single_process(experiment)
    experiment.record("PASS", case="inprocess_sql", transactions=True,
                      index=True, nested_fork=True, restart=True)


def stats_probe(experiment):
    with setup_branch(experiment) as child:
        table_id = experiment.sql(
            "SELECT table_id FROM oceanbase.__all_table WHERE table_name='parent'")[0][0]
        assert experiment.sql(
            "SELECT table_id FROM oceanbase.__all_table WHERE table_name='parent'", child
        )[0][0] == table_id
        experiment.sql("INSERT INTO phase10.parent VALUES " + ",".join(
            f"({i},{i * 10})" for i in range(3, 53)), child)
        experiment.sql("ANALYZE TABLE phase10.parent")
        experiment.sql("ANALYZE TABLE phase10.parent", child)

        def row_count(connection):
            rows = experiment.sql(
                f"SELECT row_cnt FROM oceanbase.__all_table_stat "
                f"WHERE table_id={table_id} ORDER BY row_cnt DESC", connection)
            assert rows, (table_id, rows)
            return rows[0][0]

        assert row_count(experiment.connection) == 2
        assert row_count(child) == 52
        assert row_count(experiment.connection) == 2
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    experiment.start()
    with connect(experiment, "root@phase10_child") as child:
        assert row_count(experiment.connection) == 2
        assert row_count(child) == 52
    experiment.record("PASS", case="inprocess_stats", same_table_id=True,
                      distinct_row_counts=True, restart=True)


def direct_probe(experiment):
    # mysqltest regressions: truncate_table, join_basic, bulk_insert,
    # two_order_by, idx_unique_many_idx_one_ins, generated_column,
    # rename_table2; plus child FTS and IVF index lifecycle.
    from namespace_inprocess_ddl_regressions import (
        before_restart, after_restart, checksum_error_before_restart,
        inject_checksum_errors, checksum_error_after_restart,
        interrupted_heap_recovery)

    with setup_branch(experiment, seed_global_index=True) as child:
        # Root and two forks inherit the same logical table ID. Exercise
        # simultaneous DDL so the process-wide scheduler keeps all three tasks.
        experiment.sql("FORK NAMESPACE phase10_key_peer FROM ns1")
        root_table_id = experiment.sql(
            "SELECT table_id FROM oceanbase.__all_table WHERE table_name='parent'")[0][0]
        child_table_id = experiment.sql(
            "SELECT table_id FROM oceanbase.__all_table WHERE table_name='parent'", child)[0][0]
        with connect(experiment, "root@phase10_key_peer") as peer:
            for connection, user in ((experiment.connection, "phase10_login_probe"),
                                     (child, "phase10_login_probe@phase10_child"),
                                     (peer, "phase10_login_probe@phase10_key_peer")):
                experiment.sql("CREATE USER 'phase10_login_probe'@'%' "
                               "IDENTIFIED BY 'ProbePass9!'", connection)
                experiment.sql("GRANT SELECT ON phase10.* TO "
                               "'phase10_login_probe'@'%'", connection)
                with pymysql.connect(host="127.0.0.1", port=experiment.port,
                                     user=user, password="ProbePass9!", autocommit=True,
                                     connect_timeout=3, read_timeout=40) as login:
                    assert experiment.sql("SELECT v FROM phase10.parent WHERE id=1", login) == ((10,),)
            for connection, value in ((experiment.connection, 7), (child, 11), (peer, 13)):
                experiment.sql("CREATE FUNCTION phase10.owner_udf() RETURNS INT "
                               f"RETURN {value}", connection)
                assert experiment.sql("SELECT phase10.owner_udf()", connection) == ((value,),)
            peer_table_id = experiment.sql(
                "SELECT table_id FROM oceanbase.__all_table WHERE table_name='parent'", peer)[0][0]
            assert root_table_id == child_table_id == peer_table_id, (
                root_table_id, child_table_id, peer_table_id)
            # Materialize each fork's data tablet before creating its auxiliary
            # index tablet; the native MDS helper requires a physical base.
            for connection in (child, peer):
                experiment.sql("UPDATE phase10.parent SET v=11 WHERE id=1", connection)
                experiment.sql("UPDATE phase10.parent SET v=10 WHERE id=1", connection)
            start = threading.Barrier(4)
            ddl_errors = []

            def create_owner_index(connection):
                try:
                    start.wait(timeout=10)
                    experiment.sql("CREATE UNIQUE INDEX owner_key_v ON phase10.parent(v)", connection, log=False)
                except Exception as error:
                    ddl_errors.append(error)

            builders = [threading.Thread(target=create_owner_index, args=(connection,))
                        for connection in (experiment.connection, child, peer)]
            for builder in builders:
                builder.start()
            start.wait(timeout=10)
            for builder in builders:
                builder.join(timeout=60)
            assert not any(builder.is_alive() for builder in builders), "parallel DDL did not finish"
            assert not ddl_errors, ddl_errors
            for connection in (experiment.connection, child, peer):
                assert experiment.sql(
                    "SELECT id FROM phase10.parent FORCE INDEX(owner_key_v) WHERE v=20",
                    connection) == ((2,),)
            experiment.record("same_logical_id_parallel_ddl", table_id=root_table_id,
                              namespaces=3)
            # COM_STMT_EXECUTE previously reached transaction acquisition
            # without a storage binding and returned OB_NOT_INIT.
            experiment.sql("UPDATE phase10.parent SET v=11 WHERE id=1", child)
            experiment.sql("UPDATE phase10.parent SET v=21 WHERE id=1", peer)
            for user, expected in (("root", 10), ("root@phase10_child", 11),
                                   ("root@phase10_key_peer", 21)):
                with mysql.connector.connect(
                        host="127.0.0.1", port=experiment.port, user=user,
                        password="", database="phase10", autocommit=True,
                        use_pure=True) as prepared_connection:
                    with prepared_connection.cursor(prepared=True) as prepared_cursor:
                        for _ in range(2):
                            prepared_cursor.execute(
                                "SELECT v FROM parent WHERE id=%s", (1,))
                            assert prepared_cursor.fetchall() == [(expected,)]
            for connection in (experiment.connection, child, peer):
                ps_count = experiment.sql(
                    "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat",
                    connection)[0][0]
                assert ps_count >= 1
                for virtual_table in ("__all_virtual_plan_cache_stat",
                                      "__all_virtual_plan_stat",
                                      "__all_virtual_sql_plan",
                                      "__all_virtual_plan_cache_plan_explain",
                                      "__all_virtual_change_stream_refresh_stat"):
                    assert experiment.sql(
                        f"SELECT COUNT(*) FROM oceanbase.{virtual_table}",
                        connection)
            root_ps_count = experiment.sql(
                "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat")[0][0]
            peer_ps_count = experiment.sql(
                "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat", peer)[0][0]
            experiment.sql("ALTER SYSTEM FLUSH PS CACHE", child)
            assert experiment.sql(
                "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat", child) == ((0,),)
            assert experiment.sql(
                "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat")[0][0] == root_ps_count
            assert experiment.sql(
                "SELECT stmt_count FROM oceanbase.__all_virtual_ps_stat", peer)[0][0] == peer_ps_count
            experiment.sql(
                "SELECT /*sqlstat_child_only*/ v FROM phase10.parent "
                "WHERE v=11 AND id=1", child)
            for owner, connection in (("root", experiment.connection),
                                      ("child", child), ("peer", peer)):
                sqlstat_rows = experiment.sql(
                    "SELECT query_sql FROM oceanbase.__all_virtual_sqlstat",
                    connection, log=False)
                owned_rows = [row for row in sqlstat_rows if row[0]
                              and "FROM phase10.parent WHERE v=? AND id=?" in row[0]]
                assert len(owned_rows) == (1 if owner == "child" else 0), (owner, owned_rows)
            experiment.sql("UPDATE phase10.parent SET v=10 WHERE id=1", child)
            experiment.sql("UPDATE phase10.parent SET v=10 WHERE id=1", peer)
            experiment.record("same_logical_id_prepared_execute", namespaces=3)
            # The three isolated catalogs use the same logical job key. The
            # scheduler service selected by each session must update only its
            # own catalog, including the PL enable and wakeup path.
            insert_expired_job = (
                "INSERT INTO oceanbase.__all_scheduler_job "
                "(job_name,job,lowner,powner,cowner,next_date,`interval#`,flag,"
                "exec_env,enabled,auto_drop,end_date) VALUES "
                "('owner_probe',991001,'root','root','root',NOW(),'null',0,'x',0,0,"
                "DATE_SUB(NOW(), INTERVAL 1 DAY))")
            for connection in (experiment.connection, child, peer):
                experiment.sql(insert_expired_job, connection)
                experiment.sql("CALL dbms_scheduler.enable('owner_probe')", connection)
            for _ in range(40):
                states = tuple(experiment.sql(
                    "SELECT job,state,enabled FROM oceanbase.__all_scheduler_job "
                    "WHERE job_name='owner_probe'", connection, log=False)
                    for connection in (experiment.connection, child, peer))
                if states == (((991001, "COMPLETED", 0),),) * 3:
                    break
                time.sleep(1)
            else:
                raise AssertionError("namespace scheduler ownership: %r" % (states,))
            experiment.record("same_logical_id_dbms_scheduler", namespaces=3)
            # A dead owner may leave only catalog metadata behind. GC must
            # remove each Namespace's record even when the physical lock is absent.
            orphan_owner = 4000000000
            orphan_query = (
                "SELECT owner_id,obj_id FROM oceanbase.__all_detect_lock_info_v2 "
                f"WHERE owner_id={orphan_owner}")
            for connection, lock_name in ((experiment.connection, "root_gc_orphan"),
                                          (child, "child_gc_orphan"),
                                          (peer, "peer_gc_orphan")):
                assert experiment.sql(f"SELECT GET_LOCK('{lock_name}', 0)", connection) == ((1,),)
                experiment.sql(
                    "INSERT INTO oceanbase.__all_detect_lock_info_v2 "
                    "(task_type,obj_type,obj_id,lock_mode,owner_type,owner_id,cnt,"
                    "detect_func_no,detect_func_param) "
                    "SELECT task_type,obj_type,obj_id+100000,lock_mode,owner_type,"
                    f"{orphan_owner},cnt,detect_func_no,detect_func_param "
                    "FROM oceanbase.__all_detect_lock_info_v2 "
                    f"WHERE owner_id<>{orphan_owner} LIMIT 1", connection)
                assert experiment.sql(f"SELECT RELEASE_LOCK('{lock_name}')", connection) == ((1,),)
            for _ in range(60):
                root_orphans = experiment.sql(orphan_query, log=False)
                child_orphans = experiment.sql(orphan_query, child, log=False)
                peer_orphans = experiment.sql(orphan_query, peer, log=False)
                if not root_orphans and not child_orphans and not peer_orphans:
                    break
                time.sleep(.5)
            assert not root_orphans, root_orphans
            assert not child_orphans, child_orphans
            assert not peer_orphans, peer_orphans
            experiment.record("namespace_lock_gc_orphans_reclaimed", namespaces=3)
        # The system Namespace uses the same owner-bound DDL verifier contract.
        experiment.sql("CREATE TABLE phase10.root_checksum_owner(id INT PRIMARY KEY, v INT)")
        experiment.sql("INSERT INTO phase10.root_checksum_owner VALUES(1,7),(2,9)")
        experiment.sql("CREATE INDEX idx_v ON phase10.root_checksum_owner(v)")
        experiment.sql("ALTER TABLE phase10.root_checksum_owner MODIFY COLUMN v BIGINT")
        assert experiment.sql("SELECT id,v FROM phase10.root_checksum_owner ORDER BY id") == ((1,7),(2,9))
        experiment.sql("TRUNCATE TABLE phase10.parent", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.parent", child) == ((0,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.parent") == ((2,),)
        experiment.sql("INSERT INTO phase10.parent VALUES(3,30)", child)
        assert experiment.sql("SELECT id FROM phase10.parent", child) == ((3,),)
        experiment.sql("CREATE TABLE phase10.records(id INT PRIMARY KEY, v VARCHAR(64), amount DECIMAL(12,2))", child)
        experiment.sql("INSERT INTO phase10.records VALUES(1,'first',12.34),(2,'second',56.78)", child)
        experiment.sql("ALTER SYSTEM MINOR FREEZE", child)
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
        experiment.sql("CREATE INDEX records_amount_expr ON phase10.records ((amount + 1))", child)
        assert experiment.sql(
            "SELECT id FROM phase10.records FORCE INDEX(records_amount_expr) "
            "WHERE amount + 1 = 13.34", child) == ((1,),)
        experiment.sql("PREPARE owned_stmt FROM 'SELECT id FROM phase10.records WHERE id=?'", child)
        experiment.sql("SET @owned_id=2", child)
        assert experiment.sql("EXECUTE owned_stmt USING @owned_id", child) == ((2,),)
        experiment.sql("DEALLOCATE PREPARE owned_stmt", child)
        experiment.sql(
            "CALL DBMS_AI_SERVICE.CREATE_AI_MODEL('phase10_owned_model', "
            "'{\"type\":\"dense_embedding\",\"model_name\":\"phase10-embed\"}')", child)
        assert experiment.sql(
            "SELECT MODEL_NAME FROM oceanbase.DBA_OB_AI_MODELS "
            "WHERE NAME='phase10_owned_model'", child) == (("phase10-embed",),)
        assert experiment.sql(
            "SELECT MODEL_NAME FROM oceanbase.DBA_OB_AI_MODELS "
            "WHERE NAME='phase10_owned_model'") == ()
        for connection, expected_code in ((child, 11112), (experiment.connection, 1210)):
            try:
                experiment.sql("SELECT AI_EMBED('phase10_owned_model','test')", connection)
            except pymysql.MySQLError as error:
                assert error.args[0] == expected_code, error
            else:
                raise AssertionError("AI model lookup unexpectedly succeeded")
        endpoint_definition = ('{\"ai_model_name\":\"phase10_owned_model\",'
                               '\"url\":\"http://127.0.0.1:9/\",'
                               '\"access_key\":\"test\",\"provider\":\"openai\"}')
        endpoint_ddl = ("CALL DBMS_AI_SERVICE.CREATE_AI_MODEL_ENDPOINT("
                        "'phase10_owned_endpoint', '" + endpoint_definition + "')")
        try:
            experiment.sql(endpoint_ddl)
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("root resolved a child-only AI model")
        experiment.sql(endpoint_ddl, child)
        endpoint_lookup = (
            "SELECT AI_MODEL_NAME FROM oceanbase.DBA_OB_AI_MODEL_ENDPOINTS "
            "WHERE ENDPOINT_NAME='phase10_owned_endpoint'")
        assert experiment.sql(endpoint_lookup, child) == (("phase10_owned_model",),)
        assert experiment.sql(endpoint_lookup) == ()
        experiment.sql("CALL DBMS_AI_SERVICE.DROP_AI_MODEL_ENDPOINT('phase10_owned_endpoint')", child)
        assert experiment.sql(endpoint_lookup, child) == ()
        four_scan = experiment.sql(
            "SELECT id FROM phase10.records WHERE v='first' "
            "UNION ALL SELECT id FROM phase10.records WHERE v='second' "
            "UNION ALL SELECT id FROM phase10.records WHERE v='first' "
            "UNION ALL SELECT id FROM phase10.records WHERE v='second'", child)
        assert sorted(row[0] for row in four_scan) == [1, 1, 2, 2], four_scan
        four_way_set = experiment.sql(
            "(SELECT id FROM phase10.records WHERE id=1 ORDER BY id) "
            "UNION (SELECT id FROM phase10.records WHERE id=2 ORDER BY id) "
            "UNION ALL (SELECT id FROM phase10.records WHERE id=1 ORDER BY id) "
            "EXCEPT (SELECT id FROM phase10.records WHERE id=2 ORDER BY id)", child)
        assert four_way_set == ((1,),), four_way_set
        experiment.sql("CREATE TABLE phase10.unique_index_error(pk INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.unique_index_error VALUES(1,610),(2,610)", child)
        try:
            experiment.sql("CREATE UNIQUE INDEX unique_v ON phase10.unique_index_error(v)", child)
        except pymysql.err.IntegrityError as error:
            assert error.args == (1062, "Duplicate entry '610' for key 'unique_v'"), error.args
        else:
            raise AssertionError("unique index accepted duplicate values")
        experiment.sql("CREATE TABLE phase10.root_unique_index_error(pk INT PRIMARY KEY, v INT)")
        experiment.sql("INSERT INTO phase10.root_unique_index_error VALUES(1,620),(2,620)")
        try:
            experiment.sql("CREATE UNIQUE INDEX unique_v ON phase10.root_unique_index_error(v)")
        except pymysql.err.IntegrityError as error:
            assert error.args == (1062, "Duplicate entry '620' for key 'unique_v'"), error.args
        else:
            raise AssertionError("root unique index accepted duplicate values")
        experiment.sql("CREATE TABLE phase10.ignore_rows(pk INT PRIMARY KEY, v INT, UNIQUE KEY uq_v(v))", child)
        experiment.sql("INSERT INTO phase10.ignore_rows VALUES(1,10)", child)
        experiment.sql("INSERT IGNORE INTO phase10.ignore_rows VALUES(2,20),(3,10),(4,40)", child)
        assert experiment.sql("SELECT pk,v FROM phase10.ignore_rows ORDER BY pk", child) == (
            (1, 10), (2, 20), (4, 40))
        experiment.sql("CREATE TABLE phase10.parts(id INT PRIMARY KEY, v INT) PARTITION BY HASH(id) PARTITIONS 4", child)
        experiment.sql("INSERT INTO phase10.parts VALUES(1,10),(2,20),(3,30),(4,40)", child)
        assert experiment.sql("SELECT COUNT(*),SUM(v) FROM phase10.parts", child) == ((4, 100),)
        assert experiment.sql(
            "SELECT /*+ parallel(2) */ SUM(v) FROM phase10.parts", child) == ((100,),)
        experiment.sql("START TRANSACTION", child)
        assert experiment.sql(
            "SELECT /*+ parallel(2) */ SUM(v) FROM phase10.parts", child) == ((100,),)
        experiment.sql("COMMIT", child)
        experiment.sql(
            "CREATE TABLE phase10.exchange_parts(id INT PRIMARY KEY, v INT) "
            "PARTITION BY RANGE(id) (PARTITION p0 VALUES LESS THAN (10), "
            "PARTITION p1 VALUES LESS THAN (MAXVALUE))", child)
        experiment.sql("CREATE TABLE phase10.exchange_plain(id INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.exchange_parts VALUES(1,11),(11,111)", child)
        experiment.sql("INSERT INTO phase10.exchange_plain VALUES(2,22)", child)
        experiment.sql(
            "ALTER TABLE phase10.exchange_parts EXCHANGE PARTITION p0 "
            "WITH TABLE phase10.exchange_plain WITHOUT VALIDATION", child)
        assert experiment.sql(
            "SELECT id,v FROM phase10.exchange_parts ORDER BY id", child) == ((2, 22), (11, 111))
        assert experiment.sql(
            "SELECT id,v FROM phase10.exchange_plain ORDER BY id", child) == ((1, 11),)
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
        experiment.sql(
            "CREATE TABLE phase10.empty_index_parts(c1 INT,c2 VARCHAR(20),c3 CHAR(50),"
            "INDEX idx(c2(5)),INDEX idx2(c2(7)))", child)
        experiment.sql(
            "ALTER TABLE phase10.empty_index_parts ADD INDEX idx3(c3(20))", child)
        experiment.sql(
            "INSERT INTO phase10.empty_index_parts VALUES(1,'first','third')", child)
        assert experiment.sql(
            "SELECT c1 FROM phase10.empty_index_parts FORCE INDEX(idx3) "
            "WHERE c3='third'", child) == ((1,),)
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
        experiment.sql("CREATE TABLE phase10.utf8_lob(id INT PRIMARY KEY, payload MEDIUMTEXT)", child)
        with child.cursor() as cursor:
            cursor.execute("INSERT INTO phase10.utf8_lob VALUES(1,%s)",
                           ("界" * 8000 + "山" * 8000,))
        assert experiment.sql(
            "SELECT SUBSTR(payload,7999,4),SUBSTR(payload,-1,1) "
            "FROM phase10.utf8_lob WHERE id=1", child) == (("界界山山", "山"),)
        experiment.sql("CREATE TABLE phase10.runtime_ddl(id INT PRIMARY KEY, v INT)", child)
        with child.cursor() as cursor:
            cursor.executemany("INSERT INTO phase10.runtime_ddl VALUES(%s,%s)",
                               [(i, i + 1000) for i in range(1, 201)])
        experiment.sql("CREATE UNIQUE INDEX runtime_v ON phase10.runtime_ddl(v)", child)
        experiment.sql("ALTER TABLE phase10.runtime_ddl MODIFY COLUMN v BIGINT", child)
        assert experiment.sql("SELECT COUNT(*),SUM(v) FROM phase10.runtime_ddl", child) == ((200, 220100),)
        experiment.sql("CREATE TABLE phase10.rename_a(id INT PRIMARY KEY, v INT)", child)
        experiment.sql("INSERT INTO phase10.rename_a VALUES(1,8)", child)
        experiment.sql("CREATE INDEX rename_v ON phase10.rename_a(v)", child)
        experiment.sql("RENAME TABLE phase10.rename_a TO phase10.rename_tmp, "
                       "phase10.rename_tmp TO phase10.rename_b", child)
        assert experiment.sql("SELECT id FROM phase10.rename_b FORCE INDEX(rename_v) "
                              "WHERE v=8", child) == ((1,),)
        assert ("rename_b",) in experiment.sql("SHOW TABLES FROM phase10", child)
        experiment.sql("ALTER TABLE phase10.records ADD COLUMN revision INT DEFAULT 7", child)
        assert experiment.sql("SELECT revision FROM phase10.records WHERE id=1", child) == ((7,),)
        experiment.sql("DROP INDEX records_v ON phase10.records", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.records", child) == ((2,),)
        experiment.sql("CREATE TABLE phase10.fulltext_rows(id INT PRIMARY KEY, body TEXT)", child)
        experiment.sql("INSERT INTO phase10.fulltext_rows VALUES"
                       "(1,'alpha word'),(2,'beta word')", child)
        experiment.sql("CREATE FULLTEXT INDEX fulltext_body ON phase10.fulltext_rows(body)", child)
        assert experiment.sql("SELECT id FROM phase10.fulltext_rows "
                              "WHERE MATCH(body) AGAINST('alpha')", child) == ((1,),)
        experiment.sql("CREATE TABLE phase10.fulltext_heap(body TEXT, v INT)", child)
        experiment.sql("INSERT INTO phase10.fulltext_heap VALUES"
                       "('alpha text',7),('beta text',8)", child)
        experiment.sql("CREATE FULLTEXT INDEX heap_body ON phase10.fulltext_heap(body)", child)
        experiment.sql("ALTER TABLE phase10.fulltext_heap MODIFY COLUMN v VARCHAR(20)", child)
        assert experiment.sql("SELECT v FROM phase10.fulltext_heap "
                              "WHERE MATCH(body) AGAINST('alpha')", child) == (("7",),)
        experiment.sql("INSERT INTO phase10.fulltext_heap VALUES('gamma text','9')", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.fulltext_heap", child) == ((3,),)
        experiment.sql("CREATE TABLE phase10.ik_rows(id INT PRIMARY KEY, body TEXT)", child)
        experiment.sql("INSERT INTO phase10.ik_rows VALUES"
                       "(1,'alpha word'),(2,'beta word')", child)
        experiment.sql("CREATE FULLTEXT INDEX ik_body ON phase10.ik_rows(body) "
                       "WITH PARSER ik", child)
        assert experiment.sql("SELECT id FROM phase10.ik_rows "
                              "WHERE MATCH(body) AGAINST('alpha')", child) == ((1,),)
        experiment.sql("ALTER SYSTEM SET vector_index_optimize_duty_time='[00:00:00,23:59:59]'")
        experiment.sql("CREATE TABLE phase10.ivf_rows(id INT PRIMARY KEY, embedding VECTOR(3))", child)
        experiment.sql("INSERT INTO phase10.ivf_rows VALUES "
                       "(1,'[1,0,0]'),(2,'[2,0,0]'),(3,'[3,0,0]'),"
                       "(4,'[4,0,0]'),(5,'[5,0,0]'),(6,'[6,0,0]')", child)
        experiment.sql("SET ob_query_timeout=120000000", child)
        experiment.sql("CREATE VECTOR INDEX ivf_embedding ON phase10.ivf_rows(embedding) "
                       "WITH (distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=3)", child)
        child_namespace_id = experiment.namespace_ids["phase10_child"]
        cache_rows = ()
        for _ in range(18):
            cache_rows = experiment.sql(
                "SELECT rowkey_vid_tablet_id,statistics FROM "
                "oceanbase.__all_virtual_vector_index_info", log=False)
            if any((tablet_id >> 37) & ((1 << 25) - 1) == child_namespace_id
                   and "cache_type=0" in statistics and "count=2" in statistics
                   for tablet_id, statistics in cache_rows):
                break
            time.sleep(2)
        else:
            raise AssertionError("child IVF background cache was not loaded: %r" % (cache_rows,))
        nearest_ivf = "SELECT id FROM phase10.ivf_rows ORDER BY "
        nearest_ivf += "l2_distance(embedding,[0,0,0]) APPROXIMATE LIMIT 1"
        assert experiment.sql(nearest_ivf, child) == ((1,),)
        experiment.sql("CREATE TABLE phase10.ivf_pq_rows(id INT PRIMARY KEY, embedding VECTOR(4))", child)
        pq_values = ",".join("(%d,'[%d,%d,%d,%d]')" % (i, i, i, i, i)
                             for i in range(1, 21))
        experiment.sql("INSERT INTO phase10.ivf_pq_rows VALUES " + pq_values, child)
        experiment.sql("CREATE VECTOR INDEX pq_embedding ON phase10.ivf_pq_rows(embedding) "
                       "WITH (distance=l2,type=ivf_pq,nlist=2,sample_per_nlist=5,m=2)", child)
        pq_cache_rows = ()
        for _ in range(18):
            pq_cache_rows = experiment.sql(
                "SELECT rowkey_vid_tablet_id,statistics FROM "
                "oceanbase.__all_virtual_vector_index_info", log=False)
            if any((tablet_id >> 37) & ((1 << 25) - 1) == child_namespace_id
                   and re.search(r"cache_type=1;[^}]*count=40;", statistics)
                   for tablet_id, statistics in pq_cache_rows):
                break
            time.sleep(2)
        else:
            raise AssertionError("child IVF_PQ background cache was not loaded: %r" % (pq_cache_rows,))
        nearest_pq = "SELECT id FROM phase10.ivf_pq_rows ORDER BY "
        nearest_pq += "l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1"
        pq_result = experiment.sql(nearest_pq, child)
        assert len(pq_result) == 1 and 1 <= pq_result[0][0] <= 20, pq_result
        pq_cache_rows = experiment.sql(
            "SELECT rowkey_vid_tablet_id,statistics FROM "
            "oceanbase.__all_virtual_vector_index_info", log=False)
        assert any((tablet_id >> 37) & ((1 << 25) - 1) == child_namespace_id
                   and re.search(r"cache_type=1;[^}]*count=40;", statistics)
                   for tablet_id, statistics in pq_cache_rows), pq_cache_rows
        experiment.sql("CREATE TABLE phase10.ivf_sq8_rows(id INT PRIMARY KEY, embedding VECTOR(4))", child)
        experiment.sql("INSERT INTO phase10.ivf_sq8_rows VALUES " + pq_values, child)
        experiment.sql("CREATE VECTOR INDEX sq8_embedding ON phase10.ivf_sq8_rows(embedding) "
                       "WITH (distance=l2,type=ivf_sq8,nlist=2,sample_per_nlist=5)", child)
        nearest_sq8 = "SELECT id FROM phase10.ivf_sq8_rows ORDER BY "
        nearest_sq8 += "l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1"
        assert experiment.sql(nearest_sq8, child) == ((1,),)
        experiment.sql("CREATE TABLE phase10.empty_hnsw(id INT PRIMARY KEY, embedding VECTOR(3))", child)
        experiment.sql("CREATE VECTOR INDEX empty_embedding ON phase10.empty_hnsw(embedding) "
                       "WITH (distance=l2,type=hnsw,lib=vsag)", child)
        experiment.sql("INSERT INTO phase10.empty_hnsw VALUES(1,'[1,0,0]')", child)
        experiment.sql("CALL dbms_vector.refresh_index("
                       "'phase10.empty_embedding','phase10.empty_hnsw',NULL,10000,NULL)", child)
        experiment.sql("CREATE TABLE phase10.ivf_inherited_rows(id INT PRIMARY KEY, embedding VECTOR(4))")
        experiment.sql("INSERT INTO phase10.ivf_inherited_rows VALUES "
                       "(1,'[1,0,0,0]'),(2,'[2,0,0,0]'),(3,'[3,0,0,0]'),"
                       "(4,'[4,0,0,0]'),(5,'[5,0,0,0]'),(6,'[6,0,0,0]')")
        experiment.sql("CREATE VECTOR INDEX inherited_embedding ON phase10.ivf_inherited_rows(embedding) "
                       "WITH (distance=l2,type=ivf_flat,nlist=2,sample_per_nlist=5)")
        experiment.sql("FORK NAMESPACE phase10_ivf_inherited FROM ns1")
        inherited_namespace_id = last_registered_namespace_id(experiment)
        inherited_query = "SELECT id FROM phase10.ivf_inherited_rows ORDER BY "
        inherited_query += "l2_distance(embedding,[0,0,0,0]) APPROXIMATE LIMIT 1"
        with connect(experiment, "root@phase10_ivf_inherited") as inherited:
            assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_inherited_rows", inherited) == ((6,),)
        experiment.sql("INSERT INTO phase10.ivf_inherited_rows VALUES(100,'[0,0,0,0]')")
        inherited_cache_rows = ()
        for _ in range(20):
            inherited_cache_rows = experiment.sql(
                "SELECT rowkey_vid_tablet_id,statistics FROM "
                "oceanbase.__all_virtual_vector_index_info", log=False)
            if any((tablet_id >> 37) & ((1 << 25) - 1) == inherited_namespace_id
                   and "cache_type=0" in statistics and "count=2" in statistics
                   for tablet_id, statistics in inherited_cache_rows):
                break
            time.sleep(2)
        else:
            raise AssertionError("inherited IVF background cache was not loaded: %r" %
                                 (inherited_cache_rows,))
        with connect(experiment, "root@phase10_ivf_inherited") as inherited:
            assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_inherited_rows", inherited) == ((6,),)
            assert experiment.sql(inherited_query, inherited) == ((1,),)
        before_restart(experiment, child)
        checksum_error_table_ids = checksum_error_before_restart(experiment, child)
    check_single_process(experiment)
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.terminate()
    experiment.proc.wait(timeout=20)
    inject_checksum_errors(experiment, checksum_error_table_ids)
    experiment.start()
    with connect(experiment, "root@phase10_child") as child:
        after_restart(experiment, child)
        checksum_error_after_restart(experiment, child, checksum_error_table_ids)
        assert experiment.sql(
            "SELECT id,v FROM phase10.exchange_parts ORDER BY id", child) == ((2, 22), (11, 111))
        assert experiment.sql(
            "SELECT id,v FROM phase10.exchange_plain ORDER BY id", child) == ((1, 11),)
        assert experiment.sql("SELECT id FROM phase10.fulltext_rows "
                              "WHERE MATCH(body) AGAINST('beta')", child) == ((2,),)
        assert experiment.sql("SELECT v FROM phase10.fulltext_heap "
                              "WHERE MATCH(body) AGAINST('alpha')", child) == (("7",),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.fulltext_heap", child) == ((3,),)
        assert experiment.sql("SELECT id FROM phase10.ik_rows "
                              "WHERE MATCH(body) AGAINST('beta')", child) == ((2,),)
        assert experiment.sql(nearest_ivf, child) == ((1,),)
        pq_result = experiment.sql(nearest_pq, child)
        assert len(pq_result) == 1 and 1 <= pq_result[0][0] <= 20, pq_result
        assert experiment.sql(nearest_sq8, child) == ((1,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.empty_hnsw", child) == ((1,),)
        assert experiment.sql("SELECT SUBSTR(payload,7999,4) FROM phase10.utf8_lob "
                              "WHERE id=1", child) == (("界界山山",),)
        experiment.sql("DROP INDEX ivf_embedding ON phase10.ivf_rows", child)
        experiment.sql("DROP INDEX pq_embedding ON phase10.ivf_pq_rows", child)
        experiment.sql("DROP INDEX sq8_embedding ON phase10.ivf_sq8_rows", child)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_rows", child) == ((6,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_pq_rows", child) == ((20,),)
        assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_sq8_rows", child) == ((20,),)
    with connect(experiment, "root@phase10_ivf_inherited") as inherited:
        assert experiment.sql("SELECT COUNT(*) FROM phase10.ivf_inherited_rows", inherited) == ((6,),)
        assert experiment.sql(inherited_query, inherited) == ((1,),)
    experiment.sql("FORK NAMESPACE phase10_drop_source FROM ns1")
    with connect(experiment, "root@phase10_drop_source") as source:
        experiment.sql("CREATE TABLE phase10.drop_source_rows(id INT PRIMARY KEY)", source)
        experiment.sql("INSERT INTO phase10.drop_source_rows VALUES(7)", source)
        experiment.sql("FORK NAMESPACE phase10_drop_child FROM phase10_drop_source")
        try:
            experiment.sql("DROP NAMESPACE phase10_drop_source")
        except pymysql.MySQLError:
            pass
        else:
            raise AssertionError("dropped a namespace with an active connection")
    drop_after_client_close(experiment, "phase10_drop_source")
    experiment.sql("CREATE NAMESPACE phase10_drop_source")
    with connect(experiment, "root@phase10_drop_child") as descendant:
        assert experiment.sql("SELECT id FROM phase10.drop_source_rows", descendant) == ((7,),)
        experiment.sql("FORK TABLE phase10.drop_source_rows "
                       "TO phase10.drop_source_copy", descendant)
        assert experiment.sql("SELECT id FROM phase10.drop_source_copy", descendant) == ((7,),)
    with connect(experiment, "root@phase10_drop_source") as replacement:
        assert "phase10" not in {row[0] for row in experiment.sql("SHOW DATABASES", replacement)}
    # A former 32-slot KV cache limit rejected the fifth freshly activated
    # namespace after earlier child schema services had registered caches.
    for index in range(8):
        name = f"phase10_cache_cycle_{index}"
        experiment.sql(f"CREATE NAMESPACE {name}")
        with connect(experiment, f"root@{name}", database="test") as cycle:
            assert experiment.sql("SELECT 1", cycle) == ((1,),)
        drop_after_client_close(experiment, name)
    interrupted_heap_recovery(experiment, connect)
    experiment.record("PASS", case="inprocess_direct", ddl=True, partition=True,
                      index=True, lob=True, fulltext=True, ivf=True, ivf_pq=True,
                      ivf_sq8=True, empty_hnsw=True,
                      source_drop=True, ddl_redefinition=True,
                      check_constraint=True, auto_increment=True,
                      fork_table=True, cache_lifecycle=True,
                      interrupted_heap_recovery=True, restart=True)


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


def async_vector_owner_probe(experiment):
    # Regression reproducer: an inherited async HNSW index currently makes
    # the first child INSERT retry OB_TABLET_NOT_EXIST until query timeout.
    experiment.sql(
        "CREATE TABLE test.async_owner(id INT PRIMARY KEY, embedding VECTOR(4), "
        "VECTOR INDEX idx_embedding(embedding) WITH "
        "(type=hnsw, distance=l2, sync_mode=async)) ORGANIZATION HEAP")
    experiment.sql("INSERT INTO test.async_owner VALUES(1,'[1,0,0,0]')")
    experiment.sql("FORK NAMESPACE async_child FROM ns1")
    with connect(experiment, "root@async_child") as child:
        experiment.sql("INSERT INTO test.async_owner VALUES(2,'[0,1,0,0]')", child)
        assert experiment.sql("SELECT id FROM test.async_owner ORDER BY id", child) == ((1,), (2,))


def async_vector_root_probe(experiment):
    # The root fetcher reaches ACTIVE but the dispatcher sees no transaction,
    # so the approximate query currently waits until its 30-second timeout.
    experiment.sql(
        "CREATE TABLE test.async_root(id INT PRIMARY KEY, embedding VECTOR(4), "
        "VECTOR INDEX idx_embedding(embedding) WITH "
        "(type=hnsw, distance=l2, sync_mode=async)) ORGANIZATION HEAP")
    time.sleep(8)
    experiment.sql("INSERT INTO test.async_root VALUES(1,'[1,0,0,0]')")
    assert experiment.sql(
        "SELECT id FROM test.async_root ORDER BY "
        "l2_distance(embedding,'[1,0,0,0]') APPROXIMATE LIMIT 1") == ((1,),)


def run_case(binary, case):
    experiment = BootstrapExperiment(binary, "inprocess_" + case, prototype=6)
    try:
        if case == "direct":
            experiment.log_level = "INFO"
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
         "direct": direct_probe, "tls": tls_probe,
         "stats": stats_probe,
         "async_vector": async_vector_owner_probe,
         "async_vector_root": async_vector_root_probe}[case](experiment)
        if case == "bootstrap":
            experiment.record("PASS", case="inprocess_bootstrap", one_process=True,
                              child_login=True, inherited_read=True, empty_namespace=True,
                              template_restart=True)
    finally:
        experiment.close()
