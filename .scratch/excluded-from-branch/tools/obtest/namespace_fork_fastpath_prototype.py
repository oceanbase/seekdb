#!/usr/bin/env python3
"""PROTOTYPE: can existing tablet status safely replace repeated directory transactions?

python3 tools/obtest/namespace_fork_fastpath_prototype.py --binary build_release/src/observer/seekdb
Uses a disposable real engine, root-row contention, a precommit pause, timeout and crash.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import time

import pymysql
from namespace_fork_kernel_prototype import KernelExperiment


class FastpathExperiment(KernelExperiment):
    def start(self):
        super().start()
        # This short experiment counts traces across recovery. Disk-pressure log
        # recycling can remove old traces even below max_syslog_file_count=16.
        self.sql("ALTER SYSTEM SET max_syslog_file_count=0")

    def slow_paths(self):
        return self.engine_log().count("PROTOTYPE_V4_DIRECTORY_SLOW_PATH")

    def wait_until(self, predicate, description):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.1)
        raise AssertionError(description)

    def request(self, statement, query_timeout=30000000):
        con = self.connect()
        try:
            self.sql("SET ob_query_timeout=" + str(query_timeout), con, log=False)
            return self.sql(statement, con)
        finally:
            con.close()

    def arm(self, event):
        self.sql("SET ob_global_debug_sync='AFTER_UPDATE_TABLET_TO_LS signal " + event +
                 "_ready wait_for " + event + "_release timeout 20000000 execute 1'")

    def paused(self, event, physical_count, root):
        self.sql("SET ob_global_debug_sync='now wait_for " + event + "_ready timeout 10000000'")
        # Debug-sync timeout alone is not a failure; require the actual paused state.
        self.wait_until(lambda: "name=" + event + "_release" in self.engine_log(),
                        "creator did not reach precommit pause")
        assert len(self.physical()) == physical_count
        assert self.root(self.target) == root
        self.record("physical_exists_directory_uncommitted", event_name=event,
                    tablets=self.physical(), committed_root=root)

    def release(self, event):
        self.sql("SET ob_global_debug_sync='now signal " + event + "_release'")

    def hot_with_locked_root(self, pool, phase):
        before = self.slow_paths()
        blocker = self.connect()
        root = self.root(self.target)
        def reads_and_writes():
            con = self.connect()
            try:
                self.sql("SET ob_query_timeout=2000000", con, log=False)
                for _ in range(20):
                    assert self.sql("SELECT id,v FROM " + self.target + ".t1 ORDER BY id",
                                    con, log=False) == ((1, 10), (2, 20))
                    self.sql("BEGIN", con, log=False)
                    self.sql("UPDATE " + self.target + ".t1 SET v=999 WHERE id=1", con, log=False)
                    self.sql("ROLLBACK", con, log=False)
            finally:
                con.close()
        try:
            self.sql("BEGIN", blocker, log=False)
            self.sql("SELECT * FROM __fork_proto_meta.roots WHERE database_id=" +
                     str(root[0]) + " FOR UPDATE", blocker, log=False)
            # Completion while another transaction owns the root proves independence
            # from the directory lock; this is a functional check, not a timing benchmark.
            pool.submit(reads_and_writes).result(timeout=10)
            after = self.slow_paths()
            assert after == before, (phase, before, after)
            self.record("hot_access_completed_with_root_locked", phase=phase,
                        reads=20, rolled_back_writes=20, new_directory_transactions=0)
        finally:
            self.sql("ROLLBACK", blocker, log=False)
            blocker.close()
        assert self.root(self.target) == root

    def run_fastpath(self):
        source, self.target = "__fork_proto_a_fastpath", "__fork_proto_b_fastpath"
        target = self.target
        self.sql("CREATE DATABASE " + source)
        for n in range(1, 6):
            self.sql("CREATE TABLE " + source + ".t" + str(n) + "(id INT PRIMARY KEY,v INT)")
            self.sql("INSERT INTO " + source + ".t" + str(n) + " VALUES(1,10),(2,20)")
        self.sql("FORK DATABASE " + source + " TO " + target)
        assert self.physical() == ()
        self.expect(target + ".t1", [(1, 10), (2, 20)])
        self.wait_until(lambda: self.slow_paths() == 1, "missing first materialization trace")

        with ThreadPoolExecutor(max_workers=3) as pool:
            self.hot_with_locked_root(pool, "before_restart")

            root, before = self.root(target), self.slow_paths()
            self.arm("v4_concurrent")
            first = pool.submit(self.request, "SELECT id,v FROM " + target + ".t2 ORDER BY id")
            try:
                self.paused("v4_concurrent", 2, root)
                reader = pool.submit(self.request, "SELECT id,v FROM " + target + ".t2 ORDER BY id")
                writer = pool.submit(self.request, "UPDATE " + target + ".t2 SET v=v WHERE id=1")
                self.wait_until(lambda: self.slow_paths() >= before + 3,
                                "read/write bypassed pending CREATE instead of joining its root lock")
                assert not first.done() and not reader.done() and not writer.done()
                assert self.root(target) == root
                self.record("pending_create_blocked_reader_and_writer", slow_paths=self.slow_paths())
            finally:
                self.release("v4_concurrent")
            assert first.result(timeout=15) == reader.result(timeout=15) == ((1, 10), (2, 20))
            assert writer.result(timeout=15) == ()
            assert len(self.physical()) == 2

            root, tablets = self.root(target), self.physical()
            self.arm("v4_abort")
            failed = pool.submit(self.request, "INSERT INTO " + target + ".t3 VALUES(3,300)", 2000000)
            try:
                self.paused("v4_abort", 3, root)
                time.sleep(3)  # Expire the real SQL worker deadline before allowing commit.
            finally:
                self.release("v4_abort")
            try:
                failed.result(timeout=15)
            except pymysql.MySQLError as error:
                self.record("materialization_aborted_after_physical_create", error=error.args)
                assert error.args[0] == 4012, error.args
            else:
                raise AssertionError("expired materialization unexpectedly succeeded")
            assert self.root(target) == root
            self.wait_until(lambda: self.physical() == tablets, "aborted tablet was not removed")
            before = self.slow_paths()
            self.expect(target + ".t3", [(1, 10), (2, 20)])
            self.wait_until(lambda: self.slow_paths() > before, "retry incorrectly used aborted tablet state")
            self.record("aborted_binding_not_reused_retry_succeeded")

            root, tablets = self.root(target), self.physical()
            self.arm("v4_crash")
            crashed = pool.submit(self.request, "INSERT INTO " + target + ".t4 VALUES(3,300)")
            self.paused("v4_crash", 4, root)
            self.restart()
            try:
                crashed.result(timeout=15)
            except pymysql.MySQLError as error:
                assert error.args[0] in (2006, 2013), error.args
            else:
                raise AssertionError("paused request survived process kill")
            assert self.root(target) == root
            assert self.physical() == tablets
            # No warmup read of t1: its first access after recovery must also avoid the root.
            self.hot_with_locked_root(pool, "first_access_after_restart")
            self.expect(target + ".t2", [(1, 10), (2, 20)])
            self.expect(target + ".t3", [(1, 10), (2, 20)])
            self.expect(target + ".t4", [(1, 10), (2, 20)])
            self.expect(target + ".t5", [(1, 10), (2, 20)])
            assert len(self.physical()) == 5
            assert self.tasks() == ()
            self.record("PASS", case="namespace_fork_existing_tablet_fastpath",
                        new_cache_entries=0, concurrent_pending_create=True,
                        timeout_abort=True, crash_before_commit=True, restart=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    exp = FastpathExperiment(args.binary, "fastpath_v4", prototype=2)
    try:
        exp.start()
        exp.run_fastpath()
    except BaseException as error:
        exp.record("FAIL", error=repr(error), base=exp.base)
        raise
    finally:
        exp.close()


if __name__ == "__main__":
    main()
