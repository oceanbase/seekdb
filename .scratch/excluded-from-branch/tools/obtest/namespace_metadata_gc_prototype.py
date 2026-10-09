#!/usr/bin/env python3
"""PROTOTYPE V9: safe, manually triggered metadata GC in the real engine.

SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_metadata_gc_prototype.py --binary build_release/src/observer/seekdb
An independent tree walk checks retained bytes, schema blobs and bounded deletion.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import struct
import time

import pymysql
from namespace_lineage_prototype import LineageExperiment

GC = 'FORK DATABASE __gc__ TO __gc__'


class MetadataGCExperiment(LineageExperiment):
    def start(self):
        super().start()
        self.sql('ALTER SYSTEM SET max_syslog_file_count=0')

    def reachable(self, pages):
        roots = self.sql('SELECT catalog_page,directory_page FROM __fork_proto_meta.namespaces', log=False)
        roots += tuple((r[1],r[2]) for r in self.graph())
        reachable = set()
        inspected = set()
        for pair in roots:
            for page in pair:
                if not page or page in inspected:
                    continue
                inspected.add(page)
                entries, _, nodes = self.inspect_tree(pages,page,0)
                reachable.update(map(str,nodes))
                for value,_ in entries.values():
                    object_id = str(struct.unpack('<QQQQ',value)[0])
                    assert object_id in pages, object_id
                    reachable.add(object_id)
        return reachable

    def gc(self):
        before = self.page_dump()
        retained = self.reachable(before)
        physical, graph = self.physical(), self.graph()
        passes = 0
        while True:
            self.sql(GC)
            after = self.page_dump()
            assert all(after.get(k) == v for k,v in before.items() if k in retained)
            assert set(after) <= set(before)
            assert len(before)-len(after) == min(256,len(set(before)-retained))
            passes += 1
            if set(after) == retained:
                break
            before = after
        assert self.physical() == physical and self.graph() == graph
        self.record('metadata_gc_exact_reachability', retained=len(retained), passes=passes)
        return after

    def run_lifecycle(self):
        self.setup_lineage(tables=5)
        original = self.page_dump()
        assert len(self.gc()) < len(original)
        self.sql('UPDATE '+self.table(self.b,'db1.t1')+' SET v=90 WHERE id=1')
        c,_ = self.capture('b','c')
        self.sql('UPDATE '+self.table(self.b,'db1.t1')+' SET v=100 WHERE id=1')
        self.sql('UPDATE db2.t1 SET v=123 WHERE id=1')
        self.drop('a')
        self.drop('b')
        self.gc()
        self.restart()
        self.expect(self.table(c,'db1.t1'),[(1,90),(2,20)])
        self.expect(self.table(c,'db2.t1'),[(1,10),(2,20)])
        for name in self.names:
            self.expect(self.table(c,name),[(1,90 if name=='db1.t1' else 10),(2,20)])
        stable = self.gc()
        for i in range(20):
            name = 'cycle_'+str(i)
            ns,_ = self.capture('c',name)
            self.expect(self.table(ns,'db1.t1'),[(1,90),(2,20)])
            self.expect(self.table(ns,'db2.t1'),[(1,10),(2,20)])
            self.drop(name)
            assert self.gc() == stable
        self.record('repeated_fork_access_drop_stable_pages', cycles=20, pages=len(stable))
        self.restart()
        self.expect(self.table(c,'db1.t1'),[(1,90),(2,20)])
        self.expect(self.table(c,'db2.t1'),[(1,10),(2,20)])
        self.drop('c')
        assert self.gc() == {}
        assert self.graph() == ()
        self.restart()
        assert self.gc() == {}
        self.record('PASS', case='metadata_gc_lifecycle', cycles=20, final_pages=0, restart=True)

    def request(self, sql, timeout=30000000):
        con = self.connect()
        try:
            self.sql('SET ob_query_timeout='+str(timeout), con, log=False)
            return self.sql(sql,con)
        finally:
            con.close()

    def arm(self, point, signal):
        self.sql("SET ob_global_debug_sync='"+point+' signal '+signal+'_ready wait_for '+signal+
                 "_release timeout 60000000 execute 1'")

    def paused(self, signal):
        self.sql("SET ob_global_debug_sync='now wait_for "+signal+"_ready timeout 10000000'")
        self.wait_until(lambda: 'name='+signal+'_release' in self.engine_log(), 'sync never paused')

    def release(self, signal):
        self.sql("SET ob_global_debug_sync='now signal "+signal+"_release'")

    def run_concurrent(self):
        self.setup_lineage()
        # Retire B's old catalog while a metadata reader still holds its root.
        self.sql('CREATE TABLE db1.new_source(id INT PRIMARY KEY,v INT)')
        before = self.page_dump()
        with ThreadPoolExecutor(max_workers=3) as pool:
            self.arm('BEFORE_FETCH_SIMPLE_TABLES','v9_reader')
            reader = pool.submit(self.request,'SHOW CREATE TABLE '+self.table(self.b,'db1.t1'))
            self.paused('v9_reader')
            self.drop('b')
            assert set(before)-self.reachable(before)
            gc = pool.submit(self.request,GC)
            try:
                time.sleep(0.3)
                assert not gc.done() and not reader.done()
                assert self.page_dump() == before
                self.record('gc_waits_for_retired_root_reader')
            finally:
                self.release('v9_reader')
            try:
                rows = reader.result(timeout=15)
                assert rows
            except pymysql.MySQLError as error:
                # A later schema lookup may correctly reject the deleted namespace.
                assert error.args[0] in (1049,1146,4179), error.args
                self.record('retired_reader_namespace_closed',error=error.args)
            gc.result(timeout=15)
            self.gc()

            # observe_schema has returned, but native CREATE still owns the root row.
            self.arm('BEFORE_CREATE_TABLE_TRANS_COMMIT','v9_native')
            ddl = pool.submit(self.request,'CREATE TABLE db1.late(id INT PRIMARY KEY,v INT)')
            self.paused('v9_native')
            before = self.page_dump()
            try:
                try:
                    self.request(GC,timeout=2000000)
                except pymysql.MySQLError as error:
                    assert error.args[0] in (1205,4012), error.args
                    self.record('gc_rejected_uncommitted_native_root',error=error.args)
                else:
                    raise AssertionError('GC crossed an uncommitted native root')
                assert self.page_dump() == before
            finally:
                self.release('v9_native')
            ddl.result(timeout=15)
            self.gc()
            self.sql('INSERT INTO db1.late VALUES(1,10),(2,20)')
            fresh,_ = self.capture('a','fresh')
            self.expect(self.table(fresh,'db1.late'),[(1,10),(2,20)])

            # Keep one garbage path so the next GC really performs DELETE.
            self.sql('CREATE TABLE db1.more(id INT PRIMARY KEY,v INT)')
            before = self.page_dump()
            assert set(before)-self.reachable(before)
            self.arm('AFTER_UPDATE_TABLET_TO_LS','v9_gc')
            gc = pool.submit(self.request,GC)
            self.paused('v9_gc')
            fork = pool.submit(self.request,'FORK DATABASE fresh TO concurrent')
            cold = pool.submit(self.request,'SELECT id,v FROM '+self.table(fresh,'db2.t1')+' ORDER BY id')
            try:
                time.sleep(0.3)
                assert not fork.done() and not cold.done()
                assert self.page_dump() == before
                self.record('gc_excludes_new_capture_and_cold_lookup_until_commit')
            finally:
                self.release('v9_gc')
            gc.result(timeout=15)
            fork.result(timeout=15)
            assert cold.result(timeout=15) == ((1,10),(2,20))
            self.gc()
            self.expect(self.table(self.root('concurrent')[0],'db2.t1'),[(1,10),(2,20)])
        self.restart()
        self.expect(self.table(fresh,'db1.late'),[(1,10),(2,20)])
        self.record('PASS',case='metadata_gc_concurrent',retired_reader=True,native_commit_gap=True,new_capture=True)

    def run_crash(self):
        self.setup_lineage(tables=25)
        before = self.page_dump()
        assert len(set(before)-self.reachable(before)) > 256
        graph = self.graph()
        def uncommitted():
            assert self.page_dump() == before and self.graph() == graph
            self.record('metadata_delete_uncommitted')
        self.paused_crash(GC,'v9_crash',uncommitted)
        assert self.page_dump() == before and self.graph() == graph
        self.gc()
        self.restart()
        self.expect(self.table(self.b,'db1.t1'),[(1,10),(2,20)])
        self.record('PASS',case='metadata_gc_precommit_crash',rollback=True,retry=True,restart=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True)
    parser.add_argument('--case',choices=('lifecycle','concurrent','crash','all'),default='all')
    args = parser.parse_args()
    for case in (('lifecycle','concurrent','crash') if args.case=='all' else (args.case,)):
        exp = MetadataGCExperiment(args.binary,'metadata_gc_v9_'+case,prototype=6)
        try:
            exp.start()
            getattr(exp,'run_'+case)()
        except BaseException as error:
            exp.record('FAIL',error=repr(error),base=exp.base)
            raise
        finally:
            exp.close()


if __name__ == '__main__':
    main()
