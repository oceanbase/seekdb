#!/usr/bin/env python3
"""Native client ingress into the namespace worker; shared SQL stays forbidden."""
import argparse
import concurrent.futures
import datetime
import decimal
import io
import os
import re
import resource
import subprocess
import threading
import time
import traceback

import pymysql
import mysql.connector
from namespace_worker_bootstrap_prototype import BootstrapExperiment

PARTITIONED_LOB_SOURCE = b"source-lob:" + b"s" * (64 * 1024)
PARTITIONED_LOB_CHILD = b"child-lob:" + b"c" * (65 * 1024)
PARTITIONED_LOB_BRANCH = b"branch-lob:" + b"b" * (66 * 1024)


def connect(endpoint):
    return pymysql.connect(unix_socket=endpoint, user="root", password="",
                           autocommit=True, connect_timeout=3,
                           read_timeout=int(os.environ.get(
                               "SEEKDB_TEST_READ_TIMEOUT", "20")))


def connect_tls(endpoint, wallet):
    return pymysql.connect(
        unix_socket=endpoint, host="127.0.0.1", user="root", password="",
        autocommit=True, connect_timeout=3, read_timeout=20,
        ssl_ca=str(wallet / "ca.pem"),
        ssl_cert=str(wallet / "server-cert.pem"),
        ssl_key=str(wallet / "server-key.pem"),
        ssl_verify_cert=True, ssl_verify_identity=True)


def assert_hash_partitions(ddl, column, count):
    assert re.search(
        rf"partition\s+by\s+hash\s*\(\s*{re.escape(column)}\s*\)",
        ddl,
        re.IGNORECASE), ddl
    partition_names = re.findall(
        r"partition\s+`(p[0-9]+)`",
        ddl,
        re.IGNORECASE)
    assert [name.lower() for name in partition_names] == [
        f"p{index}" for index in range(count)
    ], ddl


def assert_range_partitions(ddl, names):
    partition_names = re.findall(
        r"partition\s+`([^`]+)`\s+values\s+less\s+than",
        ddl,
        re.IGNORECASE)
    assert [name.lower() for name in partition_names] == list(names), ddl


def assert_subpartitions(ddl, names):
    subpartition_names = re.findall(
        r"subpartition\s+`([^`]+)`",
        ddl,
        re.IGNORECASE)
    assert [name.lower() for name in subpartition_names] == list(names), ddl


def connect_ready(endpoint, timeout=60):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return connect(endpoint)
        except pymysql.OperationalError as error:
            if error.args[0] not in (4023, 8001) or time.monotonic() >= deadline:
                raise
            time.sleep(.05)


def process_alive(pid):
    try:
        state = open(f"/proc/{pid}/stat", encoding="ascii").read().split()[2]
        return state != "Z"
    except FileNotFoundError:
        return False


def runtime_log(experiment):
    text = experiment.engine_log()
    output = experiment.base / "process.out"
    if output.exists():
        text += "\n" + output.read_text(errors="replace")
    return text


def refresh_namespace_endpoint_events(experiment):
    """Read each runtime log byte once while waiting for Worker endpoints."""
    state = getattr(experiment, "_namespace_endpoint_log_state", None)
    if state is None:
        state = {"offsets": {}, "tails": {}, "events": []}
        experiment._namespace_endpoint_log_state = state
    paths = sorted((experiment.base / "log").glob("seekdb.log*"))
    output = experiment.base / "process.out"
    if output.exists():
        paths.append(output)
    pattern = re.compile(
        r"PROTOTYPE_V10_WORKER_READY ns=(\d+) generation=(\d+) "
        r"pid=(\d+) endpoint=(\S+)")
    for path in paths:
        try:
            stat = path.stat()
            identity = (stat.st_dev, stat.st_ino)
            offset = state["offsets"].get(identity, 0)
            if stat.st_size < offset:
                offset = 0
                state["tails"].pop(identity, None)
            if stat.st_size == offset:
                continue
            with path.open("rb") as source:
                source.seek(offset)
                payload = source.read()
                state["offsets"][identity] = source.tell()
            text = state["tails"].pop(identity, "") + payload.decode(
                errors="replace")
            lines = text.splitlines(keepends=True)
            if lines and not lines[-1].endswith(("\n", "\r")):
                state["tails"][identity] = lines.pop()
            for line in lines:
                match = pattern.search(line)
                if match:
                    ns, generation, pid, endpoint = match.groups()
                    state["events"].append(
                        (int(ns), int(generation), int(pid), endpoint))
        except FileNotFoundError:
            # Log rotation can rename a path between glob and open. The same
            # inode is consumed under its new name on the next poll.
            continue
    return state["events"]


