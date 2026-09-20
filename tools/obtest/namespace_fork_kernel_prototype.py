#!/usr/bin/env python3
"""PROTOTYPE: catalog lookup is read-only; ordinary SQL triggers tablet creation in storage.

python3 tools/obtest/namespace_fork_kernel_prototype.py --binary build_release/src/observer/seekdb
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import subprocess
import struct
import threading
import time

import pymysql
from namespace_fork_prototype import Experiment


class KernelExperiment(Experiment):
    def start(self):
        super().start()
        if os.environ.get("SEEKDB_NAMESPACE_SQL_WORKER_PROTOTYPE") != "1":
            self.sql("SET ob_global_debug_sync='reset'")
        self.sql("CREATE DATABASE IF NOT EXISTS __fork_proto_meta")
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.pages("
                 "id BIGINT UNSIGNED PRIMARY KEY,payload VARBINARY(60000))")
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.roots("
                 "database_id BIGINT UNSIGNED PRIMARY KEY,source_id BIGINT UNSIGNED,"
                 "catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,directory_page BIGINT UNSIGNED,"
                 "directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT)")

    def root(self, database):
        rows = self.sql("SELECT r.* FROM __fork_proto_meta.roots r JOIN oceanbase.__all_database d "
                        "ON r.database_id=d.database_id WHERE d.database_name='" + database + "'")
        assert len(rows) == 1, rows
        return rows[0]

    def physical(self):
        # This virtual table iterates the local tablet map directly, without async reporting lag.
        return self.sql("SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info "
                        "WHERE tablet_id>=4611686018427387904 ORDER BY tablet_id")

    def restart(self):
        self.connection.close()
        self.connection = None
        # Crash recovery is intentional: directory roots and CREATE_TABLET MDS must agree.
        self.proc.kill()
        self.proc.wait(timeout=15)
        self.record("crash_for_recovery", pid=self.proc.pid)
        self.proc = None
        self.start()
        self.record("restarted_from_existing_data")

    def page_dump(self):
        return {str(row[0]): row[1] for row in self.sql(
            "SELECT id,HEX(payload) FROM __fork_proto_meta.pages ORDER BY id", log=False)}

    def inspect_tree(self, pages, page, cap):
        entries, depths, visited = {}, set(), set()
        def walk(page, cap, lower, upper, depth):
            assert page not in visited, ("cycle or duplicate child", page)
            visited.add(page)
            payload = bytes.fromhex(pages[str(page)])
            pos = 0
            def number():
                nonlocal pos
                n, = struct.unpack_from("<Q", payload, pos)
                pos += 8
                return n
            def blob():
                nonlocal pos
                n = number()
                out = payload[pos:pos + n]
                assert len(out) == n
                pos += n
                return out
            def combine(a, b):
                return min(a, b) if a and b else a or b
            version, leaf, size = number(), number(), number()
            assert version == 1 and leaf in (0, 1) and 0 < size <= 8
            keys = [blob().decode() for _ in range(size)]
            assert keys == sorted(set(keys)), keys
            assert all((lower is None or lower <= k) and (upper is None or k < upper) for k in keys)
            if leaf:
                depths.add(depth)
                for key in keys:
                    value, limit = blob(), number()
                    entries[key] = (value, combine(cap, limit))
            else:
                for i in range(size + 1):
                    child, limit = number(), number()
                    walk(child, combine(cap, limit), lower if i == 0 else keys[i - 1],
                         upper if i == size else keys[i], depth + 1)
            assert pos == len(payload), (pos, len(payload))
        walk(page, cap, None, None, 1)
        assert len(depths) == 1, depths
        return entries, depths.pop(), visited

    def run_kernel(self, count):
        source, target = "__fork_proto_a_kernel", "__fork_proto_b_kernel"
        self.sql("CREATE DATABASE " + source)
        for n in range(1, count + 1):
            self.sql("CREATE TABLE " + source + ".t" + str(n) + "(id INT PRIMARY KEY,v INT)", log=False)
            self.sql("INSERT INTO " + source + ".t" + str(n) + " VALUES(1,10),(2,20)", log=False)
        original = self.root(source)
        pages = int(self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages")[0][0])
        tx = self.connect()
        try:
            self.sql("BEGIN", tx)
            self.sql("UPDATE " + source + ".t1 SET v=11 WHERE id=1", tx)
            self.sql("UPDATE " + source + ".t2 SET v=22 WHERE id=2", tx)
            self.sql("FORK DATABASE " + source + " TO " + target)
            captured = self.root(target)
            assert captured[2] == original[2] and captured[4] == original[4], (original, captured)
            snapshot = captured[6]
            assert captured[3] == captured[5] == snapshot > 0, captured
            assert int(self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages")[0][0]) == pages
            assert self.physical() == ()
            assert self.user_tables(target) == ()
            assert self.tasks() == ()
            self.sql("COMMIT", tx)
        finally:
            tx.close()
        self.record("capture_shared_existing_roots", root=captured, pages=pages, target_tablets=0)
        captured_pages = self.page_dump()

        # Catalog and plan preparation alone must leave all target physical tablets absent.
        listed = self.sql("SHOW TABLES FROM " + target)
        assert {row[0] for row in listed} == {"t" + str(n) for n in range(1, count + 1)}, listed
        self.sql("SHOW CREATE TABLE " + target + ".t1")
        self.sql("EXPLAIN SELECT * FROM " + target + ".t1")
        self.sql("PREPARE probe FROM 'SELECT id,v FROM " + target + ".t1 ORDER BY id'")
        assert self.physical() == ()
        assert self.sql("EXECUTE probe") == ((1, 10), (2, 20))
        self.sql("DEALLOCATE PREPARE probe")
        assert len(self.physical()) == 1
        changed = self.root(target)
        assert changed[4] != captured[4] and changed[5] == 0, (captured, changed)
        assert self.root(source) == original
        self.record("catalog_did_not_materialize_storage_did")

        self.sql("INSERT INTO " + target + ".t3 VALUES(3,30)")
        self.expect(target + ".t3", [(1, 10), (2, 20), (3, 30)])
        self.expect(source + ".t3", [(1, 10), (2, 20)])

        self.sql("BEGIN")
        self.sql("UPDATE " + target + ".t4 SET v=40 WHERE id=1")
        self.sql("UPDATE " + target + ".t5 SET v=50 WHERE id=1")
        self.sql("ROLLBACK")
        self.expect(target + ".t4", [(1, 10), (2, 20)])
        self.expect(target + ".t5", [(1, 10), (2, 20)])
        self.record("internal_materialization_did_not_commit_business_transaction")

        barrier = threading.Barrier(8)
        def first_access(_):
            con = self.connect()
            try:
                barrier.wait(timeout=30)
                with con.cursor() as cur:
                    cur.execute("SELECT id,v FROM " + target + ".t6 ORDER BY id")
                    return cur.fetchall()
            finally:
                con.close()
        before = len(self.physical())
        with ThreadPoolExecutor(max_workers=8) as pool:
            assert all(rows == ((1, 10), (2, 20)) for rows in pool.map(first_access, range(8)))
        assert len(self.physical()) == before + 1
        self.record("concurrent_first_access_created_one_tablet", clients=8)

        self.sql("UPDATE " + source + ".t2 SET v=99 WHERE id=1")
        self.sql("DELETE FROM " + source + ".t2 WHERE id=2")
        self.sql("INSERT INTO " + source + ".t2 VALUES(3,30)")

        # GC renewal is demand-driven. Restart exercises its normal primary catchup;
        # waiting on a quiet instance alone need not advance the watermark at all.
        before_restart = self.root(target)
        tablets = self.physical()
        self.restart()
        assert self.root(target) == before_restart
        assert self.physical() == tablets
        self.expect(target + ".t3", [(1, 10), (2, 20), (3, 30)])
        self.expect(target + ".t7", [(1, 10), (2, 20)])
        assert len(self.physical()) == len(tablets) + 1
        self.record("recovered_roots_and_materialized_and_unopened_tables")

        deadline = time.monotonic() + 150
        while time.monotonic() < deadline:
            gc = int(self.sql("SELECT column_value FROM oceanbase.__all_core_table "
                              "WHERE table_name='__all_global_stat' AND column_name='snapshot_gc_scn'", log=False)[0][0])
            if gc > snapshot:
                break
            time.sleep(1)
        else:
            raise AssertionError("GC watermark did not pass captured S")
        self.expect(target + ".t2", [(1, 10), (2, 20)])
        self.record("untouched_leaf_kept_snapshot_cap_after_cow", snapshot=snapshot, gc=gc)

        # Traverse a different leaf/subtree after COW and source changes.
        self.sql("UPDATE " + source + ".t" + str(count) + " SET v=999 WHERE id=1")
        self.expect(target + ".t" + str(count), [(1, 10), (2, 20)])

        self.sql("CREATE TABLE " + source + ".late(id INT PRIMARY KEY,v INT)")
        assert self.root(source)[2] != original[2]
        try:
            self.sql("SELECT * FROM " + target + ".late")
        except pymysql.MySQLError as error:
            assert error.args[0] == 1146, error.args
        else:
            raise AssertionError("source late table leaked into captured catalog")

        for statement in ("ALTER TABLE " + source + ".t1 ADD COLUMN x INT",
                          "DROP TABLE " + source + ".t1",
                          "DROP TABLE " + target + ".t1",
                          "DROP TABLE IF EXISTS " + target + ".t1",
                          "CREATE TABLE " + target + ".new_table(id INT PRIMARY KEY,v INT)"):
            before_ddl = self.root(target)
            try:
                self.sql(statement)
            except pymysql.MySQLError as error:
                assert error.args[0] == 1235, (statement, error.args)
                self.record("unsupported_ddl_rejected", sql=statement, error=error.args)
            else:
                raise AssertionError(("unsupported DDL succeeded", statement))
            assert self.root(target) == before_ddl
            self.expect(target + ".t1", [(1, 10), (2, 20)])

        assert self.tasks() == ()
        final_pages = self.page_dump()
        assert all(final_pages[page] == content for page, content in captured_pages.items())
        final_root = self.root(target)
        catalog, catalog_height, catalog_nodes = self.inspect_tree(final_pages, captured[2], captured[3])
        original_dir, _, original_nodes = self.inspect_tree(final_pages, captured[4], captured[5])
        current_dir, directory_height, current_nodes = self.inspect_tree(final_pages, final_root[4], final_root[5])
        assert {key for key in catalog if not key.startswith('#')} == {"t" + str(n) for n in range(1, count + 1)}
        assert len(original_dir) == len(current_dir) == count
        assert all(cap == snapshot for _, cap in original_dir.values())
        bound = 0
        for value, cap in current_dir.values():
            _, _, _, physical = struct.unpack("<QQQQ", value)
            assert cap == (0 if physical else snapshot), (physical, cap, snapshot)
            bound += bool(physical)
        assert bound == len(self.physical())
        if count >= 40:
            assert catalog_height >= 3, catalog_height
        assert original_nodes - current_nodes and original_nodes & current_nodes
        dump = self.base / "directory_snapshot.json"
        dump.write_text(json.dumps(dict(captured_root=captured, final_root=final_root, pages=final_pages)))
        self.record("persistent_btree_checked", catalog_height=catalog_height,
                    directory_height=directory_height, inherited_entries=count - bound,
                    materialized_entries=bound, shared_directory_pages=len(original_nodes & current_nodes),
                    immutable_old_pages=len(captured_pages), evidence=dump)
        trace = self.engine_log()
        materializations = [line for line in trace.splitlines() if "PROTOTYPE_V2_STORAGE_MATERIALIZE" in line]
        assert len(materializations) == len(self.physical()), materializations
        assert "fork table freeze stage done" not in trace
        self.record("PASS", case="kernel_catalog_cow_storage_materialization", source_tables=count,
                    materializations=len(materializations), restart=True,
                    scope="fixed schemas; source retained; engine-table metadata pages; no online GC or baseline handoff")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--tables", type=int, default=40)
    args = parser.parse_args()
    if args.tables < 7:
        parser.error("--tables must be >=7")
    exp = KernelExperiment(args.binary, "kernel_v2", prototype=2)
    try:
        exp.start()
        exp.run_kernel(args.tables)
    except BaseException as error:
        exp.record("FAIL", error=repr(error), base=exp.base)
        raise
    finally:
        exp.close()


if __name__ == "__main__":
    main()
