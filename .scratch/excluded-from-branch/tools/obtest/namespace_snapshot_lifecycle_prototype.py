#!/usr/bin/env python3
"""PROTOTYPE: delete native namespace A while a persistent snapshot keeps B alive.

python3 tools/obtest/namespace_snapshot_lifecycle_prototype.py --binary build_release/src/observer/seekdb
Real schema DROP, transaction locks, GC, flush/merge and crash recovery. Fixed two-column tables.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import time

import pymysql
from namespace_identity_prototype import NamespaceExperiment
from namespace_fork_kernel_prototype import KernelExperiment
from namespace_fork_compaction_prototype import CompactionExperiment


class LifecycleExperiment(NamespaceExperiment, CompactionExperiment):
    def start(self):
        KernelExperiment.start(self)
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.namespaces("
                 "namespace_id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,name VARBINARY(128) UNIQUE,"
                 "source_id BIGINT UNSIGNED,catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,"
                 "directory_page BIGINT UNSIGNED,directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT,"
                 "snapshot_ref BIGINT UNSIGNED DEFAULT 0,state BIGINT DEFAULT 0)")
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.snapshots("
                 "snapshot_id BIGINT UNSIGNED PRIMARY KEY,catalog_page BIGINT UNSIGNED,"
                 "directory_page BIGINT UNSIGNED,snapshot BIGINT,schema_version BIGINT)")
        self.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")

    def state(self):
        return self.sql("SELECT state FROM __fork_proto_meta.namespaces WHERE namespace_id=1", log=False)[0][0]

    def wait_until(self, predicate, description, seconds=30):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if predicate():
                return
            time.sleep(.2)
        raise AssertionError(description)

    def denied(self, statement, con=None):
        try:
            self.sql(statement, con)
        except pymysql.MySQLError as error:
            assert error.args[0] in (1049,1146,4179,4008), error.args
            self.record("source_access_denied", sql=statement, error=error.args)
        else:
            raise AssertionError(("deleted source accepted operation", statement))

    def source_storage(self, ids):
        return self.sql("SELECT tablet_id,tablet_status,is_committed,is_empty_shell FROM "
                        "oceanbase.__all_virtual_tablet_info WHERE tablet_id IN (" +
                        ",".join(map(str,ids)) + ") ORDER BY tablet_id", log=False)

    def run_drop_crash(self):
        for db in ("db1", "db2"):
            self.sql("CREATE DATABASE " + db)
            self.sql("CREATE TABLE " + db + ".t(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO " + db + ".t VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE __empty__ TO a")
        self.sql("FORK DATABASE a TO b")
        b = self.root("b")[0]
        source_ids = [self.user_tables(db)[0][2] for db in ("db1", "db2")]
        source = self.root("a")
        normal = self.source_storage(source_ids)
        snapshot = self.sql("SELECT * FROM __fork_proto_meta.snapshots")
        self.sql("SET ob_global_debug_sync='AFTER_UPDATE_TABLET_TO_LS signal v6_drop_ready "
                 "wait_for v6_drop_release timeout 60000000 execute 1'")
        def drop():
            con = self.connect()
            try:
                return self.sql("FORK DATABASE a TO __drop__", con)
            finally:
                con.close()
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(drop)
            self.sql("SET ob_global_debug_sync='now wait_for v6_drop_ready timeout 10000000'")
            self.wait_until(lambda: "name=v6_drop_release" in self.engine_log(), "DROP never paused before commit")
            assert self.state() == 1 and self.root("a") == source
            assert self.physical() == ()
            storage = self.source_storage(source_ids)
            assert len(storage) == 2 and all(r[2] == 0 for r in storage), storage
            self.denied("SELECT * FROM db1.t")
            self.record("drop_paused_with_uncommitted_schema_and_tablet_deletions", storage=self.source_storage(source_ids))
            self.restart()
            try:
                pending.result(timeout=10)
            except pymysql.MySQLError as error:
                assert error.args[0] in (2006,2013), error.args
            else:
                raise AssertionError("uncommitted DROP unexpectedly succeeded")
        assert self.state() == 1 and self.root("a") == source
        assert self.source_storage(source_ids) == normal
        assert len(self.user_tables("db1")) == len(self.user_tables("db2")) == 1
        assert self.sql("SELECT * FROM __fork_proto_meta.snapshots") == snapshot
        self.denied("SELECT * FROM db1.t")
        self.denied("UPDATE db2.t SET v=999 WHERE id=1")
        self.sql("FORK DATABASE a TO __drop__")
        assert self.state() == 2 and self.physical() == ()
        self.expect(self.address(b,"db1","t"), [(1,10),(2,20)])
        self.expect(self.address(b,"db2","t"), [(1,10),(2,20)])
        self.record("PASS", case="namespace_drop_precommit_crash_retry", snapshot=snapshot,
                    source_namespace_stayed_closed=True, native_drop_rolled_back=True,
                    no_eager_materialization=True)

    def run_lifecycle(self):
        for db in ("db1", "db2"):
            self.sql("CREATE DATABASE " + db)
            self.sql("CREATE TABLE " + db + ".t(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO " + db + ".t VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE __empty__ TO a")
        source_ids = [self.user_tables(db)[0][2] for db in ("db1", "db2")]
        source_tables = [self.user_tables(db)[0][1] for db in ("db1", "db2")]
        old_pages = self.page_dump()
        self.sql("FORK DATABASE a TO b")
        b = self.root("b")[0]
        captured = self.root("b")
        assert self.page_dump() == old_pages and self.physical() == ()
        snapshots = self.sql("SELECT * FROM __fork_proto_meta.snapshots")
        assert len(snapshots) == 1 and snapshots[0][0] == captured[6]
        assert captured[1] == 0  # target no longer has a live source namespace dependency.
        self.expect(self.address(b,"db1","t"), [(1,10),(2,20)])
        self.sql("UPDATE " + self.address(b,"db1","t") + " SET v=90 WHERE id=1")
        assert len(self.physical()) == 1

        old, writer = self.connect(), self.connect()
        pool = ThreadPoolExecutor(max_workers=1)
        try:
            self.sql("USE db1", old)
            self.sql("SELECT id,v FROM t ORDER BY id", old)  # warm native plan/default database
            self.sql("PREPARE cached_source FROM 'SELECT id,v FROM db2.t ORDER BY id'", old)
            self.sql("EXECUTE cached_source", old)
            self.sql("BEGIN", writer)
            self.sql("UPDATE db1.t SET v=777 WHERE id=1", writer)
            def drop():
                con = self.connect()
                try:
                    return self.sql("FORK DATABASE a TO __drop__", con)
                finally:
                    con.close()
            pending = pool.submit(drop)
            self.wait_until(lambda: self.state() == 1, "namespace did not close")
            assert not pending.done(), "DROP did not wait for the active source writer"
            self.denied("SELECT id,v FROM t ORDER BY id", old)
            self.denied("EXECUTE cached_source", old)
            self.denied("UPDATE db2.t SET v=999 WHERE id=1")
            self.expect(self.address(b,"db1","t"), [(1,90),(2,20)])
            self.sql("COMMIT", writer)
            pending.result(timeout=45)
            assert self.state() == 2
            self.denied("SELECT id,v FROM t ORDER BY id", old)
            self.denied("EXECUTE cached_source", old)
        finally:
            writer.close()
            old.close()
            pool.shutdown(wait=True)

        assert self.sql("SELECT database_id FROM oceanbase.__all_database WHERE database_name IN ('db1','db2')") == ()
        assert self.sql("SELECT table_id FROM oceanbase.__all_table WHERE table_id IN (" + ",".join(map(str,source_tables)) + ")") == ()
        a = self.root("a")
        assert a[2:7] == (0,0,0,0,0), a
        assert self.sql("SELECT * FROM __fork_proto_meta.snapshots") == snapshots
        self.denied("SELECT * FROM __fork_ns_1__db1.t")
        self.denied("CREATE DATABASE after_delete")
        self.denied("FORK DATABASE a TO c")
        self.sql("FORK DATABASE a TO __drop__")  # idempotent completion
        self.wait_until(lambda: self.engine_log().count("PROTOTYPE_V6_RETAIN_SNAPSHOT_TABLET") >= 4,
                        "GC did not retain the deleted source inputs")
        assert self.source_storage(source_ids) == tuple((i,3,1,0) for i in sorted(source_ids))
        assert len(self.physical()) == 1  # db2 was never opened in B, including during DROP/GC.
        self.record("native_source_catalog_deleted_snapshot_retained", source_tablets=source_ids,
                    target_tablets=self.physical(), snapshot=snapshots)

        self.flush(source_ids)
        self.expect(self.address(b,"db1","t"), [(1,90),(2,20)])
        assert len(self.physical()) == 1
        before = self.root("b")
        self.restart()
        assert self.state() == 2 and self.root("b") == before
        assert self.source_storage(source_ids) == tuple((i,3,1,0) for i in sorted(source_ids))
        assert self.sql("SELECT * FROM __fork_proto_meta.snapshots") == snapshots
        self.denied("SELECT * FROM db1.t")
        self.denied("SELECT * FROM __fork_ns_1__db2.t")
        self.expect(self.address(b,"db1","t"), [(1,90),(2,20)])
        self.expect(self.address(b,"db2","t"), [(1,10),(2,20)])
        self.sql("INSERT INTO " + self.address(b,"db2","t") + " VALUES(3,30)")
        self.sql("DELETE FROM " + self.address(b,"db2","t") + " WHERE id=2")
        targets = [r[0] for r in self.physical()]
        self.wait_state("namespace_baselines_completed_after_source_drop", targets,
                        lambda h,t: all(any(r[0]==i and r[1]==10 for r in t) for i in targets))
        self.flush(targets)
        self.sql("ALTER SYSTEM MAJOR FREEZE")
        self.wait_state("namespace_major_completed_after_source_drop", targets,
                        lambda h,t: all(any(r[0]==i and r[1]=="MAJOR_MERGE" and r[2]>captured[6]
                                           for r in h) for i in targets))
        self.restart()
        self.expect(self.address(b,"db1","t"), [(1,90),(2,20)])
        self.expect(self.address(b,"db2","t"), [(1,10),(3,30)])
        assert self.state() == 2 and self.tasks() == ()
        final_pages = self.page_dump()
        assert all(final_pages[k]==v for k,v in old_pages.items())
        (self.base / "directory_snapshot.json").write_text(json.dumps(
            dict(snapshot=snapshots, source=self.root("a"), target=self.root("b"), pages=final_pages)))
        self.record("PASS", case="namespace_source_drop_independent_snapshot", snapshot=captured[6],
                    source_catalog_deleted=True, unopened_table_after_restart=True,
                    active_writer_drained=True, cached_source_access_denied=True, major_merge=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--case", choices=("lifecycle","crash","all"), default="all")
    args = parser.parse_args()
    for case in (("lifecycle","crash") if args.case == "all" else (args.case,)):
        exp = LifecycleExperiment(args.binary, "lifetime_v6_" + case, prototype=4)
        try:
            exp.start()
            exp.run_lifecycle() if case == "lifecycle" else exp.run_drop_crash()
        except BaseException as error:
            exp.record("FAIL", error=repr(error), base=exp.base)
            raise
        finally:
            exp.close()


if __name__ == "__main__":
    main()