def namespace_endpoint(experiment, namespace, exclude_pid=None, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for ns, generation, pid, endpoint in reversed(
                refresh_namespace_endpoint_events(experiment)):
            if (ns == namespace and endpoint and pid != exclude_pid
                    and process_alive(pid)):
                return pid, endpoint, generation
        time.sleep(.05)
    raise AssertionError(("namespace endpoint was not published", namespace, exclude_pid))


def endpoint_row(row):
    return tuple(value.decode() if isinstance(value, bytes) else value
                 for value in row)


def wait_process_dead(pid, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and process_alive(pid):
        time.sleep(.05)
    assert not process_alive(pid), ("Worker process remained alive", pid)


def assert_shared_namespace_released(experiment, namespace_ids):
    log = runtime_log(experiment)
    drained = {
        int(namespace): int(ret)
        for namespace, ret in re.findall(
            r"PROTOTYPE_NAMESPACE_ACCESS_DRAINED ns=(\d+) ret=(-?\d+)", log)
    }
    released = {
        int(namespace): (int(tables), int(databases), int(ret))
        for namespace, tables, databases, ret in re.findall(
            r"PROTOTYPE_NAMESPACE_SCHEMA_HOLDERS_RELEASED "
            r"ns=(\d+) tables=(\d+) databases=(\d+) ret=(-?\d+)", log)
    }
    for namespace_id in namespace_ids:
        assert drained.get(namespace_id) == 0, (namespace_id, drained)
        # Native SchemaService authority must not recreate a second schema
        # object cache in the shared storage process.
        assert released.get(namespace_id) == (0, 0, 0), (
            namespace_id, released)
    experiment.record(
        "shared_namespace_lifecycle_released",
        namespaces=list(namespace_ids),
        duplicate_schema_holders=False)


def assert_worker_memory_budgets(experiment, expected_budget):
    records = []
    for output in sorted((experiment.base / "run").glob(
            "namespace-worker-*/process.out")):
        match = re.search(
            r"PROTOTYPE_NAMESPACE_WORKER_RESOURCES "
            r"ns=(\d+) memory_budget=(\d+) threads=(\d+)",
            output.read_text(errors="replace"))
        assert match, output
        records.append(tuple(map(int, match.groups())))
    assert records and {namespace for namespace, _, _ in records} >= {
        1, 2, 3, 4, 5
    }, records
    assert all(budget == expected_budget for _, budget, _ in records), records
    experiment.record(
        "namespace_worker_memory_budget_verified",
        expected_budget=expected_budget,
        workers=len(records),
        namespaces=sorted({namespace for namespace, _, _ in records}))


def protocol_probe(experiment, endpoint):
    result = subprocess.run(["mysql", f"--socket={endpoint}",
                             "-uroot", "--connect-timeout=3", "-N", "-B", "-e", "SELECT 1"],
                            check=True, capture_output=True, text=True, timeout=15)
    assert result.stdout.strip() == "1", result
    with pymysql.connect(unix_socket=endpoint, user="root", autocommit=True,
                         charset="utf8mb4", client_flag=pymysql.constants.CLIENT.MULTI_STATEMENTS,
                         connect_timeout=3, read_timeout=20) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT '中文😀'; SELECT 2")
            assert cursor.fetchone() == ("中文😀",)
            assert cursor.nextset()
            assert cursor.fetchone() == (2,)
            assert not cursor.nextset()
    experiment.record("direct_cli_multiresult_charset_verified")

    connection = mysql.connector.connect(unix_socket=endpoint, user="root", password="",
                                         use_pure=True, ssl_disabled=True, autocommit=True,
                                         database="direct_check", connection_timeout=3)
    connection._socket.sock.settimeout(20)
    try:
        with connection.cursor(prepared=True) as cursor:
            statement = "SELECT id,v FROM t WHERE id=%s"
            for key, value in [(1, "committed"), (2, "second"), (1, "committed")]:
                cursor.execute(statement, (key,))
                assert cursor.fetchall() == [(key, value)]
            payload = b"long-data" * 40000
            cursor.execute("SELECT OCTET_LENGTH(%s)", (io.BytesIO(payload),))
            assert cursor.fetchone() == (len(payload),)
        experiment.record("direct_binary_prepared_long_data_verified")
        with connection.cursor() as cursor:
            cursor.execute("SET @pooled_value=10")
            cursor.execute("BEGIN")
            cursor.execute("UPDATE t SET v='pool rollback' WHERE id=1")
        connection.reset_session()
        with connection.cursor() as cursor:
            cursor.execute("SELECT @pooled_value, v FROM direct_check.t WHERE id=1")
            assert cursor.fetchone() == (None, "committed")
        experiment.record("direct_connection_reset_verified")
    finally:
        connection.close()


def cursor_snapshot_probe(experiment, endpoint):
    with connect(endpoint) as connection, connection.cursor() as cursor:
        cursor.execute("USE direct_check")
        cursor.execute(
            "CREATE PROCEDURE cursor_snapshot_ok(OUT result VARCHAR(64)) BEGIN "
            "DECLARE value VARCHAR(64); "
            "DECLARE c CURSOR FOR SELECT v FROM t ORDER BY id FOR UPDATE; "
            "OPEN c; FETCH c INTO value; SET result=value; CLOSE c; END")
        for _ in range(3):
            cursor.execute("BEGIN")
            cursor.execute("CALL cursor_snapshot_ok(@result)")
            while cursor.nextset():
                pass
            cursor.execute("SELECT @result")
            assert cursor.fetchone() == ("committed",)
            cursor.execute("COMMIT")
        cursor.execute(
            "CREATE PROCEDURE cursor_snapshot_guard(OUT result VARCHAR(64)) BEGIN "
            "DECLARE value VARCHAR(64); "
            "DECLARE c CURSOR FOR SELECT v FROM t ORDER BY id FOR UPDATE; "
            "SET result='unexpected'; SAVEPOINT before_cursor; OPEN c; "
            "ROLLBACK TO SAVEPOINT before_cursor; "
            "FETCH c INTO value; SET result=value; CLOSE c; END")
        cursor.execute("BEGIN")
        try:
            cursor.execute("CALL cursor_snapshot_guard(@result)")
        except pymysql.MySQLError as error:
            assert error.args[0] == 4138, error
        else:
            raise AssertionError("cursor fetch after savepoint rollback succeeded")
        cursor.execute("ROLLBACK")
        cursor.execute("SELECT id,v FROM t ORDER BY id")
        assert cursor.fetchall() == ((1, "committed"), (2, "second"))
    unregisters = [
        line for line in experiment.engine_log().splitlines()
        if "PROTOTYPE_NAMESPACE_TX_SNAPSHOT_UNREGISTER" in line
    ]
    assert len(unregisters) >= 4, unregisters
    assert all("remaining=0 ret=0" in line for line in unregisters[-4:]), unregisters[-4:]
    experiment.record(
        "direct_cursor_snapshot_invalidation_verified",
        for_update=True, savepoint_rollback=True,
        registrations_released=4)


def authentication_probe(experiment, endpoint):
    with connect(endpoint) as root, root.cursor() as cursor:
        cursor.execute("CREATE USER 'direct_reader'@'%' IDENTIFIED BY 'Direct-test-19!'")
        cursor.execute("GRANT SELECT ON direct_check.t TO 'direct_reader'@'%'")
    for user, password in [("direct_reader", "incorrect"), ("missing_reader", "")]:
        try:
            pymysql.connect(unix_socket=endpoint, user=user, password=password,
                            connect_timeout=3, read_timeout=20)
        except pymysql.MySQLError as error:
            assert error.args[0] == 1045, error
        else:
            raise AssertionError("invalid credentials were accepted")
    experiment.record("direct_authentication_rejection_verified")
    with pymysql.connect(unix_socket=endpoint, user="direct_reader", password="Direct-test-19!",
                         database="direct_check", autocommit=True, connect_timeout=3, read_timeout=20) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT v FROM t WHERE id=1")
            assert cursor.fetchone() == ("committed",)
            try:
                cursor.execute("UPDATE t SET v='unauthorized' WHERE id=1")
            except pymysql.MySQLError as error:
                assert error.args[0] == 1142, error
            else:
                raise AssertionError("table write without a grant was accepted")
            with connect(endpoint) as root, root.cursor() as admin:
                admin.execute("REVOKE SELECT ON direct_check.t FROM 'direct_reader'@'%'")
            try:
                cursor.execute("SELECT v FROM t WHERE id=1")
            except pymysql.MySQLError as error:
                assert error.args[0] in (1044, 1142), error
            else:
                raise AssertionError("revoked table grant remained effective")
            with connect(endpoint) as root, root.cursor() as admin:
                admin.execute("GRANT SELECT(v) ON direct_check.t TO 'direct_reader'@'%'")
            cursor.execute("SELECT v FROM t ORDER BY v")
            assert cursor.fetchall() == (("committed",), ("second",))
            try:
                cursor.execute("SELECT id FROM t")
            except pymysql.MySQLError as error:
                assert error.args[0] == 1143, error
            else:
                raise AssertionError("column without a grant was readable")
    experiment.record("direct_table_privileges_verified")


def lifecycle_probe(experiment, endpoint):
    with connect(endpoint) as control, control.cursor() as admin:
        victim = connect(endpoint)
        with victim.cursor() as cursor:
            cursor.execute("BEGIN")
            cursor.execute("UPDATE direct_check.t SET v='disconnected' WHERE id=1")
        victim.close()
        admin.execute("SET ob_query_timeout=3000000")
        admin.execute("UPDATE direct_check.t SET v='committed' WHERE id=1")
        experiment.record("direct_disconnect_rollback_unlock_verified")
        with connect(endpoint) as victim:
            def interrupted_query():
                with victim.cursor() as cursor:
                    try:
                        cursor.execute("SELECT SLEEP(10)")
                    except pymysql.MySQLError as error:
                        assert error.args[0] == 1317, error
                    else:
                        raise AssertionError("KILL QUERY did not interrupt SLEEP")
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                waiting = pool.submit(interrupted_query)
                time.sleep(.2)
                admin.execute(f"KILL QUERY {victim.thread_id()}")
                waiting.result(timeout=5)
            with victim.cursor() as cursor:
                cursor.execute("SELECT 1")
                assert cursor.fetchone() == (1,)
        experiment.record("direct_query_cancel_session_reuse_verified")
        holder = connect(endpoint)
        blocked = connect(endpoint)
        try:
            with holder.cursor() as cursor:
                cursor.execute("BEGIN")
                cursor.execute(
                    "UPDATE direct_check.t SET v='lock holder' WHERE id=1")

            def interrupted_storage_write():
                with blocked.cursor() as cursor:
                    cursor.execute("BEGIN")
                    try:
                        cursor.execute(
                            "UPDATE direct_check.t SET v='blocked' WHERE id=1")
                    except pymysql.MySQLError as error:
                        assert error.args[0] == 1317, error
                    else:
                        raise AssertionError(
                            "KILL QUERY did not interrupt the storage lock wait")

            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                waiting = pool.submit(interrupted_storage_write)
                time.sleep(.2)
                assert not waiting.done(), "write did not wait for the row lock"
                admin.execute(f"KILL QUERY {blocked.thread_id()}")
                waiting.result(timeout=5)
            with blocked.cursor() as cursor:
                cursor.execute("ROLLBACK")
                cursor.execute("SELECT 1")
                assert cursor.fetchone() == (1,)
            experiment.record("direct_storage_lock_wait_cancel_verified")
        finally:
            try:
                holder.rollback()
            finally:
                holder.close()
                blocked.close()
        admin.execute("SET GLOBAL autocommit=0")
        try:
            with pymysql.connect(unix_socket=endpoint, user="root", autocommit=None,
                                 connect_timeout=3, read_timeout=20) as fresh:
                assert not fresh.server_status & 2
                with fresh.cursor() as cursor:
                    cursor.execute("SELECT @@autocommit")
                    assert cursor.fetchone() == (0,)
        finally:
            admin.execute("SET GLOBAL autocommit=1")
        experiment.record("direct_global_variable_greeting_verified")


def management_concurrency_probe(experiment, endpoint):
    def client(index):
        with connect(endpoint) as connection, connection.cursor() as cursor:
            for generation in range(2):
                database = f"concurrent_ddl_{index}_{generation}"
                cursor.execute(f"CREATE DATABASE {database}")
                cursor.execute(f"CREATE TABLE {database}.t(id INT PRIMARY KEY, v INT)")
                cursor.execute(f"INSERT INTO {database}.t VALUES(1,%s)", (index,))
                cursor.execute(f"SELECT v FROM {database}.t WHERE id=1")
                assert cursor.fetchone() == (index,)
                cursor.execute(f"DROP DATABASE {database}")
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(client, range(3)))
    experiment.record("direct_concurrent_management_verified", clients=3, databases=6)


def forked_namespace_probe(experiment, control_endpoint):
    def activate(namespace):
        # FORK publishes the endpoint through the lifecycle path.  Do not use
        # the compatibility SQL router to create the Worker as a side effect.
        return namespace_endpoint(experiment, namespace)

    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SHOW TABLES FROM __fork_proto_meta")
        assert {row[0] for row in cursor.fetchall()} == {
            "endpoints", "namespaces", "pages", "roots", "snapshots"}
        cursor.execute("FORK DATABASE __empty__ TO direct_a")
        cursor.execute("FORK DATABASE direct_a TO direct_b")
        try:
            cursor.execute("FORK DATABASE direct_a TO __drop__")
        except pymysql.MySQLError as error:
            assert error.args[0] == 4179, error
        else:
            raise AssertionError("default namespace was deleted")
        cursor.execute(
            "SELECT namespace_id,state FROM __fork_proto_meta.namespaces "
            "WHERE name='direct_a'")
        assert cursor.fetchone() == (1, 0)
        experiment.record("default_namespace_delete_rejected")
        cursor.execute("SELECT namespace_id,name FROM __fork_proto_meta.namespaces "
                       "WHERE name IN ('direct_b','direct_c')")
        namespaces = {name.decode(): namespace for namespace, name in cursor.fetchall()}
    branch_id = namespaces["direct_b"]
    branch_pid, branch_endpoint, branch_generation = activate(branch_id)
    assert branch_endpoint != control_endpoint
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SELECT worker_pid,endpoint,generation FROM __fork_proto_meta.endpoints "
                       "WHERE namespace_id=%s", (branch_id,))
        assert endpoint_row(cursor.fetchone()) == (
            branch_pid, branch_endpoint, branch_generation)
        cursor.execute("SELECT directory_page,directory_cap,schema_version "
                       "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
                       (branch_id,))
        branch_root_before_ddl = cursor.fetchone()
    with connect(branch_endpoint) as branch, branch.cursor() as cursor:
        cursor.execute("SHOW DATABASES LIKE '__fork_proto_meta'")
        assert cursor.fetchall() == ()
        for statement in (
                "SELECT COUNT(*) FROM __fork_proto_meta.namespaces",
                "DROP TABLE __fork_proto_meta.endpoints"):
            try:
                cursor.execute(statement)
            except pymysql.MySQLError as error:
                assert error.args[0] in (1049, 1051, 1146, 1235), (statement, error)
                experiment.record("child_control_schema_rejected",
                                  statement=statement, error=error.args)
            else:
                raise AssertionError(("child Worker accessed global control schema", statement))
        cursor.execute("UPDATE direct_check.t SET v='branch snapshot' WHERE id=1")
        cursor.execute("RENAME TABLE direct_check.t TO direct_check.renamed_t")
        cursor.execute("SELECT v FROM direct_check.renamed_t WHERE id=1")
        assert cursor.fetchone() == ("branch snapshot",)
        cursor.execute("RENAME TABLE direct_check.renamed_t TO direct_check.t")
        cursor.execute("SELECT v FROM direct_check.t WHERE id=1")
        assert cursor.fetchone() == ("branch snapshot",)
        experiment.record("inherited_table_rename_verified")
        cursor.execute("CREATE DATABASE branch_only")
        cursor.execute("CREATE TABLE branch_only.t(id INT PRIMARY KEY,v INT)")
        cursor.execute("INSERT INTO branch_only.t VALUES(1,42)")
        cursor.execute(
            "CREATE PROCEDURE branch_only.fork_cursor(OUT result INT) BEGIN "
            "DECLARE value INT; "
            "DECLARE c CURSOR FOR SELECT v FROM branch_only.t FOR UPDATE; "
            "OPEN c; FETCH c INTO value; SET result=value; CLOSE c; END")
        cursor.execute("SELECT v FROM branch_only.t")
        assert cursor.fetchone() == (42,)
        cursor.execute(
            "CREATE TABLE branch_only.drop_partitioned(id INT PRIMARY KEY,v INT) "
            "PARTITION BY HASH(id) PARTITIONS 3")
        cursor.execute("INSERT INTO branch_only.drop_partitioned VALUES(1,10),(2,20),(3,30)")
        cursor.execute(
            "SELECT table_id,database_id,table_name,schema_version "
            "FROM oceanbase.__all_table WHERE table_name='drop_partitioned'")
        persisted_partitioned = cursor.fetchall()
        experiment.record("partitioned_schema_rows_before_refresh",
                          rows=persisted_partitioned)
        assert len(persisted_partitioned) == 1
        assert persisted_partitioned[0][2] == "drop_partitioned"
        cursor.execute("SHOW TABLES FROM branch_only LIKE 'drop_partitioned'")
        assert cursor.fetchone() == ("drop_partitioned",)
        experiment.record("partitioned_show_before_drop_verified")
        cursor.execute("SELECT COUNT(*),SUM(v) FROM branch_only.drop_partitioned")
        assert cursor.fetchone() == (3, decimal.Decimal("60"))
        experiment.record("partitioned_read_before_drop_verified")
        cursor.execute("DROP TABLE branch_only.drop_partitioned")
        cursor.execute("SHOW TABLES FROM branch_only LIKE 'drop_partitioned'")
        assert cursor.fetchall() == ()
        cursor.execute(
            "UPDATE direct_check.partitioned_truncate SET v=11 WHERE id=1")
        cursor.execute(
            "SELECT id,v FROM direct_check.partitioned_truncate WHERE id=1")
        assert cursor.fetchone() == (1, 11)
        cursor.execute("TRUNCATE TABLE direct_check.partitioned_truncate")
        cursor.execute("SELECT COUNT(*) FROM direct_check.partitioned_truncate")
        assert cursor.fetchone() == (0,)
        cursor.execute("INSERT INTO direct_check.partitioned_truncate VALUES(7,70)")
        cursor.execute("SELECT id,v FROM direct_check.partitioned_truncate")
        assert cursor.fetchall() == ((7, 70),)
        experiment.record("partially_materialized_partitioned_truncate_verified")
        cursor.execute(
            "UPDATE direct_check.partitioned_redefinition SET v=11 WHERE id=1")
        cursor.execute(
            "ALTER TABLE direct_check.partitioned_redefinition "
            "PARTITION BY HASH(id) PARTITIONS 5")
        cursor.execute(
            "SELECT COUNT(*),SUM(v) FROM direct_check.partitioned_redefinition")
        assert cursor.fetchone() == (9, decimal.Decimal("361"))
        cursor.execute("SHOW CREATE TABLE direct_check.partitioned_redefinition")
        partitioned_redefinition_ddl = cursor.fetchone()[1]
        experiment.record(
            "partitioned_redefinition_show_create",
            ddl=partitioned_redefinition_ddl)
        assert_hash_partitions(partitioned_redefinition_ddl, "id", 5)
        experiment.record("partially_materialized_partition_redefinition_verified")
        cursor.execute(
            "UPDATE direct_check.partitioned_maintenance SET v=111 WHERE id=11")
        cursor.execute(
            "ALTER TABLE direct_check.partitioned_maintenance ADD PARTITION "
            "(PARTITION p3 VALUES LESS THAN (40))")
        cursor.execute(
            "INSERT INTO direct_check.partitioned_maintenance VALUES(31,310)")
        cursor.execute(
            "ALTER TABLE direct_check.partitioned_maintenance DROP PARTITION (p0)")
        cursor.execute(
            "SELECT id,v FROM direct_check.partitioned_maintenance ORDER BY id")
        assert cursor.fetchall() == ((11, 111), (21, 210), (31, 310))
        cursor.execute("SHOW CREATE TABLE direct_check.partitioned_maintenance")
        assert_range_partitions(cursor.fetchone()[1], ("p1", "p2", "p3"))
        experiment.record("inherited_range_partition_maintenance_verified")
        cursor.execute(
            "UPDATE direct_check.subpartitioned_maintenance "
            "SET v=111 WHERE id=1 AND region=11")
        cursor.execute(
            "ALTER TABLE direct_check.subpartitioned_maintenance "
            "DROP SUBPARTITION p0s0")
        cursor.execute(
            "SELECT id,region,v FROM direct_check.subpartitioned_maintenance "
            "ORDER BY id,region")
        assert cursor.fetchall() == (
            (1, 11, 111), (1, 21, 210),
            (101, 1, 1010), (101, 11, 1110), (101, 21, 1210))
        cursor.execute(
            "SHOW CREATE TABLE direct_check.subpartitioned_maintenance")
        assert_subpartitions(
            cursor.fetchone()[1],
            ("p0s1", "p0s2", "p1s0", "p1s1", "p1s2"))
        experiment.record("inherited_subpartition_maintenance_verified")
        cursor.execute(
            "ALTER TABLE direct_check.partition_exchange_source "
            "EXCHANGE PARTITION p0 WITH TABLE "
            "direct_check.partition_exchange_heap WITHOUT VALIDATION")
        cursor.execute(
            "SELECT id,v FROM direct_check.partition_exchange_source ORDER BY id")
        assert cursor.fetchall() == ((2, 20), (11, 110))
        cursor.execute(
            "SELECT id,v FROM direct_check.partition_exchange_heap ORDER BY id")
        assert cursor.fetchall() == ((1, 10),)
        experiment.record("inherited_partition_exchange_verified")
        cursor.execute(
            "CREATE TABLE branch_only.recovery_exchange_source("
            "id INT PRIMARY KEY,v INT) PARTITION BY RANGE(id) ("
            "PARTITION p0 VALUES LESS THAN (10),"
            "PARTITION p1 VALUES LESS THAN (20))")
        cursor.execute(
            "CREATE TABLE branch_only.recovery_exchange_heap("
            "id INT PRIMARY KEY,v INT)")
        cursor.execute(
            "INSERT INTO branch_only.recovery_exchange_source "
            "VALUES(1,10),(11,110)")
        cursor.execute(
            "INSERT INTO branch_only.recovery_exchange_heap VALUES(2,20)")
        try:
            cursor.execute("FORK DATABASE direct_b TO illegal_child_control")
        except pymysql.MySQLError as error:
            assert error.args[0] == 1235, error
        else:
            raise AssertionError("child Worker changed global namespace lifecycle metadata")

    # Crash after both native schemas commit but before their directory delta
    # is published. The replacement Worker must replay the whole multi-table
    # delta before publishing its endpoint.
    def exchange_for_crash_recovery():
        try:
            with connect(branch_endpoint) as branch, branch.cursor() as cursor:
                cursor.execute(
                    "ALTER TABLE branch_only.recovery_exchange_source "
                    "EXCHANGE PARTITION p0 WITH TABLE "
                    "branch_only.recovery_exchange_heap WITHOUT VALIDATION")
        except (pymysql.err.InterfaceError, pymysql.err.OperationalError) as error:
            return error.args
        raise AssertionError("exchange returned before the Worker crash")

    old_branch_pid = branch_pid
    old_branch_endpoint = branch_endpoint
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        exchange_future = pool.submit(exchange_for_crash_recovery)
        deadline = time.monotonic() + 10
        while True:
            with connect(control_endpoint) as control, control.cursor() as cursor:
                cursor.execute(
                    "SELECT active_schema_changes,pending_schema_version "
                    "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
                    (branch_id,))
                active_changes, recovery_pending_version = cursor.fetchone()
            if active_changes == 0 and recovery_pending_version > 0:
                break
            assert time.monotonic() < deadline and not exchange_future.done(), (
                active_changes, recovery_pending_version)
            time.sleep(.01)
        os.kill(old_branch_pid, 9)
        wait_process_dead(old_branch_pid)
        exchange_error = exchange_future.result(timeout=10)
    branch_pid, branch_endpoint, branch_generation = namespace_endpoint(
        experiment, branch_id, exclude_pid=old_branch_pid)
    assert branch_pid != old_branch_pid and branch_endpoint != old_branch_endpoint
    with connect_ready(branch_endpoint) as branch, branch.cursor() as cursor:
        cursor.execute(
            "SELECT id,v FROM branch_only.recovery_exchange_source ORDER BY id")
        assert cursor.fetchall() == ((2, 20), (11, 110))
        cursor.execute(
            "SELECT id,v FROM branch_only.recovery_exchange_heap ORDER BY id")
        assert cursor.fetchall() == ((1, 10),)
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute(
            "SELECT active_schema_changes,pending_schema_version "
            "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
            (branch_id,))
        assert cursor.fetchone() == (0, 0)
    experiment.record(
        "multi_table_schema_delta_crash_recovery_verified",
        namespace=branch_id,
        pending_version=recovery_pending_version,
        old={"pid": old_branch_pid, "endpoint": old_branch_endpoint},
        new={"pid": branch_pid, "endpoint": branch_endpoint,
             "generation": branch_generation},
        client_error=exchange_error)

    # Stop one child DDL after its native all_* transaction commits but before
    # the namespace-directory delta is published. A concurrent fork must wait
    # on the persistent namespace fence and then inherit both schema and tablet.
    def create_fenced_table():
        with connect(branch_endpoint) as branch, branch.cursor() as cursor:
            cursor.execute("CREATE TABLE branch_only.fenced(id INT PRIMARY KEY,v INT)")

    def fork_fenced_namespace():
        with connect(control_endpoint) as control, control.cursor() as cursor:
            cursor.execute("FORK DATABASE direct_b TO direct_fenced")

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        ddl_future = pool.submit(create_fenced_table)
        deadline = time.monotonic() + 10
        while True:
            with connect(control_endpoint) as control, control.cursor() as cursor:
                cursor.execute(
                    "SELECT active_schema_changes,pending_schema_version "
                    "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
                    (branch_id,))
                active_changes, pending_version = cursor.fetchone()
            if active_changes == 0 and pending_version > 0:
                break
            assert time.monotonic() < deadline and not ddl_future.done(), (
                active_changes, pending_version)
            time.sleep(.01)
        fork_future = pool.submit(fork_fenced_namespace)
        time.sleep(.05)
        assert not fork_future.done(), "fork crossed an unpublished DDL"
        ddl_future.result(timeout=15)
        fork_future.result(timeout=15)
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SELECT namespace_id FROM __fork_proto_meta.namespaces "
                       "WHERE name='direct_fenced'")
        fenced_id, = cursor.fetchone()
        cursor.execute(
            "SELECT active_schema_changes,pending_schema_version "
            "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
            (branch_id,))
        assert cursor.fetchone() == (0, 0)
    fenced_pid, fenced_endpoint, fenced_generation = activate(fenced_id)
    with connect(fenced_endpoint) as fenced, fenced.cursor() as cursor:
        cursor.execute("INSERT INTO branch_only.fenced VALUES(1,77)")
        cursor.execute("SELECT v FROM branch_only.fenced")
        assert cursor.fetchone() == (77,)
    experiment.record(
        "direct_ddl_fork_fence_verified", namespace=fenced_id,
        worker_pid=fenced_pid, endpoint=fenced_endpoint,
        generation=fenced_generation, pending_version=pending_version)

    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SELECT directory_page,directory_cap,schema_version "
                       "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
                       (branch_id,))
        branch_root_before_fork = cursor.fetchone()
        experiment.record("branch_root_before_fork", namespace=branch_id,
                          directory_page=branch_root_before_fork[0],
                          directory_cap=branch_root_before_fork[1],
                          schema_version=branch_root_before_fork[2])
        assert branch_root_before_fork[0] != branch_root_before_ddl[0], (
            branch_root_before_ddl, branch_root_before_fork)
        cursor.execute("SHOW TABLES FROM __fork_proto_meta LIKE 'endpoints'")
        assert cursor.fetchone() == ("endpoints",)
        cursor.execute("SELECT COUNT(*) FROM __fork_proto_meta.namespaces "
                       "WHERE name='illegal_child_control'")
        assert cursor.fetchone() == (0,)
        cursor.execute("FORK DATABASE direct_b TO direct_c")
        cursor.execute("SELECT namespace_id,name FROM __fork_proto_meta.namespaces "
                       "WHERE name='direct_c'")
        child_id, child_name = cursor.fetchone()
        assert child_name == b"direct_c"
        cursor.execute("SELECT directory_page,directory_cap,schema_version "
                       "FROM __fork_proto_meta.namespaces WHERE namespace_id=%s",
                       (child_id,))
        child_root_after_fork = cursor.fetchone()
        experiment.record("child_root_after_fork", namespace=child_id,
                          directory_page=child_root_after_fork[0],
                          directory_cap=child_root_after_fork[1],
                          schema_version=child_root_after_fork[2])
        assert child_root_after_fork[0] == branch_root_before_fork[0]
    child_pid, child_endpoint, child_generation = activate(child_id)
    assert len({control_endpoint, branch_endpoint, child_endpoint}) == 3
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SELECT namespace_id,worker_pid,endpoint,generation "
                       "FROM __fork_proto_meta.endpoints "
                       "WHERE namespace_id IN (%s,%s) ORDER BY namespace_id",
                       (branch_id, child_id))
        assert [endpoint_row(row) for row in cursor.fetchall()] == [
            (branch_id, branch_pid, branch_endpoint, branch_generation),
            (child_id, child_pid, child_endpoint, child_generation)]

    with connect(branch_endpoint) as branch, connect(child_endpoint) as child, connect(control_endpoint) as source:
        with child.cursor() as cursor:
            cursor.execute("SELECT v FROM direct_check.t WHERE id=1")
            assert cursor.fetchone() == ("branch snapshot",)
            cursor.execute("SELECT v FROM branch_only.t")
            assert cursor.fetchone() == (42,)
            cursor.execute("BEGIN")
            cursor.execute("CALL branch_only.fork_cursor(@fork_cursor_result)")
            while cursor.nextset():
                pass
            cursor.execute("SELECT @fork_cursor_result")
            assert cursor.fetchone() == (42,)
            cursor.execute("COMMIT")
            experiment.record(
                "forked_worker_inherited_pl_cursor_verified",
                namespace=child_id)
            cursor.execute("SELECT id,base,doubled FROM direct_check.generated_auto ORDER BY id")
            assert cursor.fetchall() == ((1, 5, 10), (2, 7, 14))
            cursor.execute("SELECT COUNT(*),SUM(v) FROM direct_check.partitioned_records")
            assert cursor.fetchone() == (8, decimal.Decimal("280"))
            cursor.execute(
                "SELECT /*+ parallel(2) */ SUM(v) "
                "FROM direct_check.partitioned_records")
            assert cursor.fetchone() == (decimal.Decimal("280"),)
            experiment.record("child_parallel_scan_verified")
            cursor.execute("SELECT id,v FROM direct_check.partitioned_truncate")
            assert cursor.fetchall() == ((7, 70),)
            cursor.execute(
                "SELECT COUNT(*),SUM(v) "
                "FROM direct_check.partitioned_redefinition")
            assert cursor.fetchone() == (9, decimal.Decimal("361"))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_redefinition")
            assert_hash_partitions(cursor.fetchone()[1], "id", 5)
            cursor.execute(
                "SELECT id,v FROM direct_check.partitioned_maintenance ORDER BY id")
            assert cursor.fetchall() == ((11, 111), (21, 210), (31, 310))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_maintenance")
            assert_range_partitions(cursor.fetchone()[1], ("p1", "p2", "p3"))
            cursor.execute(
                "ALTER TABLE direct_check.partitioned_maintenance ADD PARTITION "
                "(PARTITION p4 VALUES LESS THAN (50))")
            cursor.execute(
                "INSERT INTO direct_check.partitioned_maintenance VALUES(41,410)")
            cursor.execute(
                "ALTER TABLE direct_check.partitioned_maintenance DROP PARTITION (p1)")
            cursor.execute(
                "SELECT id,v FROM direct_check.partitioned_maintenance ORDER BY id")
            assert cursor.fetchall() == ((21, 210), (31, 310), (41, 410))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_maintenance")
            assert_range_partitions(cursor.fetchone()[1], ("p2", "p3", "p4"))
            experiment.record("descendant_range_partition_maintenance_verified")
            cursor.execute(
                "SELECT id,region,v FROM direct_check.subpartitioned_maintenance "
                "ORDER BY id,region")
            assert cursor.fetchall() == (
                (1, 11, 111), (1, 21, 210),
                (101, 1, 1010), (101, 11, 1110), (101, 21, 1210))
            cursor.execute(
                "ALTER TABLE direct_check.subpartitioned_maintenance "
                "DROP SUBPARTITION p1s0")
            cursor.execute(
                "SELECT id,region,v FROM direct_check.subpartitioned_maintenance "
                "ORDER BY id,region")
            assert cursor.fetchall() == (
                (1, 11, 111), (1, 21, 210),
                (101, 11, 1110), (101, 21, 1210))
            cursor.execute(
                "SHOW CREATE TABLE direct_check.subpartitioned_maintenance")
            assert_subpartitions(
                cursor.fetchone()[1],
                ("p0s1", "p0s2", "p1s1", "p1s2"))
            experiment.record("descendant_subpartition_maintenance_verified")
            cursor.execute(
                "DELETE FROM direct_check.partition_exchange_heap WHERE id=1")
            cursor.execute(
                "INSERT INTO direct_check.partition_exchange_heap VALUES(12,120)")
            cursor.execute(
                "ALTER TABLE direct_check.partition_exchange_source "
                "EXCHANGE PARTITION p1 WITH TABLE "
                "direct_check.partition_exchange_heap WITHOUT VALIDATION")
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_source ORDER BY id")
            assert cursor.fetchall() == ((2, 20), (12, 120))
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_heap ORDER BY id")
            assert cursor.fetchall() == ((11, 110),)
            experiment.record("descendant_partition_exchange_verified")
            cursor.execute(
                "SELECT id,OCTET_LENGTH(payload) FROM direct_check.partitioned_lob_records "
                "ORDER BY id")
            assert cursor.fetchall() == (
                (0, len(PARTITIONED_LOB_SOURCE)),
                (1, len(PARTITIONED_LOB_SOURCE)),
                (2, len(PARTITIONED_LOB_SOURCE)))
            cursor.execute(
                "SELECT id FROM direct_check.partitioned_lob_records "
                "FORCE INDEX(partitioned_lob_bucket) WHERE bucket=20")
            assert cursor.fetchall() == ((1,),)
            cursor.execute("INSERT INTO direct_check.generated_auto(base) VALUES(11)")
            child_generated_id = cursor.lastrowid
            assert child_generated_id > 2
            cursor.execute("UPDATE direct_check.partitioned_records SET v=111 WHERE id=1")
            cursor.execute("INSERT INTO direct_check.partitioned_records VALUES(100,1000)")
            cursor.execute(
                "UPDATE direct_check.partitioned_lob_records "
                "SET payload=%s,bucket=21 WHERE id=1", (PARTITIONED_LOB_CHILD,))
            cursor.execute(
                "INSERT INTO direct_check.partitioned_lob_records VALUES(4,40,%s)",
                (PARTITIONED_LOB_CHILD,))
            cursor.execute("INSERT INTO direct_check.t VALUES(3,'child only')")
        with branch.cursor() as cursor:
            cursor.execute("UPDATE direct_check.t SET v='branch after fork' WHERE id=1")
            cursor.execute("INSERT INTO direct_check.generated_auto(base) VALUES(13)")
            branch_generated_id = cursor.lastrowid
            assert branch_generated_id > 2
            cursor.execute("SELECT id,v FROM direct_check.t ORDER BY id")
            assert cursor.fetchall() == ((1, "branch after fork"), (2, "second"))
            cursor.execute("SELECT id,base,doubled FROM direct_check.generated_auto ORDER BY id")
            assert cursor.fetchall() == (
                (1, 5, 10), (2, 7, 14), (branch_generated_id, 13, 26))
            cursor.execute("UPDATE direct_check.partitioned_records SET v=131 WHERE id=1")
            cursor.execute("SELECT v FROM direct_check.partitioned_records WHERE id=100")
            assert cursor.fetchall() == ()
            cursor.execute(
                "SELECT id,v FROM direct_check.partitioned_maintenance ORDER BY id")
            assert cursor.fetchall() == ((11, 111), (21, 210), (31, 310))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_maintenance")
            assert_range_partitions(cursor.fetchone()[1], ("p1", "p2", "p3"))
            cursor.execute(
                "SELECT id,region,v FROM direct_check.subpartitioned_maintenance "
                "ORDER BY id,region")
            assert cursor.fetchall() == (
                (1, 11, 111), (1, 21, 210),
                (101, 1, 1010), (101, 11, 1110), (101, 21, 1210))
            cursor.execute(
                "SHOW CREATE TABLE direct_check.subpartitioned_maintenance")
            assert_subpartitions(
                cursor.fetchone()[1],
                ("p0s1", "p0s2", "p1s0", "p1s1", "p1s2"))
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_source ORDER BY id")
            assert cursor.fetchall() == ((2, 20), (11, 110))
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_heap ORDER BY id")
            assert cursor.fetchall() == ((1, 10),)
            cursor.execute(
                "UPDATE direct_check.partitioned_lob_records "
                "SET payload=%s,bucket=22 WHERE id=1", (PARTITIONED_LOB_BRANCH,))
            cursor.execute(
                "SELECT OCTET_LENGTH(payload),bucket "
                "FROM direct_check.partitioned_lob_records WHERE id=1")
            assert cursor.fetchone() == (len(PARTITIONED_LOB_BRANCH), 22)
            cursor.execute("SELECT id FROM direct_check.partitioned_lob_records WHERE id=4")
            assert cursor.fetchall() == ()
            cursor.execute("DROP TABLE direct_check.partitioned_lob_records")
            cursor.execute("SHOW TABLES FROM direct_check LIKE 'partitioned_lob_records'")
            assert cursor.fetchall() == ()
        with child.cursor() as cursor:
            cursor.execute("SELECT id,v FROM direct_check.t ORDER BY id")
            assert cursor.fetchall() == ((1, "branch snapshot"), (2, "second"), (3, "child only"))
            cursor.execute("SELECT id,base,doubled FROM direct_check.generated_auto ORDER BY id")
            assert cursor.fetchall() == (
                (1, 5, 10), (2, 7, 14), (child_generated_id, 11, 22))
            cursor.execute(
                "SELECT id,v FROM direct_check.partitioned_records "
                "WHERE id IN (1,100) ORDER BY id")
            assert cursor.fetchall() == ((1, 111), (100, 1000))
            cursor.execute(
                "SELECT id,OCTET_LENGTH(payload),bucket "
                "FROM direct_check.partitioned_lob_records WHERE id IN (1,4) ORDER BY id")
            assert cursor.fetchall() == (
                (1, len(PARTITIONED_LOB_CHILD), 21),
                (4, len(PARTITIONED_LOB_CHILD), 40))
            cursor.execute(
                "SELECT id FROM direct_check.partitioned_lob_records "
                "FORCE INDEX(partitioned_lob_bucket) WHERE bucket=21")
            assert cursor.fetchall() == ((1,),)
        with source.cursor() as cursor:
            cursor.execute("SELECT id,v FROM direct_check.t ORDER BY id")
            assert cursor.fetchall() == ((1, "committed"), (2, "second"))
            cursor.execute("SHOW DATABASES LIKE 'branch_only'")
            assert cursor.fetchall() == ()
            cursor.execute(
                "SELECT OCTET_LENGTH(payload),bucket "
                "FROM direct_check.partitioned_lob_records WHERE id=1")
            assert cursor.fetchone() == (len(PARTITIONED_LOB_SOURCE), 20)
            cursor.execute("SELECT id,v FROM direct_check.partitioned_truncate ORDER BY id")
            assert cursor.fetchall() == ((0, 0), (1, 10), (2, 20))
            cursor.execute(
                "SELECT COUNT(*),SUM(v) "
                "FROM direct_check.partitioned_redefinition")
            assert cursor.fetchone() == (9, decimal.Decimal("360"))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_redefinition")
            assert_hash_partitions(cursor.fetchone()[1], "id", 3)
            cursor.execute(
                "SELECT id,v FROM direct_check.partitioned_maintenance ORDER BY id")
            assert cursor.fetchall() == ((1, 10), (11, 110), (21, 210))
            cursor.execute("SHOW CREATE TABLE direct_check.partitioned_maintenance")
            assert_range_partitions(cursor.fetchone()[1], ("p0", "p1", "p2"))
            cursor.execute(
                "SELECT id,region,v FROM direct_check.subpartitioned_maintenance "
                "ORDER BY id,region")
            assert cursor.fetchall() == (
                (1, 1, 10), (1, 11, 110), (1, 21, 210),
                (101, 1, 1010), (101, 11, 1110), (101, 21, 1210))
            cursor.execute(
                "SHOW CREATE TABLE direct_check.subpartitioned_maintenance")
            assert_subpartitions(
                cursor.fetchone()[1],
                ("p0s0", "p0s1", "p0s2", "p1s0", "p1s1", "p1s2"))
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_source ORDER BY id")
            assert cursor.fetchall() == ((1, 10), (11, 110))
            cursor.execute(
                "SELECT id,v FROM direct_check.partition_exchange_heap ORDER BY id")
            assert cursor.fetchall() == ((2, 20),)

    barrier = threading.Barrier(3)
    def concurrent_namespace_writer(target):
        label, endpoint = target
        with connect(endpoint) as connection, connection.cursor() as cursor:
            barrier.wait(timeout=10)
            cursor.execute("UPDATE direct_check.t SET v=%s WHERE id=1", (label,))
            for key in range(100, 116):
                cursor.execute("INSERT INTO direct_check.t VALUES(%s,%s)",
                               (key, f"{label}-{key}"))
            cursor.execute("SELECT v FROM direct_check.t WHERE id=1")
            assert cursor.fetchone() == (label,)
            cursor.execute("SELECT COUNT(*) FROM direct_check.t WHERE id BETWEEN 100 AND 115")
            assert cursor.fetchone() == (16,)
    endpoints = (("source concurrent", control_endpoint),
                 ("branch concurrent", branch_endpoint),
                 ("child concurrent", child_endpoint))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(concurrent_namespace_writer, endpoints))
    for label, endpoint in endpoints:
        with connect(endpoint) as connection, connection.cursor() as cursor:
            cursor.execute("SELECT v FROM direct_check.t WHERE id=1")
            assert cursor.fetchone() == (label,)
            cursor.execute("SELECT v FROM direct_check.t WHERE id=100")
            assert cursor.fetchone() == (f"{label}-100",)
    experiment.record("direct_concurrent_namespace_writes_verified",
                      namespaces=3, writes_per_namespace=17)

    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("FORK DATABASE direct_a TO direct_survivor")
        cursor.execute("SELECT namespace_id FROM __fork_proto_meta.namespaces "
                       "WHERE name='direct_survivor'")
        survivor_id, = cursor.fetchone()
    survivor_pid, survivor_endpoint, survivor_generation = activate(survivor_id)
    with connect(survivor_endpoint) as survivor, survivor.cursor() as cursor:
        cursor.execute("UPDATE direct_check.t SET v='survivor before crash' WHERE id=1")
        cursor.execute("CREATE DATABASE survivor_only")
        cursor.execute("CREATE TABLE survivor_only.t(id INT PRIMARY KEY,v VARCHAR(64))")
        cursor.execute("INSERT INTO survivor_only.t VALUES(1,'durable')")
    experiment.survivor_endpoint = {
        "namespace": survivor_id, "pid": survivor_pid, "endpoint": survivor_endpoint,
        "generation": survivor_generation}

    # The storage process owns access leases. Pause a scan after admission and
    # prove DROP closes the namespace but cannot reclaim it until that shared
    # lease is released.
    fenced_reader = connect(fenced_endpoint)

    def read_fenced_while_dropping():
        with fenced_reader.cursor() as cursor:
            cursor.execute("SELECT id,v FROM branch_only.fenced ORDER BY id")
            return cursor.fetchall()

    def drop_fenced_namespace():
        with connect(control_endpoint) as control, control.cursor() as cursor:
            cursor.execute("FORK DATABASE direct_fenced TO __drop__")

    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("ALTER SYSTEM SET debug_sync_timeout='600s'")
    config_endpoints = (branch_endpoint, child_endpoint, fenced_endpoint, survivor_endpoint)
    for endpoint in config_endpoints:
        with connect(endpoint) as worker, worker.cursor() as cursor:
            cursor.execute(
                "SELECT value FROM oceanbase.__all_virtual_parameter_stat "
                "WHERE name='debug_sync_timeout'")
            assert cursor.fetchone() == ("600s",), endpoint
    experiment.record(
        "namespace_worker_config_broadcast_verified",
        namespaces=4,
        value="600s")
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute(
            "SET ob_global_debug_sync='AFTER_TABLE_SCAN signal "
            "namespace_drop_read_ready wait_for namespace_drop_read_release "
            "timeout 60000000 execute 1'")
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        reader = pool.submit(read_fenced_while_dropping)
        try:
            with connect(control_endpoint) as control, control.cursor() as cursor:
                cursor.execute(
                    "SET ob_global_debug_sync='now wait_for "
                    "namespace_drop_read_ready timeout 10000000'")
            assert not reader.done(), "target scan did not stop at shared storage"
            pending_drop = pool.submit(drop_fenced_namespace)
            deadline = time.monotonic() + 10
            while True:
                with connect(control_endpoint) as control, control.cursor() as cursor:
                    cursor.execute(
                        "SELECT state FROM __fork_proto_meta.namespaces "
                        "WHERE namespace_id=%s", (fenced_id,))
                    state, = cursor.fetchone()
                if state == 1:
                    break
                assert time.monotonic() < deadline and not pending_drop.done(), state
                time.sleep(.01)
            time.sleep(.3)
            assert not pending_drop.done(), "DROP passed an admitted shared scan"
            experiment.record(
                "namespace_drop_waited_for_shared_scan", namespace=fenced_id)
        finally:
            with connect(control_endpoint) as control, control.cursor() as cursor:
                cursor.execute(
                    "SET ob_global_debug_sync='now signal "
                    "namespace_drop_read_release'")
        try:
            rows = reader.result(timeout=15)
            assert rows == ((1, 77),), rows
        except pymysql.MySQLError as error:
            # Namespace DROP stops its Worker after the shared storage lease is
            # released. The current lifecycle contract permits that process
            # stop to disconnect an already admitted client response.
            assert error.args[0] in (2006, 2013, 4179), error
        pending_drop.result(timeout=15)
    fenced_reader.close()
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("SET ob_global_debug_sync='AFTER_TABLE_SCAN clear'")
    wait_process_dead(fenced_pid)

    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("FORK DATABASE direct_c TO __drop__")
        cursor.execute("FORK DATABASE direct_b TO __drop__")
        cursor.execute("SELECT name,state FROM __fork_proto_meta.namespaces "
                       "WHERE name IN ('direct_b','direct_c') ORDER BY name")
        assert cursor.fetchall() == ((b"direct_b", 2), (b"direct_c", 2))
        cursor.execute("SELECT COUNT(*) FROM __fork_proto_meta.endpoints "
                       "WHERE namespace_id IN (%s,%s)", (branch_id, child_id))
        assert cursor.fetchone() == (0,)
    wait_process_dead(branch_pid)
    wait_process_dead(child_pid)
    wait_process_dead(fenced_pid)
    assert_shared_namespace_released(
        experiment, (fenced_id, child_id, branch_id))

    # Namespace 1 is both a normal SQL namespace and the control entry.  Its
    # replacement must become useful before endpoint publication can complete,
    # because that publication itself is a GLOBAL-table write routed through
    # the replacement Worker.
    old_control_pid, old_control_endpoint = experiment.control_endpoint
    with connect(old_control_endpoint) as control, control.cursor() as cursor:
        cursor.execute(
            "SELECT generation FROM __fork_proto_meta.endpoints "
            "WHERE namespace_id=1")
        old_control_generation, = cursor.fetchone()
    os.kill(old_control_pid, 9)
    wait_process_dead(old_control_pid)
    control_recovery_started = time.monotonic()
    control_pid, control_endpoint, control_generation = namespace_endpoint(
        experiment, 1, exclude_pid=old_control_pid, timeout=90)
    control_recovery_seconds = time.monotonic() - control_recovery_started
    assert control_pid != old_control_pid
    assert control_generation > old_control_generation
    with connect_ready(control_endpoint) as control, control.cursor() as cursor:
        # A lazy session open may win the respawn race; endpoint publication
        # then lands in the asynchronous recovery task. Wait for it.
        deadline = time.monotonic() + 30
        while True:
            cursor.execute(
                "SELECT worker_pid,endpoint,generation "
                "FROM __fork_proto_meta.endpoints WHERE namespace_id=1")
            row = endpoint_row(cursor.fetchone())
            if row == (control_pid, control_endpoint, control_generation):
                break
            assert time.monotonic() < deadline, (
                row, control_pid, control_endpoint, control_generation)
            time.sleep(.05)
        cursor.execute(
            "SELECT id,v FROM direct_check.t "
            "WHERE id IN (1,2,100,115) ORDER BY id")
        assert cursor.fetchall() == (
            (1, "source concurrent"), (2, "second"),
            (100, "source concurrent-100"), (115, "source concurrent-115"))
        cursor.execute("FORK DATABASE direct_a TO control_recovery_child")
        cursor.execute(
            "SELECT namespace_id FROM __fork_proto_meta.namespaces "
            "WHERE name='control_recovery_child'")
        recovery_child_id, = cursor.fetchone()
    recovery_child_pid, recovery_child_endpoint, _ = activate(recovery_child_id)
    with connect(recovery_child_endpoint) as child, child.cursor() as cursor:
        cursor.execute("SELECT id,v FROM direct_check.t WHERE id IN (1,2) ORDER BY id")
        assert cursor.fetchall() == ((1, "source concurrent"), (2, "second"))
        cursor.execute("SHOW DATABASES LIKE '__fork_proto_meta'")
        assert cursor.fetchall() == ()
    with connect(control_endpoint) as control, control.cursor() as cursor:
        cursor.execute("FORK DATABASE control_recovery_child TO __drop__")
        cursor.execute(
            "SELECT state FROM __fork_proto_meta.namespaces "
            "WHERE namespace_id=%s", (recovery_child_id,))
        assert cursor.fetchone() == (2,)
    wait_process_dead(recovery_child_pid)
    assert_shared_namespace_released(experiment, (recovery_child_id,))
    experiment.control_endpoint = (control_pid, control_endpoint)
    experiment.record(
        "default_worker_crash_recovery_verified",
        old={"pid": old_control_pid, "endpoint": old_control_endpoint,
             "generation": old_control_generation},
        new={"pid": control_pid, "endpoint": control_endpoint,
             "generation": control_generation},
        recovery_seconds=round(control_recovery_seconds, 3),
        namespace_data_recovered=True,
        global_catalog_republished=True,
        lifecycle_commands_recovered=True)

    experiment.record(
        "direct_forked_namespace_endpoints_verified",
        source_endpoint=control_endpoint,
        branch={"namespace": branch_id, "pid": branch_pid,
                "endpoint": branch_endpoint, "generation": branch_generation},
        child={"namespace": child_id, "pid": child_pid,
               "endpoint": child_endpoint, "generation": child_generation},
        survivor=experiment.survivor_endpoint,
        inherited_database_ddl=True,
        inherited_auto_increment_and_generated_columns=True,
        inherited_partitioned_table=True,
        partially_materialized_partitioned_truncate=True,
        partially_materialized_partition_redefinition=True,
        inherited_range_partition_maintenance=True,
        descendant_range_partition_maintenance=True,
        inherited_subpartition_maintenance=True,
        descendant_subpartition_maintenance=True,
        inherited_partition_exchange=True,
        descendant_partition_exchange=True,
        multi_table_schema_delta_crash_recovery=True,
        inherited_partitioned_lob_and_index=True,
        post_fork_writes_isolated=True,
        global_control_schema_hidden_from_children=True,
        child_management_rejected_before_mutation=True,
        default_worker_crash_recovery=True,
        deleted_workers_stopped=True)


