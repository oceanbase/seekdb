#!/usr/bin/env python3
"""PROTOTYPE: do inherited reads survive real flush, minor/major merge and crash?

python3 tools/obtest/namespace_fork_compaction_prototype.py --binary build_release/src/observer/seekdb
Keeps the V2 source-retention pin. Checks real SSTables and completed merge history,
not merely successful ALTER SYSTEM commands. No fork DDL task or DEBUG_SYNC hold.
"""
import argparse
import time

from namespace_fork_kernel_prototype import KernelExperiment


class CompactionExperiment(KernelExperiment):
    def start(self):
        super().start()
        # Existing runtime setting, shortened only in the disposable test instance.
        self.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")

    def history(self, ids):
        return self.sql("SELECT tablet_id,type,compaction_scn,finish_time,"
                        "participant_table FROM oceanbase.__all_virtual_tablet_compaction_history "
                        "WHERE tablet_id IN (" + ",".join(map(str, ids)) + ") "
                        "ORDER BY tablet_id,finish_time", log=False)

    def tables(self, ids):
        return self.sql("SELECT tablet_id,table_type,start_log_scn,end_log_scn,"
                        "upper_trans_version,is_active FROM oceanbase.__all_virtual_table_mgr "
                        "WHERE tablet_id IN (" + ",".join(map(str, ids)) + ") "
                        "ORDER BY tablet_id,table_type,start_log_scn", log=False)

    def wait_state(self, name, ids, condition):
        deadline, report = time.monotonic() + 180, 0
        while time.monotonic() < deadline:
            history, tables = self.history(ids), self.tables(ids)
            if condition(history, tables):
                self.record(name, history=history, tables=tables)
                return
            if time.monotonic() >= report:
                self.record("waiting_" + name, history=history, tables=tables)
                report = time.monotonic() + 15
            time.sleep(1)
        raise AssertionError((name, history, tables))

    def flush(self, ids):
        previous = {tablet: sum(r[0] == tablet and r[1] == "MINI_MERGE"
                                for r in self.history(ids)) for tablet in ids}
        for tablet in ids:
            self.sql("ALTER SYSTEM MINOR FREEZE TABLET_ID=" + str(tablet))
        def complete(history, tables):
            return all(sum(r[0] == tablet and r[1] == "MINI_MERGE" for r in history)
                       > previous[tablet] for tablet in ids) and not any(
                           r[1] == 0 and r[5] == "NO" for r in tables)
        self.wait_state("real_mini_merge_completed", ids, complete)

    def run_compaction(self):
        source, target = "__fork_proto_a_compaction", "__fork_proto_b_compaction"
        self.sql("CREATE DATABASE " + source)
        for table in ("t1", "t2"):
            self.sql("CREATE TABLE " + source + "." + table + "(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO " + source + "." + table + " VALUES(1,10),(2,20),(3,30)")
        source_ids = [r[2] for r in self.user_tables(source)]
        pages = self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages")[0][0]
        self.sql("FORK DATABASE " + source + " TO " + target)
        captured = self.root(target)
        assert self.physical() == ()
        assert self.sql("SELECT COUNT(*) FROM __fork_proto_meta.pages")[0][0] == pages

        self.sql("UPDATE " + target + ".t1 SET v=100 WHERE id=1")
        self.sql("DELETE FROM " + target + ".t1 WHERE id=2")
        self.sql("INSERT INTO " + target + ".t1 VALUES(4,400)")
        target_id = self.physical()[0][0]
        ids = source_ids + [target_id]
        # Give the background scheduler a turn while the source is still in memory.
        # Baseline preparation must not force a source flush on behalf of the fork.
        time.sleep(6)
        before_flush = self.tables(ids)
        assert not any(r[0] in source_ids and r[1] in (11, 12) for r in before_flush), before_flush
        assert not any(r[0] == target_id and r[1] == 10 for r in before_flush), before_flush
        self.record("baseline_waited_for_normal_source_flush", tables=before_flush)
        for table in ("t1", "t2"):
            self.sql("UPDATE " + source + "." + table + " SET v=11 WHERE id=1")
            self.sql("DELETE FROM " + source + "." + table + " WHERE id=3")
            self.sql("INSERT INTO " + source + "." + table + " VALUES(5,50)")

        self.flush(ids)
        self.expect(target + ".t1", [(1,100), (3,30), (4,400)])
        assert len(self.physical()) == 1  # t2 remains unmaterialized through all merges.
        self.record("opened_branch_survived_flush_unopened_still_absent")

        # Produce multiple actual disk runs so a minor merge has inputs to combine.
        for value in (12, 13, 14):
            for table in ("t1", "t2"):
                self.sql("UPDATE " + source + "." + table + " SET v=" + str(value) + " WHERE id=1")
            self.sql("UPDATE " + target + ".t1 SET v=" + str(400 + value) + " WHERE id=4")
            self.flush(ids)
        self.wait_state("real_minor_merge_completed", ids,
                        lambda history, tables: all(any(r[0] == tablet and r[1] == "MINOR_MERGE"
                                                       for r in history) for tablet in ids))
        self.expect(target + ".t1", [(1,100), (3,30), (4,414)])
        assert len(self.physical()) == 1

        self.sql("ALTER SYSTEM MAJOR FREEZE")
        self.wait_state("real_major_merge_completed", ids,
                        lambda history, tables: all(any(r[0] == tablet and r[1] == "MAJOR_MERGE"
                                                       and r[2] > captured[6] for r in history)
                                                    for tablet in ids))
        self.expect(target + ".t1", [(1,100), (3,30), (4,414)])
        assert len(self.physical()) == 1
        self.expect(target + ".t2", [(1,10), (2,20), (3,30)])
        assert len(self.physical()) == 2
        for table in ("t1", "t2"):
            self.expect(source + "." + table, [(1,14), (2,20), (5,50)])

        before = self.root(target)
        self.record("before_crash", root=before, tables=self.tables(ids + [self.physical()[-1][0]]))
        self.restart()
        assert self.root(target) == before
        self.expect(target + ".t1", [(1,100), (3,30), (4,414)])
        self.expect(target + ".t2", [(1,10), (2,20), (3,30)])
        target_ids = [row[0] for row in self.physical()]
        self.wait_state("recovered_baselines_present", target_ids,
                        lambda history, tables: all(any(r[0] == tablet and r[1] == 10
                                                       for r in tables) for tablet in target_ids))
        self.sql("BEGIN")
        self.sql("UPDATE " + target + ".t2 SET v=999 WHERE id=1")
        self.sql("ROLLBACK")
        self.expect(target + ".t2", [(1,10), (2,20), (3,30)])
        assert self.tasks() == ()
        trace = self.engine_log()
        assert "fork table freeze stage done" not in trace
        assert not any("freeze_tablet (ob_tablet_fork_task.cpp:" in line for line in trace.splitlines())
        self.record("PASS", case="namespace_fork_compaction", snapshot=captured[6],
                    source_retained=True, global_pin_retained=True, restart=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    exp = CompactionExperiment(args.binary, "compaction_v3", prototype=2)
    try:
        exp.start()
        exp.run_compaction()
    except BaseException as error:
        exp.record("FAIL", error=repr(error), base=exp.base)
        raise
    finally:
        exp.close()


if __name__ == "__main__":
    main()
