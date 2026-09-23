#!/usr/bin/env python3
"""Throwaway SQL-only workers, native nested SQL, transactions and query deadlines.

SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb
Add --case insert for V14 writes, rollback, isolation and crash recovery.
Add --case dml for native drivers, automatic conflict retries, DML and recovery.
Add --case nested for native foreign keys, nested session restoration and transactions.
Add --case ddl for CREATE TABLE followed by write and read in a fork namespace.
Add --case index for inherited secondary and unique indexes.
Linux integration probe. No claim of Windows/macOS or high-concurrency validation.
"""
import argparse
import datetime
from decimal import Decimal
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import resource
import signal
import socket
import struct
import time

import pymysql
from namespace_lineage_prototype import LineageExperiment


class WorkerExperiment(LineageExperiment):
    def start(self):
        super().start()
        self.sql("ALTER SYSTEM SET syslog_level='WARN'")

    def physical(self):
        # SQL-only workers do not own the shared process's in-memory tablet map.
        # The persisted tablet mapping exercises the normal remote table scan
        # and is sufficient for the worker fork/materialization assertions.
        return self.sql("SELECT tablet_id FROM oceanbase.__all_tablet_to_table "
                        "WHERE tablet_id>=4611686018427387904 ORDER BY tablet_id", log=False)

    def worker_connect(self, namespace, client_flag=0, read_timeout=40):
        # Public entry routes root@<branch> to that namespace's worker.
        name = self.sql("SELECT CAST(name AS CHAR) FROM __fork_proto_meta.namespaces "
                        f"WHERE namespace_id={namespace}", log=False)[0][0]
        return pymysql.connect(host="127.0.0.1", port=self.port, user=f"root@{name}", password="",
                               database="db1", charset="utf8mb4",
                               autocommit=True, connect_timeout=10, read_timeout=read_timeout,
                               write_timeout=10, client_flag=client_flag)

    def worker_pid(self, namespace):
        matches = re.findall(r"PROTOTYPE_V10_WORKER_READY ns=(\d+) generation=(\d+) pid=(\d+)",
                             self.engine_log())
        return next(int(pid) for ns, _, pid in reversed(matches) if int(ns) == namespace)

    def run_inserts(self):
        self.setup_lineage()
        c, _ = self.capture("b", "c")
        first = second = sibling = None
        try:
            first, second, sibling = self.worker_connect(self.b), self.worker_connect(self.b), self.worker_connect(c)
            pid = self.worker_pid(self.b)
            self.sql("SET @v=7", first)
            with first.cursor() as cursor:
                assert cursor.execute("INSERT INTO t1 VALUES(3,@v*6),(4,NULL)") == 2
            self.sql("INSERT INTO t1(v,id) VALUES(-50,5)", first)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=3 ORDER BY id", second) == ((3,42),(4,None),(5,-50))
            self.record("worker_insert_expressions_and_affected_rows", affected=2, second_session_visible=True)

            rows = tuple((i, i*10) for i in range(100,196))
            with first.cursor() as cursor:
                assert cursor.execute("INSERT INTO t1 VALUES" + ",".join(f"({i},{v})" for i,v in rows)) == len(rows)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", second) == rows
            self.record("worker_insert_multiple_batches", rows=len(rows), batch_limit=32)

            failed_rows = ",".join(f"({i},{i*10})" for i in range(200,241)) + ",(1,999)"
            try:
                self.sql("INSERT INTO t1 VALUES" + failed_rows, first)
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1062, error.args
                self.record("duplicate_rolls_back_whole_statement", error=error.args, rows_before_duplicate=41)
            else:
                raise AssertionError("duplicate INSERT succeeded")
            assert self.sql("SELECT id,v FROM t1 WHERE id>=200", second) == ()
            assert self.sql("SELECT v FROM t1 WHERE id=1", first) == ((10,),)
            self.sql("INSERT INTO t1 VALUES(300,3000)", first)
            assert self.worker_pid(self.b) == pid

            self.sql("SET SESSION ob_query_timeout=500000", first)
            started = time.monotonic()
            try:
                self.sql("INSERT INTO t1 VALUES(400,4000),(401,1+SLEEP(2))", first)
            except pymysql.MySQLError as error:
                assert error.args[0] == 4012, error.args
                self.record("insert_timeout_rolled_back", seconds=time.monotonic()-started, error=error.args)
            else:
                raise AssertionError("expired INSERT succeeded")
            self.sql("SET SESSION ob_query_timeout=10000000", first)
            assert self.sql("SELECT id FROM t1 WHERE id>=400", second) == ()
            assert self.sql("SELECT 1", first) == ((1,),)

            with ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(self.sql, f"INSERT INTO t1 VALUES({key},{key*10})", connection)
                           for key, connection in ((310,first),(311,second))]
                for future in futures:
                    future.result(timeout=15)
            assert self.sql("SELECT id,v FROM t1 WHERE id BETWEEN 310 AND 311 ORDER BY id", first) == ((310,3100),(311,3110))
            self.record("concurrent_insert_sessions", shared_worker=pid, transactions=2)

            self.sql("BEGIN", first)
            with first.cursor() as cursor:
                assert cursor.execute("INSERT INTO t1 VALUES(1,11) ON DUPLICATE KEY UPDATE v=VALUES(v)") == 2
            assert self.sql("SELECT v FROM t1 WHERE id=1", first) == ((11,),)
            assert self.sql("SELECT v FROM t1 WHERE id=1", second) == ((10,),)
            self.sql("ROLLBACK", first)
            assert self.sql("SELECT v FROM t1 WHERE id=1", first) == ((10,),)
            self.record("native_upsert_transaction_rolled_back")
            for query in ("INSERT IGNORE INTO t1 VALUES(1,0)", "REPLACE INTO t1 VALUES(1,0)"):
                try:
                    self.sql(query, first)
                except pymysql.MySQLError as error:
                    self.record("unsupported_insert_rejected", sql=query, error=error.args)
                else:
                    raise AssertionError(query)
            self.sql("BEGIN", first)
            self.sql("INSERT INTO t1 SELECT id+1000,v FROM t1 WHERE id<=2", first)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=1000 ORDER BY id", first) == ((1001,10),(1002,20))
            assert self.sql("SELECT id FROM t1 WHERE id>=1000", second) == ()
            self.sql("ROLLBACK", first)
            assert self.sql("SELECT id FROM t1 WHERE id>=1000", first) == ()
            self.record("native_insert_select_in_transaction")
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", sibling) == ((1,10),(2,20))
            assert self.sql("SELECT id,v FROM db1.t1 ORDER BY id") == ((1,10),(2,20))
            self.record("worker_insert_namespace_isolation", source_unchanged=True, sibling_unchanged=True)
        finally:
            for connection in (first, second, sibling):
                if connection is not None:
                    connection.close()
        self.restart()
        first, sibling = self.worker_connect(self.b), self.worker_connect(c)
        try:
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", first) == rows + ((300,3000),(310,3100),(311,3110))
            assert self.sql("SELECT id,v FROM t1 WHERE id BETWEEN 3 AND 5 ORDER BY id", first) == ((3,42),(4,None),(5,-50))
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", sibling) == ((1,10),(2,20))
            self.sql("INSERT INTO t1 VALUES(301,3010)", first)
            assert self.sql("SELECT v FROM t1 WHERE id=301", first) == ((3010,),)
            self.record("PASS", case="namespace_worker_insert", crash_recovery=True,
                        worker_sql_and_das=True, shared_transaction_and_storage=True)
        finally:
            first.close()
            sibling.close()

    def run_ddl(self):
        self.setup_lineage()
        c = d = schema_only = None
        connection = source = child = grandchild = catalogless = None
        def assert_missing(table, handle):
            try:
                self.sql("SELECT * FROM " + table, handle)
            except pymysql.ProgrammingError as error:
                assert error.args[0] == 1146, error.args
            else:
                raise AssertionError(table + " remained visible")
        def assert_lob(handle, marker):
            with handle.cursor() as cursor:
                cursor.execute("SELECT payload,text_value FROM inherited_lob WHERE id=1")
                assert cursor.fetchone() == (marker.encode() * 20000, marker * 20000)
        try:
            connection = self.worker_connect(self.b, read_timeout=20)
            assert {row[0] for row in self.sql("SHOW DATABASES", connection)} >= {"db1", "db2"}
            self.sql("USE db2", connection)
            assert self.sql("SHOW TABLES", connection) == (("t1",),)
            self.sql("USE db1", connection)
            root_before_worker_ddl = self.root("b")
            assert root_before_worker_ddl[2] == 0, root_before_worker_ddl
            self.sql("CREATE TABLE created_after_fork(id INT PRIMARY KEY,v INT)", connection)
            assert ("created_after_fork",) in self.sql("SHOW TABLES", connection)
            self.sql("CREATE TABLE inherited_drop(id INT PRIMARY KEY,v INT)", connection)
            self.sql("INSERT INTO inherited_drop VALUES(1,10)", connection)
            for table in ("cold_drop", "inherited_multi_a", "inherited_multi_b",
                          "inherited_mixed", "inherited_cow", "atomic_survivor"):
                self.sql("CREATE TABLE " + table + "(id INT PRIMARY KEY,v INT)", connection)
            self.sql("INSERT INTO inherited_cow VALUES(1,10)", connection)
            self.sql("INSERT INTO atomic_survivor VALUES(1,10)", connection)
            self.sql("CREATE TABLE inherited_lob(id INT PRIMARY KEY,payload MEDIUMBLOB,"
                     "text_value MEDIUMTEXT) LOB_INROW_THRESHOLD=0", connection)
            self.sql("INSERT INTO inherited_lob VALUES(1,REPEAT('b',20000),REPEAT('b',20000))",
                     connection)
            self.sql("CREATE TABLE inherited_write_first(id INT PRIMARY KEY,payload MEDIUMBLOB) "
                     "LOB_INROW_THRESHOLD=0", connection)
            self.sql("CREATE TABLE inherited_aux_drop(id INT PRIMARY KEY,k INT,payload MEDIUMBLOB,"
                     "KEY inherited_aux_idx(k)) LOB_INROW_THRESHOLD=0", connection)
            self.sql("INSERT INTO inherited_aux_drop VALUES(1,7,REPEAT('p',20000))", connection)
            self.sql("CREATE TABLE drop_after_fork(id INT PRIMARY KEY,v INT)", connection)
            self.sql("DROP TABLE drop_after_fork", connection)
            self.sql("DROP TABLE IF EXISTS drop_after_fork", connection)
            assert_missing("drop_after_fork", connection)
            self.sql("INSERT INTO created_after_fork VALUES(1,10)", connection)
            assert self.sql("SELECT id,v FROM created_after_fork", connection) == ((1,10),)
            root_after_worker_ddl = self.root("b")
            assert root_after_worker_ddl[2] == root_before_worker_ddl[2], (
                root_before_worker_ddl, root_after_worker_ddl)
            assert root_after_worker_ddl[4] != root_before_worker_ddl[4], (
                root_before_worker_ddl, root_after_worker_ddl)
            self.record("worker_ddl_updates_directory_without_schema_catalog",
                        namespace=self.b,
                        catalog_root=root_after_worker_ddl[2],
                        directory_before=root_before_worker_ddl[4],
                        directory_after=root_after_worker_ddl[4])
            source = self.worker_connect(self.root("a")[0])
            assert_missing("created_after_fork", source)
            c, _ = self.capture("b", "c")
            child = self.worker_connect(c)
            assert self.sql("SELECT id,v FROM created_after_fork", child) == ((1,10),)

            inherited_aux_data_id = self.sql(
                "SELECT table_id FROM oceanbase.__all_table "
                "WHERE table_name='inherited_aux_drop'", child)[0][0]
            inherited_aux_schemas = self.sql(
                "SELECT table_id,table_name,tablet_id FROM oceanbase.__all_table "
                f"WHERE table_id={inherited_aux_data_id} OR data_table_id={inherited_aux_data_id} "
                "ORDER BY table_id", child)
            assert len(inherited_aux_schemas) == 4, inherited_aux_schemas
            physical_before_aux_drop = set(self.physical())
            self.sql("DROP TABLE inherited_aux_drop", child)
            assert_missing("inherited_aux_drop", child)
            assert self.sql(
                "SELECT table_id,table_name,tablet_id FROM oceanbase.__all_table "
                f"WHERE table_id={inherited_aux_data_id} OR data_table_id={inherited_aux_data_id}",
                child) == ()
            physical_after_aux_drop = set(self.physical())
            assert physical_before_aux_drop <= physical_after_aux_drop, (
                physical_before_aux_drop - physical_after_aux_drop)
            assert self.sql(
                "SELECT table_id,table_name,tablet_id FROM oceanbase.__all_table "
                f"WHERE table_id={inherited_aux_data_id} OR data_table_id={inherited_aux_data_id} "
                "ORDER BY table_id", connection) == inherited_aux_schemas
            assert self.sql(
                "SELECT id,LENGTH(payload) FROM inherited_aux_drop FORCE INDEX(inherited_aux_idx) "
                "WHERE k=7", connection) == ((1,20000),)

            lob_before = self.physical()
            assert_lob(child, "b")
            lob_after = self.physical()
            assert len(set(lob_after) - set(lob_before)) == 3, (lob_before, lob_after)
            self.sql("UPDATE inherited_lob SET payload=REPEAT('c',20000),"
                     "text_value=REPEAT('c',20000) WHERE id=1", child)
            assert_lob(child, "c")
            assert_lob(connection, "b")

            cold_tablet = self.sql(
                "SELECT tablet_id FROM oceanbase.__all_table WHERE table_name='cold_drop'",
                child)[0][0]
            cold_physical = self.physical()
            self.sql("DROP TABLE cold_drop", child)
            cold_after = self.physical()
            cold_storage_tablet = ((1 << 62) | (c << 32) | cold_tablet,)
            assert cold_storage_tablet not in cold_physical
            assert cold_storage_tablet not in cold_after
            assert_missing("cold_drop", child)

            self.sql("DROP TABLE inherited_multi_a,inherited_multi_b", child)
            assert_missing("inherited_multi_a", child)
            assert_missing("inherited_multi_b", child)
            assert self.sql("SELECT id,v FROM atomic_survivor", child) == ((1,10),)

            self.sql("CREATE TABLE child_local_mixed(id INT PRIMARY KEY,v INT)", child)
            assert self.sql("SELECT id,v FROM atomic_survivor", child) == ((1,10),)
            self.sql("DROP TABLE child_local_mixed,inherited_mixed", child)
            assert_missing("child_local_mixed", child)
            assert_missing("inherited_mixed", child)

            assert self.sql("SELECT id,v FROM atomic_survivor", child) == ((1,10),)
            failed_drop = self.worker_connect(c)
            try:
                try:
                    self.sql("DROP TABLE atomic_survivor,missing_table", failed_drop)
                except pymysql.MySQLError as error:
                    assert error.args[0] in (1051,1146), error.args
                else:
                    raise AssertionError("multi-table DROP ignored a missing table")
            finally:
                failed_drop.close()
            assert self.sql("SELECT id,v FROM atomic_survivor", child) == ((1,10),)

            cow_before = self.physical()
            self.sql("UPDATE inherited_cow SET v=20 WHERE id=1", child)
            cow_after = self.physical()
            assert len(set(cow_after) - set(cow_before)) == 1, (cow_before, cow_after)
            self.sql("DROP TABLE inherited_cow", child)
            self.wait_until(lambda: self.physical() == cow_before,
                            "DROP did not reclaim the namespace-private tablet")
            assert_missing("inherited_cow", child)

            assert self.sql("SELECT id,v FROM inherited_drop", child) == ((1,10),)
            self.sql("DROP TABLE inherited_drop", child)
            assert_missing("inherited_drop", child)
            assert self.sql("SELECT id,v FROM inherited_drop", connection) == ((1,10),)
            for table in ("cold_drop", "inherited_multi_a", "inherited_multi_b", "inherited_mixed"):
                assert self.sql("SELECT COUNT(*) FROM " + table, connection) == ((0,),)
            assert self.sql("SELECT id,v FROM inherited_cow", connection) == ((1,10),)
            d, _ = self.capture("c", "d")
            self.sql("INSERT INTO created_after_fork VALUES(2,20)", child)
            assert self.sql("SELECT id,v FROM created_after_fork ORDER BY id", child) == ((1,10),(2,20))
            assert self.sql("SELECT id,v FROM created_after_fork", connection) == ((1,10),)
        finally:
            for handle in (connection, source, child, grandchild, catalogless):
                if handle is not None:
                    handle.close()
        self.restart()
        connection = child = grandchild = catalogless = None
        try:
            connection = self.worker_connect(self.b)
            child, grandchild = self.worker_connect(c), self.worker_connect(d)
            assert self.sql(
                "SELECT id,LENGTH(payload) FROM inherited_aux_drop FORCE INDEX(inherited_aux_idx) "
                "WHERE k=7", connection) == ((1,20000),)
            assert self.sql("SELECT id,v FROM created_after_fork ORDER BY id", child) == ((1,10),(2,20))
            assert self.sql("SELECT id,v FROM created_after_fork", grandchild) == ((1,10),)
            assert_lob(child, "c")
            schema_only, _ = self.capture("d", "schema_only")
            catalogless = self.worker_connect(schema_only)
            # Load the child SchemaService while the inherited catalog is still
            # available, then remove that duplicate schema source.  Its first
            # LOB access must materialize the main/meta/piece binding unit from
            # the schemas supplied by the Worker scan request alone.
            assert ("inherited_lob",) in self.sql("SHOW TABLES", catalogless)
            assert self.sql(
                "SELECT table_name FROM oceanbase.__all_table "
                "WHERE table_name='inherited_lob'", catalogless) == (("inherited_lob",),)
            write_first_table_id = self.sql(
                "SELECT table_id FROM oceanbase.__all_table "
                "WHERE table_name='inherited_write_first'", catalogless)[0][0]
            write_first_tablets = self.sql(
                "SELECT tablet_id FROM oceanbase.__all_table "
                f"WHERE table_id={write_first_table_id} "
                f"OR data_table_id={write_first_table_id} ORDER BY table_id",
                catalogless)
            assert len(write_first_tablets) == 3, write_first_tablets
            write_first_physical = {
                (1 << 62) | (schema_only << 32) | row[0]
                for row in write_first_tablets
            }
            assert self.root("schema_only")[2] == 0, self.root("schema_only")
            write_first_before = {row[0] for row in self.physical()}
            assert write_first_physical.isdisjoint(write_first_before), (
                write_first_physical, write_first_before)
            self.sql(
                "INSERT INTO inherited_write_first VALUES(1,REPEAT('w',20000))",
                catalogless)
            write_first_after = {row[0] for row in self.physical()}
            assert write_first_physical <= write_first_after, (
                write_first_physical, write_first_after)
            assert self.sql(
                "SELECT id,LENGTH(payload) FROM inherited_write_first", catalogless
            ) == ((1,20000),)
            assert_lob(catalogless, "c")
            self.record("worker_schema_drives_lob_materialization_without_catalog",
                        namespace=schema_only, read_first=True, write_first=True)
            assert_lob(grandchild, "c")
            for handle in (child, grandchild):
                for table in ("cold_drop", "inherited_multi_a", "inherited_multi_b",
                              "inherited_mixed", "inherited_cow", "inherited_drop",
                              "inherited_aux_drop"):
                    assert_missing(table, handle)
                assert self.sql("SELECT id,v FROM atomic_survivor", handle) == ((1,10),)
            self.record("PASS", case="namespace_worker_ddl", create_table=True,
                        schema_visible_to_worker=True, source_isolated=True,
                        inherited_drop_isolated=True, multi_drop_atomic=True,
                        mixed_drop=True, private_tablet_reclaimed=True,
                        inherited_aux_schema_dropped=True,
                        lob_binding_unit_materialized=True,
                        worker_schema_drives_materialization=True,
                        descendant_inherits_schema=True, descendant_storage_isolated=True,
                        crash_recovery=True)
        finally:
            for handle in (connection, child, grandchild, catalogless):
                if handle is not None:
                    handle.close()

    def run_indexes(self):
        self.setup_lineage()
        c = d = None
        source = child = grandchild = None
        try:
            source = self.worker_connect(self.b)
            self.sql("CREATE TABLE indexed_t(id INT PRIMARY KEY,k INT,u INT,v INT,"
                     "KEY idx_k(k),UNIQUE KEY uk_u(u))", source)
            self.sql("INSERT INTO indexed_t VALUES(1,10,100,1000),(2,20,200,2000)", source)
            c, _ = self.capture("b", "c")
            child = self.worker_connect(c)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=20", child) == ((2,2000),)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(uk_u) WHERE u=100", child) == ((1,1000),)
            self.sql("INSERT INTO indexed_t VALUES(3,30,300,3000)", child)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=30", child) == ((3,3000),)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(uk_u) WHERE u=300", child) == ((3,3000),)
            assert self.sql("SELECT id FROM indexed_t WHERE id=3", source) == ()
            self.sql("UPDATE indexed_t SET k=21,u=201,v=2001 WHERE id=2", child)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=20", child) == ()
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=21", child) == ((2,2001),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(uk_u) WHERE u=200", child) == ()
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(uk_u) WHERE u=201", child) == ((2,2001),)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=20", source) == ((2,2000),)
            try:
                self.sql("UPDATE indexed_t SET k=31,u=201,v=9999 WHERE id=3", child)
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1062, error.args
            else:
                raise AssertionError("unique index conflict succeeded")
            assert self.sql("SELECT k,u,v FROM indexed_t WHERE id=3", child) == ((30,300,3000),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=31", child) == ()
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=30", child) == ((3,),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(uk_u) WHERE u=300", child) == ((3,),)

            self.sql("DELETE FROM indexed_t WHERE id=1", child)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=10", child) == ()
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(uk_u) WHERE u=100", child) == ()
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=10", source) == ((1,),)

            d, _ = self.capture("c", "d")
            grandchild = self.worker_connect(d)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=21", grandchild) == ((2,2001),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(uk_u) WHERE u=100", grandchild) == ()
            self.sql("UPDATE indexed_t SET k=32,u=302,v=3002 WHERE id=3", child)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=32", child) == ((3,3002),)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=30", grandchild) == ((3,3000),)
        finally:
            for handle in (source, child, grandchild):
                if handle is not None:
                    handle.close()
        self.restart()
        source = child = grandchild = None
        try:
            source = self.worker_connect(self.b)
            child = self.worker_connect(c)
            grandchild = self.worker_connect(d)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(idx_k) WHERE k=10", source) == ((1,1000),)
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(uk_u) WHERE u=302", child) == ((3,3002),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=10", child) == ()
            assert self.sql("SELECT id,v FROM indexed_t FORCE INDEX(uk_u) WHERE u=300", grandchild) == ((3,3000),)
            assert self.sql("SELECT id FROM indexed_t FORCE INDEX(idx_k) WHERE k=32", grandchild) == ()
            self.record("PASS", case="namespace_worker_index", inherited_secondary_index=True,
                        inherited_unique_index=True, child_insert_updates_indexes=True,
                        child_update_rekeys_indexes=True, child_delete_updates_indexes=True,
                        unique_conflict_rolls_back=True, descendant_inherits_indexes=True,
                        descendant_storage_isolated=True, source_isolated=True,
                        crash_recovery=True)
        finally:
            for handle in (source, child, grandchild):
                if handle is not None:
                    handle.close()

    def run_nested(self):
        # Native schemas and foreign keys are enrolled before the namespace fork.
        # All statements under test then enter the worker through its public port.
        self.sql("CREATE DATABASE db1")
        self.sql("CREATE TABLE db1.parent(id INT PRIMARY KEY,v INT)")
        self.sql("CREATE TABLE db1.child(id INT PRIMARY KEY,v INT,"
                 "CONSTRAINT child_parent FOREIGN KEY(id) REFERENCES db1.parent(id) ON DELETE CASCADE)")
        self.sql("CREATE TABLE db1.grandchild(id INT PRIMARY KEY,v INT,"
                 "CONSTRAINT grandchild_child FOREIGN KEY(id) REFERENCES db1.child(id) ON DELETE CASCADE)")
        self.sql("CREATE TABLE db1.leaf(id INT PRIMARY KEY,v INT,"
                 "CONSTRAINT leaf_grandchild FOREIGN KEY(id) REFERENCES db1.grandchild(id) ON DELETE RESTRICT)")
        self.sql("FORK DATABASE __empty__ TO a")
        self.sql("FORK DATABASE a TO b")
        self.b = self.root("b")[0]
        first = second = None
        try:
            first, second = self.worker_connect(self.b), self.worker_connect(self.b)
            self.sql("SET @outer_value=73", first)
            self.sql("BEGIN", first)
            assert first.server_status & 1, "BEGIN did not report IN_TRANS"
            self.sql("INSERT INTO parent VALUES(10,100)", first)
            self.sql("INSERT INTO child VALUES(10,@outer_value)", first)
            self.sql("INSERT INTO grandchild VALUES(10,1000)", first)
            self.sql("INSERT INTO leaf VALUES(10,10000)", first)
            assert self.sql("SELECT v FROM child WHERE id=10", first) == ((73,),)
            assert self.sql("SELECT * FROM parent", second) == ()
            assert self.sql("SELECT @outer_value, DATABASE(), @@autocommit", first) == ((73,"db1",1),)
            self.record("nested_foreign_key_reads_uncommitted_parent", session=first.thread_id())

            self.sql("INSERT INTO parent VALUES(11,110)", first)
            try:
                self.sql("INSERT INTO child VALUES(11,110),(999,999)", first)
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1452, error.args
                self.record("nested_failure_rolls_back_only_statement", error=error.args)
            else:
                raise AssertionError("foreign key violation was accepted")
            assert self.sql("SELECT id FROM parent ORDER BY id", first) == ((10,),(11,))
            assert self.sql("SELECT id FROM child ORDER BY id", first) == ((10,),)
            self.sql("INSERT INTO child VALUES(11,110)", first)
            self.sql("SAVEPOINT keep_rows", first)
            try:
                self.sql("DELETE FROM parent WHERE id=10", first)
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1451, error.args
            else:
                raise AssertionError("nested RESTRICT violation was accepted")
            for table in ("parent","child","grandchild","leaf"):
                assert self.sql("SELECT id FROM " + table + " WHERE id=10", first) == ((10,),)
            assert self.sql("SELECT @outer_value, DATABASE(), @@autocommit", first) == ((73,"db1",1),)
            self.record("inner_sql_failure_restores_outer_statement_and_session", session=first.thread_id())
            self.sql("DELETE FROM leaf WHERE id=10", first)
            self.sql("DELETE FROM parent WHERE id=10", first)
            assert self.sql("SELECT * FROM grandchild", first) == ()
            assert self.sql("SELECT id FROM child", first) == ((11,),)
            self.sql("ROLLBACK TO SAVEPOINT keep_rows", first)
            assert self.sql("SELECT id FROM grandchild", first) == ((10,),)
            self.sql("RELEASE SAVEPOINT keep_rows", first)
            worker_log = "".join(path.read_text(errors="replace") for path in
                                 (self.base / "run").glob(f"namespace-worker-{self.b}-*/process.out"))
            assert re.search(rf"PROTOTYPE_V17_INNER_SQL session={first.thread_id()} nested=2", worker_log), worker_log[-3000:]
            self.record("multilevel_cascade_and_savepoint", depth=2, session=first.thread_id())
            self.sql("ROLLBACK", first)
            assert not first.server_status & 1
            for table in ("parent","child","grandchild","leaf"):
                assert self.sql("SELECT * FROM " + table, first) == ()
                assert self.sql("SELECT * FROM " + table, second) == ()
            self.record("whole_transaction_rollback")

            self.sql("SET autocommit=0", first)
            assert not first.server_status & 2
            self.sql("INSERT INTO parent VALUES(20,200)", first)
            self.sql("INSERT INTO child VALUES(20,200)", first)
            self.sql("COMMIT", first)
            assert self.sql("SELECT id FROM child", second) == ((20,),)
            self.sql("SET autocommit=1", first)
            assert first.server_status & 2
            self.record("autocommit_and_commit_visibility")

            self.sql("BEGIN", first)
            self.sql("INSERT INTO parent VALUES(30,300)", first)
            self.sql("SET ob_query_timeout=500000", first)
            try:
                self.sql("INSERT INTO child VALUES(30,300),(31,1+SLEEP(2))", first)
            except pymysql.MySQLError as error:
                assert error.args[0] == 4012, error.args
            else:
                raise AssertionError("expired statement succeeded")
            self.sql("SET ob_query_timeout=10000000", first)
            assert self.sql("SELECT id FROM parent WHERE id=30", first) == ((30,),)
            assert self.sql("SELECT id FROM child WHERE id=30", first) == ()
            self.sql("INSERT INTO child VALUES(30,300)", first)
            self.sql("COMMIT", first)
            assert self.sql("SELECT id FROM child WHERE id=30", second) == ((30,),)
            self.record("cancel_preserves_prior_transaction_work")

            self.sql("BEGIN", first)
            self.sql("UPDATE parent SET v=201 WHERE id=20", first)
            sid, offset = first.thread_id(), len(self.engine_log())
            first.close(); first = None
            self.wait_until(lambda: f"PROTOTYPE_V14_TX_RELEASED session={sid} rollback=1" in self.engine_log()[offset:],
                            "disconnect rollback missing")
            assert self.sql("SELECT v FROM parent WHERE id=20", second) == ((200,),)
            self.sql("UPDATE parent SET v=202 WHERE id=20", second)
            assert self.sql("SELECT v FROM parent WHERE id=20", second) == ((202,),)
            self.record("disconnect_rolls_back_and_unlocks")

            first = self.worker_connect(self.b)
            self.sql("BEGIN", first)
            self.sql("UPDATE parent SET v=999 WHERE id=20", first)
            os.kill(self.worker_pid(self.b), signal.SIGKILL)
            time.sleep(0.5)
            second.close(); second = None
            second = self.worker_connect(self.b)
            assert self.sql("SELECT v FROM parent WHERE id=20", second) == ((202,),)
            self.sql("UPDATE parent SET v=203 WHERE id=20", second)
            assert self.sql("SELECT v FROM parent WHERE id=20", second) == ((203,),)
            self.record("worker_death_rolls_back_idle_transaction")
            assert self.sql("SELECT * FROM db1.parent") == ()
            self.record("PASS", case="namespace_worker_nested", native_inner_sql=True,
                        session_reused=True, storage_shared=True)
        finally:
            for connection in (first, second):
                if connection is not None:
                    connection.close()

    def run_dml(self):
        self.setup_lineage()
        c, _ = self.capture("b", "c")
        first = second = sibling = None
        try:
            first, second, sibling = self.worker_connect(self.b), self.worker_connect(self.b), self.worker_connect(c)
            assert self.sql("SELECT CAST(12.340 AS DECIMAL(8,3)), DATE '2026-09-15', "
                            "TIMESTAMP '2026-09-15 01:02:03', TIME '-12:34:56', NULL, '你好'", first) == (
                (Decimal("12.340"), datetime.date(2026,9,15), datetime.datetime(2026,9,15,1,2,3),
                 -datetime.timedelta(hours=12, minutes=34, seconds=56), None, "你好"),)
            self.record("native_driver_result_types", decimal=True, date=True, datetime=True, time=True, utf8=True)
            original_mode = self.sql("SELECT @@sql_mode", first)[0][0]
            with first.cursor() as cursor:
                cursor.execute("SET sql_mode='NO_BACKSLASH_ESCAPES'")
                assert cursor._result.server_status & 512, "native OK lost NO_BACKSLASH_ESCAPES"
                cursor.execute("SET sql_mode=%s", (original_mode,))
                assert not cursor._result.server_status & 512
            self.record("native_ok_status_flags", no_backslash_escapes=True)
            self.sql("INSERT INTO t1 VALUES(3,NULL),(4,-40)", first)
            self.sql("SET @delta=7", first)
            with first.cursor() as cursor:
                assert cursor.execute("UPDATE t1 SET v=COALESCE(v,0)+@delta WHERE id<=4 AND (v IS NULL OR v<20)") == 3
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", second) == ((1,17),(2,20),(3,7),(4,-33))
            with first.cursor() as cursor:
                assert cursor.execute("UPDATE t1 SET v=v WHERE id=1") == 0
                assert cursor.execute("UPDATE t1 SET v=99 WHERE id=999") == 0
                assert cursor.execute("DELETE FROM t1 WHERE id=999") == 0
                assert cursor.execute("UPDATE t1 SET v=CASE WHEN id=2 THEN v+1 ELSE v END") == 1
                assert cursor.execute("UPDATE t1 SET v=v-1 WHERE id=2") == 1
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", second) == ((1,17),(2,20),(3,7),(4,-33))
            self.record("dml_filters_expressions_affected_rows", noop_update=0, no_match=0, mixed_update=1)
            found_rows = self.worker_connect(self.b, client_flag=pymysql.constants.CLIENT.FOUND_ROWS)
            try:
                with found_rows.cursor() as cursor:
                    assert cursor.execute("UPDATE t1 SET v=v WHERE id=1") == 1
                    assert cursor.execute("UPDATE t1 SET v=v WHERE id=999") == 0
                self.record("native_client_capabilities", found_rows=1, unchanged_default=0)
            finally:
                found_rows.close()

            rows = tuple((i, i*10) for i in range(100,196))
            self.sql("INSERT INTO t1 VALUES" + ",".join(f"({i},{v})" for i,v in rows), first)
            with first.cursor() as cursor:
                assert cursor.execute("UPDATE t1 SET v=v+5 WHERE id>=100") == 96
                assert cursor.execute("UPDATE t1 SET id=id+1000 WHERE id>=100") == 96
            moved = tuple((i+1000, v+5) for i,v in rows)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", second) == moved
            self.record("update_batches_and_primary_key", rows=96)
            try:
                self.sql("UPDATE t1 SET id=CASE WHEN id=1141 THEN 1 ELSE id+1000 END WHERE id>=1100 ORDER BY id", first)
            except pymysql.IntegrityError as error:
                assert error.args[0] == 1062, error.args
                self.record("update_duplicate_rolls_back_batches", error=error.args, successful_rows_before_conflict=41)
            else:
                raise AssertionError("duplicate UPDATE succeeded")
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", second) == moved

            with first.cursor() as cursor:
                assert cursor.execute("DELETE FROM t1 WHERE id>=1100 AND v<1645") == 64
            remaining = moved[64:]
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", second) == remaining
            self.record("delete_multiple_batches", rows=64)
            self.sql("SET SESSION ob_query_timeout=500000", first)
            started = time.monotonic()
            try:
                self.sql("DELETE FROM t1 WHERE id>=1100 AND SLEEP(2)=0", first)
            except pymysql.MySQLError as error:
                assert error.args[0] == 4012, error.args
                self.record("delete_timeout_rolled_back", seconds=time.monotonic()-started, error=error.args)
            else:
                raise AssertionError("expired DELETE succeeded")
            self.sql("SET SESSION ob_query_timeout=10000000", first)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=100 ORDER BY id", second) == remaining

            with ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(self.sql, "UPDATE t1 SET v=v+1+SLEEP(0.2) WHERE id=1", connection)
                           for connection in (first, second)]
                for future in futures:
                    future.result(timeout=15)
            assert self.sql("SELECT v FROM t1 WHERE id=1", second) == ((19,),)
            self.record("concurrent_update_same_row", initial=17, final=19, client_retries=0)

            scans_before = self.engine_log().count("PROTOTYPE_V15_TX_SCAN")
            with ThreadPoolExecutor(max_workers=1) as pool:
                slow = pool.submit(self.sql, "UPDATE t1 SET v=v+1+SLEEP(2) WHERE id=2", first)
                self.wait_until(lambda: self.engine_log().count("PROTOTYPE_V15_TX_SCAN") > scans_before,
                                "slow UPDATE did not acquire its snapshot")
                self.sql("UPDATE t1 SET v=v+1 WHERE id=2", second)
                slow.result(timeout=15)
            assert self.sql("SELECT v FROM t1 WHERE id=2", second) == ((22,),)
            worker_log = "".join(path.read_text(errors="replace") for path in
                                 (self.base / "run").glob(f"namespace-worker-{self.b}-*/process.out"))
            assert re.search(r"PROTOTYPE_V16_NATIVE_EXECUTE .*attempt=[1-9]", worker_log), "no native retry observed"
            self.record("update_snapshot_conflict_retried", initial=20, final=22, client_retries=0)
            for query in ("UPDATE IGNORE t1 SET v=0", "DELETE IGNORE FROM t1"):
                try:
                    self.sql(query, first)
                except pymysql.MySQLError as error:
                    self.record("unsupported_dml_rejected", sql=query, error=error.args)
                else:
                    raise AssertionError(query)
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", sibling) == ((1,10),(2,20))
            assert self.sql("SELECT id,v FROM db1.t1 ORDER BY id") == ((1,10),(2,20))
            expected = ((1,19),(2,22),(3,7),(4,-33)) + remaining
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", first) == expected
            self.record("update_delete_namespace_isolation", source_unchanged=True, sibling_unchanged=True)
        finally:
            for connection in (first, second, sibling):
                if connection is not None:
                    connection.close()
        self.restart()
        first, sibling = self.worker_connect(self.b), self.worker_connect(c)
        try:
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", first) == expected
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", sibling) == ((1,10),(2,20))
            with first.cursor() as cursor:
                assert cursor.execute("UPDATE t1 SET v=NULL WHERE id=3") == 1
                assert cursor.execute("DELETE FROM t1 WHERE id=4") == 1
            assert self.sql("SELECT id,v FROM t1 WHERE id<=4 ORDER BY id", first) == ((1,19),(2,22),(3,None))
            self.record("PASS", case="namespace_worker_dml", crash_recovery=True,
                        worker_sql_and_das=True, shared_transaction_and_storage=True)
        finally:
            first.close()
            sibling.close()

    def run_sessions(self, first, second):
        assert self.sql("SELECT CONNECTION_ID()", first) == ((first.thread_id(),),)
        self.sql("SET @x=17, @label='你好'", first)
        assert self.sql("SELECT @x, @label", first) == ((17, "你好"),)
        assert self.sql("SELECT @x, @label", second) == ((None, None),)
        self.sql("SET @x=29", second)
        assert self.sql("SELECT @x", first) == ((17,),)
        initial_mode = self.sql("SELECT @@sql_mode", second)
        self.sql("SET SESSION sql_mode='ANSI_QUOTES'", first)
        assert self.sql('SELECT "id" FROM t1 WHERE id=1', first) == ((1,),)
        assert self.sql("SELECT @@sql_mode", first) == (("ANSI_QUOTES",),)
        assert self.sql("SELECT @@sql_mode", second) == initial_mode
        self.sql("SET NAMES utf8mb4 COLLATE utf8mb4_bin", first)
        assert self.sql("SELECT @@collation_connection, @label", first) == (("utf8mb4_bin", "你好"),)
        self.sql("SET SESSION ob_query_timeout=5000000", first)
        assert self.sql("SELECT @@ob_query_timeout", first) == ((5000000,),)
        assert self.sql("SELECT '; stays inside a string';", first) == (("; stays inside a string",),)
        self.sql("USE db2", first)
        assert self.sql("SELECT DATABASE(),v FROM t1 WHERE id=1", first) == (("db2", 10),)
        assert self.sql("SELECT DATABASE(),v FROM t1 WHERE id=1", second) == (("db1", 90),)
        first.select_db("db1")
        assert self.sql("SELECT DATABASE(),v FROM t1 WHERE id=1", first) == (("db1", 90),)
        first.select_db("oceanbase")
        assert self.sql("SELECT DATABASE()", first) == (("oceanbase",),)
        assert self.sql("SELECT COUNT(*) FROM __all_database WHERE database_name='db1'", first) == ((1,),)
        first.select_db("db1")
        for database in ("missing_db", "__fork_ns_3__db1", "db1`; SET @x=999; --"):
            try:
                first.select_db(database)
            except pymysql.MySQLError as error:
                self.record("database_change_rejected", database=database, error=error.args)
            else:
                raise AssertionError(database)
            assert self.sql("SELECT DATABASE(),@x", first) == (("db1", 17),)
        for query in ("SET GLOBAL sql_mode=''", "SELECT missing_column FROM t1"):
            try:
                self.sql(query, first)
            except pymysql.MySQLError as error:
                self.record("session_command_rejected", sql=query, error=error.args)
            else:
                raise AssertionError(query)
            assert self.sql("SELECT @x", first) == ((17,),)
        # Native worker connections run the full MySQL command set: subquery
        # assignment and multi-statements were frame-protocol rejects only.
        self.sql("SET @y=(SELECT v FROM t1 WHERE id=1)", first)
        assert self.sql("SELECT @y", first) == self.sql("SELECT v FROM t1 WHERE id=1", first)
        with first.cursor() as cursor:
            assert cursor.execute("SELECT @x; SET @x=999; SELECT @x") == 1
            assert cursor.fetchall() == ((17,),)
            assert cursor.nextset()
            cursor.fetchall()
            assert cursor.nextset()
            assert cursor.fetchall() == ((999,),)
            assert cursor.nextset() is None
        assert self.sql("SELECT @x", first) == ((999,),)
        self.sql("SET @x=17", first)
        self.record("native_subquery_assignment_and_multi_statement", supported=True)
        self.record("persistent_session_variables_and_database", isolated=True, native_commands=True)

        # Native worker sessions are observable through the worker's own
        # processlist; the proxy holds exactly one worker session per client.
        def worker_sessions():
            return self.sql("SELECT COUNT(*) FROM information_schema.processlist", first)[0][0]
        extras = []
        baseline = worker_sessions()
        try:
            for i in range(12):
                conn = self.worker_connect(self.b)
                extras.append(conn)
                self.sql(f"SET @x={100+i}", conn)
            assert worker_sessions() == baseline + len(extras)
            # Existing native sessions keep their own connection state.
            assert self.sql("SELECT @x,@label", first) == ((17, "你好"),)
            for i, conn in enumerate(extras):
                assert self.sql("SELECT @x", conn) == ((100+i,),)
        finally:
            for conn in extras:
                conn.close()
        self.wait_until(lambda: worker_sessions() == baseline, "sessions not reclaimed")
        for i in range(8):
            conn = self.worker_connect(self.b)
            assert self.sql("SELECT @x,@label", conn) == ((None, None),)
            # Alternate graceful COM_QUIT and abrupt TCP resets.
            if i % 2:
                conn._sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                conn._force_close()
            else:
                conn.close()
            self.wait_until(lambda: worker_sessions() == baseline, "closed session not reclaimed")
        self.record("native_sessions_reclaimed_on_close_and_reset", live=worker_sessions())

        interrupted = self.worker_connect(self.b)
        scan_log = self.base / "log" / "seekdb.log"
        offset = scan_log.stat().st_size
        pid = self.worker_pid(self.b)
        def scan_opened():
            with scan_log.open("rb") as log:
                log.seek(offset)
                return f"PROTOTYPE_V10_SCAN_OPEN ns={self.b} ".encode() in log.read()
        try:
            with ThreadPoolExecutor(max_workers=1) as pool:
                pending = pool.submit(self.sql, "SELECT SLEEP(2)+v FROM t1 WHERE id=1", interrupted)
                self.wait_until(scan_opened, "disconnect probe never opened scan")
                interrupted._sock.shutdown(socket.SHUT_RDWR)
                try:
                    pending.result(timeout=10)
                except pymysql.MySQLError:
                    pass
                else:
                    raise AssertionError("disconnected query succeeded")
            self.wait_until(lambda: worker_sessions() == baseline,
                            "in-flight disconnect did not reclaim session")
            assert self.worker_pid(self.b) == pid
            assert self.sql("SELECT @x,v FROM t1 WHERE id=1", first) == ((17,90),)
            assert self.sql("SELECT @x", second) == ((29,),)
            self.record("inflight_disconnect_aborts_without_killing_other_sessions", pid=pid)
        finally:
            interrupted.close()
        self.sql("SET ob_query_timeout=30000000", first)

    def run_concurrency(self, first, second):
        scan_log = self.base / "log" / "seekdb.log"
        offset = scan_log.stat().st_size
        def opened():
            with scan_log.open("rb") as log:
                log.seek(offset)
                return f"PROTOTYPE_V10_SCAN_OPEN ns={self.b} ".encode() in log.read()
        with ThreadPoolExecutor(max_workers=2) as pool:
            slow = pool.submit(self.sql, "SELECT SLEEP(3),@x,v FROM t1 WHERE id=1", first)
            self.wait_until(opened, "slow query never opened storage scan")
            start = time.monotonic()
            assert self.sql("SELECT @x,v FROM t1 WHERE id=1", second) == ((29,90),)
            elapsed = time.monotonic()-start
            assert elapsed < 2 and not slow.done(), elapsed
            assert slow.result(timeout=10) == ((0,17,90),)
            def scan_many(connection, variable, reverse):
                for _ in range(8):
                    rows = self.sql("SELECT id,v+@x,SLEEP(0.02) FROM t1 ORDER BY id " +
                                    ("DESC" if reverse else "ASC"), connection, log=False)
                    expected = ((1,90+variable,0),(2,20+variable,0))
                    assert rows == (tuple(reversed(expected)) if reverse else expected), rows
            a = pool.submit(scan_many, first, 17, False)
            b = pool.submit(scan_many, second, 29, True)
            a.result(timeout=20); b.result(timeout=20)
        self.record("same_worker_concurrent_sql_and_scans", fast_seconds=elapsed, interleaved_queries=16)

    def run_slow_client(self, healthy):
        slow = self.worker_connect(self.b)
        pid = self.worker_pid(self.b)
        sid = slow.thread_id()
        def session_row():
            rows = self.sql(f"SELECT COMMAND,INFO FROM information_schema.processlist WHERE ID={sid}",
                            healthy, log=False)
            return rows[0] if rows else None
        try:
            # Read no response bytes. The result exceeds the TCP buffers; the
            # native writer stalls only this connection's stream.
            slow._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            slow._execute_command(3, "SELECT id,REPEAT('x',65536) FROM t1 ORDER BY id")
            time.sleep(1)
            peeked = slow._sock.recv(4 * 65536, socket.MSG_PEEK | socket.MSG_DONTWAIT)
            assert 0 < len(peeked) < 98 * 65536, len(peeked)
            assert session_row() is not None
            start = time.monotonic()
            assert self.sql("SELECT SUM(v) FROM t1", healthy) == ((48590,),)
            elapsed = time.monotonic()-start
            assert elapsed < 2, elapsed
            assert session_row() is not None, "stalled stream must not kill the session"
            slow._sock.shutdown(socket.SHUT_RDWR)
            slow._force_close()
            self.wait_until(lambda: session_row() is None, "slow disconnected session not reclaimed")
            assert self.worker_pid(self.b) == pid
            assert self.sql("SELECT 1", healthy) == ((1,),)
            self.record("slow_tcp_reader_does_not_block_other_session", fast_seconds=elapsed,
                        result_bytes_at_least=98*65536, worker_survived=True)
        finally:
            slow.close()

    def run_timeouts(self, first, second):
        pid = self.worker_pid(self.b)
        def expect_timeout(future):
            try:
                future.result(timeout=5)
            except pymysql.MySQLError as error:
                assert error.args[0] == 4012, error.args
                return error.args
            raise AssertionError("query exceeded its deadline without an error")

        # A real interval longer than the removed IPC limit. B must progress
        # while A sends no rows for 31 seconds, and A must then succeed.
        self.sql("SET ob_query_timeout=40000000", first)
        started = time.monotonic()
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(self.sql, "SELECT SLEEP(31),@x,v FROM t1 WHERE id=1", first)
            time.sleep(.3)
            assert self.sql("SELECT @x,v FROM t1 WHERE id=1", second) == ((29,90),)
            assert not pending.done()
            assert pending.result(timeout=38) == ((0,17,90),)
        self.record("query_exceeds_old_30_second_ipc_limit", seconds=time.monotonic()-started, pid=pid)

        self.sql("SET ob_query_timeout=500000", first)
        scan_log_offset = len(self.engine_log())
        for attempt in range(5):
            started = time.monotonic()
            with ThreadPoolExecutor(max_workers=1) as pool:
                pending = pool.submit(self.sql, "SELECT SLEEP(10)+v FROM t1 WHERE id=1", first)
                assert self.sql("SELECT @x,v FROM t1 WHERE id=1", second) == ((29,90),)
                error = expect_timeout(pending)
            elapsed = time.monotonic()-started
            assert .3 < elapsed < 2, elapsed
            assert self.sql("SELECT @x,v FROM t1 WHERE id=1", first) == ((17,90),)
            assert self.worker_pid(self.b) == pid and Path(f"/proc/{pid}").exists()
            self.record("query_timeout_keeps_session_and_worker", attempt=attempt, seconds=elapsed, error=error)
        # Cancellation now lets native close-scan RPCs finish before D, and the
        # gateway clears any residual scans before dropping its snapshot pin.
        remaining = re.findall(rf"PROTOTYPE_V13_SCANS_RELEASED ns={self.b} remaining=(\d+)",
                               self.engine_log()[scan_log_offset:])
        assert len(remaining) >= 5 and all(count == "0" for count in remaining), remaining
        self.sql("SET ob_query_timeout=10000000", first)
        self.sql("SET ob_query_timeout=10000000", second)

    def run_slow_client_timeout(self, healthy):
        assert self.sql("SELECT REPEAT('x',65536)", log=False) == (("x"*65536,),)
        assert self.sql("SELECT REPEAT('x',65536)", healthy, log=False) == (("x"*65536,),)
        slow = self.worker_connect(self.b)
        pid = self.worker_pid(self.b)
        try:
            before = self.engine_log().count(f"PROTOTYPE_V13_SCANS_RELEASED ns={self.b} ")
            slow._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            started = time.monotonic()
            slow._execute_command(3, "SELECT id,REPEAT('x',65536) FROM t1 ORDER BY id")
            # A stalled reader does not abort a native query: the result is
            # buffered per connection and stays a valid packet stream.
            time.sleep(1)
            assert self.sql("SELECT 1", healthy) == ((1,),)
            slow._sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024*1024)
            slow._read_query_result(unbuffered=False)
            rows = tuple(slow._result.rows)
            assert len(rows) == 98 and all(row[1] == "x"*65536 for row in rows), len(rows)
            self.wait_until(lambda: self.engine_log().count(f"PROTOTYPE_V13_SCANS_RELEASED ns={self.b} ") > before,
                            "buffered result kept scans alive")
            elapsed = time.monotonic()-started
            assert self.sql("SELECT 1", slow) == ((1,),)
            assert self.worker_pid(self.b) == pid
            self.record("slow_client_result_buffered_and_connection_reusable", seconds=elapsed, pid=pid)
        finally:
            slow.close()

    def run_workers(self):
        self.setup_lineage()
        c, _ = self.capture("b", "c")
        bconn = cconn = reconnect = stale = None
        try:
            bconn, cconn = self.worker_connect(self.b), self.worker_connect(c)
            self.sql("UPDATE t1 SET v=90 WHERE id=1", bconn)
            bp, cp = self.worker_pid(self.b), self.worker_pid(c)
            assert bp != cp and bp != self.proc.pid and cp != self.proc.pid
            self.record("three_processes_one_public_port", port=self.port, engine=self.proc.pid, b=bp, c=cp)
            assert self.sql("SELECT 1 + 2 AS answer", bconn) == ((3,),)
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", bconn) == ((1,90),(2,20))
            stale = self.worker_connect(self.b)
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", cconn) == ((1,10),(2,20))
            assert self.sql("SELECT id,v+7 FROM t1 WHERE id>=2 ORDER BY id DESC", bconn) == ((2,27),)
            assert self.sql("SELECT SUM(v) FROM t1", bconn) == ((110,),)
            assert self.sql("SELECT id,v FROM t1 WHERE id=1", cconn) == ((1,10),)
            self.run_sessions(bconn, stale)
            self.run_concurrency(bconn, stale)
            self.run_timeouts(bconn, stale)
            # UPDATE IGNORE is native syntax: duplicate-key conflicts downgrade
            # to warnings instead of failing the statement.
            with bconn.cursor() as cursor:
                assert cursor.execute("UPDATE IGNORE t1 SET id=2 WHERE id=1") == 0
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", bconn) == ((1,90),(2,20))
            self.record("worker_update_ignore_downgrades_conflicts", native_syntax=True)
            for query in ("SELECT * FROM __fork_ns_3__db1.t1" if self.b != 3 else "SELECT * FROM __fork_ns_2__db1.t1",):
                try:
                    self.sql(query, bconn)
                except pymysql.MySQLError as error:
                    self.record("worker_rejected_unsupported_query", sql=query, error=error.args)
                else:
                    raise AssertionError(query)
            bconn.select_db("db2")
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", bconn) == ((1,10),(2,20))
            bconn.select_db("db1")
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", bconn) == ((1,90),(2,20))
            scan_log = self.base / "log" / "seekdb.log"
            before = scan_log.stat().st_size
            def scan_opened():
                with scan_log.open("rb") as log:
                    log.seek(before)
                    return f"PROTOTYPE_V10_SCAN_OPEN ns={self.b} ".encode() in log.read()
            victim = self.worker_connect(self.b)
            try:
                with ThreadPoolExecutor(max_workers=2) as pool:
                    pending = pool.submit(self.sql, "SELECT SLEEP(15)+v FROM t1 WHERE id=1", bconn)
                    self.wait_until(scan_opened, "remote storage scan did not open")
                    assert self.sql("SELECT 1", stale) == ((1,),)
                    assert not pending.done()
                    second = pool.submit(self.sql, "SELECT SLEEP(15)+v FROM t1 WHERE id=2", victim)
                    def both_opened():
                        with scan_log.open("rb") as log:
                            log.seek(before)
                            return log.read().count(f"PROTOTYPE_V10_SCAN_OPEN ns={self.b} ".encode()) >= 2
                    self.wait_until(both_opened, "second remote scan did not open")
                    os.kill(bp, signal.SIGKILL)
                    assert self.sql("SELECT id,v FROM t1 ORDER BY id", cconn) == ((1,10),(2,20))
                    for future in (pending, second):
                        try:
                            future.result(timeout=10)
                        except pymysql.MySQLError as error:
                            self.record("worker_death_failed_own_query", namespace=self.b, pid=bp, error=error.args)
                        else:
                            raise AssertionError("killed worker query succeeded")
                    self.record("worker_death_wakes_all_inflight_requests", requests=2)
            finally:
                victim.close()
            reconnect = self.worker_connect(self.b)
            assert self.worker_pid(self.b) != bp
            assert self.sql("SELECT id,v FROM t1 ORDER BY id", reconnect) == ((1,90),(2,20))
            assert self.sql("SELECT @x,@label", reconnect) == ((None,None),)
            try:
                self.sql("SELECT 1", stale)
            except pymysql.MySQLError as error:
                self.record("old_activation_connection_rejected", error=error.args)
            else:
                raise AssertionError("old connection entered the new worker")
            extra = [(i, i * 10) for i in range(3, 99)]
            self.sql("INSERT INTO t1 VALUES" +
                     ",".join(f"({i},{v})" for i,v in extra), reconnect)
            assert self.sql("SELECT id,v FROM t1 WHERE id>=3 ORDER BY id", reconnect) == tuple(extra)
            assert self.sql("SELECT id,v+1 FROM t1 WHERE id>=3 AND MOD(v,30)=0 ORDER BY v DESC LIMIT 4", reconnect) == tuple(
                (i,v+1) for i,v in reversed(extra) if v % 30 == 0)[:4]
            assert self.sql("SELECT id,v FROM t1 WHERE id>=3 AND MOD(v,30)=0 ORDER BY id LIMIT 4 OFFSET 2", reconnect) == tuple(
                (i,v) for i,v in extra if v % 30 == 0)[2:6]
            assert self.sql("SELECT id FROM t1 WHERE v<0", reconnect) == ()
            self.record("bounded_scan_batches_and_worker_filter_sort", rows=len(extra), batch_limit=32)
            self.run_slow_client(reconnect)
            self.run_slow_client_timeout(reconnect)
            assert self.root("b") and self.root("c")
            for pid in (self.worker_pid(self.b), cp):
                status = Path(f"/proc/{pid}/status").read_text()
                memory = Path(f"/proc/{pid}/smaps_rollup").read_text()
                targets = []
                for fd in Path(f"/proc/{pid}/fd").iterdir():
                    try:
                        targets.append(os.readlink(fd))
                    except FileNotFoundError:
                        pass
                # Workers intentionally hold one Unix listener (their client
                # endpoint). Any other socket or engine storage fd is a leak.
                unix_listener_inodes = set()
                for line in Path(f"/proc/{pid}/net/unix").read_text().splitlines()[1:]:
                    fields = line.split()
                    if len(fields) >= 8 and fields[-1].endswith("sql.sock"):
                        unix_listener_inodes.add(fields[6])
                assert not any(
                    str(self.base / "store") in target
                    or (target.startswith("socket:")
                        and target[len("socket:["):-1] not in unix_listener_inodes)
                    for target in targets), targets
                self.record("worker_resources", pid=pid,
                            status=[line for line in status.splitlines() if line.startswith(("VmRSS:", "Threads:"))],
                            memory=[line for line in memory.splitlines() if line.startswith(("Pss:", "Private_Clean:", "Private_Dirty:"))],
                            no_engine_storage_or_network_fds=True)
                private_kib = sum(int(line.split()[1]) for line in memory.splitlines()
                                  if line.startswith(("Private_Clean:", "Private_Dirty:")))
                # Tiny warmed workload: catch cache sizing from host RAM instead of worker budget.
                assert private_kib < 64 * 1024, (pid, private_kib, "worker private memory exceeds 64 MiB")
            worker_dirs = list((self.base / "run").glob("namespace-worker-*"))
            assert worker_dirs and all(not (p / "store").exists() for p in worker_dirs)
            self.record("PASS", case="namespace_sql_worker_flow", workers_restarted=True,
                        actual_sql_pipeline=True, shared_storage=True, one_public_port=self.port)
        finally:
            for connection in (bconn, cconn, reconnect, stale):
                if connection is not None:
                    connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--case", choices=("full", "slow-timeout", "insert", "dml", "nested", "ddl", "index"), default="full")
    parser.add_argument("--in-process", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if args.in_process:
        if args.case != "full":
            parser.error("--in-process currently supports --case full")
        from namespace_inprocess_prototype import run_case
        run_case(args.binary, "sql")
        return
    case_name = {"insert": "insert_v14", "dml": "native_execution_v16", "nested": "nested_session_v17"}.get(args.case, "timeout_v13")
    experiment = WorkerExperiment(args.binary, case_name, prototype=6)
    try:
        experiment.start()
        if args.case == "index":
            experiment.run_indexes()
        elif args.case == "ddl":
            experiment.run_ddl()
        elif args.case == "nested":
            experiment.run_nested()
        elif args.case == "dml":
            experiment.run_dml()
        elif args.case == "insert":
            experiment.run_inserts()
        elif args.case == "slow-timeout":
            experiment.setup_lineage()
            experiment.sql("INSERT INTO " + experiment.table(experiment.b, "db1.t1") + " VALUES" +
                           ",".join(f"({i},{i*10})" for i in range(3,99)))
            conn = experiment.worker_connect(experiment.b)
            try:
                for _ in range(5):
                    experiment.run_slow_client_timeout(conn)
            finally:
                conn.close()
        else:
            experiment.run_workers()
    finally:
        experiment.close()


if __name__ == "__main__":
    main()