def endpoint_recovery_probe(experiment):
    old_control = experiment.control_endpoint
    old_survivor = experiment.survivor_endpoint
    experiment.connection.close()
    experiment.connection = None
    experiment.proc.kill()
    experiment.proc.wait(timeout=15)
    experiment.proc = None
    wait_process_dead(old_control[0])
    wait_process_dead(old_survivor["pid"])
    experiment.record("direct_crash_for_endpoint_recovery",
                      control_pid=old_control[0], survivor=old_survivor)

    experiment.start()
    new_control = namespace_endpoint(experiment, 1, exclude_pid=old_control[0])
    new_survivor = namespace_endpoint(
        experiment, old_survivor["namespace"], exclude_pid=old_survivor["pid"])
    reconciled = (f"PROTOTYPE_NAMESPACE_ENDPOINT_RECONCILED "
                  f"ns={old_survivor['namespace']}")
    assert reconciled in runtime_log(experiment), reconciled

    with connect(new_survivor[1]) as survivor, survivor.cursor() as cursor:
        cursor.execute(
            "SELECT value FROM oceanbase.__all_virtual_parameter_stat "
            "WHERE name='debug_sync_timeout'")
        assert cursor.fetchone() == ("600s",)
        cursor.execute("SELECT id,v FROM direct_check.t "
                       "WHERE id IN (1,2,100,115) ORDER BY id")
        assert cursor.fetchall() == (
            (1, "survivor before crash"), (2, "second"),
            (100, "source concurrent-100"), (115, "source concurrent-115"))
        cursor.execute("SELECT id,v FROM survivor_only.t")
        assert cursor.fetchall() == ((1, "durable"),)
    with connect(new_control[1]) as control, control.cursor() as cursor:
        cursor.execute("SELECT namespace_id,worker_pid,endpoint,generation "
                       "FROM __fork_proto_meta.endpoints ORDER BY namespace_id")
        assert [endpoint_row(row) for row in cursor.fetchall()] == [
            (1, new_control[0], new_control[1], new_control[2]),
            (old_survivor["namespace"], new_survivor[0],
             new_survivor[1], new_survivor[2])]
        cursor.execute("SELECT id,v FROM direct_check.t "
                       "WHERE id IN (1,2,100,115) ORDER BY id")
        assert cursor.fetchall() == (
            (1, "source concurrent"), (2, "second"),
            (100, "source concurrent-100"), (115, "source concurrent-115"))
        cursor.execute("FORK DATABASE direct_survivor TO __drop__")
        cursor.execute("SELECT state FROM __fork_proto_meta.namespaces "
                       "WHERE namespace_id=%s", (old_survivor["namespace"],))
        assert cursor.fetchone() == (2,)
        cursor.execute("SELECT COUNT(*) FROM __fork_proto_meta.endpoints "
                       "WHERE namespace_id=%s", (old_survivor["namespace"],))
        assert cursor.fetchone() == (0,)
    wait_process_dead(new_survivor[0])
    assert_shared_namespace_released(
        experiment, (old_survivor["namespace"],))
    experiment.record(
        "direct_endpoint_recovery_verified",
        namespace=old_survivor["namespace"],
        old={"pid": old_survivor["pid"], "endpoint": old_survivor["endpoint"]},
        new={"pid": new_survivor[0], "endpoint": new_survivor[1],
             "generation": new_survivor[2]},
        worker_recreated_automatically=True,
        endpoint_catalog_rebuilt=True,
        dynamic_worker_config_restored=True,
        namespace_data_recovered=True,
        source_isolation_preserved=True,
        recovered_worker_stopped_on_delete=True)


