#!/usr/bin/env python3
"""PROTOTYPE V8: root-only A -> B -> C capture with mixed inherited snapshots.

python3 tools/obtest/namespace_lineage_prototype.py --binary build_release/src/observer/seekdb
Checks real multi-level B+ tree caps, hot/cold reads, deleted-parent baselines, pin graph and crash rollback.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import struct

import pymysql
from namespace_fork_kernel_prototype import KernelExperiment
from namespace_snapshot_reclaim_prototype import ReclaimExperiment


class LineageExperiment(ReclaimExperiment):
    def start(self):
        KernelExperiment.start(self)
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.namespaces("
                 "namespace_id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,name VARBINARY(128) UNIQUE,"
                 "source_id BIGINT UNSIGNED,catalog_page BIGINT UNSIGNED,catalog_cap BIGINT,"
                 "directory_page BIGINT UNSIGNED,directory_cap BIGINT,snapshot BIGINT,schema_version BIGINT,"
                 "snapshot_ref BIGINT UNSIGNED DEFAULT 0,state BIGINT DEFAULT 0)")
        self.sql("CREATE TABLE IF NOT EXISTS __fork_proto_meta.snapshots("
                 "snapshot_id BIGINT UNSIGNED PRIMARY KEY,catalog_page BIGINT UNSIGNED,"
                 "directory_page BIGINT UNSIGNED,snapshot BIGINT,schema_version BIGINT,"
                 "catalog_cap BIGINT,directory_cap BIGINT,parent_ref BIGINT UNSIGNED,ref_count BIGINT)")
        self.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")

    def setup_lineage(self, tables=1):
        self.names = []
        for db in ("db1", "db2"):
            self.sql("CREATE DATABASE " + db)
            for i in range(1,tables+1):
                name = db + ".t" + str(i)
                self.names.append(name)
                self.sql("CREATE TABLE " + name + "(id INT PRIMARY KEY,v INT)")
                self.sql("INSERT INTO " + name + " VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE __empty__ TO a")
        self.source_ids = [r[2] for db in ("db1", "db2") for r in self.user_tables(db)]
        self.local_hot = self.user_tables("db1")[0][2]
        self.local_cold = self.user_tables("db2")[0][2]
        self.sql("FORK DATABASE a TO b")
        self.b, self.sb = self.root("b")[0], self.root("b")[6]

    def graph(self):
        snapshots = self.snapshots()
        refs = self.sql("SELECT namespace_id,snapshot_ref FROM __fork_proto_meta.namespaces", log=False)
        expected = Counter(r[1] for r in refs if r[1])
        expected.update(r[7] for r in snapshots if r[7])
        actual = {r[0]:r[8] for r in snapshots}
        assert actual == dict(expected), (snapshots, refs, expected)
        assert all(r[3] == r[0] and 0 < r[5] <= r[0] and 0 < r[6] <= r[0]
                   and (r[7] == 0 or r[7] in actual and r[7] < r[0]) for r in snapshots)
        assert self.pins() == tuple((r[0],r[4],0) for r in snapshots)
        return snapshots

    def capture(self, source, target):
        before, physical = self.page_dump(), self.physical()
        parent = self.root(source)
        self.sql("FORK DATABASE " + source + " TO " + target)
        child = self.root(target)
        assert self.page_dump() == before and self.physical() == physical
        assert child[2] == parent[2] and child[4] == parent[4] and child[1] == 0
        self.record("fork_captured_roots_without_table_work", source=source, target=target,
                    parent_root=parent, child_root=child, pages=len(before), physical=physical, graph=self.graph())
        return child[0],child[6]

    def table(self, ns, name):
        db, table = name.split('.')
        return self.address(ns, db, table)

    def run_lineage(self):
        self.setup_lineage(tables=5)  # Ten entries force distinct B+ tree leaves.
        hot, cold = self.table(self.b,"db1.t1"), self.table(self.b,"db2.t1")
        self.sql("UPDATE " + hot + " SET v=90 WHERE id=1")
        b_hot = (1<<62) | (self.b<<32) | self.local_hot
        self.sql("UPDATE db2.t1 SET v=11 WHERE id=1")
        c, sc = self.capture("b","c")
        snapshot = next(r for r in self.graph() if r[0] == sc)
        entries, height, _ = self.inspect_tree(self.page_dump(), snapshot[2], snapshot[6])
        assert height >= 2
        decoded = {int(k):(struct.unpack('<QQQQ',v),cap) for k,(v,cap) in entries.items()}
        assert decoded[self.local_hot][0][3] == b_hot and decoded[self.local_hot][1] == sc
        assert all(cap == self.sb and value[3] == 0 for k,(value,cap) in decoded.items() if k != self.local_hot)
        self.record("mixed_snapshot_caps_in_shared_tree", snapshot=sc, ancestor=self.sb, height=height, entries=decoded)
        self.sql("UPDATE " + hot + " SET v=100 WHERE id=1")
        self.sql("INSERT INTO " + hot + " VALUES(3,30)")
        self.sql("DELETE FROM " + hot + " WHERE id=2")
        self.sql("UPDATE " + cold + " SET v=77 WHERE id=1")
        b_cold = (1<<62) | (self.b<<32) | self.local_cold
        self.sql("UPDATE db2.t1 SET v=12 WHERE id=1")
        # Deleting an unmaterialized sibling must not delete its inherited B tablets.
        sibling, _ = self.capture("b","sibling")
        self.drop("sibling")
        self.graph()
        self.expect(hot, [(1,100),(3,30)])
        self.expect(cold, [(1,77),(2,20)])
        self.expect(self.table(c,"db1.t1"), [(1,90),(2,20)])
        c_hot = (1<<62) | (c<<32) | self.local_hot
        assert {r[0] for r in self.physical()} == {b_hot,b_cold,c_hot}
        self.drop("a")
        self.drop("b")
        assert all(r[8] == 1 for r in self.graph()) and len(self.snapshots()) == 2
        self.wait_empty([b_cold])
        protected = self.source_ids + [b_hot]
        assert self.source_storage(protected) == tuple((i,3,1,0) for i in sorted(protected))
        self.record("deleted_parent_storage_owned_by_child_snapshot", graph=self.graph(), storage=self.source_storage(protected))
        self.denied("SELECT * FROM " + hot)
        self.restart()
        self.graph()
        self.expect(self.table(c,"db1.t1"), [(1,90),(2,20)])
        self.expect(self.table(c,"db2.t1"), [(1,10),(2,20)])  # Still A@SB, not A@SC or later B.
        c_cold = (1<<62) | (c<<32) | self.local_cold
        self.sql("UPDATE " + self.table(c,"db1.t1") + " SET v=900 WHERE id=1")
        self.sql("DELETE FROM " + self.table(c,"db2.t1") + " WHERE id=2")
        self.sql("INSERT INTO " + self.table(c,"db2.t1") + " VALUES(4,40)")
        self.flush(self.source_ids)
        self.flush([b_hot])
        baselines = [b_hot,c_hot,c_cold]
        self.wait_state("baselines_completed_through_deleted_parent", baselines,
                        lambda h,t: all(any(r[0]==i and r[1]==10 for r in t) for i in baselines))
        self.flush([c_hot,c_cold])
        self.sql("ALTER SYSTEM MAJOR FREEZE")
        self.wait_state("child_major_completed", [c_hot,c_cold],
                        lambda h,t: all(any(r[0]==i and r[1]=='MAJOR_MERGE' and r[2]>sc for r in h)
                                       for i in (c_hot,c_cold)))
        self.restart()
        self.graph()
        self.expect(self.table(c,"db1.t1"), [(1,900),(2,20)])
        self.expect(self.table(c,"db2.t1"), [(1,10),(4,40)])
        for name in self.names:
            if name not in ("db1.t1","db2.t1"):
                self.expect(self.table(c,name), [(1,10),(2,20)])
        targets = [r[0] for r in self.physical()]
        self.drop("c")
        assert self.graph() == self.physical() == ()
        self.wait_empty(self.source_ids+[b_hot,b_cold]+targets)
        self.restart()
        assert self.graph() == self.physical() == ()
        self.wait_empty(self.source_ids+[b_hot,b_cold]+targets)
        self.record("PASS", case="multi_generation_namespace_fork", tree_height=height,
                    source_deleted=True, mixed_caps=True, deleted_parent_baseline=True,
                    child_major=True, final_snapshot_chain_released=True)

    def paused_crash(self, statement, signal, check):
        self.sql("SET ob_global_debug_sync='AFTER_UPDATE_TABLET_TO_LS signal " + signal +
                 "_ready wait_for " + signal + "_release timeout 60000000 execute 1'")
        def issue():
            con = self.connect()
            try:
                return self.sql(statement, con)
            finally:
                con.close()
        with ThreadPoolExecutor(max_workers=1) as pool:
            pending = pool.submit(issue)
            self.sql("SET ob_global_debug_sync='now wait_for " + signal + "_ready timeout 10000000'")
            self.wait_until(lambda: 'name='+signal+'_release' in self.engine_log(), "transaction never paused")
            check()
            self.restart()
            try:
                pending.result(timeout=10)
            except pymysql.MySQLError as error:
                assert error.args[0] in (2006,2013), error.args
            else:
                raise AssertionError("uncommitted operation succeeded")

    def run_crash(self):
        self.setup_lineage()
        self.sql("UPDATE db1.t1 SET v=123 WHERE id=1")
        self.drop("a")  # Fork B again after the native source and its schemas are gone.
        self.record("native_source_deleted_before_descendant_capture", graph=self.graph())
        before, pages = self.graph(), self.page_dump()
        def unpublished():
            assert self.graph() == before and self.physical() == ()
            assert self.sql("SELECT namespace_id FROM __fork_proto_meta.namespaces WHERE name='c'", log=False) == ()
            self.record("child_publication_and_parent_ref_uncommitted", graph=before)
        self.paused_crash("FORK DATABASE b TO c", "v8_capture", unpublished)
        unpublished()
        assert self.page_dump() == pages
        c, sc = self.capture("b","c")
        d, sd = self.capture("c","d")
        # Entirely cold generations preserve the OLD cap at the root itself.
        assert self.root("c")[5] == self.root("d")[5] == self.sb < sc < sd
        self.drop("b")
        self.drop("c")
        assert self.physical() == () and len(self.graph()) == 3
        assert all(r[8] == 1 for r in self.snapshots())
        self.restart()
        self.expect(self.table(d,"db1.t1"), [(1,10),(2,20)])
        targets = [r[0] for r in self.physical()]
        before, root = self.graph(), self.root("d")
        def uncommitted_release():
            assert self.state("d") == 1 and self.root("d") == root
            assert self.graph() == before
            assert all(r[1:3] == (3,0) for r in self.source_storage(targets))
            self.record("cascading_snapshot_release_uncommitted", graph=before)
        self.paused_crash("FORK DATABASE d TO __drop__", "v8_release", uncommitted_release)
        assert self.state("d") == 1 and self.root("d") == root and self.graph() == before
        assert all(r[1:] == (1,1,0) for r in self.source_storage(targets))
        self.drop("d")
        assert self.graph() == self.physical() == ()
        self.wait_empty(self.source_ids+targets)
        self.restart()
        assert self.graph() == self.physical() == ()
        self.wait_empty(self.source_ids+targets)
        self.record("PASS", case="lineage_capture_and_cascade_crash_retry", cold_generations=3,
                    publication_rolled_back=True, cascade_rolled_back=True, chain_reclaimed=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--mode", type=int, choices=(5,6), default=5)
    parser.add_argument("--case", choices=("lineage","crash","all"), default="all")
    args = parser.parse_args()
    for case in (("lineage","crash") if args.case == "all" else (args.case,)):
        exp = LineageExperiment(args.binary, "lineage_v8_"+case, prototype=args.mode)
        try:
            exp.start()
            exp.run_lineage() if case == "lineage" else exp.run_crash()
        except BaseException as error:
            exp.record("FAIL", error=repr(error), base=exp.base)
            raise
        finally:
            exp.close()


if __name__ == "__main__":
    main()
