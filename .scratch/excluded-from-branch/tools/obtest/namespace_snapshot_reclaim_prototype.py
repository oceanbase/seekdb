#!/usr/bin/env python3
"""PROTOTYPE V7: release the last snapshot owner and reclaim deleted tablets.

python3 tools/obtest/namespace_snapshot_reclaim_prototype.py --binary build_release/src/observer/seekdb
Real reader/writer draining, private tablet DELETE MDS, atomic pin release and crash retry.
Metadata B+ tree pages deliberately remain; empty shells prove storage lifetime release, not disk shrink.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import time
import re

import pymysql
from namespace_snapshot_lifecycle_prototype import LifecycleExperiment


class ReclaimExperiment(LifecycleExperiment):
    def physical(self):
        # Deleted empty shells deliberately remain in the physical tablet map.
        return self.sql("SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info "
                        "WHERE tablet_id>=4611686018427387904 AND tablet_status=1 "
                        "AND is_committed=1 ORDER BY tablet_id", log=False)

    def state(self, name="a"):
        return self.sql("SELECT state FROM __fork_proto_meta.namespaces WHERE name=UNHEX('" +
                        name.encode().hex() + "')", log=False)[0][0]

    def snapshots(self):
        return self.sql("SELECT * FROM __fork_proto_meta.snapshots ORDER BY snapshot_id", log=False)

    def pins(self):
        return self.sql("SELECT snapshot_scn,schema_version,tablet_id FROM oceanbase.__all_acquired_snapshot "
                        "WHERE snapshot_type=2 AND tablet_id=0 ORDER BY snapshot_scn", log=False)

    def refs(self):
        return self.sql("SELECT name,state,snapshot_ref FROM __fork_proto_meta.namespaces "
                        "ORDER BY namespace_id", log=False)

    def drop(self, name):
        con = self.connect()
        try:
            return self.sql("FORK DATABASE " + name + " TO __drop__", con)
        finally:
            con.close()

    def wait_empty(self, ids):
        # Native GC may remove an empty shell from the tablet map on its next pass.
        # Keep observed evidence across restarts, and never accept a reappearing live tablet.
        if not hasattr(self, "reclaimed"):
            self.reclaimed = set()
        def empty():
            rows = {r[0]: r for r in self.source_storage(ids)}
            missing = set(ids) - set(rows) - self.reclaimed
            if missing:
                removed = {int(i) for i in re.findall(
                    r"succeeded to remove tablet\(ret=0, tablet_id=\{id:(\d+)\}", self.engine_log())}
                self.reclaimed.update(missing & removed)
            for tablet, row in rows.items():
                if row[1:] != (3,1,1):
                    return False
                self.reclaimed.add(tablet)
            return set(ids) <= self.reclaimed
        self.wait_until(empty, "deleted tablets did not become empty shells or complete native GC", seconds=60)
        assert self.sql("SELECT tablet_id FROM oceanbase.__all_tablet_to_table WHERE tablet_id IN (" +
                        ",".join(map(str,ids)) + ")", log=False) == ()
        self.record("tablets_reclaimed_to_empty_shells_or_removed", storage=self.source_storage(ids),
                    verified_tablets=sorted(self.reclaimed), refs=self.refs(), snapshots=self.snapshots(), pins=self.pins())

    def setup_source(self):
        for db in ("db1", "db2"):
            self.sql("CREATE DATABASE " + db)
            self.sql("CREATE TABLE " + db + ".t(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO " + db + ".t VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE __empty__ TO a")
        self.source_ids = [self.user_tables(db)[0][2] for db in ("db1", "db2")]
        self.sql("FORK DATABASE a TO b")
        self.b = self.root("b")[0]
        self.sb = self.root("b")[6]

    def run_reclaim(self):
        self.setup_source()
        self.sql("UPDATE db2.t SET v=11 WHERE id=1")
        self.sql("FORK DATABASE a TO c")
        c, sc = self.root("c")[0], self.root("c")[6]
        self.sql("UPDATE db2.t SET v=12 WHERE id=1")
        assert len(self.snapshots()) == len(self.pins()) == 2 and sc > self.sb
        self.sql("UPDATE " + self.address(self.b,"db1","t") + " SET v=90 WHERE id=1")
        b_ids = [r[0] for r in self.physical()]
        assert len(b_ids) == 1  # B.db2 and all of C are cold.
        self.drop("a")
        assert self.state() == 2
        self.wait_until(lambda: self.engine_log().count("PROTOTYPE_V6_RETAIN_SNAPSHOT_TABLET") >= 2,
                        "source inputs were not retained")
        assert self.source_storage(self.source_ids) == tuple((i,3,1,0) for i in sorted(self.source_ids))

        old, writer = self.connect(), self.connect()
        try:
            self.sql("USE " + self.address(self.b,"db1"), old)
            self.sql("SELECT id,v FROM t ORDER BY id", old)
            self.sql("PREPARE cached_branch FROM 'SELECT id,v FROM t ORDER BY id'", old)
            self.sql("EXECUTE cached_branch", old)
            self.sql("BEGIN", writer)
            self.sql("UPDATE " + self.address(self.b,"db1","t") + " SET v=777 WHERE id=1", writer)
            with ThreadPoolExecutor(max_workers=1) as pool:
                pending = pool.submit(self.drop, "b")
                try:
                    self.wait_until(lambda: self.state("b") == 1, "B did not close")
                    self.denied("SELECT id,v FROM t ORDER BY id", old)
                    self.denied("EXECUTE cached_branch", old)
                    self.denied("UPDATE " + self.address(self.b,"db1","t") + " SET v=999 WHERE id=1")
                    time.sleep(.3)
                    if pending.done():
                        pending.result()
                        raise AssertionError("B DROP did not drain its existing writer")
                    assert len(self.snapshots()) == len(self.pins()) == 2
                    self.record("branch_drop_waited_for_writer", refs=self.refs(), pins=self.pins())
                finally:
                    self.sql("COMMIT", writer)
                pending.result(timeout=40)
            assert self.state("b") == 2
            self.denied("SELECT id,v FROM t ORDER BY id", old)
            self.denied("EXECUTE cached_branch", old)
        finally:
            writer.close()
            old.close()
        assert [r[0] for r in self.snapshots()] == [sc]
        assert [r[0] for r in self.pins()] == [sc]
        self.wait_empty(b_ids)
        assert self.source_storage(self.source_ids) == tuple((i,3,1,0) for i in sorted(self.source_ids))
        assert len(self.physical()) == 0  # No live B tablets; C still has no tablets.
        self.drop("b")
        self.flush(self.source_ids)
        self.restart()
        assert self.state("a") == self.state("b") == 2 and self.state("c") == 0
        assert [r[0] for r in self.pins()] == [sc]
        assert len(self.physical()) == 0
        self.sql("SET ob_global_debug_sync='FORK_TABLE_BUILD_DATA signal v7_baseline_ready "
                 "wait_for v7_baseline_release timeout 60000000 execute 1'")
        self.expect(self.address(c,"db2","t"), [(1,11),(2,20)])
        self.sql("SET ob_global_debug_sync='now wait_for v7_baseline_ready timeout 10000000'")
        self.wait_until(lambda: "name=v7_baseline_release" in self.engine_log(), "baseline DAG never paused")
        c_ids = [r[0] for r in self.physical()]
        assert len(c_ids) == 1  # C.db1 is never materialized, even for final deletion.
        self.record("last_sibling_cold_read_survived_drop_flush_restart", snapshot=sc,
                    source_storage=self.source_storage(self.source_ids), targets=c_ids)

        # Pause after admission, BEFORE the scan fetches inherited source inputs.
        self.sql("SET ob_global_debug_sync='AFTER_TABLE_SCAN signal v7_read_ready "
                 "wait_for v7_read_release timeout 60000000 execute 1'")
        def read():
            con = self.connect()
            try:
                return self.sql("SELECT id,v FROM " + self.address(c,"db2","t") + " ORDER BY id", con)
            finally:
                con.close()
        with ThreadPoolExecutor(max_workers=2) as pool:
            reader = pool.submit(read)
            self.sql("SET ob_global_debug_sync='now wait_for v7_read_ready timeout 10000000'")
            self.wait_until(lambda: "name=v7_read_release" in self.engine_log(), "reader never reached storage pause")
            pending = pool.submit(self.drop, "c")
            try:
                self.wait_until(lambda: self.state("c") == 1, "C did not close")
                self.denied("SELECT * FROM " + self.address(c,"db2","t"))
                time.sleep(.3)
                if pending.done():
                    pending.result()
                    raise AssertionError("last DROP passed an admitted reader")
                assert [r[0] for r in self.pins()] == [sc]
                assert self.source_storage(self.source_ids) == tuple((i,3,1,0) for i in sorted(self.source_ids))
                self.record("last_drop_waited_for_reader_before_source_fetch", refs=self.refs(), pins=self.pins())
                self.sql("SET ob_global_debug_sync='now signal v7_read_release'")
                assert reader.result(timeout=15) == ((1,11),(2,20))
                time.sleep(.3)
                if pending.done():
                    pending.result()
                    raise AssertionError("last DROP passed an admitted baseline DAG")
                assert [r[0] for r in self.pins()] == [sc]
                self.record("last_drop_waited_for_baseline_after_reader_finished", pins=self.pins())
            finally:
                self.sql("SET ob_global_debug_sync='now signal v7_read_release'")
                self.sql("SET ob_global_debug_sync='now signal v7_baseline_release'")
            pending.result(timeout=40)
        assert self.snapshots() == self.pins() == ()
        assert all(r[1:] == (2,0) for r in self.refs())
        all_ids = self.source_ids + b_ids + c_ids
        self.wait_empty(all_ids)
        pages = self.page_dump()
        assert pages  # Metadata page GC is a separate lifetime problem.
        self.restart()
        assert self.snapshots() == self.pins() == self.physical() == ()
        assert all(r[1:] == (2,0) for r in self.refs())
        self.wait_empty(all_ids)
        self.denied("SELECT * FROM " + self.address(c,"db2","t"))
        self.drop("c")
        assert self.page_dump() == pages
        self.record("PASS", case="last_namespace_snapshot_reclamation", reader_drained=True,
                    writer_drained=True, baseline_drained=True, cold_sibling_survived=True, empty_shells=all_ids,
                    restart=True, metadata_pages_retained=len(pages))

    def run_release_crash(self):
        self.setup_source()
        # An entirely cold branch has nothing to physically create or delete.
        before_snapshot, before_pins = self.snapshots(), self.pins()
        self.sql("FORK DATABASE a TO untouched")
        self.drop("untouched")
        assert self.state("untouched") == 2 and self.physical() == ()
        assert self.snapshots() == before_snapshot and self.pins() == before_pins
        self.expect("db1.t", [(1,10),(2,20)])
        self.record("unopened_branch_deleted_without_materialization", refs=self.refs())
        self.sql("UPDATE " + self.address(self.b,"db1","t") + " SET v=90 WHERE id=1")
        b_ids = [r[0] for r in self.physical()]
        self.drop("a")
        before, snapshot, pins = self.root("b"), self.snapshots(), self.pins()
        self.sql("SET ob_global_debug_sync='AFTER_UPDATE_TABLET_TO_LS signal v7_drop_ready "
                 "wait_for v7_drop_release timeout 60000000 execute 1'")
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(self.drop, "b")
            self.sql("SET ob_global_debug_sync='now wait_for v7_drop_ready timeout 10000000'")
            self.wait_until(lambda: "name=v7_drop_release" in self.engine_log(), "release never paused before commit")
            assert self.state("b") == 1 and self.root("b") == before
            assert self.snapshots() == snapshot and self.pins() == pins
            assert all(r[1:3] == (3,0) for r in self.source_storage(b_ids))
            self.record("private_delete_and_pin_release_uncommitted", storage=self.source_storage(b_ids),
                        refs=self.refs(), snapshots=snapshot, pins=pins)
            self.restart()
            try:
                pending.result(timeout=10)
            except pymysql.MySQLError as error:
                assert error.args[0] in (2006,2013), error.args
            else:
                raise AssertionError("uncommitted release unexpectedly succeeded")
        assert self.state("b") == 1 and self.root("b") == before
        assert self.snapshots() == snapshot and self.pins() == pins
        assert len(self.physical()) == 1
        assert all(r[1:] == (1,1,0) for r in self.source_storage(b_ids))
        assert self.source_storage(self.source_ids) == tuple((i,3,1,0) for i in sorted(self.source_ids))
        self.denied("SELECT * FROM " + self.address(self.b,"db1","t"))
        self.drop("b")
        assert self.snapshots() == self.pins() == self.physical() == ()
        self.wait_empty(self.source_ids + b_ids)
        self.restart()
        assert self.state("b") == 2 and self.snapshots() == self.pins() == ()
        self.wait_empty(self.source_ids + b_ids)
        self.record("PASS", case="snapshot_release_precommit_crash_retry", rollback_preserved_pin=True,
                    private_delete_rolled_back=True, retry_reclaimed=True, unopened_table_not_created=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--case", choices=("reclaim","crash","all"), default="all")
    args = parser.parse_args()
    for case in (("reclaim","crash") if args.case == "all" else (args.case,)):
        exp = ReclaimExperiment(args.binary, "reclaim_v7_" + case, prototype=4)
        try:
            exp.start()
            exp.run_reclaim() if case == "reclaim" else exp.run_release_crash()
        except BaseException as error:
            exp.record("FAIL", error=repr(error), base=exp.base)
            raise
        finally:
            exp.close()


if __name__ == "__main__":
    main()