def tls_probe(experiment):
    control_pid, control_endpoint, _ = namespace_endpoint(experiment, 1)
    wallet = experiment.base / "wallet"
    with connect_tls(control_endpoint, wallet) as control, control.cursor() as cursor:
        assert control._sock.cipher() is not None
        cursor.execute("CREATE DATABASE tls_data")
        cursor.execute("CREATE TABLE tls_data.t(id INT PRIMARY KEY,v VARCHAR(32))")
        cursor.execute("INSERT INTO tls_data.t VALUES(1,'encrypted')")
        cursor.execute("FORK DATABASE __empty__ TO tls_source")
        cursor.execute("FORK DATABASE tls_source TO tls_child")
        cursor.execute(
            "SELECT namespace_id FROM __fork_proto_meta.namespaces "
            "WHERE name='tls_child'")
        child_id, = cursor.fetchone()
    child_pid, child_endpoint, _ = namespace_endpoint(experiment, child_id)
    with connect_tls(child_endpoint, wallet) as child, child.cursor() as cursor:
        cipher = child._sock.cipher()
        assert cipher is not None and cipher[1] in ("TLSv1.2", "TLSv1.3"), cipher
        cursor.execute("SELECT id,v FROM tls_data.t")
        assert cursor.fetchone() == (1, "encrypted")
    with connect_tls(control_endpoint, wallet) as control, control.cursor() as cursor:
        cursor.execute("FORK DATABASE tls_child TO __drop__")
    wait_process_dead(child_pid)
    experiment.record(
        "namespace_worker_tls_verified",
        control_pid=control_pid,
        child_namespace=child_id,
        protocol=cipher[1],
        cipher=cipher[0],
        forked_data_visible=True)


