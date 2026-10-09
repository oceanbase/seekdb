#!/usr/bin/env python3
"""PROTOTYPE: persistent namespace roots spanning databases, explicit object addressing.

python3 tools/obtest/namespace_identity_prototype.py --binary build_release/src/observer/seekdb
The SQL spelling is test transport into the kernel. No SQL-worker isolation or source DROP.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import struct
import threading

import pymysql
from namespace_fork_kernel_prototype import KernelExperiment


class NamespaceExperiment(KernelExperiment):
    def start(self):
        super().start()
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.namespaces("
                 "namespace_id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,name VARBINARY(128) UNIQUE,"
                 "source_id BIGINT UNSIGNED,catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,"
                 "directory_page BIGINT UNSIGNED,directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT)")

    def root(self, name):
        rows = self.sql("SELECT namespace_id,source_id,catalog_page,catalog_cap,directory_page,"
                        "directory_cap,snapshot,schema_version FROM __fork_proto_meta.namespaces "
                        "WHERE name=UNHEX('" + name.encode().hex() + "')", log=False)
        assert len(rows) == 1, rows
        return rows[0]

    @staticmethod
    def address(ns, db, table=None):
        return "__fork_ns_" + str(ns) + "__" + db + ("." + table if table else "")

    def expect_error(self, statement, codes):
        try:
            self.sql(statement)
        except pymysql.MySQLError as error:
            assert error.args[0] in codes, (statement, error.args)
            self.record("expected_error", sql=statement, error=error.args)
        else:
            raise AssertionError(("unexpected success", statement))

    def run_namespace(self):
        # An existing registry with no enrolled namespace must allow ordinary DDL.
        self.sql("CREATE DATABASE unenrolled_db")
        self.sql("ALTER DATABASE unenrolled_db CHARACTER SET utf8mb4 COLLATE utf8mb4_bin")
        self.sql("DROP DATABASE unenrolled_db")
        # Enroll an existing database, then create others after namespace registration.
        self.sql("CREATE DATABASE db1")
        self.sql("CREATE TABLE db1.t1(id INT PRIMARY KEY,v INT)")
        self.sql("INSERT INTO db1.t1 VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE __empty__ TO a")
        a = self.root("a")[0]
        assert a == 1
        self.sql("CREATE DATABASE db2")
        self.sql("CREATE DATABASE empty_db")
        for table, value in (("t1", 100), ("t2", 200)):
            self.sql("CREATE TABLE db2." + table + "(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO db2." + table + " VALUES(1," + str(value) + "),(2,20)")
        source_tables = {db + "." + r[0]: (r[1], r[2]) for db in ("db1", "db2") for r in self.user_tables(db)}
        original = self.root("a")
        pages = self.page_dump()
        self.sql("FORK DATABASE a TO b")
        captured = self.root("b")
        b = captured[0]
        assert b != a and captured[1] == a and captured[6] > 0
        assert captured[2] == original[2] and captured[4] == original[4]
        assert captured[3] == captured[5] == captured[6]
        assert self.page_dump() == pages
        assert self.physical() == ()
        assert self.sql("SELECT database_name FROM oceanbase.__all_database WHERE database_name IN ('a','b')") == ()

        catalog, height, _ = self.inspect_tree(pages, captured[2], captured[3])
        assert {"Ddb1", "Ddb2", "Dempty_db"} <= set(catalog)
        assert len([k for k in catalog if k.startswith("#")]) == 3
        for db in ("db1", "db2", "empty_db"):
            expected = () if db == "empty_db" else (("t1",),) if db == "db1" else (("t1",), ("t2",))
            assert self.sql("SHOW TABLES FROM " + self.address(b, db)) == expected
        self.sql("EXPLAIN SELECT * FROM " + self.address(b, "db1", "t1"))
        assert self.physical() == ()
        self.record("namespace_fork_shared_all_database_roots", source_namespace=a, target_namespace=b,
                    captured_root=captured, tables=source_tables, pages=len(pages), catalog_height=height)

        self.expect(self.address(a, "db1", "t1"), [(1,10), (2,20)])
        self.sql("UPDATE db1.t1 SET v=11 WHERE id=1")
        self.sql("UPDATE db2.t1 SET v=101 WHERE id=1")
        self.sql("UPDATE db2.t2 SET v=201 WHERE id=1")
        self.expect(self.address(b, "db1", "t1"), [(1,10), (2,20)])
        self.sql("UPDATE " + self.address(b, "db1", "t1") + " SET v=90 WHERE id=1")
        self.sql("INSERT INTO " + self.address(b, "db1", "t1") + " VALUES(3,30)")

        # A sibling namespace resolves identical names/local IDs at its own later S.
        before = self.page_dump()
        self.sql("FORK DATABASE a TO c")
        c = self.root("c")[0]
        assert c not in (a, b) and self.page_dump() == before
        self.expect(self.address(c, "db1", "t1"), [(1,11), (2,20)])
        self.expect(self.address(b, "db1", "t1"), [(1,90), (2,20), (3,30)])
        self.expect(self.address(a, "db1", "t1"), [(1,11), (2,20)])

        blocker, other = self.connect(), self.connect()
        try:
            self.sql("BEGIN", blocker)
            self.sql("UPDATE " + self.address(b, "db1", "t1") + " SET v=999 WHERE id=1", blocker)
            self.sql("SET ob_query_timeout=2000000", other)
            self.sql("UPDATE " + self.address(c, "db1", "t1") + " SET v=77 WHERE id=1", other)
            self.sql("UPDATE db1.t1 SET v=12 WHERE id=1", other)
        finally:
            self.sql("ROLLBACK", blocker)
            blocker.close()
            other.close()
        self.expect(self.address(b, "db1", "t1"), [(1,90), (2,20), (3,30)])
        self.expect(self.address(c, "db1", "t1"), [(1,77), (2,20)])
        self.record("same_local_ids_have_independent_locks_and_cached_schemas", namespaces=[a,b,c])

        barrier = threading.Barrier(6)
        def first_access(_):
            con = self.connect()
            try:
                barrier.wait(timeout=10)
                return self.sql("SELECT id,v FROM " + self.address(b, "db2", "t1") + " ORDER BY id", con, log=False)
            finally:
                con.close()
        with ThreadPoolExecutor(max_workers=6) as pool:
            assert all(rows == ((1,100), (2,20)) for rows in pool.map(first_access, range(6)))
        self.expect(self.address(c, "db2", "t1"), [(1,101), (2,20)])
        assert len(self.physical()) == 4
        self.record("concurrent_materialization_across_databases", physical_tablets=self.physical())

        self.sql("CREATE DATABASE later_db")
        self.sql("CREATE TABLE later_db.t1(id INT PRIMARY KEY,v INT)")
        self.sql("INSERT INTO later_db.t1 VALUES(1,500)")
        self.expect(self.address(a, "later_db", "t1"), [(1,500)])
        self.expect_error("SELECT * FROM " + self.address(b, "later_db", "t1"), {1049,1146})
        self.expect_error("SELECT * FROM " + self.address(999, "db1", "t1"), {1049,1146})
        self.expect_error("FORK DATABASE a TO b", {1062})
        self.expect_error("DROP DATABASE db1", {1235})
        self.expect_error("ALTER DATABASE db1 CHARACTER SET utf8mb4 COLLATE utf8mb4_bin", {1235})
        self.expect_error("DROP TABLE " + self.address(b, "db1", "t1"), {1235})

        roots = {name: self.root(name) for name in ("a", "b", "c")}
        physical = self.physical()
        self.restart()
        assert {name: self.root(name) for name in roots} == roots
        assert self.physical() == physical
        self.expect(self.address(b, "db1", "t1"), [(1,90), (2,20), (3,30)])
        self.expect(self.address(c, "db1", "t1"), [(1,77), (2,20)])
        self.expect(self.address(b, "db2", "t2"), [(1,200), (2,20)])
        self.expect(self.address(c, "db2", "t2"), [(1,201), (2,20)])
        self.expect("db1.t1", [(1,12), (2,20)])
        self.expect("db2.t2", [(1,201), (2,20)])
        final_pages = self.page_dump()
        assert all(final_pages[k] == v for k, v in pages.items())
        for name, ns in (("b", b), ("c", c)):
            root = self.root(name)
            entries, _, _ = self.inspect_tree(final_pages, root[4], root[5])
            for value, cap in entries.values():
                _, local_table, local_tablet, physical_id = struct.unpack("<QQQQ", value)
                assert physical_id == (1 << 62) | (ns << 32) | local_tablet
                assert cap == 0 and (physical_id,) in self.physical()
                assert local_table in {ids[0] for ids in source_tables.values()}
        assert self.tasks() == ()
        (self.base / "directory_snapshot.json").write_text(json.dumps(
            dict(captured=captured, roots={name:self.root(name) for name in roots}, pages=final_pages)))
        self.record("PASS", case="namespace_identity_multidatabase", namespaces=[a,b,c],
                    databases=["db1","db2","empty_db"], shared_local_ids=True,
                    independent_locks=True, restart=True, new_physical_tablets=len(self.physical()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    exp = NamespaceExperiment(args.binary, "identity_v5", prototype=3)
    try:
        exp.start()
        exp.run_namespace()
    except BaseException as error:
        exp.record("FAIL", error=repr(error), base=exp.base)
        raise
    finally:
        exp.close()


if __name__ == "__main__":
    main()