def ddl_probe(experiment, endpoint):
    with connect(endpoint) as connection, connection.cursor() as cursor:
        cursor.execute("CREATE DATABASE ddl_check")
        cursor.execute("ALTER DATABASE ddl_check CHARACTER SET utf8mb4 COLLATE utf8mb4_bin")
        cursor.execute("USE ddl_check")
        cursor.execute("CREATE TABLE records(tenant_id INT, id INT, label VARCHAR(64), amount DECIMAL(12,2), "
                       "created DATE, nullable_value INT NULL, PRIMARY KEY(tenant_id,id))")
        cursor.execute("INSERT INTO records VALUES(1,1,'first',12.34,'2026-09-15',NULL),(1,2,'second',56.78,'2026-09-14',9)")
        cursor.execute("SELECT label,amount,created,nullable_value FROM records WHERE tenant_id=1 AND id=1")
        assert cursor.fetchone() == ("first", decimal.Decimal("12.34"), datetime.date(2026, 9, 15), None)
        experiment.record("direct_composite_key_types_verified")
        cursor.execute("SELECT /*+ parallel(2) */ SUM(amount) FROM records")
        assert cursor.fetchone() == (decimal.Decimal("69.12"),)
        experiment.record("direct_parallel_scan_verified")
        parallel_insert = ("INSERT /*+ enable_parallel_dml parallel(2) */ INTO records "
                           "SELECT tenant_id,id+10,label,amount,created,nullable_value FROM records WHERE id<=2")
        cursor.execute("BEGIN")
        cursor.execute(parallel_insert)
        cursor.execute("ROLLBACK")
        cursor.execute("SELECT COUNT(*) FROM records")
        assert cursor.fetchone() == (2,)
        cursor.execute(parallel_insert)
        cursor.execute("SELECT COUNT(*),SUM(amount) FROM records")
        assert cursor.fetchone() == (4, decimal.Decimal("138.24"))
        cursor.execute("DELETE FROM records WHERE id>10")
        experiment.record("direct_parallel_dml_verified")
        cursor.execute("CREATE INDEX records_label ON records(label)")
        cursor.execute("SELECT id FROM records FORCE INDEX(records_label) WHERE label='second'")
        assert cursor.fetchall() == ((2,),)
        cursor.execute("CREATE UNIQUE INDEX records_unique ON records(tenant_id,label)")
        try:
            cursor.execute("INSERT INTO records VALUES(1,3,'first',1,'2026-09-15',NULL)")
        except pymysql.MySQLError as error:
            assert error.args[0] == 1062, error
        else:
            raise AssertionError("unique index did not reject a duplicate")
        cursor.execute("UPDATE records SET label='changed' WHERE tenant_id=1 AND id=2")
        cursor.execute("SELECT id FROM records FORCE INDEX(records_label) WHERE label='changed'")
        assert cursor.fetchall() == ((2,),)
        experiment.record("direct_indexes_verified")
        cursor.execute("CREATE TABLE lob_records(id INT PRIMARY KEY, payload MEDIUMBLOB, text_value MEDIUMTEXT)")
        cursor.execute("INSERT INTO lob_records VALUES(1,%s,%s)", (b"blob-value", "text-value"))
        cursor.execute("SELECT payload,text_value FROM lob_records WHERE id=1")
        assert cursor.fetchone() == (b"blob-value", "text-value")
        experiment.record("direct_lob_schema_visibility_verified")
        cursor.execute("DROP TABLE lob_records")
        cursor.execute("ALTER TABLE records ADD COLUMN revision INT DEFAULT 7")
        cursor.execute("SELECT revision FROM records WHERE tenant_id=1 AND id=1")
        assert cursor.fetchone() == (7,)
        cursor.execute("DROP INDEX records_label ON records")
        cursor.execute("DROP INDEX records_unique ON records")
        cursor.execute("DROP TABLE records")
        cursor.execute("DROP DATABASE ddl_check")
        experiment.record("direct_general_ddl_verified")


def probe(experiment, case="full"):
    matches = re.findall(r"PROTOTYPE_V10_WORKER_READY ns=1 generation=\d+ pid=(\d+) endpoint=(\S+)",
                         experiment.engine_log())
    assert matches, experiment.base
    pid, endpoint = matches[-1]
    pid = int(pid)
    assert endpoint.startswith("run/namespace-worker-")
    assert endpoint.endswith("/run/sql.sock")
    experiment.control_endpoint = (pid, endpoint)
    experiment.record("direct_endpoint", worker_pid=pid, endpoint=endpoint)
    with connect_ready(endpoint) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT 1, CONNECTION_ID()")
            assert cursor.fetchone()[0] == 1
            cursor.execute("BEGIN")
            assert connection.server_status & 1
            cursor.execute("SELECT count(*) FROM oceanbase.__all_database")
            assert cursor.fetchone()[0] >= 6
            cursor.execute("ROLLBACK")
            assert not connection.server_status & 1
            cursor.execute("USE test")
            cursor.execute("SELECT DATABASE()")
            assert cursor.fetchone() == ("test",)
            cursor.execute("CREATE DATABASE direct_check")
            cursor.execute("USE direct_check")
            cursor.execute("CREATE TABLE t(id INT PRIMARY KEY, v VARCHAR(64))")
            cursor.execute(
                "CREATE TABLE generated_auto("
                "id BIGINT NOT NULL AUTO_INCREMENT PRIMARY KEY,"
                "base INT NOT NULL,"
                "doubled INT GENERATED ALWAYS AS (base * 2) VIRTUAL)")
            cursor.execute("INSERT INTO generated_auto(base) VALUES(5),(7)")
            cursor.execute("SELECT id,base,doubled FROM generated_auto ORDER BY id")
            assert cursor.fetchall() == ((1, 5, 10), (2, 7, 14))
            cursor.execute(
                "CREATE TABLE partitioned_records(id INT PRIMARY KEY,v INT) "
                "PARTITION BY HASH(id) PARTITIONS 4")
            cursor.execute(
                "INSERT INTO partitioned_records VALUES"
                + ",".join(f"({key},{key * 10})" for key in range(8)))
            cursor.execute("SELECT COUNT(*),SUM(v) FROM partitioned_records")
            assert cursor.fetchone() == (8, decimal.Decimal("280"))
            cursor.execute(
                "CREATE TABLE partitioned_lob_records("
                "id INT PRIMARY KEY,bucket INT,payload MEDIUMBLOB,"
                "INDEX partitioned_lob_bucket(bucket)) "
                "PARTITION BY HASH(id) PARTITIONS 3")
            cursor.executemany(
                "INSERT INTO partitioned_lob_records VALUES(%s,%s,%s)",
                ((0, 10, PARTITIONED_LOB_SOURCE),
                 (1, 20, PARTITIONED_LOB_SOURCE),
                 (2, 30, PARTITIONED_LOB_SOURCE)))
            cursor.execute(
                "SELECT COUNT(*),SUM(OCTET_LENGTH(payload)) FROM partitioned_lob_records")
            assert cursor.fetchone() == (
                3, decimal.Decimal(str(3 * len(PARTITIONED_LOB_SOURCE))))
            cursor.execute(
                "CREATE TABLE partitioned_truncate(id INT PRIMARY KEY,v INT) "
                "PARTITION BY HASH(id) PARTITIONS 3")
            cursor.execute(
                "INSERT INTO partitioned_truncate VALUES(0,0),(1,10),(2,20)")
            cursor.execute(
                "CREATE TABLE partitioned_redefinition(id INT PRIMARY KEY,v INT) "
                "PARTITION BY HASH(id) PARTITIONS 3")
            cursor.execute(
                "INSERT INTO partitioned_redefinition VALUES"
                + ",".join(f"({key},{key * 10})" for key in range(9)))
            cursor.execute(
                "CREATE TABLE partitioned_maintenance(id INT PRIMARY KEY,v INT) "
                "PARTITION BY RANGE(id) ("
                "PARTITION p0 VALUES LESS THAN (10),"
                "PARTITION p1 VALUES LESS THAN (20),"
                "PARTITION p2 VALUES LESS THAN (30))")
            cursor.execute(
                "INSERT INTO partitioned_maintenance VALUES(1,10),(11,110),(21,210)")
            cursor.execute(
                "CREATE TABLE subpartitioned_maintenance("
                "id INT,region INT,v INT,PRIMARY KEY(id,region)) "
                "PARTITION BY RANGE(id) SUBPARTITION BY RANGE(region) ("
                "PARTITION p0 VALUES LESS THAN (100) ("
                "SUBPARTITION p0s0 VALUES LESS THAN (10),"
                "SUBPARTITION p0s1 VALUES LESS THAN (20),"
                "SUBPARTITION p0s2 VALUES LESS THAN (30)),"
                "PARTITION p1 VALUES LESS THAN (200) ("
                "SUBPARTITION p1s0 VALUES LESS THAN (10),"
                "SUBPARTITION p1s1 VALUES LESS THAN (20),"
                "SUBPARTITION p1s2 VALUES LESS THAN (30)))")
            cursor.execute(
                "INSERT INTO subpartitioned_maintenance VALUES"
                "(1,1,10),(1,11,110),(1,21,210),"
                "(101,1,1010),(101,11,1110),(101,21,1210)")
            cursor.execute(
                "CREATE TABLE partition_exchange_source(id INT PRIMARY KEY,v INT) "
                "PARTITION BY RANGE(id) ("
                "PARTITION p0 VALUES LESS THAN (10),"
                "PARTITION p1 VALUES LESS THAN (20))")
            cursor.execute(
                "CREATE TABLE partition_exchange_heap(id INT PRIMARY KEY,v INT)")
            cursor.execute(
                "INSERT INTO partition_exchange_source VALUES(1,10),(11,110)")
            cursor.execute(
                "INSERT INTO partition_exchange_heap VALUES(2,20)")
            experiment.record("direct_table_created")
            cursor.execute("INSERT INTO t VALUES (1,'first'),(2,'second')")
            experiment.record("direct_insert_committed")
            cursor.execute("BEGIN")
            cursor.execute("UPDATE t SET v='rolled back' WHERE id=1")
            cursor.execute("ROLLBACK")
            cursor.execute("SELECT v FROM t WHERE id=1")
            assert cursor.fetchone() == ("first",)
            cursor.execute("BEGIN")
            cursor.execute("SAVEPOINT direct_savepoint")
            cursor.execute("UPDATE t SET v='savepoint rollback' WHERE id=1")
            cursor.execute("ROLLBACK TO SAVEPOINT direct_savepoint")
            cursor.execute("RELEASE SAVEPOINT direct_savepoint")
            cursor.execute("COMMIT")
            cursor.execute("SELECT v FROM t WHERE id=1")
            assert cursor.fetchone() == ("first",)
            cursor.execute("BEGIN")
            cursor.execute("UPDATE t SET v='committed' WHERE id=1")
            cursor.execute("COMMIT")
            assert not connection.server_status & 1
            cursor.execute("SELECT id,v FROM t ORDER BY id")
            assert cursor.fetchall() == ((1, "committed"), (2, "second"))
            cursor.execute("SELECT current_scn()")
            current_scn, = cursor.fetchone()
            assert int(current_scn) > 0, current_scn
            cursor.execute(
                "SELECT /*+ READ_CONSISTENCY(WEAK) */ COUNT(*) FROM t")
            assert cursor.fetchone() == (2,)
            experiment.record(
                "direct_storage_snapshot_versions_verified",
                current_scn=int(current_scn), weak_read=True)
            experiment.record("direct_ddl_dml_verified")
    def client(value):
        with connect(endpoint) as connection:
            with connection.cursor() as cursor:
                cursor.execute("SET @direct_value=%s", (value,))
                cursor.execute("SELECT @direct_value, SLEEP(0.1)")
                assert cursor.fetchone() == (value, 0)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(client, range(8)))
    if case == "full":
        protocol_probe(experiment, endpoint)
        cursor_snapshot_probe(experiment, endpoint)
    failures = []
    checks = [("management_concurrency", management_concurrency_probe),
              ("authentication", authentication_probe), ("lifecycle", lifecycle_probe),
              ("ddl", ddl_probe), ("forked_namespaces", forked_namespace_probe)]
    if case == "forked":
        checks = checks[-1:]
    for name, check in checks:
        try:
            check(experiment, endpoint)
        except Exception as error:
            traceback.print_exc()
            failures.append((name, repr(error)))
            experiment.record("direct_case_failed", case=name, error=repr(error))
    assert not failures, failures
    assert "PROTOTYPE_V18_SHARED_SQL_REJECT" not in experiment.engine_log()
    experiment.record("direct_native_ingress_verified", clients=8, transactions=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--case", choices=("full", "forked", "tls"), default="full")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    os.environ["SEEKDB_NAMESPACE_SQL_WORKER_DIRECT_PROBE"] = "1"
    os.environ["SEEKDB_NAMESPACE_DDL_PUBLISH_DELAY_US"] = "500000"
    experiment = BootstrapExperiment(args.binary, "direct_v19", prototype=6)
    # Worker endpoints are published relative to the instance base; resolve
    # them from there so deep test roots stay under the AF_UNIX sun_path limit.
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(experiment.base)
    try:
        if args.case == "tls":
            wallet_script = os.path.join(script_dir, "generate_wallet.sh")
            subprocess.run(
                [wallet_script],
                cwd=experiment.base, check=True, capture_output=True, text=True)
            experiment.extra_parameters = (
                ("ssl_client_authentication", "true"),
                ("sql_protocol_min_tls_version", "TLSv1.2"),
                ("ob_ssl_invited_common_names", "seekdb-client"))
        experiment.start()
        if args.case == "tls":
            tls_probe(experiment)
            experiment.record("PASS", namespace_worker_tls=True)
        else:
            probe(experiment, args.case)
            endpoint_recovery_probe(experiment)
            assert_worker_memory_budgets(experiment, 640 * 1024 * 1024)
            experiment.record("PASS", direct_client_ingress=True, endpoint_recovery=True)
    finally:
        experiment.close()


if __name__ == "__main__":
    main()
